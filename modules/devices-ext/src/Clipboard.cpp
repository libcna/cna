// SPDX-License-Identifier: MS-PL
#include "CNA/Devices/Clipboard.hpp"

#ifdef CNA_DEVICES

#include "CNA/Platform/CurrentPlatform.hpp"
#include "CNA/Platform/PlatformException.hpp"

namespace CNA::Devices
{
    bool Clipboard::setTextProperty(const std::string& text)
    {
        Platform::IPlatformClipboard* clipboard = Platform::GetCurrentPlatform().GetClipboard();
        if (clipboard == nullptr)
        {
            // Null exactly when the platform reports no clipboard capability. This API answers
            // with a bool, so a caller learns it failed without an exception -- the same shape
            // the SDL-backed version had when SDL_SetClipboardText failed.
            return false;
        }

        try
        {
            clipboard->SetText(text);
            return true;
        }
        catch (const Platform::PlatformException&)
        {
            return false;
        }
    }

    std::string Clipboard::getTextProperty()
    {
        Platform::IPlatformClipboard* clipboard = Platform::GetCurrentPlatform().GetClipboard();
        return clipboard != nullptr ? clipboard->GetText() : std::string();
    }

    bool Clipboard::getHasTextProperty()
    {
        Platform::IPlatformClipboard* clipboard = Platform::GetCurrentPlatform().GetClipboard();
        return clipboard != nullptr && clipboard->HasText();
    }
} // namespace CNA::Devices

#endif // CNA_DEVICES
