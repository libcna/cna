// SPDX-License-Identifier: MS-PL
#include <gtest/gtest.h>
#include "../../../../../src/Internal/GuideOverlay.hpp"
#include "CNA/Internal/GamerServices/IGamerServicesBackend.hpp"
#include "Microsoft/Xna/Framework/GamerServices/Gamer.hpp"
#include "Microsoft/Xna/Framework/GamerServices/GamerServicesDispatcher.hpp"
#include "Microsoft/Xna/Framework/GamerServices/Guide.hpp"
#include "Microsoft/Xna/Framework/GamerServices/SignedInGamer.hpp"
#include "Microsoft/Xna/Framework/GamerServices/SignedInGamerCollection.hpp"
#include "System/IServiceProvider.hpp"
#include <algorithm>

using namespace Microsoft::Xna::Framework::GamerServices;
namespace Service = CNA::Internal::GamerServices;

namespace {
struct Provider final : System::IServiceProvider {void* GetService(const std::type_info&)const override{return nullptr;}};

class GuideNotificationTest : public ::testing::Test {
protected:
    void SetUp() override {
        Service::setGuideNotificationClockForTesting([this]{return now_;});
        drain();
    }
    void TearDown() override {drain();Service::setGuideNotificationClockForTesting({});}
    void drain() {for(int step=0;step<32&&!Service::guideNotifications().empty();++step)now_+=std::chrono::seconds(5);}
    std::chrono::steady_clock::time_point now_=std::chrono::steady_clock::now();
};
}

TEST_F(GuideNotificationTest, EveryPositionSitsInsideTheTitleSafeArea) {
    const std::pair<NotificationPosition,Microsoft::Xna::Framework::Point> expected[]={
        {NotificationPosition::TopLeft,{64,36}},{NotificationPosition::TopCenter,{540,36}},{NotificationPosition::TopRight,{1016,36}},
        {NotificationPosition::CenterLeft,{64,340}},{NotificationPosition::Center,{540,340}},{NotificationPosition::CenterRight,{1016,340}},
        {NotificationPosition::BottomLeft,{64,644}},{NotificationPosition::BottomCenter,{540,644}},{NotificationPosition::BottomRight,{1016,644}}};
    for(const auto& [position,origin]:expected) {
        const auto actual=Service::guideNotificationOrigin(position,1280,720,200,40);
        EXPECT_EQ(origin.X,actual.X)<<static_cast<int>(position);EXPECT_EQ(origin.Y,actual.Y)<<static_cast<int>(position);
    }
    // XNA's default is bottom center.
    EXPECT_EQ(NotificationPosition::BottomCenter,Guide::getNotificationPositionProperty());
}

TEST_F(GuideNotificationTest, ToastsShowOneAtATimeForTheirDuration) {
    Service::postGuideNotification("first");
    Service::postGuideNotification("second");
    Service::postGuideNotification("");
    EXPECT_EQ((std::vector<std::string>{"first","second"}),Service::guideNotifications());
    now_+=Service::GuideNotificationDuration-std::chrono::milliseconds(1);
    EXPECT_EQ(2u,Service::guideNotifications().size());
    now_+=std::chrono::milliseconds(1);
    EXPECT_EQ((std::vector<std::string>{"second"}),Service::guideNotifications());
    now_+=Service::GuideNotificationDuration;
    EXPECT_TRUE(Service::guideNotifications().empty());
    // A burst keeps the one on screen and the latest behind it.
    Service::postGuideNotification("toast 0");
    ASSERT_EQ(1u,Service::guideNotifications().size());
    for(int index=1;index<12;++index)Service::postGuideNotification("toast "+std::to_string(index));
    const auto queued=Service::guideNotifications();
    ASSERT_EQ(8u,queued.size());EXPECT_EQ("toast 0",queued.front());EXPECT_EQ("toast 11",queued.back());
}

TEST_F(GuideNotificationTest, SigningInAndUnlockingAnAchievementNotify) {
    Provider provider;
    auto previous=Service::backend();
    Service::ServiceIdentity alice;alice.userId="a";alice.gamertag="Alice";
    Service::ServiceAchievement first;first.key="first";first.name="First Steps";
    auto fake=Service::makeFakeBackend({alice},{first});
    Service::setBackendForTesting(fake);
    if(!GamerServicesDispatcher::getIsInitializedProperty())GamerServicesDispatcher::Initialize(provider);
    fake->signIn(0,"Alice","fixture");
    for(int frame=0;frame<20&&Gamer::getSignedInGamersProperty()->getCountProperty()==0;++frame)GamerServicesDispatcher::Update();
    ASSERT_EQ(1,Gamer::getSignedInGamersProperty()->getCountProperty());
    EXPECT_EQ((std::vector<std::string>{"Alice signed in"}),Service::guideNotifications());
    drain();
    auto* gamer=(*Gamer::getSignedInGamersProperty())[0];
    gamer->AwardAchievement("first");
    EXPECT_EQ((std::vector<std::string>{"Achievement unlocked: First Steps"}),Service::guideNotifications());
    drain();
    // Only the first unlock is announced.
    gamer->AwardAchievement("first");
    EXPECT_TRUE(Service::guideNotifications().empty());
    fake->signOut(0);
    for(int frame=0;frame<20&&Gamer::getSignedInGamersProperty()->getCountProperty()==1;++frame)GamerServicesDispatcher::Update();
    EXPECT_EQ((std::vector<std::string>{"Alice signed out"}),Service::guideNotifications());
    Service::setBackendForTesting(previous);
}
