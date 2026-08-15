// SPDX-License-Identifier: MS-PL
#include "CNA/Devices/UrlLauncher.hpp"

#ifdef CNA_DEVICES

#include "CNA/Platform/CurrentPlatform.hpp"

namespace CNA::Devices
{
    bool UrlLauncher::Open(const std::string& url)
    {
        return Platform::GetCurrentPlatform().GetSystemInfo()->OpenUrl(url);
    }
} // namespace CNA::Devices

#endif // CNA_DEVICES
