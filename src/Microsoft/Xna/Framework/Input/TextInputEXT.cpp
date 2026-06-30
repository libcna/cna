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
    std::function<void(char)>                      TextInputEXT::TextInput   = nullptr;
    std::function<void(const std::string&, int, int)> TextInputEXT::TextEditing = nullptr;
    std::uintptr_t                                  TextInputEXT::WindowHandle = 0;

    bool TextInputEXT::IsTextInputActive()
    {
        if (SDL_Window* window = ToSdlWindow(WindowHandle))
        {
            return SDL_TextInputActive(window);
        }
        return false;
    }

    bool TextInputEXT::IsScreenKeyboardShown()
    {
        return IsScreenKeyboardShown(WindowHandle);
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
        if (SDL_Window* window = ToSdlWindow(WindowHandle))
        {
            SDL_StartTextInput(window);
        }
    }

    void TextInputEXT::StopTextInput()
    {
        if (SDL_Window* window = ToSdlWindow(WindowHandle))
        {
            SDL_StopTextInput(window);
        }
    }

    void TextInputEXT::SetInputRectangle(const Microsoft::Xna::Framework::Rectangle& rectangle)
    {
        if (SDL_Window* window = ToSdlWindow(WindowHandle))
        {
            SDL_Rect rect;
            rect.x = rectangle.X;
            rect.y = rectangle.Y;
            rect.w = rectangle.Width;
            rect.h = rectangle.Height;
            // Cursor offset 0: FNA passes 0 here as well (no IME cursor hint).
            SDL_SetTextInputArea(window, &rect, 0);
        }
    }

    void TextInputEXT::INTERNAL_OnTextInput(char c)
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
