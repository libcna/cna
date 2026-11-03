// SPDX-License-Identifier: MS-PL
#include <gtest/gtest.h>
#include <bit>
#include <cmath>
#include <cstdint>
#include <limits>
#include "Microsoft/Xna/Framework/MathHelper.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Vector4.hpp"

using namespace Microsoft::Xna::Framework;

TEST(MathSpecialValueContractTest, HermiteEvaluatesZeroWeightedNonfiniteOperandsAtBothEndpoints)
{
    // XNA MathHelper.Hermite IL_0000–0056 evaluates all four basis products;
    // 0*Inf and 0*NaN remain NaN even at an interpolation endpoint.
    const float inf = std::numeric_limits<float>::infinity();
    const float nan = std::numeric_limits<float>::quiet_NaN();
    for (float amount : {0.0f, 1.0f})
    {
        EXPECT_TRUE(std::isnan(MathHelper::Hermite(1, inf, 2, 0, amount)));
        EXPECT_TRUE(std::isnan(MathHelper::Hermite(1, 0, 2, -inf, amount)));
        EXPECT_TRUE(std::isnan(MathHelper::Hermite(1, nan, 2, 0, amount)));
    }
    EXPECT_TRUE(std::isnan(MathHelper::Hermite(1, 0, inf, 0, 0)));
    EXPECT_TRUE(std::isnan(MathHelper::Hermite(inf, 0, 2, 0, 1)));
}

TEST(MathSpecialValueContractTest, HermiteDoesNotSnapSubnormalAmountsToAnEndpoint)
{
    // XNA MathHelper.Hermite IL_0033–0056 has no epsilon shortcut.
    // max_float * denorm_min = (2^128-2^104)*2^-149 = 2^-21-2^-45.
    const float tiny = std::numeric_limits<float>::denorm_min();
    EXPECT_EQ(std::bit_cast<std::uint32_t>(MathHelper::Hermite(
        0, std::numeric_limits<float>::max(), 0, 0, tiny)), 0x34ffffffu);
    EXPECT_EQ(MathHelper::Hermite(0, 8, 0, 0, 0.25f), 1.125f); // 8*(t^3-2t^2+t).
    EXPECT_EQ(MathHelper::Hermite(0, 0, 0, 8, 0.75f), -1.125f); // 8*(t^3-t^2).
}

TEST(MathSpecialValueContractTest, ScalarSmoothStepUsesLerpArithmeticForNonfiniteEndpoints)
{
    // XNA MathHelper.SmoothStep IL_0011–0024: Lerp(a,b,t*t*(3-2*t)).
    const float inf = std::numeric_limits<float>::infinity();
    const float nan = std::numeric_limits<float>::quiet_NaN();
    for (float amount : {-inf, -1.0f, 0.0f, 0.5f, 1.0f, 2.0f, inf})
    {
        EXPECT_TRUE(std::isnan(MathHelper::SmoothStep(inf, 2, amount)));
        EXPECT_TRUE(std::isnan(MathHelper::SmoothStep(inf, inf, amount)));
    }
    EXPECT_EQ(MathHelper::SmoothStep(2, 10, -inf), 2);
    EXPECT_EQ(MathHelper::SmoothStep(2, 10, inf), 10);
    EXPECT_TRUE(std::isnan(MathHelper::SmoothStep(2, 10, nan)));
    EXPECT_EQ(MathHelper::SmoothStep(2, 10, 0.25f), 3.25f); // 2+8*(5/32).
}

namespace
{
    template<class V> class VectorSpecialValueContractTest : public ::testing::Test {};
    using Vectors = ::testing::Types<Vector2, Vector3, Vector4>;
    TYPED_TEST_SUITE(VectorSpecialValueContractTest, Vectors);

    template<class V> void ExpectNaNComponents(const V& value)
    {
        EXPECT_TRUE(std::isnan(value.X));
        EXPECT_TRUE(std::isnan(value.Y));
        if constexpr (requires { value.Z; }) EXPECT_TRUE(std::isnan(value.Z));
        if constexpr (requires { value.W; }) EXPECT_TRUE(std::isnan(value.W));
    }

    TYPED_TEST(VectorSpecialValueContractTest, SmoothStepPreservesLerpNonfiniteArithmeticInEveryComponent)
    {
        using V = TypeParam;
        // XNA Vector2.SmoothStep IL_0021–006f (Vector3/4 have the same per-component formula).
        // In particular Inf+(finite-Inf)*t is NaN, including t=0 and t=1.
        const V infinite(std::numeric_limits<float>::infinity()), finite(2);
        for (float amount : {-1.0f, 0.0f, 0.5f, 1.0f, 2.0f})
        {
            SCOPED_TRACE(amount);
            V output;
            ExpectNaNComponents(V::SmoothStep(infinite, finite, amount));
            V::SmoothStep(infinite, finite, amount, output);
            ExpectNaNComponents(output);
            ExpectNaNComponents(V::SmoothStep(infinite, infinite, amount));
            V::SmoothStep(infinite, infinite, amount, output);
            ExpectNaNComponents(output);
        }
    }

    TYPED_TEST(VectorSpecialValueContractTest, ZeroNormalizationAssertsEveryComponentAndInputPreservation)
    {
        using V = TypeParam;
        // FNA Vector2/3/4.Normalize: reciprocal zero length is Inf; 0*Inf gives NaN.
        const V input(0);
        V output(99);
        V::Normalize(input, output);
        ExpectNaNComponents(output);
        ExpectNaNComponents(V::Normalize(input));
        V instance = input;
        instance.Normalize();
        ExpectNaNComponents(instance);
        EXPECT_EQ(input, V(0));
    }

    TYPED_TEST(VectorSpecialValueContractTest, InterpolationDoesNotBroadcastTheFirstCoordinate)
    {
        using V = TypeParam;
        // Per-component cubic basis (FNA Vector*.cs); at t=1/4, h00=27/32,
        // h10=9/64, h01=5/32, h11=-3/64. Each coordinate has distinct data.
        V first(1), tangent1(4), second(-2), tangent2(2), expected(1);
        first.Y=2; tangent1.Y=-2; second.Y=6; tangent2.Y=-4; expected.Y=2.53125f;
        if constexpr (requires { first.Z; })
        {
            first.Z=-4; tangent1.Z=8; second.Z=4; tangent2.Z=0; expected.Z=-1.625f;
        }
        if constexpr (requires { first.W; })
        {
            first.W=8; tangent1.W=0; second.W=0; tangent2.W=-8; expected.W=7.125f;
        }
        // X: 27/32 + 4*9/64 - 2*5/32 - 2*3/64 = 1.
        V output;
        EXPECT_EQ(V::Hermite(first,tangent1,second,tangent2,0.25f),expected);
        V::Hermite(first,tangent1,second,tangent2,0.25f,output);
        EXPECT_EQ(output,expected);
        auto alias=first;
        V::Hermite(alias,tangent1,second,tangent2,0.25f,alias);
        EXPECT_EQ(alias,expected);
    }
}
