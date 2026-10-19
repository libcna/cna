// SPDX-License-Identifier: MS-PL
#include "Microsoft/Xna/Framework/Color.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <gtest/gtest.h>
#include <limits>

using namespace Microsoft::Xna::Framework;

namespace
{
// Independently verified by the 1,782-check genuine ColorFixedPointOracle.cs.
const std::array Colors{Color(0, 0, 0, 0),     Color(255, 255, 255, 255), Color(1, 2, 3, 4),
                        Color(10, 20, 30, 40), Color(110, 220, 130, 240), Color(0, 1, 254, 255),
                        Color(255, 254, 1, 0), Color(127, 128, 129, 130), Color(23, 137, 91, 253)};
const float Factors[]{std::numeric_limits<float>::quiet_NaN(),
                      -std::numeric_limits<float>::infinity(),
                      std::numeric_limits<float>::infinity(),
                      -1,
                      -0.0f,
                      0,
                      std::numeric_limits<float>::denorm_min(),
                      1.0f / 131072,
                      3.0f / 131072,
                      0.1f,
                      0.5f,
                      0.99999f,
                      1,
                      1.1f,
                      2,
                      255,
                      256,
                      1e20f};

std::array<int, 4> Channels(const Color &color)
{
    return {static_cast<int>(color.getRProperty()), static_cast<int>(color.getGProperty()),
            static_cast<int>(color.getBProperty()), static_cast<int>(color.getAProperty())};
}

int InterpolationFactor(float factor)
{
    const float scaled = factor * 65536.0f;
    if (std::isnan(scaled) || scaled < 0)
        return 0;
    if (scaled >= 65536)
        return 65536;
    const float floor = std::floor(scaled);
    const int integer = static_cast<int>(floor);
    return integer + (scaled - floor > 0.5f || (scaled - floor == 0.5f && integer % 2 != 0));
}
} // namespace

TEST(ColorFixedPointCompatibilityTest, LerpQuantizesTheFactorBeforeSignedChannelInterpolation)
{
    for (const auto &first : Colors)
        for (const auto &second : Colors)
            for (float factor : Factors)
            {
                SCOPED_TRACE(::testing::Message()
                             << first.ToString() << " -> " << second.ToString() << " factor=" << factor);
                auto expected = Channels(first);
                const auto end = Channels(second);
                const int fixed = InterpolationFactor(factor);
                for (std::size_t i = 0; i < expected.size(); ++i)
                    expected[i] += (end[i] - expected[i]) * fixed >> 16;
                EXPECT_EQ(Channels(Color::Lerp(first, second, factor)), expected);
            }
}

TEST(ColorFixedPointCompatibilityTest, MultiplyQuantizesCapsAndSaturatesWithoutNonfiniteIntegerCasts)
{
    for (const auto &color : Colors)
        for (float factor : Factors)
        {
            SCOPED_TRACE(::testing::Message() << color.ToString() << " factor=" << factor);
            const float scaled = factor * 65536.0f;
            const std::uint64_t fixed = std::isnan(scaled) || scaled < 0 ? 0
                                        : scaled > 16777215.0f           ? 16777215
                                                                         : static_cast<std::uint64_t>(scaled);
            auto expected = Channels(color);
            for (int &channel : expected)
                channel = static_cast<int>(std::min<std::uint64_t>(255, (channel * fixed) >> 16));
            EXPECT_EQ(Channels(Color::Multiply(color, factor)), expected);
            EXPECT_EQ(Channels(color * factor), expected);
            EXPECT_EQ(Channels(factor * color), expected); // Existing CNA commutative extension.
        }
}

TEST(ColorFixedPointCompatibilityTest, NaNLerpKeepsEveryFirstColorChannelIncludingAlpha)
{
    const Color first(10, 20, 30, 40), second(110, 220, 130, 240);
    EXPECT_EQ(Color::Lerp(first, second, std::numeric_limits<float>::quiet_NaN()), first);
    EXPECT_EQ(Color::Lerp(Color::Black, Color::White, std::numeric_limits<float>::quiet_NaN()), Color::Black);
}
