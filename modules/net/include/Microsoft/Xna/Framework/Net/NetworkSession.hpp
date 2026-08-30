// SPDX-License-Identifier: MS-PL
#pragma once
#include "CNA/CNAHelper.hpp"
#include "Microsoft/Xna/Framework/GamerServices/GamerCollection.hpp"
#include "Microsoft/Xna/Framework/GamerServices/InviteAcceptedEventArgs.hpp"
#include "Microsoft/Xna/Framework/Net/AvailableNetworkSession.hpp"
#include "Microsoft/Xna/Framework/Net/AvailableNetworkSessionCollection.hpp"
#include "Microsoft/Xna/Framework/Net/GameEndedEventArgs.hpp"
#include "Microsoft/Xna/Framework/Net/GameStartedEventArgs.hpp"
#include "Microsoft/Xna/Framework/Net/GamerJoinedEventArgs.hpp"
#include "Microsoft/Xna/Framework/Net/GamerLeftEventArgs.hpp"
#include "Microsoft/Xna/Framework/Net/HostChangedEventArgs.hpp"
#include "Microsoft/Xna/Framework/Net/NetworkSessionEndedEventArgs.hpp"
#include "Microsoft/Xna/Framework/Net/NetworkSessionEndReason.hpp"
#include "Microsoft/Xna/Framework/Net/NetworkSessionProperties.hpp"
#include "Microsoft/Xna/Framework/Net/NetworkSessionState.hpp"
#include "Microsoft/Xna/Framework/Net/NetworkSessionType.hpp"
#include "Microsoft/Xna/Framework/Net/SendDataOptions.hpp"
#include "Microsoft/Xna/Framework/Net/WriteLeaderboardsEventArgs.hpp"
#include "SharpRuntime/SharpRuntimeHelper.hpp"
#include "System/AsyncCallback.hpp"
#include "System/EventHandler.hpp"
#include "System/IAsyncResult.hpp"
#include "System/IDisposable.hpp"
#include "System/Object.hpp"
#include "System/Threading/EventWaitHandle.hpp"
#include "System/TimeSpan.hpp"
#include <any>
#include <memory>
#include <optional>
#include <queue>
#include <vector>

namespace Microsoft::Xna::Framework::GamerServices
{
    class SignedInGamer;
}

namespace Microsoft::Xna::Framework::Net
{
    class NetworkGamer;
    class LocalNetworkGamer;

    /**
     * @brief Manages the properties and gamers of a network gaming session.
     *
     * Ownership contract (Task 3.1/3.3; not applicable in real XNA, where the GC frees this
     * object once unreachable): `Create()`/`Find()`/`Join()`/`JoinInvited()` and their `End*`
     * counterparts all return a **caller-owned** `NetworkSession*` obtained via `new`. `Dispose()`
     * releases the session's owned resources (its ENet transport, every gamer it created) and
     * marks it disposed, matching `System::IDisposable`'s usual "release unmanaged resources, the
     * object may still be inspected afterward" contract — it deliberately does **not** `delete
     * this`, since a huge number of existing call sites (throughout this codebase's own test
     * suite, and any real caller written the same way) legitimately read state after `Dispose()`
     * (e.g. `getIsDisposedProperty()`), which a self-deleting `Dispose()` would turn into
     * use-after-free. The caller must `delete` the pointer separately once truly done with it
     * (typically right after `Dispose()`), the same way any other `new`-returned, non-reference-
     * counted C++ object would be freed.
     */
    class NetworkSession final : public System::Object, public System::IDisposable
    {
    public:
        /** @brief The maximum number of gamers supported by any session. */
        CNAEXT static constexpr int MaxSupportedGamers = 31;
        /** @brief The maximum number of previous gamers tracked by a session. */
        CNAEXT static constexpr int MaxPreviousGamers = 100;

        /**
         * @brief Identifies the kind of queued NetworkSession event.
         *
         * FNA declares this `internal`; ported as public since LocalNetworkGamer (a sibling
         * class, not a subclass) also needs to construct and inspect NetworkEvent values.
         */
        enum class NetworkEventType
        {
            /** @brief A gamer sent a data packet. */
            PacketSend,
            /** @brief A gamer joined the session. */
            GamerJoin,
            /** @brief A gamer left the session. */
            GamerLeave,
            /** @brief The session host changed. */
            HostChange,
            /** @brief The session state changed. */
            StateChange
        };

        /**
         * @brief A queued network event awaiting dispatch from Update().
         *
         * See NetworkEventType's doc comment for why this is public rather than internal.
         */
        struct NetworkEvent
        {
            /** @brief The kind of event. */
            NetworkEventType Type{NetworkEventType::PacketSend};
            /** @brief The gamer associated with the event, if any. */
            NetworkGamer* Gamer{nullptr};
            /**
             * @brief The gamer that sent a PacketSend event's payload, if any.
             *
             * Not part of FNA's original design: carries the sender through the session-level
             * event queue, since Gamer's meaning differs between the session-level queue (where
             * it names the recipient) and each gamer's own packetQueue_ (where it names the
             * sender) — see NetworkSession.cpp's Update() for how the two are reconciled.
             */
            CNAEXT NetworkGamer* Sender{nullptr};
            /** @brief The packet payload, for PacketSend events. */
            std::vector<SharpRuntime::bytecs> Packet;
            /** @brief The delivery option the packet was sent with. */
            SendDataOptions Reliable{SendDataOptions::None};
            /** @brief The new session state, for StateChange events. */
            NetworkSessionState State{NetworkSessionState::Lobby};
            /** @brief The reason the session ended, for StateChange-to-Ended events. */
            NetworkSessionEndReason Reason{NetworkSessionEndReason::Disconnected};
        };

        /**
         * @brief Gets whether this session has been disposed.
         *
         * @return true if disposed.
         */
        [[nodiscard]] bool getIsDisposedProperty() const;

        /**
         * @brief Gets every gamer (local and remote) currently in the session.
         *
         * @return Const reference to the gamer collection.
         */
        [[nodiscard]] const GamerServices::GamerCollection<NetworkGamer>& getAllGamersProperty() const;

        /**
         * @brief Gets the local gamers participating in this session.
         *
         * @return Const reference to the local gamer collection.
         */
        [[nodiscard]] const GamerServices::GamerCollection<LocalNetworkGamer>& getLocalGamersProperty() const;

        /**
         * @brief Gets the remote gamers participating in this session.
         *
         * @return Const reference to the remote gamer collection.
         */
        [[nodiscard]] const GamerServices::GamerCollection<NetworkGamer>& getRemoteGamersProperty() const;

        /**
         * @brief Gets the gamers who previously participated in this session.
         *
         * @return Const reference to the previous gamer collection.
         */
        [[nodiscard]] const GamerServices::GamerCollection<NetworkGamer>& getPreviousGamersProperty() const;

        /**
         * @brief Gets whether host migration is allowed.
         *
         * Task 5.1-5.4 (plans/plan_net.md Phase 5): real, local-only implementation - `false` (the
         * default) keeps FNA's own reference behavior exactly (`ENetBackend::HandleDisconnect`
         * unconditionally ends the session the instant its host peer disconnects). `true` enables
         * a real, full-reconnect migration instead: every surviving peer independently computes
         * the same deterministic new host (the lowest remaining wire id - no election round-trip
         * needed, since every peer already knows the roster) and either promotes itself (if it's
         * the chosen peer) or reconnects to the promoted peer via a real LAN discovery search (the
         * same mechanism `Find()` uses - a star topology gives surviving clients no direct channel
         * to each other, and the old host obviously can't relay anything either). This is not
         * FNA's own behavior (FNA's `AllowHostMigration` is a stored-but-inert auto-property, no
         * migration logic exists anywhere in its stubbed-out networking layer) - see
         * `ENetBackend.cpp`'s `AttemptHostMigration` for the full implementation, and this
         * property's own scope note: reconnecting/promoted peers get their remote-gamer roster
         * rebuilt from scratch (fresh `NetworkGamer*` identities, real `GamerLeave`+`GamerJoin`
         * events), not preserved across the migration - a full reconnect, not a seamless live
         * migration of existing sockets.
         *
         * @return true if host migration is allowed.
         */
        [[nodiscard]] bool getAllowHostMigrationProperty() const;

        /**
         * @brief Sets whether host migration is allowed.
         *
         * Implementation note: see getAllowHostMigrationProperty()'s doc comment for the real
         * migration behavior this now enables.
         *
         * @param value The new value.
         */
        void setAllowHostMigrationProperty(bool value);

        /**
         * @brief Gets whether gamers may join a session already in progress.
         *
         * @return true if join-in-progress is allowed.
         */
        [[nodiscard]] bool getAllowJoinInProgressProperty() const;

        /**
         * @brief Sets whether gamers may join a session already in progress.
         *
         * @param value The new value.
         */
        void setAllowJoinInProgressProperty(bool value);

        /**
         * @brief Gets the measured inbound bandwidth in bytes per second.
         *
         * @return The received bandwidth.
         */
        [[nodiscard]] int getBytesPerSecondReceivedProperty() const;

        /**
         * @brief Gets the measured outbound bandwidth in bytes per second.
         *
         * @return The sent bandwidth.
         */
        [[nodiscard]] int getBytesPerSecondSentProperty() const;

        /**
         * @brief Gets the current session host.
         *
         * @return Pointer to the host gamer.
         */
        [[nodiscard]] NetworkGamer* getHostProperty() const;

        /**
         * @brief Gets whether every local gamer is ready.
         *
         * @return true if all local gamers are ready.
         */
        [[nodiscard]] bool getIsEveryoneReadyProperty() const;

        /**
         * @brief Gets whether a local gamer is the session host.
         *
         * @return true if a local gamer is host.
         */
        [[nodiscard]] bool getIsHostProperty() const;

        /**
         * @brief Gets the maximum number of gamers allowed in the session.
         *
         * @return The maximum gamer count.
         */
        [[nodiscard]] int getMaxGamersProperty() const;

        /**
         * @brief Sets the maximum number of gamers allowed in the session.
         *
         * @param value The new maximum.
         */
        void setMaxGamersProperty(int value);

        /**
         * @brief Gets the number of private gamer slots.
         *
         * @return The private slot count.
         */
        [[nodiscard]] int getPrivateGamerSlotsProperty() const;

        /**
         * @brief Sets the number of private gamer slots.
         *
         * @param value The new slot count.
         */
        void setPrivateGamerSlotsProperty(int value);

        /**
         * @brief Gets the custom properties advertised for this session.
         *
         * @return Const reference to the NetworkSessionProperties.
         */
        [[nodiscard]] const NetworkSessionProperties& getSessionPropertiesProperty() const;

        /**
         * @brief Gets the current session state.
         *
         * @return The session state.
         */
        [[nodiscard]] NetworkSessionState getSessionStateProperty() const;

        /**
         * @brief Gets the session type.
         *
         * @return The session type.
         */
        [[nodiscard]] NetworkSessionType getSessionTypeProperty() const;

        /**
         * @brief Gets the artificially simulated network latency.
         *
         * Task 6.1-6.5 (plans/plan_net.md Phase 6): real, receive-side implementation - `ENetBackend`
         * holds AppData bound for one of this session's own local gamers in a per-session delayed-
         * delivery queue, releasing it once `now >= receiveTime + SimulatedLatency` (see
         * `ENetBackend.cpp`'s `HandleAppData`/`ReleaseDuePendingDeliveries`). Scoped to AppData
         * only - the CNA-internal session-management protocol (join/leave/state-change messages)
         * and a host's own relay hop for two *other* peers stay unaffected (see `HandleAppData`'s
         * own comment for why). This is not FNA's own behavior (FNA's `SimulatedLatency` is a
         * plain, inert auto-property with no delay queue anywhere in its stubbed-out source).
         *
         * @return The simulated latency.
         */
        [[nodiscard]] System::TimeSpan getSimulatedLatencyProperty() const;

        /**
         * @brief Sets the artificially simulated network latency.
         *
         * Implementation note: see getSimulatedLatencyProperty()'s doc comment for the real delay
         * behavior this now drives.
         *
         * @param value The new simulated latency.
         */
        void setSimulatedLatencyProperty(System::TimeSpan value);

        /**
         * @brief Gets the artificially simulated packet loss fraction.
         *
         * Task 6.1-6.5 (plans/plan_net.md Phase 6): real implementation - each AppData packet bound for
         * one of this session's own local gamers is probabilistically dropped before ever
         * reaching game code, at exactly this rate (`ENetBackend.cpp`'s
         * `ShouldDropForSimulatedLoss`; 0.0 and 1.0 are handled deterministically without touching
         * any RNG). Scoped to AppData only - see `getSimulatedLatencyProperty()`'s own doc comment
         * for why session-management/relay traffic stays unaffected. Not FNA's own behavior (FNA's
         * `SimulatedPacketLoss` is a plain, inert auto-property with no synthetic-drop logic
         * anywhere in its stubbed-out source).
         *
         * @return The simulated packet loss, from 0.0 to 1.0.
         */
        [[nodiscard]] float getSimulatedPacketLossProperty() const;

        /**
         * @brief Sets the artificially simulated packet loss fraction.
         *
         * Implementation note: see getSimulatedPacketLossProperty()'s doc comment for the real
         * drop behavior this now drives.
         *
         * @param value The new simulated packet loss, from 0.0 to 1.0.
         */
        void setSimulatedPacketLossProperty(float value);

        /** @brief Raised when a hosted game starts. */
        System::EventHandler<GameStartedEventArgs> GameStarted;
        /** @brief Raised when a hosted game ends. */
        System::EventHandler<GameEndedEventArgs> GameEnded;
        /**
         * @brief Raised when a gamer joins the session.
         *
         * The initial local gamer(s) established by `Create()`/`Join()`/`JoinInvited()` are
         * queued internally at construction time, not raised into this event directly (a caller
         * cannot possibly have subscribed yet at that point - the session pointer doesn't exist
         * until the static factory method returns). Real XNA's `GamerJoined` is documented to
         * replay itself immediately upon `+=` subscription for every gamer already in the session
         * - `System::EventHandler<T>` (sharp-runtime) has no such "replay on subscribe" hook, so
         * this port cannot reproduce that automatically (see `plans/plan_net.md`'s Task 12.3 for the
         * full investigation). **Call `Update()` once, immediately after subscribing, to receive
         * the initial join event(s) for this session's own local gamers** - this is the
         * intentional, permanent, correct pattern (not a temporary workaround) given the above
         * constraint; see `../cna-samples/samples/ClientServerSample`'s `HookSessionEvents()`
         * call site for a real, working example.
         */
        System::EventHandler<GamerJoinedEventArgs> GamerJoined;
        /** @brief Raised when a gamer leaves the session. */
        System::EventHandler<GamerLeftEventArgs> GamerLeft;
        /** @brief Raised when the session host changes. */
        System::EventHandler<HostChangedEventArgs> HostChanged;
        /** @brief Raised when the session ends. */
        System::EventHandler<NetworkSessionEndedEventArgs> SessionEnded;
        /** @brief Declared for API parity; never raised (leaderboards/TrueSkill unimplemented upstream). */
        System::EventHandler<WriteLeaderboardsEventArgs> WriteArbitratedLeaderboard;
        /** @brief Declared for API parity; never raised (leaderboards/TrueSkill unimplemented upstream). */
        System::EventHandler<WriteLeaderboardsEventArgs> WriteUnarbitratedLeaderboard;
        /** @brief Declared for API parity; never raised (leaderboards/TrueSkill unimplemented upstream). */
        System::EventHandler<WriteLeaderboardsEventArgs> WriteTrueSkill;

        /** @brief Declared for API parity; never raised upstream. */
        CNAEXT static System::EventHandler<GamerServices::InviteAcceptedEventArgs> InviteAccepted;

        /**
         * @brief Disposes the session, flushing queued packets on all local gamers.
         *
         * Task 3.1: also frees every `NetworkGamer`/`LocalNetworkGamer` this session ever created
         * (constructor-time locals, `AddLocalGamer`, `AddRemoteGamer`) - previously permanently
         * leaked (nothing anywhere in this codebase ever `delete`d a gamer). Freeing happens here,
         * at session teardown, rather than incrementally as gamers cycle out of
         * `PreviousGamers` — `ENetBackend`'s own per-session wire-id maps can hold the same raw
         * pointers, and those are only guaranteed torn down together with this session (via
         * `ENetBackend::TeardownSession`, called from here), not at an arbitrary earlier point in
         * the session's life.
         *
         * Task 12.1: idempotent - a second and every subsequent call is a safe no-op. All of
         * `AllGamers`/`LocalGamers`/`RemoteGamers`/`PreviousGamers` are emptied before returning,
         * so no caller can observe a dangling pointer into a gamer this call just freed, even
         * without calling `Dispose()` again.
         */
        void Dispose() override;

        /**
         * @brief Task 3.3: public so the caller (which owns every `NetworkSession*` this class's
         * static factory methods return - see the class's own doc comment for the full ownership
         * contract) can actually free it. Declared here but defined out-of-line in the .cpp, where
         * `NetworkGamer.hpp` makes `NetworkGamer` a complete type - `ownedGamers_` is a
         * `std::vector<std::unique_ptr<NetworkGamer>>`, and `NetworkGamer` is only
         * forward-declared in this header.
         */
        ~NetworkSession() override;

        /**
         * @brief CNAEXT: the number of gamer objects this session currently owns and has not yet
         * freed (see Dispose()'s doc comment). Exists purely to make Task 3.1's ownership fix
         * testable; not part of real XNA.
         *
         * @return The number of currently-owned, not-yet-freed gamer objects.
         */
        CNAEXT [[nodiscard]] std::size_t GetOwnedGamerCountForTesting() const;

        /**
         * @brief CNAEXT: how many `NetworkSessionAction` instances are currently live (`new`'d by
         * some `Begin*` call, not yet `delete`d by its `End*` counterpart). `NetworkSessionAction`
         * itself is private, so this forwards to its own `GetInstanceCountForTesting()`. Exists
         * purely to make Task 3.2's leak fix testable; not part of real XNA.
         *
         * @return The number of currently-live instances.
         */
        CNAEXT [[nodiscard]] static int GetActiveActionInstanceCountForTesting();

        /**
         * @brief CNAEXT: how many `NetworkSession` instances are currently live (`new`'d by
         * `EndCreate`/`EndFind`.../..., not yet `delete`d by their owning caller - see the class's
         * own doc comment for the ownership contract). Exists purely to make Task 3.3's documented
         * contract testable; not part of real XNA.
         *
         * @return The number of currently-live instances.
         */
        CNAEXT [[nodiscard]] static int GetInstanceCountForTesting();

        /**
         * @brief Returns the fully-qualified .NET type name of this class.
         * @return A const reference to the type name string.
         */
        CNAEXT [[nodiscard]] const std::string& GetTypeName() const override;

        /**
         * @brief Processes queued network events, raising the corresponding public events.
         */
        void Update();

        /**
         * @brief Adds a local gamer to the session.
         *
         * @param gamer The signed-in gamer to add.
         */
        void AddLocalGamer(GamerServices::SignedInGamer* gamer);

        /**
         * @brief Finds a gamer by its session-local identifier.
         *
         * @param gameId The gamer identifier to search for.
         * @return Pointer to the matching gamer, or nullptr if not found.
         */
        [[nodiscard]] NetworkGamer* FindGamerById(SharpRuntime::bytecs gameId) const;

        /**
         * @brief Resets the ready state of every gamer in the session.
         */
        void ResetReady();

        /**
         * @brief Transitions the session from the lobby to the playing state.
         */
        void StartGame();

        /**
         * @brief Transitions the session from the playing state back to the lobby.
         */
        void EndGame();

        /**
         * @brief Queues a network event for dispatch on the next Update().
         *
         * FNA declares this `internal`; ported as public since LocalNetworkGamer (a sibling
         * class, not a subclass) also needs to enqueue events from SendData.
         *
         * @param evt The event to queue.
         */
        CNAEXT void SendNetworkEvent(NetworkEvent evt);

        /**
         * @brief Adds a remote gamer to the session and queues its GamerJoin event.
         *
         * Not part of FNA's original design (FNA's Update() never populates AllGamers/
         * RemoteGamers for anyone but local gamers); used by ENetBackend when a peer's identity
         * is learned via the connected-channel handshake or a GamerJoinBroadcast.
         *
         * @param gamer The remote gamer to add. Ownership stays with the caller.
         */
        CNAEXT void AddRemoteGamer(NetworkGamer* gamer);

        /**
         * @brief Removes a gamer from the session, migrating it to PreviousGamers.
         *
         * If gamer is one of this machine's own local gamers, queues a StateChange-to-Ended
         * event instead (this machine's own view of the session is over); otherwise queues a
         * GamerLeave event for the remaining gamers.
         *
         * @param gamer The gamer to remove. Ownership stays with the caller.
         * @param reason Why the gamer is leaving; only observed when gamer is local (see above).
         */
        CNAEXT void RemoveGamer(NetworkGamer* gamer, NetworkSessionEndReason reason);

        /**
         * @brief Synchronously creates a new local network session.
         *
         * @param sessionType The type of session to create.
         * @param maxLocalGamers The maximum number of local gamers.
         * @param maxGamers The maximum number of total gamers.
         * @return The new NetworkSession.
         */
        [[nodiscard]] static NetworkSession* Create(
            NetworkSessionType sessionType,
            int maxLocalGamers,
            int maxGamers
        );

        /**
         * @brief Synchronously creates a new network session with custom properties.
         *
         * @param sessionType The type of session to create.
         * @param maxLocalGamers The maximum number of local gamers.
         * @param maxGamers The maximum number of total gamers.
         * @param privateGamerSlots The number of private gamer slots to reserve.
         * @param sessionProperties Custom properties to advertise for the session.
         * @return The new NetworkSession.
         */
        [[nodiscard]] static NetworkSession* Create(
            NetworkSessionType sessionType,
            int maxLocalGamers,
            int maxGamers,
            int privateGamerSlots,
            NetworkSessionProperties sessionProperties
        );

        /**
         * @brief Synchronously creates a new network session for an explicit set of local gamers.
         *
         * @param sessionType The type of session to create.
         * @param localGamers The local gamers to include.
         * @param maxGamers The maximum number of total gamers.
         * @param privateGamerSlots The number of private gamer slots to reserve.
         * @param sessionProperties Custom properties to advertise for the session.
         * @return The new NetworkSession.
         */
        [[nodiscard]] static NetworkSession* Create(
            NetworkSessionType sessionType,
            const std::vector<GamerServices::SignedInGamer*>& localGamers,
            int maxGamers,
            int privateGamerSlots,
            NetworkSessionProperties sessionProperties
        );

        /**
         * @brief Begins an asynchronous session creation.
         *
         * @param sessionType The type of session to create.
         * @param maxLocalGamers The maximum number of local gamers.
         * @param maxGamers The maximum number of total gamers.
         * @param callback The callback to invoke on completion.
         * @param asyncState A user-defined state object.
         * @return An IAsyncResult representing the pending operation.
         */
        [[nodiscard]] static System::IAsyncResult* BeginCreate(
            NetworkSessionType sessionType,
            int maxLocalGamers,
            int maxGamers,
            System::AsyncCallback callback,
            std::any asyncState
        );

        /**
         * @brief Begins an asynchronous session creation with custom properties.
         *
         * @param sessionType The type of session to create.
         * @param maxLocalGamers The maximum number of local gamers.
         * @param maxGamers The maximum number of total gamers.
         * @param privateGamerSlots The number of private gamer slots to reserve.
         * @param sessionProperties Custom properties to advertise for the session.
         * @param callback The callback to invoke on completion.
         * @param asyncState A user-defined state object.
         * @return An IAsyncResult representing the pending operation.
         */
        [[nodiscard]] static System::IAsyncResult* BeginCreate(
            NetworkSessionType sessionType,
            int maxLocalGamers,
            int maxGamers,
            int privateGamerSlots,
            NetworkSessionProperties sessionProperties,
            System::AsyncCallback callback,
            std::any asyncState
        );

        /**
         * @brief Begins an asynchronous session creation for an explicit set of local gamers.
         *
         * @param sessionType The type of session to create.
         * @param localGamers The local gamers to include.
         * @param maxGamers The maximum number of total gamers.
         * @param privateGamerSlots The number of private gamer slots to reserve.
         * @param sessionProperties Custom properties to advertise for the session.
         * @param callback The callback to invoke on completion.
         * @param asyncState A user-defined state object.
         * @return An IAsyncResult representing the pending operation.
         */
        [[nodiscard]] static System::IAsyncResult* BeginCreate(
            NetworkSessionType sessionType,
            const std::vector<GamerServices::SignedInGamer*>& localGamers,
            int maxGamers,
            int privateGamerSlots,
            NetworkSessionProperties sessionProperties,
            System::AsyncCallback callback,
            std::any asyncState
        );

        /**
         * @brief Completes an asynchronous session creation.
         *
         * @param result The IAsyncResult returned by BeginCreate.
         * @return The newly created NetworkSession.
         */
        [[nodiscard]] static NetworkSession* EndCreate(System::IAsyncResult* result);

        /**
         * @brief Synchronously searches for available network sessions.
         *
         * @param sessionType The type of session to search for.
         * @param maxLocalGamers The maximum number of local gamers.
         * @param searchProperties Properties to filter the search by.
         * @return The available sessions found.
         */
        [[nodiscard]] static AvailableNetworkSessionCollection Find(
            NetworkSessionType sessionType,
            int maxLocalGamers,
            NetworkSessionProperties searchProperties
        );

        /**
         * @brief Synchronously searches for available network sessions for an explicit set of local gamers.
         *
         * @param sessionType The type of session to search for.
         * @param localGamers The local gamers searching.
         * @param searchProperties Properties to filter the search by.
         * @return The available sessions found.
         */
        [[nodiscard]] static AvailableNetworkSessionCollection Find(
            NetworkSessionType sessionType,
            const std::vector<GamerServices::SignedInGamer*>& localGamers,
            NetworkSessionProperties searchProperties
        );

        /**
         * @brief Begins an asynchronous search for available network sessions.
         *
         * @param sessionType The type of session to search for.
         * @param maxLocalGamers The maximum number of local gamers.
         * @param searchProperties Properties to filter the search by.
         * @param callback The callback to invoke on completion.
         * @param asyncState A user-defined state object.
         * @return An IAsyncResult representing the pending operation.
         */
        [[nodiscard]] static System::IAsyncResult* BeginFind(
            NetworkSessionType sessionType,
            int maxLocalGamers,
            NetworkSessionProperties searchProperties,
            System::AsyncCallback callback,
            std::any asyncState
        );

        /**
         * @brief Begins an asynchronous search for an explicit set of local gamers.
         *
         * @param sessionType The type of session to search for.
         * @param localGamers The local gamers searching.
         * @param searchProperties Properties to filter the search by.
         * @param callback The callback to invoke on completion.
         * @param asyncState A user-defined state object.
         * @return An IAsyncResult representing the pending operation.
         */
        [[nodiscard]] static System::IAsyncResult* BeginFind(
            NetworkSessionType sessionType,
            const std::vector<GamerServices::SignedInGamer*>& localGamers,
            NetworkSessionProperties searchProperties,
            System::AsyncCallback callback,
            std::any asyncState
        );

        /**
         * @brief Completes an asynchronous session search.
         *
         * @param result The IAsyncResult returned by BeginFind.
         * @return The available sessions found (always empty; matches FNA's stub).
         */
        [[nodiscard]] static AvailableNetworkSessionCollection EndFind(System::IAsyncResult* result);

        /**
         * @brief Synchronously joins an available network session.
         *
         * @param availableSession The session to join.
         * @return The joined NetworkSession.
         */
        [[nodiscard]] static NetworkSession* Join(const AvailableNetworkSession* availableSession);

        /**
         * @brief Begins an asynchronous join of an available network session.
         *
         * @param availableSession The session to join.
         * @param callback The callback to invoke on completion.
         * @param asyncState A user-defined state object.
         * @return An IAsyncResult representing the pending operation.
         */
        [[nodiscard]] static System::IAsyncResult* BeginJoin(
            const AvailableNetworkSession* availableSession,
            System::AsyncCallback callback,
            std::any asyncState
        );

        /**
         * @brief Completes an asynchronous join.
         *
         * @param result The IAsyncResult returned by BeginJoin.
         * @return The joined NetworkSession.
         */
        [[nodiscard]] static NetworkSession* EndJoin(System::IAsyncResult* result);

        /**
         * @brief Synchronously joins a session the local gamer was invited to.
         *
         * @param maxLocalGamers The maximum number of local gamers.
         * @return The joined NetworkSession.
         */
        [[nodiscard]] static NetworkSession* JoinInvited(int maxLocalGamers);

        /**
         * @brief Synchronously joins a session an explicit set of local gamers was invited to.
         *
         * @param localGamers The local gamers joining.
         * @return The joined NetworkSession.
         */
        [[nodiscard]] static NetworkSession* JoinInvited(const std::vector<GamerServices::SignedInGamer*>& localGamers);

        /**
         * @brief Begins an asynchronous join of an invited session.
         *
         * @param maxLocalGamers The maximum number of local gamers.
         * @param callback The callback to invoke on completion.
         * @param asyncState A user-defined state object.
         * @return An IAsyncResult representing the pending operation.
         */
        [[nodiscard]] static System::IAsyncResult* BeginJoinInvited(
            int maxLocalGamers,
            System::AsyncCallback callback,
            std::any asyncState
        );

        /**
         * @brief Begins an asynchronous join of an invited session for an explicit set of local gamers.
         *
         * @param localGamers The local gamers joining.
         * @param callback The callback to invoke on completion.
         * @param asyncState A user-defined state object.
         * @return An IAsyncResult representing the pending operation.
         */
        [[nodiscard]] static System::IAsyncResult* BeginJoinInvited(
            const std::vector<GamerServices::SignedInGamer*>& localGamers,
            System::AsyncCallback callback,
            std::any asyncState
        );

        /**
         * @brief Completes an asynchronous invited-session join.
         *
         * @param result The IAsyncResult returned by BeginJoinInvited.
         * @return The joined NetworkSession.
         */
        [[nodiscard]] static NetworkSession* EndJoinInvited(System::IAsyncResult* result);

    private:
        /**
         * @brief Internal IAsyncResult implementation backing NetworkSession's Begin/End pairs.
         */
        class NetworkSessionAction : public System::IAsyncResult
        {
        public:
            /**
             * @brief Constructs a NetworkSessionAction already positioned to complete.
             *
             * @param state The user-defined async state.
             * @param callback The callback to invoke on completion.
             * @param maxLocal The maximum number of local gamers for this action.
             * @param localGamers The explicit local gamers for this action, if any.
             * @param maxPrivateSlots The maximum number of private gamer slots.
             * @param properties The session properties to search/create with.
             * @param type The NetworkSessionType this action operates on.
             * @param maxGamers The maximum gamer count retained for a create action.
             */
            NetworkSessionAction(
                std::any state,
                System::AsyncCallback callback,
                int maxLocal,
                std::optional<std::vector<GamerServices::SignedInGamer*>> localGamers,
                int maxPrivateSlots,
                NetworkSessionProperties properties,
                NetworkSessionType type,
                int maxGamers = MaxSupportedGamers
            );

            /**
             * @brief Task 3.2: decrements the live-instance counter `GetInstanceCountForTesting()`
             * reports, so a leaked `NetworkSessionAction` (one `new`'d by a `Begin*` call but never
             * `delete`d by its `End*` counterpart) is observable.
             */
            ~NetworkSessionAction() override;

            /**
             * @brief CNAEXT: how many `NetworkSessionAction` instances are currently live (`new`'d
             * by some `Begin*` call, not yet `delete`d by its `End*` counterpart). Exists purely to
             * make Task 3.2's leak fix testable; not part of real XNA.
             *
             * @return The number of currently-live instances.
             */
            CNAEXT static int GetInstanceCountForTesting();

            /** @brief Gets the user-defined state supplied to the Begin* call. */
            [[nodiscard]] const std::any& getAsyncStateProperty() const override;
            /** @brief Always false; this stub never completes synchronously. */
            [[nodiscard]] bool getCompletedSynchronouslyProperty() const override;
            /** @brief Gets whether the asynchronous operation has completed. */
            [[nodiscard]] bool getIsCompletedProperty() const override;
            /**
             * @brief Sets whether the asynchronous operation has completed.
             *
             * @param value The new completion state.
             */
            void setIsCompletedProperty(bool value);
            /** @brief Gets the wait handle signalled when the operation completes. */
            [[nodiscard]] System::Threading::WaitHandle& getAsyncWaitHandleProperty() const override;

            const System::AsyncCallback Callback;
            const int MaxLocalGamers;
            const int MaxGamers;
            const std::optional<std::vector<GamerServices::SignedInGamer*>> LocalGamers;
            const int MaxPrivateSlots;
            const NetworkSessionProperties SessionProperties;
            const NetworkSessionType SessionType;

        private:
            std::any asyncState_;
            bool isCompleted_{false};

            // Mutable: IAsyncResult::getAsyncWaitHandleProperty() is const but returns a
            // non-const WaitHandle&, so the handle exposed through it must be mutable.
            mutable System::Threading::EventWaitHandle asyncWaitHandle_;

            CNAEXT static int instanceCount_;
        };

        explicit NetworkSession(
            NetworkSessionProperties properties,
            NetworkSessionType type,
            int maxGamers,
            int privateGamerSlots,
            int maxLocal,
            std::optional<std::vector<GamerServices::SignedInGamer*>> localGamers,
            // Not part of FNA's original constructor signature (see DEFERRED.md item #20 in the
            // sibling cna-samples repo): true from EndCreate (this machine is hosting), false from
            // EndJoin/EndJoinInvited (this machine is joining someone else's session). Drives every
            // local gamer's real NetworkGamer::SetIsHost() instead of FNA's hardcoded-true stub.
            bool isHost
        );

        bool isDisposed_{false};
        GamerServices::GamerCollection<NetworkGamer> allGamers_;
        GamerServices::GamerCollection<LocalNetworkGamer> localGamers_;
        GamerServices::GamerCollection<NetworkGamer> remoteGamers_;
        GamerServices::GamerCollection<NetworkGamer> previousGamers_;
        bool allowHostMigration_{false};
        bool allowJoinInProgress_{false};
        int bytesPerSecondReceived_{0};
        int bytesPerSecondSent_{0};
        NetworkGamer* host_{nullptr};
        bool isHost_{true};
        int maxGamers_{0};
        int privateGamerSlots_{0};
        NetworkSessionProperties sessionProperties_;
        NetworkSessionState sessionState_{NetworkSessionState::Lobby};
        NetworkSessionType sessionType_{NetworkSessionType::Local};
        System::TimeSpan simulatedLatency_;
        float simulatedPacketLoss_{0.0f};

        int maxLocalGamers_{0};
        // Task 2.4: a real monotonic counter for locally-assigned placeholder ids, separate from
        // any live collection's size. AddLocalGamer used to derive its new gamer's id from
        // allGamers_.getCountProperty() at call time - since RemoveGamer shrinks that count with
        // no separate counter, a remove-then-add sequence could hand out a colliding id already
        // owned by a still-present gamer, corrupting FindGamerById.
        CNAEXT SharpRuntime::bytecs nextLocalGamerId_{0};
        std::queue<NetworkEvent> networkEvents_;

        // Task 3.1/10.2: owns every gamer this session ever created - GamerCollection<T>'s own
        // views only ever hold non-owning raw pointers (see its doc comment for the full,
        // canonical ownership contract this follows). Freed in bulk on Dispose() - see Dispose()'s
        // own doc comment for why not incrementally.
        CNAEXT std::vector<std::unique_ptr<NetworkGamer>> ownedGamers_;

        static NetworkSessionAction* activeAction_;
        static NetworkSession* activeSession_;

        // Task 12: audit_net.md High finding - every Begin* overload used to leave activeAction_'s
        // Callback stored but never invoked. Every Begin* already fully completes activeAction_
        // synchronously (see NetworkSessionAction's constructor comment), so the callback is
        // invoked once, right here, immediately after activeAction_ is assigned - never from
        // inside NetworkSessionAction's own constructor, so a re-entrant callback (one that itself
        // calls back into NetworkSession, e.g. a fresh Begin*/End*) always observes activeAction_
        // already installed, never null/stale. Returns the action captured *before* invoking the
        // callback, not activeAction_ read again afterward - a re-entrant callback that calls the
        // matching End* nulls activeAction_ as a side effect, and the Begin* caller must still get
        // back the real action it just created, not that now-stale null.
        CNAEXT static NetworkSessionAction* InvokeActiveActionCallback();

        // Task 2.15: the connect address/port BeginJoin captured from its AvailableNetworkSession
        // argument, consumed by EndJoin to actually call ENetBackend::ConnectToHost once the
        // joined session exists - NetworkSessionAction (shared by every Begin*/End* pair) has no
        // room for these without also touching Create/Find/JoinInvited's own call sites, so they
        // get the same single-pending-action static treatment as activeAction_/activeSession_
        // above rather than growing that shared class for just one caller.
        CNAEXT static std::string pendingJoinAddress_;
        CNAEXT static uint16_t pendingJoinPort_;

        // Task 3.3: incremented by the constructor, decremented by ~NetworkSession() - see
        // GetInstanceCountForTesting()'s own doc comment.
        CNAEXT static int instanceCount_;
    };
}
