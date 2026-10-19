// SPDX-License-Identifier: MS-PL

#include <gtest/gtest.h>
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include "Microsoft/Xna/Framework/MathHelper.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Vector4.hpp"

using namespace Microsoft::Xna::Framework;

namespace
{
    // Genuine XNA 4.0 oracle: every pair/triple, including distinct quiet NaNs.
    constexpr std::array<std::uint32_t, 8> samples = {
        0xff800000u, 0xbf800000u, 0x80000000u, 0u,
        0x3f800000u, 0x7f800000u, 0x7fc00001u, 0xffc12345u
    };

    void ExpectBits(float actual, float expected)
    {
        EXPECT_EQ(std::bit_cast<std::uint32_t>(actual), std::bit_cast<std::uint32_t>(expected));
    }

    template<class V> void ExpectComponents(const V& actual, float expected)
    {
        ExpectBits(actual.X, expected);
        ExpectBits(actual.Y, expected);
        if constexpr (requires { actual.Z; }) ExpectBits(actual.Z, expected);
        if constexpr (requires { actual.W; }) ExpectBits(actual.W, expected);
    }

    template<class V> class VectorBoundsCompatibilityTest : public testing::Test {};
    using Vectors = testing::Types<Vector2, Vector3, Vector4>;
    TYPED_TEST_SUITE(VectorBoundsCompatibilityTest, Vectors);

    TYPED_TEST(VectorBoundsCompatibilityTest, MinMaxOperandSelectionAndAliases)
    {
        using V = TypeParam;
        for (auto abits : samples) for (auto bbits : samples)
        {
            SCOPED_TRACE(testing::Message() << "a=" << abits << " b=" << bbits);
            const float a = std::bit_cast<float>(abits), b = std::bit_cast<float>(bbits);
            const float min = a < b ? a : b, max = a > b ? a : b;
            const V first(a), second(b);
            V result;
            ExpectComponents(V::Min(first, second), min);
            ExpectComponents(V::Max(first, second), max);
            V::Min(first, second, result); ExpectComponents(result, min);
            V::Max(first, second, result); ExpectComponents(result, max);
            result = first; V::Min(result, second, result); ExpectComponents(result, min);
            result = second; V::Min(first, result, result); ExpectComponents(result, min);
            result = first; V::Max(result, second, result); ExpectComponents(result, max);
            result = second; V::Max(first, result, result); ExpectComponents(result, max);
        }
    }

    TYPED_TEST(VectorBoundsCompatibilityTest, ClampComparisonOrderAndAliases)
    {
        using V = TypeParam;
        for (auto vbits : samples) for (auto lobits : samples) for (auto hibits : samples)
        {
            SCOPED_TRACE(testing::Message() << "value=" << vbits << " min=" << lobits << " max=" << hibits);
            const float value = std::bit_cast<float>(vbits), lo = std::bit_cast<float>(lobits), hi = std::bit_cast<float>(hibits);
            float expected = value > hi ? hi : value;
            expected = expected < lo ? lo : expected;
            const V input(value), minimum(lo), maximum(hi);
            V result;
            ExpectComponents(V::Clamp(input, minimum, maximum), expected);
            V::Clamp(input, minimum, maximum, result); ExpectComponents(result, expected);
            result = input; V::Clamp(result, minimum, maximum, result); ExpectComponents(result, expected);
            result = minimum; V::Clamp(input, result, maximum, result); ExpectComponents(result, expected);
            result = maximum; V::Clamp(input, minimum, result, result); ExpectComponents(result, expected);
        }
    }
}

TEST(MathBoundsCompatibilityTest, ScalarMinMaxPropagateFirstNanAndChooseSecondOnTies)
{
    for (auto abits : samples) for (auto bbits : samples)
    {
        SCOPED_TRACE(testing::Message() << "a=" << abits << " b=" << bbits);
        const float a = std::bit_cast<float>(abits), b = std::bit_cast<float>(bbits);
        ExpectBits(MathHelper::Min(a, b), std::isnan(a) || a < b ? a : b);
        ExpectBits(MathHelper::Max(a, b), std::isnan(a) || a > b ? a : b);
    }
}

TEST(MathBoundsCompatibilityTest, ScalarClampRetainsComparisonOrder)
{
    for (auto vbits : samples) for (auto lobits : samples) for (auto hibits : samples)
    {
        const float value = std::bit_cast<float>(vbits), lo = std::bit_cast<float>(lobits), hi = std::bit_cast<float>(hibits);
        float expected = value > hi ? hi : value;
        expected = expected < lo ? lo : expected;
        ExpectBits(MathHelper::Clamp(value, lo, hi), expected);
    }
}

TEST(MathBoundsCompatibilityTest, MixedComponentIntervalsUseEachOwnBounds)
{
    const Vector4 input(2, 5, 7, -6), minimum(-2, 3, 8, -9), maximum(1, 7, 6, -4);
    const Vector4 expected(1, 5, 8, -6);
    EXPECT_EQ(Vector4::Clamp(input, minimum, maximum), expected);
    Vector4 output;
    Vector4::Clamp(input, minimum, maximum, output);
    EXPECT_EQ(output, expected);
}
