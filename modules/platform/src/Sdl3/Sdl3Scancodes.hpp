// SPDX-License-Identifier: MS-PL
#pragma once

#include "CNA/Platform/Input/Scancode.hpp"

#include <SDL3/SDL.h>

#include <cstdint>

namespace CNA::Platform::Sdl3 {

    /**
     * @brief Translates an SDL scancode into the contract's physical-key identifier.
     *
     * Both numberings are USB HID keyboard usage IDs, so this is a **validated cast** rather than
     * a table — and the validation is the whole point. SDL's scancode space is larger than the
     * HID keyboard page: it continues past it with SDL's own values for media keys, and CNA names
     * only a subset of those. Casting blindly would let an unnamed SDL value through as a
     * `Scancode` that no `switch` in the codebase handles and that `ToString` cannot name.
     *
     * `Sleep` is the one CNA key whose value comes from SDL's extended range rather than the HID
     * keyboard page, which is why the range check cannot simply be "below 232".
     *
     * @param scancode SDL's scancode.
     * @return The equivalent physical key, or `Scancode::Unknown` for a key CNA does not name.
     */
    [[nodiscard]] inline Scancode ToScancode(const SDL_Scancode scancode)
    {
        const auto value = static_cast<std::uint16_t>(scancode);
        return IsKnownScancode(value) ? static_cast<Scancode>(value) : Scancode::Unknown;
    }

    /**
     * @brief Translates the contract's physical-key identifier back into an SDL scancode.
     *
     * The reverse of ToScancode, and lossless for every key CNA names: `Scancode::Unknown` maps
     * to `SDL_SCANCODE_UNKNOWN`, which is the same "no key" answer on both sides.
     *
     * @param scancode The contract's physical key.
     * @return The equivalent SDL scancode.
     */
    [[nodiscard]] inline SDL_Scancode ToSdlScancode(const Scancode scancode)
    {
        return static_cast<SDL_Scancode>(static_cast<std::uint16_t>(scancode));
    }

} // namespace CNA::Platform::Sdl3
