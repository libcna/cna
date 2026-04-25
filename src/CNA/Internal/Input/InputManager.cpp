#include "CNA/Internal/Input/InputManager.hpp"

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

        InternalMouseState& getInternalMouseState() {
            static InternalMouseState state{};
            return state;
        }
    }

    void InputManager::SetMousePosition(const int x, const int y) {
        auto& mouseState = getInternalMouseState();
        mouseState.X = x;
        mouseState.Y = y;
    }

    void InputManager::SetMouseButtonState(
        const MouseButton button,
        const Microsoft::Xna::Framework::Input::ButtonState state
    ) {
        auto& mouseState = getInternalMouseState();
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
        auto& mouseState = getInternalMouseState();
        mouseState.ScrollWheelValue += delta;
    }

    Microsoft::Xna::Framework::Input::MouseState InputManager::GetMouseState() {
        const auto& mouseState = getInternalMouseState();
        return Microsoft::Xna::Framework::Input::MouseState(
            mouseState.X,
            mouseState.Y,
            mouseState.LeftButton,
            mouseState.RightButton,
            mouseState.MiddleButton,
            mouseState.ScrollWheelValue
        );
    }
}
