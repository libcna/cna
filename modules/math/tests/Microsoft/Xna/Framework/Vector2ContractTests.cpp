// SPDX-License-Identifier: MS-PL

#include <gtest/gtest.h>
#include "Microsoft/Xna/Framework/Vector2.hpp"

using Microsoft::Xna::Framework::Vector2;

TEST(Vector2ContractTest, CubicAndBarycentricValueFormsHaveNonendpointLiteralComponents)
{
    const Vector2 a(1, 2), b(4, -2), c(-2, 6), d(2, -4);
    EXPECT_EQ(Vector2::Barycentric(a, b, c, 0.5f, 0.25f), Vector2(1.75f, 1));
    EXPECT_EQ(Vector2::CatmullRom(a, b, c, d, 0.5f), Vector2(0.9375f, 2.375f));
    EXPECT_EQ(Vector2::Hermite(a, b, c, d, 0.5f), Vector2(-0.25f, 4.25f));
    EXPECT_EQ(Vector2::SmoothStep(a, b, 0.25f), Vector2(1.46875f, 1.375f));
}

TEST(Vector2ContractTest, CubicAndBarycentricOutFormsHaveNonendpointLiteralComponents)
{
    const Vector2 a(1, 2), b(4, -2), c(-2, 6), d(2, -4);
    Vector2 output(99);
    Vector2::Barycentric(a, b, c, 0.5f, 0.25f, output);
    EXPECT_EQ(output, Vector2(1.75f, 1));
    Vector2::CatmullRom(a, b, c, d, 0.5f, output);
    EXPECT_EQ(output, Vector2(0.9375f, 2.375f));
    Vector2::Hermite(a, b, c, d, 0.5f, output);
    EXPECT_EQ(output, Vector2(-0.25f, 4.25f));
    Vector2::SmoothStep(a, b, 0.25f, output);
    EXPECT_EQ(output, Vector2(1.46875f, 1.375f));
}

TEST(Vector2ContractTest, LerpExtrapolatesWhileSmoothStepClampsThroughBothForms)
{
    const Vector2 a(1, 2), b(4, -2);
    Vector2 output(99);
    EXPECT_EQ(Vector2::Lerp(a, b, -1), Vector2(-2, 6));
    Vector2::Lerp(a, b, -1, output);
    EXPECT_EQ(output, Vector2(-2, 6));
    EXPECT_EQ(Vector2::Lerp(a, b, 2), Vector2(7, -6));
    Vector2::Lerp(a, b, 2, output);
    EXPECT_EQ(output, Vector2(7, -6));
    EXPECT_EQ(Vector2::SmoothStep(a, b, -1), Vector2(1, 2));
    Vector2::SmoothStep(a, b, -1, output);
    EXPECT_EQ(output, Vector2(1, 2));
    EXPECT_EQ(Vector2::SmoothStep(a, b, 2), Vector2(4, -2));
    Vector2::SmoothStep(a, b, 2, output);
    EXPECT_EQ(output, Vector2(4, -2));
}

TEST(Vector2ContractTest, NormalizationAssertsDirectionInEveryForm)
{
    const Vector2 input(3, -4);
    const Vector2 expected(0.6f, -0.8f);
    EXPECT_EQ(Vector2::Normalize(input), expected);
    Vector2 output(99);
    Vector2::Normalize(input, output);
    EXPECT_EQ(output, expected);
    auto instance = input;
    instance.Normalize();
    EXPECT_EQ(instance, expected);
    EXPECT_EQ(input, Vector2(3, -4));
}

TEST(Vector2ContractTest, DotAndReflectionUseBothCoordinatesAndDoNotNormalizeTheNormal)
{
    const Vector2 value(1, 2), normal(3, -4);
    EXPECT_EQ(Vector2::Dot(value, normal), -5);
    float dot = 99;
    Vector2::Dot(value, normal, dot);
    EXPECT_EQ(dot, -5);
    EXPECT_EQ(Vector2::Reflect(value, normal), Vector2(31, -38));
    Vector2 output(99);
    Vector2::Reflect(value, normal, output);
    EXPECT_EQ(output, Vector2(31, -38));
}
