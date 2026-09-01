// SPDX-License-Identifier: MIT
// Copyright (c) Robert Vokac and contributors
#pragma once

#include "Microsoft/Xna/Framework/Net/NetworkSessionState.hpp"
#include "Microsoft/Xna/Framework/Net/NetworkSessionProperties.hpp"
#include "Microsoft/Xna/Framework/Net/NetworkSessionType.hpp"
#include "Microsoft/Xna/Framework/Net/SendDataOptions.hpp"
#include "SharpRuntime/SharpRuntimeHelper.hpp"
#include <chrono>
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
    using Microsoft::Xna::Framework::Net::NetworkSessionProperties;
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
         * @brief Installs the host identity established by the transport handshake.
         *
         * This internal bridge is the only ENet route allowed to mutate NetworkSession's private
         * host pointer. Initial establishment is silent; migration can request HostChanged.
         *
         * @param session The session whose host was established.
         * @param host The authoritative local or remote host gamer.
         * @param raiseHostChanged Whether the replacement represents host migration.
         */
        static void EstablishTransportHost(
            NetworkSession* session,
            NetworkGamer* host,
            bool raiseHostChanged
        );

        /**
         * @brief Installs the host-authoritative session properties received by the transport.
         *
         * @param session The client session whose property snapshot is being updated.
         * @param properties The complete property collection received from the host.
         */
        static void ApplyTransportSessionProperties(
            NetworkSession* session,
            NetworkSessionProperties properties
        );

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
         * transport. sender and/or target need not already be known to ENetBackend's wire-id map:
         * if either isn't resolved yet (reachable when `SendData` is called immediately after
         * `Join()`/`ConnectToHost()`, before any `Update()` call has pumped the `ClientHello`/
         * `ServerWelcome` round-trip - see audit_net.md remediation, 2026-07-18), the call is
         * queued (bounded, oldest evicted first - see `GetDroppedAppDataCount()`) and delivered
         * automatically the moment both sides resolve, preserving each entry's own
         * payload/target/options exactly. Order is preserved for multiple queued calls sharing
         * the same (sender, target) pair (they always resolve together, and are drained in
         * original enqueue order) - across *different* pairs, delivery order follows whenever
         * each pair happens to resolve, not necessarily original `SendAppData` call order (a
         * pair that resolves later is queued longer, by definition; nothing here reorders once
         * two pairs are both resolvable at the same flush).
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
         * @brief Task 2.13: how many queued `SendAppData` calls could not eventually be
         * delivered, for any reason - counts every case, not just queue overflow.
         *
         * A `SendAppData` call whose sender and/or target aren't yet known to ENetBackend's
         * wire-id map (see `SendAppData`'s own doc comment) is queued rather than dropped
         * outright, and delivered automatically once both resolve. This counter increments once
         * per queued entry that turns out to never be deliverable: the queue itself overflowing
         * its bound (a caller queuing sends far faster than `Update()` is ever called to resolve
         * them, oldest evicted); a queued entry's sender or target gamer leaving before it ever
         * resolved (`HandleDisconnect`/`HandleGamerLeaveBroadcast` purge it by name); or the
         * whole queue being invalidated at once (host migration's full wire-id-map reset, or this
         * peer's own session ending as a client) - every one of these is a real "could not
         * eventually be delivered" outcome, counted the same way, never silently discarded with
         * no trace (third-round remediation, 2026-07-18 - the second round's own version of this
         * comment claimed this already but the purge/full-reset paths didn't actually count yet).
         * Not part of real XNA; exists purely for observability (e.g. by tests, or a game's own
         * diagnostics).
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
         * @brief Task 5.5: the gamertag `AttemptHostMigration` most recently decided the new host
         * must be, the last time session lost its host connection and this peer itself was *not*
         * the deterministically-chosen new host.
         *
         * Exists purely to make the tie-break math (excluding the dead host, picking the true
         * minimum remaining wire id) testable in a single process, where a second real
         * `NetworkSession` to actually reconnect to can't exist. Not part of real XNA.
         *
         * @param session The session to query.
         * @return The gamertag, or an empty string if no such migration attempt has happened yet
         * (or session has no registered transport).
         */
        static std::string GetLastMigrationReconnectAttemptGamertagForTesting(NetworkSession* session);

        /**
         * @brief Task 6.3 (plans/plan_net.md Phase 6): overrides the time source SimulatedLatency's
         * delayed-delivery queue reads `Now()` from, fixing it at `time` instead of the real
         * `std::chrono::steady_clock`.
         *
         * Lets tests advance simulated time deterministically (e.g. confirm a packet is *not* yet
         * released, then move the clock forward and confirm it now is) instead of depending on a
         * real sleep and a flaky timing assertion. Not part of real XNA.
         *
         * @param time The fixed value `Now()` should report until `ResetClockForTesting()`.
         */
        static void SetClockForTesting(std::chrono::steady_clock::time_point time);

        /** @brief Task 6.3: restores the real `std::chrono::steady_clock` as the time source. */
        static void ResetClockForTesting();

        /**
         * @brief Task 6.3: reseeds the process-wide RNG behind SimulatedPacketLoss's
         * probabilistic drop decision.
         *
         * Not needed for the documented probability extremes 0.0/1.0 (see
         * `ShouldDropForSimulatedLoss`'s own comment in `ENetBackend.cpp` - both are handled
         * without ever touching the RNG), but exists for completeness and any future test needing
         * a deterministic outcome at an intermediate probability. Not part of real XNA.
         *
         * @param seed The seed value.
         */
        static void SeedPacketLossRngForTesting(unsigned seed);

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
