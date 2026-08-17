// SPDX-License-Identifier: MS-PL
#pragma once

#include "CNA/CNAHelper.hpp"

#include <cstdint>
#include <string>

namespace CNA::Input
{
    /**
     * @brief CNAEXT — identity of one enumerated haptic (force-feedback) device.
     *
     * XNA 4.0 has no force-feedback API beyond `GamePad::SetVibration`'s simple rumble; this
     * descriptor pairs the platform device id with its human-readable name.
     */
    CNAEXT struct HapticInfoEXT
    {
        /** @brief The platform device id, stable while the device remains connected. */
        std::uint32_t id = 0;

        /** @brief The device's human-readable name, or empty if the platform reports none. */
        std::string name;
    };

    /**
     * @brief Compares two haptic device descriptors for equality (id and name).
     * @param left The left operand.
     * @param right The right operand.
     * @return True if both fields are equal.
     */
    [[nodiscard]] inline bool operator==(const HapticInfoEXT& left, const HapticInfoEXT& right)
    {
        return left.id == right.id && left.name == right.name;
    }

    /**
     * @brief Compares two haptic device descriptors for inequality.
     * @param left The left operand.
     * @param right The right operand.
     * @return True if the descriptors differ.
     */
    [[nodiscard]] inline bool operator!=(const HapticInfoEXT& left, const HapticInfoEXT& right)
    {
        return !(left == right);
    }
}
