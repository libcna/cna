#include <gtest/gtest.h>
#include <cmath>
#include "Microsoft/Xna/Framework/Vector4.hpp"

using Microsoft::Xna::Framework::Vector2;
using Microsoft::Xna::Framework::Vector3;
using Microsoft::Xna::Framework::Vector4;

// --- Static constants ---

TEST(Vector4Test, ZeroHasAllComponentsZero)
{
    EXPECT_FLOAT_EQ(Vector4::Zero.X, 0.0f);
    EXPECT_FLOAT_EQ(Vector4::Zero.Y, 0.0f);
    EXPECT_FLOAT_EQ(Vector4::Zero.Z, 0.0f);
    EXPECT_FLOAT_EQ(Vector4::Zero.W, 0.0f);
}

TEST(Vector4Test, OneHasAllComponentsOne)
{
    EXPECT_FLOAT_EQ(Vector4::One.X, 1.0f);
    EXPECT_FLOAT_EQ(Vector4::One.Y, 1.0f);
    EXPECT_FLOAT_EQ(Vector4::One.Z, 1.0f);
    EXPECT_FLOAT_EQ(Vector4::One.W, 1.0f);
}

TEST(Vector4Test, UnitVectors)
{
    EXPECT_FLOAT_EQ(Vector4::UnitX.X, 1.0f);
    EXPECT_FLOAT_EQ(Vector4::UnitX.Y, 0.0f);
    EXPECT_FLOAT_EQ(Vector4::UnitX.Z, 0.0f);
    EXPECT_FLOAT_EQ(Vector4::UnitX.W, 0.0f);

    EXPECT_FLOAT_EQ(Vector4::UnitW.X, 0.0f);
    EXPECT_FLOAT_EQ(Vector4::UnitW.W, 1.0f);
}

// --- Construction ---

TEST(Vector4Test, DefaultConstructorIsZero)
{
    Vector4 v;
    EXPECT_FLOAT_EQ(v.X, 0.0f);
    EXPECT_FLOAT_EQ(v.Y, 0.0f);
    EXPECT_FLOAT_EQ(v.Z, 0.0f);
    EXPECT_FLOAT_EQ(v.W, 0.0f);
}

TEST(Vector4Test, FourComponentConstructor)
{
    Vector4 v(1.0f, 2.0f, 3.0f, 4.0f);
    EXPECT_FLOAT_EQ(v.X, 1.0f);
    EXPECT_FLOAT_EQ(v.Y, 2.0f);
    EXPECT_FLOAT_EQ(v.Z, 3.0f);
    EXPECT_FLOAT_EQ(v.W, 4.0f);
}

TEST(Vector4Test, ScalarConstructorSetsAllComponents)
{
    Vector4 v(7.0f);
    EXPECT_FLOAT_EQ(v.X, 7.0f);
    EXPECT_FLOAT_EQ(v.Y, 7.0f);
    EXPECT_FLOAT_EQ(v.Z, 7.0f);
    EXPECT_FLOAT_EQ(v.W, 7.0f);
}

TEST(Vector4Test, ConstructFromVector2ZW)
{
    Vector4 v(Vector2(1.0f, 2.0f), 3.0f, 4.0f);
    EXPECT_FLOAT_EQ(v.X, 1.0f);
    EXPECT_FLOAT_EQ(v.Y, 2.0f);
    EXPECT_FLOAT_EQ(v.Z, 3.0f);
    EXPECT_FLOAT_EQ(v.W, 4.0f);
}

TEST(Vector4Test, ConstructFromVector3AndW)
{
    Vector4 v(Vector3(1.0f, 2.0f, 3.0f), 4.0f);
    EXPECT_FLOAT_EQ(v.X, 1.0f);
    EXPECT_FLOAT_EQ(v.Y, 2.0f);
    EXPECT_FLOAT_EQ(v.Z, 3.0f);
    EXPECT_FLOAT_EQ(v.W, 4.0f);
}

// --- Length ---

TEST(Vector4Test, LengthOfKnownVector)
{
    // sqrt(1+4+4+0) = 3
    Vector4 v(1.0f, 2.0f, 2.0f, 0.0f);
    EXPECT_FLOAT_EQ(v.Length(), 3.0f);
}

TEST(Vector4Test, LengthSquaredOfKnownVector)
{
    Vector4 v(1.0f, 2.0f, 2.0f, 0.0f);
    EXPECT_FLOAT_EQ(v.LengthSquared(), 9.0f);
}

// --- Normalize ---

TEST(Vector4Test, NormalizeProducesUnitVector)
{
    Vector4 v(0.0f, 0.0f, 0.0f, 5.0f);
    v.Normalize();
    EXPECT_NEAR(v.Length(), 1.0f, 1e-6f);
    EXPECT_FLOAT_EQ(v.W, 1.0f);
}

TEST(Vector4Test, NormalizeStaticPreservesOriginal)
{
    Vector4 original(1.0f, 2.0f, 3.0f, 4.0f);
    Vector4 result = Vector4::Normalize(original);
    EXPECT_NEAR(result.Length(), 1.0f, 1e-6f);
    EXPECT_FLOAT_EQ(original.X, 1.0f);
}

// --- Dot product ---

TEST(Vector4Test, DotOfOrthogonalBasisVectorsIsZero)
{
    EXPECT_FLOAT_EQ(Vector4::Dot(Vector4::UnitX, Vector4::UnitY), 0.0f);
    EXPECT_FLOAT_EQ(Vector4::Dot(Vector4::UnitX, Vector4::UnitW), 0.0f);
}

TEST(Vector4Test, DotOfSameUnitVectorIsOne)
{
    EXPECT_FLOAT_EQ(Vector4::Dot(Vector4::UnitX, Vector4::UnitX), 1.0f);
}

TEST(Vector4Test, DotProductValue)
{
    Vector4 a(1.0f, 2.0f, 3.0f, 4.0f);
    Vector4 b(5.0f, 6.0f, 7.0f, 8.0f);
    // 1*5 + 2*6 + 3*7 + 4*8 = 5+12+21+32 = 70
    EXPECT_FLOAT_EQ(Vector4::Dot(a, b), 70.0f);
}

// --- Distance ---

TEST(Vector4Test, DistanceBetweenKnownVectors)
{
    // distance from (0,0,0,0) to (1,2,2,0) = 3
    EXPECT_FLOAT_EQ(Vector4::Distance(Vector4::Zero, Vector4(1.0f, 2.0f, 2.0f, 0.0f)), 3.0f);
}

// --- Arithmetic ---

TEST(Vector4Test, AddTwoVectors)
{
    Vector4 result = Vector4::Add(Vector4(1.0f, 2.0f, 3.0f, 4.0f), Vector4(5.0f, 6.0f, 7.0f, 8.0f));
    EXPECT_FLOAT_EQ(result.X, 6.0f);
    EXPECT_FLOAT_EQ(result.Y, 8.0f);
    EXPECT_FLOAT_EQ(result.Z, 10.0f);
    EXPECT_FLOAT_EQ(result.W, 12.0f);
}

TEST(Vector4Test, SubtractTwoVectors)
{
    Vector4 result = Vector4::Subtract(Vector4(5.0f, 6.0f, 7.0f, 8.0f), Vector4(1.0f, 2.0f, 3.0f, 4.0f));
    EXPECT_FLOAT_EQ(result.X, 4.0f);
    EXPECT_FLOAT_EQ(result.Y, 4.0f);
    EXPECT_FLOAT_EQ(result.Z, 4.0f);
    EXPECT_FLOAT_EQ(result.W, 4.0f);
}

TEST(Vector4Test, MultiplyByScalar)
{
    Vector4 result = Vector4::Multiply(Vector4(1.0f, 2.0f, 3.0f, 4.0f), 2.0f);
    EXPECT_FLOAT_EQ(result.X, 2.0f);
    EXPECT_FLOAT_EQ(result.Y, 4.0f);
    EXPECT_FLOAT_EQ(result.Z, 6.0f);
    EXPECT_FLOAT_EQ(result.W, 8.0f);
}

// --- Lerp ---

TEST(Vector4Test, LerpAtHalfReturnsMidpoint)
{
    Vector4 result = Vector4::Lerp(Vector4::Zero, Vector4(2.0f, 4.0f, 6.0f, 8.0f), 0.5f);
    EXPECT_FLOAT_EQ(result.X, 1.0f);
    EXPECT_FLOAT_EQ(result.Y, 2.0f);
    EXPECT_FLOAT_EQ(result.Z, 3.0f);
    EXPECT_FLOAT_EQ(result.W, 4.0f);
}

TEST(Vector4Test, LerpAtZeroReturnsFirst)
{
    Vector4 result = Vector4::Lerp(Vector4::UnitX, Vector4::UnitY, 0.0f);
    EXPECT_FLOAT_EQ(result.X, 1.0f);
    EXPECT_FLOAT_EQ(result.Y, 0.0f);
}

TEST(Vector4Test, LerpAtOneReturnsSecond)
{
    Vector4 result = Vector4::Lerp(Vector4::UnitX, Vector4::UnitY, 1.0f);
    EXPECT_FLOAT_EQ(result.X, 0.0f);
    EXPECT_FLOAT_EQ(result.Y, 1.0f);
}

// --- Min / Max ---

TEST(Vector4Test, MinReturnsComponentWiseMinimum)
{
    Vector4 result = Vector4::Min(Vector4(3.0f, 1.0f, 5.0f, 2.0f), Vector4(1.0f, 4.0f, 2.0f, 7.0f));
    EXPECT_FLOAT_EQ(result.X, 1.0f);
    EXPECT_FLOAT_EQ(result.Y, 1.0f);
    EXPECT_FLOAT_EQ(result.Z, 2.0f);
    EXPECT_FLOAT_EQ(result.W, 2.0f);
}

TEST(Vector4Test, MaxReturnsComponentWiseMaximum)
{
    Vector4 result = Vector4::Max(Vector4(3.0f, 1.0f, 5.0f, 2.0f), Vector4(1.0f, 4.0f, 2.0f, 7.0f));
    EXPECT_FLOAT_EQ(result.X, 3.0f);
    EXPECT_FLOAT_EQ(result.Y, 4.0f);
    EXPECT_FLOAT_EQ(result.Z, 5.0f);
    EXPECT_FLOAT_EQ(result.W, 7.0f);
}

// --- Operators ---

TEST(Vector4Test, AdditionOperator)
{
    Vector4 result = Vector4(1.0f, 2.0f, 3.0f, 4.0f) + Vector4(5.0f, 6.0f, 7.0f, 8.0f);
    EXPECT_FLOAT_EQ(result.X, 6.0f);
    EXPECT_FLOAT_EQ(result.W, 12.0f);
}

TEST(Vector4Test, NegationOperator)
{
    Vector4 result = -Vector4(1.0f, -2.0f, 3.0f, -4.0f);
    EXPECT_FLOAT_EQ(result.X, -1.0f);
    EXPECT_FLOAT_EQ(result.Y, 2.0f);
    EXPECT_FLOAT_EQ(result.Z, -3.0f);
    EXPECT_FLOAT_EQ(result.W, 4.0f);
}

TEST(Vector4Test, EqualityOperator)
{
    EXPECT_TRUE(Vector4(1.0f, 2.0f, 3.0f, 4.0f) == Vector4(1.0f, 2.0f, 3.0f, 4.0f));
    EXPECT_FALSE(Vector4(1.0f, 2.0f, 3.0f, 4.0f) == Vector4(1.0f, 2.0f, 3.0f, 5.0f));
}

TEST(Vector4Test, InequalityOperator)
{
    EXPECT_TRUE(Vector4(1.0f, 2.0f, 3.0f, 4.0f) != Vector4(1.0f, 2.0f, 3.0f, 5.0f));
    EXPECT_FALSE(Vector4(1.0f, 2.0f, 3.0f, 4.0f) != Vector4(1.0f, 2.0f, 3.0f, 4.0f));
}

TEST(Vector4Test, ClampKeepsComponentsInRange)
{
    Vector4 result = Vector4::Clamp(
        Vector4(-1.0f, 0.5f, 2.0f, -5.0f),
        Vector4::Zero,
        Vector4::One
    );
    EXPECT_FLOAT_EQ(result.X, 0.0f);
    EXPECT_FLOAT_EQ(result.Y, 0.5f);
    EXPECT_FLOAT_EQ(result.Z, 1.0f);
    EXPECT_FLOAT_EQ(result.W, 0.0f);
}
