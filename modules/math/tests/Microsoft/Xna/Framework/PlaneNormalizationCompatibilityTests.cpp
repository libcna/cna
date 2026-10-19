// SPDX-License-Identifier: MS-PL

#include <gtest/gtest.h>
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <limits>
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Plane.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"

using namespace Microsoft::Xna::Framework;

namespace
{
    // Genuine XNA 4.0: wide squared magnitude and reciprocal, explicit Single
    // conversion of Math.Sqrt, and the strict near-unit shortcut. The corresponding
    // 3405-case oracle distinguishes this from six other rounding models.
    Plane ReferenceNormalize(const Plane& value)
    {
        const double x = value.Normal.X, y = value.Normal.Y, z = value.Normal.Z;
        const double squared = x*x + y*y + z*z;
        if (std::abs(squared - 1.0) < 1.1920928955078125e-7) return value;
        const float length = static_cast<float>(std::sqrt(squared));
        const double inverse = 1.0 / static_cast<double>(length);
        return Plane(static_cast<float>(x*inverse), static_cast<float>(y*inverse),
                     static_cast<float>(z*inverse), static_cast<float>(value.D*inverse));
    }

    void ExpectFloat(float actual, float expected)
    {
        if (std::isnan(expected)) EXPECT_TRUE(std::isnan(actual));
        else EXPECT_EQ(std::bit_cast<std::uint32_t>(actual), std::bit_cast<std::uint32_t>(expected));
    }

    void ExpectPlane(const Plane& actual, const Plane& expected)
    {
        ExpectFloat(actual.Normal.X, expected.Normal.X);
        ExpectFloat(actual.Normal.Y, expected.Normal.Y);
        ExpectFloat(actual.Normal.Z, expected.Normal.Z);
        ExpectFloat(actual.D, expected.D);
    }
}

TEST(PlaneNormalizationCompatibilityTest, AllFormsPreserveMeasuredRoundingAndFiniteRange)
{
    constexpr std::array<float,15> samples = {0.0f,-0.0f,1.0f,-1.0f,0.3f,-0.6f,3.0f,4.0f,
        1e20f,1e-30f,std::numeric_limits<float>::max(),std::numeric_limits<float>::denorm_min(),
        std::numeric_limits<float>::infinity(),-std::numeric_limits<float>::infinity(),
        std::bit_cast<float>(0x7fc00001u)};
    for (float x : samples) for (float y : samples) for (float z : {0.0f,0.8f,1e-30f})
    {
        SCOPED_TRACE(::testing::Message() << x << ',' << y << ',' << z);
        const Plane source(x,y,z,-2.0f), expected = ReferenceNormalize(source);
        ExpectPlane(Plane::Normalize(source), expected);
        auto input = source;
        Plane result;
        Plane::Normalize(input,result);
        ExpectPlane(result,expected);
        ExpectPlane(input,source);
        Plane::Normalize(input,input);
        ExpectPlane(input,expected);
        input = source;
        input.Normalize();
        ExpectPlane(input,expected);
    }
}

TEST(PlaneNormalizationCompatibilityTest, NearUnitShortcutAndItsStrictBoundary)
{
    for (const auto bits : {0x3f7ffffeu,0x3f7fffffu,0x3f800000u,0x3f800001u,0x3f800002u})
    {
        const Plane source(std::bit_cast<float>(bits),0,0,-2);
        ExpectPlane(Plane::Normalize(source),ReferenceNormalize(source));
    }
    const Plane almostUnit(0.8414709568023682f,0.5403022766113281f,0,-2);
    ExpectPlane(Plane::Normalize(almostUnit),almostUnit);
    auto alias = almostUnit;
    Plane::Normalize(alias,alias);
    ExpectPlane(alias,almostUnit);
    alias.Normalize();
    ExpectPlane(alias,almostUnit);
}

TEST(MatrixShadowCompatibilityTest, ScaledAxisPlanesRetainValueAndOutputResults)
{
    for (const auto& normal : {Vector3::UnitX,Vector3::UnitY,Vector3::UnitZ})
    for (float d : {-2.0f,0.0f})
    for (const auto& direction : {Vector3(-1,-1,0),Vector3(2,3,4),Vector3::Zero})
    for (float scale : {0.25f,2.0f,8.0f})
    {
        const Plane unit(normal,d), scaled(normal*scale,d*scale);
        const auto expected = Matrix::CreateShadow(direction,unit);
        EXPECT_EQ(Matrix::CreateShadow(direction,scaled),expected);
        auto planeInput = scaled;
        auto lightInput = direction;
        Matrix result;
        Matrix::CreateShadow(lightInput,planeInput,result);
        EXPECT_EQ(result,expected);
        EXPECT_EQ(planeInput,scaled);
        EXPECT_EQ(lightInput,direction);
    }
}

TEST(MatrixShadowCompatibilityTest, NearUnitPlanesRetainShadowAndReflectionCoefficients)
{
    const Plane plane(0.8414709568023682f,0.5403022766113281f,0,-2);
    const Vector3 light(2,3,4);
    const auto shadow = Matrix::CreateShadow(light,plane);
    EXPECT_EQ(shadow.M41,4.0f);
    EXPECT_EQ(shadow.M42,6.0f);
    EXPECT_EQ(shadow.M43,8.0f);
    Matrix output;
    Matrix::CreateShadow(light,plane,output);
    EXPECT_EQ(output.M41,4.0f);
    EXPECT_EQ(output.M42,6.0f);
    EXPECT_EQ(output.M43,8.0f);
    const auto reflection = Matrix::CreateReflection(plane);
    EXPECT_EQ(reflection.M41,4.0f*plane.Normal.X);
    EXPECT_EQ(reflection.M42,4.0f*plane.Normal.Y);
}
