// SPDX-License-Identifier: MIT
// Copyright (c) Robert Vokac and contributors
#pragma once

#include "Microsoft/Xna/Framework/Net/AvailableNetworkSession.hpp"
#include "Microsoft/Xna/Framework/Net/NetworkSessionType.hpp"
#include <cstdint>
#include <vector>

namespace Microsoft::Xna::Framework::Net
{
    class NetworkSession;
}

namespace CNA::Internal::Net
{
    using Microsoft::Xna::Framework::Net::AvailableNetworkSession;
    using Microsoft::Xna::Framework::Net::NetworkSession;
    using Microsoft::Xna::Framework::Net::NetworkSessionType;

    /**
     * @brief Owns the process-wide raw-UDP LAN discovery socket.
     *
     * A single well-known-port UDP socket answers DiscoveryQuery messages (see
     * NetDiscoveryProtocol) for whichever SystemLink session is currently registered as the
     * hosting side, and collects DiscoveryAnnounce replies during a Find() search. This is a
     * separate transport from ENetBackend's connected ENet channel, since discovery has to work
     * before any connection exists. See the approved Phase 5 plan's "Discovery protocol" section
     * for the full design rationale (client-queries/host-replies, not periodic host-announce; a
     * unicast loopback copy of every query as a same-machine fallback, which is also what makes
     * this reliably testable in this sandboxed environment).
     *
     * Permanently disabled on Emscripten: raw UDP broadcast/unicast has no equivalent on the Web
     * platform at all (no browser, and no Node.js `ws` package, can send a raw datagram), so every
     * method below is a no-op there (RegisterHost/UnregisterHost/Poll do nothing; FindSessions
     * always returns empty). This is a permanent platform constraint, not a TODO.
     *
     * Task 6.4: single-threaded only, by design and by contract - same reasoning and same
     * constraint as ENetBackend's own class-level doc comment. registeredHost_/socket_/
     * currentResults_ (this file's internal state) have no synchronization at all; every real
     * call path reaches this class exclusively through NetworkSession::Update() on whatever
     * single thread the game itself drives its update loop from.
     */
    class ENetDiscoveryService
    {
    public:
        /**
         * @brief Registers session as the answerable SystemLink host at connectPort.
         *
         * Only one session can ever be registered at a time in this process — the same
         * single-active-NetworkSession constraint already enforced by NetworkSession::BeginCreate's
         * activeSession_ gate applies here too. Registering a new session replaces any previous
         * registration.
         *
         * @param session The hosting session to answer discovery queries for.
         * @param connectPort The session's real ENet connect port (see ENetBackend::GetBoundPort).
         */
        static void RegisterHost(NetworkSession* session, uint16_t connectPort);

        /**
         * @brief Unregisters session.
         *
         * Safe to call even if session was never registered, or a different session is currently
         * registered (in which case this is a no-op).
         *
         * @param session The session to unregister.
         */
        static void UnregisterHost(NetworkSession* session);

        /**
         * @brief Drains all pending discovery-socket messages, replying to any incoming
         * DiscoveryQuery if a host is currently registered.
         *
         * Non-blocking. Called from NetworkSession::Update() for any RealNetworkingEnabled
         * session, so a registered host keeps answering queries for as long as it stays alive.
         */
        static void Poll();

        /**
         * @brief Synchronously searches the LAN for hosted SystemLink sessions.
         *
         * Broadcasts a DiscoveryQuery (plus a unicast copy to 127.0.0.1) and blocks for a short,
         * fixed discovery window collecting DiscoveryAnnounce replies. Returns an empty list
         * immediately for any sessionTypeFilter other than SystemLink, matching the
         * NetworkSessionType policy that only SystemLink is backed by real networking.
         *
         * @param sessionTypeFilter The session type being searched for.
         * @return The discovered sessions, each carrying its real connect address/port.
         */
        static std::vector<AvailableNetworkSession> FindSessions(NetworkSessionType sessionTypeFilter);
    };
}
