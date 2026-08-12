// SPDX-License-Identifier: MS-PL
#include "CNA/Input/Clipboard.hpp"

#include "CNA/Platform/CurrentPlatform.hpp"
#include "CNA/Platform/IPlatform.hpp"
#include "CNA/Platform/PlatformException.hpp"

namespace CNA::Input
{
    std::string Clipboard::GetTextEXT()
    {
        // Null exactly when the platform reports no clipboard capability, which for this API is
        // indistinguishable from an empty clipboard -- both mean "there is no text to paste", and
        // the SDL-backed version answered the same way when the read failed.
        CNA::Platform::IPlatformClipboard* clipboard =
            CNA::Platform::GetCurrentPlatform().GetClipboard();
        return clipboard != nullptr ? clipboard->GetText() : std::string();
    }

    void Clipboard::SetTextEXT(const std::string& text)
    {
        CNA::Platform::IPlatformClipboard* clipboard =
            CNA::Platform::GetCurrentPlatform().GetClipboard();
        if (clipboard == nullptr)
        {
            return;
        }

        // This accessor returns void, so a failed write has nowhere to be reported. Swallowing is
        // what the SDL version did -- SDL_SetClipboardText's status was discarded -- and a throw
        // from a void copy-to-clipboard would be a behaviour change, not a fix. The bool-returning
        // CNA::Devices::Clipboard is the surface that reports failure.
        try
        {
            clipboard->SetText(text);
        }
        catch (const CNA::Platform::PlatformException&)
        {
        }
    }

    bool Clipboard::HasTextEXT()
    {
        CNA::Platform::IPlatformClipboard* clipboard =
            CNA::Platform::GetCurrentPlatform().GetClipboard();
        return clipboard != nullptr && clipboard->HasText();
    }
}
