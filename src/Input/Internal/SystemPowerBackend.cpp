// SPDX-License-Identifier: MS-PL
#include "CNA/Internal/Input/SystemPowerBackend.hpp"

namespace CNA::Internal::Input
{
    namespace
    {
        struct RealSystemPowerBackend final : ISystemPowerBackend
        {
            SDL_PowerState GetPowerInfo(int* seconds, int* percent) override
            {
                return SDL_GetPowerInfo(seconds, percent);
            }
        };

        RealSystemPowerBackend g_realBackend;
        ISystemPowerBackend*   g_currentBackend = &g_realBackend;
    }

    ISystemPowerBackend& system_power_backend()
    {
        return *g_currentBackend;
    }

    void SetSystemPowerBackendForTests(ISystemPowerBackend* backend)
    {
        g_currentBackend = backend ? backend : &g_realBackend;
    }
}
