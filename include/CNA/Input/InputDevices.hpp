// SPDX-License-Identifier: MS-PL
#pragma once

#include "CNA/CNAHelper.hpp"
#include "CNA/Input/InputDeviceInfo.hpp"

#include <vector>

namespace CNA::Input
{
    /**
     * @brief NOXNA — enumerates the connected input devices, backed by SDL3.
     *
     * XNA 4.0 assumes exactly one keyboard, one mouse, and four gamepads, and exposes no device list.
     * This CNA extension enumerates every mouse, keyboard, and touch device with its id and name. It
     * is metadata only: XNA input state stays merged across devices (this is not per-device routing).
     * Platform notes: desktop (Windows/Linux) enumerate fully; macOS is partial; Android/Web expose a
     * single logical device (the lists are typically short or empty).
     */
    NOXNA class InputDevices
    {
    public:
        /** @brief Static-only utility; not instantiable. */
        InputDevices() = delete;

        /**
         * @brief Enumerates the connected mice.
         * @return A list of mouse id/name descriptors (empty if none or unsupported).
         */
        NOXNA [[nodiscard]] static std::vector<InputDeviceInfoEXT> GetMiceEXT();

        /**
         * @brief Enumerates the connected keyboards.
         * @return A list of keyboard id/name descriptors (empty if none or unsupported).
         */
        NOXNA [[nodiscard]] static std::vector<InputDeviceInfoEXT> GetKeyboardsEXT();

        /**
         * @brief Enumerates the connected touch devices.
         * @return A list of touch-device id/name descriptors (empty if none or unsupported).
         */
        NOXNA [[nodiscard]] static std::vector<InputDeviceInfoEXT> GetTouchDevicesEXT();
    };
}
