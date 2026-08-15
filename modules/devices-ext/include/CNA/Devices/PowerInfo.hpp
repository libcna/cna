// SPDX-License-Identifier: MS-PL
#pragma once

#ifdef CNA_DEVICES

#include "CNA/Devices/PowerState.hpp"

namespace CNA::Devices
{
    /**
     * @brief Reports the device's current power/battery status.
     *
     * CNA extension — no XNA/WP7 equivalent exists. Backed by SDL3's
     * `IPlatformSystemInfo::GetPowerInfo()`, which has
     * real per-platform implementations for Windows, Linux, macOS, Android, iOS, and
     * Web/Emscripten (`third_party/SDL/src/power/`).
     *
     * All members are static: this class has no instance state of its own, matching
     * the single free-function shape of the underlying SDL3 API.
     */
    class PowerInfo
    {
    public:
        /**
         * @brief Gets the current power supply state.
         *
         * A battery status should never be treated as absolute truth — battery
         * hardware reporting can be imprecise, and some platforms can only report a
         * percentage or a time estimate, not both.
         *
         * @return The current PowerState, or PowerState::Error if it could not be
         * determined.
         */
        [[nodiscard]] static PowerState getStateProperty();

        /**
         * @brief Gets the estimated battery percentage remaining.
         *
         * @return A value in [0, 100], or -1 if the percentage could not be
         * determined or there is no battery.
         */
        [[nodiscard]] static int getBatteryPercentProperty();

        /**
         * @brief Gets the estimated number of seconds of battery life remaining.
         *
         * @return The number of seconds, or -1 if it could not be determined or
         * there is no battery.
         */
        [[nodiscard]] static int getSecondsRemainingProperty();

    private:
        /** @brief Not instantiable — every member is static. */
        PowerInfo() = delete;
    };
} // namespace CNA::Devices

#endif // CNA_DEVICES
