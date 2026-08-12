// SPDX-License-Identifier: MS-PL
//
// Tasks 887/888: central InputManager::ResetAllForTests() must reset every input subsystem's
// process-wide static state in one call, so test fixtures do not have to know (and remember) the
// full list of per-subsystem reset helpers. These tests prove the fan-out actually clears each
// subsystem, and (task 891) that a repeated/shuffled run stays deterministic.

#include <gtest/gtest.h>

#include "CNA/Internal/Input/InputManager.hpp"
#include "CNA/Internal/Input/SdlInputBridge.hpp"
#include "Microsoft/Xna/Framework/Input/ButtonState.hpp"
#include "Microsoft/Xna/Framework/Input/MouseState.hpp"
#include "Microsoft/Xna/Framework/Input/Keys.hpp"
#include "Microsoft/Xna/Framework/Input/Mouse.hpp"
#include "Microsoft/Xna/Framework/Input/TextInputEXT.hpp"
#include "Microsoft/Xna/Framework/Input/Touch/GestureSample.hpp"
#include "Microsoft/Xna/Framework/Input/Touch/GestureType.hpp"
#include "Microsoft/Xna/Framework/Input/Touch/TouchLocationState.hpp"
#include "Microsoft/Xna/Framework/Input/Touch/TouchPanel.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "SharpRuntime/SharpRuntimeHelper.hpp"
#include "System/TimeSpan.hpp"

using CNA::Internal::Input::InputManager;
using SharpRuntime::charcs;
using Microsoft::Xna::Framework::Vector2;
using Microsoft::Xna::Framework::Input::Keys;
using Microsoft::Xna::Framework::Input::Mouse;
using Microsoft::Xna::Framework::Input::TextInputEXT;
using namespace Microsoft::Xna::Framework::Input::Touch;

namespace
{
    // ResetAllForTests owns InputManager state; the public Keyboard now owns a platform snapshot.
    struct Keyboard
    {
        static auto GetState() { return InputManager::GetKeyboardState(); }
    };
}

TEST(InputResetAllForTests, ClearsAccumulatedInputManagerState)
{
    InputManager::SetKeyState(Keys::A, true);
    ASSERT_TRUE(Keyboard::GetState().IsKeyDown(Keys::A));

    InputManager::ResetAllForTests();

    EXPECT_FALSE(Keyboard::GetState().IsKeyDown(Keys::A));
}

TEST(InputResetAllForTests, ClearsTouchPanelDisplayMetricsAndTouches)
{
    TouchPanel::setDisplayWidthProperty(1234);
    TouchPanel::setDisplayHeightProperty(567);
    InputManager::SetTouchState(1, TouchLocationState::Pressed, Vector2(3, 4));

    InputManager::ResetAllForTests();

    EXPECT_EQ(TouchPanel::getDisplayWidthProperty(), 0);
    EXPECT_EQ(TouchPanel::getDisplayHeightProperty(), 0);
    EXPECT_EQ(TouchPanel::GetState().getCountProperty(), 0);
}

// P5-016: reset must empty the gesture queue. Enqueue a gesture, confirm it is available, then
// ResetAllForTests (fans out to TouchPanel::ResetForTests, which drains gestures_) — queue is empty.
TEST(InputResetAllForTests, ClearsQueuedGesturesOnReset)
{
    InputManager::ResetAllForTests();
    TouchPanel::EnqueueGesture(GestureSample(GestureType::Tap, System::TimeSpan::FromMilliseconds(1.0),
                                             Vector2(1.0f, 2.0f), Vector2::Zero, Vector2::Zero, Vector2::Zero));
    ASSERT_TRUE(TouchPanel::getIsGestureAvailableProperty());

    InputManager::ResetAllForTests();

    EXPECT_FALSE(TouchPanel::getIsGestureAvailableProperty())
        << "ResetAllForTests must empty the gesture queue";
    InputManager::ResetAllForTests();
}

// P5-016: reset must clear previousTouches_ slot continuity. After a Pressed finger is committed to
// previousTouches_ via Update(), a reset wipes it, so the SAME slot/finger moving again reads Pressed
// (a fresh touch), not Moved-with-previous — proving no stale previous-frame slot state survives.
TEST(InputResetAllForTests, ClearsPreviousTouchSlotContinuityOnReset)
{
    InputManager::ResetAllForTests();
    TouchPanel::SetFinger(0, 7, Vector2(10.0f, 20.0f));                 // Pressed in slot 0
    ASSERT_EQ(TouchPanel::GetState()[0].getStateProperty(), TouchLocationState::Pressed);
    TouchPanel::Update();                                              // touches_ -> previousTouches_

    InputManager::ResetAllForTests();                                  // wipes previousTouches_

    TouchPanel::SetFinger(0, 7, Vector2(30.0f, 40.0f));                 // same slot/finger appears again
    const TouchCollection s = TouchPanel::GetState();
    ASSERT_EQ(s.getCountProperty(), 1);
    EXPECT_EQ(s[0].getStateProperty(), TouchLocationState::Pressed)
        << "after reset, a re-appearing finger must read as a fresh Pressed, not Moved-with-previous";
    InputManager::ResetAllForTests();
}

TEST(InputResetAllForTests, ClearsMouseAndTextInputCallbacks)
{
    Mouse::ClickedEXT = [](int) {};
    TextInputEXT::TextInput = [](charcs) {};
    TextInputEXT::TextEditing = [](const std::string&, int, int) {};
    ASSERT_TRUE(static_cast<bool>(Mouse::ClickedEXT));
    ASSERT_TRUE(static_cast<bool>(TextInputEXT::TextInput));

    InputManager::ResetAllForTests();

    EXPECT_FALSE(static_cast<bool>(Mouse::ClickedEXT));
    EXPECT_FALSE(static_cast<bool>(TextInputEXT::TextInput));
    EXPECT_FALSE(static_cast<bool>(TextInputEXT::TextEditing));
}

// P3-012: ResetAllForTests must return the accumulated mouse state to rest — buttons Released,
// position (0,0), and the cumulative scroll-wheel value back to 0 (it lives in InternalInputState,
// which ResetForTests reassigns wholesale). The ClickedEXT/relative-mode extension state is pinned
// by ClearsMouseAndTextInputCallbacks above.
TEST(InputResetAllForTests, ClearsAccumulatedMouseButtonsPositionAndWheel)
{
    using CNA::Internal::Input::MouseButton;
    using Microsoft::Xna::Framework::Input::ButtonState;

    // Start from a clean slate: the scroll-wheel value is process-cumulative, so a prior test's
    // delta would otherwise leak into the `before` assertion under --gtest_shuffle.
    InputManager::ResetAllForTests();

    InputManager::SetMouseButtonState(MouseButton::Left, ButtonState::Pressed);
    InputManager::SetMouseButtonState(MouseButton::XButton2, ButtonState::Pressed);
    InputManager::SetMousePosition(50, 60);
    InputManager::AddScrollWheelDelta(240);
    {
        const auto before = InputManager::GetMouseState();
        ASSERT_EQ(before.getLeftButtonProperty(), ButtonState::Pressed);
        ASSERT_EQ(before.getXProperty(), 50);
        ASSERT_EQ(before.getYProperty(), 60);
        ASSERT_EQ(before.getScrollWheelValueProperty(), 240);
    }

    InputManager::ResetAllForTests();

    const auto after = InputManager::GetMouseState();
    EXPECT_EQ(after.getLeftButtonProperty(),    ButtonState::Released);
    EXPECT_EQ(after.getXButton2Property(),      ButtonState::Released);
    EXPECT_EQ(after.getRightButtonProperty(),   ButtonState::Released);
    EXPECT_EQ(after.getMiddleButtonProperty(),  ButtonState::Released);
    EXPECT_EQ(after.getXButton1Property(),      ButtonState::Released);
    EXPECT_EQ(after.getXProperty(), 0);
    EXPECT_EQ(after.getYProperty(), 0);
    EXPECT_EQ(after.getScrollWheelValueProperty(), 0);
}

TEST(InputResetAllForTests, ResetsSequentialTouchIdCounterViaBridge)
{
    using CNA::Internal::Input::SdlInputBridge;

    auto fingerDown = [](SDL_FingerID id) {
        SDL_Event e{};
        e.type = SDL_EVENT_FINGER_DOWN;
        e.tfinger.fingerID = id;
        e.tfinger.x = 0.5f;
        e.tfinger.y = 0.5f;
        SdlInputBridge::ProcessEvent(e);
    };

    TouchPanel::setDisplayWidthProperty(100);
    TouchPanel::setDisplayHeightProperty(100);

    fingerDown(42);
    const int firstId = InputManager::GetTouchState()[0].getIdProperty();

    InputManager::ResetAllForTests();
    TouchPanel::setDisplayWidthProperty(100);
    TouchPanel::setDisplayHeightProperty(100);

    fingerDown(99); // different SDL finger id, but the compact counter restarts at 1 after reset
    const int afterResetId = InputManager::GetTouchState()[0].getIdProperty();

    EXPECT_EQ(firstId, afterResetId)
        << "ResetAllForTests must clear the finger->touch map and restart the id counter";

    InputManager::ResetAllForTests();
}

// Task 891: order-independence sanity — running the reset twice in a row from arbitrary dirty
// state must land in the same clean state (idempotent), so no test can leak into another.
TEST(InputResetAllForTests, IsIdempotent)
{
    InputManager::SetKeyState(Keys::B, true);
    TouchPanel::setDisplayWidthProperty(10);
    InputManager::ResetAllForTests();
    InputManager::ResetAllForTests();

    EXPECT_FALSE(Keyboard::GetState().IsKeyDown(Keys::B));
    EXPECT_EQ(TouchPanel::getDisplayWidthProperty(), 0);
}
