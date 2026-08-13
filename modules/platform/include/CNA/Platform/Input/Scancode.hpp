// SPDX-License-Identifier: MS-PL
#pragma once

#include <cstdint>
#include <string>

namespace CNA::Platform {

    /**
     * @brief A physical key, identified by position rather than by the character it produces.
     *
     * ### Why this exists separately from a key code
     *
     * `KeyEvent::keycode` answers "what did the user type"; this answers "which key did they
     * press". A game binding WASD wants the second: on an AZERTY keyboard those four keys are
     * still the same four positions under the same four fingers, and the characters they produce
     * are Z, Q, S and D. Reporting only the character would move a French player's movement keys.
     *
     * ### Where the values come from
     *
     * The numeric values are **USB HID keyboard usage IDs** (HID Usage Tables, Usage Page 0x07).
     * That is a published hardware standard, not any library's numbering — it is what the
     * keyboard itself puts on the wire, and every windowing system in use derives its own
     * scancodes from it. Values above the HID keyboard page (`Sleep`) are CNA's own and are
     * documented individually.
     *
     * Choosing the standard rather than inventing a numbering has one consequence worth stating
     * plainly: an implementation whose native scancodes are already HID usage IDs — SDL's are —
     * can translate with a range check rather than a table. That is a convenience, not the
     * reason. The contract would use these values even if no implementation shared them, because
     * a platform contract that renumbers a hardware standard makes every implementer maintain a
     * table for nothing.
     *
     * ### Named keys are US-layout positions
     *
     * `Scancode::A` means "the key in the position where a US keyboard has A", which is exactly
     * what a physical identifier must mean. Every name here is a position, and the descriptions
     * say so where it could be misread.
     */
    enum class Scancode : std::uint16_t
    {
        /** @brief No key, or a key this platform cannot classify. */
        Unknown = 0,
        /** @brief The A key on a US layout. */
        A = 4,
        /** @brief The B key on a US layout. */
        B = 5,
        /** @brief The C key on a US layout. */
        C = 6,
        /** @brief The D key on a US layout. */
        D = 7,
        /** @brief The E key on a US layout. */
        E = 8,
        /** @brief The F key on a US layout. */
        F = 9,
        /** @brief The G key on a US layout. */
        G = 10,
        /** @brief The H key on a US layout. */
        H = 11,
        /** @brief The I key on a US layout. */
        I = 12,
        /** @brief The J key on a US layout. */
        J = 13,
        /** @brief The K key on a US layout. */
        K = 14,
        /** @brief The L key on a US layout. */
        L = 15,
        /** @brief The M key on a US layout. */
        M = 16,
        /** @brief The N key on a US layout. */
        N = 17,
        /** @brief The O key on a US layout. */
        O = 18,
        /** @brief The P key on a US layout. */
        P = 19,
        /** @brief The Q key on a US layout. */
        Q = 20,
        /** @brief The R key on a US layout. */
        R = 21,
        /** @brief The S key on a US layout. */
        S = 22,
        /** @brief The T key on a US layout. */
        T = 23,
        /** @brief The U key on a US layout. */
        U = 24,
        /** @brief The V key on a US layout. */
        V = 25,
        /** @brief The W key on a US layout. */
        W = 26,
        /** @brief The X key on a US layout. */
        X = 27,
        /** @brief The Y key on a US layout. */
        Y = 28,
        /** @brief The Z key on a US layout. */
        Z = 29,
        /** @brief The 1 key on the main number row of a US layout. */
        D1 = 30,
        /** @brief The 2 key on the main number row of a US layout. */
        D2 = 31,
        /** @brief The 3 key on the main number row of a US layout. */
        D3 = 32,
        /** @brief The 4 key on the main number row of a US layout. */
        D4 = 33,
        /** @brief The 5 key on the main number row of a US layout. */
        D5 = 34,
        /** @brief The 6 key on the main number row of a US layout. */
        D6 = 35,
        /** @brief The 7 key on the main number row of a US layout. */
        D7 = 36,
        /** @brief The 8 key on the main number row of a US layout. */
        D8 = 37,
        /** @brief The 9 key on the main number row of a US layout. */
        D9 = 38,
        /** @brief The 0 key on the main number row of a US layout. */
        D0 = 39,
        /** @brief Return / Enter on the main block. */
        Enter = 40,
        /** @brief Escape. */
        Escape = 41,
        /** @brief Backspace. */
        Backspace = 42,
        /** @brief Tab. */
        Tab = 43,
        /** @brief Space bar. */
        Space = 44,
        /** @brief The key left of Equals on a US layout. */
        Minus = 45,
        /** @brief The key left of LeftBracket on a US layout. */
        Equals = 46,
        /** @brief The key right of P on a US layout. */
        LeftBracket = 47,
        /** @brief The key right of LeftBracket on a US layout. */
        RightBracket = 48,
        /** @brief The key above Enter on a US layout. */
        Backslash = 49,
        /** @brief The extra key next to Enter on ISO layouts. */
        NonUsHash = 50,
        /** @brief The key right of L on a US layout. */
        Semicolon = 51,
        /** @brief The key right of Semicolon on a US layout. */
        Apostrophe = 52,
        /** @brief The key left of D1 on a US layout. */
        Grave = 53,
        /** @brief The key right of M on a US layout. */
        Comma = 54,
        /** @brief The key right of Comma on a US layout. */
        Period = 55,
        /** @brief The key right of Period on a US layout. */
        Slash = 56,
        /** @brief Caps Lock. */
        CapsLock = 57,
        /** @brief F1. */
        F1 = 58,
        /** @brief F2. */
        F2 = 59,
        /** @brief F3. */
        F3 = 60,
        /** @brief F4. */
        F4 = 61,
        /** @brief F5. */
        F5 = 62,
        /** @brief F6. */
        F6 = 63,
        /** @brief F7. */
        F7 = 64,
        /** @brief F8. */
        F8 = 65,
        /** @brief F9. */
        F9 = 66,
        /** @brief F10. */
        F10 = 67,
        /** @brief F11. */
        F11 = 68,
        /** @brief F12. */
        F12 = 69,
        /** @brief Print Screen. */
        PrintScreen = 70,
        /** @brief Scroll Lock. */
        ScrollLock = 71,
        /** @brief Pause / Break. */
        Pause = 72,
        /** @brief Insert. */
        Insert = 73,
        /** @brief Home. */
        Home = 74,
        /** @brief Page Up. */
        PageUp = 75,
        /** @brief Forward delete. */
        Delete = 76,
        /** @brief End. */
        End = 77,
        /** @brief Page Down. */
        PageDown = 78,
        /** @brief Right arrow. */
        Right = 79,
        /** @brief Left arrow. */
        Left = 80,
        /** @brief Down arrow. */
        Down = 81,
        /** @brief Up arrow. */
        Up = 82,
        /** @brief Num Lock / Clear. */
        NumLock = 83,
        /** @brief Keypad /. */
        KeypadDivide = 84,
        /** @brief Keypad *. */
        KeypadMultiply = 85,
        /** @brief Keypad -. */
        KeypadMinus = 86,
        /** @brief Keypad +. */
        KeypadPlus = 87,
        /** @brief Keypad Enter. */
        KeypadEnter = 88,
        /** @brief Keypad 1. */
        Keypad1 = 89,
        /** @brief Keypad 2. */
        Keypad2 = 90,
        /** @brief Keypad 3. */
        Keypad3 = 91,
        /** @brief Keypad 4. */
        Keypad4 = 92,
        /** @brief Keypad 5. */
        Keypad5 = 93,
        /** @brief Keypad 6. */
        Keypad6 = 94,
        /** @brief Keypad 7. */
        Keypad7 = 95,
        /** @brief Keypad 8. */
        Keypad8 = 96,
        /** @brief Keypad 9. */
        Keypad9 = 97,
        /** @brief Keypad 0. */
        Keypad0 = 98,
        /** @brief Keypad . / Delete. */
        KeypadPeriod = 99,
        /** @brief The extra key left of D1 on ISO layouts. */
        NonUsBackslash = 100,
        /** @brief The context-menu key. */
        Application = 101,
        /** @brief F13. */
        F13 = 104,
        /** @brief F14. */
        F14 = 105,
        /** @brief F15. */
        F15 = 106,
        /** @brief F16. */
        F16 = 107,
        /** @brief F17. */
        F17 = 108,
        /** @brief F18. */
        F18 = 109,
        /** @brief F19. */
        F19 = 110,
        /** @brief F20. */
        F20 = 111,
        /** @brief F21. */
        F21 = 112,
        /** @brief F22. */
        F22 = 113,
        /** @brief F23. */
        F23 = 114,
        /** @brief F24. */
        F24 = 115,
        /** @brief The Menu key. */
        Menu = 118,
        /** @brief Volume up. */
        VolumeUp = 128,
        /** @brief Volume down. */
        VolumeDown = 129,
        /** @brief Keypad Clear. */
        KeypadClear = 216,
        /** @brief Keypad decimal separator. */
        KeypadDecimal = 220,
        /** @brief Left Control. */
        LeftControl = 224,
        /** @brief Left Shift. */
        LeftShift = 225,
        /** @brief Left Alt. */
        LeftAlt = 226,
        /** @brief Left GUI key (Windows, Command, Super). */
        LeftGui = 227,
        /** @brief Right Control. */
        RightControl = 228,
        /** @brief Right Shift. */
        RightShift = 229,
        /** @brief Right Alt / AltGr. */
        RightAlt = 230,
        /** @brief Right GUI key (Windows, Command, Super). */
        RightGui = 231,
        /** @brief System sleep. */
        Sleep = 258,
    };

    /**
     * @brief Returns the stable, human-readable name of a physical key.
     *
     * Used in diagnostics and key-binding UI. The name is CNA's own and does not change with the
     * user's keyboard layout — it names the position, matching the enumerator.
     *
     * @param scancode The key to name.
     * @return A stable name such as `"LeftShift"`, or `"Unknown"`.
     */
    [[nodiscard]] const std::string& ToString(Scancode scancode);

    /**
     * @brief Resolves a stable physical-key name.
     * @param name A name returned by `ToString`.
     * @return The key, or `Scancode::Unknown` when the name is not known.
     */
    [[nodiscard]] Scancode ScancodeFromString(const std::string& name);

    /**
     * @brief Gets whether a value names a key this contract knows.
     *
     * The value space is sparse — the HID keyboard page has gaps, and a platform may report a key
     * outside it — so a raw number arriving from an implementation is validated rather than cast.
     *
     * @param value The raw value to test.
     * @return True if it corresponds to a named key.
     */
    [[nodiscard]] bool IsKnownScancode(std::uint16_t value);

} // namespace CNA::Platform
