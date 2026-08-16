// SPDX-License-Identifier: MS-PL

#include "CNA/Platform/PlatformEvent.hpp"
#include "Microsoft/Xna/Framework/Input/Buttons.hpp"

#include <gtest/gtest.h>

#include <cstdint>

namespace {

using CNA::Platform::GamepadAxis;
using CNA::Platform::GamepadButton;
using Microsoft::Xna::Framework::Input::Buttons;

template <typename Left, typename Right>
constexpr bool SameValue(const Left left, const Right right)
{
    return static_cast<std::uint32_t>(left) == static_cast<std::uint32_t>(right);
}

TEST(GamepadControlVocabularyTests, PlatformButtonValuesMatchXnaButtons)
{
    EXPECT_TRUE(SameValue(GamepadButton::A, Buttons::A));
    EXPECT_TRUE(SameValue(GamepadButton::B, Buttons::B));
    EXPECT_TRUE(SameValue(GamepadButton::X, Buttons::X));
    EXPECT_TRUE(SameValue(GamepadButton::Y, Buttons::Y));
    EXPECT_TRUE(SameValue(GamepadButton::Back, Buttons::Back));
    EXPECT_TRUE(SameValue(GamepadButton::Start, Buttons::Start));
    EXPECT_TRUE(SameValue(GamepadButton::LeftShoulder, Buttons::LeftShoulder));
    EXPECT_TRUE(SameValue(GamepadButton::RightShoulder, Buttons::RightShoulder));
    EXPECT_TRUE(SameValue(GamepadButton::LeftStick, Buttons::LeftStick));
    EXPECT_TRUE(SameValue(GamepadButton::RightStick, Buttons::RightStick));
    EXPECT_TRUE(SameValue(GamepadButton::DPadUp, Buttons::DPadUp));
    EXPECT_TRUE(SameValue(GamepadButton::DPadDown, Buttons::DPadDown));
    EXPECT_TRUE(SameValue(GamepadButton::DPadLeft, Buttons::DPadLeft));
    EXPECT_TRUE(SameValue(GamepadButton::DPadRight, Buttons::DPadRight));
    EXPECT_TRUE(SameValue(GamepadButton::BigButton, Buttons::BigButton));
    EXPECT_TRUE(SameValue(GamepadButton::Misc1, Buttons::Misc1EXT));
    EXPECT_TRUE(SameValue(GamepadButton::Paddle1, Buttons::Paddle1EXT));
    EXPECT_TRUE(SameValue(GamepadButton::Paddle2, Buttons::Paddle2EXT));
    EXPECT_TRUE(SameValue(GamepadButton::Paddle3, Buttons::Paddle3EXT));
    EXPECT_TRUE(SameValue(GamepadButton::Paddle4, Buttons::Paddle4EXT));
    EXPECT_TRUE(SameValue(GamepadButton::TouchPad, Buttons::TouchPadEXT));
}

TEST(GamepadControlVocabularyTests, PlatformAxisValuesMatchSnapshotArrayOrder)
{
    EXPECT_EQ(static_cast<std::uint32_t>(GamepadAxis::LeftThumbstickX), 0u);
    EXPECT_EQ(static_cast<std::uint32_t>(GamepadAxis::LeftThumbstickY), 1u);
    EXPECT_EQ(static_cast<std::uint32_t>(GamepadAxis::RightThumbstickX), 2u);
    EXPECT_EQ(static_cast<std::uint32_t>(GamepadAxis::RightThumbstickY), 3u);
    EXPECT_EQ(static_cast<std::uint32_t>(GamepadAxis::LeftTrigger), 4u);
    EXPECT_EQ(static_cast<std::uint32_t>(GamepadAxis::RightTrigger), 5u);
}

} // namespace
