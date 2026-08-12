// SPDX-License-Identifier: MS-PL
#include "CNA/Devices/Locale.hpp"

#ifdef CNA_DEVICES

#include "CNA/Platform/CurrentPlatform.hpp"

namespace CNA::Devices
{
    std::vector<LocaleInfo> Locale::getPreferredLocalesProperty()
    {
        const std::vector<Platform::PlatformLocale> locales =
            Platform::GetCurrentPlatform().GetSystemInfo()->GetPreferredLocales();

        std::vector<LocaleInfo> result;
        result.reserve(locales.size());
        for (const Platform::PlatformLocale& locale : locales)
        {
            // The platform keeps language and country separate for exactly this consumer, so
            // there is no tag to re-split here.
            LocaleInfo info;
            info.Language = locale.language;
            info.Country = locale.country;
            result.push_back(std::move(info));
        }
        return result;
    }
} // namespace CNA::Devices

#endif // CNA_DEVICES
