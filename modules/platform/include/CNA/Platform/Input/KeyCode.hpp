// SPDX-License-Identifier: MS-PL
#pragma once

#include <cstdint>
#include <string>

namespace CNA::Platform {

    /**
     * @brief A virtual key — what the user typed, as opposed to which key they pressed.
     *
     * ### Why this exists separately from a scancode
     *
     * `Scancode` answers "which physical key"; this answers "what does that key mean on the
     * user's layout". A text field wants this one: on a French keyboard the key where a US
     * keyboard has Q produces A, and a game reading a name into a text box must receive A.
     *
     * ### Where the values come from
     *
     * The numeric values are **Windows Virtual-Key codes**, the same published numbering that
     * `Microsoft::Xna::Framework::Input::Keys` uses — `A` is 65, `F1` is 112, `LeftShift` is 160.
     * Adopting the standard rather than inventing a numbering is the same decision `Scancode`
     * made with HID usage IDs, for the same reason: a contract that renumbers a published
     * standard makes every implementer maintain a translation table for nothing.
     *
     * One consequence is worth stating so it is not mistaken for coupling. CNA's input layer maps
     * this onto XNA's `Keys` with a validated cast, and it can do so because **XNA's enum already
     * uses Virtual-Key values** — not because this enum was shaped to match CNA's input layer.
     * The contract cannot include `Keys`: `modules/input` is built on top of `modules/platform`,
     * so the dependency only runs one way.
     */
    enum class KeyCode : std::uint16_t
    {
        /** @brief The None key. */
        None = 0,
        /** @brief BACKSPACE key. */
        Back = 8,
        /** @brief TAB key. */
        Tab = 9,
        /** @brief ENTER key. */
        Enter = 13,
        /** @brief PAUSE key. */
        Pause = 19,
        /** @brief CAPS LOCK key. */
        CapsLock = 20,
        /** @brief Kana key on Japanese keyboards. */
        Kana = 21,
        /** @brief Kanji key on Japanese keyboards. */
        Kanji = 25,
        /** @brief ESC key. */
        Escape = 27,
        /** @brief IME Convert key. */
        ImeConvert = 28,
        /** @brief IME NoConvert key. */
        ImeNoConvert = 29,
        /** @brief SPACEBAR key. */
        Space = 32,
        /** @brief PAGE UP key. */
        PageUp = 33,
        /** @brief PAGE DOWN key. */
        PageDown = 34,
        /** @brief END key. */
        End = 35,
        /** @brief HOME key. */
        Home = 36,
        /** @brief LEFT ARROW key. */
        Left = 37,
        /** @brief UP ARROW key. */
        Up = 38,
        /** @brief RIGHT ARROW key. */
        Right = 39,
        /** @brief DOWN ARROW key. */
        Down = 40,
        /** @brief SELECT key. */
        Select = 41,
        /** @brief PRINT key. */
        Print = 42,
        /** @brief EXECUTE key. */
        Execute = 43,
        /** @brief PRINT SCREEN key. */
        PrintScreen = 44,
        /** @brief INS key. */
        Insert = 45,
        /** @brief DEL key. */
        Delete = 46,
        /** @brief HELP key. */
        Help = 47,
        /** @brief Digit zero key. */
        D0 = 48,
        /** @brief Digit one key. */
        D1 = 49,
        /** @brief Digit two key. */
        D2 = 50,
        /** @brief Digit three key. */
        D3 = 51,
        /** @brief Digit four key. */
        D4 = 52,
        /** @brief Digit five key. */
        D5 = 53,
        /** @brief Digit six key. */
        D6 = 54,
        /** @brief Digit seven key. */
        D7 = 55,
        /** @brief Digit eight key. */
        D8 = 56,
        /** @brief Digit nine key. */
        D9 = 57,
        /** @brief A key. */
        A = 65,
        /** @brief B key. */
        B = 66,
        /** @brief C key. */
        C = 67,
        /** @brief D key. */
        D = 68,
        /** @brief E key. */
        E = 69,
        /** @brief F key. */
        F = 70,
        /** @brief G key. */
        G = 71,
        /** @brief H key. */
        H = 72,
        /** @brief I key. */
        I = 73,
        /** @brief J key. */
        J = 74,
        /** @brief K key. */
        K = 75,
        /** @brief L key. */
        L = 76,
        /** @brief M key. */
        M = 77,
        /** @brief N key. */
        N = 78,
        /** @brief O key. */
        O = 79,
        /** @brief P key. */
        P = 80,
        /** @brief Q key. */
        Q = 81,
        /** @brief R key. */
        R = 82,
        /** @brief S key. */
        S = 83,
        /** @brief T key. */
        T = 84,
        /** @brief U key. */
        U = 85,
        /** @brief V key. */
        V = 86,
        /** @brief W key. */
        W = 87,
        /** @brief X key. */
        X = 88,
        /** @brief Y key. */
        Y = 89,
        /** @brief Z key. */
        Z = 90,
        /** @brief Left Windows key. */
        LeftWindows = 91,
        /** @brief Right Windows key. */
        RightWindows = 92,
        /** @brief Applications key. */
        Apps = 93,
        /** @brief Computer Sleep key. */
        Sleep = 95,
        /** @brief Numeric keypad 0 key. */
        NumPad0 = 96,
        /** @brief Numeric keypad 1 key. */
        NumPad1 = 97,
        /** @brief Numeric keypad 2 key. */
        NumPad2 = 98,
        /** @brief Numeric keypad 3 key. */
        NumPad3 = 99,
        /** @brief Numeric keypad 4 key. */
        NumPad4 = 100,
        /** @brief Numeric keypad 5 key. */
        NumPad5 = 101,
        /** @brief Numeric keypad 6 key. */
        NumPad6 = 102,
        /** @brief Numeric keypad 7 key. */
        NumPad7 = 103,
        /** @brief Numeric keypad 8 key. */
        NumPad8 = 104,
        /** @brief Numeric keypad 9 key. */
        NumPad9 = 105,
        /** @brief Multiply key. */
        Multiply = 106,
        /** @brief Add key. */
        Add = 107,
        /** @brief Separator key. */
        Separator = 108,
        /** @brief Subtract key. */
        Subtract = 109,
        /** @brief Decimal key. */
        Decimal = 110,
        /** @brief Divide key. */
        Divide = 111,
        /** @brief F1 key. */
        F1 = 112,
        /** @brief F2 key. */
        F2 = 113,
        /** @brief F3 key. */
        F3 = 114,
        /** @brief F4 key. */
        F4 = 115,
        /** @brief F5 key. */
        F5 = 116,
        /** @brief F6 key. */
        F6 = 117,
        /** @brief F7 key. */
        F7 = 118,
        /** @brief F8 key. */
        F8 = 119,
        /** @brief F9 key. */
        F9 = 120,
        /** @brief F10 key. */
        F10 = 121,
        /** @brief F11 key. */
        F11 = 122,
        /** @brief F12 key. */
        F12 = 123,
        /** @brief F13 key. */
        F13 = 124,
        /** @brief F14 key. */
        F14 = 125,
        /** @brief F15 key. */
        F15 = 126,
        /** @brief F16 key. */
        F16 = 127,
        /** @brief F17 key. */
        F17 = 128,
        /** @brief F18 key. */
        F18 = 129,
        /** @brief F19 key. */
        F19 = 130,
        /** @brief F20 key. */
        F20 = 131,
        /** @brief F21 key. */
        F21 = 132,
        /** @brief F22 key. */
        F22 = 133,
        /** @brief F23 key. */
        F23 = 134,
        /** @brief F24 key. */
        F24 = 135,
        /** @brief NUM LOCK key. */
        NumLock = 144,
        /** @brief SCROLL LOCK key. */
        Scroll = 145,
        /** @brief Left SHIFT key. */
        LeftShift = 160,
        /** @brief Right SHIFT key. */
        RightShift = 161,
        /** @brief Left CONTROL key. */
        LeftControl = 162,
        /** @brief Right CONTROL key. */
        RightControl = 163,
        /** @brief Left ALT key. */
        LeftAlt = 164,
        /** @brief Right ALT key. */
        RightAlt = 165,
        /** @brief Browser Back key. */
        BrowserBack = 166,
        /** @brief Browser Forward key. */
        BrowserForward = 167,
        /** @brief Browser Refresh key. */
        BrowserRefresh = 168,
        /** @brief Browser Stop key. */
        BrowserStop = 169,
        /** @brief Browser Search key. */
        BrowserSearch = 170,
        /** @brief Browser Favorites key. */
        BrowserFavorites = 171,
        /** @brief Browser Start and Home key. */
        BrowserHome = 172,
        /** @brief Volume Mute key. */
        VolumeMute = 173,
        /** @brief Volume Down key. */
        VolumeDown = 174,
        /** @brief Volume Up key. */
        VolumeUp = 175,
        /** @brief Next Track key. */
        MediaNextTrack = 176,
        /** @brief Previous Track key. */
        MediaPreviousTrack = 177,
        /** @brief Stop Media key. */
        MediaStop = 178,
        /** @brief Play/Pause Media key. */
        MediaPlayPause = 179,
        /** @brief Start Mail key. */
        LaunchMail = 180,
        /** @brief Select Media key. */
        SelectMedia = 181,
        /** @brief Start Application 1 key. */
        LaunchApplication1 = 182,
        /** @brief Start Application 2 key. */
        LaunchApplication2 = 183,
        /** @brief The OEM Semicolon key on a US standard keyboard. */
        OemSemicolon = 186,
        /** @brief For any country/region, the '+' key. */
        OemPlus = 187,
        /** @brief For any country/region, the ',' key. */
        OemComma = 188,
        /** @brief For any country/region, the '-' key. */
        OemMinus = 189,
        /** @brief For any country/region, the '.' key. */
        OemPeriod = 190,
        /** @brief The OEM question mark key on a US standard keyboard. */
        OemQuestion = 191,
        /** @brief The OEM tilde key on a US standard keyboard. */
        OemTilde = 192,
        /** @brief Green ChatPad key. */
        ChatPadGreen = 202,
        /** @brief Orange ChatPad key. */
        ChatPadOrange = 203,
        /** @brief The OEM open bracket key on a US standard keyboard. */
        OemOpenBrackets = 219,
        /** @brief The OEM pipe key on a US standard keyboard. */
        OemPipe = 220,
        /** @brief The OEM close bracket key on a US standard keyboard. */
        OemCloseBrackets = 221,
        /** @brief The OEM single/double quote key on a US standard keyboard. */
        OemQuotes = 222,
        /** @brief Used for miscellaneous characters; it can vary by keyboard. */
        Oem8 = 223,
        /** @brief The OEM angle bracket or backslash key on the RT 102 key keyboard. */
        OemBackslash = 226,
        /** @brief IME PROCESS key. */
        ProcessKey = 229,
        /** @brief OEM Copy key. */
        OemCopy = 242,
        /** @brief OEM Auto key. */
        OemAuto = 243,
        /** @brief OEM Enlarge Window key. */
        OemEnlW = 244,
        /** @brief Attn key. */
        Attn = 246,
        /** @brief CrSel key. */
        Crsel = 247,
        /** @brief ExSel key. */
        Exsel = 248,
        /** @brief Erase EOF key. */
        EraseEof = 249,
        /** @brief Play key. */
        Play = 250,
        /** @brief Zoom key. */
        Zoom = 251,
        /** @brief PA1 key. */
        Pa1 = 253,
        /** @brief CLEAR key. */
        OemClear = 254
    };

    /**
     * @brief Returns the stable, human-readable name of a virtual key.
     *
     * @param keycode The key to name.
     * @return A stable name such as `"LeftShift"`, or `"None"`.
     */
    [[nodiscard]] const std::string& ToString(KeyCode keycode);

    /**
     * @brief Gets whether a value names a key this contract knows.
     *
     * The Virtual-Key space is sparse, so a raw number arriving from an implementation is
     * validated rather than cast.
     *
     * @param value The raw value to test.
     * @return True if it corresponds to a named key.
     */
    [[nodiscard]] bool IsKnownKeyCode(std::uint16_t value);

} // namespace CNA::Platform
