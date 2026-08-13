// SPDX-License-Identifier: MS-PL
#pragma once

#include <string>

#include "Microsoft/Xna/Framework/Input/Keys.hpp"

namespace CNA::Internal::Input
{
    /**
     * @brief Legacy key-name helpers and input-state test controls.
     *
     * The name is retained for source compatibility. Native keyboard queries are delegated to
     * `IPlatformKeyboard`; this type has no SDL dependency.
     */
    class SdlInputBridge
    {
    public:
        /**
         * @brief Test-only: forces scancode mode on/off, overriding the cached
         *        `FNA_KEYBOARD_USE_SCANCODES` env value (which can't be changed in-process).
         * @param enabled True to force scancode mode on, false to force it off.
         */
        static void SetScancodeModeForTests(bool enabled);

        /** @brief Test-only: reverts scancode mode to the `FNA_KEYBOARD_USE_SCANCODES` env value. */
        static void ClearScancodeModeForTests();

        /**
         * @brief Test-only: resets `SdlInputBridge`'s process-wide file-static state — the
         *        text-input suppression + control-down flags, the platform-finger-id→touch-id map (and
         *        its counter), and the scancode-mode override — so tests don't leak state into
         *        one another. Not part of the runtime input path.
         */
        static void ResetForTests();

        static Microsoft::Xna::Framework::Input::Keys GetKeyFromScancode(
            Microsoft::Xna::Framework::Input::Keys scancode
        );

        /** @brief CNAEXT/EXT: the physical (layout-independent) name of a key, or "" if it has none. */
        static std::string GetScancodeName(Microsoft::Xna::Framework::Input::Keys key);

        /** @brief CNAEXT/EXT: the Keys value for a physical key name, or Keys::None if unrecognized. */
        static Microsoft::Xna::Framework::Input::Keys GetScancodeFromName(const std::string& name);

        /** @brief CNAEXT/EXT: the layout-dependent name of a key, or "" if it has none. */
        static std::string GetKeyName(Microsoft::Xna::Framework::Input::Keys key);

        /** @brief CNAEXT/EXT: the Keys value for a layout-dependent key name, or Keys::None if unrecognized. */
        static Microsoft::Xna::Framework::Input::Keys GetKeyFromName(const std::string& name);
    };
}
