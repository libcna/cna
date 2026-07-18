// SPDX-License-Identifier: MS-PL
#include <gtest/gtest.h>

#include "Microsoft/Xna/Framework/Input/GamePadButtons.hpp"
#include "Microsoft/Xna/Framework/Input/GamePadDPad.hpp"

using namespace Microsoft::Xna::Framework::Input;

// --- GamePadButtons ---

TEST(GamePadButtonsTest, DefaultConstructorHasAllButtonsReleased)
{
    const GamePadButtons buttons;

    EXPECT_EQ(buttons.getAProperty(), ButtonState::Released);
    EXPECT_EQ(buttons.getBProperty(), ButtonState::Released);
    EXPECT_EQ(buttons.getBackProperty(), ButtonState::Released);
    EXPECT_EQ(buttons.getXProperty(), ButtonState::Released);
    EXPECT_EQ(buttons.getYProperty(), ButtonState::Released);
    EXPECT_EQ(buttons.getStartProperty(), ButtonState::Released);
    EXPECT_EQ(buttons.getLeftShoulderProperty(), ButtonState::Released);
    EXPECT_EQ(buttons.getLeftStickProperty(), ButtonState::Released);
    EXPECT_EQ(buttons.getRightShoulderProperty(), ButtonState::Released);
    EXPECT_EQ(buttons.getRightStickProperty(), ButtonState::Released);
    EXPECT_EQ(buttons.getBigButtonProperty(), ButtonState::Released);
}

TEST(GamePadButtonsTest, ConstructorFromCombinedFlagsSetsEveryMatchingGetter)
{
    const Buttons all = Buttons::A | Buttons::B | Buttons::Back | Buttons::X | Buttons::Y |
                         Buttons::Start | Buttons::LeftShoulder | Buttons::LeftStick |
                         Buttons::RightShoulder | Buttons::RightStick | Buttons::BigButton;
    const GamePadButtons buttons(all);

    EXPECT_EQ(buttons.getAProperty(), ButtonState::Pressed);
    EXPECT_EQ(buttons.getBProperty(), ButtonState::Pressed);
    EXPECT_EQ(buttons.getBackProperty(), ButtonState::Pressed);
    EXPECT_EQ(buttons.getXProperty(), ButtonState::Pressed);
    EXPECT_EQ(buttons.getYProperty(), ButtonState::Pressed);
    EXPECT_EQ(buttons.getStartProperty(), ButtonState::Pressed);
    EXPECT_EQ(buttons.getLeftShoulderProperty(), ButtonState::Pressed);
    EXPECT_EQ(buttons.getLeftStickProperty(), ButtonState::Pressed);
    EXPECT_EQ(buttons.getRightShoulderProperty(), ButtonState::Pressed);
    EXPECT_EQ(buttons.getRightStickProperty(), ButtonState::Pressed);
    EXPECT_EQ(buttons.getBigButtonProperty(), ButtonState::Pressed);
}

TEST(GamePadButtonsTest, ConstructorFromSingleFlagLeavesOthersReleased)
{
    const GamePadButtons buttons(Buttons::X);

    EXPECT_EQ(buttons.getXProperty(), ButtonState::Pressed);
    EXPECT_EQ(buttons.getAProperty(), ButtonState::Released);
    EXPECT_EQ(buttons.getBProperty(), ButtonState::Released);
    EXPECT_EQ(buttons.getYProperty(), ButtonState::Released);
}

TEST(GamePadButtonsTest, FromButtonArrayCombinesMultipleFlagsAcrossElements)
{
    const GamePadButtons buttons = GamePadButtons::FromButtonArray({Buttons::A, Buttons::Start});

    EXPECT_EQ(buttons.getAProperty(), ButtonState::Pressed);
    EXPECT_EQ(buttons.getStartProperty(), ButtonState::Pressed);
    EXPECT_EQ(buttons.getBProperty(), ButtonState::Released);
}

TEST(GamePadButtonsTest, FromButtonArrayWithEmptyListLeavesAllButtonsReleased)
{
    const GamePadButtons buttons = GamePadButtons::FromButtonArray({});

    EXPECT_EQ(buttons.getAProperty(), ButtonState::Released);
    EXPECT_EQ(buttons.getStartProperty(), ButtonState::Released);
}

TEST(GamePadButtonsTest, EqualityOperatorsForEqualAndDifferingInstances)
{
    const GamePadButtons a(Buttons::A | Buttons::Start);
    const GamePadButtons sameAsA(Buttons::A | Buttons::Start);
    const GamePadButtons different(Buttons::B);

    EXPECT_TRUE(a.Equals(sameAsA));
    EXPECT_TRUE(a == sameAsA);
    EXPECT_FALSE(a != sameAsA);

    EXPECT_FALSE(a.Equals(different));
    EXPECT_TRUE(a != different);
    EXPECT_FALSE(a == different);
}

TEST(GamePadButtonsTest, GetHashCodeMatchesUnderlyingFlagsValueAndIsConsistent)
{
    const GamePadButtons buttons(Buttons::A | Buttons::Start);
    const GamePadButtons sameAsButtons(Buttons::A | Buttons::Start);

    EXPECT_EQ(buttons.GetHashCode(), static_cast<int>(Buttons::A | Buttons::Start));
    EXPECT_EQ(buttons.GetHashCode(), sameAsButtons.GetHashCode());
}

// --- GamePadDPad ---

TEST(GamePadDPadTest, DefaultConstructorHasAllDirectionsReleased)
{
    const GamePadDPad dpad;

    EXPECT_EQ(dpad.getUpProperty(), ButtonState::Released);
    EXPECT_EQ(dpad.getDownProperty(), ButtonState::Released);
    EXPECT_EQ(dpad.getLeftProperty(), ButtonState::Released);
    EXPECT_EQ(dpad.getRightProperty(), ButtonState::Released);
}

TEST(GamePadDPadTest, ExplicitConstructorSetsEachDirectionIndependently)
{
    const GamePadDPad dpad(ButtonState::Pressed, ButtonState::Released,
                            ButtonState::Pressed, ButtonState::Released);

    EXPECT_EQ(dpad.getUpProperty(), ButtonState::Pressed);
    EXPECT_EQ(dpad.getDownProperty(), ButtonState::Released);
    EXPECT_EQ(dpad.getLeftProperty(), ButtonState::Pressed);
    EXPECT_EQ(dpad.getRightProperty(), ButtonState::Released);
}

TEST(GamePadDPadTest, FromButtonArrayDerivesDirectionsFromCombinedFlags)
{
    const GamePadDPad dpad = GamePadDPad::FromButtonArray({Buttons::DPadUp, Buttons::DPadLeft});

    EXPECT_EQ(dpad.getUpProperty(), ButtonState::Pressed);
    EXPECT_EQ(dpad.getLeftProperty(), ButtonState::Pressed);
    EXPECT_EQ(dpad.getDownProperty(), ButtonState::Released);
    EXPECT_EQ(dpad.getRightProperty(), ButtonState::Released);
}

TEST(GamePadDPadTest, FromButtonArrayCombinesAcrossSeparateListElements)
{
    // Each list element is a distinct Buttons value; FromButtonArray must OR them all
    // together rather than only inspecting a single element.
    const GamePadDPad dpad = GamePadDPad::FromButtonArray({Buttons::DPadUp, Buttons::DPadDown});

    EXPECT_EQ(dpad.getUpProperty(), ButtonState::Pressed);
    EXPECT_EQ(dpad.getDownProperty(), ButtonState::Pressed);
}

TEST(GamePadDPadTest, FromButtonArrayWithEmptyListLeavesAllDirectionsReleased)
{
    const GamePadDPad dpad = GamePadDPad::FromButtonArray({});

    EXPECT_EQ(dpad.getUpProperty(), ButtonState::Released);
    EXPECT_EQ(dpad.getDownProperty(), ButtonState::Released);
    EXPECT_EQ(dpad.getLeftProperty(), ButtonState::Released);
    EXPECT_EQ(dpad.getRightProperty(), ButtonState::Released);
}

TEST(GamePadDPadTest, EqualityOperatorsForEqualAndDifferingInstances)
{
    const GamePadDPad a(ButtonState::Pressed, ButtonState::Released,
                        ButtonState::Released, ButtonState::Released);
    const GamePadDPad sameAsA(ButtonState::Pressed, ButtonState::Released,
                              ButtonState::Released, ButtonState::Released);
    const GamePadDPad different(ButtonState::Released, ButtonState::Pressed,
                                 ButtonState::Released, ButtonState::Released);

    EXPECT_TRUE(a.Equals(sameAsA));
    EXPECT_TRUE(a == sameAsA);
    EXPECT_FALSE(a != sameAsA);

    EXPECT_FALSE(a.Equals(different));
    EXPECT_TRUE(a != different);
    EXPECT_FALSE(a == different);
}

TEST(GamePadDPadTest, GetHashCodeMatchesFnaBitWeightedFormula)
{
    // FNA: (Down?1:0) + (Left?2:0) + (Right?4:0) + (Up?8:0).
    const GamePadDPad upAndLeft(ButtonState::Pressed, ButtonState::Released,
                                ButtonState::Pressed, ButtonState::Released);
    EXPECT_EQ(upAndLeft.GetHashCode(), 8 + 2);

    const GamePadDPad none(ButtonState::Released, ButtonState::Released,
                           ButtonState::Released, ButtonState::Released);
    EXPECT_EQ(none.GetHashCode(), 0);

    const GamePadDPad all(ButtonState::Pressed, ButtonState::Pressed,
                          ButtonState::Pressed, ButtonState::Pressed);
    EXPECT_EQ(all.GetHashCode(), 8 + 1 + 2 + 4);
}

TEST(GamePadDPadTest, GetHashCodeIsConsistentForEqualInstances)
{
    const GamePadDPad a(ButtonState::Pressed, ButtonState::Released,
                        ButtonState::Pressed, ButtonState::Released);
    const GamePadDPad sameAsA(ButtonState::Pressed, ButtonState::Released,
                              ButtonState::Pressed, ButtonState::Released);

    EXPECT_TRUE(a == sameAsA);
    EXPECT_EQ(a.GetHashCode(), sameAsA.GetHashCode());
}
