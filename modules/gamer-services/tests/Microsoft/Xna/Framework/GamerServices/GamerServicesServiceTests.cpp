// SPDX-License-Identifier: MS-PL
#include "../../../../../../platform/tests/CNA/Platform/PlatformTestDecorator.hpp"
#include "../../../../../src/Internal/GuideOverlay.hpp"
#include "CNA/Platform/Input/IPlatformKeyboard.hpp"
#include <functional>
#include <memory>
#include <tuple>
#include <typeinfo>
#include "System/detail/FrameworkCompatibility.hpp"
#include <gtest/gtest.h>
#include "Microsoft/Xna/Framework/GamerServices/GamerPrivilegeException.hpp"
#include "System/ArgumentNullException.hpp"
#include <any>
#include "System/Threading/EventWaitHandle.hpp"
#include "../../../../../src/Internal/Guide/GuideSystem.hpp"

#include "CNA/Platform/CurrentPlatform.hpp"
#include "CNA/Platform/CannedMouse.hpp"
#include "CNA/Platform/PlatformException.hpp"
#include "CNA/Platform/PlatformTestDecorator.hpp"
#include "System/ArgumentException.hpp"
#include "System/ArgumentOutOfRangeException.hpp"
#include "System/InvalidOperationException.hpp"
#include "System/NotSupportedException.hpp"
#include "System/ObjectDisposedException.hpp"
#include "CNA/Internal/GamerServices/ServiceInvitations.hpp"

#include "Microsoft/Xna/Framework/GamerServices/GamerServicesDispatcher.hpp"
#include "Microsoft/Xna/Framework/GamerServices/Guide.hpp"
#include "Microsoft/Xna/Framework/GamerServices/GuideAlreadyVisibleException.hpp"
#include "Microsoft/Xna/Framework/GamerServices/FriendGamer.hpp"
#include "Microsoft/Xna/Framework/GamerServices/GamerServicesNotAvailableException.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteFont.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Input/TextInputEXT.hpp"
#include "Microsoft/Xna/Framework/Input/Touch/GestureSample.hpp"
#include "Microsoft/Xna/Framework/Input/Touch/GestureType.hpp"
#include "Microsoft/Xna/Framework/Input/Touch/TouchPanel.hpp"
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
    // Registration order differs between link layouts (the MinGW binary runs the service tests
    // after a test that initialized the dispatcher), so this is only meaningful first.
    if (GamerServicesDispatcher::getIsInitializedProperty())
        GTEST_SKIP() << "another test initialized gamer services in this process";
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

// XNA's IsTrialMode setter is internal: each dispatcher update latches it (SystemGuideTest covers
// that); setting SimulateTrialMode alone changes only the request.
TEST(GuideTest, SimulateTrialModeGetSet) {
    const bool trial = Guide::getIsTrialModeProperty();
    Guide::setSimulateTrialModeProperty(true);
    EXPECT_TRUE(Guide::getSimulateTrialModeProperty());
    EXPECT_EQ(trial, Guide::getIsTrialModeProperty());
    Guide::setSimulateTrialModeProperty(false);
    EXPECT_FALSE(Guide::getSimulateTrialModeProperty());
    // XNA Refresh set_SimulateTrialMode IL_0000..0006 writes only simulateTrialMode.
    EXPECT_EQ(trial, Guide::getIsTrialModeProperty());
}

// IsVisible reflects the Guide's panes (XNA's setter is internal). The public getter needs
// initialized gamer services, which this suite never initializes; the pane state behind it is read
// directly (SystemGuideTests covers the public property).
TEST(GuideTest, IsVisibleFalseWithNothingPending) {
    EXPECT_FALSE(CNA::Internal::GamerServices::guideIsVisible());
}

TEST(GuideTest, NotificationPositionDefaultAndSet) {
    // Reference default: BottomCenter.
    EXPECT_EQ(NotificationPosition::BottomCenter, Guide::getNotificationPositionProperty());
    Guide::setNotificationPositionProperty(NotificationPosition::TopLeft);
    EXPECT_EQ(NotificationPosition::TopLeft, Guide::getNotificationPositionProperty());
    Guide::setNotificationPositionProperty(NotificationPosition::BottomCenter);
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


TEST(GuideTest, ScreenSaverWithoutDisplaysAlwaysReportsEnabled) {
    // XNA Refresh get_IsScreenSaverEnabled IL_0000..0001 returns true; setter IL_0000 is a no-op.
    // This covers CNA's no-display branch deterministically without the older test's GTEST_SKIP.
    struct NoDisplays final : CNA::Platform::Testing::PlatformTestDecorator {
        CNA::Platform::IPlatformDisplays* GetDisplays() override { return nullptr; }
    } platform;
    CNA::Platform::Testing::ScopedCurrentPlatform installed(platform);
    for (bool request : {false, true, false}) {
        Guide::setIsScreenSaverEnabledProperty(request);
        EXPECT_TRUE(Guide::getIsScreenSaverEnabledProperty());
    }
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
    // Reference: the kernel refuses while the Guide is visible (GuideAlreadyVisibleException).
    EXPECT_THROW(
        Guide::BeginShowKeyboardInput(
            PlayerIndex::Two, "title2", "description2", "", System::AsyncCallback{}, std::any{}
        ),
        GuideAlreadyVisibleException
    );
    PressEnter();
    delete first;
}

// Reference BeginShowKeyboardInput: title, description and default text each under 256 UTF-16
// units; a defined player.
TEST(GuideTest, BeginShowKeyboardInputValidatesArgumentsLikeTheReference) {
    KeyboardInputGuard guard;
    const std::string tooLong(256, 'x');
    for (const auto& [title, description, text] : std::vector<std::tuple<std::string, std::string, std::string>>{
             {tooLong, "d", ""}, {"t", tooLong, ""}, {"t", "d", tooLong}}) {
        EXPECT_THROW((void)Guide::BeginShowKeyboardInput(PlayerIndex::One, title, description, text, {}, {}),
                     System::ArgumentException);
    }
    EXPECT_THROW((void)Guide::BeginShowKeyboardInput(static_cast<PlayerIndex>(4), "t", "d", "", {}, {}),
                 System::ArgumentOutOfRangeException);
    EXPECT_FALSE(Guide::getHasPendingKeyboardInputEXTProperty());
    std::unique_ptr<System::IAsyncResult> result(
        Guide::BeginShowKeyboardInput(PlayerIndex::Three, "", "", std::string(255, 'y'), {}, {}));
    EXPECT_TRUE(Guide::getHasPendingKeyboardInputEXTProperty());
    PressEnter();
    EXPECT_EQ(std::string(255, 'y'), Guide::EndShowKeyboardInput(result.get()));
}

// Regression: GuideAction once stored its AsyncCallback without invoking it. Confirms
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

// --- Guide keyboard input remediation (post-Net-plan independent audit, 2026-07-18) ---
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

// The keyboard prompt is the sample's other overlay (the high-score name entry) and owns the
// screen the same way.
TEST(GuideTest, AVisibleKeyboardPromptWithholdsTouchInputFromTheGame) {
    using Microsoft::Xna::Framework::Input::Touch::TouchPanel;

    KeyboardInputGuard guard;
    TouchPanel::setInputSuppressedEXT(false);
    System::IAsyncResult* result = Guide::BeginShowKeyboardInput(
        PlayerIndex::One, "title", "description", "", System::AsyncCallback{}, std::any{}
    );
    EXPECT_TRUE(TouchPanel::getInputSuppressedEXT());
    PressEnter();
    EXPECT_FALSE(TouchPanel::getInputSuppressedEXT());
    delete result;
}

TEST(GuideTest, IsVisibleReflectsPendingKeyboardInput) {
    KeyboardInputGuard guard;
    EXPECT_FALSE(CNA::Internal::GamerServices::guideIsVisible());
    System::IAsyncResult* result = Guide::BeginShowKeyboardInput(
        PlayerIndex::One, "title", "description", "", System::AsyncCallback{}, std::any{}
    );
    EXPECT_TRUE(CNA::Internal::GamerServices::guideIsVisible());
    PressEnter();
    EXPECT_FALSE(CNA::Internal::GamerServices::guideIsVisible());
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
    // The display-text assertion this test's own name
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

// The other direction of the same branch: without
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

// SAMPLE-065: on every real XNA platform the shell owns the screen while a Guide overlay is up,
// so the game's touch panel reports nothing behind it. CNA draws the overlay inside the game's
// own Draw(), so without this the tap that misses a message-box button falls straight through to
// whatever the game is drawing underneath -- in NinjAcademy, back onto the menu entry that raised
// the box, which then raised a second one and terminated on "A message box is already pending."
TEST(GuideTest, AVisibleMessageBoxWithholdsTouchInputFromTheGame) {
    using Microsoft::Xna::Framework::Input::Touch::GestureSample;
    using Microsoft::Xna::Framework::Input::Touch::GestureType;
    using Microsoft::Xna::Framework::Input::Touch::TouchPanel;

    MessageBoxGuard guard;
    // XNA refuses gesture reads until EnabledGestures has been assigned (RV-INPUT-4).
    TouchPanel::setEnabledGesturesProperty(TouchPanel::getEnabledGesturesProperty());
    TouchPanel::setInputSuppressedEXT(false);
    ASSERT_FALSE(TouchPanel::getInputSuppressedEXT());

    System::IAsyncResult* result = Guide::BeginShowMessageBox(
        "Load game", "Saved game data detected. Load it?", std::vector<std::string>{"Yes", "No"},
        0, MessageBoxIcon::None, System::AsyncCallback{}, std::any{}
    );
    EXPECT_TRUE(TouchPanel::getInputSuppressedEXT());

    // A tap behind the overlay reaches neither the gesture queue nor the touch state.
    TouchPanel::EnqueueGesture(GestureSample(
        GestureType::Tap, System::TimeSpan::Zero,
        Microsoft::Xna::Framework::Vector2(470.0f, 300.0f), Microsoft::Xna::Framework::Vector2::Zero,
        Microsoft::Xna::Framework::Vector2::Zero, Microsoft::Xna::Framework::Vector2::Zero));
    EXPECT_FALSE(TouchPanel::getIsGestureAvailableProperty());
    EXPECT_EQ(0u, TouchPanel::GetState().getCountProperty());

    // Answering it hands input straight back.
    Guide::SimulateMessageBoxClickEXT(1);
    EXPECT_FALSE(TouchPanel::getInputSuppressedEXT());
    TouchPanel::EnqueueGesture(GestureSample(
        GestureType::Tap, System::TimeSpan::Zero,
        Microsoft::Xna::Framework::Vector2(470.0f, 300.0f), Microsoft::Xna::Framework::Vector2::Zero,
        Microsoft::Xna::Framework::Vector2::Zero, Microsoft::Xna::Framework::Vector2::Zero));
    EXPECT_TRUE(TouchPanel::getIsGestureAvailableProperty());
    (void)TouchPanel::ReadGesture();
    delete result;
}

// A gesture queued before the overlay went up must not fire the frame it comes down either.
TEST(GuideTest, ARaisedOverlayDiscardsGesturesAlreadyQueued) {
    using Microsoft::Xna::Framework::Input::Touch::GestureSample;
    using Microsoft::Xna::Framework::Input::Touch::GestureType;
    using Microsoft::Xna::Framework::Input::Touch::TouchPanel;

    MessageBoxGuard guard;
    // XNA refuses gesture reads until EnabledGestures has been assigned (RV-INPUT-4).
    TouchPanel::setEnabledGesturesProperty(TouchPanel::getEnabledGesturesProperty());
    TouchPanel::setInputSuppressedEXT(false);
    TouchPanel::EnqueueGesture(GestureSample(
        GestureType::Tap, System::TimeSpan::Zero,
        Microsoft::Xna::Framework::Vector2(10.0f, 10.0f), Microsoft::Xna::Framework::Vector2::Zero,
        Microsoft::Xna::Framework::Vector2::Zero, Microsoft::Xna::Framework::Vector2::Zero));
    ASSERT_TRUE(TouchPanel::getIsGestureAvailableProperty());

    System::IAsyncResult* result = Guide::BeginShowMessageBox(
        "title", "text", std::vector<std::string>{"OK"}, 0, MessageBoxIcon::None,
        System::AsyncCallback{}, std::any{}
    );
    EXPECT_FALSE(TouchPanel::getIsGestureAvailableProperty());

    Guide::SimulateMessageBoxClickEXT(0);
    EXPECT_FALSE(TouchPanel::getIsGestureAvailableProperty());
    delete result;
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
    // Reference: the kernel refuses while the Guide is visible, raised as GuideAlreadyVisibleException.
    EXPECT_THROW(
        Guide::BeginShowMessageBox(
            "title2", "text2", std::vector<std::string>{"OK"}, 0, MessageBoxIcon::None,
            System::AsyncCallback{}, std::any{}
        ),
        GuideAlreadyVisibleException
    );
    Guide::SimulateMessageBoxClickEXT(0);
    delete first;
}

// Reference Guide.ValidateShowMessageBoxArgs: title and text non-empty and under 256 UTF-16 units,
// one to three buttons each non-empty and under 256, focus within them.
TEST(GuideTest, BeginShowMessageBoxValidatesArgumentsLikeTheReference) {
    MessageBoxGuard guard;
    const auto show = [](const std::string& title, const std::string& text, std::vector<std::string> buttons, int focus,
                         PlayerIndex player = PlayerIndex::One) {
        return std::unique_ptr<System::IAsyncResult>(Guide::BeginShowMessageBox(
            player, title, text, std::move(buttons), focus, MessageBoxIcon::None, System::AsyncCallback{}, std::any{}));
    };
    const std::string longest(255, 'x'), tooLong(256, 'x');
    EXPECT_THROW((void)show("", "text", {"OK"}, 0), System::ArgumentException);
    EXPECT_THROW((void)show(tooLong, "text", {"OK"}, 0), System::ArgumentException);
    EXPECT_THROW((void)show("title", "", {"OK"}, 0), System::ArgumentException);
    EXPECT_THROW((void)show("title", tooLong, {"OK"}, 0), System::ArgumentException);
    EXPECT_THROW((void)show("title", "text", {"A", "B", "C", "D"}, 0), System::ArgumentException);
    EXPECT_THROW((void)show("title", "text", {"OK", ""}, 0), System::ArgumentException);
    EXPECT_THROW((void)show("title", "text", {"OK", tooLong}, 0), System::ArgumentException);
    EXPECT_THROW((void)show("title", "text", {"OK", "Cancel"}, 2), System::ArgumentOutOfRangeException);
    EXPECT_THROW((void)show("title", "text", {"OK"}, -1), System::ArgumentOutOfRangeException);
    EXPECT_THROW((void)show("title", "text", {"OK"}, 0, static_cast<PlayerIndex>(4)), System::ArgumentOutOfRangeException);
    EXPECT_FALSE(Guide::getHasPendingMessageBoxEXTProperty());
    // 255 UTF-16 units in 256 UTF-8 bytes: a two-byte character counts once, as in .NET.
    const std::string accented = std::string(254, 'x') + "\xC3\xA9";
    auto result = show(longest, accented, {"A", "B", "C"}, 2, PlayerIndex::Four);
    EXPECT_TRUE(Guide::getHasPendingMessageBoxEXTProperty());
    Guide::SimulateMessageBoxClickEXT(2);
    EXPECT_EQ(2, Guide::EndShowMessageBox(result.get()));
}

namespace {
    // Stands in for the game's modal frames: counts them and answers the Guide on a chosen frame.
    struct ScriptedModalFrames final : CNA::Internal::Runtime::IModalFrames {
        int frames = 0;
        int answerOn = 3;
        std::function<void()> answer;
        bool runModalFrame() override {
            if (++frames == answerOn) answer();
            return true;
        }
    };
    struct ModalFramesGuard {
        explicit ModalFramesGuard(CNA::Internal::Runtime::IModalFrames* frames) {
            CNA::Internal::GamerServices::setGuideModalFramesForTesting(frames);
        }
        ~ModalFramesGuard() { CNA::Internal::GamerServices::setGuideModalFramesForTesting(nullptr); }
    };
    std::unique_ptr<System::IAsyncResult> ShowTwoButtonBox() {
        return std::unique_ptr<System::IAsyncResult>(Guide::BeginShowMessageBox(PlayerIndex::One, "title", "text",
            {"OK", "Cancel"}, 0, MessageBoxIcon::None, System::AsyncCallback{}, std::any{}));
    }
}

// Reference XOverlappedAsyncResult.PrepareForEndFunction: a null result, one from another Begin, and
// a second End are refused, in that order.

namespace {
struct ForeignGuideResult final : System::IAsyncResult {
    mutable int reads = 0;
    mutable System::Threading::EventWaitHandle wait{false, System::Threading::EventResetMode::ManualReset};
    std::any state;
    bool getIsCompletedProperty() const override { ++reads; return false; }
    bool getCompletedSynchronouslyProperty() const override { ++reads; return false; }
    const std::any& getAsyncStateProperty() const override { ++reads; return state; }
    System::Threading::WaitHandle& getAsyncWaitHandleProperty() const override { ++reads; return wait; }
};
}

TEST(GuideTest, ForeignEndResultsAreRejectedBeforeTheirPropertiesAreRead) {
    // XOverlappedAsyncResult.PrepareForEndFunction IL_000e..0022 uses a type check,
    // ArgumentException(IAsyncNotFromBegin), with no ParamName and no result access.
    // PrepareForEndFunction IL_0000..000d: null uses ArgumentNullException("result").
    for (const auto& end : std::vector<std::function<void()>>{
             [] { (void)Guide::EndShowMessageBox(nullptr); },
             [] { (void)Guide::EndShowKeyboardInput(nullptr); }}) {
        try { end(); FAIL() << "Expected null-result refusal"; }
        catch (const System::ArgumentNullException& error) {
            EXPECT_EQ(typeid(System::ArgumentNullException), typeid(error));
            EXPECT_EQ("result", error.getParamNameProperty());
            const char* message = System::detail::UseNetFrameworkArgumentExceptionMessages()
                ? "Value cannot be null.\nParameter name: result"
                : "Value cannot be null. (Parameter 'result')";
            EXPECT_EQ(message, error.getMessageProperty());
        }
    }
    ForeignGuideResult foreign;
    for (const auto& end : std::vector<std::function<void()>>{
             [&] { (void)Guide::EndShowMessageBox(&foreign); },
             [&] { (void)Guide::EndShowKeyboardInput(&foreign); }}) {
        try { end(); FAIL() << "Expected ArgumentException"; }
        catch (const System::ArgumentException& error) {
            EXPECT_EQ(typeid(System::ArgumentException), typeid(error));
            EXPECT_EQ("", error.getParamNameProperty());
            EXPECT_EQ("The IAsyncResult parameter to this \"End\" function must be obtained by calling the matching \"Begin.\"", error.getMessageProperty());
        }
        EXPECT_EQ(0, foreign.reads);
    }
}

TEST(GuideTest, CancelledMessageBoxHasNoChoiceAndEndCanOnlyConsumeItOnce) {
    // EndShowMessageBox IL_0098..00ad maps the cancellation sentinel to Nullable<int> without a value.
    // PrepareForEndFunction IL_0023..0035 refuses a repeated End, even for cancellation.
    MessageBoxGuard boxes;
    auto result = ShowTwoButtonBox();
    CNA::Internal::GamerServices::GuideUi::answerMessageBox(std::nullopt);
    EXPECT_TRUE(result->getIsCompletedProperty());
    EXPECT_TRUE(result->getAsyncWaitHandleProperty().WaitOne(0));
    EXPECT_FALSE(CNA::Internal::GamerServices::guideIsVisible());
    EXPECT_EQ(std::nullopt, Guide::EndShowMessageBox(result.get()));
    try { (void)Guide::EndShowMessageBox(result.get()); FAIL() << "Expected repeated End refusal"; }
    catch (const System::InvalidOperationException& error) {
        EXPECT_EQ("An \"End\" function can only be called once for each call to \"Begin.\"", error.getMessageProperty());
    }
}

TEST(GuideTest, KeyboardRepeatedEndHasRefreshDiagnostic) {
    // XOverlappedAsyncResult.PrepareForEndFunction IL_0023..0035, CannotEndTwice resource.
    KeyboardInputGuard keyboards;
    std::unique_ptr<System::IAsyncResult> result(Guide::BeginShowKeyboardInput(
        PlayerIndex::One, "", "", "kept", {}, {}));
    PressEnter();
    EXPECT_EQ("kept", Guide::EndShowKeyboardInput(result.get()));
    try { (void)Guide::EndShowKeyboardInput(result.get()); FAIL() << "Expected repeated End refusal"; }
    catch (const System::InvalidOperationException& error) {
        EXPECT_EQ("An \"End\" function can only be called once for each call to \"Begin.\"", error.getMessageProperty());
    }
}

TEST(GuideTest, PendingEndClaimsTheResultBeforePumpingCallbacks) {
    // PrepareForEndFunction IL_0036..0048 sets endHasBeenCalled before waiting.
    // A callback reached during CNA's modal wait must not consume that same result a second time.
    MessageBoxGuard boxes;
    KeyboardInputGuard keyboards;
    ScriptedModalFrames frames;
    ModalFramesGuard installed(&frames);
    int callbacks = 0;
    auto callback = [&](System::IAsyncResult& result) {
        ++callbacks;
        EXPECT_TRUE(result.getIsCompletedProperty());
        EXPECT_THROW((void)Guide::EndShowMessageBox(&result), System::InvalidOperationException);
    };
    std::unique_ptr<System::IAsyncResult> box(Guide::BeginShowMessageBox(
        "T", "B", {"A", "B"}, 0, MessageBoxIcon::None, callback, {}));
    frames.answer = [] { Guide::SimulateMessageBoxClickEXT(1); };
    EXPECT_EQ(std::optional<int>(1), Guide::EndShowMessageBox(box.get()));
    EXPECT_EQ(1, callbacks);
    EXPECT_EQ(3, frames.frames);
    frames.frames = 0;
    frames.answerOn = 2;
    frames.answer = [] { PressEnter(); };
    std::unique_ptr<System::IAsyncResult> input(Guide::BeginShowKeyboardInput(
        PlayerIndex::One, "", "", "answer", [&](System::IAsyncResult& result) {
            ++callbacks;
            EXPECT_THROW((void)Guide::EndShowKeyboardInput(&result), System::InvalidOperationException);
        }, {}));
    EXPECT_EQ("answer", Guide::EndShowKeyboardInput(input.get()));
    EXPECT_EQ(2, callbacks);
    EXPECT_EQ(2, frames.frames);
}

TEST(GuideTest, EndFollowsTheReferenceChecks) {
    MessageBoxGuard boxes;
    KeyboardInputGuard keyboards;
    EXPECT_THROW((void)Guide::EndShowMessageBox(nullptr), System::ArgumentNullException);
    EXPECT_THROW((void)Guide::EndShowKeyboardInput(nullptr), System::ArgumentNullException);
    auto box = ShowTwoButtonBox();
    EXPECT_THROW((void)Guide::EndShowKeyboardInput(box.get()), System::ArgumentException);
    Guide::SimulateMessageBoxClickEXT(1);
    EXPECT_EQ(1, Guide::EndShowMessageBox(box.get()));
    EXPECT_THROW((void)Guide::EndShowMessageBox(box.get()), System::InvalidOperationException);

    std::unique_ptr<System::IAsyncResult> input(Guide::BeginShowKeyboardInput(
        PlayerIndex::One, "title", "description", "", System::AsyncCallback{}, std::any{}));
    EXPECT_THROW((void)Guide::EndShowMessageBox(input.get()), System::ArgumentException);
    TypeUtf16(u"Hi");
    PressEnter();
    EXPECT_EQ("Hi", Guide::EndShowKeyboardInput(input.get()));
    EXPECT_THROW((void)Guide::EndShowKeyboardInput(input.get()), System::InvalidOperationException);
}

// Reference End waits for the user's answer. CNA's Guide is drawn by the game, so the wait runs the
// game's modal frames until the answer arrives: EndShowMessageBox(BeginShowMessageBox(...)) works
// as a single call, as it does on the console.
TEST(GuideTest, EndWaitsForTheAnswerWhileTheGamesModalFramesRun) {
    MessageBoxGuard boxes;
    KeyboardInputGuard keyboards;
    ScriptedModalFrames frames;
    ModalFramesGuard installed(&frames);
    frames.answer = [] { Guide::SimulateMessageBoxClickEXT(1); };
    EXPECT_EQ(1, Guide::EndShowMessageBox(ShowTwoButtonBox().get()));
    EXPECT_EQ(3, frames.frames);

    frames.frames = 0;
    frames.answerOn = 2;
    frames.answer = [] { TypeUtf16(u"Typed"); PressEnter(); };
    std::unique_ptr<System::IAsyncResult> input(Guide::BeginShowKeyboardInput(
        PlayerIndex::One, "title", "description", "", System::AsyncCallback{}, std::any{}));
    EXPECT_EQ("Typed", Guide::EndShowKeyboardInput(input.get()));
    EXPECT_EQ(2, frames.frames);
}

// With no running game nothing could answer the Guide, so End refuses rather than hang, and stays
// callable for when the answer comes.
TEST(GuideTest, EndWithoutARunningGameRefusesAndCanBeCalledAgainOnceAnswered) {
    MessageBoxGuard boxes;
    ModalFramesGuard none(nullptr);
    auto box = ShowTwoButtonBox();
    if (CNA::Internal::GamerServices::guideModalFrames() != nullptr) GTEST_SKIP() << "a game is alive in this process";
    EXPECT_THROW((void)Guide::EndShowMessageBox(box.get()), System::InvalidOperationException);
    Guide::SimulateMessageBoxClickEXT(0);
    EXPECT_EQ(0, Guide::EndShowMessageBox(box.get()));
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

namespace
{
    /** @brief A platform whose mouse a test scripts, so a real click can drive the overlay. */
    class CannedMousePlatform final : public CNA::Platform::Testing::PlatformTestDecorator
    {
    public:
        [[nodiscard]] CNA::Platform::IPlatformMouse* GetMouse() override { return &mouse_; }

        /** @brief Publishes a mouse state. @param x Client x. @param y Client y. @param down Left held. */
        void Press(const int x, const int y, const bool down)
        {
            CNA::Platform::MouseSnapshot snapshot;
            snapshot.x = x;
            snapshot.y = y;
            snapshot.buttons = down ? std::uint8_t{1} : std::uint8_t{0};
            mouse_.SetPending(snapshot);
            mouse_.Update();
        }

    private:
        CNA::Platform::Testing::CannedMouse mouse_;
    };
}

// SAMPLE-065: the click that answers the box is a press AND a release. Lifting the touch withhold
// on the press let the release arrive as a tap on whatever the box had been covering: in
// NinjAcademy that was the loading screen behind it, which then started a second content load and
// displayed a game that had already consumed the saved state, so "resume" showed a score of 0.
TEST(GuideTest, TheClickThatAnswersAMessageBoxDoesNotAlsoReachTheGame) {
    using namespace Microsoft::Xna::Framework;
    using namespace Microsoft::Xna::Framework::Graphics;
    using Microsoft::Xna::Framework::Input::Touch::GestureSample;
    using Microsoft::Xna::Framework::Input::Touch::GestureType;
    using Microsoft::Xna::Framework::Input::Touch::TouchPanel;

    MessageBoxGuard guard;
    // XNA refuses gesture reads until EnabledGestures has been assigned (RV-INPUT-4).
    TouchPanel::setEnabledGesturesProperty(TouchPanel::getEnabledGesturesProperty());
    GraphicsDevice device;
    SpriteBatch spriteBatch(device);
    auto font = MakeSimpleTestFont(device);
    Texture2D whitePixel = MakeWhitePixelTexture(device);

    CannedMousePlatform platform;
    CNA::Platform::Testing::ScopedCurrentPlatform installed(platform);

    TouchPanel::setInputSuppressedEXT(false);
    System::IAsyncResult* result = Guide::BeginShowMessageBox(
        "Load game", "Saved game data detected. Load it?", std::vector<std::string>{"Yes", "No"},
        0, MessageBoxIcon::None, System::AsyncCallback{}, std::any{}
    );

    const auto render = [&]
    {
        spriteBatch.Begin();
        Guide::RenderPendingMessageBoxEXT(device, spriteBatch, *font, whitePixel);
        spriteBatch.End();
    };

    // The box sizes itself to its own text, so find a button by clicking rather than by
    // reproducing that arithmetic here. Each attempt releases first, because the overlay answers
    // on the press edge. Columns go from the middle outward: the box is centred, so the first
    // column normally answers, and a whole-viewport sweep (two renders per probe) took a Debug
    // SOFTWARE build past the test timeout (Apple M4 AM4-180).
    const int width = device.getViewportProperty().getWidthProperty();
    const int height = device.getViewportProperty().getHeightProperty();
    std::vector<int> columns;
    for (int offset = 0; offset < width; offset += 4)
    {
        if (width / 2 + offset < width) columns.push_back(width / 2 + offset);
        if (offset > 0 && width / 2 - offset >= 0) columns.push_back(width / 2 - offset);
    }
    for (const int x : columns)
    {
        for (int y = 0; y < height && Guide::getHasPendingMessageBoxEXTProperty(); y += 4)
        {
            platform.Press(x, y, false);
            render();
            platform.Press(x, y, true);
            render();
        }
    }
    ASSERT_FALSE(Guide::getHasPendingMessageBoxEXTProperty())
        << "no position in the viewport answered the message box";

    // The button is still held: the rest of that click belongs to the overlay, not the game.
    EXPECT_TRUE(TouchPanel::getInputSuppressedEXT());
    TouchPanel::EnqueueGesture(GestureSample(
        GestureType::Tap, System::TimeSpan::Zero, Vector2(10.0f, 10.0f), Vector2::Zero,
        Vector2::Zero, Vector2::Zero));
    EXPECT_FALSE(TouchPanel::getIsGestureAvailableProperty());

    // Releasing it hands input back on the next frame the game pumps the overlay.
    platform.Press(0, 0, false);
    render();
    EXPECT_FALSE(TouchPanel::getInputSuppressedEXT());
    TouchPanel::EnqueueGesture(GestureSample(
        GestureType::Tap, System::TimeSpan::Zero, Vector2(10.0f, 10.0f), Vector2::Zero,
        Vector2::Zero, Vector2::Zero));
    EXPECT_TRUE(TouchPanel::getIsGestureAvailableProperty());
    (void)TouchPanel::ReadGesture();

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

namespace {
    // Serves a keyboard whose held keys the test sets; everything else is the real platform.
    class CannedKeyboard final : public CNA::Platform::IPlatformKeyboard {
    public:
        CNA::Platform::KeyboardSnapshot snapshot;
        void Update() override {}
        [[nodiscard]] const CNA::Platform::KeyboardSnapshot& GetSnapshot() const override { return snapshot; }
        [[nodiscard]] bool HasKeyboard() const override { return true; }
    };
    class CannedKeyboardPlatform final : public CNA::Platform::Testing::PlatformTestDecorator {
    public:
        CannedKeyboard keyboard;
        [[nodiscard]] CNA::Platform::IPlatformKeyboard* GetKeyboard() override { return &keyboard; }
    };
}

// A keyboard answers a message box the way the console Guide is answered: the key that is already
// down when the box opens does nothing, arrows move the focus, Enter chooses, Escape cancels.
TEST(GuideTest, MessageBoxesAnswerToTheKeyboard) {
    using namespace Microsoft::Xna::Framework;
    using namespace Microsoft::Xna::Framework::Graphics;
    using CNA::Platform::KeyCode;

    MessageBoxGuard guard;
    CannedKeyboardPlatform platform;
    CNA::Platform::Testing::ScopedCurrentPlatform scope(platform);
    GraphicsDevice device;
    SpriteBatch spriteBatch(device);
    auto font = MakeSimpleTestFont(device);
    Texture2D whitePixel = MakeWhitePixelTexture(device);
    auto frame = [&](std::vector<KeyCode> keys) {
        platform.keyboard.snapshot.pressedKeys = std::move(keys);
        spriteBatch.Begin();
        Guide::RenderPendingMessageBoxEXT(device, spriteBatch, *font, whitePixel);
        spriteBatch.End();
    };

    System::IAsyncResult* result = Guide::BeginShowMessageBox(
        "title", "text", std::vector<std::string>{"A", "B", "C"}, 0, MessageBoxIcon::None, System::AsyncCallback{}, std::any{});
    frame({KeyCode::Enter});
    EXPECT_FALSE(result->getIsCompletedProperty());
    frame({});
    frame({KeyCode::Right});
    frame({KeyCode::Right});
    EXPECT_EQ(1, Guide::GetPendingMessageBoxFocusButtonForTestingEXT());
    frame({});
    frame({KeyCode::Left});
    frame({});
    frame({KeyCode::Left});
    EXPECT_EQ(2, Guide::GetPendingMessageBoxFocusButtonForTestingEXT());
    frame({KeyCode::Enter});
    ASSERT_TRUE(result->getIsCompletedProperty());
    EXPECT_EQ(std::optional<int>(2), Guide::EndShowMessageBox(result));
    delete result;

    result = Guide::BeginShowMessageBox(
        "title", "text", std::vector<std::string>{"OK"}, 0, MessageBoxIcon::None, System::AsyncCallback{}, std::any{});
    frame({});
    frame({KeyCode::Escape});
    ASSERT_TRUE(result->getIsCompletedProperty());
    EXPECT_EQ(std::nullopt, Guide::EndShowMessageBox(result));
    delete result;
}

// Apple M4 AM4-090: the Guide primes its input -- every key already down counts as
// held -- when something new appears on top, so the Enter that opened a message box cannot also
// answer it. AM4-086 judged "new" by the box's text, so a box identical to the previous one was not
// new: opened while Enter was down (the game's own Enter raising it, say), it took that Enter as a
// fresh press and answered itself. The regression first showed only when an earlier test in the
// same process had left a box with the same text; this pins it directly.
TEST(GuideTest, AnIdenticalMessageBoxOpenedWhileEnterIsDownDoesNotAnswerItself) {
    using namespace Microsoft::Xna::Framework;
    using namespace Microsoft::Xna::Framework::Graphics;
    using CNA::Platform::KeyCode;

    MessageBoxGuard guard;
    CannedKeyboardPlatform platform;
    CNA::Platform::Testing::ScopedCurrentPlatform scope(platform);
    GraphicsDevice device;
    SpriteBatch spriteBatch(device);
    auto font = MakeSimpleTestFont(device);
    Texture2D whitePixel = MakeWhitePixelTexture(device);
    auto frame = [&](std::vector<KeyCode> keys) {
        platform.keyboard.snapshot.pressedKeys = std::move(keys);
        spriteBatch.Begin();
        Guide::RenderPendingMessageBoxEXT(device, spriteBatch, *font, whitePixel);
        spriteBatch.End();
    };
    const auto show = [] {
        return Guide::BeginShowMessageBox("Same", "Same text", std::vector<std::string>{"OK"}, 0,
                                          MessageBoxIcon::None, System::AsyncCallback{}, std::any{});
    };

    System::IAsyncResult* first = show();
    frame({});
    frame({KeyCode::Escape});
    ASSERT_TRUE(first->getIsCompletedProperty()) << "Escape cancels the first box";
    EXPECT_EQ(std::nullopt, Guide::EndShowMessageBox(first));
    delete first;

    System::IAsyncResult* second = show();
    frame({KeyCode::Enter});   // Enter was already down when the identical box opened
    EXPECT_FALSE(second->getIsCompletedProperty())
        << "the Enter that was down when the box opened must not answer it";
    frame({});
    frame({KeyCode::Enter});
    ASSERT_TRUE(second->getIsCompletedProperty()) << "a new press answers it";
    EXPECT_EQ(std::optional<int>(0), Guide::EndShowMessageBox(second));
    delete second;
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

// Reentrancy regression: a callback that calls BeginShowMessageBox again must see
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

// Without a configured service, argument validation still runs first (reference order) and every
// service-backed pane refuses rather than silently doing nothing.
TEST(GuideTest, ShowMethodsValidateThenRequireTheService) {
    if (GamerServicesDispatcher::getIsInitializedProperty())
        GTEST_SKIP() << "another test initialized gamer services in this process";
    EXPECT_THROW(Guide::ShowComposeMessage(PlayerIndex::One, std::string(256, 'a'), {}), System::ArgumentException);
    EXPECT_THROW(Guide::ShowComposeMessage(PlayerIndex::One, "hi", {nullptr}), System::ArgumentException);
    EXPECT_THROW(Guide::ShowComposeMessage(PlayerIndex::One, "hi", {}), GamerServicesNotAvailableException);
    EXPECT_THROW(Guide::ShowFriendRequest(PlayerIndex::One, nullptr), System::ArgumentNullException);
    EXPECT_THROW(Guide::ShowFriends(PlayerIndex::One), GamerServicesNotAvailableException);
    // Invitations need the configured service; the session-ID overload is Windows Phone only.
    EXPECT_THROW(Guide::ShowGameInvite(PlayerIndex::One, std::vector<Gamer*>{}), GamerServicesNotAvailableException);
    EXPECT_THROW(Guide::ShowGameInvite(PlayerIndex::One, std::vector<Gamer*>{nullptr}), System::ArgumentException);
    EXPECT_THROW(Guide::ShowGameInvite(std::string("session-id")), System::NotSupportedException);
    EXPECT_THROW(Guide::ShowGamerCard(PlayerIndex::One, nullptr), System::ArgumentNullException);
    EXPECT_THROW(Guide::ShowMarketplace(PlayerIndex::One), GamerPrivilegeException);
    EXPECT_THROW(Guide::ShowMessages(PlayerIndex::One), GamerServicesNotAvailableException);
    EXPECT_THROW(Guide::ShowParty(PlayerIndex::One), GamerServicesNotAvailableException);
    EXPECT_THROW(Guide::ShowPartySessions(PlayerIndex::One), GamerServicesNotAvailableException);
    EXPECT_THROW(Guide::ShowPlayerReview(PlayerIndex::One, nullptr), System::ArgumentNullException);
    EXPECT_THROW(Guide::ShowPlayers(PlayerIndex::One), GamerServicesNotAvailableException);
    EXPECT_THROW(Guide::ShowSignIn(1, false), GamerServicesNotAvailableException);
    EXPECT_THROW(Guide::ShowAchievementsEXT(PlayerIndex::One), GamerServicesNotAvailableException);
    EXPECT_FALSE(CNA::Internal::GamerServices::guideIsVisible());
}

TEST(GuideTest, KeyboardCallbackCanEndAndBeginAgainWithoutLeakingInput) {
    // Guide.hpp BeginShowKeyboardInput callback/state contract: result is complete before callback; a completed prompt
    // releases its modal slot. CNA's text-input seam must then belong only to the new prompt.
    KeyboardInputGuard guard;
    int callbacks = 0;
    std::unique_ptr<System::IAsyncResult> second;
    std::unique_ptr<System::IAsyncResult> first(Guide::BeginShowKeyboardInput(
        PlayerIndex::One, "first", "", "one", [&](System::IAsyncResult& result) {
            ++callbacks;
            EXPECT_TRUE(result.getIsCompletedProperty());
            EXPECT_FALSE(result.getCompletedSynchronouslyProperty());
            EXPECT_EQ(std::any_cast<int>(result.getAsyncStateProperty()), 73);
            EXPECT_FALSE(Guide::getHasPendingKeyboardInputEXTProperty());
            EXPECT_EQ(Guide::EndShowKeyboardInput(&result), "one");
            second.reset(Guide::BeginShowKeyboardInput(PlayerIndex::Two, "second", "", "two", {}, {}));
        }, 73));
    PressEnter();
    ASSERT_EQ(callbacks, 1);
    ASSERT_NE(second, nullptr);
    EXPECT_FALSE(second->getIsCompletedProperty());
    EXPECT_TRUE(Guide::getHasPendingKeyboardInputEXTProperty());
    EXPECT_EQ(Guide::GetPendingKeyboardInputTitleForTestingEXT(), "second");
    TypeUtf16(u"!");
    PressEnter();
    EXPECT_EQ(Guide::EndShowKeyboardInput(second.get()), "two!");
    EXPECT_EQ(callbacks, 1);
    EXPECT_FALSE(Guide::getHasPendingKeyboardInputEXTProperty());
    EXPECT_THROW((void)Guide::EndShowKeyboardInput(first.get()), System::InvalidOperationException);
}

TEST(GuideTest, ThrowingMessageBoxCallbackLeavesCompletedAnswerAndReleasesModalSlot) {
    // Guide.hpp BeginShowMessageBox: callback runs on completion. A callback exception
    // cannot undo the already completed answer or retain the single active prompt.
    MessageBoxGuard guard;
    int callbacks = 0;
    std::unique_ptr<System::IAsyncResult> result(Guide::BeginShowMessageBox(
        "title", "text", {"yes", "no"}, 0, MessageBoxIcon::None,
        [&](System::IAsyncResult& completed) {
            ++callbacks;
            EXPECT_TRUE(completed.getIsCompletedProperty());
            EXPECT_FALSE(Guide::getHasPendingMessageBoxEXTProperty());
            throw std::runtime_error("callback failure");
        }, {}));
    EXPECT_THROW(Guide::SimulateMessageBoxClickEXT(1), std::runtime_error);
    EXPECT_EQ(callbacks, 1);
    EXPECT_TRUE(result->getIsCompletedProperty());
    EXPECT_EQ(Guide::EndShowMessageBox(result.get()), std::optional<int>(1));
    auto next = ShowTwoButtonBox();
    EXPECT_FALSE(next->getIsCompletedProperty());
    Guide::SimulateMessageBoxClickEXT(0);
    EXPECT_EQ(Guide::EndShowMessageBox(next.get()), std::optional<int>(0));
    EXPECT_EQ(callbacks, 1);
}

namespace {
template<class Exception, class Call>
void ExpectGuideParameter(Call call, const std::string& parameter) {
    try {
        call();
        FAIL() << "Expected an argument error for " << parameter;
    } catch (const Exception& error) {
        EXPECT_EQ(parameter, error.getParamNameProperty());
    }
}
}


namespace {
// XNA Refresh FrameworkResources; the suffix is SharpRuntime's documented host format.
// Comparing typeid also rejects a derived ArgumentNull/OutOfRangeException where XNA uses ArgumentException.
template<class Exception, class Call>
void ExpectRefreshArgument(Call call, const char* parameter, const char* message) {
    try { call(); FAIL() << "Expected " << parameter; }
    catch (const Exception& error) {
        EXPECT_EQ(typeid(Exception), typeid(error));
        EXPECT_EQ(parameter, error.getParamNameProperty());
        const std::string suffix = System::detail::UseNetFrameworkArgumentExceptionMessages()
            ? std::string("\nParameter name: ") + parameter
            : std::string(" (Parameter '") + parameter + "')";
        EXPECT_EQ(std::string(message) + suffix, error.getMessageProperty());
    }
}
}

TEST(GuideTest, RefreshMessageBoxDiagnosticsAndPrecedence) {
    // ValidateShowMessageBoxArgs IL_000e..018e; resource keys StringNullEmptyOrTooLong,
    // StringCollectionInvalid. Invalid arguments win over the already-visible refusal.
    MessageBoxGuard guard;
    auto pending = ShowTwoButtonBox();
    const auto show = [](const std::string& title, const std::string& text,
                         const std::vector<std::string>& buttons, int focus) {
        std::unique_ptr<System::IAsyncResult> result(Guide::BeginShowMessageBox(
            title, text, buttons, focus, MessageBoxIcon::None, {}, {}));
    };
    const char* stringMessage = "This string cannot be null or empty, and must be less than 256 characters long.";
    const char* buttonsMessage = "The argument is invalid. It must contain between 1 and 3 strings. The strings cannot be null or empty, and must be less than 256 characters long.";
    for (const std::string& bad : {std::string{}, std::string(256, 'x')}) {
        ExpectRefreshArgument<System::ArgumentException>(
            [&] { show(bad, "", {}, -1); }, "title", stringMessage);
        ExpectRefreshArgument<System::ArgumentException>(
            [&] { show("T", bad, {}, -1); }, "text", stringMessage);
    }
    for (const auto& buttons : std::vector<std::vector<std::string>>{
             {}, {"A", "B", "C", "D"}, {"A", ""}, {"A", std::string(256, 'x')}}) {
        ExpectRefreshArgument<System::ArgumentException>(
            [&] { show("T", "B", buttons, -1); }, "buttons", buttonsMessage);
    }
    for (int focus : {-1, 2})
        ExpectRefreshArgument<System::ArgumentOutOfRangeException>(
            [&] { show("T", "B", {"A", "B"}, focus); }, "focusButton",
            "Specified argument was out of the range of valid values.");
    EXPECT_FALSE(pending->getIsCompletedProperty());
    EXPECT_EQ(0, Guide::GetPendingMessageBoxFocusButtonForTestingEXT());
    Guide::SimulateMessageBoxClickEXT(1);
    EXPECT_EQ(std::optional<int>(1), Guide::EndShowMessageBox(pending.get()));
}

TEST(GuideTest, RefreshKeyboardDiagnosticsCoverBothOverloads) {
    // BeginShowKeyboardInput IL_002c..00ee; six-argument overload IL_0000..000e passes false.
    MessageBoxGuard boxes;
    KeyboardInputGuard keyboards;
    auto pending = ShowTwoButtonBox();
    const std::string longText(256, 'x');
    const char* fields[] = {"title", "description", "defaultText"};
    for (int overload = 0; overload != 3; ++overload) {
        for (int field = 0; field != 3; ++field) {
            SCOPED_TRACE(overload);
            SCOPED_TRACE(field);
            const auto show = [&] {
                const std::string title = field == 0 ? longText : "";
                const std::string description = field <= 1 ? longText : "";
                std::unique_ptr<System::IAsyncResult> result(overload == 0
                    ? Guide::BeginShowKeyboardInput(PlayerIndex::One, title, description, longText, {}, {})
                    : Guide::BeginShowKeyboardInput(PlayerIndex::One, title, description, longText, {}, {}, overload == 2));
            };
            ExpectRefreshArgument<System::ArgumentException>(show, fields[field], "This string must be less than 256 characters long.");
        }
    }
    EXPECT_FALSE(pending->getIsCompletedProperty());
    Guide::SimulateMessageBoxClickEXT(0);
    EXPECT_EQ(std::optional<int>(0), Guide::EndShowMessageBox(pending.get()));
}

TEST(GuideTest, RefreshVisibleRefusalHasActionableMessageForBothDialogKinds) {
    // ErrorHandler.GetExceptionFromResult IL_0105..010f: GamerServicesGuideAlreadyVisible.
    MessageBoxGuard boxes;
    KeyboardInputGuard keyboards;
    std::unique_ptr<System::IAsyncResult> pending(Guide::BeginShowKeyboardInput(
        PlayerIndex::One, "", "", "secret", {}, {}, true));
    for (const auto& show : std::vector<std::function<void()>>{
             [] { (void)Guide::BeginShowMessageBox("T", "B", {"OK"}, 0, MessageBoxIcon::None, {}, {}); },
             [] { (void)Guide::BeginShowKeyboardInput(PlayerIndex::One, "", "", "", {}, {}); }}) {
        try { show(); FAIL() << "Expected GuideAlreadyVisibleException"; }
        catch (const GuideAlreadyVisibleException& error) {
            EXPECT_EQ(typeid(GuideAlreadyVisibleException), typeid(error));
            EXPECT_EQ("The function cannot be completed at this time: the Guide UI is already active. Wait until Guide.IsVisible is false before issuing this call.", error.getMessageProperty());
        }
    }
    EXPECT_FALSE(pending->getIsCompletedProperty());
    Guide::SimulateKeyboardInputCancelEXT();
    EXPECT_EQ("", Guide::EndShowKeyboardInput(pending.get()));
}

TEST(GuideTest, SignInPaneCountsValidateBeforeTheVisibleGuideIsRefused) {
    // XNA Guide.ShowSignIn IL_0000..001b accepts exactly 1, 2, 4 and names paneCount.
    MessageBoxGuard boxes;
    auto pending = ShowTwoButtonBox();
    for (bool onlineOnly : {false, true}) {
        for (int panes : {-1, 0, 3, 5}) {
            ExpectGuideParameter<System::ArgumentException>(
                [&] { Guide::ShowSignIn(panes, onlineOnly); }, "paneCount");
        }
        for (int panes : {1, 2, 4})
            EXPECT_THROW(Guide::ShowSignIn(panes, onlineOnly), GuideAlreadyVisibleException);
    }
    EXPECT_FALSE(pending->getIsCompletedProperty());
    Guide::SimulateMessageBoxClickEXT(0);
    EXPECT_EQ(0, Guide::EndShowMessageBox(pending.get()));
}


TEST(GuideTest, SignInInvalidPaneDiagnosticMatchesRefresh) {
    // ShowSignIn IL_0000..001b validates before dispatch. Resource ShowSignInPaneCountInvalid.
    for (bool onlineOnly : {false, true}) {
        for (int count : {-1, 0, 3, 5}) {
            try { Guide::ShowSignIn(count, onlineOnly); FAIL() << "Expected paneCount refusal"; }
            catch (const System::ArgumentException& error) {
                EXPECT_EQ(typeid(System::ArgumentException), typeid(error));
                EXPECT_EQ("paneCount", error.getParamNameProperty());
                const char* suffix = System::detail::UseNetFrameworkArgumentExceptionMessages()
                    ? "\nParameter name: paneCount" : " (Parameter 'paneCount')";
                EXPECT_EQ(std::string("The paneCount argument is invalid. The valid options for this parameter are 1, 2, and 4.") + suffix, error.getMessageProperty());
            }
        }
    }
}

TEST(GuideTest, GamerCardInvalidPlayerPrecedesNullGamerAndVisibleGuide) {
    // ExecuteKernelCall(callType, player, gamer) IL_0000..0033: player, null, disposed.
    // Restrict this regression to undefined players; the established 4-player extension is unchanged.
    MessageBoxGuard boxes;
    auto pending = ShowTwoButtonBox();
    for (int player : {-1, 4}) {
        try { Guide::ShowGamerCard(static_cast<PlayerIndex>(player), nullptr); FAIL() << "Expected player refusal"; }
        catch (const System::ArgumentOutOfRangeException& error) {
            EXPECT_EQ("player", error.getParamNameProperty());
        }
    }
    EXPECT_FALSE(pending->getIsCompletedProperty());
    Guide::SimulateMessageBoxClickEXT(0);
    EXPECT_EQ(std::optional<int>(0), Guide::EndShowMessageBox(pending.get()));
}

TEST(GuideTest, DisposedGamerCardUsesRefreshDiagnosticBeforeVisibility) {
    // ExecuteKernelCall(player,gamer) IL_001c..0033: ObjectDisposedException("gamer", GamerInvalid).
    MessageBoxGuard boxes;
    auto pending = ShowTwoButtonBox();
    auto gamer = FriendGamer::CreateInternal("disposed", "Disposed", false, false, false, false, false, false);
    CNA::Internal::GamerServices::GamerAccess::dispose(gamer);
    try { Guide::ShowGamerCard(PlayerIndex::One, &gamer); FAIL() << "Expected disposed gamer refusal"; }
    catch (const System::ObjectDisposedException& error) {
        EXPECT_EQ("gamer", error.getObjectNameProperty());
        EXPECT_EQ("This Gamer is no longer valid. The profile may have been signed out.\nObject name: 'gamer'.", error.getMessageProperty());
    }
    EXPECT_FALSE(pending->getIsCompletedProperty());
    Guide::SimulateMessageBoxClickEXT(0);
    EXPECT_EQ(std::optional<int>(0), Guide::EndShowMessageBox(pending.get()));
}

TEST(GuideTest, SignInAndGamerCardVisibleDiagnosticsMatchRefresh) {
    // ErrorHandler.GetExceptionFromResult IL_0105..010f: GamerServicesGuideAlreadyVisible.
    MessageBoxGuard boxes;
    auto pending = ShowTwoButtonBox();
    auto gamer = FriendGamer::CreateInternal("friend", "Friend", false, false, false, false, false, false);
    for (const auto& show : std::vector<std::function<void()>>{
             [] { Guide::ShowSignIn(1, false); },
             [&] { Guide::ShowGamerCard(PlayerIndex::One, &gamer); }}) {
        try { show(); FAIL() << "Expected visible Guide refusal"; }
        catch (const GuideAlreadyVisibleException& error) {
            EXPECT_EQ("The function cannot be completed at this time: the Guide UI is already active. Wait until Guide.IsVisible is false before issuing this call.", error.getMessageProperty());
        }
    }
    EXPECT_FALSE(pending->getIsCompletedProperty());
    Guide::SimulateMessageBoxClickEXT(1);
    EXPECT_EQ(std::optional<int>(1), Guide::EndShowMessageBox(pending.get()));
}

TEST(GuideTest, SocialArgumentsValidateBeforeTheVisibleGuideIsRefused) {
    // XNA ShowComposeMessage IL_0018..0065 validates text then recipients;
    // Gamer.ValidateGamerList rejects >100 recipients or null elements. Calls with a
    // gamer use ExecuteKernelCall(player,gamer), IL_000e..0033 for the null guard.
    MessageBoxGuard boxes;
    auto pending = ShowTwoButtonBox();
    auto gamer = FriendGamer::CreateInternal("friend", "Friend", false, false, false, false, false, false);
    const std::vector<Gamer*> hundred(100, &gamer), hundredAndOne(101, &gamer);
    ExpectGuideParameter<System::ArgumentException>(
        [&] { Guide::ShowComposeMessage(PlayerIndex::One, std::string(256, 'x'), {nullptr}); }, "text");
    ExpectGuideParameter<System::ArgumentException>(
        [&] { Guide::ShowComposeMessage(PlayerIndex::One, "", hundredAndOne); }, "recipients");
    ExpectGuideParameter<System::ArgumentException>(
        [&] { Guide::ShowGameInvite(PlayerIndex::One, hundredAndOne); }, "recipients");
    ExpectGuideParameter<System::ArgumentException>(
        [&] { Guide::ShowGameInvite(PlayerIndex::One, {nullptr}); }, "recipients");
    EXPECT_THROW(Guide::ShowComposeMessage(PlayerIndex::One, std::string(255, 'x'), hundred), GuideAlreadyVisibleException);
    EXPECT_THROW(Guide::ShowGameInvite(PlayerIndex::One, hundred), GuideAlreadyVisibleException);
    for (const auto& show : std::vector<std::function<void()>>{
             [&] { Guide::ShowGamerCard(PlayerIndex::One, nullptr); },
             [&] { Guide::ShowFriendRequest(PlayerIndex::One, nullptr); },
             [&] { Guide::ShowPlayerReview(PlayerIndex::One, nullptr); }})
        ExpectGuideParameter<System::ArgumentNullException>(show, "gamer");
    for (const auto& show : std::vector<std::function<void()>>{
             [&] { Guide::ShowGamerCard(PlayerIndex::One, &gamer); },
             [&] { Guide::ShowFriendRequest(PlayerIndex::One, &gamer); },
             [&] { Guide::ShowPlayerReview(PlayerIndex::One, &gamer); },
             [&] { Guide::ShowFriends(PlayerIndex::One); },
             [&] { Guide::ShowMessages(PlayerIndex::One); },
             [&] { Guide::ShowPlayers(PlayerIndex::One); }})
        EXPECT_THROW(show(), GuideAlreadyVisibleException);
    EXPECT_FALSE(pending->getIsCompletedProperty());
    Guide::SimulateMessageBoxClickEXT(0);
    EXPECT_EQ(0, Guide::EndShowMessageBox(pending.get()));
}

TEST(GuideTest, ValidationOrderAndParameterNamesSurviveAnAlreadyVisibleGuide) {
    // XNA ValidateShowMessageBoxArgs IL_0000..018e: player, title, text,
    // buttons, focusButton; BeginShowKeyboardInput IL_0000..00ee: player,
    // title, description, defaultText. These checks precede dispatch/visibility refusal.
    MessageBoxGuard boxes;
    KeyboardInputGuard keyboards;
    auto pending = ShowTwoButtonBox();
    int callbacks = 0;
    auto show = [&](PlayerIndex player, const std::string& title, const std::string& text,
                    const std::vector<std::string>& buttons, int focus) {
        std::unique_ptr<System::IAsyncResult> unexpected(Guide::BeginShowMessageBox(
            player, title, text, buttons, focus, MessageBoxIcon::None,
            [&](auto&) { ++callbacks; }, {}));
    };
    ExpectGuideParameter<System::ArgumentOutOfRangeException>(
        [&] { show(static_cast<PlayerIndex>(-1), "", "", {}, -1); }, "player");
    ExpectGuideParameter<System::ArgumentException>(
        [&] { show(PlayerIndex::One, "", "", {}, -1); }, "title");
    ExpectGuideParameter<System::ArgumentException>(
        [&] { show(PlayerIndex::One, "title", "", {}, -1); }, "text");
    ExpectGuideParameter<System::ArgumentException>(
        [&] { show(PlayerIndex::One, "title", "text", {}, -1); }, "buttons");
    ExpectGuideParameter<System::ArgumentOutOfRangeException>(
        [&] { show(PlayerIndex::One, "title", "text", {"OK"}, -1); }, "focusButton");
    EXPECT_THROW(show(PlayerIndex::One, "title", "text", {"OK"}, 0), GuideAlreadyVisibleException);
    const std::string longText(256, 'x');
    auto keyboard = [&](PlayerIndex player, const std::string& title, const std::string& description,
                        const std::string& text) {
        std::unique_ptr<System::IAsyncResult> unexpected(Guide::BeginShowKeyboardInput(
            player, title, description, text, [&](auto&) { ++callbacks; }, {}));
    };
    ExpectGuideParameter<System::ArgumentOutOfRangeException>(
        [&] { keyboard(static_cast<PlayerIndex>(4), longText, longText, longText); }, "player");
    ExpectGuideParameter<System::ArgumentException>(
        [&] { keyboard(PlayerIndex::One, longText, longText, longText); }, "title");
    ExpectGuideParameter<System::ArgumentException>(
        [&] { keyboard(PlayerIndex::One, "", longText, longText); }, "description");
    ExpectGuideParameter<System::ArgumentException>(
        [&] { keyboard(PlayerIndex::One, "", "", longText); }, "defaultText");
    EXPECT_THROW(keyboard(PlayerIndex::One, "", "", ""), GuideAlreadyVisibleException);
    EXPECT_EQ(0, callbacks);
    EXPECT_FALSE(pending->getIsCompletedProperty());
    Guide::SimulateMessageBoxClickEXT(1);
    EXPECT_EQ(1, Guide::EndShowMessageBox(pending.get()));
}


TEST(GuideTest, EmbeddedNulCountsInEveryDialogLengthCheck) {
    // XNA Refresh String.Length includes U+0000: ValidateShowMessageBoxArgs IL_0016..0021,
    // IL_005f..006a, IL_00ca..00d5; BeginShowKeyboardInput IL_002c..00ee.
    // 'x' + NUL + 253 'x' = 255 units; one more 'x' = 256, regardless of C-string termination.
    MessageBoxGuard boxes;
    KeyboardInputGuard keyboards;
    const std::string boundary = std::string("x\0", 2) + std::string(253, 'x');
    const std::string tooLong = boundary + 'x';
    for (int field = 0; field != 3; ++field) {
        SCOPED_TRACE(field);
        const auto box = [&](const std::string& value) {
            return std::unique_ptr<System::IAsyncResult>(Guide::BeginShowMessageBox(
                field == 0 ? value : "T", field == 1 ? value : "B",
                {field == 2 ? value : "OK"}, 0, MessageBoxIcon::None, {}, {}));
        };
        std::unique_ptr<System::IAsyncResult> rejectedBox;
        EXPECT_THROW(rejectedBox = box(tooLong), System::ArgumentException);
        // Reset before destroying any unexpectedly accepted operation (including mutation runs).
        Guide::ResetPendingMessageBoxForTestingEXT();
        auto accepted = box(boundary);
        ASSERT_NE(nullptr, accepted);
        EXPECT_FALSE(accepted->getIsCompletedProperty());
        Guide::SimulateMessageBoxClickEXT(0);
        EXPECT_EQ(std::optional<int>(0), Guide::EndShowMessageBox(accepted.get()));
        for (bool password : {false, true}) {
            const auto keyboard = [&](const std::string& value) {
                return std::unique_ptr<System::IAsyncResult>(Guide::BeginShowKeyboardInput(
                    PlayerIndex::One, field == 0 ? value : "", field == 1 ? value : "",
                    field == 2 ? value : "", {}, {}, password));
            };
            // Keep ownership until reset: the Guide borrows the returned operation.
            std::unique_ptr<System::IAsyncResult> rejected;
            EXPECT_THROW(rejected = keyboard(tooLong), System::ArgumentException);
            Guide::ResetPendingKeyboardInputForTestingEXT();
            auto input = keyboard(boundary);
            ASSERT_NE(nullptr, input);
            EXPECT_FALSE(input->getIsCompletedProperty());
            Guide::SimulateKeyboardInputCancelEXT();
            EXPECT_EQ("", Guide::EndShowKeyboardInput(input.get()));
            EXPECT_TRUE(Guide::WasKeyboardInputCanceledEXT(input.get()));
        }
    }
}

TEST(GuideTest, SupplementaryCharactersCountAsTwoUtf16UnitsInEveryDialogField) {
    // XNA string.Length checks (ValidateShowMessageBoxArgs IL_000e..018e,
    // BeginShowKeyboardInput IL_002c..00ee) count UTF-16 units, not UTF-8 bytes or code points.
    // 127 U+1F600 (4 UTF-8 bytes, 2 UTF-16 units each) + 'x' = 255 units;
    // another 'x' = 256 units and must be rejected.
    MessageBoxGuard boxes;
    KeyboardInputGuard keyboards;
    std::string boundary;
    for (int i = 0; i < 127; ++i) boundary += "\xF0\x9F\x98\x80";
    boundary += 'x';
    const auto tooLong = boundary + 'x';
    for (int field = 0; field < 3; ++field) {
        SCOPED_TRACE(field);
        auto showBox = [&](const std::string& value) {
            return std::unique_ptr<System::IAsyncResult>(Guide::BeginShowMessageBox(
                field == 0 ? value : "title", field == 1 ? value : "text",
                {field == 2 ? value : "OK"}, 0, MessageBoxIcon::None, {}, {}));
        };
        EXPECT_THROW((void)showBox(tooLong), System::ArgumentException);
        EXPECT_FALSE(Guide::getHasPendingMessageBoxEXTProperty());
        auto box = showBox(boundary);
        Guide::SimulateMessageBoxClickEXT(0);
        EXPECT_EQ(0, Guide::EndShowMessageBox(box.get()));
        auto showKeyboard = [&](const std::string& value) {
            return std::unique_ptr<System::IAsyncResult>(Guide::BeginShowKeyboardInput(
                PlayerIndex::One, field == 0 ? value : "", field == 1 ? value : "",
                field == 2 ? value : "", {}, {}, true));
        };
        EXPECT_THROW((void)showKeyboard(tooLong), System::ArgumentException);
        EXPECT_FALSE(Guide::getHasPendingKeyboardInputEXTProperty());
        auto input = showKeyboard(boundary);
        PressEnter();
        EXPECT_EQ(field == 2 ? boundary : "", Guide::EndShowKeyboardInput(input.get()));
    }
}

TEST(GuideTest, WaitHandlesSignalOnlyWhenTheDialogCompletes) {
    // XNA XOverlappedAsyncResult::.ctor IL_0033..003a creates ManualResetEvent(false);
    // CompletedSynchronously IL_0000..0001 always returns false. EndShow* delegates
    // to PrepareForEndFunction (IL_003d..0048 waits for that event).
    MessageBoxGuard boxes;
    KeyboardInputGuard keyboards;
    auto box = ShowTwoButtonBox();
    EXPECT_FALSE(box->getIsCompletedProperty());
    EXPECT_FALSE(box->getCompletedSynchronouslyProperty());
    EXPECT_FALSE(box->getAsyncWaitHandleProperty().WaitOne(0));
    Guide::SimulateMessageBoxClickEXT(1);
    EXPECT_TRUE(box->getIsCompletedProperty());
    EXPECT_TRUE(box->getAsyncWaitHandleProperty().WaitOne(0));
    EXPECT_TRUE(box->getAsyncWaitHandleProperty().WaitOne(0)); // manual reset, no consumption
    EXPECT_EQ(1, Guide::EndShowMessageBox(box.get()));
    EXPECT_TRUE(box->getAsyncWaitHandleProperty().WaitOne(0));
    std::unique_ptr<System::IAsyncResult> input(Guide::BeginShowKeyboardInput(
        PlayerIndex::One, "", "", "", {}, {}));
    EXPECT_FALSE(input->getIsCompletedProperty());
    EXPECT_FALSE(input->getAsyncWaitHandleProperty().WaitOne(0));
    Guide::SimulateKeyboardInputCancelEXT();
    EXPECT_TRUE(input->getIsCompletedProperty());
    EXPECT_FALSE(input->getCompletedSynchronouslyProperty());
    EXPECT_TRUE(input->getAsyncWaitHandleProperty().WaitOne(0));
    EXPECT_EQ("", Guide::EndShowKeyboardInput(input.get()));
    EXPECT_TRUE(Guide::WasKeyboardInputCanceledEXT(input.get()));
}

TEST(GuideTest, EndingAnOlderCompletedDialogDoesNotConsumeTheCurrentDialog) {
    // XNA EndShow* consumes the supplied result (IL_0000..0006), not the newest
    // Guide request. A completed result remains valid until its own matching End.
    MessageBoxGuard boxes;
    KeyboardInputGuard keyboards;
    auto first = ShowTwoButtonBox();
    Guide::SimulateMessageBoxClickEXT(0);
    auto second = ShowTwoButtonBox();
    EXPECT_EQ(0, Guide::EndShowMessageBox(first.get()));
    EXPECT_TRUE(Guide::getHasPendingMessageBoxEXTProperty());
    EXPECT_FALSE(second->getIsCompletedProperty());
    Guide::SimulateMessageBoxClickEXT(1);
    EXPECT_EQ(1, Guide::EndShowMessageBox(second.get()));
    std::unique_ptr<System::IAsyncResult> oldInput(Guide::BeginShowKeyboardInput(
        PlayerIndex::One, "", "", "first", {}, {}));
    PressEnter();
    std::unique_ptr<System::IAsyncResult> newInput(Guide::BeginShowKeyboardInput(
        PlayerIndex::One, "", "", "second", {}, {}));
    EXPECT_EQ("first", Guide::EndShowKeyboardInput(oldInput.get()));
    EXPECT_TRUE(Guide::getHasPendingKeyboardInputEXTProperty());
    EXPECT_FALSE(newInput->getIsCompletedProperty());
    PressEnter();
    EXPECT_EQ("second", Guide::EndShowKeyboardInput(newInput.get()));
}
