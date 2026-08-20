// SPDX-License-Identifier: MIT
// Copyright (c) Robert Vokac and contributors
#include "CNA/Internal/Net/ENetBackend.hpp"
#include "CNA/Internal/Net/ENetDiscoveryService.hpp"
#include "CNA/Internal/Net/ENetHostHandle.hpp"
#include "CNA/Internal/Net/NetPacketCodec.hpp"
#include "CNA/Logger.hpp"
#include "Microsoft/Xna/Framework/GamerServices/SignedInGamer.hpp"
#include "Microsoft/Xna/Framework/Net/AvailableNetworkSession.hpp"
#include "Microsoft/Xna/Framework/Net/LocalNetworkGamer.hpp"
#include "Microsoft/Xna/Framework/Net/NetworkGamer.hpp"
#include "Microsoft/Xna/Framework/Net/NetworkSession.hpp"

#include <algorithm>
#include <chrono>
#include <enet/enet.h>
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
        }

        // The gamertag to put on the wire for gamer. LocalNetworkGamer::getGamertagProperty()
        // always reports "Stub Gamer" (an unchanged, preserved FNA stub behavior — see
        // NetworkGamer.cpp), so its real identity for other machines comes from the underlying
        // SignedInGamer instead. Already-remote NetworkGamer instances were constructed with
        // their real gamertag (see HandleClientHello/HandleServerWelcome/
        // HandleGamerJoinBroadcast), so getGamertagProperty() is correct for them as-is.
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

        // REMED-NET-001: ServerWelcome/GamerJoinBroadcast/GamerLeaveBroadcast/StateChangeBroadcast
        // are, by this protocol's own design, sent only by the session's authoritative host to its
        // connected clients. The only peer connection that is ever the authoritative host from this
        // SessionState's own point of view is state.HostPeer (set exclusively by ConnectToHost/
        // AttemptHostMigration, when this side is acting as a client of someone else). A host's own
        // HostPeer is always null (a host never connects out to anyone), so this correctly rejects
        // these four message types unconditionally on the host side too - a host should never
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

            GamerJoinBroadcastMessage broadcastMsg;
            std::vector<uint8_t> newWireIds;
            std::vector<NetworkGamer*> newGamers;
            for (const std::string& gamertag : hello.LocalGamertags)
            {
                auto* gamer = new NetworkGamer(NetworkGamer::CreateInternal(session, gamertag));
                state.OwnedRemoteGamers.emplace_back(gamer); // Task 3.1
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

            SendTo(state, peer, NetPacketCodec::Encode(welcome), SendDataOptions::Reliable);

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

        void HandleServerWelcome(NetworkSession* session, SessionState& state, const ServerWelcomeMessage& welcome)
        {
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

            // Task 5.3: this ServerWelcome completed a migration reconnect (AttemptHostMigration
            // set the flag right before issuing the Connect() call, and always fully cleared
            // WireIdToGamer first - see its own doc comment - so the host's own roster entry is
            // guaranteed fresh here, never skipped by the `contains` check above).
            if (state.AwaitingMigrationHostChangeEXT)
            {
                state.AwaitingMigrationHostChangeEXT = false;
                if (welcomedHostGamer != nullptr)
                {
                    NetworkSession::NetworkEvent evt;
                    evt.Type = NetworkSession::NetworkEventType::HostChange;
                    evt.Gamer = welcomedHostGamer;
                    session->SendNetworkEvent(std::move(evt));
                }
            }

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

            // NetworkSession::getHostProperty() cannot be used here: host_ is only ever set to
            // localGamers_[0] at construction (both for a real Create()-based host and for a real
            // Join()-based client - see the constructor) and, before this task, was never updated
            // to reflect an actual remote host afterward (NetworkEventType::HostChange existed but
            // nothing ever enqueued it - see Task 5.1's own investigation notes). Find the dying
            // host by its real IsHost flag instead - correctly maintained on every known gamer by
            // HandleServerWelcome/HandleGamerJoinBroadcast per Task 4.6.
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
            state.OwnedRemoteGamers.clear();
            // audit_net.md remediation (2026-07-18): OwnedRemoteGamers.clear() just freed every
            // remote NetworkGamer this peer knew about - any PendingPreHandshakeSends entry still
            // naming one of them would otherwise dangle. The new topology after migration makes a
            // pre-migration cross-machine send meaningless regardless, so the whole queue is
            // dropped rather than selectively purged. Counted (third-round remediation): every
            // entry here genuinely can never be delivered now, matching
            // GetDroppedAppDataCount()'s "could not eventually be delivered" contract.
            droppedAppDataCount_ += state.PendingPreHandshakeSends.size();
            state.PendingPreHandshakeSends.clear();
            state.HostPeer = nullptr;

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

                NetworkSession::NetworkEvent evt;
                evt.Type = NetworkSession::NetworkEventType::HostChange;
                evt.Gamer = session->getLocalGamersProperty()[0];
                session->SendNetworkEvent(std::move(evt));
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

        void HandleDisconnect(NetworkSession* session, SessionState& state, ENetPeer* peer)
        {
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
                HandleDisconnect(session, state, evt.peer);
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
