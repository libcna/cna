// SPDX-License-Identifier: MS-PL
#include "Microsoft/Xna/Framework/Net/NetworkSession.hpp"
#include "CNA/Internal/Net/ENetBackend.hpp"
#include "CNA/Internal/Net/ENetDiscoveryService.hpp"
#include "Microsoft/Xna/Framework/GamerServices/Gamer.hpp"
#include "Microsoft/Xna/Framework/GamerServices/GamerServicesDispatcher.hpp"
#include "Microsoft/Xna/Framework/GamerServices/GamerServicesNotAvailableException.hpp"
#include "Microsoft/Xna/Framework/GamerServices/GamerPrivilegeException.hpp"
#include "Microsoft/Xna/Framework/GamerServices/SignedInGamer.hpp"
#include "Microsoft/Xna/Framework/GamerServices/SignedInGamerCollection.hpp"
#include "Microsoft/Xna/Framework/Net/LocalNetworkGamer.hpp"
#include "Microsoft/Xna/Framework/Net/NetworkGamer.hpp"
#include "Microsoft/Xna/Framework/Net/NetworkSessionJoinException.hpp"
#include "System/ArgumentException.hpp"
#include "System/ArgumentNullException.hpp"
#include "System/ArgumentOutOfRangeException.hpp"
#include "System/InvalidOperationException.hpp"
#include "System/ObjectDisposedException.hpp"
#include <chrono>
#include <atomic>
#include <algorithm>
#include <set>
#include "System/TimeoutException.hpp"
#include <thread>
#include <map>
#include <tuple>
#include "CNA/Internal/GamerServices/IGamerServicesBackend.hpp"
#include "../Internal/OnlineSessionBinding.hpp"
#include "CNA/Internal/GamerServices/ServiceInvitations.hpp"
#include "System/NotSupportedException.hpp"

namespace Microsoft::Xna::Framework::Net
{
    using GamerServices::SignedInGamer;

    namespace
    {
        bool IsOnlineType(NetworkSessionType sessionType)
        {
            return sessionType == NetworkSessionType::PlayerMatch || sessionType == NetworkSessionType::Ranked;
        }

        void ThrowIfRestricted()
        {
            if (CNA::Internal::GamerServices::serviceCallsRestricted())
                throw System::InvalidOperationException("Networking calls are forbidden inside a final leaderboard write handler.");
        }
    }

    NetworkSession::NetworkSessionAction* NetworkSession::activeAction_ = nullptr;
    NetworkSession* NetworkSession::activeSession_ = nullptr;
    System::EventHandler<GamerServices::InviteAcceptedEventArgs> NetworkSession::InviteAccepted;
    std::optional<GamerServices::InviteAcceptedEventArgs> NetworkSession::pendingInviteAccepted_;
    // After InviteAccepted in this translation unit's initialization order.
    bool NetworkSession::inviteSinkInstalled_ = NetworkSession::InstallInviteSink();
    std::string NetworkSession::pendingJoinAddress_;
    uint16_t NetworkSession::pendingJoinPort_ = 0;
    int NetworkSession::instanceCount_ = 0;

    struct NetworkSession::NetworkSessionAction::Storage
    {
        std::any value;
        std::exception_ptr error;
        std::atomic<bool> complete{true};
        NetworkSessionAction* target=nullptr;
        System::Threading::EventWaitHandle wait{true, System::Threading::EventResetMode::ManualReset};
    };
    std::vector<NetworkSession::NetworkSessionAction*> NetworkSession::NetworkSessionAction::live_;

    NetworkSession::NetworkSessionAction::NetworkSessionAction(
        NetworkSessionOperation operation,
        std::any state,
        System::AsyncCallback callback,
        int maxLocal,
        std::optional<std::vector<SignedInGamer*>> localGamers,
        int maxPrivateSlots,
        NetworkSessionProperties properties,
        NetworkSessionType type,
        int maxGamers
    )
        : Operation(operation)
        , Callback(std::move(callback))
        , MaxLocalGamers(maxLocal)
        , MaxGamers(maxGamers)
        , LocalGamers(std::move(localGamers))
        , MaxPrivateSlots(maxPrivateSlots)
        , SessionProperties(std::move(properties))
        , SessionType(type)
        , asyncState_(std::move(state))
        , storage_(std::make_shared<Storage>())
    {
        live_.push_back(this);
        storage_->target=this;
        ++instanceCount_;
    }

    int NetworkSession::NetworkSessionAction::instanceCount_ = 0;

    NetworkSession::NetworkSessionAction::~NetworkSessionAction()
    {
        storage_->target=nullptr;
        // Cancels a pending online operation and releases unconsumed membership/transport.
        online_.reset();
        std::erase(live_, this);
        if(activeAction_==this) activeAction_=nullptr;
        --instanceCount_;
    }

    int NetworkSession::NetworkSessionAction::GetInstanceCountForTesting()
    {
        return instanceCount_;
    }

    const std::any& NetworkSession::NetworkSessionAction::getAsyncStateProperty() const { return asyncState_; }
    bool NetworkSession::NetworkSessionAction::getCompletedSynchronouslyProperty() const { return completedSynchronously_; }
    bool NetworkSession::NetworkSessionAction::getIsCompletedProperty() const { return storage_->complete.load(); }
    void NetworkSession::NetworkSessionAction::setIsCompletedProperty(bool value) { storage_->complete=value; if(value) storage_->wait.Set(); else storage_->wait.Reset(); }

    System::Threading::WaitHandle& NetworkSession::NetworkSessionAction::getAsyncWaitHandleProperty() const
    {
        return storage_->wait;
    }

    void NetworkSession::NetworkSessionAction::Queue(std::function<std::any()> work)
    {
        completedSynchronously_=false;
        setIsCompletedProperty(false);
        auto state=storage_;
        executor_=CNA::Internal::GamerServices::backend();
        executor_->submit(
            [state,work=std::move(work)] {
                try {state->value=work();} catch(...) {state->error=std::current_exception();}
            },
            [state] {
                state->complete=true;
                state->wait.Set();
                if(auto* target=state->target; target && target->Callback) {
                    auto callback=target->Callback;
                    callback(*target);
                }
            });
    }

    NetworkSession::NetworkSessionAction* NetworkSession::NetworkSessionAction::PrepareEnd(
        System::IAsyncResult* result, NetworkSessionOperation operation)
    {
        if(!result) throw System::ArgumentNullException("result");
        auto found=std::find_if(live_.begin(),live_.end(),[result](auto* action) {return action==result;});
        if(found==live_.end() || (*found)->Operation!=operation) throw System::ArgumentException("result");
        auto* action=*found;
        if(action->ended_) throw System::InvalidOperationException("End was already called.");
        if(action!=activeAction_) throw System::ArgumentException("result");
        // Claim before pumping: a callback may reenter End while the outer End is waiting.
        action->ended_=true;
        try {
            const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(30);
            while(!action->getIsCompletedProperty()) {
                GamerServices::GamerServicesDispatcher::Update();
                CNA::Internal::GamerServices::pumpRetainedCompletions(action->executor_);
                if(std::chrono::steady_clock::now()>=deadline) throw System::TimeoutException("CNA session operation timed out.");
                if(!action->getIsCompletedProperty()) std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
            if(action->storage_->error) std::rethrow_exception(action->storage_->error);
        } catch(...) {
            if(activeAction_==action) activeAction_=nullptr;
            throw;
        }
        return action;
    }

    std::any NetworkSession::NetworkSessionAction::TakeValue() {return std::move(storage_->value);}

    NetworkSession::NetworkSessionAction* NetworkSession::InvokeActiveActionCallback()
    {
        // Begin retains ownership until it returns, including when the callback calls End or throws.
        std::unique_ptr<NetworkSessionAction> action(activeAction_);
        if(action->Callback) {auto callback=action->Callback;callback(*action);}
        return action.release();
    }

    // --- Constructor ---

    NetworkSession::NetworkSession(
        NetworkSessionProperties properties,
        NetworkSessionType type,
        int maxGamers,
        int privateGamerSlots,
        int maxLocal,
        std::optional<std::vector<SignedInGamer*>> localGamers,
        bool isHost
    )
        : allGamers_(GamerServices::GamerCollection<NetworkGamer>::CreateInternal({}))
        , localGamers_(GamerServices::GamerCollection<LocalNetworkGamer>::CreateInternal({}))
        , remoteGamers_(GamerServices::GamerCollection<NetworkGamer>::CreateInternal({}))
        , previousGamers_(GamerServices::GamerCollection<NetworkGamer>::CreateInternal({}))
        , isHost_(isHost)
        , maxGamers_(maxGamers)
        , privateGamerSlots_(privateGamerSlots)
        , sessionProperties_(std::move(properties))
        , sessionType_(type)
    {
        sessionProperties_.setWriteGuard([this] {
            if (isDisposed_) throw System::ObjectDisposedException("NetworkSession");
            if (!getIsHostProperty()) throw System::InvalidOperationException("This NetworkSession is not the host");
            if (CNA::Internal::GamerServices::serviceCallsRestricted())
                throw System::InvalidOperationException("Networking calls are forbidden inside a final leaderboard write handler.");
        });
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

        // Task 3.1: GamerCollection<T> only ever holds non-owning raw pointers (matching real
        // XNA's read-only-collection API shape) - ownedGamers_ is this session's own separate
        // ownership registry, freed in bulk by Dispose()/~NetworkSession().
        for (LocalNetworkGamer* l : locals) ownedGamers_.emplace_back(l);

        // Not part of FNA's original design (see DEFERRED.md item #20 in the sibling cna-samples
        // repo): give every local gamer a real host flag and a real, locally-unique id instead of
        // FNA's hardcoded true/0 stubs. The id assigned here is only a local placeholder for
        // non-SystemLink sessions - ENetBackend overwrites it with the wire-negotiated id once a
        // real SystemLink session actually joins/hosts (see ENetBackend.cpp's AssignWireId/
        // HandleServerWelcome/HandleGamerJoinBroadcast).
        // Gamers playing on this machine share one NetworkMachine, as the reference session does.
        auto machine = std::make_shared<NetworkMachine>(NetworkMachine::CreateInternal());
        for (LocalNetworkGamer* l : locals)
        {
            l->SetIsHost(isHost_);
            l->SetId(nextLocalGamerId_++);
            l->SetSharedMachine(machine);
            machine->AddGamerInternal(l);
        }

        host_ = localGamers_[0];

        if (getIsHostProperty())
        {
            allowHostMigration_ = false;
            allowJoinInProgress_ = false;
            sessionState_ = NetworkSessionState::Lobby;
        }

        // Deviation from FNA (see DEFERRED.md item #21 in the sibling cna-samples repo, and
        // plans/plan_net.md's Task 12.3 for the full investigation): FNA's constructor queues a
        // GamerJoin NetworkEvent per initial gamer here instead, only drained by the *next*
        // Update() call. Real XNA's GamerJoined replays itself immediately for every gamer
        // already in the session the instant a handler subscribes via += - impossible for any
        // caller to observe before this point anyway, since the session pointer doesn't exist
        // until this constructor returns. sharp-runtime's EventHandler<T>::SetReplayHook()
        // (added specifically for this) reproduces that: the closure below fires once,
        // synchronously, for every gamer in allGamers_ at the moment each new handler subscribes -
        // covering these initial local gamers here, and any later ones too, without a separate
        // queued event (which would otherwise double-fire this same join once the queue drained).
        // Mid-session joins discovered while handlers already exist (AddRemoteGamer) are
        // unaffected: those still queue a real NetworkEvent, delivered through the normal
        // Update() pump, exactly as before.
        GamerJoined.SetReplayHook([this](const System::EventHandler<GamerJoinedEventArgs>::HandlerType& handler)
        {
            for (NetworkGamer* gamer : allGamers_)
            {
                handler(this, GamerJoinedEventArgs(gamer));
            }
        });

        // Reference GameStarted add accessor: a handler added while Playing is told at once.
        GameStarted.SetReplayHook([this](const System::EventHandler<GameStartedEventArgs>::HandlerType& handler)
        {
            if (sessionState_ == NetworkSessionState::Playing)
            {
                handler(this, GameStartedEventArgs());
            }
        });

        simulatedLatency_ = System::TimeSpan::Zero;
        simulatedPacketLoss_ = 0.0f;
        isDisposed_ = false;

        bytesPerSecondReceived_ = 0;
        bytesPerSecondSent_ = 0;

        if (CNA::Internal::Net::ENetBackend::RealNetworkingEnabled(sessionType_))
        {
            CNA::Internal::Net::ENetBackend::StartHosting(this);
        }

        ++instanceCount_; // Task 3.3
    }

    // Task 3.1: out-of-line so NetworkGamer (only forward-declared in the header) is a complete
    // type here, where ownedGamers_'s std::vector<std::unique_ptr<NetworkGamer>> is destroyed.
    NetworkSession::~NetworkSession()
    {
        // Task 2.1: a caller that `delete`s a NetworkSession* without calling Dispose() first
        // (a real risk - Create()/Find()/Join() all hand back a caller-owned raw pointer, per
        // this class's own ownership-contract doc comment above) used to leave activeSession_
        // dangling at the just-freed `this`, and ENetBackend::TeardownSession never ran. Every
        // subsequent BeginCreate/BeginFind/BeginJoin checks `activeSession_ != nullptr` and
        // throws, so one mismanaged delete permanently bricked session creation for the rest of
        // the process and leaked the transport (ENet host socket, discovery advertisement).
        // Standard IDisposable safety net: fall back to Dispose() here if it was never called.
        if (!isDisposed_)
        {
            ReleaseSessionResources();
        }
        --instanceCount_; // Task 3.3
    }

    int NetworkSession::GetInstanceCountForTesting()
    {
        return instanceCount_;
    }

    // --- Properties ---

    bool NetworkSession::getIsDisposedProperty() const { return isDisposed_; }

    const GamerServices::GamerCollection<NetworkGamer>& NetworkSession::getAllGamersProperty() const { return allGamers_; }
    const GamerServices::GamerCollection<LocalNetworkGamer>& NetworkSession::getLocalGamersProperty() const { return localGamers_; }
    const GamerServices::GamerCollection<NetworkGamer>& NetworkSession::getRemoteGamersProperty() const { return remoteGamers_; }
    const GamerServices::GamerCollection<NetworkGamer>& NetworkSession::getPreviousGamersProperty() const { return previousGamers_; }

    bool NetworkSession::getAllowHostMigrationProperty() const { return allowHostMigration_; }
    void NetworkSession::setAllowHostMigrationProperty(bool value)
    {
        // Online sessions follow the reference SendAllowCommand guards. SystemLink keeps its
        // per-machine flag: its clients set it locally because the transport does not propagate it.
        if (value != allowHostMigration_ && online_)
        {
            if (isDisposed_) throw System::ObjectDisposedException("NetworkSession");
            if (!getIsHostProperty()) throw System::InvalidOperationException("This NetworkSession is not the host");
        }
        allowHostMigration_ = value;
    }

    bool NetworkSession::getAllowJoinInProgressProperty() const { return allowJoinInProgress_; }
    void NetworkSession::setAllowJoinInProgressProperty(bool value)
    {
        if (value == allowJoinInProgress_) return;
        // Reference AllowJoinInProgress: an unchanged value is ignored, then Ranked refuses before
        // the host/disposed checks of SendAllowCommand (Microsoft.Xna.Framework.Net NetworkSession IL).
        if (sessionType_ == NetworkSessionType::Ranked)
            throw System::NotSupportedException("Ranked sessions do not support join-in-progress.");
        if (online_)
        {
            if (isDisposed_) throw System::ObjectDisposedException("NetworkSession");
            if (!getIsHostProperty()) throw System::InvalidOperationException("This NetworkSession is not the host");
        }
        allowJoinInProgress_ = value;
    }

    int NetworkSession::getBytesPerSecondReceivedProperty() const { return bytesPerSecondReceived_; }
    int NetworkSession::getBytesPerSecondSentProperty() const { return bytesPerSecondSent_; }

    NetworkGamer* NetworkSession::getHostProperty() const { return host_; }

    bool NetworkSession::getIsEveryoneReadyProperty() const
    {
        // Reference IsEveryoneReady: every gamer of the session, and false for an empty session.
        if (allGamers_.getCountProperty() == 0) return false;
        for (NetworkGamer* gamer : allGamers_)
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
    void NetworkSession::setMaxGamersProperty(int value)
    {
        if (value == maxGamers_) return;
        // Reference MaxGamers: bounds against occupied public slots, then SetGamerSlots guards.
        int fullPublic = 0;
        for (NetworkGamer* gamer : allGamers_) if (!gamer->getIsPrivateSlotProperty()) ++fullPublic;
        const int minimum = sessionType_ == NetworkSessionType::Local ? 1 : 2;
        if (value < minimum || value > MaxSupportedGamers || value < fullPublic + privateGamerSlots_)
            throw System::ArgumentOutOfRangeException("value");
        if (isDisposed_) throw System::ObjectDisposedException("NetworkSession");
        if (!getIsHostProperty()) throw System::InvalidOperationException("This NetworkSession is not the host");
        maxGamers_ = value;
    }

    int NetworkSession::getPrivateGamerSlotsProperty() const { return privateGamerSlots_; }
    void NetworkSession::setPrivateGamerSlotsProperty(int value)
    {
        if (value == privateGamerSlots_) return;
        int fullPublic = 0, fullPrivate = 0;
        for (NetworkGamer* gamer : allGamers_) ++(gamer->getIsPrivateSlotProperty() ? fullPrivate : fullPublic);
        if (value < fullPrivate || value > maxGamers_ - fullPublic)
            throw System::ArgumentOutOfRangeException("value");
        if (isDisposed_) throw System::ObjectDisposedException("NetworkSession");
        if (!getIsHostProperty()) throw System::InvalidOperationException("This NetworkSession is not the host");
        privateGamerSlots_ = value;
    }

    NetworkSessionProperties& NetworkSession::getSessionPropertiesProperty() { return sessionProperties_; }
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
        if(CNA::Internal::GamerServices::serviceCallsRestricted())throw System::InvalidOperationException("Networking calls are forbidden inside a final leaderboard write handler.");
        // Task 12.1: Dispose() itself was not idempotent - a second call (a real, reachable
        // pattern: e.g. an explicit Dispose() followed by an RAII wrapper/fixture destructor that
        // also unconditionally calls Dispose()) re-entered the body below and hit a use-after-free
        // in the ClearPacketQueue() loop, because ownedGamers_.clear() further down destroys the
        // locally-owned gamer objects while localGamers_/allGamers_ (raw, non-owning views) were
        // never pruned - only RemoveGamer() prunes them, and Dispose() never called it. Confirmed
        // under AddressSanitizer (heap-buffer-overflow) - see audit_net.md's Critical finding 1.
        if (isDisposed_)
        {
            return;
        }

        if(!leaderboardGameplay_.empty()&&sessionState_==NetworkSessionState::Playing)FinalizeServiceLeaderboards(true);
        ReleaseSessionResources();
    }

    void NetworkSession::ReleaseSessionResources()
    {
        if(isDisposed_)return;
        // Finalization must not invoke user callbacks or throw from a C++ destructor.
        AbandonServiceLeaderboards();
        for (LocalNetworkGamer* gamer : localGamers_)
        {
            gamer->ClearPacketQueue();
        }
        if (CNA::Internal::Net::ENetBackend::RealNetworkingEnabled(sessionType_))
        {
            CNA::Internal::Net::ENetBackend::TeardownSession(this);
        }
        // Disconnects the relay and leaves directory membership while remote gamers still exist.
        if (online_) online_->close();
        // Task 3.1: frees every gamer this session ever owned - after TeardownSession above, so
        // ENetBackend's own per-session wire-id maps (which can hold these same raw pointers) are
        // already torn down first, never left holding a reference to now-freed memory.
        ownedGamers_.clear();
        // Defense-in-depth, independent of the isDisposed_ guard above: localGamers_/remoteGamers_/
        // allGamers_/previousGamers_ are all non-owning raw-pointer views that can still hold
        // pointers into the objects just freed by ownedGamers_.clear() (previousGamers_ especially,
        // since RemoveGamer() moves a departing gamer's pointer there instead of dropping it) - a
        // caller reading a public collection property after a *single* Dispose() call must never be
        // able to observe a dangling pointer, so all four are cleared here rather than relying on
        // no caller ever calling Dispose() twice.
        localGamers_.Clear();
        remoteGamers_.Clear();
        allGamers_.Clear();
        previousGamers_.Clear();
        host_ = nullptr;
        online_.reset();
        activeSession_ = nullptr;
        isDisposed_ = true;
    }

    std::size_t NetworkSession::GetOwnedGamerCountForTesting() const
    {
        return ownedGamers_.size();
    }

    int NetworkSession::GetActiveActionInstanceCountForTesting()
    {
        return NetworkSessionAction::GetInstanceCountForTesting();
    }

    const std::string& NetworkSession::GetTypeName() const
    {
        static const std::string typeName = "Microsoft.Xna.Framework.Net.NetworkSession";
        return typeName;
    }

    void NetworkSession::Update()
    {
        if(CNA::Internal::GamerServices::serviceCallsRestricted())throw System::InvalidOperationException("Networking calls are forbidden inside a final leaderboard write handler.");
        if (isDisposed_)
        {
            throw System::ObjectDisposedException("this");
        }

        if (CNA::Internal::Net::ENetBackend::RealNetworkingEnabled(sessionType_))
        {
            CNA::Internal::Net::ENetBackend::PumpSession(this);
            CNA::Internal::Net::ENetDiscoveryService::Poll();
        }
        if (online_) online_->pump();

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
                if (CNA::Internal::Net::ENetBackend::RealNetworkingEnabled(sessionType_) || online_)
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
                    else if (evt.Gamer != nullptr && online_)
                    {
                        online_->send(evt.Sender, evt.Gamer, evt.Packet, evt.Reliable);
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
                GamerJoined.Raise(this, GamerJoinedEventArgs(evt.Gamer));
            }
            else if (evt.Type == NetworkEventType::GamerLeave)
            {
                GamerLeft.Raise(this, GamerLeftEventArgs(evt.Gamer));
            }
            else if (evt.Type == NetworkEventType::HostChange)
            {
                HostChanged.Raise(this, HostChangedEventArgs(host_, evt.Gamer));
                host_ = evt.Gamer;
            }
            else // NetworkEventType::StateChange
            {
                // The reference applies the new state before raising its event.
                if (evt.State == NetworkSessionState::Playing)
                {
                    // Every machine of an online session opens its own local write epoch.
                    if(online_ && leaderboardGameplay_.empty()) BeginOnlineLeaderboards();
                    if(!leaderboardGameplay_.empty()) {
                        OpenLeaderboardWriters();
                        leaderboardTransitionPending_=false;
                    }
                    sessionState_ = evt.State;
                    // The host publishes gameplay state only once its own transition has happened.
                    if(online_ && online_->host()) online_->requestState(evt.State);
                    GameStarted.Raise(this, GameStartedEventArgs());
                }
                else if (evt.State == NetworkSessionState::Lobby)
                {
                    if(!leaderboardGameplay_.empty()) {
                        if(online_ && !online_->host()) {
                            // A client cannot retry the host's EndGame; a failed final commit is abandoned.
                            try{FinalizeServiceLeaderboards();}catch(...){AbandonServiceLeaderboards();}
                        }
                        else {
                            try{FinalizeServiceLeaderboards();}catch(...){leaderboardTransitionPending_=false;throw;}
                        }
                        leaderboardTransitionPending_=false;
                    }
                    // Every machine clears readiness when a game ends, so the lobby starts over.
                    if (sessionState_ == NetworkSessionState::Playing) ClearReadyInternal();
                    sessionState_ = evt.State;
                    if(online_ && online_->host()) online_->requestState(evt.State);
                    GameEnded.Raise(this, GameEndedEventArgs());
                }
                else
                {
                    // Losing the session while playing still offers the local gamers' final writes.
                    if(!leaderboardGameplay_.empty() && sessionState_==NetworkSessionState::Playing) {
                        try{FinalizeServiceLeaderboards(true);}catch(...){AbandonServiceLeaderboards();}
                    }
                    sessionState_ = evt.State;
                    SessionEnded.Raise(this, NetworkSessionEndedEventArgs(evt.Reason));
                }
            }
        }
    }

    void NetworkSession::AddLocalGamer(SignedInGamer* gamer)
    {
        if(CNA::Internal::GamerServices::serviceCallsRestricted())throw System::InvalidOperationException("Networking calls are forbidden inside a final leaderboard write handler.");
        // Reference NetworkSession.AddLocalGamer, in its validation order.
        if (gamer == nullptr) throw System::ArgumentNullException("gamer");
        if (gamer->getIsDisposedProperty()) throw System::ObjectDisposedException("gamer");
        if (isDisposed_) throw System::ObjectDisposedException("NetworkSession");
        for (LocalNetworkGamer* local : localGamers_)
            if (local->getSignedInGamerProperty() == gamer) throw System::ArgumentException("The gamer is already in the session.", "gamer");
        if (sessionState_ == NetworkSessionState::Playing && !allowJoinInProgress_)
            throw System::InvalidOperationException("The session does not allow joining a game in progress.");
        if (sessionState_ == NetworkSessionState::Ended) throw System::InvalidOperationException("The session has ended.");
        int fullPublicSlots = 0;
        for (NetworkGamer* member : allGamers_) if (!member->getIsPrivateSlotProperty()) ++fullPublicSlots;
        if (maxGamers_ - privateGamerSlots_ - fullPublicSlots <= 0)
            throw System::InvalidOperationException("The session has no open public slot.");
        // The service directory admits one complete local group per machine; extending it is not implemented yet.
        if (online_)
            throw System::NotSupportedException("CNA online sessions do not yet add local gamers after creation or join.");
        if (localGamers_.getCountProperty() == maxLocalGamers_)
        {
            throw System::InvalidOperationException("LocalGamer max limit!");
        }
        auto* adding = new LocalNetworkGamer(LocalNetworkGamer::CreateInternal(gamer, this));
        adding->SetIsHost(isHost_);
        if (localGamers_.getCountProperty() > 0)
        {
            adding->SetSharedMachine(localGamers_[0]->GetSharedMachine());
            adding->GetSharedMachine()->AddGamerInternal(adding);
        }
        // Task 2.4: nextLocalGamerId_ is a real monotonic counter, never derived from any live
        // collection's size (allGamers_.getCountProperty() shrinks on RemoveGamer, so a
        // remove-then-add sequence used to hand out a colliding id already owned by a
        // still-present gamer, corrupting FindGamerById).
        adding->SetId(nextLocalGamerId_++);
        localGamers_.Add(adding);
        allGamers_.Add(adding);
        ownedGamers_.emplace_back(adding); // Task 3.1

        // Task 2.3: AddRemoteGamer (below) already enqueues a GamerJoin event so a handler
        // already subscribed before it runs still learns about the new gamer; AddLocalGamer never
        // did, so a handler subscribed before this call had no way to learn about the newly-added
        // local gamer at all (no replay hook covers this path - SetReplayHook only fires on
        // subscription, not on a later Add()).
        NetworkEvent evt;
        evt.Type = NetworkEventType::GamerJoin;
        evt.Gamer = adding;
        SendNetworkEvent(std::move(evt));
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
        if(CNA::Internal::GamerServices::serviceCallsRestricted())throw System::InvalidOperationException("Networking calls are forbidden inside a final leaderboard write handler.");
        if (isDisposed_) throw System::ObjectDisposedException("this");
        if (!getIsHostProperty()) throw System::InvalidOperationException("This NetworkSession is not the host");
        if (sessionState_ != NetworkSessionState::Lobby) throw System::InvalidOperationException("NetworkSession is not Lobby");

        ClearReadyInternal();
        std::vector<NetworkGamer*> everyone(allGamers_.begin(), allGamers_.end());
        PublishGamerReady(everyone);
    }

    void NetworkSession::ApplyGamerReadyInternal(NetworkGamer& gamer, bool value)
    {
        gamer.SetIsReadyInternal(value);
    }

    void NetworkSession::RemoveMachineInternal(NetworkGamer* gamer)
    {
        if (isDisposed_) throw System::ObjectDisposedException("NetworkSession");
        if (online_)
        {
            online_->removeMachine(gamer);
            return;
        }
        if (CNA::Internal::Net::ENetBackend::RealNetworkingEnabled(sessionType_))
            CNA::Internal::Net::ENetBackend::RemoveMachine(this, gamer);
    }

    void NetworkSession::OrderGamersInternal()
    {
        // Reference GamerCollection.Insert keeps every collection sorted by the gamer's session
        // index, so the host comes first on every machine; CNA's cross-machine Id plays that role.
        const auto order = [](auto& collection) {
            std::vector gamers(collection.begin(), collection.end());
            const auto byId = [](const auto* left, const auto* right) { return left->getIdProperty() < right->getIdProperty(); };
            if (std::is_sorted(gamers.begin(), gamers.end(), byId)) return;
            std::stable_sort(gamers.begin(), gamers.end(), byId);
            collection.Clear();
            for (auto* gamer : gamers) collection.Add(gamer);
        };
        order(allGamers_);
        order(localGamers_);
        order(remoteGamers_);
    }

    void NetworkSession::ClearReadyInternal()
    {
        for (NetworkGamer* gamer : allGamers_)
        {
            gamer->SetIsReadyInternal(false);
        }
    }

    void NetworkSession::PublishGamerReady(const std::vector<NetworkGamer*>& gamers)
    {
        if (gamers.empty()) return;
        if (online_)
        {
            online_->publishReady(gamers);
        }
        else if (CNA::Internal::Net::ENetBackend::RealNetworkingEnabled(sessionType_))
        {
            CNA::Internal::Net::ENetBackend::PublishGamerReady(this, gamers);
        }
    }

    void NetworkSession::StartGame()
    {
        if(CNA::Internal::GamerServices::serviceCallsRestricted())throw System::InvalidOperationException("Networking calls are forbidden inside a final leaderboard write handler.");
        if (isDisposed_) throw System::ObjectDisposedException("this");
        if (!getIsHostProperty()) throw System::InvalidOperationException("This NetworkSession is not the host");
        if (sessionState_ != NetworkSessionState::Lobby) throw System::InvalidOperationException("NetworkSession is not Lobby");
        if(leaderboardTransitionPending_)throw System::InvalidOperationException("A gameplay transition is already pending.");
        if(sessionType_==NetworkSessionType::LocalWithLeaderboards&&CNA::Internal::GamerServices::backend()->serviceEnabled()) {
            std::vector<std::string> users;for(auto* gamer:localGamers_)users.push_back(gamer->serviceUserId_);
            leaderboardGameplay_=CNA::Internal::GamerServices::backend()->beginLeaderboardGame(users);
            leaderboardOwner_=users.front();leaderboardTransitionPending_=true;
        }


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
        if(CNA::Internal::GamerServices::serviceCallsRestricted())throw System::InvalidOperationException("Networking calls are forbidden inside a final leaderboard write handler.");
        if (isDisposed_) throw System::ObjectDisposedException("this");
        if (!getIsHostProperty()) throw System::InvalidOperationException("This NetworkSession is not the host");
        if (sessionState_ != NetworkSessionState::Playing) throw System::InvalidOperationException("NetworkSession is not Playing");
        if(leaderboardTransitionPending_)throw System::InvalidOperationException("A gameplay transition is already pending.");
        if(!leaderboardGameplay_.empty())leaderboardTransitionPending_=true;


        NetworkEvent evt;
        evt.Type = NetworkEventType::StateChange;
        evt.State = NetworkSessionState::Lobby;
        SendNetworkEvent(std::move(evt));

        if (CNA::Internal::Net::ENetBackend::RealNetworkingEnabled(sessionType_))
        {
            CNA::Internal::Net::ENetBackend::BroadcastStateChange(this, NetworkSessionState::Lobby);
        }
    }

    std::shared_ptr<CNA::Internal::GamerServices::IGamerServicesBackend> NetworkSession::LeaderboardService() const {
        // An online session's epochs belong to the backend that owns its membership.
        return online_ ? online_->origin() : CNA::Internal::GamerServices::backend();
    }

    void NetworkSession::BeginOnlineLeaderboards() {
        auto service=LeaderboardService();
        if(!service || !service->serviceEnabled()) return;
        std::vector<std::string> users;for(auto* gamer:localGamers_)users.push_back(gamer->serviceUserId_);
        if(users.empty()) return;
        // Unavailable leaderboards leave the writers closed rather than failing the transition.
        try {leaderboardGameplay_=service->beginLeaderboardGame(users);leaderboardOwner_=users.front();}
        catch(...) {leaderboardGameplay_.clear();}
    }

    void NetworkSession::AbandonServiceLeaderboards() noexcept {
        if(!leaderboardGameplay_.empty()) {
            // Capture only owned logical values so queued cleanup cannot reference this session.
            try {
                const auto service=LeaderboardService();
                if(!service) throw std::runtime_error("no leaderboard service");
                const auto gameplay=leaderboardGameplay_,owner=leaderboardOwner_;
                auto* executor=service.get();
                // The backend joins its own executor before destruction. A queued task must not
                // retain that backend and cause its destructor to run on the executor thread.
                service->submit([executor,gameplay,owner]{try{executor->abortLeaderboardGame(gameplay,owner);}catch(...){}},[]{});
            }catch(...){}
        }
        CloseLeaderboardWriters();
        leaderboardGameplay_.clear();
    }

    void NetworkSession::OpenLeaderboardWriters() {
        for(auto* gamer:localGamers_){gamer->leaderboardWriter_.BeginServiceGameplay();gamer->getSignedInGamerProperty()->leaderboardWriter_.BeginServiceGameplay();}
        // Ranked machines write arbitrated statistics and TrueSkill for every gamer; elsewhere the
        // host reports TrueSkill for every gamer (LeaderboardWriter documentation).
        if(online_ && (sessionType_==NetworkSessionType::Ranked || getIsHostProperty()))
            for(auto* gamer:remoteGamers_)gamer->leaderboardWriter_.BeginServiceGameplay();
        roundDeparted_.clear();
    }

    void NetworkSession::CloseLeaderboardWriters() {
        for(auto* gamer:localGamers_){gamer->leaderboardWriter_.EndServiceGameplay();if(gamer->getSignedInGamerProperty())gamer->getSignedInGamerProperty()->leaderboardWriter_.EndServiceGameplay();}
        for(auto* gamer:remoteGamers_)gamer->leaderboardWriter_.EndServiceGameplay();
        for(auto* gamer:roundDeparted_)gamer->leaderboardWriter_.EndServiceGameplay();
        roundDeparted_.clear();
    }

    void NetworkSession::FinalizeServiceLeaderboards(bool isLeaving) {
        using CNA::Internal::GamerServices::ServiceLeaderboardWrite;
        const bool ranked=online_ && sessionType_==NetworkSessionType::Ranked;
        const bool trueSkill=ranked || getIsHostProperty();
        // A leaving machine reports only its own gamers, so its partial view cannot break the
        // agreement of the machines that finish the round.
        std::vector<NetworkGamer*> everyone;
        for(auto* gamer:localGamers_)everyone.push_back(gamer);
        if(!isLeaving)for(auto* gamer:remoteGamers_)everyone.push_back(gamer);
        CNA::Internal::GamerServices::withRestrictedServiceCalls([&] {
            for(auto* gamer:localGamers_)WriteUnarbitratedLeaderboard.Raise(this,WriteLeaderboardsEventArgs::CreateInternal(gamer,isLeaving));
            if(ranked)for(auto* gamer:everyone)WriteArbitratedLeaderboard.Raise(this,WriteLeaderboardsEventArgs::CreateInternal(gamer,isLeaving&&gamer->getIsLocalProperty()));
            if(trueSkill)for(auto* gamer:everyone)WriteTrueSkill.Raise(this,WriteLeaderboardsEventArgs::CreateInternal(gamer,isLeaving&&gamer->getIsLocalProperty()));
        });
        std::map<std::tuple<std::string,std::string,int>,ServiceLeaderboardWrite> writes;
        auto collect=[&](GamerServices::LeaderboardWriter& writer) {
            for(auto& row:writer.CollectServiceWrites()) {
                auto key=std::make_tuple(row.userId,row.key,row.mode);const auto existing=writes.find(key);
                if(existing!=writes.end()&&existing->second!=row)throw System::InvalidOperationException("Conflicting leaderboard writes for the same gamer.");
                writes[key]=std::move(row);
            }
        };
        for(auto* gamer:localGamers_){collect(gamer->leaderboardWriter_);collect(gamer->getSignedInGamerProperty()->leaderboardWriter_);}
        // Only Ranked arbitration can carry rows about other machines' gamers; a non-Ranked host's
        // TrueSkill rows for remote gamers have no storage authority and are not submitted.
        if(ranked && !isLeaving) {
            for(auto* gamer:remoteGamers_)collect(gamer->leaderboardWriter_);
            for(auto* gamer:roundDeparted_)collect(gamer->leaderboardWriter_);
        }
        std::vector<ServiceLeaderboardWrite> rows;for(auto& [key,row]:writes){(void)key;rows.push_back(std::move(row));}
        std::optional<CNA::Internal::GamerServices::ServiceArbitration> arbitration;
        if(ranked)arbitration=CNA::Internal::GamerServices::ServiceArbitration{online_->session(),online_->revision()};
        const auto service=LeaderboardService();
        if(!service) throw GamerServices::GamerServicesNotAvailableException("The leaderboard service is no longer available.");
        service->commitLeaderboardGame(leaderboardGameplay_,leaderboardOwner_,rows,arbitration);
        CloseLeaderboardWriters();
        leaderboardGameplay_.clear();
    }

    void NetworkSession::SendNetworkEvent(NetworkEvent evt)
    {
        networkEvents_.push(std::move(evt));
    }

    void NetworkSession::SetHostFromTransport(NetworkGamer* host, bool raiseHostChanged)
    {
        NetworkGamer* oldHost = host_;
        if (raiseHostChanged && oldHost != host)
        {
            HostChanged.Raise(this, HostChangedEventArgs(oldHost, host));
        }
        host_ = host;
    }

    void NetworkSession::SetSessionPropertiesFromTransport(NetworkSessionProperties properties)
    {
        sessionProperties_.replaceFromTransport(properties);
    }

    void NetworkSession::AddRemoteGamer(NetworkGamer* gamer)
    {
        // Task 3.1: AddRemoteGamer deliberately does NOT take ownership of gamer, unlike the
        // constructor/AddLocalGamer above - its existing, established contract (see
        // NetworkSessionTests.cpp's AddRemoteGamer* tests, which pass stack-allocated NetworkGamer
        // instances) never assumed ownership transfer, and changing that here would crash those
        // tests (deleting non-heap memory) the moment this session is disposed or this call
        // throws. Ownership of the gamers ENetBackend actually `new`s belongs to ENetBackend's own
        // per-session SessionState instead - see ENetBackend.cpp's OwnedRemoteGamers.

        // Task 2.5: AddRemoteGamer used to add any remote gamer unconditionally, silently
        // violating the documented "maximum players allowed" contract - no FNA equivalent exists
        // to match (AddRemoteGamer is a CNA-internal, CNAEXT extension; real FNA's networking is
        // entirely stubbed out), so InvalidOperationException is used for symmetry with
        // AddLocalGamer's own existing max-limit guard just above.
        if (allGamers_.getCountProperty() >= maxGamers_)
        {
            throw System::InvalidOperationException("Session is full!");
        }
        remoteGamers_.Add(gamer);
        allGamers_.Add(gamer);
        OrderGamersInternal();

        NetworkEvent evt;
        evt.Type = NetworkEventType::GamerJoin;
        evt.Gamer = gamer;
        SendNetworkEvent(std::move(evt));
    }

    void NetworkSession::RemoveGamer(NetworkGamer* gamer, NetworkSessionEndReason reason)
    {
        if(CNA::Internal::GamerServices::serviceCallsRestricted())throw System::InvalidOperationException("Networking calls are forbidden inside a final leaderboard write handler.");
        bool isLocal = false;
        for (LocalNetworkGamer* local : localGamers_)
        {
            if (local == gamer)
            {
                isLocal = true;
                break;
            }
        }

        if(isLocal&&!leaderboardGameplay_.empty()&&sessionState_==NetworkSessionState::Playing)FinalizeServiceLeaderboards(true);
        if(!isLocal&&online_&&sessionType_==NetworkSessionType::Ranked&&!leaderboardGameplay_.empty()&&sessionState_==NetworkSessionState::Playing) {
            // The remaining machines write the departing gamer's (bad) statistics while it is
            // still a round member; they are submitted with this machine's final report.
            CNA::Internal::GamerServices::withRestrictedServiceCalls([&] {
                WriteArbitratedLeaderboard.Raise(this,WriteLeaderboardsEventArgs::CreateInternal(gamer,true));
                WriteTrueSkill.Raise(this,WriteLeaderboardsEventArgs::CreateInternal(gamer,true));
            });
            roundDeparted_.push_back(gamer);
        }
        gamer->SetHasLeftSession(true);
        gamer->GetSharedMachine()->RemoveGamerInternal(gamer);
        // Task 2.2: localGamers_ was never pruned here, unlike remoteGamers_/allGamers_ just
        // below - a removed local gamer kept appearing in getLocalGamersProperty() forever,
        // breaking the AllGamers == LocalGamers UNION RemoteGamers invariant. Reachable in
        // production via ENetBackend.cpp's RemoveGamer(locals[0], HostEndedSession) call.
        if (isLocal)
        {
            localGamers_.Remove(static_cast<LocalNetworkGamer*>(gamer));
        }
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
        std::unique_ptr<System::IAsyncResult> result(BeginCreate(sessionType, maxLocalGamers, maxGamers, System::AsyncCallback{}, std::any{}));
        return EndCreate(result.get());
    }

    NetworkSession* NetworkSession::Create(
        NetworkSessionType sessionType,
        int maxLocalGamers,
        int maxGamers,
        int privateGamerSlots,
        NetworkSessionProperties sessionProperties
    )
    {
        std::unique_ptr<System::IAsyncResult> result(BeginCreate(
            sessionType, maxLocalGamers, maxGamers, privateGamerSlots,
            std::move(sessionProperties), System::AsyncCallback{}, std::any{}
        ));
        return EndCreate(result.get());
    }

    NetworkSession* NetworkSession::Create(
        NetworkSessionType sessionType,
        const std::vector<SignedInGamer*>& localGamers,
        int maxGamers,
        int privateGamerSlots,
        NetworkSessionProperties sessionProperties
    )
    {
        std::unique_ptr<System::IAsyncResult> result(BeginCreate(
            sessionType, localGamers, maxGamers, privateGamerSlots,
            std::move(sessionProperties), System::AsyncCallback{}, std::any{}
        ));
        return EndCreate(result.get());
    }

    System::IAsyncResult* NetworkSession::BeginCreate(
        NetworkSessionType sessionType,
        int maxLocalGamers,
        int maxGamers,
        System::AsyncCallback callback,
        std::any asyncState
    )
    {
        if(CNA::Internal::GamerServices::serviceCallsRestricted())throw System::InvalidOperationException("Networking calls are forbidden inside a final leaderboard write handler.");
        if (maxLocalGamers < 1 || maxLocalGamers > 4)
        {
            throw System::ArgumentOutOfRangeException("maxLocalGamers");
        }
        if (maxGamers < 2 || maxGamers > MaxSupportedGamers)
        {
            throw System::ArgumentOutOfRangeException("maxGamers");
        }

        if (activeAction_ != nullptr || activeSession_ != nullptr)
        {
            throw System::InvalidOperationException();
        }

        activeAction_ = new NetworkSessionAction(
            NetworkSessionOperation::Create, std::move(asyncState), std::move(callback), maxLocalGamers, std::nullopt, 0,
            NetworkSessionProperties{}, sessionType, maxGamers
        );
        if (IsOnlineType(sessionType)) return QueueOnlineSession({}, {});
        return InvokeActiveActionCallback();
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
        if(CNA::Internal::GamerServices::serviceCallsRestricted())throw System::InvalidOperationException("Networking calls are forbidden inside a final leaderboard write handler.");
        if (maxLocalGamers < 1 || maxLocalGamers > 4)
        {
            throw System::ArgumentOutOfRangeException("maxLocalGamers");
        }
        if (maxGamers < 2 || maxGamers > MaxSupportedGamers)
        {
            throw System::ArgumentOutOfRangeException("maxGamers");
        }
        // Reference BeginCreate refuses privateGamerSlots >= maxGamers (Microsoft.Xna.Framework.Net
        // NetworkSession IL): at least one slot always remains public.
        if (privateGamerSlots < 0 || privateGamerSlots >= maxGamers)
        {
            throw System::ArgumentOutOfRangeException("privateGamerSlots");
        }

        if (activeAction_ != nullptr || activeSession_ != nullptr)
        {
            throw System::InvalidOperationException();
        }

        activeAction_ = new NetworkSessionAction(
            NetworkSessionOperation::Create, std::move(asyncState), std::move(callback), maxLocalGamers, std::nullopt, privateGamerSlots,
            std::move(sessionProperties), sessionType, maxGamers
        );
        if (IsOnlineType(sessionType)) return QueueOnlineSession({}, {});
        return InvokeActiveActionCallback();
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
        if(CNA::Internal::GamerServices::serviceCallsRestricted())throw System::InvalidOperationException("Networking calls are forbidden inside a final leaderboard write handler.");
        if (maxGamers < 2 || maxGamers > MaxSupportedGamers)
        {
            throw System::ArgumentOutOfRangeException("maxGamers");
        }
        // Reference BeginCreate refuses privateGamerSlots >= maxGamers (Microsoft.Xna.Framework.Net
        // NetworkSession IL): at least one slot always remains public.
        if (privateGamerSlots < 0 || privateGamerSlots >= maxGamers)
        {
            throw System::ArgumentOutOfRangeException("privateGamerSlots");
        }

        if (activeAction_ != nullptr || activeSession_ != nullptr)
        {
            throw System::InvalidOperationException();
        }

        activeAction_ = new NetworkSessionAction(
            NetworkSessionOperation::Create, std::move(asyncState), std::move(callback), 0, localGamers, privateGamerSlots,
            std::move(sessionProperties), sessionType, maxGamers
        );
        if (IsOnlineType(sessionType)) return QueueOnlineSession({}, {});
        return InvokeActiveActionCallback();
    }

    NetworkSession* NetworkSession::EndCreate(System::IAsyncResult* result)
    {
        if(CNA::Internal::GamerServices::serviceCallsRestricted())throw System::InvalidOperationException("Networking calls are forbidden inside a final leaderboard write handler.");
        auto* action=NetworkSessionAction::PrepareEnd(result, NetworkSessionOperation::Create);
        if (action->online_) return CompleteOnlineSession(action);

        // Task 6.1: the constructor call below can throw (e.g. the maxLocalGamers-only overload
        // falls back to an empty global Gamer::SignedInGamers list, making `host_ =
        // localGamers_[0]` throw) - previously activeAction_ was only cleared *after* this call
        // succeeded, so a throw here left it permanently non-null, bricking every subsequent
        // Begin* call for the rest of the process with InvalidOperationException. Clear it in a
        // catch before rethrowing, same as the already-safe success path below.
        NetworkSession* created;
        try
        {
            // Intentional correction over FNA's stub: XNA documents this exact value as the
            // session limit, and CNA's functional networking consumes it for capacity/discovery.
            created = new NetworkSession(
                action->SessionProperties,
                action->SessionType,
                action->MaxGamers,
                action->MaxPrivateSlots,
                action->MaxLocalGamers,
                action->LocalGamers,
                true // EndCreate: this machine is hosting (see DEFERRED.md item #20)
            );
        }
        catch (...)
        {
            activeAction_ = nullptr;
            throw;
        }

        activeSession_ = created;

        activeAction_ = nullptr;
        return activeSession_;
    }

    System::IAsyncResult* NetworkSession::QueueOnlineSession(const std::string& session, const std::string& invite)
    {
        using namespace CNA::Internal::GamerServices;
        using namespace CNA::Internal::Net;
        std::unique_ptr<NetworkSessionAction> action(activeAction_);
        const bool joining=!session.empty();
        action->onlineGamers_=ServiceLocalGamers(action->MaxLocalGamers,action->LocalGamers);
        OnlineSessionRequest request;
        request.operation=joining ? OnlineSessionRequest::Operation::Join : OnlineSessionRequest::Operation::Create;
        request.kind=action->SessionType==NetworkSessionType::Ranked ? ServiceSessionKind::Ranked : ServiceSessionKind::PlayerMatch;
        std::vector<std::string> names;
        for(auto* gamer:action->onlineGamers_) {
            action->onlineUsers_.push_back(gamer->serviceUserId_);
            names.push_back(gamer->getGamertagProperty());
        }
        request.owner=action->onlineUsers_.front();request.users=action->onlineUsers_;
        if(joining) {request.session=session;request.invite=invite;}
        else {
            request.settings.maxGamers=action->MaxGamers;request.settings.privateSlots=action->MaxPrivateSlots;
            for(int index=0;index<8;++index) request.settings.properties[index]=action->SessionProperties.getItem(index);
        }
        auto origin=backend();
        action->executor_=origin;action->completedSynchronously_=false;action->setIsCompletedProperty(false);
        // Completion captures only the shared Storage; the action alone owns the operation.
        auto storage=action->storage_;
        const auto fixture=onlineSessionFixture();
        action->online_=std::make_unique<OnlineSessionOperation>(origin,std::move(request),std::move(names),[storage] {
            storage->complete=true;storage->wait.Set();
            if(auto* owner=storage->target; owner && owner->Callback) {auto callback=owner->Callback;callback(*owner);}
        },fixture.preparation ? fixture.preparation() : OnlinePreparationDependencies{},
          fixture.realtime ? fixture.realtime() : ServiceENetDependencies{});
        return action.release();
    }

    NetworkSession* NetworkSession::CompleteOnlineSession(NetworkSessionAction* action)
    {
        using namespace CNA::Internal::Net;
        const bool joining=action->Operation!=NetworkSessionOperation::Create;
        activeAction_=nullptr;
        EstablishedOnlineSession established;
        try {established=action->online_->take();}
        catch(...) {throwOnlineEndFailure(std::current_exception(),joining);}
        // Every frozen local gamer must still be published when the session is materialized.
        const auto* published=GamerServices::Gamer::getSignedInGamersProperty();
        for(auto* gamer:action->onlineGamers_)
            if(std::find(published->begin(),published->end(),gamer)==published->end() || gamer->getIsDisposedProperty())
                throw GamerServices::GamerPrivilegeException("A local gamer signed out before the network session was ready.");
        const auto& snapshot=established.engine->snapshot();
        NetworkSessionProperties properties;
        for(int index=0;index<8;++index) properties.setItem(index,snapshot.properties[static_cast<std::size_t>(index)]);
        std::unique_ptr<NetworkSession> created(new NetworkSession(properties,action->SessionType,snapshot.maxGamers,
            snapshot.privateSlots,action->MaxLocalGamers,action->onlineGamers_,!joining));
        // Explicit-gamer creation uses the reference four-gamer local limit; implicit keeps the requested one.
        created->maxLocalGamers_=action->LocalGamers && !joining ? 4 : std::max(action->MaxLocalGamers,
            static_cast<int>(action->onlineGamers_.size()));
        created->online_=std::make_unique<OnlineSessionBinding>(*created,std::move(established),action->onlineUsers_);
        activeSession_=created.get();
        try {
            // Like the reference EndCreateOrJoin, deliver events queued before the caller can subscribe.
            created->Update();
            if(created->sessionState_==NetworkSessionState::Ended || created->getIsHostProperty()==joining)
                throw NetworkSessionJoinException("The requested network session could not be found.",
                    NetworkSessionJoinError::SessionNotFound);
        }catch(...) {
            created->Dispose();
            throw;
        }
        // A consumed invitation cannot be joined again.
        auto& accepted=CNA::Internal::GamerServices::acceptedInvitation();
        if(action->Operation==NetworkSessionOperation::JoinInvited && accepted
            && accepted->invitation.session==created->online_->session()) accepted.reset();
        return created.release();
    }

    // --- Static Find methods ---

    AvailableNetworkSessionCollection NetworkSession::Find(
        NetworkSessionType sessionType,
        int maxLocalGamers,
        NetworkSessionProperties searchProperties
    )
    {
        std::unique_ptr<System::IAsyncResult> result(BeginFind(
            sessionType, maxLocalGamers, std::move(searchProperties), System::AsyncCallback{}, std::any{}
        ));
        return EndFind(result.get());
    }

    AvailableNetworkSessionCollection NetworkSession::Find(
        NetworkSessionType sessionType,
        const std::vector<SignedInGamer*>& localGamers,
        NetworkSessionProperties searchProperties
    )
    {
        std::unique_ptr<System::IAsyncResult> result(BeginFind(
            sessionType, localGamers, std::move(searchProperties), System::AsyncCallback{}, std::any{}
        ));
        return EndFind(result.get());
    }

    std::vector<SignedInGamer*> NetworkSession::ServiceLocalGamers(
        int maxLocalGamers, const std::optional<std::vector<SignedInGamer*>>& explicitGamers)
    {
        using GamerServices::Gamer;
        if(!GamerServices::GamerServicesDispatcher::getIsInitializedProperty()
            || !CNA::Internal::GamerServices::backend()->serviceEnabled())
            throw GamerServices::GamerServicesNotAvailableException("Configure and initialize CNA Gamer Services before online sessions.");
        const auto* published=Gamer::getSignedInGamersProperty();
        std::vector<SignedInGamer*> gamers;
        if(explicitGamers) gamers=*explicitGamers;
        else for(auto* gamer:*published) {
            if(!gamer->getIsGuestProperty() && gamers.size()<static_cast<std::size_t>(maxLocalGamers)) gamers.push_back(gamer);
        }
        if(gamers.empty() || gamers.size()>4) throw System::ArgumentException("localGamers");
        std::set<std::string> users;
        for(auto* gamer:gamers) {
            // Compare against the published list before dereferencing a caller-supplied pointer.
            if(!gamer || std::find(published->begin(),published->end(),gamer)==published->end())
                throw System::ArgumentException("localGamers");
            if(gamer->getIsDisposedProperty()) throw System::ObjectDisposedException("localGamers");
            if(!gamer->getIsSignedInToLiveProperty() || gamer->getIsGuestProperty()
                || !gamer->getPrivilegesProperty().getAllowOnlineSessionsProperty() || gamer->serviceUserId_.empty())
                throw GamerServices::GamerServicesNotAvailableException("A local gamer is not authorized for CNA online sessions.");
            if(!users.insert(gamer->serviceUserId_).second) throw System::ArgumentException("localGamers");
        }
        return gamers;
    }

    System::IAsyncResult* NetworkSession::QueueServiceSearch()
    {
        using namespace CNA::Internal::GamerServices;
        std::unique_ptr<NetworkSessionAction> action(activeAction_);
        action->onlineGamers_=ServiceLocalGamers(action->MaxLocalGamers,action->LocalGamers);
        const auto actor=action->onlineGamers_.front()->serviceUserId_;
        const int locals=action->LocalGamers ? static_cast<int>(action->onlineGamers_.size()) : action->MaxLocalGamers;
        const auto kind=action->SessionType==NetworkSessionType::Ranked ? ServiceSessionKind::Ranked : ServiceSessionKind::PlayerMatch;
        ServiceSessionProperties filters{};
        for(int index=0;index<8;++index) filters[index]=action->SessionProperties.getItem(index);
        auto service=backend();
        auto* executor=service.get();
        // The result owns this executor; a queued job must not own its own backend.
        action->Queue([executor,actor,locals,kind,filters]() -> std::any {
            std::vector<ServiceSessionSnapshot> results;
            std::set<std::string> seen;
            for(int offset=0;offset<256;offset+=32) {
                auto page=executor->sessionDirectory().find(actor,kind,locals,filters,offset,32);
                if(page.start!=offset || page.sessions.size()>32 || (page.more && page.sessions.size()!=32))
                    throw ServiceOperationError("INVALID_RESPONSE");
                for(auto& item:page.sessions) {
                    if(item.kind!=kind || item.currentGamers<1 || item.maxGamers<2 || item.maxGamers>31
                        || item.currentGamers>item.maxGamers || item.privateSlots<0 || item.privateSlots>=item.maxGamers
                        || item.openPublicSlots<locals || item.openPrivateSlots<0 || item.openPrivateSlots>item.privateSlots
                        || item.currentGamers+item.openPublicSlots+item.openPrivateSlots!=item.maxGamers
                        || (item.state!=ServiceSessionState::Lobby && !item.allowJoinInProgress)
                        || item.hostGamertag.empty() || item.hostGamertag.size()>32 || !item.members.empty()
                        || item.session.size()!=32 || !seen.insert(item.session).second)
                        throw ServiceOperationError("INVALID_RESPONSE");
                    for(int index=0;index<8;++index) if(filters[index] && filters[index]!=item.properties[index])
                        throw ServiceOperationError("INVALID_RESPONSE");
                    results.push_back(std::move(item));
                }
                if(!page.more) return results;
            }
            throw ServiceOperationError("LIMIT_EXCEEDED");
        });
        return action.release();
    }

    System::IAsyncResult* NetworkSession::BeginFind(
        NetworkSessionType sessionType,
        int maxLocalGamers,
        NetworkSessionProperties searchProperties,
        System::AsyncCallback callback,
        std::any asyncState
    )
    {
        if(CNA::Internal::GamerServices::serviceCallsRestricted())throw System::InvalidOperationException("Networking calls are forbidden inside a final leaderboard write handler.");
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
            NetworkSessionOperation::Find, std::move(asyncState), std::move(callback), maxLocalGamers, std::nullopt, 0,
            std::move(searchProperties), sessionType
        );
        if(sessionType==NetworkSessionType::PlayerMatch || sessionType==NetworkSessionType::Ranked)
            return QueueServiceSearch();
        return InvokeActiveActionCallback();
    }

    System::IAsyncResult* NetworkSession::BeginFind(
        NetworkSessionType sessionType,
        const std::vector<SignedInGamer*>& localGamers,
        NetworkSessionProperties searchProperties,
        System::AsyncCallback callback,
        std::any asyncState
    )
    {
        if(CNA::Internal::GamerServices::serviceCallsRestricted())throw System::InvalidOperationException("Networking calls are forbidden inside a final leaderboard write handler.");
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
            NetworkSessionOperation::Find, std::move(asyncState), std::move(callback), locals, localGamers, 0,
            std::move(searchProperties), sessionType
        );
        if(sessionType==NetworkSessionType::PlayerMatch || sessionType==NetworkSessionType::Ranked)
            return QueueServiceSearch();
        return InvokeActiveActionCallback();
    }

    AvailableNetworkSessionCollection NetworkSession::EndFind(System::IAsyncResult* result)
    {
        if(CNA::Internal::GamerServices::serviceCallsRestricted())throw System::InvalidOperationException("Networking calls are forbidden inside a final leaderboard write handler.");
        auto* action=NetworkSessionAction::PrepareEnd(result, NetworkSessionOperation::Find);

        NetworkSessionType type = action->SessionType;
        if(type==NetworkSessionType::PlayerMatch || type==NetworkSessionType::Ranked) {
            activeAction_=nullptr;
            auto snapshots=std::any_cast<std::vector<CNA::Internal::GamerServices::ServiceSessionSnapshot>>(action->TakeValue());
            // Joining a listing uses the same local gamers whose search produced it.
            const auto searchers=std::make_shared<const std::vector<SignedInGamer*>>(action->onlineGamers_);
            std::vector<AvailableNetworkSession> available;
            available.reserve(snapshots.size());
            for(auto& snapshot:snapshots) {
                NetworkSessionProperties properties;
                for(int index=0;index<8;++index) properties.setItem(index,snapshot.properties[index]);
                auto item=AvailableNetworkSession::CreateInternal(snapshot.currentGamers,snapshot.hostGamertag,
                    snapshot.openPrivateSlots,snapshot.openPublicSlots,std::move(properties),QualityOfService::CreateInternal(),"",0,type);
                item.serviceSnapshot_=std::make_shared<const CNA::Internal::GamerServices::ServiceSessionSnapshot>(std::move(snapshot));
                item.serviceLocals_=searchers;
                available.push_back(std::move(item));
            }
            return AvailableNetworkSessionCollection::CreateInternal(std::move(available));
        }
        activeAction_ = nullptr;

        if (CNA::Internal::Net::ENetBackend::RealNetworkingEnabled(type))
        {
            return AvailableNetworkSessionCollection::CreateInternal(
                CNA::Internal::Net::ENetDiscoveryService::FindSessions(type)
            );
        }

        // Non-SystemLink types stay fully synthetic: FNA never actually populates a
        // discovered-sessions list in this stub.
        return AvailableNetworkSessionCollection::CreateInternal({});
    }

    // --- Static Join methods ---

    NetworkSession* NetworkSession::Join(const AvailableNetworkSession* availableSession)
    {
        std::unique_ptr<System::IAsyncResult> result(BeginJoin(availableSession, System::AsyncCallback{}, std::any{}));
        return EndJoin(result.get());
    }

    System::IAsyncResult* NetworkSession::BeginJoin(
        const AvailableNetworkSession* availableSession,
        System::AsyncCallback callback,
        std::any asyncState
    )
    {
        if(CNA::Internal::GamerServices::serviceCallsRestricted())throw System::InvalidOperationException("Networking calls are forbidden inside a final leaderboard write handler.");
        if (availableSession == nullptr)
        {
            throw System::ArgumentNullException("availableSession");
        }
        // Reference BeginJoin: a listing whose search collection was disposed cannot be joined.
        if (availableSession->collectionDisposed_ && *availableSession->collectionDisposed_)
        {
            throw System::ObjectDisposedException("availableSession");
        }
        if (activeAction_ != nullptr || activeSession_ != nullptr)
        {
            throw System::InvalidOperationException();
        }

        if (IsOnlineType(availableSession->GetSessionType()))
        {
            // Reference BeginJoin refuses a listing without a live search parent with
            // ObjectDisposedException; only an authenticated service search result has one here.
            if (!availableSession->serviceSnapshot_ || !availableSession->serviceLocals_)
                throw System::ObjectDisposedException("availableSession");
            activeAction_ = new NetworkSessionAction(
                NetworkSessionOperation::Join, std::move(asyncState), std::move(callback),
                static_cast<int>(availableSession->serviceLocals_->size()), *availableSession->serviceLocals_, 0,
                NetworkSessionProperties{}, availableSession->GetSessionType());
            return QueueOnlineSession(availableSession->serviceSnapshot_->session, {});
        }

        // Task 2.15: FNA hardcodes NetworkSessionType.PlayerMatch here (marked FIXME upstream) -
        // harmless in FNA itself (networking is entirely stubbed out there regardless of session
        // type), but a real functional gap in CNA, whose ENet transport is gated specifically on
        // SystemLink: every session produced via the real public Join() entry point would
        // otherwise permanently have real networking disabled. Derive the real type (and stash
        // the connect address/port for EndJoin below) from availableSession instead.
        pendingJoinAddress_ = availableSession->GetConnectAddress();
        pendingJoinPort_ = availableSession->GetConnectPort();
        activeAction_ = new NetworkSessionAction(
            NetworkSessionOperation::Join, std::move(asyncState), std::move(callback), 4, std::nullopt, 0,
            // FNA passes null for SessionProperties here (marked FIXME upstream); substituted
            // with a default instance since this port's SessionProperties isn't nullable.
            NetworkSessionProperties{},
            availableSession->GetSessionType()
        );
        return InvokeActiveActionCallback();
    }

    NetworkSession* NetworkSession::EndJoin(System::IAsyncResult* result)
    {
        if(CNA::Internal::GamerServices::serviceCallsRestricted())throw System::InvalidOperationException("Networking calls are forbidden inside a final leaderboard write handler.");
        auto* action=NetworkSessionAction::PrepareEnd(result, NetworkSessionOperation::Join);
        if (action->online_) return CompleteOnlineSession(action);

        int actionMaxLocalGamers = action->MaxLocalGamers;
        auto actionLocalGamers = action->LocalGamers;
        NetworkSessionType actionSessionType = action->SessionType;
        activeAction_ = nullptr;

        activeSession_ = new NetworkSession(
            // FNA passes null for properties here (marked FIXME upstream); substituted with a
            // default instance since this port's SessionProperties isn't nullable.
            NetworkSessionProperties{},
            actionSessionType,   // Task 2.15: the real type derived in BeginJoin, not a hardcoded one
            MaxSupportedGamers,  // FIXME upstream
            4,                    // FIXME upstream
            actionMaxLocalGamers,
            actionLocalGamers,
            false // EndJoin: this machine is joining someone else's session (see DEFERRED.md item #20)
        );

        // Task 2.15: the constructor above only starts activeSession_'s own ENet host (see its
        // own RealNetworkingEnabled-gated StartHosting call) - actually connecting out to the
        // session being joined is this call's own responsibility, same as ConnectToHost's other
        // production caller. Non-SystemLink session types remain synthetic even if an external
        // caller supplied descriptive host metadata, and a manually-constructed SystemLink entry
        // with no real discovery-sourced connect info (GetConnectAddress() empty) also stays
        // disconnected.
        if (CNA::Internal::Net::ENetBackend::RealNetworkingEnabled(actionSessionType) &&
            !pendingJoinAddress_.empty())
        {
            const auto cleanupFailedJoin = []
            {
                NetworkSession* failedSession = activeSession_;
                if (failedSession != nullptr)
                {
                    failedSession->Dispose();
                    delete failedSession;
                }
                activeSession_ = nullptr;
                pendingJoinAddress_.clear();
                pendingJoinPort_ = 0;
            };

            try
            {
                CNA::Internal::Net::ENetBackend::ConnectToHost(
                    activeSession_, pendingJoinAddress_, pendingJoinPort_
                );

#ifndef __EMSCRIPTEN__
                // XNA 4.0 documents Join as blocking until the operation completes. EndJoin is
                // where CNA's current synchronous action performs the real work, so keep pumping
                // on this same owner thread until ServerWelcome has replaced the construction-
                // time local Host placeholder with the authoritative remote host. Returning any
                // earlier lets a faithful caller send its first packet to itself
                // (ClientServerSample does exactly that before its first Update call).
                NetworkGamer* constructionTimeHost = activeSession_->getHostProperty();
                const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
                while (activeSession_->getHostProperty() == constructionTimeHost &&
                       activeSession_->getSessionStateProperty() != NetworkSessionState::Ended)
                {
                    activeSession_->Update();
                    if (std::chrono::steady_clock::now() >= deadline)
                    {
                        break;
                    }
                    std::this_thread::sleep_for(std::chrono::milliseconds(1));
                }

                if (activeSession_->getHostProperty() == constructionTimeHost)
                {
                    cleanupFailedJoin();
                    throw NetworkSessionJoinException(
                        "The requested network session could not be found.",
                        NetworkSessionJoinError::SessionNotFound
                    );
                }
#endif
            }
            catch (const NetworkSessionJoinException&)
            {
                throw;
            }
            catch (...)
            {
                const std::exception_ptr cause = std::current_exception();
                cleanupFailedJoin();
                throw NetworkSessionJoinException(
                    "The requested network session could not be found.", cause
                );
            }
        }
        pendingJoinAddress_.clear();
        pendingJoinPort_ = 0;

        return activeSession_;
    }

    // --- Static JoinInvited methods ---

    NetworkSession* NetworkSession::JoinInvited(int maxLocalGamers)
    {
        std::unique_ptr<System::IAsyncResult> result(BeginJoinInvited(maxLocalGamers, System::AsyncCallback{}, std::any{}));
        return EndJoinInvited(result.get());
    }

    NetworkSession* NetworkSession::JoinInvited(const std::vector<SignedInGamer*>& localGamers)
    {
        std::unique_ptr<System::IAsyncResult> result(BeginJoinInvited(localGamers, System::AsyncCallback{}, std::any{}));
        return EndJoinInvited(result.get());
    }

    System::IAsyncResult* NetworkSession::BeginJoinInvited(
        int maxLocalGamers,
        System::AsyncCallback callback,
        std::any asyncState
    )
    {
        ThrowIfRestricted();
        if (maxLocalGamers < 1 || maxLocalGamers > 4)
        {
            throw System::ArgumentOutOfRangeException("maxLocalGamers");
        }
        if (activeAction_ != nullptr || activeSession_ != nullptr)
        {
            throw System::InvalidOperationException();
        }
        return QueueInvitedSession(maxLocalGamers, std::nullopt, std::move(callback), std::move(asyncState));
    }

    System::IAsyncResult* NetworkSession::BeginJoinInvited(
        const std::vector<SignedInGamer*>& localGamers,
        System::AsyncCallback callback,
        std::any asyncState
    )
    {
        ThrowIfRestricted();
        // Reference GetLocalGamers: null entries, disposed gamers and an empty list are refused first.
        for (SignedInGamer* gamer : localGamers)
        {
            if (gamer == nullptr) throw System::ArgumentException("Gamer is null.", "localGamers");
        }
        if (localGamers.empty() || localGamers.size() > 4) throw System::ArgumentException("localGamers");
        if (activeAction_ != nullptr || activeSession_ != nullptr)
        {
            throw System::InvalidOperationException();
        }
        return QueueInvitedSession(static_cast<int>(localGamers.size()), localGamers, std::move(callback), std::move(asyncState));
    }

    System::IAsyncResult* NetworkSession::QueueInvitedSession(
        int maxLocalGamers, std::optional<std::vector<SignedInGamer*>> localGamers,
        System::AsyncCallback callback, std::any asyncState)
    {
        using namespace CNA::Internal::GamerServices;
        if(!GamerServices::GamerServicesDispatcher::getIsInitializedProperty() || !backend()->serviceEnabled())
            throw GamerServices::GamerServicesNotAvailableException("Configure and initialize CNA Gamer Services before joining an invitation.");
        // Only a Guide-accepted invitation can be joined (reference NotInvited/InviteeNotSignedIn).
        const auto& accepted=acceptedInvitation();
        if(!accepted) throw System::InvalidOperationException("No game invitation has been accepted.");
        const auto* published=GamerServices::Gamer::getSignedInGamersProperty();
        auto* invitee=accepted->gamer;
        if(std::find(published->begin(),published->end(),invitee)==published->end() || invitee->serviceUserId_!=accepted->user)
            throw System::InvalidOperationException("The gamer who accepted the invitation is no longer signed in.");
        // The invitee owns the join request, so it always comes first.
        std::vector<SignedInGamer*> gamers{invitee};
        if(localGamers) {
            if(std::find(localGamers->begin(),localGamers->end(),invitee)==localGamers->end())
                throw System::InvalidOperationException("The gamer who accepted the invitation must join.");
            for(auto* gamer:*localGamers) if(gamer!=invitee) gamers.push_back(gamer);
        } else {
            for(auto* gamer:*published)
                if(gamer!=invitee && !gamer->getIsGuestProperty() && gamers.size()<static_cast<std::size_t>(maxLocalGamers)) gamers.push_back(gamer);
        }
        const auto type=accepted->invitation.kind==ServiceSessionKind::Ranked ? NetworkSessionType::Ranked : NetworkSessionType::PlayerMatch;
        activeAction_ = new NetworkSessionAction(
            NetworkSessionOperation::JoinInvited, std::move(asyncState), std::move(callback),
            static_cast<int>(gamers.size()), gamers, 0, NetworkSessionProperties{}, type);
        return QueueOnlineSession(accepted->invitation.session, accepted->invitation.invite);
    }

    NetworkSession* NetworkSession::EndJoinInvited(System::IAsyncResult* result)
    {
        ThrowIfRestricted();
        auto* action=NetworkSessionAction::PrepareEnd(result, NetworkSessionOperation::JoinInvited);
        return CompleteOnlineSession(action);
    }

    bool NetworkSession::InstallInviteSink()
    {
        CNA::Internal::GamerServices::setInviteAcceptedSink([](const CNA::Internal::GamerServices::AcceptedInvitation& accepted) {
            DeliverInviteAccepted(accepted.gamer, accepted.invitation.session);
        });
        // Reference InviteAccepted add accessor: a pending acceptance is delivered to the first subscriber.
        InviteAccepted.SetReplayHook([](const System::EventHandler<GamerServices::InviteAcceptedEventArgs>::HandlerType& handler) {
            if(!pendingInviteAccepted_) return;
            const auto args=*pendingInviteAccepted_;pendingInviteAccepted_.reset();
            handler(nullptr, args);
        });
        return true;
    }

    void NetworkSession::DeliverInviteAccepted(SignedInGamer* gamer, const std::string& session)
    {
        const bool current=activeSession_ && activeSession_->online_ && activeSession_->online_->session()==session;
        GamerServices::InviteAcceptedEventArgs args(gamer, current);
        if(InviteAccepted.Empty()) pendingInviteAccepted_=args;
        else {pendingInviteAccepted_.reset();InviteAccepted.Raise(nullptr, args);}
    }
}
