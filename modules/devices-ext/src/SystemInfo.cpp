// SPDX-License-Identifier: MS-PL
#include "CNA/Devices/SystemInfo.hpp"

#ifdef CNA_DEVICES

#include "CNA/Platform/CurrentPlatform.hpp"

namespace CNA::Devices
{
    int SystemInfo::getLogicalCpuCoreCountProperty()
    {
        return Platform::GetCurrentPlatform().GetSystemInfo()->GetLogicalCoreCount();
    }

    int SystemInfo::getSystemRamMegabytesProperty()
    {
        return Platform::GetCurrentPlatform().GetSystemInfo()->GetSystemMemoryMegabytes();
    }
} // namespace CNA::Devices

#endif // CNA_DEVICES
