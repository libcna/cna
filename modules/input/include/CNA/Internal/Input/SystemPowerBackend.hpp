// SPDX-License-Identifier: MS-PL
#pragma once

#include <SDL3/SDL.h>

// Internal (CNA) seam over SDL3's system power API (SDL_GetPowerInfo), used by CNA::Input::Power.
//
// This exists ONLY so the host battery/charge query can be exercised deterministically in tests
// (there is no real battery on CI). Production always uses the real SDL-backed implementation.
namespace CNA::Internal::Input
{
    /**
     * @brief Injectable seam over the SDL3 system power API.
     */
    struct ISystemPowerBackend
    {
        virtual ~ISystemPowerBackend() = default;

        /**
         * @brief Queries the host power source. Fills `*seconds` (remaining runtime, or -1 unknown)
         * and `*percent` (0-100, or -1 unknown). Mirrors SDL_GetPowerInfo.
         */
        virtual SDL_PowerState GetPowerInfo(int* seconds, int* percent) = 0;
    };

    /**
     * @brief Returns the active system power backend (the real SDL one unless overridden).
     */
    ISystemPowerBackend& system_power_backend();

    /**
     * @brief Test-only: swaps in a fake system power backend; pass nullptr to restore the real one.
     */
    void SetSystemPowerBackendForTests(ISystemPowerBackend* backend);
}
