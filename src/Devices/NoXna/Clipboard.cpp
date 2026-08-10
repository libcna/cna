// SPDX-License-Identifier: MS-PL
#include "CNA/Devices/Clipboard.hpp"

#ifdef CNA_DEVICES

#include <SDL3/SDL_clipboard.h>
#include <SDL3/SDL_stdinc.h>

namespace CNA::Devices
{
    bool Clipboard::setTextProperty(const std::string& text)
    {
        return SDL_SetClipboardText(text.c_str());
    }

    std::string Clipboard::getTextProperty()
    {
        char* text = SDL_GetClipboardText();
        if (text == nullptr)
        {
            return {};
        }

        std::string result(text);
        SDL_free(text);
        return result;
    }

    bool Clipboard::getHasTextProperty()
    {
        return SDL_HasClipboardText();
    }
} // namespace CNA::Devices

#endif // CNA_DEVICES
