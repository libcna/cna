// SPDX-License-Identifier: MS-PL
#pragma once

#include "CNA/CNAHelper.hpp"
#include "CNA/Input/JoystickType.hpp"

#include <cstdint>
#include <string>

namespace CNA::Input
{
    /**
     * @brief CNAEXT — identity of one enumerated raw joystick device.
     *
     * XNA 4.0 only ever modeled Xbox-style mapped gamepads (`GamePad`); it has no notion of a raw
     * joystick (flight sticks, wheels, throttles, arbitrary HID controllers). This descriptor pairs
     * the platform device id with its human-readable name and physical type.
     */
    CNAEXT struct JoystickInfoEXT
    {
        /** @brief The platform device id, stable while this joystick stays connected. */
        std::uint32_t id = 0;

        /** @brief The device's human-readable name, or empty if the platform reports none. */
        std::string name;

        /** @brief The device's physical category. */
        JoystickTypeEXT type = JoystickTypeEXT::Unknown;
    };

    /**
     * @brief Compares two joystick descriptors for equality (id, name, and type).
     * @param left The left operand.
     * @param right The right operand.
     * @return True if all fields are equal.
     */
    [[nodiscard]] inline bool operator==(const JoystickInfoEXT& left, const JoystickInfoEXT& right)
    {
        return left.id == right.id && left.name == right.name && left.type == right.type;
    }

    /**
     * @brief Compares two joystick descriptors for inequality.
     * @param left The left operand.
     * @param right The right operand.
     * @return True if the descriptors differ.
     */
    [[nodiscard]] inline bool operator!=(const JoystickInfoEXT& left, const JoystickInfoEXT& right)
    {
        return !(left == right);
    }
}
