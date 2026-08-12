// SPDX-License-Identifier: MS-PL
//
// Tasks 820-821: table-driven tests for the SDL->XNA keyboard conversion.
//
// try_convert_sdl_key / try_convert_sdl_scancode are file-local to SdlInputBridge.cpp, so they are
// exercised through synthetic SDL_EVENT_KEY_DOWN events driven into SdlInputBridge::ProcessEvent
// (the real path), reading the bridge's accumulator back through InputManager. The public
// Keyboard surface moved to the platform's once-per-frame snapshot in PLAT-79, so using it here
// would test a different source than the synthetic event was intended to exercise. Scancode mode
// is normally
// gated by the process-cached FNA_KEYBOARD_USE_SCANCODES env var; SdlInputBridge exposes
// SetScancodeModeForTests/ClearScancodeModeForTests so both modes can be exercised in one binary.

#include <gtest/gtest.h>

#include <SDL3/SDL.h>

#include "CNA/Internal/Input/InputManager.hpp"
#include "CNA/Internal/Input/SdlInputBridge.hpp"
#include "Microsoft/Xna/Framework/Input/Keyboard.hpp"
#include "Microsoft/Xna/Framework/Input/Keys.hpp"

using CNA::Internal::Input::InputManager;
using CNA::Internal::Input::SdlInputBridge;
using Microsoft::Xna::Framework::Input::Keys;

namespace
{
    // Keeps the assertions compact while naming the state source these bridge tests own.
    struct Keyboard
    {
        static auto GetState() { return InputManager::GetKeyboardState(); }
        static Keys GetKeyFromScancodeEXT(const Keys key)
        {
            return Microsoft::Xna::Framework::Input::Keyboard::GetKeyFromScancodeEXT(key);
        }
    };

    class SdlInputBridgeKeyboardTest : public ::testing::Test
    {
    protected:
        void SetUp() override { Reset(); }
        void TearDown() override { Reset(); }

        static void Reset()
        {
            SdlInputBridge::ClearScancodeModeForTests();
            InputManager::ResetForTests();
        }
    };

    SDL_Event keyDownWithKeycode(const SDL_Keycode key)
    {
        SDL_Event e{};
        e.type = SDL_EVENT_KEY_DOWN;
        e.key.key = key;
        e.key.scancode = SDL_SCANCODE_UNKNOWN;
        e.key.repeat = false;
        return e;
    }

    SDL_Event keyDownWithScancode(const SDL_Scancode scancode)
    {
        SDL_Event e{};
        e.type = SDL_EVENT_KEY_DOWN;
        e.key.scancode = scancode;
        e.key.key = SDLK_UNKNOWN;
        e.key.repeat = false;
        return e;
    }

    SDL_Event keyDownRepeat(const SDL_Keycode key)
    {
        SDL_Event e = keyDownWithKeycode(key);
        e.key.repeat = true;
        return e;
    }

    SDL_Event keyUpWithKeycode(const SDL_Keycode key)
    {
        SDL_Event e = keyDownWithKeycode(key);
        e.type = SDL_EVENT_KEY_UP;
        return e;
    }
}

// DEC-15 / INPUT-KBD-020 / INPUT-BRIDGE-109: CNA matches FNA — a window focus-loss does NOT clear
// accumulated input state. FNA is event-driven too (its Keyboard.keys list is mutated by KEY_DOWN/KEY_UP)
// and on SDL_EVENT_WINDOW_FOCUS_LOST it only sets game.IsActive = false (SDL3_FNAPlatform.cs:1026-1035);
// it never clears keys. So a held key survives a focus-loss event, identical to FNA. This pins that
// decision (a beyond-FNA transient clear was considered and rejected); games gate input on Game.IsActive.
TEST_F(SdlInputBridgeKeyboardTest, WindowFocusLostDoesNotClearHeldKeysMatchingFna)
{
    SdlInputBridge::ProcessEvent(keyDownWithKeycode(SDLK_A));
    ASSERT_TRUE(Keyboard::GetState().IsKeyDown(Keys::A));

    SDL_Event focusLost{};
    focusLost.type = SDL_EVENT_WINDOW_FOCUS_LOST;
    SdlInputBridge::ProcessEvent(focusLost);

    EXPECT_TRUE(Keyboard::GetState().IsKeyDown(Keys::A))
        << "focus loss must not clear accumulated key state (matches FNA)";
}

// INPUT-KBD-021: window lifecycle events (minimize / restore / maximize / close-request / focus change)
// are not keyboard events — the bridge has no case for them, so they fall through as no-ops and must not
// touch accumulated keyboard state. Held keys survive the whole sequence unchanged (this is the same
// event-driven consequence as DEC-15's focus-loss: CNA never clears keys; games gate on Game.IsActive).
TEST_F(SdlInputBridgeKeyboardTest, WindowLifecycleEventsDoNotCorruptKeyboardState)
{
    SdlInputBridge::ProcessEvent(keyDownWithKeycode(SDLK_A));
    SdlInputBridge::ProcessEvent(keyDownWithKeycode(SDLK_B));
    ASSERT_EQ(Keyboard::GetState().GetPressedKeys().size(), 2u);

    for (const Uint32 type : {SDL_EVENT_WINDOW_MINIMIZED, SDL_EVENT_WINDOW_RESTORED,
                              SDL_EVENT_WINDOW_MAXIMIZED, SDL_EVENT_WINDOW_FOCUS_GAINED,
                              SDL_EVENT_WINDOW_CLOSE_REQUESTED})
    {
        SDL_Event e{};
        e.type = type;
        e.window.windowID = 0;
        SdlInputBridge::ProcessEvent(e);
    }

    const auto kb = Keyboard::GetState();
    EXPECT_TRUE(kb.IsKeyDown(Keys::A));
    EXPECT_TRUE(kb.IsKeyDown(Keys::B));
    EXPECT_EQ(kb.GetPressedKeys().size(), 2u) << "window events must not add/remove keys";
    EXPECT_FALSE(kb.IsKeyDown(Keys::None));

    // A real KEY_UP still works normally afterwards (state machine intact).
    SdlInputBridge::ProcessEvent(keyUpWithKeycode(SDLK_A));
    EXPECT_TRUE(Keyboard::GetState().IsKeyUp(Keys::A));
    EXPECT_TRUE(Keyboard::GetState().IsKeyDown(Keys::B));
}

// P8-001(c): the bridge deliberately does NOT consume window-resize / display-orientation / quit events
// (unlike FNA, which uses them for coordinate scaling, adapter reset, and Game exit — those belong to the
// graphics/app layers, not input). They must fall through the default: no-op branch without mutating input
// state or crashing.
TEST_F(SdlInputBridgeKeyboardTest, UnconsumedResizeDisplayAndQuitEventsDoNotAffectInputState)
{
    SdlInputBridge::ProcessEvent(keyDownWithKeycode(SDLK_A));
    ASSERT_TRUE(Keyboard::GetState().IsKeyDown(Keys::A));

    SDL_Event resize{};
    resize.type = SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED;
    resize.window.windowID = 0;
    resize.window.data1 = 1920;
    resize.window.data2 = 1080;
    SdlInputBridge::ProcessEvent(resize);

    SDL_Event orientation{};
    orientation.type = SDL_EVENT_DISPLAY_ORIENTATION;
    SdlInputBridge::ProcessEvent(orientation);

    SDL_Event quit{};
    quit.type = SDL_EVENT_QUIT;
    SdlInputBridge::ProcessEvent(quit);

    // None are consumed -> keyboard state is unchanged and nothing crashes.
    EXPECT_TRUE(Keyboard::GetState().IsKeyDown(Keys::A));
    EXPECT_EQ(Keyboard::GetState().GetPressedKeys().size(), 1u)
        << "resize/display/quit events must not touch input state";
}

// --- Task 820: keycode map (default mode) via synthetic KEY_DOWN events ---

TEST_F(SdlInputBridgeKeyboardTest, KeycodeMapCoversLettersDigitsNumpadOemModifiersFunctionAndMediaKeys)
{
    struct Case { SDL_Keycode key; Keys expected; const char* name; };
    const Case cases[] = {
        // Letters
        {SDLK_A, Keys::A, "A"},
        {SDLK_D, Keys::D, "D"},
        {SDLK_F, Keys::F, "F"},
        // Digits
        {SDLK_0, Keys::D0, "D0"},
        {SDLK_1, Keys::D1, "D1"},
        // Numpad
        {SDLK_KP_1, Keys::NumPad1, "NumPad1"},
        // OEM
        {SDLK_SEMICOLON, Keys::OemSemicolon, "OemSemicolon"},
        {SDLK_COMMA, Keys::OemComma, "OemComma"},
        {SDLK_PERIOD, Keys::OemPeriod, "OemPeriod"},
        // Modifiers
        {SDLK_LCTRL, Keys::LeftControl, "LeftControl"},
        {SDLK_LSHIFT, Keys::LeftShift, "LeftShift"},
        {SDLK_LALT, Keys::LeftAlt, "LeftAlt"},
        // Function keys (incl. the extended F13-F24 range from task 763)
        {SDLK_F1, Keys::F1, "F1"},
        {SDLK_F13, Keys::F13, "F13"},
        {SDLK_F24, Keys::F24, "F24"},
        // Navigation / whitespace
        {SDLK_UP, Keys::Up, "Up"},
        {SDLK_SPACE, Keys::Space, "Space"},
        {SDLK_RETURN, Keys::Enter, "Enter"},
        {SDLK_ESCAPE, Keys::Escape, "Escape"},
        // Media / system keys
        {SDLK_VOLUMEUP, Keys::VolumeUp, "VolumeUp"},
        {SDLK_APPLICATION, Keys::Apps, "Apps"},
        {SDLK_SLEEP, Keys::Sleep, "Sleep"},
    };

    for (const Case& c : cases)
    {
        InputManager::ResetForTests();
        SdlInputBridge::ProcessEvent(keyDownWithKeycode(c.key));
        EXPECT_TRUE(Keyboard::GetState().IsKeyDown(c.expected)) << c.name;
    }
}

// P2-009: left/right Shift, Control, Alt each map to their OWN distinct Keys value through the real
// KEY_DOWN path (the keycode map above only spot-checks the left variants), and the three lock keys
// (CapsLock / NumLock / Scroll) map through too. Also pins "no accidental merging" — XNA/FNA keep the
// left and right modifier as separate Keys, so pressing one must NOT light the other.
TEST_F(SdlInputBridgeKeyboardTest, ModifierAndLockKeysMapToDistinctKeysWithoutMerging)
{
    struct Case { SDL_Keycode key; Keys expected; const char* name; };
    const Case cases[] = {
        {SDLK_LSHIFT, Keys::LeftShift,    "LeftShift"},
        {SDLK_RSHIFT, Keys::RightShift,   "RightShift"},
        {SDLK_LCTRL,  Keys::LeftControl,  "LeftControl"},
        {SDLK_RCTRL,  Keys::RightControl, "RightControl"},
        {SDLK_LALT,   Keys::LeftAlt,      "LeftAlt"},
        {SDLK_RALT,   Keys::RightAlt,     "RightAlt"},
        {SDLK_CAPSLOCK,      Keys::CapsLock, "CapsLock"},
        {SDLK_NUMLOCKCLEAR,  Keys::NumLock,  "NumLock"},
        {SDLK_SCROLLLOCK,    Keys::Scroll,   "Scroll"},
    };
    for (const Case& c : cases)
    {
        InputManager::ResetForTests();
        SdlInputBridge::ProcessEvent(keyDownWithKeycode(c.key));
        EXPECT_TRUE(Keyboard::GetState().IsKeyDown(c.expected)) << c.name;
        EXPECT_EQ(Keyboard::GetState().GetPressedKeys().size(), 1u)
            << c.name << " must light exactly one key";
    }

    // No accidental merging: each side is independent of its mirror.
    struct Pair { SDL_Keycode key; Keys pressed; Keys mirror; const char* name; };
    const Pair pairs[] = {
        {SDLK_LSHIFT, Keys::LeftShift,    Keys::RightShift,   "LShift!=RShift"},
        {SDLK_RSHIFT, Keys::RightShift,   Keys::LeftShift,    "RShift!=LShift"},
        {SDLK_LCTRL,  Keys::LeftControl,  Keys::RightControl, "LCtrl!=RCtrl"},
        {SDLK_RCTRL,  Keys::RightControl, Keys::LeftControl,  "RCtrl!=LCtrl"},
        {SDLK_LALT,   Keys::LeftAlt,      Keys::RightAlt,     "LAlt!=RAlt"},
        {SDLK_RALT,   Keys::RightAlt,     Keys::LeftAlt,      "RAlt!=LAlt"},
    };
    for (const Pair& p : pairs)
    {
        InputManager::ResetForTests();
        SdlInputBridge::ProcessEvent(keyDownWithKeycode(p.key));
        EXPECT_TRUE(Keyboard::GetState().IsKeyDown(p.pressed)) << p.name;
        EXPECT_FALSE(Keyboard::GetState().IsKeyDown(p.mirror))
            << p.name << " — the mirror modifier must stay up (no merging)";
    }
}

// P2-056: every left/right modifier and lock key held down AT ONCE (not one-at-a-time as the test
// above) must all remain independently tracked — proving pressedKeys_'s std::unordered_set<Keys>
// correctly distinguishes all 9 simultaneously-live entries with no collision/overwrite, and that
// releasing one leaves the other 8 untouched.
TEST_F(SdlInputBridgeKeyboardTest, SimultaneousModifierAndLockKeyCombinationTracksAllIndependently)
{
    const SDL_Keycode allKeys[] = {
        SDLK_LSHIFT, SDLK_RSHIFT, SDLK_LCTRL, SDLK_RCTRL, SDLK_LALT, SDLK_RALT,
        SDLK_CAPSLOCK, SDLK_NUMLOCKCLEAR, SDLK_SCROLLLOCK,
    };
    const Keys allXnaKeys[] = {
        Keys::LeftShift, Keys::RightShift, Keys::LeftControl, Keys::RightControl,
        Keys::LeftAlt, Keys::RightAlt, Keys::CapsLock, Keys::NumLock, Keys::Scroll,
    };

    for (const SDL_Keycode key : allKeys)
    {
        SdlInputBridge::ProcessEvent(keyDownWithKeycode(key));
    }

    const auto state = Keyboard::GetState();
    for (const Keys expected : allXnaKeys)
    {
        EXPECT_TRUE(state.IsKeyDown(expected)) << static_cast<int>(expected);
    }
    EXPECT_EQ(state.GetPressedKeys().size(), 9u);

    // Releasing one (LeftShift) must not disturb the other 8.
    SDL_Event up{};
    up.type = SDL_EVENT_KEY_UP;
    up.key.key = SDLK_LSHIFT;
    up.key.scancode = SDL_SCANCODE_UNKNOWN;
    SdlInputBridge::ProcessEvent(up);

    const auto afterRelease = Keyboard::GetState();
    EXPECT_FALSE(afterRelease.IsKeyDown(Keys::LeftShift));
    for (const Keys expected : allXnaKeys)
    {
        if (expected == Keys::LeftShift) continue;
        EXPECT_TRUE(afterRelease.IsKeyDown(expected)) << static_cast<int>(expected);
    }
    EXPECT_EQ(afterRelease.GetPressedKeys().size(), 8u);
}

TEST_F(SdlInputBridgeKeyboardTest, UnmappedKeycodeIsDroppedNotMarkedNone)
{
    // DEC-16: SDLK_UNKNOWN (and any unmapped keycode) is dropped rather than marked Keys::None pressed.
    // This is an intentional deviation from FNA, whose ToXNAKey returns Keys.None for an unmapped key and
    // then Keyboard.keys.Add(Keys.None) (SDL3_FNAPlatform.cs:905-908) — polluting the pressed set with a
    // meaningless "None" key. CNA drops instead, so IsKeyDown(None) stays false.
    SdlInputBridge::ProcessEvent(keyDownWithKeycode(SDLK_UNKNOWN));
    EXPECT_FALSE(Keyboard::GetState().IsKeyDown(Keys::None));
    EXPECT_EQ(Keyboard::GetState().GetPressedKeys().size(), 0u);
}

// INPUT-KBD-009: a locale/accented keycode with no XNA Keys equivalent (SDL delivers it as the raw
// Unicode codepoint, and try_convert_sdl_key has no case for it) hits the default `nullopt` path and is
// DROPPED — the DEC-16 policy again (never pollute the pressed set with Keys::None). This is the
// "é → unmapped" locale fallback. A full line-by-line diff of the keycode map vs FNA's INTERNAL_keyMap is
// byte-identical on all 122 shared keycodes; the only differences are DEC-16 (SDLK_UNKNOWN drop) and
// DEC-17 (SDLK_AC_BACK → Escape). (Codepoints that DO resolve — e.g. æ/ø — correspond to named SDLK
// constants present in both maps and are covered by the map itself, not here.)
TEST_F(SdlInputBridgeKeyboardTest, LocaleUnmappedKeycodeIsDroppedNotMarkedNone)
{
    // é (U+00E9) and a CJK ideograph 中 (U+4E2D): printable codepoints with no named SDLK / map entry.
    for (const SDL_Keycode kc : {SDL_Keycode{0xE9}, SDL_Keycode{0x4E2D}})
    {
        InputManager::ResetForTests();
        SdlInputBridge::ProcessEvent(keyDownWithKeycode(kc));

        EXPECT_FALSE(Keyboard::GetState().IsKeyDown(Keys::None))
            << "locale keycode " << static_cast<unsigned>(kc) << " must not mark Keys::None";
        EXPECT_EQ(Keyboard::GetState().GetPressedKeys().size(), 0u)
            << "locale keycode " << static_cast<unsigned>(kc) << " must be dropped";
    }
}

// INPUT-KBD-014: non-US layout keys. In keycode mode SDL delivers the layout's symbol as a keycode;
// accented / non-ASCII letters (German ä ö ü ß, French é è à ç, Czech ě š č) have no XNA Keys value and
// are DROPPED (never pollute the pressed set — the DEC-16 policy). This is a fundamental XNA limitation
// (its Keys enum is US-centric); such text reaches games via TextInputEXT, not Keyboard.
TEST_F(SdlInputBridgeKeyboardTest, NonUsLayoutAccentedKeysAreUnmappedInKeycodeMode)
{
    struct Case { SDL_Keycode cp; const char* name; };
    const Case dropped[] = {
        {SDL_Keycode{0xE4}, "de:a-diaeresis"}, {SDL_Keycode{0xF6}, "de:o-diaeresis"},
        {SDL_Keycode{0xFC}, "de:u-diaeresis"}, {SDL_Keycode{0xDF}, "de:sharp-s"},
        {SDL_Keycode{0xE9}, "fr:e-acute"},     {SDL_Keycode{0xE8}, "fr:e-grave"},
        {SDL_Keycode{0xE0}, "fr:a-grave"},     {SDL_Keycode{0xE7}, "fr:c-cedilla"},
        {SDL_Keycode{0x11B}, "cz:e-caron"},    {SDL_Keycode{0x161}, "cz:s-caron"},
        {SDL_Keycode{0x10D}, "cz:c-caron"},
    };
    for (const Case& c : dropped)
    {
        InputManager::ResetForTests();
        SdlInputBridge::ProcessEvent(keyDownWithKeycode(c.cp));
        EXPECT_FALSE(Keyboard::GetState().IsKeyDown(Keys::None)) << c.name;
        EXPECT_EQ(Keyboard::GetState().GetPressedKeys().size(), 0u) << c.name << " must be dropped";
    }
}

// INPUT-KBD-014: the Nordic exception — a few non-ASCII keys whose physical position is a US OEM key
// still resolve to that OEM key (matching FNA), rather than dropping. Pins that FNA-faithful behavior.
TEST_F(SdlInputBridgeKeyboardTest, NordicOemKeysMapToTheirOemKeyMatchingFna)
{
    InputManager::ResetForTests();
    SdlInputBridge::ProcessEvent(keyDownWithKeycode(SDL_Keycode{0xE6})); // æ
    EXPECT_TRUE(Keyboard::GetState().IsKeyDown(Keys::OemQuotes));

    InputManager::ResetForTests();
    SdlInputBridge::ProcessEvent(keyDownWithKeycode(SDL_Keycode{0xF8})); // ø
    EXPECT_TRUE(Keyboard::GetState().IsKeyDown(Keys::OemSemicolon));
}

// INPUT-KBD-016: among media/browser keys, CNA maps ONLY VolumeUp/VolumeDown from SDL — exactly like
// FNA's keyMap (INPUT-KBD-009 proved the whole map is FNA-identical). The other media/browser keys SDL
// delivers (next/prev track, play/pause, AC_HOME/SEARCH, …) have no keyMap entry and are dropped, even
// though XNA defines Keys::MediaNextTrack / BrowserHome / etc. — those Keys have no desktop SDL source.
TEST_F(SdlInputBridgeKeyboardTest, SdlMediaBrowserKeysAreUnmappedExceptVolumeMatchingFna)
{
    // Mapped:
    InputManager::ResetForTests();
    SdlInputBridge::ProcessEvent(keyDownWithKeycode(SDLK_VOLUMEUP));
    EXPECT_TRUE(Keyboard::GetState().IsKeyDown(Keys::VolumeUp));
    InputManager::ResetForTests();
    SdlInputBridge::ProcessEvent(keyDownWithKeycode(SDLK_VOLUMEDOWN));
    EXPECT_TRUE(Keyboard::GetState().IsKeyDown(Keys::VolumeDown));

    // Unmapped (dropped) — FNA has no keyMap entry for these:
    for (const SDL_Keycode kc : {SDLK_MEDIA_NEXT_TRACK, SDLK_MEDIA_PREVIOUS_TRACK,
                                 SDLK_MEDIA_PLAY_PAUSE, SDLK_AC_HOME, SDLK_AC_SEARCH})
    {
        InputManager::ResetForTests();
        SdlInputBridge::ProcessEvent(keyDownWithKeycode(kc));
        EXPECT_EQ(Keyboard::GetState().GetPressedKeys().size(), 0u)
            << "SDL media/browser keycode " << static_cast<unsigned>(kc) << " must be dropped";
    }
}

// INPUT-KBD-015 / INPUT-KBD-017: the IME keys (Kana/Kanji/ImeConvert/ImeNoConvert/ProcessKey) and the
// ChatPad keys (Green/Orange) exist in the XNA Keys enum (exhaustively value-pinned by INPUT-KBD-001) but
// have no desktop SDL source — IME keys are Windows IME virtual keys, ChatPad keys are Xbox console-only.
// FNA maps none of them either. This compiles the values (presence) and documents the intentional omission;
// the "never produced by SDL" side is implied by the FNA-identical keyMap/scanMap (INPUT-KBD-009/010).
TEST_F(SdlInputBridgeKeyboardTest, ImeAndChatPadKeysExistAndAreConsoleOrImeOnly)
{
    static_assert(static_cast<int>(Keys::Kana) == 21 && static_cast<int>(Keys::Kanji) == 25);
    static_assert(static_cast<int>(Keys::ImeConvert) == 28 && static_cast<int>(Keys::ImeNoConvert) == 29);
    static_assert(static_cast<int>(Keys::ProcessKey) == 229);
    static_assert(static_cast<int>(Keys::ChatPadGreen) == 202 && static_cast<int>(Keys::ChatPadOrange) == 203);
    SUCCEED() << "IME + ChatPad Keys present; intentionally unmapped from SDL (see docs/input-fna-fidelity.md)";
}

// DEC-17: SDLK_AC_BACK (the Android/browser Back button) maps to Keys::Escape. This is a CNA-only
// convenience (FNA has no AC_BACK mapping) so "back" acts as cancel/exit on those platforms.
TEST_F(SdlInputBridgeKeyboardTest, AndroidBackButtonMapsToEscape)
{
    SdlInputBridge::ProcessEvent(keyDownWithKeycode(SDLK_AC_BACK));
    EXPECT_TRUE(Keyboard::GetState().IsKeyDown(Keys::Escape));
}

// --- Task 820: scancode map (scancode mode) via synthetic KEY_DOWN events ---

TEST_F(SdlInputBridgeKeyboardTest, ScancodeMapUsedWhenScancodeModeForced)
{
    SdlInputBridge::SetScancodeModeForTests(true);

    // INPUT-KBD-010: one representative per scancode-map group. A full mechanical diff of
    // try_convert_sdl_scancode vs FNA's INTERNAL_scanMap is byte-identical on all 122 shared scancodes;
    // the only differences are the three CNA drops (UNKNOWN / NONUSHASH / NONUSBACKSLASH — INPUT-KBD-011),
    // covered by IsoLayoutExtraScancodesAreDroppedNotMarkedNone.
    struct Case { SDL_Scancode scancode; Keys expected; const char* name; };
    const Case cases[] = {
        // Letters / digits
        {SDL_SCANCODE_A, Keys::A, "A"},
        {SDL_SCANCODE_1, Keys::D1, "D1"},
        // Numpad
        {SDL_SCANCODE_KP_1, Keys::NumPad1, "NumPad1"},
        {SDL_SCANCODE_KP_PLUS, Keys::Add, "Add"},
        // Modifiers
        {SDL_SCANCODE_LSHIFT, Keys::LeftShift, "LeftShift"},
        {SDL_SCANCODE_LCTRL, Keys::LeftControl, "LeftControl"},
        {SDL_SCANCODE_LALT, Keys::LeftAlt, "LeftAlt"},
        // Function keys (incl. extended F13)
        {SDL_SCANCODE_F1, Keys::F1, "F1"},
        {SDL_SCANCODE_F13, Keys::F13, "F13"},
        // OEM
        {SDL_SCANCODE_SEMICOLON, Keys::OemSemicolon, "OemSemicolon"},
        {SDL_SCANCODE_COMMA, Keys::OemComma, "OemComma"},
        {SDL_SCANCODE_PERIOD, Keys::OemPeriod, "OemPeriod"},
        {SDL_SCANCODE_GRAVE, Keys::OemTilde, "OemTilde"},
        {SDL_SCANCODE_SLASH, Keys::OemQuestion, "OemQuestion"},
        // Navigation / whitespace
        {SDL_SCANCODE_UP, Keys::Up, "Up"},
        {SDL_SCANCODE_SPACE, Keys::Space, "Space"},
        {SDL_SCANCODE_RETURN, Keys::Enter, "Enter"},
        {SDL_SCANCODE_ESCAPE, Keys::Escape, "Escape"},
        // Media
        {SDL_SCANCODE_VOLUMEUP, Keys::VolumeUp, "VolumeUp"},
    };

    for (const Case& c : cases)
    {
        InputManager::ResetForTests();
        SdlInputBridge::ProcessEvent(keyDownWithScancode(c.scancode));
        EXPECT_TRUE(Keyboard::GetState().IsKeyDown(c.expected)) << c.name;
    }
}

TEST_F(SdlInputBridgeKeyboardTest, ScancodeModeIgnoresTheLayoutDependentKeycode)
{
    // In scancode mode the physical position (scancode) wins; a bogus/mismatched keycode on the
    // same event must not influence the result.
    SdlInputBridge::SetScancodeModeForTests(true);

    SDL_Event e = keyDownWithScancode(SDL_SCANCODE_A);
    e.key.key = SDLK_Z; // deliberately inconsistent — should be ignored in scancode mode
    SdlInputBridge::ProcessEvent(e);

    EXPECT_TRUE(Keyboard::GetState().IsKeyDown(Keys::A));
    EXPECT_FALSE(Keyboard::GetState().IsKeyDown(Keys::Z));
}

// INPUT-KBD-019: a repeated KEY_DOWN (SDL sets event.key.repeat) must keep the key down without any
// spurious transition — the bridge skips the pressed-state update on repeats (state is already set), so
// the pressed set stays exactly {key} across any number of repeats, and only a real KEY_UP releases it.
// (The text-synthesis half of the repeat gate is covered by SdlInputBridgeTextInputTest.
// KeyRepeatReemitsControlCharacter; this is the state half.)
TEST_F(SdlInputBridgeKeyboardTest, KeyRepeatKeepsKeyDownWithoutSpuriousTransitions)
{
    SdlInputBridge::ProcessEvent(keyDownWithKeycode(SDLK_A)); // initial press
    ASSERT_TRUE(Keyboard::GetState().IsKeyDown(Keys::A));

    for (int i = 0; i < 5; ++i)
        SdlInputBridge::ProcessEvent(keyDownRepeat(SDLK_A)); // auto-repeat fires

    const auto held = Keyboard::GetState();
    EXPECT_TRUE(held.IsKeyDown(Keys::A));
    EXPECT_EQ(held.GetPressedKeys().size(), 1u) << "repeats must not add duplicate or spurious keys";

    SdlInputBridge::ProcessEvent(keyUpWithKeycode(SDLK_A)); // the only event that releases it
    EXPECT_TRUE(Keyboard::GetState().IsKeyUp(Keys::A));
    EXPECT_EQ(Keyboard::GetState().GetPressedKeys().size(), 0u);
}

// INPUT-KBD-011/019: scancodes with no XNA Keys value — the no-scancode sentinel (UNKNOWN) and the two
// ISO-layout extra keys (NONUSHASH, NONUSBACKSLASH) — are DROPPED, the same DEC-16 policy as unmapped
// keycodes, so IsKeyDown(None) stays false and the pressed set stays empty rather than being polluted
// with a meaningless None. (FNA maps the ISO keys to Keys.None with its own "need verification" FIXME.)
TEST_F(SdlInputBridgeKeyboardTest, IsoLayoutExtraScancodesAreDroppedNotMarkedNone)
{
    SdlInputBridge::SetScancodeModeForTests(true);

    for (const SDL_Scancode sc : {SDL_SCANCODE_UNKNOWN, SDL_SCANCODE_NONUSHASH, SDL_SCANCODE_NONUSBACKSLASH})
    {
        InputManager::ResetForTests();
        SdlInputBridge::ProcessEvent(keyDownWithScancode(sc));

        EXPECT_FALSE(Keyboard::GetState().IsKeyDown(Keys::None))
            << "scancode " << static_cast<int>(sc) << " must not mark Keys::None pressed";
        EXPECT_EQ(Keyboard::GetState().GetPressedKeys().size(), 0u)
            << "scancode " << static_cast<int>(sc) << " must be dropped, leaving the pressed set empty";
    }
}

// --- Task 821: Keyboard::GetKeyFromScancodeEXT in both modes ---

TEST_F(SdlInputBridgeKeyboardTest, GetKeyFromScancodeEXTIsIdentityInScancodeMode)
{
    // FNA's GetKeyFromScancode short-circuits to the input in scancode mode (SDL3_FNAPlatform.cs:
    // 2768-2773) — layout-independent, so this is robust everywhere.
    SdlInputBridge::SetScancodeModeForTests(true);

    EXPECT_EQ(Keyboard::GetKeyFromScancodeEXT(Keys::A), Keys::A);
    EXPECT_EQ(Keyboard::GetKeyFromScancodeEXT(Keys::F13), Keys::F13);
    EXPECT_EQ(Keyboard::GetKeyFromScancodeEXT(Keys::OemComma), Keys::OemComma);
    EXPECT_EQ(Keyboard::GetKeyFromScancodeEXT(Keys::None), Keys::None);
}

TEST_F(SdlInputBridgeKeyboardTest, GetKeyFromScancodeEXTTranslatesInNormalMode)
{
    SdlInputBridge::SetScancodeModeForTests(false);

    // F13 is layout-invariant (its scancode's keycode is the same on any layout), so its
    // xnaMap -> SDL_GetKeyFromScancode -> keyMap round-trip is stable across CI environments.
    EXPECT_EQ(Keyboard::GetKeyFromScancodeEXT(Keys::F13), Keys::F13);

    // A Keys value with no SDL scancode (see task 819's unmappable list) resolves to None.
    EXPECT_EQ(Keyboard::GetKeyFromScancodeEXT(Keys::Kana), Keys::None);
}
