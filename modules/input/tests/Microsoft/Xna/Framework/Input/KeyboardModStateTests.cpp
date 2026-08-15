// SPDX-License-Identifier: MS-PL
#include <gtest/gtest.h>

#include "CNA/Input/KeyModifiers.hpp"
#include "CNA/Platform/CannedKeyboard.hpp"
#include "Microsoft/Xna/Framework/Input/Keyboard.hpp"

#include <memory>

using CNA::Input::KeyModifiersEXT;
using CNA::Platform::KeyCode;
using CNA::Platform::KeyModifier;
using CNA::Platform::Testing::CannedKeyboardPlatform;
using CNA::Platform::Testing::ScopedCurrentPlatform;
using Microsoft::Xna::Framework::Input::Keyboard;
using Microsoft::Xna::Framework::Input::Keys;

namespace
{
    [[nodiscard]] constexpr std::uint16_t Bit(const KeyModifier modifier)
    {
        return static_cast<std::uint16_t>(modifier);
    }

    class KeyboardModStateEXTTest : public ::testing::Test
    {
    protected:
        CannedKeyboardPlatform platform;
        std::unique_ptr<ScopedCurrentPlatform> installed;

        void SetUp() override
        {
            installed = std::make_unique<ScopedCurrentPlatform>(platform);
        }

        void TearDown() override { installed.reset(); }

        static bool Has(const KeyModifiersEXT set, const KeyModifiersEXT flag)
        {
            return (set & flag) == flag;
        }
    };
}

TEST_F(KeyboardModStateEXTTest, NoModifiersMapsToNone)
{
    platform.Canned().SetPending({});
    platform.Canned().Update();
    EXPECT_EQ(Keyboard::GetModStateEXT(), KeyModifiersEXT::None);
}

TEST_F(KeyboardModStateEXTTest, MissingKeyboardServiceReturnsNone)
{
    platform.Canned().SetPending({}, Bit(KeyModifier::Shift));
    platform.Canned().Update();
    platform.SetKeyboardAvailable(false);

    EXPECT_EQ(Keyboard::GetModStateEXT(), KeyModifiersEXT::None);
}

TEST_F(KeyboardModStateEXTTest, EachPlatformModifierMapsToItsPublicFlag)
{
    struct Case
    {
        KeyModifier platform;
        KeyModifiersEXT input;
    };
    constexpr Case cases[] = {
        {KeyModifier::Shift, KeyModifiersEXT::Shift},
        {KeyModifier::Control, KeyModifiersEXT::Ctrl},
        {KeyModifier::Alt, KeyModifiersEXT::Alt},
        {KeyModifier::Gui, KeyModifiersEXT::Gui},
        {KeyModifier::CapsLock, KeyModifiersEXT::Caps},
        {KeyModifier::NumLock, KeyModifiersEXT::Num},
        {KeyModifier::ScrollLock, KeyModifiersEXT::Scroll},
        {KeyModifier::Mode, KeyModifiersEXT::Mode},
    };

    for (const Case& testCase : cases)
    {
        platform.Canned().SetPending({}, Bit(testCase.platform));
        platform.Canned().Update();
        EXPECT_EQ(Keyboard::GetModStateEXT(), testCase.input);
    }
}

TEST_F(KeyboardModStateEXTTest, CombinedModifiersProduceCombinedFlags)
{
    platform.Canned().SetPending(
        {}, Bit(KeyModifier::Control) | Bit(KeyModifier::Shift) | Bit(KeyModifier::CapsLock));
    platform.Canned().Update();
    const KeyModifiersEXT result = Keyboard::GetModStateEXT();

    EXPECT_TRUE(Has(result, KeyModifiersEXT::Ctrl));
    EXPECT_TRUE(Has(result, KeyModifiersEXT::Shift));
    EXPECT_TRUE(Has(result, KeyModifiersEXT::Caps));
    EXPECT_FALSE(Has(result, KeyModifiersEXT::Alt));
    EXPECT_FALSE(Has(result, KeyModifiersEXT::Gui));
    EXPECT_FALSE(Has(result, KeyModifiersEXT::Num));
    EXPECT_FALSE(Has(result, KeyModifiersEXT::Scroll));
    EXPECT_FALSE(Has(result, KeyModifiersEXT::Mode));
}

TEST_F(KeyboardModStateEXTTest, KeysAndModifiersAdvanceOnTheSameUpdate)
{
    platform.Canned().SetPending({KeyCode::A}, Bit(KeyModifier::Control));
    platform.Canned().Update();
    ASSERT_TRUE(Keyboard::GetState().IsKeyDown(Keys::A));
    ASSERT_EQ(Keyboard::GetModStateEXT(), KeyModifiersEXT::Ctrl);

    // Merely changing the host-side state must not let the formerly-live modifier query race
    // ahead of the per-frame key snapshot.
    platform.Canned().SetPending({KeyCode::B}, Bit(KeyModifier::Shift));
    EXPECT_TRUE(Keyboard::GetState().IsKeyDown(Keys::A));
    EXPECT_TRUE(Keyboard::GetState().IsKeyUp(Keys::B));
    EXPECT_EQ(Keyboard::GetModStateEXT(), KeyModifiersEXT::Ctrl);

    platform.Canned().Update();
    EXPECT_TRUE(Keyboard::GetState().IsKeyUp(Keys::A));
    EXPECT_TRUE(Keyboard::GetState().IsKeyDown(Keys::B));
    EXPECT_EQ(Keyboard::GetModStateEXT(), KeyModifiersEXT::Shift);
}
