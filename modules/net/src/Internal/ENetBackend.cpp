// SPDX-License-Identifier: MIT
// Copyright (c) Robert Vokac and contributors
#include "CNA/Internal/Net/ENetBackend.hpp"
#include "CNA/Internal/Net/ENetDiscoveryService.hpp"
#include "CNA/Internal/Net/ENetHostHandle.hpp"
#include "CNA/Internal/Net/NetPacketCodec.hpp"
#include "TrafficRate.hpp"
#include "CNA/Logger.hpp"
#include "Microsoft/Xna/Framework/GamerServices/SignedInGamer.hpp"
#include "Microsoft/Xna/Framework/Net/AvailableNetworkSession.hpp"
#include "Microsoft/Xna/Framework/Net/LocalNetworkGamer.hpp"
#include "Microsoft/Xna/Framework/Net/NetworkGamer.hpp"
#include "Microsoft/Xna/Framework/Net/NetworkMachine.hpp"
#include "Microsoft/Xna/Framework/Net/NetworkSession.hpp"

#include <algorithm>
#include <chrono>
#include <enet/enet.h>
#include <map>
#include <memory>
#include <optional>
#include <random>
#include <stdexcept>
#include <unordered_map>
#include <vector>

namespace CNA::Internal::Net
{
    using Microsoft::Xna::Framework::GamerServices::SignedInGamer;
    using Microsoft::Xna::Framework::Net::LocalNetworkGamer;
    using Microsoft::Xna::Framework::Net::NetworkGamer;
    using Microsoft::Xna::Framework::Net::NetworkSession;
    using Microsoft::Xna::Framework::Net::NetworkSessionEndReason;
    using Microsoft::Xna::Framework::Net::NetworkSessionProperties;

    namespace
    {
        constexpr size_t kMaxPeers = static_cast<size_t>(NetworkSession::MaxSupportedGamers);
        constexpr size_t kChannelLimit = 2;
        constexpr uint8_t kControlChannel = 0;

        // audit_net.md remediation (2026-07-18): bound on SessionState::PendingPreHandshakeAppData
        // below. The real handshake this queue bridges (ConnectToHost() -> ClientHello ->
        // ServerWelcome) normally completes within a single Update() call, so a healthy caller
        // queues at most a handful of sends here, briefly. 64 is generous headroom for that real
        // case while still bounding worst-case memory if a caller spins SendData in a loop before
        // ever calling Update() (confirmed reachable - see SendAppData's own comment).
        constexpr size_t kMaxPendingPreHandshakeAppData = 64;

#ifdef __EMSCRIPTEN__
        // Emscripten's SOCKFS bind()/getsockname() shim never reports back a real OS-assigned
        // ephemeral port (see NEXT.md) - hosting on Web must request a fixed, known port instead
        // of relying on ENET_PORT_ANY (0) + read-back. Only reachable in practice by a Node.js-run
        // dedicated relay/server build: a real browser tab can never accept incoming connections
        // at all (browsers cannot open listening sockets), so hosting is moot there regardless.
        constexpr uint16_t kEmscriptenHostPort = 61191;
#endif

        // Task 6.1-6.3 (plans/plan_net.md Phase 6): injectable time source + seeded RNG backing
        // SimulatedLatency/SimulatedPacketLoss below - both default to real behavior in
        // production (real steady_clock, a genuinely-seeded RNG) and are only ever overridden from
        // test code via ENetBackend's own ForTesting statics, so tests never depend on a real
        // sleep or unseeded randomness (a hard determinism requirement - see plans/plan_net.md's Task
        // 6.3's own note).
        std::optional<std::chrono::steady_clock::time_point>& ClockOverrideForTesting()
        {
            static std::optional<std::chrono::steady_clock::time_point> override_;
            return override_;
        }

        std::chrono::steady_clock::time_point Now()
        {
            return ClockOverrideForTesting().has_value() ? *ClockOverrideForTesting() : std::chrono::steady_clock::now();
        }

        std::mt19937& PacketLossRng()
        {
            static std::mt19937 rng(std::random_device{}());
            return rng;
        }

        // Task 6.2: SimulatedPacketLoss is documented as a [0,1] drop probability. 0 and 1 are
        // handled without ever touching the RNG, so a test using exactly those two values (Task
        // 6.4's own requirement) is deterministic regardless of RNG state, real or seeded.
        bool ShouldDropForSimulatedLoss(float lossProbability)
        {
            if (lossProbability <= 0.0f)
            {
                return false;
            }
            if (lossProbability >= 1.0f)
            {
                return true;
            }
            std::uniform_real_distribution<float> dist(0.0f, 1.0f);
            return dist(PacketLossRng()) < lossProbability;
        }

        // Task 6.1/6.2: a real AppData packet, bound for one of this session's own local gamers,
        // held until ReleaseTime instead of being delivered the instant it's received - see
        // HandleAppData's own comment for why this is scoped to local delivery only (not
        // session-management traffic, and not host-relay traffic passing through to someone else).
        struct PendingDelayedDelivery
        {
            NetworkGamer* Target{nullptr};
            NetworkGamer* Sender{nullptr};
            std::vector<SharpRuntime::bytecs> Payload;
            SendDataOptions Options{SendDataOptions::None};
            std::chrono::steady_clock::time_point ReleaseTime;
        };

        // audit_net.md remediation (2026-07-18): a real SendAppData call made before the
        // ClientHello/ServerWelcome handshake has populated SessionState::GamerToWireId for
        // sender and/or target - see SendAppData's own comment for the confirmed-reachable
        // scenario. Held here (bounded by kMaxPendingPreHandshakeAppData, oldest evicted first)
        // instead of silently dropped, and flushed by FlushPendingPreHandshakeAppData once both
        // ends resolve. Sender/Target are raw NetworkGamer* (not wire-ids, which don't exist yet
        // for an unresolved entry) - purged by gamer pointer wherever GamerToWireId loses an
        // entry (HandleDisconnect, host-migration reset), so a removed gamer's pointer is never
        // dereferenced after the fact.
        struct PendingPreHandshakeAppData
        {
            NetworkGamer* Sender{nullptr};
            NetworkGamer* Target{nullptr};
            std::vector<SharpRuntime::bytecs> Payload;
            SendDataOptions Options{SendDataOptions::None};
        };

        // Per-session ENet transport state. HostPeer is set only when this session itself
        // initiated an outbound ConnectToHost() — it identifies "the one peer we asked to
        // connect to", distinguishing "we are the client of this specific connection" (in
        // HandleConnect) from "someone connected to us" (nothing to do until their ClientHello).
        // Wire-ids are assigned by whichever side is playing host for a given connection: lazily
        // for local gamers (EnsureLocalWireIds, first time a roster snapshot is needed) and via
        // the host-assigned ServerWelcome/GamerJoinBroadcast values for gamers learned from wire.
        struct SessionState
        {
            ENetHostHandle Host;
            uint8_t NextWireId{0};
            ENetPeer* HostPeer{nullptr};
            std::unordered_map<NetworkGamer*, uint8_t> GamerToWireId;
            std::unordered_map<uint8_t, NetworkGamer*> WireIdToGamer;
            std::unordered_map<ENetPeer*, std::vector<uint8_t>> PeerWireIds;
            // Host-only: which peer owns a given remote wire-id, for AppData relay (Task 5.5).
            // Never populated for the host's own local gamers (they need no peer to reach).
            std::unordered_map<uint8_t, ENetPeer*> WireIdToPeer;
            // Task 2.11: ids reclaimed from disconnected peers (see HandleDisconnect), reused by
            // AssignWireId before ever incrementing NextWireId. Without this, NextWireId (a
            // uint8_t) wraps after 256 *cumulative* joins over the session's life (churn, not 256
            // simultaneous gamers), silently reassigning an id already owned by a still-connected
            // gamer and corrupting HandleAppData's wire-id-based routing.
            std::vector<uint8_t> FreeWireIds;
            // Task 3.1/10.2: every remote NetworkGamer this SessionState's own HandleClientHello/
            // HandleServerWelcome/HandleGamerJoinBroadcast ever `new`s (NetworkSession::
            // AddRemoteGamer deliberately never takes ownership - see its own doc comment - since
            // its established contract also accepts non-heap gamers, e.g. in tests). This is this
            // SessionState's own ownership registry per GamerCollection<T>'s canonical contract
            // (see its doc comment); freed automatically when this SessionState is destroyed
            // (TeardownSession erasing it from Sessions()), which already happens at the same time
            // NetworkSession::Dispose() frees everything *it* owns.
            std::vector<std::unique_ptr<NetworkGamer>> OwnedRemoteGamers;
            // Task 5.3 (plans/plan_net.md Phase 5): set by AttemptHostMigration right before issuing the
            // reconnect Connect() call, so the ServerWelcome that completes it (a genuine
            // migration, not a plain fresh join) knows to raise NetworkEventType::HostChange once
            // a real NetworkGamer* for the new host exists - see HandleServerWelcome.
            bool AwaitingMigrationHostChangeEXT{false};
            // Task 5.5: the gamertag AttemptHostMigration most recently decided the new host must
            // be, whenever this peer wasn't that new host itself - set purely so the tie-break
            // math (excluding the dead host, picking the true minimum remaining wire id) is
            // testable in a single process, where a second real NetworkSession to actually
            // reconnect to can't exist (see ENetBackendTests.cpp's own SystemLinkSessionFixture
            // comment on why). Not part of real XNA.
            std::string LastMigrationReconnectAttemptGamertagForTestingEXT;
            // Task 6.2 (plans/plan_net.md Phase 6): AppData bound for one of this session's own local
            // gamers, held here until its ReleaseTime by ReleaseDuePendingDeliveries - see
            // PendingDelayedDelivery's own comment.
            std::vector<PendingDelayedDelivery> PendingDeliveries;
            // audit_net.md remediation (2026-07-18): see PendingPreHandshakeAppData's own comment.
            std::vector<PendingPreHandshakeAppData> PendingPreHandshakeSends;
            // Snapshot last published by this transport host. NetworkSession exposes the XNA
            // get-only property as a mutable collection reference, so polling during Update() is
            // the only way to observe arbitrary indexer mutations without wrapping that public
            // collection in a CNA-specific proxy.
            NetworkSessionProperties LastPublishedSessionProperties;
            // SystemLink host: the settings and machine grouping every client was last told.
            std::optional<SessionSettingsMessage> LastPublishedSettings;
            std::vector<MachineRosterEntry> LastPublishedMachines;
            // Host: each client peer's machine number (the host's own machine is 0).
            std::unordered_map<ENetPeer*, uint8_t> PeerMachineIds;
            uint8_t NextMachineId{1};
            // Client: local gamers added after the welcome, waiting for the host to number them.
            std::vector<LocalNetworkGamer*> PendingLocalAdds;
            bool Welcomed{false};
            // BytesPerSecondSent/Received, from this host's wire totals.
            TrafficRate Traffic;
            // Host: when the round trips to client gamers are next published.
            std::chrono::steady_clock::time_point NextStatsBroadcast{};
            // Client: the host's round trips to the gamers on client machines, by wire id.
            std::map<uint8_t, uint16_t> HostRoundtrips;
            // Client: the host-numbered machines of remote gamers.
            std::map<uint8_t, std::shared_ptr<Microsoft::Xna::Framework::Net::NetworkMachine>> RemoteMachines;
        };

        // Task 2.13: process-wide, since SendAppData's silent-drop path (sender/target not yet in
        // any per-session SessionState::GamerToWireId map) isn't tied to one particular session.
        std::size_t droppedAppDataCount_ = 0;

        std::unordered_map<NetworkSession*, std::unique_ptr<SessionState>>& Sessions()
        {
            static std::unordered_map<NetworkSession*, std::unique_ptr<SessionState>> sessions;
            return sessions;
        }

        uint8_t AssignWireId(SessionState& state, NetworkGamer* gamer)
        {
            uint8_t id;
            if (!state.FreeWireIds.empty())
            {
                id = state.FreeWireIds.back();
                state.FreeWireIds.pop_back();
            }
            else
            {
                id = state.NextWireId++;
            }
            state.GamerToWireId[gamer] = id;
            state.WireIdToGamer[id] = gamer;
            // Surface the real, cross-machine-consistent wire-id through the public
            // NetworkGamer::Id property (see DEFERRED.md item #20 in the sibling cna-samples
            // repo) - overwrites NetworkSession's own construction-time local placeholder id.
            gamer->SetId(id);
            return id;
        }

        // Assigns a wire-id to any of session's local gamers that don't have one yet. Called
        // lazily, only once this session actually needs to describe its own roster to a peer
        // (i.e. it is playing host for that connection) — a pure "client" session that only ever
        // calls ConnectToHost never assigns its own locals independently; it waits for the
        // host-assigned ids in ServerWelcome instead, avoiding two independently-numbered wire-id
        // spaces colliding.
        void EnsureLocalWireIds(NetworkSession* session, SessionState& state)
        {
            for (LocalNetworkGamer* gamer : session->getLocalGamersProperty())
            {
                if (!state.GamerToWireId.contains(gamer))
                {
                    AssignWireId(state, gamer);
                }
            }
            ENetBackend::OrderTransportGamers(session);
        }

        // Roster identity comes from the local signed-in account or the received remote identity.
        std::string WireGamertagFor(NetworkGamer* gamer)
        {
            if (auto* local = dynamic_cast<LocalNetworkGamer*>(gamer))
            {
                return local->getSignedInGamerProperty()->getGamertagProperty();
            }
            return gamer->getGamertagProperty();
        }

        std::vector<RosterEntry> SnapshotRoster(NetworkSession* session, SessionState& state)
        {
            std::vector<RosterEntry> roster;
            for (NetworkGamer* gamer : session->getAllGamersProperty())
            {
                // Task 4.6: this runs on the host, whose own view of IsHost is already accurate
                // for every gamer it knows about (its own local gamer is IsHost==true from
                // construction; every remote gamer was set IsHost==false in HandleClientHello
                // below) - forwarding it lets a newly-joining client's HandleServerWelcome
                // correctly identify which roster entry is the host.
                roster.push_back(RosterEntry{state.GamerToWireId.at(gamer), WireGamertagFor(gamer),
                                              gamer->getIsHostProperty()});
            }
            return roster;
        }

        // Task 6.8: queues the packet on peer without flushing - used by per-peer broadcast
        // fan-out loops, which queue every peer's copy first and flush exactly once after the
        // loop (see SendTo just below for the single-recipient case, which still flushes
        // immediately every time).
        void QueueSend(
            SessionState& state,
            ENetPeer* peer,
            const std::vector<SharpRuntime::bytecs>& bytes,
            SendDataOptions options,
            uint8_t channel = kControlChannel
        )
        {
            state.Host.Send(peer, channel, bytes.data(), bytes.size(), NetPacketCodec::SendDataOptionsToEnetFlags(options));
        }

        void SendTo(
            SessionState& state,
            ENetPeer* peer,
            const std::vector<SharpRuntime::bytecs>& bytes,
            SendDataOptions options,
            uint8_t channel = kControlChannel
        )
        {
            QueueSend(state, peer, bytes, options, channel);
            state.Host.Flush();
        }

        bool SessionPropertiesEqual(
            const NetworkSessionProperties& left,
            const NetworkSessionProperties& right
        )
        {
            if (left.getCountProperty() != right.getCountProperty())
            {
                return false;
            }
            for (int i = 0; i < left.getCountProperty(); ++i)
            {
                if (left.getItem(i) != right.getItem(i))
                {
                    return false;
                }
            }
            return true;
        }

        SessionSettingsMessage CurrentSettings(NetworkSession* session)
        {
            SessionSettingsMessage message;
            message.MaxGamers = static_cast<uint8_t>(session->getMaxGamersProperty());
            message.PrivateGamerSlots = static_cast<uint8_t>(session->getPrivateGamerSlotsProperty());
            message.AllowJoinInProgress = session->getAllowJoinInProgressProperty();
            message.AllowHostMigration = session->getAllowHostMigrationProperty();
            return message;
        }

        std::vector<MachineRosterEntry> CurrentMachines(NetworkSession* session, SessionState& state)
        {
            std::vector<MachineRosterEntry> entries;
            for (LocalNetworkGamer* local : session->getLocalGamersProperty())
            {
                const auto wireId = state.GamerToWireId.find(local);
                if (wireId != state.GamerToWireId.end()) entries.push_back({wireId->second, 0});
            }
            for (const auto& [peer, wireIds] : state.PeerWireIds)
            {
                const auto machine = state.PeerMachineIds.find(peer);
                if (machine == state.PeerMachineIds.end()) continue;
                for (uint8_t wireId : wireIds) entries.push_back({wireId, machine->second});
            }
            std::sort(entries.begin(), entries.end(), [](const auto& a, const auto& b) { return a.WireId < b.WireId; });
            return entries;
        }

        // SystemLink: every machine reports the host's settings and machine grouping (the reference
        // kernel shares both); the host republishes them whenever they change.
        void PublishSettingsAndMachinesIfChanged(NetworkSession* session, SessionState& state)
        {
            if (state.HostPeer != nullptr || state.PeerWireIds.empty()) return;
            const auto settings = CurrentSettings(session);
            const auto& last = state.LastPublishedSettings;
            if (!last || last->MaxGamers != settings.MaxGamers || last->PrivateGamerSlots != settings.PrivateGamerSlots ||
                last->AllowJoinInProgress != settings.AllowJoinInProgress || last->AllowHostMigration != settings.AllowHostMigration)
            {
                const auto bytes = NetPacketCodec::Encode(settings);
                for (auto& [peer, wireIds] : state.PeerWireIds) QueueSend(state, peer, bytes, SendDataOptions::Reliable);
                state.LastPublishedSettings = settings;
            }
            auto machines = CurrentMachines(session, state);
            const bool same = machines.size() == state.LastPublishedMachines.size() &&
                std::equal(machines.begin(), machines.end(), state.LastPublishedMachines.begin(),
                    [](const auto& a, const auto& b) { return a.WireId == b.WireId && a.MachineId == b.MachineId; });
            if (!same)
            {
                const auto bytes = NetPacketCodec::Encode(MachineRosterMessage{machines});
                for (auto& [peer, wireIds] : state.PeerWireIds) QueueSend(state, peer, bytes, SendDataOptions::Reliable);
                state.LastPublishedMachines = std::move(machines);
            }
            state.Host.Flush();
        }

        // Host: once a second, the round trip to each gamer on a client machine. A client measures its
        // own round trip to this host; one to a gamer on another client, relayed through this host,
        // also needs this host's.
        void PublishNetworkStats(SessionState& state, std::chrono::steady_clock::time_point now)
        {
            if (state.HostPeer != nullptr || state.PeerWireIds.empty() || now < state.NextStatsBroadcast) return;
            state.NextStatsBroadcast = now + std::chrono::seconds(1);
            NetworkStatsMessage message;
            for (const auto& [wireId, peer] : state.WireIdToPeer)
                message.Entries.push_back(RoundtripEntry{wireId, static_cast<uint16_t>(std::min<enet_uint32>(peer->roundTripTime, 65535))});
            if (message.Entries.empty()) return;
            const auto bytes = NetPacketCodec::Encode(message);
            for (auto& [peer, wireIds] : state.PeerWireIds) QueueSend(state, peer, bytes, SendDataOptions::None);
        }

        // Client: a gamer on the host's machine is one round trip to the host away; one on another
        // client adds the host's round trip to it.
        void ApplyClientRoundtrips(SessionState& state)
        {
            if (state.HostPeer == nullptr || state.HostPeer->state != ENET_PEER_STATE_CONNECTED) return;
            for (const auto& [wireId, gamer] : state.WireIdToGamer)
            {
                if (gamer->getIsLocalProperty()) continue;
                const auto relayed = state.HostRoundtrips.find(wireId);
                const double milliseconds = static_cast<double>(state.HostPeer->roundTripTime) +
                    (relayed != state.HostRoundtrips.end() ? relayed->second : 0);
                gamer->SetRoundtripTime(System::TimeSpan::FromMilliseconds(milliseconds));
            }
        }

        void HandleSessionSettings(NetworkSession* session, const SessionSettingsMessage& message)
        {
            ENetBackend::ApplyTransportSettings(session, message.MaxGamers, message.PrivateGamerSlots,
                message.AllowJoinInProgress, message.AllowHostMigration);
        }

        void HandleMachineRoster(SessionState& state, const MachineRosterMessage& message)
        {
            for (const auto& entry : message.Entries)
            {
                const auto found = state.WireIdToGamer.find(entry.WireId);
                if (found == state.WireIdToGamer.end() || found->second->getIsLocalProperty()) continue;
                NetworkGamer* gamer = found->second;
                auto& machine = state.RemoteMachines[entry.MachineId];
                if (!machine)
                    machine = std::make_shared<Microsoft::Xna::Framework::Net::NetworkMachine>(
                        Microsoft::Xna::Framework::Net::NetworkMachine::CreateInternal());
                if (gamer->GetSharedMachine() == machine) continue;
                gamer->GetSharedMachine()->RemoveGamerInternal(gamer);
                gamer->SetSharedMachine(machine);
                machine->AddGamerInternal(gamer);
            }
        }

        void PublishSessionPropertiesIfChanged(NetworkSession* session, SessionState& state)
        {
            if (state.HostPeer != nullptr)
            {
                return; // only the transport host owns and publishes session properties
            }

            const NetworkSessionProperties& current = session->getSessionPropertiesProperty();
            if (SessionPropertiesEqual(current, state.LastPublishedSessionProperties))
            {
                return;
            }

            SessionPropertiesBroadcastMessage message;
            message.SessionProperties = current;
            const auto bytes = NetPacketCodec::Encode(message);
            for (auto& [peer, wireIds] : state.PeerWireIds)
            {
                QueueSend(state, peer, bytes, SendDataOptions::Reliable);
            }
            state.Host.Flush();
            state.LastPublishedSessionProperties = current;
        }

        // audit_net.md remediation (2026-07-18): the actual wire-send + relay logic, shared by
        // SendAppData's immediate-resolve path and FlushPendingPreHandshakeAppData's later-resolve
        // path below - identical routing either way, only the timing of wire-id resolution
        // differs.
        void DeliverAppData(
            SessionState& state,
            uint8_t senderWireId,
            uint8_t targetWireId,
            const std::vector<SharpRuntime::bytecs>& payload,
            SendDataOptions options
        )
        {
            AppDataMessage msg;
            msg.SenderWireId = senderWireId;
            msg.TargetWireId = targetWireId;
            msg.Options = options;
            msg.Payload = payload;
            auto bytes = NetPacketCodec::Encode(msg);

            if (state.HostPeer != nullptr)
            {
                // We're a client: everything goes to the host, which relays as needed.
                SendTo(state, state.HostPeer, bytes, options);
                return;
            }

            // We're the host: relay directly to the peer that owns the target wire-id.
            auto peerIt = state.WireIdToPeer.find(targetWireId);
            if (peerIt != state.WireIdToPeer.end())
            {
                SendTo(state, peerIt->second, bytes, options);
            }
        }

        // audit_net.md remediation (2026-07-18): called whenever GamerToWireId gains new entries
        // (end of HandleClientHello/HandleServerWelcome/HandleGamerJoinBroadcast) - delivers, in
        // original send order, every queued PendingPreHandshakeAppData entry whose sender AND
        // target have now resolved to a wire-id, and leaves everything else queued for a later
        // call (e.g. a target that's still a whole separate handshake away).
        void FlushPendingPreHandshakeAppData(SessionState& state)
        {
            if (state.PendingPreHandshakeSends.empty())
            {
                return;
            }
            std::vector<PendingPreHandshakeAppData> stillPending;
            stillPending.reserve(state.PendingPreHandshakeSends.size());
            for (auto& pending : state.PendingPreHandshakeSends)
            {
                auto senderIt = state.GamerToWireId.find(pending.Sender);
                auto targetIt = state.GamerToWireId.find(pending.Target);
                if (senderIt == state.GamerToWireId.end() || targetIt == state.GamerToWireId.end())
                {
                    stillPending.push_back(std::move(pending));
                    continue;
                }
                DeliverAppData(state, senderIt->second, targetIt->second, pending.Payload, pending.Options);
            }
            state.PendingPreHandshakeSends = std::move(stillPending);
        }

        // audit_net.md remediation (2026-07-18, third round): drops any PendingPreHandshakeSends
        // entries that reference gamer, called wherever GamerToWireId loses an entry for a gamer
        // that will never resolve now (HandleDisconnect, HandleGamerLeaveBroadcast, host-migration
        // reset) - without this, a queued send naming a gamer that left before the handshake ever
        // completed would sit in the queue until evicted by kMaxPendingPreHandshakeAppData churn,
        // or reference a NetworkGamer* that becomes dangling once its owning
        // SessionState::OwnedRemoteGamers entry is freed. Each entry removed here genuinely can
        // never be delivered now (its sender or target is gone), so it counts via
        // droppedAppDataCount_ same as a queue-overflow eviction - GetDroppedAppDataCount()'s own
        // contract is "could not eventually be delivered" for any reason, not just overflow.
        void PurgePendingPreHandshakeSendsFor(SessionState& state, NetworkGamer* gamer)
        {
            auto& pending = state.PendingPreHandshakeSends;
            auto removedBegin = std::remove_if(
                pending.begin(), pending.end(),
                [gamer](const PendingPreHandshakeAppData& p) { return p.Sender == gamer || p.Target == gamer; }
            );
            droppedAppDataCount_ += static_cast<std::size_t>(std::distance(removedBegin, pending.end()));
            pending.erase(removedBegin, pending.end());
        }

        // REMED-NET-001: ServerWelcome/GamerJoinBroadcast/GamerLeaveBroadcast/StateChangeBroadcast/
        // SessionPropertiesBroadcast
        // are, by this protocol's own design, sent only by the session's authoritative host to its
        // connected clients. The only peer connection that is ever the authoritative host from this
        // SessionState's own point of view is state.HostPeer (set exclusively by ConnectToHost/
        // AttemptHostMigration, when this side is acting as a client of someone else). A host's own
        // HostPeer is always null (a host never connects out to anyone), so this correctly rejects
        // these five message types unconditionally on the host side too - a host should never
        // receive host-authoritative messages from one of its own connecting clients at all.
        bool IsFromAuthoritativeHost(const SessionState& state, ENetPeer* peer)
        {
            return state.HostPeer != nullptr && peer == state.HostPeer;
        }

        // REMED-NET-001: any connected peer - a modified/malicious client needing no MITM, just a
        // custom ENet client speaking this fully-inferable wire format - could otherwise forge a
        // host-only broadcast message directly to the host (or to a fellow client's own incidental
        // listening socket, since even a "client" role peer owns a real accepting ENetHost here -
        // see ConnectToHost's own comment) to kick arbitrary gamers, inject fake gamers, corrupt
        // wire-id assignment, or force an arbitrary session-state transition. Logged as a first-class
        // protocol event (not a silent drop) and the offending peer is disconnected, matching this
        // file's own established "malformed/hostile input gets the peer disconnected" convention
        // (see HandleClientHello's Playing/AllowJoinInProgress rejection just below).
        void RejectUnauthorizedHostOnlyMessage(SessionState& state, ENetPeer* peer, const char* messageTypeName)
        {
            CNA::Logger::Warn(
                std::string("[ENetBackend] Rejected host-only ") + messageTypeName
                    + " from a non-host peer (forged or misbehaving client) - disconnecting the peer.",
                CNA::LogCategory::APPLICATION
            );
            state.Host.Disconnect(peer, 0);
        }

        void HandleClientHello(NetworkSession* session, SessionState& state, ENetPeer* peer, const ClientHelloMessage& hello)
        {
            // REMED-NET-003: a peer that already completed its handshake (already present in
            // PeerWireIds, unconditionally populated at the end of a first successful ClientHello
            // below) resending ClientHello would `new` a fresh batch of NetworkGamer objects every
            // time - unbounded roster growth from a single connected peer. A genuine reconnect
            // always arrives on a brand-new ENetPeer (a fresh CONNECT event - ENet never reuses a
            // live peer object across two separate connections), so keying this guard on peer
            // identity cannot reject a legitimate reconnect. AddLocalGamer (the real public API for
            // adding a split-screen local gamer after the initial handshake) never re-sends
            // ClientHello either - it only updates this session's own local bookkeeping - so this
            // also cannot reject that legitimate flow.
            if (state.PeerWireIds.contains(peer))
            {
                CNA::Logger::Warn(
                    "[ENetBackend] Rejected duplicate ClientHello from an already-handshaked peer - "
                    "disconnecting the peer.",
                    CNA::LogCategory::APPLICATION
                );
                state.Host.Disconnect(peer, 0);
                return;
            }

            // Task 2.7: incoming ClientHello was previously accepted unconditionally regardless of
            // sessionState_/AllowJoinInProgress - a host with AllowJoinInProgress == false still
            // silently accepted new players mid-Playing state. Reject by disconnecting the peer
            // outright (rather than a silent drop) so the connecting client isn't left hanging
            // forever waiting for a ServerWelcome that will never arrive.
            if (session->getSessionStateProperty() == NetworkSessionState::Playing
                && !session->getAllowJoinInProgressProperty())
            {
                state.Host.Disconnect(peer, 0);
                return;
            }

            EnsureLocalWireIds(session, state);

            ServerWelcomeMessage welcome;
            welcome.ExistingRoster = SnapshotRoster(session, state);
            welcome.SessionProperties = session->getSessionPropertiesProperty();

            GamerJoinBroadcastMessage broadcastMsg;
            std::vector<uint8_t> newWireIds;
            std::vector<NetworkGamer*> newGamers;
            // The connecting peer is one machine: its gamers share it (NetworkGamer.Machine).
            auto machine = std::make_shared<Microsoft::Xna::Framework::Net::NetworkMachine>(
                Microsoft::Xna::Framework::Net::NetworkMachine::CreateInternal());
            for (const std::string& gamertag : hello.LocalGamertags)
            {
                auto* gamer = new NetworkGamer(NetworkGamer::CreateInternal(session, gamertag));
                state.OwnedRemoteGamers.emplace_back(gamer); // Task 3.1
                gamer->SetSharedMachine(machine);
                machine->AddGamerInternal(gamer);
                // We are the host handling an incoming ClientHello, so this gamer belongs to the
                // connecting client - never the host.
                gamer->SetIsHost(false);
                uint8_t id = AssignWireId(state, gamer);
                welcome.AssignedWireIds.push_back(id);
                newWireIds.push_back(id);
                newGamers.push_back(gamer);
                broadcastMsg.NewGamers.push_back(RosterEntry{id, gamertag, false});
                state.WireIdToPeer[id] = peer;
            }
            state.PeerWireIds[peer] = std::move(newWireIds);
            if (!state.PeerMachineIds.contains(peer)) state.PeerMachineIds[peer] = state.NextMachineId++;

            SendTo(state, peer, NetPacketCodec::Encode(welcome), SendDataOptions::Reliable);
            // The joiner learns the host's settings and machine grouping with its welcome.
            SendTo(state, peer, NetPacketCodec::Encode(CurrentSettings(session)), SendDataOptions::Reliable);

            // Gamers already marked ready keep that state for the machine that just joined.
            GamerReadyMessage readiness;
            for (const auto& [gamer, wireId] : state.GamerToWireId)
            {
                if (gamer->getIsReadyProperty()) readiness.Entries.push_back(GamerReadyEntry{wireId, true});
            }
            if (!readiness.Entries.empty()) SendTo(state, peer, NetPacketCodec::Encode(readiness), SendDataOptions::Reliable);

            // AddRemoteGamer() raises GamerJoined for our own session, so it happens after the
            // ServerWelcome send (the new peer learns its ids from the welcome, not this event).
            for (NetworkGamer* gamer : newGamers)
            {
                session->AddRemoteGamer(gamer);
            }

            if (!broadcastMsg.NewGamers.empty())
            {
                auto bytes = NetPacketCodec::Encode(broadcastMsg);
                // Task 6.8: queue every peer's copy first, flush exactly once after the loop -
                // avoids one enet_host_flush() syscall per peer for what's otherwise identical
                // fan-out traffic.
                for (auto& [otherPeer, wireIds] : state.PeerWireIds)
                {
                    if (otherPeer != peer)
                    {
                        QueueSend(state, otherPeer, bytes, SendDataOptions::Reliable);
                    }
                }
                state.Host.Flush();
            }

            // audit_net.md remediation (2026-07-18): GamerToWireId just gained entries for both
            // this host's own locals (EnsureLocalWireIds above, first time only) and the newly
            // joined remote gamers - either could complete a pending pre-handshake send.
            FlushPendingPreHandshakeAppData(state);
        }

        void RequestPendingLocalAdds(SessionState& state)
        {
            if (!state.Welcomed || state.HostPeer == nullptr) return;
            for (LocalNetworkGamer* local : state.PendingLocalAdds)
                SendTo(state, state.HostPeer, NetPacketCodec::Encode(AddLocalGamerMessage{WireGamertagFor(local)}),
                       SendDataOptions::Reliable);
        }

        // Host: a client added a local gamer after joining. It joins that client's machine and
        // everyone (the requester included, which binds its own gamer to the id) hears of it.
        void HandleAddLocalGamer(NetworkSession* session, SessionState& state, ENetPeer* peer, const AddLocalGamerMessage& msg)
        {
            auto owned = state.PeerWireIds.find(peer);
            if (owned == state.PeerWireIds.end() || owned->second.empty() || msg.Gamertag.empty()) return;
            if (session->getAllGamersProperty().getCountProperty() >= session->getMaxGamersProperty()) return;
            auto* gamer = new NetworkGamer(NetworkGamer::CreateInternal(session, msg.Gamertag));
            state.OwnedRemoteGamers.emplace_back(gamer);
            gamer->SetIsHost(false);
            const auto sibling = state.WireIdToGamer.find(owned->second.front());
            if (sibling != state.WireIdToGamer.end())
            {
                gamer->SetSharedMachine(sibling->second->GetSharedMachine());
                gamer->GetSharedMachine()->AddGamerInternal(gamer);
            }
            const uint8_t id = AssignWireId(state, gamer);
            state.WireIdToPeer[id] = peer;
            owned->second.push_back(id);
            session->AddRemoteGamer(gamer);
            const auto bytes = NetPacketCodec::Encode(GamerJoinBroadcastMessage{{RosterEntry{id, msg.Gamertag, false}}});
            for (auto& [other, wireIds] : state.PeerWireIds) QueueSend(state, other, bytes, SendDataOptions::Reliable);
            state.Host.Flush();
            FlushPendingPreHandshakeAppData(state);
        }

        void HandleServerWelcome(NetworkSession* session, SessionState& state, const ServerWelcomeMessage& welcome)
        {
            ENetBackend::ApplyTransportSessionProperties(session, welcome.SessionProperties);

            const auto& locals = session->getLocalGamersProperty();
            for (int i = 0; i < locals.getCountProperty() && i < static_cast<int>(welcome.AssignedWireIds.size()); ++i)
            {
                uint8_t id = welcome.AssignedWireIds[static_cast<size_t>(i)];
                state.GamerToWireId[locals[i]] = id;
                state.WireIdToGamer[id] = locals[i];
                // Overwrite NetworkSession's own construction-time local placeholder id with the
                // real, host-negotiated one (see DEFERRED.md item #20).
                locals[i]->SetId(id);
            }

            NetworkGamer* welcomedHostGamer = nullptr;
            for (const RosterEntry& entry : welcome.ExistingRoster)
            {
                if (state.WireIdToGamer.contains(entry.WireId))
                {
                    continue;
                }
                auto* gamer = new NetworkGamer(NetworkGamer::CreateInternal(session, entry.Gamertag));
                state.OwnedRemoteGamers.emplace_back(gamer); // Task 3.1
                state.GamerToWireId[gamer] = entry.WireId;
                state.WireIdToGamer[entry.WireId] = gamer;
                gamer->SetId(entry.WireId);
                // Task 4.6: RosterEntry now carries a real host flag (SnapshotRoster on the host
                // side forwards each gamer's own accurate IsHost), so a client correctly learns
                // which remote gamer is the actual host here instead of always defaulting false.
                gamer->SetIsHost(entry.IsHost);
                session->AddRemoteGamer(gamer);
                if (entry.IsHost)
                {
                    welcomedHostGamer = gamer;
                }
            }

            // A normal Join() must not expose the construction-time local gamer as Host. Establish
            // the authoritative remote identity immediately while PumpSession is already running
            // on NetworkSession's owner thread, so EndJoin can observe completed ServerWelcome.
            // A migration asks the same helper to raise HostChanged; initial establishment does
            // not represent a host replacement and deliberately stays silent.
            if (welcomedHostGamer != nullptr)
            {
                ENetBackend::EstablishTransportHost(
                    session, welcomedHostGamer, state.AwaitingMigrationHostChangeEXT
                );
                state.AwaitingMigrationHostChangeEXT = false;
            }

            // The host-assigned ids place this machine's gamers after everyone already here.
            ENetBackend::OrderTransportGamers(session);
            // Local gamers added while this machine was still joining ask for their ids now; one
            // that made it into the hello was numbered above and must not be admitted twice.
            state.Welcomed = true;
            std::erase_if(state.PendingLocalAdds, [&](LocalNetworkGamer* local) { return state.GamerToWireId.contains(local); });
            for (LocalNetworkGamer* local : session->getLocalGamersProperty())
                if (!state.GamerToWireId.contains(local) &&
                    std::find(state.PendingLocalAdds.begin(), state.PendingLocalAdds.end(), local) == state.PendingLocalAdds.end())
                    state.PendingLocalAdds.push_back(local);
            RequestPendingLocalAdds(state);

            // audit_net.md remediation (2026-07-18): GamerToWireId just gained entries for this
            // client's own locals and every already-existing roster gamer - either could complete
            // a pending pre-handshake send queued before this ServerWelcome arrived.
            FlushPendingPreHandshakeAppData(state);
        }

        void HandleGamerJoinBroadcast(NetworkSession* session, SessionState& state, const GamerJoinBroadcastMessage& msg)
        {
            for (const RosterEntry& entry : msg.NewGamers)
            {
                if (state.WireIdToGamer.contains(entry.WireId))
                {
                    continue;
                }
                // A local gamer this machine asked the host to admit comes back numbered.
                const auto pending = std::find_if(state.PendingLocalAdds.begin(), state.PendingLocalAdds.end(),
                    [&](LocalNetworkGamer* local) { return WireGamertagFor(local) == entry.Gamertag; });
                if (pending != state.PendingLocalAdds.end())
                {
                    LocalNetworkGamer* local = *pending;
                    state.PendingLocalAdds.erase(pending);
                    state.GamerToWireId[local] = entry.WireId;
                    state.WireIdToGamer[entry.WireId] = local;
                    local->SetId(entry.WireId);
                    ENetBackend::OrderTransportGamers(session);
                    continue;
                }
                auto* gamer = new NetworkGamer(NetworkGamer::CreateInternal(session, entry.Gamertag));
                state.OwnedRemoteGamers.emplace_back(gamer); // Task 3.1
                state.GamerToWireId[gamer] = entry.WireId;
                state.WireIdToGamer[entry.WireId] = gamer;
                gamer->SetId(entry.WireId);
                // Task 4.6: same real host-flag propagation as HandleServerWelcome above - always
                // false in practice here, since a newly-joining client (the only thing this
                // broadcast ever announces) can never be the host.
                gamer->SetIsHost(entry.IsHost);
                session->AddRemoteGamer(gamer);
            }

            // audit_net.md remediation (2026-07-18): a pending send may have been targeting a
            // gamer this broadcast just introduced (a third gamer that joined after our own
            // handshake completed, still unresolved until now).
            FlushPendingPreHandshakeAppData(state);
        }

        void HandleStateChangeBroadcast(NetworkSession* session, SessionState& /*state*/, const StateChangeBroadcastMessage& msg)
        {
            NetworkSession::NetworkEvent evt;
            evt.Type = NetworkSession::NetworkEventType::StateChange;
            evt.State = msg.NewState;
            session->SendNetworkEvent(std::move(evt));
        }

        // The host accepts a client's report only for that client's own gamers and passes it on to
        // the other clients; a client accepts the host's reports for any gamer, its own included
        // (ResetReady).
        void HandleGamerReady(NetworkSession* session, SessionState& state, ENetPeer* fromPeer, const GamerReadyMessage& msg)
        {
            const bool host = state.HostPeer == nullptr;
            if (!host && !IsFromAuthoritativeHost(state, fromPeer))
            {
                RejectUnauthorizedHostOnlyMessage(state, fromPeer, "GamerReadyBroadcast");
                return;
            }
            if (session->getSessionStateProperty() != NetworkSessionState::Lobby)
            {
                return;
            }
            GamerReadyMessage accepted;
            for (const GamerReadyEntry& entry : msg.Entries)
            {
                if (host)
                {
                    const auto owned = state.PeerWireIds.find(fromPeer);
                    if (owned == state.PeerWireIds.end() ||
                        std::find(owned->second.begin(), owned->second.end(), entry.WireId) == owned->second.end())
                    {
                        continue;
                    }
                }
                const auto gamer = state.WireIdToGamer.find(entry.WireId);
                if (gamer == state.WireIdToGamer.end()) continue;
                ENetBackend::ApplyTransportGamerReady(*gamer->second, entry.IsReady);
                accepted.Entries.push_back(entry);
            }
            if (host && !accepted.Entries.empty())
            {
                const auto bytes = NetPacketCodec::Encode(accepted);
                for (auto& [otherPeer, wireIds] : state.PeerWireIds)
                {
                    if (otherPeer != fromPeer) QueueSend(state, otherPeer, bytes, SendDataOptions::Reliable);
                }
                state.Host.Flush();
            }
        }

        void HandleSessionPropertiesBroadcast(
            NetworkSession* session,
            const SessionPropertiesBroadcastMessage& message
        )
        {
            ENetBackend::ApplyTransportSessionProperties(session, message.SessionProperties);
        }

        void HandleAppData(NetworkSession* session, SessionState& state, ENetPeer* fromPeer, const AppDataMessage& msg)
        {
            auto targetIt = state.WireIdToGamer.find(msg.TargetWireId);
            if (targetIt == state.WireIdToGamer.end())
            {
                return; // unknown target; drop
            }
            NetworkGamer* target = targetIt->second;

            if (target->getIsLocalProperty())
            {
                // Task 6.1/6.2 (plans/plan_net.md Phase 6): SimulatedLatency/SimulatedPacketLoss are
                // scoped to exactly this point - AppData about to be delivered to one of this
                // session's own local gamers (real XNA's own documented meaning: what does *my*
                // network connection do to data addressed to *me*) - not to the CNA-internal
                // session-management protocol (ClientHello/ServerWelcome/etc., which needs to stay
                // reliable for the session itself to function coherently and was never part of the
                // real XNA API surface these properties govern), and not to a host's own relay hop
                // for two *other* peers just passing through (not data this machine's own game
                // code ever sees - see the relay branch below, unaffected).
                if (ShouldDropForSimulatedLoss(session->getSimulatedPacketLossProperty()))
                {
                    return;
                }

                auto senderIt = state.WireIdToGamer.find(msg.SenderWireId);
                NetworkGamer* senderGamer = (senderIt != state.WireIdToGamer.end()) ? senderIt->second : nullptr;

                System::TimeSpan latency = session->getSimulatedLatencyProperty();
                if (latency <= System::TimeSpan::Zero)
                {
                    // Task 6.4's own regression requirement: zero latency (the default) behaves
                    // exactly as before this task - immediate delivery, no queue involved at all.
                    NetworkSession::NetworkEvent evt;
                    evt.Type = NetworkSession::NetworkEventType::PacketSend;
                    evt.Gamer = target;
                    evt.Sender = senderGamer;
                    evt.Packet = msg.Payload;
                    evt.Reliable = msg.Options;
                    session->SendNetworkEvent(std::move(evt));
                    return;
                }

                PendingDelayedDelivery pending;
                pending.Target = target;
                pending.Sender = senderGamer;
                pending.Payload = msg.Payload;
                pending.Options = msg.Options;
                pending.ReleaseTime = Now() + std::chrono::microseconds(
                    static_cast<int64_t>(latency.getTotalMillisecondsProperty() * 1000.0)
                );
                state.PendingDeliveries.push_back(std::move(pending));
                return;
            }

            if (state.HostPeer == nullptr)
            {
                // We're the host and target belongs to someone else: relay it on, unless the
                // sender already owns it (that would just echo the packet back to its origin).
                auto peerIt = state.WireIdToPeer.find(msg.TargetWireId);
                if (peerIt != state.WireIdToPeer.end() && peerIt->second != fromPeer)
                {
                    SendTo(state, peerIt->second, NetPacketCodec::Encode(msg), msg.Options);
                }
            }
            // Else: we're a client that received an AppData for a gamer we don't own and aren't
            // hosting for — shouldn't happen in this star topology; drop defensively.
        }

        // Task 6.2: delivers every pending delayed AppData whose ReleaseTime has passed - called
        // once per PumpSession, so a packet queued by SimulatedLatency is handed to game code on
        // whichever later Update() call first observes Now() >= ReleaseTime, not necessarily the
        // very next one (matching how a real delayed packet's exact arrival frame isn't
        // predictable either).
        void ReleaseDuePendingDeliveries(NetworkSession* session, SessionState& state)
        {
            if (state.PendingDeliveries.empty())
            {
                return;
            }
            auto now = Now();
            auto it = state.PendingDeliveries.begin();
            while (it != state.PendingDeliveries.end())
            {
                if (it->ReleaseTime <= now)
                {
                    NetworkSession::NetworkEvent evt;
                    evt.Type = NetworkSession::NetworkEventType::PacketSend;
                    evt.Gamer = it->Target;
                    evt.Sender = it->Sender;
                    evt.Packet = std::move(it->Payload);
                    evt.Reliable = it->Options;
                    session->SendNetworkEvent(std::move(evt));
                    it = state.PendingDeliveries.erase(it);
                }
                else
                {
                    ++it;
                }
            }
        }

        void HandleGamerLeaveBroadcast(NetworkSession* session, SessionState& state, const GamerLeaveBroadcastMessage& msg)
        {
            for (uint8_t wireId : msg.WireIds)
            {
                auto gamerIt = state.WireIdToGamer.find(wireId);
                if (gamerIt == state.WireIdToGamer.end())
                {
                    continue; // unknown; already removed or never known
                }
                NetworkGamer* gamer = gamerIt->second;
                session->RemoveGamer(gamer, NetworkSessionEndReason::Disconnected);
                state.GamerToWireId.erase(gamer);
                state.WireIdToGamer.erase(gamerIt);
                // audit_net.md remediation (2026-07-18, third round): this path was missing the
                // same purge HandleDisconnect's own per-gamer removal already does - a pending
                // send naming a gamer who left via this broadcast (not a direct disconnect of
                // *our* connection) would otherwise sit unresolved in the queue until evicted by
                // unrelated churn, since GamerToWireId can never gain an entry for it again.
                PurgePendingPreHandshakeSendsFor(state, gamer);
            }
        }

        // Task 5.1/5.2/5.3 (plans/plan_net.md Phase 5): called from HandleDisconnect's host-lost branch
        // when AllowHostMigration is true, instead of ending the session outright. Every survivor
        // independently computes the same deterministic new host (lowest remaining wire id) from
        // its own already-cached roster - no extra negotiation round-trip is needed, since every
        // peer already learned the full roster via ServerWelcome/GamerJoinBroadcast. Returns true
        // if a migration attempt was made (a real promotion, or a reconnect attempt was actually
        // launched); false if the caller should fall back to the pre-existing immediate-
        // session-end behavior (migration disabled, nobody left to migrate to, or no reachable new
        // host was found - Task 5.1's documented "best-effort, not guaranteed" limitation, no
        // retry loop here).
        bool AttemptHostMigration(NetworkSession* session, SessionState& state)
        {
            if (!session->getAllowHostMigrationProperty())
            {
                return false;
            }

            // Find the dying host by its real IsHost flag instead of the public Host property.
            // This code runs while the disconnect is actively pruning the wire roster, so using
            // the transport's authoritative map keeps the migration decision independent of the
            // public event queue's dispatch timing.
            // Only ever matches a *remote* gamer: this peer just lost a connection it was the
            // client of (peer == state.HostPeer, checked by the caller), so the dying host can
            // never be one of this peer's own local gamers by construction - excluding locals also
            // sidesteps a real, separate, pre-existing quirk where a NetworkSession constructed
            // via Create() (unlike a real Join()) leaves its own local gamers' IsHost at whatever
            // the constructor set, even after ConnectToHost turns it into a client (see
            // ENetBackendTests.cpp's own SystemLinkSessionFixture comment).
            uint8_t deadHostId = 0;
            bool foundDeadHost = false;
            for (const auto& [id, gamer] : state.WireIdToGamer)
            {
                if (!gamer->getIsLocalProperty() && gamer->getIsHostProperty())
                {
                    deadHostId = id;
                    foundDeadHost = true;
                    break;
                }
            }
            if (!foundDeadHost)
            {
                return false; // handshake never completed far enough to even learn who the host was
            }

            std::vector<uint8_t> survivorIds;
            for (const auto& [id, gamer] : state.WireIdToGamer)
            {
                if (id != deadHostId)
                {
                    survivorIds.push_back(id);
                }
            }
            if (survivorIds.empty())
            {
                return false; // nothing left to migrate to (shouldn't happen - own locals survive)
            }
            uint8_t newHostId = *std::min_element(survivorIds.begin(), survivorIds.end());

            bool selfIsNewHost = false;
            for (LocalNetworkGamer* local : session->getLocalGamersProperty())
            {
                auto it = state.GamerToWireId.find(local);
                if (it != state.GamerToWireId.end() && it->second == newHostId)
                {
                    selfIsNewHost = true;
                    break;
                }
            }

            // Still valid to read here - the shared cleanup below hasn't run yet.
            std::string newHostGamertag;
            if (!selfIsNewHost)
            {
                newHostGamertag = state.WireIdToGamer.at(newHostId)->getGamertagProperty();
                state.LastMigrationReconnectAttemptGamertagForTestingEXT = newHostGamertag;
            }

            // Shared cleanup for both outcomes below: every remote gamer this peer knew about is
            // really gone from its point of view now (real GamerLeave events - a reconnecting/
            // promoted peer's roster genuinely gets rebuilt from scratch, not preserved across the
            // migration - see plans/plan_net.md Task 5.1's own "simple, not seamless" scope note), and
            // every wire-id bookkeeping structure is reset so the handshake that follows (either
            // accepting fresh reconnects as the new host, or this peer's own reconnect to whoever
            // else was promoted) starts from a clean slate - stale entries from the dead host's own
            // numbering would otherwise collide with freshly (re)assigned ids.
            std::vector<NetworkGamer*> staleRemotes(
                session->getRemoteGamersProperty().begin(), session->getRemoteGamersProperty().end()
            );
            for (NetworkGamer* gamer : staleRemotes)
            {
                session->RemoveGamer(gamer, NetworkSessionEndReason::Disconnected);
            }
            state.WireIdToGamer.clear();
            state.GamerToWireId.clear();
            state.PeerWireIds.clear();
            state.WireIdToPeer.clear();
            state.FreeWireIds.clear();
            state.NextWireId = 0;
            // Keep departed remote identities alive until session teardown. RemoveGamer stores
            // them in PreviousGamers and queues GamerLeft arguments, while Host still names the
            // old remote until the migration result is established. Freeing OwnedRemoteGamers
            // here made all three public views dangle; the old construction-time Host bug merely
            // masked that use-after-free. Wire maps above are still cleared, so retained objects
            // cannot participate in the new transport topology.
            //
            // Pending sends name the old topology and therefore still cannot be delivered. Count
            // and clear them exactly as before, without relying on gamer destruction for safety.
            droppedAppDataCount_ += state.PendingPreHandshakeSends.size();
            state.PendingPreHandshakeSends.clear();
            state.HostPeer = nullptr;
            // The next hello (or this machine's own numbering) covers every local gamer.
            state.Welcomed = false;
            state.PendingLocalAdds.clear();
            state.HostRoundtrips.clear();

            if (selfIsNewHost)
            {
                for (LocalNetworkGamer* local : session->getLocalGamersProperty())
                {
                    local->SetIsHost(true);
                }
                // Task 5.2: this peer's ENetHostHandle is already bound and running -
                // ConnectToHost's own non-Emscripten path always calls StartHosting first, even
                // for a pure client - so no new socket is needed to start accepting real incoming
                // connections.
                ENetDiscoveryService::RegisterHost(session, state.Host.getBoundPortProperty());

                ENetBackend::EstablishTransportHost(
                    session, session->getLocalGamersProperty()[0], true
                );
                return true;
            }

            // Task 5.3: a star topology gives surviving clients no direct channel to each other,
            // and the dead host obviously can't relay anything either - LAN rediscovery (the same
            // mechanism NetworkSession::Find() already uses) is the only way left to learn the new
            // host's address. Matches by gamertag - a pre-existing, honest limitation of this
            // whole discovery layer (two same-gamertag hosts on one LAN are already ambiguous
            // today), not a new gap host migration introduces.
            //
            // A few short attempts, not one: ENetDiscoveryService's own doc comments already
            // acknowledge that which of several same-port-bound sockets actually *receives* a
            // given datagram is OS-arbitrary when more than one process shares the discovery port
            // (exactly the situation here - the newly-promoted peer's own RegisterHost call and
            // this peer's FindSessions call can be landing at almost the same real wall-clock
            // moment, across genuinely separate processes on a real multi-machine LAN). A single
            // 150ms search window occasionally missing a reply it should have gotten is a real,
            // acknowledged platform characteristic, not a bug this task can fix at the socket
            // layer - retrying a handful of times (~150ms each, so still well under a second total
            // even in the worst case) is the pragmatic mitigation, and it's free of cost for the
            // common single-process case (an unregistered gamertag stays unregistered no matter
            // how many times it's searched for, so this loop still exits after the first attempt
            // there).
            for (int attempt = 0; attempt < 3; ++attempt)
            {
                for (const AvailableNetworkSession& candidate
                     : ENetDiscoveryService::FindSessions(session->getSessionTypeProperty()))
                {
                    if (candidate.getHostGamertagProperty() == newHostGamertag)
                    {
                        state.AwaitingMigrationHostChangeEXT = true;
                        // state.Host.Connect(...) is called directly here rather than through the
                        // public ENetBackend::ConnectToHost(session, ...) wrapper deliberately: on
                        // Emscripten, that wrapper *replaces* Sessions()[session] outright (a
                        // fresh client-only SessionState), which would leave this very function's
                        // own `state` reference - and PumpSession's, still iterating its event
                        // loop on the stack above this call - dangling for the rest of this pump.
                        // Calling Connect() directly on the already-existing, already-bound host
                        // handle reconnects to a new address without ever touching Sessions()
                        // itself, safe on every platform (and functionally identical to the
                        // wrapper's own non-Emscripten body once StartHosting's redundant no-op is
                        // accounted for).
                        state.HostPeer = state.Host.Connect(
                            candidate.GetConnectAddress(), candidate.GetConnectPort(), kChannelLimit
                        );
                        return true;
                    }
                }
            }

            return false;
        }

        void RemovePeerGamers(NetworkSession* session, SessionState& state, ENetPeer* peer);

        void HandleDisconnect(NetworkSession* session, SessionState& state, ENetPeer* peer, uint32_t data)
        {
            if (peer == state.HostPeer && data == DisconnectRemovedByHost)
            {
                // The host removed this machine (NetworkMachine.RemoveFromSession): no migration.
                const auto& locals = session->getLocalGamersProperty();
                if (locals.getCountProperty() > 0)
                {
                    session->RemoveGamer(locals[0], NetworkSessionEndReason::RemovedByHost);
                }
                state.HostPeer = nullptr;
                droppedAppDataCount_ += state.PendingPreHandshakeSends.size();
                state.PendingPreHandshakeSends.clear();
                return;
            }
            if (peer == state.HostPeer)
            {
                // Task 5.2/5.3/5.4: migration replaces the old immediate-end behavior only when
                // AllowHostMigration is true AND a real migration attempt could be launched (a
                // promotion, or a reconnect to a discovered new host) - AttemptHostMigration
                // itself falls back to false for every case that should still behave exactly as
                // before (disabled, or no reachable new host), so this stays a pure branch, not a
                // behavior change for the pre-existing default-off path.
                if (AttemptHostMigration(session, state))
                {
                    return;
                }

                // We're a client and just lost our connection to the host: our own view of this
                // session is over. RemoveGamer's isLocal branch raises a single session-wide
                // SessionEnded event no matter which local gamer is passed (see its own doc
                // comment), so any one of them is a valid trigger.
                const auto& locals = session->getLocalGamersProperty();
                if (locals.getCountProperty() > 0)
                {
                    session->RemoveGamer(locals[0], NetworkSessionEndReason::HostEndedSession);
                }
                state.HostPeer = nullptr;
                // audit_net.md remediation (2026-07-18): this client's whole view of the session
                // just ended - any send still queued for a not-yet-resolved target can never be
                // delivered into a roster that's about to be gone regardless. Counted (third-round
                // remediation) - see the host-migration reset's own identical comment above.
                droppedAppDataCount_ += state.PendingPreHandshakeSends.size();
                state.PendingPreHandshakeSends.clear();
                return;
            }

            // We're the host (or at least not this peer's upstream) and one of our clients
            // disconnected: remove every gamer it owned and tell the remaining peers.
            RemovePeerGamers(session, state, peer);
        }

        // Removes every gamer a client peer owned and tells the remaining peers they left.
        void RemovePeerGamers(NetworkSession* session, SessionState& state, ENetPeer* peer)
        {
            auto peerWireIdsIt = state.PeerWireIds.find(peer);
            if (peerWireIdsIt == state.PeerWireIds.end())
            {
                return; // a peer we never completed a handshake with; nothing to clean up
            }

            GamerLeaveBroadcastMessage broadcastMsg;
            for (uint8_t wireId : peerWireIdsIt->second)
            {
                auto gamerIt = state.WireIdToGamer.find(wireId);
                if (gamerIt == state.WireIdToGamer.end())
                {
                    continue;
                }
                NetworkGamer* gamer = gamerIt->second;
                session->RemoveGamer(gamer, NetworkSessionEndReason::Disconnected);
                broadcastMsg.WireIds.push_back(wireId);
                state.GamerToWireId.erase(gamer);
                state.WireIdToGamer.erase(gamerIt);
                state.WireIdToPeer.erase(wireId);
                // audit_net.md remediation (2026-07-18): gamer just left and will never resolve
                // now - any send still queued naming it as sender or target must not linger.
                PurgePendingPreHandshakeSendsFor(state, gamer);
                // Task 2.11: reclaim the id for reuse by a future AssignWireId call, instead of
                // leaving NextWireId to eventually wrap around after enough cumulative join/leave
                // churn.
                state.FreeWireIds.push_back(wireId);
            }
            state.PeerWireIds.erase(peerWireIdsIt);
            state.PeerMachineIds.erase(peer);

            if (!broadcastMsg.WireIds.empty())
            {
                auto bytes = NetPacketCodec::Encode(broadcastMsg);
                // Task 6.8: same batch-then-flush-once reasoning as HandleClientHello's own
                // broadcast fan-out above.
                for (auto& [otherPeer, wireIds] : state.PeerWireIds)
                {
                    QueueSend(state, otherPeer, bytes, SendDataOptions::Reliable);
                }
                state.Host.Flush();
            }
        }

        void HandleConnect(NetworkSession* session, SessionState& state, ENetPeer* peer)
        {
            if (peer != state.HostPeer)
            {
                // Someone connected to us; nothing to do until their ClientHello arrives.
                return;
            }

            ClientHelloMessage hello;
            for (LocalNetworkGamer* gamer : session->getLocalGamersProperty())
            {
                hello.LocalGamertags.push_back(gamer->getSignedInGamerProperty()->getGamertagProperty());
            }
            SendTo(state, peer, NetPacketCodec::Encode(hello), SendDataOptions::Reliable);
        }

        void HandleReceive(NetworkSession* session, SessionState& state, ENetPeer* peer, ENetPacket* packet)
        {
            std::vector<SharpRuntime::bytecs> data(packet->data, packet->data + packet->dataLength);
            if (data.empty())
            {
                return;
            }
            // Task 1.4: this packet arrived over an already-open ENet channel, but nothing else
            // validates its payload — a truncated/corrupted packet from any connected peer makes
            // any Decode* call below throw std::runtime_error (BinaryReader::ReadBytes/ReadString
            // throw on underflow). Uncaught, that exception used to propagate straight out of
            // Update() into the caller's own game loop: a remote DoS from a single bad packet. Drop
            // the offending packet and keep the session running instead.
            try
            {
                switch (NetPacketCodec::PeekTag(data))
                {
                    case MessageTag::ClientHello:
                        HandleClientHello(session, state, peer, NetPacketCodec::DecodeClientHello(data));
                        break;
                    case MessageTag::ServerWelcome:
                        // REMED-NET-001: host-only message - see IsFromAuthoritativeHost's own comment.
                        if (!IsFromAuthoritativeHost(state, peer))
                        {
                            RejectUnauthorizedHostOnlyMessage(state, peer, "ServerWelcome");
                            break;
                        }
                        HandleServerWelcome(session, state, NetPacketCodec::DecodeServerWelcome(data));
                        break;
                    case MessageTag::GamerJoinBroadcast:
                        if (!IsFromAuthoritativeHost(state, peer))
                        {
                            RejectUnauthorizedHostOnlyMessage(state, peer, "GamerJoinBroadcast");
                            break;
                        }
                        HandleGamerJoinBroadcast(session, state, NetPacketCodec::DecodeGamerJoinBroadcast(data));
                        break;
                    case MessageTag::GamerLeaveBroadcast:
                        if (!IsFromAuthoritativeHost(state, peer))
                        {
                            RejectUnauthorizedHostOnlyMessage(state, peer, "GamerLeaveBroadcast");
                            break;
                        }
                        HandleGamerLeaveBroadcast(session, state, NetPacketCodec::DecodeGamerLeaveBroadcast(data));
                        break;
                    case MessageTag::StateChangeBroadcast:
                        if (!IsFromAuthoritativeHost(state, peer))
                        {
                            RejectUnauthorizedHostOnlyMessage(state, peer, "StateChangeBroadcast");
                            break;
                        }
                        HandleStateChangeBroadcast(session, state, NetPacketCodec::DecodeStateChangeBroadcast(data));
                        break;
                    case MessageTag::SessionPropertiesBroadcast:
                        if (!IsFromAuthoritativeHost(state, peer))
                        {
                            RejectUnauthorizedHostOnlyMessage(state, peer, "SessionPropertiesBroadcast");
                            break;
                        }
                        HandleSessionPropertiesBroadcast(
                            session,
                            NetPacketCodec::DecodeSessionPropertiesBroadcast(data)
                        );
                        break;
                    case MessageTag::GamerReadyBroadcast:
                        HandleGamerReady(session, state, peer, NetPacketCodec::DecodeGamerReady(data));
                        break;
                    case MessageTag::AddLocalGamerRequest:
                        // Only a host admits gamers.
                        if (state.HostPeer != nullptr) break;
                        HandleAddLocalGamer(session, state, peer, NetPacketCodec::DecodeAddLocalGamer(data));
                        break;
                    case MessageTag::SessionSettingsBroadcast:
                        if (!IsFromAuthoritativeHost(state, peer))
                        {
                            RejectUnauthorizedHostOnlyMessage(state, peer, "SessionSettingsBroadcast");
                            break;
                        }
                        HandleSessionSettings(session, NetPacketCodec::DecodeSessionSettings(data));
                        break;
                    case MessageTag::MachineRosterBroadcast:
                        if (!IsFromAuthoritativeHost(state, peer))
                        {
                            RejectUnauthorizedHostOnlyMessage(state, peer, "MachineRosterBroadcast");
                            break;
                        }
                        HandleMachineRoster(state, NetPacketCodec::DecodeMachineRoster(data));
                        break;
                    case MessageTag::NetworkStatsBroadcast:
                        if (!IsFromAuthoritativeHost(state, peer))
                        {
                            RejectUnauthorizedHostOnlyMessage(state, peer, "NetworkStatsBroadcast");
                            break;
                        }
                        state.HostRoundtrips.clear();
                        for (const auto& entry : NetPacketCodec::DecodeNetworkStats(data).Entries)
                            state.HostRoundtrips[entry.WireId] = entry.Milliseconds;
                        break;
                    case MessageTag::AppData:
                        HandleAppData(session, state, peer, NetPacketCodec::DecodeAppData(data));
                        break;
                    default:
                        break;
                }
            }
            catch (const std::exception&)
            {
                // Malformed/truncated payload - drop it and keep the session alive.
            }
        }

        // Task 1.4: guarantees enet_packet_destroy runs even if HandleReceive somehow still lets
        // an exception escape (defense-in-depth alongside the try/catch above) - previously a
        // plain post-call `enet_packet_destroy(evt.packet)` in PumpSession was skipped whenever an
        // exception unwound past it, leaking the packet.
        class ReceivedPacketGuard
        {
        public:
            explicit ReceivedPacketGuard(ENetPacket* packet) : packet_(packet) { }
            ~ReceivedPacketGuard() { enet_packet_destroy(packet_); }
            ReceivedPacketGuard(const ReceivedPacketGuard&) = delete;
            ReceivedPacketGuard& operator=(const ReceivedPacketGuard&) = delete;

        private:
            ENetPacket* packet_;
        };
    }

    void ENetBackend::EstablishTransportHost(
        NetworkSession* session,
        NetworkGamer* host,
        bool raiseHostChanged
    )
    {
        session->SetHostFromTransport(host, raiseHostChanged);
    }

    void ENetBackend::ApplyTransportSessionProperties(
        NetworkSession* session,
        NetworkSessionProperties properties
    )
    {
        session->SetSessionPropertiesFromTransport(std::move(properties));
    }

    void ENetBackend::ApplyTransportGamerReady(NetworkGamer& gamer, bool value)
    {
        NetworkSession::ApplyGamerReadyInternal(gamer, value);
    }

    void ENetBackend::RemoveMachine(NetworkSession* session, NetworkGamer* gamer)
    {
        auto found = Sessions().find(session);
        if (found == Sessions().end())
        {
            return;
        }
        SessionState& state = *found->second;
        const auto wireId = state.GamerToWireId.find(gamer);
        if (wireId == state.GamerToWireId.end())
        {
            return;
        }
        const auto peer = state.WireIdToPeer.find(wireId->second);
        if (peer == state.WireIdToPeer.end())
        {
            return;
        }
        ENetPeer* removed = peer->second;
        // The host's view changes now; the removed machine learns why from the disconnect data.
        RemovePeerGamers(session, state, removed);
        state.Host.Disconnect(removed, DisconnectRemovedByHost);
        state.Host.Flush();
    }

    void ENetBackend::AnnounceLocalGamer(NetworkSession* session, LocalNetworkGamer* local)
    {
        auto found = Sessions().find(session);
        if (found == Sessions().end()) return;
        SessionState& state = *found->second;
        if (state.HostPeer == nullptr)
        {
            // The host numbers its own gamer; clients already here hear of it now (later ones get
            // it in their welcome).
            if (state.PeerWireIds.empty()) return;
            const uint8_t id = AssignWireId(state, local);
            const auto bytes = NetPacketCodec::Encode(GamerJoinBroadcastMessage{{RosterEntry{id, WireGamertagFor(local), local->getIsHostProperty()}}});
            for (auto& [peer, wireIds] : state.PeerWireIds) QueueSend(state, peer, bytes, SendDataOptions::Reliable);
            state.Host.Flush();
            ENetBackend::OrderTransportGamers(session);
            return;
        }
        state.PendingLocalAdds.push_back(local);
        if (state.Welcomed)
        {
            SendTo(state, state.HostPeer, NetPacketCodec::Encode(AddLocalGamerMessage{WireGamertagFor(local)}),
                   SendDataOptions::Reliable);
        }
    }

    void ENetBackend::ApplyTransportSettings(NetworkSession* session, int maxGamers, int privateGamerSlots,
        bool allowJoinInProgress, bool allowHostMigration)
    {
        session->SetSettingsFromTransport(maxGamers, privateGamerSlots, allowJoinInProgress, allowHostMigration);
    }

    void ENetBackend::OrderTransportGamers(NetworkSession* session)
    {
        session->OrderGamersInternal();
    }

    bool ENetBackend::RealNetworkingEnabled(NetworkSessionType sessionType)
    {
        return sessionType == NetworkSessionType::SystemLink;
    }

    void ENetBackend::StartHosting(NetworkSession* session)
    {
        if (!RealNetworkingEnabled(session->getSessionTypeProperty()))
        {
            return;
        }

        auto& sessions = Sessions();
        if (sessions.contains(session))
        {
            return;
        }

#ifdef __EMSCRIPTEN__
        auto state = std::make_unique<SessionState>(
            SessionState{ENetHostHandle::CreateHost(kEmscriptenHostPort, kMaxPeers, kChannelLimit)}
        );
#else
        auto state = std::make_unique<SessionState>(
            SessionState{ENetHostHandle::CreateHost(0, kMaxPeers, kChannelLimit)}
        );
#endif
        uint16_t boundPort = state->Host.getBoundPortProperty();
        state->LastPublishedSessionProperties = session->getSessionPropertiesProperty();

        // Task 6.3: RegisterHost can throw (EnsureSocket's bind/create failure). Previously the
        // session was already emplace()'d into Sessions() by this point - a throw here left a
        // real, live, bound ENet host registered but never discoverable via Find(), with no
        // rollback and no way to retry (StartHosting is a no-op once Sessions() already contains
        // this session). Registering for discovery *before* committing to Sessions() means a
        // throw here instead just unwinds normally: `state`'s ENetHostHandle destructor tears
        // down the half-created host, and Sessions() never learns about it at all.
        ENetDiscoveryService::RegisterHost(session, boundPort);

        sessions.emplace(session, std::move(state));
    }

    void ENetBackend::TeardownSession(NetworkSession* session)
    {
        auto it = Sessions().find(session);
        if (it != Sessions().end())
        {
            // Task 2.14: previously just erased from Sessions(), destroying the ENetHostHandle
            // (-> enet_host_destroy()) with no prior enet_peer_disconnect for still-connected
            // peers - they'd wait out ENet's internal connection timeout instead of receiving an
            // immediate, clean disconnect notification. Disconnect every known peer first and
            // flush so the DISCONNECT packets actually go out before the host is torn down.
            SessionState& state = *it->second;
            for (const auto& [peer, wireIds] : state.PeerWireIds)
            {
                state.Host.Disconnect(peer, 0);
            }
            if (state.HostPeer != nullptr)
            {
                state.Host.Disconnect(state.HostPeer, 0);
            }
            state.Host.Flush();
        }
        Sessions().erase(session);
        ENetDiscoveryService::UnregisterHost(session);
    }

    void ENetBackend::PumpSession(NetworkSession* session)
    {
        auto it = Sessions().find(session);
        if (it == Sessions().end())
        {
            return;
        }
        SessionState& state = *it->second;

        ENetEvent evt;
        while (state.Host.Service(0, evt) > 0)
        {
            if (evt.type == ENET_EVENT_TYPE_CONNECT)
            {
                HandleConnect(session, state, evt.peer);
            }
            else if (evt.type == ENET_EVENT_TYPE_RECEIVE)
            {
                ReceivedPacketGuard packetGuard(evt.packet);
                HandleReceive(session, state, evt.peer, evt.packet);
            }
            else if (evt.type == ENET_EVENT_TYPE_DISCONNECT)
            {
                HandleDisconnect(session, state, evt.peer, evt.data);
            }
        }

        // Task 4.1: NetworkGamer::RoundtripTime was permanently dead (never assigned anywhere) -
        // ENet already natively tracks real per-peer RTT; surface it every pump instead. Scoped to
        // the host's view of each of its directly-connected remote gamers (WireIdToPeer only holds
        // entries the host itself populated in HandleClientHello) - a client's own view of the
        // host, or of any other client relayed through the host in this star topology, has no
        // equivalent direct ENetPeer to read from without further plumbing, and stays at its
        // default (unmeasured) TimeSpan::Zero.
        for (const auto& [wireId, peer] : state.WireIdToPeer)
        {
            auto gamerIt = state.WireIdToGamer.find(wireId);
            if (gamerIt != state.WireIdToGamer.end())
            {
                gamerIt->second->SetRoundtripTime(System::TimeSpan::FromMilliseconds(peer->roundTripTime));
            }
        }

        // Task 6.2: hands off any delayed AppData whose SimulatedLatency has now elapsed. Runs
        // every pump regardless of whether any new ENet events arrived above, so a packet queued
        // by a previous pump still gets released on schedule even if nothing new comes in.
        ReleaseDuePendingDeliveries(session, state);
        ApplyClientRoundtrips(state);
        const auto now = std::chrono::steady_clock::now();
        if (state.Traffic.sample(now, state.Host.getTotalSentDataProperty(), state.Host.getTotalReceivedDataProperty()))
        {
            session->SetTrafficFromTransport(state.Traffic.sentPerSecond(), state.Traffic.receivedPerSecond());
        }
        PublishSessionPropertiesIfChanged(session, state);
        PublishNetworkStats(state, now);
        PublishSettingsAndMachinesIfChanged(session, state);
    }

    uint16_t ENetBackend::GetBoundPort(NetworkSession* session)
    {
        auto it = Sessions().find(session);
        if (it == Sessions().end())
        {
            return 0;
        }
        return it->second->Host.getBoundPortProperty();
    }

    std::size_t ENetBackend::GetDroppedAppDataCount()
    {
        return droppedAppDataCount_;
    }

    void ENetBackend::ResetDroppedAppDataCount()
    {
        droppedAppDataCount_ = 0;
    }

    std::size_t ENetBackend::GetOwnedRemoteGamerCountForTesting(NetworkSession* session)
    {
        auto it = Sessions().find(session);
        if (it == Sessions().end())
        {
            return 0;
        }
        return it->second->OwnedRemoteGamers.size();
    }

    std::size_t ENetBackend::GetSessionCountForTesting()
    {
        return Sessions().size();
    }

    std::string ENetBackend::GetLastMigrationReconnectAttemptGamertagForTesting(NetworkSession* session)
    {
        auto it = Sessions().find(session);
        if (it == Sessions().end())
        {
            return {};
        }
        return it->second->LastMigrationReconnectAttemptGamertagForTestingEXT;
    }

    void ENetBackend::SetClockForTesting(std::chrono::steady_clock::time_point time)
    {
        ClockOverrideForTesting() = time;
    }

    void ENetBackend::ResetClockForTesting()
    {
        ClockOverrideForTesting().reset();
    }

    void ENetBackend::SeedPacketLossRngForTesting(unsigned seed)
    {
        PacketLossRng().seed(seed);
    }

    void ENetBackend::ConnectToHost(NetworkSession* session, const std::string& address, uint16_t port)
    {
        if (!RealNetworkingEnabled(session->getSessionTypeProperty()))
        {
            return;
        }

#ifdef __EMSCRIPTEN__
        // A real browser tab can never bind/listen (see NEXT.md), so the "client" role here is
        // rebuilt as a pure outbound-only host instead of reusing whatever StartHosting's
        // constructor call already bound - matching what a real browser can actually do.
        Sessions()[session] = std::make_unique<SessionState>(
            SessionState{ENetHostHandle::CreateClient(kChannelLimit)}
        );
        Sessions()[session]->LastPublishedSessionProperties = session->getSessionPropertiesProperty();
#else
        StartHosting(session);
#endif

        SessionState& state = *Sessions().at(session);
        state.HostPeer = state.Host.Connect(address, port, kChannelLimit);
    }

    void ENetBackend::SendAppData(
        NetworkSession* session,
        NetworkGamer* sender,
        NetworkGamer* target,
        const std::vector<SharpRuntime::bytecs>& payload,
        SendDataOptions options
    )
    {
        if (!RealNetworkingEnabled(session->getSessionTypeProperty()))
        {
            return;
        }

        auto it = Sessions().find(session);
        if (it == Sessions().end())
        {
            return;
        }
        SessionState& state = *it->second;

        auto senderIt = state.GamerToWireId.find(sender);
        auto targetIt = state.GamerToWireId.find(target);
        if (senderIt == state.GamerToWireId.end() || targetIt == state.GamerToWireId.end())
        {
            // audit_net.md remediation (2026-07-18): reachable when SendData is called
            // immediately after Join()/ConnectToHost(), before any Update() call has pumped the
            // ClientHello/ServerWelcome round-trip that populates GamerToWireId (confirmed
            // reachable via the real public LocalNetworkGamer::SendData path, not just internal
            // plumbing - see ENetBackendTests.cpp's own reachability test). Previously a totally
            // silent, then a counted-but-still-silent, drop; now queued for delivery once
            // FlushPendingPreHandshakeAppData resolves both ends (HandleClientHello/
            // HandleServerWelcome/HandleGamerJoinBroadcast all call it), bounded so a caller that
            // never calls Update() can't grow this without limit - the oldest queued entry is
            // evicted (and counted as a drop, same observable meaning GetDroppedAppDataCount()
            // always had: "a SendAppData call that could not eventually be delivered") to make
            // room for the newest, once kMaxPendingPreHandshakeAppData is reached.
            auto& pending = state.PendingPreHandshakeSends;
            if (pending.size() >= kMaxPendingPreHandshakeAppData)
            {
                pending.erase(pending.begin());
                ++droppedAppDataCount_;
            }
            pending.push_back(PendingPreHandshakeAppData{sender, target, payload, options});
            return;
        }

        DeliverAppData(state, senderIt->second, targetIt->second, payload, options);
    }

    void ENetBackend::PublishGamerReady(NetworkSession* session, const std::vector<NetworkGamer*>& gamers)
    {
        if (!RealNetworkingEnabled(session->getSessionTypeProperty()))
        {
            return;
        }
        auto it = Sessions().find(session);
        if (it == Sessions().end())
        {
            return;
        }
        SessionState& state = *it->second;

        GamerReadyMessage msg;
        for (NetworkGamer* gamer : gamers)
        {
            const auto wireId = state.GamerToWireId.find(gamer);
            if (wireId != state.GamerToWireId.end()) msg.Entries.push_back(GamerReadyEntry{wireId->second, gamer->getIsReadyProperty()});
        }
        if (msg.Entries.empty())
        {
            return;
        }
        const auto bytes = NetPacketCodec::Encode(msg);
        if (state.HostPeer != nullptr)
        {
            // A client reports to the host, which passes it on.
            SendTo(state, state.HostPeer, bytes, SendDataOptions::Reliable);
            return;
        }
        for (auto& [peer, wireIds] : state.PeerWireIds)
        {
            QueueSend(state, peer, bytes, SendDataOptions::Reliable);
        }
        state.Host.Flush();
    }

    void ENetBackend::BroadcastStateChange(NetworkSession* session, NetworkSessionState newState)
    {
        if (!RealNetworkingEnabled(session->getSessionTypeProperty()))
        {
            return;
        }

        auto it = Sessions().find(session);
        if (it == Sessions().end())
        {
            return;
        }
        SessionState& state = *it->second;

        if (state.HostPeer != nullptr)
        {
            return; // only the ENet-transport host broadcasts state changes
        }

        StateChangeBroadcastMessage msg;
        msg.NewState = newState;
        auto bytes = NetPacketCodec::Encode(msg);
        // Task 6.8: same batch-then-flush-once reasoning as HandleClientHello/HandleDisconnect's
        // own broadcast fan-outs.
        for (auto& [peer, wireIds] : state.PeerWireIds)
        {
            QueueSend(state, peer, bytes, SendDataOptions::Reliable);
        }
        state.Host.Flush();
    }
}
