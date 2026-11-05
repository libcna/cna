// SPDX-License-Identifier: MS-PL
#include <gtest/gtest.h>
#include <limits>
#include "Microsoft/Xna/Framework/Input/GamePadState.hpp"
using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Input;

TEST(Tq075GamePadThumbSticksTest, PublicClampHandlesNaNAndInfinityOnAllFourAxes)
{
    // Refresh ctor IL_000e..006b: Vector2.Min(value,One), then Max(value,-One).
    // Vector2.Min IL_0010/0037 ordered blt chooses second operand for NaN -> +1.
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float inf = std::numeric_limits<float>::infinity();
    const GamePadThumbSticks allNaN(Vector2(nan, nan), Vector2(nan, nan));
    EXPECT_EQ(allNaN.getLeftProperty(), Vector2(1, 1));
    EXPECT_EQ(allNaN.getRightProperty(), Vector2(1, 1));
    const GamePadThumbSticks infinite(Vector2(inf, -inf), Vector2(-inf, inf));
    EXPECT_EQ(infinite.getLeftProperty(), Vector2(1, -1));
    EXPECT_EQ(infinite.getRightProperty(), Vector2(-1, 1));
    const GamePadState raw(Vector2(nan, nan), Vector2(nan, nan), 0, 0, {});
    const GamePadState parts(allNaN, GamePadTriggers(), GamePadButtons(), GamePadDPad());
    for (const auto& state : {raw, parts}) {
        EXPECT_EQ(state.getThumbSticksProperty().getLeftProperty(), Vector2(1, 1));
        EXPECT_EQ(state.getThumbSticksProperty().getRightProperty(), Vector2(1, 1));
        EXPECT_TRUE(state.IsButtonDown(Buttons::LeftThumbstickRight | Buttons::LeftThumbstickUp |
                                      Buttons::RightThumbstickRight | Buttons::RightThumbstickUp));
    }
}

TEST(Tq075GamePadThumbSticksTest, PublicClampKeepsSquareCornersAndUnquantizedFractions)
{
    // Same ctor: each axis is clamped independently; no radial clamp or sample conversion.
    const GamePadThumbSticks value(Vector2(1, 1), Vector2(-0.125f, 0.0625f));
    EXPECT_EQ(value.getLeftProperty(), Vector2(1, 1));
    EXPECT_EQ(value.getRightProperty(), Vector2(-0.125f, 0.0625f));
    EXPECT_FLOAT_EQ(value.getLeftProperty().LengthSquared(), 2);
}
