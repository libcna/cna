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

// P1-008: the Left-trigger case above only exercises `triggers.Left > GamePad.TriggerThreshold`
// (GamePadState.cs:94-97); this is the symmetric Right-trigger branch (GamePadState.cs:98-101),
// previously untested on its own.
TEST(GamePadStateTest, FourArgConstructorPacksRightTriggerPastThresholdAsButton)
{
    const GamePadThumbSticks sticks(Vector2::Zero, Vector2::Zero);
    const GamePadTriggers triggers(0.0f, GamePad::TriggerThreshold + 0.1f);
    const GamePadButtons buttons(static_cast<Buttons>(0));
    const GamePadDPad dpad;

    const GamePadState state(sticks, triggers, buttons, dpad);

    EXPECT_TRUE(state.IsButtonDown(Buttons::RightTrigger));
    EXPECT_FALSE(state.IsButtonDown(Buttons::LeftTrigger));
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

// P1-008: StickToButtons (GamePadState.cs:193-221) has 4 independent conditions per stick (X > dz,
// X < -dz, Y > dz, Y < -dz). The test above only ever drives LeftThumbstickRight and
// RightThumbstickDown true; these two tests drive the remaining 6 branches true at least once each.
TEST(GamePadStateTest, FourArgConstructorPacksLeftUpAndRightRightThumbstickDirectionBranches)
{
    const GamePadThumbSticks sticks(
        Vector2(0.0f, GamePad::LeftDeadZone + 0.1f),
        Vector2(GamePad::RightDeadZone + 0.1f, 0.0f));
    const GamePadTriggers triggers(0.0f, 0.0f);
    const GamePadButtons buttons(static_cast<Buttons>(0));
    const GamePadDPad dpad;

    const GamePadState state(sticks, triggers, buttons, dpad);

    EXPECT_TRUE(state.IsButtonDown(Buttons::LeftThumbstickUp));
    EXPECT_FALSE(state.IsButtonDown(Buttons::LeftThumbstickDown));
    EXPECT_TRUE(state.IsButtonDown(Buttons::RightThumbstickRight));
    EXPECT_FALSE(state.IsButtonDown(Buttons::RightThumbstickLeft));
}

TEST(GamePadStateTest, FourArgConstructorPacksLeftLeftDownAndRightLeftUpThumbstickDirectionBranches)
{
    const GamePadThumbSticks sticks(
        Vector2(-(GamePad::LeftDeadZone + 0.1f), -(GamePad::LeftDeadZone + 0.1f)),
        Vector2(-(GamePad::RightDeadZone + 0.1f), GamePad::RightDeadZone + 0.1f));
    const GamePadTriggers triggers(0.0f, 0.0f);
    const GamePadButtons buttons(static_cast<Buttons>(0));
    const GamePadDPad dpad;

    const GamePadState state(sticks, triggers, buttons, dpad);

    EXPECT_TRUE(state.IsButtonDown(Buttons::LeftThumbstickLeft));
    EXPECT_TRUE(state.IsButtonDown(Buttons::LeftThumbstickDown));
    EXPECT_FALSE(state.IsButtonDown(Buttons::LeftThumbstickRight));
    EXPECT_FALSE(state.IsButtonDown(Buttons::LeftThumbstickUp));

    EXPECT_TRUE(state.IsButtonDown(Buttons::RightThumbstickLeft));
    EXPECT_TRUE(state.IsButtonDown(Buttons::RightThumbstickUp));
    EXPECT_FALSE(state.IsButtonDown(Buttons::RightThumbstickRight));
    EXPECT_FALSE(state.IsButtonDown(Buttons::RightThumbstickDown));
}

// P1-008: FNA's StickToButtons/threshold checks are strict (`>`/`<`, GamePadState.cs:203-218 and
// :94-101), so a stick or trigger value exactly AT the dead-zone/threshold constant must not set the
// corresponding button. Pins this exact boundary against an accidental `>=`/`<=` regression.
TEST(GamePadStateTest, FourArgConstructorLeavesButtonsUnsetExactlyAtDeadZoneAndTriggerThresholdBoundaries)
{
    const GamePadThumbSticks sticks(
        Vector2(GamePad::LeftDeadZone, -GamePad::LeftDeadZone),
        Vector2(-GamePad::RightDeadZone, GamePad::RightDeadZone));
    const GamePadTriggers triggers(GamePad::TriggerThreshold, GamePad::TriggerThreshold);
    const GamePadButtons buttons(static_cast<Buttons>(0));
    const GamePadDPad dpad;

    const GamePadState state(sticks, triggers, buttons, dpad);

    EXPECT_FALSE(state.IsButtonDown(Buttons::LeftThumbstickRight));
    EXPECT_FALSE(state.IsButtonDown(Buttons::LeftThumbstickDown));
    EXPECT_FALSE(state.IsButtonDown(Buttons::RightThumbstickLeft));
    EXPECT_FALSE(state.IsButtonDown(Buttons::RightThumbstickUp));
    EXPECT_FALSE(state.IsButtonDown(Buttons::LeftTrigger));
    EXPECT_FALSE(state.IsButtonDown(Buttons::RightTrigger));
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

// P1-008: operator== (GamePadState.cs:232-240) ANDs together all 6 fields (IsConnected, PacketNumber,
// Buttons, DPad, ThumbSticks, Triggers). The tests above only isolate Buttons/PacketNumber/IsConnected
// differences; this pins that a DPad-only, ThumbSticks-only, or Triggers-only difference is also
// enough to break equality (guards against a field being silently dropped from Equals()). Uses the
// 4-arg constructor throughout with an identical explicit GamePadButtons(A) so the (dead-zone /
// threshold driven) Buttons field never itself changes between the states being compared.
TEST(GamePadStateTest, EqualityConsidersDPadThumbSticksAndTriggersIndependently)
{
    const GamePadThumbSticks zeroSticks(Vector2::Zero, Vector2::Zero);
    const GamePadTriggers zeroTriggers(0.0f, 0.0f);
    const GamePadButtons onlyA(Buttons::A);
    const GamePadDPad released(ButtonState::Released, ButtonState::Released,
                               ButtonState::Released, ButtonState::Released);

    const GamePadState baseline(zeroSticks, zeroTriggers, onlyA, released);

    // Only DPad differs (Buttons(A), ThumbSticks, and Triggers are identical to baseline).
    const GamePadDPad upPressed(ButtonState::Pressed, ButtonState::Released,
                                ButtonState::Released, ButtonState::Released);
    const GamePadState differentDPad(zeroSticks, zeroTriggers, onlyA, upPressed);
    EXPECT_FALSE(baseline.Equals(differentDPad));
    EXPECT_TRUE(baseline != differentDPad);

    // Below the dead zone, so no thumbstick button is synthesized: differs only in ThumbSticks.Left.
    const GamePadThumbSticks differentSticks(Vector2(GamePad::LeftDeadZone - 0.1f, 0.0f), Vector2::Zero);
    const GamePadState differentThumbSticks(differentSticks, zeroTriggers, onlyA, released);
    EXPECT_FALSE(baseline.Equals(differentThumbSticks));
    EXPECT_TRUE(baseline != differentThumbSticks);

    // Below TriggerThreshold, so no LeftTrigger button is synthesized: differs only in Triggers.Left.
    const GamePadTriggers differentTriggersVal(GamePad::TriggerThreshold - 0.05f, 0.0f);
    const GamePadState differentTriggers(zeroSticks, differentTriggersVal, onlyA, released);
    EXPECT_FALSE(baseline.Equals(differentTriggers));
    EXPECT_TRUE(baseline != differentTriggers);
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
