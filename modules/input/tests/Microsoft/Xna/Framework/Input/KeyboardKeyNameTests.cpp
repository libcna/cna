// SPDX-License-Identifier: MS-PL
#include <gtest/gtest.h>

#include "Microsoft/Xna/Framework/Input/Keyboard.hpp"
#include "Microsoft/Xna/Framework/Input/Keys.hpp"

#include <SDL3/SDL.h>

#include <string>

using Microsoft::Xna::Framework::Input::Keyboard;
using Microsoft::Xna::Framework::Input::Keys;

// GetKeyNameEXT is layout-dependent: it resolves scancode -> keycode through the active keymap, which
// SDL only establishes once the video subsystem is up. Initialize it (Xvfb+x11 in CI) and skip if a
// display is unavailable, mirroring the MouseCursor / Clipboard tests. CI runs a US layout, so the
// basic Latin keys resolve to their ASCII names.
namespace
{
    class KeyboardKeyNameEXTTest : public ::testing::Test
    {
    protected:
        bool videoUp_ = false;
        void SetUp() override
        {
            videoUp_ = SDL_InitSubSystem(SDL_INIT_VIDEO);
            if (!videoUp_)
                GTEST_SKIP() << "SDL_InitSubSystem(SDL_INIT_VIDEO) failed (no display): " << SDL_GetError();
        }
        void TearDown() override
        {
            if (videoUp_)
                SDL_QuitSubSystem(SDL_INIT_VIDEO);
        }
    };
}

TEST_F(KeyboardKeyNameEXTTest, BasicKeysHaveExpectedNamesOnUsLayout)
{
    EXPECT_EQ(Keyboard::GetKeyNameEXT(Keys::A), "A");
    EXPECT_EQ(Keyboard::GetKeyNameEXT(Keys::Z), "Z");
    EXPECT_EQ(Keyboard::GetKeyNameEXT(Keys::Space), "Space");
}

TEST_F(KeyboardKeyNameEXTTest, UnmappedKeyHasEmptyName)
{
    EXPECT_EQ(Keyboard::GetKeyNameEXT(Keys::None), "");
}

TEST_F(KeyboardKeyNameEXTTest, NameToKeyReversesKeyToName)
{
    const Keys keys[] = {
        Keys::A, Keys::B, Keys::Z, Keys::Space, Keys::Enter,
        Keys::Left, Keys::Right, Keys::Up, Keys::Down, Keys::Escape,
    };
    for (const Keys k : keys)
    {
        const std::string name = Keyboard::GetKeyNameEXT(k);
        ASSERT_FALSE(name.empty()) << "expected a name for key " << static_cast<int>(k);
        EXPECT_EQ(Keyboard::GetKeyFromNameEXT(name), k) << "round-trip failed for \"" << name << "\"";
    }
}

TEST_F(KeyboardKeyNameEXTTest, UnrecognizedNameYieldsNone)
{
    EXPECT_EQ(Keyboard::GetKeyFromNameEXT("Not A Real Key"), Keys::None);
    EXPECT_EQ(Keyboard::GetKeyFromNameEXT(""), Keys::None);
}
