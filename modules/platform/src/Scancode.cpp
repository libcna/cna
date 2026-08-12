// SPDX-License-Identifier: MS-PL

#include "CNA/Platform/Input/Scancode.hpp"

#include <array>
#include <algorithm>

namespace CNA::Platform {

    namespace {

        /// Name and value in one table, so the two answers below cannot drift apart. A separate
        /// switch for each is the version of this that goes wrong: someone adds a key to one and
        /// not the other, and IsKnownScancode then rejects a key ToString can name.
        struct ScancodeName
        {
            Scancode scancode;
            const char* name;
        };

        constexpr std::array<ScancodeName, 125> kScancodeNames{{
            {Scancode::Unknown, "Unknown"},
            {Scancode::A, "A"},
            {Scancode::B, "B"},
            {Scancode::C, "C"},
            {Scancode::D, "D"},
            {Scancode::E, "E"},
            {Scancode::F, "F"},
            {Scancode::G, "G"},
            {Scancode::H, "H"},
            {Scancode::I, "I"},
            {Scancode::J, "J"},
            {Scancode::K, "K"},
            {Scancode::L, "L"},
            {Scancode::M, "M"},
            {Scancode::N, "N"},
            {Scancode::O, "O"},
            {Scancode::P, "P"},
            {Scancode::Q, "Q"},
            {Scancode::R, "R"},
            {Scancode::S, "S"},
            {Scancode::T, "T"},
            {Scancode::U, "U"},
            {Scancode::V, "V"},
            {Scancode::W, "W"},
            {Scancode::X, "X"},
            {Scancode::Y, "Y"},
            {Scancode::Z, "Z"},
            {Scancode::D1, "D1"},
            {Scancode::D2, "D2"},
            {Scancode::D3, "D3"},
            {Scancode::D4, "D4"},
            {Scancode::D5, "D5"},
            {Scancode::D6, "D6"},
            {Scancode::D7, "D7"},
            {Scancode::D8, "D8"},
            {Scancode::D9, "D9"},
            {Scancode::D0, "D0"},
            {Scancode::Enter, "Enter"},
            {Scancode::Escape, "Escape"},
            {Scancode::Backspace, "Backspace"},
            {Scancode::Tab, "Tab"},
            {Scancode::Space, "Space"},
            {Scancode::Minus, "Minus"},
            {Scancode::Equals, "Equals"},
            {Scancode::LeftBracket, "LeftBracket"},
            {Scancode::RightBracket, "RightBracket"},
            {Scancode::Backslash, "Backslash"},
            {Scancode::NonUsHash, "NonUsHash"},
            {Scancode::Semicolon, "Semicolon"},
            {Scancode::Apostrophe, "Apostrophe"},
            {Scancode::Grave, "Grave"},
            {Scancode::Comma, "Comma"},
            {Scancode::Period, "Period"},
            {Scancode::Slash, "Slash"},
            {Scancode::CapsLock, "CapsLock"},
            {Scancode::F1, "F1"},
            {Scancode::F2, "F2"},
            {Scancode::F3, "F3"},
            {Scancode::F4, "F4"},
            {Scancode::F5, "F5"},
            {Scancode::F6, "F6"},
            {Scancode::F7, "F7"},
            {Scancode::F8, "F8"},
            {Scancode::F9, "F9"},
            {Scancode::F10, "F10"},
            {Scancode::F11, "F11"},
            {Scancode::F12, "F12"},
            {Scancode::PrintScreen, "PrintScreen"},
            {Scancode::ScrollLock, "ScrollLock"},
            {Scancode::Pause, "Pause"},
            {Scancode::Insert, "Insert"},
            {Scancode::Home, "Home"},
            {Scancode::PageUp, "PageUp"},
            {Scancode::Delete, "Delete"},
            {Scancode::End, "End"},
            {Scancode::PageDown, "PageDown"},
            {Scancode::Right, "Right"},
            {Scancode::Left, "Left"},
            {Scancode::Down, "Down"},
            {Scancode::Up, "Up"},
            {Scancode::NumLock, "NumLock"},
            {Scancode::KeypadDivide, "KeypadDivide"},
            {Scancode::KeypadMultiply, "KeypadMultiply"},
            {Scancode::KeypadMinus, "KeypadMinus"},
            {Scancode::KeypadPlus, "KeypadPlus"},
            {Scancode::KeypadEnter, "KeypadEnter"},
            {Scancode::Keypad1, "Keypad1"},
            {Scancode::Keypad2, "Keypad2"},
            {Scancode::Keypad3, "Keypad3"},
            {Scancode::Keypad4, "Keypad4"},
            {Scancode::Keypad5, "Keypad5"},
            {Scancode::Keypad6, "Keypad6"},
            {Scancode::Keypad7, "Keypad7"},
            {Scancode::Keypad8, "Keypad8"},
            {Scancode::Keypad9, "Keypad9"},
            {Scancode::Keypad0, "Keypad0"},
            {Scancode::KeypadPeriod, "KeypadPeriod"},
            {Scancode::NonUsBackslash, "NonUsBackslash"},
            {Scancode::Application, "Application"},
            {Scancode::F13, "F13"},
            {Scancode::F14, "F14"},
            {Scancode::F15, "F15"},
            {Scancode::F16, "F16"},
            {Scancode::F17, "F17"},
            {Scancode::F18, "F18"},
            {Scancode::F19, "F19"},
            {Scancode::F20, "F20"},
            {Scancode::F21, "F21"},
            {Scancode::F22, "F22"},
            {Scancode::F23, "F23"},
            {Scancode::F24, "F24"},
            {Scancode::Menu, "Menu"},
            {Scancode::VolumeUp, "VolumeUp"},
            {Scancode::VolumeDown, "VolumeDown"},
            {Scancode::KeypadClear, "KeypadClear"},
            {Scancode::KeypadDecimal, "KeypadDecimal"},
            {Scancode::LeftControl, "LeftControl"},
            {Scancode::LeftShift, "LeftShift"},
            {Scancode::LeftAlt, "LeftAlt"},
            {Scancode::LeftGui, "LeftGui"},
            {Scancode::RightControl, "RightControl"},
            {Scancode::RightShift, "RightShift"},
            {Scancode::RightAlt, "RightAlt"},
            {Scancode::RightGui, "RightGui"},
            {Scancode::Sleep, "Sleep"},
        }};

    } // namespace

    const std::string& ToString(const Scancode scancode)
    {
        // Built once, indexed by the same table, so a name is stable storage the caller may hold.
        static const std::array<std::string, kScancodeNames.size()> names = [] {
            std::array<std::string, kScancodeNames.size()> built;
            for (std::size_t i = 0; i < kScancodeNames.size(); ++i)
            {
                built[i] = kScancodeNames[i].name;
            }
            return built;
        }();

        for (std::size_t i = 0; i < kScancodeNames.size(); ++i)
        {
            if (kScancodeNames[i].scancode == scancode)
            {
                return names[i];
            }
        }
        return names[0];  // Unknown is the first entry
    }

    bool IsKnownScancode(const std::uint16_t value)
    {
        return std::any_of(kScancodeNames.begin(), kScancodeNames.end(),
                           [value](const ScancodeName& entry) {
                               return static_cast<std::uint16_t>(entry.scancode) == value;
                           });
    }

} // namespace CNA::Platform
