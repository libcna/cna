// SPDX-License-Identifier: MS-PL

#include <gtest/gtest.h>
#include <cmath>
#include "Microsoft/Xna/Framework/Vector4.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Quaternion.hpp"

using Microsoft::Xna::Framework::Matrix;
using Microsoft::Xna::Framework::Quaternion;
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

// --- Equals ---

TEST(Vector4Test, EqualsReturnsTrueForSameVector)
{
    Vector4 a(1.0f, 2.0f, 3.0f, 4.0f);
    Vector4 b(1.0f, 2.0f, 3.0f, 4.0f);
    EXPECT_TRUE(a.Equals(b));
}

TEST(Vector4Test, EqualsReturnsFalseForDifferentVector)
{
    Vector4 a(1.0f, 2.0f, 3.0f, 4.0f);
    Vector4 b(1.0f, 2.0f, 3.0f, 5.0f);
    EXPECT_FALSE(a.Equals(b));
}

// --- GetHashCode ---

TEST(Vector4Test, GetHashCodeEqualVectorsGiveEqualHash)
{
    Vector4 a(1.0f, 2.0f, 3.0f, 4.0f);
    Vector4 b(1.0f, 2.0f, 3.0f, 4.0f);
    EXPECT_EQ(a.GetHashCode(), b.GetHashCode());
}

TEST(Vector4Test, GetHashCodeDifferentVectorsTypicallyDiffer)
{
    Vector4 a(1.0f, 2.0f, 3.0f, 4.0f);
    Vector4 b(5.0f, 6.0f, 7.0f, 8.0f);
    EXPECT_NE(a.GetHashCode(), b.GetHashCode());
}

// --- ToString ---

TEST(Vector4Test, ToStringContainsXYZW)
{
    Vector4 v(1.0f, 2.0f, 3.0f, 4.0f);
    std::string s = v.ToString();
    EXPECT_NE(s.find("X"), std::string::npos);
    EXPECT_NE(s.find("Y"), std::string::npos);
    EXPECT_NE(s.find("Z"), std::string::npos);
    EXPECT_NE(s.find("W"), std::string::npos);
}

// --- Length ---

TEST(Vector4Test, LengthOfKnownVector)
{
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

TEST(Vector4Test, NormalizeOutRef)
{
    Vector4 result;
    Vector4::Normalize(Vector4(1.0f, 2.0f, 3.0f, 4.0f), result);
    EXPECT_NEAR(result.Length(), 1.0f, 1e-6f);
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
    EXPECT_FLOAT_EQ(Vector4::Dot(a, b), 70.0f);
}

TEST(Vector4Test, DotOutRef)
{
    float result = 0.0f;
    Vector4::Dot(Vector4(1.0f, 2.0f, 3.0f, 4.0f), Vector4(5.0f, 6.0f, 7.0f, 8.0f), result);
    EXPECT_FLOAT_EQ(result, 70.0f);
}

// --- Distance ---

TEST(Vector4Test, DistanceBetweenKnownVectors)
{
    EXPECT_FLOAT_EQ(Vector4::Distance(Vector4::Zero, Vector4(1.0f, 2.0f, 2.0f, 0.0f)), 3.0f);
}

TEST(Vector4Test, DistanceOutRef)
{
    float result = 0.0f;
    Vector4::Distance(Vector4::Zero, Vector4(1.0f, 2.0f, 2.0f, 0.0f), result);
    EXPECT_FLOAT_EQ(result, 3.0f);
}

TEST(Vector4Test, DistanceSquaredOutRef)
{
    float result = 0.0f;
    Vector4::DistanceSquared(Vector4::Zero, Vector4(1.0f, 2.0f, 2.0f, 0.0f), result);
    EXPECT_FLOAT_EQ(result, 9.0f);
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

TEST(Vector4Test, AddOutRef)
{
    Vector4 result;
    Vector4::Add(Vector4(1.0f, 2.0f, 3.0f, 4.0f), Vector4(5.0f, 6.0f, 7.0f, 8.0f), result);
    EXPECT_FLOAT_EQ(result.X, 6.0f);
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

TEST(Vector4Test, SubtractOutRef)
{
    Vector4 result;
    Vector4::Subtract(Vector4(5.0f, 6.0f, 7.0f, 8.0f), Vector4(1.0f, 2.0f, 3.0f, 4.0f), result);
    EXPECT_FLOAT_EQ(result.X, 4.0f);
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

TEST(Vector4Test, MultiplyScalarOutRef)
{
    Vector4 result;
    Vector4::Multiply(Vector4(1.0f, 2.0f, 3.0f, 4.0f), 2.0f, result);
    EXPECT_FLOAT_EQ(result.X, 2.0f);
    EXPECT_FLOAT_EQ(result.W, 8.0f);
}

TEST(Vector4Test, MultiplyVectorByVector)
{
    Vector4 result = Vector4::Multiply(Vector4(1.0f, 2.0f, 3.0f, 4.0f), Vector4(2.0f, 3.0f, 4.0f, 5.0f));
    EXPECT_FLOAT_EQ(result.X, 2.0f);
    EXPECT_FLOAT_EQ(result.Y, 6.0f);
    EXPECT_FLOAT_EQ(result.Z, 12.0f);
    EXPECT_FLOAT_EQ(result.W, 20.0f);
}

TEST(Vector4Test, MultiplyVectorByVectorOutRef)
{
    Vector4 result;
    Vector4::Multiply(Vector4(1.0f, 2.0f, 3.0f, 4.0f), Vector4(2.0f, 3.0f, 4.0f, 5.0f), result);
    EXPECT_FLOAT_EQ(result.X, 2.0f);
    EXPECT_FLOAT_EQ(result.W, 20.0f);
}

TEST(Vector4Test, DivideVectorByScalar)
{
    Vector4 result = Vector4::Divide(Vector4(2.0f, 4.0f, 6.0f, 8.0f), 2.0f);
    EXPECT_FLOAT_EQ(result.X, 1.0f);
    EXPECT_FLOAT_EQ(result.Y, 2.0f);
    EXPECT_FLOAT_EQ(result.Z, 3.0f);
    EXPECT_FLOAT_EQ(result.W, 4.0f);
}

TEST(Vector4Test, DivideScalarOutRef)
{
    Vector4 result;
    Vector4::Divide(Vector4(2.0f, 4.0f, 6.0f, 8.0f), 2.0f, result);
    EXPECT_FLOAT_EQ(result.X, 1.0f);
    EXPECT_FLOAT_EQ(result.W, 4.0f);
}

TEST(Vector4Test, DivideVectorByVector)
{
    Vector4 result = Vector4::Divide(Vector4(4.0f, 6.0f, 9.0f, 8.0f), Vector4(2.0f, 3.0f, 3.0f, 4.0f));
    EXPECT_FLOAT_EQ(result.X, 2.0f);
    EXPECT_FLOAT_EQ(result.W, 2.0f);
}

TEST(Vector4Test, DivideVectorByVectorOutRef)
{
    Vector4 result;
    Vector4::Divide(Vector4(4.0f, 6.0f, 9.0f, 8.0f), Vector4(2.0f, 3.0f, 3.0f, 4.0f), result);
    EXPECT_FLOAT_EQ(result.X, 2.0f);
    EXPECT_FLOAT_EQ(result.W, 2.0f);
}

// --- Negate ---

TEST(Vector4Test, NegateValue)
{
    Vector4 result = Vector4::Negate(Vector4(1.0f, -2.0f, 3.0f, -4.0f));
    EXPECT_FLOAT_EQ(result.X, -1.0f);
    EXPECT_FLOAT_EQ(result.Y, 2.0f);
    EXPECT_FLOAT_EQ(result.Z, -3.0f);
    EXPECT_FLOAT_EQ(result.W, 4.0f);
}

TEST(Vector4Test, NegateOutRef)
{
    Vector4 result;
    Vector4::Negate(Vector4(1.0f, -2.0f, 3.0f, -4.0f), result);
    EXPECT_FLOAT_EQ(result.X, -1.0f);
    EXPECT_FLOAT_EQ(result.W, 4.0f);
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

TEST(Vector4Test, LerpOutRef)
{
    Vector4 result;
    Vector4::Lerp(Vector4::Zero, Vector4(2.0f, 4.0f, 6.0f, 8.0f), 0.5f, result);
    EXPECT_FLOAT_EQ(result.X, 1.0f);
    EXPECT_FLOAT_EQ(result.W, 4.0f);
}

// --- SmoothStep ---

TEST(Vector4Test, SmoothStepAtZeroReturnsFirst)
{
    Vector4 result = Vector4::SmoothStep(Vector4::Zero, Vector4::One, 0.0f);
    EXPECT_FLOAT_EQ(result.X, 0.0f);
}

TEST(Vector4Test, SmoothStepAtOneReturnsSecond)
{
    Vector4 result = Vector4::SmoothStep(Vector4::Zero, Vector4::One, 1.0f);
    EXPECT_FLOAT_EQ(result.X, 1.0f);
}

TEST(Vector4Test, SmoothStepOutRef)
{
    Vector4 result;
    Vector4::SmoothStep(Vector4::Zero, Vector4::One, 0.0f, result);
    EXPECT_FLOAT_EQ(result.X, 0.0f);
}

// --- Clamp ---

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

TEST(Vector4Test, ClampOutRef)
{
    Vector4 result;
    Vector4::Clamp(Vector4(-1.0f, 0.5f, 2.0f, -5.0f), Vector4::Zero, Vector4::One, result);
    EXPECT_FLOAT_EQ(result.X, 0.0f);
    EXPECT_FLOAT_EQ(result.W, 0.0f);
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

TEST(Vector4Test, MinOutRef)
{
    Vector4 result;
    Vector4::Min(Vector4(3.0f, 1.0f, 5.0f, 2.0f), Vector4(1.0f, 4.0f, 2.0f, 7.0f), result);
    EXPECT_FLOAT_EQ(result.X, 1.0f);
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

TEST(Vector4Test, MaxOutRef)
{
    Vector4 result;
    Vector4::Max(Vector4(3.0f, 1.0f, 5.0f, 2.0f), Vector4(1.0f, 4.0f, 2.0f, 7.0f), result);
    EXPECT_FLOAT_EQ(result.Y, 4.0f);
    EXPECT_FLOAT_EQ(result.W, 7.0f);
}

// --- Barycentric ---

TEST(Vector4Test, BarycentricAmount1OneReturnsV2)
{
    Vector4 result = Vector4::Barycentric(
        Vector4(0.0f, 0.0f, 0.0f, 0.0f), Vector4(1.0f, 0.0f, 0.0f, 0.0f),
        Vector4(0.0f, 1.0f, 0.0f, 0.0f), 1.0f, 0.0f);
    EXPECT_NEAR(result.X, 1.0f, 1e-6f);
}

TEST(Vector4Test, BarycentricOutRef)
{
    Vector4 result;
    Vector4::Barycentric(
        Vector4(0.0f, 0.0f, 0.0f, 0.0f), Vector4(1.0f, 0.0f, 0.0f, 0.0f),
        Vector4(0.0f, 1.0f, 0.0f, 0.0f), 0.0f, 1.0f, result);
    EXPECT_NEAR(result.Y, 1.0f, 1e-6f);
}

// --- CatmullRom ---

TEST(Vector4Test, CatmullRomAtZeroReturnsV2)
{
    Vector4 result = Vector4::CatmullRom(
        Vector4(-1.0f, 0.0f, 0.0f, 0.0f), Vector4(0.0f, 0.0f, 0.0f, 0.0f),
        Vector4(1.0f, 0.0f, 0.0f, 0.0f), Vector4(2.0f, 0.0f, 0.0f, 0.0f), 0.0f);
    EXPECT_NEAR(result.X, 0.0f, 1e-5f);
}

TEST(Vector4Test, CatmullRomOutRef)
{
    Vector4 result;
    Vector4::CatmullRom(
        Vector4(-1.0f, 0.0f, 0.0f, 0.0f), Vector4(0.0f, 0.0f, 0.0f, 0.0f),
        Vector4(1.0f, 0.0f, 0.0f, 0.0f), Vector4(2.0f, 0.0f, 0.0f, 0.0f), 0.0f, result);
    EXPECT_NEAR(result.X, 0.0f, 1e-5f);
}

// --- Hermite ---

TEST(Vector4Test, HermiteAtZeroReturnsV1)
{
    Vector4 result = Vector4::Hermite(
        Vector4(0.0f, 0.0f, 0.0f, 0.0f), Vector4(1.0f, 0.0f, 0.0f, 0.0f),
        Vector4(2.0f, 0.0f, 0.0f, 0.0f), Vector4(1.0f, 0.0f, 0.0f, 0.0f), 0.0f);
    EXPECT_NEAR(result.X, 0.0f, 1e-6f);
}

TEST(Vector4Test, HermiteAtOneReturnsV2)
{
    Vector4 result = Vector4::Hermite(
        Vector4(0.0f, 0.0f, 0.0f, 0.0f), Vector4(1.0f, 0.0f, 0.0f, 0.0f),
        Vector4(2.0f, 0.0f, 0.0f, 0.0f), Vector4(1.0f, 0.0f, 0.0f, 0.0f), 1.0f);
    EXPECT_NEAR(result.X, 2.0f, 1e-5f);
}

TEST(Vector4Test, HermiteOutRef)
{
    Vector4 result;
    Vector4::Hermite(
        Vector4(0.0f, 0.0f, 0.0f, 0.0f), Vector4(1.0f, 0.0f, 0.0f, 0.0f),
        Vector4(2.0f, 0.0f, 0.0f, 0.0f), Vector4(1.0f, 0.0f, 0.0f, 0.0f), 0.0f, result);
    EXPECT_NEAR(result.X, 0.0f, 1e-6f);
}

// --- Transform by Matrix (Vector4) ---

TEST(Vector4Test, TransformVector4ByIdentityMatrix)
{
    Vector4 v(1.0f, 2.0f, 3.0f, 4.0f);
    Vector4 result = Vector4::Transform(v, Matrix::getIdentityProperty());
    EXPECT_NEAR(result.X, 1.0f, 1e-5f);
    EXPECT_NEAR(result.Y, 2.0f, 1e-5f);
    EXPECT_NEAR(result.Z, 3.0f, 1e-5f);
    EXPECT_NEAR(result.W, 4.0f, 1e-5f);
}

TEST(Vector4Test, TransformVector4ByIdentityMatrixOutRef)
{
    Vector4 v(1.0f, 2.0f, 3.0f, 4.0f);
    Vector4 result;
    Vector4::Transform(v, Matrix::getIdentityProperty(), result);
    EXPECT_NEAR(result.X, 1.0f, 1e-5f);
    EXPECT_NEAR(result.W, 4.0f, 1e-5f);
}

TEST(Vector4Test, TransformVector2ByIdentityMatrix)
{
    Vector4 result = Vector4::Transform(Vector2(3.0f, 4.0f), Matrix::getIdentityProperty());
    EXPECT_NEAR(result.X, 3.0f, 1e-5f);
    EXPECT_NEAR(result.Y, 4.0f, 1e-5f);
}

TEST(Vector4Test, TransformVector2ByIdentityMatrixOutRef)
{
    Vector4 result;
    Vector4::Transform(Vector2(3.0f, 4.0f), Matrix::getIdentityProperty(), result);
    EXPECT_NEAR(result.X, 3.0f, 1e-5f);
    EXPECT_NEAR(result.Y, 4.0f, 1e-5f);
}

TEST(Vector4Test, TransformVector3ByIdentityMatrix)
{
    Vector4 result = Vector4::Transform(Vector3(1.0f, 2.0f, 3.0f), Matrix::getIdentityProperty());
    EXPECT_NEAR(result.X, 1.0f, 1e-5f);
    EXPECT_NEAR(result.Y, 2.0f, 1e-5f);
    EXPECT_NEAR(result.Z, 3.0f, 1e-5f);
}

TEST(Vector4Test, TransformVector3ByIdentityMatrixOutRef)
{
    Vector4 result;
    Vector4::Transform(Vector3(1.0f, 2.0f, 3.0f), Matrix::getIdentityProperty(), result);
    EXPECT_NEAR(result.X, 1.0f, 1e-5f);
    EXPECT_NEAR(result.Z, 3.0f, 1e-5f);
}

// --- Transform by Quaternion ---

TEST(Vector4Test, TransformVector4ByIdentityQuaternion)
{
    Vector4 v(1.0f, 2.0f, 3.0f, 4.0f);
    Vector4 result = Vector4::Transform(v, Quaternion::Identity);
    EXPECT_NEAR(result.X, 1.0f, 1e-5f);
    EXPECT_NEAR(result.Y, 2.0f, 1e-5f);
    EXPECT_NEAR(result.Z, 3.0f, 1e-5f);
    EXPECT_NEAR(result.W, 4.0f, 1e-5f);
}

TEST(Vector4Test, TransformVector4ByIdentityQuaternionOutRef)
{
    Vector4 v(1.0f, 2.0f, 3.0f, 4.0f);
    Vector4 result;
    Vector4::Transform(v, Quaternion::Identity, result);
    EXPECT_NEAR(result.X, 1.0f, 1e-5f);
    EXPECT_NEAR(result.W, 4.0f, 1e-5f);
}

TEST(Vector4Test, TransformVector2ByIdentityQuaternion)
{
    Vector4 result = Vector4::Transform(Vector2(3.0f, 4.0f), Quaternion::Identity);
    EXPECT_NEAR(result.X, 3.0f, 1e-5f);
    EXPECT_NEAR(result.Y, 4.0f, 1e-5f);
}

TEST(Vector4Test, TransformVector3ByIdentityQuaternion)
{
    Vector4 result = Vector4::Transform(Vector3(1.0f, 2.0f, 3.0f), Quaternion::Identity);
    EXPECT_NEAR(result.X, 1.0f, 1e-5f);
    EXPECT_NEAR(result.Y, 2.0f, 1e-5f);
    EXPECT_NEAR(result.Z, 3.0f, 1e-5f);
}

// --- Operators ---

TEST(Vector4Test, AdditionOperator)
{
    Vector4 result = Vector4(1.0f, 2.0f, 3.0f, 4.0f) + Vector4(5.0f, 6.0f, 7.0f, 8.0f);
    EXPECT_FLOAT_EQ(result.X, 6.0f);
    EXPECT_FLOAT_EQ(result.W, 12.0f);
}

TEST(Vector4Test, SubtractionOperator)
{
    Vector4 result = Vector4(5.0f, 6.0f, 7.0f, 8.0f) - Vector4(1.0f, 2.0f, 3.0f, 4.0f);
    EXPECT_FLOAT_EQ(result.X, 4.0f);
    EXPECT_FLOAT_EQ(result.W, 4.0f);
}

TEST(Vector4Test, NegationOperator)
{
    Vector4 result = -Vector4(1.0f, -2.0f, 3.0f, -4.0f);
    EXPECT_FLOAT_EQ(result.X, -1.0f);
    EXPECT_FLOAT_EQ(result.Y, 2.0f);
    EXPECT_FLOAT_EQ(result.Z, -3.0f);
    EXPECT_FLOAT_EQ(result.W, 4.0f);
}

TEST(Vector4Test, ScalarMultiplicationOperator)
{
    Vector4 result = Vector4(1.0f, 2.0f, 3.0f, 4.0f) * 2.0f;
    EXPECT_FLOAT_EQ(result.X, 2.0f);
    EXPECT_FLOAT_EQ(result.W, 8.0f);
}

TEST(Vector4Test, ScalarLeftMultiplicationOperator)
{
    Vector4 result = 2.0f * Vector4(1.0f, 2.0f, 3.0f, 4.0f);
    EXPECT_FLOAT_EQ(result.X, 2.0f);
    EXPECT_FLOAT_EQ(result.W, 8.0f);
}

TEST(Vector4Test, ComponentWiseMultiplicationOperator)
{
    Vector4 result = Vector4(1.0f, 2.0f, 3.0f, 4.0f) * Vector4(2.0f, 3.0f, 4.0f, 5.0f);
    EXPECT_FLOAT_EQ(result.X, 2.0f);
    EXPECT_FLOAT_EQ(result.W, 20.0f);
}

TEST(Vector4Test, DivisionByScalarOperator)
{
    Vector4 result = Vector4(2.0f, 4.0f, 6.0f, 8.0f) / 2.0f;
    EXPECT_FLOAT_EQ(result.X, 1.0f);
    EXPECT_FLOAT_EQ(result.W, 4.0f);
}

TEST(Vector4Test, ComponentWiseDivisionOperator)
{
    Vector4 result = Vector4(4.0f, 6.0f, 9.0f, 8.0f) / Vector4(2.0f, 3.0f, 3.0f, 4.0f);
    EXPECT_FLOAT_EQ(result.X, 2.0f);
    EXPECT_FLOAT_EQ(result.W, 2.0f);
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
