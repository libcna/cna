// SPDX-License-Identifier: MS-PL
#pragma once

#include "CNA/CNAHelper.hpp"

namespace CNA::Input
{
    /**
     * @brief CNAEXT — the position of a joystick's POV hat (D-pad-like 8-way switch).
     *
     * Mirrors SDL3's `SDL_HAT_*` values, which combine an up/down bit with a left/right bit; the
     * nine reachable combinations are enumerated here rather than exposed as a bit flag set.
     */
    CNAEXT enum class JoystickHatPositionEXT
    {
        /** @brief The hat is not pressed in any direction. */
        Centered,
        /** @brief The hat is pressed up. */
        Up,
        /** @brief The hat is pressed right. */
        Right,
        /** @brief The hat is pressed down. */
        Down,
        /** @brief The hat is pressed left. */
        Left,
        /** @brief The hat is pressed up and to the right. */
        RightUp,
        /** @brief The hat is pressed down and to the right. */
        RightDown,
        /** @brief The hat is pressed up and to the left. */
        LeftUp,
        /** @brief The hat is pressed down and to the left. */
        LeftDown
    };
}
