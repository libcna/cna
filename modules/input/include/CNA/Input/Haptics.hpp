// SPDX-License-Identifier: MS-PL
#pragma once

#include "CNA/CNAHelper.hpp"
#include "CNA/Input/HapticDevice.hpp"
#include "CNA/Input/HapticInfo.hpp"

#include <cstdint>
#include <vector>

namespace CNA::Input
{
    /**
     * @brief CNAEXT — force-feedback (haptic) device enumeration and opening, backed by SDL3's
     *        `SDL_haptic` API.
     *
     * XNA 4.0 has no force-feedback API beyond `GamePad::SetVibration`'s simple dual-motor rumble.
     * This CNA extension exposes SDL3's richer model: device enumeration, opening a device standalone
     * or from an already-connected joystick/mouse, and (via the returned `HapticDevice`) the full
     * effect-building/playback lifecycle. Real actuation requires physical force-feedback hardware
     * (a wheel or joystick with actuators) — most gamepads only support simple rumble, not this API.
     */
    CNAEXT class Haptics
    {
    public:
        /** @brief Static-only utility; not instantiable. */
        Haptics() = delete;

        /**
         * @brief Enumerates the connected haptic devices.
         * @return A list of haptic id/name descriptors (empty if none connected).
         */
        CNAEXT [[nodiscard]] static std::vector<HapticInfoEXT> GetHapticsEXT();

        /**
         * @brief Opens a standalone haptic device by its enumeration id.
         * @param id A device id from `GetHapticsEXT()`.
         * @return The opened device; `IsOpenEXT()` is false if opening failed.
         */
        CNAEXT [[nodiscard]] static HapticDevice OpenEXT(std::uint32_t id);

        /**
         * @brief Opens the haptic device backing an already-connected raw joystick, if it has one.
         *
         * The common case for wheels/HOTAS setups: pair with `CNA::Input::Joysticks`'s enumeration to
         * find the joystick id first.
         *
         * @param joystickId The platform device id from `Joysticks::GetJoysticksEXT()`.
         * @return The opened device; `IsOpenEXT()` is false if the joystick is not connected, has no
         *         haptic capability, or opening failed.
         */
        CNAEXT [[nodiscard]] static HapticDevice OpenFromJoystickEXT(std::uint32_t joystickId);

        /**
         * @brief Opens the default mouse's haptic device, if it has one.
         * @return The opened device; `IsOpenEXT()` is false if the mouse has no haptic capability or
         *         opening failed.
         */
        CNAEXT [[nodiscard]] static HapticDevice OpenFromMouseEXT();

        /**
         * @brief Returns true if the given connected raw joystick has haptic capability.
         * @param joystickId The platform device id reported by raw-joystick enumeration.
         */
        CNAEXT [[nodiscard]] static bool IsJoystickHapticEXT(std::uint32_t joystickId);

        /** @brief Returns true if the default mouse has haptic capability. */
        CNAEXT [[nodiscard]] static bool IsMouseHapticEXT();
    };
}
