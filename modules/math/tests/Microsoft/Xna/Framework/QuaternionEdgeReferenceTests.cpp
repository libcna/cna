// SPDX-License-Identifier: MS-PL
#include <gtest/gtest.h>
#include <array>
#include <bit>
#include <cstdint>
#include <cmath>
#include "Microsoft/Xna/Framework/MathHelper.hpp"
#include "Microsoft/Xna/Framework/Quaternion.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"

using namespace Microsoft::Xna::Framework;

TEST(QuaternionEdgeReferenceTest, UnaryOperationsHaveExactIndependentResults)
{
    Quaternion value(1, 1, 1, 1), output;
    EXPECT_EQ(value.Length(), 2);
    EXPECT_EQ(value.LengthSquared(), 4);
    EXPECT_EQ(Quaternion::Normalize(value), Quaternion(0.5f, 0.5f, 0.5f, 0.5f));
    Quaternion::Normalize(value, output);
    EXPECT_EQ(output, Quaternion(0.5f, 0.5f, 0.5f, 0.5f));
    auto instance = value;
    instance.Normalize();
    EXPECT_EQ(instance, Quaternion(0.5f, 0.5f, 0.5f, 0.5f));
    EXPECT_EQ(Quaternion::Inverse(value), Quaternion(-0.25f, -0.25f, -0.25f, 0.25f));
    Quaternion::Inverse(value, output);
    EXPECT_EQ(output, Quaternion(-0.25f, -0.25f, -0.25f, 0.25f));
    EXPECT_EQ(Quaternion::Conjugate(value), Quaternion(-1, -1, -1, 1));
    Quaternion::Conjugate(value, output);
    EXPECT_EQ(output, Quaternion(-1, -1, -1, 1));
    instance = value;
    instance.Conjugate();
    EXPECT_EQ(instance, Quaternion(-1, -1, -1, 1));
}

TEST(QuaternionEdgeReferenceTest, ZeroAndWideAxisPreserveEstablishedIeeeAndXnaOutcomes)
{
    Quaternion zero, output;
    zero.Normalize();
    EXPECT_TRUE(std::isnan(zero.X));
    Quaternion::Normalize(Quaternion(), output);
    EXPECT_TRUE(std::isnan(output.X));
    EXPECT_TRUE(std::isnan(Quaternion::Normalize(Quaternion()).X));
    EXPECT_TRUE(std::isnan(Quaternion::Inverse(Quaternion()).W));
    Quaternion::Inverse(Quaternion(), output);
    EXPECT_TRUE(std::isnan(output.W));
    for (const auto& pair : {std::array{1e20f, -1e-20f}, std::array{1e-30f, -1e30f}})
    {
        Quaternion axis(pair[0], 0, 0, 0);
        EXPECT_EQ(axis.Length(), pair[0]);
        EXPECT_EQ(Quaternion::Normalize(axis), Quaternion(1, 0, 0, 0));
        Quaternion::Normalize(axis, output);
        EXPECT_EQ(output, Quaternion(1, 0, 0, 0));
        auto instance = axis;
        instance.Normalize();
        EXPECT_EQ(instance, Quaternion(1, 0, 0, 0));
        EXPECT_FLOAT_EQ(Quaternion::Inverse(axis).X, pair[1]);
        Quaternion::Inverse(axis, output);
        EXPECT_FLOAT_EQ(output.X, pair[1]);
        EXPECT_EQ(output.Y, 0);
        EXPECT_TRUE(std::signbit(output.Y));
        EXPECT_FALSE(std::signbit(output.W));
    }
}

TEST(QuaternionEdgeReferenceTest, IdenticalOppositeAndOrthogonalInterpolation)
{
    const Quaternion identity = Quaternion::Identity, opposite(0, 0, 0, -1), halfTurn(0, 0, 1, 0);
    Quaternion output;
    for (const Quaternion other : {identity, opposite})
    {
        for (float amount : {-1.0f, 0.0f, 0.5f, 1.0f, 2.0f})
        {
            EXPECT_EQ(Quaternion::Lerp(identity, other, amount), identity);
            Quaternion::Lerp(identity, other, amount, output);
            EXPECT_EQ(output, identity);
            EXPECT_EQ(Quaternion::Slerp(identity, other, amount), identity);
            Quaternion::Slerp(identity, other, amount, output);
            EXPECT_EQ(output, identity);
        }
    }
    Quaternion lerpOut, slerpOut;
    Quaternion::Lerp(identity, halfTurn, 0.5f, lerpOut);
    Quaternion::Slerp(identity, halfTurn, 0.5f, slerpOut);
    for (const Quaternion result : {Quaternion::Lerp(identity, halfTurn, 0.5f), lerpOut,
                                    Quaternion::Slerp(identity, halfTurn, 0.5f), slerpOut})
    {
        EXPECT_EQ(result.X, 0);
        EXPECT_EQ(result.Y, 0);
        EXPECT_NEAR(result.Z, 0.7071067811865475, 1e-7);
        EXPECT_NEAR(result.W, 0.7071067811865475, 1e-7);
    }
}

TEST(QuaternionEdgeReferenceTest, ConcatenationHasIndependentNoncommutativeOrder)
{
    const Quaternion x(1, 0, 0, 0), y(0, 1, 0, 0);
    Quaternion output;
    EXPECT_EQ(Quaternion::Concatenate(x, y), Quaternion(0, 0, -1, 0));
    Quaternion::Concatenate(x, y, output);
    EXPECT_EQ(output, Quaternion(0, 0, -1, 0));
    EXPECT_EQ(Quaternion::Concatenate(y, x), Quaternion(0, 0, 1, 0));
    Quaternion::Concatenate(y, x, output);
    EXPECT_EQ(output, Quaternion(0, 0, 1, 0));
}

TEST(QuaternionEdgeReferenceTest, AxisAngleAndYawPitchRollHaveAnalyticRotations)
{
    Quaternion output;
    EXPECT_EQ(Quaternion::CreateFromAxisAngle(Vector3(2, -1, 0.5f), 0), Quaternion::Identity);
    Quaternion::CreateFromAxisAngle(Vector3(2, -1, 0.5f), 0, output);
    EXPECT_EQ(output, Quaternion::Identity);
    EXPECT_EQ(Quaternion::CreateFromYawPitchRoll(0, 0, 0), Quaternion::Identity);
    Quaternion::CreateFromYawPitchRoll(0, 0, 0, output);
    EXPECT_EQ(output, Quaternion::Identity);
    Quaternion axisOut, eulerOut;
    Quaternion::CreateFromAxisAngle(Vector3::UnitY, MathHelper::PiOver2, axisOut);
    Quaternion::CreateFromYawPitchRoll(MathHelper::PiOver2, 0, 0, eulerOut);
    for (const auto& result : {Quaternion::CreateFromAxisAngle(Vector3::UnitY, MathHelper::PiOver2), axisOut,
                               Quaternion::CreateFromYawPitchRoll(MathHelper::PiOver2, 0, 0), eulerOut})
    {
        EXPECT_EQ(result.X, 0);
        EXPECT_EQ(result.Z, 0);
        EXPECT_NEAR(result.Y, 0.7071067811865475, 1e-7);
        EXPECT_NEAR(result.W, 0.7071067811865475, 1e-7);
    }
}

TEST(QuaternionEdgeReferenceTest, RotationMatrixBranchesHaveExactLiteralQuaternions)
{
    const Matrix identity = Matrix::getIdentityProperty();
    const Matrix x(1, 0, 0, 0, 0, -1, 0, 0, 0, 0, -1, 0, 0, 0, 0, 1);
    const Matrix y(-1, 0, 0, 0, 0, 1, 0, 0, 0, 0, -1, 0, 0, 0, 0, 1);
    const Matrix z(-1, 0, 0, 0, 0, -1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1);
    const std::array matrices{identity, x, y, z, Matrix()};
    const std::array expected{Quaternion::Identity, Quaternion(1, 0, 0, 0), Quaternion(0, 1, 0, 0),
                              Quaternion(0, 0, 1, 0), Quaternion(0.5f, 0, 0, 0)};
    for (std::size_t i = 0; i < matrices.size(); ++i)
    {
        EXPECT_EQ(Quaternion::CreateFromRotationMatrix(matrices[i]), expected[i]);
        Quaternion output;
        Quaternion::CreateFromRotationMatrix(matrices[i], output);
        EXPECT_EQ(output, expected[i]);
    }
}

TEST(QuaternionEdgeReferenceTest, FactoryRoundingMatchesPinnedReferenceBits)
{
    // Observed FNA 00498b0b8/net8 outputs for exactly representable angle inputs.
    Quaternion axisOut;
    Quaternion::CreateFromAxisAngle(Vector3::UnitX, 0.390625f, axisOut);
    for (const auto& result : {Quaternion::CreateFromAxisAngle(Vector3::UnitX, 0.390625f), axisOut})
    {
        EXPECT_EQ(std::bit_cast<std::uint32_t>(result.X), UINT32_C(0x3e46bb19));
        EXPECT_EQ(std::bit_cast<std::uint32_t>(result.W), UINT32_C(0x3f7b21f8));
    }
    struct Witness { float yaw, pitch, roll; std::array<std::uint32_t, 4> bits; };
    for (const auto& witness : {
        Witness{0.390625f, -1.359375f, 1.65625f, {0xbe9c8fe3, 0x3f0e69f4, 0x3f250189, 0x3eda2b2f}},
        Witness{-0.015625f, 1.265625f, 0.390625f, {0x3f1434c4, 0xbdf7b6ce, 0x3e24e2f1, 0x3f4a44fc}}})
    {
        Quaternion output;
        Quaternion::CreateFromYawPitchRoll(witness.yaw, witness.pitch, witness.roll, output);
        for (const auto& result : {Quaternion::CreateFromYawPitchRoll(witness.yaw, witness.pitch, witness.roll), output})
        {
            EXPECT_EQ(std::bit_cast<std::uint32_t>(result.X), witness.bits[0]);
            EXPECT_EQ(std::bit_cast<std::uint32_t>(result.Y), witness.bits[1]);
            EXPECT_EQ(std::bit_cast<std::uint32_t>(result.Z), witness.bits[2]);
            EXPECT_EQ(std::bit_cast<std::uint32_t>(result.W), witness.bits[3]);
        }
    }
}
