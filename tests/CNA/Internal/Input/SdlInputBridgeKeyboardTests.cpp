// SPDX-License-Identifier: MS-PL
//
// Tasks 820-821: table-driven tests for the SDL->XNA keyboard conversion.
//
// try_convert_sdl_key / try_convert_sdl_scancode are file-local to SdlInputBridge.cpp, so they are
// exercised through synthetic SDL_EVENT_KEY_DOWN events driven into SdlInputBridge::ProcessEvent
// (the real path), reading the result back via Keyboard::GetState(). Scancode mode is normally
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
using Microsoft::Xna::Framework::Input::Keyboard;
using Microsoft::Xna::Framework::Input::Keys;

namespace
{
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

    struct Case { SDL_Scancode scancode; Keys expected; const char* name; };
    const Case cases[] = {
        {SDL_SCANCODE_A, Keys::A, "A"},
        {SDL_SCANCODE_1, Keys::D1, "D1"},
        {SDL_SCANCODE_KP_1, Keys::NumPad1, "NumPad1"},
        {SDL_SCANCODE_KP_PLUS, Keys::Add, "Add"},
        {SDL_SCANCODE_LSHIFT, Keys::LeftShift, "LeftShift"},
        {SDL_SCANCODE_F1, Keys::F1, "F1"},
        {SDL_SCANCODE_F13, Keys::F13, "F13"},
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
