// SPDX-License-Identifier: MS-PL
#pragma once

#include <cstdint>
#include <string>

namespace CNA::Platform {

    /**
     * @brief Drives standalone haptic devices.
     *
     * Separate from `IPlatformGamepad::SetRumble` because the callers are genuinely different:
     * `Microsoft::Devices::VibrateController` drives the *device's own* haptics with no gamepad
     * involved, which is the normal case on a phone. PLAT-24 deferred this interface until a
     * distinct caller showed up; `modules/devices` and `modules/input` are two.
     *
     * Devices are addressed by index into the currently-connected set, which changes on hotplug.
     * An index that is no longer valid reports failure rather than throwing — a device being
     * unplugged mid-effect is ordinary, not exceptional.
     */
    class IPlatformHaptics
    {
    public:
        /** @brief Destroys the service, closing any device it opened. */
        virtual ~IPlatformHaptics() = default;

        /**
         * @brief Gets how many haptic devices are connected.
         *
         * @return The device count; zero when none are present.
         */
        [[nodiscard]] virtual int GetCount() const = 0;

        /**
         * @brief Gets a device's human-readable name.
         *
         * @param index The device to name.
         * @return The name, or an empty string for an invalid index.
         */
        [[nodiscard]] virtual std::string GetName(int index) const = 0;

        /**
         * @brief Gets whether a device can play a simple rumble effect.
         *
         * Not every haptic device supports rumble; some expose only richer effect types. A
         * caller checks this rather than discovering it from a failed play.
         *
         * @param index The device to query.
         * @return True if simple rumble is supported.
         */
        [[nodiscard]] virtual bool SupportsRumble(int index) const = 0;

        /**
         * @brief Plays a simple rumble effect.
         *
         * @param index The device to rumble.
         * @param strength Intensity in [0, 1]; values outside are clamped.
         * @param durationMilliseconds How long to rumble for.
         * @return True if the device accepted the effect.
         */
        virtual bool PlayRumble(int index, float strength, std::uint32_t durationMilliseconds) = 0;

        /**
         * @brief Stops a rumble effect in progress.
         *
         * @param index The device to stop.
         * @return True if the device accepted the request.
         */
        virtual bool StopRumble(int index) = 0;
    };

} // namespace CNA::Platform
