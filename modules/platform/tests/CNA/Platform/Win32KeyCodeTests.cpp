// SPDX-License-Identifier: MS-PL
//
// plans/plan_win32.md WIN32-0035: Windows virtual keys -> CNA::Platform::KeyCode.
//
// `KeyCode` reproduces XNA's `Keys`, whose values ARE the Windows VK codes, so the mapping is
// numerically an identity. What it is not is a cast: the VK space is sparse, Windows reports keys
// this contract does not name, and three of the most important VKs -- Shift, Control and Alt --
// arrive without saying which side was pressed even though `KeyCode` distinguishes them.

#include "Win32/Win32KeyCodes.hpp"

#include "Win32/Win32Common.hpp"

#include <gtest/gtest.h>

namespace {

using namespace CNA::Platform;
using namespace CNA::Platform::Win32;

// --- validated identity ------------------------------------------------------------------------

TEST(Win32KeyCode, MapsTheVirtualKeysTheContractNames)
{
    EXPECT_EQ(ToKeyCode(VK_BACK), KeyCode::Back);
    EXPECT_EQ(ToKeyCode(VK_RETURN), KeyCode::Enter);
    EXPECT_EQ(ToKeyCode(VK_ESCAPE), KeyCode::Escape);
    EXPECT_EQ(ToKeyCode(VK_SPACE), KeyCode::Space);
    EXPECT_EQ(ToKeyCode('A'), KeyCode::A);
    EXPECT_EQ(ToKeyCode('Z'), KeyCode::Z);
    EXPECT_EQ(ToKeyCode('0'), KeyCode::D0);
    EXPECT_EQ(ToKeyCode(VK_F1), KeyCode::F1);
    EXPECT_EQ(ToKeyCode(VK_F24), KeyCode::F24);
    EXPECT_EQ(ToKeyCode(VK_NUMPAD0), KeyCode::NumPad0);
    EXPECT_EQ(ToKeyCode(VK_OEM_1), KeyCode::OemSemicolon);
    EXPECT_EQ(ToKeyCode(VK_OEM_102), KeyCode::OemBackslash);
}

TEST(Win32KeyCode, MapsPageKeysThroughTheirHistoricalVirtualKeyNames)
{
    // VK_PRIOR and VK_NEXT are Page Up and Page Down. Reading the names as "previous/next" and
    // mapping them to arrow keys is a real and easy mistake.
    EXPECT_EQ(ToKeyCode(VK_PRIOR), KeyCode::PageUp);
    EXPECT_EQ(ToKeyCode(VK_NEXT), KeyCode::PageDown);
    EXPECT_EQ(ToKeyCode(VK_SNAPSHOT), KeyCode::PrintScreen);
}

TEST(Win32KeyCode, ReportsNoneForVirtualKeysTheContractDoesNotName)
{
    // The mouse buttons occupy VK 1-6 and must never surface as keyboard keys, VK_PACKET is the
    // synthetic key SendInput uses to deliver a Unicode character, and the space above 0xFE is
    // unassigned.
    EXPECT_EQ(ToKeyCode(VK_LBUTTON), KeyCode::None);
    EXPECT_EQ(ToKeyCode(VK_RBUTTON), KeyCode::None);
    EXPECT_EQ(ToKeyCode(VK_MBUTTON), KeyCode::None);
    EXPECT_EQ(ToKeyCode(VK_PACKET), KeyCode::None);
    EXPECT_EQ(ToKeyCode(0), KeyCode::None);
    EXPECT_EQ(ToKeyCode(0x10000), KeyCode::None);
}

TEST(Win32KeyCode, DoesNotNameTheUnsidedModifierVirtualKeys)
{
    // XNA has no plain Shift/Control/Alt key, only sided ones. Reporting VK_SHIFT as a KeyCode
    // would need an enumerator that does not exist, so the unsided VKs must resolve to a side
    // before they are mapped -- which is what the next block tests.
    EXPECT_EQ(ToKeyCode(VK_SHIFT), KeyCode::None);
    EXPECT_EQ(ToKeyCode(VK_CONTROL), KeyCode::None);
    EXPECT_EQ(ToKeyCode(VK_MENU), KeyCode::None);
}

// --- sided resolution --------------------------------------------------------------------------

TEST(Win32KeyCode, ResolvesControlAndAltFromTheExtendedFlag)
{
    EXPECT_EQ(ResolveSidedVirtualKey(VK_CONTROL, Win32PhysicalKey{0x1D, false}),
              static_cast<std::uint32_t>(VK_LCONTROL));
    EXPECT_EQ(ResolveSidedVirtualKey(VK_CONTROL, Win32PhysicalKey{0x1D, true}),
              static_cast<std::uint32_t>(VK_RCONTROL));
    EXPECT_EQ(ResolveSidedVirtualKey(VK_MENU, Win32PhysicalKey{0x38, false}),
              static_cast<std::uint32_t>(VK_LMENU));
    EXPECT_EQ(ResolveSidedVirtualKey(VK_MENU, Win32PhysicalKey{0x38, true}),
              static_cast<std::uint32_t>(VK_RMENU));
}

TEST(Win32KeyCode, ResolvedModifiersMapToTheSidedKeyCodes)
{
    EXPECT_EQ(ToKeyCode(ResolveSidedVirtualKey(VK_CONTROL, Win32PhysicalKey{0x1D, false})),
              KeyCode::LeftControl);
    EXPECT_EQ(ToKeyCode(ResolveSidedVirtualKey(VK_CONTROL, Win32PhysicalKey{0x1D, true})),
              KeyCode::RightControl);
    EXPECT_EQ(ToKeyCode(ResolveSidedVirtualKey(VK_MENU, Win32PhysicalKey{0x38, false})),
              KeyCode::LeftAlt);
    EXPECT_EQ(ToKeyCode(ResolveSidedVirtualKey(VK_MENU, Win32PhysicalKey{0x38, true})),
              KeyCode::RightAlt);
}

TEST(Win32KeyCode, ResolvesShiftThroughTheLayoutBecauseNeitherSideIsExtended)
{
    // MapVirtualKeyW(MAPVK_VSC_TO_VK_EX) is the only mapping that answers with a side here. If
    // the host layout declines, the fallback is Left rather than the unsided VK_SHIFT, because
    // an unsided value has no KeyCode at all and the press would vanish.
    const std::uint32_t left = ResolveSidedVirtualKey(VK_SHIFT, Win32PhysicalKey{0x2A, false});
    const std::uint32_t right = ResolveSidedVirtualKey(VK_SHIFT, Win32PhysicalKey{0x36, false});
    EXPECT_NE(ToKeyCode(left), KeyCode::None);
    EXPECT_NE(ToKeyCode(right), KeyCode::None);
    EXPECT_EQ(left, static_cast<std::uint32_t>(VK_LSHIFT));
    EXPECT_EQ(right, static_cast<std::uint32_t>(VK_RSHIFT));
}

TEST(Win32KeyCode, LeavesAnAlreadySpecificVirtualKeyAlone)
{
    EXPECT_EQ(ResolveSidedVirtualKey('W', Win32PhysicalKey{0x11, false}),
              static_cast<std::uint32_t>('W'));
    EXPECT_EQ(ResolveSidedVirtualKey(VK_LSHIFT, Win32PhysicalKey{0x2A, false}),
              static_cast<std::uint32_t>(VK_LSHIFT));
}

// --- layout queries ----------------------------------------------------------------------------

TEST(Win32KeyCode, ResolvesPhysicalKeysThroughTheActiveLayout)
{
    // The letters are position-dependent and the answer differs per layout, so the assertion is
    // that the layout answered at all -- not that it answered with a particular character.
    EXPECT_NE(KeyCodeFromScancode(Scancode::A), KeyCode::None);
    EXPECT_NE(KeyCodeFromScancode(Scancode::Space), KeyCode::None);
    EXPECT_EQ(KeyCodeFromScancode(Scancode::Space), KeyCode::Space)
        << "the space bar is the same key on every layout";
    EXPECT_EQ(KeyCodeFromScancode(Scancode::Escape), KeyCode::Escape);
    EXPECT_EQ(KeyCodeFromScancode(Scancode::F1), KeyCode::F1);
}

TEST(Win32KeyCode, ReportsNoneForAPhysicalKeyWindowsDoesNotHave)
{
    EXPECT_EQ(KeyCodeFromScancode(Scancode::Unknown), KeyCode::None);
}

TEST(Win32KeyCode, NamesKeysThroughTheLayoutWithoutCrashing)
{
    // GetKeyNameTextW is layout- and locale-dependent, so the durable assertion is that the named
    // keys produce something and an unnameable one produces an empty string rather than garbage.
    EXPECT_TRUE(LayoutKeyName(Scancode::Unknown).empty());
    for (const Scancode scancode : {Scancode::A, Scancode::Escape, Scancode::F1, Scancode::Space})
    {
        const std::string name = LayoutKeyName(scancode);
        EXPECT_FALSE(name.empty()) << ToString(scancode);
    }
}

TEST(Win32KeyCode, ResolvesASingleCharacterNameThroughTheLayout)
{
    // VkKeyScanW answers for a character; a layout without that character reports None rather
    // than an arbitrary key.
    EXPECT_EQ(KeyCodeFromLayoutName("a"), KeyCode::A);
    EXPECT_EQ(KeyCodeFromLayoutName(""), KeyCode::None);
}

} // namespace
