// SPDX-License-Identifier: MS-PL
#include <gtest/gtest.h>
#include <cmath>

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

    // XNA ApplyStickDeadZone IL_0004-0029: trunc(.5*32767)=16383; (16383-7849)/(32767-7849).
    const float expectedX = 8534.0f / 24918.0f;
    EXPECT_NEAR(left.X, expectedX, 1e-5f);
    EXPECT_FLOAT_EQ(left.Y, 0.0f);

    input.Reset();
}

TEST_F(GamePadThumbSticksTest, IndependentAxesModeExcludesRightStickDeadZoneUsingRightDeadZoneConstant)
{
    // XNA ApplyRightStickDeadZone IL_0003 supplies 8689 rather than left threshold 7849.
    input.Reset();
    input.SetGamePadConnection(PlayerIndex::One, true);
    input.SetGamePadAxisValue(
        PlayerIndex::One, CNA::Platform::GamepadAxis::RightThumbstickX, 0.5f);
    input.SetGamePadAxisValue(
        PlayerIndex::One, CNA::Platform::GamepadAxis::RightThumbstickY, 0.0f);

    const auto state = GamePad::GetState(PlayerIndex::One, GamePadDeadZone::IndependentAxes);
    const Vector2 right = state.getThumbSticksProperty().getRightProperty();

    // XNA ApplyStickDeadZone IL_0004-0029: (16383-8689)/(32767-8689).
    const float expectedX = 7694.0f / 24078.0f;
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

    // XNA Circular IL_0037-0096, axial sample 16383: (16383-7849)/(32767-7849).
    const float expectedLength = 8534.0f / 24918.0f;
    EXPECT_NEAR(left.X, 0.0f, 1e-5f);
    EXPECT_NEAR(left.Y, expectedLength, 1e-5f);

    input.Reset();
}

TEST_F(GamePadThumbSticksTest, CircularModeRescalesRightStickUsingRightDeadZoneConstant)
{
    // XNA ApplyRightStickDeadZone IL_0003 supplies the right threshold for Circular too.
    input.Reset();
    input.SetGamePadConnection(PlayerIndex::One, true);
    input.SetGamePadAxisValue(
        PlayerIndex::One, CNA::Platform::GamepadAxis::RightThumbstickX, 0.0f);
    input.SetGamePadAxisValue(
        PlayerIndex::One, CNA::Platform::GamepadAxis::RightThumbstickY, 0.5f);

    const auto state = GamePad::GetState(PlayerIndex::One, GamePadDeadZone::Circular);
    const Vector2 right = state.getThumbSticksProperty().getRightProperty();

    // XNA Circular IL_0037-0096, axial sample 16383: (16383-8689)/(32767-8689).
    const float expectedLength = 7694.0f / 24078.0f;
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

    // XNA Circular IL_0037-0096: samples(32767,32767) exceed the max radius;
    // clamp excluded radial length to 1 before scaling the integer direction.
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

    // XNA ApplyStickDeadZone None IL_009d-00c8: trunc(v*32767)/32767.
    const auto none = GamePad::GetState(PlayerIndex::One, GamePadDeadZone::None).getThumbSticksProperty();
    EXPECT_EQ(none.getLeftProperty(), Vector2(-9830.0f / 32767.0f, 13106.0f / 32767.0f));
    EXPECT_EQ(none.getRightProperty(), Vector2(9830.0f / 32767.0f, -13106.0f / 32767.0f));

    // XNA IndependentAxes IL_0004-0029: integer samples 9830 and 13106, signed.
    const auto axes = GamePad::GetState(PlayerIndex::One, GamePadDeadZone::IndependentAxes).getThumbSticksProperty();
    EXPECT_NEAR(axes.getLeftProperty().X, -1981.0f / 24918.0f, 1e-6f);
    EXPECT_NEAR(axes.getLeftProperty().Y, 5257.0f / 24918.0f, 1e-6f);
    EXPECT_NEAR(axes.getRightProperty().X, 1141.0f / 24078.0f, 1e-6f);
    EXPECT_NEAR(axes.getRightProperty().Y, -4417.0f / 24078.0f, 1e-6f);

    // XNA Circular IL_0037-0096: sqrt(9830^2+13106^2)=sqrt(268396136), stored Single.
    // Constants below follow that integer radius (not CNA output); 2e-7 resolves one sample.
    const auto radial = GamePad::GetState(PlayerIndex::One, GamePadDeadZone::Circular).getThumbSticksProperty();
    const float radius = static_cast<float>(std::sqrt(268396136.0));
    const float leftScale = ((radius - 7849.0f) / 24918.0f) / radius;
    const float rightScale = ((radius - 8689.0f) / 24078.0f) / radius;
    EXPECT_NEAR(radial.getLeftProperty().X, -9830.0f * leftScale, 2e-7f);
    EXPECT_NEAR(radial.getLeftProperty().Y, 13106.0f * leftScale, 2e-7f);
    EXPECT_NEAR(radial.getRightProperty().X, 9830.0f * rightScale, 2e-7f);
    EXPECT_NEAR(radial.getRightProperty().Y, -13106.0f * rightScale, 2e-7f);
}

TEST_F(GamePadThumbSticksTest, Tq002SignedDeadZoneEndpointsAreZeroInBothModes)
{
    input.SetGamePadConnection(PlayerIndex::One, true);
    using CNA::Platform::GamepadAxis;
    // XNA ApplyStickDeadZone IL_0004-0029/0037-0096: exact native thresholds, both signs.
    for (const float sign : {-1.0f, 1.0f})
    {
        input.SetGamePadAxisValue(PlayerIndex::One, GamepadAxis::LeftThumbstickX, sign * 7849.0f / 32767.0f);
        input.SetGamePadAxisValue(PlayerIndex::One, GamepadAxis::RightThumbstickY, sign * 8689.0f / 32767.0f);
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

// Owner seam: XNA FillInternalState IL_030a-0315 / 0331-033c / 0358-0363 / 037f-038a.
// XNA ApplyStickDeadZone IL_0004-0029 / 009d-00c8. Half a native sample stays at the endpoint.
TEST_F(GamePadThumbSticksTest, Tq036SignedQuantizedThresholdsAndNextSample)
{
    input.SetGamePadConnection(PlayerIndex::One, true);
    using CNA::Platform::GamepadAxis;
    for (const float sign : {-1.0f, 1.0f})
    {
        input.SetGamePadAxisValue(PlayerIndex::One, GamepadAxis::LeftThumbstickX, sign * 7849.5f / 32767.0f);
        input.SetGamePadAxisValue(PlayerIndex::One, GamepadAxis::LeftThumbstickY, sign * 7850.5f / 32767.0f);
        input.SetGamePadAxisValue(PlayerIndex::One, GamepadAxis::RightThumbstickX, sign * 8690.5f / 32767.0f);
        input.SetGamePadAxisValue(PlayerIndex::One, GamepadAxis::RightThumbstickY, sign * 8689.5f / 32767.0f);
        const auto axes = GamePad::GetState(PlayerIndex::One).getThumbSticksProperty();
        EXPECT_FLOAT_EQ(axes.getLeftProperty().X, 0.0f);
        EXPECT_FLOAT_EQ(axes.getLeftProperty().Y, sign / 24918.0f);
        EXPECT_FLOAT_EQ(axes.getRightProperty().X, sign / 24078.0f);
        EXPECT_FLOAT_EQ(axes.getRightProperty().Y, 0.0f);
        const auto none = GamePad::GetState(PlayerIndex::One, GamePadDeadZone::None).getThumbSticksProperty();
        EXPECT_FLOAT_EQ(none.getLeftProperty().X, sign * 7849.0f / 32767.0f);
        EXPECT_FLOAT_EQ(none.getLeftProperty().Y, sign * 7850.0f / 32767.0f);
        EXPECT_FLOAT_EQ(none.getRightProperty().X, sign * 8690.0f / 32767.0f);
        EXPECT_FLOAT_EQ(none.getRightProperty().Y, sign * 8689.0f / 32767.0f);
    }
}

// XNA Circular IL_0037-0096 clamps radial length before dividing, preserving integer direction.
TEST_F(GamePadThumbSticksTest, Tq036CircularUsesIntegerRadiusAndClampsBeforeDirection)
{
    input.SetGamePadConnection(PlayerIndex::One, true);
    using CNA::Platform::GamepadAxis;
    // Exact 3-4-5 integer triangle: trunc inputs 12000,16000 -> radius 20000.
    input.SetGamePadAxisValue(PlayerIndex::One, GamepadAxis::LeftThumbstickX, -12000.5f / 32767.0f);
    input.SetGamePadAxisValue(PlayerIndex::One, GamepadAxis::LeftThumbstickY, 16000.5f / 32767.0f);
    // Radius above max: sqrt(30000^2+(-30000)^2)=30000*sqrt(2), direction (+,-)/sqrt(2).
    input.SetGamePadAxisValue(PlayerIndex::One, GamepadAxis::RightThumbstickX, 30000.5f / 32767.0f);
    input.SetGamePadAxisValue(PlayerIndex::One, GamepadAxis::RightThumbstickY, -30000.5f / 32767.0f);
    const auto sticks = GamePad::GetState(PlayerIndex::One, GamePadDeadZone::Circular).getThumbSticksProperty();
    EXPECT_NEAR(sticks.getLeftProperty().X, -0.6f * 12151.0f / 24918.0f, 2e-7f);
    EXPECT_NEAR(sticks.getLeftProperty().Y, 0.8f * 12151.0f / 24918.0f, 2e-7f);
    EXPECT_NEAR(sticks.getRightProperty().X, 0.7071067811865475f, 2e-7f);
    EXPECT_NEAR(sticks.getRightProperty().Y, -0.7071067811865475f, 2e-7f);
}
