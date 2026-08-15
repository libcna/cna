// SPDX-License-Identifier: MS-PL
#pragma once

#ifdef CNA_DEVICES

#include <string>

namespace CNA::Devices
{
    /**
     * @brief Opens a URL/URI in the system's default external application.
     *
     * CNA extension — no XNA/WP7 equivalent exists. Delegates to the selected platform's
     * external-URL service, which may use a desktop shell, mobile intent or browser API.
     *
     * @note A successful result means only that
     * *something* was launched to handle the URL — not that it actually loaded.
     * Opening a URL may also cause the game window to lose focus, or move the app to
     * the background on mobile.
     */
    class UrlLauncher
    {
    public:
        /**
         * @brief Opens the given URL/URI in a separate, system-provided application.
         *
         * @param url A valid URL/URI to open, e.g. "https://example.com" or
         * "file:///full/path/to/file" for a local file, if supported by the platform.
         * @return true if something was launched to handle the URL; false if this is
         * unimplemented/unavailable on the current platform, or the URL could not be
         * handled.
         */
        static bool Open(const std::string& url);

    private:
        /** @brief Not instantiable — every member is static. */
        UrlLauncher() = delete;
    };
} // namespace CNA::Devices

#endif // CNA_DEVICES
