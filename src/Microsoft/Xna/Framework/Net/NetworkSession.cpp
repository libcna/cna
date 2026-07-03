// SPDX-License-Identifier: MS-PL
#include "Microsoft/Xna/Framework/Net/NetworkSession.hpp"
#include "CNA/Internal/Net/ENetBackend.hpp"
#include "Microsoft/Xna/Framework/GamerServices/Gamer.hpp"
#include "Microsoft/Xna/Framework/GamerServices/GamerServicesDispatcher.hpp"
#include "Microsoft/Xna/Framework/GamerServices/SignedInGamer.hpp"
#include "Microsoft/Xna/Framework/GamerServices/SignedInGamerCollection.hpp"
#include "Microsoft/Xna/Framework/Net/LocalNetworkGamer.hpp"
#include "Microsoft/Xna/Framework/Net/NetworkGamer.hpp"
#include "System/ArgumentException.hpp"
#include "System/ArgumentNullException.hpp"
#include "System/ArgumentOutOfRangeException.hpp"
#include "System/InvalidOperationException.hpp"
#include "System/ObjectDisposedException.hpp"

namespace Microsoft::Xna::Framework::Net
{
    using GamerServices::SignedInGamer;

    NetworkSession::NetworkSessionAction* NetworkSession::activeAction_ = nullptr;
    NetworkSession* NetworkSession::activeSession_ = nullptr;
    System::EventHandler<GamerServices::InviteAcceptedEventArgs> NetworkSession::InviteAccepted;

    // --- NetworkSessionAction ---

    NetworkSession::NetworkSessionAction::NetworkSessionAction(
        std::any state,
        System::AsyncCallback callback,
        int maxLocal,
        std::optional<std::vector<SignedInGamer*>> localGamers,
        int maxPrivateSlots,
        NetworkSessionProperties properties,
        NetworkSessionType type
    )
        : Callback(std::move(callback))
        , MaxLocalGamers(maxLocal)
        , LocalGamers(std::move(localGamers))
        , MaxPrivateSlots(maxPrivateSlots)
        , SessionProperties(std::move(properties))
        , SessionType(type)
        , asyncState_(std::move(state))
        , asyncWaitHandle_(true, System::Threading::EventResetMode::ManualReset)
    {
    }

    const std::any& NetworkSession::NetworkSessionAction::getAsyncStateProperty() const { return asyncState_; }
    bool NetworkSession::NetworkSessionAction::getCompletedSynchronouslyProperty() const { return false; }
    bool NetworkSession::NetworkSessionAction::getIsCompletedProperty() const { return isCompleted_; }
    void NetworkSession::NetworkSessionAction::setIsCompletedProperty(bool value) { isCompleted_ = value; }

    System::Threading::WaitHandle& NetworkSession::NetworkSessionAction::getAsyncWaitHandleProperty() const
    {
        return asyncWaitHandle_;
    }

    // --- Constructor ---

    NetworkSession::NetworkSession(
        NetworkSessionProperties properties,
        NetworkSessionType type,
        int maxGamers,
        int privateGamerSlots,
        int maxLocal,
        std::optional<std::vector<SignedInGamer*>> localGamers
    )
        : allGamers_(GamerServices::GamerCollection<NetworkGamer>::CreateInternal({}))
        , localGamers_(GamerServices::GamerCollection<LocalNetworkGamer>::CreateInternal({}))
        , remoteGamers_(GamerServices::GamerCollection<NetworkGamer>::CreateInternal({}))
        , previousGamers_(GamerServices::GamerCollection<NetworkGamer>::CreateInternal({}))
        , maxGamers_(maxGamers)
        , privateGamerSlots_(privateGamerSlots)
        , sessionProperties_(std::move(properties))
        , sessionType_(type)
    {
        std::vector<LocalNetworkGamer*> locals;
        if (!localGamers.has_value())
        {
            maxLocalGamers_ = maxLocal;
            auto* signedIn = GamerServices::Gamer::getSignedInGamersProperty();
            for (int i = 0; i < signedIn->getCountProperty() && i < maxLocalGamers_; ++i)
            {
                SignedInGamer* g = (*signedIn)[i];
                if (!g->getIsGuestProperty())
                {
                    locals.push_back(new LocalNetworkGamer(LocalNetworkGamer::CreateInternal(g, this)));
                }
            }
        }
        else
        {
            maxLocalGamers_ = 0;
            for (SignedInGamer* gamer : *localGamers)
            {
                locals.push_back(new LocalNetworkGamer(LocalNetworkGamer::CreateInternal(gamer, this)));
                ++maxLocalGamers_;
            }
        }
        for (LocalNetworkGamer* l : locals) localGamers_.Add(l);

        // RemoteGamers stays empty: FNA's constructor never populates it (matches upstream, not a gap here).
        for (LocalNetworkGamer* l : locals) allGamers_.Add(l);

        host_ = localGamers_[0];

        if (getIsHostProperty())
        {
            allowHostMigration_ = false;
            allowJoinInProgress_ = false;
            sessionState_ = NetworkSessionState::Lobby;
        }

        for (NetworkGamer* gamer : allGamers_)
        {
            NetworkEvent evt;
            evt.Type = NetworkEventType::GamerJoin;
            evt.Gamer = gamer;
            SendNetworkEvent(std::move(evt));
        }

        simulatedLatency_ = System::TimeSpan::Zero;
        simulatedPacketLoss_ = 0.0f;
        isDisposed_ = false;

        bytesPerSecondReceived_ = 0;
        bytesPerSecondSent_ = 0;

        if (CNA::Internal::Net::ENetBackend::RealNetworkingEnabled(sessionType_))
        {
            CNA::Internal::Net::ENetBackend::StartHosting(this);
        }
    }

    // --- Properties ---

    bool NetworkSession::getIsDisposedProperty() const { return isDisposed_; }

    const GamerServices::GamerCollection<NetworkGamer>& NetworkSession::getAllGamersProperty() const { return allGamers_; }
    const GamerServices::GamerCollection<LocalNetworkGamer>& NetworkSession::getLocalGamersProperty() const { return localGamers_; }
    const GamerServices::GamerCollection<NetworkGamer>& NetworkSession::getRemoteGamersProperty() const { return remoteGamers_; }
    const GamerServices::GamerCollection<NetworkGamer>& NetworkSession::getPreviousGamersProperty() const { return previousGamers_; }

    bool NetworkSession::getAllowHostMigrationProperty() const { return allowHostMigration_; }
    void NetworkSession::setAllowHostMigrationProperty(bool value) { allowHostMigration_ = value; }

    bool NetworkSession::getAllowJoinInProgressProperty() const { return allowJoinInProgress_; }
    void NetworkSession::setAllowJoinInProgressProperty(bool value) { allowJoinInProgress_ = value; }

    int NetworkSession::getBytesPerSecondReceivedProperty() const { return bytesPerSecondReceived_; }
    int NetworkSession::getBytesPerSecondSentProperty() const { return bytesPerSecondSent_; }

    NetworkGamer* NetworkSession::getHostProperty() const { return host_; }

    bool NetworkSession::getIsEveryoneReadyProperty() const
    {
        for (LocalNetworkGamer* gamer : localGamers_)
        {
            if (!gamer->getIsReadyProperty()) return false;
        }
        return true;
    }

    bool NetworkSession::getIsHostProperty() const
    {
        for (LocalNetworkGamer* gamer : localGamers_)
        {
            if (gamer->getIsHostProperty()) return true;
        }
        return false;
    }

    int NetworkSession::getMaxGamersProperty() const { return maxGamers_; }
    void NetworkSession::setMaxGamersProperty(int value) { maxGamers_ = value; }

    int NetworkSession::getPrivateGamerSlotsProperty() const { return privateGamerSlots_; }
    void NetworkSession::setPrivateGamerSlotsProperty(int value) { privateGamerSlots_ = value; }

    const NetworkSessionProperties& NetworkSession::getSessionPropertiesProperty() const { return sessionProperties_; }
    NetworkSessionState NetworkSession::getSessionStateProperty() const { return sessionState_; }
    NetworkSessionType NetworkSession::getSessionTypeProperty() const { return sessionType_; }

    System::TimeSpan NetworkSession::getSimulatedLatencyProperty() const { return simulatedLatency_; }
    void NetworkSession::setSimulatedLatencyProperty(System::TimeSpan value) { simulatedLatency_ = value; }

    float NetworkSession::getSimulatedPacketLossProperty() const { return simulatedPacketLoss_; }
    void NetworkSession::setSimulatedPacketLossProperty(float value) { simulatedPacketLoss_ = value; }

    // --- Public methods ---

    void NetworkSession::Dispose()
    {
        for (LocalNetworkGamer* gamer : localGamers_)
        {
            gamer->ClearPacketQueue();
        }
        if (CNA::Internal::Net::ENetBackend::RealNetworkingEnabled(sessionType_))
        {
            CNA::Internal::Net::ENetBackend::TeardownSession(this);
        }
        activeSession_ = nullptr;
        isDisposed_ = true;
    }

    void NetworkSession::Update()
    {
        if (isDisposed_)
        {
            throw System::ObjectDisposedException("this");
        }

        if (CNA::Internal::Net::ENetBackend::RealNetworkingEnabled(sessionType_))
        {
            CNA::Internal::Net::ENetBackend::PumpSession(this);
        }

        while (!networkEvents_.empty())
        {
            NetworkEvent evt = std::move(networkEvents_.front());
            networkEvents_.pop();

            if (evt.Type == NetworkEventType::PacketSend)
            {
                // Gated behind RealNetworkingEnabled so non-SystemLink session types keep their
                // pre-Phase-5 behavior byte-for-byte: PacketSend stays a complete no-op for them
                // (see Task 5.9's planned regression pass), matching that no remote gamer can
                // exist on a non-SystemLink session anyway (AddRemoteGamer is only ever called
                // from ENetBackend's own RealNetworkingEnabled-gated handshake code).
                if (CNA::Internal::Net::ENetBackend::RealNetworkingEnabled(sessionType_))
                {
                    if (auto* localTarget = dynamic_cast<LocalNetworkGamer*>(evt.Gamer))
                    {
                        // Target is local to this machine (whether the packet originated here
                        // too, or arrived via the real ENet transport for one of our own
                        // gamers) — deliver directly into its own packetQueue_. ReceiveData
                        // matches its result's Gamer field against the SENDER, not the target
                        // (see NetworkEvent::Sender's doc comment), so remap here.
                        NetworkEvent delivered = evt;
                        delivered.Gamer = evt.Sender;
                        localTarget->EnqueuePacket(std::move(delivered));
                    }
                    else if (evt.Gamer != nullptr)
                    {
                        // Target is remote: transmit over ENet (the host relays if it isn't
                        // itself the owner of the target gamer's connection).
                        CNA::Internal::Net::ENetBackend::SendAppData(this, evt.Sender, evt.Gamer, evt.Packet, evt.Reliable);
                    }
                }
            }
            else if (evt.Type == NetworkEventType::GamerJoin)
            {
                GamerJoined.Raise(nullptr, GamerJoinedEventArgs(evt.Gamer));
            }
            else if (evt.Type == NetworkEventType::GamerLeave)
            {
                GamerLeft.Raise(nullptr, GamerLeftEventArgs(evt.Gamer));
            }
            else if (evt.Type == NetworkEventType::HostChange)
            {
                HostChanged.Raise(nullptr, HostChangedEventArgs(host_, evt.Gamer));
                host_ = evt.Gamer;
            }
            else // NetworkEventType::StateChange
            {
                if (evt.State == NetworkSessionState::Playing)
                {
                    GameStarted.Raise(nullptr, GameStartedEventArgs());
                }
                else if (evt.State == NetworkSessionState::Lobby)
                {
                    GameEnded.Raise(nullptr, GameEndedEventArgs());
                }
                else
                {
                    SessionEnded.Raise(nullptr, NetworkSessionEndedEventArgs(evt.Reason));
                }
                sessionState_ = evt.State;
            }
        }
    }

    void NetworkSession::AddLocalGamer(SignedInGamer* gamer)
    {
        if (localGamers_.getCountProperty() == maxLocalGamers_)
        {
            throw System::InvalidOperationException("LocalGamer max limit!");
        }
        auto* adding = new LocalNetworkGamer(LocalNetworkGamer::CreateInternal(gamer, this));
        localGamers_.Add(adding);
        allGamers_.Add(adding);
    }

    NetworkGamer* NetworkSession::FindGamerById(SharpRuntime::bytecs gameId) const
    {
        for (NetworkGamer* g : allGamers_)
        {
            if (g->getIdProperty() == gameId) return g;
        }
        return nullptr;
    }

    void NetworkSession::ResetReady()
    {
        if (isDisposed_) throw System::ObjectDisposedException("this");
        if (!getIsHostProperty()) throw System::InvalidOperationException("This NetworkSession is not the host");

        for (NetworkGamer* gamer : allGamers_)
        {
            gamer->setIsReadyProperty(false);
        }
    }

    void NetworkSession::StartGame()
    {
        if (isDisposed_) throw System::ObjectDisposedException("this");
        if (!getIsHostProperty()) throw System::InvalidOperationException("This NetworkSession is not the host");
        if (sessionState_ != NetworkSessionState::Lobby) throw System::InvalidOperationException("NetworkSession is not Lobby");

        NetworkEvent evt;
        evt.Type = NetworkEventType::StateChange;
        evt.State = NetworkSessionState::Playing;
        SendNetworkEvent(std::move(evt));

        if (CNA::Internal::Net::ENetBackend::RealNetworkingEnabled(sessionType_))
        {
            CNA::Internal::Net::ENetBackend::BroadcastStateChange(this, NetworkSessionState::Playing);
        }
    }

    void NetworkSession::EndGame()
    {
        if (isDisposed_) throw System::ObjectDisposedException("this");
        if (!getIsHostProperty()) throw System::InvalidOperationException("This NetworkSession is not the host");
        if (sessionState_ != NetworkSessionState::Playing) throw System::InvalidOperationException("NetworkSession is not Playing");

        NetworkEvent evt;
        evt.Type = NetworkEventType::StateChange;
        evt.State = NetworkSessionState::Lobby;
        SendNetworkEvent(std::move(evt));

        if (CNA::Internal::Net::ENetBackend::RealNetworkingEnabled(sessionType_))
        {
            CNA::Internal::Net::ENetBackend::BroadcastStateChange(this, NetworkSessionState::Lobby);
        }
    }

    void NetworkSession::SendNetworkEvent(NetworkEvent evt)
    {
        networkEvents_.push(std::move(evt));
    }

    void NetworkSession::AddRemoteGamer(NetworkGamer* gamer)
    {
        remoteGamers_.Add(gamer);
        allGamers_.Add(gamer);

        NetworkEvent evt;
        evt.Type = NetworkEventType::GamerJoin;
        evt.Gamer = gamer;
        SendNetworkEvent(std::move(evt));
    }

    void NetworkSession::RemoveGamer(NetworkGamer* gamer, NetworkSessionEndReason reason)
    {
        bool isLocal = false;
        for (LocalNetworkGamer* local : localGamers_)
        {
            if (local == gamer)
            {
                isLocal = true;
                break;
            }
        }

        gamer->SetHasLeftSession(true);
        remoteGamers_.Remove(gamer);
        allGamers_.Remove(gamer);

        // Not part of FNA's original design (no prior real implementation exists to match):
        // evict oldest-first once the tracked history exceeds MaxPreviousGamers.
        previousGamers_.Add(gamer);
        while (previousGamers_.getCountProperty() > MaxPreviousGamers)
        {
            previousGamers_.Remove(previousGamers_[0]);
        }

        if (isLocal)
        {
            NetworkEvent evt;
            evt.Type = NetworkEventType::StateChange;
            evt.State = NetworkSessionState::Ended;
            evt.Reason = reason;
            SendNetworkEvent(std::move(evt));
        }
        else
        {
            NetworkEvent evt;
            evt.Type = NetworkEventType::GamerLeave;
            evt.Gamer = gamer;
            SendNetworkEvent(std::move(evt));
        }
    }

    // --- Static Create methods ---

    NetworkSession* NetworkSession::Create(NetworkSessionType sessionType, int maxLocalGamers, int maxGamers)
    {
        System::IAsyncResult* result = BeginCreate(sessionType, maxLocalGamers, maxGamers, System::AsyncCallback{}, std::any{});
        while (!result->getIsCompletedProperty())
        {
            if (!GamerServices::GamerServicesDispatcher::UpdateAsync())
            {
                activeAction_->setIsCompletedProperty(true);
            }
        }
        return EndCreate(result);
    }

    NetworkSession* NetworkSession::Create(
        NetworkSessionType sessionType,
        int maxLocalGamers,
        int maxGamers,
        int privateGamerSlots,
        NetworkSessionProperties sessionProperties
    )
    {
        System::IAsyncResult* result = BeginCreate(
            sessionType, maxLocalGamers, maxGamers, privateGamerSlots,
            std::move(sessionProperties), System::AsyncCallback{}, std::any{}
        );
        while (!result->getIsCompletedProperty())
        {
            if (!GamerServices::GamerServicesDispatcher::UpdateAsync())
            {
                activeAction_->setIsCompletedProperty(true);
            }
        }
        return EndCreate(result);
    }

    NetworkSession* NetworkSession::Create(
        NetworkSessionType sessionType,
        const std::vector<SignedInGamer*>& localGamers,
        int maxGamers,
        int privateGamerSlots,
        NetworkSessionProperties sessionProperties
    )
    {
        System::IAsyncResult* result = BeginCreate(
            sessionType, localGamers, maxGamers, privateGamerSlots,
            std::move(sessionProperties), System::AsyncCallback{}, std::any{}
        );
        while (!result->getIsCompletedProperty())
        {
            if (!GamerServices::GamerServicesDispatcher::UpdateAsync())
            {
                activeAction_->setIsCompletedProperty(true);
            }
        }
        return EndCreate(result);
    }

    System::IAsyncResult* NetworkSession::BeginCreate(
        NetworkSessionType sessionType,
        int maxLocalGamers,
        int /*maxGamers*/, // FNA never uses this parameter in this overload's body; preserved as-is.
        System::AsyncCallback callback,
        std::any asyncState
    )
    {
        if (maxLocalGamers < 1 || maxLocalGamers > 4)
        {
            throw System::ArgumentOutOfRangeException("maxLocalGamers");
        }
        if (activeAction_ != nullptr || activeSession_ != nullptr)
        {
            throw System::InvalidOperationException();
        }

        activeAction_ = new NetworkSessionAction(
            std::move(asyncState), std::move(callback), maxLocalGamers, std::nullopt, 0,
            NetworkSessionProperties{}, sessionType
        );
        return activeAction_;
    }

    System::IAsyncResult* NetworkSession::BeginCreate(
        NetworkSessionType sessionType,
        int maxLocalGamers,
        int maxGamers,
        int privateGamerSlots,
        NetworkSessionProperties sessionProperties,
        System::AsyncCallback callback,
        std::any asyncState
    )
    {
        if (maxLocalGamers < 1 || maxLocalGamers > 4)
        {
            throw System::ArgumentOutOfRangeException("maxLocalGamers");
        }
        if (privateGamerSlots < 0 || privateGamerSlots > maxGamers)
        {
            throw System::ArgumentOutOfRangeException("privateGamerSlots");
        }
        if (activeAction_ != nullptr || activeSession_ != nullptr)
        {
            throw System::InvalidOperationException();
        }

        activeAction_ = new NetworkSessionAction(
            std::move(asyncState), std::move(callback), maxLocalGamers, std::nullopt, privateGamerSlots,
            std::move(sessionProperties), sessionType
        );
        return activeAction_;
    }

    System::IAsyncResult* NetworkSession::BeginCreate(
        NetworkSessionType sessionType,
        const std::vector<SignedInGamer*>& localGamers,
        int maxGamers,
        int privateGamerSlots,
        NetworkSessionProperties sessionProperties,
        System::AsyncCallback callback,
        std::any asyncState
    )
    {
        if (privateGamerSlots < 0 || privateGamerSlots > maxGamers)
        {
            throw System::ArgumentOutOfRangeException("privateGamerSlots");
        }
        if (activeAction_ != nullptr || activeSession_ != nullptr)
        {
            throw System::InvalidOperationException();
        }

        activeAction_ = new NetworkSessionAction(
            std::move(asyncState), std::move(callback), 0, localGamers, privateGamerSlots,
            std::move(sessionProperties), sessionType
        );
        return activeAction_;
    }

    NetworkSession* NetworkSession::EndCreate(System::IAsyncResult* result)
    {
        if (result != activeAction_)
        {
            throw System::ArgumentException("result");
        }

        // FNA hardcodes 69 here instead of forwarding the caller's original maxGamers argument
        // (which BeginCreate never even stored) — preserved as-is.
        activeSession_ = new NetworkSession(
            activeAction_->SessionProperties,
            activeAction_->SessionType,
            69,
            activeAction_->MaxPrivateSlots,
            activeAction_->MaxLocalGamers,
            activeAction_->LocalGamers
        );

        activeAction_ = nullptr;
        return activeSession_;
    }

    // --- Static Find methods ---

    AvailableNetworkSessionCollection NetworkSession::Find(
        NetworkSessionType sessionType,
        int maxLocalGamers,
        NetworkSessionProperties searchProperties
    )
    {
        System::IAsyncResult* result = BeginFind(
            sessionType, maxLocalGamers, std::move(searchProperties), System::AsyncCallback{}, std::any{}
        );
        while (!result->getIsCompletedProperty())
        {
            if (!GamerServices::GamerServicesDispatcher::UpdateAsync())
            {
                activeAction_->setIsCompletedProperty(true);
            }
        }
        return EndFind(result);
    }

    AvailableNetworkSessionCollection NetworkSession::Find(
        NetworkSessionType sessionType,
        const std::vector<SignedInGamer*>& localGamers,
        NetworkSessionProperties searchProperties
    )
    {
        System::IAsyncResult* result = BeginFind(
            sessionType, localGamers, std::move(searchProperties), System::AsyncCallback{}, std::any{}
        );
        while (!result->getIsCompletedProperty())
        {
            if (!GamerServices::GamerServicesDispatcher::UpdateAsync())
            {
                activeAction_->setIsCompletedProperty(true);
            }
        }
        return EndFind(result);
    }

    System::IAsyncResult* NetworkSession::BeginFind(
        NetworkSessionType sessionType,
        int maxLocalGamers,
        NetworkSessionProperties searchProperties,
        System::AsyncCallback callback,
        std::any asyncState
    )
    {
        if (sessionType == NetworkSessionType::Local)
        {
            throw System::ArgumentException("sessionType");
        }
        if (maxLocalGamers < 1 || maxLocalGamers > 4)
        {
            throw System::ArgumentOutOfRangeException("maxLocalGamers");
        }
        if (activeAction_ != nullptr || activeSession_ != nullptr)
        {
            throw System::InvalidOperationException();
        }

        activeAction_ = new NetworkSessionAction(
            std::move(asyncState), std::move(callback), maxLocalGamers, std::nullopt, 0,
            std::move(searchProperties), sessionType
        );
        return activeAction_;
    }

    System::IAsyncResult* NetworkSession::BeginFind(
        NetworkSessionType sessionType,
        const std::vector<SignedInGamer*>& localGamers,
        NetworkSessionProperties searchProperties,
        System::AsyncCallback callback,
        std::any asyncState
    )
    {
        if (sessionType == NetworkSessionType::Local)
        {
            throw System::ArgumentException("sessionType");
        }
        if (activeAction_ != nullptr || activeSession_ != nullptr)
        {
            throw System::InvalidOperationException();
        }

        int locals = static_cast<int>(localGamers.size());

        activeAction_ = new NetworkSessionAction(
            std::move(asyncState), std::move(callback), locals, localGamers, 0,
            std::move(searchProperties), sessionType
        );
        return activeAction_;
    }

    AvailableNetworkSessionCollection NetworkSession::EndFind(System::IAsyncResult* result)
    {
        if (result != activeAction_)
        {
            throw System::ArgumentException("result");
        }

        // Always empty: FNA never actually populates a discovered-sessions list in this stub.
        activeAction_ = nullptr;
        return AvailableNetworkSessionCollection::CreateInternal({});
    }

    // --- Static Join methods ---

    NetworkSession* NetworkSession::Join(const AvailableNetworkSession* availableSession)
    {
        System::IAsyncResult* result = BeginJoin(availableSession, System::AsyncCallback{}, std::any{});
        while (!result->getIsCompletedProperty())
        {
            if (!GamerServices::GamerServicesDispatcher::UpdateAsync())
            {
                activeAction_->setIsCompletedProperty(true);
            }
        }
        return EndJoin(result);
    }

    System::IAsyncResult* NetworkSession::BeginJoin(
        const AvailableNetworkSession* availableSession,
        System::AsyncCallback callback,
        std::any asyncState
    )
    {
        if (availableSession == nullptr)
        {
            throw System::ArgumentNullException("availableSession");
        }
        if (activeAction_ != nullptr || activeSession_ != nullptr)
        {
            throw System::InvalidOperationException();
        }

        activeAction_ = new NetworkSessionAction(
            std::move(asyncState), std::move(callback), 4, std::nullopt, 0,
            // FNA passes null for SessionProperties here (marked FIXME upstream); substituted
            // with a default instance since this port's SessionProperties isn't nullable.
            NetworkSessionProperties{},
            NetworkSessionType::PlayerMatch // FIXME upstream: hardcoded rather than derived from availableSession.
        );
        return activeAction_;
    }

    NetworkSession* NetworkSession::EndJoin(System::IAsyncResult* result)
    {
        if (result != activeAction_)
        {
            throw System::ArgumentException("result");
        }

        int actionMaxLocalGamers = activeAction_->MaxLocalGamers;
        auto actionLocalGamers = activeAction_->LocalGamers;
        activeAction_ = nullptr;

        activeSession_ = new NetworkSession(
            // FNA passes null for properties here (marked FIXME upstream); substituted with a
            // default instance since this port's SessionProperties isn't nullable.
            NetworkSessionProperties{},
            NetworkSessionType::PlayerMatch, // FIXME upstream
            MaxSupportedGamers,              // FIXME upstream
            4,                                // FIXME upstream
            actionMaxLocalGamers,
            actionLocalGamers
        );
        return activeSession_;
    }

    // --- Static JoinInvited methods ---

    NetworkSession* NetworkSession::JoinInvited(int maxLocalGamers)
    {
        System::IAsyncResult* result = BeginJoinInvited(maxLocalGamers, System::AsyncCallback{}, std::any{});
        while (!result->getIsCompletedProperty())
        {
            if (!GamerServices::GamerServicesDispatcher::UpdateAsync())
            {
                activeAction_->setIsCompletedProperty(true);
            }
        }
        return EndJoinInvited(result);
    }

    NetworkSession* NetworkSession::JoinInvited(const std::vector<SignedInGamer*>& localGamers)
    {
        System::IAsyncResult* result = BeginJoinInvited(localGamers, System::AsyncCallback{}, std::any{});
        while (!result->getIsCompletedProperty())
        {
            if (!GamerServices::GamerServicesDispatcher::UpdateAsync())
            {
                activeAction_->setIsCompletedProperty(true);
            }
        }
        return EndJoinInvited(result);
    }

    System::IAsyncResult* NetworkSession::BeginJoinInvited(
        int maxLocalGamers,
        System::AsyncCallback callback,
        std::any asyncState
    )
    {
        if (maxLocalGamers < 1 || maxLocalGamers > 4)
        {
            throw System::ArgumentOutOfRangeException("maxLocalGamers");
        }
        if (activeAction_ != nullptr || activeSession_ != nullptr)
        {
            throw System::InvalidOperationException();
        }

        activeAction_ = new NetworkSessionAction(
            std::move(asyncState), std::move(callback), maxLocalGamers, std::nullopt, 0,
            NetworkSessionProperties{}, // FNA passes null here (marked FIXME upstream); see BeginJoin.
            NetworkSessionType::PlayerMatch // FIXME upstream
        );
        return activeAction_;
    }

    System::IAsyncResult* NetworkSession::BeginJoinInvited(
        const std::vector<SignedInGamer*>& localGamers,
        System::AsyncCallback callback,
        std::any asyncState
    )
    {
        if (activeAction_ != nullptr || activeSession_ != nullptr)
        {
            throw System::InvalidOperationException();
        }

        activeAction_ = new NetworkSessionAction(
            std::move(asyncState), std::move(callback), 0, localGamers, 0,
            NetworkSessionProperties{}, // FNA passes null here (marked FIXME upstream); see BeginJoin.
            NetworkSessionType::PlayerMatch // FIXME upstream
        );
        return activeAction_;
    }

    NetworkSession* NetworkSession::EndJoinInvited(System::IAsyncResult* result)
    {
        if (result != activeAction_)
        {
            throw System::ArgumentException("result");
        }

        int actionMaxLocalGamers = activeAction_->MaxLocalGamers;
        auto actionLocalGamers = activeAction_->LocalGamers;
        activeAction_ = nullptr;

        activeSession_ = new NetworkSession(
            NetworkSessionProperties{}, // FNA passes null here (marked FIXME upstream); see BeginJoin.
            NetworkSessionType::PlayerMatch, // FIXME upstream
            MaxSupportedGamers,              // FIXME upstream
            4,                                // FIXME upstream
            actionMaxLocalGamers,
            actionLocalGamers
        );
        return activeSession_;
    }
}
