// SPDX-License-Identifier: MIT
// Copyright (c) Robert Vokac and contributors
#include "CNA/Internal/Net/ENetBackend.hpp"
#include "CNA/Internal/Net/ENetDiscoveryService.hpp"
#include "CNA/Internal/Net/ENetHostHandle.hpp"
#include "CNA/Internal/Net/NetPacketCodec.hpp"
#include "Microsoft/Xna/Framework/GamerServices/SignedInGamer.hpp"
#include "Microsoft/Xna/Framework/Net/LocalNetworkGamer.hpp"
#include "Microsoft/Xna/Framework/Net/NetworkGamer.hpp"
#include "Microsoft/Xna/Framework/Net/NetworkSession.hpp"

#include <enet/enet.h>
#include <memory>
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

#ifdef __EMSCRIPTEN__
        // Emscripten's SOCKFS bind()/getsockname() shim never reports back a real OS-assigned
        // ephemeral port (see NEXT.md) - hosting on Web must request a fixed, known port instead
        // of relying on ENET_PORT_ANY (0) + read-back. Only reachable in practice by a Node.js-run
        // dedicated relay/server build: a real browser tab can never accept incoming connections
        // at all (browsers cannot open listening sockets), so hosting is moot there regardless.
        constexpr uint16_t kEmscriptenHostPort = 61191;
#endif

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
            // Task 3.1: every remote NetworkGamer this SessionState's own HandleClientHello/
            // HandleServerWelcome/HandleGamerJoinBroadcast ever `new`s, previously permanently
            // leaked (NetworkSession::AddRemoteGamer deliberately never takes ownership - see its
            // own doc comment - since its established contract also accepts non-heap gamers, e.g.
            // in tests). Freed automatically when this SessionState is destroyed (TeardownSession
            // erasing it from Sessions()), which already happens at the same time
            // NetworkSession::Dispose() frees everything *it* owns.
            std::vector<std::unique_ptr<NetworkGamer>> OwnedRemoteGamers;
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
                roster.push_back(RosterEntry{state.GamerToWireId.at(gamer), WireGamertagFor(gamer)});
            }
            return roster;
        }

        void SendTo(
            SessionState& state,
            ENetPeer* peer,
            const std::vector<SharpRuntime::bytecs>& bytes,
            SendDataOptions options,
            uint8_t channel = kControlChannel
        )
        {
            state.Host.Send(peer, channel, bytes.data(), bytes.size(), NetPacketCodec::SendDataOptionsToEnetFlags(options));
            state.Host.Flush();
        }

        void HandleClientHello(NetworkSession* session, SessionState& state, ENetPeer* peer, const ClientHelloMessage& hello)
        {
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
                // connecting client - never the host (see DEFERRED.md item #20).
                gamer->SetIsHost(false);
                uint8_t id = AssignWireId(state, gamer);
                welcome.AssignedWireIds.push_back(id);
                newWireIds.push_back(id);
                newGamers.push_back(gamer);
                broadcastMsg.NewGamers.push_back(RosterEntry{id, gamertag});
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
                for (auto& [otherPeer, wireIds] : state.PeerWireIds)
                {
                    if (otherPeer != peer)
                    {
                        SendTo(state, otherPeer, bytes, SendDataOptions::Reliable);
                    }
                }
            }
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
                // RosterEntry doesn't carry a host flag, so this new remote gamer's IsHost stays
                // at NetworkGamer's default (false) even when it's actually the host's gamer -
                // a scoped, documented limitation (see DEFERRED.md item #20 and
                // NetworkGamer::SetIsHost's doc comment). Its Id is real and wire-consistent
                // regardless.
                gamer->SetId(entry.WireId);
                session->AddRemoteGamer(gamer);
            }
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
                // Same scoped IsHost limitation as HandleServerWelcome above; Id is real either way.
                gamer->SetId(entry.WireId);
                session->AddRemoteGamer(gamer);
            }
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
                auto senderIt = state.WireIdToGamer.find(msg.SenderWireId);
                NetworkSession::NetworkEvent evt;
                evt.Type = NetworkSession::NetworkEventType::PacketSend;
                evt.Gamer = target;
                evt.Sender = (senderIt != state.WireIdToGamer.end()) ? senderIt->second : nullptr;
                evt.Packet = msg.Payload;
                evt.Reliable = msg.Options;
                session->SendNetworkEvent(std::move(evt));
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
            }
        }

        void HandleDisconnect(NetworkSession* session, SessionState& state, ENetPeer* peer)
        {
            if (peer == state.HostPeer)
            {
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
                // Task 2.11: reclaim the id for reuse by a future AssignWireId call, instead of
                // leaving NextWireId to eventually wrap around after enough cumulative join/leave
                // churn.
                state.FreeWireIds.push_back(wireId);
            }
            state.PeerWireIds.erase(peerWireIdsIt);

            if (!broadcastMsg.WireIds.empty())
            {
                auto bytes = NetPacketCodec::Encode(broadcastMsg);
                for (auto& [otherPeer, wireIds] : state.PeerWireIds)
                {
                    SendTo(state, otherPeer, bytes, SendDataOptions::Reliable);
                }
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
                        HandleServerWelcome(session, state, NetPacketCodec::DecodeServerWelcome(data));
                        break;
                    case MessageTag::GamerJoinBroadcast:
                        HandleGamerJoinBroadcast(session, state, NetPacketCodec::DecodeGamerJoinBroadcast(data));
                        break;
                    case MessageTag::GamerLeaveBroadcast:
                        HandleGamerLeaveBroadcast(session, state, NetPacketCodec::DecodeGamerLeaveBroadcast(data));
                        break;
                    case MessageTag::StateChangeBroadcast:
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
            // Task 2.13: reachable when SendData is called immediately after Join()/
            // ConnectToHost(), before any Update() call has pumped the ClientHello/ServerWelcome
            // round-trip that populates GamerToWireId. Previously a totally silent, unobservable
            // drop; surfaced via a simple counter rather than a bigger queue-and-flush-once-ready
            // redesign, since nothing else in this codebase retries a dropped send either.
            ++droppedAppDataCount_;
            return;
        }

        AppDataMessage msg;
        msg.SenderWireId = senderIt->second;
        msg.TargetWireId = targetIt->second;
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
        auto peerIt = state.WireIdToPeer.find(msg.TargetWireId);
        if (peerIt != state.WireIdToPeer.end())
        {
            SendTo(state, peerIt->second, bytes, options);
        }
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
        for (auto& [peer, wireIds] : state.PeerWireIds)
        {
            SendTo(state, peer, bytes, SendDataOptions::Reliable);
        }
    }
}
