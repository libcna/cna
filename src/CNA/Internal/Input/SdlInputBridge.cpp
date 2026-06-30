#include "CNA/Internal/Input/SdlInputBridge.hpp"

#include "CNA/Internal/Backends/Common/IGraphicsBackend.hpp"
#include "CNA/Internal/Input/InputManager.hpp"
#include "Microsoft/Xna/Framework/Input/GamePadCapabilities.hpp"
#include "Microsoft/Xna/Framework/Input/GamePadType.hpp"
#include "Microsoft/Xna/Framework/Input/TextInputEXT.hpp"

#include <algorithm>
#include <string>
#include <array>
#include <optional>
#include <unordered_map>

namespace
{
    using CNA::Internal::Input::GamePadAxis;
    using CNA::Internal::Input::GamePadButton;
    using Microsoft::Xna::Framework::PlayerIndex;
    using Microsoft::Xna::Framework::Input::ButtonState;
    using Microsoft::Xna::Framework::Input::Touch::TouchLocationState;

    constexpr std::size_t MaxSupportedGamePads = 4;

    std::array<SDL_Gamepad*, MaxSupportedGamePads>& get_opened_gamepads()
    {
        static std::array<SDL_Gamepad*, MaxSupportedGamePads> openedGamePads{};
        return openedGamePads;
    }

    std::unordered_map<SDL_JoystickID, PlayerIndex>& get_gamepad_to_player_index_map()
    {
        static std::unordered_map<SDL_JoystickID, PlayerIndex> gamepadToPlayerIndex;
        return gamepadToPlayerIndex;
    }

    PlayerIndex slot_to_player_index(const std::size_t slot)
    {
        switch (slot)
        {
        case 0:
            return PlayerIndex::One;
        case 1:
            return PlayerIndex::Two;
        case 2:
            return PlayerIndex::Three;
        default:
            return PlayerIndex::Four;
        }
    }

    std::optional<std::size_t> try_get_slot_for_player_index(const PlayerIndex playerIndex)
    {
        const int slot = static_cast<int>(playerIndex);
        if (slot < 0 || slot >= static_cast<int>(MaxSupportedGamePads))
        {
            return std::nullopt;
        }
        return static_cast<std::size_t>(slot);
    }

    std::optional<std::size_t> try_find_free_gamepad_slot()
    {
        const auto& openedGamePads = get_opened_gamepads();
        for (std::size_t slot = 0; slot < openedGamePads.size(); ++slot)
        {
            if (openedGamePads[slot] == nullptr)
            {
                return slot;
            }
        }
        return std::nullopt;
    }

    std::optional<PlayerIndex> try_get_player_index_for_gamepad_id(const SDL_JoystickID gamePadId)
    {
        const auto& gamepadToPlayerIndex = get_gamepad_to_player_index_map();
        const auto item = gamepadToPlayerIndex.find(gamePadId);
        if (item == gamepadToPlayerIndex.end())
        {
            return std::nullopt;
        }
        return item->second;
    }

    std::optional<GamePadButton> try_convert_sdl_gamepad_button(const SDL_GamepadButton button)
    {
        switch (button)
        {
        case SDL_GAMEPAD_BUTTON_SOUTH:
            return GamePadButton::A;
        case SDL_GAMEPAD_BUTTON_EAST:
            return GamePadButton::B;
        case SDL_GAMEPAD_BUTTON_WEST:
            return GamePadButton::X;
        case SDL_GAMEPAD_BUTTON_NORTH:
            return GamePadButton::Y;
        case SDL_GAMEPAD_BUTTON_BACK:
            return GamePadButton::Back;
        case SDL_GAMEPAD_BUTTON_START:
            return GamePadButton::Start;
        case SDL_GAMEPAD_BUTTON_LEFT_SHOULDER:
            return GamePadButton::LeftShoulder;
        case SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER:
            return GamePadButton::RightShoulder;
        case SDL_GAMEPAD_BUTTON_LEFT_STICK:
            return GamePadButton::LeftStick;
        case SDL_GAMEPAD_BUTTON_RIGHT_STICK:
            return GamePadButton::RightStick;
        case SDL_GAMEPAD_BUTTON_DPAD_UP:
            return GamePadButton::DPadUp;
        case SDL_GAMEPAD_BUTTON_DPAD_DOWN:
            return GamePadButton::DPadDown;
        case SDL_GAMEPAD_BUTTON_DPAD_LEFT:
            return GamePadButton::DPadLeft;
        case SDL_GAMEPAD_BUTTON_DPAD_RIGHT:
            return GamePadButton::DPadRight;
        case SDL_GAMEPAD_BUTTON_GUIDE:
            return GamePadButton::BigButton;
        default:
            return std::nullopt;
        }
    }

    std::optional<GamePadAxis> try_convert_sdl_gamepad_axis(const SDL_GamepadAxis axis)
    {
        switch (axis)
        {
        case SDL_GAMEPAD_AXIS_LEFTX:
            return GamePadAxis::LeftThumbstickX;
        case SDL_GAMEPAD_AXIS_LEFTY:
            return GamePadAxis::LeftThumbstickY;
        case SDL_GAMEPAD_AXIS_RIGHTX:
            return GamePadAxis::RightThumbstickX;
        case SDL_GAMEPAD_AXIS_RIGHTY:
            return GamePadAxis::RightThumbstickY;
        case SDL_GAMEPAD_AXIS_LEFT_TRIGGER:
            return GamePadAxis::LeftTrigger;
        case SDL_GAMEPAD_AXIS_RIGHT_TRIGGER:
            return GamePadAxis::RightTrigger;
        default:
            return std::nullopt;
        }
    }

    float normalize_stick_axis(const Sint16 value)
    {
        if (value >= 0)
        {
            return std::clamp(static_cast<float>(value) / 32767.0f, 0.0f, 1.0f);
        }
        return std::clamp(static_cast<float>(value) / 32768.0f, -1.0f, 0.0f);
    }

    float normalize_trigger_axis(const Sint16 value)
    {
        return std::clamp(static_cast<float>(value) / 32767.0f, 0.0f, 1.0f);
    }

    std::unordered_map<SDL_FingerID, int>& get_finger_id_to_touch_id_map()
    {
        static std::unordered_map<SDL_FingerID, int> fingerIdToTouchId;
        return fingerIdToTouchId;
    }

    int& get_next_touch_id()
    {
        static int nextTouchId = 1;
        return nextTouchId;
    }

    int get_or_create_touch_id(const SDL_FingerID fingerId)
    {
        auto& fingerIdToTouchId = get_finger_id_to_touch_id_map();
        const auto existing = fingerIdToTouchId.find(fingerId);
        if (existing != fingerIdToTouchId.end())
        {
            return existing->second;
        }

        const int touchId = get_next_touch_id();
        get_next_touch_id() += 1;
        fingerIdToTouchId[fingerId] = touchId;
        return touchId;
    }

    std::optional<int> try_get_touch_id(const SDL_FingerID fingerId)
    {
        const auto& fingerIdToTouchId = get_finger_id_to_touch_id_map();
        const auto existing = fingerIdToTouchId.find(fingerId);
        if (existing == fingerIdToTouchId.end())
        {
            return std::nullopt;
        }
        return existing->second;
    }

    void release_touch_id_mapping(const SDL_FingerID fingerId)
    {
        auto& fingerIdToTouchId = get_finger_id_to_touch_id_map();
        fingerIdToTouchId.erase(fingerId);
    }

    /// Converts window-space coordinates to logical (renderer) coordinates.
    /// When SDL_SetRenderLogicalPresentation is active (letterbox on Android),
    /// this maps physical coords into the game's virtual coordinate space.
    /// Falls back to the raw coords if no renderer is available.
    Microsoft::Xna::Framework::Vector2 to_logical_position(SDL_Window* window, float windowX, float windowY)
    {
        if (window != nullptr)
        {
            // SDL_renderer backend: use SDL's built-in logical-presentation transform.
            SDL_Renderer* renderer = SDL_GetRenderer(window);
            if (renderer != nullptr)
            {
                float logX = windowX, logY = windowY;
                if (SDL_RenderCoordinatesFromWindow(renderer, windowX, windowY, &logX, &logY))
                {
                    return Microsoft::Xna::Framework::Vector2(logX, logY);
                }
            }
            // Other backends (e.g. EasyGL): use the backend's own transform if registered.
            auto* backend = CNA::Internal::Backends::IGraphicsBackend::GetForWindow(window);
            if (backend != nullptr)
            {
                float logX = windowX, logY = windowY;
                if (backend->TransformWindowToLogical(windowX, windowY, logX, logY))
                    return Microsoft::Xna::Framework::Vector2(logX, logY);
            }
        }
        return Microsoft::Xna::Framework::Vector2(windowX, windowY);
    }

    Microsoft::Xna::Framework::Vector2 to_touch_pixel_position(const SDL_TouchFingerEvent& touchEvent)
    {
        SDL_Window* window = nullptr;
        if (touchEvent.windowID != 0)
        {
            window = SDL_GetWindowFromID(touchEvent.windowID);
        }
        if (window == nullptr)
        {
            window = SDL_GetMouseFocus();
        }

        // SDL touch coords are normalized 0..1 relative to the window in points.
        // Convert to window-point coordinates first, then map to logical coords.
        int winW = 1, winH = 1;
        if (window != nullptr)
        {
            SDL_GetWindowSize(window, &winW, &winH);
        }
        const float windowX = touchEvent.x * static_cast<float>(winW);
        const float windowY = touchEvent.y * static_cast<float>(winH);

        return to_logical_position(window, windowX, windowY);
    }

    std::optional<Microsoft::Xna::Framework::Input::Keys> try_convert_sdl_key(const SDL_Keycode keycode)
    {
        using Microsoft::Xna::Framework::Input::Keys;
        switch (keycode)
        {
        case SDLK_AC_BACK: return Keys::Escape;
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
        case SDLK_TAB: return Keys::Tab;
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
        case SDLK_BACKSPACE: return Keys::Back;
        case SDLK_LALT:  return Keys::LeftAlt;
        case SDLK_RALT:  return Keys::RightAlt;
        case SDLK_LGUI:  return Keys::LeftWindows;
        case SDLK_RGUI:  return Keys::RightWindows;
        case SDLK_CAPSLOCK:  return Keys::CapsLock;
        case SDLK_NUMLOCKCLEAR: return Keys::NumLock;
        case SDLK_SCROLLLOCK:   return Keys::Scroll;
        case SDLK_F1:  return Keys::F1;
        case SDLK_F2:  return Keys::F2;
        case SDLK_F3:  return Keys::F3;
        case SDLK_F4:  return Keys::F4;
        case SDLK_F5:  return Keys::F5;
        case SDLK_F6:  return Keys::F6;
        case SDLK_F7:  return Keys::F7;
        case SDLK_F8:  return Keys::F8;
        case SDLK_F9:  return Keys::F9;
        case SDLK_F10: return Keys::F10;
        case SDLK_F11: return Keys::F11;
        case SDLK_F12: return Keys::F12;
        case SDLK_KP_0: return Keys::NumPad0;
        case SDLK_KP_1: return Keys::NumPad1;
        case SDLK_KP_2: return Keys::NumPad2;
        case SDLK_KP_3: return Keys::NumPad3;
        case SDLK_KP_4: return Keys::NumPad4;
        case SDLK_KP_5: return Keys::NumPad5;
        case SDLK_KP_6: return Keys::NumPad6;
        case SDLK_KP_7: return Keys::NumPad7;
        case SDLK_KP_8: return Keys::NumPad8;
        case SDLK_KP_9: return Keys::NumPad9;
        case SDLK_KP_MULTIPLY: return Keys::Multiply;
        case SDLK_KP_PLUS:     return Keys::Add;
        case SDLK_KP_MINUS:    return Keys::Subtract;
        case SDLK_KP_DECIMAL:  return Keys::Decimal;
        case SDLK_KP_DIVIDE:   return Keys::Divide;
        case SDLK_KP_ENTER:    return Keys::Enter;
        case SDLK_SEMICOLON:   return Keys::OemSemicolon;
        case SDLK_EQUALS:      return Keys::OemPlus;
        case SDLK_COMMA:       return Keys::OemComma;
        case SDLK_MINUS:       return Keys::OemMinus;
        case SDLK_PERIOD:      return Keys::OemPeriod;
        case SDLK_SLASH:       return Keys::OemQuestion;
        case SDLK_GRAVE:       return Keys::OemTilde;
        case SDLK_LEFTBRACKET: return Keys::OemOpenBrackets;
        case SDLK_BACKSLASH:   return Keys::OemPipe;
        case SDLK_RIGHTBRACKET:return Keys::OemCloseBrackets;
        case SDLK_APOSTROPHE:  return Keys::OemQuotes;
        case SDLK_PAGEUP:   return Keys::PageUp;
        case SDLK_PAGEDOWN: return Keys::PageDown;
        case SDLK_HOME:     return Keys::Home;
        case SDLK_END:      return Keys::End;
        case SDLK_INSERT:   return Keys::Insert;
        case SDLK_DELETE:   return Keys::Delete;
        case SDLK_PRINTSCREEN: return Keys::PrintScreen;
        case SDLK_PAUSE:       return Keys::Pause;
        default: return std::nullopt;
        }
    }
}

namespace CNA::Internal::Input
{
    static SDL_Gamepad* get_sdl_gamepad_for_player(const Microsoft::Xna::Framework::PlayerIndex playerIndex)
    {
        const auto slot = static_cast<std::size_t>(static_cast<int>(playerIndex));
        if (slot >= MaxSupportedGamePads)
            return nullptr;
        return get_opened_gamepads()[slot];
    }

    bool SdlInputBridge::SetVibration(
        Microsoft::Xna::Framework::PlayerIndex playerIndex,
        float leftMotor,
        float rightMotor
    )
    {
        SDL_Gamepad* gamepad = get_sdl_gamepad_for_player(playerIndex);
        if (gamepad == nullptr)
            return false;
        const auto left  = static_cast<Uint16>(std::clamp(leftMotor,  0.0f, 1.0f) * 0xFFFF);
        const auto right = static_cast<Uint16>(std::clamp(rightMotor, 0.0f, 1.0f) * 0xFFFF);
        return SDL_RumbleGamepad(gamepad, left, right, 0);
    }

    bool SdlInputBridge::SetTriggerVibration(
        Microsoft::Xna::Framework::PlayerIndex playerIndex,
        float leftTrigger,
        float rightTrigger
    )
    {
        SDL_Gamepad* gamepad = get_sdl_gamepad_for_player(playerIndex);
        if (gamepad == nullptr)
            return false;
        const auto left  = static_cast<Uint16>(std::clamp(leftTrigger,  0.0f, 1.0f) * 0xFFFF);
        const auto right = static_cast<Uint16>(std::clamp(rightTrigger, 0.0f, 1.0f) * 0xFFFF);
        return SDL_RumbleGamepadTriggers(gamepad, left, right, 0);
    }

    std::string SdlInputBridge::GetGUID(Microsoft::Xna::Framework::PlayerIndex playerIndex)
    {
        SDL_Gamepad* gamepad = get_sdl_gamepad_for_player(playerIndex);
        if (gamepad == nullptr)
            return "";
        SDL_JoystickID joystickId = SDL_GetGamepadID(gamepad);
        if (joystickId == 0)
            return "";
        SDL_GUID guid = SDL_GetGamepadGUIDForID(joystickId);
        char buf[33] = {};
        SDL_GUIDToString(guid, buf, static_cast<int>(sizeof(buf)));
        return std::string(buf);
    }

    static Microsoft::Xna::Framework::Input::GamePadType sdl_joystick_type_to_gamepad_type(SDL_JoystickType t)
    {
        using Microsoft::Xna::Framework::Input::GamePadType;
        switch (t)
        {
        case SDL_JOYSTICK_TYPE_GAMEPAD:      return GamePadType::GamePad;
        case SDL_JOYSTICK_TYPE_WHEEL:        return GamePadType::Wheel;
        case SDL_JOYSTICK_TYPE_ARCADE_STICK: return GamePadType::ArcadeStick;
        case SDL_JOYSTICK_TYPE_FLIGHT_STICK: return GamePadType::FlightStick;
        case SDL_JOYSTICK_TYPE_DANCE_PAD:    return GamePadType::DancePad;
        case SDL_JOYSTICK_TYPE_GUITAR:       return GamePadType::Guitar;
        case SDL_JOYSTICK_TYPE_DRUM_KIT:     return GamePadType::DrumKit;
        case SDL_JOYSTICK_TYPE_ARCADE_PAD:   return GamePadType::BigButtonPad;
        default:                             return GamePadType::Unknown;
        }
    }

    Microsoft::Xna::Framework::Input::GamePadCapabilities
    SdlInputBridge::GetCapabilities(Microsoft::Xna::Framework::PlayerIndex playerIndex)
    {
        using Microsoft::Xna::Framework::Input::GamePadCapabilities;

        SDL_Gamepad* gamepad = get_sdl_gamepad_for_player(playerIndex);
        if (gamepad == nullptr)
            return GamePadCapabilities{};

        GamePadCapabilities caps{};
        caps.IsConnected = true;

        // Joystick type → GamePadType
        SDL_Joystick* joystick = SDL_GetGamepadJoystick(gamepad);
        if (joystick != nullptr)
            caps.GamePadType_ = sdl_joystick_type_to_gamepad_type(SDL_GetJoystickType(joystick));

        // Buttons
        caps.HasAButton            = SDL_GamepadHasButton(gamepad, SDL_GAMEPAD_BUTTON_SOUTH);
        caps.HasBButton            = SDL_GamepadHasButton(gamepad, SDL_GAMEPAD_BUTTON_EAST);
        caps.HasXButton            = SDL_GamepadHasButton(gamepad, SDL_GAMEPAD_BUTTON_WEST);
        caps.HasYButton            = SDL_GamepadHasButton(gamepad, SDL_GAMEPAD_BUTTON_NORTH);
        caps.HasBackButton         = SDL_GamepadHasButton(gamepad, SDL_GAMEPAD_BUTTON_BACK);
        caps.HasBigButton          = SDL_GamepadHasButton(gamepad, SDL_GAMEPAD_BUTTON_GUIDE);
        caps.HasStartButton        = SDL_GamepadHasButton(gamepad, SDL_GAMEPAD_BUTTON_START);
        caps.HasLeftStickButton    = SDL_GamepadHasButton(gamepad, SDL_GAMEPAD_BUTTON_LEFT_STICK);
        caps.HasRightStickButton   = SDL_GamepadHasButton(gamepad, SDL_GAMEPAD_BUTTON_RIGHT_STICK);
        caps.HasLeftShoulderButton = SDL_GamepadHasButton(gamepad, SDL_GAMEPAD_BUTTON_LEFT_SHOULDER);
        caps.HasRightShoulderButton= SDL_GamepadHasButton(gamepad, SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER);
        caps.HasDPadUpButton       = SDL_GamepadHasButton(gamepad, SDL_GAMEPAD_BUTTON_DPAD_UP);
        caps.HasDPadDownButton     = SDL_GamepadHasButton(gamepad, SDL_GAMEPAD_BUTTON_DPAD_DOWN);
        caps.HasDPadLeftButton     = SDL_GamepadHasButton(gamepad, SDL_GAMEPAD_BUTTON_DPAD_LEFT);
        caps.HasDPadRightButton    = SDL_GamepadHasButton(gamepad, SDL_GAMEPAD_BUTTON_DPAD_RIGHT);

        // Axes
        caps.HasLeftXThumbStick  = SDL_GamepadHasAxis(gamepad, SDL_GAMEPAD_AXIS_LEFTX);
        caps.HasLeftYThumbStick  = SDL_GamepadHasAxis(gamepad, SDL_GAMEPAD_AXIS_LEFTY);
        caps.HasRightXThumbStick = SDL_GamepadHasAxis(gamepad, SDL_GAMEPAD_AXIS_RIGHTX);
        caps.HasRightYThumbStick = SDL_GamepadHasAxis(gamepad, SDL_GAMEPAD_AXIS_RIGHTY);
        caps.HasLeftTrigger      = SDL_GamepadHasAxis(gamepad, SDL_GAMEPAD_AXIS_LEFT_TRIGGER);
        caps.HasRightTrigger     = SDL_GamepadHasAxis(gamepad, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER);

        // Rumble: probe with all-zero call — returns true if supported
        const bool hasRumble = SDL_RumbleGamepad(gamepad, 0, 0, 0);
        caps.HasLeftVibrationMotor  = hasRumble;
        caps.HasRightVibrationMotor = hasRumble;

        // Trigger rumble
        caps.HasTriggerVibrationMotorsEXT = SDL_RumbleGamepadTriggers(gamepad, 0, 0, 0);

        // Light bar (RGB LED)
        const SDL_PropertiesID props = SDL_GetGamepadProperties(gamepad);
        if (props != 0)
            caps.HasLightBarEXT = SDL_GetBooleanProperty(props, SDL_PROP_GAMEPAD_CAP_RGB_LED_BOOLEAN, false);

        // Extended buttons
        caps.HasMisc1EXT   = SDL_GamepadHasButton(gamepad, SDL_GAMEPAD_BUTTON_MISC1);
        caps.HasPaddle1EXT = SDL_GamepadHasButton(gamepad, SDL_GAMEPAD_BUTTON_RIGHT_PADDLE1);
        caps.HasPaddle2EXT = SDL_GamepadHasButton(gamepad, SDL_GAMEPAD_BUTTON_LEFT_PADDLE1);
        caps.HasPaddle3EXT = SDL_GamepadHasButton(gamepad, SDL_GAMEPAD_BUTTON_RIGHT_PADDLE2);
        caps.HasPaddle4EXT = SDL_GamepadHasButton(gamepad, SDL_GAMEPAD_BUTTON_LEFT_PADDLE2);

        // Touchpad, gyro, accelerometer
        caps.HasTouchPadEXT      = SDL_GetNumGamepadTouchpads(gamepad) > 0;
        caps.HasGyroEXT          = SDL_GamepadHasSensor(gamepad, SDL_SENSOR_GYRO);
        caps.HasAccelerometerEXT = SDL_GamepadHasSensor(gamepad, SDL_SENSOR_ACCEL);

        return caps;
    }

    void SdlInputBridge::ProcessEvent(const SDL_Event& event)
    {
        switch (event.type)
        {
        case SDL_EVENT_MOUSE_MOTION:
            {
                SDL_Window* win = (event.motion.windowID != 0)
                                      ? SDL_GetWindowFromID(event.motion.windowID)
                                      : SDL_GetMouseFocus();
                const auto pos = to_logical_position(win, event.motion.x, event.motion.y);
                InputManager::SetMousePosition(static_cast<int>(pos.X), static_cast<int>(pos.Y));
                break;
            }
        case SDL_EVENT_MOUSE_BUTTON_DOWN:
        case SDL_EVENT_MOUSE_BUTTON_UP:
            {
                const auto state =
                    event.type == SDL_EVENT_MOUSE_BUTTON_DOWN
                        ? Microsoft::Xna::Framework::Input::ButtonState::Pressed
                        : Microsoft::Xna::Framework::Input::ButtonState::Released;

                switch (event.button.button)
                {
                case SDL_BUTTON_LEFT:
                    InputManager::SetMouseButtonState(MouseButton::Left, state);
                    break;
                case SDL_BUTTON_RIGHT:
                    InputManager::SetMouseButtonState(MouseButton::Right, state);
                    break;
                case SDL_BUTTON_MIDDLE:
                    InputManager::SetMouseButtonState(MouseButton::Middle, state);
                    break;
                case SDL_BUTTON_X1:
                    InputManager::SetMouseButtonState(MouseButton::XButton1, state);
                    break;
                case SDL_BUTTON_X2:
                    InputManager::SetMouseButtonState(MouseButton::XButton2, state);
                    break;
                default:
                    break;
                }

                {
                    SDL_Window* win = (event.button.windowID != 0)
                                          ? SDL_GetWindowFromID(event.button.windowID)
                                          : SDL_GetMouseFocus();
                    const auto pos = to_logical_position(win, event.button.x, event.button.y);
                    InputManager::SetMousePosition(static_cast<int>(pos.X), static_cast<int>(pos.Y));
                }
                break;
            }
        case SDL_EVENT_MOUSE_WHEEL:
            InputManager::AddScrollWheelDelta(
                static_cast<int>(event.wheel.y * 120.0f)
            );
            break;
        case SDL_EVENT_KEY_DOWN:
        case SDL_EVENT_KEY_UP:
            {
                if (event.type == SDL_EVENT_KEY_DOWN && event.key.repeat)
                {
                    break;
                }

                const auto key = try_convert_sdl_key(event.key.key);

#ifdef __ANDROID__
                {
                    const char* evtName = (event.type == SDL_EVENT_KEY_DOWN) ? "KEY_DOWN" : "KEY_UP";
                    const char* keyName = SDL_GetKeyName(event.key.key);
                    if (key.has_value())
                    {
                        SDL_Log("[Keyboard] SDL_%s scancode=%d keycode=%d (0x%x) keyname='%s' mod=0x%x -> XNA Keys=%d",
                                evtName,
                                static_cast<int>(event.key.scancode),
                                static_cast<int>(event.key.key),
                                static_cast<unsigned>(event.key.key),
                                keyName ? keyName : "?",
                                static_cast<unsigned>(event.key.mod),
                                static_cast<int>(key.value()));
                    }
                    else
                    {
                        SDL_Log("[Keyboard] SDL_%s scancode=%d keycode=%d (0x%x) keyname='%s' mod=0x%x -> unmapped",
                                evtName,
                                static_cast<int>(event.key.scancode),
                                static_cast<int>(event.key.key),
                                static_cast<unsigned>(event.key.key),
                                keyName ? keyName : "?",
                                static_cast<unsigned>(event.key.mod));
                    }
                }
#endif

                if (!key.has_value())
                {
                    break;
                }

                const bool pressed = event.type == SDL_EVENT_KEY_DOWN;
                InputManager::SetKeyState(key.value(), pressed);

#ifdef __ANDROID__
                {
                    const auto snapshot = CNA::Internal::Input::InputManager::GetKeyboardState();
                    const auto allPressed = snapshot.GetPressedKeys();
                    std::string keyList;
                    for (const auto k : allPressed)
                    {
                        keyList += std::to_string(static_cast<int>(k));
                        keyList += ' ';
                    }
                    SDL_Log("[Keyboard] KeyboardState updated: XNA Keys=%d pressed=%s | total pressed=%zu [%s]",
                            static_cast<int>(key.value()),
                            pressed ? "true" : "false",
                            allPressed.size(),
                            keyList.c_str());
                }
#endif
                break;
            }
        case SDL_EVENT_TEXT_INPUT:
            {
                // SDL delivers UTF-8 text in event.text.text. CNA's TextInput callback is
                // char-based (byte-oriented), so forward each UTF-8 byte in order: a consumer
                // appending them to a std::string reconstructs the original UTF-8 text.
                // (FNA decodes to UTF-16 because C# strings are UTF-16; CNA uses UTF-8 std::string.)
                if (const char* text = event.text.text)
                {
                    for (const char* p = text; *p != '\0'; ++p)
                    {
                        Microsoft::Xna::Framework::Input::TextInputEXT::INTERNAL_OnTextInput(*p);
                    }
                }
                break;
            }
        case SDL_EVENT_TEXT_EDITING:
            {
                // IME composition draft text (UTF-8). Pass the bytes straight through to
                // CNA's UTF-8 std::string callback. FNA passes null for an empty composition;
                // CNA maps that to an empty string with start/length 0 (std::string& can't be null).
                if (event.edit.text != nullptr && event.edit.text[0] != '\0')
                {
                    Microsoft::Xna::Framework::Input::TextInputEXT::INTERNAL_OnTextEditing(
                        std::string(event.edit.text),
                        event.edit.start,
                        event.edit.length);
                }
                else
                {
                    Microsoft::Xna::Framework::Input::TextInputEXT::INTERNAL_OnTextEditing(
                        std::string(), 0, 0);
                }
                break;
            }
        case SDL_EVENT_FINGER_DOWN:
            {
                const int touchId = get_or_create_touch_id(event.tfinger.fingerID);
                InputManager::SetTouchState(
                    touchId,
                    TouchLocationState::Pressed,
                    to_touch_pixel_position(event.tfinger)
                );
                break;
            }
        case SDL_EVENT_FINGER_MOTION:
            {
                const int touchId = get_or_create_touch_id(event.tfinger.fingerID);
                InputManager::SetTouchState(
                    touchId,
                    TouchLocationState::Moved,
                    to_touch_pixel_position(event.tfinger)
                );
                break;
            }
        case SDL_EVENT_FINGER_UP:
            {
                const int touchId = try_get_touch_id(event.tfinger.fingerID).value_or(
                    get_or_create_touch_id(event.tfinger.fingerID)
                );

                InputManager::SetTouchState(
                    touchId,
                    TouchLocationState::Released,
                    to_touch_pixel_position(event.tfinger)
                );
                release_touch_id_mapping(event.tfinger.fingerID);
                break;
            }
        case SDL_EVENT_GAMEPAD_ADDED:
            {
                if (!SDL_IsGamepad(event.gdevice.which))
                {
                    break;
                }

                auto& gamepadToPlayerIndex = get_gamepad_to_player_index_map();
                if (gamepadToPlayerIndex.contains(event.gdevice.which))
                {
                    break;
                }

                const auto freeSlot = try_find_free_gamepad_slot();
                if (!freeSlot.has_value())
                {
                    break;
                }

                SDL_Gamepad* gamepad = SDL_OpenGamepad(event.gdevice.which);
                if (gamepad == nullptr)
                {
                    break;
                }

                const PlayerIndex playerIndex = slot_to_player_index(freeSlot.value());
                get_opened_gamepads()[freeSlot.value()] = gamepad;
                gamepadToPlayerIndex[event.gdevice.which] = playerIndex;
                InputManager::SetGamePadConnection(playerIndex, true);
                break;
            }
        case SDL_EVENT_GAMEPAD_REMOVED:
            {
                auto& gamepadToPlayerIndex = get_gamepad_to_player_index_map();
                const auto playerIndex = try_get_player_index_for_gamepad_id(event.gdevice.which);
                if (!playerIndex.has_value())
                {
                    break;
                }

                const auto slot = try_get_slot_for_player_index(playerIndex.value());
                if (slot.has_value())
                {
                    auto& openedGamePad = get_opened_gamepads()[slot.value()];
                    if (openedGamePad != nullptr)
                    {
                        SDL_CloseGamepad(openedGamePad);
                        openedGamePad = nullptr;
                    }
                }

                gamepadToPlayerIndex.erase(event.gdevice.which);
                InputManager::SetGamePadConnection(playerIndex.value(), false);
                break;
            }
        case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
        case SDL_EVENT_GAMEPAD_BUTTON_UP:
            {
                const auto playerIndex = try_get_player_index_for_gamepad_id(event.gbutton.which);
                if (!playerIndex.has_value())
                {
                    break;
                }

                const auto button = try_convert_sdl_gamepad_button(
                    static_cast<SDL_GamepadButton>(event.gbutton.button)
                );
                if (!button.has_value())
                {
                    break;
                }

                const auto state = event.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN
                                       ? ButtonState::Pressed
                                       : ButtonState::Released;

                InputManager::SetGamePadButtonState(playerIndex.value(), button.value(), state);
                break;
            }
        case SDL_EVENT_GAMEPAD_AXIS_MOTION:
            {
                const auto playerIndex = try_get_player_index_for_gamepad_id(event.gaxis.which);
                if (!playerIndex.has_value())
                {
                    break;
                }

                const auto axis = try_convert_sdl_gamepad_axis(
                    static_cast<SDL_GamepadAxis>(event.gaxis.axis)
                );
                if (!axis.has_value())
                {
                    break;
                }

                float value = 0.0f;
                switch (axis.value())
                {
                case GamePadAxis::LeftThumbstickX:
                case GamePadAxis::RightThumbstickX:
                    value = normalize_stick_axis(event.gaxis.value);
                    break;
                case GamePadAxis::LeftThumbstickY:
                case GamePadAxis::RightThumbstickY:
                    value = -normalize_stick_axis(event.gaxis.value);
                    break;
                case GamePadAxis::LeftTrigger:
                case GamePadAxis::RightTrigger:
                    value = normalize_trigger_axis(event.gaxis.value);
                    break;
                }

                InputManager::SetGamePadAxisValue(playerIndex.value(), axis.value(), value);
                break;
            }
        default:
            break;
        }
    }
}
