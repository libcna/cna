// SPDX-License-Identifier: MS-PL
#include "Microsoft/Xna/Framework/Input/TextInputEXT.hpp"

namespace Microsoft::Xna::Framework::Input
{
    std::function<void(char)>                      TextInputEXT::TextInput   = nullptr;
    std::function<void(const std::string&, int, int)> TextInputEXT::TextEditing = nullptr;
    std::uintptr_t                                  TextInputEXT::WindowHandle = 0;

    bool TextInputEXT::IsTextInputActive()
    {
        return false;
    }

    bool TextInputEXT::IsScreenKeyboardShown()
    {
        return IsScreenKeyboardShown(WindowHandle);
    }

    bool TextInputEXT::IsScreenKeyboardShown(std::uintptr_t /*window*/)
    {
        return false;
    }

    void TextInputEXT::StartTextInput()
    {
    }

    void TextInputEXT::StopTextInput()
    {
    }

    void TextInputEXT::SetInputRectangle(const Microsoft::Xna::Framework::Rectangle& /*rectangle*/)
    {
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
