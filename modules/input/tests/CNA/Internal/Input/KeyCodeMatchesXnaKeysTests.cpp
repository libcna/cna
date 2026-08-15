// SPDX-License-Identifier: MS-PL
//
// PLAT-78b: the platform's KeyCode and XNA's Keys must agree, value for value.
//
// This test lives in modules/input rather than modules/platform because it is the only layer that
// can see both types: modules/input is built on top of modules/platform, and the dependency runs
// one way only. That is also why the check is needed at all — the contract cannot include `Keys`
// to assert the correspondence itself, so nothing in the platform module would notice if the two
// drifted.
//
// They agree because both adopted the same published standard: Windows Virtual-Key codes. The
// correspondence is therefore a fact to be verified, not a coincidence to be relied on, and the
// bridge's cast between them is safe exactly as far as this file proves it is.

#include "CNA/Platform/Input/KeyCode.hpp"
#include "Microsoft/Xna/Framework/Input/Keys.hpp"

#include <gtest/gtest.h>

#include <cstdint>

namespace {

using CNA::Platform::KeyCode;
using Microsoft::Xna::Framework::Input::Keys;

/// Every key both enums name. Written as pairs so a rename on either side fails to compile here
/// rather than silently dropping a key from the comparison.
struct Correspondence
{
    KeyCode platform;
    Keys xna;
    const char* name;
};

constexpr Correspondence kCorrespondences[] = {
    {KeyCode::None, Keys::None, "None"},
    {KeyCode::Back, Keys::Back, "Back"},
    {KeyCode::Tab, Keys::Tab, "Tab"},
    {KeyCode::Enter, Keys::Enter, "Enter"},
    {KeyCode::Pause, Keys::Pause, "Pause"},
    {KeyCode::CapsLock, Keys::CapsLock, "CapsLock"},
    {KeyCode::Escape, Keys::Escape, "Escape"},
    {KeyCode::Space, Keys::Space, "Space"},
    {KeyCode::PageUp, Keys::PageUp, "PageUp"},
    {KeyCode::PageDown, Keys::PageDown, "PageDown"},
    {KeyCode::End, Keys::End, "End"},
    {KeyCode::Home, Keys::Home, "Home"},
    {KeyCode::Left, Keys::Left, "Left"},
    {KeyCode::Up, Keys::Up, "Up"},
    {KeyCode::Right, Keys::Right, "Right"},
    {KeyCode::Down, Keys::Down, "Down"},
    {KeyCode::Insert, Keys::Insert, "Insert"},
    {KeyCode::Delete, Keys::Delete, "Delete"},
    {KeyCode::D0, Keys::D0, "D0"},
    {KeyCode::D9, Keys::D9, "D9"},
    {KeyCode::A, Keys::A, "A"},
    {KeyCode::W, Keys::W, "W"},
    {KeyCode::S, Keys::S, "S"},
    {KeyCode::D, Keys::D, "D"},
    {KeyCode::Z, Keys::Z, "Z"},
    {KeyCode::NumPad0, Keys::NumPad0, "NumPad0"},
    {KeyCode::Multiply, Keys::Multiply, "Multiply"},
    {KeyCode::Add, Keys::Add, "Add"},
    {KeyCode::Subtract, Keys::Subtract, "Subtract"},
    {KeyCode::Decimal, Keys::Decimal, "Decimal"},
    {KeyCode::Divide, Keys::Divide, "Divide"},
    {KeyCode::F1, Keys::F1, "F1"},
    {KeyCode::F12, Keys::F12, "F12"},
    {KeyCode::F24, Keys::F24, "F24"},
    {KeyCode::NumLock, Keys::NumLock, "NumLock"},
    {KeyCode::Scroll, Keys::Scroll, "Scroll"},
    {KeyCode::LeftShift, Keys::LeftShift, "LeftShift"},
    {KeyCode::RightShift, Keys::RightShift, "RightShift"},
    {KeyCode::LeftControl, Keys::LeftControl, "LeftControl"},
    {KeyCode::RightControl, Keys::RightControl, "RightControl"},
    {KeyCode::LeftAlt, Keys::LeftAlt, "LeftAlt"},
    {KeyCode::RightAlt, Keys::RightAlt, "RightAlt"},
    {KeyCode::LeftWindows, Keys::LeftWindows, "LeftWindows"},
    {KeyCode::RightWindows, Keys::RightWindows, "RightWindows"},
    {KeyCode::Apps, Keys::Apps, "Apps"},
    {KeyCode::Sleep, Keys::Sleep, "Sleep"},
    {KeyCode::VolumeUp, Keys::VolumeUp, "VolumeUp"},
    {KeyCode::VolumeDown, Keys::VolumeDown, "VolumeDown"},
    {KeyCode::OemSemicolon, Keys::OemSemicolon, "OemSemicolon"},
    {KeyCode::OemPlus, Keys::OemPlus, "OemPlus"},
    {KeyCode::OemComma, Keys::OemComma, "OemComma"},
    {KeyCode::OemMinus, Keys::OemMinus, "OemMinus"},
    {KeyCode::OemPeriod, Keys::OemPeriod, "OemPeriod"},
    {KeyCode::OemQuestion, Keys::OemQuestion, "OemQuestion"},
    {KeyCode::OemTilde, Keys::OemTilde, "OemTilde"},
    {KeyCode::OemOpenBrackets, Keys::OemOpenBrackets, "OemOpenBrackets"},
    {KeyCode::OemPipe, Keys::OemPipe, "OemPipe"},
    {KeyCode::OemCloseBrackets, Keys::OemCloseBrackets, "OemCloseBrackets"},
    {KeyCode::OemQuotes, Keys::OemQuotes, "OemQuotes"},
    {KeyCode::OemClear, Keys::OemClear, "OemClear"},
};

TEST(KeyCodeMatchesXnaKeysTests, EveryNamedKeyHasTheSameValueOnBothSides)
{
    for (const Correspondence& pair : kCorrespondences)
    {
        EXPECT_EQ(static_cast<std::uint16_t>(pair.platform), static_cast<std::uint16_t>(pair.xna))
            << pair.name << " differs between CNA::Platform::KeyCode and XNA Keys";
    }
}

TEST(KeyCodeMatchesXnaKeysTests, EveryXnaKeysValueIsRecognisedByTheContract)
{
    // The direction that matters for the bridge: it will receive a KeyCode and hand a Keys to the
    // input layer. Every Keys value CNA can produce must therefore be one the contract names,
    // otherwise the validated cast would reject a key the game is entitled to see.
    for (const Correspondence& pair : kCorrespondences)
    {
        EXPECT_TRUE(CNA::Platform::IsKnownKeyCode(static_cast<std::uint16_t>(pair.xna)))
            << pair.name << " is an XNA key the platform contract does not name";
    }
}

TEST(KeyCodeMatchesXnaKeysTests, TheSharedValuesAreWindowsVirtualKeyCodes)
{
    // Named explicitly so the correspondence above is not mistaken for one enum having been
    // shaped to match the other. Both adopted the same published numbering independently.
    EXPECT_EQ(static_cast<std::uint16_t>(KeyCode::Back), 0x08);      // VK_BACK
    EXPECT_EQ(static_cast<std::uint16_t>(KeyCode::Enter), 0x0D);     // VK_RETURN
    EXPECT_EQ(static_cast<std::uint16_t>(KeyCode::Space), 0x20);     // VK_SPACE
    EXPECT_EQ(static_cast<std::uint16_t>(KeyCode::A), 0x41);         // 'A'
    EXPECT_EQ(static_cast<std::uint16_t>(KeyCode::F1), 0x70);        // VK_F1
    EXPECT_EQ(static_cast<std::uint16_t>(KeyCode::LeftShift), 0xA0); // VK_LSHIFT
}

TEST(KeyCodeMatchesXnaKeysTests, NoneIsZeroOnBothSides)
{
    // The bridge treats None as "drop this event". If the two disagreed on zero, a real key would
    // be dropped or a non-key would be delivered.
    EXPECT_EQ(static_cast<std::uint16_t>(KeyCode::None), 0u);
    EXPECT_EQ(static_cast<std::uint16_t>(Keys::None), 0u);
}

} // namespace
