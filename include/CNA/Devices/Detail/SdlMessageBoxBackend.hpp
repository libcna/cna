// SPDX-License-Identifier: MS-PL
#pragma once

#ifdef CNA_DEVICES

#include "CNA/Devices/Detail/IMessageBoxBackend.hpp"

namespace CNA::Devices::Detail
{
    /**
     * @brief Real `IMessageBoxBackend` implementation calling SDL3's
     * `SDL_ShowSimpleMessageBox()`/`SDL_ShowMessageBox()` directly. The default
     * backend `MessageBox` uses outside of tests.
     */
    class SdlMessageBoxBackend final : public IMessageBoxBackend
    {
    public:
        void ShowSimple(MessageBoxType type, const std::string& title, const std::string& message) override;

        int Show(
            MessageBoxType type,
            const std::string& title,
            const std::string& message,
            const std::vector<std::string>& buttonLabels) override;
    };
} // namespace CNA::Devices::Detail

#endif // CNA_DEVICES
