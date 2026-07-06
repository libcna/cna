// SPDX-License-Identifier: MS-PL
//
// Task 804: verify Mouse::ClickedEXT button numbering end-to-end from the real SDL event entry
// point. SdlInputBridge::ProcessEvent calls Mouse::INTERNAL_onClicked(event.button.button - 1)
// on SDL_EVENT_MOUSE_BUTTON_DOWN (matching FNA's SDL3_FNAPlatform.cs:958-960). SDL button
// constants are 1-based (SDL_BUTTON_LEFT=1 .. SDL_BUTTON_X2=5), so the resulting XNA-facing
// index must be 0-based (Left=0, Middle=1, Right=2, XButton1=3, XButton2=4).

#include <gtest/gtest.h>

#include <SDL3/SDL.h>

#include "CNA/Internal/Input/InputManager.hpp"
#include "CNA/Internal/Input/SdlInputBridge.hpp"
#include "Microsoft/Xna/Framework/Input/ButtonState.hpp"
#include "Microsoft/Xna/Framework/Input/Mouse.hpp"

using CNA::Internal::Input::InputManager;
using CNA::Internal::Input::SdlInputBridge;
using Microsoft::Xna::Framework::Input::ButtonState;
using Microsoft::Xna::Framework::Input::Mouse;

namespace
{
    SDL_Event mouseButtonEvent(const Uint32 type, const Uint8 button)
    {
        SDL_Event e{};
        e.type = type;
        e.button.button = button;
        e.button.windowID = 0; // no window -> to_logical_position passes raw coords through
        e.button.x = 0.0f;
        e.button.y = 0.0f;
        return e;
    }
}

TEST(SdlInputBridgeMouseTest, ButtonDownFiresClickedEXTWithZeroBasedIndex)
{
    struct Case { Uint8 sdlButton; int expectedIndex; };
    const Case cases[] = {
        { SDL_BUTTON_LEFT,   0 },
        { SDL_BUTTON_MIDDLE, 1 },
        { SDL_BUTTON_RIGHT,  2 },
        { SDL_BUTTON_X1,     3 },
        { SDL_BUTTON_X2,     4 },
    };

    for (const auto& c : cases)
    {
        int fired = -999;
        Mouse::ClickedEXT = [&fired](const int button) { fired = button; };

        SDL_Event e = mouseButtonEvent(SDL_EVENT_MOUSE_BUTTON_DOWN, c.sdlButton);
        SdlInputBridge::ProcessEvent(e);

        EXPECT_EQ(fired, c.expectedIndex)
            << "SDL button " << static_cast<int>(c.sdlButton)
            << " should map to XNA ClickedEXT index " << c.expectedIndex;
    }

    Mouse::ClickedEXT = nullptr;
}

// INPUT-MOUSE-008 / -009: every one of the five mouse buttons transitions Pressed↔Released end-to-end
// through the real SDL bridge (SDL_EVENT_MOUSE_BUTTON_DOWN/UP → InputManager → MouseState), and only the
// pressed button reads Pressed. Complements the InputManager-level GetStateReflectsPositionAndButtons and
// covers the X1/X2 mapping (MOUSE-009) on the state path, not just the ClickedEXT index path.
TEST(SdlInputBridgeMouseButtonStateTest, AllFiveButtonsTransitionThroughBridge)
{
    struct Case { Uint8 sdlButton; ButtonState (Microsoft::Xna::Framework::Input::MouseState::*getter)() const; const char* name; };
    using MS = Microsoft::Xna::Framework::Input::MouseState;
    const Case cases[] = {
        {SDL_BUTTON_LEFT,   &MS::getLeftButtonProperty,    "Left"},
        {SDL_BUTTON_RIGHT,  &MS::getRightButtonProperty,   "Right"},
        {SDL_BUTTON_MIDDLE, &MS::getMiddleButtonProperty,  "Middle"},
        {SDL_BUTTON_X1,     &MS::getXButton1Property,      "XButton1"},
        {SDL_BUTTON_X2,     &MS::getXButton2Property,      "XButton2"},
    };

    for (const Case& c : cases)
    {
        InputManager::ResetForTests();

        SdlInputBridge::ProcessEvent(mouseButtonEvent(SDL_EVENT_MOUSE_BUTTON_DOWN, c.sdlButton));
        const auto down = Mouse::GetState();
        EXPECT_EQ((down.*c.getter)(), ButtonState::Pressed) << c.name << " should be Pressed after DOWN";
        // every OTHER button stays Released
        for (const Case& o : cases)
            if (o.sdlButton != c.sdlButton)
                EXPECT_EQ((down.*o.getter)(), ButtonState::Released)
                    << o.name << " must stay Released while only " << c.name << " is down";

        SdlInputBridge::ProcessEvent(mouseButtonEvent(SDL_EVENT_MOUSE_BUTTON_UP, c.sdlButton));
        EXPECT_EQ((Mouse::GetState().*c.getter)(), ButtonState::Released) << c.name << " should be Released after UP";
    }

    InputManager::ResetForTests();
}

// P8-007: window lifecycle events (focus-lost / minimized / restored / close-requested) are no-ops for
// MOUSE state too, not just keyboard — they fall through the bridge's default: branch. A held button
// survives them (matching FNA/DEC-15: input state is not cleared on focus loss; games gate on Game.IsActive).
TEST(SdlInputBridgeMouseButtonStateTest, WindowLifecycleEventsDoNotCorruptMouseState)
{
    InputManager::ResetForTests();

    SdlInputBridge::ProcessEvent(mouseButtonEvent(SDL_EVENT_MOUSE_BUTTON_DOWN, SDL_BUTTON_LEFT));
    ASSERT_EQ(Mouse::GetState().getLeftButtonProperty(), ButtonState::Pressed);

    for (const Uint32 type : {SDL_EVENT_WINDOW_FOCUS_LOST, SDL_EVENT_WINDOW_MINIMIZED,
                              SDL_EVENT_WINDOW_RESTORED, SDL_EVENT_WINDOW_CLOSE_REQUESTED})
    {
        SDL_Event e{};
        e.type = type;
        e.window.windowID = 0;
        SdlInputBridge::ProcessEvent(e);
    }

    EXPECT_EQ(Mouse::GetState().getLeftButtonProperty(), ButtonState::Pressed)
        << "window lifecycle events must not release a held mouse button";

    SdlInputBridge::ProcessEvent(mouseButtonEvent(SDL_EVENT_MOUSE_BUTTON_UP, SDL_BUTTON_LEFT));
    EXPECT_EQ(Mouse::GetState().getLeftButtonProperty(), ButtonState::Released);
    InputManager::ResetForTests();
}

// P3-004: an unknown/out-of-range SDL mouse button (SDL only defines 1..5) must be ignored safely —
// the bridge's button switch has a `default: break;`, so it changes no button state and does not
// crash. Only the position (carried by every button event) is applied.
TEST(SdlInputBridgeMouseButtonStateTest, UnknownSdlButtonIsIgnoredSafely)
{
    using MS = Microsoft::Xna::Framework::Input::MouseState;
    InputManager::ResetForTests();

    // Button index 99 (and 0, which SDL never emits) must not touch any of the five XNA buttons.
    for (const Uint8 bogus : {Uint8{0}, Uint8{6}, Uint8{99}, Uint8{255}})
    {
        SDL_Event e = mouseButtonEvent(SDL_EVENT_MOUSE_BUTTON_DOWN, bogus);
        e.button.x = 12.0f;
        e.button.y = 34.0f;
        EXPECT_NO_THROW(SdlInputBridge::ProcessEvent(e)) << "bogus button " << static_cast<int>(bogus);

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

TEST(SdlInputBridgeMouseTest, ButtonUpDoesNotFireClickedEXT)
{
    // FNA fires INTERNAL_onClicked only on button-down, never on button-up.
    bool fired = false;
    Mouse::ClickedEXT = [&fired](const int) { fired = true; };

    SDL_Event e = mouseButtonEvent(SDL_EVENT_MOUSE_BUTTON_UP, SDL_BUTTON_LEFT);
    SdlInputBridge::ProcessEvent(e);

    EXPECT_FALSE(fired);

    Mouse::ClickedEXT = nullptr;
}

namespace
{
    SDL_Event mouseWheelEvent(const float y)
    {
        SDL_Event e{};
        e.type = SDL_EVENT_MOUSE_WHEEL;
        e.wheel.x = 0.0f;
        e.wheel.y = y;
        e.wheel.windowID = 0;
        return e;
    }

    // Applies one wheel event and returns the resulting change in the cumulative
    // ScrollWheelValue (which is process-lifetime cumulative, matching XNA), so tests are
    // independent of any prior accumulation.
    int wheelDelta(const float y)
    {
        const int before = Mouse::GetState().getScrollWheelValueProperty();
        SDL_Event e = mouseWheelEvent(y);
        SdlInputBridge::ProcessEvent(e);
        return Mouse::GetState().getScrollWheelValueProperty() - before;
    }

    // N-005: drives a horizontal-only wheel event (wheel.x) and returns the delta applied to the NOXNA/EXT
    // horizontal accumulator.
    int horizontalWheelDelta(const float x)
    {
        const int before = Mouse::GetState().getHorizontalScrollWheelValueEXTProperty();
        SDL_Event e{};
        e.type = SDL_EVENT_MOUSE_WHEEL;
        e.wheel.x = x;
        e.wheel.y = 0.0f;
        e.wheel.windowID = 0;
        SdlInputBridge::ProcessEvent(e);
        return Mouse::GetState().getHorizontalScrollWheelValueEXTProperty() - before;
    }
}

TEST(SdlInputBridgeMouseWheelTest, WholeNotchesScaleBy120)
{
    // FNA: `(int) evt.wheel.y * 120` — one notch up/down is +/-120 XNA units.
    EXPECT_EQ(wheelDelta(1.0f), 120);
    EXPECT_EQ(wheelDelta(-1.0f), -120);
    EXPECT_EQ(wheelDelta(3.0f), 360);
}

TEST(SdlInputBridgeMouseWheelTest, ZeroDeltaLeavesValueUnchanged)
{
    EXPECT_EQ(wheelDelta(0.0f), 0);
}

// N-005 (was DEC-18): SDL's horizontal wheel.x is now surfaced as the NOXNA/EXT horizontal scroll wheel,
// routed to a SEPARATE accumulator from the vertical XNA ScrollWheelValue. A horizontal-only event must
// leave the vertical value untouched.
TEST(SdlInputBridgeMouseWheelTest, HorizontalWheelDoesNotAffectVerticalScrollWheel)
{
    const int beforeVertical = Mouse::GetState().getScrollWheelValueProperty();

    SDL_Event e{};
    e.type = SDL_EVENT_MOUSE_WHEEL;
    e.wheel.x = 5.0f; // horizontal only
    e.wheel.y = 0.0f;
    e.wheel.windowID = 0;
    SdlInputBridge::ProcessEvent(e);

    EXPECT_EQ(Mouse::GetState().getScrollWheelValueProperty(), beforeVertical)
        << "horizontal wheel must not touch the vertical ScrollWheelValue";
}

// N-005: horizontal wheel.x accumulates into getHorizontalScrollWheelValueEXTProperty, scaled by the same
// XNA 120-unit notch as the vertical wheel.
TEST(SdlInputBridgeMouseWheelTest, HorizontalWheelAccumulatesInEXTPropertyBy120Notches)
{
    EXPECT_EQ(horizontalWheelDelta(1.0f), 120);
    EXPECT_EQ(horizontalWheelDelta(-1.0f), -120);
    EXPECT_EQ(horizontalWheelDelta(3.0f), 360);
    EXPECT_EQ(horizontalWheelDelta(0.0f), 0);
    // Same cast-then-scale truncation as vertical: sub-notch precision motion is discarded.
    EXPECT_EQ(horizontalWheelDelta(0.5f), 0);
}

// N-005: the two wheels are fully independent — a vertical-only event leaves the horizontal EXT value alone.
TEST(SdlInputBridgeMouseWheelTest, VerticalWheelDoesNotAffectHorizontalEXTValue)
{
    const int beforeHorizontal = Mouse::GetState().getHorizontalScrollWheelValueEXTProperty();
    (void)wheelDelta(2.0f); // vertical only
    EXPECT_EQ(Mouse::GetState().getHorizontalScrollWheelValueEXTProperty(), beforeHorizontal);
}

TEST(SdlInputBridgeMouseWheelTest, FractionalSubNotchIsTruncatedBeforeScaling)
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

TEST(SdlInputBridgeMouseWheelTest, RepeatedEventsAccumulate)
{
    const int before = Mouse::GetState().getScrollWheelValueProperty();
    SDL_Event up = mouseWheelEvent(1.0f);
    SdlInputBridge::ProcessEvent(up);
    SdlInputBridge::ProcessEvent(up);
    SdlInputBridge::ProcessEvent(up);
    EXPECT_EQ(Mouse::GetState().getScrollWheelValueProperty() - before, 360);
}
