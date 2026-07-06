// SPDX-License-Identifier: MS-PL
#include <gtest/gtest.h>

#include "Microsoft/Xna/Framework/Input/GamePad.hpp"
#include "Microsoft/Xna/Framework/Input/GamePadState.hpp"

using Microsoft::Xna::Framework::Vector2;
using namespace Microsoft::Xna::Framework::Input;

TEST(GamePadStateTest, DefaultConstructorProducesDisconnectedStateAtRest)
{
    const GamePadState state;

    EXPECT_FALSE(state.getIsConnectedProperty());
    EXPECT_EQ(state.getPacketNumberProperty(), 0);
    EXPECT_FALSE(state.IsButtonDown(Buttons::A));
    EXPECT_TRUE(state.IsButtonUp(Buttons::A));
    EXPECT_EQ(state.getThumbSticksProperty().getLeftProperty(), Vector2::Zero);
    EXPECT_FLOAT_EQ(state.getTriggersProperty().getLeftProperty(), 0.0f);
}

TEST(GamePadStateTest, FourArgConstructorMarksConnectedAndPacksExplicitButtons)
{
    const GamePadThumbSticks sticks(Vector2::Zero, Vector2::Zero);
    const GamePadTriggers triggers(0.0f, 0.0f);
    const GamePadButtons buttons(Buttons::A | Buttons::Start);
    const GamePadDPad dpad;

    const GamePadState state(sticks, triggers, buttons, dpad);

    EXPECT_TRUE(state.getIsConnectedProperty());
    EXPECT_TRUE(state.IsButtonDown(Buttons::A));
    EXPECT_TRUE(state.IsButtonDown(Buttons::Start));
    EXPECT_FALSE(state.IsButtonDown(Buttons::B));
}

TEST(GamePadStateTest, FourArgConstructorPacksTriggersPastThresholdAsButtons)
{
    const GamePadThumbSticks sticks(Vector2::Zero, Vector2::Zero);
    const GamePadTriggers triggers(GamePad::TriggerThreshold + 0.1f, 0.0f);
    const GamePadButtons buttons(static_cast<Buttons>(0));
    const GamePadDPad dpad;

    const GamePadState state(sticks, triggers, buttons, dpad);

    EXPECT_TRUE(state.IsButtonDown(Buttons::LeftTrigger));
    EXPECT_FALSE(state.IsButtonDown(Buttons::RightTrigger));
}

TEST(GamePadStateTest, FourArgConstructorPacksThumbstickDirectionsAsButtons)
{
    const GamePadThumbSticks sticks(
        Vector2(GamePad::LeftDeadZone + 0.1f, 0.0f),
        Vector2(0.0f, -(GamePad::RightDeadZone + 0.1f)));
    const GamePadTriggers triggers(0.0f, 0.0f);
    const GamePadButtons buttons(static_cast<Buttons>(0));
    const GamePadDPad dpad;

    const GamePadState state(sticks, triggers, buttons, dpad);

    EXPECT_TRUE(state.IsButtonDown(Buttons::LeftThumbstickRight));
    EXPECT_FALSE(state.IsButtonDown(Buttons::LeftThumbstickLeft));
    EXPECT_TRUE(state.IsButtonDown(Buttons::RightThumbstickDown));
    EXPECT_FALSE(state.IsButtonDown(Buttons::RightThumbstickUp));
}

TEST(GamePadStateTest, FiveArgConstructorBuildsEquivalentPackedState)
{
    const GamePadState state(
        Vector2(GamePad::LeftDeadZone + 0.1f, 0.0f),
        Vector2::Zero,
        GamePad::TriggerThreshold + 0.1f,
        0.0f,
        {Buttons::Start, Buttons::DPadUp});

    EXPECT_TRUE(state.getIsConnectedProperty());
    EXPECT_TRUE(state.IsButtonDown(Buttons::Start));
    EXPECT_EQ(state.getDPadProperty().getUpProperty(), ButtonState::Pressed);
    EXPECT_TRUE(state.IsButtonDown(Buttons::LeftTrigger));
    EXPECT_TRUE(state.IsButtonDown(Buttons::LeftThumbstickRight));
}

TEST(GamePadStateTest, IsButtonDownRequiresAllRequestedFlagsToBePressed)
{
    const GamePadState state(Vector2::Zero, Vector2::Zero, 0.0f, 0.0f, {Buttons::A});

    EXPECT_TRUE(state.IsButtonDown(Buttons::A));
    EXPECT_FALSE(state.IsButtonDown(Buttons::A | Buttons::B));
    EXPECT_TRUE(state.IsButtonUp(Buttons::A | Buttons::B));
}

// A4-006: dedicated IsButtonUp coverage. FNA IsButtonUp(b) == `(buttons & b) != b` — true UNLESS every
// requested bit is set (NOT simply "all up"). So a partially-down combined query is Up. CNA is byte-identical.
TEST(GamePadStateTest, IsButtonUpIsTrueUnlessAllRequestedButtonsAreDown)
{
    const GamePadState aDown(Vector2::Zero, Vector2::Zero, 0.0f, 0.0f, {Buttons::A});
    EXPECT_FALSE(aDown.IsButtonUp(Buttons::A));               // A is down -> not up
    EXPECT_TRUE(aDown.IsButtonUp(Buttons::B));                // B unpressed -> up
    EXPECT_TRUE(aDown.IsButtonUp(Buttons::A | Buttons::B));   // A down but B up -> not all down -> up

    const GamePadState bothDown(Vector2::Zero, Vector2::Zero, 0.0f, 0.0f, {Buttons::A, Buttons::B});
    EXPECT_FALSE(bothDown.IsButtonUp(Buttons::A | Buttons::B)); // all requested down -> not up
    EXPECT_FALSE(bothDown.IsButtonUp(Buttons::A));
}

TEST(GamePadStateTest, EqualityOperatorsForEqualAndDifferingInstances)
{
    const GamePadState a(Vector2::Zero, Vector2::Zero, 0.0f, 0.0f, {Buttons::A});
    const GamePadState sameAsA(Vector2::Zero, Vector2::Zero, 0.0f, 0.0f, {Buttons::A});
    const GamePadState differentButtons(Vector2::Zero, Vector2::Zero, 0.0f, 0.0f, {Buttons::B});
    const GamePadState disconnected;

    EXPECT_TRUE(a.Equals(sameAsA));
    EXPECT_TRUE(a == sameAsA);
    EXPECT_FALSE(a != sameAsA);

    EXPECT_FALSE(a.Equals(differentButtons));
    EXPECT_TRUE(a != differentButtons);
    EXPECT_FALSE(a == differentButtons);

    EXPECT_FALSE(a.Equals(disconnected));
    EXPECT_TRUE(a != disconnected);
}

TEST(GamePadStateTest, EqualityConsidersPacketNumber)
{
    GamePadState a(Vector2::Zero, Vector2::Zero, 0.0f, 0.0f, {Buttons::A});
    GamePadState differentPacket(Vector2::Zero, Vector2::Zero, 0.0f, 0.0f, {Buttons::A});
    differentPacket.setPacketNumberProperty(5);

    EXPECT_FALSE(a.Equals(differentPacket));
    EXPECT_TRUE(a != differentPacket);
}

TEST(GamePadStateTest, GetHashCodeMatchesButtonsHashXorPacketFormula)
{
    GamePadState state(Vector2::Zero, Vector2::Zero, 0.0f, 0.0f, {Buttons::A});
    state.setPacketNumberProperty(3);

    const int expected = state.getButtonsProperty().GetHashCode() ^ (3 * 31);
    EXPECT_EQ(state.GetHashCode(), expected);

    GamePadState sameAsState(Vector2::Zero, Vector2::Zero, 0.0f, 0.0f, {Buttons::A});
    sameAsState.setPacketNumberProperty(3);
    EXPECT_EQ(state.GetHashCode(), sameAsState.GetHashCode());
}

TEST(GamePadStateTest, ToStringReturnsFullyQualifiedTypeNameRegardlessOfState)
{
    const GamePadState disconnected;
    const GamePadState connected(Vector2::Zero, Vector2::Zero, 0.0f, 0.0f, {Buttons::A});

    EXPECT_EQ(disconnected.ToString(), "Microsoft.Xna.Framework.Input.GamePadState");
    EXPECT_EQ(connected.ToString(), "Microsoft.Xna.Framework.Input.GamePadState");
}
