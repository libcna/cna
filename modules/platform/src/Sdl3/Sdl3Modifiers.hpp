// SPDX-License-Identifier: MS-PL
#pragma once

#include "CNA/Platform/Input/IPlatformKeyboard.hpp"

#include <SDL3/SDL.h>

#include <cstdint>

namespace CNA::Platform::Sdl3 {

    /**
     * @brief Translates SDL's modifier mask into the contract's `KeyModifier` layout.
     *
     * Shared between the keyboard snapshot and the event mapper deliberately. The two produce
     * modifier masks that a caller will compare against each other -- a key event's modifiers
     * against the frame's snapshot -- and two independent translations are exactly how those
     * quietly stop agreeing.
     *
     * The bit values happen to be close to SDL's, which is why passing the native mask straight
     * through would work today and be silently wrong on a second implementation. The sided masks
     * (`SDL_KMOD_SHIFT` is `LSHIFT | RSHIFT`) collapse here, because the contract does not
     * distinguish left from right.
     *
     * @param mod SDL's modifier mask.
     * @return The equivalent mask of `KeyModifier` bits.
     */
    [[nodiscard]] inline std::uint16_t ToPlatformModifiers(const SDL_Keymod mod)
    {
        std::uint16_t result = 0;
        const auto set = [&result](const KeyModifier modifier) {
            result |= static_cast<std::uint16_t>(modifier);
        };

        if ((mod & SDL_KMOD_SHIFT) != 0)  set(KeyModifier::Shift);
        if ((mod & SDL_KMOD_CTRL) != 0)   set(KeyModifier::Control);
        if ((mod & SDL_KMOD_ALT) != 0)    set(KeyModifier::Alt);
        if ((mod & SDL_KMOD_GUI) != 0)    set(KeyModifier::Gui);
        if ((mod & SDL_KMOD_CAPS) != 0)   set(KeyModifier::CapsLock);
        if ((mod & SDL_KMOD_NUM) != 0)    set(KeyModifier::NumLock);
        if ((mod & SDL_KMOD_SCROLL) != 0) set(KeyModifier::ScrollLock);
        if ((mod & SDL_KMOD_MODE) != 0)   set(KeyModifier::Mode);
        return result;
    }

} // namespace CNA::Platform::Sdl3
