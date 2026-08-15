// SPDX-License-Identifier: MS-PL
#pragma once

#include "CNA/CNAHelper.hpp"
#include "CNA/Input/PowerState.hpp"

namespace CNA::Input
{
    /**
     * @brief CNAEXT — host system battery / charge information.
     *
     * XNA 4.0 has no power API; this is a CNA extension (whole class `CNAEXT`). It reports whether the
     * machine is on battery and how much charge/runtime remains, so a game can, for example, pause or
     * dim when the battery runs low. Platform notes: desktop (Windows/Linux/macOS) and Android report
     * battery state; on the web it is best-effort (the Battery Status API is deprecated in some
     * browsers) and typically reports Unknown.
     */
    CNAEXT class Power
    {
    public:
        /** @brief Static-only utility; not instantiable. */
        Power() = delete;

        /**
         * @brief Returns the host power state and fills the remaining runtime and charge level.
         * @param secondsLeft Output receiving the seconds of runtime remaining, or -1 if unknown.
         * @param percent Output receiving the battery charge (0-100), or -1 if unknown.
         * @return The current power state (OnBattery, Charging, Charged, NoBattery, Unknown, or Error).
         */
        CNAEXT [[nodiscard]] static PowerStateEXT GetInfoEXT(int& secondsLeft, int& percent);
    };
}
