// SPDX-License-Identifier: MS-PL

#include <gtest/gtest.h>
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <limits>
#include <type_traits>
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Vector4.hpp"

using namespace Microsoft::Xna::Framework;

namespace
{
    std::array<Vector4,18> Samples()
    {
        const float maximum = std::numeric_limits<float>::max();
        return {Vector4::Zero,Vector4(-0.0f,-0.0f,0,-0.0f),Vector4::One,
            Vector4::UnitX,Vector4::UnitY,Vector4::UnitZ,Vector4::UnitW,
            Vector4(1,-2,3,-4),Vector4(5,6,-7,-8),Vector4(0.3f,-0.6f,0.8f,0.1f),
            Vector4(1e20f,1e20f,1,2),Vector4(1e20f,-1e20f,1,3),Vector4(1e20f,1e20f,2,3),
            Vector4(1e-30f),Vector4(maximum,maximum,1,2),Vector4(maximum,-maximum,1,3),
            Vector4(std::numeric_limits<float>::infinity(),0,0,0),
            Vector4(std::bit_cast<float>(0x7fc00001u),1,2,3)};
    }

    void ExpectFloat(float actual,float expected)
    {
        if (std::isnan(expected)) EXPECT_TRUE(std::isnan(actual));
        else EXPECT_EQ(std::bit_cast<std::uint32_t>(actual),std::bit_cast<std::uint32_t>(expected));
    }

    template<class Vector> Vector Project(const Vector4& value)
    {
        if constexpr (std::is_same_v<Vector,Vector2>) return Vector2(value.X,value.Y);
        else if constexpr (std::is_same_v<Vector,Vector3>) return Vector3(value.X,value.Y,value.Z);
        else return value;
    }

    template<class Vector> void CheckDots()
    {
        // Matches all genuine XNA value/out/component-alias results for this table.
        for (const auto& a : Samples()) for (const auto& b : Samples())
        {
            double sum = static_cast<double>(a.X)*b.X + static_cast<double>(a.Y)*b.Y;
            if constexpr (!std::is_same_v<Vector,Vector2>) sum += static_cast<double>(a.Z)*b.Z;
            if constexpr (std::is_same_v<Vector,Vector4>) sum += static_cast<double>(a.W)*b.W;
            const float expected = static_cast<float>(sum);
            const auto left = Project<Vector>(a), right = Project<Vector>(b);
            ExpectFloat(Vector::Dot(left,right),expected);
            float output;
            Vector::Dot(left,right,output);
            ExpectFloat(output,expected);
            auto alias = left;
            Vector::Dot(alias,right,alias.X);
            ExpectFloat(alias.X,expected);
            alias = right;
            Vector::Dot(left,alias,alias.Y);
            ExpectFloat(alias.Y,expected);
        }
    }

    Vector3 ReferenceCross(const Vector3& a,const Vector3& b)
    {
        return Vector3(static_cast<float>(static_cast<double>(a.Y)*b.Z-static_cast<double>(a.Z)*b.Y),
                       static_cast<float>(static_cast<double>(a.Z)*b.X-static_cast<double>(a.X)*b.Z),
                       static_cast<float>(static_cast<double>(a.X)*b.Y-static_cast<double>(a.Y)*b.X));
    }

    void ExpectVector(const Vector3& actual,const Vector3& expected)
    {
        ExpectFloat(actual.X,expected.X);
        ExpectFloat(actual.Y,expected.Y);
        ExpectFloat(actual.Z,expected.Z);
    }
}

TEST(VectorProductCompatibilityTest, Vector2DotRetainsWideProductsAndComponentAliases)
{
    CheckDots<Vector2>();
}

TEST(VectorProductCompatibilityTest, Vector3DotRetainsWideProductsAndComponentAliases)
{
    CheckDots<Vector3>();
}

TEST(VectorProductCompatibilityTest, Vector4DotRetainsWideProductsAndComponentAliases)
{
    CheckDots<Vector4>();
}

TEST(VectorProductCompatibilityTest, CrossRetainsWideProductsZeroSignsAndBothOutputAliases)
{
    for (const auto& a : Samples()) for (const auto& b : Samples())
    {
        const auto originalLeft = Project<Vector3>(a), originalRight = Project<Vector3>(b);
        const auto expected = ReferenceCross(originalLeft,originalRight);
        ExpectVector(Vector3::Cross(originalLeft,originalRight),expected);
        auto left = originalLeft, right = originalRight;
        Vector3 output;
        Vector3::Cross(left,right,output);
        ExpectVector(output,expected);
        ExpectVector(left,originalLeft);
        ExpectVector(right,originalRight);
        Vector3::Cross(left,right,left);
        ExpectVector(left,expected);
        ExpectVector(right,originalRight);
        left = originalLeft;
        Vector3::Cross(left,right,right);
        ExpectVector(right,expected);
        ExpectVector(left,originalLeft);
    }
    for (const auto& sample : Samples())
    {
        auto alias = Project<Vector3>(sample);
        const auto expected = ReferenceCross(alias,alias);
        Vector3::Cross(alias,alias,alias);
        ExpectVector(alias,expected);
    }
}
