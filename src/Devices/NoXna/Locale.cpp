// SPDX-License-Identifier: MS-PL
#include "CNA/Devices/Locale.hpp"

#ifdef CNA_DEVICES

#include <SDL3/SDL_locale.h>
#include <SDL3/SDL_stdinc.h>

namespace CNA::Devices
{
    std::vector<LocaleInfo> Locale::getPreferredLocalesProperty()
    {
        std::vector<LocaleInfo> result;

        int count = 0;
        SDL_Locale** locales = SDL_GetPreferredLocales(&count);
        if (locales == nullptr)
        {
            return result;
        }

        result.reserve(static_cast<std::size_t>(count));
        for (int i = 0; i < count; ++i)
        {
            const SDL_Locale* locale = locales[i];
            if (locale == nullptr)
            {
                continue;
            }

            LocaleInfo info;
            info.Language = (locale->language != nullptr) ? locale->language : "";
            info.Country = (locale->country != nullptr) ? locale->country : "";
            result.push_back(std::move(info));
        }

        SDL_free(locales);
        return result;
    }
} // namespace CNA::Devices

#endif // CNA_DEVICES
