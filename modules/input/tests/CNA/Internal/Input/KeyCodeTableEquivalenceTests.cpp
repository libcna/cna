// SPDX-License-Identifier: MS-PL
//
// PLAT-78b: the keycode table moved out of SdlInputBridge without changing its answers.
//
// `SdlInputBridge` still owns its original SDL-keycode table, reachable through
// `GetKeyFromName`; `CNA::Platform::Sdl3::ToKeyCode` is the copy that moved into the platform
// module. While both exist they must agree for every key, because a migration that silently
// remapped one surfaces as "my game's controls moved" rather than as a test failure.
//
// This file is deliberately temporary. It is deleted when the bridge's own table goes, and until
// then its whole job is to make the two copies impossible to drift apart.

#include "CNA/Internal/Input/SdlInputBridge.hpp"
#include "CNA/Platform/Input/KeyCode.hpp"
#include "Microsoft/Xna/Framework/Input/Keys.hpp"

#include "../../../../../platform/src/Sdl3/Sdl3KeyCodes.hpp"

#include <SDL3/SDL.h>
#include <gtest/gtest.h>

#include <cstdint>
#include <string>

namespace {

using CNA::Internal::Input::SdlInputBridge;
using CNA::Platform::KeyCode;
using CNA::Platform::Sdl3::ToKeyCode;
using Microsoft::Xna::Framework::Input::Keys;

/// Runs one SDL keycode through both tables and reports any disagreement.
///
/// The bridge's table is file-static, so the only way to reach it is `GetKeyFromName`, which
/// takes the round trip keycode -> name -> keycode. That trip is **not** identity for every
/// keycode: SDL's naming is many-to-one, so `SDLK_RETURN2` is named "Return" and comes back as
/// `SDLK_RETURN`, and `'!'` comes back as the key that produces it. Comparing across a lossy
/// round trip measures SDL's name table, not CNA's keycode tables -- it reported two failures
/// where the two tables in fact agreed exactly.
///
/// So the round trip is verified before it is trusted, and a keycode that does not survive it is
/// skipped and counted rather than silently compared.
///
/// @return True if the keycode was actually compared.
bool ExpectSameAnswer(const SDL_Keycode keycode, const char* origin)
{
    const char* name = SDL_GetKeyName(keycode);
    if (name == nullptr || *name == '\0')
    {
        return false;
    }
    if (SDL_GetKeyFromName(name) != keycode)
    {
        return false;  // an alias; the bridge's table is unreachable for it
    }

    const KeyCode viaPlatform = ToKeyCode(keycode);
    const Keys viaBridge = SdlInputBridge::GetKeyFromName(name);

    EXPECT_EQ(static_cast<std::uint16_t>(viaPlatform), static_cast<std::uint16_t>(viaBridge))
        << origin << ": SDL keycode " << static_cast<int>(keycode) << " (\"" << name << "\") maps to "
        << CNA::Platform::ToString(viaPlatform) << " in the platform table but to XNA Keys "
        << static_cast<int>(viaBridge) << " in the bridge";
    return true;
}

TEST(KeyCodeTableEquivalenceTests, EveryPhysicalKeyAgreesBetweenTheTwoTables)
{
    // Driven from the scancode side so the set is every key a real keyboard has, rather than a
    // list someone maintained by hand.
    int compared = 0;
    for (int raw = 0; raw < SDL_SCANCODE_COUNT; ++raw)
    {
        const SDL_Keycode keycode =
            SDL_GetKeyFromScancode(static_cast<SDL_Scancode>(raw), SDL_KMOD_NONE, true);
        if (keycode == SDLK_UNKNOWN)
        {
            continue;
        }
        compared += ExpectSameAnswer(keycode, "scancode sweep") ? 1 : 0;
    }

    // A sweep that compared nothing would pass silently.
    EXPECT_GE(compared, 100);
}

TEST(KeyCodeTableEquivalenceTests, ThePrintableAsciiRangeAgrees)
{
    // SDL keycodes for printable keys are their character values, and this is the range a text
    // field actually exercises.
    int compared = 0;
    for (int character = 0x20; character < 0x7F; ++character)
    {
        compared += ExpectSameAnswer(static_cast<SDL_Keycode>(character), "printable ASCII") ? 1 : 0;
    }
    EXPECT_GE(compared, 40);
}

TEST(KeyCodeTableEquivalenceTests, TheNonUsLayoutFallbacksAgree)
{
    // The hand-written tail of the table: characters non-US layouts produce on keys the US-layout
    // entries above already cover. Easy to drop in a port precisely because nothing else
    // references them.
    for (const SDL_Keycode keycode : {static_cast<SDL_Keycode>(0x00B2),   // '²' AZERTY
                                      static_cast<SDL_Keycode>('|'),      // Norwegian
                                      static_cast<SDL_Keycode>('+'),      // Norwegian
                                      static_cast<SDL_Keycode>(0x00F8),   // 'ø' Norwegian
                                      static_cast<SDL_Keycode>(0x00E6),   // 'æ' Norwegian
                                      static_cast<SDL_Keycode>(0x00E9)})  // 'é' BEPO, unmapped
    {
        // Compared where the round trip allows it; the platform table is asserted directly below
        // regardless, since these are the entries most likely to be dropped in a port.
        (void)ExpectSameAnswer(keycode, "non-US fallback");
    }

    EXPECT_EQ(ToKeyCode(static_cast<SDL_Keycode>(0x00B2)), KeyCode::OemTilde);
    EXPECT_EQ(ToKeyCode(static_cast<SDL_Keycode>('|')), KeyCode::OemPipe);
    EXPECT_EQ(ToKeyCode(static_cast<SDL_Keycode>('+')), KeyCode::OemPlus);
    EXPECT_EQ(ToKeyCode(static_cast<SDL_Keycode>(0x00F8)), KeyCode::OemSemicolon);
    EXPECT_EQ(ToKeyCode(static_cast<SDL_Keycode>(0x00E6)), KeyCode::OemQuotes);
    EXPECT_EQ(ToKeyCode(static_cast<SDL_Keycode>(0x00E9)), KeyCode::None);
}

TEST(KeyCodeTableEquivalenceTests, TheAndroidBackKeyStillBecomesEscape)
{
    // A CNA-only mapping with no FNA counterpart (DEC-17). Nothing upstream would notice if the
    // port dropped it, and on Android it is the difference between the Back button pausing the
    // game and doing nothing at all.
    EXPECT_EQ(ToKeyCode(SDLK_AC_BACK), KeyCode::Escape);
}

} // namespace
