// SPDX-License-Identifier: MS-PL
#pragma once

#include "CNA/Platform/Input/Scancode.hpp"

#include <cstdint>

namespace CNA::Platform::Win32 {

    /**
     * @brief A physical key as Windows reports it: a set-1 scan code plus the extended flag.
     *
     * Windows puts the scan code in `lParam` bits 16-23 and the extended-key flag in bit 24. The
     * pair is what identifies a key, not the scan code alone: Left and Right Control both report
     * scan code `0x1D` and differ only in that flag, and Pause and Num Lock both report `0x45`.
     * Packing them into one value (`scanCode | 0x100` when extended) is what lets a single lookup
     * table answer, which is why this type exists instead of two loose integers.
     */
    struct Win32PhysicalKey
    {
        /** @brief The set-1 scan code from `lParam` bits 16-23. */
        std::uint8_t scanCode = 0;
        /** @brief The extended-key flag from `lParam` bit 24. */
        bool extended = false;

        /**
         * @brief Packs the pair into the single value the lookup tables are keyed by.
         *
         * @return `scanCode`, with bit 8 set when the key is extended.
         */
        [[nodiscard]] constexpr std::uint16_t ToPacked() const
        {
            return static_cast<std::uint16_t>(scanCode | (extended ? 0x100u : 0u));
        }
    };

    /**
     * @brief Extracts the physical key Windows encoded in a keyboard message's `lParam`.
     *
     * @param lParam The `lParam` of a `WM_KEYDOWN`/`WM_KEYUP`/`WM_SYSKEYDOWN`/`WM_SYSKEYUP`.
     * @return The scan code and extended flag.
     */
    [[nodiscard]] Win32PhysicalKey PhysicalKeyFromLParam(std::uint64_t lParam);

    /**
     * @brief Translates a Windows physical key into CNA's layout-independent physical key.
     *
     * CNA's `Scancode` values are **USB HID keyboard usage IDs**, which is a different numbering
     * from the PS/2 set-1 codes Windows reports — so this is a real translation table, not a cast.
     * Getting it wrong is invisible on a US layout and moves a French player's movement keys, which
     * is the whole reason the contract keeps physical and logical keys apart.
     *
     * @param key The scan code and extended flag from a keyboard message.
     * @return The matching physical key, or `Scancode::Unknown` when Windows reported a key this
     *         contract does not name.
     */
    [[nodiscard]] Scancode ToScancode(Win32PhysicalKey key);

    /**
     * @brief Translates CNA's physical key back into the scan code Windows expects.
     *
     * The inverse of @ref ToScancode, needed by the layout queries (`GetKeyFromScancode`,
     * `GetKeyName`) which have to hand a scan code to `MapVirtualKeyW`.
     *
     * @param scancode The physical key.
     * @param key Receives the Windows scan code and extended flag; untouched when this returns
     *        false.
     * @return True when this physical key exists on a Windows keyboard.
     */
    [[nodiscard]] bool ToWin32PhysicalKey(Scancode scancode, Win32PhysicalKey& key);

} // namespace CNA::Platform::Win32
