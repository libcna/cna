// SPDX-License-Identifier: MS-PL
#include <gtest/gtest.h>

#include "CNA/Input/KeyModifiers.hpp"
#include "CNA/Internal/Input/SystemKeyboardBackend.hpp"
#include "Microsoft/Xna/Framework/Input/Keyboard.hpp"

#include <SDL3/SDL.h>

using CNA::Input::KeyModifiersEXT;
using CNA::Internal::Input::ISystemKeyboardBackend;
using CNA::Internal::Input::SetSystemKeyboardBackendForTests;
using Microsoft::Xna::Framework::Input::Keyboard;

namespace
{
    // A fake modifier source, so Keyboard::GetModStateEXT can be exercised deterministically (SDL's
    // real mod state is only updated by real key events, which a headless test does not deliver).
    class FakeSystemKeyboardBackend final : public ISystemKeyboardBackend
    {
    public:
        SDL_Keymod mod = SDL_KMOD_NONE;
        SDL_Keymod GetModState() override { return mod; }
    };

    class KeyboardModStateEXTTest : public ::testing::Test
    {
    protected:
        FakeSystemKeyboardBackend fake;
        void SetUp() override { SetSystemKeyboardBackendForTests(&fake); }
        void TearDown() override { SetSystemKeyboardBackendForTests(nullptr); }

        static bool has(KeyModifiersEXT set, KeyModifiersEXT flag)
        {
            return (set & flag) == flag;
        }
    };
}

TEST_F(KeyboardModStateEXTTest, NoModifiersMapsToNone)
{
    fake.mod = SDL_KMOD_NONE;
    EXPECT_EQ(Keyboard::GetModStateEXT(), KeyModifiersEXT::None);
}

// Each SDL modifier/lock bit maps to its KeyModifiersEXT flag, and the left/right variants of the
// four held modifiers collapse onto a single flag.
TEST_F(KeyboardModStateEXTTest, EachSdlModifierMapsToItsFlag)
{
    struct Case { SDL_Keymod sdl; KeyModifiersEXT flag; };
    const Case cases[] = {
        {SDL_KMOD_LSHIFT, KeyModifiersEXT::Shift},
        {SDL_KMOD_RSHIFT, KeyModifiersEXT::Shift},
        {SDL_KMOD_LCTRL,  KeyModifiersEXT::Ctrl},
        {SDL_KMOD_RCTRL,  KeyModifiersEXT::Ctrl},
        {SDL_KMOD_LALT,   KeyModifiersEXT::Alt},
        {SDL_KMOD_RALT,   KeyModifiersEXT::Alt},
        {SDL_KMOD_LGUI,   KeyModifiersEXT::Gui},
        {SDL_KMOD_RGUI,   KeyModifiersEXT::Gui},
        {SDL_KMOD_CAPS,   KeyModifiersEXT::Caps},
        {SDL_KMOD_NUM,    KeyModifiersEXT::Num},
        {SDL_KMOD_SCROLL, KeyModifiersEXT::Scroll},
        {SDL_KMOD_MODE,   KeyModifiersEXT::Mode},
    };
    for (const Case& c : cases)
    {
        fake.mod = c.sdl;
        EXPECT_EQ(Keyboard::GetModStateEXT(), c.flag);
    }
}

// A composite SDL mod state produces every corresponding flag and none of the others.
TEST_F(KeyboardModStateEXTTest, CombinedModifiersProduceCombinedFlags)
{
    fake.mod = static_cast<SDL_Keymod>(SDL_KMOD_LCTRL | SDL_KMOD_RSHIFT | SDL_KMOD_CAPS);
    const KeyModifiersEXT result = Keyboard::GetModStateEXT();

    EXPECT_TRUE(has(result, KeyModifiersEXT::Ctrl));
    EXPECT_TRUE(has(result, KeyModifiersEXT::Shift));
    EXPECT_TRUE(has(result, KeyModifiersEXT::Caps));

    EXPECT_FALSE(has(result, KeyModifiersEXT::Alt));
    EXPECT_FALSE(has(result, KeyModifiersEXT::Gui));
    EXPECT_FALSE(has(result, KeyModifiersEXT::Num));
    EXPECT_FALSE(has(result, KeyModifiersEXT::Scroll));
    EXPECT_FALSE(has(result, KeyModifiersEXT::Mode));
}
