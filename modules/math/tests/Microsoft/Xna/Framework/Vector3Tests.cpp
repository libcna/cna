// SPDX-License-Identifier: MS-PL

#include <gtest/gtest.h>
#include <bit>
#include <cmath>
#include <cstdint>
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Quaternion.hpp"

using Microsoft::Xna::Framework::Matrix;
using Microsoft::Xna::Framework::Quaternion;
using Microsoft::Xna::Framework::Vector2;
using Microsoft::Xna::Framework::Vector3;

// --- Static constants ---

TEST(Vector3Test, ZeroHasAllComponentsZero)
{
    EXPECT_FLOAT_EQ(Vector3::Zero.X, 0.0f);
    EXPECT_FLOAT_EQ(Vector3::Zero.Y, 0.0f);
    EXPECT_FLOAT_EQ(Vector3::Zero.Z, 0.0f);
}

TEST(Vector3Test, OneHasAllComponentsOne)
{
    EXPECT_FLOAT_EQ(Vector3::One.X, 1.0f);
    EXPECT_FLOAT_EQ(Vector3::One.Y, 1.0f);
    EXPECT_FLOAT_EQ(Vector3::One.Z, 1.0f);
}

TEST(Vector3Test, UnitVectors)
{
    EXPECT_FLOAT_EQ(Vector3::UnitX.X, 1.0f);
    EXPECT_FLOAT_EQ(Vector3::UnitX.Y, 0.0f);
    EXPECT_FLOAT_EQ(Vector3::UnitX.Z, 0.0f);

    EXPECT_FLOAT_EQ(Vector3::UnitY.X, 0.0f);
    EXPECT_FLOAT_EQ(Vector3::UnitY.Y, 1.0f);
    EXPECT_FLOAT_EQ(Vector3::UnitY.Z, 0.0f);

    EXPECT_FLOAT_EQ(Vector3::UnitZ.X, 0.0f);
    EXPECT_FLOAT_EQ(Vector3::UnitZ.Y, 0.0f);
    EXPECT_FLOAT_EQ(Vector3::UnitZ.Z, 1.0f);
}

TEST(Vector3Test, DirectionConstantsMatchXnaConvention)
{
    EXPECT_FLOAT_EQ(Vector3::Up.Y, 1.0f);
    EXPECT_FLOAT_EQ(Vector3::Down.Y, -1.0f);
    EXPECT_FLOAT_EQ(Vector3::Right.X, 1.0f);
    EXPECT_FLOAT_EQ(Vector3::Left.X, -1.0f);
    EXPECT_FLOAT_EQ(Vector3::Forward.Z, -1.0f);
    EXPECT_FLOAT_EQ(Vector3::Backward.Z, 1.0f);
}

// --- Construction ---

TEST(Vector3Test, DefaultConstructorIsZero)
{
    Vector3 v;
    EXPECT_FLOAT_EQ(v.X, 0.0f);
    EXPECT_FLOAT_EQ(v.Y, 0.0f);
    EXPECT_FLOAT_EQ(v.Z, 0.0f);
}

TEST(Vector3Test, ThreeComponentConstructor)
{
    Vector3 v(1.0f, 2.0f, 3.0f);
    EXPECT_FLOAT_EQ(v.X, 1.0f);
    EXPECT_FLOAT_EQ(v.Y, 2.0f);
    EXPECT_FLOAT_EQ(v.Z, 3.0f);
}

TEST(Vector3Test, ScalarConstructorSetsAllComponents)
{
    Vector3 v(5.0f);
    EXPECT_FLOAT_EQ(v.X, 5.0f);
    EXPECT_FLOAT_EQ(v.Y, 5.0f);
    EXPECT_FLOAT_EQ(v.Z, 5.0f);
}

TEST(Vector3Test, ConstructFromVector2AndZ)
{
    Vector3 v(Vector2(1.0f, 2.0f), 3.0f);
    EXPECT_FLOAT_EQ(v.X, 1.0f);
    EXPECT_FLOAT_EQ(v.Y, 2.0f);
    EXPECT_FLOAT_EQ(v.Z, 3.0f);
}

// --- Equals ---

TEST(Vector3Test, EqualsReturnsTrueForSameVector)
{
    Vector3 a(1.0f, 2.0f, 3.0f);
    Vector3 b(1.0f, 2.0f, 3.0f);
    EXPECT_TRUE(a.Equals(b));
}

TEST(Vector3Test, EqualsReturnsFalseForDifferentVector)
{
    Vector3 a(1.0f, 2.0f, 3.0f);
    Vector3 b(1.0f, 2.0f, 4.0f);
    EXPECT_FALSE(a.Equals(b));
}

// --- GetHashCode ---

TEST(Vector3Test, GetHashCodeEqualVectorsGiveEqualHash)
{
    Vector3 a(1.0f, 2.0f, 3.0f);
    Vector3 b(1.0f, 2.0f, 3.0f);
    EXPECT_EQ(a.GetHashCode(), b.GetHashCode());
}

TEST(Vector3Test, GetHashCodeDifferentVectorsTypicallyDiffer)
{
    Vector3 a(1.0f, 2.0f, 3.0f);
    Vector3 b(4.0f, 5.0f, 6.0f);
    EXPECT_NE(a.GetHashCode(), b.GetHashCode());
}

// --- ToString ---

TEST(Vector3Test, ToStringContainsXYZ)
{
    Vector3 v(1.0f, 2.0f, 3.0f);
    std::string s = v.ToString();
    EXPECT_NE(s.find("X"), std::string::npos);
    EXPECT_NE(s.find("Y"), std::string::npos);
    EXPECT_NE(s.find("Z"), std::string::npos);
}

// --- Length ---

TEST(Vector3Test, LengthOfKnown345Vector)
{
    Vector3 v(3.0f, 4.0f, 0.0f);
    EXPECT_FLOAT_EQ(v.Length(), 5.0f);
}

TEST(Vector3Test, LengthSquaredOf3_4_0Is25)
{
    Vector3 v(3.0f, 4.0f, 0.0f);
    EXPECT_FLOAT_EQ(v.LengthSquared(), 25.0f);
}

TEST(Vector3Test, UnitVectorHasLengthOne)
{
    EXPECT_NEAR(Vector3::UnitX.Length(), 1.0f, 1e-6f);
    EXPECT_NEAR(Vector3::UnitY.Length(), 1.0f, 1e-6f);
    EXPECT_NEAR(Vector3::UnitZ.Length(), 1.0f, 1e-6f);
}

// --- Normalize ---

TEST(Vector3Test, NormalizeInPlaceProducesUnitVector)
{
    Vector3 v(0.0f, 0.0f, 5.0f);
    v.Normalize();
    EXPECT_NEAR(v.Length(), 1.0f, 1e-6f);
    EXPECT_FLOAT_EQ(v.Z, 1.0f);
}

TEST(Vector3Test, NormalizeStaticPreservesOriginal)
{
    Vector3 original(1.0f, 2.0f, 3.0f);
    Vector3 result = Vector3::Normalize(original);
    EXPECT_NEAR(result.Length(), 1.0f, 1e-6f);
    EXPECT_FLOAT_EQ(original.X, 1.0f);
}

TEST(Vector3Test, NormalizeOutRef)
{
    Vector3 result;
    Vector3::Normalize(Vector3(1.0f, 2.0f, 3.0f), result);
    EXPECT_NEAR(result.Length(), 1.0f, 1e-6f);
}

// --- Cross product ---

TEST(Vector3Test, CrossXcrossYEqualsZ)
{
    Vector3 result = Vector3::Cross(Vector3::UnitX, Vector3::UnitY);
    EXPECT_NEAR(result.X, 0.0f, 1e-6f);
    EXPECT_NEAR(result.Y, 0.0f, 1e-6f);
    EXPECT_NEAR(result.Z, 1.0f, 1e-6f);
}

TEST(Vector3Test, CrossYcrossXEqualsNegativeZ)
{
    Vector3 result = Vector3::Cross(Vector3::UnitY, Vector3::UnitX);
    EXPECT_NEAR(result.Z, -1.0f, 1e-6f);
}

TEST(Vector3Test, CrossParallelVectorsIsZero)
{
    Vector3 result = Vector3::Cross(Vector3::UnitX, Vector3::UnitX);
    EXPECT_NEAR(result.Length(), 0.0f, 1e-6f);
}

TEST(Vector3Test, CrossOutRef)
{
    Vector3 result;
    Vector3::Cross(Vector3::UnitX, Vector3::UnitY, result);
    EXPECT_NEAR(result.Z, 1.0f, 1e-6f);
}

// --- Dot product ---

TEST(Vector3Test, DotOfOrthogonalVectorsIsZero)
{
    EXPECT_FLOAT_EQ(Vector3::Dot(Vector3::UnitX, Vector3::UnitY), 0.0f);
    EXPECT_FLOAT_EQ(Vector3::Dot(Vector3::UnitX, Vector3::UnitZ), 0.0f);
}

TEST(Vector3Test, DotOfParallelVectorsIsProduct)
{
    Vector3 a(2.0f, 0.0f, 0.0f);
    Vector3 b(3.0f, 0.0f, 0.0f);
    EXPECT_FLOAT_EQ(Vector3::Dot(a, b), 6.0f);
}

TEST(Vector3Test, DotProductSymmetry)
{
    Vector3 a(1.0f, 2.0f, 3.0f);
    Vector3 b(4.0f, 5.0f, 6.0f);
    EXPECT_FLOAT_EQ(Vector3::Dot(a, b), Vector3::Dot(b, a));
}

TEST(Vector3Test, DotOutRef)
{
    float result = 0.0f;
    Vector3::Dot(Vector3(2.0f, 0.0f, 0.0f), Vector3(3.0f, 0.0f, 0.0f), result);
    EXPECT_FLOAT_EQ(result, 6.0f);
}

// --- Distance ---

TEST(Vector3Test, DistanceBetween3_4_0AndOriginIs5)
{
    EXPECT_FLOAT_EQ(Vector3::Distance(Vector3::Zero, Vector3(3.0f, 4.0f, 0.0f)), 5.0f);
}

TEST(Vector3Test, DistanceOutRef)
{
    float result = 0.0f;
    Vector3::Distance(Vector3::Zero, Vector3(3.0f, 4.0f, 0.0f), result);
    EXPECT_FLOAT_EQ(result, 5.0f);
}

TEST(Vector3Test, DistanceSquaredBetween3_4_0AndOriginIs25)
{
    EXPECT_FLOAT_EQ(Vector3::DistanceSquared(Vector3::Zero, Vector3(3.0f, 4.0f, 0.0f)), 25.0f);
}

TEST(Vector3Test, DistanceSquaredOutRef)
{
    float result = 0.0f;
    Vector3::DistanceSquared(Vector3::Zero, Vector3(3.0f, 4.0f, 0.0f), result);
    EXPECT_FLOAT_EQ(result, 25.0f);
}

// --- Arithmetic ---

TEST(Vector3Test, AddTwoVectors)
{
    Vector3 result = Vector3::Add(Vector3(1.0f, 2.0f, 3.0f), Vector3(4.0f, 5.0f, 6.0f));
    EXPECT_FLOAT_EQ(result.X, 5.0f);
    EXPECT_FLOAT_EQ(result.Y, 7.0f);
    EXPECT_FLOAT_EQ(result.Z, 9.0f);
}

TEST(Vector3Test, AddOutRef)
{
    Vector3 result;
    Vector3::Add(Vector3(1.0f, 2.0f, 3.0f), Vector3(4.0f, 5.0f, 6.0f), result);
    EXPECT_FLOAT_EQ(result.X, 5.0f);
    EXPECT_FLOAT_EQ(result.Y, 7.0f);
    EXPECT_FLOAT_EQ(result.Z, 9.0f);
}

TEST(Vector3Test, SubtractTwoVectors)
{
    Vector3 result = Vector3::Subtract(Vector3(5.0f, 7.0f, 9.0f), Vector3(1.0f, 2.0f, 3.0f));
    EXPECT_FLOAT_EQ(result.X, 4.0f);
    EXPECT_FLOAT_EQ(result.Y, 5.0f);
    EXPECT_FLOAT_EQ(result.Z, 6.0f);
}

TEST(Vector3Test, SubtractOutRef)
{
    Vector3 result;
    Vector3::Subtract(Vector3(5.0f, 7.0f, 9.0f), Vector3(1.0f, 2.0f, 3.0f), result);
    EXPECT_FLOAT_EQ(result.X, 4.0f);
    EXPECT_FLOAT_EQ(result.Y, 5.0f);
    EXPECT_FLOAT_EQ(result.Z, 6.0f);
}

TEST(Vector3Test, MultiplyByScalar)
{
    Vector3 result = Vector3::Multiply(Vector3(1.0f, 2.0f, 3.0f), 2.0f);
    EXPECT_FLOAT_EQ(result.X, 2.0f);
    EXPECT_FLOAT_EQ(result.Y, 4.0f);
    EXPECT_FLOAT_EQ(result.Z, 6.0f);
}

TEST(Vector3Test, MultiplyScalarOutRef)
{
    Vector3 result;
    Vector3::Multiply(Vector3(1.0f, 2.0f, 3.0f), 2.0f, result);
    EXPECT_FLOAT_EQ(result.X, 2.0f);
    EXPECT_FLOAT_EQ(result.Y, 4.0f);
    EXPECT_FLOAT_EQ(result.Z, 6.0f);
}

TEST(Vector3Test, MultiplyVectorByVector)
{
    Vector3 result = Vector3::Multiply(Vector3(1.0f, 2.0f, 3.0f), Vector3(4.0f, 5.0f, 6.0f));
    EXPECT_FLOAT_EQ(result.X, 4.0f);
    EXPECT_FLOAT_EQ(result.Y, 10.0f);
    EXPECT_FLOAT_EQ(result.Z, 18.0f);
}

TEST(Vector3Test, MultiplyVectorByVectorOutRef)
{
    Vector3 result;
    Vector3::Multiply(Vector3(1.0f, 2.0f, 3.0f), Vector3(4.0f, 5.0f, 6.0f), result);
    EXPECT_FLOAT_EQ(result.X, 4.0f);
    EXPECT_FLOAT_EQ(result.Y, 10.0f);
    EXPECT_FLOAT_EQ(result.Z, 18.0f);
}

TEST(Vector3Test, DivideByScalar)
{
    Vector3 result = Vector3::Divide(Vector3(2.0f, 4.0f, 6.0f), 2.0f);
    EXPECT_FLOAT_EQ(result.X, 1.0f);
    EXPECT_FLOAT_EQ(result.Y, 2.0f);
    EXPECT_FLOAT_EQ(result.Z, 3.0f);
}

TEST(Vector3Test, DivideScalarOutRef)
{
    Vector3 result;
    Vector3::Divide(Vector3(2.0f, 4.0f, 6.0f), 2.0f, result);
    EXPECT_FLOAT_EQ(result.X, 1.0f);
    EXPECT_FLOAT_EQ(result.Y, 2.0f);
    EXPECT_FLOAT_EQ(result.Z, 3.0f);
}

TEST(Vector3Test, DivideVectorByVector)
{
    Vector3 result = Vector3::Divide(Vector3(4.0f, 6.0f, 9.0f), Vector3(2.0f, 3.0f, 3.0f));
    EXPECT_FLOAT_EQ(result.X, 2.0f);
    EXPECT_FLOAT_EQ(result.Y, 2.0f);
    EXPECT_FLOAT_EQ(result.Z, 3.0f);
}

TEST(Vector3Test, DivideVectorByVectorOutRef)
{
    Vector3 result;
    Vector3::Divide(Vector3(4.0f, 6.0f, 9.0f), Vector3(2.0f, 3.0f, 3.0f), result);
    EXPECT_FLOAT_EQ(result.X, 2.0f);
    EXPECT_FLOAT_EQ(result.Y, 2.0f);
    EXPECT_FLOAT_EQ(result.Z, 3.0f);
}

// --- Negate ---

TEST(Vector3Test, NegateValue)
{
    Vector3 result = Vector3::Negate(Vector3(1.0f, -2.0f, 3.0f));
    EXPECT_FLOAT_EQ(result.X, -1.0f);
    EXPECT_FLOAT_EQ(result.Y, 2.0f);
    EXPECT_FLOAT_EQ(result.Z, -3.0f);
}

TEST(Vector3Test, NegateOutRef)
{
    Vector3 result;
    Vector3::Negate(Vector3(1.0f, -2.0f, 3.0f), result);
    EXPECT_FLOAT_EQ(result.X, -1.0f);
    EXPECT_FLOAT_EQ(result.Y, 2.0f);
    EXPECT_FLOAT_EQ(result.Z, -3.0f);
}

// --- Lerp ---

TEST(Vector3Test, LerpAtHalfReturnsMidpoint)
{
    Vector3 result = Vector3::Lerp(Vector3::Zero, Vector3(2.0f, 4.0f, 6.0f), 0.5f);
    EXPECT_FLOAT_EQ(result.X, 1.0f);
    EXPECT_FLOAT_EQ(result.Y, 2.0f);
    EXPECT_FLOAT_EQ(result.Z, 3.0f);
}

TEST(Vector3Test, LerpOutRef)
{
    Vector3 result;
    Vector3::Lerp(Vector3::Zero, Vector3(2.0f, 4.0f, 6.0f), 0.5f, result);
    EXPECT_FLOAT_EQ(result.X, 1.0f);
    EXPECT_FLOAT_EQ(result.Y, 2.0f);
    EXPECT_FLOAT_EQ(result.Z, 3.0f);
}

// --- SmoothStep ---

TEST(Vector3Test, SmoothStepAtZeroReturnsFirst)
{
    Vector3 result = Vector3::SmoothStep(Vector3::Zero, Vector3::One, 0.0f);
    EXPECT_FLOAT_EQ(result.X, 0.0f);
}

TEST(Vector3Test, SmoothStepAtOneReturnsSecond)
{
    Vector3 result = Vector3::SmoothStep(Vector3::Zero, Vector3::One, 1.0f);
    EXPECT_FLOAT_EQ(result.X, 1.0f);
}

TEST(Vector3Test, SmoothStepOutRef)
{
    Vector3 result;
    Vector3::SmoothStep(Vector3::Zero, Vector3::One, 0.0f, result);
    EXPECT_FLOAT_EQ(result.X, 0.0f);
}

// --- Clamp ---

TEST(Vector3Test, ClampKeepsComponentsInRange)
{
    Vector3 result = Vector3::Clamp(Vector3(-5.0f, 10.0f, 3.0f),
                                    Vector3(0.0f, 0.0f, 0.0f), Vector3(5.0f, 5.0f, 5.0f));
    EXPECT_FLOAT_EQ(result.X, 0.0f);
    EXPECT_FLOAT_EQ(result.Y, 5.0f);
    EXPECT_FLOAT_EQ(result.Z, 3.0f);
}

TEST(Vector3Test, ClampOutRef)
{
    Vector3 result;
    Vector3::Clamp(Vector3(-5.0f, 10.0f, 3.0f),
                   Vector3(0.0f, 0.0f, 0.0f), Vector3(5.0f, 5.0f, 5.0f), result);
    EXPECT_FLOAT_EQ(result.X, 0.0f);
    EXPECT_FLOAT_EQ(result.Y, 5.0f);
    EXPECT_FLOAT_EQ(result.Z, 3.0f);
}

// --- Min / Max ---

TEST(Vector3Test, MinReturnsComponentWiseMinimum)
{
    Vector3 result = Vector3::Min(Vector3(3.0f, 1.0f, 5.0f), Vector3(1.0f, 4.0f, 2.0f));
    EXPECT_FLOAT_EQ(result.X, 1.0f);
    EXPECT_FLOAT_EQ(result.Y, 1.0f);
    EXPECT_FLOAT_EQ(result.Z, 2.0f);
}

TEST(Vector3Test, MinOutRef)
{
    Vector3 result;
    Vector3::Min(Vector3(3.0f, 1.0f, 5.0f), Vector3(1.0f, 4.0f, 2.0f), result);
    EXPECT_FLOAT_EQ(result.X, 1.0f);
    EXPECT_FLOAT_EQ(result.Z, 2.0f);
}

TEST(Vector3Test, MaxReturnsComponentWiseMaximum)
{
    Vector3 result = Vector3::Max(Vector3(3.0f, 1.0f, 5.0f), Vector3(1.0f, 4.0f, 2.0f));
    EXPECT_FLOAT_EQ(result.X, 3.0f);
    EXPECT_FLOAT_EQ(result.Y, 4.0f);
    EXPECT_FLOAT_EQ(result.Z, 5.0f);
}

TEST(Vector3Test, MaxOutRef)
{
    Vector3 result;
    Vector3::Max(Vector3(3.0f, 1.0f, 5.0f), Vector3(1.0f, 4.0f, 2.0f), result);
    EXPECT_FLOAT_EQ(result.Y, 4.0f);
    EXPECT_FLOAT_EQ(result.Z, 5.0f);
}

// --- Reflect ---

TEST(Vector3Test, ReflectAcrossXYPlane)
{
    Vector3 result = Vector3::Reflect(Vector3(0.0f, -1.0f, 0.0f), Vector3::UnitY);
    EXPECT_NEAR(result.X, 0.0f, 1e-6f);
    EXPECT_NEAR(result.Y, 1.0f, 1e-6f);
    EXPECT_NEAR(result.Z, 0.0f, 1e-6f);
}

TEST(Vector3Test, ReflectOutRef)
{
    Vector3 result;
    Vector3::Reflect(Vector3(0.0f, -1.0f, 0.0f), Vector3::UnitY, result);
    EXPECT_NEAR(result.Y, 1.0f, 1e-6f);
}

// --- Barycentric ---

TEST(Vector3Test, BarycentricAmount1OneReturnsV2)
{
    Vector3 result = Vector3::Barycentric(
        Vector3(0.0f, 0.0f, 0.0f), Vector3(1.0f, 0.0f, 0.0f), Vector3(0.0f, 1.0f, 0.0f), 1.0f, 0.0f);
    EXPECT_NEAR(result.X, 1.0f, 1e-6f);
    EXPECT_NEAR(result.Y, 0.0f, 1e-6f);
}

TEST(Vector3Test, BarycentricOutRef)
{
    Vector3 result;
    Vector3::Barycentric(
        Vector3(0.0f, 0.0f, 0.0f), Vector3(1.0f, 0.0f, 0.0f), Vector3(0.0f, 1.0f, 0.0f), 0.0f, 1.0f, result);
    EXPECT_NEAR(result.X, 0.0f, 1e-6f);
    EXPECT_NEAR(result.Y, 1.0f, 1e-6f);
}

// --- CatmullRom ---

TEST(Vector3Test, CatmullRomAtZeroReturnsV2)
{
    Vector3 result = Vector3::CatmullRom(
        Vector3(-1.0f, 0.0f, 0.0f), Vector3(0.0f, 0.0f, 0.0f),
        Vector3(1.0f, 0.0f, 0.0f), Vector3(2.0f, 0.0f, 0.0f), 0.0f);
    EXPECT_NEAR(result.X, 0.0f, 1e-5f);
}

TEST(Vector3Test, CatmullRomOutRef)
{
    Vector3 result;
    Vector3::CatmullRom(
        Vector3(-1.0f, 0.0f, 0.0f), Vector3(0.0f, 0.0f, 0.0f),
        Vector3(1.0f, 0.0f, 0.0f), Vector3(2.0f, 0.0f, 0.0f), 0.0f, result);
    EXPECT_NEAR(result.X, 0.0f, 1e-5f);
}

TEST(Vector3Test, CatmullRomMatchesXnaComponentPrecision)
{
    const Vector3 result = Vector3::CatmullRom(
        Vector3(1732.28723f, 0.0f, 0.0f),
        Vector3(1593.86609f, 943.755737f, 102.217514f),
        Vector3(1325.83545f, 832.348f, 53.6925468f),
        Vector3(1195.86243f, 693.090454f, 32.3657455f),
        0.01f);
    EXPECT_EQ(std::bit_cast<std::uint32_t>(result.X), UINT32_C(0x44c6fa0d));
    EXPECT_EQ(std::bit_cast<std::uint32_t>(result.Y), UINT32_C(0x446cf416));
    EXPECT_EQ(std::bit_cast<std::uint32_t>(result.Z), UINT32_C(0x42ccf074));
}

// --- Hermite ---

TEST(Vector3Test, HermiteAtZeroReturnsV1)
{
    Vector3 result = Vector3::Hermite(
        Vector3(0.0f, 0.0f, 0.0f), Vector3(1.0f, 0.0f, 0.0f),
        Vector3(2.0f, 0.0f, 0.0f), Vector3(1.0f, 0.0f, 0.0f), 0.0f);
    EXPECT_NEAR(result.X, 0.0f, 1e-6f);
}

TEST(Vector3Test, HermiteAtOneReturnsV2)
{
    Vector3 result = Vector3::Hermite(
        Vector3(0.0f, 0.0f, 0.0f), Vector3(1.0f, 0.0f, 0.0f),
        Vector3(2.0f, 0.0f, 0.0f), Vector3(1.0f, 0.0f, 0.0f), 1.0f);
    EXPECT_NEAR(result.X, 2.0f, 1e-5f);
}

TEST(Vector3Test, HermiteOutRef)
{
    Vector3 result;
    Vector3::Hermite(
        Vector3(0.0f, 0.0f, 0.0f), Vector3(1.0f, 0.0f, 0.0f),
        Vector3(2.0f, 0.0f, 0.0f), Vector3(1.0f, 0.0f, 0.0f), 0.0f, result);
    EXPECT_NEAR(result.X, 0.0f, 1e-6f);
}

// --- Transform by Matrix ---

TEST(Vector3Test, TransformByIdentityMatrix)
{
    Vector3 v(1.0f, 2.0f, 3.0f);
    Vector3 result = Vector3::Transform(v, Matrix::getIdentityProperty());
    EXPECT_NEAR(result.X, 1.0f, 1e-5f);
    EXPECT_NEAR(result.Y, 2.0f, 1e-5f);
    EXPECT_NEAR(result.Z, 3.0f, 1e-5f);
}

TEST(Vector3Test, TransformByIdentityMatrixOutRef)
{
    Vector3 v(1.0f, 2.0f, 3.0f);
    Vector3 result;
    Vector3::Transform(v, Matrix::getIdentityProperty(), result);
    EXPECT_NEAR(result.X, 1.0f, 1e-5f);
    EXPECT_NEAR(result.Y, 2.0f, 1e-5f);
    EXPECT_NEAR(result.Z, 3.0f, 1e-5f);
}

// --- TransformNormal by Matrix ---

TEST(Vector3Test, TransformNormalByIdentityMatrix)
{
    Vector3 n(1.0f, 0.0f, 0.0f);
    Vector3 result = Vector3::TransformNormal(n, Matrix::getIdentityProperty());
    EXPECT_NEAR(result.X, 1.0f, 1e-5f);
    EXPECT_NEAR(result.Y, 0.0f, 1e-5f);
    EXPECT_NEAR(result.Z, 0.0f, 1e-5f);
}

TEST(Vector3Test, TransformNormalByIdentityMatrixOutRef)
{
    Vector3 n(0.0f, 1.0f, 0.0f);
    Vector3 result;
    Vector3::TransformNormal(n, Matrix::getIdentityProperty(), result);
    EXPECT_NEAR(result.X, 0.0f, 1e-5f);
    EXPECT_NEAR(result.Y, 1.0f, 1e-5f);
    EXPECT_NEAR(result.Z, 0.0f, 1e-5f);
}

// --- Transform by Quaternion ---

TEST(Vector3Test, TransformByIdentityQuaternion)
{
    Vector3 v(1.0f, 2.0f, 3.0f);
    Vector3 result = Vector3::Transform(v, Quaternion::Identity);
    EXPECT_NEAR(result.X, 1.0f, 1e-5f);
    EXPECT_NEAR(result.Y, 2.0f, 1e-5f);
    EXPECT_NEAR(result.Z, 3.0f, 1e-5f);
}

TEST(Vector3Test, TransformByIdentityQuaternionOutRef)
{
    Vector3 v(1.0f, 2.0f, 3.0f);
    Vector3 result;
    Vector3::Transform(v, Quaternion::Identity, result);
    EXPECT_NEAR(result.X, 1.0f, 1e-5f);
    EXPECT_NEAR(result.Y, 2.0f, 1e-5f);
    EXPECT_NEAR(result.Z, 3.0f, 1e-5f);
}

// --- Operators ---

TEST(Vector3Test, AdditionOperator)
{
    Vector3 result = Vector3(1.0f, 2.0f, 3.0f) + Vector3(4.0f, 5.0f, 6.0f);
    EXPECT_FLOAT_EQ(result.X, 5.0f);
    EXPECT_FLOAT_EQ(result.Y, 7.0f);
    EXPECT_FLOAT_EQ(result.Z, 9.0f);
}

TEST(Vector3Test, SubtractionOperator)
{
    Vector3 result = Vector3(5.0f, 7.0f, 9.0f) - Vector3(1.0f, 2.0f, 3.0f);
    EXPECT_FLOAT_EQ(result.X, 4.0f);
    EXPECT_FLOAT_EQ(result.Y, 5.0f);
    EXPECT_FLOAT_EQ(result.Z, 6.0f);
}

TEST(Vector3Test, NegationOperator)
{
    Vector3 result = -Vector3(1.0f, -2.0f, 3.0f);
    EXPECT_FLOAT_EQ(result.X, -1.0f);
    EXPECT_FLOAT_EQ(result.Y, 2.0f);
    EXPECT_FLOAT_EQ(result.Z, -3.0f);
}

TEST(Vector3Test, ScalarMultiplicationOperator)
{
    Vector3 result = Vector3(1.0f, 2.0f, 3.0f) * 2.0f;
    EXPECT_FLOAT_EQ(result.X, 2.0f);
    EXPECT_FLOAT_EQ(result.Y, 4.0f);
    EXPECT_FLOAT_EQ(result.Z, 6.0f);
}

TEST(Vector3Test, ScalarLeftMultiplicationOperator)
{
    Vector3 result = 2.0f * Vector3(1.0f, 2.0f, 3.0f);
    EXPECT_FLOAT_EQ(result.X, 2.0f);
    EXPECT_FLOAT_EQ(result.Y, 4.0f);
    EXPECT_FLOAT_EQ(result.Z, 6.0f);
}

TEST(Vector3Test, ComponentWiseMultiplicationOperator)
{
    Vector3 result = Vector3(1.0f, 2.0f, 3.0f) * Vector3(4.0f, 5.0f, 6.0f);
    EXPECT_FLOAT_EQ(result.X, 4.0f);
    EXPECT_FLOAT_EQ(result.Y, 10.0f);
    EXPECT_FLOAT_EQ(result.Z, 18.0f);
}

TEST(Vector3Test, DivisionByScalarOperator)
{
    Vector3 result = Vector3(2.0f, 4.0f, 6.0f) / 2.0f;
    EXPECT_FLOAT_EQ(result.X, 1.0f);
    EXPECT_FLOAT_EQ(result.Y, 2.0f);
    EXPECT_FLOAT_EQ(result.Z, 3.0f);
}

TEST(Vector3Test, ComponentWiseDivisionOperator)
{
    Vector3 result = Vector3(4.0f, 6.0f, 9.0f) / Vector3(2.0f, 3.0f, 3.0f);
    EXPECT_FLOAT_EQ(result.X, 2.0f);
    EXPECT_FLOAT_EQ(result.Y, 2.0f);
    EXPECT_FLOAT_EQ(result.Z, 3.0f);
}

TEST(Vector3Test, EqualityOperator)
{
    EXPECT_TRUE(Vector3(1.0f, 2.0f, 3.0f) == Vector3(1.0f, 2.0f, 3.0f));
    EXPECT_FALSE(Vector3(1.0f, 2.0f, 3.0f) == Vector3(1.0f, 2.0f, 4.0f));
}

TEST(Vector3Test, InequalityOperator)
{
    EXPECT_FALSE(Vector3(1.0f, 2.0f, 3.0f) != Vector3(1.0f, 2.0f, 3.0f));
    EXPECT_TRUE(Vector3(1.0f, 2.0f, 3.0f) != Vector3(1.0f, 2.0f, 4.0f));
}

TEST(Vector3Test, CompoundAddAssign)
{
    Vector3 v(1.0f, 2.0f, 3.0f);
    v += Vector3(4.0f, 5.0f, 6.0f);
    EXPECT_FLOAT_EQ(v.X, 5.0f);
    EXPECT_FLOAT_EQ(v.Y, 7.0f);
    EXPECT_FLOAT_EQ(v.Z, 9.0f);
}

TEST(Vector3Test, CompoundSubtractAssign)
{
    Vector3 v(5.0f, 7.0f, 9.0f);
    v -= Vector3(1.0f, 2.0f, 3.0f);
    EXPECT_FLOAT_EQ(v.X, 4.0f);
    EXPECT_FLOAT_EQ(v.Y, 5.0f);
    EXPECT_FLOAT_EQ(v.Z, 6.0f);
}
