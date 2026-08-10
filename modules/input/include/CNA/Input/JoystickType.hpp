// SPDX-License-Identifier: MS-PL
#pragma once

#include "CNA/CNAHelper.hpp"

namespace CNA::Input
{
    /**
     * @brief CNAEXT — the physical category of a raw joystick device. Mirrors SDL3's `SDL_JoystickType`.
     */
    CNAEXT enum class JoystickTypeEXT
    {
        /** @brief Unknown or unrecognized joystick type. */
        Unknown,
        /** @brief A device SDL also maps as a gamepad. */
        Gamepad,
        /** @brief A steering wheel. */
        Wheel,
        /** @brief An arcade-style stick. */
        ArcadeStick,
        /** @brief A flight stick (HOTAS-style). */
        FlightStick,
        /** @brief A dance pad. */
        DancePad,
        /** @brief A guitar-shaped controller. */
        Guitar,
        /** @brief A drum-kit controller. */
        DrumKit,
        /** @brief An arcade-cabinet pad. */
        ArcadePad,
        /** @brief A throttle quadrant. */
        Throttle
    };
}
