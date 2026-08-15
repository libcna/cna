// SPDX-License-Identifier: MS-PL
//
// Task 804 / PLAT-90: verify Mouse::ClickedEXT button numbering from the platform-independent
// event entry point. The contract's buttons retain the established 1..5 ordering, while the
// XNA-facing ClickedEXT index is 0-based.

#include <gtest/gtest.h>

#include "CNA/Internal/Input/CoordinateTransformTestRenderer.hpp"
#include "CNA/Internal/Input/InputManager.hpp"
#include "CNA/Internal/Input/PlatformInputBridge.hpp"
#include "CNA/Internal/Renderers/Common/IGraphicsRenderer.hpp"
#include "CNA/Platform/PlatformEvent.hpp"
#include "Microsoft/Xna/Framework/Input/ButtonState.hpp"
#include "Microsoft/Xna/Framework/Input/Mouse.hpp"

using CNA::Internal::Input::InputManager;
using CNA::Internal::Input::PlatformInputBridge;
using namespace CNA::Platform;
using Microsoft::Xna::Framework::Input::ButtonState;
using PublicMouse = Microsoft::Xna::Framework::Input::Mouse;

namespace
{
    // These tests inspect PlatformInputBridge's event accumulator. The public Mouse reads
    // IPlatformMouse after PLAT-80, which is a different once-per-frame source.
    struct Mouse
    {
        static auto GetState() { return InputManager::GetMouseState(); }
        inline static auto& ClickedEXT = PublicMouse::ClickedEXT;
    };

    PlatformEvent mouseButtonEvent(const bool pressed, const std::uint8_t button,
                                   const float x = 0.0f, const float y = 0.0f)
    {
        return MouseButtonEvent{0, button, pressed, 1, x, y};
    }

    PlatformEvent mouseMotionEvent(const float x, const float y,
                                   const float deltaX, const float deltaY)
    {
        return MouseMotionEvent{0, x, y, deltaX, deltaY};
    }
}

TEST(PlatformInputBridgeMouseTest, ButtonDownFiresClickedEXTWithZeroBasedIndex)
{
    struct Case { std::uint8_t button; int expectedIndex; };
    const Case cases[] = {
        {1, 0}, {2, 1}, {3, 2}, {4, 3}, {5, 4},
    };

    for (const auto& c : cases)
    {
        int fired = -999;
        Mouse::ClickedEXT = [&fired](const int button) { fired = button; };

        PlatformInputBridge::ProcessEvent(mouseButtonEvent(true, c.button));

        EXPECT_EQ(fired, c.expectedIndex)
            << "platform button " << static_cast<int>(c.button)
            << " should map to XNA ClickedEXT index " << c.expectedIndex;
    }

    Mouse::ClickedEXT = nullptr;
}

// INPUT-MOUSE-008 / -009: every one of the five mouse buttons transitions Pressed↔Released end-to-end
// through the platform event bridge (MouseButtonEvent → InputManager → MouseState), and only the
// pressed button reads Pressed. Complements the InputManager-level GetStateReflectsPositionAndButtons and
// covers the X1/X2 mapping (MOUSE-009) on the state path, not just the ClickedEXT index path.
TEST(PlatformInputBridgeMouseButtonStateTest, AllFiveButtonsTransitionThroughBridge)
{
    struct Case { std::uint8_t button; ButtonState (Microsoft::Xna::Framework::Input::MouseState::*getter)() const; const char* name; };
    using MS = Microsoft::Xna::Framework::Input::MouseState;
    const Case cases[] = {
        {1, &MS::getLeftButtonProperty,   "Left"},
        {3, &MS::getRightButtonProperty,  "Right"},
        {2, &MS::getMiddleButtonProperty, "Middle"},
        {4, &MS::getXButton1Property,     "XButton1"},
        {5, &MS::getXButton2Property,     "XButton2"},
    };

    for (const Case& c : cases)
    {
        InputManager::ResetForTests();

        PlatformInputBridge::ProcessEvent(mouseButtonEvent(true, c.button));
        const auto down = Mouse::GetState();
        EXPECT_EQ((down.*c.getter)(), ButtonState::Pressed) << c.name << " should be Pressed after DOWN";
        // every OTHER button stays Released
        for (const Case& o : cases)
            if (o.button != c.button)
                EXPECT_EQ((down.*o.getter)(), ButtonState::Released)
                    << o.name << " must stay Released while only " << c.name << " is down";

        PlatformInputBridge::ProcessEvent(mouseButtonEvent(false, c.button));
        EXPECT_EQ((Mouse::GetState().*c.getter)(), ButtonState::Released) << c.name << " should be Released after UP";
    }

    InputManager::ResetForTests();
}

// P8-007: window lifecycle events (focus-lost / minimized / restored / close-requested) are no-ops for
// MOUSE state too, not just keyboard — they fall through the bridge's default: branch. A held button
// survives them (matching FNA/DEC-15: input state is not cleared on focus loss; games gate on Game.IsActive).
TEST(PlatformInputBridgeMouseButtonStateTest, WindowLifecycleEventsDoNotCorruptMouseState)
{
    InputManager::ResetForTests();

    PlatformInputBridge::ProcessEvent(mouseButtonEvent(true, 1));
    ASSERT_EQ(Mouse::GetState().getLeftButtonProperty(), ButtonState::Pressed);

    for (const WindowEventKind kind : {WindowEventKind::FocusLost, WindowEventKind::Minimized,
                                       WindowEventKind::Restored,
                                       WindowEventKind::CloseRequested})
    {
        PlatformInputBridge::ProcessEvent(WindowEvent{0, kind, 0, 0});
    }

    EXPECT_EQ(Mouse::GetState().getLeftButtonProperty(), ButtonState::Pressed)
        << "window lifecycle events must not release a held mouse button";

    PlatformInputBridge::ProcessEvent(mouseButtonEvent(false, 1));
    EXPECT_EQ(Mouse::GetState().getLeftButtonProperty(), ButtonState::Released);
    InputManager::ResetForTests();
}

// P3-004: an unknown/out-of-range platform mouse button (the contract defines 1..5) is ignored safely —
// the bridge's button switch has a `default: break;`, so it changes no button state and does not
// crash. Only the position (carried by every button event) is applied.
TEST(PlatformInputBridgeMouseButtonStateTest, UnknownButtonIsIgnoredSafely)
{
    using MS = Microsoft::Xna::Framework::Input::MouseState;
    InputManager::ResetForTests();

    // Button index 99 (and reserved index 0) must not touch any of the five XNA buttons.
    for (const std::uint8_t bogus : {std::uint8_t{0}, std::uint8_t{6},
                                     std::uint8_t{99}, std::uint8_t{255}})
    {
        EXPECT_NO_THROW(PlatformInputBridge::ProcessEvent(
            mouseButtonEvent(true, bogus, 12.0f, 34.0f))) << "bogus button " << static_cast<int>(bogus);

        const auto s = Mouse::GetState();
        EXPECT_EQ(s.getLeftButtonProperty(),    ButtonState::Released);
        EXPECT_EQ(s.getRightButtonProperty(),   ButtonState::Released);
        EXPECT_EQ(s.getMiddleButtonProperty(),  ButtonState::Released);
        EXPECT_EQ(s.getXButton1Property(),      ButtonState::Released);
        EXPECT_EQ(s.getXButton2Property(),      ButtonState::Released);
        // Position carried by the event is still applied (P3-005: button events update X/Y).
        EXPECT_EQ(s.getXProperty(), 12);
        EXPECT_EQ(s.getYProperty(), 34);
    }

    InputManager::ResetForTests();
}

TEST(PlatformInputBridgeMouseTest, ButtonUpDoesNotFireClickedEXT)
{
    // FNA fires INTERNAL_onClicked only on button-down, never on button-up.
    bool fired = false;
    Mouse::ClickedEXT = [&fired](const int) { fired = true; };

    PlatformInputBridge::ProcessEvent(mouseButtonEvent(false, 1));

    EXPECT_FALSE(fired);

    Mouse::ClickedEXT = nullptr;
}

// P3-013: MouseMotionEvent is the source of absolute mouse position updates outside a button event,
// so the ProcessEvent wiring itself (not just InputManager::SetMousePosition called
// directly, as the InputManager-level tests do) must be exercised end-to-end.
TEST(PlatformInputBridgeMouseTest, MotionEventUpdatesAbsolutePosition)
{
    InputManager::ResetForTests();

    PlatformInputBridge::ProcessEvent(mouseMotionEvent(123.0f, 456.0f, 0.0f, 0.0f));

    const auto state = Mouse::GetState();
    EXPECT_EQ(state.getXProperty(), 123);
    EXPECT_EQ(state.getYProperty(), 456);

    InputManager::ResetForTests();
}

// PLAT-66: the bridge uses WindowId -> IGraphicsRenderer as its sole coordinate-conversion path.
// A motion event in platform client coordinates must account for both scale and letterbox offset.
TEST(PlatformInputBridgeMouseTest, MotionEventUsesRegisteredPlatformCoordinateTransform)
{
    InputManager::ResetForTests();

    constexpr WindowId Window = 0x7ffe1001u;
    CNA::Internal::Input::Testing::CoordinateTransformTestRenderer renderer(
        100.0f, 100.0f, 50.0f, 0.0f, 100.0f, 100.0f);
    CNA::Internal::Renderers::IGraphicsRenderer::RegisterForWindow(Window, &renderer);

    PlatformInputBridge::ProcessEvent(
        MouseMotionEvent{Window, 100.0f, 50.0f, 0.0f, 0.0f});

    // The 100x100 logical viewport occupies x=50..150 in a 200x100 client area.
    const auto state = Mouse::GetState();
    EXPECT_EQ(state.getXProperty(), 50);
    EXPECT_EQ(state.getYProperty(), 50);
    EXPECT_EQ(renderer.WindowToLogicalCalls(), 1);

    CNA::Internal::Renderers::IGraphicsRenderer::UnregisterForWindow(Window);
    InputManager::ResetForTests();
}

// P3-013/P3-025: a motion event's delta fields must reach InputManager::AddMouseRelativeDelta
// through the real ProcessEvent path — the existing
// RelativeModeAccumulatesDeltaAndDrainsOnRead test in MouseInputTests.cpp calls
// AddMouseRelativeDelta directly, which proves the accumulation/drain logic but not this wiring.
TEST(PlatformInputBridgeMouseTest, MotionEventRelativeDeltaReachesInputManagerThroughBridge)
{
    InputManager::ResetForTests();
    InputManager::SetMouseRelativeMode(true);

    PlatformInputBridge::ProcessEvent(mouseMotionEvent(0.0f, 0.0f, 5.0f, -7.0f));
    PlatformInputBridge::ProcessEvent(mouseMotionEvent(0.0f, 0.0f, 2.0f, 1.0f));

    const auto state = Mouse::GetState();
    EXPECT_EQ(state.getXProperty(), 7);
    EXPECT_EQ(state.getYProperty(), -6);

    // Draining semantics: a second read with no new motion returns 0,0.
    const auto drained = Mouse::GetState();
    EXPECT_EQ(drained.getXProperty(), 0);
    EXPECT_EQ(drained.getYProperty(), 0);

    InputManager::SetMouseRelativeMode(false);
    InputManager::ResetForTests();
}

namespace
{
    PlatformEvent mouseWheelEvent(const float y)
    {
        return MouseWheelEvent{0, 0.0f, y};
    }

    // Applies one wheel event and returns the resulting change in the cumulative
    // ScrollWheelValue (which is process-lifetime cumulative, matching XNA), so tests are
    // independent of any prior accumulation.
    int wheelDelta(const float y)
    {
        const int before = Mouse::GetState().getScrollWheelValueProperty();
        PlatformInputBridge::ProcessEvent(mouseWheelEvent(y));
        return Mouse::GetState().getScrollWheelValueProperty() - before;
    }

    // N-005: drives a horizontal-only wheel event (wheel.x) and returns the delta applied to the CNAEXT/EXT
    // horizontal accumulator.
    int horizontalWheelDelta(const float x)
    {
        const int before = Mouse::GetState().getHorizontalScrollWheelValueEXTProperty();
        PlatformInputBridge::ProcessEvent(MouseWheelEvent{0, x, 0.0f});
        return Mouse::GetState().getHorizontalScrollWheelValueEXTProperty() - before;
    }
}

TEST(PlatformInputBridgeMouseWheelTest, WholeNotchesScaleBy120)
{
    // FNA: `(int) evt.wheel.y * 120` — one notch up/down is +/-120 XNA units.
    EXPECT_EQ(wheelDelta(1.0f), 120);
    EXPECT_EQ(wheelDelta(-1.0f), -120);
    EXPECT_EQ(wheelDelta(3.0f), 360);
}

TEST(PlatformInputBridgeMouseWheelTest, ZeroDeltaLeavesValueUnchanged)
{
    EXPECT_EQ(wheelDelta(0.0f), 0);
}

// N-005 (was DEC-18): SDL's horizontal wheel.x is now surfaced as the CNAEXT/EXT horizontal scroll wheel,
// routed to a SEPARATE accumulator from the vertical XNA ScrollWheelValue. A horizontal-only event must
// leave the vertical value untouched.
TEST(PlatformInputBridgeMouseWheelTest, HorizontalWheelDoesNotAffectVerticalScrollWheel)
{
    const int beforeVertical = Mouse::GetState().getScrollWheelValueProperty();

    PlatformInputBridge::ProcessEvent(MouseWheelEvent{0, 5.0f, 0.0f});

    EXPECT_EQ(Mouse::GetState().getScrollWheelValueProperty(), beforeVertical)
        << "horizontal wheel must not touch the vertical ScrollWheelValue";
}

// N-005: horizontal wheel.x accumulates into getHorizontalScrollWheelValueEXTProperty, scaled by the same
// XNA 120-unit notch as the vertical wheel.
TEST(PlatformInputBridgeMouseWheelTest, HorizontalWheelAccumulatesInEXTPropertyBy120Notches)
{
    EXPECT_EQ(horizontalWheelDelta(1.0f), 120);
    EXPECT_EQ(horizontalWheelDelta(-1.0f), -120);
    EXPECT_EQ(horizontalWheelDelta(3.0f), 360);
    EXPECT_EQ(horizontalWheelDelta(0.0f), 0);
    // Same cast-then-scale truncation as vertical: sub-notch precision motion is discarded.
    EXPECT_EQ(horizontalWheelDelta(0.5f), 0);
}

// N-005: the two wheels are fully independent — a vertical-only event leaves the horizontal EXT value alone.
TEST(PlatformInputBridgeMouseWheelTest, VerticalWheelDoesNotAffectHorizontalEXTValue)
{
    const int beforeHorizontal = Mouse::GetState().getHorizontalScrollWheelValueEXTProperty();
    (void)wheelDelta(2.0f); // vertical only
    EXPECT_EQ(Mouse::GetState().getHorizontalScrollWheelValueEXTProperty(), beforeHorizontal);
}

TEST(PlatformInputBridgeMouseWheelTest, FractionalSubNotchIsTruncatedBeforeScaling)
{
    // The crux of the FNA-fidelity fix: the SDL float is cast to int BEFORE multiplying by 120,
    // so sub-notch precision-wheel motion is discarded (FNA/XNA report whole notches only). A
    // multiply-then-cast implementation would wrongly yield 60 / 108 / -60 here.
    EXPECT_EQ(wheelDelta(0.5f), 0);
    EXPECT_EQ(wheelDelta(0.9f), 0);
    EXPECT_EQ(wheelDelta(1.9f), 120);   // truncates to 1 notch
    EXPECT_EQ(wheelDelta(-0.5f), 0);
    EXPECT_EQ(wheelDelta(-1.9f), -120); // truncates toward zero to -1 notch
}

TEST(PlatformInputBridgeMouseWheelTest, RepeatedEventsAccumulate)
{
    const int before = Mouse::GetState().getScrollWheelValueProperty();
    const PlatformEvent up = mouseWheelEvent(1.0f);
    PlatformInputBridge::ProcessEvent(up);
    PlatformInputBridge::ProcessEvent(up);
    PlatformInputBridge::ProcessEvent(up);
    EXPECT_EQ(Mouse::GetState().getScrollWheelValueProperty() - before, 360);
}
