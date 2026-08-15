// SPDX-License-Identifier: MS-PL
#pragma once

#ifdef CNA_DEVICES

#include <vector>

#include "CNA/Devices/LocaleInfo.hpp"

namespace CNA::Devices
{
    /**
     * @brief Reports the user's preferred locale(s) (language/region).
     *
     * CNA extension — no XNA/WP7 equivalent exists. Backed by SDL3's
     * `IPlatformSystemInfo::GetPreferredLocales()`, which
     * reads OS-level locale settings on every platform CNA targets, including
     * Web/Emscripten (via the browser's own language settings).
     *
     * All members are static: this class has no instance state of its own.
     */
    class Locale
    {
    public:
        /**
         * @brief Gets the user's preferred locales, most-preferred first.
         *
         * A platform may report more than one locale if the user configured several
         * preferred languages. The returned list may be empty if no locale could be
         * determined.
         *
         * @return The user's preferred locales, ordered by preference.
         */
        [[nodiscard]] static std::vector<LocaleInfo> getPreferredLocalesProperty();

    private:
        /** @brief Not instantiable — every member is static. */
        Locale() = delete;
    };
} // namespace CNA::Devices

#endif // CNA_DEVICES
