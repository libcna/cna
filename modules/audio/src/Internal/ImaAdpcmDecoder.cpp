// SPDX-License-Identifier: MS-PL
#include "CNA/Internal/Audio/ImaAdpcmDecoder.hpp"

#include <algorithm>
#include <array>
#include <cstring>
#include <stdexcept>

namespace CNA::Internal::Audio
{
    namespace
    {
        constexpr std::array<int, 16> IndexTable = {
            -1, -1, -1, -1, 2, 4, 6, 8,
            -1, -1, -1, -1, 2, 4, 6, 8};

        constexpr std::array<int, 89> StepTable = {
            7,     8,     9,     10,    11,    12,    13,    14,    16,    17,
            19,    21,    23,    25,    28,    31,    34,    37,    41,    45,
            50,    55,    60,    66,    73,    80,    88,    97,    107,   118,
            130,   143,   157,   173,   190,   209,   230,   253,   279,   307,
            337,   371,   408,   449,   494,   544,   598,   658,   724,   796,
            876,   963,   1060,  1166,  1282,  1411,  1552,  1707,  1878,  2066,
            2272,  2499,  2749,  3024,  3327,  3660,  4026,  4428,  4871,  5358,
            5894,  6484,  7132,  7845,  8630,  9493,  10442, 11487, 12635, 13899,
            15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794, 32767};

        struct ChannelState
        {
            int predictor = 0;
            int stepIndex = 0;
        };

        [[nodiscard]] std::int16_t DecodeNibble(ChannelState& state, const std::uint8_t nibble) noexcept
        {
            const int step = StepTable[static_cast<std::size_t>(state.stepIndex)];
            int diff = step >> 3;
            if ((nibble & 4u) != 0u) diff += step;
            if ((nibble & 2u) != 0u) diff += step >> 1;
            if ((nibble & 1u) != 0u) diff += step >> 2;
            state.predictor += (nibble & 8u) != 0u ? -diff : diff;
            state.predictor = std::clamp(state.predictor, -32768, 32767);
            state.stepIndex = std::clamp(
                state.stepIndex + IndexTable[nibble], 0, static_cast<int>(StepTable.size()) - 1);
            return static_cast<std::int16_t>(state.predictor);
        }

        [[nodiscard]] ChannelState ReadHeader(const std::uint8_t* at)
        {
            std::int16_t predictor = 0;
            std::memcpy(&predictor, at, sizeof(predictor));
            ChannelState state;
            state.predictor = predictor;
            state.stepIndex = std::clamp(static_cast<int>(at[2]), 0, static_cast<int>(StepTable.size()) - 1);
            return state;
        }
    }

    std::vector<std::int16_t> DecodeImaAdpcm(
        const std::span<const std::uint8_t> bytes, const std::uint16_t channels,
        const std::uint16_t blockAlign, const std::uint16_t samplesPerBlock)
    {
        if (channels != 1u && channels != 2u)
        {
            throw std::invalid_argument("DecodeImaAdpcm: only one or two channels can be decoded.");
        }
        const std::size_t headerBytes = 4u * channels;
        if (blockAlign <= headerBytes)
        {
            throw std::invalid_argument("DecodeImaAdpcm: blockAlign is too small for its own header.");
        }
        if (blockAlign == 0u || bytes.size() % blockAlign != 0u)
        {
            throw std::invalid_argument(
                "DecodeImaAdpcm: byte length is not a whole number of blocks.");
        }

        const std::size_t blockCount = bytes.size() / blockAlign;
        std::vector<std::int16_t> output;
        output.reserve(blockCount * samplesPerBlock * channels);

        for (std::size_t block = 0; block < blockCount; ++block)
        {
            const std::uint8_t* base = bytes.data() + block * blockAlign;
            if (channels == 1u)
            {
                ChannelState state = ReadHeader(base);
                output.push_back(static_cast<std::int16_t>(state.predictor));
                const std::uint8_t* data = base + 4u;
                const std::size_t dataBytes = blockAlign - 4u;
                std::size_t frame = 1u;
                for (std::size_t i = 0u; i < dataBytes && frame < samplesPerBlock; ++i)
                {
                    const std::uint8_t byte = data[i];
                    output.push_back(DecodeNibble(state, static_cast<std::uint8_t>(byte & 0x0Fu)));
                    ++frame;
                    if (frame < samplesPerBlock)
                    {
                        output.push_back(DecodeNibble(state, static_cast<std::uint8_t>(byte >> 4)));
                        ++frame;
                    }
                }
            }
            else
            {
                ChannelState left = ReadHeader(base);
                ChannelState right = ReadHeader(base + 4u);
                output.push_back(static_cast<std::int16_t>(left.predictor));
                output.push_back(static_cast<std::int16_t>(right.predictor));
                const std::uint8_t* data = base + 8u;
                const std::size_t dataBytes = blockAlign - 8u;
                std::size_t frame = 1u;
                for (std::size_t group = 0u; group + 8u <= dataBytes && frame < samplesPerBlock; group += 8u)
                {
                    std::array<std::int16_t, 8> leftSamples{};
                    std::array<std::int16_t, 8> rightSamples{};
                    for (std::size_t i = 0u; i < 4u; ++i)
                    {
                        const std::uint8_t byte = data[group + i];
                        leftSamples[i * 2u] = DecodeNibble(left, static_cast<std::uint8_t>(byte & 0x0Fu));
                        leftSamples[i * 2u + 1u] = DecodeNibble(left, static_cast<std::uint8_t>(byte >> 4));
                    }
                    for (std::size_t i = 0u; i < 4u; ++i)
                    {
                        const std::uint8_t byte = data[group + 4u + i];
                        rightSamples[i * 2u] = DecodeNibble(right, static_cast<std::uint8_t>(byte & 0x0Fu));
                        rightSamples[i * 2u + 1u] = DecodeNibble(right, static_cast<std::uint8_t>(byte >> 4));
                    }
                    for (std::size_t i = 0u; i < 8u && frame < samplesPerBlock; ++i, ++frame)
                    {
                        output.push_back(leftSamples[i]);
                        output.push_back(rightSamples[i]);
                    }
                }
            }
        }
        return output;
    }
}
