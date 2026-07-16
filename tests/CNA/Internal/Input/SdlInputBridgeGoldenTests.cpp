// SPDX-License-Identifier: MS-PL
//
// INPUT-TEST-008: golden behaviour tests for the input bridge.
//
// These are the correctness counterpart to SdlInputBridgeFuzzTests.cpp. The fuzz test drives random
// SDL events and only proves the bridge never crashes / corrupts state; it asserts nothing about the
// *value* of the resulting state. A golden test instead drives a FIXED, hand-recorded sequence of SDL
// events through the real entry point (SdlInputBridge::ProcessEvent) and asserts the COMPLETE resulting
// state snapshot against explicitly pre-computed expected values. So fuzz proves "no crash", golden
// proves "correct output" for a known script.
//
// Everything here is deterministic headless: keyboard uses keycode mode (forced), mouse events carry
// windowID 0 so to_logical_position passes raw coordinates straight through (no window/renderer), and
// touch positions use an explicit 1000x1000 display metric. No real window, GPU, or display is needed.

#include <gtest/gtest.h>

#include <SDL3/SDL.h>

#include <algorithm>
#include <vector>

#include "CNA/Internal/Input/InputManager.hpp"
#include "CNA/Internal/Input/SdlInputBridge.hpp"
#include "Microsoft/Xna/Framework/Input/ButtonState.hpp"
#include "Microsoft/Xna/Framework/Input/Keyboard.hpp"
#include "Microsoft/Xna/Framework/Input/Keys.hpp"
#include "Microsoft/Xna/Framework/Input/Mouse.hpp"
#include "Microsoft/Xna/Framework/Input/Touch/TouchPanel.hpp"

using CNA::Internal::Input::InputManager;
using CNA::Internal::Input::SdlInputBridge;
using Microsoft::Xna::Framework::Input::ButtonState;
using Microsoft::Xna::Framework::Input::Keyboard;
using Microsoft::Xna::Framework::Input::Keys;
using Microsoft::Xna::Framework::Input::Mouse;
using namespace Microsoft::Xna::Framework::Input::Touch;

namespace
{
    constexpr int DisplaySize = 1000;

    SDL_Event keyEvent(const Uint32 type, const SDL_Keycode key)
    {
        SDL_Event e{};
        e.type = type;
        e.key.key = key;
        e.key.scancode = SDL_SCANCODE_UNKNOWN;
        e.key.repeat = false;
        return e;
    }

    SDL_Event mouseMotion(const float x, const float y)
    {
        SDL_Event e{};
        e.type = SDL_EVENT_MOUSE_MOTION;
        e.motion.windowID = 0; // no window -> to_logical_position passes raw coords through
        e.motion.x = x;
        e.motion.y = y;
        e.motion.xrel = 0.0f;
        e.motion.yrel = 0.0f;
        return e;
    }

    // A mouse-button event also carries a cursor position (the bridge calls SetMousePosition from it,
    // matching FNA), so the coords are explicit here to keep a script's cursor position coherent.
    SDL_Event mouseButton(const Uint32 type, const Uint8 button, const float x = 0.0f, const float y = 0.0f)
    {
        SDL_Event e{};
        e.type = type;
        e.button.button = button;
        e.button.windowID = 0;
        e.button.x = x;
        e.button.y = y;
        return e;
    }

    SDL_Event mouseWheel(const float y)
    {
        SDL_Event e{};
        e.type = SDL_EVENT_MOUSE_WHEEL;
        e.wheel.windowID = 0;
        e.wheel.x = 0.0f;
        e.wheel.y = y;
        return e;
    }

    SDL_Event fingerEvent(const Uint32 type, const SDL_FingerID fingerId,
                          const float x, const float y,
                          const float dx = 0.0f, const float dy = 0.0f)
    {
        SDL_Event e{};
        e.type = type;
        e.tfinger.fingerID = fingerId;
        e.tfinger.x = x;
        e.tfinger.y = y;
        e.tfinger.dx = dx;
        e.tfinger.dy = dy;
        return e;
    }

    std::vector<int> pressedKeyValues()
    {
        std::vector<Keys> keys = Keyboard::GetState().GetPressedKeys();
        std::vector<int> out;
        out.reserve(keys.size());
        for (const Keys k : keys)
            out.push_back(static_cast<int>(k));
        std::sort(out.begin(), out.end());
        return out;
    }

    struct SdlInputBridgeGoldenTest : ::testing::Test
    {
        void SetUp() override
        {
            InputManager::ResetAllForTests();
            SdlInputBridge::ClearScancodeModeForTests(); // pin keycode mode (layout-independent script)
            TouchPanel::setDisplayWidthProperty(DisplaySize);
            TouchPanel::setDisplayHeightProperty(DisplaySize);
        }

        void TearDown() override { InputManager::ResetAllForTests(); }
    };
}

// Golden keyboard script: a fixed press/release timeline resolves to one exact pressed set. Proves the
// bridge accumulates precisely the still-held keys and drops the released ones — no more, no less.
TEST_F(SdlInputBridgeGoldenTest, KeyboardScriptResolvesToExactPressedSet)
{
    // Timeline: press W, A, S, D and Space; then lift A and S; then press LeftShift.
    // Expected still-held: W, D, Space, LeftShift.  (A, S were released.)
    const SDL_Event script[] = {
        keyEvent(SDL_EVENT_KEY_DOWN, SDLK_W),
        keyEvent(SDL_EVENT_KEY_DOWN, SDLK_A),
        keyEvent(SDL_EVENT_KEY_DOWN, SDLK_S),
        keyEvent(SDL_EVENT_KEY_DOWN, SDLK_D),
        keyEvent(SDL_EVENT_KEY_DOWN, SDLK_SPACE),
        keyEvent(SDL_EVENT_KEY_UP, SDLK_A),
        keyEvent(SDL_EVENT_KEY_UP, SDLK_S),
        keyEvent(SDL_EVENT_KEY_DOWN, SDLK_LSHIFT),
    };
    for (const SDL_Event& e : script)
        SdlInputBridge::ProcessEvent(e);

    std::vector<int> expected = {
        static_cast<int>(Keys::W),
        static_cast<int>(Keys::D),
        static_cast<int>(Keys::Space),
        static_cast<int>(Keys::LeftShift),
    };
    std::sort(expected.begin(), expected.end());

    EXPECT_EQ(pressedKeyValues(), expected);

    const auto kb = Keyboard::GetState();
    EXPECT_TRUE(kb.IsKeyDown(Keys::W));
    EXPECT_TRUE(kb.IsKeyDown(Keys::D));
    EXPECT_TRUE(kb.IsKeyDown(Keys::Space));
    EXPECT_TRUE(kb.IsKeyDown(Keys::LeftShift));
    EXPECT_TRUE(kb.IsKeyUp(Keys::A));
    EXPECT_TRUE(kb.IsKeyUp(Keys::S));
}

// Golden mouse script: motion + button transitions + wheel notches resolve to one exact MouseState.
TEST_F(SdlInputBridgeGoldenTest, MouseScriptResolvesToExactState)
{
    const int wheelBefore = Mouse::GetState().getScrollWheelValueProperty();

    // Timeline: move to (640,360); press+hold Left; press+release Right; scroll +2 then -1 notch;
    // finally move to (100,200). Expected: pos (100,200), Left held, Right released, wheel +120.
    const SDL_Event script[] = {
        mouseMotion(640.0f, 360.0f),
        mouseButton(SDL_EVENT_MOUSE_BUTTON_DOWN, SDL_BUTTON_LEFT),
        mouseButton(SDL_EVENT_MOUSE_BUTTON_DOWN, SDL_BUTTON_RIGHT),
        mouseButton(SDL_EVENT_MOUSE_BUTTON_UP, SDL_BUTTON_RIGHT),
        mouseWheel(2.0f),
        mouseWheel(-1.0f),
        mouseMotion(100.0f, 200.0f),
    };
    for (const SDL_Event& e : script)
        SdlInputBridge::ProcessEvent(e);

    const auto m = Mouse::GetState();
    EXPECT_EQ(m.getXProperty(), 100);
    EXPECT_EQ(m.getYProperty(), 200);
    EXPECT_EQ(m.getLeftButtonProperty(), ButtonState::Pressed);
    EXPECT_EQ(m.getRightButtonProperty(), ButtonState::Released);
    EXPECT_EQ(m.getMiddleButtonProperty(), ButtonState::Released);
    EXPECT_EQ(m.getScrollWheelValueProperty() - wheelBefore, 120); // (+2 - 1) notches * 120
}

// Golden two-finger touch script: assert the exact TouchCollection snapshot at each checkpoint. Note
// GetState() advances tracking (promotes Pressed->Moved next frame, consumes Released), so the golden
// values below are the snapshot observed at each specific point in the recorded sequence.
//
// Position units: TouchPanel::GetState() falls back to InputManager's event-driven snapshot, whose
// position comes from to_touch_pixel_position() = normalized SDL coord * SDL window size. Headless
// (no window) leaves the window size at 1x1, so GetState() reports the raw normalized 0..1 coordinate.
// (The 1000x1000 display metric set in SetUp only feeds the GestureDetector/ReadGesture path, not this
// one, so it does not affect these values.)
TEST_F(SdlInputBridgeGoldenTest, TwoFingerScriptResolvesToExactTouchSnapshots)
{
    // Finger A down at normalized (0.25,0.25).
    SdlInputBridge::ProcessEvent(fingerEvent(SDL_EVENT_FINGER_DOWN, 9001, 0.25f, 0.25f));
    {
        const TouchCollection s = TouchPanel::GetState();
        ASSERT_EQ(s.getCountProperty(), 1);
        EXPECT_EQ(s[0].getStateProperty(), TouchLocationState::Pressed);
        EXPECT_FLOAT_EQ(s[0].getPositionProperty().X, 0.25f);
        EXPECT_FLOAT_EQ(s[0].getPositionProperty().Y, 0.25f);
    }
    TouchPanel::Update(); // frame boundary (INP-AUD-001): A's Pressed promotes to Moved

    // Finger B down at (0.75,0.75). A was snapshotted Pressed in the prior frame, so it now reports
    // Moved (same position); B is the fresh Pressed touch.
    SdlInputBridge::ProcessEvent(fingerEvent(SDL_EVENT_FINGER_DOWN, 9002, 0.75f, 0.75f));
    {
        const TouchCollection s = TouchPanel::GetState();
        ASSERT_EQ(s.getCountProperty(), 2);
        EXPECT_EQ(s[0].getStateProperty(), TouchLocationState::Moved);
        EXPECT_FLOAT_EQ(s[0].getPositionProperty().X, 0.25f);
        EXPECT_EQ(s[1].getStateProperty(), TouchLocationState::Pressed);
        EXPECT_FLOAT_EQ(s[1].getPositionProperty().X, 0.75f);
        EXPECT_FLOAT_EQ(s[1].getPositionProperty().Y, 0.75f);
    }
    TouchPanel::Update(); // frame boundary: B's Pressed promotes to Moved

    // Finger A moves to (0.5,0.5); B lifts.
    SdlInputBridge::ProcessEvent(fingerEvent(SDL_EVENT_FINGER_MOTION, 9001, 0.5f, 0.5f, 0.25f, 0.25f));
    SdlInputBridge::ProcessEvent(fingerEvent(SDL_EVENT_FINGER_UP, 9002, 0.75f, 0.75f));
    {
        const TouchCollection s = TouchPanel::GetState();
        ASSERT_EQ(s.getCountProperty(), 2);
        EXPECT_EQ(s[0].getStateProperty(), TouchLocationState::Moved);
        EXPECT_FLOAT_EQ(s[0].getPositionProperty().X, 0.5f);
        EXPECT_FLOAT_EQ(s[0].getPositionProperty().Y, 0.5f);
        EXPECT_EQ(s[1].getStateProperty(), TouchLocationState::Released);
    }
    TouchPanel::Update(); // frame boundary: B (Released) is retired

    // After the frame advances past the Released snapshot, B is gone; only A remains, still Moved
    // at (0.5,0.5).
    {
        const TouchCollection s = TouchPanel::GetState();
        ASSERT_EQ(s.getCountProperty(), 1);
        EXPECT_EQ(s[0].getStateProperty(), TouchLocationState::Moved);
        EXPECT_FLOAT_EQ(s[0].getPositionProperty().X, 0.5f);
    }
}

// Golden cross-subsystem checkpoint: keyboard, mouse and touch events interleaved in one recorded
// stream must land in independent, non-interfering state — the whole-input snapshot is exactly what
// the script dictates for each subsystem.
TEST_F(SdlInputBridgeGoldenTest, InterleavedSessionResolvesEachSubsystemIndependently)
{
    const int wheelBefore = Mouse::GetState().getScrollWheelValueProperty();

    const SDL_Event script[] = {
        keyEvent(SDL_EVENT_KEY_DOWN, SDLK_A),
        mouseMotion(320.0f, 240.0f),
        fingerEvent(SDL_EVENT_FINGER_DOWN, 9100, 0.1f, 0.2f),
        keyEvent(SDL_EVENT_KEY_DOWN, SDLK_B),
        mouseButton(SDL_EVENT_MOUSE_BUTTON_DOWN, SDL_BUTTON_LEFT, 320.0f, 240.0f),
        mouseWheel(1.0f),
        keyEvent(SDL_EVENT_KEY_UP, SDLK_A),
    };
    for (const SDL_Event& e : script)
        SdlInputBridge::ProcessEvent(e);

    const auto kb = Keyboard::GetState();
    EXPECT_TRUE(kb.IsKeyUp(Keys::A));
    EXPECT_TRUE(kb.IsKeyDown(Keys::B));

    const auto m = Mouse::GetState();
    EXPECT_EQ(m.getXProperty(), 320);
    EXPECT_EQ(m.getYProperty(), 240);
    EXPECT_EQ(m.getLeftButtonProperty(), ButtonState::Pressed);
    EXPECT_EQ(m.getScrollWheelValueProperty() - wheelBefore, 120);

    const TouchCollection t = TouchPanel::GetState();
    ASSERT_EQ(t.getCountProperty(), 1);
    EXPECT_EQ(t[0].getStateProperty(), TouchLocationState::Pressed);
    EXPECT_FLOAT_EQ(t[0].getPositionProperty().X, 0.1f); // normalized headless coord (see touch test above)
    EXPECT_FLOAT_EQ(t[0].getPositionProperty().Y, 0.2f);
}
