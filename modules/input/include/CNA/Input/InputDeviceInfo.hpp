// SPDX-License-Identifier: MS-PL
#pragma once

#include "CNA/CNAHelper.hpp"

#include <cstdint>
#include <string>

namespace CNA::Input
{
    /**
     * @brief CNAEXT — identity of one enumerated input device (mouse, keyboard, or touch device).
     *
     * XNA 4.0 assumes a single keyboard/mouse and four gamepads; it has no device enumeration. This
     * CNA extension descriptor pairs an SDL device instance id with its human-readable name. It is
     * metadata only — XNA input state stays merged across devices.
     */
    CNAEXT struct InputDeviceInfoEXT
    {
        /** @brief The stable platform device instance id. */
        std::uint64_t id = 0;

        /** @brief The device's human-readable name, or empty if SDL reports none. */
        std::string name;
    };

    /**
     * @brief Compares two device descriptors for equality (id and name).
     * @param left The left operand.
     * @param right The right operand.
     * @return True if both id and name are equal.
     */
    [[nodiscard]] inline bool operator==(const InputDeviceInfoEXT& left, const InputDeviceInfoEXT& right)
    {
        return left.id == right.id && left.name == right.name;
    }

    /**
     * @brief Compares two device descriptors for inequality.
     * @param left The left operand.
     * @param right The right operand.
     * @return True if the descriptors differ.
     */
    [[nodiscard]] inline bool operator!=(const InputDeviceInfoEXT& left, const InputDeviceInfoEXT& right)
    {
        return !(left == right);
    }
}
