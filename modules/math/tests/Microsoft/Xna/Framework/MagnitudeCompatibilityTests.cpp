// SPDX-License-Identifier: MS-PL

#include <gtest/gtest.h>
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <limits>
#include <type_traits>
#include "Microsoft/Xna/Framework/Quaternion.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Vector4.hpp"

using namespace Microsoft::Xna::Framework;

namespace
{
    std::array<Vector4,21> Samples()
    {
        return {Vector4::Zero,Vector4(-0.0f),Vector4::One,Vector4::UnitX,Vector4::UnitW,
            Vector4(3,4,0,0),Vector4(1,2,3,4),Vector4(0.3f,-0.6f,0.8f,0.1f),
            Vector4(0,1,0.8f,0),Vector4(0.8414709568023682f,0.5403022766113281f,0,0),
            Vector4(1e20f,0,0,0),Vector4(-1e20f,0,0,0),Vector4(1e20f),
            Vector4(1e-30f,0,0,0),Vector4(-1e-30f,0,0,0),Vector4(1e-30f),
            Vector4(std::numeric_limits<float>::denorm_min(),0,0,0),
            Vector4(std::numeric_limits<float>::max(),0,0,0),Vector4(std::numeric_limits<float>::max()),
            Vector4(std::numeric_limits<float>::infinity(),0,0,0),
            Vector4(std::numeric_limits<float>::quiet_NaN(),0,0,0)};
    }

    template<class V> constexpr int Components = std::is_same_v<V,Vector2>?2:std::is_same_v<V,Vector3>?3:4;

    template<class V> V Project(const Vector4& value)
    {
        if constexpr (Components<V> == 2) return V(value.X,value.Y);
        else if constexpr (Components<V> == 3) return V(value.X,value.Y,value.Z);
        else return V(value.X,value.Y,value.Z,value.W);
    }

    template<class V> std::array<float,4> Fields(const V& value)
    {
        if constexpr (Components<V> == 2) return {value.X,value.Y,0,0};
        else if constexpr (Components<V> == 3) return {value.X,value.Y,value.Z,0};
        else return {value.X,value.Y,value.Z,value.W};
    }

    void ExpectFloat(float actual,float expected)
    {
        if (std::isnan(expected)) EXPECT_TRUE(std::isnan(actual));
        else EXPECT_EQ(std::bit_cast<std::uint32_t>(actual),std::bit_cast<std::uint32_t>(expected));
    }

    template<class V> void ExpectFields(const V& actual,const std::array<float,4>& expected)
    {
        const auto fields = Fields(actual);
        for (int i=0;i<Components<V>;++i) ExpectFloat(fields[i],expected[i]);
    }

    template<class V> float ReferenceLength(const V& value)
    {
        double squared = 0;
        for (float component : Fields(value)) squared += static_cast<double>(component)*component;
        return static_cast<float>(std::sqrt(squared));
    }

    template<class V> class MagnitudeCompatibilityTest : public ::testing::Test {};
    using Magnitudes = ::testing::Types<Vector2,Vector3,Vector4,Quaternion>;
    TYPED_TEST_SUITE(MagnitudeCompatibilityTest,Magnitudes);

    TYPED_TEST(MagnitudeCompatibilityTest, LengthRetainsFiniteRangeWithoutChangingSquaredResult)
    {
        for (const auto& sample : Samples())
        {
            const auto value = Project<TypeParam>(sample);
            ExpectFloat(value.Length(),ReferenceLength(value));
        }
        EXPECT_TRUE(std::isinf(Project<TypeParam>(Vector4(1e20f,0,0,0)).LengthSquared()));
        ExpectFloat(Project<TypeParam>(Vector4(1e-30f,0,0,0)).LengthSquared(),0.0f);
    }

    TYPED_TEST(MagnitudeCompatibilityTest, NormalizeMatchesMeasuredRoundingInEveryForm)
    {
        // Genuine direct-entry XNA oracle: round the sqrt to float, not the reciprocal.
        for (const auto& sample : Samples())
        {
            const auto source = Project<TypeParam>(sample);
            auto expected = Fields(source);
            const double inverse = 1.0/static_cast<double>(ReferenceLength(source));
            for (float& component : expected) component = static_cast<float>(component*inverse);
            ExpectFields(TypeParam::Normalize(source),expected);
            auto input = source;
            TypeParam output;
            TypeParam::Normalize(input,output);
            ExpectFields(output,expected);
            ExpectFields(input,Fields(source));
            TypeParam::Normalize(input,input);
            ExpectFields(input,expected);
            input = source;
            input.Normalize();
            ExpectFields(input,expected);
        }
    }

    template<class V> class DistanceCompatibilityTest : public ::testing::Test {};
    using Vectors = ::testing::Types<Vector2,Vector3,Vector4>;
    TYPED_TEST_SUITE(DistanceCompatibilityTest,Vectors);

    TYPED_TEST(DistanceCompatibilityTest, ValueOutputAndComponentAliasesKeepWideIntermediates)
    {
        for (const auto& a : Samples()) for (const auto& b : Samples())
        {
            const auto sourceLeft = Project<TypeParam>(a), sourceRight = Project<TypeParam>(b);
            const auto leftFields = Fields(sourceLeft), rightFields = Fields(sourceRight);
            double squared = 0;
            for (int i=0;i<Components<TypeParam>;++i)
            {
                const double difference = static_cast<double>(leftFields[i])-rightFields[i];
                squared += difference*difference;
            }
            const float expected = static_cast<float>(std::sqrt(squared));
            ExpectFloat(TypeParam::Distance(sourceLeft,sourceRight),expected);
            auto left = sourceLeft, right = sourceRight;
            float output;
            TypeParam::Distance(left,right,output);
            ExpectFloat(output,expected);
            ExpectFields(left,leftFields);
            ExpectFields(right,rightFields);
            TypeParam::Distance(left,right,left.X);
            ExpectFloat(left.X,expected);
            left = sourceLeft;
            TypeParam::Distance(left,right,right.Y);
            ExpectFloat(right.Y,expected);
        }
        const auto zero = Project<TypeParam>(Vector4::Zero);
        const auto huge = Project<TypeParam>(Vector4(1e20f,0,0,0));
        const auto tiny = Project<TypeParam>(Vector4(1e-30f,0,0,0));
        EXPECT_TRUE(std::isinf(TypeParam::DistanceSquared(zero,huge)));
        ExpectFloat(TypeParam::DistanceSquared(zero,tiny),0.0f);
        float output;
        TypeParam::DistanceSquared(zero,huge,output);
        EXPECT_TRUE(std::isinf(output));
        TypeParam::DistanceSquared(zero,tiny,output);
        ExpectFloat(output,0.0f);
    }
}
