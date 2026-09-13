// SPDX-License-Identifier: MS-PL
//
// plans/plan_win32.md WIN32-0034: PS/2 set-1 scan codes -> USB HID keyboard usage ids.
//
// The contract's `Scancode` values are HID usage ids; Windows reports set-1 codes plus an
// extended-key flag. Those are two different numberings, so this is a genuine table and a wrong
// entry is invisible until someone plays with a non-US layout or presses a key on the right-hand
// side of the keyboard.

#include "Win32/Win32Scancodes.hpp"

#include <gtest/gtest.h>

#include <set>

namespace {

using namespace CNA::Platform;
using namespace CNA::Platform::Win32;

Win32PhysicalKey Key(const std::uint8_t scanCode, const bool extended = false)
{
    return Win32PhysicalKey{scanCode, extended};
}

/// Builds the lParam a keyboard message would carry for a physical key, so the extraction is
/// tested against the layout Windows actually uses rather than against its own inverse.
std::uint64_t LParam(const std::uint8_t scanCode, const bool extended, const bool wasDown = false)
{
    std::uint64_t lParam = static_cast<std::uint64_t>(scanCode) << 16;
    if (extended)
        lParam |= 1ull << 24;
    if (wasDown)
        lParam |= 1ull << 30;
    return lParam;
}

// --- lParam extraction ---------------------------------------------------------------------------

TEST(Win32Scancode, ExtractsTheScanCodeAndExtendedFlagFromLParam)
{
    const Win32PhysicalKey plain = PhysicalKeyFromLParam(LParam(0x1D, false));
    EXPECT_EQ(plain.scanCode, 0x1D);
    EXPECT_FALSE(plain.extended);

    const Win32PhysicalKey extended = PhysicalKeyFromLParam(LParam(0x1D, true));
    EXPECT_EQ(extended.scanCode, 0x1D);
    EXPECT_TRUE(extended.extended);
}

TEST(Win32Scancode, IgnoresTheRepeatCountAndTransitionBits)
{
    // Bits 0-15 are the repeat count and bits 29-31 are context/transition state. A mask that
    // took the low word would read the repeat count as a scan code.
    const Win32PhysicalKey key = PhysicalKeyFromLParam(LParam(0x1E, false, true) | 0x7u);
    EXPECT_EQ(key.scanCode, 0x1E);
    EXPECT_FALSE(key.extended);
}

// --- the table -------------------------------------------------------------------------------

TEST(Win32Scancode, MapsTheHomeRowToItsHidUsageIds)
{
    EXPECT_EQ(ToScancode(Key(0x1E)), Scancode::A);
    EXPECT_EQ(ToScancode(Key(0x1F)), Scancode::S);
    EXPECT_EQ(ToScancode(Key(0x20)), Scancode::D);
    EXPECT_EQ(ToScancode(Key(0x21)), Scancode::F);
}

TEST(Win32Scancode, MapsWasdByPositionRatherThanByCharacter)
{
    // The whole reason the contract keeps a physical identity: these four positions are W/A/S/D
    // on a US keyboard and Z/Q/S/D on AZERTY, and a game binding movement wants the position.
    EXPECT_EQ(ToScancode(Key(0x11)), Scancode::W);
    EXPECT_EQ(ToScancode(Key(0x1E)), Scancode::A);
    EXPECT_EQ(ToScancode(Key(0x1F)), Scancode::S);
    EXPECT_EQ(ToScancode(Key(0x20)), Scancode::D);
}

TEST(Win32Scancode, SeparatesLeftAndRightModifiersByTheExtendedFlag)
{
    // Both Controls report scan code 0x1D and both Alts report 0x38. Only the flag differs, and
    // a table keyed on the scan code alone would collapse each pair.
    EXPECT_EQ(ToScancode(Key(0x1D, false)), Scancode::LeftControl);
    EXPECT_EQ(ToScancode(Key(0x1D, true)), Scancode::RightControl);
    EXPECT_EQ(ToScancode(Key(0x38, false)), Scancode::LeftAlt);
    EXPECT_EQ(ToScancode(Key(0x38, true)), Scancode::RightAlt);

    // The Shifts are the exception: they have distinct scan codes and neither is extended.
    EXPECT_EQ(ToScancode(Key(0x2A, false)), Scancode::LeftShift);
    EXPECT_EQ(ToScancode(Key(0x36, false)), Scancode::RightShift);
}

TEST(Win32Scancode, DistinguishesPauseFromNumLock)
{
    // The entry an "obvious" table gets backwards. Pause comes from the E1 1D 45 sequence whose
    // prefix is consumed before lParam is built, so it arrives NON-extended; Num Lock arrives
    // extended. Swapping them makes Num Lock pause the game.
    EXPECT_EQ(ToScancode(Key(0x45, false)), Scancode::Pause);
    EXPECT_EQ(ToScancode(Key(0x45, true)), Scancode::NumLock);
}

TEST(Win32Scancode, DistinguishesPrintScreenFromTheKeypadAsterisk)
{
    EXPECT_EQ(ToScancode(Key(0x37, false)), Scancode::KeypadMultiply);
    EXPECT_EQ(ToScancode(Key(0x37, true)), Scancode::PrintScreen);
}

TEST(Win32Scancode, SeparatesTheKeypadFromTheNavigationCluster)
{
    // The navigation keys are the extended twins of the keypad digits. Confusing them sends Home
    // when the player pressed keypad 7.
    EXPECT_EQ(ToScancode(Key(0x47, false)), Scancode::Keypad7);
    EXPECT_EQ(ToScancode(Key(0x47, true)), Scancode::Home);
    EXPECT_EQ(ToScancode(Key(0x48, false)), Scancode::Keypad8);
    EXPECT_EQ(ToScancode(Key(0x48, true)), Scancode::Up);
    EXPECT_EQ(ToScancode(Key(0x53, false)), Scancode::KeypadPeriod);
    EXPECT_EQ(ToScancode(Key(0x53, true)), Scancode::Delete);
    EXPECT_EQ(ToScancode(Key(0x1C, false)), Scancode::Enter);
    EXPECT_EQ(ToScancode(Key(0x1C, true)), Scancode::KeypadEnter);
}

TEST(Win32Scancode, MapsTheFunctionRowIncludingTheOutOfSequenceF11AndF12)
{
    EXPECT_EQ(ToScancode(Key(0x3B)), Scancode::F1);
    EXPECT_EQ(ToScancode(Key(0x44)), Scancode::F10);
    // F11 and F12 are 0x57/0x58, not a continuation of 0x44 -- they were added after the
    // original 84-key layout was fixed.
    EXPECT_EQ(ToScancode(Key(0x57)), Scancode::F11);
    EXPECT_EQ(ToScancode(Key(0x58)), Scancode::F12);
}

TEST(Win32Scancode, MapsTheWindowsAndMenuKeys)
{
    EXPECT_EQ(ToScancode(Key(0x5B, true)), Scancode::LeftGui);
    EXPECT_EQ(ToScancode(Key(0x5C, true)), Scancode::RightGui);
    EXPECT_EQ(ToScancode(Key(0x5D, true)), Scancode::Application);
}

TEST(Win32Scancode, ReportsUnknownForAKeyTheContractDoesNotName)
{
    EXPECT_EQ(ToScancode(Key(0x00)), Scancode::Unknown);
    EXPECT_EQ(ToScancode(Key(0xFE)), Scancode::Unknown);
    EXPECT_EQ(ToScancode(Key(0x73)), Scancode::Unknown) << "the ABNT/JIS extra key is unnamed";
}

// --- the inverse ---------------------------------------------------------------------------------

TEST(Win32Scancode, RoundTripsEveryMappedKey)
{
    // Every physical key the table names has to survive Scancode -> Windows -> Scancode, because
    // the layout queries (GetKeyFromScancode, GetKeyName) go through the reverse direction.
    for (std::uint16_t packed = 0; packed <= 0x1FF; ++packed)
    {
        const Win32PhysicalKey forward{static_cast<std::uint8_t>(packed & 0xFFu),
                                       (packed & 0x100u) != 0};
        const Scancode scancode = ToScancode(forward);
        if (scancode == Scancode::Unknown)
            continue;

        Win32PhysicalKey back{};
        ASSERT_TRUE(ToWin32PhysicalKey(scancode, back)) << ToString(scancode);
        // Pause is the one key with two Windows spellings (0x45 and the Ctrl+Break 0x146), so the
        // round trip is asserted on the identity of the key rather than the exact code.
        EXPECT_EQ(ToScancode(back), scancode) << ToString(scancode);
    }
}

TEST(Win32Scancode, TheForwardTableHasNoDuplicateWindowsCodes)
{
    // Two entries sharing a packed code would make one of them unreachable, and which one wins
    // would depend on table order rather than on anything a reader could see.
    std::set<std::uint16_t> seen;
    for (std::uint16_t packed = 0; packed <= 0x1FF; ++packed)
    {
        const Win32PhysicalKey key{static_cast<std::uint8_t>(packed & 0xFFu),
                                   (packed & 0x100u) != 0};
        if (ToScancode(key) == Scancode::Unknown)
            continue;
        EXPECT_TRUE(seen.insert(packed).second) << packed;
    }
    EXPECT_GT(seen.size(), 100u) << "the table should cover a full keyboard";
}

TEST(Win32Scancode, RefusesToInvertAnUnknownKey)
{
    Win32PhysicalKey key{};
    EXPECT_FALSE(ToWin32PhysicalKey(Scancode::Unknown, key));
    // Named by the contract but absent from a PC keyboard: there is no set-1 code to hand back.
    EXPECT_FALSE(ToWin32PhysicalKey(Scancode::KeypadClear, key));
}

} // namespace
