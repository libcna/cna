// SPDX-License-Identifier: MS-PL
//
// plans/plan_x11.md X11-0030/X11-0031/X11-0032/X11-0051: the X11 backend's pure translation
// tables, tested with no X server at all.
//
// These are the functions where a mistake is silent: a wrong scancode moves a player's movement
// keys, a wrong virtual key types the wrong character into a text field, and a wheel button
// mapped as an ordinary button makes every scroll look like a click. None of that produces an
// error anywhere -- it produces a game that feels broken. So the tables are tested directly,
// which also means they are covered on a machine with no display.

// GoogleTest comes FIRST, deliberately. X11Headers.hpp undefines Xlib's `Bool` and `Status`
// macros precisely so this order is not load-bearing, but a header that parses before any X macro
// exists cannot be affected by one that is added later either -- and this file is the only place
// in CNA where a C++ library header and an X header meet.
#include <gtest/gtest.h>

#include "../../../src/X11/X11EventMapper.hpp"
#include "../../../src/X11/X11Keyboard.hpp"

#include <X11/keysym.h>

namespace {

using namespace CNA::Platform;
using namespace CNA::Platform::X11;

// `<X11/X.h>` declares `typedef unsigned char KeyCode` at global scope, and the using-directive
// above makes `CNA::Platform::KeyCode` visible there too -- so the bare name is ambiguous in this
// one translation unit. A typedef cannot be undefined the way `None` and `Bool` can, so the
// collision is resolved by a declaration in this nested namespace, which hides both.
using KeyCode = CNA::Platform::KeyCode;
using Scancode = CNA::Platform::Scancode;

// --- physical keys: XKB key names -> Scancode ---------------------------------------------------

TEST(X11ScancodeMapping, LetterRowsMapToTheirUsLayoutPositions)
{
    // AD01 is the top-row leftmost letter position. On a US layout that key is Q, which is what
    // Scancode::Q means -- "the key where a US keyboard has Q". On AZERTY the same key produces
    // A, and this mapping must not move, which is the whole point of a scancode.
    EXPECT_EQ(ScancodeFromXkbKeyName("AD01"), Scancode::Q);
    EXPECT_EQ(ScancodeFromXkbKeyName("AD10"), Scancode::P);
    EXPECT_EQ(ScancodeFromXkbKeyName("AC01"), Scancode::A);
    EXPECT_EQ(ScancodeFromXkbKeyName("AC09"), Scancode::L);
    EXPECT_EQ(ScancodeFromXkbKeyName("AB01"), Scancode::Z);
    EXPECT_EQ(ScancodeFromXkbKeyName("AB07"), Scancode::M);
}

TEST(X11ScancodeMapping, TheWasdBlockIsWhereAGameExpectsIt)
{
    // The binding every game has. W is AD02, A/S/D are AC01/AC02/AC03 -- not the same row, which
    // is exactly the sort of thing an arithmetic mapping gets wrong.
    EXPECT_EQ(ScancodeFromXkbKeyName("AD02"), Scancode::W);
    EXPECT_EQ(ScancodeFromXkbKeyName("AC01"), Scancode::A);
    EXPECT_EQ(ScancodeFromXkbKeyName("AC02"), Scancode::S);
    EXPECT_EQ(ScancodeFromXkbKeyName("AC03"), Scancode::D);
}

TEST(X11ScancodeMapping, NumberRowMapsToDigitPositionsWithZeroLast)
{
    // AE10 is the 0 key, and it comes AFTER AE09 (the 9 key) -- the digit row runs 1..9 then 0,
    // so a mapping that treated AE01..AE10 as 0..9 would be off by one on every digit.
    EXPECT_EQ(ScancodeFromXkbKeyName("AE01"), Scancode::D1);
    EXPECT_EQ(ScancodeFromXkbKeyName("AE09"), Scancode::D9);
    EXPECT_EQ(ScancodeFromXkbKeyName("AE10"), Scancode::D0);
    EXPECT_EQ(ScancodeFromXkbKeyName("AE11"), Scancode::Minus);
    EXPECT_EQ(ScancodeFromXkbKeyName("AE12"), Scancode::Equals);
}

TEST(X11ScancodeMapping, ModifiersAreSidedAndAltGrTakesTheRightAltPosition)
{
    EXPECT_EQ(ScancodeFromXkbKeyName("LFSH"), Scancode::LeftShift);
    EXPECT_EQ(ScancodeFromXkbKeyName("RTSH"), Scancode::RightShift);
    EXPECT_EQ(ScancodeFromXkbKeyName("LCTL"), Scancode::LeftControl);
    EXPECT_EQ(ScancodeFromXkbKeyName("RCTL"), Scancode::RightControl);
    EXPECT_EQ(ScancodeFromXkbKeyName("LALT"), Scancode::LeftAlt);
    EXPECT_EQ(ScancodeFromXkbKeyName("RALT"), Scancode::RightAlt);
    EXPECT_EQ(ScancodeFromXkbKeyName("LWIN"), Scancode::LeftGui);
    EXPECT_EQ(ScancodeFromXkbKeyName("RWIN"), Scancode::RightGui);

    // LVL3 and ALGR are what an ISO layout calls the AltGr key, and it sits in the right-Alt
    // position. A game binding "right alt" by position must find it there.
    EXPECT_EQ(ScancodeFromXkbKeyName("LVL3"), Scancode::RightAlt);
    EXPECT_EQ(ScancodeFromXkbKeyName("ALGR"), Scancode::RightAlt);
}

TEST(X11ScancodeMapping, FunctionAndNavigationKeysAreNamed)
{
    EXPECT_EQ(ScancodeFromXkbKeyName("FK01"), Scancode::F1);
    EXPECT_EQ(ScancodeFromXkbKeyName("FK12"), Scancode::F12);
    EXPECT_EQ(ScancodeFromXkbKeyName("FK13"), Scancode::F13);
    EXPECT_EQ(ScancodeFromXkbKeyName("FK24"), Scancode::F24);
    EXPECT_EQ(ScancodeFromXkbKeyName("LEFT"), Scancode::Left);
    EXPECT_EQ(ScancodeFromXkbKeyName("RGHT"), Scancode::Right);
    EXPECT_EQ(ScancodeFromXkbKeyName("HOME"), Scancode::Home);
    // NEXT is XKB's name for Page Down. It reads like a navigation key of some other kind, which
    // is exactly why it is pinned here.
    EXPECT_EQ(ScancodeFromXkbKeyName("NEXT"), Scancode::PageDown);
    EXPECT_EQ(ScancodeFromXkbKeyName("PGUP"), Scancode::PageUp);
}

TEST(X11ScancodeMapping, KeypadIsDistinctFromTheNumberRow)
{
    // A keypad 1 and a number-row 1 are different physical keys and must stay different
    // scancodes, even though both produce the character '1'.
    EXPECT_EQ(ScancodeFromXkbKeyName("KP1"), Scancode::Keypad1);
    EXPECT_NE(ScancodeFromXkbKeyName("KP1"), ScancodeFromXkbKeyName("AE01"));
    EXPECT_EQ(ScancodeFromXkbKeyName("KPDV"), Scancode::KeypadDivide);
    EXPECT_EQ(ScancodeFromXkbKeyName("KPEN"), Scancode::KeypadEnter);
    EXPECT_NE(ScancodeFromXkbKeyName("KPEN"), ScancodeFromXkbKeyName("RTRN"));
}

TEST(X11ScancodeMapping, AShortNameDoesNotMatchALongerOneThatStartsWithIt)
{
    // XKB names live in a four-byte, NUL-padded field rather than a C string. A prefix comparison
    // that ignored the padding would let "KP1" match a hypothetical "KP10", and "ESC" match
    // "ESCX". The padding check is what prevents that, so it is asserted rather than assumed.
    const char padded[4] = {'K', 'P', '1', '\0'};
    const char longer[4] = {'K', 'P', '1', '0'};
    EXPECT_EQ(ScancodeFromXkbKeyName(padded), Scancode::Keypad1);
    EXPECT_EQ(ScancodeFromXkbKeyName(longer), Scancode::Unknown);
}

TEST(X11ScancodeMapping, AnUnknownVendorKeyIsUnknownRatherThanGuessed)
{
    // Keyboards invent vendor-specific keys and XKB names them I150, I235 and so on. Reporting
    // Unknown is the honest answer; picking a nearby scancode would bind a game action to a key
    // the user cannot identify.
    const char vendor[4] = {'I', '2', '3', '5'};
    EXPECT_EQ(ScancodeFromXkbKeyName(vendor), Scancode::Unknown);
    EXPECT_EQ(ScancodeFromXkbKeyName(nullptr), Scancode::Unknown);
}

// --- logical keys: keysym -> KeyCode -------------------------------------------------------------

TEST(X11KeyCodeMapping, LowerAndUpperCaseLettersBothProduceTheUpperCaseVirtualKey)
{
    // Windows virtual keys have no shifted identity: there is a VK_A and no VK_a. Both X keysyms
    // therefore fold onto the same value, and a mapping that handled only one of them would
    // report KeyCode::None for every capital letter.
    EXPECT_EQ(KeyCodeFromKeysym(XK_a), KeyCode::A);
    EXPECT_EQ(KeyCodeFromKeysym(XK_A), KeyCode::A);
    EXPECT_EQ(KeyCodeFromKeysym(XK_z), KeyCode::Z);
    EXPECT_EQ(KeyCodeFromKeysym(XK_Z), KeyCode::Z);
}

TEST(X11KeyCodeMapping, FunctionKeysRespectTheVirtualKeyGapBetweenF12AndF13)
{
    // The bug two other CNA keyboard backends made independently. X's XK_F1..XK_F24 are
    // contiguous; Windows virtual keys are not -- F1..F12 are 0x70..0x7B and F13..F24 restart at
    // 0x7C. Computing F13 from F1 lands 11 values past F12, which is a different key.
    EXPECT_EQ(KeyCodeFromKeysym(XK_F1), KeyCode::F1);
    EXPECT_EQ(KeyCodeFromKeysym(XK_F12), KeyCode::F12);
    EXPECT_EQ(KeyCodeFromKeysym(XK_F13), KeyCode::F13);
    EXPECT_EQ(KeyCodeFromKeysym(XK_F24), KeyCode::F24);
    EXPECT_EQ(static_cast<int>(KeyCode::F12), 123);
    EXPECT_EQ(static_cast<int>(KeyCode::F13), 124);
}

TEST(X11KeyCodeMapping, SidedModifiersStaySided)
{
    EXPECT_EQ(KeyCodeFromKeysym(XK_Shift_L), KeyCode::LeftShift);
    EXPECT_EQ(KeyCodeFromKeysym(XK_Shift_R), KeyCode::RightShift);
    EXPECT_EQ(KeyCodeFromKeysym(XK_Control_L), KeyCode::LeftControl);
    EXPECT_EQ(KeyCodeFromKeysym(XK_Control_R), KeyCode::RightControl);
    EXPECT_EQ(KeyCodeFromKeysym(XK_Alt_L), KeyCode::LeftAlt);
    EXPECT_EQ(KeyCodeFromKeysym(XK_Alt_R), KeyCode::RightAlt);
    EXPECT_EQ(KeyCodeFromKeysym(XK_Super_L), KeyCode::LeftWindows);
    EXPECT_EQ(KeyCodeFromKeysym(XK_Super_R), KeyCode::RightWindows);
}

TEST(X11KeyCodeMapping, AltGrReportsTheRightAltVirtualKey)
{
    // ISO_Level3_Shift and Mode_switch are what an ISO layout names the key Windows reports as
    // VK_RMENU. A game that checks for right Alt must see it on a European keyboard too.
    EXPECT_EQ(KeyCodeFromKeysym(XK_ISO_Level3_Shift), KeyCode::RightAlt);
    EXPECT_EQ(KeyCodeFromKeysym(XK_Mode_switch), KeyCode::RightAlt);
}

TEST(X11KeyCodeMapping, KeypadDigitsAndTheirNavigationAliasesAgree)
{
    // With Num Lock off the server sends XK_KP_Left rather than XK_KP_4 for the same physical
    // key. Both are that key's identity, so both report NumPad4 -- XNA has no separate
    // "keypad 4 with num lock off".
    EXPECT_EQ(KeyCodeFromKeysym(XK_KP_4), KeyCode::NumPad4);
    EXPECT_EQ(KeyCodeFromKeysym(XK_KP_Left), KeyCode::NumPad4);
    EXPECT_EQ(KeyCodeFromKeysym(XK_KP_0), KeyCode::NumPad0);
    EXPECT_EQ(KeyCodeFromKeysym(XK_KP_Insert), KeyCode::NumPad0);
    EXPECT_EQ(KeyCodeFromKeysym(XK_KP_Enter), KeyCode::Enter);
    EXPECT_EQ(KeyCodeFromKeysym(XK_KP_Add), KeyCode::Add);
    EXPECT_EQ(KeyCodeFromKeysym(XK_KP_Divide), KeyCode::Divide);
}

TEST(X11KeyCodeMapping, PunctuationUsesTheOemVirtualKeys)
{
    EXPECT_EQ(KeyCodeFromKeysym(XK_semicolon), KeyCode::OemSemicolon);
    EXPECT_EQ(KeyCodeFromKeysym(XK_comma), KeyCode::OemComma);
    EXPECT_EQ(KeyCodeFromKeysym(XK_period), KeyCode::OemPeriod);
    EXPECT_EQ(KeyCodeFromKeysym(XK_slash), KeyCode::OemQuestion);
    EXPECT_EQ(KeyCodeFromKeysym(XK_grave), KeyCode::OemTilde);
    EXPECT_EQ(KeyCodeFromKeysym(XK_bracketleft), KeyCode::OemOpenBrackets);
    EXPECT_EQ(KeyCodeFromKeysym(XK_bracketright), KeyCode::OemCloseBrackets);
    EXPECT_EQ(KeyCodeFromKeysym(XK_backslash), KeyCode::OemPipe);
    EXPECT_EQ(KeyCodeFromKeysym(XK_apostrophe), KeyCode::OemQuotes);
}

TEST(X11KeyCodeMapping, AnUnmappedKeysymIsNoneRatherThanAnArbitraryKey)
{
    EXPECT_EQ(KeyCodeFromKeysym(NoSymbol), KeyCode::None);
    // A dead key produces a character but names no virtual key. Reporting None is what keeps a
    // key binding UI from offering "dead acute" as though it were a bindable virtual key.
    EXPECT_EQ(KeyCodeFromKeysym(XK_dead_acute), KeyCode::None);
}

// --- modifiers -----------------------------------------------------------------------------------

TEST(X11ModifierMapping, CoreModifierBitsBecomeTheContractMask)
{
    const std::uint16_t shift = ModifiersFromXState(ShiftMask, 0);
    EXPECT_TRUE(HasModifier(shift, KeyModifier::Shift));
    EXPECT_FALSE(HasModifier(shift, KeyModifier::Control));

    const std::uint16_t combined = ModifiersFromXState(ControlMask | Mod1Mask | Mod4Mask, 0);
    EXPECT_TRUE(HasModifier(combined, KeyModifier::Control));
    EXPECT_TRUE(HasModifier(combined, KeyModifier::Alt));
    EXPECT_TRUE(HasModifier(combined, KeyModifier::Gui));
}

TEST(X11ModifierMapping, LatchedLocksAreReported)
{
    const std::uint16_t locks = ModifiersFromXState(LockMask | Mod2Mask, 0);
    EXPECT_TRUE(HasModifier(locks, KeyModifier::CapsLock));
    EXPECT_TRUE(HasModifier(locks, KeyModifier::NumLock));
}

TEST(X11ModifierMapping, AltGrIsReportedOnlyForTheLayoutsOwnModeSwitchBit)
{
    // Which modifier bit carries Mode_switch is the layout's choice, not a protocol constant.
    // Hardcoding Mod5 would report Mode on a layout that uses that bit for something else, and
    // miss it on one that puts Mode_switch elsewhere.
    EXPECT_FALSE(HasModifier(ModifiersFromXState(Mod5Mask, 0), KeyModifier::Mode));
    EXPECT_TRUE(HasModifier(ModifiersFromXState(Mod5Mask, Mod5Mask), KeyModifier::Mode));
    EXPECT_TRUE(HasModifier(ModifiersFromXState(Mod3Mask, Mod3Mask), KeyModifier::Mode));
}

// --- wheel and buttons ---------------------------------------------------------------------------

TEST(X11ButtonMapping, WheelButtonsAreClassifiedAsScrollNotAsButtons)
{
    // X has no scroll axis; a wheel notch arrives as a press and release of button 4..7. CNA
    // models scrolling separately, so these must never reach a game as button events -- a game
    // watching for clicks would see a phantom click on every scroll notch.
    EXPECT_EQ(ClassifyWheelButton(4), WheelDirection::Up);
    EXPECT_EQ(ClassifyWheelButton(5), WheelDirection::Down);
    EXPECT_EQ(ClassifyWheelButton(6), WheelDirection::Left);
    EXPECT_EQ(ClassifyWheelButton(7), WheelDirection::Right);
    EXPECT_EQ(MapButtonNumber(4), 0);
    EXPECT_EQ(MapButtonNumber(5), 0);
    EXPECT_EQ(MapButtonNumber(6), 0);
    EXPECT_EQ(MapButtonNumber(7), 0);
}

TEST(X11ButtonMapping, TheThreeStandardButtonsPassThroughUnchanged)
{
    EXPECT_EQ(ClassifyWheelButton(1), WheelDirection::None);
    EXPECT_EQ(MapButtonNumber(1), 1);
    EXPECT_EQ(MapButtonNumber(2), 2);
    EXPECT_EQ(MapButtonNumber(3), 3);
}

TEST(X11ButtonMapping, ExtraButtonsCloseTheGapTheWheelLeaves)
{
    // X spends 4..7 on the wheel, so a mouse's first extra button is 8. Passing that through
    // would leave CNA with buttons 1,2,3,8,9 and a hole where the wheel was.
    EXPECT_EQ(MapButtonNumber(8), 4);
    EXPECT_EQ(MapButtonNumber(9), 5);
    EXPECT_EQ(MapButtonNumber(10), 6);
}

// --- focus filtering -----------------------------------------------------------------------------

TEST(X11FocusFiltering, GrabInducedFocusChangesAreNotRealFocusChanges)
{
    // Opening a menu, dragging a window, or this backend's own pointer grab for relative mouse
    // mode all produce FocusOut/FocusIn with a grab mode. Reporting them would make a game pause
    // every time the player opened a menu.
    EXPECT_FALSE(IsRealFocusChange(NotifyGrab, NotifyAncestor));
    EXPECT_FALSE(IsRealFocusChange(NotifyUngrab, NotifyAncestor));
    EXPECT_TRUE(IsRealFocusChange(NotifyNormal, NotifyAncestor));
    EXPECT_TRUE(IsRealFocusChange(NotifyWhileGrabbed, NotifyNonlinear));
}

TEST(X11FocusFiltering, FocusMovingBetweenAWindowAndItsChildIsNotAFocusChange)
{
    // NotifyInferior means focus moved within this window's own subtree. The window as a whole
    // did not lose focus, and telling the application it did would be wrong.
    EXPECT_FALSE(IsRealFocusChange(NotifyNormal, NotifyInferior));
    EXPECT_FALSE(IsRealFocusChange(NotifyNormal, NotifyPointer));
    EXPECT_FALSE(IsRealFocusChange(NotifyNormal, NotifyPointerRoot));
}

// --- auto-repeat ---------------------------------------------------------------------------------

TEST(X11AutoRepeat, AReleasePressPairAtTheSameTimestampIsOneRepeat)
{
    // Without detectable auto-repeat the server sends a held key as release+press at the SAME
    // timestamp. The timestamp is what makes this safe to coalesce: a human cannot release and
    // re-press inside one server millisecond, so a genuine double-tap is never swallowed.
    XKeyEvent release{};
    release.keycode = 38;
    release.time = 12345;
    release.window = 0x400001;

    XEvent next{};
    next.type = KeyPress;
    next.xkey.keycode = 38;
    next.xkey.time = 12345;
    next.xkey.window = 0x400001;

    EXPECT_TRUE(IsAutoRepeatPair(release, next));
}

TEST(X11AutoRepeat, ADeliberateDoubleTapIsNotCoalesced)
{
    XKeyEvent release{};
    release.keycode = 38;
    release.time = 12345;
    release.window = 0x400001;

    XEvent next{};
    next.type = KeyPress;
    next.xkey.keycode = 38;
    next.xkey.time = 12395;  // 50 ms later: a fast human, not the server's repeat
    next.xkey.window = 0x400001;

    EXPECT_FALSE(IsAutoRepeatPair(release, next));
}

TEST(X11AutoRepeat, ADifferentKeyOrWindowIsNeverARepeatOfThisOne)
{
    XKeyEvent release{};
    release.keycode = 38;
    release.time = 12345;
    release.window = 0x400001;

    XEvent otherKey{};
    otherKey.type = KeyPress;
    otherKey.xkey.keycode = 39;
    otherKey.xkey.time = 12345;
    otherKey.xkey.window = 0x400001;
    EXPECT_FALSE(IsAutoRepeatPair(release, otherKey));

    XEvent otherWindow{};
    otherWindow.type = KeyPress;
    otherWindow.xkey.keycode = 38;
    otherWindow.xkey.time = 12345;
    otherWindow.xkey.window = 0x400002;
    EXPECT_FALSE(IsAutoRepeatPair(release, otherWindow));

    XEvent notAPress{};
    notAPress.type = ButtonPress;
    EXPECT_FALSE(IsAutoRepeatPair(release, notAPress));
}

} // namespace
