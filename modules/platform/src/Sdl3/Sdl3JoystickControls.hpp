// SPDX-License-Identifier: MS-PL
#pragma once

#include "CNA/Platform/Input/IPlatformJoystick.hpp"

#include <SDL3/SDL.h>

namespace CNA::Platform::Sdl3 {

    /** @brief Maps an SDL raw-device category into CNA's platform vocabulary. */
    [[nodiscard]] inline JoystickKind ToJoystickKind(const SDL_JoystickType type)
    {
        switch (type)
        {
            case SDL_JOYSTICK_TYPE_GAMEPAD:      return JoystickKind::Gamepad;
            case SDL_JOYSTICK_TYPE_WHEEL:        return JoystickKind::Wheel;
            case SDL_JOYSTICK_TYPE_ARCADE_STICK: return JoystickKind::ArcadeStick;
            case SDL_JOYSTICK_TYPE_FLIGHT_STICK: return JoystickKind::FlightStick;
            case SDL_JOYSTICK_TYPE_DANCE_PAD:    return JoystickKind::DancePad;
            case SDL_JOYSTICK_TYPE_GUITAR:       return JoystickKind::Guitar;
            case SDL_JOYSTICK_TYPE_DRUM_KIT:     return JoystickKind::DrumKit;
            case SDL_JOYSTICK_TYPE_ARCADE_PAD:   return JoystickKind::ArcadePad;
            case SDL_JOYSTICK_TYPE_THROTTLE:     return JoystickKind::Throttle;
            case SDL_JOYSTICK_TYPE_UNKNOWN:      return JoystickKind::Unknown;
        }
        return JoystickKind::Unknown;
    }

    /** @brief Maps SDL's POV bit layout into CNA's explicit nine-position vocabulary. */
    [[nodiscard]] inline JoystickHat ToJoystickHat(const Uint8 hat)
    {
        switch (hat)
        {
            case SDL_HAT_UP:        return JoystickHat::Up;
            case SDL_HAT_RIGHT:     return JoystickHat::Right;
            case SDL_HAT_DOWN:      return JoystickHat::Down;
            case SDL_HAT_LEFT:      return JoystickHat::Left;
            case SDL_HAT_RIGHTUP:   return JoystickHat::RightUp;
            case SDL_HAT_RIGHTDOWN: return JoystickHat::RightDown;
            case SDL_HAT_LEFTUP:    return JoystickHat::LeftUp;
            case SDL_HAT_LEFTDOWN:  return JoystickHat::LeftDown;
            case SDL_HAT_CENTERED:  return JoystickHat::Centered;
            default:                return JoystickHat::Centered;
        }
    }

    /** @brief Maps SDL's battery answer into CNA's platform vocabulary. */
    [[nodiscard]] inline JoystickPowerState ToJoystickPowerState(const SDL_PowerState state)
    {
        switch (state)
        {
            case SDL_POWERSTATE_ON_BATTERY: return JoystickPowerState::OnBattery;
            case SDL_POWERSTATE_NO_BATTERY: return JoystickPowerState::NoBattery;
            case SDL_POWERSTATE_CHARGING:   return JoystickPowerState::Charging;
            case SDL_POWERSTATE_CHARGED:    return JoystickPowerState::Charged;
            case SDL_POWERSTATE_UNKNOWN:    return JoystickPowerState::Unknown;
            case SDL_POWERSTATE_ERROR:      return JoystickPowerState::Error;
        }
        return JoystickPowerState::Error;
    }

} // namespace CNA::Platform::Sdl3
