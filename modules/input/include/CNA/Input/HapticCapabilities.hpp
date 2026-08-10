// SPDX-License-Identifier: MS-PL
#pragma once

#include "CNA/CNAHelper.hpp"
#include "CNA/Input/HapticFeature.hpp"

#include <string>

namespace CNA::Input
{
    /**
     * @brief NOXNA — the static hardware shape of an opened haptic device.
     */
    NOXNA struct HapticCapabilitiesEXT
    {
        /** @brief True if the device handle is currently open. */
        bool isOpen = false;

        /** @brief The device's human-readable name, or empty if SDL reports none. */
        std::string name;

        /** @brief The effect families and global capabilities the device supports. */
        HapticFeatureEXT features = HapticFeatureEXT::None;

        /** @brief Number of axes the device reports. */
        int axisCount = 0;

        /** @brief Maximum number of effects the device can store, or -1 if closed/unknown. */
        int maxEffects = -1;

        /** @brief Maximum number of effects the device can play simultaneously, or -1 if closed/unknown. */
        int maxEffectsPlaying = -1;

        /** @brief True if the simple `PlayRumbleEXT` convenience is supported. */
        bool rumbleSupported = false;
    };

    /**
     * @brief Compares two haptic capability snapshots for equality (every field).
     * @param left The left operand.
     * @param right The right operand.
     * @return True if all fields are equal.
     */
    [[nodiscard]] inline bool operator==(const HapticCapabilitiesEXT& left, const HapticCapabilitiesEXT& right)
    {
        return left.isOpen == right.isOpen
            && left.name == right.name
            && left.features == right.features
            && left.axisCount == right.axisCount
            && left.maxEffects == right.maxEffects
            && left.maxEffectsPlaying == right.maxEffectsPlaying
            && left.rumbleSupported == right.rumbleSupported;
    }

    /**
     * @brief Compares two haptic capability snapshots for inequality.
     * @param left The left operand.
     * @param right The right operand.
     * @return True if any field differs.
     */
    [[nodiscard]] inline bool operator!=(const HapticCapabilitiesEXT& left, const HapticCapabilitiesEXT& right)
    {
        return !(left == right);
    }
}
