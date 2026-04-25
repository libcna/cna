#include "CNA/Internal/Input/InputManager.hpp"

#include <unordered_set>

namespace CNA::Internal::Input {
    namespace {
        struct InternalMouseState {
            int X = 0;
            int Y = 0;
            int ScrollWheelValue = 0;
            Microsoft::Xna::Framework::Input::ButtonState LeftButton = Microsoft::Xna::Framework::Input::ButtonState::Released;
            Microsoft::Xna::Framework::Input::ButtonState RightButton = Microsoft::Xna::Framework::Input::ButtonState::Released;
            Microsoft::Xna::Framework::Input::ButtonState MiddleButton = Microsoft::Xna::Framework::Input::ButtonState::Released;
        };

        struct InternalInputState {
            InternalMouseState Mouse;
            std::unordered_set<Microsoft::Xna::Framework::Input::Keys> PressedKeys;
        };

        InternalInputState& getInternalInputState() {
            static InternalInputState state{};
            return state;
        }
    }

    void InputManager::SetMousePosition(const int x, const int y) {
        auto& mouseState = getInternalInputState().Mouse;
        mouseState.X = x;
        mouseState.Y = y;
    }

    void InputManager::SetMouseButtonState(
        const MouseButton button,
        const Microsoft::Xna::Framework::Input::ButtonState state
    ) {
        auto& mouseState = getInternalInputState().Mouse;
        switch (button) {
            case MouseButton::Left:
                mouseState.LeftButton = state;
                break;
            case MouseButton::Right:
                mouseState.RightButton = state;
                break;
            case MouseButton::Middle:
                mouseState.MiddleButton = state;
                break;
        }
    }

    void InputManager::AddScrollWheelDelta(const int delta) {
        auto& mouseState = getInternalInputState().Mouse;
        mouseState.ScrollWheelValue += delta;
    }

    void InputManager::SetKeyState(
        const Microsoft::Xna::Framework::Input::Keys key,
        const bool pressed
    ) {
        auto& pressedKeys = getInternalInputState().PressedKeys;
        if (pressed) {
            pressedKeys.insert(key);
            return;
        }
        pressedKeys.erase(key);
    }

    Microsoft::Xna::Framework::Input::MouseState InputManager::GetMouseState() {
        const auto& mouseState = getInternalInputState().Mouse;
        return Microsoft::Xna::Framework::Input::MouseState(
            mouseState.X,
            mouseState.Y,
            mouseState.LeftButton,
            mouseState.RightButton,
            mouseState.MiddleButton,
            mouseState.ScrollWheelValue
        );
    }

    Microsoft::Xna::Framework::Input::KeyboardState InputManager::GetKeyboardState() {
        const auto& pressedKeys = getInternalInputState().PressedKeys;
        return Microsoft::Xna::Framework::Input::KeyboardState(pressedKeys);
    }
}
