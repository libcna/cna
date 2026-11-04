// SPDX-License-Identifier: MS-PL
#include <gtest/gtest.h>

#include "CNA/Platform/CannedGamepadStateDriver.hpp"
#include "Microsoft/Xna/Framework/Input/GamePad.hpp"
#include "Microsoft/Xna/Framework/Input/GamePadTriggers.hpp"
#include "Microsoft/Xna/Framework/MathHelper.hpp"

#include <any>
#include <cmath>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Input;

namespace
{
    class GamePadTriggersTest : public ::testing::Test
    {
    protected:
        CNA::Internal::Input::Testing::CannedGamepadStateDriver input;
    };
}

// --- Public API: construction, exact equality, hashing ---

TEST_F(GamePadTriggersTest, DefaultConstructorIsAtRest)
{
    const GamePadTriggers triggers;

    EXPECT_FLOAT_EQ(triggers.getLeftProperty(), 0.0f);
    EXPECT_FLOAT_EQ(triggers.getRightProperty(), 0.0f);
}

TEST_F(GamePadTriggersTest, TwoArgConstructorClampsToZeroOneRange)
{
    const GamePadTriggers triggers(-0.5f, 1.5f);

    EXPECT_FLOAT_EQ(triggers.getLeftProperty(), 0.0f);
    EXPECT_FLOAT_EQ(triggers.getRightProperty(), 1.0f);
}

TEST_F(GamePadTriggersTest, Tq027EqualityUsesExactFloatComparison)
{
    // XNA GamePadTriggers.op_Equality IL_0000..0022 compares floats exactly;
    // Equals(object) IL_0024..0035 delegates to it. 0.25 + one ULP = 0.25 + 2^-25.
    const GamePadTriggers a(0.25f, 0.5f);
    const float justAbove = std::nextafter(0.25f, 1.0f);
    const GamePadTriggers almostEqual(justAbove, 0.5f);
    const GamePadTriggers clearlyDifferent(0.26f, 0.5f);
    ASSERT_NE(a.getLeftProperty(), almostEqual.getLeftProperty());
    ASSERT_LT(std::fabs(a.getLeftProperty() - almostEqual.getLeftProperty()), MathHelper::MachineEpsilonFloat);
    for (const auto& different : {almostEqual, clearlyDifferent,
                                 GamePadTriggers(0.25f, std::nextafter(0.5f, 1.0f))})
    {
        EXPECT_FALSE(a.Equals(different));
        EXPECT_FALSE(a.Equals(std::any(different)));
        EXPECT_FALSE(a == different);
        EXPECT_TRUE(a != different);
    }
    EXPECT_TRUE(a.Equals(GamePadTriggers(0.25f, 0.5f)));
    EXPECT_TRUE(a == GamePadTriggers(0.25f, 0.5f));
    EXPECT_FALSE(a != GamePadTriggers(0.25f, 0.5f));
}

TEST_F(GamePadTriggersTest, Tq027GetHashCodeXorsStoredFloatWords)
{
    // XNA GetHashCode IL_0000..0010 + SmartGetHashCode IL_0028..0048:
    // .25 = 0x3e800000, .75 = 0x3f400000, XOR = 0x01c00000 (no sum).
    const GamePadTriggers triggers(0.25f, 0.75f);
    EXPECT_EQ(triggers.GetHashCode(), 0x01c00000);
    EXPECT_EQ(GamePadTriggers().GetHashCode(), 0x7fffffff);
    EXPECT_EQ(GamePadTriggers(0.25f, 0.25f).GetHashCode(), 0x7fffffff);
    EXPECT_EQ(GamePadTriggers(0.25f, 0.0f).GetHashCode(), 0x3e800000);
    EXPECT_EQ(GamePadTriggers(0.0f, 0.25f).GetHashCode(), 0x3e800000);
}

// --- Dead-zone mode: only reachable via GamePad::GetState's private 3-arg ctor ---

TEST_F(GamePadTriggersTest, NonNoneDeadZoneModeExcludesTriggerThresholdThenClamps)
{
    input.Reset();
    input.SetGamePadConnection(PlayerIndex::One, true);
    const float rawValue = GamePad::TriggerThreshold + 0.2f;
    input.SetGamePadAxisValue(
        PlayerIndex::One, CNA::Platform::GamepadAxis::LeftTrigger, rawValue);

    const auto state = GamePad::GetState(PlayerIndex::One, GamePadDeadZone::IndependentAxes);

    const float expected = MathHelper::Clamp(
        GamePad::ExcludeAxisDeadZone(rawValue, GamePad::TriggerThreshold), 0.0f, 1.0f);
    EXPECT_NEAR(state.getTriggersProperty().getLeftProperty(), expected, 1e-5f);

    input.Reset();
}

TEST_F(GamePadTriggersTest, NonNoneDeadZoneModeZeroesValueWithinThreshold)
{
    input.Reset();
    input.SetGamePadConnection(PlayerIndex::One, true);
    input.SetGamePadAxisValue(
        PlayerIndex::One, CNA::Platform::GamepadAxis::LeftTrigger, GamePad::TriggerThreshold * 0.5f);

    const auto state = GamePad::GetState(PlayerIndex::One, GamePadDeadZone::IndependentAxes);

    EXPECT_FLOAT_EQ(state.getTriggersProperty().getLeftProperty(), 0.0f);

    input.Reset();
}

TEST_F(GamePadTriggersTest, NoneDeadZoneModePassesValueThroughClampedOnly)
{
    input.Reset();
    input.SetGamePadConnection(PlayerIndex::One, true);
    input.SetGamePadAxisValue(
        PlayerIndex::One, CNA::Platform::GamepadAxis::LeftTrigger, GamePad::TriggerThreshold * 0.5f);

    const auto state = GamePad::GetState(PlayerIndex::One, GamePadDeadZone::None);

    // Unlike IndependentAxes/Circular, None applies no dead-zone exclusion at all.
    EXPECT_FLOAT_EQ(state.getTriggersProperty().getLeftProperty(), GamePad::TriggerThreshold * 0.5f);

    input.Reset();
}

TEST_F(GamePadTriggersTest, NonNoneDeadZoneModeAppliesIndependentlyToBothTriggers)
{
    // Guards against a left/right field swap in the internal 3-arg constructor:
    // Left and Right use distinct raw values here, so a copy/paste mistake that fed
    // leftTrigger into `right` (or vice versa) would make this test fail.
    input.Reset();
    input.SetGamePadConnection(PlayerIndex::One, true);
    const float rawLeft = GamePad::TriggerThreshold + 0.2f;
    const float rawRight = GamePad::TriggerThreshold + 0.5f;
    input.SetGamePadAxisValue(
        PlayerIndex::One, CNA::Platform::GamepadAxis::LeftTrigger, rawLeft);
    input.SetGamePadAxisValue(
        PlayerIndex::One, CNA::Platform::GamepadAxis::RightTrigger, rawRight);

    const auto state = GamePad::GetState(PlayerIndex::One, GamePadDeadZone::IndependentAxes);

    const float expectedLeft = MathHelper::Clamp(
        GamePad::ExcludeAxisDeadZone(rawLeft, GamePad::TriggerThreshold), 0.0f, 1.0f);
    const float expectedRight = MathHelper::Clamp(
        GamePad::ExcludeAxisDeadZone(rawRight, GamePad::TriggerThreshold), 0.0f, 1.0f);
    ASSERT_NE(expectedLeft, expectedRight);

    EXPECT_NEAR(state.getTriggersProperty().getLeftProperty(), expectedLeft, 1e-5f);
    EXPECT_NEAR(state.getTriggersProperty().getRightProperty(), expectedRight, 1e-5f);

    input.Reset();
}


TEST_F(GamePadTriggersTest, Tq025EveryModeUsesIndependentTriggerArithmetic)
{
    // XNA GamePadDeadZoneUtils.ApplyTriggerDeadZone IL_0000..0026 and ApplyLinearDeadZone IL_001d..0033:
    // None: value / 255; other modes: max(0, (value - 30) / 225), clamped to one.
    input.SetGamePadConnection(PlayerIndex::One, true);
    input.SetGamePadAxisValue(PlayerIndex::One, CNA::Platform::GamepadAxis::LeftTrigger, 75.0f / 255.0f);
    input.SetGamePadAxisValue(PlayerIndex::One, CNA::Platform::GamepadAxis::RightTrigger, 165.0f / 255.0f);
    for (const auto mode : {GamePadDeadZone::None, GamePadDeadZone::IndependentAxes,
                            GamePadDeadZone::Circular})
    {
        SCOPED_TRACE(static_cast<int>(mode));
        const auto triggers = GamePad::GetState(PlayerIndex::One, mode).getTriggersProperty();
        EXPECT_NEAR(triggers.getLeftProperty(), mode == GamePadDeadZone::None ? 75.0f / 255.0f : 0.2f, 1e-6f);
        EXPECT_NEAR(triggers.getRightProperty(), mode == GamePadDeadZone::None ? 165.0f / 255.0f : 0.6f, 1e-6f);
    }
    // Boundary 30 is excluded; 31 is the first pressed native value: (31-30)/225.
    input.SetGamePadAxisValue(PlayerIndex::One, CNA::Platform::GamepadAxis::LeftTrigger, 30.0f / 255.0f);
    input.SetGamePadAxisValue(PlayerIndex::One, CNA::Platform::GamepadAxis::RightTrigger, 31.0f / 255.0f);
    for (const auto mode : {GamePadDeadZone::IndependentAxes, GamePadDeadZone::Circular})
    {
        const auto triggers = GamePad::GetState(PlayerIndex::One, mode).getTriggersProperty();
        EXPECT_FLOAT_EQ(triggers.getLeftProperty(), 0.0f);
        EXPECT_NEAR(triggers.getRightProperty(), 1.0f / 225.0f, 1e-7f);
    }
}
