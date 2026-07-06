// SPDX-License-Identifier: MS-PL
#include <gtest/gtest.h>

#include "CNA/Internal/Net/ENetBackend.hpp"
#include "Microsoft/Xna/Framework/GamerServices/SignedInGamer.hpp"
#include "Microsoft/Xna/Framework/Net/LocalNetworkGamer.hpp"
#include "Microsoft/Xna/Framework/Net/NetworkSession.hpp"
#include "System/ArgumentException.hpp"
#include "System/ArgumentNullException.hpp"
#include "System/ArgumentOutOfRangeException.hpp"
#include "System/InvalidOperationException.hpp"
#include "System/ObjectDisposedException.hpp"

using namespace Microsoft::Xna::Framework::Net;
using Microsoft::Xna::Framework::GamerServices::SignedInGamer;

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
    // FNA's EndCreate hardcodes 69 instead of forwarding the caller's maxGamers — preserved as-is.
    EXPECT_EQ(session->getMaxGamersProperty(), 69);
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

// NOTE: AddLocalGamer()'s successful (non-throwing) path is not exercised here, for the same
// reason AddLocalGamerThrowsAtMaxLimit's own comment explains: the explicit-local-gamers Create()/
// JoinInvited() overloads always set maxLocalGamers_ to the passed list's size, so any session
// constructed through them already has zero spare capacity — AddLocalGamer always throws
// immediately. There is no currently-safe way to construct a session with spare local-gamer
// capacity (the maxLocalGamers-only overloads fall back to the empty global Gamer::SignedInGamers
// in this test binary — see this file's own top-of-file note on why that's unsafe to exercise).

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
    session->GamerJoined += [&joinCount](System::Object*, const GamerJoinedEventArgs&) { ++joinCount; };
    EXPECT_EQ(joinCount, 1); // fired synchronously by the += itself, before any Update() call

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
// inside EndCreate — and FNA's EndCreate (faithfully preserved here) sets activeAction = null
// *after* constructing the NetworkSession, so a constructor throw leaves activeAction
// permanently non-null for the rest of the process, with no public API to clear it. Exercising
// that path would permanently break every later test that calls any NetworkSession Begin*
// method. Matches the same "cannot be safely unit-tested" category as
// GamerServicesDispatcher::Initialize(). The identical constructor-throw risk applies to
// Join(AvailableNetworkSession*) and JoinInvited(int) below, for the same reason — both also
// route through a std::nullopt LocalGamers list.

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
// NOTE: Join(...) and EndJoin(...) beyond argument validation are not exercised here.
// BeginJoin's NetworkSessionAction always carries a std::nullopt LocalGamers list (FNA passes
// null unconditionally, marked FIXME upstream — see NetworkSession.cpp), so completing via
// EndJoin always reaches the empty-global-SignedInGamers constructor throw described above the
// Create family tests, and there is no way to call BeginJoin successfully and later reclaim
// activeAction_ without hitting that throw. Only the null-check path below is safe to test.

TEST(NetworkSessionTest, BeginJoinRejectsNullAvailableSession) {
    EXPECT_THROW(
        NetworkSession::BeginJoin(nullptr, System::AsyncCallback{}, std::any{}),
        System::ArgumentNullException
    );
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

namespace {
    struct LocalGamerFixture {
        SignedInGamer signedIn = MakeSignedInGamer();
        NetworkSession* session = NetworkSession::Create(
            NetworkSessionType::Local, std::vector<SignedInGamer*>{&signedIn}, 8, 0, NetworkSessionProperties{}
        );
        LocalNetworkGamer* gamer = session->getLocalGamersProperty()[0];

        ~LocalGamerFixture() { session->Dispose(); }
    };
}

TEST(LocalNetworkGamerTest, IsLocalIsTrue) {
    LocalGamerFixture fixture;
    EXPECT_TRUE(fixture.gamer->getIsLocalProperty());
}

TEST(LocalNetworkGamerTest, SignedInGamerPropertyMatchesConstructorArgument) {
    LocalGamerFixture fixture;
    EXPECT_EQ(fixture.gamer->getSignedInGamerProperty(), &fixture.signedIn);
}

TEST(LocalNetworkGamerTest, NoDataAvailableByDefault) {
    LocalGamerFixture fixture;
    EXPECT_FALSE(fixture.gamer->getIsDataAvailableProperty());
}

TEST(LocalNetworkGamerTest, ReceiveDataReturnsZeroWhenQueueEmpty) {
    LocalGamerFixture fixture;
    std::vector<SharpRuntime::bytecs> buffer(4);
    NetworkGamer* sender = nullptr;
    EXPECT_EQ(fixture.gamer->ReceiveData(buffer, sender), 0);
    EXPECT_EQ(sender, nullptr);
}

TEST(LocalNetworkGamerTest, SendDataThenReceiveDataRoundtrip) {
    LocalGamerFixture fixture;
    std::vector<SharpRuntime::bytecs> payload{1, 2, 3, 4};
    fixture.gamer->SendData(payload, SendDataOptions::Reliable);

    // SendData enqueues a NetworkEvent on the session, not directly on packetQueue_. Update()'s
    // PacketSend handling (Task 5.5) is gated behind ENetBackend::RealNetworkingEnabled(), which
    // is false for this fixture's NetworkSessionType::Local — so it stays a no-op here (matching
    // FNA's own always-empty PacketSend branch) and IsDataAvailable legitimately stays false. See
    // CNA::Internal::Net::ENetBackendTest's AppData relay tests for the real (SystemLink) path.
    fixture.session->Update();
    EXPECT_FALSE(fixture.gamer->getIsDataAvailableProperty());
}

TEST(LocalNetworkGamerTest, SendDataWithOffsetAndCount) {
    LocalGamerFixture fixture;
    std::vector<SharpRuntime::bytecs> payload{1, 2, 3, 4, 5};
    fixture.gamer->SendData(payload, 1, 3, SendDataOptions::None);
}

TEST(LocalNetworkGamerTest, SendDataToRecipient) {
    LocalGamerFixture fixture;
    std::vector<SharpRuntime::bytecs> payload{9, 9};
    fixture.gamer->SendData(payload, SendDataOptions::InOrder, fixture.gamer);
}

TEST(LocalNetworkGamerTest, SendDataWithOffsetAndCountToRecipient) {
    LocalGamerFixture fixture;
    std::vector<SharpRuntime::bytecs> payload{1, 2, 3, 4};
    fixture.gamer->SendData(payload, 0, 2, SendDataOptions::None, fixture.gamer);
}

TEST(LocalNetworkGamerTest, SendDataFromPacketWriter) {
    LocalGamerFixture fixture;
    PacketWriter writer;
    writer.Write(static_cast<int32_t>(42));
    fixture.gamer->SendData(writer, SendDataOptions::Reliable);
}

TEST(LocalNetworkGamerTest, SendDataFromPacketWriterToRecipient) {
    LocalGamerFixture fixture;
    PacketWriter writer;
    writer.Write(static_cast<int32_t>(42));
    fixture.gamer->SendData(writer, SendDataOptions::Reliable, fixture.gamer);
}

TEST(LocalNetworkGamerTest, ReceiveDataIntoPacketReaderReturnsZero) {
    LocalGamerFixture fixture;
    PacketReader reader;
    NetworkGamer* sender = nullptr;
    // No packet queued, so IsDataAvailable is false and this returns 0 immediately.
    EXPECT_EQ(fixture.gamer->ReceiveData(reader, sender), 0);
}

TEST(LocalNetworkGamerTest, EnableSendVoiceAndSendPartyInvitesAreNoOps) {
    LocalGamerFixture fixture;
    fixture.gamer->EnableSendVoice(fixture.gamer, true);
    fixture.gamer->SendPartyInvites();
}

TEST(LocalNetworkGamerTest, ClearPacketQueueLeavesNoDataAvailable) {
    LocalGamerFixture fixture;
    fixture.gamer->ClearPacketQueue();
    EXPECT_FALSE(fixture.gamer->getIsDataAvailableProperty());
}

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
    session->GamerLeft += [&leftCount](System::Object*, const GamerLeftEventArgs&) { ++leftCount; };

    session->RemoveGamer(&remote, NetworkSessionEndReason::Disconnected);
    EXPECT_TRUE(remote.getHasLeftSessionProperty());
    EXPECT_EQ(session->getRemoteGamersProperty().getCountProperty(), 0);
    EXPECT_EQ(session->getAllGamersProperty().getCountProperty(), 1);
    EXPECT_EQ(session->getPreviousGamersProperty().getCountProperty(), 1);
    EXPECT_EQ(session->getPreviousGamersProperty()[0], &remote);

    session->Update();
    EXPECT_EQ(leftCount, 1);

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

    session->RemoveGamer(session->getLocalGamersProperty()[0], NetworkSessionEndReason::HostEndedSession);
    session->Update();

    EXPECT_EQ(endedCount, 1);
    EXPECT_EQ(observedReason, NetworkSessionEndReason::HostEndedSession);
    EXPECT_EQ(session->getSessionStateProperty(), NetworkSessionState::Ended);

    session->Dispose();
}
