#include "CNA/Internal/Input/SdlInputBridge.hpp"

#include "CNA/Internal/Input/InputManager.hpp"

namespace CNA::Internal::Input {
    void SdlInputBridge::ProcessEvent(const SDL_Event& event) {
        switch (event.type) {
            case SDL_EVENT_MOUSE_MOTION:
                InputManager::SetMousePosition(
                    static_cast<int>(event.motion.x),
                    static_cast<int>(event.motion.y)
                );
                break;
            case SDL_EVENT_MOUSE_BUTTON_DOWN:
            case SDL_EVENT_MOUSE_BUTTON_UP: {
                const auto state =
                    event.type == SDL_EVENT_MOUSE_BUTTON_DOWN
                        ? Microsoft::Xna::Framework::Input::ButtonState::Pressed
                        : Microsoft::Xna::Framework::Input::ButtonState::Released;

                switch (event.button.button) {
                    case SDL_BUTTON_LEFT:
                        InputManager::SetMouseButtonState(MouseButton::Left, state);
                        break;
                    case SDL_BUTTON_RIGHT:
                        InputManager::SetMouseButtonState(MouseButton::Right, state);
                        break;
                    case SDL_BUTTON_MIDDLE:
                        InputManager::SetMouseButtonState(MouseButton::Middle, state);
                        break;
                    default:
                        break;
                }

                InputManager::SetMousePosition(
                    static_cast<int>(event.button.x),
                    static_cast<int>(event.button.y)
                );

                break;
            }
            case SDL_EVENT_MOUSE_WHEEL:
                InputManager::AddScrollWheelDelta(
                    static_cast<int>(event.wheel.y * 120.0f)
                );
                break;
            default:
                break;
        }
    }
}
