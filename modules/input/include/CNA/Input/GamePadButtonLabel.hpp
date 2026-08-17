// SPDX-License-Identifier: MS-PL
#pragma once

#include "CNA/CNAHelper.hpp"

namespace CNA::Input
{
    /**
     * @brief CNAEXT — the glyph printed on a gamepad face button for the connected controller type.
     *
     * XNA 4.0 exposes only positional button names (A/B/X/Y); the printed label differs by controller
     * family (Xbox uses A/B/X/Y, PlayStation uses cross/circle/square/triangle, Nintendo swaps A/B and
     * X/Y). This CNA extension lets UI prompts show the correct glyph. It mirrors the platform
     * contract's `GamepadButtonLabel`.
     */
    CNAEXT enum class GamePadButtonLabelEXT
    {
        /** @brief No known label (non-face button, or the controller type is unknown). */
        Unknown,
        /** @brief The "A" label (Xbox / Nintendo layout). */
        A,
        /** @brief The "B" label (Xbox / Nintendo layout). */
        B,
        /** @brief The "X" label (Xbox / Nintendo layout). */
        X,
        /** @brief The "Y" label (Xbox / Nintendo layout). */
        Y,
        /** @brief The cross (✕) label (PlayStation layout). */
        Cross,
        /** @brief The circle (○) label (PlayStation layout). */
        Circle,
        /** @brief The square (□) label (PlayStation layout). */
        Square,
        /** @brief The triangle (△) label (PlayStation layout). */
        Triangle
    };
}
