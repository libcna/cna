// SPDX-License-Identifier: MS-PL
#include <gtest/gtest.h>

#include "CNA/Internal/Net/ENetBackend.hpp"
#include "CNA/Internal/Net/ENetHostHandle.hpp"
#include "CNA/Internal/Net/NetPacketCodec.hpp"
#include "Microsoft/Xna/Framework/GamerServices/Gamer.hpp"
#include "Microsoft/Xna/Framework/GamerServices/SignedInGamer.hpp"
#include "Microsoft/Xna/Framework/GamerServices/SignedInGamerCollection.hpp"
#include "Microsoft/Xna/Framework/Net/LocalNetworkGamer.hpp"
#include "Microsoft/Xna/Framework/Net/NetworkSession.hpp"
#include "System/ArgumentException.hpp"
#include "System/ArgumentNullException.hpp"
#include "System/ArgumentOutOfRangeException.hpp"
#include "System/InvalidOperationException.hpp"
#include "System/ObjectDisposedException.hpp"
#include <atomic>
#include <chrono>
#include <future>
#include <thread>

using namespace Microsoft::Xna::Framework::Net;
using Microsoft::Xna::Framework::GamerServices::Gamer;
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

    NetworkSessionProperties& mutableProperties = session->getSessionPropertiesProperty();
    mutableProperties[0] = 7;
    mutableProperties[1] = std::nullopt;
    mutableProperties[2] = -9;

    const NetworkSession& constSession = *session;
    const NetworkSessionProperties& observed = constSession.getSessionPropertiesProperty();
    ASSERT_EQ(observed.getCountProperty(), 3);
    EXPECT_EQ(observed.getItem(0), 7);
    EXPECT_EQ(observed.getItem(1), std::nullopt);
    EXPECT_EQ(observed.getItem(2), -9);

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

    EXPECT_EQ(NetworkSession::GetActiveActionInstanceCountForTesting(), before);

    session->Dispose();
}

// audit_net.md High finding: BeginCreate/BeginFind/BeginJoin/BeginJoinInvited stored their
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

    EXPECT_EQ(callCount, 1);
    EXPECT_EQ(observedResult, result);
    ASSERT_TRUE(result->getIsCompletedProperty());
    EXPECT_EQ(std::any_cast<int>(result->getAsyncStateProperty()), 42);

    NetworkSession* session = NetworkSession::EndCreate(result);
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

    NetworkSession* completedSession = nullptr;
    NetworkSession::BeginCreate(
        NetworkSessionType::Local, 1, 8,
        [&completedSession](System::IAsyncResult& ar) {
            completedSession = NetworkSession::EndCreate(&ar);
        },
        std::any{}
    );

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

    session->setSimulatedLatencyProperty(System::TimeSpan::FromMilliseconds(50));
    EXPECT_EQ(session->getSimulatedLatencyProperty(), System::TimeSpan::FromMilliseconds(50));
    session->setSimulatedPacketLossProperty(0.25f);
    EXPECT_FLOAT_EQ(session->getSimulatedPacketLossProperty(), 0.25f);

    session->Dispose();
}

TEST(NetworkSessionTest, IsEveryoneReadyReflectsLocalGamers) {
    auto gamer = MakeSignedInGamer();
    NetworkSession* session = NetworkSession::Create(
        NetworkSessionType::Local, std::vector<SignedInGamer*>{&gamer}, 8, 0, NetworkSessionProperties{}
    );

    EXPECT_FALSE(session->getIsEveryoneReadyProperty());
    session->getLocalGamersProperty()[0]->setIsReadyProperty(true);
    EXPECT_TRUE(session->getIsEveryoneReadyProperty());

    session->Dispose();
}

TEST(NetworkSessionTest, ResetReadySetsAllGamersNotReady) {
    auto gamer = MakeSignedInGamer();
    NetworkSession* session = NetworkSession::Create(
        NetworkSessionType::Local, std::vector<SignedInGamer*>{&gamer}, 8, 0, NetworkSessionProperties{}
    );

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

    session->StartGame();
    session->Update();
    EXPECT_EQ(session->getSessionStateProperty(), NetworkSessionState::Playing);

    session->EndGame();
    session->Update();
    EXPECT_EQ(session->getSessionStateProperty(), NetworkSessionState::Lobby);

    session->Dispose();
}

// Task 5.19: WriteArbitratedLeaderboard/WriteUnarbitratedLeaderboard/WriteTrueSkill had zero test
// coverage, not even a subscribe-smoke-test - even though they're correctly never raised (matching
// FNA, where leaderboards/TrueSkill are unimplemented upstream). Subscribing to all three and
// exercising a full Create -> StartGame -> EndGame -> Dispose lifecycle both proves each event
// exists under its exact FNA name/spelling (a rename/typo here would fail to compile) and locks in
// that none of them ever actually fires.
TEST(NetworkSessionTest, WriteLeaderboardAndTrueSkillEventsAreNeverRaised) {
    auto gamer = MakeSignedInGamer();
    NetworkSession* session = NetworkSession::Create(
        NetworkSessionType::Local, std::vector<SignedInGamer*>{&gamer}, 8, 0, NetworkSessionProperties{}
    );

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

    EXPECT_THROW(session->EndGame(), System::InvalidOperationException);

    session->Dispose();
}

TEST(NetworkSessionTest, AddLocalGamerThrowsAtMaxLimit) {
    auto gamer = MakeSignedInGamer();
    auto second = MakeSignedInGamer("tag2");
    NetworkSession* session = NetworkSession::Create(
        NetworkSessionType::Local, std::vector<SignedInGamer*>{&gamer}, 8, 0, NetworkSessionProperties{}
    );

    // maxLocalGamers_ tracks the count passed to the explicit-list constructor overload (1
    // here), so the very next AddLocalGamer call is already at the limit.
    EXPECT_THROW(session->AddLocalGamer(&second), System::InvalidOperationException);

    session->Dispose();
}

TEST(NetworkSessionTest, FindGamerByIdMatchesSoleLocalGamer) {
    auto gamer = MakeSignedInGamer();
    NetworkSession* session = NetworkSession::Create(
        NetworkSessionType::Local, std::vector<SignedInGamer*>{&gamer}, 8, 0, NetworkSessionProperties{}
    );

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

    EXPECT_TRUE(session->getLocalGamersProperty()[0]->getIsHostProperty());
    EXPECT_TRUE(session->getIsHostProperty());

    session->Dispose();
}

TEST(NetworkSessionTest, JoinInvitedMakesLocalGamersReportIsHostFalse) {
    auto gamer = MakeSignedInGamer();
    NetworkSession* session = NetworkSession::JoinInvited(std::vector<SignedInGamer*>{&gamer});

    EXPECT_FALSE(session->getLocalGamersProperty()[0]->getIsHostProperty());
    EXPECT_FALSE(session->getIsHostProperty());

    session->Dispose();
}

// NOTE: the explicit-local-gamers Create()/JoinInvited() overloads always set maxLocalGamers_ to
// the passed list's size (zero spare capacity — see AddLocalGamerThrowsAtMaxLimit's own comment
// above), and the maxLocalGamers-only overload falls back to the global Gamer::SignedInGamers,
// which defaults to empty in this test binary. An empty list makes the constructor's
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

    EXPECT_THROW(NetworkSession::EndCreate(result), std::exception);

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
    NetworkSession* session = NetworkSession::EndCreate(recovery);
    ASSERT_NE(session, nullptr);
    session->Dispose();
}

// Task 2.3: AddLocalGamer only did localGamers_.Add(adding); allGamers_.Add(adding); with no event
// enqueue at all — unlike AddRemoteGamer just below, which explicitly enqueues a GamerJoin event.
// A handler already subscribed before AddLocalGamer ran never learned about the newly-added local
// gamer (no replay, no queued event).
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
    NetworkSession* session = NetworkSession::Create(NetworkSessionType::Local, 2, 8);
    ASSERT_EQ(session->getLocalGamersProperty().getCountProperty(), 1);

    auto newGamer = MakeSignedInGamer("NewLocalPlayer");
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
    EXPECT_EQ(session->getLocalGamersProperty().getCountProperty(), 2);

    session->Update();
    EXPECT_EQ(joinCount, 1);
    ASSERT_NE(joinedGamer, nullptr);
    EXPECT_EQ(joinedGamer, session->getLocalGamersProperty()[1]);

    session->Dispose();
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

    NetworkSession* session = NetworkSession::Create(NetworkSessionType::Local, 3, 8);
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
    auto newSignedIn = MakeSignedInGamer("D");
    session->AddLocalGamer(&newSignedIn);
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

    session->Dispose();
    EXPECT_THROW(session->Update(), System::ObjectDisposedException);
}

TEST(NetworkSessionTest, ResetReadyAfterDisposeThrows) {
    auto gamer = MakeSignedInGamer();
    NetworkSession* session = NetworkSession::Create(
        NetworkSessionType::Local, std::vector<SignedInGamer*>{&gamer}, 8, 0, NetworkSessionProperties{}
    );

    session->Dispose();
    EXPECT_THROW(session->ResetReady(), System::ObjectDisposedException);
}

TEST(NetworkSessionTest, StartAndEndGameAfterDisposeThrow) {
    auto gamer = MakeSignedInGamer();
    NetworkSession* session = NetworkSession::Create(
        NetworkSessionType::Local, std::vector<SignedInGamer*>{&gamer}, 8, 0, NetworkSessionProperties{}
    );

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

TEST(NetworkSessionTest, BeginCreateOverloadsValidateMaxGamers) {
    auto gamer = MakeSignedInGamer();

    EXPECT_THROW(
        NetworkSession::BeginCreate(
            NetworkSessionType::Local, 1, 1, System::AsyncCallback{}, std::any{}),
        System::ArgumentOutOfRangeException
    );
    EXPECT_THROW(
        NetworkSession::BeginCreate(
            NetworkSessionType::Local, 1, 32, 0, NetworkSessionProperties{},
            System::AsyncCallback{}, std::any{}),
        System::ArgumentOutOfRangeException
    );
    EXPECT_THROW(
        NetworkSession::BeginCreate(
            NetworkSessionType::Local, std::vector<SignedInGamer*>{&gamer}, 1, 0,
            NetworkSessionProperties{}, System::AsyncCallback{}, std::any{}),
        System::ArgumentOutOfRangeException
    );
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

    EXPECT_THROW(
        NetworkSession::BeginCreate(NetworkSessionType::Local, 1, 4, System::AsyncCallback{}, std::any{}),
        System::InvalidOperationException
    );

    NetworkSession* session = NetworkSession::EndCreate(result);
    session->Dispose();
}

TEST(NetworkSessionTest, EndCreateWithMismatchedResultThrows) {
    auto gamer = MakeSignedInGamer();
    System::IAsyncResult* result = NetworkSession::BeginCreate(
        NetworkSessionType::Local, std::vector<SignedInGamer*>{&gamer}, 8, 0,
        NetworkSessionProperties{}, System::AsyncCallback{}, std::any{}
    );

    auto* bogus = reinterpret_cast<System::IAsyncResult*>(0x1);
    EXPECT_THROW(NetworkSession::EndCreate(bogus), System::ArgumentException);

    NetworkSession* session = NetworkSession::EndCreate(result);
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

    auto* bogus = reinterpret_cast<System::IAsyncResult*>(0x1);
    EXPECT_THROW(NetworkSession::EndFind(bogus), System::ArgumentException);

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
// synthetic PlayerMatch entry is descriptive only: CNA has no PlayerMatch transport, so Join must
// not interpret that address as a SystemLink endpoint and wait for a ServerWelcome that can never
// arrive. The SAMPLE-091 handshake wait originally missed this session-type gate.
TEST(NetworkSessionTest, JoinDoesNotActivateTransportForSyntheticPlayerMatchSession) {
    SignedInGamer joiningGamer = MakeSignedInGamer("PlayerMatchJoiner");
    Gamer::setSignedInGamersProperty(new SignedInGamerCollection(
        SignedInGamerCollection::CreateInternal({&joiningGamer})
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
        NetworkSessionType::PlayerMatch
    );

    NetworkSession* joined = nullptr;
    ASSERT_NO_THROW(joined = NetworkSession::Join(&availableSession));
    ASSERT_NE(joined, nullptr);
    EXPECT_EQ(joined->getSessionTypeProperty(), NetworkSessionType::PlayerMatch);
    EXPECT_FALSE(joined->getIsHostProperty());
    EXPECT_EQ(joined->getHostProperty(), joined->getLocalGamersProperty()[0]);
    EXPECT_EQ(CNA::Internal::Net::ENetBackend::GetBoundPort(joined), 0);

    joined->Dispose();
    delete joined;
}

// --- Static JoinInvited/BeginJoinInvited/EndJoinInvited family ---

TEST(NetworkSessionTest, BeginJoinInvitedValidatesMaxLocalGamers) {
    EXPECT_THROW(
        NetworkSession::BeginJoinInvited(0, System::AsyncCallback{}, std::any{}),
        System::ArgumentOutOfRangeException
    );
}

TEST(NetworkSessionTest, JoinInvitedWithExplicitLocalGamersSucceeds) {
    auto gamer = MakeSignedInGamer();
    // Unlike Join(AvailableNetworkSession*) and JoinInvited(int), this overload's
    // BeginJoinInvited passes the caller's own gamer list straight through as
    // NetworkSessionAction::LocalGamers (never std::nullopt), so EndJoinInvited's constructor
    // call uses that real, non-empty list — it never touches the empty global
    // Gamer::SignedInGamers and is safe to complete.
    NetworkSession* session = NetworkSession::JoinInvited(std::vector<SignedInGamer*>{&gamer});
    EXPECT_EQ(session->getSessionTypeProperty(), NetworkSessionType::PlayerMatch);
    session->Dispose();
}

// NOTE: JoinInvited(int) is not exercised here for the same reason Create(sessionType,
// maxLocalGamers, maxGamers) and Join(AvailableNetworkSession*) aren't — its BeginJoinInvited
// overload always records a std::nullopt LocalGamers list, so completing it via
// EndJoinInvited always hits the empty-global-SignedInGamers constructor throw, which would
// permanently strand activeAction_. Only the argument-validation path above is safe.

TEST(NetworkSessionTest, EndJoinInvitedWithMismatchedResultThrows) {
    auto gamer = MakeSignedInGamer();
    System::IAsyncResult* result = NetworkSession::BeginJoinInvited(
        std::vector<SignedInGamer*>{&gamer}, System::AsyncCallback{}, std::any{}
    );

    auto* bogus = reinterpret_cast<System::IAsyncResult*>(0x1);
    EXPECT_THROW(NetworkSession::EndJoinInvited(bogus), System::ArgumentException);

    NetworkSession* session = NetworkSession::EndJoinInvited(result);
    session->Dispose();
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

    EXPECT_EQ(CNA::Internal::Net::ENetBackend::GetBoundPort(session), 0);

    session->Dispose();
}

TEST(NetworkSessionTest, SystemLinkSessionGetsARealBoundPort) {
    auto gamer = MakeSignedInGamer();
    NetworkSession* session = NetworkSession::Create(
        NetworkSessionType::SystemLink, std::vector<SignedInGamer*>{&gamer}, 8, 0, NetworkSessionProperties{}
    );

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

    // Subscribing here replays once for the local gamer that already joined during
    // construction (Task 12.3) - tracked separately from the remote join below, which is a
    // real, queued, Update()-driven event (AddRemoteGamer is unaffected by the replay hook,
    // which only fires for gamers already present at *subscription* time).
    std::vector<std::string> joinedGamertags;
    session->GamerJoined += [&joinedGamertags](System::Object*, const GamerJoinedEventArgs& e) {
        joinedGamertags.push_back(e.getGamerProperty()->getGamertagProperty());
    };
    ASSERT_EQ(joinedGamertags.size(), 1u);
    EXPECT_EQ(joinedGamertags[0], "Stub Gamer"); // LocalNetworkGamer always reports this gamertag

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
    ASSERT_EQ(session->getAllGamersProperty().getCountProperty(), 1);
    // This setter deliberately exercises the full-session branch with the host already occupying
    // the only slot; XNA creation itself requires a maximum between 2 and 31.
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
    session->Update();

    NetworkGamer remote = NetworkGamer::CreateInternal(session, "RemotePlayer");
    session->AddRemoteGamer(&remote);
    session->Update();

    int leftCount = 0;
    // Task 2.1: sender must be the raising NetworkSession itself, not nullptr.
    System::Object* observedSender = nullptr;
    session->GamerLeft += [&leftCount, &observedSender](System::Object* sender, const GamerLeftEventArgs&) {
        ++leftCount;
        observedSender = sender;
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

    session->Dispose();
}

TEST(NetworkSessionTest, RemoveGamerOnLocalGamerRaisesSessionEndedWithReason) {
    auto gamer = MakeSignedInGamer();
    NetworkSession* session = NetworkSession::Create(
        NetworkSessionType::Local, std::vector<SignedInGamer*>{&gamer}, 8, 0, NetworkSessionProperties{}
    );
    session->Update();

    int endedCount = 0;
    NetworkSessionEndReason observedReason = NetworkSessionEndReason::ClientSignedOut;
    session->SessionEnded += [&](System::Object*, const NetworkSessionEndedEventArgs& e) {
        ++endedCount;
        observedReason = e.getEndReasonProperty();
    };

    LocalNetworkGamer* localGamer = session->getLocalGamersProperty()[0];
    session->RemoveGamer(localGamer, NetworkSessionEndReason::HostEndedSession);
    session->Update();

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
    NetworkSession* session = NetworkSession::Create(NetworkSessionType::Local, 2, 8);
    ASSERT_EQ(session->getLocalGamersProperty().getCountProperty(), 1);
    EXPECT_EQ(session->GetOwnedGamerCountForTesting(), 1u);

    auto newGamer = MakeSignedInGamer("NewLocalPlayer");
    session->AddLocalGamer(&newGamer);
    EXPECT_EQ(session->GetOwnedGamerCountForTesting(), 2u);

    session->Dispose();
    EXPECT_EQ(session->GetOwnedGamerCountForTesting(), 0u);
}

// audit_net.md Critical finding 1 / Task 12.1: Dispose() itself was not idempotent - a second
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

// audit_net.md Critical finding 1: "After only one disposal, callers can also obtain a collection
// containing dangling gamer pointers, so the problem is not confined to a second call." Confirms
// the defense-in-depth fix: PreviousGamers (populated by RemoveGamer, which moves a departing
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

    NetworkSession* session = NetworkSession::Create(NetworkSessionType::Local, 2, 8);
    ASSERT_EQ(session->getLocalGamersProperty().getCountProperty(), 1);

    auto newGamer = MakeSignedInGamer("NewLocalPlayer");
    session->AddLocalGamer(&newGamer);
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

// audit_net.md Suggested fix: "invalidate/clear ... host_ before owned gamers are destroyed."
TEST(NetworkSessionTest, DisposeClearsHostProperty) {
    auto gamer = MakeSignedInGamer();
    NetworkSession* session = NetworkSession::Create(
        NetworkSessionType::Local, std::vector<SignedInGamer*>{&gamer}, 8, 0, NetworkSessionProperties{}
    );
    ASSERT_NE(session->getHostProperty(), nullptr);

    session->Dispose();
    EXPECT_EQ(session->getHostProperty(), nullptr);
}
