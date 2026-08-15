// SPDX-License-Identifier: MS-PL
#pragma once

#include "CNA/CNAHelper.hpp"

namespace CNA::Input
{
    /**
     * @brief CNAEXT — the position of a joystick's POV hat (D-pad-like 8-way switch).
     *
     * Native APIs often encode hats as combined direction bits; the nine reachable combinations
     * are enumerated here so no native layout becomes public API.
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
