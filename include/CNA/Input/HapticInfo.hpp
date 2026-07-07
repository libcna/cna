// SPDX-License-Identifier: MS-PL
#pragma once

#include "CNA/CNAHelper.hpp"

#include <cstdint>
#include <string>

namespace CNA::Input
{
    /**
     * @brief NOXNA — identity of one enumerated haptic (force-feedback) device.
     *
     * XNA 4.0 has no force-feedback API beyond `GamePad::SetVibration`'s simple rumble; this
     * descriptor pairs the SDL haptic instance id with its human-readable name.
     */
    NOXNA struct HapticInfoEXT
    {
        /** @brief The SDL haptic instance id (`SDL_HapticID`). */
        std::uint32_t id = 0;

        /** @brief The device's human-readable name, or empty if SDL reports none. */
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
