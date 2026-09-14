// SPDX-License-Identifier: MS-PL

#include "Win32Scancodes.hpp"

#include <algorithm>
#include <array>

namespace CNA::Platform::Win32 {

    namespace {

        struct ScancodeMapping
        {
            std::uint16_t packed;
            Scancode scancode;
        };

        /// Set-1 scan code (bit 8 = extended) -> USB HID keyboard usage id.
        ///
        /// Two entries are worth pointing at, because they are the ones an "obvious" table gets
        /// backwards: Pause reports the **non**-extended 0x45 (the E1 prefix of its E1 1D 45
        /// sequence is consumed before lParam is built), while Num Lock reports the extended
        /// 0x145. Print Screen is likewise the extended 0x137, not 0x37 -- 0x37 is the keypad
        /// asterisk.
        constexpr std::array<ScancodeMapping, 121> kScancodes{{
            {0x001, Scancode::Escape},
            {0x002, Scancode::D1},
            {0x003, Scancode::D2},
            {0x004, Scancode::D3},
            {0x005, Scancode::D4},
            {0x006, Scancode::D5},
            {0x007, Scancode::D6},
            {0x008, Scancode::D7},
            {0x009, Scancode::D8},
            {0x00A, Scancode::D9},
            {0x00B, Scancode::D0},
            {0x00C, Scancode::Minus},
            {0x00D, Scancode::Equals},
            {0x00E, Scancode::Backspace},
            {0x00F, Scancode::Tab},
            {0x010, Scancode::Q},
            {0x011, Scancode::W},
            {0x012, Scancode::E},
            {0x013, Scancode::R},
            {0x014, Scancode::T},
            {0x015, Scancode::Y},
            {0x016, Scancode::U},
            {0x017, Scancode::I},
            {0x018, Scancode::O},
            {0x019, Scancode::P},
            {0x01A, Scancode::LeftBracket},
            {0x01B, Scancode::RightBracket},
            {0x01C, Scancode::Enter},
            {0x01D, Scancode::LeftControl},
            {0x01E, Scancode::A},
            {0x01F, Scancode::S},
            {0x020, Scancode::D},
            {0x021, Scancode::F},
            {0x022, Scancode::G},
            {0x023, Scancode::H},
            {0x024, Scancode::J},
            {0x025, Scancode::K},
            {0x026, Scancode::L},
            {0x027, Scancode::Semicolon},
            {0x028, Scancode::Apostrophe},
            {0x029, Scancode::Grave},
            {0x02A, Scancode::LeftShift},
            {0x02B, Scancode::Backslash},
            {0x02C, Scancode::Z},
            {0x02D, Scancode::X},
            {0x02E, Scancode::C},
            {0x02F, Scancode::V},
            {0x030, Scancode::B},
            {0x031, Scancode::N},
            {0x032, Scancode::M},
            {0x033, Scancode::Comma},
            {0x034, Scancode::Period},
            {0x035, Scancode::Slash},
            {0x036, Scancode::RightShift},
            {0x037, Scancode::KeypadMultiply},
            {0x038, Scancode::LeftAlt},
            {0x039, Scancode::Space},
            {0x03A, Scancode::CapsLock},
            {0x03B, Scancode::F1},
            {0x03C, Scancode::F2},
            {0x03D, Scancode::F3},
            {0x03E, Scancode::F4},
            {0x03F, Scancode::F5},
            {0x040, Scancode::F6},
            {0x041, Scancode::F7},
            {0x042, Scancode::F8},
            {0x043, Scancode::F9},
            {0x044, Scancode::F10},
            {0x045, Scancode::Pause},
            {0x046, Scancode::ScrollLock},
            {0x047, Scancode::Keypad7},
            {0x048, Scancode::Keypad8},
            {0x049, Scancode::Keypad9},
            {0x04A, Scancode::KeypadMinus},
            {0x04B, Scancode::Keypad4},
            {0x04C, Scancode::Keypad5},
            {0x04D, Scancode::Keypad6},
            {0x04E, Scancode::KeypadPlus},
            {0x04F, Scancode::Keypad1},
            {0x050, Scancode::Keypad2},
            {0x051, Scancode::Keypad3},
            {0x052, Scancode::Keypad0},
            {0x053, Scancode::KeypadPeriod},
            {0x056, Scancode::NonUsBackslash},
            {0x057, Scancode::F11},
            {0x058, Scancode::F12},
            {0x064, Scancode::F13},
            {0x065, Scancode::F14},
            {0x066, Scancode::F15},
            {0x067, Scancode::F16},
            {0x068, Scancode::F17},
            {0x069, Scancode::F18},
            {0x06A, Scancode::F19},
            {0x06B, Scancode::F20},
            {0x06C, Scancode::F21},
            {0x06D, Scancode::F22},
            {0x06E, Scancode::F23},
            {0x076, Scancode::F24},
            {0x11C, Scancode::KeypadEnter},
            {0x11D, Scancode::RightControl},
            {0x12E, Scancode::VolumeDown},
            {0x130, Scancode::VolumeUp},
            {0x135, Scancode::KeypadDivide},
            {0x137, Scancode::PrintScreen},
            {0x138, Scancode::RightAlt},
            {0x145, Scancode::NumLock},
            {0x146, Scancode::Pause},
            {0x147, Scancode::Home},
            {0x148, Scancode::Up},
            {0x149, Scancode::PageUp},
            {0x14B, Scancode::Left},
            {0x14D, Scancode::Right},
            {0x14F, Scancode::End},
            {0x150, Scancode::Down},
            {0x151, Scancode::PageDown},
            {0x152, Scancode::Insert},
            {0x153, Scancode::Delete},
            {0x15B, Scancode::LeftGui},
            {0x15C, Scancode::RightGui},
            {0x15D, Scancode::Application},
            {0x15F, Scancode::Sleep},
        }};

    } // namespace

    Win32PhysicalKey PhysicalKeyFromLParam(const std::uint64_t lParam)
    {
        Win32PhysicalKey key;
        key.scanCode = static_cast<std::uint8_t>((lParam >> 16) & 0xFFu);
        key.extended = ((lParam >> 24) & 0x1u) != 0;
        return key;
    }

    Scancode ToScancode(const Win32PhysicalKey key)
    {
        const std::uint16_t packed = key.ToPacked();
        const auto found = std::find_if(
            kScancodes.begin(), kScancodes.end(),
            [packed](const ScancodeMapping& mapping) { return mapping.packed == packed; });
        return found != kScancodes.end() ? found->scancode : Scancode::Unknown;
    }

    bool ToWin32PhysicalKey(const Scancode scancode, Win32PhysicalKey& key)
    {
        if (scancode == Scancode::Unknown)
            return false;

        // Pause appears twice in the forward table (0x045 and the Ctrl+Break 0x146). The first
        // match is the one a keyboard actually produces, and find_if returns it.
        const auto found = std::find_if(
            kScancodes.begin(), kScancodes.end(),
            [scancode](const ScancodeMapping& mapping) { return mapping.scancode == scancode; });
        if (found == kScancodes.end())
            return false;

        key.scanCode = static_cast<std::uint8_t>(found->packed & 0xFFu);
        key.extended = (found->packed & 0x100u) != 0;
        return true;
    }

} // namespace CNA::Platform::Win32
