// SPDX-License-Identifier: MS-PL
#include <gtest/gtest.h>
#include "CNA/Internal/GamerServices/IGamerServicesBackend.hpp"
#include "CNA/Internal/GamerServices/ServiceInvitations.hpp"
#include "Microsoft/Xna/Framework/GamerServices/Gamer.hpp"
#include "Microsoft/Xna/Framework/GamerServices/GamerServicesDispatcher.hpp"
#include "Microsoft/Xna/Framework/GamerServices/Guide.hpp"
#include "Microsoft/Xna/Framework/GamerServices/GuideAlreadyVisibleException.hpp"
#include "Microsoft/Xna/Framework/GamerServices/GamerPrivilegeException.hpp"
#include "Microsoft/Xna/Framework/GamerServices/SignedInGamer.hpp"
#include "Microsoft/Xna/Framework/GamerServices/SignedInGamerCollection.hpp"
#include "Microsoft/Xna/Framework/Input/TextInputEXT.hpp"
#include "System/IServiceProvider.hpp"
#include <array>

namespace Service=CNA::Internal::GamerServices;
using namespace Microsoft::Xna::Framework::GamerServices;
using Microsoft::Xna::Framework::PlayerIndex;
namespace {
struct Provider final : System::IServiceProvider {void* GetService(const std::type_info&) const override {return nullptr;}};
void type(const std::string& value) {
    for(unsigned char character:value)Microsoft::Xna::Framework::Input::TextInputEXT::INTERNAL_OnTextInput(character);
    Microsoft::Xna::Framework::Input::TextInputEXT::INTERNAL_OnTextInput(u'\r');
}
class GuideSocialPaneTest : public ::testing::Test {
protected:
    void SetUp() override {
        previous_=Service::backend();
        std::vector<Service::ServiceIdentity> identities;
        for(auto [id,tag]:{std::pair{"a","Alice"},{"b","Bob"}}) {
            Service::ServiceIdentity identity;identity.userId=id;identity.gamertag=tag;identity.allowOnlineSessions=true;identities.push_back(identity);
        }
        Service::ServiceAchievement achievement;achievement.key="first";achievement.name="First steps";
        service=Service::makeFakeBackend(std::move(identities),{achievement});Service::setBackendForTesting(service);
        Service::resetInvitationsForTesting();
        if(!GamerServicesDispatcher::getIsInitializedProperty())GamerServicesDispatcher::Initialize(provider_);
        service->signIn(0,"Alice","fixture");service->signIn(1,"Bob","fixture");GamerServicesDispatcher::Update();
        ASSERT_EQ(2,Gamer::getSignedInGamersProperty()->getCountProperty());
    }
    void TearDown() override {
        Guide::ResetPendingMessageBoxForTestingEXT();Guide::ResetPendingKeyboardInputForTestingEXT();
        service->signOut(0);service->signOut(1);GamerServicesDispatcher::Update();
        Service::resetInvitationsForTesting();Service::setBackendForTesting(previous_);
    }
    SignedInGamer* gamer(int slot){return (*Gamer::getSignedInGamersProperty())[slot];}
    void settle(){for(int index=0;index<5;++index)GamerServicesDispatcher::Update();}
    Provider provider_;
    std::shared_ptr<Service::IGamerServicesBackend> previous_,service;
};
}

TEST_F(GuideSocialPaneTest, ComposeSendsAndTheRecipientReadsRepliesAndDeletes) {
    Guide::ShowComposeMessage(PlayerIndex::One,"good game",{gamer(1)});
    ASSERT_TRUE(Guide::getHasPendingKeyboardInputEXTProperty());
    EXPECT_THROW(Guide::ShowComposeMessage(PlayerIndex::One,"again",{}),GuideAlreadyVisibleException);
    type("");settle();
    auto inbox=service->messages("b",0,10);
    ASSERT_EQ(1u,inbox.messages.size());EXPECT_EQ("Alice",inbox.messages[0].sender);EXPECT_EQ("good game",inbox.messages[0].text);
    EXPECT_EQ(1,inbox.unread);
    Guide::ShowMessages(PlayerIndex::Two);ASSERT_TRUE(Guide::getHasPendingMessageBoxEXTProperty());
    EXPECT_EQ(0,service->messages("b",0,10).unread);
    Guide::SimulateMessageBoxClickEXT(1);settle();
    ASSERT_TRUE(Guide::getHasPendingKeyboardInputEXTProperty());type("rematch?");settle();
    ASSERT_EQ(1u,service->messages("a",0,10).messages.size());EXPECT_EQ("rematch?",service->messages("a",0,10).messages[0].text);
    Guide::ShowMessages(PlayerIndex::Two);Guide::SimulateMessageBoxClickEXT(2);settle();
    EXPECT_EQ(0,service->messages("b",0,10).total);
    ASSERT_TRUE(Guide::getHasPendingMessageBoxEXTProperty());Guide::SimulateMessageBoxClickEXT(0);settle();
}

TEST_F(GuideSocialPaneTest, ComposeWithoutRecipientsAsksForAGamertag) {
    Guide::ShowComposeMessage(PlayerIndex::Two,"",{});type("hello there");settle();
    ASSERT_TRUE(Guide::getHasPendingKeyboardInputEXTProperty());type("Alice");settle();
    ASSERT_EQ(1u,service->messages("a",0,10).messages.size());EXPECT_EQ("Bob",service->messages("a",0,10).messages[0].sender);
}

TEST_F(GuideSocialPaneTest, PlayerReviewRecordsPreferAvoidAndClear) {
    Guide::ShowPlayerReview(PlayerIndex::One,gamer(1));Guide::SimulateMessageBoxClickEXT(1);settle();
    Guide::ShowPlayerReview(PlayerIndex::One,gamer(1));Guide::SimulateMessageBoxClickEXT(0);settle();
    // The fixture keeps the latest rating; clearing withdraws it.
    Guide::ShowPlayerReview(PlayerIndex::One,gamer(1));Guide::SimulateMessageBoxClickEXT(2);settle();
    Guide::ShowPlayerReview(PlayerIndex::One,gamer(1));Guide::SimulateMessageBoxClickEXT(3);settle();
    EXPECT_FALSE(Guide::getIsVisibleProperty());
}

TEST_F(GuideSocialPaneTest, UnavailableServicesExplainThemselvesInsteadOfDoingNothing) {
    Guide::ShowMarketplace(PlayerIndex::One);EXPECT_TRUE(Guide::getHasPendingMessageBoxEXTProperty());Guide::SimulateMessageBoxClickEXT(0);
    Guide::ShowParty(PlayerIndex::One);EXPECT_TRUE(Guide::getHasPendingMessageBoxEXTProperty());Guide::SimulateMessageBoxClickEXT(0);
    Guide::ShowPartySessions(PlayerIndex::One);EXPECT_TRUE(Guide::getHasPendingMessageBoxEXTProperty());Guide::SimulateMessageBoxClickEXT(0);
    EXPECT_FALSE(Guide::getIsTrialModeProperty());
    EXPECT_THROW(Guide::ShowMarketplace(PlayerIndex::Three),GamerPrivilegeException);
}

TEST_F(GuideSocialPaneTest, PlayersAndAchievementsPanesListServiceState) {
    Service::rememberRecentPlayer("Bob");Service::rememberRecentPlayer("Carol");Service::rememberRecentPlayer("Bob");
    EXPECT_EQ((std::vector<std::string>{"Bob","Carol"}),Service::recentPlayers());
    Guide::ShowPlayers(PlayerIndex::One);ASSERT_TRUE(Guide::getHasPendingMessageBoxEXTProperty());
    Guide::SimulateMessageBoxClickEXT(0);settle();ASSERT_TRUE(Guide::getHasPendingKeyboardInputEXTProperty());
    type("Bob");settle();EXPECT_TRUE(Guide::getHasPendingMessageBoxEXTProperty());
    Guide::ResetPendingMessageBoxForTestingEXT();
    Guide::ShowAchievementsEXT(PlayerIndex::One);EXPECT_TRUE(Guide::getHasPendingMessageBoxEXTProperty());
}

TEST_F(GuideSocialPaneTest, DelayNotificationsKeepsAnActiveDelayAndCapsIt) {
    Guide::DelayNotifications(System::TimeSpan::FromSeconds(1));
    Guide::DelayNotifications(System::TimeSpan::FromSeconds(500));
    EXPECT_FALSE(Guide::getIsVisibleProperty());
}
