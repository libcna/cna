#include <gtest/gtest.h>

#include "CNA/Internal/Input/InputManager.hpp"
#include "Microsoft/Xna/Framework/Input/Keyboard.hpp"

using namespace Microsoft::Xna::Framework::Input;

namespace {
    void ResetKeyboardState() {
        const Keys keysToReset[] = {
            Keys::Left,
            Keys::Right,
            Keys::Up,
            Keys::Down,
            Keys::Space,
            Keys::Enter,
            Keys::Escape,
            Keys::LeftControl,
            Keys::RightControl,
            Keys::LeftShift,
            Keys::RightShift,
            Keys::A,
            Keys::D,
            Keys::W,
            Keys::S,
        };

        for (const auto key : keysToReset) {
            CNA::Internal::Input::InputManager::SetKeyState(key, false);
        }
    }
}

TEST(KeyboardInputTest, GetStateReflectsPressedAndReleasedKeys) {
    ResetKeyboardState();

    CNA::Internal::Input::InputManager::SetKeyState(Keys::Left, true);
    CNA::Internal::Input::InputManager::SetKeyState(Keys::Space, true);

    const auto state = Keyboard::GetState();

    EXPECT_TRUE(state.IsKeyDown(Keys::Left));
    EXPECT_TRUE(state.IsKeyDown(Keys::Space));
    EXPECT_TRUE(state.IsKeyUp(Keys::Right));

    ResetKeyboardState();
}

TEST(KeyboardInputTest, SnapshotDoesNotChangeAfterInternalStateMutation) {
    ResetKeyboardState();

    CNA::Internal::Input::InputManager::SetKeyState(Keys::A, true);
    const auto snapshot = Keyboard::GetState();

    CNA::Internal::Input::InputManager::SetKeyState(Keys::A, false);
    CNA::Internal::Input::InputManager::SetKeyState(Keys::D, true);

    EXPECT_TRUE(snapshot.IsKeyDown(Keys::A));
    EXPECT_TRUE(snapshot.IsKeyUp(Keys::D));

    const auto currentState = Keyboard::GetState();
    EXPECT_TRUE(currentState.IsKeyUp(Keys::A));
    EXPECT_TRUE(currentState.IsKeyDown(Keys::D));

    ResetKeyboardState();
}

TEST(KeyboardInputTest, GetPressedKeysContainsOnlyPressedKeys) {
    ResetKeyboardState();

    CNA::Internal::Input::InputManager::SetKeyState(Keys::W, true);
    CNA::Internal::Input::InputManager::SetKeyState(Keys::S, true);
    CNA::Internal::Input::InputManager::SetKeyState(Keys::W, false);

    const auto state = Keyboard::GetState();
    const auto pressedKeys = state.GetPressedKeys();

    EXPECT_EQ(pressedKeys.size(), 1);
    EXPECT_EQ(pressedKeys[0], Keys::S);
    EXPECT_TRUE(state.IsKeyDown(Keys::S));
    EXPECT_TRUE(state.IsKeyUp(Keys::W));

    ResetKeyboardState();
}