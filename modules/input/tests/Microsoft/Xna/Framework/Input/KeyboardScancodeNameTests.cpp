// SPDX-License-Identifier: MS-PL
#include <gtest/gtest.h>

#include "Microsoft/Xna/Framework/Input/Keyboard.hpp"
#include "Microsoft/Xna/Framework/Input/Keys.hpp"

#include <string>

using Microsoft::Xna::Framework::Input::Keyboard;
using Microsoft::Xna::Framework::Input::Keys;

// SDL scancode names are static, layout-independent English strings and need no video subsystem, so
// these are fully deterministic headless.

TEST(KeyboardScancodeNameEXTTest, KnownKeysHaveStableEnglishNames)
{
    EXPECT_EQ(Keyboard::GetScancodeNameEXT(Keys::A), "A");
    EXPECT_EQ(Keyboard::GetScancodeNameEXT(Keys::Z), "Z");
    EXPECT_EQ(Keyboard::GetScancodeNameEXT(Keys::Space), "Space");
}

TEST(KeyboardScancodeNameEXTTest, UnmappedKeyHasEmptyName)
{
    // Keys::None has no physical key, so it has no scancode and therefore no name.
    EXPECT_EQ(Keyboard::GetScancodeNameEXT(Keys::None), "");
}

TEST(KeyboardScancodeNameEXTTest, NameToKeyReversesKeyToName)
{
    const Keys keys[] = {
        Keys::A, Keys::B, Keys::Z, Keys::Space, Keys::Enter,
        Keys::Left, Keys::Right, Keys::Up, Keys::Down, Keys::Escape,
    };
    for (const Keys k : keys)
    {
        const std::string name = Keyboard::GetScancodeNameEXT(k);
        ASSERT_FALSE(name.empty()) << "expected a physical name for key " << static_cast<int>(k);
        EXPECT_EQ(Keyboard::GetScancodeFromNameEXT(name), k) << "round-trip failed for \"" << name << "\"";
    }
}

TEST(KeyboardScancodeNameEXTTest, UnrecognizedNameYieldsNone)
{
    EXPECT_EQ(Keyboard::GetScancodeFromNameEXT("Not A Real Key"), Keys::None);
    EXPECT_EQ(Keyboard::GetScancodeFromNameEXT(""), Keys::None);
}
