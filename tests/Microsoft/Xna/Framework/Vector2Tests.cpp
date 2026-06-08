#include <gtest/gtest.h>
#include <cmath>
#include "Microsoft/Xna/Framework/Vector2.hpp"

using Microsoft::Xna::Framework::Vector2;

// --- Static constants ---

TEST(Vector2Test, ZeroHasBothComponentsZero)
{
    EXPECT_FLOAT_EQ(Vector2::Zero.X, 0.0f);
    EXPECT_FLOAT_EQ(Vector2::Zero.Y, 0.0f);
}

TEST(Vector2Test, OneHasBothComponentsOne)
{
    EXPECT_FLOAT_EQ(Vector2::One.X, 1.0f);
    EXPECT_FLOAT_EQ(Vector2::One.Y, 1.0f);
}

TEST(Vector2Test, UnitXAndUnitY)
{
    EXPECT_FLOAT_EQ(Vector2::UnitX.X, 1.0f);
    EXPECT_FLOAT_EQ(Vector2::UnitX.Y, 0.0f);
    EXPECT_FLOAT_EQ(Vector2::UnitY.X, 0.0f);
    EXPECT_FLOAT_EQ(Vector2::UnitY.Y, 1.0f);
}

// --- Construction ---

TEST(Vector2Test, DefaultConstructorIsZero)
{
    Vector2 v;
    EXPECT_FLOAT_EQ(v.X, 0.0f);
    EXPECT_FLOAT_EQ(v.Y, 0.0f);
}

TEST(Vector2Test, TwoComponentConstructor)
{
    Vector2 v(3.0f, 4.0f);
    EXPECT_FLOAT_EQ(v.X, 3.0f);
    EXPECT_FLOAT_EQ(v.Y, 4.0f);
}

TEST(Vector2Test, ScalarConstructorSetsBothComponents)
{
    Vector2 v(7.0f);
    EXPECT_FLOAT_EQ(v.X, 7.0f);
    EXPECT_FLOAT_EQ(v.Y, 7.0f);
}

// --- Length ---

TEST(Vector2Test, LengthOf3_4Is5)
{
    Vector2 v(3.0f, 4.0f);
    EXPECT_FLOAT_EQ(v.Length(), 5.0f);
}

TEST(Vector2Test, LengthSquaredOf3_4Is25)
{
    Vector2 v(3.0f, 4.0f);
    EXPECT_FLOAT_EQ(v.LengthSquared(), 25.0f);
}

TEST(Vector2Test, ZeroVectorHasZeroLength)
{
    EXPECT_FLOAT_EQ(Vector2::Zero.Length(), 0.0f);
}

// --- Normalize ---

TEST(Vector2Test, NormalizeInPlaceProducesUnitVector)
{
    Vector2 v(0.0f, 5.0f);
    v.Normalize();
    EXPECT_FLOAT_EQ(v.Length(), 1.0f);
    EXPECT_FLOAT_EQ(v.X, 0.0f);
    EXPECT_FLOAT_EQ(v.Y, 1.0f);
}

TEST(Vector2Test, NormalizeStaticPreservesOriginal)
{
    Vector2 original(3.0f, 4.0f);
    Vector2 result = Vector2::Normalize(original);
    EXPECT_FLOAT_EQ(result.Length(), 1.0f);
    EXPECT_FLOAT_EQ(original.X, 3.0f);
}

// --- Arithmetic ---

TEST(Vector2Test, AddTwoVectors)
{
    Vector2 a(1.0f, 2.0f);
    Vector2 b(3.0f, 4.0f);
    Vector2 result = Vector2::Add(a, b);
    EXPECT_FLOAT_EQ(result.X, 4.0f);
    EXPECT_FLOAT_EQ(result.Y, 6.0f);
}

TEST(Vector2Test, SubtractTwoVectors)
{
    Vector2 a(5.0f, 7.0f);
    Vector2 b(2.0f, 3.0f);
    Vector2 result = Vector2::Subtract(a, b);
    EXPECT_FLOAT_EQ(result.X, 3.0f);
    EXPECT_FLOAT_EQ(result.Y, 4.0f);
}

TEST(Vector2Test, MultiplyByScalar)
{
    Vector2 v(2.0f, 3.0f);
    Vector2 result = Vector2::Multiply(v, 2.0f);
    EXPECT_FLOAT_EQ(result.X, 4.0f);
    EXPECT_FLOAT_EQ(result.Y, 6.0f);
}

TEST(Vector2Test, DivideByScalar)
{
    Vector2 v(4.0f, 6.0f);
    Vector2 result = Vector2::Divide(v, 2.0f);
    EXPECT_FLOAT_EQ(result.X, 2.0f);
    EXPECT_FLOAT_EQ(result.Y, 3.0f);
}

// --- Dot product ---

TEST(Vector2Test, DotOfOrthogonalVectorsIsZero)
{
    EXPECT_FLOAT_EQ(Vector2::Dot(Vector2::UnitX, Vector2::UnitY), 0.0f);
}

TEST(Vector2Test, DotOfParallelVectorsIsProduct)
{
    EXPECT_FLOAT_EQ(Vector2::Dot(Vector2(2.0f, 0.0f), Vector2(3.0f, 0.0f)), 6.0f);
}

// --- Distance ---

TEST(Vector2Test, DistanceBetween3_4AndOriginIs5)
{
    EXPECT_FLOAT_EQ(Vector2::Distance(Vector2::Zero, Vector2(3.0f, 4.0f)), 5.0f);
}

TEST(Vector2Test, DistanceSquaredBetween3_4AndOriginIs25)
{
    EXPECT_FLOAT_EQ(Vector2::DistanceSquared(Vector2::Zero, Vector2(3.0f, 4.0f)), 25.0f);
}

// --- Lerp ---

TEST(Vector2Test, LerpAtZeroReturnsFirstVector)
{
    Vector2 result = Vector2::Lerp(Vector2::Zero, Vector2::One, 0.0f);
    EXPECT_FLOAT_EQ(result.X, 0.0f);
    EXPECT_FLOAT_EQ(result.Y, 0.0f);
}

TEST(Vector2Test, LerpAtOneReturnsSecondVector)
{
    Vector2 result = Vector2::Lerp(Vector2::Zero, Vector2::One, 1.0f);
    EXPECT_FLOAT_EQ(result.X, 1.0f);
    EXPECT_FLOAT_EQ(result.Y, 1.0f);
}

TEST(Vector2Test, LerpAtHalfReturnsMidpoint)
{
    Vector2 result = Vector2::Lerp(Vector2::Zero, Vector2(4.0f, 2.0f), 0.5f);
    EXPECT_FLOAT_EQ(result.X, 2.0f);
    EXPECT_FLOAT_EQ(result.Y, 1.0f);
}

// --- Min / Max ---

TEST(Vector2Test, MinReturnsComponentWiseMinimum)
{
    Vector2 result = Vector2::Min(Vector2(3.0f, 1.0f), Vector2(1.0f, 4.0f));
    EXPECT_FLOAT_EQ(result.X, 1.0f);
    EXPECT_FLOAT_EQ(result.Y, 1.0f);
}

TEST(Vector2Test, MaxReturnsComponentWiseMaximum)
{
    Vector2 result = Vector2::Max(Vector2(3.0f, 1.0f), Vector2(1.0f, 4.0f));
    EXPECT_FLOAT_EQ(result.X, 3.0f);
    EXPECT_FLOAT_EQ(result.Y, 4.0f);
}

// --- Operators ---

TEST(Vector2Test, AdditionOperator)
{
    Vector2 result = Vector2(1.0f, 2.0f) + Vector2(3.0f, 4.0f);
    EXPECT_FLOAT_EQ(result.X, 4.0f);
    EXPECT_FLOAT_EQ(result.Y, 6.0f);
}

TEST(Vector2Test, SubtractionOperator)
{
    Vector2 result = Vector2(5.0f, 7.0f) - Vector2(2.0f, 3.0f);
    EXPECT_FLOAT_EQ(result.X, 3.0f);
    EXPECT_FLOAT_EQ(result.Y, 4.0f);
}

TEST(Vector2Test, NegationOperator)
{
    Vector2 result = -Vector2(3.0f, -4.0f);
    EXPECT_FLOAT_EQ(result.X, -3.0f);
    EXPECT_FLOAT_EQ(result.Y, 4.0f);
}

TEST(Vector2Test, MultiplicationByScalarOperator)
{
    Vector2 result = Vector2(2.0f, 3.0f) * 3.0f;
    EXPECT_FLOAT_EQ(result.X, 6.0f);
    EXPECT_FLOAT_EQ(result.Y, 9.0f);
}

TEST(Vector2Test, DivisionByScalarOperator)
{
    Vector2 result = Vector2(6.0f, 9.0f) / 3.0f;
    EXPECT_FLOAT_EQ(result.X, 2.0f);
    EXPECT_FLOAT_EQ(result.Y, 3.0f);
}

TEST(Vector2Test, EqualityOperator)
{
    Vector2 a(1.0f, 2.0f);
    Vector2 b(1.0f, 2.0f);
    EXPECT_TRUE(a == b);
    EXPECT_FALSE(a != b);
}

TEST(Vector2Test, InequalityOperator)
{
    Vector2 a(1.0f, 2.0f);
    Vector2 b(1.0f, 3.0f);
    EXPECT_FALSE(a == b);
    EXPECT_TRUE(a != b);
}

// --- Clamp ---

TEST(Vector2Test, ClampKeepsComponentsInRange)
{
    Vector2 result = Vector2::Clamp(Vector2(-5.0f, 10.0f), Vector2(0.0f, 0.0f), Vector2(5.0f, 5.0f));
    EXPECT_FLOAT_EQ(result.X, 0.0f);
    EXPECT_FLOAT_EQ(result.Y, 5.0f);
}

// --- Negate ---

TEST(Vector2Test, NegateStaticFlipsBothComponents)
{
    Vector2 result = Vector2::Negate(Vector2(1.0f, -2.0f));
    EXPECT_FLOAT_EQ(result.X, -1.0f);
    EXPECT_FLOAT_EQ(result.Y, 2.0f);
}
