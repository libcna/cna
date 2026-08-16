// SPDX-License-Identifier: MS-PL
#pragma once

struct SDL_Window;

namespace CNA::Platform::Sdl3 {

    /**
     * @brief Asks UIKit to re-query the orientation set for an already-created iOS window.
     *
     * Compiled only on iOS. The declaration is unconditional so the call site needs no
     * preprocessor branch around the header itself.
     *
     * @param window The native window whose root view controller should refresh its answer.
     */
    void RequestAppleOrientationUpdate(SDL_Window* window);

} // namespace CNA::Platform::Sdl3
