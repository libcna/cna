// SPDX-License-Identifier: MS-PL
#include "CNA/Devices/UrlLauncher.hpp"

#ifdef CNA_DEVICES

#include <SDL3/SDL_misc.h>

namespace CNA::Devices
{
    bool UrlLauncher::Open(const std::string& url)
    {
        return SDL_OpenURL(url.c_str());
    }
} // namespace CNA::Devices

#endif // CNA_DEVICES
