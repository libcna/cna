// SPDX-License-Identifier: MS-PL

#include <gtest/gtest.h>
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <limits>
#include "Microsoft/Xna/Framework/MathHelper.hpp"

using namespace Microsoft::Xna::Framework;

namespace
{
    float Tq072Float(std::uint32_t word) { return std::bit_cast<float>(word); }
    void Tq072Bits(float actual, std::uint32_t expected)
    {
        EXPECT_EQ(std::bit_cast<std::uint32_t>(actual), expected);
    }

}

TEST(Tq072MathHelperArithmeticTest, LerpAndBarycentricKeepTheDifferenceUntilReturn)
{
    // Refresh Lerp IL_0000-0007; Barycentric IL_0000-000e.
    // 2^25 + (1 - 2^25)*1 = 1. A Single difference rounds to -2^25, giving 0.
    Tq072Bits(MathHelper::Lerp(0x1p25f, 1, 1), 0x3f800000u);
    Tq072Bits(MathHelper::Barycentric(0x1p25f, 1, 0x1p25f, 1, 1), 0x3f800000u);
}

TEST(Tq072MathHelperArithmeticTest, CatmullRomRoundsBothPowerLocals)
{
    // Refresh CatmullRom IL_0000-000a: Single square=0x3ee9e687, cube=0x3e9e17b9.
    // For (0,0,0,2), result=cube-square=-0x1.2f3b38p-3; unrounded cube gives ...9b.
    Tq072Bits(MathHelper::CatmullRom(0, 0, 0, 2, Tq072Float(0x3f2d0798u)), 0xbe179d9cu);
}

TEST(Tq072MathHelperArithmeticTest, HermiteRoundsPowersAndCompleteBasisExpressions)
{
    // Refresh Hermite IL_0003-0043: Single power and basis stores, no interior stores.
    // First t: s2=0x3f45191c, s3=0x3f2cf179, h1=2*s3-3*s2+1=0x3d2979e0.
    Tq072Bits(MathHelper::Hermite(1, 0, 0, 0, Tq072Float(0x3f60a069u)), 0x3d2979e0u);
    // Second t: h1 rounded after the complete expression is ...43; float operators give ...42.
    Tq072Bits(MathHelper::Hermite(1, 0, 0, 0, Tq072Float(0x3e603d61u)), 0x3f608c43u);
}

TEST(Tq072MathHelperArithmeticTest, SmoothStepRoundsTheCompleteWeightAtTheLerpCall)
{
    // Refresh SmoothStep IL_0013-0024: t*t*(3-2*t), then Single argument conversion.
    // t=0x1.9660b6p-5 gives weight 0x1.d3d114p-8 (0x3be9e88a).
    // Storing t*t or 3-2*t first gives 0x3be9e889.
    Tq072Bits(MathHelper::SmoothStep(0, 1, Tq072Float(0x3d4b305bu)), 0x3be9e88au);
}

TEST(Tq072MathHelperArithmeticTest, HermiteDoesNotSnapThePredecessorOfOne)
{
    // Refresh Hermite IL_0007-000a,0040-0056: t=1-2^-24, s2=1-2^-23,
    // s3=1-3*2^-24; h4=-2^-24. Tangent2=2^24 gives exactly -1.
    Tq072Bits(MathHelper::Hermite(0, 0, 0, 0x1p24f, Tq072Float(0x3f7fffffu)), 0xbf800000u);
}

TEST(Tq072MathHelperArithmeticTest, StoredPowerOverflowPropagatesThroughCubicTerms)
{
    // Refresh CatmullRom IL_0005,000a and Hermite IL_0006,000a store float32.
    // At t=2^64 the square is +Inf. Zero coefficient * Inf makes NaN.
    EXPECT_TRUE(std::isnan(MathHelper::CatmullRom(0, 0, 0, 2, 0x1p64f)));
    // At 2^43 the square is finite but the Single cube overflows; 0*Inf is NaN.
    EXPECT_TRUE(std::isnan(MathHelper::CatmullRom(0, 0, 0, 0, 0x1p43f)));
    EXPECT_TRUE(std::isnan(MathHelper::Hermite(1, 0, 0, 0, 0x1p64f)));
    for (float t : {std::numeric_limits<float>::infinity(),
                    -std::numeric_limits<float>::infinity(),
                    std::numeric_limits<float>::quiet_NaN()})
    {
        EXPECT_TRUE(std::isnan(MathHelper::CatmullRom(0, 0, 0, 2, t)));
        EXPECT_TRUE(std::isnan(MathHelper::Hermite(1, 0, 0, 0, t)));
    }
}

TEST(Tq072MathHelperArithmeticTest, ZeroWeightsStillEvaluateNonfiniteInputs)
{
    // Refresh Barycentric IL_0001-000d, Lerp IL_0001-0006 and CatmullRom IL_000b-0052:
    // no zero-weight or endpoint branches. Classification only: NaN payload is unspecified.
    const float inf = std::numeric_limits<float>::infinity();
    const float nan = std::numeric_limits<float>::quiet_NaN();
    EXPECT_TRUE(std::isnan(MathHelper::Lerp(1, inf, 0)));
    EXPECT_TRUE(std::isnan(MathHelper::Lerp(inf, 1, 1)));
    EXPECT_TRUE(std::isnan(MathHelper::Lerp(1, 2, nan)));
    EXPECT_TRUE(std::isnan(MathHelper::Barycentric(1, inf, 2, 0, 0)));
    EXPECT_TRUE(std::isnan(MathHelper::Barycentric(1, 2, inf, 0, 0)));
    EXPECT_TRUE(std::isnan(MathHelper::Barycentric(1, 2, 3, nan, 0)));
    EXPECT_TRUE(std::isnan(MathHelper::Barycentric(1, 2, 3, 0, nan)));
    EXPECT_TRUE(std::isnan(MathHelper::CatmullRom(inf, 1, 2, 3, 0)));
    EXPECT_TRUE(std::isnan(MathHelper::CatmullRom(0, 1, 2, inf, 1)));
}

TEST(Tq072MathHelperArithmeticTest, InterpolationArithmeticCanonicalizesTheseNegativeZeroEndpoints)
{
    // Refresh add order: -0 + (+0) becomes +0, including the zero basis products.
    // Lerp IL_0006, Barycentric IL_0006,000d, CatmullRom IL_001e-0052,
    // Hermite IL_004c-0055, SmoothStep IL_0024. Endpoint early returns change these bits.
    Tq072Bits(MathHelper::Lerp(-0.0f, 0, 0), 0u);
    Tq072Bits(MathHelper::Barycentric(-0.0f, 0, 0, 0, 0), 0u);
    Tq072Bits(MathHelper::CatmullRom(0, -0.0f, 0, 0, 0), 0u);
    Tq072Bits(MathHelper::Hermite(-0.0f, 0, 0, 0, 0), 0u);
    Tq072Bits(MathHelper::SmoothStep(-0.0f, 0, 0), 0u);
}

TEST(Tq072MathHelperArithmeticTest, DistancePreservesSingleRangeAndAbsoluteZero)
{
    // Refresh Distance IL_0002-0008: subtract, Single Abs call, Single return.
    // 1-2^-25 is exactly halfway between 1 and its predecessor: ties-to-even gives 1.
    Tq072Bits(MathHelper::Distance(1, 0x1p-25f), 0x3f800000u);
    Tq072Bits(MathHelper::Distance(-0.0f, 0.0f), 0u);
    Tq072Bits(MathHelper::Distance(0, -0.0f), 0u);
    Tq072Bits(MathHelper::Distance(0x1p-149f, 0), 1u);
    Tq072Bits(MathHelper::Distance(std::numeric_limits<float>::max(),
                                  -std::numeric_limits<float>::max()), 0x7f800000u);
    const float inf = std::numeric_limits<float>::infinity();
    Tq072Bits(MathHelper::Distance(-inf, 1), 0x7f800000u);
    EXPECT_TRUE(std::isnan(MathHelper::Distance(inf, inf)));
    EXPECT_TRUE(std::isnan(MathHelper::Distance(std::numeric_limits<float>::quiet_NaN(), 1)));
}

TEST(Tq072MathHelperArithmeticTest, AngleConversionUsesRefreshSingleConstants)
{
    // Refresh ToDegrees/ToRadians IL_0001: ldc.r4 57.2957764 / 0.0174532924.
    // ToDegrees(1) is the literal 0x42652ee0; exact 180/pi would give ...e1.
    Tq072Bits(MathHelper::ToDegrees(1), 0x42652ee0u);
    // binary32(1e-7) * binary32(pi/180) -> ...50; full double factor -> ...51.
    Tq072Bits(MathHelper::ToRadians(Tq072Float(0x33d6bf95u)), 0x30efe050u);
    for (auto convert : {MathHelper::ToDegrees, MathHelper::ToRadians})
    {
        Tq072Bits(convert(-0.0f), 0x80000000u);
        Tq072Bits(convert(0.0f), 0u);
        Tq072Bits(convert(std::numeric_limits<float>::infinity()), 0x7f800000u);
        Tq072Bits(convert(-std::numeric_limits<float>::infinity()), 0xff800000u);
        EXPECT_TRUE(std::isnan(convert(std::numeric_limits<float>::quiet_NaN())));
    }
}

TEST(Tq072MathHelperArithmeticTest, WrapAngleUsesTheRefreshDivisorAndAdjacentPiBoundary)
{
    // Refresh WrapAngle IL_0001-0011: binary64 IEEERemainder with divisor
    // exactly 6.283185482025146484375, then binary32; IL_0013-0035 maps -Pi to +Pi.
    // All words below follow integer remainder on binary32 input significands.
    constexpr std::array<std::array<std::uint32_t, 2>, 10> cases = {{
        {{0x40490fdau, 0x40490fdau}}, {{0x40490fdcu, 0xc0490fdau}},
        {{0xc0490fdau, 0xc0490fdau}}, {{0xc0490fdcu, 0x40490fdau}},
        {{0x4b800000u, 0xbfae2fecu}}, {{0xcb800000u, 0x3fae2fecu}},
        {{0x7f7fffffu, 0x3fddb0f8u}}, {{0xff7fffffu, 0xbfddb0f8u}},
        {{0x40c90fdbu, 0u}}, {{0xc0c90fdbu, 0x80000000u}}
    }};
    for (const auto& c : cases)
    {
        SCOPED_TRACE(c[0]);
        Tq072Bits(MathHelper::WrapAngle(Tq072Float(c[0])), c[1]);
    }
}
