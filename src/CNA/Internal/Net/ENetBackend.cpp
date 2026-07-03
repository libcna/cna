// SPDX-License-Identifier: MIT
// Copyright (c) Robert Vokac and contributors
#include "CNA/Internal/Net/ENetBackend.hpp"
#include "CNA/Internal/Net/ENetHostHandle.hpp"
#include "Microsoft/Xna/Framework/Net/NetworkSession.hpp"

#include <enet/enet.h>
#include <memory>
#include <unordered_map>

namespace CNA::Internal::Net
{
    using Microsoft::Xna::Framework::Net::NetworkSession;

    namespace
    {
        constexpr size_t kMaxPeers = static_cast<size_t>(NetworkSession::MaxSupportedGamers);
        constexpr size_t kChannelLimit = 2;

        // Per-session ENet transport state. Grows in later Phase 5 tasks (peer<->gamer maps,
        // wire-id counter) as handshake/relay/discovery logic needs it.
        struct SessionState
        {
            ENetHostHandle Host;
        };

        std::unordered_map<NetworkSession*, std::unique_ptr<SessionState>>& Sessions()
        {
            static std::unordered_map<NetworkSession*, std::unique_ptr<SessionState>> sessions;
            return sessions;
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

        ENetHostHandle& host = it->second->Host;
        ENetEvent evt;
        while (host.Service(0, evt) > 0)
        {
            // Task 5.4+ translates CONNECT/DISCONNECT/RECEIVE events into
            // NetworkSession::NetworkEvent/AddRemoteGamer()/RemoveGamer() calls. No peer can
            // exist yet at this task, so there is nothing to translate.
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
}
