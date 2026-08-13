// SPDX-License-Identifier: MS-PL
#pragma once

#ifdef CNA_DEVICES

namespace CNA::Devices
{
    /**
     * @brief Physical position of a camera device relative to the system, when known.
     *
     * CNA extension — no XNA/WP7 equivalent exists. Mapped from the selected platform's own
     * camera-position vocabulary.
     */
    enum class CameraPosition
    {
        /** @brief Position not reported by the platform. */
        Unknown,
        /** @brief Front-facing (same side as the screen), relevant on mobile. */
        FrontFacing,
        /** @brief Back-facing (opposite the screen), relevant on mobile. */
        BackFacing
    };
} // namespace CNA::Devices

#endif // CNA_DEVICES
