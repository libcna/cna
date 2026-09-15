// SPDX-License-Identifier: MS-PL
#pragma once

#include "CNA/Platform/Input/KeyCode.hpp"
#include "CNA/Platform/Input/Scancode.hpp"

#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

namespace CNA::Platform::Xkb {

    /**
     * @brief A keysym, as XKB defines it: the same 32-bit values in the X protocol and in
     * xkbcommon (`KeySym` in Xlib, `xkb_keysym_t` in libxkbcommon).
     *
     * This directory is compiled for the X11 and the Wayland backends alike (plans/plan_wayland.md
     * WAYLAND-0010), and it includes neither library's header: the few keysym values the tables
     * need are stated in the implementation, where they are checked at compile time against
     * whichever of the two headers the build has.
     */
    using Keysym = std::uint32_t;

    /** @brief The "no symbol" keysym (`NoSymbol`, `XKB_KEY_NoSymbol`). */
    inline constexpr Keysym kNoSymbol = 0;

    /**
     * @brief Translates an XKB key name into a physical CNA scancode.
     *
     * XKB key names are the physical-position concept `Scancode` documents: `AD01` is the key
     * where a US keyboard has `Q` on every ruleset and under every layout, because the names come
     * from `xkeyboard-config`'s `keycodes/` files, which both X servers and Wayland compositors
     * load. Keycode arithmetic (`evdev + 8`) is right only for one ruleset, and evdev codes are
     * not USB HID usage IDs anyway (plans/plan_x11.md D3).
     *
     * @param name The name, at most four characters (a longer one names no key here).
     * @return The matching scancode, or `Scancode::Unknown`.
     */
    [[nodiscard]] Scancode ScancodeFromKeyName(std::string_view name);

    /**
     * @brief Translates an XKB key name held in XKB's fixed four-byte field into a scancode.
     *
     * The field is NUL-padded rather than NUL-terminated (Xlib's `XkbKeyNameRec`), so it is read
     * length-bounded and the bytes after the name must be padding: `KP1` must not match `KP10`.
     *
     * @param name The four-byte field.
     * @return The matching scancode, or `Scancode::Unknown`.
     */
    [[nodiscard]] Scancode ScancodeFromKeyNameField(const char name[4]);

    /**
     * @brief Translates a keysym into a layout-dependent CNA key code.
     *
     * `KeyCode` values are Windows virtual-key codes, which name the unshifted identity of a key:
     * there is a `VK_A` but no `VK_a`. The caller passes the keysym at shift level 0 of the active
     * layout and gets back the virtual key for it.
     *
     * @param keysym The keysym.
     * @return The matching key code, or `KeyCode::None` when XKB names no virtual key for it.
     */
    [[nodiscard]] KeyCode KeyCodeFromKeysym(Keysym keysym);

    /**
     * @brief Gets the virtual key a US layout has at a physical position.
     *
     * Covers the character positions -- letters, the number row and the punctuation keys -- which
     * are the only ones whose meaning a layout changes.
     *
     * @param scancode The physical key.
     * @return The key, or `KeyCode::None` for a position that is not a character key.
     */
    [[nodiscard]] KeyCode UsLayoutKeyCode(Scancode scancode);

    /** @brief What one key produces on the active layout, without and with Shift. */
    struct KeySymbols
    {
        /** @brief The keysym at shift level 0, or `kNoSymbol`. */
        Keysym unshifted = kNoSymbol;
        /** @brief The keysym at shift level 1, or `kNoSymbol`. */
        Keysym shifted = kNoSymbol;
    };

    /**
     * @brief Derives every key's virtual key from what the active layout produces.
     *
     * The rules are SDL3's defaults (its `latin_letters` and `french_numbers` keycode options),
     * which are also what Windows virtual keys do:
     *
     * - A layout whose letter keys do not type Latin letters (Cyrillic, Greek, Thai, ...) gets the
     *   US meaning for every character position, so a game bound to W/A/S/D still works.
     * - A layout whose whole number row types symbols unshifted and digits shifted (AZERTY, Czech
     *   QWERTZ) reports the digits: `Keys.D1` is still the key that types 1.
     * - Every other key is what its unshifted keysym names; QWERTZ's Z is `KeyCode::Z`.
     *
     * @param scancodes Each keycode's physical key.
     * @param symbols Each keycode's keysyms on the active layout; the same length as @p scancodes.
     * @return Each keycode's virtual key, as many as @p scancodes has.
     */
    [[nodiscard]] std::vector<KeyCode> BuildKeyCodeTable(std::span<const Scancode> scancodes,
                                                         std::span<const KeySymbols> symbols);

} // namespace CNA::Platform::Xkb
