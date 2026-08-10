// SPDX-License-Identifier: MS-PL
#pragma once

#ifdef CNA_DEVICES

namespace CNA::Devices
{
    /**
     * @brief Icon/severity of a `MessageBox` dialog.
     *
     * Mirrors SDL3's `SDL_MESSAGEBOX_ERROR`/`_WARNING`/`_INFORMATION` flags
     * (`third_party/SDL/include/SDL3/SDL_messagebox.h`). CNA extension — no
     * XNA/WP7 equivalent exists.
     */
    enum class MessageBoxType
    {
        /** @brief An error dialog. */
        Error,
        /** @brief A warning dialog. */
        Warning,
        /** @brief An informational dialog. */
        Information
    };
} // namespace CNA::Devices

#endif // CNA_DEVICES
