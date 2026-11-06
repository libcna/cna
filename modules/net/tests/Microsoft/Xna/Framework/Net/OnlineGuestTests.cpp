// SPDX-License-Identifier: MS-PL
#include <gtest/gtest.h>
#include "CNA/Internal/GamerServices/IGamerServicesBackend.hpp"
#include "CNA/Internal/GamerServices/ServiceInvitations.hpp"
#include "Microsoft/Xna/Framework/GamerServices/LeaderboardWriter.hpp"
#include "Microsoft/Xna/Framework/GamerServices/LeaderboardIdentity.hpp"
#include "Microsoft/Xna/Framework/GamerServices/Gamer.hpp"
#include "Microsoft/Xna/Framework/GamerServices/GamerServicesDispatcher.hpp"
#include "Microsoft/Xna/Framework/GamerServices/GamerPrivilegeException.hpp"
#include "Microsoft/Xna/Framework/GamerServices/GamerPrivileges.hpp"
#include "Microsoft/Xna/Framework/GamerServices/SignedInGamer.hpp"
#include "Microsoft/Xna/Framework/GamerServices/SignedInGamerCollection.hpp"
#include "Microsoft/Xna/Framework/Net/NetworkSession.hpp"
#include "Microsoft/Xna/Framework/Net/LocalNetworkGamer.hpp"
#include "Microsoft/Xna/Framework/Net/WriteLeaderboardsEventArgs.hpp"
#include "Microsoft/Xna/Framework/GamerServices/LeaderboardEntry.hpp"
#include "System/IServiceProvider.hpp"
#include <memory>
#include <utility>
namespace {
namespace S=CNA::Internal::GamerServices;
using namespace Microsoft::Xna::Framework::GamerServices;
using namespace Microsoft::Xna::Framework::Net;
using Microsoft::Xna::Framework::PlayerIndex;
struct Provider final:System::IServiceProvider {void* GetService(const std::type_info&)const override{return nullptr;}};
class OnlineGuestTest:public ::testing::Test {
protected:
    void SetUp()override {
        previous=S::backend();
        S::ServiceIdentity alice;alice.userId="alice";alice.gamertag="Alice";alice.allowOnlineSessions=true;
        S::ServiceIdentity bob;bob.userId="bob";bob.gamertag="Bob";bob.allowOnlineSessions=true;
        service=S::makeFakeBackend({alice,bob},{},{S::ServiceLeaderboardFixture{"GuestBoundary",0,false,false,{}}});S::setFakeOnlineGuests(*service,true);S::setBackendForTesting(service);
        if(!GamerServicesDispatcher::getIsInitializedProperty())GamerServicesDispatcher::Initialize(provider);
        service->signIn(0,"Alice","fixture");GamerServicesDispatcher::Update();
        service->signInGuest(1,"Alice (1)",0);GamerServicesDispatcher::Update();
    }
    void TearDown()override {
        for(int slot=0;slot<4;++slot)service->signOut(slot);
        GamerServicesDispatcher::Update();S::setBackendForTesting(previous);
    }
    SignedInGamer* gamer(PlayerIndex slot){return (*Gamer::getSignedInGamersProperty())[slot];}
    std::shared_ptr<S::IGamerServicesBackend> previous,service;Provider provider;
};
}
TEST_F(OnlineGuestTest, ServiceBackedGuestHasOnlineButNoIndependentPrivileges) {
    // Oracle: WIRE identity/privilege policy; XS IsGuest IL_0000-0006 is the stored guest flag.
    auto* parent=gamer(PlayerIndex::One);auto* guest=gamer(PlayerIndex::Two);
    ASSERT_NE(nullptr,parent);ASSERT_NE(nullptr,guest);
    EXPECT_FALSE(parent->getIsGuestProperty());EXPECT_TRUE(guest->getIsGuestProperty());
    EXPECT_TRUE(guest->getIsSignedInToLiveProperty());EXPECT_EQ("Alice (1)",guest->getGamertagProperty());
    EXPECT_TRUE(guest->getPrivilegesProperty().getAllowOnlineSessionsProperty());
    EXPECT_FALSE(guest->getPrivilegesProperty().getAllowPurchaseContentProperty());
    EXPECT_FALSE(guest->getPrivilegesProperty().getAllowTradeContentProperty());
    EXPECT_FALSE(guest->getPrivilegesProperty().getAllowPremiumContentProperty());
    EXPECT_THROW(guest->AwardAchievement("first"),GamerPrivilegeException);
}
TEST_F(OnlineGuestTest, ExplicitAndImplicitSearchCountTheGuestCapacity) {
    // Oracle: parent+guest = two requested slots; remote has 4-1=3 public slots.
    // The remote account is authorized by the fake service, not published as another local player.
    service->signIn(3,"Bob","fixture");(void)service->pump();
    S::ServiceSessionSettings settings;settings.maxGamers=4;
    for(const auto kind:{S::ServiceSessionKind::PlayerMatch,S::ServiceSessionKind::Ranked}) {
        const auto remote=service->sessionDirectory().create("bob",{"bob"},kind,settings);
        const auto type=kind==S::ServiceSessionKind::Ranked?NetworkSessionType::Ranked:NetworkSessionType::PlayerMatch;
        auto explicitResult=NetworkSession::Find(type,std::vector<SignedInGamer*>{gamer(PlayerIndex::One),gamer(PlayerIndex::Two)},{});
        ASSERT_EQ(1,explicitResult.getCountProperty());
        EXPECT_EQ(1,std::as_const(explicitResult)[0].getCurrentGamerCountProperty());EXPECT_EQ(3,std::as_const(explicitResult)[0].getOpenPublicGamerSlotsProperty());
        EXPECT_EQ(1,NetworkSession::Find(type,2,{}).getCountProperty());
        // Find reserves the caller-requested four slots; only three public slots exist.
        EXPECT_EQ(0,NetworkSession::Find(type,4,{}).getCountProperty());
        service->sessionDirectory().leave("bob",remote.session);
        settings.maxGamers=2;const auto tight=service->sessionDirectory().create("bob",{"bob"},kind,settings);
        EXPECT_EQ(0,NetworkSession::Find(type,2,{}).getCountProperty());
        EXPECT_EQ(0,NetworkSession::Find(type,std::vector<SignedInGamer*>{gamer(PlayerIndex::One),gamer(PlayerIndex::Two)},{}).getCountProperty());
        service->sessionDirectory().leave("bob",tight.session);settings.maxGamers=4;
    }
}
TEST_F(OnlineGuestTest, GuestCannotSearchWithoutItsLocalParent) {
    // Oracle: WIRE complete-machine policy, independent of managed XNA collection validation.
    EXPECT_THROW((void)NetworkSession::Find(NetworkSessionType::PlayerMatch,
        std::vector<SignedInGamer*>{gamer(PlayerIndex::Two)},{}),GamerPrivilegeException);
    EXPECT_EQ(0,NetworkSession::Find(NetworkSessionType::PlayerMatch,1,{}).getCountProperty());
}
TEST_F(OnlineGuestTest, ParentSignOutDisposesBothBeforeLaterSlotReuse) {
    // Oracle: XS HandlePlayerSignInChanged IL_0022-0034 disposes before SignedOut;
    // sibling cascade is CNA parent-lifetime policy, not inferred from that IL.
    auto* parent=gamer(PlayerIndex::One);auto* guest=gamer(PlayerIndex::Two);
    ASSERT_NE(nullptr,parent);ASSERT_NE(nullptr,guest);
    service->signOut(0);GamerServicesDispatcher::Update();
    EXPECT_TRUE(parent->getIsDisposedProperty());EXPECT_TRUE(guest->getIsDisposedProperty());
    EXPECT_EQ(nullptr,gamer(PlayerIndex::One));EXPECT_EQ(nullptr,gamer(PlayerIndex::Two));
    service->signIn(0,"Bob","fixture");GamerServicesDispatcher::Update();
    ASSERT_NE(nullptr,gamer(PlayerIndex::One));EXPECT_EQ("Bob",gamer(PlayerIndex::One)->getGamertagProperty());
    EXPECT_EQ(nullptr,gamer(PlayerIndex::Two));
}
TEST_F(OnlineGuestTest, PendingGuestCannotAttachAfterParentSlotIsReused) {
    // Oracle: WIRE family + parent-slot generation, not merely the numeric host slot.
    service->signOutGuest(1);GamerServicesDispatcher::Update();
    service->signInGuest(1,"Alice (1)",0); // Guest authority request not yet published.
    service->signOut(0);service->signIn(0,"Bob","fixture");
    GamerServicesDispatcher::Update();GamerServicesDispatcher::Update();
    ASSERT_NE(nullptr,gamer(PlayerIndex::One));EXPECT_EQ("Bob",gamer(PlayerIndex::One)->getGamertagProperty());
    EXPECT_EQ(nullptr,gamer(PlayerIndex::Two));EXPECT_EQ(1,Gamer::getSignedInGamersProperty()->getCountProperty());
}

TEST_F(OnlineGuestTest, ServiceGuestWriterCannotFallBackToLocalProgress) {
    // Owner accounting policy: reject before constructing a draft, outside gameplay too.
    // Windows Refresh GetLeaderboard IL_0000-000a is a platform stub, not an Xbox oracle.
    auto* guest=gamer(PlayerIndex::Two);ASSERT_NE(nullptr,guest);
    EXPECT_TRUE(S::GamerAccess::userId(*guest).starts_with("g:"));
    LeaderboardIdentity board;board.setKeyProperty("GuestDenial");board.setGameModeProperty(0);
    EXPECT_THROW((void)guest->getLeaderboardWriterProperty().GetLeaderboard(board),GamerPrivilegeException);
}
TEST_F(OnlineGuestTest, ImplicitSearchSelectsAccountBeforeLowerSlotGuest) {
    // A numeric slot does not confer machine ownership. The parent is player Three here.
    service->signOut(0);GamerServicesDispatcher::Update();
    service->signIn(2,"Bob","fixture");GamerServicesDispatcher::Update();
    service->signInGuest(0,"Bob (1)",2);GamerServicesDispatcher::Update();
    EXPECT_NO_THROW((void)NetworkSession::Find(NetworkSessionType::PlayerMatch,1,{}));
}

TEST_F(OnlineGuestTest, CredentialedGuestCannotPolluteALocalWithLeaderboardsAccountEpoch) {
    // Owner no-progress policy follows the service guest identity across session kinds.
    // Uncredentialed legacy guests keep their existing local-only behavior and tests.
    std::unique_ptr<NetworkSession> local(NetworkSession::Create(NetworkSessionType::LocalWithLeaderboards,
        std::vector<SignedInGamer*>{gamer(PlayerIndex::One),gamer(PlayerIndex::Two)},4,0,{}));
    LeaderboardIdentity board;board.setKeyProperty("GuestBoundary");
    int unarbitrated=0,skill=0;
    local->WriteUnarbitratedLeaderboard.Add([&](auto*,const WriteLeaderboardsEventArgs& args){++unarbitrated;EXPECT_FALSE(args.getGamerProperty()->getIsGuestProperty());});
    local->WriteTrueSkill.Add([&](auto*,const WriteLeaderboardsEventArgs& args){++skill;EXPECT_FALSE(args.getGamerProperty()->getIsGuestProperty());});
    local->StartGame();local->Update();ASSERT_EQ(NetworkSessionState::Playing,local->getSessionStateProperty());
    auto* account=local->getLocalGamersProperty()[0];auto* guest=local->getLocalGamersProperty()[1];
    EXPECT_THROW((void)guest->getLeaderboardWriterProperty().GetLeaderboard(board),GamerPrivilegeException);
    account->getLeaderboardWriterProperty().GetLeaderboard(board)->setRatingProperty(17);
    local->EndGame();local->Update();EXPECT_EQ(NetworkSessionState::Lobby,local->getSessionStateProperty());
    EXPECT_EQ(1,unarbitrated);EXPECT_EQ(1,skill);
    const auto rows=service->readLeaderboard("GuestBoundary",0,0,10,"",{});
    ASSERT_EQ(1U,rows.entries.size());EXPECT_EQ("alice",rows.entries[0].userId);EXPECT_EQ(17,rows.entries[0].rating);
    local->Dispose();
}
