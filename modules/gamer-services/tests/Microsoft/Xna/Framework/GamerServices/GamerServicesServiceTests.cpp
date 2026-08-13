// SPDX-License-Identifier: MS-PL
#include <gtest/gtest.h>
#include <any>

#include "CNA/Platform/CurrentPlatform.hpp"
#include "CNA/Platform/PlatformException.hpp"
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
#include "Microsoft/Xna/Framework/Input/TextInputEXT.hpp"
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
//      (`Window_.setWindowInternal(GraphicsDevice_.GetPlatformWindowInternal(), ...);` in
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

// Post-plan_net.md remediation (2026-07-18): IsVisible now reflects real pending
// message-box/keyboard-input state (decision 1a - real observable behavior over a PC no-op stub,
// now that both overlays are genuinely real). With nothing pending, it still reads false, and the
// setter is still a no-op - only the "always" part of the old test name/assumption was wrong.
TEST(GuideTest, IsVisibleFalseWithNothingPendingAndSetterIsNoOp) {
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
    CNA::Platform::IPlatform& platform = CNA::Platform::GetCurrentPlatform();
    if (platform.GetDisplays() == nullptr)
    {
        // Headless and Terminal deliberately advertise no display service. Guide's safe fallback
        // is "enabled", and setting an unavailable host feature is a no-op rather than fabricated
        // mutable state.
        EXPECT_TRUE(Guide::getIsScreenSaverEnabledProperty());
        Guide::setIsScreenSaverEnabledProperty(false);
        EXPECT_TRUE(Guide::getIsScreenSaverEnabledProperty());
        GTEST_SKIP() << "the selected platform has no display/screen-saver service";
    }

    try
    {
        platform.AcquireSubsystem(CNA::Platform::PlatformSubsystem::Video);
    }
    catch (const CNA::Platform::PlatformException& error)
    {
        GTEST_SKIP() << "video subsystem unavailable: " << error.what();
    }

    Guide::setIsScreenSaverEnabledProperty(true);
    EXPECT_TRUE(Guide::getIsScreenSaverEnabledProperty());
    Guide::setIsScreenSaverEnabledProperty(false);
    EXPECT_FALSE(Guide::getIsScreenSaverEnabledProperty());

    platform.ReleaseSubsystem(CNA::Platform::PlatformSubsystem::Video);
}

// --- Guide keyboard input capture (Task 3.2) ---
//
// Unlike the message box, real typed text arrives through TextInputEXT::TextInput, which already
// fires automatically via the engine's own event pump - no separate render/pump entry point is
// needed. Tests simulate typing/Enter/Backspace directly via
// Microsoft::Xna::Framework::Input::TextInputEXT::INTERNAL_OnTextInput, exactly as the plan calls
// for. pendingKeyboardInput_ is process-wide static state, so every test guards cleanup via
// KeyboardInputGuard to avoid stranding the single-pending-request guard (and any leftover
// TextInputEXT subscription) for every later test in this binary.
namespace {
    struct KeyboardInputGuard {
        ~KeyboardInputGuard() { Guide::ResetPendingKeyboardInputForTestingEXT(); }
    };

    void TypeUtf16(const std::u16string& text) {
        for (char16_t c : text) {
            Microsoft::Xna::Framework::Input::TextInputEXT::INTERNAL_OnTextInput(
                static_cast<SharpRuntime::charcs>(c));
        }
    }

    void PressEnter() {
        Microsoft::Xna::Framework::Input::TextInputEXT::INTERNAL_OnTextInput(
            static_cast<SharpRuntime::charcs>(u'\r'));
    }

    // Moved up from the message-box test block below (also used by the keyboard-input
    // remediation tests' own RenderPendingKeyboardInputEXT coverage - must be declared before
    // first use in a single-pass translation unit).
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

TEST(GuideTest, BeginShowKeyboardInputDoesNotCompleteSynchronously) {
    KeyboardInputGuard guard;
    System::IAsyncResult* result = Guide::BeginShowKeyboardInput(
        PlayerIndex::One, "title", "description", "default", System::AsyncCallback{}, std::any{}
    );
    ASSERT_NE(nullptr, result);
    EXPECT_FALSE(result->getIsCompletedProperty());
    EXPECT_FALSE(result->getCompletedSynchronouslyProperty());
    PressEnter();
    EXPECT_TRUE(result->getIsCompletedProperty());
    delete result;
}

TEST(GuideTest, TypedTextIsReturnedExactlyAfterEnter) {
    KeyboardInputGuard guard;
    System::IAsyncResult* result = Guide::BeginShowKeyboardInput(
        PlayerIndex::One, "title", "description", "", System::AsyncCallback{}, std::any{}
    );
    TypeUtf16(u"Hello");
    EXPECT_FALSE(result->getIsCompletedProperty());
    PressEnter();
    ASSERT_TRUE(result->getIsCompletedProperty());
    EXPECT_EQ(Guide::EndShowKeyboardInput(result), "Hello");
    delete result;
}

TEST(GuideTest, PasswordModeOverloadCompletesTheSameWayAsNonPasswordOverload) {
    KeyboardInputGuard guard;
    System::IAsyncResult* result = Guide::BeginShowKeyboardInput(
        PlayerIndex::Two, "title", "description", "", System::AsyncCallback{}, std::any{}, true
    );
    TypeUtf16(u"secret");
    PressEnter();
    ASSERT_TRUE(result->getIsCompletedProperty());
    EXPECT_EQ(Guide::EndShowKeyboardInput(result), "secret");
    delete result;
}

// Plan checklist: "confirm defaultText pre-seeds correctly" / "a user who presses Enter
// immediately gets the default, matching real XNA semantics."
TEST(GuideTest, DefaultTextPreSeedsAndIsReturnedIfEnterPressedImmediately) {
    KeyboardInputGuard guard;
    System::IAsyncResult* result = Guide::BeginShowKeyboardInput(
        PlayerIndex::One, "title", "description", "default text", System::AsyncCallback{}, std::any{}
    );
    PressEnter();
    EXPECT_EQ(Guide::EndShowKeyboardInput(result), "default text");
    delete result;
}

TEST(GuideTest, DefaultTextIsEditableBeforeConfirming) {
    KeyboardInputGuard guard;
    System::IAsyncResult* result = Guide::BeginShowKeyboardInput(
        PlayerIndex::One, "title", "description", "abc", System::AsyncCallback{}, std::any{}
    );
    TypeUtf16(u"def");
    PressEnter();
    EXPECT_EQ(Guide::EndShowKeyboardInput(result), "abcdef");
    delete result;
}

// Plan checklist: "confirm a surrogate pair (an emoji) round-trips correctly."
TEST(GuideTest, SurrogatePairEmojiRoundTripsCorrectly) {
    KeyboardInputGuard guard;
    System::IAsyncResult* result = Guide::BeginShowKeyboardInput(
        PlayerIndex::One, "title", "description", "", System::AsyncCallback{}, std::any{}
    );
    // U+1F600 GRINNING FACE = surrogate pair D83D DE00, delivered as two separate TextInput calls
    // exactly as real typing would (SDL emits a code point above U+FFFF as high-then-low
    // surrogate, per TextInputEXT::TextInput's own documented UTF-16 contract).
    TypeUtf16(u"Hi \U0001F600!");
    PressEnter();
    EXPECT_EQ(Guide::EndShowKeyboardInput(result), "Hi \xF0\x9F\x98\x80!");
    delete result;
}

// Backspace after an emoji must delete the whole code point (both surrogate halves), not just
// the low surrogate.
TEST(GuideTest, BackspaceAfterEmojiRemovesTheWholeSurrogatePair) {
    KeyboardInputGuard guard;
    System::IAsyncResult* result = Guide::BeginShowKeyboardInput(
        PlayerIndex::One, "title", "description", "", System::AsyncCallback{}, std::any{}
    );
    TypeUtf16(u"a\U0001F600");
    Microsoft::Xna::Framework::Input::TextInputEXT::INTERNAL_OnTextInput(static_cast<SharpRuntime::charcs>(u'\b'));
    PressEnter();
    EXPECT_EQ(Guide::EndShowKeyboardInput(result), "a");
    delete result;
}

TEST(GuideTest, BackspaceRemovesLastTypedCharacter) {
    KeyboardInputGuard guard;
    System::IAsyncResult* result = Guide::BeginShowKeyboardInput(
        PlayerIndex::One, "title", "description", "", System::AsyncCallback{}, std::any{}
    );
    TypeUtf16(u"Hellp");
    Microsoft::Xna::Framework::Input::TextInputEXT::INTERNAL_OnTextInput(static_cast<SharpRuntime::charcs>(u'\b'));
    TypeUtf16(u"o");
    PressEnter();
    EXPECT_EQ(Guide::EndShowKeyboardInput(result), "Hello");
    delete result;
}

TEST(GuideTest, EndShowKeyboardInputThrowsIfCalledTooEarly) {
    KeyboardInputGuard guard;
    System::IAsyncResult* result = Guide::BeginShowKeyboardInput(
        PlayerIndex::One, "title", "description", "", System::AsyncCallback{}, std::any{}
    );
    TypeUtf16(u"partial");
    EXPECT_THROW(Guide::EndShowKeyboardInput(result), System::InvalidOperationException);
    PressEnter();
    EXPECT_NO_THROW(Guide::EndShowKeyboardInput(result));
    delete result;
}

TEST(GuideTest, EndShowKeyboardInputThrowsForMismatchedResult) {
    EXPECT_THROW(Guide::EndShowKeyboardInput(nullptr), System::ArgumentException);
}

TEST(GuideTest, BeginShowKeyboardInputThrowsWhileAnotherIsPending) {
    KeyboardInputGuard guard;
    System::IAsyncResult* first = Guide::BeginShowKeyboardInput(
        PlayerIndex::One, "title", "description", "", System::AsyncCallback{}, std::any{}
    );
    EXPECT_THROW(
        Guide::BeginShowKeyboardInput(
            PlayerIndex::Two, "title2", "description2", "", System::AsyncCallback{}, std::any{}
        ),
        System::InvalidOperationException
    );
    PressEnter();
    delete first;
}

// audit_net.md High finding: GuideAction stored its AsyncCallback but never invoked it. Confirms
// the callback now fires exactly once, on Enter (not at Begin*, since this action no longer
// completes synchronously), with the correct IAsyncResult identity and AsyncState.
TEST(GuideTest, BeginShowKeyboardInputInvokesCallbackExactlyOnceOnEnterWithCorrectIdentity) {
    KeyboardInputGuard guard;
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

    EXPECT_EQ(callCount, 0);
    TypeUtf16(u"x");
    EXPECT_EQ(callCount, 0);
    PressEnter();

    EXPECT_EQ(callCount, 1);
    EXPECT_EQ(observedResult, result);
    ASSERT_TRUE(result->getIsCompletedProperty());
    EXPECT_EQ(std::any_cast<int>(result->getAsyncStateProperty()), 7);

    Guide::EndShowKeyboardInput(result);
    delete result;
}

// --- Guide keyboard input remediation (post-plan_net.md independent audit, 2026-07-18) ---
//
// An independent post-completion audit found BeginShowKeyboardInput's title/description
// parameters were silently discarded, UsePasswordMode was stored but never used, IsVisible never
// reflected a real pending request, and there was no cancel path at all. The tests below cover
// each of those four gaps directly.

TEST(GuideTest, TitleAndDescriptionAreStoredForRendering) {
    KeyboardInputGuard guard;
    System::IAsyncResult* result = Guide::BeginShowKeyboardInput(
        PlayerIndex::One, "Enter your name", "Used for the leaderboard", "", System::AsyncCallback{}, std::any{}
    );
    EXPECT_EQ(Guide::GetPendingKeyboardInputTitleForTestingEXT(), "Enter your name");
    EXPECT_EQ(Guide::GetPendingKeyboardInputDescriptionForTestingEXT(), "Used for the leaderboard");
    PressEnter();
    delete result;
}

TEST(GuideTest, GetPendingKeyboardInputTitleThrowsWhenNothingPending) {
    EXPECT_THROW(Guide::GetPendingKeyboardInputTitleForTestingEXT(), System::InvalidOperationException);
    EXPECT_THROW(Guide::GetPendingKeyboardInputDescriptionForTestingEXT(), System::InvalidOperationException);
}

TEST(GuideTest, HasPendingKeyboardInputEXTReflectsRealState) {
    KeyboardInputGuard guard;
    EXPECT_FALSE(Guide::getHasPendingKeyboardInputEXTProperty());
    System::IAsyncResult* result = Guide::BeginShowKeyboardInput(
        PlayerIndex::One, "title", "description", "", System::AsyncCallback{}, std::any{}
    );
    EXPECT_TRUE(Guide::getHasPendingKeyboardInputEXTProperty());
    PressEnter();
    EXPECT_FALSE(Guide::getHasPendingKeyboardInputEXTProperty());
    delete result;
}

TEST(GuideTest, IsVisibleReflectsPendingKeyboardInput) {
    KeyboardInputGuard guard;
    EXPECT_FALSE(Guide::getIsVisibleProperty());
    System::IAsyncResult* result = Guide::BeginShowKeyboardInput(
        PlayerIndex::One, "title", "description", "", System::AsyncCallback{}, std::any{}
    );
    EXPECT_TRUE(Guide::getIsVisibleProperty());
    PressEnter();
    EXPECT_FALSE(Guide::getIsVisibleProperty());
    delete result;
}

TEST(GuideTest, WasKeyboardInputCanceledEXTFalseAfterNormalConfirm) {
    KeyboardInputGuard guard;
    System::IAsyncResult* result = Guide::BeginShowKeyboardInput(
        PlayerIndex::One, "title", "description", "", System::AsyncCallback{}, std::any{}
    );
    TypeUtf16(u"Hello");
    PressEnter();
    EXPECT_FALSE(Guide::WasKeyboardInputCanceledEXT(result));
    EXPECT_EQ(Guide::EndShowKeyboardInput(result), "Hello");
    delete result;
}

TEST(GuideTest, SimulateKeyboardInputCancelEXTCancelsAndClearsBuffer) {
    KeyboardInputGuard guard;
    int callCount = 0;
    System::IAsyncResult* result = Guide::BeginShowKeyboardInput(
        PlayerIndex::One, "title", "description", "",
        [&callCount](System::IAsyncResult&) { ++callCount; }, std::any{}
    );
    TypeUtf16(u"partial text");
    EXPECT_FALSE(result->getIsCompletedProperty());

    Guide::SimulateKeyboardInputCancelEXT();

    EXPECT_TRUE(result->getIsCompletedProperty());
    EXPECT_EQ(callCount, 1);
    EXPECT_TRUE(Guide::WasKeyboardInputCanceledEXT(result));
    // A canceled edit discards the typed text (matching a real on-screen keyboard's own
    // cancel-discards-the-edit semantics), same "nothing to show" convention FNA's own stub
    // already uses for its always-empty EndShowKeyboardInput.
    EXPECT_EQ(Guide::EndShowKeyboardInput(result), "");
    EXPECT_FALSE(Guide::getHasPendingKeyboardInputEXTProperty());
    delete result;
}

TEST(GuideTest, SimulateKeyboardInputCancelEXTThrowsWhenNothingPending) {
    EXPECT_THROW(Guide::SimulateKeyboardInputCancelEXT(), System::InvalidOperationException);
}

TEST(GuideTest, WasKeyboardInputCanceledEXTThrowsForMismatchedResult) {
    EXPECT_THROW(Guide::WasKeyboardInputCanceledEXT(nullptr), System::ArgumentException);
}

// The synthetic "render frame (Escape not pressed) -> type -> Enter -> End" cycle: a real
// RenderPendingKeyboardInputEXT call (smoke-tested, mirrors RenderPendingMessageBoxEXT's own
// established pattern) must never resolve the pending request by itself - only a real Escape
// press or SimulateKeyboardInputCancelEXT/Enter can.
TEST(GuideTest, RenderPendingKeyboardInputDoesNotAutoCompleteAndSupportsPasswordMasking) {
    using namespace Microsoft::Xna::Framework;
    using namespace Microsoft::Xna::Framework::Graphics;

    KeyboardInputGuard guard;
    GraphicsDevice device;
    SpriteBatch spriteBatch(device);
    auto font = MakeSimpleTestFont(device);
    Texture2D whitePixel = MakeWhitePixelTexture(device);

    System::IAsyncResult* result = Guide::BeginShowKeyboardInput(
        PlayerIndex::One, "Enter password", "6+ characters", "", System::AsyncCallback{}, std::any{}, true
    );
    TypeUtf16(u"secret");

    spriteBatch.Begin();
    EXPECT_NO_THROW(Guide::RenderPendingKeyboardInputEXT(device, spriteBatch, *font, whitePixel));
    spriteBatch.End();
    // No real Escape press occurred - rendering alone must never cancel/resolve the pending
    // request, matching RenderPendingMessageBoxEXT's own equivalent guarantee for mouse clicks.
    EXPECT_FALSE(result->getIsCompletedProperty());
    // audit_net.md remediation (2026-07-18): the display-text assertion this test's own name
    // ("SupportsPasswordMasking") always implied but never actually checked - EndShowKeyboardInput
    // below only ever proves the real typed text round-trips, which is correct/expected even if
    // masking were entirely broken. GetPendingKeyboardInputDisplayTextForTestingEXT reads the same
    // masking decision RenderPendingKeyboardInputEXT itself draws (see ComputeDisplayText's own
    // comment in Guide.cpp) - this genuinely fails if that branch is ever removed or broken.
    EXPECT_EQ(Guide::GetPendingKeyboardInputDisplayTextForTestingEXT(), "******");

    PressEnter();
    ASSERT_TRUE(result->getIsCompletedProperty());
    // usePasswordMode only masks the on-screen render - the real typed text still round-trips
    // through EndShowKeyboardInput exactly, matching real XNA (both overloads complete
    // identically; only on-screen display differs).
    EXPECT_EQ(Guide::EndShowKeyboardInput(result), "secret");
    delete result;
}

// audit_net.md remediation (2026-07-18): the other direction of the same branch - without
// usePasswordMode, the display text must be the real typed text unmasked. Together with the test
// above, this covers both branches of ComputeDisplayText, so neither "always mask" nor "never
// mask" could pass both tests.
TEST(GuideTest, RenderPendingKeyboardInputDisplaysRealTextWhenPasswordModeIsOff) {
    using namespace Microsoft::Xna::Framework;
    using namespace Microsoft::Xna::Framework::Graphics;

    KeyboardInputGuard guard;

    System::IAsyncResult* result = Guide::BeginShowKeyboardInput(
        PlayerIndex::One, "Enter name", "", "", System::AsyncCallback{}, std::any{}, false
    );
    TypeUtf16(u"secret");

    EXPECT_EQ(Guide::GetPendingKeyboardInputDisplayTextForTestingEXT(), "secret");

    PressEnter();
    ASSERT_TRUE(result->getIsCompletedProperty());
    EXPECT_EQ(Guide::EndShowKeyboardInput(result), "secret");
    delete result;
}

TEST(GuideTest, RenderPendingKeyboardInputIsNoOpWhenNothingPending) {
    using namespace Microsoft::Xna::Framework;
    using namespace Microsoft::Xna::Framework::Graphics;

    KeyboardInputGuard guard;
    GraphicsDevice device;
    SpriteBatch spriteBatch(device);
    auto font = MakeSimpleTestFont(device);
    Texture2D whitePixel = MakeWhitePixelTexture(device);

    spriteBatch.Begin();
    EXPECT_NO_THROW(Guide::RenderPendingKeyboardInputEXT(device, spriteBatch, *font, whitePixel));
    spriteBatch.End();
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
