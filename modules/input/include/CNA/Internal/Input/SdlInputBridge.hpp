// SPDX-License-Identifier: MS-PL
#pragma once

#include <SDL3/SDL.h>
#include <cstdint>
#include <string>
#include <vector>

#include "Microsoft/Xna/Framework/Input/Keys.hpp"

namespace CNA::Internal::Input
{
    /**
     * @brief Bridge between SDL3 events and CNA internal input state.
     *
     * This bridge knows SDL types, but exposes them only internally.
     */
    class SdlInputBridge
    {
    public:
        /**
         * @brief Compatibility adapter for legacy SDL-shaped input tests.
         *
         * Input events are converted to `PlatformEvent` and delegated to `PlatformInputBridge`.
         * `Game` consumes `PlatformEvent` directly; this remains only while PLAT-90 retires the
         * remaining keyboard/touch native-shaped tests.
         */
        static void ProcessEvent(const SDL_Event& event);

        /**
         * @brief Translates a US-layout Keys value to the Keys value the current keyboard
         * layout produces at that same physical key position. Returns Keys::None if no
         * mapping exists in either direction.
         */
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
         *        text-input suppression + control-down flags, the SDL-finger-id→touch-id map (and
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
