// SPDX-License-Identifier: MS-PL
#include "CNA/Internal/Audio/MsAdpcmEncoder.hpp"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <stdexcept>
#include <utility>

namespace CNA::Internal::Audio
{
    namespace
    {
        /** @brief The step the delta is scaled by after each nibble. */
        constexpr std::array<int, 16> AdaptationTable = {230, 230, 230, 230, 307, 409, 512, 614,
                                                         768, 614, 512, 409, 307, 230, 230, 230};

        constexpr std::array<std::pair<std::int16_t, std::int16_t>, 7> Coefficients = {
            std::pair<std::int16_t, std::int16_t>{256, 0},   std::pair<std::int16_t, std::int16_t>{512, -256},
            std::pair<std::int16_t, std::int16_t>{0, 0},     std::pair<std::int16_t, std::int16_t>{192, 64},
            std::pair<std::int16_t, std::int16_t>{240, 0},   std::pair<std::int16_t, std::int16_t>{460, -208},
            std::pair<std::int16_t, std::int16_t>{392, -232}};

        [[nodiscard]] std::int16_t Clamp16(const int value) noexcept
        {
            return static_cast<std::int16_t>(std::clamp(value, -32768, 32767));
        }

        void AppendInt16(std::vector<std::uint8_t>& bytes, const int value)
        {
            const auto word = static_cast<std::uint16_t>(static_cast<std::int16_t>(value));
            bytes.push_back(static_cast<std::uint8_t>(word & 0xFFu));
            bytes.push_back(static_cast<std::uint8_t>((word >> 8) & 0xFFu));
        }

        /** @brief One channel's running encoder state within a block. */
        struct ChannelState
        {
            int coefficient = 0;
            int delta = 16;
            std::int16_t sample1 = 0;
            std::int16_t sample2 = 0;
        };

        /**
         * @brief Encodes one frame of one channel, answering the nibble and advancing the state.
         *
         * @param state The channel's state.
         * @param sample The frame to encode.
         * @return The nibble, in the low four bits.
         */
        [[nodiscard]] std::uint8_t EncodeSample(ChannelState& state, const std::int16_t sample)
        {
            const auto [coef1, coef2] = Coefficients[static_cast<std::size_t>(state.coefficient)];
            const int predicted = (state.sample1 * coef1 + state.sample2 * coef2) / 256;
            int error = sample - predicted;
            int nibble = error / state.delta;
            // The rounding is to nearest, which is what keeps the error from drifting one way.
            const int remainder = error - nibble * state.delta;
            if (remainder > state.delta / 2)
            {
                ++nibble;
            }
            else if (remainder < -state.delta / 2)
            {
                --nibble;
            }
            nibble = std::clamp(nibble, -8, 7);
            const std::int16_t reconstructed = Clamp16(predicted + nibble * state.delta);
            state.sample2 = state.sample1;
            state.sample1 = reconstructed;
            state.delta = std::max(16, AdaptationTable[static_cast<std::size_t>(nibble & 0xF)] * state.delta / 256);
            return static_cast<std::uint8_t>(nibble & 0xF);
        }

        /**
         * @brief The delta a block starts with: a quarter of the average step the predictor has to
         *        cover, which is the range one nibble reaches comfortably.
         *
         * A block that starts at the smallest delta cannot follow a loud signal until the
         * adaptation catches up, and every block boundary then rings; choosing the delta from the
         * block's own content is what keeps the error flat across boundaries.
         */
        [[nodiscard]] int InitialDelta(const std::span<const std::int16_t> samples,
                                       const std::uint16_t channels, const std::size_t channel,
                                       const std::size_t frames, const int coefficient)
        {
            const auto [coef1, coef2] = Coefficients[static_cast<std::size_t>(coefficient)];
            long long total = 0;
            std::size_t counted = 0;
            std::int16_t sample1 = samples[static_cast<std::size_t>(channels) + channel];
            std::int16_t sample2 = samples[channel];
            for (std::size_t frame = 2; frame < frames; ++frame)
            {
                const std::int16_t sample = samples[frame * channels + channel];
                const int predicted = (sample1 * coef1 + sample2 * coef2) / 256;
                total += std::abs(sample - predicted);
                ++counted;
                sample2 = sample1;
                sample1 = sample;
            }
            if (counted == 0u)
            {
                return 16;
            }
            return std::clamp(static_cast<int>(total / static_cast<long long>(counted) / 4), 16, 16384);
        }

        /** @brief The squared error one predictor choice costs over a block, for the search. */
        [[nodiscard]] long long BlockError(const std::span<const std::int16_t> samples,
                                           const std::uint16_t channels, const std::size_t channel,
                                           const std::size_t frames, const int coefficient,
                                           const int initialDelta)
        {
            ChannelState state;
            state.coefficient = coefficient;
            state.delta = initialDelta;
            state.sample2 = samples[channel];
            state.sample1 = samples[static_cast<std::size_t>(channels) + channel];
            long long error = 0;
            for (std::size_t frame = 2; frame < frames; ++frame)
            {
                const std::int16_t sample = samples[frame * channels + channel];
                (void)EncodeSample(state, sample);
                const long long difference = static_cast<long long>(sample) - state.sample1;
                error += difference * difference;
            }
            return error;
        }
    }

    std::span<const std::pair<std::int16_t, std::int16_t>> MsAdpcmCoefficients() noexcept
    {
        return {Coefficients.data(), Coefficients.size()};
    }

    std::vector<std::uint8_t> MsAdpcmFormatExtension(const std::uint16_t samplesPerBlock)
    {
        std::vector<std::uint8_t> extension;
        AppendInt16(extension, samplesPerBlock);
        AppendInt16(extension, static_cast<int>(Coefficients.size()));
        for (const auto& [first, second] : Coefficients)
        {
            AppendInt16(extension, first);
            AppendInt16(extension, second);
        }
        return extension;
    }

    EncodedMsAdpcm EncodeMsAdpcm(const std::span<const std::int16_t> samples, const std::uint16_t channels,
                                 const std::uint16_t samplesPerBlock)
    {
        if (channels != 1u && channels != 2u)
        {
            throw std::invalid_argument("EncodeMsAdpcm: only one or two channels can be encoded.");
        }
        if (samplesPerBlock < 4u)
        {
            throw std::invalid_argument("EncodeMsAdpcm: a block must carry at least four frames.");
        }
        EncodedMsAdpcm encoded;
        encoded.samplesPerBlock = samplesPerBlock;
        encoded.blockAlign = static_cast<std::uint16_t>(7 * channels + (samplesPerBlock - 2) * channels / 2);
        const std::size_t frames = samples.size() / channels;
        if (frames == 0u)
        {
            return encoded;
        }
        const std::size_t blocks = (frames + samplesPerBlock - 1u) / samplesPerBlock;
        encoded.frameCount = static_cast<std::uint32_t>(blocks * samplesPerBlock);
        // The tail is padded with its own last frame, so every block is whole and a decoder reads
        // silence-free audio to the end.
        std::vector<std::int16_t> padded(static_cast<std::size_t>(encoded.frameCount) * channels);
        for (std::size_t frame = 0; frame < padded.size() / channels; ++frame)
        {
            const std::size_t source = std::min(frame, frames - 1u);
            for (std::size_t channel = 0; channel < channels; ++channel)
            {
                padded[frame * channels + channel] = samples[source * channels + channel];
            }
        }
        encoded.bytes.reserve(blocks * encoded.blockAlign);
        for (std::size_t block = 0; block < blocks; ++block)
        {
            const std::span<const std::int16_t> window(
                padded.data() + block * samplesPerBlock * channels,
                static_cast<std::size_t>(samplesPerBlock) * channels);
            std::vector<ChannelState> states(channels);
            for (std::size_t channel = 0; channel < channels; ++channel)
            {
                // The predictor is chosen per block and per channel by trying all seven, which is
                // what keeps a quiet block from being encoded with a loud one's coefficients.
                int best = 0;
                int bestDelta = 16;
                long long bestError = -1;
                for (int coefficient = 0; coefficient < static_cast<int>(Coefficients.size()); ++coefficient)
                {
                    const int estimate = InitialDelta(window, channels, channel, samplesPerBlock, coefficient);
                    // The estimate is a starting point, not an answer: a few scalings of it are
                    // tried and the one that reconstructs the block closest is kept.
                    for (const int numerator : {2, 3, 4, 6, 8})
                    {
                        const int delta = std::clamp(estimate * 4 / numerator, 16, 16384);
                        const long long error =
                            BlockError(window, channels, channel, samplesPerBlock, coefficient, delta);
                        if (bestError < 0 || error < bestError)
                        {
                            bestError = error;
                            best = coefficient;
                            bestDelta = delta;
                        }
                    }
                }
                states[channel].coefficient = best;
                states[channel].sample2 = window[channel];
                states[channel].sample1 = window[static_cast<std::size_t>(channels) + channel];
                states[channel].delta = bestDelta;
            }
            for (std::size_t channel = 0; channel < channels; ++channel)
            {
                encoded.bytes.push_back(static_cast<std::uint8_t>(states[channel].coefficient));
            }
            for (std::size_t channel = 0; channel < channels; ++channel)
            {
                AppendInt16(encoded.bytes, states[channel].delta);
            }
            for (std::size_t channel = 0; channel < channels; ++channel)
            {
                AppendInt16(encoded.bytes, states[channel].sample1);
            }
            for (std::size_t channel = 0; channel < channels; ++channel)
            {
                AppendInt16(encoded.bytes, states[channel].sample2);
            }
            for (std::size_t frame = 2; frame < samplesPerBlock; frame += (channels == 1u ? 2u : 1u))
            {
                std::uint8_t packed = 0u;
                if (channels == 1u)
                {
                    const std::uint8_t high = EncodeSample(states[0], window[frame]);
                    const std::uint8_t low =
                        frame + 1u < samplesPerBlock ? EncodeSample(states[0], window[frame + 1u]) : 0u;
                    packed = static_cast<std::uint8_t>((high << 4) | low);
                }
                else
                {
                    const std::uint8_t left = EncodeSample(states[0], window[frame * 2u]);
                    const std::uint8_t right = EncodeSample(states[1], window[frame * 2u + 1u]);
                    packed = static_cast<std::uint8_t>((left << 4) | right);
                }
                encoded.bytes.push_back(packed);
            }
        }
        return encoded;
    }
}
