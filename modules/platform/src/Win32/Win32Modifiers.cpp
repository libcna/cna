// SPDX-License-Identifier: MS-PL

#include "Win32Modifiers.hpp"

#include "Win32Common.hpp"

#include "CNA/Platform/Input/IPlatformKeyboard.hpp"

namespace CNA::Platform::Win32 {

    namespace {

        bool IsHeld(const int virtualKey)
        {
            return (GetKeyState(virtualKey) & 0x8000) != 0;
        }

        bool IsToggled(const int virtualKey)
        {
            return (GetKeyState(virtualKey) & 0x0001) != 0;
        }

        void Set(std::uint16_t& modifiers, const KeyModifier modifier, const bool active)
        {
            if (active)
                modifiers |= static_cast<std::uint16_t>(modifier);
        }

    } // namespace

    std::uint16_t CurrentModifiers()
    {
        std::uint16_t modifiers = 0;
        Set(modifiers, KeyModifier::Shift, IsHeld(VK_SHIFT));
        Set(modifiers, KeyModifier::Control, IsHeld(VK_CONTROL));
        Set(modifiers, KeyModifier::Alt, IsHeld(VK_MENU));
        Set(modifiers, KeyModifier::Gui, IsHeld(VK_LWIN) || IsHeld(VK_RWIN));
        Set(modifiers, KeyModifier::CapsLock, IsToggled(VK_CAPITAL));
        Set(modifiers, KeyModifier::NumLock, IsToggled(VK_NUMLOCK));
        Set(modifiers, KeyModifier::ScrollLock, IsToggled(VK_SCROLL));

        // AltGr is not a key Windows reports. A layout that has it synthesises Right-Alt together
        // with Left-Control, and that pair is the only signal available -- so Mode is reported for
        // exactly it, and a genuine Ctrl+Alt chord is indistinguishable by construction, on
        // Windows as much as here.
        Set(modifiers, KeyModifier::Mode, IsHeld(VK_RMENU) && IsHeld(VK_LCONTROL));
        return modifiers;
    }

} // namespace CNA::Platform::Win32
