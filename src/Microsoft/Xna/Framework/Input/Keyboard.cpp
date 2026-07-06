// SPDX-License-Identifier: MS-PL
//
// Created by robertvokac on 5/26/25.
//

#include "Microsoft/Xna/Framework/Input/Keyboard.hpp"

#include "CNA/Internal/Input/InputManager.hpp"
#include "CNA/Internal/Input/SdlInputBridge.hpp"

namespace Microsoft::Xna::Framework::Input
{
    KeyboardState Keyboard::GetState()
    {
        return CNA::Internal::Input::InputManager::GetKeyboardState();
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
        return CNA::Internal::Input::SdlInputBridge::GetModState();
    }

    std::string Keyboard::GetScancodeNameEXT(Keys key)
    {
        return CNA::Internal::Input::SdlInputBridge::GetScancodeName(key);
    }

    Keys Keyboard::GetScancodeFromNameEXT(const std::string& name)
    {
        return CNA::Internal::Input::SdlInputBridge::GetScancodeFromName(name);
    }
}
