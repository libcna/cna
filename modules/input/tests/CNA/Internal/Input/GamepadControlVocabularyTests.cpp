// SPDX-License-Identifier: MS-PL

#include "CNA/Internal/Input/InputManager.hpp"
#include "CNA/Platform/PlatformEvent.hpp"
#include "Microsoft/Xna/Framework/Input/Buttons.hpp"

#include <gtest/gtest.h>

#include <cstdint>

namespace {

using CNA::Internal::Input::GamePadAxis;
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

TEST(GamepadControlVocabularyTests, PlatformAxisValuesMatchTheInputManagersVocabulary)
{
    EXPECT_TRUE(SameValue(GamepadAxis::LeftThumbstickX, GamePadAxis::LeftThumbstickX));
    EXPECT_TRUE(SameValue(GamepadAxis::LeftThumbstickY, GamePadAxis::LeftThumbstickY));
    EXPECT_TRUE(SameValue(GamepadAxis::RightThumbstickX, GamePadAxis::RightThumbstickX));
    EXPECT_TRUE(SameValue(GamepadAxis::RightThumbstickY, GamePadAxis::RightThumbstickY));
    EXPECT_TRUE(SameValue(GamepadAxis::LeftTrigger, GamePadAxis::LeftTrigger));
    EXPECT_TRUE(SameValue(GamepadAxis::RightTrigger, GamePadAxis::RightTrigger));
}

} // namespace
