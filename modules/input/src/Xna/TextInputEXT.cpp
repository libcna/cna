// SPDX-License-Identifier: MS-PL
#include "Microsoft/Xna/Framework/Input/TextInputEXT.hpp"

#include "CNA/Input/TextInputType.hpp"
#include "CNA/Platform/CurrentPlatform.hpp"
#include "CNA/Platform/IPlatform.hpp"
#include "CNA/Platform/Input/IPlatformTextInput.hpp"
#include "CNA/Platform/PlatformException.hpp"

namespace
{
    [[nodiscard]] CNA::Platform::IPlatformTextInput* CurrentTextInput()
    {
        return CNA::Platform::GetCurrentPlatform().GetTextInput();
    }

    [[nodiscard]] CNA::Platform::TextInputType ToPlatformTextInputType(
        const CNA::Input::TextInputTypeEXT type)
    {
        using PlatformType = CNA::Platform::TextInputType;
        switch (type)
        {
            case CNA::Input::TextInputTypeEXT::Text:                  return PlatformType::Text;
            case CNA::Input::TextInputTypeEXT::TextName:              return PlatformType::TextName;
            case CNA::Input::TextInputTypeEXT::TextEmail:             return PlatformType::TextEmail;
            case CNA::Input::TextInputTypeEXT::TextUsername:          return PlatformType::TextUsername;
            case CNA::Input::TextInputTypeEXT::TextPasswordHidden:    return PlatformType::TextPasswordHidden;
            case CNA::Input::TextInputTypeEXT::TextPasswordVisible:   return PlatformType::TextPasswordVisible;
            case CNA::Input::TextInputTypeEXT::Number:                return PlatformType::Number;
            case CNA::Input::TextInputTypeEXT::NumberPasswordHidden:  return PlatformType::NumberPasswordHidden;
            case CNA::Input::TextInputTypeEXT::NumberPasswordVisible: return PlatformType::NumberPasswordVisible;
        }
        return PlatformType::Text;
    }
}

namespace Microsoft::Xna::Framework::Input
{
    System::MulticastAction<charcs>                       TextInputEXT::TextInput;
    System::MulticastAction<const std::string&, int, int> TextInputEXT::TextEditing;
    System::MulticastAction<const std::vector<std::string>&, int, bool> TextInputEXT::TextEditingCandidatesEXT;
    std::uintptr_t                                  TextInputEXT::windowHandle_ = 0;
    std::uint32_t                                   TextInputEXT::windowId_ = 0;

    std::uintptr_t TextInputEXT::getWindowHandleProperty()
    {
        return windowHandle_;
    }

    void TextInputEXT::setWindowHandleProperty(std::uintptr_t value)
    {
        windowHandle_ = value;
        // A raw handle carries no portable identity. GraphicsDevice publishes the matching id in
        // a second, explicit call; clearing first prevents an unrelated replacement handle from
        // inheriting the old window's id during that hand-off.
        windowId_ = 0;
    }

    void TextInputEXT::INTERNAL_setWindowId(const std::uint32_t value)
    {
        windowId_ = windowHandle_ != 0 ? value : 0;
    }

    bool TextInputEXT::IsTextInputActive()
    {
        CNA::Platform::IPlatformTextInput* input =
            windowId_ != 0 ? CurrentTextInput() : nullptr;
        return input != nullptr && input->IsActive(windowId_);
    }

    bool TextInputEXT::IsScreenKeyboardShown()
    {
        return IsScreenKeyboardShown(windowHandle_);
    }

    bool TextInputEXT::IsScreenKeyboardShown(std::uintptr_t window)
    {
        // The frozen FNA surface accepts a native handle, but the portable platform contract
        // intentionally does not. GraphicsDevice publishes the one active input surface as a
        // handle/id pair, so only that known pair can be queried safely.
        CNA::Platform::IPlatformTextInput* input =
            window != 0 && window == windowHandle_ && windowId_ != 0
                ? CurrentTextInput()
                : nullptr;
        return input != nullptr && input->IsScreenKeyboardShown(windowId_);
    }

    void TextInputEXT::StartTextInput()
    {
        CNA::Platform::IPlatformTextInput* input =
            windowId_ != 0 ? CurrentTextInput() : nullptr;
        if (input != nullptr)
        {
            try
            {
                input->Start(windowId_, CNA::Platform::TextInputType::Default);
            }
            catch (const CNA::Platform::PlatformException&)
            {
                // FNA's void API ignores a native start failure; preserve that public contract.
            }
        }
    }

    void TextInputEXT::StopTextInput()
    {
        CNA::Platform::IPlatformTextInput* input =
            windowId_ != 0 ? CurrentTextInput() : nullptr;
        if (input != nullptr)
        {
            try
            {
                input->Stop(windowId_);
            }
            catch (const CNA::Platform::PlatformException&)
            {
                // FNA's void API ignores a native stop failure; preserve that public contract.
            }
        }
    }

    void TextInputEXT::StartTextInputWithTypeEXT(CNA::Input::TextInputTypeEXT type)
    {
        CNA::Platform::IPlatformTextInput* input =
            windowId_ != 0 ? CurrentTextInput() : nullptr;
        if (input != nullptr)
        {
            try
            {
                input->Start(windowId_, ToPlatformTextInputType(type));
            }
            catch (const CNA::Platform::PlatformException&)
            {
                // FNA's void API ignores a native start failure; preserve that public contract.
            }
        }
    }

    void TextInputEXT::SetInputRectangle(const Microsoft::Xna::Framework::Rectangle& rectangle)
    {
        CNA::Platform::IPlatformTextInput* input =
            windowId_ != 0 ? CurrentTextInput() : nullptr;
        if (input != nullptr)
        {
            // Cursor offset 0 matches FNA's SetTextInputRectangle implementation exactly. The
            // native API treats it as the text cursor's x-offset relative to the rectangle (an
            // optional IME placement hint); FNA passes 0 and marks the missing cursor information
            // as an upstream FIXME. CNA follows that behaviour rather than inventing an offset.
            try
            {
                input->SetInputArea(
                    windowId_, CNA::Platform::TextInputArea{
                                   rectangle.X, rectangle.Y, rectangle.Width, rectangle.Height, 0});
            }
            catch (const CNA::Platform::PlatformException&)
            {
                // FNA's void API ignores a native area-update failure.
            }
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
        windowId_ = 0;
    }
}
