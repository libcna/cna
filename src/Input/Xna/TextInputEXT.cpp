// SPDX-License-Identifier: MS-PL
#include "Microsoft/Xna/Framework/Input/TextInputEXT.hpp"

#include "CNA/Input/TextInputType.hpp"

#include <SDL3/SDL.h>

namespace
{
    // WindowHandle stores an SDL_Window* as an integer (FNA models it as IntPtr).
    inline SDL_Window* ToSdlWindow(std::uintptr_t handle)
    {
        return reinterpret_cast<SDL_Window*>(handle);
    }

    SDL_TextInputType ToSdlTextInputType(CNA::Input::TextInputTypeEXT type)
    {
        switch (type)
        {
            case CNA::Input::TextInputTypeEXT::Text:                 return SDL_TEXTINPUT_TYPE_TEXT;
            case CNA::Input::TextInputTypeEXT::TextName:             return SDL_TEXTINPUT_TYPE_TEXT_NAME;
            case CNA::Input::TextInputTypeEXT::TextEmail:            return SDL_TEXTINPUT_TYPE_TEXT_EMAIL;
            case CNA::Input::TextInputTypeEXT::TextUsername:         return SDL_TEXTINPUT_TYPE_TEXT_USERNAME;
            case CNA::Input::TextInputTypeEXT::TextPasswordHidden:   return SDL_TEXTINPUT_TYPE_TEXT_PASSWORD_HIDDEN;
            case CNA::Input::TextInputTypeEXT::TextPasswordVisible:  return SDL_TEXTINPUT_TYPE_TEXT_PASSWORD_VISIBLE;
            case CNA::Input::TextInputTypeEXT::Number:                return SDL_TEXTINPUT_TYPE_NUMBER;
            case CNA::Input::TextInputTypeEXT::NumberPasswordHidden:  return SDL_TEXTINPUT_TYPE_NUMBER_PASSWORD_HIDDEN;
            case CNA::Input::TextInputTypeEXT::NumberPasswordVisible: return SDL_TEXTINPUT_TYPE_NUMBER_PASSWORD_VISIBLE;
        }
        return SDL_TEXTINPUT_TYPE_TEXT;
    }
}

namespace Microsoft::Xna::Framework::Input
{
    System::MulticastAction<charcs>                       TextInputEXT::TextInput;
    System::MulticastAction<const std::string&, int, int> TextInputEXT::TextEditing;
    System::MulticastAction<const std::vector<std::string>&, int, bool> TextInputEXT::TextEditingCandidatesEXT;
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

    void TextInputEXT::StartTextInputWithTypeEXT(CNA::Input::TextInputTypeEXT type)
    {
        if (SDL_Window* window = ToSdlWindow(windowHandle_))
        {
            SDL_PropertiesID props = SDL_CreateProperties();
            SDL_SetNumberProperty(props, SDL_PROP_TEXTINPUT_TYPE_NUMBER, ToSdlTextInputType(type));
            SDL_StartTextInputWithProperties(window, props);
            SDL_DestroyProperties(props);
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

    void TextInputEXT::INTERNAL_OnTextEditingCandidates(
        const std::vector<std::string>& candidates, int selected, bool horizontal)
    {
        if (TextEditingCandidatesEXT)
            TextEditingCandidatesEXT(candidates, selected, horizontal);
    }

    void TextInputEXT::ResetForTests()
    {
        TextInput    = nullptr;
        TextEditing  = nullptr;
        TextEditingCandidatesEXT = nullptr;
        windowHandle_ = 0;
    }
}
