// SPDX-License-Identifier: MS-PL
#pragma once

#include "CNA/CNAHelper.hpp"

namespace CNA::Input
{
    /**
     * @brief CNAEXT — battery / charge state of a power source (gamepad or the host system).
     *
     * XNA 4.0 has no battery API; this is a CNA extension shared by `GamePad::GetPowerInfoEXT`
     * (per-controller) and `CNA::Input::Power` (host system).
     */
    CNAEXT enum class PowerStateEXT
    {
        /** @brief The power state could not be determined (error querying the source). */
        Error,
        /** @brief The charge state is unknown (e.g. a wired device that does not report a battery). */
        Unknown,
        /** @brief Running on battery and discharging. */
        OnBattery,
        /** @brief Plugged in with no battery present. */
        NoBattery,
        /** @brief Plugged in and charging the battery. */
        Charging,
        /** @brief Plugged in and the battery is fully charged. */
        Charged
    };
}
