// SPDX-License-Identifier: MS-PL
//
// Created by robertvokac on 5/26/25.
//

#include "Microsoft/Xna/Framework/Input/Keyboard.hpp"
#include "CNA/Internal/Input/SystemInput.hpp"
#include "../Internal/SystemInput.hpp"

#include "CNA/Internal/Input/SdlInputBridge.hpp"
#include "CNA/Input/KeyModifiers.hpp"
#include "CNA/Platform/CurrentPlatform.hpp"
#include "CNA/Platform/Input/IPlatformKeyboard.hpp"
#include "CNA/Platform/Input/KeyCode.hpp"

#include <unordered_set>

namespace
{
    [[nodiscard]] CNA::Platform::IPlatformKeyboard* CurrentKeyboard()
    {
        return CNA::Platform::GetCurrentPlatform().GetKeyboard();
    }

    [[nodiscard]] CNA::Input::KeyModifiersEXT ToInputModifiers(const std::uint16_t modifiers)
    {
        using CNA::Input::KeyModifiersEXT;
        using CNA::Platform::HasModifier;
        using CNA::Platform::KeyModifier;

        KeyModifiersEXT result = KeyModifiersEXT::None;
        if (HasModifier(modifiers, KeyModifier::Shift)) result |= KeyModifiersEXT::Shift;
        if (HasModifier(modifiers, KeyModifier::Control)) result |= KeyModifiersEXT::Ctrl;
        if (HasModifier(modifiers, KeyModifier::Alt)) result |= KeyModifiersEXT::Alt;
        if (HasModifier(modifiers, KeyModifier::Gui)) result |= KeyModifiersEXT::Gui;
        if (HasModifier(modifiers, KeyModifier::CapsLock)) result |= KeyModifiersEXT::Caps;
        if (HasModifier(modifiers, KeyModifier::NumLock)) result |= KeyModifiersEXT::Num;
        if (HasModifier(modifiers, KeyModifier::ScrollLock)) result |= KeyModifiersEXT::Scroll;
        if (HasModifier(modifiers, KeyModifier::Mode)) result |= KeyModifiersEXT::Mode;
        return result;
    }
}

namespace
{
    std::unordered_set<Microsoft::Xna::Framework::Input::Keys> PressedKeys()
    {
        std::unordered_set<Microsoft::Xna::Framework::Input::Keys> pressedKeys;
        const CNA::Platform::IPlatformKeyboard* keyboard = CurrentKeyboard();
        if (keyboard == nullptr)
        {
            return pressedKeys;
        }
        const CNA::Platform::KeyboardSnapshot& snapshot = keyboard->GetSnapshot();
        pressedKeys.reserve(snapshot.pressedKeys.size());
        for (const CNA::Platform::KeyCode key : snapshot.pressedKeys)
        {
            if (key != CNA::Platform::KeyCode::None)
            {
                pressedKeys.insert(static_cast<Microsoft::Xna::Framework::Input::Keys>(key));
            }
        }
        return pressedKeys;
    }
}

namespace CNA::Internal::Input
{
    Microsoft::Xna::Framework::Input::KeyboardState systemKeyboardState()
    {
        return Microsoft::Xna::Framework::Input::KeyboardState(PressedKeys());
    }
}

namespace Microsoft::Xna::Framework::Input
{
    KeyboardState Keyboard::GetState()
    {
        std::unordered_set<Keys> pressedKeys = PressedKeys();
        // While the Guide owns the keyboard the game sees nothing, and keys it used stay hidden
        // until released.
        CNA::Internal::Input::filterGameKeys(pressedKeys);
        return KeyboardState(pressedKeys);
    }

    KeyboardState Keyboard::GetState(Microsoft::Xna::Framework::PlayerIndex /*playerIndex*/)
    {
        return GetState();
    }

    Keys Keyboard::GetKeyFromScancodeEXT(Keys scancode)
    {
        return CNA::Internal::Input::SdlInputBridge::GetKeyFromScancode(scancode);
    }

    CNA::Input::KeyModifiersEXT Keyboard::GetModStateEXT()
    {
        const CNA::Platform::IPlatformKeyboard* keyboard = CurrentKeyboard();
        return keyboard != nullptr
            ? ToInputModifiers(keyboard->GetSnapshot().modifiers)
            : CNA::Input::KeyModifiersEXT::None;
    }

    std::string Keyboard::GetScancodeNameEXT(Keys key)
    {
        return CNA::Internal::Input::SdlInputBridge::GetScancodeName(key);
    }

    Keys Keyboard::GetScancodeFromNameEXT(const std::string& name)
    {
        return CNA::Internal::Input::SdlInputBridge::GetScancodeFromName(name);
    }

    std::string Keyboard::GetKeyNameEXT(Keys key)
    {
        return CNA::Internal::Input::SdlInputBridge::GetKeyName(key);
    }

    Keys Keyboard::GetKeyFromNameEXT(const std::string& name)
    {
        return CNA::Internal::Input::SdlInputBridge::GetKeyFromName(name);
    }
}
