// SPDX-License-Identifier: MS-PL
#include <gtest/gtest.h>

#include "Microsoft/Xna/Framework/Input/Keyboard.hpp"
#include "Microsoft/Xna/Framework/Input/Keys.hpp"
#include "CNA/Platform/CurrentPlatform.hpp"
#include "CNA/Platform/PlatformException.hpp"

#include <string>

using Microsoft::Xna::Framework::Input::Keyboard;
using Microsoft::Xna::Framework::Input::Keys;

// GetKeyNameEXT is layout-dependent: it resolves scancode -> keycode through the active keymap, which
// the window platform establishes once its video subsystem is up. Acquire it through the platform
// contract and skip if a display is unavailable. CI runs a US layout, so basic Latin keys resolve
// to their ASCII names.
namespace
{
    class KeyboardKeyNameEXTTest : public ::testing::Test
    {
    protected:
        bool videoUp_ = false;
        void SetUp() override
        {
            try
            {
                CNA::Platform::GetCurrentPlatform().AcquireSubsystem(
                    CNA::Platform::PlatformSubsystem::Video);
                videoUp_ = true;
            }
            catch (const CNA::Platform::PlatformException& error)
            {
                GTEST_SKIP() << "video subsystem unavailable: " << error.what();
            }
        }
        void TearDown() override
        {
            if (videoUp_)
                CNA::Platform::GetCurrentPlatform().ReleaseSubsystem(
                    CNA::Platform::PlatformSubsystem::Video);
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
