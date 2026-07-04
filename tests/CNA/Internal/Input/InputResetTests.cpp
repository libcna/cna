// SPDX-License-Identifier: MS-PL
//
// Tasks 887/888: central InputManager::ResetAllForTests() must reset every input subsystem's
// process-wide static state in one call, so test fixtures do not have to know (and remember) the
// full list of per-subsystem reset helpers. These tests prove the fan-out actually clears each
// subsystem, and (task 891) that a repeated/shuffled run stays deterministic.

#include <gtest/gtest.h>

#include "CNA/Internal/Input/InputManager.hpp"
#include "CNA/Internal/Input/SdlInputBridge.hpp"
#include "Microsoft/Xna/Framework/Input/Keyboard.hpp"
#include "Microsoft/Xna/Framework/Input/Keys.hpp"
#include "Microsoft/Xna/Framework/Input/Mouse.hpp"
#include "Microsoft/Xna/Framework/Input/TextInputEXT.hpp"
#include "Microsoft/Xna/Framework/Input/Touch/TouchLocationState.hpp"
#include "Microsoft/Xna/Framework/Input/Touch/TouchPanel.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "SharpRuntime/SharpRuntimeHelper.hpp"

using CNA::Internal::Input::InputManager;
using SharpRuntime::charcs;
using Microsoft::Xna::Framework::Vector2;
using Microsoft::Xna::Framework::Input::Keyboard;
using Microsoft::Xna::Framework::Input::Keys;
using Microsoft::Xna::Framework::Input::Mouse;
using Microsoft::Xna::Framework::Input::TextInputEXT;
using namespace Microsoft::Xna::Framework::Input::Touch;

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
