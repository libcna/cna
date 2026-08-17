// SPDX-License-Identifier: MS-PL
#pragma once

#include "CNA/CNAHelper.hpp"
#include "CNA/Input/JoystickHatPosition.hpp"
#include "Microsoft/Xna/Framework/Point.hpp"

#include <cstdint>
#include <vector>

namespace CNA::Input
{
    /**
     * @brief CNAEXT — a snapshot of a raw joystick's current axis/button/hat/trackball state.
     *
     * Unlike `GamePadState`, values here are raw and unmapped: axis order, button numbering, and hat
     * count are whatever the hardware/driver reports, with no XNA-style semantic assignment
     * (LeftThumbstick, A button, …). Games that need mapped semantics should use `GamePad` instead.
     */
    CNAEXT struct JoystickStateEXT
    {
        /** @brief Raw signed axis values (-32768 to 32767), one per axis. */
        std::vector<std::int16_t> axes;

        /** @brief Button down/up state, one per button. */
        std::vector<bool> buttons;

        /** @brief POV hat positions, one per hat. */
        std::vector<JoystickHatPositionEXT> hats;

        /** @brief Trackball relative motion published by the latest platform update, one per ball. */
        std::vector<Microsoft::Xna::Framework::Point> balls;
    };

    /**
     * @brief Compares two joystick state snapshots for equality (every field).
     * @param left The left operand.
     * @param right The right operand.
     * @return True if all fields are equal.
     */
    [[nodiscard]] inline bool operator==(const JoystickStateEXT& left, const JoystickStateEXT& right)
    {
        return left.axes == right.axes
            && left.buttons == right.buttons
            && left.hats == right.hats
            && left.balls == right.balls;
    }

    /**
     * @brief Compares two joystick state snapshots for inequality.
     * @param left The left operand.
     * @param right The right operand.
     * @return True if any field differs.
     */
    [[nodiscard]] inline bool operator!=(const JoystickStateEXT& left, const JoystickStateEXT& right)
    {
        return !(left == right);
    }
}
