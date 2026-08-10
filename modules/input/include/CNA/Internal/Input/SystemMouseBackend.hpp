// SPDX-License-Identifier: MS-PL
#pragma once

// Internal (CNA) seam over SDL3's global-mouse API (capture / global position / global warp), used
// by the Mouse::*GlobalEXT / SetCaptureEXT extensions. This exists ONLY so those can be exercised
// deterministically in tests (there is no real desktop cursor on CI). Production uses the real SDL
// implementation.
namespace CNA::Internal::Input
{
    /**
     * @brief Injectable seam over the SDL3 global-mouse API.
     */
    struct ISystemMouseBackend
    {
        virtual ~ISystemMouseBackend() = default;

        /** @brief Enables/disables capturing mouse events outside the window. Returns success. SDL_CaptureMouse. */
        virtual bool CaptureMouse(bool enabled) = 0;

        /** @brief Fills the desktop-relative cursor position. SDL_GetGlobalMouseState. */
        virtual void GetGlobalMouseState(float* x, float* y) = 0;

        /** @brief Moves the cursor to a desktop-relative position. Returns success. SDL_WarpMouseGlobal. */
        virtual bool WarpMouseGlobal(float x, float y) = 0;
    };

    /**
     * @brief Returns the active system mouse backend (the real SDL one unless overridden).
     */
    ISystemMouseBackend& system_mouse_backend();

    /**
     * @brief Test-only: swaps in a fake mouse backend; pass nullptr to restore the real one.
     */
    void SetSystemMouseBackendForTests(ISystemMouseBackend* backend);
}
