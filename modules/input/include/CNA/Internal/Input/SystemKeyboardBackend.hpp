// SPDX-License-Identifier: MS-PL
#pragma once

#include <SDL3/SDL.h>

// Internal (CNA) seam over SDL3's global keyboard modifier state (SDL_GetModState), used by
// Keyboard::GetModStateEXT. This exists ONLY so the modifier/lock state can be injected in tests
// (SDL's real mod state is only updated by real key events, which headless tests don't have).
namespace CNA::Internal::Input
{
    /**
     * @brief Injectable seam over the SDL3 keyboard modifier-state query.
     */
    struct ISystemKeyboardBackend
    {
        virtual ~ISystemKeyboardBackend() = default;

        /** @brief Returns the currently active modifier/lock keys. Mirrors SDL_GetModState. */
        virtual SDL_Keymod GetModState() = 0;
    };

    /**
     * @brief Returns the active system keyboard backend (the real SDL one unless overridden).
     */
    ISystemKeyboardBackend& system_keyboard_backend();

    /**
     * @brief Test-only: swaps in a fake keyboard backend; pass nullptr to restore the real one.
     */
    void SetSystemKeyboardBackendForTests(ISystemKeyboardBackend* backend);
}
