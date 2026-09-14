// SPDX-License-Identifier: MS-PL

#include "X11Keyboard.hpp"

#include "X11Display.hpp"

#include <X11/keysym.h>
#include <algorithm>
#include <cctype>
#include <cstring>

namespace CNA::Platform::X11 {

    namespace {

        struct XkbNameScancode
        {
            const char* name;
            Scancode scancode;
        };

        /// XKB key name -> physical scancode.
        ///
        /// The names come from `xkeyboard-config`'s `keycodes/` files and are shared by every
        /// ruleset: `AD01` is the top-row leftmost letter position whether the server numbers it
        /// 24 (evdev) or something else (xfree86). `AE`/`AD`/`AC`/`AB` are the four
        /// alphanumeric rows counted from the top; `FK` the function keys; `KP` the keypad.
        ///
        /// A name absent from this table maps to `Scancode::Unknown`, which is the honest answer
        /// for the vendor-specific `I2xx` keys a particular keyboard invents.
        constexpr XkbNameScancode kXkbNames[] = {
            // Number row. AE13 is the extra key Japanese and some ISO keyboards have.
            {"AE01", Scancode::D1}, {"AE02", Scancode::D2}, {"AE03", Scancode::D3},
            {"AE04", Scancode::D4}, {"AE05", Scancode::D5}, {"AE06", Scancode::D6},
            {"AE07", Scancode::D7}, {"AE08", Scancode::D8}, {"AE09", Scancode::D9},
            {"AE10", Scancode::D0}, {"AE11", Scancode::Minus}, {"AE12", Scancode::Equals},

            // Top letter row: QWERTY positions.
            {"AD01", Scancode::Q}, {"AD02", Scancode::W}, {"AD03", Scancode::E},
            {"AD04", Scancode::R}, {"AD05", Scancode::T}, {"AD06", Scancode::Y},
            {"AD07", Scancode::U}, {"AD08", Scancode::I}, {"AD09", Scancode::O},
            {"AD10", Scancode::P}, {"AD11", Scancode::LeftBracket},
            {"AD12", Scancode::RightBracket},

            // Home row. AC12 is the ISO key beside Enter, which HID calls "Non-US #".
            {"AC01", Scancode::A}, {"AC02", Scancode::S}, {"AC03", Scancode::D},
            {"AC04", Scancode::F}, {"AC05", Scancode::G}, {"AC06", Scancode::H},
            {"AC07", Scancode::J}, {"AC08", Scancode::K}, {"AC09", Scancode::L},
            {"AC10", Scancode::Semicolon}, {"AC11", Scancode::Apostrophe},
            {"AC12", Scancode::NonUsHash},

            // Bottom letter row.
            {"AB01", Scancode::Z}, {"AB02", Scancode::X}, {"AB03", Scancode::C},
            {"AB04", Scancode::V}, {"AB05", Scancode::B}, {"AB06", Scancode::N},
            {"AB07", Scancode::M}, {"AB08", Scancode::Comma}, {"AB09", Scancode::Period},
            {"AB10", Scancode::Slash},

            // Keys with their own names.
            {"TLDE", Scancode::Grave},
            {"BKSL", Scancode::Backslash},
            {"LSGT", Scancode::NonUsBackslash},
            {"SPCE", Scancode::Space},
            {"RTRN", Scancode::Enter},
            {"TAB",  Scancode::Tab},
            {"BKSP", Scancode::Backspace},
            {"ESC",  Scancode::Escape},
            {"CAPS", Scancode::CapsLock},

            // Modifiers. LVL3 is the ISO level-3 shift, which is AltGr on most European layouts
            // and sits where a US keyboard has the right Alt -- so it maps to RightAlt, matching
            // what a game binding "right alt" means by position.
            {"LFSH", Scancode::LeftShift},  {"RTSH", Scancode::RightShift},
            {"LCTL", Scancode::LeftControl}, {"RCTL", Scancode::RightControl},
            {"LALT", Scancode::LeftAlt},    {"RALT", Scancode::RightAlt},
            {"LWIN", Scancode::LeftGui},    {"RWIN", Scancode::RightGui},
            {"LMTA", Scancode::LeftGui},    {"RMTA", Scancode::RightGui},
            {"ALGR", Scancode::RightAlt},   {"LVL3", Scancode::RightAlt},
            {"MENU", Scancode::Application}, {"COMP", Scancode::Application},

            // Function keys.
            {"FK01", Scancode::F1},  {"FK02", Scancode::F2},  {"FK03", Scancode::F3},
            {"FK04", Scancode::F4},  {"FK05", Scancode::F5},  {"FK06", Scancode::F6},
            {"FK07", Scancode::F7},  {"FK08", Scancode::F8},  {"FK09", Scancode::F9},
            {"FK10", Scancode::F10}, {"FK11", Scancode::F11}, {"FK12", Scancode::F12},
            {"FK13", Scancode::F13}, {"FK14", Scancode::F14}, {"FK15", Scancode::F15},
            {"FK16", Scancode::F16}, {"FK17", Scancode::F17}, {"FK18", Scancode::F18},
            {"FK19", Scancode::F19}, {"FK20", Scancode::F20}, {"FK21", Scancode::F21},
            {"FK22", Scancode::F22}, {"FK23", Scancode::F23}, {"FK24", Scancode::F24},

            // Navigation and editing. NEXT is Page Down, which is the one name here that does not
            // read as what it is.
            {"PRSC", Scancode::PrintScreen}, {"SCLK", Scancode::ScrollLock},
            {"PAUS", Scancode::Pause},       {"INS",  Scancode::Insert},
            {"HOME", Scancode::Home},        {"PGUP", Scancode::PageUp},
            {"DELE", Scancode::Delete},      {"END",  Scancode::End},
            {"PGDN", Scancode::PageDown},    {"NEXT", Scancode::PageDown},
            {"PRIO", Scancode::PageUp},
            {"UP",   Scancode::Up},          {"DOWN", Scancode::Down},
            {"LEFT", Scancode::Left},        {"RGHT", Scancode::Right},

            // Keypad.
            {"NMLK", Scancode::NumLock},        {"KPDV", Scancode::KeypadDivide},
            {"KPMU", Scancode::KeypadMultiply}, {"KPSU", Scancode::KeypadMinus},
            {"KPAD", Scancode::KeypadPlus},     {"KPEN", Scancode::KeypadEnter},
            {"KP0",  Scancode::Keypad0}, {"KP1", Scancode::Keypad1},
            {"KP2",  Scancode::Keypad2}, {"KP3", Scancode::Keypad3},
            {"KP4",  Scancode::Keypad4}, {"KP5", Scancode::Keypad5},
            {"KP6",  Scancode::Keypad6}, {"KP7", Scancode::Keypad7},
            {"KP8",  Scancode::Keypad8}, {"KP9", Scancode::Keypad9},
            {"KPDL", Scancode::KeypadPeriod}, {"KPPT", Scancode::KeypadPeriod},

            // Media keys the core keyboard carries.
            {"MUTE", Scancode::Unknown},
            {"VOL-", Scancode::VolumeDown}, {"VOL+", Scancode::VolumeUp},
            {"POWR", Scancode::Sleep},
        };

    } // namespace

    Scancode ScancodeFromXkbKeyName(const char name[4])
    {
        if (name == nullptr)
        {
            return Scancode::Unknown;
        }
        // XKB names are a fixed four-byte field, NUL-padded rather than NUL-terminated, so a
        // strcmp against the table would read past the field for a four-character name. The
        // comparison is length-bounded for exactly that reason.
        for (const XkbNameScancode& entry : kXkbNames)
        {
            const std::size_t length = std::strlen(entry.name);
            if (length > 4)
            {
                continue;
            }
            if (std::strncmp(name, entry.name, length) != 0)
            {
                continue;
            }
            // The remainder of the four-byte field must be padding, or "KP1" would match the
            // name "KP10" if such a key existed.
            bool padded = true;
            for (std::size_t index = length; index < 4; ++index)
            {
                if (name[index] != '\0' && name[index] != ' ')
                {
                    padded = false;
                    break;
                }
            }
            if (padded)
            {
                return entry.scancode;
            }
        }
        return Scancode::Unknown;
    }

    KeyCode KeyCodeFromKeysym(const KeySym keysym)
    {
        // Letters. X gives lower- and upper-case keysyms separate values; virtual keys have only
        // the upper-case identity, which is why both ranges fold onto it.
        if (keysym >= XK_a && keysym <= XK_z)
        {
            return static_cast<KeyCode>(static_cast<std::uint16_t>('A' + (keysym - XK_a)));
        }
        if (keysym >= XK_A && keysym <= XK_Z)
        {
            return static_cast<KeyCode>(static_cast<std::uint16_t>('A' + (keysym - XK_A)));
        }
        if (keysym >= XK_0 && keysym <= XK_9)
        {
            return static_cast<KeyCode>(static_cast<std::uint16_t>('0' + (keysym - XK_0)));
        }

        // Function keys are two ranges on BOTH sides. X's XK_F1..XK_F35 are contiguous, but
        // Windows virtual keys are not: F1..F12 are 0x70..0x7B and F13..F24 restart at 0x7C. A
        // single range computed from F1 would report F13 as 0x7C+11 -- the mistake both the SDL2
        // backend and the terminal keyboard made independently, which is why it is split here.
        if (keysym >= XK_F1 && keysym <= XK_F12)
        {
            return static_cast<KeyCode>(static_cast<std::uint16_t>(112 + (keysym - XK_F1)));
        }
        if (keysym >= XK_F13 && keysym <= XK_F24)
        {
            return static_cast<KeyCode>(static_cast<std::uint16_t>(124 + (keysym - XK_F13)));
        }

        switch (keysym)
        {
            case XK_space: return KeyCode::Space;
            case XK_Return: return KeyCode::Enter;
            case XK_Escape: return KeyCode::Escape;
            case XK_BackSpace: return KeyCode::Back;
            case XK_Tab:
            case XK_ISO_Left_Tab: return KeyCode::Tab;
            case XK_Left: return KeyCode::Left;
            case XK_Right: return KeyCode::Right;
            case XK_Up: return KeyCode::Up;
            case XK_Down: return KeyCode::Down;
            case XK_Home: return KeyCode::Home;
            case XK_End: return KeyCode::End;
            case XK_Prior: return KeyCode::PageUp;
            case XK_Next: return KeyCode::PageDown;
            case XK_Insert: return KeyCode::Insert;
            case XK_Delete: return KeyCode::Delete;
            case XK_Pause:
            case XK_Break: return KeyCode::Pause;
            case XK_Print:
            case XK_Sys_Req: return KeyCode::PrintScreen;
            case XK_Help: return KeyCode::Help;
            case XK_Select: return KeyCode::Select;
            case XK_Execute: return KeyCode::Execute;
            case XK_Caps_Lock: return KeyCode::CapsLock;
            case XK_Num_Lock: return KeyCode::NumLock;
            case XK_Scroll_Lock: return KeyCode::Scroll;

            case XK_Shift_L: return KeyCode::LeftShift;
            case XK_Shift_R: return KeyCode::RightShift;
            case XK_Control_L: return KeyCode::LeftControl;
            case XK_Control_R: return KeyCode::RightControl;
            case XK_Alt_L: return KeyCode::LeftAlt;
            // AltGr is the right-hand Alt position on every layout that has one, and
            // ISO_Level3_Shift is what an ISO layout names that key. Both report RightAlt, which
            // is the virtual key Windows produces for the same physical key.
            case XK_Alt_R:
            case XK_ISO_Level3_Shift:
            case XK_Mode_switch: return KeyCode::RightAlt;
            case XK_Super_L:
            case XK_Meta_L: return KeyCode::LeftWindows;
            case XK_Super_R:
            case XK_Meta_R: return KeyCode::RightWindows;
            case XK_Menu: return KeyCode::Apps;

            // Keypad digits have two keysyms each: the digit when Num Lock is on and the
            // navigation function when it is off. Both map to the numeric virtual key, because
            // that is the key's identity -- XNA's NumPad4 is the same key as the one that moves
            // the caret left.
            case XK_KP_0:
            case XK_KP_Insert: return KeyCode::NumPad0;
            case XK_KP_1:
            case XK_KP_End: return KeyCode::NumPad1;
            case XK_KP_2:
            case XK_KP_Down: return KeyCode::NumPad2;
            case XK_KP_3:
            case XK_KP_Next: return KeyCode::NumPad3;
            case XK_KP_4:
            case XK_KP_Left: return KeyCode::NumPad4;
            case XK_KP_5:
            case XK_KP_Begin: return KeyCode::NumPad5;
            case XK_KP_6:
            case XK_KP_Right: return KeyCode::NumPad6;
            case XK_KP_7:
            case XK_KP_Home: return KeyCode::NumPad7;
            case XK_KP_8:
            case XK_KP_Up: return KeyCode::NumPad8;
            case XK_KP_9:
            case XK_KP_Prior: return KeyCode::NumPad9;
            case XK_KP_Decimal:
            case XK_KP_Delete: return KeyCode::Decimal;
            case XK_KP_Enter: return KeyCode::Enter;
            case XK_KP_Add: return KeyCode::Add;
            case XK_KP_Subtract: return KeyCode::Subtract;
            case XK_KP_Multiply: return KeyCode::Multiply;
            case XK_KP_Divide: return KeyCode::Divide;
            case XK_KP_Separator: return KeyCode::Separator;

            // Punctuation. Windows names these by the key's position on a US layout -- OEM_1 is
            // the semicolon key, OEM_2 the slash key -- so the mapping is from the US character,
            // which is what the group-0 unshifted keysym of that position gives on a US layout.
            case XK_semicolon: return KeyCode::OemSemicolon;
            case XK_equal:
            case XK_plus: return KeyCode::OemPlus;
            case XK_comma: return KeyCode::OemComma;
            case XK_minus: return KeyCode::OemMinus;
            case XK_period: return KeyCode::OemPeriod;
            case XK_slash: return KeyCode::OemQuestion;
            case XK_grave: return KeyCode::OemTilde;
            case XK_bracketleft: return KeyCode::OemOpenBrackets;
            case XK_backslash: return KeyCode::OemPipe;
            case XK_bracketright: return KeyCode::OemCloseBrackets;
            case XK_apostrophe: return KeyCode::OemQuotes;
            case XK_less:
            case XK_greater: return KeyCode::OemBackslash;

            // Input-method keys XNA names.
            case XK_Kana_Lock:
            case XK_Kana_Shift: return KeyCode::Kana;
            case XK_Kanji: return KeyCode::Kanji;
            case XK_Henkan_Mode: return KeyCode::ImeConvert;
            case XK_Muhenkan: return KeyCode::ImeNoConvert;
            case XK_Multi_key: return KeyCode::ProcessKey;

            default: return KeyCode::None;
        }
    }

    KeyCode UsLayoutKeyCode(const Scancode scancode)
    {
        const auto value = static_cast<std::uint16_t>(scancode);
        if (value >= static_cast<std::uint16_t>(Scancode::A) &&
            value <= static_cast<std::uint16_t>(Scancode::Z))
        {
            return static_cast<KeyCode>(static_cast<std::uint16_t>(
                'A' + (value - static_cast<std::uint16_t>(Scancode::A))));
        }
        if (value >= static_cast<std::uint16_t>(Scancode::D1) &&
            value <= static_cast<std::uint16_t>(Scancode::D9))
        {
            return static_cast<KeyCode>(static_cast<std::uint16_t>(
                '1' + (value - static_cast<std::uint16_t>(Scancode::D1))));
        }
        switch (scancode)
        {
            case Scancode::D0: return KeyCode::D0;
            case Scancode::Minus: return KeyCode::OemMinus;
            case Scancode::Equals: return KeyCode::OemPlus;
            case Scancode::LeftBracket: return KeyCode::OemOpenBrackets;
            case Scancode::RightBracket: return KeyCode::OemCloseBrackets;
            case Scancode::Backslash: return KeyCode::OemPipe;
            case Scancode::Semicolon: return KeyCode::OemSemicolon;
            case Scancode::Apostrophe: return KeyCode::OemQuotes;
            case Scancode::Grave: return KeyCode::OemTilde;
            case Scancode::Comma: return KeyCode::OemComma;
            case Scancode::Period: return KeyCode::OemPeriod;
            case Scancode::Slash: return KeyCode::OemQuestion;
            case Scancode::NonUsBackslash: return KeyCode::OemBackslash;
            default: return KeyCode::None;
        }
    }

    std::array<KeyCode, kX11KeycodeCount> BuildKeyCodeTable(
        const std::array<Scancode, kX11KeycodeCount>& scancodes,
        const std::array<X11KeySymbols, kX11KeycodeCount>& symbols)
    {
        const auto keycodeOf = [&scancodes](const Scancode wanted) -> int {
            for (std::size_t keycode = 0; keycode < kX11KeycodeCount; ++keycode)
            {
                if (scancodes[keycode] == wanted)
                {
                    return static_cast<int>(keycode);
                }
            }
            return -1;
        };
        const auto isDigit = [](const KeySym keysym) { return keysym >= XK_0 && keysym <= XK_9; };

        // Latin letters: the first of A..D that has a symbol decides. A Latin-1 keysym has the
        // same value as its code point, so "at most 0xFF" is exactly SDL3's test.
        bool latinLetters = true;
        for (const Scancode letter : {Scancode::A, Scancode::B, Scancode::C, Scancode::D})
        {
            const int keycode = keycodeOf(letter);
            if (keycode < 0 || symbols[static_cast<std::size_t>(keycode)].unshifted == NoSymbol)
            {
                continue;
            }
            latinLetters = symbols[static_cast<std::size_t>(keycode)].unshifted <= 0xFF;
            break;
        }

        // A number row of symbols over digits, on every one of its ten keys.
        bool frenchNumbers = true;
        for (std::uint16_t value = static_cast<std::uint16_t>(Scancode::D1);
             value <= static_cast<std::uint16_t>(Scancode::D0) && frenchNumbers; ++value)
        {
            const int keycode = keycodeOf(static_cast<Scancode>(value));
            frenchNumbers = keycode >= 0 &&
                            !isDigit(symbols[static_cast<std::size_t>(keycode)].unshifted) &&
                            isDigit(symbols[static_cast<std::size_t>(keycode)].shifted);
        }

        std::array<KeyCode, kX11KeycodeCount> table{};
        table.fill(KeyCode::None);
        for (std::size_t keycode = 0; keycode < kX11KeycodeCount; ++keycode)
        {
            const Scancode scancode = scancodes[keycode];
            const X11KeySymbols& symbol = symbols[keycode];
            if (!latinLetters)
            {
                const KeyCode us = UsLayoutKeyCode(scancode);
                if (us != KeyCode::None)
                {
                    table[keycode] = us;
                    continue;
                }
            }
            const auto value = static_cast<std::uint16_t>(scancode);
            if (frenchNumbers && value >= static_cast<std::uint16_t>(Scancode::D1) &&
                value <= static_cast<std::uint16_t>(Scancode::D0))
            {
                table[keycode] = KeyCodeFromKeysym(symbol.shifted);
                continue;
            }
            table[keycode] = KeyCodeFromKeysym(symbol.unshifted);
        }
        return table;
    }

    std::uint16_t ModifiersFromXState(const unsigned int state, const unsigned int modeSwitchMask)
    {
        std::uint16_t result = 0;
        const auto add = [&result](const KeyModifier modifier) {
            result |= static_cast<std::uint16_t>(modifier);
        };
        if ((state & ShiftMask) != 0) { add(KeyModifier::Shift); }
        if ((state & ControlMask) != 0) { add(KeyModifier::Control); }
        if ((state & Mod1Mask) != 0) { add(KeyModifier::Alt); }
        if ((state & Mod4Mask) != 0) { add(KeyModifier::Gui); }
        if ((state & LockMask) != 0) { add(KeyModifier::CapsLock); }
        // Num Lock is conventionally Mod2 and Scroll Lock Mod5 or Mod3, but the assignment is a
        // layout's choice rather than a protocol constant. Mod2 is universal enough to hardcode;
        // AltGr's bit is looked up from the keyboard mapping instead, which is what
        // modeSwitchMask carries.
        if ((state & Mod2Mask) != 0) { add(KeyModifier::NumLock); }
        if (modeSwitchMask != 0 && (state & modeSwitchMask) != 0) { add(KeyModifier::Mode); }
        return result;
    }

    X11Keyboard::X11Keyboard(X11Connection& connection) : connection_(connection)
    {
        scancodes_.fill(Scancode::Unknown);
        keycodes_.fill(KeyCode::None);
        held_.fill(false);
        RefreshKeyboardMapping();
    }

    KeySym X11Keyboard::KeysymAt(const XkbDescPtr description, const unsigned int keycode,
                                 const int level) const
    {
        Display* display = connection_.GetDisplay();
        if (description != nullptr)
        {
            // A key can define fewer groups than the keymap has -- F1 has one, a letter has as
            // many as there are layouts -- and XKB says per key what an out-of-range group means:
            // wrap, clamp or redirect. XkbKeycodeToKeysym does not apply that rule itself.
            int group = group_;
            if (keycode >= description->min_key_code && keycode <= description->max_key_code)
            {
                const int groups = XkbKeyNumGroups(description, keycode);
                const unsigned char info = XkbKeyGroupInfo(description, keycode);
                if (groups > 0 && group >= groups)
                {
                    switch (XkbOutOfRangeGroupAction(info))
                    {
                        case XkbRedirectIntoRange:
                            group = XkbOutOfRangeGroupNumber(info);
                            if (group >= groups) { group = 0; }
                            break;
                        case XkbClampIntoRange:
                            group = groups - 1;
                            break;
                        default:
                            group %= groups;
                            break;
                    }
                }
            }
            const KeySym keysym = XkbKeycodeToKeysym(display, static_cast<::KeyCode>(keycode),
                                                     group, level);
            if (keysym != NoSymbol)
            {
                return keysym;
            }
        }
        // The pre-XKB path, still correct and still needed for a server with no XKB: the core
        // mapping's first two columns are group 1's unshifted and shifted symbols.
        XKeyEvent probe{};
        probe.display = display;
        probe.keycode = keycode;
        probe.state = 0;
        return XLookupKeysym(&probe, level);
    }

    void X11Keyboard::RefreshKeyboardMapping()
    {
        Display* display = connection_.GetDisplay();

        scancodes_.fill(Scancode::Unknown);
        keycodes_.fill(KeyCode::None);

        // --- physical: XKB key names ---------------------------------------------------------
        if (connection_.HasXkb())
        {
            XkbDescPtr description = XkbGetMap(display, 0, XkbUseCoreKbd);
            if (description != nullptr)
            {
                if (XkbGetNames(display, XkbKeyNamesMask, description) == Success &&
                    description->names != nullptr && description->names->keys != nullptr)
                {
                    const int first = std::max<int>(description->min_key_code, kMinKeycode);
                    const int last = std::min<int>(description->max_key_code, kMaxKeycode);
                    for (int keycode = first; keycode <= last; ++keycode)
                    {
                        scancodes_[static_cast<std::size_t>(keycode)] =
                            ScancodeFromXkbKeyName(description->names->keys[keycode].name);
                    }
                }
                XkbFreeKeyboard(description, 0, True);
            }
        }

        // --- logical: what each key means on the ACTIVE layout ----------------------------------
        //
        // The active layout is the effective XKB group. The first version always read group 0,
        // so switching from "us" to "cz" (group 1) rebuilt the same US table: the layout switch
        // this refresh exists for never reached a single key code
        // (plans/plan_native_platform_validation.md NPV-0117). AltGr does not change the group
        // in XKB -- it is a shift *level* -- so following the group cannot make keys move while
        // AltGr is held.
        group_ = 0;
        if (connection_.HasXkb())
        {
            XkbStateRec state{};
            if (XkbGetState(display, XkbUseCoreKbd, &state) == Success)
            {
                group_ = state.group;
            }
        }
        // One request for every key's group layout, rather than one per key.
        XkbDescPtr description =
            connection_.HasXkb() ? XkbGetMap(display, XkbKeySymsMask, XkbUseCoreKbd) : nullptr;
        std::array<X11KeySymbols, kKeycodeCount> symbols{};
        for (int keycode = kMinKeycode; keycode <= kMaxKeycode; ++keycode)
        {
            symbols[static_cast<std::size_t>(keycode)].unshifted =
                KeysymAt(description, static_cast<unsigned int>(keycode), 0);
            symbols[static_cast<std::size_t>(keycode)].shifted =
                KeysymAt(description, static_cast<unsigned int>(keycode), 1);
        }
        keycodes_ = BuildKeyCodeTable(scancodes_, symbols);
        if (description != nullptr)
        {
            XkbFreeKeyboard(description, 0, True);
        }

        // --- which modifier bit is AltGr ------------------------------------------------------
        //
        // Hardcoding Mod5 would be wrong on layouts that put Mode_switch elsewhere, and there is
        // no constant for it: the mapping is data the server owns. Reading it costs one round
        // trip, here rather than per event.
        modeSwitchMask_ = 0;
        XModifierKeymap* modifiers = XGetModifierMapping(display);
        if (modifiers != nullptr)
        {
            for (int modifier = 0; modifier < 8; ++modifier)
            {
                for (int slot = 0; slot < modifiers->max_keypermod; ++slot)
                {
                    const int index = modifier * modifiers->max_keypermod + slot;
                    const unsigned int keycode = modifiers->modifiermap[index];
                    if (keycode == 0)
                    {
                        continue;
                    }
                    const KeySym keysym =
                        XkbKeycodeToKeysym(display, static_cast<::KeyCode>(keycode), 0, 0);
                    if (keysym == XK_Mode_switch || keysym == XK_ISO_Level3_Shift)
                    {
                        modeSwitchMask_ = 1u << modifier;
                    }
                }
            }
            XFreeModifiermap(modifiers);
        }
    }

    void X11Keyboard::Update()
    {
        Display* display = connection_.GetDisplay();

        // XQueryKeymap is the server's own answer to "which keys are physically down right now",
        // which is exactly what a level query needs and what accumulated press/release events
        // cannot give after a missed event. The bit vector is 32 bytes, one bit per keycode.
        char keys[32] = {};
        XQueryKeymap(display, keys);

        snapshot_.pressedKeys.clear();
        held_.fill(false);
        for (int keycode = kMinKeycode; keycode <= kMaxKeycode; ++keycode)
        {
            const bool down =
                (keys[keycode / 8] & (1 << (keycode % 8))) != 0;
            if (!down)
            {
                continue;
            }
            held_[static_cast<std::size_t>(keycode)] = true;
            const KeyCode key = keycodes_[static_cast<std::size_t>(keycode)];
            if (key != KeyCode::None &&
                std::find(snapshot_.pressedKeys.begin(), snapshot_.pressedKeys.end(), key) ==
                    snapshot_.pressedKeys.end())
            {
                snapshot_.pressedKeys.push_back(key);
            }
        }

        unsigned int state = 0;
        if (connection_.HasXkb())
        {
            XkbStateRec xkbState{};
            if (XkbGetState(display, XkbUseCoreKbd, &xkbState) == Success)
            {
                // `mods` is the effective modifier set: held plus latched plus locked, which is
                // what a level query means by "Caps Lock is on".
                state = xkbState.mods | xkbState.locked_mods;
            }
        }
        if (state == 0)
        {
            ::Window root = kNone;
            ::Window child = kNone;
            int rootX = 0;
            int rootY = 0;
            int windowX = 0;
            int windowY = 0;
            unsigned int mask = 0;
            if (XQueryPointer(display, connection_.GetRoot(), &root, &child, &rootX, &rootY,
                              &windowX, &windowY, &mask) == True)
            {
                state = mask;
            }
        }
        snapshot_.modifiers = ModifiersFromXState(state, modeSwitchMask_);
    }

    Scancode X11Keyboard::GetScancode(const unsigned int keycode) const
    {
        if (keycode >= kKeycodeCount)
        {
            return Scancode::Unknown;
        }
        return scancodes_[keycode];
    }

    KeyCode X11Keyboard::GetKeyCode(const unsigned int keycode) const
    {
        if (keycode >= kKeycodeCount)
        {
            return KeyCode::None;
        }
        return keycodes_[keycode];
    }

    KeyCode X11Keyboard::GetKeyFromScancode(const Scancode scancode) const
    {
        if (scancode == Scancode::Unknown)
        {
            return KeyCode::None;
        }
        for (std::size_t keycode = kMinKeycode; keycode < kKeycodeCount; ++keycode)
        {
            if (scancodes_[keycode] == scancode)
            {
                return keycodes_[keycode];
            }
        }
        return KeyCode::None;
    }

    std::string X11Keyboard::GetScancodeName(const Scancode scancode) const
    {
        // The contract's own stable name, not an X one. A scancode name must not change with the
        // layout -- that is the whole distinction between a scancode and a key code -- and X has
        // no layout-independent key label to offer.
        return ToString(scancode);
    }

    Scancode X11Keyboard::GetScancodeFromName(const std::string& name) const
    {
        return ScancodeFromString(name);
    }

    std::string X11Keyboard::GetKeyName(const Scancode scancode) const
    {
        if (scancode == Scancode::Unknown)
        {
            return {};
        }
        Display* display = connection_.GetDisplay();
        for (std::size_t keycode = kMinKeycode; keycode < kKeycodeCount; ++keycode)
        {
            if (scancodes_[keycode] != scancode)
            {
                continue;
            }
            // The label on the ACTIVE layout, as the contract says, so a user who switched to
            // "cz" is shown the key they have now.
            XkbDescPtr description =
                connection_.HasXkb() ? XkbGetMap(display, XkbKeySymsMask, XkbUseCoreKbd) : nullptr;
            const KeySym keysym = KeysymAt(description, static_cast<unsigned int>(keycode), 0);
            if (description != nullptr)
            {
                XkbFreeKeyboard(description, 0, True);
            }
            if (keysym == NoSymbol)
            {
                return {};
            }
            const char* name = XKeysymToString(keysym);
            if (name == nullptr)
            {
                return {};
            }

            // X's keysym names are the protocol's own spelling, not a label: the A key is `a` and
            // the space bar is `space`. This method's contract is the name a key-binding UI shows
            // the user, which is what is printed on the keycap -- `A` and `Space`. The existing
            // cross-implementation suite pins exactly that (KeyboardKeyNameTests asserts `"A"`,
            // `"Z"` and `"Space"`), and it caught this: the first implementation returned X's
            // spelling unchanged and the whole backend disagreed with every other one.
            std::string label(name);
            if (!label.empty())
            {
                label[0] = static_cast<char>(
                    std::toupper(static_cast<unsigned char>(label[0])));
            }
            return label;
        }
        return {};
    }

    KeyCode X11Keyboard::GetKeyFromName(const std::string& name) const
    {
        if (name.empty())
        {
            return KeyCode::None;
        }

        // Tried as given first, because a caller may legitimately pass X's own spelling.
        KeySym keysym = XStringToKeysym(name.c_str());

        // Then as X spells it. GetKeyName capitalises for display, so `Space` must resolve even
        // though X calls that keysym `space` -- otherwise the name this very class produces does
        // not round-trip through it, which is what KeyboardKeyNameTests.NameToKeyReversesKeyToName
        // asserts across every implementation.
        if (keysym == NoSymbol)
        {
            std::string lowered = name;
            lowered[0] = static_cast<char>(std::tolower(static_cast<unsigned char>(lowered[0])));
            keysym = XStringToKeysym(lowered.c_str());
        }
        if (keysym == NoSymbol)
        {
            // A single letter is the remaining case: `A` is the label, `a` is the keysym whose
            // virtual key it names, and both must answer KeyCode::A.
            std::string allLower = name;
            std::transform(allLower.begin(), allLower.end(), allLower.begin(),
                           [](const unsigned char c) { return static_cast<char>(std::tolower(c)); });
            keysym = XStringToKeysym(allLower.c_str());
        }
        if (keysym == NoSymbol)
        {
            return KeyCode::None;
        }
        return KeyCodeFromKeysym(keysym);
    }

    bool X11Keyboard::TrackKeyState(const unsigned int keycode, const bool pressed)
    {
        if (keycode >= kKeycodeCount)
        {
            return false;
        }
        const bool wasHeld = held_[keycode];
        held_[keycode] = pressed;
        // A press of a key that was already held is auto-repeat. With detectable auto-repeat this
        // is the only signal there is, because the server sends no intervening release -- and it
        // is a better signal than a timeout would be, because it is the server's own state rather
        // than a guess about how fast a human types.
        return pressed && wasHeld;
    }

    void X11Keyboard::ReleaseAllKeys()
    {
        held_.fill(false);
        snapshot_.pressedKeys.clear();
    }

    bool X11Keyboard::IsKeyHeld(const unsigned int keycode) const
    {
        return keycode < kKeycodeCount && held_[keycode];
    }

} // namespace CNA::Platform::X11
