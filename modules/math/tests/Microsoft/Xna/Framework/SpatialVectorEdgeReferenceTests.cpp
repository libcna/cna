// SPDX-License-Identifier: MS-PL
#include <gtest/gtest.h>
#include <cmath>
#include <limits>
#include <type_traits>
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Vector4.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Quaternion.hpp"

using namespace Microsoft::Xna::Framework;

namespace
{
    template<class V> class SpatialVectorEdgeReferenceTest : public ::testing::Test {};
    using SpatialVectors = ::testing::Types<Vector3, Vector4>;
    TYPED_TEST_SUITE(SpatialVectorEdgeReferenceTest, SpatialVectors);

    TYPED_TEST(SpatialVectorEdgeReferenceTest, DivideClampAndIeeeClassifications)
    {
        using V = TypeParam;
        const V a(-8), b(2), low(-2), high(2);
        V output;
        EXPECT_EQ(V::Divide(a, b), V(-4));
        V::Divide(a, b, output);
        EXPECT_EQ(output, V(-4));
        EXPECT_EQ(a / b, V(-4));
        EXPECT_EQ(V::Divide(a, 2), V(-4));
        V::Divide(a, 2, output);
        EXPECT_EQ(output, V(-4));
        EXPECT_EQ(a / 2, V(-4));
        EXPECT_EQ(V::Clamp(a, low, high), low);
        V::Clamp(a, low, high, output);
        EXPECT_EQ(output, low);
        EXPECT_EQ(V::Clamp(a, high, low), high);
        V::Clamp(a, high, low, output);
        EXPECT_EQ(output, high);
        V::Divide(V(-0.0f), 2, output);
        EXPECT_EQ(output.X, 0);
        EXPECT_TRUE(std::signbit(output.X));
        V::Divide(V(0), V(0), output);
        EXPECT_TRUE(std::isnan(output.X));
        V::Divide(V(1), -0.0f, output);
        EXPECT_EQ(output.X, -std::numeric_limits<float>::infinity());
    }

    TYPED_TEST(SpatialVectorEdgeReferenceTest, LargeTinyAxesAndZeroNormalization)
    {
        using V = TypeParam;
        for (float scale : {1e20f, 1e-30f})
        {
            V axis = V::UnitX * scale, output;
            EXPECT_EQ(axis.Length(), scale);
            EXPECT_EQ(V::Normalize(axis), V::UnitX);
            V::Normalize(axis, output);
            EXPECT_EQ(output, V::UnitX);
            axis.Normalize();
            EXPECT_EQ(axis, V::UnitX);
        }
        V zero = V::Zero, output;
        zero.Normalize();
        V::Normalize(V::Zero, output);
        EXPECT_TRUE(std::isnan(zero.X));
        EXPECT_TRUE(std::isnan(output.X));
        EXPECT_TRUE(std::isnan(V::Normalize(V::Zero).X));
    }

    TYPED_TEST(SpatialVectorEdgeReferenceTest, InterpolationHasLiteralDyadicResults)
    {
        using V = TypeParam;
        const V a(1), b(4), c(-2), d(2);
        V output;
        EXPECT_EQ(V::Lerp(a, b, 0.5f), V(2.5f));
        V::Lerp(a, b, 0.5f, output);
        EXPECT_EQ(output, V(2.5f));
        EXPECT_EQ(V::SmoothStep(a, b, 0.5f), V(2.5f));
        V::SmoothStep(a, b, 0.5f, output);
        EXPECT_EQ(output, V(2.5f));
        EXPECT_EQ(V::Hermite(a, b, c, d, 0.5f), V(-0.25f));
        V::Hermite(a, b, c, d, 0.5f, output);
        EXPECT_EQ(output, V(-0.25f));
        EXPECT_EQ(V::CatmullRom(a, b, c, d, 0.5f), V(0.9375f));
        V::CatmullRom(a, b, c, d, 0.5f, output);
        EXPECT_EQ(output, V(0.9375f));
        EXPECT_EQ(V::Barycentric(a, b, c, 0.5f, 0.25f), V(1.75f));
        V::Barycentric(a, b, c, 0.5f, 0.25f, output);
        EXPECT_EQ(output, V(1.75f));
    }

    TYPED_TEST(SpatialVectorEdgeReferenceTest, InterpolationExtrapolatesAndSmoothStepClamps)
    {
        using V = TypeParam;
        const V a(1), b(4), c(-2), d(2);
        struct Expected { float t, lerp, smooth, hermite, catmull, barycentric; };
        for (const auto& expected : {Expected{-1, -2, 1, -34, -18, -2.75f},
                                    Expected{2, 7, 4, 29, 21, 6.25f}})
        {
            V output;
            EXPECT_EQ(V::Lerp(a, b, expected.t), V(expected.lerp));
            V::Lerp(a, b, expected.t, output);
            EXPECT_EQ(output, V(expected.lerp));
            EXPECT_EQ(V::SmoothStep(a, b, expected.t), V(expected.smooth));
            V::SmoothStep(a, b, expected.t, output);
            EXPECT_EQ(output, V(expected.smooth));
            EXPECT_EQ(V::Hermite(a, b, c, d, expected.t), V(expected.hermite));
            V::Hermite(a, b, c, d, expected.t, output);
            EXPECT_EQ(output, V(expected.hermite));
            EXPECT_EQ(V::CatmullRom(a, b, c, d, expected.t), V(expected.catmull));
            V::CatmullRom(a, b, c, d, expected.t, output);
            EXPECT_EQ(output, V(expected.catmull));
            EXPECT_EQ(V::Barycentric(a, b, c, expected.t, 0.25f), V(expected.barycentric));
            V::Barycentric(a, b, c, expected.t, 0.25f, output);
            EXPECT_EQ(output, V(expected.barycentric));
        }
    }

    TYPED_TEST(SpatialVectorEdgeReferenceTest, EqualityHashAndShortDecimalFormat)
    {
        using V = TypeParam;
        const V a(1.5f), same(1.5f), different(-1.5f);
        EXPECT_TRUE(a.Equals(same));
        EXPECT_TRUE(a == same);
        EXPECT_FALSE(a != same);
        EXPECT_FALSE(a.Equals(different));
        EXPECT_FALSE(a == different);
        EXPECT_TRUE(a != different);
        EXPECT_EQ(a.GetHashCode(), same.GetHashCode());
        EXPECT_EQ(V(-0.0f).GetHashCode(), V::Zero.GetHashCode());
        if constexpr (std::is_same_v<V, Vector3>)
            EXPECT_EQ(a.ToString(), "{X:1.5 Y:1.5 Z:1.5}");
        else
            EXPECT_EQ(a.ToString(), "{X:1.5 Y:1.5 Z:1.5 W:1.5}");
    }
}

TEST(SpatialVectorTransformEdgeTest, Vector3PositionNormalQuaternionCrossAndReflect)
{
    const Matrix matrix(2, 0, 1, 0.5f, 0, -3, 0, 0, 1, 0, 4, 0, 7, 8, 9, 1);
    const Quaternion rotation(0.25f, -0.5f, 0.75f, 1);
    const Vector3 a(1, 2, 3), b(4, 3, 2);
    Vector3 output;
    EXPECT_EQ(Vector3::Transform(a, matrix), Vector3(12, 2, 22));
    Vector3::Transform(a, matrix, output);
    EXPECT_EQ(output, Vector3(12, 2, 22));
    EXPECT_EQ(Vector3::TransformNormal(a, matrix), Vector3(5, -6, 13));
    Vector3::TransformNormal(a, matrix, output);
    EXPECT_EQ(output, Vector3(5, -6, 13));
    EXPECT_EQ(Vector3::Transform(a, rotation), Vector3(-6, -3, 2));
    Vector3::Transform(a, rotation, output);
    EXPECT_EQ(output, Vector3(-6, -3, 2));
    EXPECT_EQ(Vector3::Cross(a, b), Vector3(-5, 10, -5));
    Vector3::Cross(a, b, output);
    EXPECT_EQ(output, Vector3(-5, 10, -5));
    EXPECT_EQ(Vector3::Reflect(a, b), Vector3(-127, -94, -61));
    Vector3::Reflect(a, b, output);
    EXPECT_EQ(output, Vector3(-127, -94, -61));
}

TEST(SpatialVectorTransformEdgeTest, Vector4TransformAllSourceDimensions)
{
    const Matrix matrix(2, 0, 1, 0.5f, 0, -3, 0, 0, 1, 0, 4, 0, 7, 8, 9, 1);
    const Quaternion rotation(0.25f, -0.5f, 0.75f, 1);
    const Vector2 a2(1, 2);
    const Vector3 a3(1, 2, 3);
    const Vector4 a4(1, 2, 3, 4);
    Vector4 output;
    EXPECT_EQ(Vector4::Transform(a2, matrix), Vector4(9, 2, 10, 1.5f));
    Vector4::Transform(a2, matrix, output);
    EXPECT_EQ(output, Vector4(9, 2, 10, 1.5f));
    EXPECT_EQ(Vector4::Transform(a3, matrix), Vector4(12, 2, 22, 1.5f));
    Vector4::Transform(a3, matrix, output);
    EXPECT_EQ(output, Vector4(12, 2, 22, 1.5f));
    EXPECT_EQ(Vector4::Transform(a4, matrix), Vector4(33, 26, 49, 4.5f));
    Vector4::Transform(a4, matrix, output);
    EXPECT_EQ(output, Vector4(33, 26, 49, 4.5f));
    EXPECT_EQ(Vector4::Transform(a2, rotation), Vector4(-4.125f, 0.75f, 0.875f, 1));
    Vector4::Transform(a2, rotation, output);
    EXPECT_EQ(output, Vector4(-4.125f, 0.75f, 0.875f, 1));
    EXPECT_EQ(Vector4::Transform(a3, rotation), Vector4(-6, -3, 2, 1));
    Vector4::Transform(a3, rotation, output);
    EXPECT_EQ(output, Vector4(-6, -3, 2, 1));
    EXPECT_EQ(Vector4::Transform(a4, rotation), Vector4(-6, -3, 2, 4));
    Vector4::Transform(a4, rotation, output);
    EXPECT_EQ(output, Vector4(-6, -3, 2, 4));
}

TEST(SpatialVectorTransformEdgeTest, CrossCancelsWideProductsAndKeepsXnaZeroSign)
{
    // Existing measured-XNA decision 0e7be08e0; finite parallel vectors have zero cross product.
    for (const Vector3 input : {Vector3::Zero, Vector3(1e20f, 1e20f, 0)})
    {
        Vector3 output;
        Vector3::Cross(input, input, output);
        for (const Vector3 result : {Vector3::Cross(input, input), output})
        {
            EXPECT_EQ(result, Vector3::Zero);
            EXPECT_FALSE(std::signbit(result.X));
            EXPECT_FALSE(std::signbit(result.Y));
            EXPECT_FALSE(std::signbit(result.Z));
        }
    }
}
