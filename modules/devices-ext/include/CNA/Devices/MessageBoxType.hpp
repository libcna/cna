// SPDX-License-Identifier: MS-PL
#pragma once

#ifdef CNA_DEVICES

namespace CNA::Devices
{
    /**
     * @brief Icon/severity of a `MessageBox` dialog.
     *
     * Uses CNA's platform-neutral error/warning/information vocabulary. CNA extension — no
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
