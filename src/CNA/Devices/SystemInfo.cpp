// SPDX-License-Identifier: MS-PL
#include "CNA/Devices/SystemInfo.hpp"

#ifdef CNA_DEVICES

#include <SDL3/SDL_cpuinfo.h>

namespace CNA::Devices
{
    int SystemInfo::getLogicalCpuCoreCountProperty()
    {
        return SDL_GetNumLogicalCPUCores();
    }

    int SystemInfo::getSystemRamMegabytesProperty()
    {
        return SDL_GetSystemRAM();
    }
} // namespace CNA::Devices

#endif // CNA_DEVICES
