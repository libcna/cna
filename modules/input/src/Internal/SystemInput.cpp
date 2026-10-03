// SPDX-License-Identifier: MS-PL
#include "CNA/Internal/Input/SystemInput.hpp"
#include "SystemInput.hpp"
#include "CNA/Platform/Input/KeyboardGamepad.hpp"
#include <array>

namespace CNA::Internal::Input
{
    namespace
    {
        using Microsoft::Xna::Framework::Input::Keys;
        bool owns = false;
        // What was held when the system let go: hidden from the game until released.
        std::unordered_set<Keys> maskedKeys;
        std::array<std::uint32_t, 4> maskedButtons{};
        std::uint32_t maskedMouse = 0;
    }

    void setSystemOwnsInput(bool value)
    {
        if (owns && !value)
        {
            using namespace Microsoft::Xna::Framework::Input;
            const auto keys = systemKeyboardState().GetPressedKeys();
            maskedKeys = std::unordered_set<Keys>(keys.begin(), keys.end());
            for (int player = 0; player < 4; ++player)
            {
                const auto pad = systemGamePadState(static_cast<Microsoft::Xna::Framework::PlayerIndex>(player));
                std::uint32_t held = 0;
                for (int bit = 0; bit < 32; ++bit)
                    if (pad.IsButtonDown(static_cast<Buttons>(1u << bit))) held |= 1u << bit;
                maskedButtons[static_cast<std::size_t>(player)] = held;
            }
            // The system's pad read deliberately excludes keyboard emulation. Still suppress
            // held software buttons on dismissal, so Escape cannot immediately exit the game.
            if (CNA::Platform::KeyboardGamepad::IsEnabled())
                maskedButtons[0] |= CNA::Platform::KeyboardGamepad::GetHeldButtons();
            const auto mouse = systemMouseState();
            maskedMouse = (mouse.getLeftButtonProperty() == ButtonState::Pressed ? 0x01u : 0u)
                | (mouse.getMiddleButtonProperty() == ButtonState::Pressed ? 0x02u : 0u)
                | (mouse.getRightButtonProperty() == ButtonState::Pressed ? 0x04u : 0u)
                | (mouse.getXButton1Property() == ButtonState::Pressed ? 0x08u : 0u)
                | (mouse.getXButton2Property() == ButtonState::Pressed ? 0x10u : 0u);
        }
        owns = value;
    }

    bool systemOwnsInput() { return owns; }

    void filterGameKeys(std::unordered_set<Keys>& pressed)
    {
        if (owns)
        {
            pressed.clear();
            return;
        }
        std::erase_if(maskedKeys, [&](Keys key) { return !pressed.contains(key); });
        for (const Keys key : maskedKeys) pressed.erase(key);
    }

    std::uint32_t filterGameButtons(int slot, std::uint32_t buttons)
    {
        if (slot < 0 || slot >= 4) return buttons;
        if (owns) return 0;
        const auto index = static_cast<std::size_t>(slot);
        maskedButtons[index] &= buttons;
        return buttons & ~maskedButtons[index];
    }

    std::uint32_t filterGameMouseButtons(std::uint32_t buttons)
    {
        if (owns) return 0;
        maskedMouse &= buttons;
        return buttons & ~maskedMouse;
    }
}
