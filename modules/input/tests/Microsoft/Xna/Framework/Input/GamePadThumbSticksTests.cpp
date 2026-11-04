// SPDX-License-Identifier: MS-PL
#include <gtest/gtest.h>

#include "CNA/Platform/CannedGamepadStateDriver.hpp"
#include "Microsoft/Xna/Framework/Input/GamePad.hpp"
#include "Microsoft/Xna/Framework/Input/GamePadThumbSticks.hpp"

using Microsoft::Xna::Framework::Vector2;
using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Input;

namespace
{
    class GamePadThumbSticksTest : public ::testing::Test
    {
    protected:
        CNA::Internal::Input::Testing::CannedGamepadStateDriver input;
    };
}

// --- Public API: construction, equality, hashing (no dead-zone mode involved) ---

TEST_F(GamePadThumbSticksTest, DefaultConstructorIsAtRest)
{
    const GamePadThumbSticks sticks;

    EXPECT_EQ(sticks.getLeftProperty(), Vector2::Zero);
    EXPECT_EQ(sticks.getRightProperty(), Vector2::Zero);
}

TEST_F(GamePadThumbSticksTest, TwoArgConstructorAppliesSquareClamp)
{
    const GamePadThumbSticks sticks(Vector2(2.0f, -3.0f), Vector2(0.5f, 1.5f));

    EXPECT_FLOAT_EQ(sticks.getLeftProperty().X, 1.0f);
    EXPECT_FLOAT_EQ(sticks.getLeftProperty().Y, -1.0f);
    EXPECT_FLOAT_EQ(sticks.getRightProperty().X, 0.5f);
    EXPECT_FLOAT_EQ(sticks.getRightProperty().Y, 1.0f);
}

TEST_F(GamePadThumbSticksTest, EqualityOperatorsForEqualAndDifferingInstances)
{
    const GamePadThumbSticks a(Vector2(0.1f, 0.2f), Vector2(0.3f, 0.4f));
    const GamePadThumbSticks sameAsA(Vector2(0.1f, 0.2f), Vector2(0.3f, 0.4f));
    const GamePadThumbSticks different(Vector2(0.9f, 0.2f), Vector2(0.3f, 0.4f));

    EXPECT_TRUE(a.Equals(sameAsA));
    EXPECT_TRUE(a == sameAsA);
    EXPECT_FALSE(a != sameAsA);

    EXPECT_FALSE(a.Equals(different));
    EXPECT_TRUE(a != different);
    EXPECT_FALSE(a == different);
}

TEST_F(GamePadThumbSticksTest, Tq027GetHashCodeXorsFourStoredFloatWords)
{
    // XNA GetHashCode IL_0000..0010 + SmartGetHashCode IL_0028..0048: raw binary32 XOR.
    // .25=3e800000, -.5=bf000000, .75=3f400000, .125=3e000000 -> 80c00000.
    const GamePadThumbSticks sticks(Vector2(0.25f, -0.5f), Vector2(0.75f, 0.125f));
    EXPECT_EQ(sticks.GetHashCode(), -2134900736); // signed 0x80c00000.
    EXPECT_EQ(GamePadThumbSticks().GetHashCode(), 0x7fffffff);
    for (const auto& value : {
             GamePadThumbSticks(Vector2(0.25f, 0), Vector2::Zero),
             GamePadThumbSticks(Vector2(0, 0.25f), Vector2::Zero),
             GamePadThumbSticks(Vector2::Zero, Vector2(0.25f, 0)),
             GamePadThumbSticks(Vector2::Zero, Vector2(0, 0.25f))})
        EXPECT_EQ(value.GetHashCode(), 0x3e800000);
    EXPECT_EQ(GamePadThumbSticks(Vector2(0.25f, 0.25f), Vector2::Zero).GetHashCode(), 0x7fffffff);
}

// --- Dead-zone modes: only reachable via GamePad::GetState's private 3-arg ctor ---

TEST_F(GamePadThumbSticksTest, IndependentAxesModeExcludesPerAxisDeadZoneThenSquareClamps)
{
    input.Reset();
    input.SetGamePadConnection(PlayerIndex::One, true);
    input.SetGamePadAxisValue(
        PlayerIndex::One, CNA::Platform::GamepadAxis::LeftThumbstickX, 0.5f);
    input.SetGamePadAxisValue(
        PlayerIndex::One, CNA::Platform::GamepadAxis::LeftThumbstickY, 0.0f);

    const auto state = GamePad::GetState(PlayerIndex::One, GamePadDeadZone::IndependentAxes);
    const Vector2 left = state.getThumbSticksProperty().getLeftProperty();

    // FNA GamePad.cs: (v - 7849/32768)/(1 - 7849/32768); at v=1/2 this is 8535/24919.
    const float expectedX = 8535.0f / 24919.0f;
    EXPECT_NEAR(left.X, expectedX, 1e-5f);
    EXPECT_FLOAT_EQ(left.Y, 0.0f);

    input.Reset();
}

TEST_F(GamePadThumbSticksTest, IndependentAxesModeExcludesRightStickDeadZoneUsingRightDeadZoneConstant)
{
    // The Left-stick test above only exercises GamePad::LeftDeadZone. LeftDeadZone
    // (7849/32768) and RightDeadZone (8689/32768) are distinct constants per FNA's
    // GamePad.cs, and GamePadThumbSticks::ApplyDeadZone wires right_.X/right_.Y to
    // RightDeadZone specifically (not LeftDeadZone) for GamePadDeadZone::IndependentAxes.
    // This pins that per-stick wiring, not just the shared formula.
    input.Reset();
    input.SetGamePadConnection(PlayerIndex::One, true);
    input.SetGamePadAxisValue(
        PlayerIndex::One, CNA::Platform::GamepadAxis::RightThumbstickX, 0.5f);
    input.SetGamePadAxisValue(
        PlayerIndex::One, CNA::Platform::GamepadAxis::RightThumbstickY, 0.0f);

    const auto state = GamePad::GetState(PlayerIndex::One, GamePadDeadZone::IndependentAxes);
    const Vector2 right = state.getThumbSticksProperty().getRightProperty();

    // FNA GamePad.cs: (1/2 - 8689/32768)/(1 - 8689/32768) = 7695/24079.
    const float expectedX = 7695.0f / 24079.0f;
    EXPECT_NE(GamePad::LeftDeadZone, GamePad::RightDeadZone);
    EXPECT_NEAR(right.X, expectedX, 1e-5f);
    EXPECT_FLOAT_EQ(right.Y, 0.0f);

    input.Reset();
}

TEST_F(GamePadThumbSticksTest, IndependentAxesModeZeroesValuesWithinDeadZone)
{
    input.Reset();
    input.SetGamePadConnection(PlayerIndex::One, true);
    input.SetGamePadAxisValue(
        PlayerIndex::One, CNA::Platform::GamepadAxis::LeftThumbstickX, GamePad::LeftDeadZone * 0.5f);

    const auto state = GamePad::GetState(PlayerIndex::One, GamePadDeadZone::IndependentAxes);

    EXPECT_FLOAT_EQ(state.getThumbSticksProperty().getLeftProperty().X, 0.0f);

    input.Reset();
}

TEST_F(GamePadThumbSticksTest, CircularModeZeroesValuesWithinDeadZoneRadius)
{
    input.Reset();
    input.SetGamePadConnection(PlayerIndex::One, true);
    input.SetGamePadAxisValue(
        PlayerIndex::One, CNA::Platform::GamepadAxis::LeftThumbstickX, 0.1f);
    input.SetGamePadAxisValue(
        PlayerIndex::One, CNA::Platform::GamepadAxis::LeftThumbstickY, 0.1f);

    // Length(0.1, 0.1) ~= 0.1414, below LeftDeadZone (~0.2395).
    const auto state = GamePad::GetState(PlayerIndex::One, GamePadDeadZone::Circular);

    EXPECT_EQ(state.getThumbSticksProperty().getLeftProperty(), Vector2::Zero);

    input.Reset();
}

TEST_F(GamePadThumbSticksTest, CircularModeRescalesValueOutsideDeadZoneRadius)
{
    input.Reset();
    input.SetGamePadConnection(PlayerIndex::One, true);
    input.SetGamePadAxisValue(
        PlayerIndex::One, CNA::Platform::GamepadAxis::LeftThumbstickX, 0.0f);
    input.SetGamePadAxisValue(
        PlayerIndex::One, CNA::Platform::GamepadAxis::LeftThumbstickY, 0.5f);

    const auto state = GamePad::GetState(PlayerIndex::One, GamePadDeadZone::Circular);
    const Vector2 left = state.getThumbSticksProperty().getLeftProperty();

    // FNA ExcludeCircularDeadZone at radius 1/2: (16384 - 7849)/(32768 - 7849).
    const float expectedLength = 8535.0f / 24919.0f;
    EXPECT_NEAR(left.X, 0.0f, 1e-5f);
    EXPECT_NEAR(left.Y, expectedLength, 1e-5f);

    input.Reset();
}

TEST_F(GamePadThumbSticksTest, CircularModeRescalesRightStickUsingRightDeadZoneConstant)
{
    // Mirrors CircularModeRescalesValueOutsideDeadZoneRadius but for the Right stick, pinning
    // that ApplyDeadZone's Circular branch calls ExcludeCircularDeadZone(right_, RightDeadZone)
    // -- not LeftDeadZone -- for the right stick specifically.
    input.Reset();
    input.SetGamePadConnection(PlayerIndex::One, true);
    input.SetGamePadAxisValue(
        PlayerIndex::One, CNA::Platform::GamepadAxis::RightThumbstickX, 0.0f);
    input.SetGamePadAxisValue(
        PlayerIndex::One, CNA::Platform::GamepadAxis::RightThumbstickY, 0.5f);

    const auto state = GamePad::GetState(PlayerIndex::One, GamePadDeadZone::Circular);
    const Vector2 right = state.getThumbSticksProperty().getRightProperty();

    // FNA ExcludeCircularDeadZone at radius 1/2: (16384 - 8689)/(32768 - 8689).
    const float expectedLength = 7695.0f / 24079.0f;
    EXPECT_NE(GamePad::LeftDeadZone, GamePad::RightDeadZone);
    EXPECT_NEAR(right.X, 0.0f, 1e-5f);
    EXPECT_NEAR(right.Y, expectedLength, 1e-5f);

    input.Reset();
}

TEST_F(GamePadThumbSticksTest, CircularModeClampsMagnitudeToUnitCircle)
{
    input.Reset();
    input.SetGamePadConnection(PlayerIndex::One, true);
    input.SetGamePadAxisValue(
        PlayerIndex::One, CNA::Platform::GamepadAxis::LeftThumbstickX, 1.0f);
    input.SetGamePadAxisValue(
        PlayerIndex::One, CNA::Platform::GamepadAxis::LeftThumbstickY, 1.0f);

    // (1,1) has length sqrt(2); after dead-zone exclusion it still exceeds 1 and must be
    // circularly clamped back onto the unit circle, preserving direction.
    const auto state = GamePad::GetState(PlayerIndex::One, GamePadDeadZone::Circular);
    const Vector2 left = state.getThumbSticksProperty().getLeftProperty();

    EXPECT_NEAR(left.Length(), 1.0f, 1e-4f);
    EXPECT_NEAR(left.X, left.Y, 1e-4f);

    input.Reset();
}

TEST_F(GamePadThumbSticksTest, Tq002ModesDistinguishSignedAxesFromRadialRescaling)
{
    input.SetGamePadConnection(PlayerIndex::One, true);
    using CNA::Platform::GamepadAxis;
    input.SetGamePadAxisValue(PlayerIndex::One, GamepadAxis::LeftThumbstickX, -0.3f);
    input.SetGamePadAxisValue(PlayerIndex::One, GamepadAxis::LeftThumbstickY, 0.4f);
    input.SetGamePadAxisValue(PlayerIndex::One, GamepadAxis::RightThumbstickX, 0.3f);
    input.SetGamePadAxisValue(PlayerIndex::One, GamepadAxis::RightThumbstickY, -0.4f);

    // FNA GamePadThumbSticks.ApplyDeadZone: None preserves every component.
    const auto none = GamePad::GetState(PlayerIndex::One, GamePadDeadZone::None).getThumbSticksProperty();
    EXPECT_EQ(none.getLeftProperty(), Vector2(-0.3f, 0.4f));
    EXPECT_EQ(none.getRightProperty(), Vector2(0.3f, -0.4f));

    // Independent axes: (abs(v)*32768 - threshold)/(32768 - threshold), retaining sign.
    const auto axes = GamePad::GetState(PlayerIndex::One, GamePadDeadZone::IndependentAxes).getThumbSticksProperty();
    EXPECT_NEAR(axes.getLeftProperty().X, -1981.4f / 24919.0f, 1e-6f);
    EXPECT_NEAR(axes.getLeftProperty().Y, 5258.2f / 24919.0f, 1e-6f);
    EXPECT_NEAR(axes.getRightProperty().X, 1141.4f / 24079.0f, 1e-6f);
    EXPECT_NEAR(axes.getRightProperty().Y, -4418.2f / 24079.0f, 1e-6f);

    // Radius sqrt(.3^2+.4^2)=.5, unit direction (.6,.8); scale by the excluded radius.
    const auto radial = GamePad::GetState(PlayerIndex::One, GamePadDeadZone::Circular).getThumbSticksProperty();
    EXPECT_NEAR(radial.getLeftProperty().X, -0.6f * 8535.0f / 24919.0f, 1e-6f);
    EXPECT_NEAR(radial.getLeftProperty().Y, 0.8f * 8535.0f / 24919.0f, 1e-6f);
    EXPECT_NEAR(radial.getRightProperty().X, 0.6f * 7695.0f / 24079.0f, 1e-6f);
    EXPECT_NEAR(radial.getRightProperty().Y, -0.8f * 7695.0f / 24079.0f, 1e-6f);
}

TEST_F(GamePadThumbSticksTest, Tq002SignedDeadZoneEndpointsAreZeroInBothModes)
{
    input.SetGamePadConnection(PlayerIndex::One, true);
    using CNA::Platform::GamepadAxis;
    // Literal thresholds from FNA GamePad.cs, independent of CNA's constants.
    for (const float sign : {-1.0f, 1.0f})
    {
        input.SetGamePadAxisValue(PlayerIndex::One, GamepadAxis::LeftThumbstickX, sign * 7849.0f / 32768.0f);
        input.SetGamePadAxisValue(PlayerIndex::One, GamepadAxis::RightThumbstickY, sign * 8689.0f / 32768.0f);
        for (const auto mode : {GamePadDeadZone::IndependentAxes, GamePadDeadZone::Circular})
        {
            const auto sticks = GamePad::GetState(PlayerIndex::One, mode).getThumbSticksProperty();
            EXPECT_EQ(sticks.getLeftProperty(), Vector2::Zero);
            EXPECT_EQ(sticks.getRightProperty(), Vector2::Zero);
        }
    }
}


TEST_F(GamePadThumbSticksTest, Tq025EqualityChecksEachStickComponent)
{
    // XNA GamePadThumbSticks.op_Equality IL_0000..002a compares both Vector2 values.
    const GamePadThumbSticks origin;
    const GamePadThumbSticks values[] = {
        {Vector2(0.125f, 0.0f), Vector2::Zero},
        {Vector2(0.0f, -0.125f), Vector2::Zero},
        {Vector2::Zero, Vector2(0.125f, 0.0f)},
        {Vector2::Zero, Vector2(0.0f, -0.125f)}};
    for (const auto& value : values)
    {
        EXPECT_FALSE(value.Equals(origin));
        EXPECT_FALSE(value == origin);
        EXPECT_TRUE(value != origin);
        EXPECT_TRUE(value.Equals(value));
        EXPECT_EQ(value.GetHashCode(), GamePadThumbSticks(value).GetHashCode());
    }
}
