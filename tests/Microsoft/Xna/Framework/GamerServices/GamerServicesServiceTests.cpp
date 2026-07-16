// SPDX-License-Identifier: MS-PL
#include <gtest/gtest.h>
#include <any>

#include <SDL3/SDL.h>

#include "System/NotSupportedException.hpp"

#include "Microsoft/Xna/Framework/GamerServices/GamerServicesDispatcher.hpp"
#include "Microsoft/Xna/Framework/GamerServices/Guide.hpp"
#include "Microsoft/Xna/Framework/PlayerIndex.hpp"

// No tests for GamerServicesComponent: like GameComponent (see GameComponentTests.cpp), it
// requires a live Game reference (SDL/graphics backend) to construct.
//
// Task 10.3 investigated whether a lightweight fake-Game/mock-IServiceProvider test double could
// avoid that requirement - confirmed not feasible, and not valuable enough to justify diverging
// from the real XNA API to force it:
//   1. Game's own constructor unconditionally stands up a real GraphicsDevice/backend/window
//      (`Window_.setWindowInternal(GraphicsDevice_.GetBackend().GetWindowInternal());` in
//      Game::Game()) - there is no "lightweight" Game to fake; any Game instance needs a real
//      backend regardless.
//   2. GamerServicesComponent's public constructor signature (`GamerServicesComponent(Game&
//      game)`) must match FNA's real API exactly, so it cannot be changed to accept an injectable
//      fake/interface instead.
//   3. GamerServicesComponent::Initialize()/Update() are two trivial one-line forwards to
//      GamerServicesDispatcher::Initialize()/Update() with no independent branching or state of
//      their own - both targets are already extensively, directly unit-tested elsewhere
//      (GamerServicesDispatcherTest above, GamerServicesDispatcherHangRegressionTest.cpp).
//   4. Both real hang bugs this task's own framing cites (the NetworkSession hang and Task 7.1's
//      GetAchievements hang) were caught by out-of-process harnesses calling
//      GamerServicesDispatcher::Initialize() directly - exactly simulating "a
//      GamerServicesComponent exists" - not by any hypothetical direct component-level test. A
//      GamerServicesComponent unit test would add no coverage beyond what those harnesses already
//      exercise.
// Decision: re-affirm the existing no-direct-tests stance; the forwarding logic has no
// remaining untested risk to catch.
//
// GamerServicesDispatcher::Initialize() is intentionally never called from this suite: it sets
// a process-lifetime static (IsInitialized = true) with no way to reset it, which would change
// the behavior of GamerServicesDispatcher::UpdateAsync() for every other test in this binary
// (e.g. SignedInGamerTest.GetAchievementsReturnsEmptyCollection relies on UpdateAsync() being a
// same-iteration false while uninitialized).

using namespace Microsoft::Xna::Framework::GamerServices;
using Microsoft::Xna::Framework::PlayerIndex;

// --- GamerServicesDispatcher ---

TEST(GamerServicesDispatcherTest, IsInitializedDefaultsFalse) {
    EXPECT_FALSE(GamerServicesDispatcher::getIsInitializedProperty());
}

TEST(GamerServicesDispatcherTest, WindowHandleGetSet) {
    GamerServicesDispatcher::setWindowHandleProperty(0);
    EXPECT_EQ(0u, GamerServicesDispatcher::getWindowHandleProperty());
    GamerServicesDispatcher::setWindowHandleProperty(0x1234);
    EXPECT_EQ(0x1234u, GamerServicesDispatcher::getWindowHandleProperty());
    GamerServicesDispatcher::setWindowHandleProperty(0);
}

TEST(GamerServicesDispatcherTest, InstallingTitleUpdateNeverFiresAutomatically) {
    bool fired = false;
    auto token = GamerServicesDispatcher::InstallingTitleUpdate.Add(
        [&fired](System::Object* /*sender*/, const System::EventArgs& /*e*/) { fired = true; }
    );
    GamerServicesDispatcher::Update();
    GamerServicesDispatcher::UpdateAsync();
    EXPECT_FALSE(fired);
    GamerServicesDispatcher::InstallingTitleUpdate.Remove(token);
}

TEST(GamerServicesDispatcherTest, UpdateDoesNotThrow) {
    EXPECT_NO_THROW(GamerServicesDispatcher::Update());
}

TEST(GamerServicesDispatcherTest, UpdateAsyncReturnsIsInitialized) {
    EXPECT_EQ(GamerServicesDispatcher::getIsInitializedProperty(), GamerServicesDispatcher::UpdateAsync());
}

// --- Guide ---

TEST(GuideTest, IsTrialModeGetSet) {
    Guide::setIsTrialModeProperty(true);
    EXPECT_TRUE(Guide::getIsTrialModeProperty());
    Guide::setIsTrialModeProperty(false);
    EXPECT_FALSE(Guide::getIsTrialModeProperty());
}

TEST(GuideTest, SimulateTrialModeGetSet) {
    Guide::setSimulateTrialModeProperty(true);
    EXPECT_TRUE(Guide::getSimulateTrialModeProperty());
    Guide::setSimulateTrialModeProperty(false);
    EXPECT_FALSE(Guide::getSimulateTrialModeProperty());
}

TEST(GuideTest, IsVisibleAlwaysFalseAndSetterIsNoOp) {
    EXPECT_FALSE(Guide::getIsVisibleProperty());
    Guide::setIsVisibleProperty(true);
    EXPECT_FALSE(Guide::getIsVisibleProperty());
}

TEST(GuideTest, NotificationPositionDefaultAndSet) {
    EXPECT_EQ(NotificationPosition::BottomRight, Guide::getNotificationPositionProperty());
    Guide::setNotificationPositionProperty(NotificationPosition::TopLeft);
    EXPECT_EQ(NotificationPosition::TopLeft, Guide::getNotificationPositionProperty());
    Guide::setNotificationPositionProperty(NotificationPosition::BottomRight);
}

TEST(GuideTest, IsScreenSaverEnabledGetSet) {
    if (!SDL_InitSubSystem(SDL_INIT_VIDEO))
    {
        GTEST_SKIP() << "SDL_InitSubSystem(SDL_INIT_VIDEO) failed: " << SDL_GetError();
    }

    Guide::setIsScreenSaverEnabledProperty(true);
    EXPECT_TRUE(Guide::getIsScreenSaverEnabledProperty());
    Guide::setIsScreenSaverEnabledProperty(false);
    EXPECT_FALSE(Guide::getIsScreenSaverEnabledProperty());

    SDL_QuitSubSystem(SDL_INIT_VIDEO);
}

TEST(GuideTest, BeginEndShowKeyboardInput) {
    System::IAsyncResult* result = Guide::BeginShowKeyboardInput(
        PlayerIndex::One, "title", "description", "default", System::AsyncCallback{}, std::any{}
    );
    ASSERT_NE(nullptr, result);
    EXPECT_TRUE(result->getIsCompletedProperty());
    EXPECT_FALSE(result->getCompletedSynchronouslyProperty());
    EXPECT_EQ("", Guide::EndShowKeyboardInput(result));
    delete result;
}

TEST(GuideTest, BeginEndShowKeyboardInputWithPasswordModeOverload) {
    System::IAsyncResult* result = Guide::BeginShowKeyboardInput(
        PlayerIndex::Two, "title", "description", "default", System::AsyncCallback{}, std::any{}, true
    );
    ASSERT_NE(nullptr, result);
    EXPECT_TRUE(result->getIsCompletedProperty());
    EXPECT_EQ("", Guide::EndShowKeyboardInput(result));
    delete result;
}

// audit_net.md High finding: GuideAction stored its AsyncCallback but never invoked it, despite
// this action already completing synchronously (getIsCompletedProperty() == true) right after
// BeginShowKeyboardInput returns. Confirms the callback now fires exactly once with the correct
// IAsyncResult identity and AsyncState.
TEST(GuideTest, BeginShowKeyboardInputInvokesCallbackExactlyOnceWithCorrectIdentity) {
    int callCount = 0;
    System::IAsyncResult* observedResult = nullptr;
    std::any state = 7;

    System::IAsyncResult* result = Guide::BeginShowKeyboardInput(
        PlayerIndex::One, "title", "description", "default",
        [&callCount, &observedResult](System::IAsyncResult& ar) {
            ++callCount;
            observedResult = &ar;
        },
        state
    );

    EXPECT_EQ(callCount, 1);
    EXPECT_EQ(observedResult, result);
    ASSERT_TRUE(result->getIsCompletedProperty());
    EXPECT_EQ(std::any_cast<int>(result->getAsyncStateProperty()), 7);

    Guide::EndShowKeyboardInput(result);
    delete result;
}

TEST(GuideTest, BeginShowMessageBoxThrows) {
    EXPECT_THROW(
        Guide::BeginShowMessageBox(
            "title", "text", std::vector<std::string>{"OK"}, 0, MessageBoxIcon::None,
            System::AsyncCallback{}, std::any{}
        ),
        System::NotSupportedException
    );
}

TEST(GuideTest, BeginShowMessageBoxPlayerOverloadThrows) {
    EXPECT_THROW(
        Guide::BeginShowMessageBox(
            PlayerIndex::One, "title", "text", std::vector<std::string>{"OK"}, 0, MessageBoxIcon::None,
            System::AsyncCallback{}, std::any{}
        ),
        System::NotSupportedException
    );
}

TEST(GuideTest, EndShowMessageBoxThrows) {
    EXPECT_THROW(Guide::EndShowMessageBox(nullptr), System::NotSupportedException);
}

TEST(GuideTest, DelayNotificationsDoesNotThrow) {
    EXPECT_NO_THROW(Guide::DelayNotifications(System::TimeSpan::FromSeconds(1)));
}

TEST(GuideTest, ShowMethodsDoNotThrow) {
    EXPECT_NO_THROW(Guide::ShowComposeMessage(PlayerIndex::One, "hi", {}));
    EXPECT_NO_THROW(Guide::ShowFriendRequest(PlayerIndex::One, nullptr));
    EXPECT_NO_THROW(Guide::ShowFriends(PlayerIndex::One));
    EXPECT_NO_THROW(Guide::ShowGameInvite(PlayerIndex::One, std::vector<Gamer*>{}));
    EXPECT_NO_THROW(Guide::ShowGameInvite(std::string("session-id")));
    EXPECT_NO_THROW(Guide::ShowGamerCard(PlayerIndex::One, nullptr));
    EXPECT_NO_THROW(Guide::ShowMarketplace(PlayerIndex::One));
    EXPECT_NO_THROW(Guide::ShowMessages(PlayerIndex::One));
    EXPECT_NO_THROW(Guide::ShowParty(PlayerIndex::One));
    EXPECT_NO_THROW(Guide::ShowPartySessions(PlayerIndex::One));
    EXPECT_NO_THROW(Guide::ShowPlayerReview(PlayerIndex::One, nullptr));
    EXPECT_NO_THROW(Guide::ShowPlayers(PlayerIndex::One));
    EXPECT_NO_THROW(Guide::ShowSignIn(1, false));
    EXPECT_NO_THROW(Guide::ShowAchievementsEXT(PlayerIndex::One));
}
