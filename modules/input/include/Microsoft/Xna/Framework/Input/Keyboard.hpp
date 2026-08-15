// SPDX-License-Identifier: MS-PL
#pragma once

#include "KeyboardState.hpp"
#include "Keys.hpp"
#include "CNA/CNAHelper.hpp"
#include "Microsoft/Xna/Framework/PlayerIndex.hpp"

#include <cstdint>
#include <string>

// Forward-declared rather than including CNA/Input/KeyModifiers.hpp: this is a strict-XNA header
// (P1-027/P1-028) and must not require a consumer to pull in a CNA::Input extension header just to
// use Keyboard's non-EXT surface. GetModStateEXT() below only needs the type name to declare its
// return type; Keyboard.cpp includes the real header for the definition.
namespace CNA::Input
{
    enum class KeyModifiersEXT : std::uint32_t;
}

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
         * @brief Returns the keyboard state captured at the start of the current frame.
         * @return The most recently published platform keyboard snapshot.
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
        CNAEXT static Keys GetKeyFromScancodeEXT(Keys scancode);

        /**
         * @brief CNAEXT/EXT: returns modifier and lock keys from the current frame snapshot.
         * @return A bit set of modifiers (Shift/Ctrl/Alt/Gui) and lock states (Caps/Num/Scroll/Mode).
         */
        CNAEXT static CNA::Input::KeyModifiersEXT GetModStateEXT();

        /**
         * @brief CNAEXT/EXT: returns the physical (layout-independent) name of a key.
         * @param key The key to name.
         * @return The scancode name (e.g. "A", "Space", "Left Shift"), or "" if the key has none.
         */
        CNAEXT static std::string GetScancodeNameEXT(Keys key);

        /**
         * @brief CNAEXT/EXT: returns the key for a physical key name (the inverse of GetScancodeNameEXT).
         * @param name The scancode name to look up.
         * @return The corresponding key, or Keys::None if the name is not recognized.
         */
        CNAEXT static Keys GetScancodeFromNameEXT(const std::string& name);

        /**
         * @brief CNAEXT/EXT: returns the layout-dependent (virtual key) name of a key.
         * @param key The key to name.
         * @return The key name for the active keyboard layout, or "" if the key has none.
         */
        CNAEXT static std::string GetKeyNameEXT(Keys key);

        /**
         * @brief CNAEXT/EXT: returns the key for a layout-dependent key name (the inverse of GetKeyNameEXT).
         * @param name The key name to look up.
         * @return The corresponding key, or Keys::None if the name is not recognized.
         */
        CNAEXT static Keys GetKeyFromNameEXT(const std::string& name);
    };
}
