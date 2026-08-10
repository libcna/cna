// SPDX-License-Identifier: MS-PL
#pragma once

#ifdef CNA_DEVICES

namespace CNA::Devices
{
    /**
     * @brief Physical position of a camera device relative to the system, when known.
     *
     * Mirrors SDL3's `SDL_CameraPosition` (`third_party/SDL/include/SDL3/SDL_camera.h`).
     * CNA extension — no XNA/WP7 equivalent exists.
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
