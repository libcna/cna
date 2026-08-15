// SPDX-License-Identifier: MS-PL
#pragma once

#include "CNA/CNAHelper.hpp"

namespace CNA::Input
{
    /**
     * @brief CNAEXT — how a connected gamepad is physically attached (wired vs. wireless).
     *
     * XNA 4.0 has no notion of a controller's connection medium; this CNA extension mirrors the
     * platform contract's `GamepadConnectionState`. Games can use it to warn about a low-battery
     * wireless pad.
     */
    CNAEXT enum class GamePadConnectionStateEXT
    {
        /** @brief The connection medium is unknown (or the controller is disconnected). */
        Unknown,
        /** @brief The controller is attached over a wired connection. */
        Wired,
        /** @brief The controller is attached over a wireless connection. */
        Wireless
    };
}
