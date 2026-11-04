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

TEST(GamePadButtonsTest, Tq027GetHashCodeXorsXnaFieldsAndSubstitutesZero)
{
    // XNA GamePadButtons.GetHashCode IL_0000..0010 calls Helpers.SmartGetHashCode:
    // IL_0028..0048 XORs eleven int32 ButtonState fields, replacing zero with INT_MAX.
    const GamePadButtons buttons(Buttons::A | Buttons::Start);
    const GamePadButtons sameAsButtons(Buttons::A | Buttons::Start);
    EXPECT_EQ(buttons.GetHashCode(), 0x7fffffff); // 1 ^ 1 = 0 -> INT_MAX.
    EXPECT_EQ(buttons.GetHashCode(), sameAsButtons.GetHashCode());
    EXPECT_EQ(GamePadButtons().GetHashCode(), 0x7fffffff);
    for (const auto flag : {Buttons::A, Buttons::B, Buttons::X, Buttons::Y,
                           Buttons::Start, Buttons::Back, Buttons::LeftStick, Buttons::RightStick,
                           Buttons::LeftShoulder, Buttons::RightShoulder, Buttons::BigButton})
    {
        SCOPED_TRACE(static_cast<uint32_t>(flag));
        EXPECT_EQ(GamePadButtons(flag).GetHashCode(), 1);
    }
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

TEST(GamePadDPadTest, Tq027GetHashCodeXorsDirectionsAndSubstitutesZero)
{
    // XNA GamePadDPad.GetHashCode IL_0000..0010 + SmartGetHashCode IL_0028..0048.
    const GamePadDPad upAndLeft(ButtonState::Pressed, ButtonState::Released,
                              ButtonState::Pressed, ButtonState::Released);
    EXPECT_EQ(upAndLeft.GetHashCode(), 0x7fffffff); // 1 ^ 1 = 0 -> INT_MAX.
    const GamePadDPad none;
    EXPECT_EQ(none.GetHashCode(), 0x7fffffff);
    const GamePadDPad all(ButtonState::Pressed, ButtonState::Pressed,
                        ButtonState::Pressed, ButtonState::Pressed);
    EXPECT_EQ(all.GetHashCode(), 0x7fffffff);
    for (const auto flag : {Buttons::DPadUp, Buttons::DPadDown, Buttons::DPadLeft, Buttons::DPadRight})
        EXPECT_EQ(GamePadDPad::FromButtonArray({flag}).GetHashCode(), 1);
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


TEST(GamePadButtonsTest, Tq025EveryPhysicalButtonIsIsolated)
{
    // XNA GamePadButtons ctor IL_0000..00f6 extracts each physical bit independently.
    // Getter order below is the XNA ToString IL_0000..01f2 order, with literal singletons.
    using B = GamePadButtons;
    struct Entry { Buttons flag; ButtonState (B::*get)() const; const char* text; };
    const Entry entries[] = {
        {Buttons::A, &B::getAProperty, "{Buttons:A}"},
        {Buttons::B, &B::getBProperty, "{Buttons:B}"},
        {Buttons::X, &B::getXProperty, "{Buttons:X}"},
        {Buttons::Y, &B::getYProperty, "{Buttons:Y}"},
        {Buttons::LeftShoulder, &B::getLeftShoulderProperty, "{Buttons:LeftShoulder}"},
        {Buttons::RightShoulder, &B::getRightShoulderProperty, "{Buttons:RightShoulder}"},
        {Buttons::LeftStick, &B::getLeftStickProperty, "{Buttons:LeftStick}"},
        {Buttons::RightStick, &B::getRightStickProperty, "{Buttons:RightStick}"},
        {Buttons::Start, &B::getStartProperty, "{Buttons:Start}"},
        {Buttons::Back, &B::getBackProperty, "{Buttons:Back}"},
        {Buttons::BigButton, &B::getBigButtonProperty, "{Buttons:BigButton}"}};
    for (const auto& selected : entries)
    {
        SCOPED_TRACE(selected.text);
        const B value(selected.flag);
        EXPECT_EQ(value.ToString(), selected.text);
        for (const auto& observed : entries)
            EXPECT_EQ((value.*observed.get)(), selected.flag == observed.flag
                ? ButtonState::Pressed : ButtonState::Released);
        EXPECT_FALSE(value.Equals(B{}));
        EXPECT_FALSE(value == B{});
        EXPECT_TRUE(value != B{});
        const B copy = B::FromButtonArray({selected.flag, selected.flag});
        EXPECT_TRUE(value.Equals(copy));
        EXPECT_EQ(value.GetHashCode(), copy.GetHashCode());
    }
}

TEST(GamePadDPadTest, Tq025EveryDirectionIsIsolatedInEqualityAndText)
{
    // XNA GamePadDPad.op_Equality IL_0000..0042 compares all four directions;
    // ToString emits the pressed direction (or None), not an enum numeric value.
    const auto off = ButtonState::Released;
    const auto on = ButtonState::Pressed;
    const GamePadDPad values[] = {{on, off, off, off}, {off, on, off, off},
                                  {off, off, on, off}, {off, off, off, on}};
    const char* texts[] = {"{DPad:Up}", "{DPad:Down}", "{DPad:Left}", "{DPad:Right}"};
    for (unsigned i = 0; i < 4; ++i)
    {
        EXPECT_EQ(values[i].ToString(), texts[i]);
        EXPECT_FALSE(values[i].Equals(GamePadDPad{}));
        EXPECT_FALSE(values[i] == GamePadDPad{});
        EXPECT_TRUE(values[i] != GamePadDPad{});
        for (unsigned j = 0; j < 4; ++j)
        {
            EXPECT_EQ(values[i].Equals(values[j]), i == j);
            EXPECT_EQ(values[i] == values[j], i == j);
            EXPECT_EQ(values[i] != values[j], i != j);
        }
    }
}

TEST(GamePadButtonsTest, Tq025DPadAndVirtualFlagsDoNotAffectPhysicalButtonValue)
{
    // XNA GamePadButtons ctor IL_0000..00f6 extracts eleven physical button fields only.
    const GamePadButtons physical(Buttons::A);
    for (const auto flag : {Buttons::DPadUp, Buttons::DPadDown, Buttons::DPadLeft,
                            Buttons::DPadRight, Buttons::LeftTrigger, Buttons::RightTrigger,
                            Buttons::LeftThumbstickLeft, Buttons::LeftThumbstickRight,
                            Buttons::LeftThumbstickUp, Buttons::LeftThumbstickDown,
                            Buttons::RightThumbstickLeft, Buttons::RightThumbstickRight,
                            Buttons::RightThumbstickUp, Buttons::RightThumbstickDown})
    {
        const GamePadButtons mixed(Buttons::A | flag);
        EXPECT_TRUE(physical.Equals(mixed));
        EXPECT_TRUE(physical == mixed);
        EXPECT_FALSE(physical != mixed);
        EXPECT_EQ(physical.GetHashCode(), mixed.GetHashCode());
        EXPECT_EQ(mixed.ToString(), "{Buttons:A}");
    }
}
