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

TEST_F(GamePadThumbSticksTest, GetHashCodeMatchesLeftPlus37TimesRightFormula)
{
    const GamePadThumbSticks sticks(Vector2(0.1f, 0.2f), Vector2(0.3f, 0.4f));

    const int expected = static_cast<int>(
        static_cast<unsigned>(sticks.getLeftProperty().GetHashCode()) +
        37u * static_cast<unsigned>(sticks.getRightProperty().GetHashCode()));
    EXPECT_EQ(sticks.GetHashCode(), expected);
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

    const float expectedX = GamePad::ExcludeAxisDeadZone(0.5f, GamePad::LeftDeadZone);
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

    const float expectedX = GamePad::ExcludeAxisDeadZone(0.5f, GamePad::RightDeadZone);
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

    const float originalLength = 0.5f;
    const float expectedLength = (originalLength - GamePad::LeftDeadZone) / (1.0f - GamePad::LeftDeadZone);
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

    const float originalLength = 0.5f;
    const float expectedLength = (originalLength - GamePad::RightDeadZone) / (1.0f - GamePad::RightDeadZone);
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
