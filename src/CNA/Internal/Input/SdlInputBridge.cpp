#include "CNA/Internal/Input/SdlInputBridge.hpp"

#include "CNA/Internal/Input/InputManager.hpp"

#include <optional>

namespace {
    std::optional<Microsoft::Xna::Framework::Input::Keys> try_convert_sdl_key(const SDL_Keycode keycode) {
        using Microsoft::Xna::Framework::Input::Keys;
        switch (keycode) {
            case SDLK_LEFT: return Keys::Left;
            case SDLK_RIGHT: return Keys::Right;
            case SDLK_UP: return Keys::Up;
            case SDLK_DOWN: return Keys::Down;
            case SDLK_SPACE: return Keys::Space;
            case SDLK_RETURN: return Keys::Enter;
            case SDLK_ESCAPE: return Keys::Escape;
            case SDLK_LCTRL: return Keys::LeftControl;
            case SDLK_RCTRL: return Keys::RightControl;
            case SDLK_LSHIFT: return Keys::LeftShift;
            case SDLK_RSHIFT: return Keys::RightShift;
            case SDLK_A: return Keys::A;
            case SDLK_B: return Keys::B;
            case SDLK_C: return Keys::C;
            case SDLK_D: return Keys::D;
            case SDLK_E: return Keys::E;
            case SDLK_F: return Keys::F;
            case SDLK_G: return Keys::G;
            case SDLK_H: return Keys::H;
            case SDLK_I: return Keys::I;
            case SDLK_J: return Keys::J;
            case SDLK_K: return Keys::K;
            case SDLK_L: return Keys::L;
            case SDLK_M: return Keys::M;
            case SDLK_N: return Keys::N;
            case SDLK_O: return Keys::O;
            case SDLK_P: return Keys::P;
            case SDLK_Q: return Keys::Q;
            case SDLK_R: return Keys::R;
            case SDLK_S: return Keys::S;
            case SDLK_T: return Keys::T;
            case SDLK_U: return Keys::U;
            case SDLK_V: return Keys::V;
            case SDLK_W: return Keys::W;
            case SDLK_X: return Keys::X;
            case SDLK_Y: return Keys::Y;
            case SDLK_Z: return Keys::Z;
            case SDLK_0: return Keys::D0;
            case SDLK_1: return Keys::D1;
            case SDLK_2: return Keys::D2;
            case SDLK_3: return Keys::D3;
            case SDLK_4: return Keys::D4;
            case SDLK_5: return Keys::D5;
            case SDLK_6: return Keys::D6;
            case SDLK_7: return Keys::D7;
            case SDLK_8: return Keys::D8;
            case SDLK_9: return Keys::D9;
            case SDLK_F11: return Keys::F11;
            default: return std::nullopt;
        }
    }
}

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
            case SDL_EVENT_KEY_DOWN:
            case SDL_EVENT_KEY_UP: {
                if (event.type == SDL_EVENT_KEY_DOWN && event.key.repeat) {
                    break;
                }

                const auto key = try_convert_sdl_key(event.key.key);
                if (!key.has_value()) {
                    break;
                }

                const bool pressed = event.type == SDL_EVENT_KEY_DOWN;
                InputManager::SetKeyState(key.value(), pressed);
                break;
            }
            default:
                break;
        }
    }
}
