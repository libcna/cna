// SPDX-License-Identifier: MS-PL
#include "CNA/Internal/Input/SystemKeyboardBackend.hpp"

namespace CNA::Internal::Input
{
    namespace
    {
        struct RealSystemKeyboardBackend final : ISystemKeyboardBackend
        {
            SDL_Keymod GetModState() override
            {
                return SDL_GetModState();
            }
        };

        RealSystemKeyboardBackend g_realBackend;
        ISystemKeyboardBackend*   g_currentBackend = &g_realBackend;
    }

    ISystemKeyboardBackend& system_keyboard_backend()
    {
        return *g_currentBackend;
    }

    void SetSystemKeyboardBackendForTests(ISystemKeyboardBackend* backend)
    {
        g_currentBackend = backend ? backend : &g_realBackend;
    }
}
