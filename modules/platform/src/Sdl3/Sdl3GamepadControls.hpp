// SPDX-License-Identifier: MS-PL
#pragma once

#include "CNA/Platform/PlatformEvent.hpp"

#include <SDL3/SDL_gamepad.h>

#include <algorithm>
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

} // namespace CNA::Platform::Sdl3
