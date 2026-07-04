// SPDX-License-Identifier: MS-PL
#include "Microsoft/Xna/Framework/Input/TextInputEXT.hpp"

#include <SDL3/SDL.h>

namespace
{
    // WindowHandle stores an SDL_Window* as an integer (FNA models it as IntPtr).
    inline SDL_Window* ToSdlWindow(std::uintptr_t handle)
    {
        return reinterpret_cast<SDL_Window*>(handle);
    }
}

namespace Microsoft::Xna::Framework::Input
{
    std::function<void(charcs)>                     TextInputEXT::TextInput   = nullptr;
    std::function<void(const std::string&, int, int)> TextInputEXT::TextEditing = nullptr;
    std::uintptr_t                                  TextInputEXT::windowHandle_ = 0;

    std::uintptr_t TextInputEXT::getWindowHandleProperty()
    {
        return windowHandle_;
    }

    void TextInputEXT::setWindowHandleProperty(std::uintptr_t value)
    {
        windowHandle_ = value;
    }

    bool TextInputEXT::IsTextInputActive()
    {
        if (SDL_Window* window = ToSdlWindow(windowHandle_))
        {
            return SDL_TextInputActive(window);
        }
        return false;
    }

    bool TextInputEXT::IsScreenKeyboardShown()
    {
        return IsScreenKeyboardShown(windowHandle_);
    }

    bool TextInputEXT::IsScreenKeyboardShown(std::uintptr_t window)
    {
        if (SDL_Window* w = ToSdlWindow(window))
        {
            return SDL_ScreenKeyboardShown(w);
        }
        return false;
    }

    void TextInputEXT::StartTextInput()
    {
        // Guard against a null window: WindowHandle is not populated until the window
        // is created (plan_input.md Task 703). FNA passes the handle straight through.
        if (SDL_Window* window = ToSdlWindow(windowHandle_))
        {
            SDL_StartTextInput(window);
        }
    }

    void TextInputEXT::StopTextInput()
    {
        if (SDL_Window* window = ToSdlWindow(windowHandle_))
        {
            SDL_StopTextInput(window);
        }
    }

    void TextInputEXT::SetInputRectangle(const Microsoft::Xna::Framework::Rectangle& rectangle)
    {
        if (SDL_Window* window = ToSdlWindow(windowHandle_))
        {
            SDL_Rect rect;
            rect.x = rectangle.X;
            rect.y = rectangle.Y;
            rect.w = rectangle.Width;
            rect.h = rectangle.Height;
            // Cursor offset 0 matches FNA exactly (SetTextInputRectangle,
            // SDL3_FNAPlatform.cs:779: `SDL_SetTextInputArea(window, ref rect, 0)`). SDL3's third
            // argument is the text cursor's x-offset relative to rect->x (an optional IME
            // placement hint); FNA passes 0 and flags it with its own `// FIXME SDL3: Do we need a
            // cursor here?` — CNA follows FNA rather than inventing a cursor offset it doesn't have.
            SDL_SetTextInputArea(window, &rect, 0);
        }
    }

    void TextInputEXT::INTERNAL_OnTextInput(charcs c)
    {
        if (TextInput)
            TextInput(c);
    }

    void TextInputEXT::INTERNAL_OnTextEditing(const std::string& text, int start, int length)
    {
        if (TextEditing)
            TextEditing(text, start, length);
    }
}
