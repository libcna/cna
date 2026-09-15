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
        // what the previous native version did — its set status was discarded — and a throw
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

    // plans/plan_x11.md X11-0157: the same three answers over the primary selection, which only
    // X11 and Wayland desktops have. Where there is none, it reads as empty and ignores writes,
    // exactly as an absent clipboard does above.

    std::string Clipboard::GetPrimarySelectionTextEXT()
    {
        CNA::Platform::IPlatformClipboard* selection =
            CNA::Platform::GetCurrentPlatform().GetPrimarySelection();
        return selection != nullptr ? selection->GetText() : std::string();
    }

    void Clipboard::SetPrimarySelectionTextEXT(const std::string& text)
    {
        CNA::Platform::IPlatformClipboard* selection =
            CNA::Platform::GetCurrentPlatform().GetPrimarySelection();
        if (selection == nullptr)
        {
            return;
        }
        try
        {
            selection->SetText(text);
        }
        catch (const CNA::Platform::PlatformException&)
        {
            // Void, like SetTextEXT: a selection another client won in the same instant has
            // nowhere to be reported, and losing it is what selecting elsewhere does anyway.
        }
    }

    bool Clipboard::HasPrimarySelectionTextEXT()
    {
        CNA::Platform::IPlatformClipboard* selection =
            CNA::Platform::GetCurrentPlatform().GetPrimarySelection();
        return selection != nullptr && selection->HasText();
    }

    // plans/plan_x11.md X11-0158: formats other than text. A clipboard that carries only text
    // inherits the contract's defaults -- nothing to read, a refusal to write -- which read here
    // as empty and as false.

    std::vector<std::string> Clipboard::GetMimeTypesEXT()
    {
        CNA::Platform::IPlatformClipboard* clipboard =
            CNA::Platform::GetCurrentPlatform().GetClipboard();
        return clipboard != nullptr ? clipboard->GetMimeTypes() : std::vector<std::string>();
    }

    bool Clipboard::HasDataEXT(const std::string& mimeType)
    {
        CNA::Platform::IPlatformClipboard* clipboard =
            CNA::Platform::GetCurrentPlatform().GetClipboard();
        return clipboard != nullptr && clipboard->HasData(mimeType);
    }

    std::vector<std::uint8_t> Clipboard::GetDataEXT(const std::string& mimeType)
    {
        CNA::Platform::IPlatformClipboard* clipboard =
            CNA::Platform::GetCurrentPlatform().GetClipboard();
        return clipboard != nullptr ? clipboard->GetData(mimeType) : std::vector<std::uint8_t>();
    }

    bool Clipboard::SetDataEXT(const std::string& mimeType, const std::vector<std::uint8_t>& data)
    {
        return SetDataEXT({{mimeType, data}});
    }

    bool Clipboard::SetDataEXT(
        const std::vector<std::pair<std::string, std::vector<std::uint8_t>>>& formats)
    {
        CNA::Platform::IPlatformClipboard* clipboard =
            CNA::Platform::GetCurrentPlatform().GetClipboard();
        if (clipboard == nullptr)
        {
            return false;
        }
        std::vector<CNA::Platform::ClipboardOffer> offers;
        offers.reserve(formats.size());
        for (const auto& [mimeType, data] : formats)
        {
            offers.push_back({mimeType, data});
        }
        try
        {
            clipboard->SetData(offers);
            return true;
        }
        catch (const CNA::Platform::PlatformException&)
        {
            // A text-only clipboard's refusal, or ownership lost in the same instant.
            return false;
        }
    }
}
