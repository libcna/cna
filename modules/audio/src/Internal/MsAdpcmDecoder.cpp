// SPDX-License-Identifier: MS-PL
#include "CNA/Internal/Audio/MsAdpcmDecoder.hpp"

#include "CNA/Internal/Audio/MsAdpcmEncoder.hpp"

#include <algorithm>
#include <cstring>
#include <stdexcept>

namespace CNA::Internal::Audio
{
    namespace
    {
        [[nodiscard]] std::int16_t Clamp16(const int value) noexcept
        {
            return static_cast<std::int16_t>(std::clamp(value, -32768, 32767));
        }

        [[nodiscard]] std::int16_t ReadInt16Le(const std::uint8_t* at) noexcept
        {
            std::int16_t value = 0;
            std::memcpy(&value, at, sizeof(value));
            return value;
        }

        struct ChannelState
        {
            int coefficient = 0;
            int delta = 16;
            std::int16_t sample1 = 0;
            std::int16_t sample2 = 0;
        };

        // The exact reverse of MsAdpcmEncoder.cpp's EncodeSample: same predictor, same adaptation.
        [[nodiscard]] std::int16_t DecodeNibble(
            ChannelState& state, const std::uint8_t nibble,
            const std::span<const std::pair<std::int16_t, std::int16_t>> coefficients,
            const std::span<const int> adaptation)
        {
            const auto [coef1, coef2] = coefficients[static_cast<std::size_t>(state.coefficient)];
            const int predicted = (state.sample1 * coef1 + state.sample2 * coef2) / 256;
            const int signedNibble = nibble >= 8u ? static_cast<int>(nibble) - 16 : static_cast<int>(nibble);
            const std::int16_t reconstructed = Clamp16(predicted + signedNibble * state.delta);
            state.sample2 = state.sample1;
            state.sample1 = reconstructed;
            state.delta = std::max(16, adaptation[nibble] * state.delta / 256);
            return reconstructed;
        }
    }

    std::vector<std::int16_t> DecodeMsAdpcm(
        const std::span<const std::uint8_t> bytes, const std::uint16_t channels,
        const std::uint16_t blockAlign, const std::uint16_t samplesPerBlock)
    {
        if (channels != 1u && channels != 2u)
        {
            throw std::invalid_argument("DecodeMsAdpcm: only one or two channels can be decoded.");
        }
        if (samplesPerBlock < 2u)
        {
            throw std::invalid_argument("DecodeMsAdpcm: a block must carry at least two frames.");
        }
        const std::size_t headerBytes = 7u * channels;
        if (blockAlign < headerBytes)
        {
            throw std::invalid_argument("DecodeMsAdpcm: blockAlign is too small for its own header.");
        }
        if (blockAlign == 0u || bytes.size() % blockAlign != 0u)
        {
            throw std::invalid_argument(
                "DecodeMsAdpcm: byte length is not a whole number of blocks.");
        }

        const auto coefficients = MsAdpcmCoefficients();
        const auto adaptation = MsAdpcmAdaptationTable();
        const std::size_t blockCount = bytes.size() / blockAlign;
        std::vector<std::int16_t> output;
        output.reserve(blockCount * samplesPerBlock * channels);

        for (std::size_t block = 0; block < blockCount; ++block)
        {
            const std::uint8_t* base = bytes.data() + block * blockAlign;
            std::vector<ChannelState> states(channels);
            for (std::uint16_t channel = 0; channel < channels; ++channel)
            {
                const std::uint8_t coefficientIndex = base[channel];
                if (coefficientIndex >= coefficients.size())
                {
                    throw std::runtime_error(
                        "DecodeMsAdpcm: block names a predictor coefficient index out of range.");
                }
                states[channel].coefficient = coefficientIndex;
            }
            const std::uint8_t* deltas = base + channels;
            const std::uint8_t* sample1s = deltas + 2u * channels;
            const std::uint8_t* sample2s = sample1s + 2u * channels;
            for (std::uint16_t channel = 0; channel < channels; ++channel)
            {
                states[channel].delta = ReadInt16Le(deltas + 2u * channel);
                states[channel].sample1 = ReadInt16Le(sample1s + 2u * channel);
                states[channel].sample2 = ReadInt16Le(sample2s + 2u * channel);
            }

            // Frames 0 and 1 are the header's uncompressed sample2/sample1, in that order -- the
            // same order EncodeMsAdpcm seeded them from the source window.
            output.resize(output.size() + static_cast<std::size_t>(channels));
            for (std::uint16_t channel = 0; channel < channels; ++channel)
            {
                output[output.size() - channels + channel] = states[channel].sample2;
            }
            output.resize(output.size() + static_cast<std::size_t>(channels));
            for (std::uint16_t channel = 0; channel < channels; ++channel)
            {
                output[output.size() - channels + channel] = states[channel].sample1;
            }

            const std::uint8_t* nibbles = sample2s + 2u * channels;
            std::size_t nibbleByteIndex = 0u;
            if (channels == 1u)
            {
                for (std::size_t frame = 2u; frame < samplesPerBlock; frame += 2u)
                {
                    const std::uint8_t packed = nibbles[nibbleByteIndex++];
                    output.push_back(DecodeNibble(
                        states[0], static_cast<std::uint8_t>(packed >> 4), coefficients, adaptation));
                    if (frame + 1u < samplesPerBlock)
                    {
                        output.push_back(DecodeNibble(
                            states[0], static_cast<std::uint8_t>(packed & 0x0Fu), coefficients, adaptation));
                    }
                }
            }
            else
            {
                for (std::size_t frame = 2u; frame < samplesPerBlock; ++frame)
                {
                    const std::uint8_t packed = nibbles[nibbleByteIndex++];
                    const std::int16_t left = DecodeNibble(
                        states[0], static_cast<std::uint8_t>(packed >> 4), coefficients, adaptation);
                    const std::int16_t right = DecodeNibble(
                        states[1], static_cast<std::uint8_t>(packed & 0x0Fu), coefficients, adaptation);
                    output.push_back(left);
                    output.push_back(right);
                }
            }
        }
        return output;
    }
}
