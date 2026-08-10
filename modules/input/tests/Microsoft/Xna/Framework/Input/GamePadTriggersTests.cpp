// SPDX-License-Identifier: MS-PL
#include <gtest/gtest.h>

#include "CNA/Internal/Input/InputManager.hpp"
#include "Microsoft/Xna/Framework/Input/GamePad.hpp"
#include "Microsoft/Xna/Framework/Input/GamePadTriggers.hpp"
#include "Microsoft/Xna/Framework/MathHelper.hpp"
#include "System/Single.hpp"

#include <cmath>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Input;

namespace
{
    void ResetGamePadState()
    {
        CNA::Internal::Input::InputManager::SetGamePadConnection(PlayerIndex::One, false);
    }
}

// --- Public API: construction, epsilon equality, hashing ---

TEST(GamePadTriggersTest, DefaultConstructorIsAtRest)
{
    const GamePadTriggers triggers;

    EXPECT_FLOAT_EQ(triggers.getLeftProperty(), 0.0f);
    EXPECT_FLOAT_EQ(triggers.getRightProperty(), 0.0f);
}

TEST(GamePadTriggersTest, TwoArgConstructorClampsToZeroOneRange)
{
    const GamePadTriggers triggers(-0.5f, 1.5f);

    EXPECT_FLOAT_EQ(triggers.getLeftProperty(), 0.0f);
    EXPECT_FLOAT_EQ(triggers.getRightProperty(), 1.0f);
}

TEST(GamePadTriggersTest, EqualityUsesEpsilonToleranceRatherThanExactFloatEquality)
{
    // MathHelper::MachineEpsilonFloat is the epsilon relative to 1.0 (~2^-24), so any
    // base value below 0.5 has a ULP smaller than it — the very next representable float
    // is guaranteed to be "within epsilon" per WithinEpsilon's fixed absolute threshold.
    const float justAbove = std::nextafter(0.1f, 1.0f);
    const GamePadTriggers a(0.1f, 0.1f);
    const GamePadTriggers almostEqual(justAbove, justAbove);
    const GamePadTriggers clearlyDifferent(0.1f + 0.01f, 0.1f);

    ASSERT_NE(a.getLeftProperty(), almostEqual.getLeftProperty());
    ASSERT_LT(std::fabs(a.getLeftProperty() - almostEqual.getLeftProperty()), MathHelper::MachineEpsilonFloat);

    EXPECT_TRUE(a.Equals(almostEqual));
    EXPECT_TRUE(a == almostEqual);
    EXPECT_FALSE(a != almostEqual);

    EXPECT_FALSE(a.Equals(clearlyDifferent));
    EXPECT_TRUE(a != clearlyDifferent);
    EXPECT_FALSE(a == clearlyDifferent);
}

TEST(GamePadTriggersTest, GetHashCodeMatchesFloatBitHashSumFormula)
{
    const GamePadTriggers triggers(0.25f, 0.75f);

    const int expected = System::Single::GetHashCode(0.25f) + System::Single::GetHashCode(0.75f);
    EXPECT_EQ(triggers.GetHashCode(), expected);
}

// --- Dead-zone mode: only reachable via GamePad::GetState's private 3-arg ctor ---

TEST(GamePadTriggersTest, NonNoneDeadZoneModeExcludesTriggerThresholdThenClamps)
{
    ResetGamePadState();
    CNA::Internal::Input::InputManager::SetGamePadConnection(PlayerIndex::One, true);
    const float rawValue = GamePad::TriggerThreshold + 0.2f;
    CNA::Internal::Input::InputManager::SetGamePadAxisValue(
        PlayerIndex::One, CNA::Internal::Input::GamePadAxis::LeftTrigger, rawValue);

    const auto state = GamePad::GetState(PlayerIndex::One, GamePadDeadZone::IndependentAxes);

    const float expected = MathHelper::Clamp(
        GamePad::ExcludeAxisDeadZone(rawValue, GamePad::TriggerThreshold), 0.0f, 1.0f);
    EXPECT_NEAR(state.getTriggersProperty().getLeftProperty(), expected, 1e-5f);

    ResetGamePadState();
}

TEST(GamePadTriggersTest, NonNoneDeadZoneModeZeroesValueWithinThreshold)
{
    ResetGamePadState();
    CNA::Internal::Input::InputManager::SetGamePadConnection(PlayerIndex::One, true);
    CNA::Internal::Input::InputManager::SetGamePadAxisValue(
        PlayerIndex::One, CNA::Internal::Input::GamePadAxis::LeftTrigger, GamePad::TriggerThreshold * 0.5f);

    const auto state = GamePad::GetState(PlayerIndex::One, GamePadDeadZone::IndependentAxes);

    EXPECT_FLOAT_EQ(state.getTriggersProperty().getLeftProperty(), 0.0f);

    ResetGamePadState();
}

TEST(GamePadTriggersTest, NoneDeadZoneModePassesValueThroughClampedOnly)
{
    ResetGamePadState();
    CNA::Internal::Input::InputManager::SetGamePadConnection(PlayerIndex::One, true);
    CNA::Internal::Input::InputManager::SetGamePadAxisValue(
        PlayerIndex::One, CNA::Internal::Input::GamePadAxis::LeftTrigger, GamePad::TriggerThreshold * 0.5f);

    const auto state = GamePad::GetState(PlayerIndex::One, GamePadDeadZone::None);

    // Unlike IndependentAxes/Circular, None applies no dead-zone exclusion at all.
    EXPECT_FLOAT_EQ(state.getTriggersProperty().getLeftProperty(), GamePad::TriggerThreshold * 0.5f);

    ResetGamePadState();
}

TEST(GamePadTriggersTest, NonNoneDeadZoneModeAppliesIndependentlyToBothTriggers)
{
    // Guards against a left/right field swap in the internal 3-arg constructor:
    // Left and Right use distinct raw values here, so a copy/paste mistake that fed
    // leftTrigger into `right` (or vice versa) would make this test fail.
    ResetGamePadState();
    CNA::Internal::Input::InputManager::SetGamePadConnection(PlayerIndex::One, true);
    const float rawLeft = GamePad::TriggerThreshold + 0.2f;
    const float rawRight = GamePad::TriggerThreshold + 0.5f;
    CNA::Internal::Input::InputManager::SetGamePadAxisValue(
        PlayerIndex::One, CNA::Internal::Input::GamePadAxis::LeftTrigger, rawLeft);
    CNA::Internal::Input::InputManager::SetGamePadAxisValue(
        PlayerIndex::One, CNA::Internal::Input::GamePadAxis::RightTrigger, rawRight);

    const auto state = GamePad::GetState(PlayerIndex::One, GamePadDeadZone::IndependentAxes);

    const float expectedLeft = MathHelper::Clamp(
        GamePad::ExcludeAxisDeadZone(rawLeft, GamePad::TriggerThreshold), 0.0f, 1.0f);
    const float expectedRight = MathHelper::Clamp(
        GamePad::ExcludeAxisDeadZone(rawRight, GamePad::TriggerThreshold), 0.0f, 1.0f);
    ASSERT_NE(expectedLeft, expectedRight);

    EXPECT_NEAR(state.getTriggersProperty().getLeftProperty(), expectedLeft, 1e-5f);
    EXPECT_NEAR(state.getTriggersProperty().getRightProperty(), expectedRight, 1e-5f);

    ResetGamePadState();
}
