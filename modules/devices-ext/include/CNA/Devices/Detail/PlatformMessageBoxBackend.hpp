// SPDX-License-Identifier: MS-PL
#pragma once

#ifdef CNA_DEVICES

#include "CNA/Devices/Detail/IMessageBoxBackend.hpp"

namespace CNA::Devices::Detail
{
    /**
     * @brief Real `IMessageBoxBackend` implementation, forwarding to the platform's dialog
     * service. The default backend `MessageBox` uses outside of tests.
     */
    class PlatformMessageBoxBackend final : public IMessageBoxBackend
    {
    public:
        /**
         * @brief Shows a message box with a single dismiss button.
         * @param type How prominently to present it.
         * @param title The dialog title.
         * @param message The message body.
         */
        void ShowSimple(MessageBoxType type, const std::string& title, const std::string& message) override;

        /**
         * @brief Shows a message box with a choice of buttons.
         * @param type How prominently to present it.
         * @param title The dialog title.
         * @param message The message body.
         * @param buttonLabels The button labels, in display order.
         * @return The chosen button's index, or -1 when dismissed or unavailable.
         */
        int Show(
            MessageBoxType type,
            const std::string& title,
            const std::string& message,
            const std::vector<std::string>& buttonLabels) override;
    };
} // namespace CNA::Devices::Detail

#endif // CNA_DEVICES
