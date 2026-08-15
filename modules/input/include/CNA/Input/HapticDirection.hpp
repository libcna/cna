// SPDX-License-Identifier: MS-PL
#pragma once

#include "CNA/CNAHelper.hpp"

#include <array>
#include <cstdint>

namespace CNA::Input
{
    /**
     * @brief CNAEXT — the coordinate system used to encode a haptic effect's direction. Mirrors
     *        the platform contract's haptic direction type.
     */
    CNAEXT enum class HapticDirectionTypeEXT
    {
        /** @brief Direction is a single polar angle (hundredths of a degree, clockwise from north). */
        Polar,
        /** @brief Direction is an (X, Y, Z) cartesian vector. */
        Cartesian,
        /** @brief Direction is two spherical rotation angles. */
        Spherical,
        /** @brief Play the effect on the device's steering-wheel axis (best cross-platform guess). */
        SteeringAxis
    };

    /**
     * @brief CNAEXT — the direction a haptic effect's force comes from. Mirrors SDL3's
     *        `CNA::Platform::HapticDirection`.
     */
    CNAEXT struct HapticDirectionEXT
    {
        /** @brief The coordinate system the `values` are encoded in. */
        HapticDirectionTypeEXT type = HapticDirectionTypeEXT::Polar;

        /** @brief The encoded direction; how many components are used depends on `type`. */
        std::array<std::int32_t, 3> values{};
    };

    /**
     * @brief Compares two haptic directions for equality (type and values).
     * @param left The left operand.
     * @param right The right operand.
     * @return True if both fields are equal.
     */
    [[nodiscard]] inline bool operator==(const HapticDirectionEXT& left, const HapticDirectionEXT& right)
    {
        return left.type == right.type && left.values == right.values;
    }

    /**
     * @brief Compares two haptic directions for inequality.
     * @param left The left operand.
     * @param right The right operand.
     * @return True if the directions differ.
     */
    [[nodiscard]] inline bool operator!=(const HapticDirectionEXT& left, const HapticDirectionEXT& right)
    {
        return !(left == right);
    }
}
