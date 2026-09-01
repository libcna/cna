// SPDX-License-Identifier: MS-PL
#include "Microsoft/Xna/Framework/Net/NetworkSession.hpp"
#include "CNA/Internal/Net/ENetBackend.hpp"
#include "CNA/Internal/Net/ENetDiscoveryService.hpp"
#include "Microsoft/Xna/Framework/GamerServices/Gamer.hpp"
#include "Microsoft/Xna/Framework/GamerServices/GamerServicesDispatcher.hpp"
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
#include <thread>

namespace Microsoft::Xna::Framework::Net
{
    using GamerServices::SignedInGamer;

    NetworkSession::NetworkSessionAction* NetworkSession::activeAction_ = nullptr;
    NetworkSession* NetworkSession::activeSession_ = nullptr;
    System::EventHandler<GamerServices::InviteAcceptedEventArgs> NetworkSession::InviteAccepted;
    std::string NetworkSession::pendingJoinAddress_;
    uint16_t NetworkSession::pendingJoinPort_ = 0;
    int NetworkSession::instanceCount_ = 0;

    // --- NetworkSessionAction ---

    NetworkSession::NetworkSessionAction::NetworkSessionAction(
        std::any state,
        System::AsyncCallback callback,
        int maxLocal,
        std::optional<std::vector<SignedInGamer*>> localGamers,
        int maxPrivateSlots,
        NetworkSessionProperties properties,
        NetworkSessionType type,
        int maxGamers
    )
        : Callback(std::move(callback))
        , MaxLocalGamers(maxLocal)
        , MaxGamers(maxGamers)
        , LocalGamers(std::move(localGamers))
        , MaxPrivateSlots(maxPrivateSlots)
        , SessionProperties(std::move(properties))
        , SessionType(type)
        , asyncState_(std::move(state))
        // Deviation from FNA: FNA's NetworkSessionAction sets IsCompleted = false here and relies
        // on GamerServicesDispatcher.Update() to eventually complete it. GamerServicesDispatcher's
        // Update() is a permanently empty no-op in both FNA and CNA, so once a
        // GamerServicesComponent exists (isInitialized == true, i.e. every real sample's own
        // constructor), UpdateAsync() unconditionally returns true forever and nothing ever
        // completes the pending action - Create()/Find()/Join()'s synchronous polling loop spins
        // forever (confirmed to reproduce identically against the real FNA reference source, not
        // just CNA's port). CNA's Create/Find/Join/JoinInvited already perform all of their real
        // work synchronously inside EndCreate/EndFind/EndJoin/EndJoinInvited (constructing the
        // NetworkSession or calling into ENetBackend/ENetDiscoveryService directly, no multi-frame
        // deferred I/O), so there is no real pending operation for the loop to ever be waiting on;
        // marking every action completed immediately is a correctness fix, not a design change.
        , isCompleted_(true)
        , asyncWaitHandle_(true, System::Threading::EventResetMode::ManualReset)
    {
        ++instanceCount_;
    }

    int NetworkSession::NetworkSessionAction::instanceCount_ = 0;

    NetworkSession::NetworkSessionAction::~NetworkSessionAction()
    {
        --instanceCount_;
    }

    int NetworkSession::NetworkSessionAction::GetInstanceCountForTesting()
    {
        return instanceCount_;
    }

    const std::any& NetworkSession::NetworkSessionAction::getAsyncStateProperty() const { return asyncState_; }
    bool NetworkSession::NetworkSessionAction::getCompletedSynchronouslyProperty() const { return false; }
    bool NetworkSession::NetworkSessionAction::getIsCompletedProperty() const { return isCompleted_; }
    void NetworkSession::NetworkSessionAction::setIsCompletedProperty(bool value) { isCompleted_ = value; }

    System::Threading::WaitHandle& NetworkSession::NetworkSessionAction::getAsyncWaitHandleProperty() const
    {
        return asyncWaitHandle_;
    }

    // Task 12/audit_net.md High finding: the public headers document that AsyncCallback runs on
    // completion, but every Begin* used to only store it in NetworkSessionAction, never invoke it.
    // Every Begin* already completes activeAction_ synchronously at construction time, so this is
    // called once, right after activeAction_ is assigned - never from inside NetworkSessionAction's
    // own constructor, so a re-entrant callback always observes activeAction_ already installed.
    NetworkSession::NetworkSessionAction* NetworkSession::InvokeActiveActionCallback()
    {
        // Captured before invoking, not re-read from activeAction_ afterward: a re-entrant
        // callback that calls the matching End* nulls activeAction_ as a side effect, and the
        // caller (some Begin* overload, about to `return` this) must still get back the action it
        // just created, not that now-stale null.
        NetworkSessionAction* action = activeAction_;
        if (action->Callback)
        {
            action->Callback(*action);
        }
        return action;
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
        for (LocalNetworkGamer* l : locals)
        {
            l->SetIsHost(isHost_);
            l->SetId(nextLocalGamerId_++);
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
            Dispose();
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

        for (LocalNetworkGamer* gamer : localGamers_)
        {
            gamer->ClearPacketQueue();
        }
        if (CNA::Internal::Net::ENetBackend::RealNetworkingEnabled(sessionType_))
        {
            CNA::Internal::Net::ENetBackend::TeardownSession(this);
        }
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
        if (isDisposed_)
        {
            throw System::ObjectDisposedException("this");
        }

        if (CNA::Internal::Net::ENetBackend::RealNetworkingEnabled(sessionType_))
        {
            CNA::Internal::Net::ENetBackend::PumpSession(this);
            CNA::Internal::Net::ENetDiscoveryService::Poll();
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
                if (evt.State == NetworkSessionState::Playing)
                {
                    GameStarted.Raise(this, GameStartedEventArgs());
                }
                else if (evt.State == NetworkSessionState::Lobby)
                {
                    GameEnded.Raise(this, GameEndedEventArgs());
                }
                else
                {
                    SessionEnded.Raise(this, NetworkSessionEndedEventArgs(evt.Reason));
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
        adding->SetIsHost(isHost_);
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
        sessionProperties_ = std::move(properties);
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
        int maxGamers,
        System::AsyncCallback callback,
        std::any asyncState
    )
    {
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
            std::move(asyncState), std::move(callback), maxLocalGamers, std::nullopt, 0,
            NetworkSessionProperties{}, sessionType, maxGamers
        );
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
        if (maxLocalGamers < 1 || maxLocalGamers > 4)
        {
            throw System::ArgumentOutOfRangeException("maxLocalGamers");
        }
        if (maxGamers < 2 || maxGamers > MaxSupportedGamers)
        {
            throw System::ArgumentOutOfRangeException("maxGamers");
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
            std::move(sessionProperties), sessionType, maxGamers
        );
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
        if (maxGamers < 2 || maxGamers > MaxSupportedGamers)
        {
            throw System::ArgumentOutOfRangeException("maxGamers");
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
            std::move(asyncState), std::move(callback), 0, localGamers, privateGamerSlots,
            std::move(sessionProperties), sessionType, maxGamers
        );
        return InvokeActiveActionCallback();
    }

    NetworkSession* NetworkSession::EndCreate(System::IAsyncResult* result)
    {
        if (result != activeAction_)
        {
            throw System::ArgumentException("result");
        }

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
                activeAction_->SessionProperties,
                activeAction_->SessionType,
                activeAction_->MaxGamers,
                activeAction_->MaxPrivateSlots,
                activeAction_->MaxLocalGamers,
                activeAction_->LocalGamers,
                true // EndCreate: this machine is hosting (see DEFERRED.md item #20)
            );
        }
        catch (...)
        {
            delete activeAction_;
            activeAction_ = nullptr;
            throw;
        }

        activeSession_ = created;

        // Task 3.2: every End* used to just drop activeAction_ (a bare `= nullptr;`) with no prior
        // delete - `new NetworkSessionAction(...)` in every Begin* permanently leaked one object
        // per Begin*/End* cycle.
        delete activeAction_;
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
        return InvokeActiveActionCallback();
    }

    AvailableNetworkSessionCollection NetworkSession::EndFind(System::IAsyncResult* result)
    {
        if (result != activeAction_)
        {
            throw System::ArgumentException("result");
        }

        NetworkSessionType type = activeAction_->SessionType;
        delete activeAction_; // Task 3.2
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

        // Task 2.15: FNA hardcodes NetworkSessionType.PlayerMatch here (marked FIXME upstream) -
        // harmless in FNA itself (networking is entirely stubbed out there regardless of session
        // type), but a real functional gap in CNA, whose ENet transport is gated specifically on
        // SystemLink: every session produced via the real public Join() entry point would
        // otherwise permanently have real networking disabled. Derive the real type (and stash
        // the connect address/port for EndJoin below) from availableSession instead.
        pendingJoinAddress_ = availableSession->GetConnectAddress();
        pendingJoinPort_ = availableSession->GetConnectPort();
        activeAction_ = new NetworkSessionAction(
            std::move(asyncState), std::move(callback), 4, std::nullopt, 0,
            // FNA passes null for SessionProperties here (marked FIXME upstream); substituted
            // with a default instance since this port's SessionProperties isn't nullable.
            NetworkSessionProperties{},
            availableSession->GetSessionType()
        );
        return InvokeActiveActionCallback();
    }

    NetworkSession* NetworkSession::EndJoin(System::IAsyncResult* result)
    {
        if (result != activeAction_)
        {
            throw System::ArgumentException("result");
        }

        int actionMaxLocalGamers = activeAction_->MaxLocalGamers;
        auto actionLocalGamers = activeAction_->LocalGamers;
        NetworkSessionType actionSessionType = activeAction_->SessionType;
        delete activeAction_; // Task 3.2
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
        return InvokeActiveActionCallback();
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
        return InvokeActiveActionCallback();
    }

    NetworkSession* NetworkSession::EndJoinInvited(System::IAsyncResult* result)
    {
        if (result != activeAction_)
        {
            throw System::ArgumentException("result");
        }

        int actionMaxLocalGamers = activeAction_->MaxLocalGamers;
        auto actionLocalGamers = activeAction_->LocalGamers;
        delete activeAction_; // Task 3.2
        activeAction_ = nullptr;

        activeSession_ = new NetworkSession(
            NetworkSessionProperties{}, // FNA passes null here (marked FIXME upstream); see BeginJoin.
            NetworkSessionType::PlayerMatch, // FIXME upstream
            MaxSupportedGamers,              // FIXME upstream
            4,                                // FIXME upstream
            actionMaxLocalGamers,
            actionLocalGamers,
            false // EndJoinInvited: this machine is joining someone else's session (see DEFERRED.md item #20)
        );
        return activeSession_;
    }
}
