// SPDX-License-Identifier: MS-PL
#pragma once

#include "CNA/CNAHelper.hpp"
#include "CNA/Input/JoystickType.hpp"
#include "CNA/Input/PowerState.hpp"

#include <string>

namespace CNA::Input
{
    /**
     * @brief CNAEXT — the static hardware shape and identity of a raw joystick device.
     */
    CNAEXT struct JoystickCapabilitiesEXT
    {
        /** @brief True if a joystick with this instance id is currently connected. */
        bool isConnected = false;

        /** @brief The number of axes the device reports. */
        int axisCount = 0;

        /** @brief The number of buttons the device reports. */
        int buttonCount = 0;

        /** @brief The number of POV hats the device reports. */
        int hatCount = 0;

        /** @brief The number of trackballs the device reports. */
        int ballCount = 0;

        /** @brief The device's physical category. */
        JoystickTypeEXT type = JoystickTypeEXT::Unknown;

        /** @brief The device's human-readable name, or empty if the platform reports none. */
        std::string name;

        /** @brief The device's lowercase hexadecimal hardware GUID (empty if disconnected). */
        std::string guid;

        /** @brief The device's battery/charge state (`PowerStateEXT::Unknown` if disconnected). */
        PowerStateEXT powerState = PowerStateEXT::Unknown;

        /** @brief Battery charge percent (0-100), or -1 if unknown/disconnected. */
        int powerPercent = -1;
    };

    /**
     * @brief Compares two joystick capability snapshots for equality (every field).
     * @param left The left operand.
     * @param right The right operand.
     * @return True if all fields are equal.
     */
    [[nodiscard]] inline bool operator==(const JoystickCapabilitiesEXT& left, const JoystickCapabilitiesEXT& right)
    {
        return left.isConnected == right.isConnected
            && left.axisCount == right.axisCount
            && left.buttonCount == right.buttonCount
            && left.hatCount == right.hatCount
            && left.ballCount == right.ballCount
            && left.type == right.type
            && left.name == right.name
            && left.guid == right.guid
            && left.powerState == right.powerState
            && left.powerPercent == right.powerPercent;
    }

    /**
     * @brief Compares two joystick capability snapshots for inequality.
     * @param left The left operand.
     * @param right The right operand.
     * @return True if any field differs.
     */
    [[nodiscard]] inline bool operator!=(const JoystickCapabilitiesEXT& left, const JoystickCapabilitiesEXT& right)
    {
        return !(left == right);
    }
}
