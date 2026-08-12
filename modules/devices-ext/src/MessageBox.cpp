// SPDX-License-Identifier: MS-PL
#include "CNA/Devices/MessageBox.hpp"

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

    /// The dialog service, or null when this platform cannot show a message box.
    CNA::Platform::IPlatformDialogs* Dialogs()
    {
        CNA::Platform::IPlatform& platform = CNA::Platform::GetCurrentPlatform();
        return platform.GetCapabilities().messageBox ? platform.GetDialogs() : nullptr;
    }
} // namespace

namespace CNA::Devices
{
    bool MessageBox::getIsSupportedProperty()
    {
        return Platform::GetCurrentPlatform().GetCapabilities().messageBox;
    }

    void MessageBox::ShowSimple(const MessageBoxType type, const std::string& title,
                                const std::string& message)
    {
        Platform::IPlatformDialogs* dialogs = Dialogs();
        if (dialogs == nullptr)
        {
            return;
        }

        // A message box that could not be shown is not worth taking a process down for: this is
        // usually the last thing a game does on its way out, and on a headless or embedded host
        // there may be no one to show it to.
        try
        {
            dialogs->ShowMessageBox(ToSeverity(type), title, message, nullptr);
        }
        catch (const Platform::PlatformException&)
        {
        }
    }

    int MessageBox::Show(const MessageBoxType type, const std::string& title,
                         const std::string& message,
                         const std::vector<std::string>& buttonLabels)
    {
        Platform::IPlatformDialogs* dialogs = Dialogs();
        if (dialogs == nullptr)
        {
            return -1;
        }

        // -1 is already this API's "no choice was made", so a platform that cannot show the
        // dialog reports the same thing a user dismissing it would, and a caller branching on the
        // result needs no separate unavailable case.
        try
        {
            return dialogs->ShowMessageBoxWithButtons(ToSeverity(type), title, message,
                                                      buttonLabels, nullptr);
        }
        catch (const Platform::PlatformException&)
        {
            return -1;
        }
    }
} // namespace CNA::Devices

#endif // CNA_DEVICES
