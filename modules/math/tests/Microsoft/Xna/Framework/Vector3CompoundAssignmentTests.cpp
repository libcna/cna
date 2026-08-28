// SPDX-License-Identifier: MS-PL
// C# synthesises `*=` and `/=` from the declared operators, so XNA game code writes
// `velocity *= sensitivity` without Vector3 declaring anything. C++ has to spell them out, and
// Vector3 carried only `+=`/`-=` while Vector2 already had the full set.

#include <gtest/gtest.h>

#include "Microsoft/Xna/Framework/Vector3.hpp"

using Microsoft::Xna::Framework::Vector3;

namespace
{
    TEST(Vector3CompoundAssignmentTest, ScalarMultiplyMatchesTheBinaryOperator)
    {
        Vector3 value(1.5f, -2.0f, 3.25f);
        value *= 2.0f;

        const Vector3 expected = Vector3(1.5f, -2.0f, 3.25f) * 2.0f;
        EXPECT_FLOAT_EQ(expected.X, value.X);
        EXPECT_FLOAT_EQ(expected.Y, value.Y);
        EXPECT_FLOAT_EQ(expected.Z, value.Z);
    }

    TEST(Vector3CompoundAssignmentTest, ComponentwiseMultiplyMatchesTheBinaryOperator)
    {
        Vector3 value(1.5f, -2.0f, 3.25f);
        value *= Vector3(2.0f, 0.5f, -4.0f);

        const Vector3 expected = Vector3(1.5f, -2.0f, 3.25f) * Vector3(2.0f, 0.5f, -4.0f);
        EXPECT_FLOAT_EQ(expected.X, value.X);
        EXPECT_FLOAT_EQ(expected.Y, value.Y);
        EXPECT_FLOAT_EQ(expected.Z, value.Z);
    }

    TEST(Vector3CompoundAssignmentTest, ComponentwiseDivideMatchesTheBinaryOperator)
    {
        Vector3 value(1.5f, -2.0f, 3.25f);
        value /= Vector3(2.0f, 0.5f, -4.0f);

        const Vector3 expected = Vector3(1.5f, -2.0f, 3.25f) / Vector3(2.0f, 0.5f, -4.0f);
        EXPECT_FLOAT_EQ(expected.X, value.X);
        EXPECT_FLOAT_EQ(expected.Y, value.Y);
        EXPECT_FLOAT_EQ(expected.Z, value.Z);
    }

    // The scalar divide multiplies by a reciprocal, as Vector3::Divide does. Asserting equality
    // with the binary operator rather than with X/divider is what pins that: the two differ by a
    // rounding for most inputs, and a game mixing the two forms must not see them disagree.
    TEST(Vector3CompoundAssignmentTest, ScalarDivideMatchesTheBinaryOperatorBitForBit)
    {
        Vector3 value(1.5f, -2.0f, 3.25f);
        value /= 3.0f;

        const Vector3 expected = Vector3(1.5f, -2.0f, 3.25f) / 3.0f;
        EXPECT_EQ(expected.X, value.X);
        EXPECT_EQ(expected.Y, value.Y);
        EXPECT_EQ(expected.Z, value.Z);
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
