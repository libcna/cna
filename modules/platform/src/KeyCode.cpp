// SPDX-License-Identifier: MS-PL

#include "CNA/Platform/Input/KeyCode.hpp"

#include <algorithm>
#include <array>

namespace CNA::Platform {

    namespace {

        /// Name and value in one table, for the same reason Scancode.cpp keeps one: a separate
        /// switch per answer is where someone adds a key to one and not the other.
        struct KeyCodeName
        {
            KeyCode keycode;
            const char* name;
        };

        constexpr std::array<KeyCodeName, 160> kKeyCodeNames{{
            {KeyCode::None, "None"},
            {KeyCode::Back, "Back"},
            {KeyCode::Tab, "Tab"},
            {KeyCode::Enter, "Enter"},
            {KeyCode::Pause, "Pause"},
            {KeyCode::CapsLock, "CapsLock"},
            {KeyCode::Kana, "Kana"},
            {KeyCode::Kanji, "Kanji"},
            {KeyCode::Escape, "Escape"},
            {KeyCode::ImeConvert, "ImeConvert"},
            {KeyCode::ImeNoConvert, "ImeNoConvert"},
            {KeyCode::Space, "Space"},
            {KeyCode::PageUp, "PageUp"},
            {KeyCode::PageDown, "PageDown"},
            {KeyCode::End, "End"},
            {KeyCode::Home, "Home"},
            {KeyCode::Left, "Left"},
            {KeyCode::Up, "Up"},
            {KeyCode::Right, "Right"},
            {KeyCode::Down, "Down"},
            {KeyCode::Select, "Select"},
            {KeyCode::Print, "Print"},
            {KeyCode::Execute, "Execute"},
            {KeyCode::PrintScreen, "PrintScreen"},
            {KeyCode::Insert, "Insert"},
            {KeyCode::Delete, "Delete"},
            {KeyCode::Help, "Help"},
            {KeyCode::D0, "D0"},
            {KeyCode::D1, "D1"},
            {KeyCode::D2, "D2"},
            {KeyCode::D3, "D3"},
            {KeyCode::D4, "D4"},
            {KeyCode::D5, "D5"},
            {KeyCode::D6, "D6"},
            {KeyCode::D7, "D7"},
            {KeyCode::D8, "D8"},
            {KeyCode::D9, "D9"},
            {KeyCode::A, "A"},
            {KeyCode::B, "B"},
            {KeyCode::C, "C"},
            {KeyCode::D, "D"},
            {KeyCode::E, "E"},
            {KeyCode::F, "F"},
            {KeyCode::G, "G"},
            {KeyCode::H, "H"},
            {KeyCode::I, "I"},
            {KeyCode::J, "J"},
            {KeyCode::K, "K"},
            {KeyCode::L, "L"},
            {KeyCode::M, "M"},
            {KeyCode::N, "N"},
            {KeyCode::O, "O"},
            {KeyCode::P, "P"},
            {KeyCode::Q, "Q"},
            {KeyCode::R, "R"},
            {KeyCode::S, "S"},
            {KeyCode::T, "T"},
            {KeyCode::U, "U"},
            {KeyCode::V, "V"},
            {KeyCode::W, "W"},
            {KeyCode::X, "X"},
            {KeyCode::Y, "Y"},
            {KeyCode::Z, "Z"},
            {KeyCode::LeftWindows, "LeftWindows"},
            {KeyCode::RightWindows, "RightWindows"},
            {KeyCode::Apps, "Apps"},
            {KeyCode::Sleep, "Sleep"},
            {KeyCode::NumPad0, "NumPad0"},
            {KeyCode::NumPad1, "NumPad1"},
            {KeyCode::NumPad2, "NumPad2"},
            {KeyCode::NumPad3, "NumPad3"},
            {KeyCode::NumPad4, "NumPad4"},
            {KeyCode::NumPad5, "NumPad5"},
            {KeyCode::NumPad6, "NumPad6"},
            {KeyCode::NumPad7, "NumPad7"},
            {KeyCode::NumPad8, "NumPad8"},
            {KeyCode::NumPad9, "NumPad9"},
            {KeyCode::Multiply, "Multiply"},
            {KeyCode::Add, "Add"},
            {KeyCode::Separator, "Separator"},
            {KeyCode::Subtract, "Subtract"},
            {KeyCode::Decimal, "Decimal"},
            {KeyCode::Divide, "Divide"},
            {KeyCode::F1, "F1"},
            {KeyCode::F2, "F2"},
            {KeyCode::F3, "F3"},
            {KeyCode::F4, "F4"},
            {KeyCode::F5, "F5"},
            {KeyCode::F6, "F6"},
            {KeyCode::F7, "F7"},
            {KeyCode::F8, "F8"},
            {KeyCode::F9, "F9"},
            {KeyCode::F10, "F10"},
            {KeyCode::F11, "F11"},
            {KeyCode::F12, "F12"},
            {KeyCode::F13, "F13"},
            {KeyCode::F14, "F14"},
            {KeyCode::F15, "F15"},
            {KeyCode::F16, "F16"},
            {KeyCode::F17, "F17"},
            {KeyCode::F18, "F18"},
            {KeyCode::F19, "F19"},
            {KeyCode::F20, "F20"},
            {KeyCode::F21, "F21"},
            {KeyCode::F22, "F22"},
            {KeyCode::F23, "F23"},
            {KeyCode::F24, "F24"},
            {KeyCode::NumLock, "NumLock"},
            {KeyCode::Scroll, "Scroll"},
            {KeyCode::LeftShift, "LeftShift"},
            {KeyCode::RightShift, "RightShift"},
            {KeyCode::LeftControl, "LeftControl"},
            {KeyCode::RightControl, "RightControl"},
            {KeyCode::LeftAlt, "LeftAlt"},
            {KeyCode::RightAlt, "RightAlt"},
            {KeyCode::BrowserBack, "BrowserBack"},
            {KeyCode::BrowserForward, "BrowserForward"},
            {KeyCode::BrowserRefresh, "BrowserRefresh"},
            {KeyCode::BrowserStop, "BrowserStop"},
            {KeyCode::BrowserSearch, "BrowserSearch"},
            {KeyCode::BrowserFavorites, "BrowserFavorites"},
            {KeyCode::BrowserHome, "BrowserHome"},
            {KeyCode::VolumeMute, "VolumeMute"},
            {KeyCode::VolumeDown, "VolumeDown"},
            {KeyCode::VolumeUp, "VolumeUp"},
            {KeyCode::MediaNextTrack, "MediaNextTrack"},
            {KeyCode::MediaPreviousTrack, "MediaPreviousTrack"},
            {KeyCode::MediaStop, "MediaStop"},
            {KeyCode::MediaPlayPause, "MediaPlayPause"},
            {KeyCode::LaunchMail, "LaunchMail"},
            {KeyCode::SelectMedia, "SelectMedia"},
            {KeyCode::LaunchApplication1, "LaunchApplication1"},
            {KeyCode::LaunchApplication2, "LaunchApplication2"},
            {KeyCode::OemSemicolon, "OemSemicolon"},
            {KeyCode::OemPlus, "OemPlus"},
            {KeyCode::OemComma, "OemComma"},
            {KeyCode::OemMinus, "OemMinus"},
            {KeyCode::OemPeriod, "OemPeriod"},
            {KeyCode::OemQuestion, "OemQuestion"},
            {KeyCode::OemTilde, "OemTilde"},
            {KeyCode::ChatPadGreen, "ChatPadGreen"},
            {KeyCode::ChatPadOrange, "ChatPadOrange"},
            {KeyCode::OemOpenBrackets, "OemOpenBrackets"},
            {KeyCode::OemPipe, "OemPipe"},
            {KeyCode::OemCloseBrackets, "OemCloseBrackets"},
            {KeyCode::OemQuotes, "OemQuotes"},
            {KeyCode::Oem8, "Oem8"},
            {KeyCode::OemBackslash, "OemBackslash"},
            {KeyCode::ProcessKey, "ProcessKey"},
            {KeyCode::OemCopy, "OemCopy"},
            {KeyCode::OemAuto, "OemAuto"},
            {KeyCode::OemEnlW, "OemEnlW"},
            {KeyCode::Attn, "Attn"},
            {KeyCode::Crsel, "Crsel"},
            {KeyCode::Exsel, "Exsel"},
            {KeyCode::EraseEof, "EraseEof"},
            {KeyCode::Play, "Play"},
            {KeyCode::Zoom, "Zoom"},
            {KeyCode::Pa1, "Pa1"},
            {KeyCode::OemClear, "OemClear"},
        }};

    } // namespace

    const std::string& ToString(const KeyCode keycode)
    {
        static const std::array<std::string, kKeyCodeNames.size()> names = [] {
            std::array<std::string, kKeyCodeNames.size()> built;
            for (std::size_t i = 0; i < kKeyCodeNames.size(); ++i)
            {
                built[i] = kKeyCodeNames[i].name;
            }
            return built;
        }();

        for (std::size_t i = 0; i < kKeyCodeNames.size(); ++i)
        {
            if (kKeyCodeNames[i].keycode == keycode)
            {
                return names[i];
            }
        }
        return names[0];  // None is the first entry
    }

    bool IsKnownKeyCode(const std::uint16_t value)
    {
        return std::any_of(kKeyCodeNames.begin(), kKeyCodeNames.end(),
                           [value](const KeyCodeName& entry) {
                               return static_cast<std::uint16_t>(entry.keycode) == value;
                           });
    }

} // namespace CNA::Platform
