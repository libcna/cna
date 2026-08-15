// SPDX-License-Identifier: MS-PL
#pragma once

#include "CNA/Platform/Input/IPlatformGamepad.hpp"

#include <SDL3/SDL_gamepad.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <optional>

namespace CNA::Platform::Sdl3 {

    /** @brief Translates one SDL gamepad axis into CNA's platform-independent vocabulary. */
    inline std::optional<GamepadAxis> ToGamepadAxis(const SDL_GamepadAxis axis)
    {
        switch (axis)
        {
            case SDL_GAMEPAD_AXIS_LEFTX:         return GamepadAxis::LeftThumbstickX;
            case SDL_GAMEPAD_AXIS_LEFTY:         return GamepadAxis::LeftThumbstickY;
            case SDL_GAMEPAD_AXIS_RIGHTX:        return GamepadAxis::RightThumbstickX;
            case SDL_GAMEPAD_AXIS_RIGHTY:        return GamepadAxis::RightThumbstickY;
            case SDL_GAMEPAD_AXIS_LEFT_TRIGGER:  return GamepadAxis::LeftTrigger;
            case SDL_GAMEPAD_AXIS_RIGHT_TRIGGER: return GamepadAxis::RightTrigger;
            default:                             return std::nullopt;
        }
    }

    /** @brief Translates one SDL gamepad button into CNA's platform-independent vocabulary. */
    inline std::optional<GamepadButton> ToGamepadButton(const SDL_GamepadButton button)
    {
        switch (button)
        {
            case SDL_GAMEPAD_BUTTON_SOUTH:          return GamepadButton::A;
            case SDL_GAMEPAD_BUTTON_EAST:           return GamepadButton::B;
            case SDL_GAMEPAD_BUTTON_WEST:           return GamepadButton::X;
            case SDL_GAMEPAD_BUTTON_NORTH:          return GamepadButton::Y;
            case SDL_GAMEPAD_BUTTON_BACK:           return GamepadButton::Back;
            case SDL_GAMEPAD_BUTTON_START:          return GamepadButton::Start;
            case SDL_GAMEPAD_BUTTON_LEFT_SHOULDER:  return GamepadButton::LeftShoulder;
            case SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER: return GamepadButton::RightShoulder;
            case SDL_GAMEPAD_BUTTON_LEFT_STICK:     return GamepadButton::LeftStick;
            case SDL_GAMEPAD_BUTTON_RIGHT_STICK:    return GamepadButton::RightStick;
            case SDL_GAMEPAD_BUTTON_DPAD_UP:        return GamepadButton::DPadUp;
            case SDL_GAMEPAD_BUTTON_DPAD_DOWN:      return GamepadButton::DPadDown;
            case SDL_GAMEPAD_BUTTON_DPAD_LEFT:      return GamepadButton::DPadLeft;
            case SDL_GAMEPAD_BUTTON_DPAD_RIGHT:     return GamepadButton::DPadRight;
            case SDL_GAMEPAD_BUTTON_GUIDE:          return GamepadButton::BigButton;
            case SDL_GAMEPAD_BUTTON_MISC1:          return GamepadButton::Misc1;
            case SDL_GAMEPAD_BUTTON_RIGHT_PADDLE1:  return GamepadButton::Paddle1;
            case SDL_GAMEPAD_BUTTON_LEFT_PADDLE1:   return GamepadButton::Paddle2;
            case SDL_GAMEPAD_BUTTON_RIGHT_PADDLE2:  return GamepadButton::Paddle3;
            case SDL_GAMEPAD_BUTTON_LEFT_PADDLE2:   return GamepadButton::Paddle4;
            case SDL_GAMEPAD_BUTTON_TOUCHPAD:       return GamepadButton::TouchPad;
            default:                                return std::nullopt;
        }
    }

    /** @brief Translates one CNA button back to SDL for label queries. */
    inline std::optional<SDL_GamepadButton> ToSdlGamepadButton(const GamepadButton button)
    {
        switch (button)
        {
            case GamepadButton::A:             return SDL_GAMEPAD_BUTTON_SOUTH;
            case GamepadButton::B:             return SDL_GAMEPAD_BUTTON_EAST;
            case GamepadButton::X:             return SDL_GAMEPAD_BUTTON_WEST;
            case GamepadButton::Y:             return SDL_GAMEPAD_BUTTON_NORTH;
            case GamepadButton::Back:          return SDL_GAMEPAD_BUTTON_BACK;
            case GamepadButton::Start:         return SDL_GAMEPAD_BUTTON_START;
            case GamepadButton::LeftShoulder:  return SDL_GAMEPAD_BUTTON_LEFT_SHOULDER;
            case GamepadButton::RightShoulder: return SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER;
            case GamepadButton::LeftStick:     return SDL_GAMEPAD_BUTTON_LEFT_STICK;
            case GamepadButton::RightStick:    return SDL_GAMEPAD_BUTTON_RIGHT_STICK;
            case GamepadButton::DPadUp:        return SDL_GAMEPAD_BUTTON_DPAD_UP;
            case GamepadButton::DPadDown:      return SDL_GAMEPAD_BUTTON_DPAD_DOWN;
            case GamepadButton::DPadLeft:      return SDL_GAMEPAD_BUTTON_DPAD_LEFT;
            case GamepadButton::DPadRight:     return SDL_GAMEPAD_BUTTON_DPAD_RIGHT;
            case GamepadButton::BigButton:     return SDL_GAMEPAD_BUTTON_GUIDE;
            case GamepadButton::Misc1:         return SDL_GAMEPAD_BUTTON_MISC1;
            case GamepadButton::Paddle1:       return SDL_GAMEPAD_BUTTON_RIGHT_PADDLE1;
            case GamepadButton::Paddle2:       return SDL_GAMEPAD_BUTTON_LEFT_PADDLE1;
            case GamepadButton::Paddle3:       return SDL_GAMEPAD_BUTTON_RIGHT_PADDLE2;
            case GamepadButton::Paddle4:       return SDL_GAMEPAD_BUTTON_LEFT_PADDLE2;
            case GamepadButton::TouchPad:      return SDL_GAMEPAD_BUTTON_TOUCHPAD;
        }
        return std::nullopt;
    }

    /** @brief Maps SDL's physical joystick class into the platform contract. */
    inline GamepadKind ToGamepadKind(const SDL_JoystickType type)
    {
        switch (type)
        {
            case SDL_JOYSTICK_TYPE_GAMEPAD:      return GamepadKind::Gamepad;
            case SDL_JOYSTICK_TYPE_WHEEL:        return GamepadKind::Wheel;
            case SDL_JOYSTICK_TYPE_ARCADE_STICK: return GamepadKind::ArcadeStick;
            case SDL_JOYSTICK_TYPE_FLIGHT_STICK: return GamepadKind::FlightStick;
            case SDL_JOYSTICK_TYPE_DANCE_PAD:    return GamepadKind::DancePad;
            case SDL_JOYSTICK_TYPE_GUITAR:       return GamepadKind::Guitar;
            case SDL_JOYSTICK_TYPE_DRUM_KIT:     return GamepadKind::DrumKit;
            case SDL_JOYSTICK_TYPE_ARCADE_PAD:   return GamepadKind::BigButtonPad;
            default:                             return GamepadKind::Unknown;
        }
    }

    /** @brief Maps SDL's emulated controller family into CNA-owned identity vocabulary. */
    inline GamepadModel ToGamepadModel(const SDL_GamepadType type)
    {
        switch (type)
        {
            case SDL_GAMEPAD_TYPE_STANDARD:                     return GamepadModel::Standard;
            case SDL_GAMEPAD_TYPE_XBOX360:                      return GamepadModel::Xbox360;
            case SDL_GAMEPAD_TYPE_XBOXONE:                      return GamepadModel::XboxOne;
            case SDL_GAMEPAD_TYPE_PS3:                          return GamepadModel::PlayStation3;
            case SDL_GAMEPAD_TYPE_PS4:                          return GamepadModel::PlayStation4;
            case SDL_GAMEPAD_TYPE_PS5:                          return GamepadModel::PlayStation5;
            case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_PRO:          return GamepadModel::NintendoSwitchPro;
            case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_LEFT:  return GamepadModel::NintendoSwitchJoyConLeft;
            case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_RIGHT: return GamepadModel::NintendoSwitchJoyConRight;
            case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_PAIR:  return GamepadModel::NintendoSwitchJoyConPair;
            case SDL_GAMEPAD_TYPE_GAMECUBE:                     return GamepadModel::GameCube;
            default:                                            return GamepadModel::Unknown;
        }
    }

    /** @brief Maps an SDL connection state without leaking the SDL enum. */
    inline GamepadConnectionState ToGamepadConnectionState(const SDL_JoystickConnectionState state)
    {
        switch (state)
        {
            case SDL_JOYSTICK_CONNECTION_WIRED:    return GamepadConnectionState::Wired;
            case SDL_JOYSTICK_CONNECTION_WIRELESS: return GamepadConnectionState::Wireless;
            default:                               return GamepadConnectionState::Unknown;
        }
    }

    /** @brief Maps an SDL battery state without leaking the SDL enum. */
    inline GamepadPowerState ToGamepadPowerState(const SDL_PowerState state)
    {
        switch (state)
        {
            case SDL_POWERSTATE_ON_BATTERY: return GamepadPowerState::OnBattery;
            case SDL_POWERSTATE_CHARGING:   return GamepadPowerState::Charging;
            case SDL_POWERSTATE_CHARGED:    return GamepadPowerState::Charged;
            case SDL_POWERSTATE_NO_BATTERY: return GamepadPowerState::NoBattery;
            case SDL_POWERSTATE_UNKNOWN:    return GamepadPowerState::Unknown;
            default:                        return GamepadPowerState::Error;
        }
    }

    /** @brief Maps the glyph SDL reports for a controller button. */
    inline GamepadButtonLabel ToGamepadButtonLabel(const SDL_GamepadButtonLabel label)
    {
        switch (label)
        {
            case SDL_GAMEPAD_BUTTON_LABEL_A:        return GamepadButtonLabel::A;
            case SDL_GAMEPAD_BUTTON_LABEL_B:        return GamepadButtonLabel::B;
            case SDL_GAMEPAD_BUTTON_LABEL_X:        return GamepadButtonLabel::X;
            case SDL_GAMEPAD_BUTTON_LABEL_Y:        return GamepadButtonLabel::Y;
            case SDL_GAMEPAD_BUTTON_LABEL_CROSS:    return GamepadButtonLabel::Cross;
            case SDL_GAMEPAD_BUTTON_LABEL_CIRCLE:   return GamepadButtonLabel::Circle;
            case SDL_GAMEPAD_BUTTON_LABEL_SQUARE:   return GamepadButtonLabel::Square;
            case SDL_GAMEPAD_BUTTON_LABEL_TRIANGLE: return GamepadButtonLabel::Triangle;
            default:                                return GamepadButtonLabel::Unknown;
        }
    }

    /**
     * @brief Normalises an SDL signed axis exactly as CNA's existing input bridge does.
     *
     * Both signed halves use 32767, followed by a clamp of the single over-range -32768
     * endpoint. Thumbstick Y is inverted into XNA's Y-up convention; triggers are clamped to
     * [0, 1]. Keeping this in one helper prevents event and snapshot paths from drifting.
     */
    inline float NormalizeGamepadAxis(const GamepadAxis axis, const Sint16 value)
    {
        const float stick = std::clamp(static_cast<float>(value) / 32767.0f, -1.0f, 1.0f);
        switch (axis)
        {
            case GamepadAxis::LeftThumbstickX:
            case GamepadAxis::RightThumbstickX:
                return stick;
            case GamepadAxis::LeftThumbstickY:
            case GamepadAxis::RightThumbstickY:
                return -stick;
            case GamepadAxis::LeftTrigger:
            case GamepadAxis::RightTrigger:
                return std::clamp(static_cast<float>(value) / 32767.0f, 0.0f, 1.0f);
        }
        return 0.0f;
    }

    /** @brief Clamps a public motor strength and maps NaN to off before SDL's 16-bit conversion. */
    inline std::uint16_t NormalizeGamepadMotorLevel(const float value)
    {
        if (std::isnan(value))
        {
            return 0;
        }
        return static_cast<std::uint16_t>(
            std::clamp(value, 0.0f, 1.0f) * 65535.0f);
    }

} // namespace CNA::Platform::Sdl3
