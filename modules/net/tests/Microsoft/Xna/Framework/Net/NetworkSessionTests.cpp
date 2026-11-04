// SPDX-License-Identifier: MS-PL
#include <gtest/gtest.h>

#include <memory>
#include "CNA/Internal/Net/ENetBackend.hpp"
#include "CNA/Internal/Net/ENetHostHandle.hpp"
#include "CNA/Internal/Net/NetPacketCodec.hpp"
#include "CNA/Internal/GamerServices/ServiceInvitations.hpp"
#include "Microsoft/Xna/Framework/Net/GameStartedEventArgs.hpp"
#include "Microsoft/Xna/Framework/Net/HostChangedEventArgs.hpp"
#include "Microsoft/Xna/Framework/GamerServices/Gamer.hpp"
#include "Microsoft/Xna/Framework/GamerServices/SignedInGamer.hpp"
#include "Microsoft/Xna/Framework/GamerServices/SignedInGamerCollection.hpp"
#include "Microsoft/Xna/Framework/Net/LocalNetworkGamer.hpp"
#include "Microsoft/Xna/Framework/Net/NetworkSession.hpp"
#include "Microsoft/Xna/Framework/Net/NetworkSessionJoinException.hpp"
#include "System/ArgumentException.hpp"
#include "System/ArgumentNullException.hpp"
#include "Microsoft/Xna/Framework/GamerServices/GamerServicesNotAvailableException.hpp"
#include "System/ArgumentOutOfRangeException.hpp"
#include "System/InvalidOperationException.hpp"
#include "System/ObjectDisposedException.hpp"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <functional>
#include <future>
#include <string>
#include <thread>

using namespace Microsoft::Xna::Framework::Net;
using Microsoft::Xna::Framework::GamerServices::Gamer;
using Microsoft::Xna::Framework::GamerServices::GamerServicesNotAvailableException;
using Microsoft::Xna::Framework::GamerServices::SignedInGamer;
using Microsoft::Xna::Framework::GamerServices::SignedInGamerCollection;

namespace {
    SignedInGamer MakeSignedInGamer(const std::string& tag = "tag1") {
        return SignedInGamer::CreateInternal(tag);
    }
}

// --- Construction via the explicit-local-gamers Create() overload ---

TEST(NetworkSessionTest, CreateWithExplicitLocalGamers) {
    auto gamer = MakeSignedInGamer();
    NetworkSession* session = NetworkSession::Create(
        NetworkSessionType::Local, std::vector<SignedInGamer*>{&gamer}, 8, 2, NetworkSessionProperties{}
    );
    std::unique_ptr<NetworkSession> sessionOwner(session);

    EXPECT_EQ(session->getSessionTypeProperty(), NetworkSessionType::Local);
    EXPECT_EQ(session->getMaxGamersProperty(), 8);
    EXPECT_EQ(session->getPrivateGamerSlotsProperty(), 2);
    EXPECT_EQ(session->getLocalGamersProperty().getCountProperty(), 1);
    EXPECT_EQ(session->getAllGamersProperty().getCountProperty(), 1);
    EXPECT_EQ(session->getRemoteGamersProperty().getCountProperty(), 0);
    EXPECT_EQ(session->getPreviousGamersProperty().getCountProperty(), 0);
    EXPECT_EQ(session->getHostProperty(), session->getLocalGamersProperty()[0]);
    EXPECT_TRUE(session->getIsHostProperty());
    EXPECT_EQ(session->getSessionStateProperty(), NetworkSessionState::Lobby);
    EXPECT_FALSE(session->getIsDisposedProperty());
    EXPECT_EQ(session->getSimulatedLatencyProperty(), System::TimeSpan::Zero);
    EXPECT_FLOAT_EQ(session->getSimulatedPacketLossProperty(), 0.0f);
    EXPECT_EQ(session->getBytesPerSecondReceivedProperty(), 0);
    EXPECT_EQ(session->getBytesPerSecondSentProperty(), 0);

    session->Dispose();
    EXPECT_TRUE(session->getIsDisposedProperty());
}

TEST(NetworkSessionTest, SessionPropertiesGetterExposesMutableXnaIndexerCollection) {
    auto gamer = MakeSignedInGamer();
    NetworkSession* session = NetworkSession::Create(
        NetworkSessionType::Local, std::vector<SignedInGamer*>{&gamer}, 8, 0, NetworkSessionProperties{}
    );
    std::unique_ptr<NetworkSession> sessionOwner(session);

    NetworkSessionProperties& mutableProperties = session->getSessionPropertiesProperty();
    mutableProperties[0] = 7;
    mutableProperties[1] = std::nullopt;
    mutableProperties[2] = -9;

    const NetworkSession& constSession = *session;
    const NetworkSessionProperties& observed = constSession.getSessionPropertiesProperty();
    ASSERT_EQ(observed.getCountProperty(), 8);
    EXPECT_EQ(observed.getItem(0), 7);
    EXPECT_EQ(observed.getItem(1), std::nullopt);
    EXPECT_EQ(observed.getItem(2), -9);
    auto retained = mutableProperties[0];
    session->Dispose();
    EXPECT_THROW(mutableProperties[0] = 4, System::ObjectDisposedException);
    EXPECT_THROW(mutableProperties.setItem(1, 4), System::ObjectDisposedException);
    EXPECT_THROW(retained = 4, System::ObjectDisposedException);
    EXPECT_THROW(mutableProperties = NetworkSessionProperties{}, System::ObjectDisposedException);
    EXPECT_EQ(mutableProperties.getItem(0), 7);
}

// XNA's property guard (sessionProperties_PropertyChanging IL_001e..0030) reads the session's own
// isHost flag, which only a host change moves (ProcessHostChanged IL_0052..005e); a gamer's host
// bit does not (TQ-043).
TEST(NetworkSessionTest, RetainedPropertyProxyChecksCurrentHostRoleBeforeEachWrite) {
    auto gamer = MakeSignedInGamer();
    NetworkSession* session = NetworkSession::Create(NetworkSessionType::Local,
        std::vector<SignedInGamer*>{&gamer}, 8, 0, NetworkSessionProperties{});
    std::unique_ptr<NetworkSession> sessionOwner(session);
    auto& properties = session->getSessionPropertiesProperty();
    auto retained = properties[7];
    retained = 12;
    auto* local = session->getLocalGamersProperty()[0];
    local->SetIsHost(false);
    EXPECT_TRUE(session->getIsHostProperty());
    retained = 13;
    EXPECT_EQ(properties.getItem(7), 13);
    NetworkGamer remote = NetworkGamer::CreateInternal(session, "RemoteHost");
    session->AddRemoteGamer(&remote);
    NetworkSession::NetworkEvent toRemote;
    toRemote.Type = NetworkSession::NetworkEventType::HostChange;
    toRemote.Gamer = &remote;
    session->SendNetworkEvent(toRemote);
    EXPECT_FALSE(session->getIsHostProperty());
    EXPECT_THROW(retained = 12, System::InvalidOperationException);
    EXPECT_THROW(properties.setItem(7, 14), System::InvalidOperationException);
    EXPECT_EQ(properties.getItem(7), 13);
    NetworkSession::NetworkEvent toLocal;
    toLocal.Type = NetworkSession::NetworkEventType::HostChange;
    toLocal.Gamer = local;
    session->SendNetworkEvent(toLocal);
    retained = 14;
    EXPECT_EQ(properties.getItem(7), 14);
    session->Dispose();
}

// Task 3.2: every End* (EndCreate/EndFind/EndJoin/EndJoinInvited) used to just drop activeAction_
// (a bare `= nullptr;`) with no prior delete - `new NetworkSessionAction(...)` in every Begin*
// permanently leaked one object per Begin*/End* cycle. Confirms a full Create() (BeginCreate ->
// EndCreate) cycle leaves the live-instance count unchanged, proving the action was actually
// freed, not just forgotten.
TEST(NetworkSessionTest, CreateDoesNotLeakNetworkSessionAction) {
    int before = NetworkSession::GetActiveActionInstanceCountForTesting();

    auto gamer = MakeSignedInGamer();
    NetworkSession* session = NetworkSession::Create(
        NetworkSessionType::Local, std::vector<SignedInGamer*>{&gamer}, 8, 0, NetworkSessionProperties{}
    );
    std::unique_ptr<NetworkSession> sessionOwner(session);

    EXPECT_EQ(NetworkSession::GetActiveActionInstanceCountForTesting(), before);

    session->Dispose();
}

// Regression: BeginCreate/BeginFind/BeginJoin/BeginJoinInvited once stored their
// AsyncCallback in NetworkSessionAction but never invoked it, despite NetworkSession.hpp
// documenting that AsyncCallback runs on completion. Confirms the callback now fires exactly
// once, synchronously (matching this action already completing at construction time), with the
// correct IAsyncResult identity and AsyncState.
TEST(NetworkSessionTest, BeginCreateInvokesCallbackExactlyOnceWithCorrectIdentity) {
    SignedInGamer extraGamer = MakeSignedInGamer("CallbackTag");
    Gamer::setSignedInGamersProperty(new SignedInGamerCollection(
        SignedInGamerCollection::CreateInternal({&extraGamer})
    ));
    struct RestoreGlobalGuard {
        ~RestoreGlobalGuard() {
            Gamer::setSignedInGamersProperty(new SignedInGamerCollection(SignedInGamerCollection::CreateInternal({})));
        }
    } restoreGuard;

    int callCount = 0;
    System::IAsyncResult* observedResult = nullptr;
    std::any state = 42;

    System::IAsyncResult* result = NetworkSession::BeginCreate(
        NetworkSessionType::Local, 1, 8,
        [&callCount, &observedResult](System::IAsyncResult& ar) {
            ++callCount;
            observedResult = &ar;
        },
        state
    );
    std::unique_ptr<System::IAsyncResult> resultOwner(result);

    EXPECT_EQ(callCount, 1);
    EXPECT_EQ(observedResult, result);
    ASSERT_TRUE(result->getIsCompletedProperty());
    EXPECT_EQ(std::any_cast<int>(result->getAsyncStateProperty()), 42);

    NetworkSession* session = NetworkSession::EndCreate(result);
    std::unique_ptr<NetworkSession> sessionOwner(session);
    EXPECT_EQ(session->getMaxGamersProperty(), 8);
    session->Dispose();
}

// The most common real APM usage: a callback that immediately consumes its IAsyncResult by
// calling the matching End* from within the callback itself. Confirms this reentrant path leaves
// activeAction_/activeSession_ in a correct, non-stale state - a subsequent Create() must not be
// permanently blocked by leftover state from the reentrant EndCreate call.
TEST(NetworkSessionTest, BeginCreateCallbackCanReentrantlyCallEndCreate) {
    SignedInGamer extraGamer = MakeSignedInGamer("ReentrantTag");
    Gamer::setSignedInGamersProperty(new SignedInGamerCollection(
        SignedInGamerCollection::CreateInternal({&extraGamer})
    ));
    struct RestoreGlobalGuard {
        ~RestoreGlobalGuard() {
            Gamer::setSignedInGamersProperty(new SignedInGamerCollection(SignedInGamerCollection::CreateInternal({})));
        }
    } restoreGuard;

    std::unique_ptr<NetworkSession> completedSession;
    auto* result=NetworkSession::BeginCreate(
        NetworkSessionType::Local, 1, 8,
        [&completedSession](System::IAsyncResult& ar) {
            completedSession.reset(NetworkSession::EndCreate(&ar));
        },
        std::any{}
    );

    std::unique_ptr<System::IAsyncResult> owner(result);
    EXPECT_TRUE(result->getIsCompletedProperty());
    EXPECT_TRUE(result->getCompletedSynchronouslyProperty());
    EXPECT_TRUE(result->getAsyncWaitHandleProperty().WaitOne(0));
    EXPECT_THROW((void)NetworkSession::EndCreate(result), System::InvalidOperationException);
    ASSERT_NE(completedSession, nullptr);
    EXPECT_EQ(completedSession->getSessionTypeProperty(), NetworkSessionType::Local);
    completedSession->Dispose();

    // activeAction_/activeSession_ must be cleared by the reentrant EndCreate, not left stale -
    // a fresh Create() must not throw InvalidOperationException.
    auto gamer2 = MakeSignedInGamer("tag2");
    NetworkSession* second = nullptr;
    EXPECT_NO_THROW(
        second = NetworkSession::Create(
            NetworkSessionType::Local, std::vector<SignedInGamer*>{&gamer2}, 8, 0, NetworkSessionProperties{}
        )
    );
    std::unique_ptr<NetworkSession> secondOwner(second);
    ASSERT_NE(second, nullptr);
    second->Dispose();
}

// Task 3.3: no code path anywhere in this codebase ever deleted a NetworkSession* - Dispose() only
// ever flipped isDisposed_ to true. Confirmed and documented the ownership contract instead of
// changing Dispose() to self-delete (see the class's own doc comment for why: an enormous number
// of existing call sites, throughout this very test suite, legitimately read state - e.g.
// getIsDisposedProperty() - right after calling Dispose(), which a self-deleting Dispose() would
// turn into use-after-free). This test demonstrates the documented contract actually working: the
// caller Dispose()s, then deletes, and the live-instance count returns to baseline either way.
TEST(NetworkSessionTest, DeletingAfterDisposeLeavesNoLeak) {
    int before = NetworkSession::GetInstanceCountForTesting();

    auto gamer = MakeSignedInGamer();
    NetworkSession* session = NetworkSession::Create(
        NetworkSessionType::Local, std::vector<SignedInGamer*>{&gamer}, 8, 0, NetworkSessionProperties{}
    );
    EXPECT_EQ(NetworkSession::GetInstanceCountForTesting(), before + 1);

    session->Dispose();
    delete session; // the documented contract: caller owns the pointer, frees it once done

    EXPECT_EQ(NetworkSession::GetInstanceCountForTesting(), before);
}

// Task 2.1: NetworkSession's destructor previously did not call Dispose(), so deleting a session
// without disposing it first left the activeSession_ singleton dangling at freed memory. Every
// subsequent BeginCreate/BeginFind/BeginJoin checks `activeSession_ != nullptr` and throws
// InvalidOperationException, so one mismanaged delete permanently bricked session creation for
// the rest of the process. This proves the destructor's Dispose() fallback fixes that: a session
// deleted without an explicit Dispose() call must still allow a brand-new session to be created
// afterward.
TEST(NetworkSessionTest, DeletingWithoutDisposeStillAllowsCreatingANewSession) {
    int before = NetworkSession::GetInstanceCountForTesting();

    auto gamer1 = MakeSignedInGamer();
    NetworkSession* first = NetworkSession::Create(
        NetworkSessionType::Local, std::vector<SignedInGamer*>{&gamer1}, 8, 0, NetworkSessionProperties{}
    );
    delete first; // deliberately skips Dispose() - the exact bug scenario

    // Before the fix, activeSession_ still pointed at the just-freed `first`, so this would throw
    // InvalidOperationException ("activeAction_ != nullptr || activeSession_ != nullptr").
    auto gamer2 = MakeSignedInGamer("tag2");
    NetworkSession* second = nullptr;
    EXPECT_NO_THROW(
        second = NetworkSession::Create(
            NetworkSessionType::Local, std::vector<SignedInGamer*>{&gamer2}, 8, 0, NetworkSessionProperties{}
        )
    );
    ASSERT_NE(second, nullptr);

    second->Dispose();
    delete second;

    EXPECT_EQ(NetworkSession::GetInstanceCountForTesting(), before);
}

// Confirms the destructor's Dispose() fallback also tears down real ENet transport state (not
// just the activeSession_ singleton) - a fresh SystemLink session created after a
// delete-without-Dispose gets its own real bound port, proving ENetBackend::TeardownSession
// actually ran for the leaked one.
TEST(NetworkSessionTest, DeletingWithoutDisposeTearsDownRealTransport) {
    auto gamer1 = MakeSignedInGamer();
    NetworkSession* first = NetworkSession::Create(
        NetworkSessionType::SystemLink, std::vector<SignedInGamer*>{&gamer1}, 8, 0, NetworkSessionProperties{}
    );
    EXPECT_GT(CNA::Internal::Net::ENetBackend::GetBoundPort(first), 0);
    delete first; // deliberately skips Dispose()

    auto gamer2 = MakeSignedInGamer("tag2");
    NetworkSession* second = NetworkSession::Create(
        NetworkSessionType::SystemLink, std::vector<SignedInGamer*>{&gamer2}, 8, 0, NetworkSessionProperties{}
    );
    EXPECT_GT(CNA::Internal::Net::ENetBackend::GetBoundPort(second), 0);

    second->Dispose();
    delete second;
}

TEST(NetworkSessionTest, AllowHostMigrationAndJoinInProgressGetSet) {
    auto gamer = MakeSignedInGamer();
    NetworkSession* session = NetworkSession::Create(
        NetworkSessionType::Local, std::vector<SignedInGamer*>{&gamer}, 8, 0, NetworkSessionProperties{}
    );
    std::unique_ptr<NetworkSession> sessionOwner(session);

    session->setAllowHostMigrationProperty(true);
    EXPECT_TRUE(session->getAllowHostMigrationProperty());
    session->setAllowJoinInProgressProperty(true);
    EXPECT_TRUE(session->getAllowJoinInProgressProperty());

    session->Dispose();
}

TEST(NetworkSessionTest, MaxGamersAndPrivateGamerSlotsGetSet) {
    auto gamer = MakeSignedInGamer();
    NetworkSession* session = NetworkSession::Create(
        NetworkSessionType::Local, std::vector<SignedInGamer*>{&gamer}, 8, 0, NetworkSessionProperties{}
    );
    std::unique_ptr<NetworkSession> sessionOwner(session);

    session->setMaxGamersProperty(16);
    EXPECT_EQ(session->getMaxGamersProperty(), 16);
    session->setPrivateGamerSlotsProperty(3);
    EXPECT_EQ(session->getPrivateGamerSlotsProperty(), 3);

    session->Dispose();
}

TEST(NetworkSessionTest, SimulatedLatencyAndPacketLossGetSet) {
    auto gamer = MakeSignedInGamer();
    NetworkSession* session = NetworkSession::Create(
        NetworkSessionType::Local, std::vector<SignedInGamer*>{&gamer}, 8, 0, NetworkSessionProperties{}
    );
    std::unique_ptr<NetworkSession> sessionOwner(session);

    session->setSimulatedLatencyProperty(System::TimeSpan::FromMilliseconds(50));
    EXPECT_EQ(session->getSimulatedLatencyProperty(), System::TimeSpan::FromMilliseconds(50));
    session->setSimulatedPacketLossProperty(0.25f);
    EXPECT_FLOAT_EQ(session->getSimulatedPacketLossProperty(), 0.25f);

    // XNA's NetworkSimulator refuses a negative latency and a loss outside 0..1, keeping the old value.
    EXPECT_THROW(session->setSimulatedLatencyProperty(System::TimeSpan::FromMilliseconds(-1)),
                 System::ArgumentOutOfRangeException);
    EXPECT_THROW(session->setSimulatedPacketLossProperty(-0.5f), System::ArgumentOutOfRangeException);
    EXPECT_THROW(session->setSimulatedPacketLossProperty(1.5f), System::ArgumentOutOfRangeException);
    EXPECT_EQ(session->getSimulatedLatencyProperty(), System::TimeSpan::FromMilliseconds(50));
    EXPECT_FLOAT_EQ(session->getSimulatedPacketLossProperty(), 0.25f);

    session->Dispose();
}

TEST(NetworkSessionTest, IsEveryoneReadyReflectsLocalGamers) {
    auto gamer = MakeSignedInGamer();
    NetworkSession* session = NetworkSession::Create(
        NetworkSessionType::Local, std::vector<SignedInGamer*>{&gamer}, 8, 0, NetworkSessionProperties{}
    );
    std::unique_ptr<NetworkSession> sessionOwner(session);

    EXPECT_FALSE(session->getIsEveryoneReadyProperty());
    session->getLocalGamersProperty()[0]->setIsReadyProperty(true);
    EXPECT_TRUE(session->getIsEveryoneReadyProperty());

    // XNA get_IsEveryoneReady IL_0000..0033 walks AllGamers: a remote gamer that is not ready
    // keeps it false; and an empty session is not ready (IL_000c..0010).
    NetworkGamer remote = NetworkGamer::CreateInternal(session, "NotReadyRemote");
    session->AddRemoteGamer(&remote);
    EXPECT_FALSE(session->getIsEveryoneReadyProperty());
    session->RemoveGamer(&remote, NetworkSessionEndReason::Disconnected);
    EXPECT_TRUE(session->getIsEveryoneReadyProperty());
    session->RemoveGamer(session->getLocalGamersProperty()[0], NetworkSessionEndReason::ClientSignedOut);
    ASSERT_EQ(session->getAllGamersProperty().getCountProperty(), 0);
    EXPECT_FALSE(session->getIsEveryoneReadyProperty());

    session->Dispose();
}

TEST(NetworkSessionTest, IsReadyIsSetOnlyInTheLobbyAndClearsWhenTheGameEnds) {
    auto gamer = MakeSignedInGamer();
    NetworkSession* session = NetworkSession::Create(
        NetworkSessionType::Local, std::vector<SignedInGamer*>{&gamer}, 8, 0, NetworkSessionProperties{}
    );
    std::unique_ptr<NetworkSession> sessionOwner(session);
    auto* local = session->getLocalGamersProperty()[0];

    local->setIsReadyProperty(true);
    session->StartGame();
    session->Update();
    ASSERT_EQ(session->getSessionStateProperty(), NetworkSessionState::Playing);
    EXPECT_TRUE(local->getIsReadyProperty());
    EXPECT_THROW(local->setIsReadyProperty(false), System::InvalidOperationException);
    EXPECT_THROW(session->ResetReady(), System::InvalidOperationException);

    // Every machine starts the next lobby with nobody ready.
    session->EndGame();
    session->Update();
    ASSERT_EQ(session->getSessionStateProperty(), NetworkSessionState::Lobby);
    EXPECT_FALSE(local->getIsReadyProperty());
    EXPECT_FALSE(session->getIsEveryoneReadyProperty());

    session->Dispose();
}

TEST(NetworkSessionTest, ResetReadySetsAllGamersNotReady) {
    auto gamer = MakeSignedInGamer();
    NetworkSession* session = NetworkSession::Create(
        NetworkSessionType::Local, std::vector<SignedInGamer*>{&gamer}, 8, 0, NetworkSessionProperties{}
    );
    std::unique_ptr<NetworkSession> sessionOwner(session);

    session->getLocalGamersProperty()[0]->setIsReadyProperty(true);
    session->ResetReady();
    EXPECT_FALSE(session->getLocalGamersProperty()[0]->getIsReadyProperty());

    session->Dispose();
}

TEST(NetworkSessionTest, StartGameThenEndGameTransitionsState) {
    auto gamer = MakeSignedInGamer();
    NetworkSession* session = NetworkSession::Create(
        NetworkSessionType::Local, std::vector<SignedInGamer*>{&gamer}, 8, 0, NetworkSessionProperties{}
    );
    std::unique_ptr<NetworkSession> sessionOwner(session);

    session->StartGame();
    session->Update();
    EXPECT_EQ(session->getSessionStateProperty(), NetworkSessionState::Playing);

    session->EndGame();
    session->Update();
    EXPECT_EQ(session->getSessionStateProperty(), NetworkSessionState::Lobby);

    session->Dispose();
}

// Task 5.19: WriteArbitratedLeaderboard/WriteUnarbitratedLeaderboard/WriteTrueSkill on a Local
// session, which writes no leaderboards (with a CNA service, LocalWithLeaderboards, PlayerMatch and
// Ranked raise them: OnlineLeaderboardTest and the service client harness). Subscribing to all three and exercising a full Create ->
// StartGame -> EndGame -> Dispose lifecycle proves each event exists under its exact name and that
// a Local session never raises them.
TEST(NetworkSessionTest, WriteLeaderboardAndTrueSkillEventsAreNeverRaised) {
    auto gamer = MakeSignedInGamer();
    NetworkSession* session = NetworkSession::Create(
        NetworkSessionType::Local, std::vector<SignedInGamer*>{&gamer}, 8, 0, NetworkSessionProperties{}
    );
    std::unique_ptr<NetworkSession> sessionOwner(session);

    int arbitratedCount = 0;
    int unarbitratedCount = 0;
    int trueSkillCount = 0;
    session->WriteArbitratedLeaderboard += [&](System::Object*, const WriteLeaderboardsEventArgs&) { ++arbitratedCount; };
    session->WriteUnarbitratedLeaderboard += [&](System::Object*, const WriteLeaderboardsEventArgs&) { ++unarbitratedCount; };
    session->WriteTrueSkill += [&](System::Object*, const WriteLeaderboardsEventArgs&) { ++trueSkillCount; };

    session->Update();
    session->StartGame();
    session->Update();
    session->EndGame();
    session->Update();

    EXPECT_EQ(arbitratedCount, 0);
    EXPECT_EQ(unarbitratedCount, 0);
    EXPECT_EQ(trueSkillCount, 0);

    session->Dispose();
}

TEST(NetworkSessionTest, StartGameWhileNotInLobbyThrows) {
    auto gamer = MakeSignedInGamer();
    NetworkSession* session = NetworkSession::Create(
        NetworkSessionType::Local, std::vector<SignedInGamer*>{&gamer}, 8, 0, NetworkSessionProperties{}
    );
    std::unique_ptr<NetworkSession> sessionOwner(session);

    session->StartGame();
    session->Update();
    EXPECT_THROW(session->StartGame(), System::InvalidOperationException);

    session->EndGame();
    session->Update();
    session->Dispose();
}

TEST(NetworkSessionTest, EndGameWhileNotPlayingThrows) {
    auto gamer = MakeSignedInGamer();
    NetworkSession* session = NetworkSession::Create(
        NetworkSessionType::Local, std::vector<SignedInGamer*>{&gamer}, 8, 0, NetworkSessionProperties{}
    );
    std::unique_ptr<NetworkSession> sessionOwner(session);

    EXPECT_THROW(session->EndGame(), System::InvalidOperationException);

    session->Dispose();
}

// Reference Create with an explicit list uses a local-gamer limit of 4, whatever the list's length,
// and a gamer listed twice joins once.
TEST(NetworkSessionTest, AddLocalGamerThrowsAtMaxLimit) {
    auto gamer = MakeSignedInGamer();
    auto second = MakeSignedInGamer("tag2");
    auto third = MakeSignedInGamer("tag3");
    auto fourth = MakeSignedInGamer("tag4");
    auto fifth = MakeSignedInGamer("tag5");
    NetworkSession* session = NetworkSession::Create(
        NetworkSessionType::Local, std::vector<SignedInGamer*>{&gamer, &gamer}, 8, 0, NetworkSessionProperties{}
    );
    std::unique_ptr<NetworkSession> sessionOwner(session);
    ASSERT_EQ(session->getLocalGamersProperty().getCountProperty(), 1);

    session->AddLocalGamer(&second);
    session->AddLocalGamer(&third);
    session->AddLocalGamer(&fourth);
    // The three wait for the next Update, and already fill the limit (TQ-039).
    EXPECT_EQ(session->getLocalGamersProperty().getCountProperty(), 1);
    EXPECT_THROW(session->AddLocalGamer(&fifth), System::InvalidOperationException);
    session->Update();
    EXPECT_EQ(session->getLocalGamersProperty().getCountProperty(), 4);
    EXPECT_THROW(session->AddLocalGamer(&fifth), System::InvalidOperationException);

    session->Dispose();
}

// XNA AddLocalGamer checks a null gamer (IL_0000..000d), a disposed gamer (IL_000e..0025), a
// disposed session (IL_0026..0043) and a gamer already in the session (IL_0044..008c), then the
// state and the public slots, and leaves the local-gamer limit to its kernel (IL_00ef..0192),
// whose refusal managed IL cannot name. CNA keeps its limit refusal after every reference check,
// and a refused gamer is never queued (TQ-043, retained boundary).
TEST(NetworkSessionTest, AddLocalGamerChecksTheLocalLimitAfterEveryReferenceCheck) {
    SignedInGamer first = MakeSignedInGamer("First");
    Gamer::setSignedInGamersProperty(new SignedInGamerCollection(SignedInGamerCollection::CreateInternal({&first})));
    struct RestoreGlobalGuard {
        ~RestoreGlobalGuard() {
            Gamer::setSignedInGamersProperty(new SignedInGamerCollection(SignedInGamerCollection::CreateInternal({})));
        }
    } restoreGuard;
    SignedInGamer second = MakeSignedInGamer("Second");
    SignedInGamer disposed = MakeSignedInGamer("Disposed");
    CNA::Internal::GamerServices::GamerAccess::dispose(disposed);
    // A local-gamer limit of 1, already reached by the signed-in gamer; 8 slots leave room.
    std::unique_ptr<NetworkSession> session(NetworkSession::Create(NetworkSessionType::Local, 1, 8));
    ASSERT_EQ(session->getLocalGamersProperty().getCountProperty(), 1);
    int joined = 0;
    session->GamerJoined += [&](System::Object*, const GamerJoinedEventArgs&) { ++joined; };
    joined = 0;

    try {
        session->AddLocalGamer(nullptr);
        ADD_FAILURE() << "a null gamer was accepted";
    } catch (const System::ArgumentNullException& e) {
        EXPECT_EQ(e.getParamNameProperty(), "gamer");
    } catch (const std::exception& e) {
        ADD_FAILURE() << "refused by a later check: " << e.what();
    }
    try {
        session->AddLocalGamer(&disposed);
        ADD_FAILURE() << "a disposed gamer was accepted";
    } catch (const System::ObjectDisposedException&) {
    } catch (const std::exception& e) {
        ADD_FAILURE() << "refused by a later check: " << e.what();
    }
    try {
        session->AddLocalGamer(&first);
        ADD_FAILURE() << "a gamer already in the session was accepted";
    } catch (const System::ArgumentNullException&) {
        ADD_FAILURE() << "refused as null";
    } catch (const System::ArgumentException& e) {
        EXPECT_EQ(e.getParamNameProperty(), "gamer");
    } catch (const std::exception& e) {
        ADD_FAILURE() << "refused by a later check: " << e.what();
    }
    EXPECT_THROW(session->AddLocalGamer(&second), System::InvalidOperationException);
    session->Update();
    EXPECT_EQ(joined, 0);
    EXPECT_EQ(session->getLocalGamersProperty().getCountProperty(), 1);
    EXPECT_EQ(session->GetOwnedGamerCountForTesting(), 1u);
    session->Dispose();
    EXPECT_THROW(session->AddLocalGamer(&second), System::ObjectDisposedException);
}

// Reference GetLocalGamers runs before every other check of an explicit-list Create, Find or
// JoinInvited: a null entry or an empty list is an ArgumentException, a disposed gamer an
// ObjectDisposedException. (Online requests look a gamer up among the signed-in gamers before
// touching it, so a signed-out one is refused there as not signed in.)
TEST(NetworkSessionTest, ExplicitLocalGamerListsAreCheckedFirst) {
    SignedInGamer* none = nullptr;
    // maxGamers 0 (below every type's minimum) and a Local Find are invalid too; the list is
    // reported instead. ArgumentOutOfRangeException is an ArgumentException, so the name decides.
    const std::vector<SignedInGamer*> invalidLists[] = {{}, {none}};
    for (const auto& list : invalidLists) {
        try {
            (void)NetworkSession::BeginCreate(NetworkSessionType::Local, list, 0, 0,
                NetworkSessionProperties{}, System::AsyncCallback{}, std::any{});
            ADD_FAILURE() << "an invalid list was accepted";
        } catch (const System::ArgumentException& e) {
            EXPECT_EQ(e.getParamNameProperty(), "localGamers");
        }
    }
    try {
        (void)NetworkSession::BeginFind(NetworkSessionType::Local, std::vector<SignedInGamer*>{}, NetworkSessionProperties{},
            System::AsyncCallback{}, std::any{});
        ADD_FAILURE() << "an empty list was accepted";
    } catch (const System::ArgumentException& e) {
        EXPECT_EQ(e.getParamNameProperty(), "localGamers");
    }
    EXPECT_THROW(NetworkSession::BeginJoinInvited(std::vector<SignedInGamer*>{}, System::AsyncCallback{}, std::any{}),
                 System::ArgumentException);

    SignedInGamer disposed = MakeSignedInGamer("Disposed");
    CNA::Internal::GamerServices::GamerAccess::dispose(disposed);
    EXPECT_THROW(NetworkSession::BeginCreate(NetworkSessionType::Local, std::vector<SignedInGamer*>{&disposed}, 1, 0,
                     NetworkSessionProperties{}, System::AsyncCallback{}, std::any{}), System::ObjectDisposedException);
    EXPECT_THROW(NetworkSession::BeginFind(NetworkSessionType::Local, std::vector<SignedInGamer*>{&disposed},
                     NetworkSessionProperties{}, System::AsyncCallback{}, std::any{}), System::ObjectDisposedException);
}

TEST(NetworkSessionTest, FindGamerByIdMatchesSoleLocalGamer) {
    auto gamer = MakeSignedInGamer();
    NetworkSession* session = NetworkSession::Create(
        NetworkSessionType::Local, std::vector<SignedInGamer*>{&gamer}, 8, 0, NetworkSessionProperties{}
    );
    std::unique_ptr<NetworkSession> sessionOwner(session);

    EXPECT_EQ(session->FindGamerById(0), session->getAllGamersProperty()[0]);
    EXPECT_EQ(session->FindGamerById(1), nullptr);

    session->Dispose();
}

// Real, distinct per-gamer ids (see DEFERRED.md item #20 in the sibling cna-samples repo) — a
// multi-gamer session used to have every gamer report Id == 0, so FindGamerById always resolved
// to the first gamer regardless of which id was requested.
TEST(NetworkSessionTest, MultipleLocalGamersGetDistinctIdsAndFindGamerByIdRoutesCorrectly) {
    auto gamer1 = MakeSignedInGamer("tag1");
    auto gamer2 = MakeSignedInGamer("tag2");
    NetworkSession* session = NetworkSession::Create(
        NetworkSessionType::Local, std::vector<SignedInGamer*>{&gamer1, &gamer2}, 8, 0, NetworkSessionProperties{}
    );
    std::unique_ptr<NetworkSession> sessionOwner(session);

    LocalNetworkGamer* first = session->getLocalGamersProperty()[0];
    LocalNetworkGamer* second = session->getLocalGamersProperty()[1];
    EXPECT_EQ(first->getIdProperty(), 0);
    EXPECT_EQ(second->getIdProperty(), 1);
    EXPECT_EQ(session->FindGamerById(0), first);
    EXPECT_EQ(session->FindGamerById(1), second);
    EXPECT_EQ(session->FindGamerById(2), nullptr);

    session->Dispose();
}

// NetworkGamer::IsHost (see DEFERRED.md item #20): real per-instance state derived from whether
// the owning NetworkSession was created via Create() (host) vs. Join()/JoinInvited() (client),
// instead of FNA's hardcoded-true stub that made every gamer, on every machine, report host.
TEST(NetworkSessionTest, CreateMakesLocalGamersReportIsHostTrue) {
    auto gamer = MakeSignedInGamer();
    NetworkSession* session = NetworkSession::Create(
        NetworkSessionType::Local, std::vector<SignedInGamer*>{&gamer}, 8, 0, NetworkSessionProperties{}
    );
    std::unique_ptr<NetworkSession> sessionOwner(session);

    EXPECT_TRUE(session->getLocalGamersProperty()[0]->getIsHostProperty());
    EXPECT_TRUE(session->getIsHostProperty());

    session->Dispose();
}

// "A joiner is not the host" is still worth pinning, but JoinInvited can no longer produce a
// session to check it on (see JoinInvitedWithExplicitLocalGamersRefuses). Join() is the remaining
// route by which this machine joins someone else's session, so it carries the assertion now.
TEST(NetworkSessionTest, AJoinedSessionMakesLocalGamersReportIsHostFalse) {
    auto gamer = MakeSignedInGamer();
    Gamer::setSignedInGamersProperty(new SignedInGamerCollection(
        SignedInGamerCollection::CreateInternal({&gamer})
    ));
    struct RestoreGlobalGuard {
        ~RestoreGlobalGuard() {
            Gamer::setSignedInGamersProperty(
                new SignedInGamerCollection(SignedInGamerCollection::CreateInternal({}))
            );
        }
    } restoreGuard;

    AvailableNetworkSession availableSession = AvailableNetworkSession::CreateInternal(
        1, "SyntheticHost", 0, 8, NetworkSessionProperties{},
        QualityOfService::CreateInternal(), "127.0.0.1", 27015,
        NetworkSessionType::Local
    );

    NetworkSession* session = NetworkSession::Join(&availableSession);
    ASSERT_NE(session, nullptr);

    EXPECT_FALSE(session->getLocalGamersProperty()[0]->getIsHostProperty());
    EXPECT_FALSE(session->getIsHostProperty());

    session->Dispose();
    delete session;
}

// NOTE: the maxLocalGamers-only overload falls back to the global Gamer::SignedInGamers, which
// defaults to empty in this test binary. An empty list makes the constructor's
// `host_ = localGamers_[0]` throw - Task 6.1 fixed EndCreate so that no longer permanently
// corrupts activeAction_ (see FailedCreateDoesNotPermanentlyStrandActiveAction just below, which
// now exercises this throw directly instead of avoiding it). Every other test in this file still
// gets spare local-gamer capacity safely by temporarily installing its own one-gamer global list
// (restored via RAII) before using the maxLocalGamers-only overload, simply because that's the
// realistic, working way to use this overload - not because the empty-list path is unsafe to
// exercise anymore.

// Task 6.1: EndCreate used to construct the new NetworkSession *before* clearing activeAction_ -
// a throwing constructor (confirmed reachable via the empty-global-SignedInGamers path documented
// above) left activeAction_ permanently non-null, bricking every subsequent Begin* call for the
// rest of the process with InvalidOperationException. Fixed: EndCreate now clears activeAction_ in
// a catch block before rethrowing, exactly like the pre-existing success path already did.
TEST(NetworkSessionTest, FailedCreateDoesNotPermanentlyStrandActiveAction) {
    System::IAsyncResult* result = NetworkSession::BeginCreate(
        NetworkSessionType::Local, 1, 8, System::AsyncCallback{}, std::any{}
    );
    std::unique_ptr<System::IAsyncResult> resultOwner(result);

    EXPECT_THROW((void)NetworkSession::EndCreate(result), std::exception);

    // The real proof: activeAction_ must not still be stuck - a fresh Begin*/End* cycle (using a
    // real, non-empty local-gamer list via the same RAII global-swap technique used throughout
    // this file) must succeed, rather than throwing InvalidOperationException("session already
    // being created").
    SignedInGamer gamer = MakeSignedInGamer();
    Gamer::setSignedInGamersProperty(new SignedInGamerCollection(
        SignedInGamerCollection::CreateInternal({&gamer})
    ));
    struct RestoreGlobalGuard {
        ~RestoreGlobalGuard() {
            Gamer::setSignedInGamersProperty(new SignedInGamerCollection(SignedInGamerCollection::CreateInternal({})));
        }
    } restoreGuard;

    System::IAsyncResult* recovery = NetworkSession::BeginCreate(
        NetworkSessionType::Local, 1, 8, System::AsyncCallback{}, std::any{}
    );
    std::unique_ptr<System::IAsyncResult> recoveryOwner(recovery);
    NetworkSession* session = NetworkSession::EndCreate(recovery);
    std::unique_ptr<NetworkSession> sessionOwner(session);
    ASSERT_NE(session, nullptr);
    session->Dispose();
}

// Task 2.3: AddLocalGamer only did localGamers_.Add(adding); allGamers_.Add(adding); with no event
// enqueue at all — unlike AddRemoteGamer just below, which explicitly enqueues a GamerJoin event.
// A handler already subscribed before AddLocalGamer ran never learned about the newly-added local
// gamer (no replay, no queued event).
// Reference NetworkSession.AddLocalGamer checks, in order: null, disposed gamer, disposed session,
// gamer already in the session, Playing without join-in-progress, Ended, no open public slot.
// Reference GameStarted add accessor: a handler added while Playing is told at once; in the lobby
// it is not.
TEST(NetworkSessionTest, GameStartedReplaysForAHandlerAddedWhilePlaying) {
    SignedInGamer gamer = MakeSignedInGamer("PlayerTag");
    std::unique_ptr<NetworkSession> session(NetworkSession::Create(
        NetworkSessionType::Local, std::vector<SignedInGamer*>{&gamer}, 8, 0, NetworkSessionProperties{}));
    int lobbyCalls = 0;
    const auto lobby = session->GameStarted.Add([&](System::Object*, const GameStartedEventArgs&) { ++lobbyCalls; });
    EXPECT_EQ(0, lobbyCalls);
    session->StartGame();
    session->Update();
    ASSERT_EQ(NetworkSessionState::Playing, session->getSessionStateProperty());
    EXPECT_EQ(1, lobbyCalls);
    int lateCalls = 0;
    System::Object* sender = nullptr;
    const auto late = session->GameStarted.Add([&](System::Object* from, const GameStartedEventArgs&) { ++lateCalls; sender = from; });
    EXPECT_EQ(1, lateCalls);
    EXPECT_EQ(session.get(), sender);
    session->GameStarted.Remove(lobby);
    session->GameStarted.Remove(late);
    session->Dispose();
}

TEST(NetworkSessionTest, AddLocalGamerFollowsTheReferenceChecks) {
    SignedInGamer first = MakeSignedInGamer("FirstTag");
    Gamer::setSignedInGamersProperty(new SignedInGamerCollection(SignedInGamerCollection::CreateInternal({&first})));
    struct RestoreGlobalGuard {
        ~RestoreGlobalGuard() {
            Gamer::setSignedInGamersProperty(new SignedInGamerCollection(SignedInGamerCollection::CreateInternal({})));
        }
    } restoreGuard;
    std::unique_ptr<NetworkSession> session(NetworkSession::Create(NetworkSessionType::Local, 4, 2));
    ASSERT_EQ(session->getLocalGamersProperty().getCountProperty(), 1);
    EXPECT_THROW(session->AddLocalGamer(nullptr), System::ArgumentNullException);
    EXPECT_THROW(session->AddLocalGamer(&first), System::ArgumentException);
    SignedInGamer second = MakeSignedInGamer("SecondTag");
    session->StartGame();
    session->Update();
    ASSERT_EQ(session->getSessionStateProperty(), NetworkSessionState::Playing);
    session->setAllowJoinInProgressProperty(false);
    EXPECT_THROW(session->AddLocalGamer(&second), System::InvalidOperationException);
    session->EndGame();
    session->Update();
    session->AddLocalGamer(&second);
    // Two gamers fill the two slots, the second while it still waits for the next Update.
    SignedInGamer third = MakeSignedInGamer("ThirdTag");
    EXPECT_THROW(session->AddLocalGamer(&third), System::InvalidOperationException);
    session->Update();
    EXPECT_EQ(session->getAllGamersProperty().getCountProperty(), 2);
    EXPECT_THROW(session->AddLocalGamer(&third), System::InvalidOperationException);
    session->Dispose();
    EXPECT_THROW(session->AddLocalGamer(&third), System::ObjectDisposedException);
}

TEST(NetworkSessionTest, AddLocalGamerRaisesGamerJoinedForAnAlreadySubscribedHandler) {
    // Task 2.15 fixed a latent double-free here (and in the two other tests using this same
    // pattern): Gamer::setSignedInGamersProperty(value) unconditionally deletes whatever
    // signedInGamers_ previously pointed to - capturing that pointer via getSignedInGamersProperty()
    // and later trying to setSignedInGamersProperty() back to it re-deletes an already-freed
    // object. Installing a brand-new empty collection on teardown avoids ever reusing a pointer
    // the setter has already freed - restoring the observable "no one signed in" default state
    // other tests rely on without resurrecting a dangling pointer.
    SignedInGamer extraGamer = MakeSignedInGamer("ExtraTag");
    Gamer::setSignedInGamersProperty(new SignedInGamerCollection(
        SignedInGamerCollection::CreateInternal({&extraGamer})
    ));
    struct RestoreGlobalGuard {
        ~RestoreGlobalGuard() {
            Gamer::setSignedInGamersProperty(new SignedInGamerCollection(SignedInGamerCollection::CreateInternal({})));
        }
    } restoreGuard;

    // maxLocalGamers=2, but only 1 gamer (extraGamer) is actually signed in globally, so the
    // constructor only fills 1 of the 2 local-gamer slots - leaving room for AddLocalGamer below.
    auto newGamer = MakeSignedInGamer("NewLocalPlayer");
    NetworkSession* session = NetworkSession::Create(NetworkSessionType::Local, 2, 8);
    std::unique_ptr<NetworkSession> sessionOwner(session);
    ASSERT_EQ(session->getLocalGamersProperty().getCountProperty(), 1);

    int joinCount = 0;
    NetworkGamer* joinedGamer = nullptr;
    session->GamerJoined += [&joinCount, &joinedGamer](System::Object*, const GamerJoinedEventArgs& e) {
        ++joinCount;
        joinedGamer = e.getGamerProperty();
    };
    // The += above already replayed once for extraGamer (Task 12.3's SetReplayHook) - reset so
    // this test isolates AddLocalGamer's own new join below.
    joinCount = 0;
    joinedGamer = nullptr;

    session->AddLocalGamer(&newGamer);
    EXPECT_EQ(session->getLocalGamersProperty().getCountProperty(), 1);
    EXPECT_EQ(joinCount, 0);

    session->Update();
    EXPECT_EQ(joinCount, 1);
    ASSERT_EQ(session->getLocalGamersProperty().getCountProperty(), 2);
    ASSERT_NE(joinedGamer, nullptr);
    EXPECT_EQ(joinedGamer, session->getLocalGamersProperty()[1]);

    session->Dispose();
}

// XNA AddLocalGamer validates and then only dispatches a kernel command (IL_00ef..0192); nothing
// is added to a collection. The gamer joins in a later Update: ProcessUpdateResults
// (Update IL_0078..007a) runs ProcessGamerJoined, which inserts it into AllGamers and LocalGamers
// (IL_00f0..0127) before queueing GamerJoined (IL_01a9..01bc), and RaiseUpdateEvents
// (Update IL_008c..008d) raises it, so the handler already finds the gamer in both collections.
// CNA joins a Local or SystemLink gamer at the next Update. A gamer still waiting to join is
// refused as already in the session (TQ-039).
TEST(NetworkSessionTest, AddLocalGamerJoinsAtTheNextUpdate) {
    SignedInGamer first = MakeSignedInGamer("First");
    SignedInGamer second = MakeSignedInGamer("Second");
    std::unique_ptr<NetworkSession> session(NetworkSession::Create(
        NetworkSessionType::Local, std::vector<SignedInGamer*>{&first}, 4, 0, NetworkSessionProperties{}));
    LocalNetworkGamer* host = session->getLocalGamersProperty()[0];
    std::vector<std::string> log;
    session->GamerJoined += [&](System::Object* sender, const GamerJoinedEventArgs& e) {
        EXPECT_EQ(sender, session.get());
        const auto& locals = session->getLocalGamersProperty();
        const auto& all = session->getAllGamersProperty();
        EXPECT_NE(std::find(locals.begin(), locals.end(), e.getGamerProperty()), locals.end());
        EXPECT_NE(std::find(all.begin(), all.end(), e.getGamerProperty()), all.end());
        log.push_back(e.getGamerProperty()->getGamertagProperty());
    };
    ASSERT_EQ(log, (std::vector<std::string>{"First"}));
    log.clear();

    session->AddLocalGamer(&second);
    EXPECT_EQ(session->getLocalGamersProperty().getCountProperty(), 1);
    EXPECT_EQ(session->getAllGamersProperty().getCountProperty(), 1);
    EXPECT_EQ(session->FindGamerById(1), nullptr);
    EXPECT_EQ(session->GetOwnedGamerCountForTesting(), 1u);
    EXPECT_TRUE(log.empty());
    try {
        session->AddLocalGamer(&second);
        ADD_FAILURE() << "a gamer waiting to join was added twice";
    } catch (const System::ArgumentException& e) {
        EXPECT_EQ(e.getParamNameProperty(), "gamer");
    }

    session->Update();
    EXPECT_EQ(log, (std::vector<std::string>{"Second"}));
    const auto& locals = session->getLocalGamersProperty();
    ASSERT_EQ(locals.getCountProperty(), 2);
    EXPECT_EQ(session->getAllGamersProperty().getCountProperty(), 2);
    LocalNetworkGamer* added = locals[1];
    EXPECT_EQ(added->getSignedInGamerProperty(), &second);
    EXPECT_EQ(added->getIdProperty(), 1);
    EXPECT_EQ(session->FindGamerById(1), added);
    EXPECT_EQ(&added->getMachineProperty(), &host->getMachineProperty());
    EXPECT_EQ(session->GetOwnedGamerCountForTesting(), 2u);
    EXPECT_THROW(session->AddLocalGamer(&second), System::ArgumentException);

    session->Update();
    EXPECT_EQ(log.size(), 1u);
    session->Dispose();
}

// A gamer still waiting for its Update when the session is disposed never joins: nothing is
// created for it and no GamerJoined is raised (TQ-039; Dispose itself is CNA's, see
// DisposeClearsHostProperty).
TEST(NetworkSessionTest, DisposeDropsAnAddedGamerThatHasNotJoined) {
    SignedInGamer first = MakeSignedInGamer("First");
    SignedInGamer second = MakeSignedInGamer("Second");
    std::unique_ptr<NetworkSession> session(NetworkSession::Create(
        NetworkSessionType::Local, std::vector<SignedInGamer*>{&first}, 4, 0, NetworkSessionProperties{}));
    int joined = 0;
    session->GamerJoined += [&](System::Object*, const GamerJoinedEventArgs&) { ++joined; };
    joined = 0;
    session->AddLocalGamer(&second);
    session->Dispose();
    EXPECT_EQ(joined, 0);
    EXPECT_EQ(session->GetOwnedGamerCountForTesting(), 0u);
    EXPECT_EQ(session->getLocalGamersProperty().getCountProperty(), 0);
    EXPECT_THROW(session->Update(), System::ObjectDisposedException);
    EXPECT_EQ(joined, 0);
}

// Task 2.4: AddLocalGamer used to derive its new gamer's id from allGamers_.getCountProperty() at
// call time, rather than a separate monotonic counter. Since RemoveGamer shrinks that live count
// (once Task 2.2 fixed it to prune localGamers_ too), a remove-then-add sequence could hand a new
// gamer the same id already owned by a still-present gamer, corrupting FindGamerById.
TEST(NetworkSessionTest, RemoveThenAddLocalGamerChurnNeverProducesAnIdCollision) {
    // See Task 2.15's fix note above (AddLocalGamerRaisesGamerJoinedForAnAlreadySubscribedHandler)
    // for why this installs a fresh empty collection on teardown rather than restoring a captured
    // "previous" pointer - Gamer::setSignedInGamersProperty deletes the old value unconditionally.
    SignedInGamer gamerA = MakeSignedInGamer("A");
    SignedInGamer gamerB = MakeSignedInGamer("B");
    SignedInGamer gamerC = MakeSignedInGamer("C");
    Gamer::setSignedInGamersProperty(new SignedInGamerCollection(
        SignedInGamerCollection::CreateInternal({&gamerA, &gamerB, &gamerC})
    ));
    struct RestoreGlobalGuard {
        ~RestoreGlobalGuard() {
            Gamer::setSignedInGamersProperty(new SignedInGamerCollection(SignedInGamerCollection::CreateInternal({})));
        }
    } restoreGuard;

    auto newSignedIn = MakeSignedInGamer("D");
    NetworkSession* session = NetworkSession::Create(NetworkSessionType::Local, 3, 8);
    std::unique_ptr<NetworkSession> sessionOwner(session);
    ASSERT_EQ(session->getLocalGamersProperty().getCountProperty(), 3);
    LocalNetworkGamer* local0 = session->getLocalGamersProperty()[0];
    LocalNetworkGamer* local1 = session->getLocalGamersProperty()[1];
    LocalNetworkGamer* local2 = session->getLocalGamersProperty()[2];
    ASSERT_EQ(local0->getIdProperty(), 0);
    ASSERT_EQ(local1->getIdProperty(), 1);
    ASSERT_EQ(local2->getIdProperty(), 2);

    // Remove the middle gamer (id 1): allGamers_/localGamers_ counts both drop to 2.
    session->RemoveGamer(local1, NetworkSessionEndReason::Disconnected);
    ASSERT_EQ(session->getAllGamersProperty().getCountProperty(), 2);

    // Without Task 2.4's fix, this would derive the new id from allGamers_.getCountProperty()
    // (now 2) - colliding with local2, which already owns id 2.
    session->AddLocalGamer(&newSignedIn);
    session->Update(); // the added gamer joins here (TQ-039)
    ASSERT_EQ(session->getLocalGamersProperty().getCountProperty(), 3); // local0, local2, newGamer
    NetworkGamer* newGamer = session->getLocalGamersProperty()[2];
    EXPECT_EQ(newGamer->getIdProperty(), 3);
    EXPECT_NE(newGamer->getIdProperty(), local2->getIdProperty());

    EXPECT_EQ(session->FindGamerById(0), local0);
    EXPECT_EQ(session->FindGamerById(1), nullptr); // removed, and never reissued
    EXPECT_EQ(session->FindGamerById(2), local2);
    EXPECT_EQ(session->FindGamerById(3), newGamer);

    session->Dispose();
}

TEST(NetworkSessionTest, UpdateAfterDisposeThrows) {
    auto gamer = MakeSignedInGamer();
    NetworkSession* session = NetworkSession::Create(
        NetworkSessionType::Local, std::vector<SignedInGamer*>{&gamer}, 8, 0, NetworkSessionProperties{}
    );
    std::unique_ptr<NetworkSession> sessionOwner(session);

    session->Dispose();
    EXPECT_THROW(session->Update(), System::ObjectDisposedException);
}

TEST(NetworkSessionTest, ResetReadyAfterDisposeThrows) {
    auto gamer = MakeSignedInGamer();
    NetworkSession* session = NetworkSession::Create(
        NetworkSessionType::Local, std::vector<SignedInGamer*>{&gamer}, 8, 0, NetworkSessionProperties{}
    );
    std::unique_ptr<NetworkSession> sessionOwner(session);

    session->Dispose();
    EXPECT_THROW(session->ResetReady(), System::ObjectDisposedException);
}

TEST(NetworkSessionTest, StartAndEndGameAfterDisposeThrow) {
    auto gamer = MakeSignedInGamer();
    NetworkSession* session = NetworkSession::Create(
        NetworkSessionType::Local, std::vector<SignedInGamer*>{&gamer}, 8, 0, NetworkSessionProperties{}
    );
    std::unique_ptr<NetworkSession> sessionOwner(session);

    session->Dispose();
    EXPECT_THROW(session->StartGame(), System::ObjectDisposedException);
    EXPECT_THROW(session->EndGame(), System::ObjectDisposedException);
}

// Task 12.3 (DEFERRED.md item #21 in the sibling cna-samples repo): real XNA's GamerJoined
// replays itself immediately for every gamer already in the session the instant a handler
// subscribes via += - not deferred to the next Update() call, since no caller could possibly
// have subscribed before Create()/Join() returned the session pointer in the first place.
// sharp-runtime's EventHandler<T>::SetReplayHook() (set in NetworkSession's own constructor)
// reproduces this; Update() afterward has nothing left to add for these construction-time gamers.
TEST(NetworkSessionTest, GamerJoinedReplaysImmediatelyOnSubscriptionForConstructionTimeGamers) {
    auto gamer = MakeSignedInGamer();
    NetworkSession* session = NetworkSession::Create(
        NetworkSessionType::Local, std::vector<SignedInGamer*>{&gamer}, 8, 0, NetworkSessionProperties{}
    );
    std::unique_ptr<NetworkSession> sessionOwner(session);

    int joinCount = 0;
    // Task 2.1: sender must be the raising NetworkSession itself (real XNA passes the raising
    // instance for every event; NetworkSession used to hardcode nullptr since it didn't inherit
    // System::Object at all, leaving no `this`-as-Object* to pass).
    System::Object* observedSender = nullptr;
    session->GamerJoined += [&joinCount, &observedSender](System::Object* sender, const GamerJoinedEventArgs&) {
        ++joinCount;
        observedSender = sender;
    };
    EXPECT_EQ(joinCount, 1); // fired synchronously by the += itself, before any Update() call
    EXPECT_EQ(observedSender, session);

    session->Update();
    EXPECT_EQ(joinCount, 1); // nothing left queued for Update() to drain

    session->Dispose();
}

// A handler subscribing well after construction (and after other Update() calls already ran)
// must still be caught up on every gamer currently in the session - not just gamers who join
// after it subscribes.
TEST(NetworkSessionTest, GamerJoinedReplaysForALateSubscriber) {
    auto gamer = MakeSignedInGamer();
    NetworkSession* session = NetworkSession::Create(
        NetworkSessionType::Local, std::vector<SignedInGamer*>{&gamer}, 8, 0, NetworkSessionProperties{}
    );
    std::unique_ptr<NetworkSession> sessionOwner(session);
    session->Update();
    session->Update();

    int joinCount = 0;
    session->GamerJoined += [&joinCount, session](System::Object*, const GamerJoinedEventArgs& e) {
        ++joinCount;
        EXPECT_EQ(e.getGamerProperty(), session->getAllGamersProperty()[0]);
    };
    EXPECT_EQ(joinCount, 1);

    session->Dispose();
}

// --- Static Create/BeginCreate/EndCreate family ---
//
// NOTE on what is NOT tested here: NetworkSession::Create(sessionType, maxLocalGamers,
// maxGamers) — the overload with no explicit gamer list — falls back to the global
// Gamer::SignedInGamers, which is empty in this test binary (GamerServicesDispatcher::
// Initialize() is deliberately never called from tests; see its own "cannot be unit tested"
// note). That makes the constructor's Host = LocalGamers[0] throw std::out_of_range from
// inside EndCreate. The constructor-failure cleanup is tested separately; this note only explains
// why the ordinary successful simple overload needs a temporary published gamer as used above.
// The identical empty-global constraint applies to Join(AvailableNetworkSession*) and
// JoinInvited(int) below, because both also route through a std::nullopt LocalGamers list.

TEST(NetworkSessionTest, BeginCreateSimpleOverloadValidatesMaxLocalGamers) {
    EXPECT_THROW(
        NetworkSession::BeginCreate(NetworkSessionType::Local, 0, 4, System::AsyncCallback{}, std::any{}),
        System::ArgumentOutOfRangeException
    );
    EXPECT_THROW(
        NetworkSession::BeginCreate(NetworkSessionType::Local, 5, 4, System::AsyncCallback{}, std::any{}),
        System::ArgumentOutOfRangeException
    );
}

// XNA BeginCreate IL_0013..002d bounds maxGamers by MinSupportedGamers(sessionType) and 31;
// MinSupportedGamers IL_0002..0009 is 1 for Local and 2 for every other type. (This test used to
// refuse a one-gamer Local session, which XNA creates: LocalSessionMayHoldASingleGamer.)
TEST(NetworkSessionTest, BeginCreateOverloadsValidateMaxGamers) {
    auto gamer = MakeSignedInGamer();
    const auto expectRefused = [&gamer](NetworkSessionType type, int maxGamers) {
        SCOPED_TRACE(::testing::Message() << "type=" << static_cast<int>(type) << " maxGamers=" << maxGamers);
        const std::function<void()> overloads[] = {
            [&] { (void)NetworkSession::BeginCreate(type, 1, maxGamers, System::AsyncCallback{}, std::any{}); },
            [&] { (void)NetworkSession::BeginCreate(type, 1, maxGamers, 0, NetworkSessionProperties{},
                      System::AsyncCallback{}, std::any{}); },
            [&] { (void)NetworkSession::BeginCreate(type, std::vector<SignedInGamer*>{&gamer}, maxGamers, 0,
                      NetworkSessionProperties{}, System::AsyncCallback{}, std::any{}); },
        };
        for (const auto& begin : overloads) {
            try {
                begin();
                ADD_FAILURE() << "maxGamers was accepted";
            } catch (const System::ArgumentOutOfRangeException& e) {
                EXPECT_EQ(e.getParamNameProperty(), "maxGamers");
            }
        }
    };
    const int before = NetworkSession::GetActiveActionInstanceCountForTesting();
    expectRefused(NetworkSessionType::Local, 0);
    expectRefused(NetworkSessionType::Local, 32);
    for (const auto type : {NetworkSessionType::SystemLink, NetworkSessionType::PlayerMatch,
                            NetworkSessionType::Ranked, NetworkSessionType::LocalWithLeaderboards}) {
        expectRefused(type, 1);
        expectRefused(type, 32);
    }
    // A refusal creates no pending operation that would block the next Begin*.
    EXPECT_EQ(NetworkSession::GetActiveActionInstanceCountForTesting(), before);
}

TEST(NetworkSessionTest, LocalSessionMayHoldASingleGamer) {
    SignedInGamer solo = MakeSignedInGamer("Solo");
    SignedInGamer second = MakeSignedInGamer("Second");
    std::unique_ptr<NetworkSession> session(NetworkSession::Create(
        NetworkSessionType::Local, std::vector<SignedInGamer*>{&solo}, 1, 0, NetworkSessionProperties{}));
    ASSERT_NE(session, nullptr);
    EXPECT_EQ(session->getMaxGamersProperty(), 1);
    EXPECT_EQ(session->getPrivateGamerSlotsProperty(), 0);
    ASSERT_EQ(session->getAllGamersProperty().getCountProperty(), 1);
    // The host fills the only public slot: XNA AddLocalGamer IL_00c8..00ee.
    EXPECT_THROW(session->AddLocalGamer(&second), System::InvalidOperationException);
    EXPECT_EQ(session->getAllGamersProperty().getCountProperty(), 1);
    // BeginCreate IL_002e..0043: privateGamerSlots must stay below maxGamers.
    session->Dispose();
    session.reset();
    EXPECT_THROW((void)NetworkSession::BeginCreate(NetworkSessionType::Local, std::vector<SignedInGamer*>{&solo}, 1, 1,
                     NetworkSessionProperties{}, System::AsyncCallback{}, std::any{}),
                 System::ArgumentOutOfRangeException);

    // The two signed-in-gamer overloads accept one as well.
    Gamer::setSignedInGamersProperty(new SignedInGamerCollection(SignedInGamerCollection::CreateInternal({&solo})));
    struct RestoreGlobalGuard {
        ~RestoreGlobalGuard() {
            Gamer::setSignedInGamersProperty(new SignedInGamerCollection(SignedInGamerCollection::CreateInternal({})));
        }
    } restoreGuard;
    std::unique_ptr<NetworkSession> simple(NetworkSession::Create(NetworkSessionType::Local, 1, 1));
    EXPECT_EQ(simple->getMaxGamersProperty(), 1);
    EXPECT_EQ(simple->getLocalGamersProperty().getCountProperty(), 1);
    simple->Dispose();
    simple.reset();
    std::unique_ptr<NetworkSession> withProperties(
        NetworkSession::Create(NetworkSessionType::Local, 1, 1, 0, NetworkSessionProperties{}));
    EXPECT_EQ(withProperties->getMaxGamersProperty(), 1);
    withProperties->Dispose();
}

TEST(NetworkSessionTest, BeginCreatePropertiesOverloadValidatesPrivateGamerSlots) {
    EXPECT_THROW(
        NetworkSession::BeginCreate(
            NetworkSessionType::Local, 1, 4, -1, NetworkSessionProperties{}, System::AsyncCallback{}, std::any{}
        ),
        System::ArgumentOutOfRangeException
    );
    EXPECT_THROW(
        NetworkSession::BeginCreate(
            NetworkSessionType::Local, 1, 4, 5, NetworkSessionProperties{}, System::AsyncCallback{}, std::any{}
        ),
        System::ArgumentOutOfRangeException
    );
}

TEST(NetworkSessionTest, BeginCreateLocalGamersOverloadValidatesPrivateGamerSlots) {
    auto gamer = MakeSignedInGamer();
    EXPECT_THROW(
        NetworkSession::BeginCreate(
            NetworkSessionType::Local, std::vector<SignedInGamer*>{&gamer}, 4, 5,
            NetworkSessionProperties{}, System::AsyncCallback{}, std::any{}
        ),
        System::ArgumentOutOfRangeException
    );
}

TEST(NetworkSessionTest, BeginCreateWhileActionPendingThrows) {
    auto gamer = MakeSignedInGamer();
    System::IAsyncResult* result = NetworkSession::BeginCreate(
        NetworkSessionType::Local, std::vector<SignedInGamer*>{&gamer}, 8, 0,
        NetworkSessionProperties{}, System::AsyncCallback{}, std::any{}
    );
    std::unique_ptr<System::IAsyncResult> resultOwner(result);

    EXPECT_THROW(
        NetworkSession::BeginCreate(NetworkSessionType::Local, 1, 4, System::AsyncCallback{}, std::any{}),
        System::InvalidOperationException
    );

    NetworkSession* session = NetworkSession::EndCreate(result);
    std::unique_ptr<NetworkSession> sessionOwner(session);
    session->Dispose();
}

TEST(NetworkSessionTest, EndCreateWithMismatchedResultThrows) {
    auto gamer = MakeSignedInGamer();
    System::IAsyncResult* result = NetworkSession::BeginCreate(
        NetworkSessionType::Local, std::vector<SignedInGamer*>{&gamer}, 8, 0,
        NetworkSessionProperties{}, System::AsyncCallback{}, std::any{}
    );
    std::unique_ptr<System::IAsyncResult> resultOwner(result);

    auto* bogus = reinterpret_cast<System::IAsyncResult*>(0x1);
    EXPECT_THROW((void)NetworkSession::EndCreate(bogus), System::ArgumentException);

    NetworkSession* session = NetworkSession::EndCreate(result);
    std::unique_ptr<NetworkSession> sessionOwner(session);
    session->Dispose();
}

// --- Static Find/BeginFind/EndFind family ---

TEST(NetworkSessionTest, FindReturnsEmptyCollection) {
    auto gamer = MakeSignedInGamer();
    AvailableNetworkSessionCollection sessions = NetworkSession::Find(
        NetworkSessionType::SystemLink, 1, NetworkSessionProperties{}
    );
    EXPECT_EQ(sessions.getCountProperty(), 0);

    AvailableNetworkSessionCollection sessions2 = NetworkSession::Find(
        NetworkSessionType::SystemLink, std::vector<SignedInGamer*>{&gamer}, NetworkSessionProperties{}
    );
    EXPECT_EQ(sessions2.getCountProperty(), 0);
}

TEST(NetworkSessionTest, BeginFindRejectsLocalSessionType) {
    auto gamer = MakeSignedInGamer();
    EXPECT_THROW(
        NetworkSession::BeginFind(NetworkSessionType::Local, 1, NetworkSessionProperties{}, System::AsyncCallback{}, std::any{}),
        System::ArgumentException
    );
    EXPECT_THROW(
        NetworkSession::BeginFind(
            NetworkSessionType::Local, std::vector<SignedInGamer*>{&gamer}, NetworkSessionProperties{},
            System::AsyncCallback{}, std::any{}
        ),
        System::ArgumentException
    );
}

TEST(NetworkSessionTest, BeginFindValidatesMaxLocalGamers) {
    EXPECT_THROW(
        NetworkSession::BeginFind(NetworkSessionType::SystemLink, 0, NetworkSessionProperties{}, System::AsyncCallback{}, std::any{}),
        System::ArgumentOutOfRangeException
    );
}

TEST(NetworkSessionTest, EndFindWithMismatchedResultThrows) {
    System::IAsyncResult* result = NetworkSession::BeginFind(
        NetworkSessionType::SystemLink, 1, NetworkSessionProperties{}, System::AsyncCallback{}, std::any{}
    );
    std::unique_ptr<System::IAsyncResult> resultOwner(result);

    auto* bogus = reinterpret_cast<System::IAsyncResult*>(0x1);
    EXPECT_THROW((void)NetworkSession::EndFind(bogus), System::ArgumentException);

    NetworkSession::EndFind(result);
}

// --- Static Join/BeginJoin/EndJoin family ---
//
// NOTE: Join(...)/EndJoin(...) completing successfully needs the same temporary one-gamer global
// SignedInGamers swap Task 2.3's AddLocalGamer test uses (BeginJoin's NetworkSessionAction always
// carries a std::nullopt LocalGamers list - FNA passes null unconditionally, marked FIXME
// upstream - so the constructor's fallback-to-global-list path is the only one EndJoin can ever
// reach; an empty list there throws and permanently corrupts activeAction_ for the rest of the
// process). See JoinActivatesRealNetworkingForTheCorrectSessionType below for the real,
// full-round-trip Join() test this makes possible (Task 2.15).

TEST(NetworkSessionTest, BeginJoinRejectsNullAvailableSession) {
    EXPECT_THROW(
        NetworkSession::BeginJoin(nullptr, System::AsyncCallback{}, std::any{}),
        System::ArgumentNullException
    );
}

// SAMPLE-091 regression: Join previously returned before ClientHello/ServerWelcome completed and
// left Host pointing at the client's own local gamer. ClientServerSample faithfully sends its
// first input packet to session.Host before calling session.Update, so the 16-byte client-input
// packet was looped back locally and then misread as the host's 17-byte state record. Exercise the
// real public Join route and a real ENet handshake; Host must already identify the remote peer at
// return, and the first packet sent through that identity must reach the actual server.
TEST(NetworkSessionTest, JoinActivatesRealNetworkingForTheCorrectSessionType) {
    CNA::Internal::Net::ENetHostHandle fakeHostBeingJoined = CNA::Internal::Net::ENetHostHandle::CreateHost(0, 4, 2);
    uint16_t fakeHostPort = fakeHostBeingJoined.getBoundPortProperty();
    ASSERT_GT(fakeHostPort, 0);

    std::promise<CNA::Internal::Net::AppDataMessage> receivedAppDataPromise;
    std::future<CNA::Internal::Net::AppDataMessage> receivedAppData = receivedAppDataPromise.get_future();
    std::atomic<bool> receivedHello{false};
    std::atomic<bool> appDataRecorded{false};
    std::jthread serverThread([&](std::stop_token stopToken) {
        ENetPeer* peerFromHostSide = nullptr;
        while (!stopToken.stop_requested()) {
            ENetEvent evt{};
            if (fakeHostBeingJoined.Service(0, evt) <= 0) {
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
                continue;
            }
            if (evt.type == ENET_EVENT_TYPE_CONNECT) {
                peerFromHostSide = evt.peer;
                continue;
            }
            if (evt.type != ENET_EVENT_TYPE_RECEIVE) {
                continue;
            }

            std::vector<SharpRuntime::bytecs> data(
                evt.packet->data, evt.packet->data + evt.packet->dataLength
            );
            const auto tag = CNA::Internal::Net::NetPacketCodec::PeekTag(data);
            if (tag == CNA::Internal::Net::MessageTag::ClientHello) {
                receivedHello.store(true);
                CNA::Internal::Net::ServerWelcomeMessage welcome;
                welcome.AssignedWireIds = {5};
                welcome.ExistingRoster = {{0, "FakeHost", true}};
                const auto bytes = CNA::Internal::Net::NetPacketCodec::Encode(welcome);
                fakeHostBeingJoined.Send(
                    peerFromHostSide, 0, bytes.data(), bytes.size(), ENET_PACKET_FLAG_RELIABLE
                );
                fakeHostBeingJoined.Flush();
            } else if (tag == CNA::Internal::Net::MessageTag::AppData &&
                       !appDataRecorded.exchange(true)) {
                receivedAppDataPromise.set_value(
                    CNA::Internal::Net::NetPacketCodec::DecodeAppData(data)
                );
            }
            enet_packet_destroy(evt.packet);
        }
    });

    // See Task 2.15's fix note on AddLocalGamerRaisesGamerJoinedForAnAlreadySubscribedHandler
    // (above) for why this installs a fresh empty collection on teardown rather than restoring a
    // captured "previous" pointer - Gamer::setSignedInGamersProperty deletes the old value
    // unconditionally, so reusing a captured "previous" pointer here is what actually surfaced
    // this as a reproducible double-free in the first place.
    SignedInGamer joiningGamer = MakeSignedInGamer("Joiner");
    Gamer::setSignedInGamersProperty(new SignedInGamerCollection(
        SignedInGamerCollection::CreateInternal({&joiningGamer})
    ));
    struct RestoreGlobalGuard {
        ~RestoreGlobalGuard() {
            Gamer::setSignedInGamersProperty(new SignedInGamerCollection(SignedInGamerCollection::CreateInternal({})));
        }
    } restoreGuard;

    AvailableNetworkSession availableSession = AvailableNetworkSession::CreateInternal(
        1, "FakeHost", 0, 8, NetworkSessionProperties{}, QualityOfService::CreateInternal(),
        "127.0.0.1", fakeHostPort, NetworkSessionType::SystemLink
    );

    NetworkSession* joined = NetworkSession::Join(&availableSession);
    ASSERT_NE(joined, nullptr);
    EXPECT_EQ(joined->getSessionTypeProperty(), NetworkSessionType::SystemLink);
    EXPECT_GT(CNA::Internal::Net::ENetBackend::GetBoundPort(joined), 0);

    ASSERT_TRUE(receivedHello.load());
    NetworkGamer* host = joined->getHostProperty();
    ASSERT_NE(host, nullptr);
    EXPECT_NE(host, joined->getLocalGamersProperty()[0]);
    EXPECT_FALSE(host->getIsLocalProperty());
    EXPECT_TRUE(host->getIsHostProperty());
    EXPECT_EQ(host->getGamertagProperty(), "FakeHost");
    EXPECT_FALSE(joined->getIsHostProperty());
    EXPECT_THROW(joined->getSessionPropertiesProperty()[0] = 7, System::InvalidOperationException);
    EXPECT_FALSE(joined->getSessionPropertiesProperty().getItem(0).has_value());

    const std::vector<SharpRuntime::bytecs> payload{1, 2, 3, 4};
    joined->getLocalGamersProperty()[0]->SendData(payload, SendDataOptions::Reliable, host);
    for (int i = 0; i < 200 &&
                    receivedAppData.wait_for(std::chrono::milliseconds(0)) != std::future_status::ready;
         ++i) {
        joined->Update();
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    ASSERT_EQ(receivedAppData.wait_for(std::chrono::milliseconds(0)), std::future_status::ready);
    const auto packet = receivedAppData.get();
    EXPECT_EQ(packet.SenderWireId, 5);
    EXPECT_EQ(packet.TargetWireId, 0);
    EXPECT_EQ(packet.Options, SendDataOptions::Reliable);
    EXPECT_EQ(packet.Payload, payload);

    joined->Dispose();
    delete joined;
    serverThread.request_stop();
}

// A C binding can construct an AvailableNetworkSession description directly. Host metadata on a
// synthetic PlayerMatch entry is descriptive only: a PlayerMatch session is joined through the CNA
// service and its relay, never at a listed address, so Join must not interpret that address as a
// SystemLink endpoint and wait for a ServerWelcome that can never arrive. The SAMPLE-091 handshake wait originally missed this session-type gate.
// A listing that no authenticated search produced has no live search parent; the reference
// BeginJoin reports that as ObjectDisposedException before any service or transport work.
TEST(NetworkSessionTest, SyntheticPlayerMatchListingCannotBypassServiceMembership) {
    auto available=AvailableNetworkSession::CreateInternal(1,"SyntheticHost",0,8,{},
        QualityOfService::CreateInternal(),"127.0.0.1",27015,NetworkSessionType::PlayerMatch);
    EXPECT_THROW((void)NetworkSession::Join(&available), System::ObjectDisposedException);
    EXPECT_EQ(0,NetworkSession::GetActiveActionInstanceCountForTesting());
}

// --- Static JoinInvited/BeginJoinInvited/EndJoinInvited family ---

TEST(NetworkSessionTest, BeginJoinInvitedValidatesMaxLocalGamers) {
    EXPECT_THROW(
        NetworkSession::BeginJoinInvited(0, System::AsyncCallback{}, std::any{}),
        System::ArgumentOutOfRangeException
    );
}

// Without a configured CNA service no Guide invitation can have been accepted, so both
// JoinInvited overloads refuse before doing anything. This overload used to succeed and return a
// PlayerMatch session built out of nothing -- no invitation, no host, no transport -- which is not
// what SAMPLE-096 observed. The accepted-invitation path (Guide acceptance -> InviteAccepted ->
// JoinInvited) is covered by OnlineInvitationTest and the real two-process service E2E; these two
// cases pin the unconfigured refusal and that EndJoinInvited refuses another family's result.
TEST(NetworkSessionTest, JoinInvitedWithExplicitLocalGamersRefuses) {
    auto gamer = MakeSignedInGamer();
    EXPECT_THROW(
        NetworkSession::JoinInvited(std::vector<SignedInGamer*>{&gamer}),
        GamerServicesNotAvailableException
    );
}

TEST(NetworkSessionTest, EndJoinInvitedRefusesAnyResultBecauseNoBeginCanProduceOne) {
    // A BeginCreate result compares equal to activeAction_, so this was once a way around the
    // refusal: EndJoinInvited hardcoded a PlayerMatch session regardless of the producing Begin*.
    // The operation-family guard now refuses any result that BeginJoinInvited did not produce.
    auto gamer = MakeSignedInGamer();
    System::IAsyncResult* createResult = NetworkSession::BeginCreate(
        NetworkSessionType::Local, std::vector<SignedInGamer*>{&gamer}, 8, 0,
        NetworkSessionProperties{}, System::AsyncCallback{}, std::any{}
    );
    std::unique_ptr<System::IAsyncResult> createResultOwner(createResult);
    EXPECT_THROW((void)NetworkSession::EndJoinInvited(createResult), System::ArgumentException);

    // The create action is still pending and still completable, so the refusal stranded nothing.
    NetworkSession* session = NetworkSession::EndCreate(createResult);
    ASSERT_NE(session, nullptr);
    EXPECT_EQ(session->getSessionTypeProperty(), NetworkSessionType::Local);
    session->Dispose();
    delete session;
}

TEST(NetworkSessionTest, EndJoinInvitedWithMismatchedResultThrows) {
    // A pointer no Begin* produced is refused without being dereferenced. The accepted-invitation
    // half lives in OnlineInvitationTest; EndJoinInvitedRefusesAnyResultBecauseNoBeginCanProduceOne
    // proves a real pending action of another family survives the refusal.
    auto* bogus = reinterpret_cast<System::IAsyncResult*>(0x1);
    EXPECT_THROW((void)NetworkSession::EndJoinInvited(bogus), System::ArgumentException);
}

// --- LocalNetworkGamer ---
//
// Task 5.12: LocalNetworkGamer's own test cases moved to a dedicated LocalNetworkGamerTests.cpp,
// matching CHECKLIST.md's per-class test-file convention (pure test-file reorganization, no
// behavior change).

// --- Phase 5: real ENet-backed networking (SystemLink only) ---
//
// SystemLink is the only NetworkSessionType that starts a real ENet host (see
// CNA::Internal::Net::ENetBackend::RealNetworkingEnabled). No existing test above constructs a
// SystemLink session — only Find/BeginFind/EndFind use that type, and those never construct a
// NetworkSession — so this is the first real-hosting exercise in the suite.

TEST(NetworkSessionTest, SystemLinkSessionHostsRealNetworkingAndUpdatesCleanly) {
    auto gamer = MakeSignedInGamer();
    NetworkSession* session = NetworkSession::Create(
        NetworkSessionType::SystemLink, std::vector<SignedInGamer*>{&gamer}, 8, 0, NetworkSessionProperties{}
    );
    std::unique_ptr<NetworkSession> sessionOwner(session);

    // Update() drains the real ENet transport (no peers connected yet) as well as the local
    // event queue; neither should throw.
    EXPECT_NO_THROW(session->Update());
    EXPECT_NO_THROW(session->Update());

    session->Dispose();
}

TEST(NetworkSessionTest, LocalSessionTypeDoesNotStartRealNetworking) {
    auto gamer = MakeSignedInGamer();
    NetworkSession* session = NetworkSession::Create(
        NetworkSessionType::Local, std::vector<SignedInGamer*>{&gamer}, 8, 0, NetworkSessionProperties{}
    );
    std::unique_ptr<NetworkSession> sessionOwner(session);

    EXPECT_EQ(CNA::Internal::Net::ENetBackend::GetBoundPort(session), 0);

    session->Dispose();
}

TEST(NetworkSessionTest, SystemLinkSessionGetsARealBoundPort) {
    auto gamer = MakeSignedInGamer();
    NetworkSession* session = NetworkSession::Create(
        NetworkSessionType::SystemLink, std::vector<SignedInGamer*>{&gamer}, 8, 0, NetworkSessionProperties{}
    );
    std::unique_ptr<NetworkSession> sessionOwner(session);

    EXPECT_GT(CNA::Internal::Net::ENetBackend::GetBoundPort(session), 0);

    session->Dispose();
    // TeardownSession() unregisters the transport, so the port is no longer reported afterward.
    EXPECT_EQ(CNA::Internal::Net::ENetBackend::GetBoundPort(session), 0);
}

TEST(NetworkSessionTest, AddRemoteGamerJoinsRostersAndRaisesGamerJoined) {
    auto gamer = MakeSignedInGamer();
    NetworkSession* session = NetworkSession::Create(
        NetworkSessionType::Local, std::vector<SignedInGamer*>{&gamer}, 8, 0, NetworkSessionProperties{}
    );
    std::unique_ptr<NetworkSession> sessionOwner(session);

    // Subscribing here replays once for the local gamer that already joined during
    // construction (Task 12.3) - tracked separately from the remote join below, which is a
    // real, queued, Update()-driven event (AddRemoteGamer is unaffected by the replay hook,
    // which only fires for gamers already present at *subscription* time).
    std::vector<std::string> joinedGamertags;
    session->GamerJoined += [&joinedGamertags](System::Object*, const GamerJoinedEventArgs& e) {
        joinedGamertags.push_back(e.getGamerProperty()->getGamertagProperty());
    };
    ASSERT_EQ(joinedGamertags.size(), 1u);
    EXPECT_EQ(joinedGamertags[0], gamer.getGamertagProperty());
    EXPECT_EQ(session->getLocalGamersProperty()[0]->getDisplayNameProperty(), gamer.getDisplayNameProperty());

    NetworkGamer remote = NetworkGamer::CreateInternal(session, "RemotePlayer");
    session->AddRemoteGamer(&remote);
    EXPECT_EQ(session->getRemoteGamersProperty().getCountProperty(), 1);
    EXPECT_EQ(session->getAllGamersProperty().getCountProperty(), 2);

    session->Update();
    ASSERT_EQ(joinedGamertags.size(), 2u);
    EXPECT_EQ(joinedGamertags[1], "RemotePlayer");

    session->Dispose();
}

// Task 2.5: AddRemoteGamer used to add any remote gamer unconditionally, regardless of maxGamers_,
// silently violating the documented "maximum players allowed" contract.
TEST(NetworkSessionTest, AddRemoteGamerThrowsWhenSessionIsAlreadyAtMaxGamers) {
    auto gamer = MakeSignedInGamer();
    NetworkSession* session = NetworkSession::Create(
        NetworkSessionType::Local, std::vector<SignedInGamer*>{&gamer}, 8, 0, NetworkSessionProperties{}
    );
    std::unique_ptr<NetworkSession> sessionOwner(session);
    ASSERT_EQ(session->getAllGamersProperty().getCountProperty(), 1);
    // A Local session may hold one gamer (XNA MinSupportedGamers), so the host fills the only slot.
    session->setMaxGamersProperty(1);

    NetworkGamer remote = NetworkGamer::CreateInternal(session, "RemotePlayer");
    EXPECT_THROW(session->AddRemoteGamer(&remote), System::InvalidOperationException);
    EXPECT_EQ(session->getRemoteGamersProperty().getCountProperty(), 0);
    EXPECT_EQ(session->getAllGamersProperty().getCountProperty(), 1);

    session->Dispose();
}

TEST(NetworkSessionTest, RemoveGamerOnRemoteGamerRaisesGamerLeftAndMigratesToPrevious) {
    auto gamer = MakeSignedInGamer();
    NetworkSession* session = NetworkSession::Create(
        NetworkSessionType::Local, std::vector<SignedInGamer*>{&gamer}, 8, 0, NetworkSessionProperties{}
    );
    std::unique_ptr<NetworkSession> sessionOwner(session);
    session->Update();

    NetworkGamer remote = NetworkGamer::CreateInternal(session, "RemotePlayer");
    session->AddRemoteGamer(&remote);
    session->Update();

    int leftCount = 0;
    // Task 2.1: sender must be the raising NetworkSession itself, not nullptr.
    System::Object* observedSender = nullptr;
    NetworkGamer* observedGamer = nullptr;
    session->GamerLeft += [&leftCount, &observedSender, &observedGamer](System::Object* sender, const GamerLeftEventArgs& e) {
        ++leftCount;
        observedSender = sender;
        observedGamer = e.getGamerProperty();
    };

    session->RemoveGamer(&remote, NetworkSessionEndReason::Disconnected);
    EXPECT_TRUE(remote.getHasLeftSessionProperty());
    EXPECT_EQ(session->getRemoteGamersProperty().getCountProperty(), 0);
    EXPECT_EQ(session->getAllGamersProperty().getCountProperty(), 1);
    EXPECT_EQ(session->getPreviousGamersProperty().getCountProperty(), 1);
    EXPECT_EQ(session->getPreviousGamersProperty()[0], &remote);

    session->Update();
    EXPECT_EQ(leftCount, 1);
    EXPECT_EQ(observedSender, session);
    EXPECT_EQ(observedGamer, &remote);

    session->Dispose();
}

// XNA ProcessGamerLeft queues GamerLeft for a local gamer as for a remote one (IL_01cb..01de,
// <ProcessGamerLeft>b__17), after it has left AllGamers and LocalGamers (IL_0030..005d) and been
// marked as having left (IL_01a6..01b4). The last local gamer's leave also ends this machine's
// session: XNA leaves that to its kernel, CNA raises SessionEnded with the given reason after the
// GamerLeft (TQ-043, retained boundary).
TEST(NetworkSessionTest, RemovingTheLastLocalGamerRaisesGamerLeftThenSessionEnded) {
    auto gamer = MakeSignedInGamer();
    NetworkSession* session = NetworkSession::Create(
        NetworkSessionType::Local, std::vector<SignedInGamer*>{&gamer}, 8, 0, NetworkSessionProperties{}
    );
    std::unique_ptr<NetworkSession> sessionOwner(session);
    session->Update();

    int endedCount = 0;
    NetworkSessionEndReason observedReason = NetworkSessionEndReason::ClientSignedOut;
    std::vector<std::string> log;
    session->GamerLeft += [&](System::Object* sender, const GamerLeftEventArgs& e) {
        EXPECT_EQ(sender, session);
        EXPECT_TRUE(e.getGamerProperty()->getHasLeftSessionProperty());
        EXPECT_EQ(session->getLocalGamersProperty().getCountProperty(), 0);
        log.push_back("left:" + e.getGamerProperty()->getGamertagProperty());
    };
    session->SessionEnded += [&](System::Object*, const NetworkSessionEndedEventArgs& e) {
        ++endedCount;
        observedReason = e.getEndReasonProperty();
        log.push_back("ended");
    };

    LocalNetworkGamer* localGamer = session->getLocalGamersProperty()[0];
    session->RemoveGamer(localGamer, NetworkSessionEndReason::HostEndedSession);
    EXPECT_TRUE(log.empty());
    session->Update();

    EXPECT_EQ(log, (std::vector<std::string>{"left:tag1", "ended"}));
    EXPECT_EQ(endedCount, 1);
    EXPECT_EQ(observedReason, NetworkSessionEndReason::HostEndedSession);
    EXPECT_EQ(session->getSessionStateProperty(), NetworkSessionState::Ended);
    // Task 2.2: localGamers_ used to never be pruned in RemoveGamer (unlike remoteGamers_/
    // allGamers_, which already were) - a removed local gamer kept appearing in
    // getLocalGamersProperty() forever, breaking the AllGamers == LocalGamers UNION RemoteGamers
    // invariant.
    EXPECT_EQ(session->getLocalGamersProperty().getCountProperty(), 0);
    EXPECT_EQ(session->getAllGamersProperty().getCountProperty(), 0);
    EXPECT_EQ(session->getPreviousGamersProperty().getCountProperty(), 1);
    EXPECT_EQ(session->getPreviousGamersProperty()[0], localGamer);

    session->Dispose();
}

// Refresh ProcessGamerLeft IL_006d..007e clears Host; ProcessHostChanged IL_0018..0071
// then installs the result supplied by the kernel. This test supplies that result explicitly:
// it does not claim the managed IL specifies automatic successor selection (TQ-054 S3).
TEST(NetworkSessionTest, TQ054ExplicitLocalSuccessorIsVisibleToTheEarlierLeave) {
    auto first = MakeSignedInGamer("First");
    auto second = MakeSignedInGamer("Second");
    std::unique_ptr<NetworkSession> session(NetworkSession::Create(NetworkSessionType::Local,
        std::vector<SignedInGamer*>{&first, &second}, 4, 0, NetworkSessionProperties{}));
    auto* oldHost = session->getLocalGamersProperty()[0];
    auto* successor = session->getLocalGamersProperty()[1];
    std::vector<std::string> log;
    session->GamerLeft += [&](System::Object*, const GamerLeftEventArgs& e) {
        EXPECT_EQ(e.getGamerProperty(), oldHost);
        EXPECT_EQ(session->getHostProperty(), successor);
        EXPECT_TRUE(successor->getIsHostProperty());
        EXPECT_TRUE(session->getIsHostProperty());
        EXPECT_EQ(session->getSessionStateProperty(), NetworkSessionState::Lobby);
        log.push_back("left");
    };
    session->HostChanged += [&](System::Object*, const HostChangedEventArgs& e) {
        EXPECT_EQ(e.getOldHostProperty(), nullptr);
        EXPECT_EQ(e.getNewHostProperty(), successor);
        log.push_back("hostChanged");
    };
    session->SessionEnded += [&](System::Object*, const NetworkSessionEndedEventArgs&) {
        log.push_back("unexpectedEnd");
    };
    session->RemoveGamer(oldHost, NetworkSessionEndReason::ClientSignedOut);
    EXPECT_EQ(session->getHostProperty(), nullptr); // intermediate ProcessGamerLeft result
    NetworkSession::NetworkEvent change;
    change.Type = NetworkSession::NetworkEventType::HostChange;
    change.Gamer = successor;
    session->SendNetworkEvent(change);
    EXPECT_TRUE(log.empty());
    session->Update();
    EXPECT_EQ(log, (std::vector<std::string>{"left", "hostChanged"}));
    ASSERT_EQ(session->getLocalGamersProperty().getCountProperty(), 1);
    EXPECT_EQ(session->getLocalGamersProperty()[0], successor);
    ASSERT_EQ(session->getPreviousGamersProperty().getCountProperty(), 1);
    EXPECT_EQ(session->getPreviousGamersProperty()[0], oldHost);
    EXPECT_TRUE(oldHost->getHasLeftSessionProperty());
    // ProcessGamerLeft IL_01a6..01b4 adds HasLeftSession; it does not clear the old host bit.
    EXPECT_TRUE(oldHost->getIsHostProperty());
    session->Update();
    EXPECT_EQ(log.size(), 2u);
}

// A local gamer that leaves while another stays raises GamerLeft alone (XNA ProcessGamerLeft
// IL_01cb..01de), and the session goes on. The host gamer leaving takes Host with it
// (IL_006d..007e); only once the last local gamer has left does the session end (TQ-043).
TEST(NetworkSessionTest, ALocalGamerLeavingRaisesGamerLeftAndTheSessionGoesOnWhileOneRemains) {
    SignedInGamer first = MakeSignedInGamer("First");
    SignedInGamer second = MakeSignedInGamer("Second");
    std::unique_ptr<NetworkSession> session(NetworkSession::Create(
        NetworkSessionType::Local, std::vector<SignedInGamer*>{&first, &second}, 4, 0, NetworkSessionProperties{}));
    LocalNetworkGamer* host = session->getLocalGamersProperty()[0];
    LocalNetworkGamer* other = session->getLocalGamersProperty()[1];
    std::vector<std::string> log;
    session->GamerLeft += [&](System::Object*, const GamerLeftEventArgs& e) {
        log.push_back("left:" + e.getGamerProperty()->getGamertagProperty());
    };
    session->SessionEnded += [&](System::Object*, const NetworkSessionEndedEventArgs& e) {
        log.push_back("ended:" + std::to_string(static_cast<int>(e.getEndReasonProperty())));
    };

    session->RemoveGamer(other, NetworkSessionEndReason::ClientSignedOut);
    session->Update();
    EXPECT_EQ(log, (std::vector<std::string>{"left:Second"}));
    EXPECT_EQ(session->getSessionStateProperty(), NetworkSessionState::Lobby);
    ASSERT_EQ(session->getLocalGamersProperty().getCountProperty(), 1);
    EXPECT_EQ(session->getLocalGamersProperty()[0], host);
    EXPECT_TRUE(other->getHasLeftSessionProperty());
    ASSERT_EQ(session->getPreviousGamersProperty().getCountProperty(), 1);
    EXPECT_EQ(session->getPreviousGamersProperty()[0], other);
    EXPECT_EQ(session->getHostProperty(), host);
    session->StartGame();
    session->Update();
    EXPECT_EQ(session->getSessionStateProperty(), NetworkSessionState::Playing);

    log.clear();
    session->RemoveGamer(host, NetworkSessionEndReason::ClientSignedOut);
    EXPECT_EQ(session->getHostProperty(), nullptr);
    session->Update();
    // ClientSignedOut = 0 (Net.dll enum literal).
    EXPECT_EQ(log, (std::vector<std::string>{"left:First", "ended:0"}));
    EXPECT_EQ(session->getSessionStateProperty(), NetworkSessionState::Ended);
    session->Dispose();
}

// Refresh LocalNetworkGamer.get_SignedInGamer IL_0000..0014 returns the cached identity;
// ProcessGamerLeft never replaces it. This pins the retained identity after a leave,
// not the native kernel's unmeasured sign-out/removal policy (TQ-054 S4).
TEST(NetworkSessionTest, TQ054DepartedLocalKeepsItsSignedInIdentity) {
    auto first = MakeSignedInGamer("First");
    auto second = MakeSignedInGamer("Second");
    std::unique_ptr<NetworkSession> session(NetworkSession::Create(NetworkSessionType::Local,
        std::vector<SignedInGamer*>{&first, &second}, 4, 0, NetworkSessionProperties{}));
    auto* leaving = session->getLocalGamersProperty()[1];
    ASSERT_EQ(leaving->getSignedInGamerProperty(), &second);
    int left = 0;
    session->GamerLeft += [&](System::Object*, const GamerLeftEventArgs& e) {
        EXPECT_EQ(e.getGamerProperty(), leaving);
        EXPECT_EQ(leaving->getSignedInGamerProperty(), &second);
        ++left;
    };
    session->RemoveGamer(leaving, NetworkSessionEndReason::ClientSignedOut);
    session->Update();
    EXPECT_EQ(left, 1);
    ASSERT_EQ(session->getPreviousGamersProperty().getCountProperty(), 1);
    EXPECT_EQ(session->getPreviousGamersProperty()[0], leaving);
    EXPECT_EQ(leaving->getSignedInGamerProperty(), &second);
    ASSERT_EQ(session->getLocalGamersProperty().getCountProperty(), 1);
    EXPECT_TRUE(leaving->getHasLeftSessionProperty());
    EXPECT_EQ(session->getLocalGamersProperty()[0]->getSignedInGamerProperty(), &first);
}

// Task 3.1: every LocalNetworkGamer/NetworkGamer this session ever created (constructor-time
// locals, AddLocalGamer, AddRemoteGamer) used to be permanently leaked - nothing anywhere in this
// codebase ever deleted one. Confirms the fix: gamers created via the constructor and
// AddLocalGamer are tracked (GetOwnedGamerCountForTesting() > 0) and all freed on Dispose().
TEST(NetworkSessionTest, DisposeFreesEveryGamerTheSessionEverOwned) {
    SignedInGamerCollection* previousGlobal = Gamer::getSignedInGamersProperty();
    SignedInGamer extraGamer = MakeSignedInGamer("ExtraTag");
    Gamer::setSignedInGamersProperty(new SignedInGamerCollection(
        SignedInGamerCollection::CreateInternal({&extraGamer})
    ));
    struct RestoreGlobalGuard {
        ~RestoreGlobalGuard() {
            Gamer::setSignedInGamersProperty(new SignedInGamerCollection(SignedInGamerCollection::CreateInternal({})));
        }
    } restoreGuard;

    // maxLocalGamers=2, but only 1 gamer is actually signed in globally - leaves room for
    // AddLocalGamer below (see Task 2.3's identical setup for why this is needed at all).
    auto newGamer = MakeSignedInGamer("NewLocalPlayer");
    NetworkSession* session = NetworkSession::Create(NetworkSessionType::Local, 2, 8);
    std::unique_ptr<NetworkSession> sessionOwner(session);
    ASSERT_EQ(session->getLocalGamersProperty().getCountProperty(), 1);
    EXPECT_EQ(session->GetOwnedGamerCountForTesting(), 1u);

    session->AddLocalGamer(&newGamer);
    EXPECT_EQ(session->GetOwnedGamerCountForTesting(), 1u); // it joins at the next Update (TQ-039)
    session->Update();
    EXPECT_EQ(session->GetOwnedGamerCountForTesting(), 2u);

    session->Dispose();
    EXPECT_EQ(session->GetOwnedGamerCountForTesting(), 0u);
}

// Regression: Dispose() itself was not idempotent - a second
// direct call re-entered the whole body and iterated localGamers_ over an already-freed gamer
// object (ownedGamers_.clear() during the first call destroys it, but nothing pruned the raw
// pointer out of localGamers_), a confirmed ASan heap-buffer-overflow/use-after-free. Confirms a
// second, direct Dispose() call (no fixture/RAII wrapper involved) is a safe no-op that leaves the
// session in the same state as a single call.
TEST(NetworkSessionTest, DisposeCalledTwiceDirectlyIsSafeAndIdempotent) {
    auto gamer = MakeSignedInGamer();
    NetworkSession* session = NetworkSession::Create(
        NetworkSessionType::Local, std::vector<SignedInGamer*>{&gamer}, 8, 0, NetworkSessionProperties{}
    );
    std::unique_ptr<NetworkSession> sessionOwner(session);
    session->Update();

    EXPECT_NO_THROW(session->Dispose());
    EXPECT_TRUE(session->getIsDisposedProperty());
    EXPECT_EQ(session->GetOwnedGamerCountForTesting(), 0u);

    // The bug: this second call used to iterate localGamers_ (still populated pre-fix) and
    // dereference the LocalNetworkGamer the first call already deleted.
    EXPECT_NO_THROW(session->Dispose());
    EXPECT_TRUE(session->getIsDisposedProperty());
    EXPECT_EQ(session->GetOwnedGamerCountForTesting(), 0u);
    EXPECT_EQ(session->getLocalGamersProperty().getCountProperty(), 0);
    EXPECT_EQ(session->getAllGamersProperty().getCountProperty(), 0);
}

// Regression: after only one disposal, callers could obtain a collection containing dangling
// gamer pointers, so the problem was not confined to a second call. This confirms the
// defense-in-depth fix: PreviousGamers (populated by RemoveGamer, which moves a departing
// gamer's raw pointer there rather than dropping it) is emptied by Dispose() too, not just
// LocalGamers/AllGamers - a single Dispose() call must never leave a dangling pointer reachable
// through *any* public collection property.
TEST(NetworkSessionTest, DisposeClearsPreviousGamersSoNoDanglingPointerIsObservable) {
    SignedInGamerCollection* previousGlobal = Gamer::getSignedInGamersProperty();
    SignedInGamer extraGamer = MakeSignedInGamer("ExtraTag");
    Gamer::setSignedInGamersProperty(new SignedInGamerCollection(
        SignedInGamerCollection::CreateInternal({&extraGamer})
    ));
    struct RestoreGlobalGuard {
        ~RestoreGlobalGuard() {
            Gamer::setSignedInGamersProperty(new SignedInGamerCollection(SignedInGamerCollection::CreateInternal({})));
        }
    } restoreGuard;

    auto newGamer = MakeSignedInGamer("NewLocalPlayer");
    NetworkSession* session = NetworkSession::Create(NetworkSessionType::Local, 2, 8);
    std::unique_ptr<NetworkSession> sessionOwner(session);
    ASSERT_EQ(session->getLocalGamersProperty().getCountProperty(), 1);

    session->AddLocalGamer(&newGamer);
    session->Update(); // the added gamer joins here (TQ-039)
    NetworkGamer* leaving = session->getLocalGamersProperty()[1];
    ASSERT_EQ(session->GetOwnedGamerCountForTesting(), 2u);

    // Moves `leaving` (an owned gamer) into PreviousGamers without freeing it - only Dispose()'s
    // ownedGamers_.clear() actually deletes the object.
    session->RemoveGamer(leaving, NetworkSessionEndReason::Disconnected);
    ASSERT_EQ(session->getPreviousGamersProperty().getCountProperty(), 1);

    session->Dispose();

    // Before the fix, this still reported 1 and getPreviousGamersProperty()[0] pointed at the
    // just-freed `leaving` object.
    EXPECT_EQ(session->getPreviousGamersProperty().getCountProperty(), 0);
    EXPECT_EQ(session->getLocalGamersProperty().getCountProperty(), 0);
    EXPECT_EQ(session->getAllGamersProperty().getCountProperty(), 0);
    EXPECT_EQ(session->getRemoteGamersProperty().getCountProperty(), 0);
}

// Disposing invalidates host_ before owned gamers are destroyed. XNA's Dispose(bool) disposes every
// gamer of AllGamers and PreviousGamers (IL_00dd..014b) but leaves the collections and Host as they
// were; CNA frees the gamers it owns, so it empties all four collections and clears Host, whoever
// the host is. This difference is the owner's decision of 2026-11-04 (TQ-039).
TEST(NetworkSessionTest, DisposeClearsHostProperty) {
    auto gamer = MakeSignedInGamer();
    NetworkSession* session = NetworkSession::Create(
        NetworkSessionType::Local, std::vector<SignedInGamer*>{&gamer}, 8, 0, NetworkSessionProperties{}
    );
    std::unique_ptr<NetworkSession> sessionOwner(session);
    ASSERT_NE(session->getHostProperty(), nullptr);
    NetworkGamer remote = NetworkGamer::CreateInternal(session, "Remote");
    NetworkGamer departed = NetworkGamer::CreateInternal(session, "Departed");
    session->AddRemoteGamer(&remote);
    session->AddRemoteGamer(&departed);
    session->RemoveGamer(&departed, NetworkSessionEndReason::Disconnected);
    NetworkSession::NetworkEvent change;
    change.Type = NetworkSession::NetworkEventType::HostChange;
    change.Gamer = &remote;
    session->SendNetworkEvent(change);
    session->Update();
    ASSERT_EQ(session->getHostProperty(), &remote);
    ASSERT_EQ(session->getRemoteGamersProperty().getCountProperty(), 1);
    ASSERT_EQ(session->getPreviousGamersProperty().getCountProperty(), 1);

    session->Dispose();
    EXPECT_EQ(session->getHostProperty(), nullptr);
    EXPECT_EQ(session->getAllGamersProperty().getCountProperty(), 0);
    EXPECT_EQ(session->getLocalGamersProperty().getCountProperty(), 0);
    EXPECT_EQ(session->getRemoteGamersProperty().getCountProperty(), 0);
    EXPECT_EQ(session->getPreviousGamersProperty().getCountProperty(), 0);
}


TEST(NetworkSessionTest, EndFamiliesRejectNullAndForeignResultsWithoutAnAction) {
    auto* foreign = reinterpret_cast<System::IAsyncResult*>(0x1);
    EXPECT_THROW((void)NetworkSession::EndCreate(nullptr), System::ArgumentNullException);
    EXPECT_THROW((void)NetworkSession::EndFind(nullptr), System::ArgumentNullException);
    EXPECT_THROW((void)NetworkSession::EndJoin(nullptr), System::ArgumentNullException);
    EXPECT_THROW((void)NetworkSession::EndJoinInvited(nullptr), System::ArgumentNullException);
    EXPECT_THROW((void)NetworkSession::EndCreate(foreign), System::ArgumentException);
    EXPECT_THROW((void)NetworkSession::EndFind(foreign), System::ArgumentException);
    EXPECT_THROW((void)NetworkSession::EndJoin(foreign), System::ArgumentException);
}

TEST(NetworkSessionTest, WrongEndFamilyPreservesPendingCreate) {
    auto gamer = MakeSignedInGamer();
    const int before = NetworkSession::GetActiveActionInstanceCountForTesting();
    auto* result = NetworkSession::BeginCreate(NetworkSessionType::Local,
        std::vector<SignedInGamer*>{&gamer}, 8, 0, {}, {}, {});
    std::unique_ptr<System::IAsyncResult> resultOwner(result);
    EXPECT_THROW((void)NetworkSession::EndFind(result), System::ArgumentException);
    EXPECT_THROW((void)NetworkSession::EndJoin(result), System::ArgumentException);
    EXPECT_THROW((void)NetworkSession::EndCreate(nullptr), System::ArgumentNullException);
    EXPECT_EQ(NetworkSession::GetActiveActionInstanceCountForTesting(), before + 1);
    EXPECT_THROW((void)NetworkSession::BeginFind(NetworkSessionType::SystemLink, 1, {}, {}, {}),
        System::InvalidOperationException);
    auto* session = NetworkSession::EndCreate(result);
    EXPECT_EQ(session->getMaxGamersProperty(), 8);
    resultOwner.reset();
    EXPECT_EQ(NetworkSession::GetActiveActionInstanceCountForTesting(), before);
    session->Dispose();
    delete session;
}

TEST(NetworkSessionTest, WrongEndFamilyPreservesPendingFind) {
    const int before = NetworkSession::GetActiveActionInstanceCountForTesting();
    auto* result = NetworkSession::BeginFind(NetworkSessionType::SystemLink, 1, {}, {}, {});
    std::unique_ptr<System::IAsyncResult> resultOwner(result);
    EXPECT_THROW((void)NetworkSession::EndCreate(result), System::ArgumentException);
    EXPECT_THROW((void)NetworkSession::EndJoin(result), System::ArgumentException);
    EXPECT_THROW((void)NetworkSession::EndFind(nullptr), System::ArgumentNullException);
    EXPECT_EQ(NetworkSession::GetActiveActionInstanceCountForTesting(), before + 1);
    EXPECT_THROW((void)NetworkSession::BeginFind(NetworkSessionType::SystemLink, 1, {}, {}, {}),
        System::InvalidOperationException);
    EXPECT_EQ(NetworkSession::EndFind(result).getCountProperty(), 0);
    resultOwner.reset();
    EXPECT_EQ(NetworkSession::GetActiveActionInstanceCountForTesting(), before);
}

TEST(NetworkSessionTest, WrongEndFamilyPreservesPendingJoin) {
    auto gamer = MakeSignedInGamer();
    Gamer::setSignedInGamersProperty(new SignedInGamerCollection(
        SignedInGamerCollection::CreateInternal({&gamer})));
    struct RestoreGlobalGuard {
        ~RestoreGlobalGuard() {
            Gamer::setSignedInGamersProperty(new SignedInGamerCollection(
                SignedInGamerCollection::CreateInternal({})));
        }
    } restoreGuard;
    auto available = AvailableNetworkSession::CreateInternal(1, "Host", 0, 8, {},
        QualityOfService::CreateInternal(), "", 0, NetworkSessionType::Local);
    const int before = NetworkSession::GetActiveActionInstanceCountForTesting();
    auto* result = NetworkSession::BeginJoin(&available, {}, {});
    std::unique_ptr<System::IAsyncResult> resultOwner(result);
    EXPECT_THROW((void)NetworkSession::EndCreate(result), System::ArgumentException);
    EXPECT_THROW((void)NetworkSession::EndFind(result), System::ArgumentException);
    EXPECT_THROW((void)NetworkSession::EndJoin(nullptr), System::ArgumentNullException);
    EXPECT_THROW((void)NetworkSession::EndJoin(reinterpret_cast<System::IAsyncResult*>(0x1)),
        System::ArgumentException);
    EXPECT_EQ(NetworkSession::GetActiveActionInstanceCountForTesting(), before + 1);
    EXPECT_THROW((void)NetworkSession::BeginJoin(&available, {}, {}), System::InvalidOperationException);
    auto* session = NetworkSession::EndJoin(result);
    EXPECT_FALSE(session->getIsHostProperty());
    resultOwner.reset();
    EXPECT_EQ(NetworkSession::GetActiveActionInstanceCountForTesting(), before);
    session->Dispose();
    delete session;
}

TEST(NetworkSessionTest, DroppingAnUnconsumedResultReleasesBusyState) {
    auto gamer=MakeSignedInGamer();
    const int before=NetworkSession::GetActiveActionInstanceCountForTesting();
    {
        std::unique_ptr<System::IAsyncResult> result(NetworkSession::BeginCreate(
            NetworkSessionType::Local,std::vector<SignedInGamer*>{&gamer},8,0,{}, {},42));
        EXPECT_EQ(before+1,NetworkSession::GetActiveActionInstanceCountForTesting());
    }
    EXPECT_EQ(before,NetworkSession::GetActiveActionInstanceCountForTesting());
    auto* session=NetworkSession::Create(NetworkSessionType::Local,std::vector<SignedInGamer*>{&gamer},8,0,{});
    session->Dispose();delete session;
}

TEST(NetworkSessionTest, ThrowingImmediateCallbackDoesNotLeakOrStrandTheResult) {
    auto gamer=MakeSignedInGamer();
    const int before=NetworkSession::GetActiveActionInstanceCountForTesting();
    EXPECT_THROW((void)NetworkSession::BeginCreate(NetworkSessionType::Local,
        std::vector<SignedInGamer*>{&gamer},8,0,{},[](auto&) {throw std::runtime_error("callback");},{}),std::runtime_error);
    EXPECT_EQ(before,NetworkSession::GetActiveActionInstanceCountForTesting());
    auto* session=NetworkSession::Create(NetworkSessionType::Local,std::vector<SignedInGamer*>{&gamer},8,0,{});
    session->Dispose();delete session;
}

TEST(NetworkSessionTest, RefusedTransitionsPreserveReadinessAndDoNotQueueEvents) {
    // XNA WriteTransitionCommand IL_0031..0062 checks required state before touching
    // the command buffer (IL_0063 onward). A refused call must have no deferred effects.
    auto gamer = MakeSignedInGamer("TqTransition");
    std::unique_ptr<NetworkSession> session(NetworkSession::Create(
        NetworkSessionType::Local, std::vector<SignedInGamer*>{&gamer}, 4, 0, NetworkSessionProperties{}));
    auto* local = session->getLocalGamersProperty()[0];
    int started = 0, ended = 0;
    session->GameStarted += [&](System::Object* sender, const GameStartedEventArgs&) {
        EXPECT_EQ(sender, session.get());
        EXPECT_EQ(session->getSessionStateProperty(), NetworkSessionState::Playing);
        ++started;
    };
    session->GameEnded += [&](System::Object* sender, const GameEndedEventArgs&) {
        EXPECT_EQ(sender, session.get());
        EXPECT_EQ(session->getSessionStateProperty(), NetworkSessionState::Lobby);
        EXPECT_FALSE(local->getIsReadyProperty());
        ++ended;
    };
    local->setIsReadyProperty(true);
    EXPECT_THROW(session->EndGame(), System::InvalidOperationException);
    session->Update();
    EXPECT_EQ(session->getSessionStateProperty(), NetworkSessionState::Lobby);
    EXPECT_TRUE(local->getIsReadyProperty());
    EXPECT_EQ(started, 0);
    EXPECT_EQ(ended, 0);
    session->StartGame();
    session->Update();
    EXPECT_EQ(started, 1);
    EXPECT_THROW(session->StartGame(), System::InvalidOperationException);
    EXPECT_THROW(session->ResetReady(), System::InvalidOperationException);
    EXPECT_THROW(local->setIsReadyProperty(false), System::InvalidOperationException);
    session->Update();
    EXPECT_EQ(session->getSessionStateProperty(), NetworkSessionState::Playing);
    EXPECT_TRUE(local->getIsReadyProperty());
    EXPECT_EQ(started, 1);
    EXPECT_EQ(ended, 0);
    session->EndGame();
    session->Update();
    EXPECT_EQ(started, 1);
    EXPECT_EQ(ended, 1);
    EXPECT_THROW(session->EndGame(), System::InvalidOperationException);
    session->Update();
    EXPECT_EQ(ended, 1);
    session->Dispose();
}

// XNA set_AllowHostMigration and set_AllowJoinInProgress (IL_0000..0007) ignore an unchanged
// value; a change goes through SendAllowCommand, whose IL_0000..0030 refuses a disposed session
// (ObjectDisposedException) and a non-host (InvalidOperationException) for every session type,
// Local included, before anything is stored. The slot setters reach the same guards through
// SetGamerSlots IL_0000..0030 once their range checks pass. The session stops being the host when
// a host change names a remote gamer (ProcessHostChanged IL_0052..005e; TQ-043).
TEST(NetworkSessionTest, AllowSettersRefuseANonHostOrDisposedLocalSessionAndKeepTheOldValue) {
    auto gamer = MakeSignedInGamer("AllowHost");
    std::unique_ptr<NetworkSession> session(NetworkSession::Create(
        NetworkSessionType::Local, std::vector<SignedInGamer*>{&gamer}, 4, 0, NetworkSessionProperties{}));
    auto* local = session->getLocalGamersProperty()[0];
    NetworkGamer remote = NetworkGamer::CreateInternal(session.get(), "RemoteHost");
    session->AddRemoteGamer(&remote);
    NetworkSession::NetworkEvent change;
    change.Type = NetworkSession::NetworkEventType::HostChange;

    change.Gamer = &remote;
    session->SendNetworkEvent(change);
    ASSERT_FALSE(session->getIsHostProperty());
    EXPECT_NO_THROW(session->setAllowHostMigrationProperty(false));
    EXPECT_NO_THROW(session->setAllowJoinInProgressProperty(false));
    EXPECT_THROW(session->setAllowHostMigrationProperty(true), System::InvalidOperationException);
    EXPECT_THROW(session->setAllowJoinInProgressProperty(true), System::InvalidOperationException);
    EXPECT_FALSE(session->getAllowHostMigrationProperty());
    EXPECT_FALSE(session->getAllowJoinInProgressProperty());

    change.Gamer = local;
    session->SendNetworkEvent(change);
    ASSERT_TRUE(session->getIsHostProperty());
    session->setAllowHostMigrationProperty(true);
    session->setAllowJoinInProgressProperty(true);
    session->Dispose();

    EXPECT_NO_THROW(session->setAllowHostMigrationProperty(true));
    EXPECT_NO_THROW(session->setAllowJoinInProgressProperty(true));
    EXPECT_THROW(session->setAllowHostMigrationProperty(false), System::ObjectDisposedException);
    EXPECT_THROW(session->setAllowJoinInProgressProperty(false), System::ObjectDisposedException);
    EXPECT_TRUE(session->getAllowHostMigrationProperty());
    EXPECT_TRUE(session->getAllowJoinInProgressProperty());
    EXPECT_THROW(session->setMaxGamersProperty(8), System::ObjectDisposedException);
    EXPECT_THROW(session->setPrivateGamerSlotsProperty(1), System::ObjectDisposedException);
    EXPECT_EQ(session->getMaxGamersProperty(), 4);
    EXPECT_EQ(session->getPrivateGamerSlotsProperty(), 0);
}

// XNA ProcessHostChanged IL_0018..004d keeps the old host, installs the new one and only then
// queues HostChanged, whose handler (b__1a IL_0010..0027) passes (oldHost, current Host): a
// handler already reads the new Session.Host. Nothing is raised before Update
// (RaiseUpdateEvents). A host change applies as it arrives, as XNA processes every kernel result
// of an Update before raising any (Update IL_0078..008d; TQ-043). The transport bridge used by
// host migration behaves the same way, and its silent initial establishment raises nothing.
TEST(NetworkSessionTest, HostChangedHandlersAlreadySeeTheNewHost) {
    auto gamer = MakeSignedInGamer("FirstHost");
    std::unique_ptr<NetworkSession> session(NetworkSession::Create(
        NetworkSessionType::Local, std::vector<SignedInGamer*>{&gamer}, 4, 0, NetworkSessionProperties{}));
    auto* local = session->getLocalGamersProperty()[0];
    ASSERT_EQ(session->getHostProperty(), local);
    NetworkGamer successor = NetworkGamer::CreateInternal(session.get(), "Successor");
    session->AddRemoteGamer(&successor);
    session->Update();

    struct Change { System::Object* sender; NetworkGamer* oldHost; NetworkGamer* newHost; NetworkGamer* hostDuringEvent; };
    std::vector<Change> changes;
    session->HostChanged += [&](System::Object* sender, const HostChangedEventArgs& e) {
        changes.push_back({sender, e.getOldHostProperty(), e.getNewHostProperty(), session->getHostProperty()});
    };

    NetworkSession::NetworkEvent change;
    change.Type = NetworkSession::NetworkEventType::HostChange;
    change.Gamer = &successor;
    session->SendNetworkEvent(change);
    EXPECT_TRUE(changes.empty());
    EXPECT_EQ(session->getHostProperty(), &successor);
    session->Update();
    ASSERT_EQ(changes.size(), 1u);
    EXPECT_EQ(changes[0].sender, session.get());
    EXPECT_EQ(changes[0].oldHost, local);
    EXPECT_EQ(changes[0].newHost, &successor);
    EXPECT_EQ(changes[0].hostDuringEvent, &successor);
    EXPECT_EQ(session->getHostProperty(), &successor);

    CNA::Internal::Net::ENetBackend::EstablishTransportHost(session.get(), local, true);
    EXPECT_EQ(changes.size(), 1u);
    EXPECT_EQ(session->getHostProperty(), local);
    session->Update();
    ASSERT_EQ(changes.size(), 2u);
    EXPECT_EQ(changes[1].oldHost, &successor);
    EXPECT_EQ(changes[1].newHost, local);
    EXPECT_EQ(changes[1].hostDuringEvent, local);

    CNA::Internal::Net::ENetBackend::EstablishTransportHost(session.get(), &successor, false);
    EXPECT_EQ(changes.size(), 2u);
    EXPECT_EQ(session->getHostProperty(), &successor);
    session->Dispose();
}

// XNA ProcessStateChanged stores the new state first (IL_0010..0019); Ended queues only
// SessionEnded with its reason (IL_001e..0056), Playing queues GameStarted (IL_0057..0072), and
// GameEnded is queued only for Playing -> Lobby (IL_0077..0098). Losing the session in the middle
// of a game is therefore SessionEnded alone.
TEST(NetworkSessionTest, LosingTheSessionWhilePlayingRaisesSessionEndedButNotGameEnded) {
    auto gamer = MakeSignedInGamer("Player");
    std::unique_ptr<NetworkSession> session(NetworkSession::Create(
        NetworkSessionType::Local, std::vector<SignedInGamer*>{&gamer}, 4, 0, NetworkSessionProperties{}));
    std::vector<std::string> log;
    const auto stateName = [&] { return std::to_string(static_cast<int>(session->getSessionStateProperty())); };
    session->GameStarted += [&](System::Object*, const GameStartedEventArgs&) { log.push_back("started@" + stateName()); };
    session->GameEnded += [&](System::Object*, const GameEndedEventArgs&) { log.push_back("gameEnded@" + stateName()); };
    session->SessionEnded += [&](System::Object* sender, const NetworkSessionEndedEventArgs& e) {
        EXPECT_EQ(sender, session.get());
        log.push_back("sessionEnded@" + stateName() + ":" + std::to_string(static_cast<int>(e.getEndReasonProperty())));
    };

    session->StartGame();
    session->Update();
    session->RemoveGamer(session->getLocalGamersProperty()[0], NetworkSessionEndReason::RemovedByHost);
    EXPECT_EQ(log, (std::vector<std::string>{"started@1"}));
    session->Update();
    // Playing = 1, Ended = 2, RemovedByHost = 2 (Net.dll enum literals).
    EXPECT_EQ(log, (std::vector<std::string>{"started@1", "sessionEnded@2:2"}));
    EXPECT_EQ(session->getSessionStateProperty(), NetworkSessionState::Ended);
    session->Update();
    EXPECT_EQ(log.size(), 2u);
    session->Dispose();
}

// XNA queues every session event (each Process* method Enqueues a PendingEvent) and raises them
// only from RaiseUpdateEvents IL_0000..0020, in queue order. ProcessGamerLeft removes the gamer
// from AllGamers, marks it HasLeftSession (IL_019b..01b4) and moves it to PreviousGamers
// (IL_01ba..01c6) before queueing GamerLeft, so its handler sees the gamer already gone.
TEST(NetworkSessionTest, QueuedSessionEventsAreRaisedInOrderAtTheNextUpdate) {
    auto gamer = MakeSignedInGamer("HostTag");
    std::unique_ptr<NetworkSession> session(NetworkSession::Create(
        NetworkSessionType::Local, std::vector<SignedInGamer*>{&gamer}, 4, 0, NetworkSessionProperties{}));
    session->Update();
    std::vector<std::string> log;
    session->GamerJoined += [&](System::Object* sender, const GamerJoinedEventArgs& e) {
        EXPECT_EQ(sender, session.get());
        log.push_back("joined:" + e.getGamerProperty()->getGamertagProperty());
    };
    ASSERT_EQ(log, (std::vector<std::string>{"joined:HostTag"}));
    log.clear();
    session->GameStarted += [&](System::Object*, const GameStartedEventArgs&) { log.push_back("started"); };
    NetworkGamer* leftGamer = nullptr;
    session->GamerLeft += [&](System::Object* sender, const GamerLeftEventArgs& e) {
        EXPECT_EQ(sender, session.get());
        leftGamer = e.getGamerProperty();
        EXPECT_TRUE(leftGamer->getHasLeftSessionProperty());
        const auto& all = session->getAllGamersProperty();
        const auto& previous = session->getPreviousGamersProperty();
        EXPECT_EQ(std::find(all.begin(), all.end(), leftGamer), all.end());
        EXPECT_NE(std::find(previous.begin(), previous.end(), leftGamer), previous.end());
        log.push_back("left:" + leftGamer->getGamertagProperty());
    };

    NetworkGamer remote = NetworkGamer::CreateInternal(session.get(), "RemoteTag");
    session->AddRemoteGamer(&remote);
    session->StartGame();
    session->RemoveGamer(&remote, NetworkSessionEndReason::Disconnected);
    EXPECT_TRUE(log.empty());
    session->Update();
    EXPECT_EQ(log, (std::vector<std::string>{"joined:RemoteTag", "started", "left:RemoteTag"}));
    EXPECT_EQ(leftGamer, &remote);
    session->Update();
    EXPECT_EQ(log.size(), 3u);
    session->Dispose();
}

// XNA ProcessGamerLeft IL_006d..007e sets Host to null when the gamer leaving is the host, before
// GamerLeft is queued, and a later ProcessHostChanged keeps that null as its old host
// (IL_0018..001f) before it installs the new one (IL_0040..004d). A gamer other than the host
// leaves Host alone. The transport's host replacement used by SystemLink migration reports the
// same null old host (TQ-039).
TEST(NetworkSessionTest, HostBecomesNullWhenTheHostLeavesUntilAHostChange) {
    auto gamer = MakeSignedInGamer("LocalTag");
    std::unique_ptr<NetworkSession> session(NetworkSession::Create(
        NetworkSessionType::Local, std::vector<SignedInGamer*>{&gamer}, 4, 0, NetworkSessionProperties{}));
    auto* local = session->getLocalGamersProperty()[0];
    NetworkGamer remoteHost = NetworkGamer::CreateInternal(session.get(), "RemoteHost");
    NetworkGamer bystander = NetworkGamer::CreateInternal(session.get(), "Bystander");
    NetworkGamer successor = NetworkGamer::CreateInternal(session.get(), "Successor");
    session->AddRemoteGamer(&remoteHost);
    session->AddRemoteGamer(&bystander);
    session->AddRemoteGamer(&successor);
    NetworkSession::NetworkEvent toRemote;
    toRemote.Type = NetworkSession::NetworkEventType::HostChange;
    toRemote.Gamer = &remoteHost;
    session->SendNetworkEvent(toRemote);
    session->Update();
    ASSERT_EQ(session->getHostProperty(), &remoteHost);

    struct Change { NetworkGamer* oldHost; NetworkGamer* newHost; };
    std::vector<Change> changes;
    std::vector<NetworkGamer*> hostWhenLeft;
    session->HostChanged += [&](System::Object*, const HostChangedEventArgs& e) {
        changes.push_back({e.getOldHostProperty(), e.getNewHostProperty()});
    };
    session->GamerLeft += [&](System::Object*, const GamerLeftEventArgs&) {
        hostWhenLeft.push_back(session->getHostProperty());
    };

    session->RemoveGamer(&bystander, NetworkSessionEndReason::Disconnected);
    EXPECT_EQ(session->getHostProperty(), &remoteHost);
    session->Update();
    session->RemoveGamer(&remoteHost, NetworkSessionEndReason::Disconnected);
    EXPECT_EQ(session->getHostProperty(), nullptr);
    session->Update();
    EXPECT_EQ(hostWhenLeft, (std::vector<NetworkGamer*>{&remoteHost, nullptr}));
    EXPECT_EQ(session->getHostProperty(), nullptr);
    EXPECT_TRUE(changes.empty());

    NetworkSession::NetworkEvent toLocal;
    toLocal.Type = NetworkSession::NetworkEventType::HostChange;
    toLocal.Gamer = local;
    session->SendNetworkEvent(toLocal);
    session->Update();
    ASSERT_EQ(changes.size(), 1u);
    EXPECT_EQ(changes[0].oldHost, nullptr);
    EXPECT_EQ(changes[0].newHost, local);
    EXPECT_EQ(session->getHostProperty(), local);

    CNA::Internal::Net::ENetBackend::EstablishTransportHost(session.get(), &successor, true);
    session->Update();
    ASSERT_EQ(changes.size(), 2u);
    EXPECT_EQ(changes[1].oldHost, local);
    session->RemoveGamer(&successor, NetworkSessionEndReason::Disconnected);
    EXPECT_EQ(session->getHostProperty(), nullptr);
    CNA::Internal::Net::ENetBackend::EstablishTransportHost(session.get(), local, true);
    session->Update();
    ASSERT_EQ(changes.size(), 3u);
    EXPECT_EQ(changes[2].oldHost, nullptr);
    EXPECT_EQ(changes[2].newHost, local);
    EXPECT_EQ(session->getHostProperty(), local);
    session->Dispose();
}

// XNA NetworkGamer.IsHost is the gamer's own state bit (get_IsHost IL_0000..000e) and a session has
// one host gamer, the first listed (GetLocalGamers IL_0069..0077). NetworkSession.IsHost is the
// session's own flag (get_IsHost IL_0000..0006), set at creation (.ctor IL_008d..0090) and moved
// only by ProcessHostChanged, which clears the old host's bit (IL_002a..003b), installs the new host
// (IL_0040..004d), makes IsHost whether it is local (IL_0052..005e) and sets its bit
// (IL_0063..0071). The host-only calls read that flag (SendAllowCommand and WriteTransitionCommand
// IL_001e..0030). ProcessGamerLeft clears Host (IL_006d..007e) and only adds HasLeftSession to the
// departing gamer's state (IL_01a6..01b4), so neither flag moves when a host leaves (TQ-043).
TEST(NetworkSessionTest, OnlyTheHostGamerReportsIsHostAndHostChangesMoveIt) {
    SignedInGamer first = MakeSignedInGamer("First");
    SignedInGamer second = MakeSignedInGamer("Second");
    SignedInGamer third = MakeSignedInGamer("Third");
    std::unique_ptr<NetworkSession> session(NetworkSession::Create(
        NetworkSessionType::Local, std::vector<SignedInGamer*>{&first, &second}, 4, 0, NetworkSessionProperties{}));
    const auto& locals = session->getLocalGamersProperty();
    ASSERT_EQ(locals.getCountProperty(), 2);
    LocalNetworkGamer* host = locals[0];
    LocalNetworkGamer* other = locals[1];
    EXPECT_EQ(session->getHostProperty(), host);
    EXPECT_TRUE(host->getIsHostProperty());
    EXPECT_FALSE(other->getIsHostProperty());
    EXPECT_TRUE(session->getIsHostProperty());

    session->AddLocalGamer(&third);
    session->Update();
    ASSERT_EQ(locals.getCountProperty(), 3);
    EXPECT_FALSE(locals[2]->getIsHostProperty());
    EXPECT_TRUE(host->getIsHostProperty());

    NetworkGamer remote = NetworkGamer::CreateInternal(session.get(), "Remote");
    session->AddRemoteGamer(&remote);
    session->Update();
    EXPECT_FALSE(remote.getIsHostProperty());
    NetworkSession::NetworkEvent change;
    change.Type = NetworkSession::NetworkEventType::HostChange;
    change.Gamer = &remote;
    session->SendNetworkEvent(change);
    EXPECT_EQ(session->getHostProperty(), &remote);
    EXPECT_TRUE(remote.getIsHostProperty());
    EXPECT_FALSE(host->getIsHostProperty());
    EXPECT_FALSE(session->getIsHostProperty());
    EXPECT_THROW(session->StartGame(), System::InvalidOperationException);
    EXPECT_THROW(session->setAllowJoinInProgressProperty(true), System::InvalidOperationException);
    session->Update();
    EXPECT_EQ(session->getSessionStateProperty(), NetworkSessionState::Lobby);

    change.Gamer = other;
    session->SendNetworkEvent(change);
    session->Update();
    EXPECT_EQ(session->getHostProperty(), other);
    EXPECT_TRUE(other->getIsHostProperty());
    EXPECT_FALSE(remote.getIsHostProperty());
    EXPECT_FALSE(host->getIsHostProperty());
    EXPECT_FALSE(locals[2]->getIsHostProperty());
    EXPECT_TRUE(session->getIsHostProperty());
    session->setAllowJoinInProgressProperty(true);
    EXPECT_TRUE(session->getAllowJoinInProgressProperty());

    // The host gamer leaves: Host goes, but the session is still the host and the departed gamer
    // keeps its own bit.
    session->RemoveGamer(other, NetworkSessionEndReason::ClientSignedOut);
    EXPECT_EQ(session->getHostProperty(), nullptr);
    EXPECT_TRUE(session->getIsHostProperty());
    EXPECT_TRUE(other->getIsHostProperty());
    EXPECT_FALSE(host->getIsHostProperty());
    session->Dispose();
}

// XNA's Update processes every kernel result (ProcessUpdateResults, IL_0078..007a) before
// RaiseUpdateEvents raises any (IL_008c..008d). When the host's leave and the next host change come
// together, ProcessGamerLeft clears Host (IL_006d..007e) and ProcessHostChanged installs the next one
// (IL_0018..0071) before GamerLeft is raised, so its handler already sees the new Host; the events
// are raised in arrival order. HostChanged passes Host as it is when raised (<ProcessHostChanged>b__1a
// IL_0010..0027), so two changes in one Update both report the final host. CNA's transport inputs
// apply as they arrive (TQ-043).
TEST(NetworkSessionTest, AHostLeaveAndAHostChangeInOneUpdateAreBothAppliedBeforeEitherIsRaised) {
    SignedInGamer gamer = MakeSignedInGamer("LocalTag");
    std::unique_ptr<NetworkSession> session(NetworkSession::Create(
        NetworkSessionType::Local, std::vector<SignedInGamer*>{&gamer}, 4, 0, NetworkSessionProperties{}));
    LocalNetworkGamer* local = session->getLocalGamersProperty()[0];
    NetworkGamer departing = NetworkGamer::CreateInternal(session.get(), "Departing");
    NetworkGamer successor = NetworkGamer::CreateInternal(session.get(), "Successor");
    NetworkGamer last = NetworkGamer::CreateInternal(session.get(), "Last");
    session->AddRemoteGamer(&departing);
    session->AddRemoteGamer(&successor);
    session->AddRemoteGamer(&last);
    NetworkSession::NetworkEvent change;
    change.Type = NetworkSession::NetworkEventType::HostChange;
    change.Gamer = &departing;
    session->SendNetworkEvent(change);
    session->Update();
    ASSERT_EQ(session->getHostProperty(), &departing);

    const auto name = [](const NetworkGamer* gamer) { return gamer != nullptr ? gamer->getGamertagProperty() : std::string("none"); };
    std::vector<std::string> log;
    session->GamerLeft += [&](System::Object*, const GamerLeftEventArgs& e) {
        log.push_back("left:" + name(e.getGamerProperty()) + " host=" + name(session->getHostProperty()));
    };
    session->HostChanged += [&](System::Object*, const HostChangedEventArgs& e) {
        log.push_back("changed:" + name(e.getOldHostProperty()) + "->" + name(e.getNewHostProperty()) +
                      " host=" + name(session->getHostProperty()));
    };

    session->RemoveGamer(&departing, NetworkSessionEndReason::Disconnected);
    change.Gamer = &successor;
    session->SendNetworkEvent(change);
    EXPECT_EQ(session->getHostProperty(), &successor);
    EXPECT_TRUE(successor.getIsHostProperty());
    EXPECT_TRUE(log.empty());
    session->Update();
    EXPECT_EQ(log, (std::vector<std::string>{"left:Departing host=Successor", "changed:none->Successor host=Successor"}));

    log.clear();
    change.Gamer = &last;
    session->SendNetworkEvent(change);
    change.Gamer = local;
    session->SendNetworkEvent(change);
    EXPECT_EQ(session->getHostProperty(), local);
    EXPECT_TRUE(local->getIsHostProperty());
    EXPECT_FALSE(last.getIsHostProperty());
    EXPECT_FALSE(successor.getIsHostProperty());
    EXPECT_TRUE(session->getIsHostProperty());
    session->Update();
    EXPECT_EQ(log, (std::vector<std::string>{"changed:Successor->LocalTag host=LocalTag",
                                             "changed:Last->LocalTag host=LocalTag"}));
    session->Dispose();
}

#ifndef __EMSCRIPTEN__
// A refused SystemLink join ends the joining machine's session. Join must report the refusal
// whether Host is still the construction-time placeholder or null (a host that left takes Host
// with it, XNA ProcessGamerLeft IL_006d..007e) (TQ-039, TQ-043).
TEST(NetworkSessionTest, JoinRefusedByTheHostThrowsAfterHostIsCleared) {
    CNA::Internal::Net::ENetHostHandle refusingHost = CNA::Internal::Net::ENetHostHandle::CreateHost(0, 4, 2);
    const uint16_t refusingPort = refusingHost.getBoundPortProperty();
    ASSERT_GT(refusingPort, 0);
    std::atomic<bool> refused{false};
    std::jthread serverThread([&](std::stop_token stopToken) {
        ENetPeer* peer = nullptr;
        while (!stopToken.stop_requested()) {
            ENetEvent evt{};
            if (refusingHost.Service(0, evt) <= 0) {
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
                continue;
            }
            if (evt.type == ENET_EVENT_TYPE_CONNECT) {
                peer = evt.peer;
                continue;
            }
            if (evt.type != ENET_EVENT_TYPE_RECEIVE) continue;
            std::vector<SharpRuntime::bytecs> data(evt.packet->data, evt.packet->data + evt.packet->dataLength);
            enet_packet_destroy(evt.packet);
            if (peer != nullptr &&
                CNA::Internal::Net::NetPacketCodec::PeekTag(data) == CNA::Internal::Net::MessageTag::ClientHello &&
                !refused.exchange(true)) {
                refusingHost.Disconnect(peer, CNA::Internal::Net::DisconnectSessionNotJoinable);
                refusingHost.Flush();
            }
        }
    });
    SignedInGamer joiningGamer = MakeSignedInGamer("Joiner");
    Gamer::setSignedInGamersProperty(new SignedInGamerCollection(SignedInGamerCollection::CreateInternal({&joiningGamer})));
    struct RestoreGlobalGuard {
        ~RestoreGlobalGuard() {
            Gamer::setSignedInGamersProperty(new SignedInGamerCollection(SignedInGamerCollection::CreateInternal({})));
        }
    } restoreGuard;
    AvailableNetworkSession availableSession = AvailableNetworkSession::CreateInternal(
        1, "RefusingHost", 0, 8, NetworkSessionProperties{}, QualityOfService::CreateInternal(),
        "127.0.0.1", refusingPort, NetworkSessionType::SystemLink);

    try {
        NetworkSession* joined = NetworkSession::Join(&availableSession);
        if (joined != nullptr) {
            joined->Dispose();
            delete joined;
        }
        ADD_FAILURE() << "a refused join returned a session";
    } catch (const NetworkSessionJoinException& e) {
        EXPECT_EQ(e.getJoinErrorProperty(), NetworkSessionJoinError::SessionNotJoinable);
    }
    EXPECT_TRUE(refused.load());
    serverThread.request_stop();

    // The refused join gave back the process's one session slot.
    SignedInGamer after = MakeSignedInGamer("After");
    std::unique_ptr<NetworkSession> next(NetworkSession::Create(
        NetworkSessionType::Local, std::vector<SignedInGamer*>{&after}, 4, 0, NetworkSessionProperties{}));
    ASSERT_NE(next, nullptr);
    next->Dispose();
}
#endif

// Refresh Net.dll Update IL_0078..008d processes all results before raising events;
// ProcessStateChanged IL_0010..0019 stores the state before queueing its notification.
TEST(NetworkSessionTest, TQ054EarlierJoinSeesProcessedStateAndCommandsStayDeferred) {
    auto first = MakeSignedInGamer("First");
    std::unique_ptr<NetworkSession> session(NetworkSession::Create(NetworkSessionType::Local,
        std::vector<SignedInGamer*>{&first}, 4, 0, NetworkSessionProperties{}));
    session->Update();
    std::vector<std::string> log;
    session->GamerJoined += [&](System::Object*, const GamerJoinedEventArgs& e) {
        if (e.getGamerProperty()->getGamertagProperty() != "Second") return; // initial replay
        EXPECT_EQ(session->getSessionStateProperty(), NetworkSessionState::Playing);
        log.push_back("joined");
    };
    session->GameStarted += [&](System::Object*, const GameStartedEventArgs&) {
        EXPECT_EQ(session->getSessionStateProperty(), NetworkSessionState::Playing);
        log.push_back("started");
        session->EndGame();
        EXPECT_EQ(session->getSessionStateProperty(), NetworkSessionState::Playing);
    };
    session->GameEnded += [&](System::Object*, const GameEndedEventArgs&) {
        EXPECT_EQ(session->getSessionStateProperty(), NetworkSessionState::Lobby);
        log.push_back("ended");
    };
    // Inject a join result before StartGame's result in this batch.
    NetworkGamer remote = NetworkGamer::CreateInternal(session.get(), "Second");
    session->AddRemoteGamer(&remote);
    session->StartGame();
    EXPECT_EQ(session->getSessionStateProperty(), NetworkSessionState::Lobby);
    EXPECT_TRUE(log.empty());
    session->Update();
    EXPECT_EQ(log, (std::vector<std::string>{"joined", "started"}));
    EXPECT_EQ(session->getSessionStateProperty(), NetworkSessionState::Playing);
    session->Update();
    EXPECT_EQ(log, (std::vector<std::string>{"joined", "started", "ended"}));
    session->Update();
    EXPECT_EQ(log.size(), 3u);
    session->Dispose();
}

// ProcessStateChanged IL_0057..0098 chooses notifications from each old/new pair,
// while every callback reads the final state of the processed batch (Update IL_0078..008d).
TEST(NetworkSessionTest, TQ054StateBurstKeepsEventOrderAndSuppressesLobbyNoOp) {
    auto gamer = MakeSignedInGamer();
    std::unique_ptr<NetworkSession> session(NetworkSession::Create(NetworkSessionType::Local,
        std::vector<SignedInGamer*>{&gamer}, 4, 0, NetworkSessionProperties{}));
    std::vector<std::string> log;
    const auto record = [&](const char* event) {
        EXPECT_EQ(session->getSessionStateProperty(), NetworkSessionState::Lobby);
        log.emplace_back(event);
    };
    session->GameStarted += [&](System::Object*, const GameStartedEventArgs&) { record("started"); };
    session->GameEnded += [&](System::Object*, const GameEndedEventArgs&) { record("ended"); };
    NetworkSession::NetworkEvent state;
    state.Type = NetworkSession::NetworkEventType::StateChange;
    state.State = NetworkSessionState::Lobby;
    session->SendNetworkEvent(state); // Lobby -> Lobby: no notification
    state.State = NetworkSessionState::Playing;
    session->SendNetworkEvent(state);
    state.State = NetworkSessionState::Lobby;
    session->SendNetworkEvent(state);
    session->SendNetworkEvent(state); // duplicate Lobby: no notification
    session->Update();
    EXPECT_EQ(log, (std::vector<std::string>{"started", "ended"}));
    session->Update();
    EXPECT_EQ(log.size(), 2u);
}

// ProcessStateChanged IL_001e..0056 captures the end reason; a prior GamerLeft callback
// observes Ended too, because all results are processed before callbacks (Update IL_0078..008d).
TEST(NetworkSessionTest, TQ054LastLeaveHandlerAlreadySeesEnded) {
    auto gamer = MakeSignedInGamer();
    std::unique_ptr<NetworkSession> session(NetworkSession::Create(NetworkSessionType::Local,
        std::vector<SignedInGamer*>{&gamer}, 4, 0, NetworkSessionProperties{}));
    std::vector<std::string> log;
    session->GamerLeft += [&](System::Object*, const GamerLeftEventArgs&) {
        EXPECT_EQ(session->getSessionStateProperty(), NetworkSessionState::Ended);
        log.push_back("left");
    };
    session->SessionEnded += [&](System::Object*, const NetworkSessionEndedEventArgs& e) {
        EXPECT_EQ(e.getEndReasonProperty(), NetworkSessionEndReason::ClientSignedOut);
        log.push_back("sessionEnded");
    };
    // Last-local end generation is the existing TQ-043 boundary; this test pins batch visibility.
    session->RemoveGamer(session->getLocalGamersProperty()[0], NetworkSessionEndReason::ClientSignedOut);
    EXPECT_TRUE(log.empty());
    session->Update();
    EXPECT_EQ(log, (std::vector<std::string>{"left", "sessionEnded"}));
}

// RaiseUpdateEvents IL_0002..000f dequeues just the current callback before invoking it.
// A thrown callback cannot undo processing or discard later notifications.
TEST(NetworkSessionTest, TQ054ThrowingJoinPreservesProcessedStateAndLaterNotification) {
    auto gamer = MakeSignedInGamer();
    std::unique_ptr<NetworkSession> session(NetworkSession::Create(NetworkSessionType::Local,
        std::vector<SignedInGamer*>{&gamer}, 4, 0, NetworkSessionProperties{}));
    int joins = 0, starts = 0;
    session->GamerJoined += [&](System::Object*, const GamerJoinedEventArgs& e) {
        if (e.getGamerProperty()->getGamertagProperty() != "Throwing") return;
        ++joins;
        throw System::InvalidOperationException("test callback");
    };
    session->GameStarted += [&](System::Object*, const GameStartedEventArgs&) { ++starts; };
    NetworkGamer remote = NetworkGamer::CreateInternal(session.get(), "Throwing");
    session->AddRemoteGamer(&remote);
    session->StartGame();
    EXPECT_THROW(session->Update(), System::InvalidOperationException);
    EXPECT_EQ(session->getSessionStateProperty(), NetworkSessionState::Playing);
    EXPECT_EQ(joins, 1);
    EXPECT_EQ(starts, 0);
    session->Update();
    EXPECT_EQ(joins, 1);
    EXPECT_EQ(starts, 1);
    session->Update();
    EXPECT_EQ(starts, 1);
    session->Dispose();
}

// CNA lifetime rule: Dispose frees owned gamers. Pending notifications must not retain
// gamer pointers that a later callback could dereference after an earlier callback disposes.
TEST(NetworkSessionTest, TQ054DisposeInsideCallbackDropsRemainingNotifications) {
    auto gamer = MakeSignedInGamer();
    std::unique_ptr<NetworkSession> session(NetworkSession::Create(NetworkSessionType::Local,
        std::vector<SignedInGamer*>{&gamer}, 4, 0, NetworkSessionProperties{}));
    int starts = 0;
    session->GameStarted += [&](System::Object*, const GameStartedEventArgs&) { ++starts; };
    session->GamerLeft += [&](System::Object*, const GamerLeftEventArgs&) { session->Dispose(); };
    NetworkGamer remote = NetworkGamer::CreateInternal(session.get(), "Leaving");
    session->AddRemoteGamer(&remote);
    session->Update();
    session->RemoveGamer(&remote, NetworkSessionEndReason::Disconnected);
    session->StartGame();
    session->Update();
    EXPECT_TRUE(session->getIsDisposedProperty());
    EXPECT_EQ(starts, 0);
}

// SOCKETS: SystemLink opens ENet UDP even though these result injections need no peer.
// Refresh ProcessReceivedData IL_0053..0079 queues each recipient's packet before callbacks.
// ReceiveData(byte[], int, out) IL_00b9..00e4 copies Size bytes and returns sender/Size.
TEST(NetworkSessionTest, TQ054EarlierJoinCanConsumeReceivedPacketsInOrder) {
    auto first = MakeSignedInGamer("First");
    auto second = MakeSignedInGamer("Second");
    std::unique_ptr<NetworkSession> session(NetworkSession::Create(NetworkSessionType::SystemLink,
        std::vector<SignedInGamer*>{&first, &second}, 4, 0, NetworkSessionProperties{}));
    auto* receiver = session->getLocalGamersProperty()[0];
    auto* other = session->getLocalGamersProperty()[1];
    NetworkGamer remote = NetworkGamer::CreateInternal(session.get(), "Sender");
    int callbacks = 0;
    session->GamerJoined += [&](System::Object*, const GamerJoinedEventArgs& e) {
        if (e.getGamerProperty() != &remote) return;
        ++callbacks;
        ASSERT_TRUE(receiver->getIsDataAvailableProperty());
        EXPECT_FALSE(other->getIsDataAvailableProperty());
        std::vector<SharpRuntime::bytecs> bytes(4, 0xcc);
        NetworkGamer* sender = nullptr;
        EXPECT_EQ(receiver->ReceiveData(bytes, sender), 3);
        EXPECT_EQ(sender, &remote);
        EXPECT_EQ(bytes, (std::vector<SharpRuntime::bytecs>{0x00, 0x7f, 0xff, 0xcc}));
        ASSERT_TRUE(receiver->getIsDataAvailableProperty());
        EXPECT_EQ(receiver->ReceiveData(bytes, sender), 1);
        EXPECT_EQ(sender, &remote);
        EXPECT_EQ(bytes, (std::vector<SharpRuntime::bytecs>{0x42, 0x7f, 0xff, 0xcc}));
        EXPECT_FALSE(receiver->getIsDataAvailableProperty());
    };
    session->AddRemoteGamer(&remote);
    NetworkSession::NetworkEvent packet;
    packet.Type = NetworkSession::NetworkEventType::PacketSend;
    packet.Gamer = receiver;
    packet.Sender = &remote;
    packet.Packet = {0x00, 0x7f, 0xff}; // chosen literal bytes, not CNA output
    session->SendNetworkEvent(packet);
    packet.Packet = {0x42};
    session->SendNetworkEvent(packet);
    session->Update();
    EXPECT_EQ(callbacks, 1);
    EXPECT_FALSE(receiver->getIsDataAvailableProperty());
    session->Update();
    EXPECT_EQ(callbacks, 1);
    session->Dispose();
}
