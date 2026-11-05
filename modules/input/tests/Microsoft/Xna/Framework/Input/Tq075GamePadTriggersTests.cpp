// SPDX-License-Identifier: MS-PL
#include <gtest/gtest.h>
#include <any>
#include <bit>
#include <cstdint>
#include <cmath>
#include <limits>
#include "Microsoft/Xna/Framework/Input/GamePadTriggers.hpp"
using namespace Microsoft::Xna::Framework::Input;

TEST(Tq075GamePadTriggersTest, ConstructorClampsBothSidesAndPreservesNaN)
{
    // Refresh ctor IL_000e..0061 uses System.Math.Min/Max: infinities clamp; NaN propagates.
    const float inf = std::numeric_limits<float>::infinity();
    const GamePadTriggers lowHigh(-inf, inf), highLow(inf, -inf);
    EXPECT_FLOAT_EQ(lowHigh.getLeftProperty(), 0);
    EXPECT_FLOAT_EQ(lowHigh.getRightProperty(), 1);
    EXPECT_FLOAT_EQ(highLow.getLeftProperty(), 1);
    EXPECT_FLOAT_EQ(highLow.getRightProperty(), 0);
    const float nan = std::bit_cast<float>(std::uint32_t{0x7fc00001u});
    const GamePadTriggers leftNaN(nan, 0), rightNaN(0, nan);
    EXPECT_TRUE(std::isnan(leftNaN.getLeftProperty()));
    EXPECT_FLOAT_EQ(leftNaN.getRightProperty(), 0);
    EXPECT_FLOAT_EQ(rightNaN.getLeftProperty(), 0);
    EXPECT_TRUE(std::isnan(rightNaN.getRightProperty()));
    // op_Equality IL_000e/001e is exact, including unordered floats; Equals(object)
    // IL_0024..0035 delegates. A value containing NaN is unequal even to its exact copy.
    for (const auto& value : {leftNaN, rightNaN}) {
        const auto copy = value;
        EXPECT_FALSE(value.Equals(copy));
        EXPECT_FALSE(value.Equals(std::any(copy)));
        EXPECT_FALSE(value == copy);
        EXPECT_TRUE(value != copy);
        // SmartGetHashCode IL_0028..0048 XORs raw32 words, retaining this qNaN payload.
        EXPECT_EQ(value.GetHashCode(), 0x7fc00001);
    }
}

TEST(Tq075GamePadTriggersTest, SignedZeroInputsCanonicalizeBeforeRawHashing)
{
    // Refresh ctor IL_002b..0035/0057..0061 calls Framework Math.Max(value,+0).
    // Pinned BCL4 Math.Max(Single) IL_0002..0011 returns operand two for equal zeros.
    // Stored words are both +0; Helpers.SmartGetHashCode IL_003d..0048 substitutes INT_MAX.
    const GamePadTriggers zero, negativeLeft(-0.0f, 0), negativeRight(0, -0.0f);
    for (const auto& value : {negativeLeft, negativeRight}) {
        EXPECT_TRUE(value.Equals(zero));
        EXPECT_TRUE(value.Equals(std::any(zero)));
        EXPECT_TRUE(value == zero);
        EXPECT_FALSE(value != zero);
        EXPECT_FALSE(std::signbit(value.getLeftProperty()));
        EXPECT_FALSE(std::signbit(value.getRightProperty()));
        EXPECT_EQ(value.GetHashCode(), 0x7fffffff);
    }
    EXPECT_EQ(GamePadTriggers(-0.0f, -0.0f).GetHashCode(), 0x7fffffff);
}
