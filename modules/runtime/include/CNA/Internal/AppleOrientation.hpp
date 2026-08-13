#pragma once

struct SDL_Window;

namespace CNA::Internal
{
    /** Ask UIKit to re-query SDL_HINT_ORIENTATIONS for an already-created iOS window. */
    void RequestAppleOrientationUpdate(SDL_Window* window);
}
