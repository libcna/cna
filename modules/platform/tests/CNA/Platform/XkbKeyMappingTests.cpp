// SPDX-License-Identifier: MS-PL
//
// plans/plan_wayland.md WAYLAND-0010: the XKB tables the X11 and Wayland backends share. The X11
// suites (X11ScancodeMapping, X11KeyCodeMapping, X11KeyCodeTable) exercise them through the X11
// backend's own entry points; these exercise the shared entry points directly, in the forms only
// xkbcommon uses -- NUL-terminated key names, and keymaps with keycodes past 255 -- and they run in
// both backends' builds, which is what makes "the same key under X11 and Wayland" a test rather
// than an argument.

#include <gtest/gtest.h>

#include "../../../src/Xkb/XkbKeyMapping.hpp"

#include <string>
#include <vector>

namespace {

using namespace CNA::Platform;
namespace Xkb = CNA::Platform::Xkb;

TEST(XkbKeyMapping, NulTerminatedNamesMapLikeTheFixedFields)
{
    // xkbcommon hands out names as ordinary C strings; X hands out a NUL-padded four-byte field.
    // Every name must mean the same key either way.
    const char* names[] = {"AD01", "AC01", "AB10", "AE10", "TLDE", "LSGT", "SPCE", "RTRN", "TAB",
                           "ESC",  "UP",   "LEFT", "KP0",  "KP9",  "KPDL", "LVL3", "RALT", "FK12",
                           "FK13", "FK24", "NMLK", "MENU", "LWIN", "PRSC", "INS",  "DELE", "END"};
    for (const char* name : names)
    {
        char field[4] = {0, 0, 0, 0};
        for (int index = 0; index < 4 && name[index] != '\0'; ++index)
        {
            field[index] = name[index];
        }
        EXPECT_EQ(Xkb::ScancodeFromKeyName(name), Xkb::ScancodeFromKeyNameField(field)) << name;
        EXPECT_NE(Xkb::ScancodeFromKeyName(name), Scancode::Unknown) << name;
    }
}

TEST(XkbKeyMapping, TheShortNamesAreNotPrefixesOfLongerOnes)
{
    EXPECT_EQ(Xkb::ScancodeFromKeyName("KP1"), Scancode::Keypad1);
    EXPECT_EQ(Xkb::ScancodeFromKeyName("KP10"), Scancode::Unknown);
    EXPECT_EQ(Xkb::ScancodeFromKeyName("UP"), Scancode::Up);
    EXPECT_EQ(Xkb::ScancodeFromKeyName("UPX"), Scancode::Unknown);
}

TEST(XkbKeyMapping, EmptyOverlongAndVendorNamesAreUnknown)
{
    EXPECT_EQ(Xkb::ScancodeFromKeyName(""), Scancode::Unknown);
    // xkbcommon does not limit a name to four characters; the table's names are all at most four,
    // so a longer one is some keymap's own invention.
    EXPECT_EQ(Xkb::ScancodeFromKeyName("AD01X"), Scancode::Unknown);
    EXPECT_EQ(Xkb::ScancodeFromKeyName("I248"), Scancode::Unknown);
    EXPECT_EQ(Xkb::ScancodeFromKeyNameField(nullptr), Scancode::Unknown);
}

TEST(XkbKeyMapping, KeysymsAreTheXProtocolValues)
{
    // The values the shared code states itself; a wrong one would break both backends at once.
    EXPECT_EQ(Xkb::KeyCodeFromKeysym(0x0061), KeyCode::A);         // a
    EXPECT_EQ(Xkb::KeyCodeFromKeysym(0x005a), KeyCode::Z);         // Z
    EXPECT_EQ(Xkb::KeyCodeFromKeysym(0x0035), KeyCode::D5);        // 5
    EXPECT_EQ(Xkb::KeyCodeFromKeysym(0xff0d), KeyCode::Enter);     // Return
    EXPECT_EQ(Xkb::KeyCodeFromKeysym(0xff8d), KeyCode::Enter);     // KP_Enter
    EXPECT_EQ(Xkb::KeyCodeFromKeysym(0xffbe), KeyCode::F1);        // F1
    EXPECT_EQ(Xkb::KeyCodeFromKeysym(0xffca), KeyCode::F13);       // F13, after the virtual-key gap
    EXPECT_EQ(Xkb::KeyCodeFromKeysym(0xfe03), KeyCode::RightAlt);  // ISO_Level3_Shift
    EXPECT_EQ(Xkb::KeyCodeFromKeysym(0xffe1), KeyCode::LeftShift); // Shift_L
    EXPECT_EQ(Xkb::KeyCodeFromKeysym(0xff9b), KeyCode::NumPad3);   // KP_Next, Num Lock off
    EXPECT_EQ(Xkb::KeyCodeFromKeysym(Xkb::kNoSymbol), KeyCode::None);
    // A Czech letter has no virtual key of its own.
    EXPECT_EQ(Xkb::KeyCodeFromKeysym(0x01b9), KeyCode::None);      // scaron
}

TEST(XkbKeyMapping, ATableLongerThanAnXKeymapIsBuiltWhole)
{
    // Wayland keycodes are evdev codes plus 8 and run past 255 (KEY_MAX is 0x2ff). The shared
    // builder takes any length; the keys beyond 255 must be translated, not dropped.
    const std::size_t count = 0x2ff + 9;
    std::vector<Scancode> scancodes(count, Scancode::Unknown);
    std::vector<Xkb::KeySymbols> symbols(count);
    scancodes[38] = Scancode::A;
    symbols[38] = {0x0061, 0x0041};
    scancodes[600] = Scancode::F13;
    symbols[600] = {0xffca, 0xffca};

    const std::vector<KeyCode> table = Xkb::BuildKeyCodeTable(scancodes, symbols);
    ASSERT_EQ(table.size(), count);
    EXPECT_EQ(table[38], KeyCode::A);
    EXPECT_EQ(table[600], KeyCode::F13);
    EXPECT_EQ(table[0], KeyCode::None);
}

TEST(XkbKeyMapping, AShorterSymbolListLeavesTheRestUnmapped)
{
    // Mismatched lengths are a caller's mistake; the builder answers for the keys it can see and
    // never reads past either list.
    const std::vector<Scancode> scancodes = {Scancode::A, Scancode::B, Scancode::C};
    const std::vector<Xkb::KeySymbols> symbols = {{0x0061, 0x0041}};
    const std::vector<KeyCode> table = Xkb::BuildKeyCodeTable(scancodes, symbols);
    ASSERT_EQ(table.size(), 3u);
    EXPECT_EQ(table[0], KeyCode::A);
    EXPECT_EQ(table[1], KeyCode::None);
    EXPECT_EQ(table[2], KeyCode::None);
}

TEST(XkbKeyMapping, CzechQwertyTypesDigitsShiftedAndKeepsItsLetters)
{
    // cz+qwerty, the layout this workstation's owner uses: the number row types ě š č ř ž ý á í é
    // unshifted and the digits shifted, so Keys.D1 must still be the key that types 1.
    std::vector<Scancode> scancodes;
    std::vector<Xkb::KeySymbols> symbols;
    const Xkb::Keysym czechRow[] = {0x0000002b, 0x01ec, 0x01b9, 0x01e8, 0x01f8,
                                    0x01be,     0x00fd, 0x00e1, 0x00ed, 0x00e9};
    for (int digit = 0; digit < 10; ++digit)
    {
        scancodes.push_back(static_cast<Scancode>(static_cast<int>(Scancode::D1) + digit));
        const Xkb::Keysym shifted = digit == 9 ? 0x0030 : static_cast<Xkb::Keysym>(0x0031 + digit);
        symbols.push_back({czechRow[digit], shifted});
    }
    scancodes.push_back(Scancode::Y);
    symbols.push_back({0x0079, 0x0059});
    scancodes.push_back(Scancode::A);
    symbols.push_back({0x0061, 0x0041});

    const std::vector<KeyCode> table = Xkb::BuildKeyCodeTable(scancodes, symbols);
    EXPECT_EQ(table[0], KeyCode::D1);
    EXPECT_EQ(table[8], KeyCode::D9);
    EXPECT_EQ(table[9], KeyCode::D0);
    EXPECT_EQ(table[10], KeyCode::Y) << "QWERTY: Y stays where a US keyboard has it";
    EXPECT_EQ(table[11], KeyCode::A);
}

} // namespace
