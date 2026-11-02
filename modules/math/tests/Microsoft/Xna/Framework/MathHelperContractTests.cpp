// SPDX-License-Identifier: MS-PL

#include <gtest/gtest.h>
#include <bit>
#include <cmath>
#include <cstdint>
#include <limits>
#include "Microsoft/Xna/Framework/MathHelper.hpp"

using Microsoft::Xna::Framework::MathHelper;

TEST(MathHelperContractTest, PublicConstantsHaveIndependentBinary32Values)
{
    EXPECT_EQ(std::bit_cast<std::uint32_t>(MathHelper::E), 0x402df854u);
    EXPECT_EQ(std::bit_cast<std::uint32_t>(MathHelper::Log10E), 0x3ede5bd9u);
    EXPECT_EQ(std::bit_cast<std::uint32_t>(MathHelper::Log2E), 0x3fb8aa3bu);
    EXPECT_EQ(std::bit_cast<std::uint32_t>(MathHelper::Pi), 0x40490fdbu);
    EXPECT_EQ(std::bit_cast<std::uint32_t>(MathHelper::PiOver2), 0x3fc90fdbu);
    EXPECT_EQ(std::bit_cast<std::uint32_t>(MathHelper::PiOver4), 0x3f490fdbu);
    EXPECT_EQ(std::bit_cast<std::uint32_t>(MathHelper::TwoPi), 0x40c90fdbu);
}

TEST(MathHelperContractTest, NontrivialInterpolationAndExtrapolationHaveDyadicLiterals)
{
    // The cubic basis at one half has weights (1/2,1/8,1/2,-1/8).
    EXPECT_EQ(MathHelper::Hermite(1, 4, -2, 2, 0.5f), -0.25f);
    EXPECT_EQ(MathHelper::CatmullRom(1, 4, -2, 2, 0.5f), 0.9375f);
    EXPECT_EQ(MathHelper::Barycentric(1, 4, -2, 0.5f, 0.25f), 1.75f);
    EXPECT_EQ(MathHelper::Lerp(1, 4, -1), -2);
    EXPECT_EQ(MathHelper::Lerp(1, 4, 2), 7);
    EXPECT_EQ(MathHelper::SmoothStep(1, 4, 0.25f), 1.46875f);
}

TEST(MathHelperContractTest, WrapAngleEndpointSignsAndNonfiniteInputs)
{
    EXPECT_EQ(std::bit_cast<std::uint32_t>(MathHelper::WrapAngle(-MathHelper::Pi)), 0x40490fdbu);
    EXPECT_EQ(std::bit_cast<std::uint32_t>(MathHelper::WrapAngle(MathHelper::Pi)), 0x40490fdbu);
    EXPECT_EQ(std::bit_cast<std::uint32_t>(MathHelper::WrapAngle(-0.0f)), 0x80000000u);
    EXPECT_EQ(std::bit_cast<std::uint32_t>(MathHelper::WrapAngle(-MathHelper::TwoPi)), 0x80000000u);
    EXPECT_TRUE(std::isnan(MathHelper::WrapAngle(std::numeric_limits<float>::infinity())));
    EXPECT_TRUE(std::isnan(MathHelper::WrapAngle(-std::numeric_limits<float>::infinity())));
    EXPECT_TRUE(std::isnan(MathHelper::WrapAngle(std::numeric_limits<float>::quiet_NaN())));
}
