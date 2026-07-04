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
#include "System/Threading/EventWaitHandle.hpp"
#include "System/TimeSpan.hpp"
#include <any>
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
     */
    class NetworkSession final : public System::IDisposable
    {
    public:
        /** @brief The maximum number of gamers supported by any session. */
        NOXNA static constexpr int MaxSupportedGamers = 31;
        /** @brief The maximum number of previous gamers tracked by a session. */
        NOXNA static constexpr int MaxPreviousGamers = 100;

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
            NOXNA NetworkGamer* Sender{nullptr};
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
         * @return true if host migration is allowed.
         */
        [[nodiscard]] bool getAllowHostMigrationProperty() const;

        /**
         * @brief Sets whether host migration is allowed.
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
         * @return The simulated latency.
         */
        [[nodiscard]] System::TimeSpan getSimulatedLatencyProperty() const;

        /**
         * @brief Sets the artificially simulated network latency.
         *
         * @param value The new simulated latency.
         */
        void setSimulatedLatencyProperty(System::TimeSpan value);

        /**
         * @brief Gets the artificially simulated packet loss fraction.
         *
         * @return The simulated packet loss, from 0.0 to 1.0.
         */
        [[nodiscard]] float getSimulatedPacketLossProperty() const;

        /**
         * @brief Sets the artificially simulated packet loss fraction.
         *
         * @param value The new simulated packet loss, from 0.0 to 1.0.
         */
        void setSimulatedPacketLossProperty(float value);

        /** @brief Raised when a hosted game starts. */
        System::EventHandler<GameStartedEventArgs> GameStarted;
        /** @brief Raised when a hosted game ends. */
        System::EventHandler<GameEndedEventArgs> GameEnded;
        /** @brief Raised when a gamer joins the session. */
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
        NOXNA static System::EventHandler<GamerServices::InviteAcceptedEventArgs> InviteAccepted;

        /**
         * @brief Disposes the session, flushing queued packets on all local gamers.
         */
        void Dispose() override;

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
        NOXNA void SendNetworkEvent(NetworkEvent evt);

        /**
         * @brief Adds a remote gamer to the session and queues its GamerJoin event.
         *
         * Not part of FNA's original design (FNA's Update() never populates AllGamers/
         * RemoteGamers for anyone but local gamers); used by ENetBackend when a peer's identity
         * is learned via the connected-channel handshake or a GamerJoinBroadcast.
         *
         * @param gamer The remote gamer to add. Ownership stays with the caller.
         */
        NOXNA void AddRemoteGamer(NetworkGamer* gamer);

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
        NOXNA void RemoveGamer(NetworkGamer* gamer, NetworkSessionEndReason reason);

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
             */
            NetworkSessionAction(
                std::any state,
                System::AsyncCallback callback,
                int maxLocal,
                std::optional<std::vector<GamerServices::SignedInGamer*>> localGamers,
                int maxPrivateSlots,
                NetworkSessionProperties properties,
                NetworkSessionType type
            );

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
        };

        explicit NetworkSession(
            NetworkSessionProperties properties,
            NetworkSessionType type,
            int maxGamers,
            int privateGamerSlots,
            int maxLocal,
            std::optional<std::vector<GamerServices::SignedInGamer*>> localGamers
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
        int maxGamers_{0};
        int privateGamerSlots_{0};
        NetworkSessionProperties sessionProperties_;
        NetworkSessionState sessionState_{NetworkSessionState::Lobby};
        NetworkSessionType sessionType_{NetworkSessionType::Local};
        System::TimeSpan simulatedLatency_;
        float simulatedPacketLoss_{0.0f};

        int maxLocalGamers_{0};
        std::queue<NetworkEvent> networkEvents_;

        static NetworkSessionAction* activeAction_;
        static NetworkSession* activeSession_;
    };
}
