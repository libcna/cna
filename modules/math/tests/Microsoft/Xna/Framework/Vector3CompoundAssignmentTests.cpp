// SPDX-License-Identifier: MS-PL
// C# synthesises `*=` and `/=` from the declared operators, so XNA game code writes
// `velocity *= sensitivity` without Vector3 declaring anything. C++ has to spell them out, and
// Vector3 carried only `+=`/`-=` while Vector2 already had the full set.

#include <gtest/gtest.h>
#include <bit>
#include <cmath>
#include <cstdint>
#include <limits>

#include "Microsoft/Xna/Framework/Vector3.hpp"

using Microsoft::Xna::Framework::Vector3;

namespace
{
    TEST(Vector3CompoundAssignmentTest, ScalarMultiplyHasIndependentLiteralResult)
    {
        Vector3 value(1.5f, -2.0f, 3.25f);
        value *= 2.0f;

        const Vector3 expected(3.0f, -4.0f, 6.5f);
        EXPECT_FLOAT_EQ(expected.X, value.X);
        EXPECT_FLOAT_EQ(expected.Y, value.Y);
        EXPECT_FLOAT_EQ(expected.Z, value.Z);
    }

    TEST(Vector3CompoundAssignmentTest, ComponentwiseMultiplyHasIndependentLiteralResult)
    {
        Vector3 value(1.5f, -2.0f, 3.25f);
        value *= Vector3(2.0f, 0.5f, -4.0f);

        const Vector3 expected(3.0f, -1.0f, -13.0f);
        EXPECT_FLOAT_EQ(expected.X, value.X);
        EXPECT_FLOAT_EQ(expected.Y, value.Y);
        EXPECT_FLOAT_EQ(expected.Z, value.Z);
    }

    TEST(Vector3CompoundAssignmentTest, ComponentwiseDivideHasIndependentLiteralResult)
    {
        Vector3 value(1.5f, -2.0f, 3.25f);
        value /= Vector3(2.0f, 0.5f, -4.0f);

        const Vector3 expected(0.75f, -4.0f, -0.8125f);
        EXPECT_FLOAT_EQ(expected.X, value.X);
        EXPECT_FLOAT_EQ(expected.Y, value.Y);
        EXPECT_FLOAT_EQ(expected.Z, value.Z);
    }

    TEST(Vector3CompoundAssignmentTest, ScalarDivideHasIndependentLiteralResult)
    {
        Vector3 value(1.5f, -2.0f, 3.25f);
        value /= 3.0f;

        const Vector3 expected(0.5f, -0.6666666865348816f, 1.0833333730697632f);
        EXPECT_EQ(expected.X, value.X);
        EXPECT_EQ(expected.Y, value.Y);
        EXPECT_EQ(expected.Z, value.Z);
    }

    TEST(Vector3CompoundAssignmentTest, ScalarDivideUsesTheXnaRoundedReciprocal)
    {
        Vector3 value(7, -7, 10);
        EXPECT_EQ(&(value /= 3.0f), &value);
        EXPECT_EQ(std::bit_cast<std::uint32_t>(value.X), 0x40155556u);
        EXPECT_EQ(std::bit_cast<std::uint32_t>(value.Y), 0xc0155556u);
        EXPECT_EQ(std::bit_cast<std::uint32_t>(value.Z), 0x40555556u);
    }

    TEST(Vector3CompoundAssignmentTest, TinyScalarDivisorOverflowsTheXnaReciprocal)
    {
        const float tiny = std::numeric_limits<float>::denorm_min();
        Vector3 value(tiny, -tiny, -0.0f);
        value /= tiny;
        EXPECT_EQ(value.X, std::numeric_limits<float>::infinity());
        EXPECT_EQ(value.Y, -std::numeric_limits<float>::infinity());
        EXPECT_TRUE(std::isnan(value.Z));
    }

    TEST(Vector3CompoundAssignmentTest, EachOperatorReturnsThisVector)
    {
        Vector3 value(1, 2, 3);
        EXPECT_EQ(&value, &(value *= 2.0f));
        EXPECT_EQ(&value, &(value *= Vector3(1, 1, 1)));
        EXPECT_EQ(&value, &(value /= 2.0f));
        EXPECT_EQ(&value, &(value /= Vector3(1, 1, 1)));
    }
}
