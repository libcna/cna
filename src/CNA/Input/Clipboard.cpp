// SPDX-License-Identifier: MS-PL
#include "CNA/Input/Clipboard.hpp"

#include <SDL3/SDL.h>

namespace CNA::Input
{
    std::string Clipboard::GetTextEXT()
    {
        // SDL_GetClipboardText always returns a heap-allocated string (empty "" if the clipboard has no
        // text); the caller owns it and must SDL_free it.
        char* raw = SDL_GetClipboardText();
        std::string result = (raw != nullptr) ? raw : "";
        SDL_free(raw);
        return result;
    }

    void Clipboard::SetTextEXT(const std::string& text)
    {
        SDL_SetClipboardText(text.c_str());
    }

    bool Clipboard::HasTextEXT()
    {
        return SDL_HasClipboardText();
    }
}
