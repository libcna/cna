// SPDX-License-Identifier: MS-PL

#include <gtest/gtest.h>
#include <bit>
#include <cmath>
#include <cstdint>
#include <limits>
#include "Microsoft/Xna/Framework/Quaternion.hpp"

using Microsoft::Xna::Framework::Quaternion;

namespace
{
    void ExpectBits(const Quaternion& q, std::uint32_t x, std::uint32_t y,
                    std::uint32_t z, std::uint32_t w)
    {
        EXPECT_EQ(std::bit_cast<std::uint32_t>(q.X), x);
        EXPECT_EQ(std::bit_cast<std::uint32_t>(q.Y), y);
        EXPECT_EQ(std::bit_cast<std::uint32_t>(q.Z), z);
        EXPECT_EQ(std::bit_cast<std::uint32_t>(q.W), w);
    }

    void ExpectNaN(const Quaternion& q)
    {
        EXPECT_TRUE(std::isnan(q.X));
        EXPECT_TRUE(std::isnan(q.Y));
        EXPECT_TRUE(std::isnan(q.Z));
        EXPECT_TRUE(std::isnan(q.W));
    }

    struct Sample { float magnitude; std::uint32_t inverseX; };
    // CNA-DEMOS-ISSUE-119: observed bits from the genuine XNA 4.0 x86 oracle.
    constexpr Sample Samples[] = {
        {1e20f, 0x9e3ce508}, {1e-30f, 0xf149f2ca},
        {-1e20f, 0x1e3ce508}, {-1e-30f, 0x7149f2ca},
        {std::numeric_limits<float>::max(), 0x80200000}
    };
}

TEST(QuaternionRangeTest, InversePreservesExtremeFiniteMagnitudesAndAliasing)
{
    for (const auto& sample : Samples)
    {
        SCOPED_TRACE(sample.magnitude);
        const Quaternion q(sample.magnitude, 0, 0, 0);
        ExpectBits(Quaternion::Inverse(q), sample.inverseX, 0x80000000, 0x80000000, 0);
        Quaternion output;
        Quaternion::Inverse(q, output);
        ExpectBits(output, sample.inverseX, 0x80000000, 0x80000000, 0);
        output = q;
        Quaternion::Inverse(output, output);
        ExpectBits(output, sample.inverseX, 0x80000000, 0x80000000, 0);
    }
}

TEST(QuaternionRangeTest, DividePreservesExtremeFiniteMagnitudesAndBothInputAliases)
{
    for (const auto& sample : Samples)
    {
        SCOPED_TRACE(sample.magnitude);
        const Quaternion q(sample.magnitude, 0, 0, 0);
        ExpectBits(Quaternion::Divide(Quaternion::Identity, q), sample.inverseX, 0, 0, 0);
        ExpectBits(Quaternion::Identity / q, sample.inverseX, 0, 0, 0);
        Quaternion output;
        Quaternion::Divide(Quaternion::Identity, q, output);
        ExpectBits(output, sample.inverseX, 0, 0, 0);
        output = q;
        Quaternion::Divide(Quaternion::Identity, output, output);
        ExpectBits(output, sample.inverseX, 0, 0, 0);
        output = Quaternion::Identity;
        Quaternion::Divide(output, q, output);
        ExpectBits(output, sample.inverseX, 0, 0, 0);
        ExpectBits(Quaternion::Divide(q, q), 0, 0, 0, 0x3f800000);
        output = q;
        Quaternion::Divide(output, output, output);
        ExpectBits(output, 0, 0, 0, 0x3f800000);
    }
}

TEST(QuaternionRangeTest, DivideDoesNotRoundInverseComponentsBeforeMultiplication)
{
    const Quaternion numerator(0, 1e20f, 0, 0);
    const Quaternion denominator(1e20f, 1e-20f, 0, 0);
    // An intermediate inverse Y around -1e-60 must contribute to the final W.
    ExpectBits(Quaternion::Divide(numerator, denominator), 0, 0, 0x3f800000, 0x000116c2);
    Quaternion alias = denominator;
    Quaternion::Divide(numerator, alias, alias);
    ExpectBits(alias, 0, 0, 0x3f800000, 0x000116c2);
}

TEST(QuaternionRangeTest, UndefinedInputsRetainIeeeClassifications)
{
    ExpectNaN(Quaternion::Inverse(Quaternion()));
    ExpectNaN(Quaternion::Divide(Quaternion::Identity, Quaternion()));
    const Quaternion infinite(std::numeric_limits<float>::infinity(), 0, 0, 0);
    const auto inverse = Quaternion::Inverse(infinite);
    EXPECT_TRUE(std::isnan(inverse.X));
    EXPECT_EQ(std::bit_cast<std::uint32_t>(inverse.Y), 0x80000000u);
    EXPECT_EQ(std::bit_cast<std::uint32_t>(inverse.Z), 0x80000000u);
    EXPECT_EQ(std::bit_cast<std::uint32_t>(inverse.W), 0u);
    ExpectNaN(Quaternion::Divide(Quaternion::Identity, infinite));
    const Quaternion nan(std::numeric_limits<float>::quiet_NaN(), 0, 0, 0);
    ExpectNaN(Quaternion::Inverse(nan));
    ExpectNaN(Quaternion::Divide(Quaternion::Identity, nan));
}
