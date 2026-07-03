// SPDX-License-Identifier: MIT
// Copyright (c) Robert Vokac and contributors
#pragma once

#include "Microsoft/Xna/Framework/Net/NetworkSessionType.hpp"
#include <cstdint>
#include <string>

namespace Microsoft::Xna::Framework::Net
{
    class NetworkSession;
}

namespace CNA::Internal::Net
{
    using Microsoft::Xna::Framework::Net::NetworkSession;
    using Microsoft::Xna::Framework::Net::NetworkSessionType;

    /**
     * @brief Static facade wiring NetworkSession to the real ENet transport.
     *
     * Mirrors the existing GamerServicesDispatcher static-class pattern rather than an abstract
     * backend interface, since ENet is this project's only networking implementation. Keeps all
     * ENet C-API types out of Microsoft::Xna::Framework::Net headers: per-session transport state
     * lives entirely in ENetBackend.cpp, keyed by NetworkSession* in a private registry.
     */
    class ENetBackend
    {
    public:
        /**
         * @brief Returns whether sessionType uses real ENet-backed networking.
         *
         * Only SystemLink does. Local/LocalWithLeaderboards are single-machine by XNA design;
         * PlayerMatch/Ranked imply Xbox LIVE-style internet matchmaking that this project has no
         * server for, so they stay fully synthetic (unchanged from the pre-Phase-5 stub).
         *
         * @param sessionType The session type to check.
         * @return true if sessionType should be backed by a real ENet host.
         */
        static bool RealNetworkingEnabled(NetworkSessionType sessionType);

        /**
         * @brief Starts hosting a real ENet host for session, if not already hosting.
         *
         * Binds to an OS-assigned ephemeral UDP port. No-op if
         * RealNetworkingEnabled(session's type) is false, or if a host is already registered
         * for this session.
         *
         * @param session The session to start hosting.
         */
        static void StartHosting(NetworkSession* session);

        /**
         * @brief Tears down and unregisters any ENet transport state for session.
         *
         * Safe to call even if no transport state is registered for session (no-op).
         *
         * @param session The session to tear down.
         */
        static void TeardownSession(NetworkSession* session);

        /**
         * @brief Drains all pending ENet events for session's transport, if any.
         *
         * Non-blocking. No-op if no transport state is registered for session.
         *
         * @param session The session to pump.
         */
        static void PumpSession(NetworkSession* session);

        /**
         * @brief Gets the local UDP port assigned to session's ENet host.
         *
         * @param session The session to query.
         * @return The bound port, or 0 if session isn't currently hosting real networking.
         */
        static uint16_t GetBoundPort(NetworkSession* session);

        /**
         * @brief Connects session to a remote host and begins the ClientHello/ServerWelcome
         * handshake, exercising the same transport session's own EndJoin() would eventually use
         * (bypassing EndJoin's pre-existing activeAction-stranding bug — see NEXT.md).
         *
         * No-op if RealNetworkingEnabled(session's type) is false. session must already have (or
         * be able to start) its own registered ENet host, since even a "client" role peer owns
         * an ENetHost in this design (see ENetHostHandle's own doc comment).
         *
         * @param session The (already-constructed, local) session initiating the connection.
         * @param address Dotted IPv4 address or resolvable hostname of the host to connect to.
         * @param port The host's bound UDP port (see GetBoundPort()).
         */
        static void ConnectToHost(NetworkSession* session, const std::string& address, uint16_t port);
    };
}
