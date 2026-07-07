// SPDX-License-Identifier: MIT
// Copyright (c) Robert Vokac and contributors
#pragma once

#include "Microsoft/Xna/Framework/Net/NetworkSessionState.hpp"
#include "Microsoft/Xna/Framework/Net/NetworkSessionType.hpp"
#include "Microsoft/Xna/Framework/Net/SendDataOptions.hpp"
#include "SharpRuntime/SharpRuntimeHelper.hpp"
#include <cstdint>
#include <string>
#include <vector>

namespace Microsoft::Xna::Framework::Net
{
    class NetworkSession;
    class NetworkGamer;
}

namespace CNA::Internal::Net
{
    using Microsoft::Xna::Framework::Net::NetworkGamer;
    using Microsoft::Xna::Framework::Net::NetworkSession;
    using Microsoft::Xna::Framework::Net::NetworkSessionState;
    using Microsoft::Xna::Framework::Net::NetworkSessionType;
    using Microsoft::Xna::Framework::Net::SendDataOptions;

    /**
     * @brief Static facade wiring NetworkSession to the real ENet transport.
     *
     * Mirrors the existing GamerServicesDispatcher static-class pattern rather than an abstract
     * backend interface, since ENet is this project's only networking implementation. Keeps all
     * ENet C-API types out of Microsoft::Xna::Framework::Net headers: per-session transport state
     * lives entirely in ENetBackend.cpp, keyed by NetworkSession* in a private registry.
     *
     * Task 6.4: single-threaded only, by design and by contract - every method here (and the
     * process-wide session registry backing them) must always be called from the same thread,
     * matching the same expectation real XNA's own single-threaded Game/Update loop already
     * places on NetworkSession itself. There is no internal synchronization (no
     * std::mutex/std::atomic anywhere in this module); every real call path drives this class
     * exclusively through NetworkSession::Update() on whatever thread the game itself calls
     * Update() from. A caller that invokes NetworkSession::Update() from more than one thread, or
     * calls any of these methods directly off-thread, would race silently - this is an explicit,
     * intentional constraint, not an oversight, and matches XNA's own single-threaded design.
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

        /**
         * @brief Sends payload from sender to target over the real ENet transport, relaying
         * through the host if session isn't the one hosting target's connection.
         *
         * No-op if RealNetworkingEnabled(session's type) is false, or session has no registered
         * transport. Assumes sender and target are both already known to ENetBackend (true
         * whenever called from NetworkSession::Update()'s PacketSend handling, since both always
         * come from session->getAllGamersProperty(), which only ever contains gamers already
         * assigned a wire-id by the Task 5.4 handshake).
         *
         * @param session The local session sending the data.
         * @param sender The local gamer sending the data.
         * @param target The (necessarily remote) gamer to deliver payload to.
         * @param payload The application payload bytes.
         * @param options The delivery guarantees to request.
         */
        static void SendAppData(
            NetworkSession* session,
            NetworkGamer* sender,
            NetworkGamer* target,
            const std::vector<SharpRuntime::bytecs>& payload,
            SendDataOptions options
        );

        /**
         * @brief Task 2.13: how many `SendAppData` calls have been silently dropped because
         * sender and/or target weren't yet known to ENetBackend's wire-id map.
         *
         * Reachable in practice when `SendData` is called immediately after `Join()`/
         * `ConnectToHost()`, before any `Update()` call has pumped the `ClientHello`/
         * `ServerWelcome` round-trip that populates the map. Not part of real XNA; exists purely
         * so this previously-totally-silent drop is at least observable (e.g. by tests, or a
         * game's own diagnostics) instead of vanishing with no trace at all.
         *
         * @return The number of drops observed so far, process-wide, since startup or the last
         * `ResetDroppedAppDataCount()` call.
         */
        static std::size_t GetDroppedAppDataCount();

        /** @brief Task 2.13: resets the counter `GetDroppedAppDataCount()` reports back to zero. */
        static void ResetDroppedAppDataCount();

        /**
         * @brief Task 3.1: the number of remote gamer objects session's own transport state
         * currently owns and has not yet freed (created by `HandleClientHello`/
         * `HandleServerWelcome`/`HandleGamerJoinBroadcast`, freed when this session's transport
         * state is torn down). Exists purely to make Task 3.1's ownership fix testable; not part
         * of real XNA.
         *
         * @param session The session to query.
         * @return The number of currently-owned, not-yet-freed remote gamer objects, or 0 if
         * session has no registered transport.
         */
        static std::size_t GetOwnedRemoteGamerCountForTesting(NetworkSession* session);

        /**
         * @brief Task 6.3: the number of sessions currently registered in `ENetBackend`'s own
         * process-wide transport-state map.
         *
         * Exists purely to make `StartHosting`'s all-or-nothing registration invariant testable:
         * a session should never be committed to this map unless *every* step of `StartHosting`
         * (including discovery registration) succeeded. Not part of real XNA.
         *
         * @return The number of currently-registered sessions.
         */
        static std::size_t GetSessionCountForTesting();

        /**
         * @brief Broadcasts a session state change (StartGame/EndGame) to every connected peer.
         *
         * No-op if RealNetworkingEnabled(session's type) is false, session has no registered
         * transport, or session isn't itself the ENet-transport host — a session that connected
         * out via ConnectToHost never broadcasts state changes; only the actual host's own
         * StartGame/EndGame call should propagate to everyone else.
         *
         * @param session The hosting session whose state changed.
         * @param newState The new session state.
         */
        static void BroadcastStateChange(NetworkSession* session, NetworkSessionState newState);
    };
}
