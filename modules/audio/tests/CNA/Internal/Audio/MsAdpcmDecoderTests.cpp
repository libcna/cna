// SPDX-License-Identifier: MS-PL
//
// TQ-018 follow-up: the .xnb reader harness reached a signed overflow in the MS-ADPCM step
// adaptation (MsAdpcmDecoder.cpp, `adaptation[nibble] * delta`). These tests decode hand-built
// blocks whose expected samples follow from the MS-ADPCM rules alone.

#include <gtest/gtest.h>

#include <cstdint>
#include <vector>

#include "CNA/Internal/Audio/MsAdpcmDecoder.hpp"

namespace
{
    using CNA::Internal::Audio::DecodeMsAdpcm;

    // One mono block: predictor 0 (coef1 = 256, coef2 = 0, so the prediction is sample1), the
    // given initial step, sample1 = sample2 = 0, then the nibbles high-first.
    std::vector<std::uint8_t> MonoBlock(const std::int16_t delta, const std::vector<std::uint8_t>& nibbles)
    {
        std::vector<std::uint8_t> block = {
            0u,
            static_cast<std::uint8_t>(delta & 0xFF), static_cast<std::uint8_t>((delta >> 8) & 0xFF),
            0u, 0u,
            0u, 0u,
        };
        for (std::size_t i = 0; i < nibbles.size(); i += 2)
        {
            const std::uint8_t low = i + 1 < nibbles.size() ? nibbles[i + 1] : 0u;
            block.push_back(static_cast<std::uint8_t>((nibbles[i] << 4) | low));
        }
        return block;
    }
}

// Eight nibbles of 8 (signed -8, adaptation 768) grow the step from 32767 by 3x per nibble:
// 98301, 294903, 884709, 2654127, then past INT_MAX/768 = 2796202, where the old code computed
// 768 * 7962381 in int (overflow). Every one of those samples saturates at -32768. The next
// nibble, 1, adds one large positive step to -32768 and must saturate at +32767 -- for any step
// of at least 65535. With int wraparound the step instead collapsed to the floor of 16 and the
// sample read -32752. Nibble 15 (signed -1) then swings back to -32768, and so on.
TEST(MsAdpcmDecoderTest, AGrowingStepNeverWrapsAndKeepsSaturating)
{
    std::vector<std::uint8_t> nibbles(8, 8u);
    for (int i = 0; i < 6; ++i)
    {
        nibbles.push_back(1u);
        nibbles.push_back(15u);
    }
    const auto block = MonoBlock(32767, nibbles);
    const auto samplesPerBlock = static_cast<std::uint16_t>(2 + nibbles.size());
    const auto pcm = DecodeMsAdpcm(block, 1, static_cast<std::uint16_t>(block.size()), samplesPerBlock);

    std::vector<std::int16_t> expected = {0, 0};  // sample2, sample1 from the header
    expected.insert(expected.end(), 8, std::int16_t{-32768});
    for (int i = 0; i < 6; ++i)
    {
        expected.push_back(32767);
        expected.push_back(-32768);
    }
    EXPECT_EQ(pcm, expected);
}

// The floor of 16 still holds: a zero header step and nibble 0 keep reconstructing the
// prediction (0) while the step stays at 16, so nibble 1 then yields exactly 16.
TEST(MsAdpcmDecoderTest, TheStepFloorIsSixteen)
{
    const auto block = MonoBlock(0, {0u, 0u, 1u, 0u});
    const auto pcm = DecodeMsAdpcm(block, 1, static_cast<std::uint16_t>(block.size()), 6);
    // nibble 0: 0 + 0*0 = 0, step max(16, 230*0/256) = 16; nibble 0: 0, step max(16, 230*16/256=14)=16;
    // nibble 1: 0 + 1*16 = 16, step 16; nibble 0: prediction 16 + 0 = 16.
    const std::vector<std::int16_t> expected = {0, 0, 0, 0, 16, 16};
    EXPECT_EQ(pcm, expected);
}
