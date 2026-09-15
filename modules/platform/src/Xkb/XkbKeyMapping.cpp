// SPDX-License-Identifier: MS-PL

#include "XkbKeyMapping.hpp"

#include <algorithm>
#include <cstring>

// Only to check the values below at compile time; nothing else is taken from either header.
#if __has_include(<xkbcommon/xkbcommon-keysyms.h>)
#include <xkbcommon/xkbcommon-keysyms.h>
#define CNA_XKB_REFERENCE(name) XKB_KEY_##name
#elif __has_include(<X11/keysym.h>)
#include <X11/keysym.h>
#define CNA_XKB_REFERENCE(name) XK_##name
#endif

#if defined(CNA_XKB_REFERENCE)
#define CNA_XKB_CHECK(name) static_assert(Sym::name == CNA_XKB_REFERENCE(name), #name)
#define CNA_XKB_CHECK_AS(ours, theirs) static_assert(Sym::ours == CNA_XKB_REFERENCE(theirs), #ours)
#else
#define CNA_XKB_CHECK(name) static_assert(true)
#define CNA_XKB_CHECK_AS(ours, theirs) static_assert(true)
#endif

namespace CNA::Platform::Xkb {

    namespace {

        /// The keysyms the tables below name. Their values are fixed by the X protocol's keysym
        /// definitions, which xkbcommon reproduces unchanged; they are stated here so that this
        /// directory needs neither library's header (plans/plan_wayland.md WAYLAND-0010). The
        /// compile-time checks after the list compare every one against whichever header the
        /// build has, so a typo cannot survive.
        namespace Sym {
            constexpr Keysym a = 0x0061, z = 0x007a, A = 0x0041, Z = 0x005a;
            constexpr Keysym d0 = 0x0030, d9 = 0x0039;
            constexpr Keysym F1 = 0xffbe, F12 = 0xffc9, F13 = 0xffca, F24 = 0xffd5;
            constexpr Keysym space = 0x0020, Return = 0xff0d, Escape = 0xff1b, BackSpace = 0xff08;
            constexpr Keysym Tab = 0xff09, ISO_Left_Tab = 0xfe20;
            constexpr Keysym Left = 0xff51, Up = 0xff52, Right = 0xff53, Down = 0xff54;
            constexpr Keysym Home = 0xff50, End = 0xff57, Prior = 0xff55, Next = 0xff56;
            constexpr Keysym Insert = 0xff63, Delete = 0xffff, Pause = 0xff13, Break = 0xff6b;
            constexpr Keysym Print = 0xff61, Sys_Req = 0xff15, Help = 0xff6a, Select = 0xff60;
            constexpr Keysym Execute = 0xff62;
            constexpr Keysym Caps_Lock = 0xffe5, Num_Lock = 0xff7f, Scroll_Lock = 0xff14;
            constexpr Keysym Shift_L = 0xffe1, Shift_R = 0xffe2, Control_L = 0xffe3;
            constexpr Keysym Control_R = 0xffe4, Alt_L = 0xffe9, Alt_R = 0xffea;
            constexpr Keysym ISO_Level3_Shift = 0xfe03, Mode_switch = 0xff7e;
            constexpr Keysym Super_L = 0xffeb, Super_R = 0xffec, Meta_L = 0xffe7, Meta_R = 0xffe8;
            constexpr Keysym Menu = 0xff67;
            constexpr Keysym KP_0 = 0xffb0, KP_1 = 0xffb1, KP_2 = 0xffb2, KP_3 = 0xffb3;
            constexpr Keysym KP_4 = 0xffb4, KP_5 = 0xffb5, KP_6 = 0xffb6, KP_7 = 0xffb7;
            constexpr Keysym KP_8 = 0xffb8, KP_9 = 0xffb9;
            constexpr Keysym KP_Insert = 0xff9e, KP_End = 0xff9c, KP_Down = 0xff99, KP_Next = 0xff9b;
            constexpr Keysym KP_Left = 0xff96, KP_Begin = 0xff9d, KP_Right = 0xff98;
            constexpr Keysym KP_Home = 0xff95, KP_Up = 0xff97, KP_Prior = 0xff9a;
            constexpr Keysym KP_Decimal = 0xffae, KP_Delete = 0xff9f, KP_Enter = 0xff8d;
            constexpr Keysym KP_Add = 0xffab, KP_Subtract = 0xffad, KP_Multiply = 0xffaa;
            constexpr Keysym KP_Divide = 0xffaf, KP_Separator = 0xffac;
            constexpr Keysym semicolon = 0x003b, equal = 0x003d, plus = 0x002b, comma = 0x002c;
            constexpr Keysym minus = 0x002d, period = 0x002e, slash = 0x002f, grave = 0x0060;
            constexpr Keysym bracketleft = 0x005b, backslash = 0x005c, bracketright = 0x005d;
            constexpr Keysym apostrophe = 0x0027, less = 0x003c, greater = 0x003e;
            constexpr Keysym Kana_Lock = 0xff2d, Kana_Shift = 0xff2e, Kanji = 0xff21;
            constexpr Keysym Henkan_Mode = 0xff23, Muhenkan = 0xff22, Multi_key = 0xff20;
        } // namespace Sym

        CNA_XKB_CHECK(a); CNA_XKB_CHECK(z); CNA_XKB_CHECK(A); CNA_XKB_CHECK(Z);
        CNA_XKB_CHECK_AS(d0, 0); CNA_XKB_CHECK_AS(d9, 9);
        CNA_XKB_CHECK(F1); CNA_XKB_CHECK(F12); CNA_XKB_CHECK(F13); CNA_XKB_CHECK(F24);
        CNA_XKB_CHECK(space); CNA_XKB_CHECK(Return); CNA_XKB_CHECK(Escape); CNA_XKB_CHECK(BackSpace);
        CNA_XKB_CHECK(Tab); CNA_XKB_CHECK(ISO_Left_Tab);
        CNA_XKB_CHECK(Left); CNA_XKB_CHECK(Up); CNA_XKB_CHECK(Right); CNA_XKB_CHECK(Down);
        CNA_XKB_CHECK(Home); CNA_XKB_CHECK(End); CNA_XKB_CHECK(Prior); CNA_XKB_CHECK(Next);
        CNA_XKB_CHECK(Insert); CNA_XKB_CHECK(Delete); CNA_XKB_CHECK(Pause); CNA_XKB_CHECK(Break);
        CNA_XKB_CHECK(Print); CNA_XKB_CHECK(Sys_Req); CNA_XKB_CHECK(Help); CNA_XKB_CHECK(Select);
        CNA_XKB_CHECK(Execute);
        CNA_XKB_CHECK(Caps_Lock); CNA_XKB_CHECK(Num_Lock); CNA_XKB_CHECK(Scroll_Lock);
        CNA_XKB_CHECK(Shift_L); CNA_XKB_CHECK(Shift_R); CNA_XKB_CHECK(Control_L);
        CNA_XKB_CHECK(Control_R); CNA_XKB_CHECK(Alt_L); CNA_XKB_CHECK(Alt_R);
        CNA_XKB_CHECK(ISO_Level3_Shift); CNA_XKB_CHECK(Mode_switch);
        CNA_XKB_CHECK(Super_L); CNA_XKB_CHECK(Super_R); CNA_XKB_CHECK(Meta_L); CNA_XKB_CHECK(Meta_R);
        CNA_XKB_CHECK(Menu);
        CNA_XKB_CHECK(KP_0); CNA_XKB_CHECK(KP_1); CNA_XKB_CHECK(KP_2); CNA_XKB_CHECK(KP_3);
        CNA_XKB_CHECK(KP_4); CNA_XKB_CHECK(KP_5); CNA_XKB_CHECK(KP_6); CNA_XKB_CHECK(KP_7);
        CNA_XKB_CHECK(KP_8); CNA_XKB_CHECK(KP_9);
        CNA_XKB_CHECK(KP_Insert); CNA_XKB_CHECK(KP_End); CNA_XKB_CHECK(KP_Down); CNA_XKB_CHECK(KP_Next);
        CNA_XKB_CHECK(KP_Left); CNA_XKB_CHECK(KP_Begin); CNA_XKB_CHECK(KP_Right);
        CNA_XKB_CHECK(KP_Home); CNA_XKB_CHECK(KP_Up); CNA_XKB_CHECK(KP_Prior);
        CNA_XKB_CHECK(KP_Decimal); CNA_XKB_CHECK(KP_Delete); CNA_XKB_CHECK(KP_Enter);
        CNA_XKB_CHECK(KP_Add); CNA_XKB_CHECK(KP_Subtract); CNA_XKB_CHECK(KP_Multiply);
        CNA_XKB_CHECK(KP_Divide); CNA_XKB_CHECK(KP_Separator);
        CNA_XKB_CHECK(semicolon); CNA_XKB_CHECK(equal); CNA_XKB_CHECK(plus); CNA_XKB_CHECK(comma);
        CNA_XKB_CHECK(minus); CNA_XKB_CHECK(period); CNA_XKB_CHECK(slash); CNA_XKB_CHECK(grave);
        CNA_XKB_CHECK(bracketleft); CNA_XKB_CHECK(backslash); CNA_XKB_CHECK(bracketright);
        CNA_XKB_CHECK(apostrophe); CNA_XKB_CHECK(less); CNA_XKB_CHECK(greater);
        CNA_XKB_CHECK(Kana_Lock); CNA_XKB_CHECK(Kana_Shift); CNA_XKB_CHECK(Kanji);
        CNA_XKB_CHECK(Henkan_Mode); CNA_XKB_CHECK(Muhenkan); CNA_XKB_CHECK(Multi_key);

#undef CNA_XKB_CHECK
#undef CNA_XKB_CHECK_AS
#undef CNA_XKB_REFERENCE

        struct NameScancode
        {
            const char* name;
            Scancode scancode;
        };

        /// XKB key name -> physical scancode.
        ///
        /// The names come from `xkeyboard-config`'s `keycodes/` files and are shared by every
        /// ruleset: `AD01` is the top-row leftmost letter position whether the keycode is 24
        /// (evdev) or something else (xfree86). `AE`/`AD`/`AC`/`AB` are the four alphanumeric rows
        /// counted from the top; `FK` the function keys; `KP` the keypad.
        ///
        /// A name absent from this table maps to `Scancode::Unknown`, which is the honest answer
        /// for the vendor-specific `I2xx` keys a particular keyboard invents.
        constexpr NameScancode kNames[] = {
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

    Scancode ScancodeFromKeyName(const std::string_view name)
    {
        if (name.empty() || name.size() > 4)
        {
            return Scancode::Unknown;
        }
        for (const NameScancode& entry : kNames)
        {
            if (name == entry.name)
            {
                return entry.scancode;
            }
        }
        return Scancode::Unknown;
    }

    Scancode ScancodeFromKeyNameField(const char name[4])
    {
        if (name == nullptr)
        {
            return Scancode::Unknown;
        }
        // XKB names are a fixed four-byte field, NUL-padded rather than NUL-terminated, so a
        // strcmp against the table would read past the field for a four-character name. The
        // comparison is length-bounded for exactly that reason.
        for (const NameScancode& entry : kNames)
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

    KeyCode KeyCodeFromKeysym(const Keysym keysym)
    {
        // Letters. XKB gives lower- and upper-case keysyms separate values; virtual keys have only
        // the upper-case identity, which is why both ranges fold onto it.
        if (keysym >= Sym::a && keysym <= Sym::z)
        {
            return static_cast<KeyCode>(static_cast<std::uint16_t>('A' + (keysym - Sym::a)));
        }
        if (keysym >= Sym::A && keysym <= Sym::Z)
        {
            return static_cast<KeyCode>(static_cast<std::uint16_t>('A' + (keysym - Sym::A)));
        }
        if (keysym >= Sym::d0 && keysym <= Sym::d9)
        {
            return static_cast<KeyCode>(static_cast<std::uint16_t>('0' + (keysym - Sym::d0)));
        }

        // Function keys are two ranges on BOTH sides. XKB's F1..F35 are contiguous, but Windows
        // virtual keys are not: F1..F12 are 0x70..0x7B and F13..F24 restart at 0x7C. A single
        // range computed from F1 would report F13 as 0x7C+11 -- the mistake both the SDL2 backend
        // and the terminal keyboard made independently, which is why it is split here.
        if (keysym >= Sym::F1 && keysym <= Sym::F12)
        {
            return static_cast<KeyCode>(static_cast<std::uint16_t>(112 + (keysym - Sym::F1)));
        }
        if (keysym >= Sym::F13 && keysym <= Sym::F24)
        {
            return static_cast<KeyCode>(static_cast<std::uint16_t>(124 + (keysym - Sym::F13)));
        }

        switch (keysym)
        {
            case Sym::space: return KeyCode::Space;
            case Sym::Return: return KeyCode::Enter;
            case Sym::Escape: return KeyCode::Escape;
            case Sym::BackSpace: return KeyCode::Back;
            case Sym::Tab:
            case Sym::ISO_Left_Tab: return KeyCode::Tab;
            case Sym::Left: return KeyCode::Left;
            case Sym::Right: return KeyCode::Right;
            case Sym::Up: return KeyCode::Up;
            case Sym::Down: return KeyCode::Down;
            case Sym::Home: return KeyCode::Home;
            case Sym::End: return KeyCode::End;
            case Sym::Prior: return KeyCode::PageUp;
            case Sym::Next: return KeyCode::PageDown;
            case Sym::Insert: return KeyCode::Insert;
            case Sym::Delete: return KeyCode::Delete;
            case Sym::Pause:
            case Sym::Break: return KeyCode::Pause;
            case Sym::Print:
            case Sym::Sys_Req: return KeyCode::PrintScreen;
            case Sym::Help: return KeyCode::Help;
            case Sym::Select: return KeyCode::Select;
            case Sym::Execute: return KeyCode::Execute;
            case Sym::Caps_Lock: return KeyCode::CapsLock;
            case Sym::Num_Lock: return KeyCode::NumLock;
            case Sym::Scroll_Lock: return KeyCode::Scroll;

            case Sym::Shift_L: return KeyCode::LeftShift;
            case Sym::Shift_R: return KeyCode::RightShift;
            case Sym::Control_L: return KeyCode::LeftControl;
            case Sym::Control_R: return KeyCode::RightControl;
            case Sym::Alt_L: return KeyCode::LeftAlt;
            // AltGr is the right-hand Alt position on every layout that has one, and
            // ISO_Level3_Shift is what an ISO layout names that key. Both report RightAlt, which
            // is the virtual key Windows produces for the same physical key.
            case Sym::Alt_R:
            case Sym::ISO_Level3_Shift:
            case Sym::Mode_switch: return KeyCode::RightAlt;
            case Sym::Super_L:
            case Sym::Meta_L: return KeyCode::LeftWindows;
            case Sym::Super_R:
            case Sym::Meta_R: return KeyCode::RightWindows;
            case Sym::Menu: return KeyCode::Apps;

            // Keypad digits have two keysyms each: the digit when Num Lock is on and the
            // navigation function when it is off. Both map to the numeric virtual key, because
            // that is the key's identity -- XNA's NumPad4 is the same key as the one that moves
            // the caret left.
            case Sym::KP_0:
            case Sym::KP_Insert: return KeyCode::NumPad0;
            case Sym::KP_1:
            case Sym::KP_End: return KeyCode::NumPad1;
            case Sym::KP_2:
            case Sym::KP_Down: return KeyCode::NumPad2;
            case Sym::KP_3:
            case Sym::KP_Next: return KeyCode::NumPad3;
            case Sym::KP_4:
            case Sym::KP_Left: return KeyCode::NumPad4;
            case Sym::KP_5:
            case Sym::KP_Begin: return KeyCode::NumPad5;
            case Sym::KP_6:
            case Sym::KP_Right: return KeyCode::NumPad6;
            case Sym::KP_7:
            case Sym::KP_Home: return KeyCode::NumPad7;
            case Sym::KP_8:
            case Sym::KP_Up: return KeyCode::NumPad8;
            case Sym::KP_9:
            case Sym::KP_Prior: return KeyCode::NumPad9;
            case Sym::KP_Decimal:
            case Sym::KP_Delete: return KeyCode::Decimal;
            case Sym::KP_Enter: return KeyCode::Enter;
            case Sym::KP_Add: return KeyCode::Add;
            case Sym::KP_Subtract: return KeyCode::Subtract;
            case Sym::KP_Multiply: return KeyCode::Multiply;
            case Sym::KP_Divide: return KeyCode::Divide;
            case Sym::KP_Separator: return KeyCode::Separator;

            // Punctuation. Windows names these by the key's position on a US layout -- OEM_1 is
            // the semicolon key, OEM_2 the slash key -- so the mapping is from the US character,
            // which is what the level-0 keysym of that position gives on a US layout.
            case Sym::semicolon: return KeyCode::OemSemicolon;
            case Sym::equal:
            case Sym::plus: return KeyCode::OemPlus;
            case Sym::comma: return KeyCode::OemComma;
            case Sym::minus: return KeyCode::OemMinus;
            case Sym::period: return KeyCode::OemPeriod;
            case Sym::slash: return KeyCode::OemQuestion;
            case Sym::grave: return KeyCode::OemTilde;
            case Sym::bracketleft: return KeyCode::OemOpenBrackets;
            case Sym::backslash: return KeyCode::OemPipe;
            case Sym::bracketright: return KeyCode::OemCloseBrackets;
            case Sym::apostrophe: return KeyCode::OemQuotes;
            case Sym::less:
            case Sym::greater: return KeyCode::OemBackslash;

            // Input-method keys XNA names.
            case Sym::Kana_Lock:
            case Sym::Kana_Shift: return KeyCode::Kana;
            case Sym::Kanji: return KeyCode::Kanji;
            case Sym::Henkan_Mode: return KeyCode::ImeConvert;
            case Sym::Muhenkan: return KeyCode::ImeNoConvert;
            case Sym::Multi_key: return KeyCode::ProcessKey;

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

    std::vector<KeyCode> BuildKeyCodeTable(const std::span<const Scancode> scancodes,
                                           const std::span<const KeySymbols> symbols)
    {
        const std::size_t count = std::min(scancodes.size(), symbols.size());
        const auto keycodeOf = [&scancodes, count](const Scancode wanted) -> std::ptrdiff_t {
            for (std::size_t keycode = 0; keycode < count; ++keycode)
            {
                if (scancodes[keycode] == wanted)
                {
                    return static_cast<std::ptrdiff_t>(keycode);
                }
            }
            return -1;
        };
        const auto isDigit = [](const Keysym keysym) { return keysym >= Sym::d0 && keysym <= Sym::d9; };

        // Latin letters: the first of A..D that has a symbol decides. A Latin-1 keysym has the
        // same value as its code point, so "at most 0xFF" is exactly SDL3's test.
        bool latinLetters = true;
        for (const Scancode letter : {Scancode::A, Scancode::B, Scancode::C, Scancode::D})
        {
            const std::ptrdiff_t keycode = keycodeOf(letter);
            if (keycode < 0 || symbols[static_cast<std::size_t>(keycode)].unshifted == kNoSymbol)
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
            const std::ptrdiff_t keycode = keycodeOf(static_cast<Scancode>(value));
            frenchNumbers = keycode >= 0 &&
                            !isDigit(symbols[static_cast<std::size_t>(keycode)].unshifted) &&
                            isDigit(symbols[static_cast<std::size_t>(keycode)].shifted);
        }

        std::vector<KeyCode> table(scancodes.size(), KeyCode::None);
        for (std::size_t keycode = 0; keycode < count; ++keycode)
        {
            const Scancode scancode = scancodes[keycode];
            const KeySymbols& symbol = symbols[keycode];
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

} // namespace CNA::Platform::Xkb
