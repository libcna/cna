// SPDX-License-Identifier: MS-PL

#include <gtest/gtest.h>
#include <cmath>
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Quaternion.hpp"

using Microsoft::Xna::Framework::Matrix;
using Microsoft::Xna::Framework::Quaternion;
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

// --- Equals ---

TEST(Vector2Test, EqualsReturnsTrueForSameVector)
{
    Vector2 a(1.0f, 2.0f);
    Vector2 b(1.0f, 2.0f);
    EXPECT_TRUE(a.Equals(b));
}

TEST(Vector2Test, EqualsReturnsFalseForDifferentVector)
{
    Vector2 a(1.0f, 2.0f);
    Vector2 b(1.0f, 3.0f);
    EXPECT_FALSE(a.Equals(b));
}

// --- GetHashCode ---

TEST(Vector2Test, GetHashCodeEqualVectorsGiveEqualHash)
{
    Vector2 a(1.0f, 2.0f);
    Vector2 b(1.0f, 2.0f);
    EXPECT_EQ(a.GetHashCode(), b.GetHashCode());
}

TEST(Vector2Test, GetHashCodeDifferentVectorsTypicallyDiffer)
{
    Vector2 a(1.0f, 2.0f);
    Vector2 b(3.0f, 4.0f);
    EXPECT_NE(a.GetHashCode(), b.GetHashCode());
}

// --- ToString ---

TEST(Vector2Test, ToStringFormat)
{
    Vector2 v(1.0f, 2.0f);
    std::string s = v.ToString();
    EXPECT_NE(s.find("1"), std::string::npos);
    EXPECT_NE(s.find("2"), std::string::npos);
    EXPECT_NE(s.find("X"), std::string::npos);
    EXPECT_NE(s.find("Y"), std::string::npos);
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

TEST(Vector2Test, NormalizeOutRef)
{
    Vector2 v(3.0f, 4.0f);
    Vector2 result;
    Vector2::Normalize(v, result);
    EXPECT_NEAR(result.Length(), 1.0f, 1e-6f);
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

TEST(Vector2Test, AddOutRef)
{
    Vector2 a(1.0f, 2.0f);
    Vector2 b(3.0f, 4.0f);
    Vector2 result;
    Vector2::Add(a, b, result);
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

TEST(Vector2Test, SubtractOutRef)
{
    Vector2 a(5.0f, 7.0f);
    Vector2 b(2.0f, 3.0f);
    Vector2 result;
    Vector2::Subtract(a, b, result);
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

TEST(Vector2Test, MultiplyScalarOutRef)
{
    Vector2 v(2.0f, 3.0f);
    Vector2 result;
    Vector2::Multiply(v, 2.0f, result);
    EXPECT_FLOAT_EQ(result.X, 4.0f);
    EXPECT_FLOAT_EQ(result.Y, 6.0f);
}

TEST(Vector2Test, MultiplyVectorByVector)
{
    Vector2 result = Vector2::Multiply(Vector2(2.0f, 3.0f), Vector2(4.0f, 5.0f));
    EXPECT_FLOAT_EQ(result.X, 8.0f);
    EXPECT_FLOAT_EQ(result.Y, 15.0f);
}

TEST(Vector2Test, MultiplyVectorByVectorOutRef)
{
    Vector2 result;
    Vector2::Multiply(Vector2(2.0f, 3.0f), Vector2(4.0f, 5.0f), result);
    EXPECT_FLOAT_EQ(result.X, 8.0f);
    EXPECT_FLOAT_EQ(result.Y, 15.0f);
}

TEST(Vector2Test, DivideByScalar)
{
    Vector2 v(4.0f, 6.0f);
    Vector2 result = Vector2::Divide(v, 2.0f);
    EXPECT_FLOAT_EQ(result.X, 2.0f);
    EXPECT_FLOAT_EQ(result.Y, 3.0f);
}

TEST(Vector2Test, DivideScalarOutRef)
{
    Vector2 v(4.0f, 6.0f);
    Vector2 result;
    Vector2::Divide(v, 2.0f, result);
    EXPECT_FLOAT_EQ(result.X, 2.0f);
    EXPECT_FLOAT_EQ(result.Y, 3.0f);
}

TEST(Vector2Test, DivideVectorByVector)
{
    Vector2 result = Vector2::Divide(Vector2(6.0f, 9.0f), Vector2(2.0f, 3.0f));
    EXPECT_FLOAT_EQ(result.X, 3.0f);
    EXPECT_FLOAT_EQ(result.Y, 3.0f);
}

TEST(Vector2Test, DivideVectorByVectorOutRef)
{
    Vector2 result;
    Vector2::Divide(Vector2(6.0f, 9.0f), Vector2(2.0f, 3.0f), result);
    EXPECT_FLOAT_EQ(result.X, 3.0f);
    EXPECT_FLOAT_EQ(result.Y, 3.0f);
}

// --- Negate ---

TEST(Vector2Test, NegateStaticFlipsBothComponents)
{
    Vector2 result = Vector2::Negate(Vector2(1.0f, -2.0f));
    EXPECT_FLOAT_EQ(result.X, -1.0f);
    EXPECT_FLOAT_EQ(result.Y, 2.0f);
}

TEST(Vector2Test, NegateOutRef)
{
    Vector2 result;
    Vector2::Negate(Vector2(3.0f, -4.0f), result);
    EXPECT_FLOAT_EQ(result.X, -3.0f);
    EXPECT_FLOAT_EQ(result.Y, 4.0f);
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

TEST(Vector2Test, DotOutRef)
{
    float result = 0.0f;
    Vector2::Dot(Vector2(2.0f, 0.0f), Vector2(3.0f, 0.0f), result);
    EXPECT_FLOAT_EQ(result, 6.0f);
}

// --- Distance ---

TEST(Vector2Test, DistanceBetween3_4AndOriginIs5)
{
    EXPECT_FLOAT_EQ(Vector2::Distance(Vector2::Zero, Vector2(3.0f, 4.0f)), 5.0f);
}

TEST(Vector2Test, DistanceOutRef)
{
    float result = 0.0f;
    Vector2::Distance(Vector2::Zero, Vector2(3.0f, 4.0f), result);
    EXPECT_FLOAT_EQ(result, 5.0f);
}

TEST(Vector2Test, DistanceSquaredBetween3_4AndOriginIs25)
{
    EXPECT_FLOAT_EQ(Vector2::DistanceSquared(Vector2::Zero, Vector2(3.0f, 4.0f)), 25.0f);
}

TEST(Vector2Test, DistanceSquaredOutRef)
{
    float result = 0.0f;
    Vector2::DistanceSquared(Vector2::Zero, Vector2(3.0f, 4.0f), result);
    EXPECT_FLOAT_EQ(result, 25.0f);
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

TEST(Vector2Test, LerpOutRef)
{
    Vector2 result;
    Vector2::Lerp(Vector2::Zero, Vector2(4.0f, 2.0f), 0.5f, result);
    EXPECT_FLOAT_EQ(result.X, 2.0f);
    EXPECT_FLOAT_EQ(result.Y, 1.0f);
}

// --- SmoothStep ---

TEST(Vector2Test, SmoothStepAtZeroReturnsFirst)
{
    Vector2 result = Vector2::SmoothStep(Vector2::Zero, Vector2::One, 0.0f);
    EXPECT_FLOAT_EQ(result.X, 0.0f);
    EXPECT_FLOAT_EQ(result.Y, 0.0f);
}

TEST(Vector2Test, SmoothStepAtOneReturnsSecond)
{
    Vector2 result = Vector2::SmoothStep(Vector2::Zero, Vector2::One, 1.0f);
    EXPECT_FLOAT_EQ(result.X, 1.0f);
    EXPECT_FLOAT_EQ(result.Y, 1.0f);
}

TEST(Vector2Test, SmoothStepOutRef)
{
    Vector2 result;
    Vector2::SmoothStep(Vector2::Zero, Vector2::One, 0.0f, result);
    EXPECT_FLOAT_EQ(result.X, 0.0f);
    EXPECT_FLOAT_EQ(result.Y, 0.0f);
}

// --- Clamp ---

TEST(Vector2Test, ClampKeepsComponentsInRange)
{
    Vector2 result = Vector2::Clamp(Vector2(-5.0f, 10.0f), Vector2(0.0f, 0.0f), Vector2(5.0f, 5.0f));
    EXPECT_FLOAT_EQ(result.X, 0.0f);
    EXPECT_FLOAT_EQ(result.Y, 5.0f);
}

TEST(Vector2Test, ClampOutRef)
{
    Vector2 result;
    Vector2::Clamp(Vector2(-5.0f, 10.0f), Vector2(0.0f, 0.0f), Vector2(5.0f, 5.0f), result);
    EXPECT_FLOAT_EQ(result.X, 0.0f);
    EXPECT_FLOAT_EQ(result.Y, 5.0f);
}

// --- Min / Max ---

TEST(Vector2Test, MinReturnsComponentWiseMinimum)
{
    Vector2 result = Vector2::Min(Vector2(3.0f, 1.0f), Vector2(1.0f, 4.0f));
    EXPECT_FLOAT_EQ(result.X, 1.0f);
    EXPECT_FLOAT_EQ(result.Y, 1.0f);
}

TEST(Vector2Test, MinOutRef)
{
    Vector2 result;
    Vector2::Min(Vector2(3.0f, 1.0f), Vector2(1.0f, 4.0f), result);
    EXPECT_FLOAT_EQ(result.X, 1.0f);
    EXPECT_FLOAT_EQ(result.Y, 1.0f);
}

TEST(Vector2Test, MaxReturnsComponentWiseMaximum)
{
    Vector2 result = Vector2::Max(Vector2(3.0f, 1.0f), Vector2(1.0f, 4.0f));
    EXPECT_FLOAT_EQ(result.X, 3.0f);
    EXPECT_FLOAT_EQ(result.Y, 4.0f);
}

TEST(Vector2Test, MaxOutRef)
{
    Vector2 result;
    Vector2::Max(Vector2(3.0f, 1.0f), Vector2(1.0f, 4.0f), result);
    EXPECT_FLOAT_EQ(result.X, 3.0f);
    EXPECT_FLOAT_EQ(result.Y, 4.0f);
}

// --- Reflect ---

TEST(Vector2Test, ReflectAcrossXAxis)
{
    // Incoming: (1,-1), normal: (0,1) → reflected: (1,1)
    Vector2 result = Vector2::Reflect(Vector2(1.0f, -1.0f), Vector2::UnitY);
    EXPECT_NEAR(result.X, 1.0f, 1e-6f);
    EXPECT_NEAR(result.Y, 1.0f, 1e-6f);
}

TEST(Vector2Test, ReflectOutRef)
{
    Vector2 result;
    Vector2::Reflect(Vector2(1.0f, -1.0f), Vector2::UnitY, result);
    EXPECT_NEAR(result.X, 1.0f, 1e-6f);
    EXPECT_NEAR(result.Y, 1.0f, 1e-6f);
}

// --- Barycentric ---

TEST(Vector2Test, BarycentricAmount1OneReturnsV2)
{
    // v1 + 1*(v2-v1) + 0*(v3-v1) = v2
    Vector2 result = Vector2::Barycentric(
        Vector2(0.0f, 0.0f), Vector2(1.0f, 0.0f), Vector2(0.0f, 1.0f), 1.0f, 0.0f);
    EXPECT_NEAR(result.X, 1.0f, 1e-6f);
    EXPECT_NEAR(result.Y, 0.0f, 1e-6f);
}

TEST(Vector2Test, BarycentricOutRef)
{
    Vector2 result;
    Vector2::Barycentric(
        Vector2(0.0f, 0.0f), Vector2(1.0f, 0.0f), Vector2(0.0f, 1.0f), 0.0f, 1.0f, result);
    EXPECT_NEAR(result.X, 0.0f, 1e-6f);
    EXPECT_NEAR(result.Y, 1.0f, 1e-6f);
}

// --- CatmullRom ---

TEST(Vector2Test, CatmullRomAtZeroReturnsV2)
{
    Vector2 result = Vector2::CatmullRom(
        Vector2(-1.0f, 0.0f), Vector2(0.0f, 0.0f), Vector2(1.0f, 0.0f), Vector2(2.0f, 0.0f), 0.0f);
    EXPECT_NEAR(result.X, 0.0f, 1e-5f);
    EXPECT_NEAR(result.Y, 0.0f, 1e-5f);
}

TEST(Vector2Test, CatmullRomOutRef)
{
    Vector2 result;
    Vector2::CatmullRom(
        Vector2(-1.0f, 0.0f), Vector2(0.0f, 0.0f), Vector2(1.0f, 0.0f), Vector2(2.0f, 0.0f), 0.0f, result);
    EXPECT_NEAR(result.X, 0.0f, 1e-5f);
    EXPECT_NEAR(result.Y, 0.0f, 1e-5f);
}

// --- Hermite ---

TEST(Vector2Test, HermiteAtZeroReturnsV1)
{
    Vector2 result = Vector2::Hermite(
        Vector2(0.0f, 0.0f), Vector2(1.0f, 0.0f), Vector2(2.0f, 0.0f), Vector2(1.0f, 0.0f), 0.0f);
    EXPECT_NEAR(result.X, 0.0f, 1e-6f);
    EXPECT_NEAR(result.Y, 0.0f, 1e-6f);
}

TEST(Vector2Test, HermiteAtOneReturnsV2)
{
    Vector2 result = Vector2::Hermite(
        Vector2(0.0f, 0.0f), Vector2(1.0f, 0.0f), Vector2(2.0f, 0.0f), Vector2(1.0f, 0.0f), 1.0f);
    EXPECT_NEAR(result.X, 2.0f, 1e-5f);
    EXPECT_NEAR(result.Y, 0.0f, 1e-5f);
}

TEST(Vector2Test, HermiteOutRef)
{
    Vector2 result;
    Vector2::Hermite(
        Vector2(0.0f, 0.0f), Vector2(1.0f, 0.0f), Vector2(2.0f, 0.0f), Vector2(1.0f, 0.0f), 0.0f, result);
    EXPECT_NEAR(result.X, 0.0f, 1e-6f);
    EXPECT_NEAR(result.Y, 0.0f, 1e-6f);
}

// --- Transform by Matrix ---

TEST(Vector2Test, TransformByIdentityMatrix)
{
    Vector2 v(3.0f, 4.0f);
    Vector2 result = Vector2::Transform(v, Matrix::getIdentityProperty());
    EXPECT_NEAR(result.X, 3.0f, 1e-5f);
    EXPECT_NEAR(result.Y, 4.0f, 1e-5f);
}

TEST(Vector2Test, TransformByIdentityMatrixOutRef)
{
    Vector2 v(3.0f, 4.0f);
    Vector2 result;
    Vector2::Transform(v, Matrix::getIdentityProperty(), result);
    EXPECT_NEAR(result.X, 3.0f, 1e-5f);
    EXPECT_NEAR(result.Y, 4.0f, 1e-5f);
}

// --- TransformNormal by Matrix ---

TEST(Vector2Test, TransformNormalByIdentityMatrix)
{
    Vector2 n(1.0f, 0.0f);
    Vector2 result = Vector2::TransformNormal(n, Matrix::getIdentityProperty());
    EXPECT_NEAR(result.X, 1.0f, 1e-5f);
    EXPECT_NEAR(result.Y, 0.0f, 1e-5f);
}

TEST(Vector2Test, TransformNormalByIdentityMatrixOutRef)
{
    Vector2 n(1.0f, 0.0f);
    Vector2 result;
    Vector2::TransformNormal(n, Matrix::getIdentityProperty(), result);
    EXPECT_NEAR(result.X, 1.0f, 1e-5f);
    EXPECT_NEAR(result.Y, 0.0f, 1e-5f);
}

// --- Transform by Quaternion ---

TEST(Vector2Test, TransformByIdentityQuaternion)
{
    Vector2 v(3.0f, 4.0f);
    Vector2 result = Vector2::Transform(v, Quaternion::Identity);
    EXPECT_NEAR(result.X, 3.0f, 1e-5f);
    EXPECT_NEAR(result.Y, 4.0f, 1e-5f);
}

TEST(Vector2Test, TransformByIdentityQuaternionOutRef)
{
    Vector2 v(3.0f, 4.0f);
    Vector2 result;
    Vector2::Transform(v, Quaternion::Identity, result);
    EXPECT_NEAR(result.X, 3.0f, 1e-5f);
    EXPECT_NEAR(result.Y, 4.0f, 1e-5f);
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

TEST(Vector2Test, ScalarLeftMultiplicationOperator)
{
    Vector2 result = 3.0f * Vector2(2.0f, 3.0f);
    EXPECT_FLOAT_EQ(result.X, 6.0f);
    EXPECT_FLOAT_EQ(result.Y, 9.0f);
}

TEST(Vector2Test, ComponentWiseMultiplicationOperator)
{
    Vector2 result = Vector2(2.0f, 3.0f) * Vector2(4.0f, 5.0f);
    EXPECT_FLOAT_EQ(result.X, 8.0f);
    EXPECT_FLOAT_EQ(result.Y, 15.0f);
}

TEST(Vector2Test, DivisionByScalarOperator)
{
    Vector2 result = Vector2(6.0f, 9.0f) / 3.0f;
    EXPECT_FLOAT_EQ(result.X, 2.0f);
    EXPECT_FLOAT_EQ(result.Y, 3.0f);
}

TEST(Vector2Test, ComponentWiseDivisionOperator)
{
    Vector2 result = Vector2(6.0f, 9.0f) / Vector2(2.0f, 3.0f);
    EXPECT_FLOAT_EQ(result.X, 3.0f);
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

// C# synthesises `+=` and `-=` from the declared op_Addition/op_Subtraction, so XNA game
// code writes `position += delta`. C++ has to declare them; Vector3 already did, Vector2
// did not, which is what SAMPLE-020's `blocks[i].Position += new Vector2(0, BlockFallSpeed)`
// exposed.

TEST(Vector2Test, CompoundAddMatchesTheBinaryOperator)
{
    Vector2 accumulated(1.0f, 2.0f);
    const Vector2 delta(0.25f, -3.5f);

    accumulated += delta;

    const Vector2 expected = Vector2(1.0f, 2.0f) + delta;
    EXPECT_FLOAT_EQ(accumulated.X, expected.X);
    EXPECT_FLOAT_EQ(accumulated.Y, expected.Y);
}

TEST(Vector2Test, CompoundSubtractMatchesTheBinaryOperator)
{
    Vector2 accumulated(1.0f, 2.0f);
    const Vector2 delta(0.25f, -3.5f);

    accumulated -= delta;

    const Vector2 expected = Vector2(1.0f, 2.0f) - delta;
    EXPECT_FLOAT_EQ(accumulated.X, expected.X);
    EXPECT_FLOAT_EQ(accumulated.Y, expected.Y);
}

TEST(Vector2Test, CompoundOperatorsReturnThisSoTheyChain)
{
    Vector2 value(0.0f, 0.0f);
    const Vector2 one(1.0f, 1.0f);

    (value += one) += one;
    EXPECT_FLOAT_EQ(value.X, 2.0f);
    EXPECT_FLOAT_EQ(value.Y, 2.0f);

    (value -= one) -= one;
    EXPECT_FLOAT_EQ(value.X, 0.0f);
    EXPECT_FLOAT_EQ(value.Y, 0.0f);
}
