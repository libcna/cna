// SPDX-License-Identifier: MS-PL
#pragma once

#ifdef CNA_DEVICES

#include <string>

namespace CNA::Devices
{
    /**
     * @brief One file-type filter entry for `FileDialog`'s open/save dialogs.
     *
     * Mirrors SDL3's `SDL_DialogFileFilter` (`third_party/SDL/include/SDL3/SDL_dialog.h`).
     * CNA extension — no XNA/WP7 equivalent exists.
     */
    struct FileDialogFilter
    {
        /** @brief User-readable label for the filter, e.g. "Office document". */
        std::string Name;

        /**
         * @brief Semicolon-separated list of file extensions, e.g. "doc;docx", or a
         * single "*" for an "All files" filter. Extensions may only contain
         * alphanumeric characters, hyphens, underscores, and periods.
         */
        std::string Pattern;
    };
} // namespace CNA::Devices

#endif // CNA_DEVICES
