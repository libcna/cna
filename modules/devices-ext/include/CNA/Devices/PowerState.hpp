// SPDX-License-Identifier: MS-PL
#pragma once

#ifdef CNA_DEVICES

namespace CNA::Devices
{
    /**
     * @brief Describes the current state of the device's power supply.
     *
     * Uses CNA's platform-neutral power-state vocabulary. CNA extension — no XNA/WP7
     * equivalent exists.
     */
    enum class PowerState
    {
        /** @brief The power state could not be determined due to an error. */
        Error,
        /** @brief The power state is unknown; not necessarily an error. */
        Unknown,
        /** @brief Running on battery, not connected to external power. */
        OnBattery,
        /** @brief Connected to external power; no battery present. */
        NoBattery,
        /** @brief Connected to external power; battery is charging. */
        Charging,
        /** @brief Connected to external power; battery is fully charged. */
        Charged
    };
} // namespace CNA::Devices

#endif // CNA_DEVICES
