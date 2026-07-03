// SPDX-License-Identifier: MIT
// Copyright (c) Robert Vokac and contributors
#include "CNA/Internal/Net/ENetBackend.hpp"
#include "CNA/Internal/Net/ENetHostHandle.hpp"
#include "CNA/Internal/Net/NetPacketCodec.hpp"
#include "Microsoft/Xna/Framework/GamerServices/SignedInGamer.hpp"
#include "Microsoft/Xna/Framework/Net/LocalNetworkGamer.hpp"
#include "Microsoft/Xna/Framework/Net/NetworkGamer.hpp"
#include "Microsoft/Xna/Framework/Net/NetworkSession.hpp"

#include <enet/enet.h>
#include <memory>
#include <unordered_map>
#include <vector>

namespace CNA::Internal::Net
{
    using Microsoft::Xna::Framework::GamerServices::SignedInGamer;
    using Microsoft::Xna::Framework::Net::LocalNetworkGamer;
    using Microsoft::Xna::Framework::Net::NetworkGamer;
    using Microsoft::Xna::Framework::Net::NetworkSession;

    namespace
    {
        constexpr size_t kMaxPeers = static_cast<size_t>(NetworkSession::MaxSupportedGamers);
        constexpr size_t kChannelLimit = 2;
        constexpr uint8_t kControlChannel = 0;

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
        };

        std::unordered_map<NetworkSession*, std::unique_ptr<SessionState>>& Sessions()
        {
            static std::unordered_map<NetworkSession*, std::unique_ptr<SessionState>> sessions;
            return sessions;
        }

        uint8_t AssignWireId(SessionState& state, NetworkGamer* gamer)
        {
            uint8_t id = state.NextWireId++;
            state.GamerToWireId[gamer] = id;
            state.WireIdToGamer[id] = gamer;
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
            EnsureLocalWireIds(session, state);

            ServerWelcomeMessage welcome;
            welcome.ExistingRoster = SnapshotRoster(session, state);

            GamerJoinBroadcastMessage broadcastMsg;
            std::vector<uint8_t> newWireIds;
            std::vector<NetworkGamer*> newGamers;
            for (const std::string& gamertag : hello.LocalGamertags)
            {
                auto* gamer = new NetworkGamer(NetworkGamer::CreateInternal(session, gamertag));
                uint8_t id = AssignWireId(state, gamer);
                welcome.AssignedWireIds.push_back(id);
                newWireIds.push_back(id);
                newGamers.push_back(gamer);
                broadcastMsg.NewGamers.push_back(RosterEntry{id, gamertag});
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
            }

            for (const RosterEntry& entry : welcome.ExistingRoster)
            {
                if (state.WireIdToGamer.contains(entry.WireId))
                {
                    continue;
                }
                auto* gamer = new NetworkGamer(NetworkGamer::CreateInternal(session, entry.Gamertag));
                state.GamerToWireId[gamer] = entry.WireId;
                state.WireIdToGamer[entry.WireId] = gamer;
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
                state.GamerToWireId[gamer] = entry.WireId;
                state.WireIdToGamer[entry.WireId] = gamer;
                session->AddRemoteGamer(gamer);
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
                default:
                    // GamerLeaveBroadcast/StateChangeBroadcast/AppData: Tasks 5.5-5.7.
                    break;
            }
        }
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

        auto state = std::make_unique<SessionState>(
            SessionState{ENetHostHandle::CreateHost(0, kMaxPeers, kChannelLimit)}
        );
        sessions.emplace(session, std::move(state));
    }

    void ENetBackend::TeardownSession(NetworkSession* session)
    {
        Sessions().erase(session);
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
                HandleReceive(session, state, evt.peer, evt.packet);
                enet_packet_destroy(evt.packet);
            }
            // ENET_EVENT_TYPE_DISCONNECT: Task 5.6.
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

    void ENetBackend::ConnectToHost(NetworkSession* session, const std::string& address, uint16_t port)
    {
        if (!RealNetworkingEnabled(session->getSessionTypeProperty()))
        {
            return;
        }
        StartHosting(session);

        SessionState& state = *Sessions().at(session);
        state.HostPeer = state.Host.Connect(address, port, kChannelLimit);
    }
}
