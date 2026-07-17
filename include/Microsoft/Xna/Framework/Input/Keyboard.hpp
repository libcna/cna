// SPDX-License-Identifier: MS-PL
#pragma once

#include "KeyboardState.hpp"
#include "Keys.hpp"
#include "CNA/CNAHelper.hpp"
#include "CNA/Input/KeyModifiers.hpp"
#include "Microsoft/Xna/Framework/PlayerIndex.hpp"

#include <string>

namespace Microsoft::Xna::Framework::Input
{
    /**
     * @brief Allows getting keystrokes from keyboard.
     */
    class Keyboard
    {
    public:
        /** @brief Keyboard is a static class (XNA `public static class Keyboard`) and cannot be instantiated. */
        Keyboard() = delete;

        /**
         * @brief Returns the current keyboard state.
         * @return The current keyboard state.
         */
        static KeyboardState GetState();

        /**
         * @brief Returns the current keyboard state for a given player.
         * @param playerIndex Player index of the keyboard.
         * @return The current keyboard state.
         */
        static KeyboardState GetState(Microsoft::Xna::Framework::PlayerIndex playerIndex);

        /**
         * @brief Returns the Keys value corresponding to the given hardware scancode (FNA extension).
         * @param scancode The scancode to translate.
         * @return The corresponding Keys value.
         */
        NOXNA static Keys GetKeyFromScancodeEXT(Keys scancode);

        /**
         * @brief NOXNA/EXT: returns the currently active keyboard modifier and lock keys.
         * @return A bit set of active modifiers (Shift/Ctrl/Alt/Gui) and lock states (Caps/Num/Scroll/Mode).
         */
        NOXNA static CNA::Input::KeyModifiersEXT GetModStateEXT();

        /**
         * @brief NOXNA/EXT: returns the physical (layout-independent) name of a key.
         * @param key The key to name.
         * @return The scancode name (e.g. "A", "Space", "Left Shift"), or "" if the key has none.
         */
        NOXNA static std::string GetScancodeNameEXT(Keys key);

        /**
         * @brief NOXNA/EXT: returns the key for a physical key name (the inverse of GetScancodeNameEXT).
         * @param name The scancode name to look up.
         * @return The corresponding key, or Keys::None if the name is not recognized.
         */
        NOXNA static Keys GetScancodeFromNameEXT(const std::string& name);

        /**
         * @brief NOXNA/EXT: returns the layout-dependent (virtual key) name of a key.
         * @param key The key to name.
         * @return The key name for the active keyboard layout, or "" if the key has none.
         */
        NOXNA static std::string GetKeyNameEXT(Keys key);

        /**
         * @brief NOXNA/EXT: returns the key for a layout-dependent key name (the inverse of GetKeyNameEXT).
         * @param name The key name to look up.
         * @return The corresponding key, or Keys::None if the name is not recognized.
         */
        NOXNA static Keys GetKeyFromNameEXT(const std::string& name);
    };
}
