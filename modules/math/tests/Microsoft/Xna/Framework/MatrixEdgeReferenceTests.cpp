// SPDX-License-Identifier: MS-PL
#include <gtest/gtest.h>
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Plane.hpp"
#include "Microsoft/Xna/Framework/Quaternion.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"

using namespace Microsoft::Xna::Framework;

namespace
{
    constexpr std::array<float Matrix::*, 16> fields = {
        &Matrix::M11, &Matrix::M12, &Matrix::M13, &Matrix::M14,
        &Matrix::M21, &Matrix::M22, &Matrix::M23, &Matrix::M24,
        &Matrix::M31, &Matrix::M32, &Matrix::M33, &Matrix::M34,
        &Matrix::M41, &Matrix::M42, &Matrix::M43, &Matrix::M44
    };

    void ExpectNumbers(const Matrix& actual, const Matrix& expected)
    {
        for (const auto field : fields) EXPECT_EQ(actual.*field, expected.*field);
    }

    void ExpectAllNaN(const Matrix& actual)
    {
        for (const auto field : fields) EXPECT_TRUE(std::isnan(actual.*field));
    }
}

TEST(MatrixEdgeReferenceTest, AxisAngleRoundsDoubleTrigBeforeMatrixArithmetic)
{
    // Literal bits distinguish cos(double(Single)) rounded to Single from cosf.
    Matrix output;
    Matrix::CreateFromAxisAngle(Vector3::UnitX, -0.84375f, output);
    for (const auto& actual : {Matrix::CreateFromAxisAngle(Vector3::UnitX, -0.84375f), output})
    {
        EXPECT_EQ(std::bit_cast<std::uint32_t>(actual.M22), 0x3f2a2788u);
        EXPECT_EQ(std::bit_cast<std::uint32_t>(actual.M33), 0x3f2a2788u);
        EXPECT_EQ(std::bit_cast<std::uint32_t>(actual.M23), 0xbf3f44a0u);
        EXPECT_EQ(std::bit_cast<std::uint32_t>(actual.M32), 0x3f3f44a0u);
        EXPECT_EQ(actual.M11, 1);
        EXPECT_EQ(actual.M44, 1);
    }
}

TEST(MatrixEdgeReferenceTest, CameraFactoriesHaveIndependentAxisAlignedCoefficients)
{
    const Vector3 position(0, 0, 5), target(0, 0, -1), up(0, 1, 0);
    const Matrix expectedView(1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, -5, 1);
    const Matrix expectedWorld(1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 5, 1);
    ExpectNumbers(Matrix::CreateLookAt(position, target, up), expectedView);
    Matrix output;
    Matrix::CreateLookAt(position, target, up, output);
    ExpectNumbers(output, expectedView);
    EXPECT_TRUE(std::signbit(output.M41));
    EXPECT_TRUE(std::signbit(output.M42));
    ExpectNumbers(Matrix::CreateWorld(position, target, up), expectedWorld);
    Matrix::CreateWorld(position, target, up, output);
    ExpectNumbers(output, expectedWorld);
    EXPECT_TRUE(std::signbit(output.M31));
    EXPECT_TRUE(std::signbit(output.M32));
}

TEST(MatrixEdgeReferenceTest, CoincidentAndParallelCamerasPropagateUndefinedBasis)
{
    Matrix output;
    Matrix::CreateLookAt(Vector3::Zero, Vector3::Zero, Vector3::Up, output);
    for (const auto& value : {Matrix::CreateLookAt(Vector3::Zero, Vector3::Zero, Vector3::Up), output})
    {
        EXPECT_TRUE(std::isnan(value.M11));
        EXPECT_TRUE(std::isnan(value.M33));
        EXPECT_TRUE(std::isnan(value.M41));
        EXPECT_EQ(value.M14, 0);
        EXPECT_EQ(value.M44, 1);
    }
    Matrix::CreateWorld(Vector3::One, Vector3::Up, Vector3::Up, output);
    for (const auto& value : {Matrix::CreateWorld(Vector3::One, Vector3::Up, Vector3::Up), output})
    {
        EXPECT_TRUE(std::isnan(value.M11));
        EXPECT_TRUE(std::isnan(value.M22));
        EXPECT_EQ(value.M32, -1);
        EXPECT_EQ(value.M41, 1);
    }
}

TEST(MatrixEdgeReferenceTest, BillboardCoincidenceUsesOptionalForwardWithoutNormalizingIt)
{
    const Matrix expected(-1, 0, 0, 0, 0, 1, 0, 0, 0, 0, -1, 0, 0, 0, 0, 1);
    const Matrix explicitExpected(1, 0, 0, 0, 0, 2, 0, 0, 0, 0, 2, 0, 0, 0, 0, 1);
    Matrix output;
    Matrix::CreateBillboard(Vector3::Zero, Vector3::Zero, Vector3::Up, std::nullopt, output);
    ExpectNumbers(output, expected);
    ExpectNumbers(Matrix::CreateBillboard(Vector3::Zero, Vector3::Zero, Vector3::Up, std::nullopt), expected);
    const Vector3 forward(0, 0, -2);
    Matrix::CreateBillboard(Vector3::Zero, Vector3::Zero, Vector3::Up, forward, output);
    ExpectNumbers(output, explicitExpected);
    ExpectNumbers(Matrix::CreateBillboard(Vector3::Zero, Vector3::Zero, Vector3::Up, forward), explicitExpected);
}

TEST(MatrixEdgeReferenceTest, ConstrainedBillboardParallelFallbacksHaveLiteralAxes)
{
    const Matrix expected(-1, 0, 0, 0, 0, 1, 0, 0, 0, 0, -1, 0, 0, 1, 0, 1);
    for (const auto forward : {std::optional<Vector3>{}, std::optional<Vector3>{Vector3::Up}})
    {
        Matrix output;
        Matrix::CreateConstrainedBillboard(Vector3::Up, Vector3::Zero, Vector3::Up,
            std::nullopt, forward, output);
        ExpectNumbers(output, expected);
        ExpectNumbers(Matrix::CreateConstrainedBillboard(Vector3::Up, Vector3::Zero,
            Vector3::Up, std::nullopt, forward), expected);
    }
    const Matrix zAxis(0, -1, 0, 0, 0, 0, -1, 0, 1, 0, 0, 0, 0, 0, -1, 1);
    Matrix output;
    Matrix::CreateConstrainedBillboard(Vector3::Forward, Vector3::Zero, Vector3::Forward,
        std::nullopt, std::nullopt, output);
    ExpectNumbers(output, zAxis);
}

TEST(MatrixEdgeReferenceTest, QuaternionAndYawPitchRollFactoriesHaveLiteralMatrices)
{
    const Quaternion q(0.5f, -0.5f, 0.5f, -0.5f);
    const Matrix expected(0, -1, 0, 0, 0, 0, -1, 0, 1, 0, 0, 0, 0, 0, 0, 1);
    Matrix output;
    Matrix::CreateFromQuaternion(q, output);
    ExpectNumbers(output, expected);
    ExpectNumbers(Matrix::CreateFromQuaternion(q), expected);
    Matrix::CreateFromYawPitchRoll(0, 0, 0, output);
    const Matrix identity(1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1);
    ExpectNumbers(output, identity);
    ExpectNumbers(Matrix::CreateFromYawPitchRoll(0, 0, 0), identity);
    const Quaternion nonunit(1, 2, 3, 4);
    const Matrix nonunitExpected(-25, 28, -10, 0, -20, -19, 20, 0, 22, 4, -9, 0, 0, 0, 0, 1);
    Matrix::CreateFromQuaternion(nonunit, output);
    ExpectNumbers(output, nonunitExpected);
    ExpectNumbers(Matrix::CreateFromQuaternion(nonunit), nonunitExpected);
}

TEST(MatrixEdgeReferenceTest, ReflectionAndShadowNormalizeScaledPlanesWithLiteralCoefficients)
{
    const Plane plane(Vector3(0, 2, 0), -4);
    const Vector3 light(1, -2, 3);
    const Matrix reflection(1, 0, 0, 0, 0, -1, 0, 0, 0, 0, 1, 0, 0, 4, 0, 1);
    const Matrix shadow(-2, 0, 0, 0, -1, 0, -3, 0, 0, 0, -2, 0, 2, -4, 6, -2);
    Matrix output;
    Matrix::CreateReflection(plane, output);
    ExpectNumbers(output, reflection);
    ExpectNumbers(Matrix::CreateReflection(plane), reflection);
    Matrix::CreateShadow(light, plane, output);
    ExpectNumbers(output, shadow);
    ExpectNumbers(Matrix::CreateShadow(light, plane), shadow);
    EXPECT_FALSE(std::signbit(output.M12));
    EXPECT_FALSE(std::signbit(output.M32));
}

TEST(MatrixEdgeReferenceTest, YawPitchRollRetainsNonzeroReferenceBits)
{
    const std::array<std::uint32_t, 9> expected = {0x3f1aa674u, 0xbd592700u, 0x3f4b8e61u,
        0x3f2ce01au, 0x3f105808u, 0xbef36eedu, 0xbed8a44au, 0x3f52fd5cu, 0x3ec0ba28u};
    Matrix output;
    Matrix::CreateFromYawPitchRoll(-0.84375f, -0.96875f, -0.09375f, output);
    for (const auto& actual : {Matrix::CreateFromYawPitchRoll(-0.84375f, -0.96875f, -0.09375f), output})
        for (std::size_t row = 0; row < 3; ++row)
            for (std::size_t column = 0; column < 3; ++column)
                EXPECT_EQ(std::bit_cast<std::uint32_t>(actual.*fields[row * 4 + column]), expected[row * 3 + column]);
}

TEST(MatrixEdgeReferenceTest, InversionRetainsTheEstablishedWideRoundingDecision)
{
    // XNASWEEP-172's established storage model; keep this observed witness separate
    // from FNA's float reciprocal, whose result here is bd9dd284.
    const Matrix input(-6.25f, -1.125f, 11, 3.125f, -10.875f, 13.25f, -0.125f, 10.125f,
        3.125f, -0.125f, 0.75f, 10.625f, -14.125f, -1, 4.5f, -6.75f);
    Matrix output;
    Matrix::Invert(input, output);
    auto alias = input;
    Matrix::Invert(alias, alias);
    for (const auto& actual : {Matrix::Invert(input), output, alias})
        EXPECT_EQ(std::bit_cast<std::uint32_t>(actual.M13), 0xbd9dd283u);
}

TEST(MatrixEdgeReferenceTest, SingularAndNearlySingularInversionHasLiteralOutcomes)
{
    Matrix output;
    const Matrix zero;
    Matrix::Invert(zero, output);
    ExpectAllNaN(output);
    ExpectAllNaN(Matrix::Invert(zero));
    auto alias = zero;
    Matrix::Invert(alias, alias);
    ExpectAllNaN(alias);
    const Matrix near(1, 1, 0, 0, 1, 1.00000011920928955078125f, 0, 0,
        0, 0, 1, 0, 0, 0, 0, 1);
    const Matrix inverse(8388609, -8388608, 0, 0, -8388608, 8388608, 0, 0,
        0, 0, 1, 0, 0, 0, 0, 1);
    Matrix::Invert(near, output);
    ExpectNumbers(output, inverse);
    ExpectNumbers(Matrix::Invert(near), inverse);
    alias = near;
    Matrix::Invert(alias, alias);
    ExpectNumbers(alias, inverse);
    EXPECT_EQ(near.Determinant(), 0.00000011920928955078125f);
    EXPECT_EQ(zero.Determinant(), 0);
}

TEST(MatrixEdgeReferenceTest, DecomposeKeepsMeasuredReflectionAndZeroAxisContracts)
{
    const Matrix reflection(-2, 0, 0, 0, 0, 3, 0, 0, 0, 0, 4, 0, 7, 8, 9, 1);
    Vector3 scale, translation;
    Quaternion rotation;
    EXPECT_TRUE(reflection.Decompose(scale, rotation, translation));
    EXPECT_EQ(scale, Vector3(2, 3, -4));
    EXPECT_EQ(translation, Vector3(7, 8, 9));
    EXPECT_EQ(rotation.Y, 1);
    EXPECT_EQ(rotation.X, 0);
    EXPECT_EQ(rotation.Z, 0);
    EXPECT_EQ(rotation.W, 0);
    const Matrix zeroAxis(1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 7, 8, 9, 1);
    EXPECT_TRUE(zeroAxis.Decompose(scale, rotation, translation));
    EXPECT_EQ(scale, Vector3(1, 0, 1));
    EXPECT_EQ(translation, Vector3(7, 8, 9));
    EXPECT_TRUE(std::isnan(rotation.X));
    EXPECT_TRUE(std::isnan(rotation.Y));
    EXPECT_TRUE(std::isnan(rotation.Z));
    EXPECT_TRUE(std::isnan(rotation.W));
}

TEST(MatrixEdgeReferenceTest, TransposeAndLerpUseIndependentLiteralResultsInAliasedForms)
{
    const Matrix source(1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16);
    const Matrix transpose(1, 5, 9, 13, 2, 6, 10, 14, 3, 7, 11, 15, 4, 8, 12, 16);
    const Matrix identity(1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1);
    const Matrix midpoint(1, 1, 1.5f, 2, 2.5f, 3.5f, 3.5f, 4, 4.5f, 5, 6, 6, 6.5f, 7, 7.5f, 8.5f);
    Matrix output;
    Matrix::Transpose(source, output);
    ExpectNumbers(output, transpose);
    ExpectNumbers(Matrix::Transpose(source), transpose);
    auto alias = source;
    Matrix::Transpose(alias, alias);
    ExpectNumbers(alias, transpose);
    Matrix::Lerp(source, identity, 0.5f, output);
    ExpectNumbers(output, midpoint);
    ExpectNumbers(Matrix::Lerp(source, identity, 0.5f), midpoint);
    alias = source;
    Matrix::Lerp(alias, identity, 0.5f, alias);
    ExpectNumbers(alias, midpoint);
    alias = identity;
    Matrix::Lerp(source, alias, 0.5f, alias);
    ExpectNumbers(alias, midpoint);
    EXPECT_EQ(source.Determinant(), 0);
    EXPECT_EQ(identity.Determinant(), 1);
}

TEST(MatrixEdgeReferenceTest, RankThreeSingularInverseRetainsItsOneInfiniteCofactor)
{
    // FNA Matrix.Invert: det=0, inverseDet=+Inf. Only cofactor (4,4) is 1;
    // the other fifteen zero cofactors multiply Inf and produce NaN.
    const Matrix source(1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,0);
    Matrix output;
    Matrix::Invert(source,output);
    auto alias=source;
    Matrix::Invert(alias,alias);
    for (const auto& inverse : {Matrix::Invert(source),output,alias})
        for (const auto field : fields)
            if (field==&Matrix::M44)
                EXPECT_EQ(inverse.*field,std::numeric_limits<float>::infinity());
            else
                EXPECT_TRUE(std::isnan(inverse.*field));
    EXPECT_EQ(source.Determinant(),0);
}

TEST(MatrixEdgeReferenceTest, OrthographicFactoriesHaveFullAsymmetricLiteralCoefficients)
{
    // FNA Matrix.CreateOrthographic: diag=(2/w,2/h,1/(n-f),1), tz=n/(n-f).
    const Matrix centered(0.25f,0,0,0, 0,0.5f,0,0, 0,0,-0.25f,0, 0,0,-0.5f,1);
    Matrix output;
    EXPECT_EQ(Matrix::CreateOrthographic(8,4,2,6),centered);
    Matrix::CreateOrthographic(8,4,2,6,output);
    EXPECT_EQ(output,centered);
    // Off-center translation = (-(r+l)/(r-l),-(t+b)/(t-b),n/(n-f)).
    const Matrix offset(0.25f,0,0,0, 0,0.5f,0,0, 0,0,-0.25f,0, -0.5f,0.5f,-0.5f,1);
    EXPECT_EQ(Matrix::CreateOrthographicOffCenter(-2,6,-3,1,2,6),offset);
    Matrix::CreateOrthographicOffCenter(-2,6,-3,1,2,6,output);
    EXPECT_EQ(output,offset);
}

TEST(MatrixEdgeReferenceTest, ElementwiseDivisionAndScalarDivisionWriteAllSixteenFields)
{
    // FNA Matrix.Divide is component division, not multiplication by an inverse.
    const Matrix source(2,-4,6,-8, 10,-12,14,-16, 18,-20,22,-24, 26,-28,30,-32);
    const Matrix denominator(1,2,3,4, 5,6,7,8, 9,10,11,12, 13,14,15,16);
    const Matrix quotient(2,-2,2,-2, 2,-2,2,-2, 2,-2,2,-2, 2,-2,2,-2);
    const Matrix half(1,-2,3,-4, 5,-6,7,-8, 9,-10,11,-12, 13,-14,15,-16);
    Matrix output;
    EXPECT_EQ(Matrix::Divide(source,denominator),quotient);
    EXPECT_EQ(source/denominator,quotient);
    Matrix::Divide(source,denominator,output);
    EXPECT_EQ(output,quotient);
    auto alias=source;
    Matrix::Divide(alias,denominator,alias);
    EXPECT_EQ(alias,quotient);
    alias=denominator;
    Matrix::Divide(source,alias,alias);
    EXPECT_EQ(alias,quotient);
    EXPECT_EQ(Matrix::Divide(source,2),half);
    EXPECT_EQ(source/2,half);
    Matrix::Divide(source,2,output);
    EXPECT_EQ(output,half);
    alias=source;
    Matrix::Divide(alias,2,alias);
    EXPECT_EQ(alias,half);
}

TEST(MatrixEdgeReferenceTest, DecomposeFailureStillWritesScaleAndTranslationAndClearsRotation)
{
    // XNA Matrix.Decompose determinant-refusal path; shear rows (1,1,0),(0,1,0),(0,0,1).
    // Normalized determinant=1/sqrt(2); (det-1)^2 > 0.0001, so rotation=Identity.
    const Matrix shear(1,1,0,0, 0,1,0,0, 0,0,1,0, 7,8,9,1);
    Vector3 scale(99),translation(99);
    Quaternion rotation(9,8,7,6);
    EXPECT_FALSE(shear.Decompose(scale,rotation,translation));
    EXPECT_FLOAT_EQ(scale.X,1.4142135623730951f);
    EXPECT_EQ(scale.Y,1);
    EXPECT_EQ(scale.Z,1);
    EXPECT_EQ(translation,Vector3(7,8,9));
    EXPECT_EQ(rotation,Quaternion(0,0,0,1));
}
