// SPDX-License-Identifier: MS-PL
#pragma once

#include "CNA/Input/InputDeviceInfo.hpp"

#include <vector>

// Internal (CNA) seam over SDL3's device enumeration (mice/keyboards/touch devices), used by
// CNA::Input::InputDevices. This exists ONLY so enumeration can be injected in tests (CI has no
// predictable set of physical devices). Production always uses the real SDL-backed implementation.
namespace CNA::Internal::Input
{
    /**
     * @brief Injectable seam over the SDL3 input-device enumeration API.
     */
    struct ISystemDeviceBackend
    {
        virtual ~ISystemDeviceBackend() = default;

        /** @brief Enumerates connected mice (SDL_GetMice + SDL_GetMouseNameForID). */
        virtual std::vector<CNA::Input::InputDeviceInfoEXT> GetMice() = 0;

        /** @brief Enumerates connected keyboards (SDL_GetKeyboards + SDL_GetKeyboardNameForID). */
        virtual std::vector<CNA::Input::InputDeviceInfoEXT> GetKeyboards() = 0;

        /** @brief Enumerates connected touch devices (SDL_GetTouchDevices + SDL_GetTouchDeviceName). */
        virtual std::vector<CNA::Input::InputDeviceInfoEXT> GetTouchDevices() = 0;
    };

    /**
     * @brief Returns the active system device backend (the real SDL one unless overridden).
     */
    ISystemDeviceBackend& system_device_backend();

    /**
     * @brief Test-only: swaps in a fake device backend; pass nullptr to restore the real one.
     */
    void SetSystemDeviceBackendForTests(ISystemDeviceBackend* backend);
}
