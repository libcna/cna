// SPDX-License-Identifier: MS-PL
#include <gtest/gtest.h>
#include "CNA/Internal/Audio/EncodedWaveBankDecoder.hpp"

#include <array>
#include <cstdint>
#include <stdexcept>
#include <vector>

namespace
{
int Sample(const std::vector<std::uint8_t>& bytes, std::size_t sample)
{
    const auto word = unsigned(bytes.at(sample * 2)) | (unsigned(bytes.at(sample * 2 + 1)) << 8);
    return word < 32768 ? static_cast<int>(word) : static_cast<int>(word) - 65536;
}

TEST(EncodedWaveBankMatrix, EverySourceChannelUsesTheMeasuredXAudio2Coefficients)
{
    // Oracle: FAudio src/matrix_defaults.inl:159-160 (6 x 2), destination-major.
    // Each literal is round(8192 * the corresponding coefficient), e.g.
    // FL->L:8192*.294545442=2412.916... ->2413, LFE:8192*.090909094 ->745.
    constexpr std::array<std::array<int,2>,6> expected{{
        {2413,0}, {0,2413}, {1705,1705}, {745,745}, {2063,1266}, {1266,2063}
    }};
    for (int sign : {1,-1})
    {
        std::vector<std::int16_t> native(6 * 6,0);
        for (std::size_t channel=0; channel<6; ++channel) native[channel*6+channel]=sign*8192;
        const auto stereo=CNA::Internal::Audio::DownmixEncodedWaveBank51(native);
        ASSERT_EQ(stereo.size(),6u*2u*2u); // six source frames remain six stereo frames
        for (std::size_t channel=0; channel<6; ++channel)
        {
            EXPECT_EQ(Sample(stereo,channel*2),sign*expected[channel][0]) << channel;
            EXPECT_EQ(Sample(stereo,channel*2+1),sign*expected[channel][1]) << channel;
        }
    }
}

TEST(EncodedWaveBankMatrix, NormalizedFullScaleDoesNotWrapAndPartialFramesRefuse)
{
    // Each destination's coefficients sum to ~1, including the nonzero LFE and
    // surround crossfeed. Unity source frames must reach the same S16 endpoint.
    for (auto endpoint : {std::int16_t(32767),std::int16_t(-32768)})
    {
        const auto stereo=CNA::Internal::Audio::DownmixEncodedWaveBank51(
            std::vector<std::int16_t>(12,endpoint));
        ASSERT_EQ(stereo.size(),8u);
        for (int i=0; i<4; ++i) EXPECT_EQ(Sample(stereo,i),endpoint);
    }
    EXPECT_THROW((void)CNA::Internal::Audio::DownmixEncodedWaveBank51(std::vector<std::int16_t>(5)),std::runtime_error);
    EXPECT_TRUE(CNA::Internal::Audio::DownmixEncodedWaveBank51({}).empty());
}
}
