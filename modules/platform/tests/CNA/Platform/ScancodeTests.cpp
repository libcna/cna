// SPDX-License-Identifier: MS-PL
//
// PLAT-78a: the contract's physical-key vocabulary.
//
// The values are USB HID keyboard usage IDs, which means two things worth testing rather than
// assuming. The mapping to any implementation whose scancodes are also HID usage IDs is a cast,
// and a cast is exactly where an unnamed value slips through unnoticed — so the validation that
// makes it safe is what most of this file is about. And because the enum was generated, the
// generator's own failure modes (a duplicated value, a name that does not match its enumerator)
// would be invisible without a check.

#include "CNA/Platform/Input/Scancode.hpp"

#include <gtest/gtest.h>

#include <array>
#include <set>
#include <string>

namespace {

using namespace CNA::Platform;

/// Every key the contract names. Written out rather than derived from the same table ToString
/// uses, so a key silently dropped from that table fails here instead of quietly disappearing.
constexpr Scancode kEveryScancode[] = {
    Scancode::Unknown,        Scancode::A,
    Scancode::B,              Scancode::C,
    Scancode::D,              Scancode::E,
    Scancode::F,              Scancode::G,
    Scancode::H,              Scancode::I,
    Scancode::J,              Scancode::K,
    Scancode::L,              Scancode::M,
    Scancode::N,              Scancode::O,
    Scancode::P,              Scancode::Q,
    Scancode::R,              Scancode::S,
    Scancode::T,              Scancode::U,
    Scancode::V,              Scancode::W,
    Scancode::X,              Scancode::Y,
    Scancode::Z,              Scancode::D1,
    Scancode::D2,             Scancode::D3,
    Scancode::D4,             Scancode::D5,
    Scancode::D6,             Scancode::D7,
    Scancode::D8,             Scancode::D9,
    Scancode::D0,             Scancode::Enter,
    Scancode::Escape,         Scancode::Backspace,
    Scancode::Tab,            Scancode::Space,
    Scancode::Minus,          Scancode::Equals,
    Scancode::LeftBracket,    Scancode::RightBracket,
    Scancode::Backslash,      Scancode::NonUsHash,
    Scancode::Semicolon,      Scancode::Apostrophe,
    Scancode::Grave,          Scancode::Comma,
    Scancode::Period,         Scancode::Slash,
    Scancode::CapsLock,       Scancode::F1,
    Scancode::F2,             Scancode::F3,
    Scancode::F4,             Scancode::F5,
    Scancode::F6,             Scancode::F7,
    Scancode::F8,             Scancode::F9,
    Scancode::F10,            Scancode::F11,
    Scancode::F12,            Scancode::PrintScreen,
    Scancode::ScrollLock,     Scancode::Pause,
    Scancode::Insert,         Scancode::Home,
    Scancode::PageUp,         Scancode::Delete,
    Scancode::End,            Scancode::PageDown,
    Scancode::Right,          Scancode::Left,
    Scancode::Down,           Scancode::Up,
    Scancode::NumLock,        Scancode::KeypadDivide,
    Scancode::KeypadMultiply, Scancode::KeypadMinus,
    Scancode::KeypadPlus,     Scancode::KeypadEnter,
    Scancode::Keypad1,        Scancode::Keypad2,
    Scancode::Keypad3,        Scancode::Keypad4,
    Scancode::Keypad5,        Scancode::Keypad6,
    Scancode::Keypad7,        Scancode::Keypad8,
    Scancode::Keypad9,        Scancode::Keypad0,
    Scancode::KeypadPeriod,   Scancode::NonUsBackslash,
    Scancode::Application,    Scancode::F13,
    Scancode::F14,            Scancode::F15,
    Scancode::F16,            Scancode::F17,
    Scancode::F18,            Scancode::F19,
    Scancode::F20,            Scancode::F21,
    Scancode::F22,            Scancode::F23,
    Scancode::F24,            Scancode::Menu,
    Scancode::VolumeUp,       Scancode::VolumeDown,
    Scancode::KeypadClear,    Scancode::KeypadDecimal,
    Scancode::LeftControl,    Scancode::LeftShift,
    Scancode::LeftAlt,        Scancode::LeftGui,
    Scancode::RightControl,   Scancode::RightShift,
    Scancode::RightAlt,       Scancode::RightGui,
    Scancode::Sleep,
};

TEST(ScancodeTests, EveryNamedKeyIsRecognisedAndNamed)
{
    for (const Scancode scancode : kEveryScancode)
    {
        const auto value = static_cast<std::uint16_t>(scancode);
        EXPECT_TRUE(IsKnownScancode(value)) << "value " << value;
        EXPECT_FALSE(ToString(scancode).empty()) << "value " << value;
    }
}

TEST(ScancodeTests, NoTwoKeysShareAValue)
{
    // The enum was generated from a table. A duplicated value would make one of the two keys
    // permanently unreachable, and nothing else in the build would notice.
    std::set<std::uint16_t> seen;
    for (const Scancode scancode : kEveryScancode)
    {
        const auto value = static_cast<std::uint16_t>(scancode);
        EXPECT_TRUE(seen.insert(value).second) << "value " << value << " is used twice";
    }
    EXPECT_EQ(seen.size(), std::size(kEveryScancode));
}

TEST(ScancodeTests, NoTwoKeysShareAName)
{
    std::set<std::string> seen;
    for (const Scancode scancode : kEveryScancode)
    {
        EXPECT_TRUE(seen.insert(ToString(scancode)).second)
            << "name " << ToString(scancode) << " is used twice";
    }
}

TEST(ScancodeTests, TheValuesAreHidKeyboardUsageIds)
{
    // Spot-checked against the HID Usage Tables, Usage Page 0x07. These are the numbers the
    // keyboard itself puts on the wire, which is the whole reason the contract adopted them
    // rather than inventing a numbering every implementer would have to tabulate.
    EXPECT_EQ(static_cast<std::uint16_t>(Scancode::A), 0x04);
    EXPECT_EQ(static_cast<std::uint16_t>(Scancode::D1), 0x1E);
    EXPECT_EQ(static_cast<std::uint16_t>(Scancode::Enter), 0x28);
    EXPECT_EQ(static_cast<std::uint16_t>(Scancode::Space), 0x2C);
    EXPECT_EQ(static_cast<std::uint16_t>(Scancode::F1), 0x3A);
    EXPECT_EQ(static_cast<std::uint16_t>(Scancode::LeftControl), 0xE0);
    EXPECT_EQ(static_cast<std::uint16_t>(Scancode::RightGui), 0xE7);
}

TEST(ScancodeTests, UnknownIsZeroAndIsItselfARecognisedAnswer)
{
    // Zero is HID's own "no event" usage, so it costs nothing to adopt. It must be recognised
    // rather than rejected: an implementation reporting a key it cannot classify is answering the
    // question, and a caller needs to be able to name what it got.
    EXPECT_EQ(static_cast<std::uint16_t>(Scancode::Unknown), 0u);
    EXPECT_TRUE(IsKnownScancode(0));
    EXPECT_EQ(ToString(Scancode::Unknown), "Unknown");
}

TEST(ScancodeTests, ValuesInTheGapsAreRejected)
{
    // The HID keyboard page is sparse. These sit inside its range but name nothing, and a cast
    // from an implementation's native scancode would produce exactly such a value -- which is why
    // the contract offers a validity check rather than expecting implementations to cast blindly.
    // 1-3 are HID's error codes, 116-117 and 119-127 are keys CNA does not name, 232 is past the
    // modifier block, and 500 is well outside anything. Every one of them is reachable by casting
    // a real SDL scancode, which is the point.
    for (const std::uint16_t value : {std::uint16_t{1}, std::uint16_t{2}, std::uint16_t{3},
                                      std::uint16_t{116}, std::uint16_t{119}, std::uint16_t{127},
                                      std::uint16_t{232}, std::uint16_t{500}})
    {
        EXPECT_FALSE(IsKnownScancode(value)) << "value " << value;
    }
}

TEST(ScancodeTests, AnUnrecognisedValueIsNamedRatherThanLeftBlank)
{
    // ToString is used in diagnostics and key-binding UI, where an empty string is worse than a
    // wrong one: it renders as a blank row the user cannot act on.
    EXPECT_EQ(ToString(static_cast<Scancode>(999)), "Unknown");
}

TEST(ScancodeTests, NamesAreStableStorage)
{
    // The return type is a const reference, so a caller may hold it. Returning a reference to a
    // temporary would be a dangling read that usually appears to work.
    const std::string& first = ToString(Scancode::LeftShift);
    const std::string& second = ToString(Scancode::LeftShift);
    EXPECT_EQ(&first, &second);
    EXPECT_EQ(first, "LeftShift");
}

TEST(ScancodeTests, TheLetterKeysAreContiguousAsHidDefinesThem)
{
    // A through Z are consecutive in the HID page, which is what lets a keyboard-binding UI
    // iterate them. If the generator had reordered or renumbered them, this is what would say so.
    for (int i = 0; i < 26; ++i)
    {
        const auto expected = static_cast<std::uint16_t>(0x04 + i);
        EXPECT_TRUE(IsKnownScancode(expected)) << "letter index " << i;
    }
    EXPECT_EQ(static_cast<std::uint16_t>(Scancode::Z) - static_cast<std::uint16_t>(Scancode::A), 25);
}

} // namespace
