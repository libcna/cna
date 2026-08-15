// SPDX-License-Identifier: MS-PL
#pragma once

#ifdef CNA_DEVICES

#include <string>

namespace CNA::Devices
{
    /**
     * @brief One user-preferred locale (language + optional country/region).
     *
     * Mirrors the platform contract's preferred-locale record.
     * CNA extension — no XNA/WP7 equivalent exists.
     */
    struct LocaleInfo
    {
        /** @brief ISO-639 language code, e.g. "en" for English, "de" for German. */
        std::string Language;

        /**
         * @brief ISO-3166 country code, e.g. "US" for America, or an empty string
         * if the platform did not report a country for this locale.
         */
        std::string Country;
    };
} // namespace CNA::Devices

#endif // CNA_DEVICES
