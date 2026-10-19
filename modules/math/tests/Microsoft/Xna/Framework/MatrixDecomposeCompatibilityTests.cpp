// SPDX-License-Identifier: MS-PL

#include <gtest/gtest.h>
#include <cmath>
#include <limits>
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Quaternion.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"

using Microsoft::Xna::Framework::Matrix;
using Microsoft::Xna::Framework::Quaternion;
using Microsoft::Xna::Framework::Vector3;

namespace
{
    void ExpectNaN(const Quaternion& q)
    {
        EXPECT_TRUE(std::isnan(q.X));
        EXPECT_TRUE(std::isnan(q.Y));
        EXPECT_TRUE(std::isnan(q.Z));
        EXPECT_TRUE(std::isnan(q.W));
    }

    void ExpectMatrixNear(const Matrix& actual, const Matrix& expected)
    {
        const float a[] = {actual.M11, actual.M12, actual.M13, actual.M14,
            actual.M21, actual.M22, actual.M23, actual.M24,
            actual.M31, actual.M32, actual.M33, actual.M34,
            actual.M41, actual.M42, actual.M43, actual.M44};
        const float b[] = {expected.M11, expected.M12, expected.M13, expected.M14,
            expected.M21, expected.M22, expected.M23, expected.M24,
            expected.M31, expected.M32, expected.M33, expected.M34,
            expected.M41, expected.M42, expected.M43, expected.M44};
        for (int i = 0; i < 16; ++i) EXPECT_NEAR(a[i], b[i], 1e-5f) << i;
    }
}

TEST(MatrixDecomposeCompatibilityTest, ReflectionSignUsesLargestAxisWithStableTies)
{
    // Measured with genuine XNA 4.0, including odd/even sign parity and X/Y/Z ties.
    struct Sample { Vector3 input; Vector3 expected; };
    const Sample samples[] = {
        {{-2, 3, 4}, {2, 3, -4}}, {{-5, 3, 4}, {-5, 3, 4}},
        {{2, -5, 4}, {2, -5, 4}}, {{2, 3, -5}, {2, 3, -5}},
        {{-2, -3, 4}, {2, 3, 4}}, {{-2, -3, -4}, {2, 3, -4}},
        {{-1, 1, 1}, {-1, 1, 1}}, {{-2, 2, 1}, {-2, 2, 1}},
        {{-1, 2, 2}, {1, -2, 2}}, {{-2, 1, 2}, {-2, 1, 2}}
    };
    for (const auto& sample : samples)
    {
        SCOPED_TRACE(sample.input.ToString());
        const Matrix m = Matrix::CreateScale(sample.input) * Matrix::CreateRotationY(0.7f) *
            Matrix::CreateTranslation(7, 8, 9);
        Vector3 scale, translation;
        Quaternion rotation;
        EXPECT_TRUE(m.Decompose(scale, rotation, translation));
        EXPECT_NEAR(scale.X, sample.expected.X, 1e-5f);
        EXPECT_NEAR(scale.Y, sample.expected.Y, 1e-5f);
        EXPECT_NEAR(scale.Z, sample.expected.Z, 1e-5f);
        EXPECT_EQ(translation, Vector3(7, 8, 9));
        ExpectMatrixNear(Matrix::CreateScale(scale) * Matrix::CreateFromQuaternion(rotation) *
            Matrix::CreateTranslation(translation), m);
    }
}

TEST(MatrixDecomposeCompatibilityTest, ZeroAxesSucceedWithUndefinedRotation)
{
    for (const Vector3 input : {Vector3(1, 0, 1), Vector3(0, 1, 1), Vector3(1, 1, 0),
         Vector3(1, 0, 0), Vector3(0, 1, 0), Vector3(0, 0, 1), Vector3::Zero})
    {
        SCOPED_TRACE(input.ToString());
        Vector3 scale, translation;
        Quaternion rotation;
        const Matrix m = Matrix::CreateScale(input) * Matrix::CreateTranslation(7, 8, 9);
        EXPECT_TRUE(m.Decompose(scale, rotation, translation));
        EXPECT_EQ(scale, input);
        EXPECT_EQ(translation, Vector3(7, 8, 9));
        ExpectNaN(rotation);
    }
}

TEST(MatrixDecomposeCompatibilityTest, TinyAxesAndShearUseXnaDeterminantRefusal)
{
    for (const float x : {1e-5f, 1e-4f, 0.00010001f})
    {
        Vector3 scale, translation;
        Quaternion rotation;
        EXPECT_EQ(Matrix::CreateScale(x, 1, 1).Decompose(scale, rotation, translation), x >= 1e-4f);
        EXPECT_EQ(scale, Vector3(x, 1, 1));
        EXPECT_EQ(rotation, Quaternion::Identity);
    }
    for (const float shear : {0.05f, 0.1f, 0.2f, 0.5f, 1.0f})
    {
        Matrix m = Matrix::getIdentityProperty();
        m.M12 = shear;
        Vector3 scale, translation;
        Quaternion rotation;
        const bool accepted = m.Decompose(scale, rotation, translation);
        EXPECT_EQ(accepted, shear <= 0.1f);
        EXPECT_NEAR(scale.X, std::sqrt(1 + shear * shear), 1e-6f);
        EXPECT_EQ(scale.Y, 1);
        EXPECT_EQ(scale.Z, 1);
        if (!accepted) EXPECT_EQ(rotation, Quaternion::Identity);
    }
}

TEST(MatrixDecomposeCompatibilityTest, FourthColumnAndNonfiniteInputsMatchXna)
{
    Matrix m = Matrix::getIdentityProperty();
    m.M14 = 3; m.M24 = -4; m.M34 = 5; m.M44 = 7;
    Vector3 scale, translation;
    Quaternion rotation;
    EXPECT_TRUE(m.Decompose(scale, rotation, translation));
    EXPECT_EQ(scale, Vector3::One);
    EXPECT_EQ(rotation, Quaternion::Identity);
    for (const float x : {std::numeric_limits<float>::quiet_NaN(),
                          std::numeric_limits<float>::infinity()})
    {
        m = Matrix::getIdentityProperty(); m.M11 = x;
        EXPECT_TRUE(m.Decompose(scale, rotation, translation));
        ExpectNaN(rotation);
    }
}

TEST(MatrixDecomposeCompatibilityTest, ScaleOutputWinsWhenAliasedWithTranslation)
{
    const Matrix m = Matrix::CreateScale(-2, 3, 4) * Matrix::CreateTranslation(7, 8, 9);
    Vector3 shared;
    Quaternion rotation;
    EXPECT_TRUE(m.Decompose(shared, rotation, shared));
    EXPECT_EQ(shared, Vector3(2, 3, -4));
}
