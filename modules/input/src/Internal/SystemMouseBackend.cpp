// SPDX-License-Identifier: MS-PL
#include "CNA/Internal/Input/SystemMouseBackend.hpp"

#include <SDL3/SDL.h>

namespace CNA::Internal::Input
{
    namespace
    {
        struct RealSystemMouseBackend final : ISystemMouseBackend
        {
            bool CaptureMouse(bool enabled) override
            {
                return SDL_CaptureMouse(enabled);
            }
            void GetGlobalMouseState(float* x, float* y) override
            {
                SDL_GetGlobalMouseState(x, y);
            }
            bool WarpMouseGlobal(float x, float y) override
            {
                return SDL_WarpMouseGlobal(x, y);
            }
        };

        RealSystemMouseBackend g_realBackend;
        ISystemMouseBackend*   g_currentBackend = &g_realBackend;
    }

    ISystemMouseBackend& system_mouse_backend()
    {
        return *g_currentBackend;
    }

    void SetSystemMouseBackendForTests(ISystemMouseBackend* backend)
    {
        g_currentBackend = backend ? backend : &g_realBackend;
    }
}
