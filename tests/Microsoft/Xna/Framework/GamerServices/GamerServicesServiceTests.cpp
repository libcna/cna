// SPDX-License-Identifier: MS-PL
#include <gtest/gtest.h>
#include <any>

#include <SDL3/SDL.h>

#include "System/ArgumentException.hpp"
#include "System/ArgumentOutOfRangeException.hpp"
#include "System/InvalidOperationException.hpp"
#include "System/NotSupportedException.hpp"

#include "Microsoft/Xna/Framework/GamerServices/GamerServicesDispatcher.hpp"
#include "Microsoft/Xna/Framework/GamerServices/Guide.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteFont.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/PlayerIndex.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"

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

// --- Guide message box overlay (Task 3.1) ---
//
// Unlike BeginShowKeyboardInput, this does not complete synchronously - it needs a real button
// selection, driven either by real mouse input through RenderPendingMessageBoxEXT (exercised via
// a real headless GraphicsDevice/SpriteBatch/SpriteFont below, smoke-test only - no way to
// synthesize a real OS mouse click in a unit test) or, for headless demos/tests, the dedicated
// SimulateMessageBoxClickEXT entry point. pendingMessageBox_ is process-wide static state, so
// every test below guards cleanup via ResetPendingMessageBoxForTestingEXT() to avoid stranding
// the single-pending-box guard for every later test in this binary.
namespace {
    struct MessageBoxGuard {
        ~MessageBoxGuard() { Guide::ResetPendingMessageBoxForTestingEXT(); }
    };

    Microsoft::Xna::Framework::Graphics::Texture2D MakeWhitePixelTexture(
        Microsoft::Xna::Framework::Graphics::GraphicsDevice& device
    ) {
        const std::vector<uint8_t> px = {255, 255, 255, 255};
        return Microsoft::Xna::Framework::Graphics::Texture2D::CreateFromPixels(device, 1, 1, px);
    }

    std::unique_ptr<Microsoft::Xna::Framework::Graphics::SpriteFont> MakeSimpleTestFont(
        Microsoft::Xna::Framework::Graphics::GraphicsDevice& device
    ) {
        using namespace Microsoft::Xna::Framework;
        using namespace Microsoft::Xna::Framework::Graphics;

        const std::vector<uint8_t> px = {255, 255, 255, 255};
        Texture2D atlas = Texture2D::CreateFromPixels(device, 1, 1, px);

        std::vector<SharpRuntime::charcs> chars;
        std::vector<Rectangle> bounds;
        std::vector<Rectangle> cropping;
        std::vector<Vector3> kerning;
        for (char c = 32; c < 127; ++c)
        {
            chars.push_back(static_cast<SharpRuntime::charcs>(c));
            bounds.push_back(Rectangle(0, 0, 1, 1));
            cropping.push_back(Rectangle(0, 0, 8, 14));
            kerning.push_back(Vector3(0.0f, 8.0f, 0.0f));
        }

        return std::make_unique<SpriteFont>(atlas, bounds, cropping, chars, 16, 1.0f, kerning,
                                             static_cast<SharpRuntime::charcs>(' '));
    }
}

TEST(GuideTest, HasPendingMessageBoxDefaultsFalse) {
    MessageBoxGuard guard;
    EXPECT_FALSE(Guide::getHasPendingMessageBoxEXTProperty());
}

TEST(GuideTest, BeginShowMessageBoxDoesNotThrowAndReturnsValidResult) {
    MessageBoxGuard guard;
    System::IAsyncResult* result = Guide::BeginShowMessageBox(
        "title", "text", std::vector<std::string>{"OK", "Cancel"}, 0, MessageBoxIcon::Alert,
        System::AsyncCallback{}, std::any{}
    );
    ASSERT_NE(nullptr, result);
    EXPECT_FALSE(result->getIsCompletedProperty());
    EXPECT_TRUE(Guide::getHasPendingMessageBoxEXTProperty());
    Guide::SimulateMessageBoxClickEXT(0);
    delete result;
}

TEST(GuideTest, BeginShowMessageBoxPlayerOverloadDoesNotThrowAndReturnsValidResult) {
    MessageBoxGuard guard;
    System::IAsyncResult* result = Guide::BeginShowMessageBox(
        PlayerIndex::One, "title", "text", std::vector<std::string>{"OK"}, 0, MessageBoxIcon::None,
        System::AsyncCallback{}, std::any{}
    );
    ASSERT_NE(nullptr, result);
    EXPECT_FALSE(result->getIsCompletedProperty());
    Guide::SimulateMessageBoxClickEXT(0);
    delete result;
}

TEST(GuideTest, BeginShowMessageBoxRejectsEmptyButtons) {
    MessageBoxGuard guard;
    EXPECT_THROW(
        Guide::BeginShowMessageBox(
            "title", "text", std::vector<std::string>{}, 0, MessageBoxIcon::None,
            System::AsyncCallback{}, std::any{}
        ),
        System::ArgumentException
    );
    EXPECT_FALSE(Guide::getHasPendingMessageBoxEXTProperty());
}

TEST(GuideTest, BeginShowMessageBoxThrowsWhileAnotherIsPending) {
    MessageBoxGuard guard;
    System::IAsyncResult* first = Guide::BeginShowMessageBox(
        "title", "text", std::vector<std::string>{"OK"}, 0, MessageBoxIcon::None,
        System::AsyncCallback{}, std::any{}
    );
    EXPECT_THROW(
        Guide::BeginShowMessageBox(
            "title2", "text2", std::vector<std::string>{"OK"}, 0, MessageBoxIcon::None,
            System::AsyncCallback{}, std::any{}
        ),
        System::InvalidOperationException
    );
    Guide::SimulateMessageBoxClickEXT(0);
    delete first;
}

TEST(GuideTest, EndShowMessageBoxThrowsIfCalledTooEarly) {
    MessageBoxGuard guard;
    System::IAsyncResult* result = Guide::BeginShowMessageBox(
        "title", "text", std::vector<std::string>{"OK"}, 0, MessageBoxIcon::None,
        System::AsyncCallback{}, std::any{}
    );
    EXPECT_THROW(Guide::EndShowMessageBox(result), System::InvalidOperationException);
    Guide::SimulateMessageBoxClickEXT(0);
    EXPECT_NO_THROW(Guide::EndShowMessageBox(result));
    delete result;
}

TEST(GuideTest, EndShowMessageBoxThrowsForMismatchedResult) {
    EXPECT_THROW(Guide::EndShowMessageBox(nullptr), System::ArgumentException);
}

// The synthetic "render frame -> simulate click -> End" cycle the plan calls for: a real
// RenderPendingMessageBoxEXT call (smoke-tested below) followed by a real SimulateMessageBoxClickEXT
// completion, confirming EndShowMessageBox returns exactly the clicked button's index.
TEST(GuideTest, RenderSimulateClickEndCycleReturnsCorrectButtonIndex) {
    using namespace Microsoft::Xna::Framework;
    using namespace Microsoft::Xna::Framework::Graphics;

    MessageBoxGuard guard;
    GraphicsDevice device;
    SpriteBatch spriteBatch(device);
    auto font = MakeSimpleTestFont(device);
    Texture2D whitePixel = MakeWhitePixelTexture(device);

    System::IAsyncResult* result = Guide::BeginShowMessageBox(
        "Title", "Body text", std::vector<std::string>{"Yes", "No", "Cancel"}, 1, MessageBoxIcon::Alert,
        System::AsyncCallback{}, std::any{}
    );

    spriteBatch.Begin();
    EXPECT_NO_THROW(Guide::RenderPendingMessageBoxEXT(device, spriteBatch, *font, whitePixel));
    spriteBatch.End();
    // No real mouse click occurred - rendering alone must never resolve the pending box.
    EXPECT_FALSE(result->getIsCompletedProperty());

    Guide::SimulateMessageBoxClickEXT(2);
    EXPECT_TRUE(result->getIsCompletedProperty());

    std::optional<int> selected = Guide::EndShowMessageBox(result);
    ASSERT_TRUE(selected.has_value());
    EXPECT_EQ(*selected, 2);
    delete result;
}

TEST(GuideTest, RenderPendingMessageBoxIsNoOpWhenNothingPending) {
    using namespace Microsoft::Xna::Framework;
    using namespace Microsoft::Xna::Framework::Graphics;

    MessageBoxGuard guard;
    GraphicsDevice device;
    SpriteBatch spriteBatch(device);
    auto font = MakeSimpleTestFont(device);
    Texture2D whitePixel = MakeWhitePixelTexture(device);

    spriteBatch.Begin();
    EXPECT_NO_THROW(Guide::RenderPendingMessageBoxEXT(device, spriteBatch, *font, whitePixel));
    spriteBatch.End();
}

// Task 3.1 checklist: "focusButton parameter is honored as the initial default selection."
// GetPendingMessageBoxFocusButtonForTestingEXT confirms it round-trips correctly without
// requiring pixel readback of the rendered highlight.
TEST(GuideTest, FocusButtonRoundTripsToPendingMessageBox) {
    MessageBoxGuard guard;
    System::IAsyncResult* result = Guide::BeginShowMessageBox(
        "title", "text", std::vector<std::string>{"A", "B", "C"}, 2, MessageBoxIcon::None,
        System::AsyncCallback{}, std::any{}
    );
    EXPECT_EQ(Guide::GetPendingMessageBoxFocusButtonForTestingEXT(), 2);
    Guide::SimulateMessageBoxClickEXT(0);
    delete result;
}

TEST(GuideTest, SimulateMessageBoxClickThrowsWhenNothingPending) {
    MessageBoxGuard guard;
    EXPECT_THROW(Guide::SimulateMessageBoxClickEXT(0), System::InvalidOperationException);
}

TEST(GuideTest, SimulateMessageBoxClickThrowsForOutOfRangeIndex) {
    MessageBoxGuard guard;
    System::IAsyncResult* result = Guide::BeginShowMessageBox(
        "title", "text", std::vector<std::string>{"OK"}, 0, MessageBoxIcon::None,
        System::AsyncCallback{}, std::any{}
    );
    EXPECT_THROW(Guide::SimulateMessageBoxClickEXT(-1), System::ArgumentOutOfRangeException);
    EXPECT_THROW(Guide::SimulateMessageBoxClickEXT(1), System::ArgumentOutOfRangeException);
    Guide::SimulateMessageBoxClickEXT(0);
    delete result;
}

// audit_net.md-style reentrancy check (same class of bug fixed for NetworkSession's Begin* /
// Phase 13): a callback that reentrantly calls BeginShowMessageBox again must see
// pendingMessageBox_ already cleared, not stale.
TEST(GuideTest, CallbackCanReentrantlyBeginANewMessageBox) {
    MessageBoxGuard guard;
    System::IAsyncResult* second = nullptr;
    System::IAsyncResult* first = Guide::BeginShowMessageBox(
        "first", "text", std::vector<std::string>{"OK"}, 0, MessageBoxIcon::None,
        [&second](System::IAsyncResult&) {
            second = Guide::BeginShowMessageBox(
                "second", "text", std::vector<std::string>{"OK"}, 0, MessageBoxIcon::None,
                System::AsyncCallback{}, std::any{}
            );
        },
        std::any{}
    );

    Guide::SimulateMessageBoxClickEXT(0);
    ASSERT_NE(second, nullptr);
    Guide::SimulateMessageBoxClickEXT(0);
    delete first;
    delete second;
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
