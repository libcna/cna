// SPDX-License-Identifier: MS-PL
//
// Task 755: unit tests for MouseState, Mouse, and MouseCursor.
//
// MouseCursor::FromTexture2D is NOT covered here: building a Texture2D with real CPU-side
// pixel data (or a non-Color/ColorSrgbEXT SurfaceFormat) requires a GraphicsDevice, and
// GraphicsDevice's constructor creates a real SDL window and graphics backend — out of scope
// for this headless unit-test binary, matching the precedent already established in
// OcclusionQueryDynamicBufferTests.cpp for other GraphicsDevice-dependent classes.

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <utility>
#include <vector>

#include <SDL3/SDL.h>

#include "CNA/Internal/Input/InputManager.hpp"
#include "CNA/Platform/CannedMouse.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Input/Mouse.hpp"
#include "Microsoft/Xna/Framework/Input/MouseCursor.hpp"
#include "Microsoft/Xna/Framework/Input/MouseState.hpp"

using namespace Microsoft::Xna::Framework::Input;
using Microsoft::Xna::Framework::Color;
using Microsoft::Xna::Framework::Graphics::SurfaceFormat;
using Microsoft::Xna::Framework::Graphics::Texture2D;

namespace
{
    void ResetMouseState()
    {
        CNA::Internal::Input::InputManager::SetMousePosition(0, 0);
        CNA::Internal::Input::InputManager::SetMouseButtonState(
            CNA::Internal::Input::MouseButton::Left, ButtonState::Released);
        CNA::Internal::Input::InputManager::SetMouseButtonState(
            CNA::Internal::Input::MouseButton::Right, ButtonState::Released);
        CNA::Internal::Input::InputManager::SetMouseButtonState(
            CNA::Internal::Input::MouseButton::Middle, ButtonState::Released);
        CNA::Internal::Input::InputManager::SetMouseButtonState(
            CNA::Internal::Input::MouseButton::XButton1, ButtonState::Released);
        CNA::Internal::Input::InputManager::SetMouseButtonState(
            CNA::Internal::Input::MouseButton::XButton2, ButtonState::Released);
        CNA::Internal::Input::InputManager::SetMouseRelativeMode(false);
        Mouse::setWindowHandleProperty(0);
        Mouse::ClickedEXT   = nullptr;
    }

    class MousePlatformInputTest : public ::testing::Test
    {
    protected:
        CNA::Platform::Testing::CannedMousePlatform platform;
        std::unique_ptr<CNA::Platform::Testing::ScopedCurrentPlatform> installed;

        void SetUp() override
        {
            ResetMouseState();
            installed = std::make_unique<CNA::Platform::Testing::ScopedCurrentPlatform>(platform);
        }

        void TearDown() override
        {
            Mouse::ResetForTests();
            installed.reset();
        }
    };
}

// ===========================================================================
// MouseState
// ===========================================================================

TEST(MouseStateTest, DefaultConstructorAllValuesAtRest)
{
    const MouseState state;

    EXPECT_EQ(state.getXProperty(), 0);
    EXPECT_EQ(state.getYProperty(), 0);
    EXPECT_EQ(state.getScrollWheelValueProperty(), 0);
    EXPECT_EQ(state.getLeftButtonProperty(), ButtonState::Released);
    EXPECT_EQ(state.getRightButtonProperty(), ButtonState::Released);
    EXPECT_EQ(state.getMiddleButtonProperty(), ButtonState::Released);
    EXPECT_EQ(state.getXButton1Property(), ButtonState::Released);
    EXPECT_EQ(state.getXButton2Property(), ButtonState::Released);

    // P1-018: the CNAEXT/EXT horizontal wheel also defaults to 0 through the true (parameterless)
    // default constructor, not just through the 8-arg XNA ctor (already covered separately below).
    EXPECT_EQ(state.getHorizontalScrollWheelValueEXTProperty(), 0);
}

TEST(MouseStateTest, EightArgConstructorSetsEveryFieldInTheRightSlot)
{
    // Ctor order is (x, y, scrollWheel, leftButton, middleButton, rightButton, xButton1,
    // xButton2) — alternate Pressed/Released so an accidental parameter swap fails this test.
    const MouseState state(10, 20, 30,
                            ButtonState::Pressed, ButtonState::Released, ButtonState::Pressed,
                            ButtonState::Released, ButtonState::Pressed);

    EXPECT_EQ(state.getXProperty(), 10);
    EXPECT_EQ(state.getYProperty(), 20);
    EXPECT_EQ(state.getScrollWheelValueProperty(), 30);
    EXPECT_EQ(state.getLeftButtonProperty(), ButtonState::Pressed);
    EXPECT_EQ(state.getMiddleButtonProperty(), ButtonState::Released);
    EXPECT_EQ(state.getRightButtonProperty(), ButtonState::Pressed);
    EXPECT_EQ(state.getXButton1Property(), ButtonState::Released);
    EXPECT_EQ(state.getXButton2Property(), ButtonState::Pressed);

    // N-005: the 8-arg XNA ctor leaves the CNAEXT/EXT horizontal wheel at 0.
    EXPECT_EQ(state.getHorizontalScrollWheelValueEXTProperty(), 0);
}

// N-005: the 9-arg CNAEXT/EXT ctor sets the horizontal wheel while keeping every XNA field in the same slot.
TEST(MouseStateTest, NineArgConstructorAlsoSetsHorizontalScrollWheelEXT)
{
    const MouseState state(10, 20, 30,
                            ButtonState::Pressed, ButtonState::Released, ButtonState::Pressed,
                            ButtonState::Released, ButtonState::Pressed,
                            240); // horizontal wheel

    EXPECT_EQ(state.getXProperty(), 10);
    EXPECT_EQ(state.getScrollWheelValueProperty(), 30);
    EXPECT_EQ(state.getHorizontalScrollWheelValueEXTProperty(), 240);
}

// N-005: the horizontal wheel is a CNAEXT/EXT extra field deliberately EXCLUDED from Equals and GetHashCode,
// so those stay byte-identical to FNA (which has no horizontal wheel). Two states differing only in the
// horizontal wheel are equal and hash equal.
TEST(MouseStateTest, HorizontalScrollWheelEXTIsExcludedFromEqualityAndHash)
{
    const MouseState a(1, 2, 3, ButtonState::Pressed, ButtonState::Released, ButtonState::Released,
                       ButtonState::Released, ButtonState::Released, 120);
    const MouseState b(1, 2, 3, ButtonState::Pressed, ButtonState::Released, ButtonState::Released,
                       ButtonState::Released, ButtonState::Released, 999);

    EXPECT_TRUE(a.Equals(b));
    EXPECT_TRUE(a == b);
    EXPECT_FALSE(a != b);
    EXPECT_EQ(a.GetHashCode(), b.GetHashCode());
}

TEST(MouseStateTest, EqualsAndOperatorsReturnTrueForIdenticalStates)
{
    const MouseState a(1, 2, 3, ButtonState::Pressed, ButtonState::Released, ButtonState::Pressed,
                        ButtonState::Released, ButtonState::Pressed);
    const MouseState b(1, 2, 3, ButtonState::Pressed, ButtonState::Released, ButtonState::Pressed,
                        ButtonState::Released, ButtonState::Pressed);

    EXPECT_TRUE(a.Equals(b));
    EXPECT_TRUE(a == b);
    EXPECT_FALSE(a != b);
}

TEST(MouseStateTest, EqualsAndOperatorsReturnFalseWhenPositionDiffers)
{
    const MouseState a(1, 2, 3, ButtonState::Released, ButtonState::Released, ButtonState::Released,
                        ButtonState::Released, ButtonState::Released);
    const MouseState b(9, 2, 3, ButtonState::Released, ButtonState::Released, ButtonState::Released,
                        ButtonState::Released, ButtonState::Released);

    EXPECT_FALSE(a.Equals(b));
    EXPECT_FALSE(a == b);
    EXPECT_TRUE(a != b);
}

TEST(MouseStateTest, EqualsAndOperatorsReturnFalseWhenScrollWheelDiffers)
{
    const MouseState a(1, 2, 3, ButtonState::Released, ButtonState::Released, ButtonState::Released,
                        ButtonState::Released, ButtonState::Released);
    const MouseState b(1, 2, 99, ButtonState::Released, ButtonState::Released, ButtonState::Released,
                        ButtonState::Released, ButtonState::Released);

    EXPECT_FALSE(a.Equals(b));
    EXPECT_TRUE(a != b);
}

TEST(MouseStateTest, EqualsAndOperatorsReturnFalseWhenAButtonDiffers)
{
    const MouseState a(1, 2, 3, ButtonState::Released, ButtonState::Released, ButtonState::Released,
                        ButtonState::Released, ButtonState::Released);
    const MouseState b(1, 2, 3, ButtonState::Pressed, ButtonState::Released, ButtonState::Released,
                        ButtonState::Released, ButtonState::Released);

    EXPECT_FALSE(a.Equals(b));
    EXPECT_TRUE(a != b);
}

TEST(MouseStateTest, GetHashCodeMatchesFormula)
{
    const MouseState state(3, 5, 7, ButtonState::Released, ButtonState::Released,
                            ButtonState::Released, ButtonState::Released, ButtonState::Released);

    const int expected = 3 ^ (5 * 31) ^ (7 * 17);
    EXPECT_EQ(state.GetHashCode(), expected);
}

TEST(MouseStateTest, GetHashCodeIsConsistentForEqualStates)
{
    const MouseState a(3, 5, 7, ButtonState::Pressed, ButtonState::Released, ButtonState::Released,
                        ButtonState::Released, ButtonState::Released);
    const MouseState b(3, 5, 7, ButtonState::Pressed, ButtonState::Released, ButtonState::Released,
                        ButtonState::Released, ButtonState::Released);

    EXPECT_EQ(a.GetHashCode(), b.GetHashCode());
}

TEST(MouseStateTest, ToStringFormatsNoneWhenNoButtonsPressed)
{
    const MouseState state(1, 2, 3, ButtonState::Released, ButtonState::Released,
                            ButtonState::Released, ButtonState::Released, ButtonState::Released);

    EXPECT_EQ(state.ToString(), "[MouseState X=1, Y=2, Buttons=None, Wheel=3]");
}

TEST(MouseStateTest, ToStringFormatsMultiplePressedButtonsInLeftRightMiddleXButton1XButton2Order)
{
    // leftButton=Pressed, xButton2=Pressed; middle/right/xButton1 stay Released.
    const MouseState state(0, 0, 0, ButtonState::Pressed, ButtonState::Released,
                            ButtonState::Released, ButtonState::Released, ButtonState::Pressed);

    EXPECT_EQ(state.ToString(), "[MouseState X=0, Y=0, Buttons=Left XButton2, Wheel=0]");
}

// ===========================================================================
// Mouse
// ===========================================================================

TEST_F(MousePlatformInputTest, GetStateReflectsPublishedPositionAndAllFiveButtons)
{
    CNA::Platform::MouseSnapshot snapshot;
    snapshot.x = 15;
    snapshot.y = 25;
    snapshot.buttons = 0x01 | 0x02 | 0x04 | 0x08 | 0x10;
    platform.Canned().SetPending(snapshot);
    platform.Canned().Update();

    const auto state = Mouse::GetState();

    EXPECT_EQ(state.getXProperty(), 15);
    EXPECT_EQ(state.getYProperty(), 25);
    EXPECT_EQ(state.getLeftButtonProperty(), ButtonState::Pressed);
    EXPECT_EQ(state.getMiddleButtonProperty(), ButtonState::Pressed);
    EXPECT_EQ(state.getRightButtonProperty(), ButtonState::Pressed);
    EXPECT_EQ(state.getXButton1Property(), ButtonState::Pressed);
    EXPECT_EQ(state.getXButton2Property(), ButtonState::Pressed);
}

TEST_F(MousePlatformInputTest, GetStateReflectsBothCumulativeWheelAxes)
{
    CNA::Platform::MouseSnapshot snapshot;
    snapshot.scrollX = -240;
    snapshot.scrollY = 360;
    platform.Canned().SetPending(snapshot);
    platform.Canned().Update();

    const auto state = Mouse::GetState();
    EXPECT_EQ(state.getScrollWheelValueProperty(), 360);
    EXPECT_EQ(state.getHorizontalScrollWheelValueEXTProperty(), -240);
}

TEST_F(MousePlatformInputTest, SetPositionUsesTheSnapshotWindowAndUpdatesGetState)
{
    CNA::Platform::MouseSnapshot snapshot;
    snapshot.window = 27;
    platform.Canned().SetPending(snapshot);
    platform.Canned().Update();

    Mouse::SetPosition(42, 84);
    const auto state = Mouse::GetState();

    EXPECT_EQ(platform.Canned().PositionCalls(), 1);
    EXPECT_EQ(platform.Canned().LastPositionWindow(), 27u);
    EXPECT_EQ(state.getXProperty(), 42);
    EXPECT_EQ(state.getYProperty(), 84);
}

TEST_F(MousePlatformInputTest, GetStateDoesNotAdvanceThePlatformFrame)
{
    CNA::Platform::MouseSnapshot snapshot;
    snapshot.x = 3;
    platform.Canned().SetPending(snapshot);
    platform.Canned().Update();
    ASSERT_EQ(platform.Canned().UpdateCount(), 1);

    EXPECT_EQ(Mouse::GetState().getXProperty(), 3);
    EXPECT_EQ(Mouse::GetState().getXProperty(), 3);
    EXPECT_EQ(platform.Canned().UpdateCount(), 1);
}

TEST_F(MousePlatformInputTest, MissingMouseServiceReturnsRestState)
{
    platform.SetMouseAvailable(false);

    const auto state = Mouse::GetState();
    EXPECT_EQ(state, MouseState());
}

TEST(MouseTest, InternalOnClickedFiresClickedEXTWithButtonIndex)
{
    ResetMouseState();

    int firedButton = -1;
    Mouse::ClickedEXT = [&firedButton](const int button) { firedButton = button; };

    Mouse::INTERNAL_onClicked(2);

    EXPECT_EQ(firedButton, 2);

    ResetMouseState();
}

// DEC-06: ClickedEXT is a multicast System::MulticastAction<int> (matches FNA's Action<int>).
TEST(MouseTest, ClickedEXTIsMulticastAndInvokesAllSubscribersInOrder)
{
    ResetMouseState();
    Mouse::ClickedEXT = nullptr;

    std::vector<int> order;
    Mouse::ClickedEXT += [&order](int) { order.push_back(1); };
    Mouse::ClickedEXT += [&order](const int button) { order.push_back(100 + button); };

    Mouse::INTERNAL_onClicked(3);

    ASSERT_EQ(order.size(), 2u) << "both subscribers must fire (multicast, matching FNA)";
    EXPECT_EQ(order[0], 1);
    EXPECT_EQ(order[1], 103);

    ResetMouseState();
}

// DEC-06: `=` replaces the whole invocation list (C# `field = handler;`), while `+=` adds.
TEST(MouseTest, ClickedEXTAssignmentReplacesAllSubscribers)
{
    ResetMouseState();
    Mouse::ClickedEXT = nullptr;

    int viaAdded = 0;
    int viaAssigned = 0;
    Mouse::ClickedEXT += [&viaAdded](int) { ++viaAdded; };
    Mouse::ClickedEXT = [&viaAssigned](int) { ++viaAssigned; }; // replaces the += subscriber above

    Mouse::INTERNAL_onClicked(0);

    EXPECT_EQ(viaAdded, 0) << "assignment must replace, not append";
    EXPECT_EQ(viaAssigned, 1);

    ResetMouseState();
}

TEST(MouseTest, InternalOnClickedIsSafeWithNoSubscriber)
{
    ResetMouseState();

    EXPECT_NO_THROW(Mouse::INTERNAL_onClicked(0));

    ResetMouseState();
}

TEST_F(MousePlatformInputTest, GetIsRelativeMouseModeEXTDefaultsToFalse)
{
    EXPECT_FALSE(Mouse::getIsRelativeMouseModeEXTProperty());
}

TEST_F(MousePlatformInputTest, RelativeModeAccumulatesDeltaAndDrainsOnRead)
{
    CNA::Platform::MouseSnapshot snapshot;
    snapshot.window = 9;
    platform.Canned().SetPending(snapshot);
    platform.Canned().Update();
    Mouse::setIsRelativeMouseModeEXTProperty(true);

    platform.Canned().AddPendingRelativeDelta(3.0f, -4.0f);
    platform.Canned().AddPendingRelativeDelta(1.0f, 1.0f);
    platform.Canned().Update();

    const auto first = Mouse::GetState();
    EXPECT_EQ(first.getXProperty(), 4);
    EXPECT_EQ(first.getYProperty(), -3);

    // Draining semantics (mirrors FNA's SDL_GetRelativeMouseState): a second read with no new
    // motion in between returns 0,0.
    const auto second = Mouse::GetState();
    EXPECT_EQ(second.getXProperty(), 0);
    EXPECT_EQ(second.getYProperty(), 0);
}

TEST(MouseTest, IsRelativeMouseModeEXTRoundTripsThroughRealWindow)
{
    ResetMouseState();

    if (!SDL_InitSubSystem(SDL_INIT_VIDEO))
    {
        GTEST_SKIP() << "SDL_InitSubSystem(SDL_INIT_VIDEO) failed: " << SDL_GetError();
    }

    SDL_Window* window = SDL_CreateWindow("MouseInputTests", 64, 64, SDL_WINDOW_HIDDEN);
    if (!window)
    {
        SDL_QuitSubSystem(SDL_INIT_VIDEO);
        GTEST_SKIP() << "SDL_CreateWindow failed: " << SDL_GetError();
    }

    Mouse::setWindowHandleProperty(reinterpret_cast<std::uintptr_t>(window));

    Mouse::setIsRelativeMouseModeEXTProperty(true);
    EXPECT_TRUE(Mouse::getIsRelativeMouseModeEXTProperty());

    Mouse::setIsRelativeMouseModeEXTProperty(false);
    EXPECT_FALSE(Mouse::getIsRelativeMouseModeEXTProperty());

    SDL_DestroyWindow(window);
    SDL_QuitSubSystem(SDL_INIT_VIDEO);
    ResetMouseState();
}

TEST_F(MousePlatformInputTest, SetIsRelativeMouseModeEXTUsesTheAssociatedPlatformWindow)
{
    CNA::Platform::MouseSnapshot snapshot;
    snapshot.window = 11;
    platform.Canned().SetPending(snapshot);
    platform.Canned().Update();

    Mouse::setIsRelativeMouseModeEXTProperty(true);
    EXPECT_TRUE(Mouse::getIsRelativeMouseModeEXTProperty());
    EXPECT_EQ(platform.Canned().RelativeModeCalls(), 1);
    EXPECT_EQ(platform.Canned().LastRelativeWindow(), 11u);
    Mouse::setIsRelativeMouseModeEXTProperty(false);
    EXPECT_FALSE(Mouse::getIsRelativeMouseModeEXTProperty());
    EXPECT_EQ(platform.Canned().RelativeModeCalls(), 2);
}

TEST_F(MousePlatformInputTest, SetPositionIsNoOpWhenRelativeModeEnabled)
{
    CNA::Platform::MouseSnapshot snapshot;
    snapshot.window = 13;
    snapshot.x = 7;
    snapshot.y = 7;
    platform.Canned().SetPending(snapshot);
    platform.Canned().Update();

    Mouse::setIsRelativeMouseModeEXTProperty(true);
    Mouse::SetPosition(99, 99); // must be a no-op while relative mode is on (Mouse.cs:106-110)

    EXPECT_EQ(platform.Canned().PositionCalls(), 0);
}

TEST_F(MousePlatformInputTest, SetRelativeMouseModeIsSafeNoOpWithNoWindow)
{
    EXPECT_NO_THROW(Mouse::setIsRelativeMouseModeEXTProperty(true));
    EXPECT_FALSE(Mouse::getIsRelativeMouseModeEXTProperty());
    EXPECT_EQ(platform.Canned().RelativeModeCalls(), 0);
}

TEST_F(MousePlatformInputTest, SetPositionIsSafeAndUpdatesSnapshotWithNoWindow)
{
    ASSERT_EQ(Mouse::getWindowHandleProperty(), 0u);

    EXPECT_NO_THROW(Mouse::SetPosition(123, 456));

    const auto state = Mouse::GetState();
    EXPECT_EQ(state.getXProperty(), 123);
    EXPECT_EQ(state.getYProperty(), 456);

    // Negative and large coordinates must be equally safe with no window (no crash, state tracks).
    EXPECT_NO_THROW(Mouse::SetPosition(-100, -100));
    EXPECT_NO_THROW(Mouse::SetPosition(1 << 20, 1 << 20));
    EXPECT_EQ(Mouse::GetState().getXProperty(), 1 << 20);
    EXPECT_EQ(Mouse::GetState().getYProperty(), 1 << 20);
}

TEST(MouseTest, SetCursorIsSafeNoOpForDisposedCursor)
{
    // Task 803: a disposed cursor has a null SDL handle. SetCursor must no-op rather than call
    // SDL_SetCursor(NULL) (which forces a redraw of the current cursor instead of clearing it).
    if (!SDL_InitSubSystem(SDL_INIT_VIDEO))
    {
        GTEST_SKIP() << "SDL_InitSubSystem(SDL_INIT_VIDEO) failed: " << SDL_GetError();
    }

    SDL_Cursor* raw = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_DEFAULT);
    if (!raw)
    {
        SDL_QuitSubSystem(SDL_INIT_VIDEO);
        GTEST_SKIP() << "SDL_CreateSystemCursor failed: " << SDL_GetError();
    }

    MouseCursor cursor(raw, /*owning=*/true);
    cursor.Dispose();
    ASSERT_EQ(cursor.GetSDLCursor(), nullptr);

    EXPECT_NO_THROW(Mouse::SetCursor(cursor));

    SDL_QuitSubSystem(SDL_INIT_VIDEO);
}

// P1-016: positive-path counterpart to SetCursorIsSafeNoOpForDisposedCursor above — the disposed
// case only exercises the early-return guard, never the actual SDL_SetCursor(handle) call
// (Mouse.cpp). Uses a locally-owned cursor created fresh inside this test (same pattern as the
// sibling test above), not one of the process-wide stock singletons: the stock singletons are
// created once on first access and cached for the process lifetime, but their underlying
// SDL_Cursor* is tied to the SDL video subsystem's lifetime — an earlier test in the (shuffled)
// run order that calls SDL_QuitSubSystem(SDL_INIT_VIDEO) invalidates that cached handle, which
// this test's own SDL_InitSubSystem call does not recreate, causing an intermittent, shuffle-order
// -dependent failure. A cursor created fresh after this test's own SDL_InitSubSystem call cannot
// be stale from another test's subsystem-quit/reinit cycle.
TEST(MouseTest, SetCursorAppliesTheGivenCursorToSDL)
{
    if (!SDL_InitSubSystem(SDL_INIT_VIDEO))
    {
        GTEST_SKIP() << "SDL_InitSubSystem(SDL_INIT_VIDEO) failed: " << SDL_GetError();
    }

    SDL_Cursor* raw = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_CROSSHAIR);
    if (!raw)
    {
        SDL_QuitSubSystem(SDL_INIT_VIDEO);
        GTEST_SKIP() << "SDL_CreateSystemCursor failed: " << SDL_GetError();
    }

    MouseCursor cursor(raw, /*owning=*/true);
    ASSERT_EQ(cursor.GetSDLCursor(), raw);

    Mouse::SetCursor(cursor);
    EXPECT_EQ(SDL_GetCursor(), raw);

    SDL_QuitSubSystem(SDL_INIT_VIDEO);
}

TEST(MouseTest, SetPositionConvertsLogicalToWindowForLetterboxedRenderer)
{
    // Task 847 / a-0001: on a scaled/letterboxed window the OS cursor must land at the correct
    // *window* pixel — SetPosition converts the caller's logical coords back to window space. This
    // exercises the SDL_Renderer path (SDL_RenderCoordinatesToWindow) that logical_to_window uses.
    ResetMouseState();

    if (!SDL_InitSubSystem(SDL_INIT_VIDEO))
    {
        GTEST_SKIP() << "SDL_InitSubSystem(SDL_INIT_VIDEO) failed: " << SDL_GetError();
    }

    SDL_Window* window = SDL_CreateWindow("MouseWarpTest", 200, 200, SDL_WINDOW_HIDDEN);
    if (!window)
    {
        SDL_QuitSubSystem(SDL_INIT_VIDEO);
        GTEST_SKIP() << "SDL_CreateWindow failed: " << SDL_GetError();
    }
    SDL_Renderer* renderer = SDL_CreateRenderer(window, nullptr);
    if (!renderer)
    {
        SDL_DestroyWindow(window);
        SDL_QuitSubSystem(SDL_INIT_VIDEO);
        GTEST_SKIP() << "SDL_CreateRenderer failed: " << SDL_GetError();
    }

    // Logical 100x100 presented into a 200x200 window → a uniform 2x scale (square, no bars).
    SDL_SetRenderLogicalPresentation(renderer, 100, 100, SDL_LOGICAL_PRESENTATION_LETTERBOX);

    // The exact conversion Mouse::SetPosition applies for the renderer path: logical (50,50) center
    // maps to window (100,100). This is what gets fed to SDL_WarpMouseInWindow.
    float wx = 0.0f, wy = 0.0f;
    ASSERT_TRUE(SDL_RenderCoordinatesToWindow(renderer, 50.0f, 50.0f, &wx, &wy));
    EXPECT_NEAR(wx, 100.0f, 1.0f);
    EXPECT_NEAR(wy, 100.0f, 1.0f);

    // SetPosition keeps GetState() reporting the *logical* position the caller set (the warp target
    // is window-space; the OS-cursor landing itself is verified manually — global-mouse readback is
    // Wayland-restricted here, see docs/input-manual-verification-results.md).
    Mouse::setWindowHandleProperty(reinterpret_cast<std::uintptr_t>(window));
    Mouse::SetPosition(50, 50);
    const auto state = Mouse::GetState();
    EXPECT_EQ(state.getXProperty(), 50);
    EXPECT_EQ(state.getYProperty(), 50);

    Mouse::setWindowHandleProperty(0);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_QuitSubSystem(SDL_INIT_VIDEO);
    ResetMouseState();
}

TEST(MouseTest, SetPositionHandlesLetterboxOffsetNotJustScale)
{
    // Task 858: prove OFFSET handling, not just uniform scale. A 100×100 logical presentation
    // LETTERBOXed into a NON-square 200×100 window (aspect 2:1) fits the square content to the
    // 100px height and centers it horizontally → 50px bars on each side. So logical center (50,50)
    // maps to window (100,50) — NOT (50,50): the +50px horizontal offset must be applied.
    // Mouse::SetPosition uses SDL_RenderCoordinatesToWindow, which is offset-aware.
    ResetMouseState();

    if (!SDL_InitSubSystem(SDL_INIT_VIDEO))
    {
        GTEST_SKIP() << "SDL_InitSubSystem(SDL_INIT_VIDEO) failed: " << SDL_GetError();
    }

    SDL_Window* window = SDL_CreateWindow("MouseLetterboxTest", 200, 100, SDL_WINDOW_HIDDEN);
    if (!window)
    {
        SDL_QuitSubSystem(SDL_INIT_VIDEO);
        GTEST_SKIP() << "SDL_CreateWindow failed: " << SDL_GetError();
    }
    SDL_Renderer* renderer = SDL_CreateRenderer(window, nullptr);
    if (!renderer)
    {
        SDL_DestroyWindow(window);
        SDL_QuitSubSystem(SDL_INIT_VIDEO);
        GTEST_SKIP() << "SDL_CreateRenderer failed: " << SDL_GetError();
    }

    SDL_SetRenderLogicalPresentation(renderer, 100, 100, SDL_LOGICAL_PRESENTATION_LETTERBOX);

    float wx = 0.0f, wy = 0.0f;
    // Center: 50px left bar + 50px (scale 1.0) → x=100; no vertical bars → y=50.
    ASSERT_TRUE(SDL_RenderCoordinatesToWindow(renderer, 50.0f, 50.0f, &wx, &wy));
    EXPECT_NEAR(wx, 100.0f, 1.0f) << "horizontal letterbox offset not applied";
    EXPECT_NEAR(wy, 50.0f, 1.0f);
    EXPECT_GT(wx, 60.0f) << "if wx≈50 the offset was ignored (scale-only, wrong)";

    // Top-left logical corner lands at the left bar edge, not the window origin.
    ASSERT_TRUE(SDL_RenderCoordinatesToWindow(renderer, 0.0f, 0.0f, &wx, &wy));
    EXPECT_NEAR(wx, 50.0f, 1.0f);
    EXPECT_NEAR(wy, 0.0f, 1.0f);

    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_QuitSubSystem(SDL_INIT_VIDEO);
    ResetMouseState();
}

// ===========================================================================
// MouseCursor
// ===========================================================================

TEST(MouseCursorTest, StockCursorsAreNonNullWhenVideoAvailable)
{
    if (!SDL_InitSubSystem(SDL_INIT_VIDEO))
    {
        GTEST_SKIP() << "SDL_InitSubSystem(SDL_INIT_VIDEO) failed: " << SDL_GetError();
    }

    EXPECT_NE(MouseCursor::getArrowProperty().GetSDLCursor(), nullptr);
    EXPECT_NE(MouseCursor::getCrosshairProperty().GetSDLCursor(), nullptr);
    EXPECT_NE(MouseCursor::getHandProperty().GetSDLCursor(), nullptr);
    EXPECT_NE(MouseCursor::getIBeamProperty().GetSDLCursor(), nullptr);
    EXPECT_NE(MouseCursor::getNoProperty().GetSDLCursor(), nullptr);
    EXPECT_NE(MouseCursor::getSizeAllProperty().GetSDLCursor(), nullptr);
    EXPECT_NE(MouseCursor::getSizeNESWProperty().GetSDLCursor(), nullptr);
    EXPECT_NE(MouseCursor::getSizeNSProperty().GetSDLCursor(), nullptr);
    EXPECT_NE(MouseCursor::getSizeNWSEProperty().GetSDLCursor(), nullptr);
    EXPECT_NE(MouseCursor::getSizeWEProperty().GetSDLCursor(), nullptr);
    EXPECT_NE(MouseCursor::getWaitProperty().GetSDLCursor(), nullptr);
    EXPECT_NE(MouseCursor::getWaitArrowProperty().GetSDLCursor(), nullptr);

    SDL_QuitSubSystem(SDL_INIT_VIDEO);
}

TEST(MouseCursorTest, StockCursorGetterReturnsTheSameInstanceOnRepeatedCalls)
{
    if (!SDL_InitSubSystem(SDL_INIT_VIDEO))
    {
        GTEST_SKIP() << "SDL_InitSubSystem(SDL_INIT_VIDEO) failed: " << SDL_GetError();
    }

    SDL_Cursor* first  = MouseCursor::getArrowProperty().GetSDLCursor();
    SDL_Cursor* second = MouseCursor::getArrowProperty().GetSDLCursor();
    EXPECT_EQ(first, second);

    SDL_QuitSubSystem(SDL_INIT_VIDEO);
}

TEST(MouseCursorTest, DisposingAStockSingletonIsANoOpAndKeepsItUsable)
{
    // Task 834: the system-cursor singletons are shared for the process lifetime; disposing one
    // must NOT free the SDL cursor (that would break it for every other holder, and this same
    // binary's other tests rely on the stock cursors staying valid). Dispose() is a no-op for them.
    if (!SDL_InitSubSystem(SDL_INIT_VIDEO))
    {
        GTEST_SKIP() << "SDL_InitSubSystem(SDL_INIT_VIDEO) failed: " << SDL_GetError();
    }

    MouseCursor& crosshair = MouseCursor::getCrosshairProperty();
    SDL_Cursor* handle = crosshair.GetSDLCursor();
    ASSERT_NE(handle, nullptr);

    crosshair.Dispose(); // must be a no-op
    crosshair.Dispose(); // idempotent

    // The handle is unchanged (not freed/nulled), and a fresh getter returns the same instance.
    // Without the singleton guard, Dispose() would have nulled sdlCursor_ and this would fail.
    // (We deliberately don't SDL_SetCursor(handle) here: SDL destroys all cursors on
    // SDL_QuitSubSystem(VIDEO), so across the shared binary's per-test init/quit cycles the cached
    // handle may reference a torn-down SDL session — the pointer identity is what task 834 checks.)
    EXPECT_EQ(crosshair.GetSDLCursor(), handle);
    EXPECT_EQ(MouseCursor::getCrosshairProperty().GetSDLCursor(), handle);

    SDL_QuitSubSystem(SDL_INIT_VIDEO);
}

TEST(MouseCursorTest, DefaultConstructorCreatesNonNullOwningCursor)
{
    if (!SDL_InitSubSystem(SDL_INIT_VIDEO))
    {
        GTEST_SKIP() << "SDL_InitSubSystem(SDL_INIT_VIDEO) failed: " << SDL_GetError();
    }

    const MouseCursor cursor;
    EXPECT_NE(cursor.GetSDLCursor(), nullptr);

    SDL_QuitSubSystem(SDL_INIT_VIDEO);
}

TEST(MouseCursorTest, DisposeReleasesHandleAndIsIdempotent)
{
    if (!SDL_InitSubSystem(SDL_INIT_VIDEO))
    {
        GTEST_SKIP() << "SDL_InitSubSystem(SDL_INIT_VIDEO) failed: " << SDL_GetError();
    }

    SDL_Cursor* raw = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_DEFAULT);
    if (!raw)
    {
        SDL_QuitSubSystem(SDL_INIT_VIDEO);
        GTEST_SKIP() << "SDL_CreateSystemCursor failed: " << SDL_GetError();
    }

    MouseCursor cursor(raw, /*owning=*/true);
    EXPECT_EQ(cursor.GetSDLCursor(), raw);

    cursor.Dispose();
    EXPECT_EQ(cursor.GetSDLCursor(), nullptr);

    EXPECT_NO_THROW(cursor.Dispose());
    EXPECT_EQ(cursor.GetSDLCursor(), nullptr);

    SDL_QuitSubSystem(SDL_INIT_VIDEO);
}

TEST(MouseCursorTest, NonOwningConstructorDoesNotDestroyCursorOnDestruction)
{
    if (!SDL_InitSubSystem(SDL_INIT_VIDEO))
    {
        GTEST_SKIP() << "SDL_InitSubSystem(SDL_INIT_VIDEO) failed: " << SDL_GetError();
    }

    SDL_Cursor* raw = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_DEFAULT);
    if (!raw)
    {
        SDL_QuitSubSystem(SDL_INIT_VIDEO);
        GTEST_SKIP() << "SDL_CreateSystemCursor failed: " << SDL_GetError();
    }

    {
        const MouseCursor cursor(raw, /*owning=*/false);
        EXPECT_EQ(cursor.GetSDLCursor(), raw);
    }
    // cursor's destructor ran Dispose(), but owning_=false means SDL_DestroyCursor was NOT
    // called on `raw` — the test still owns its cleanup.
    SDL_DestroyCursor(raw);

    SDL_QuitSubSystem(SDL_INIT_VIDEO);
}

TEST(MouseCursorTest, MoveConstructorTransfersOwnershipAndNullsSource)
{
    if (!SDL_InitSubSystem(SDL_INIT_VIDEO))
    {
        GTEST_SKIP() << "SDL_InitSubSystem(SDL_INIT_VIDEO) failed: " << SDL_GetError();
    }

    SDL_Cursor* raw = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_DEFAULT);
    if (!raw)
    {
        SDL_QuitSubSystem(SDL_INIT_VIDEO);
        GTEST_SKIP() << "SDL_CreateSystemCursor failed: " << SDL_GetError();
    }

    MouseCursor original(raw, /*owning=*/true);
    const MouseCursor moved(std::move(original));

    // Regression coverage for task 752's fix: the old defaulted move ctor bitwise-copied the
    // raw pointer without nulling the source, so both objects believed they owned it.
    EXPECT_EQ(moved.GetSDLCursor(), raw);
    EXPECT_EQ(original.GetSDLCursor(), nullptr);

    SDL_QuitSubSystem(SDL_INIT_VIDEO);
}

TEST(MouseCursorTest, MoveAssignmentDisposesPreviousHandleAndTransfersOwnership)
{
    if (!SDL_InitSubSystem(SDL_INIT_VIDEO))
    {
        GTEST_SKIP() << "SDL_InitSubSystem(SDL_INIT_VIDEO) failed: " << SDL_GetError();
    }

    SDL_Cursor* rawA = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_DEFAULT);
    SDL_Cursor* rawB = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_CROSSHAIR);
    if (!rawA || !rawB)
    {
        SDL_QuitSubSystem(SDL_INIT_VIDEO);
        GTEST_SKIP() << "SDL_CreateSystemCursor failed: " << SDL_GetError();
    }

    MouseCursor a(rawA, /*owning=*/true);
    MouseCursor b(rawB, /*owning=*/true);

    a = std::move(b);

    EXPECT_EQ(a.GetSDLCursor(), rawB);
    EXPECT_EQ(b.GetSDLCursor(), nullptr);

    SDL_QuitSubSystem(SDL_INIT_VIDEO);
}

TEST(MouseCursorTest, SelfMoveAssignmentLeavesCursorIntact)
{
    // Task P1-017: move-assignment guards self-assignment with `if (this != &other)` before
    // calling Dispose() and reading `other`'s fields. Without that guard, `cursor = std::move(cursor)`
    // would Dispose() the cursor (freeing/nulling sdlCursor_) and then copy those now-cleared fields
    // back onto itself, silently destroying the handle. Indirection through a pointer keeps this a
    // genuine runtime self-move rather than something a compiler could diagnose/elide at compile time.
    if (!SDL_InitSubSystem(SDL_INIT_VIDEO))
    {
        GTEST_SKIP() << "SDL_InitSubSystem(SDL_INIT_VIDEO) failed: " << SDL_GetError();
    }

    SDL_Cursor* raw = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_DEFAULT);
    if (!raw)
    {
        SDL_QuitSubSystem(SDL_INIT_VIDEO);
        GTEST_SKIP() << "SDL_CreateSystemCursor failed: " << SDL_GetError();
    }

    MouseCursor cursor(raw, /*owning=*/true);
    MouseCursor* self = &cursor;
    *self             = std::move(*self);

    EXPECT_EQ(cursor.GetSDLCursor(), raw);

    SDL_QuitSubSystem(SDL_INIT_VIDEO);
}

TEST(MouseCursorTest, ColorCursorSurvivesSourcePixelBufferDestruction)
{
    // Task 831: exercises the exact SDL lifetime pattern MouseCursor::FromTexture2D relies on,
    // without needing a GraphicsDevice/Texture2D (which the headless suite excludes). A surface
    // created via SDL_CreateSurfaceFrom only *references* the pixel buffer; SDL_CreateColorCursor
    // must copy the pixels (verified against SDL3 source: it converts RGBA32 -> ARGB8888 into an
    // independent surface and the platform builds its own cursor). So destroying the surface and
    // the source buffer immediately afterward must leave the cursor valid and usable.
    if (!SDL_InitSubSystem(SDL_INIT_VIDEO))
    {
        GTEST_SKIP() << "SDL_InitSubSystem(SDL_INIT_VIDEO) failed: " << SDL_GetError();
    }

    constexpr int w = 4;
    constexpr int h = 4;
    std::vector<std::uint32_t> rgba(static_cast<std::size_t>(w * h), 0xFF00FF00u); // opaque green

    SDL_Surface* surface = SDL_CreateSurfaceFrom(
        w, h, SDL_PIXELFORMAT_RGBA32, rgba.data(), w * static_cast<int>(sizeof(std::uint32_t)));
    if (!surface)
    {
        SDL_QuitSubSystem(SDL_INIT_VIDEO);
        GTEST_SKIP() << "SDL_CreateSurfaceFrom failed: " << SDL_GetError();
    }

    SDL_Cursor* cursor = SDL_CreateColorCursor(surface, 0, 0);
    SDL_DestroySurface(surface);

    // Scribble then release the source buffer: if the cursor still referenced it, later use would
    // read freed/garbage memory.
    std::fill(rgba.begin(), rgba.end(), 0xDEADBEEFu);
    rgba.clear();
    rgba.shrink_to_fit();

    ASSERT_NE(cursor, nullptr) << "SDL_CreateColorCursor failed: " << SDL_GetError();
    EXPECT_TRUE(SDL_SetCursor(cursor)); // still usable -> pixels were copied, not referenced

    SDL_DestroyCursor(cursor);
    SDL_QuitSubSystem(SDL_INIT_VIDEO);
}

// Task 832: FromTexture2D via a CPU-only Texture2D fixture (Texture2D::CreateCpuOnlyForTests),
// which avoids the GraphicsDevice the real construction path needs.

TEST(MouseCursorTest, FromTexture2DCreatesCursorFromColorTexture)
{
    if (!SDL_InitSubSystem(SDL_INIT_VIDEO))
    {
        GTEST_SKIP() << "SDL_InitSubSystem(SDL_INIT_VIDEO) failed: " << SDL_GetError();
    }

    const std::vector<Color> pixels(16, Color::White); // 4x4 opaque white
    const Texture2D tex = Texture2D::CreateCpuOnlyForTests(4, 4, SurfaceFormat::Color, pixels);

    MouseCursor cursor = MouseCursor::FromTexture2D(tex, 0, 0);
    EXPECT_NE(cursor.GetSDLCursor(), nullptr);

    SDL_QuitSubSystem(SDL_INIT_VIDEO);
}

TEST(MouseCursorTest, FromTexture2DAcceptsColorSrgbTexture)
{
    if (!SDL_InitSubSystem(SDL_INIT_VIDEO))
    {
        GTEST_SKIP() << "SDL_InitSubSystem(SDL_INIT_VIDEO) failed: " << SDL_GetError();
    }

    const std::vector<Color> pixels(16, Color::White);
    const Texture2D tex = Texture2D::CreateCpuOnlyForTests(4, 4, SurfaceFormat::ColorSrgbEXT, pixels);

    MouseCursor cursor = MouseCursor::FromTexture2D(tex, 1, 2);
    EXPECT_NE(cursor.GetSDLCursor(), nullptr);

    SDL_QuitSubSystem(SDL_INIT_VIDEO);
}

TEST(MouseCursorTest, FromTexture2DRejectsNonColorSurfaceFormat)
{
    // The format check throws std::invalid_argument before any SDL call, so no video is needed.
    const std::vector<Color> pixels(16, Color::White);
    const Texture2D tex = Texture2D::CreateCpuOnlyForTests(4, 4, SurfaceFormat::Bgr565, pixels);

    EXPECT_THROW((void)MouseCursor::FromTexture2D(tex, 0, 0), std::invalid_argument);
}

TEST(MouseCursorTest, FromTexture2DThrowsWhenOriginIsOutsideTheTexture)
{
    if (!SDL_InitSubSystem(SDL_INIT_VIDEO))
    {
        GTEST_SKIP() << "SDL_InitSubSystem(SDL_INIT_VIDEO) failed: " << SDL_GetError();
    }

    const std::vector<Color> pixels(16, Color::White);
    const Texture2D tex = Texture2D::CreateCpuOnlyForTests(4, 4, SurfaceFormat::Color, pixels);

    // originX/Y = 100 lie outside the 4x4 cursor; SDL_CreateColorCursor rejects the hot spot,
    // so FromTexture2D surfaces it as std::runtime_error.
    EXPECT_THROW((void)MouseCursor::FromTexture2D(tex, 100, 100), std::runtime_error);

    SDL_QuitSubSystem(SDL_INIT_VIDEO);
}
