// SPDX-License-Identifier: MS-PL
#include "CNA/Devices/Detail/PlatformMessageBoxBackend.hpp"

#ifdef CNA_DEVICES

#include "CNA/Platform/CurrentPlatform.hpp"
#include "CNA/Platform/IPlatform.hpp"
#include "CNA/Platform/PlatformException.hpp"

namespace
{
    CNA::Platform::MessageBoxSeverity ToSeverity(const CNA::Devices::MessageBoxType type)
    {
        switch (type)
        {
        case CNA::Devices::MessageBoxType::Error:       return CNA::Platform::MessageBoxSeverity::Error;
        case CNA::Devices::MessageBoxType::Warning:     return CNA::Platform::MessageBoxSeverity::Warning;
        case CNA::Devices::MessageBoxType::Information: return CNA::Platform::MessageBoxSeverity::Information;
        }
        return CNA::Platform::MessageBoxSeverity::Information;
    }
} // namespace

namespace CNA::Devices::Detail
{
    void PlatformMessageBoxBackend::ShowSimple(const MessageBoxType type, const std::string& title,
                                               const std::string& message)
    {
        CNA::Platform::IPlatformDialogs* dialogs =
            CNA::Platform::GetCurrentPlatform().GetDialogs();
        if (dialogs == nullptr)
        {
            return;
        }

        // A message box that could not be shown is not worth taking a process down for: this is
        // usually the last thing a game does on its way out, and on a headless or embedded host
        // there may be no one to show it to. The previous SDL version discarded the status too.
        try
        {
            dialogs->ShowMessageBox(ToSeverity(type), title, message, nullptr);
        }
        catch (const CNA::Platform::PlatformException&)
        {
        }
    }

    int PlatformMessageBoxBackend::Show(const MessageBoxType type, const std::string& title,
                                        const std::string& message,
                                        const std::vector<std::string>& buttonLabels)
    {
        CNA::Platform::IPlatformDialogs* dialogs =
            CNA::Platform::GetCurrentPlatform().GetDialogs();
        if (dialogs == nullptr)
        {
            return -1;
        }

        // -1 is already this API's "no choice was made", so a platform that cannot show the
        // dialog reports the same thing a user dismissing it would. A caller branching on the
        // result therefore needs no separate unavailable case.
        try
        {
            return dialogs->ShowMessageBoxWithButtons(ToSeverity(type), title, message,
                                                      buttonLabels, nullptr);
        }
        catch (const CNA::Platform::PlatformException&)
        {
            return -1;
        }
    }
} // namespace CNA::Devices::Detail

#endif // CNA_DEVICES
