// SPDX-License-Identifier: MS-PL
#include "CNA/Internal/Audio/EncodedWaveBankDecoder.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <utility>

namespace CNA::Internal::Audio
{
    EncodedWaveBankDecoder::EncodedWaveBankDecoder(EncodedWaveBankCodec codec, std::uint32_t rate,
        std::uint8_t channels, std::vector<std::int16_t> pcm)
        : codec_(codec), rate_(rate), channels_(channels), pcm_(std::move(pcm)) {}

    EncodedWaveBankDecoder::~EncodedWaveBankDecoder() = default;
    EncodedWaveBankCodec EncodedWaveBankDecoder::Codec() const noexcept { return codec_; }
    std::uint32_t EncodedWaveBankDecoder::SampleRate() const noexcept { return rate_; }
    std::uint8_t EncodedWaveBankDecoder::Channels() const noexcept { return channels_; }
    std::uint64_t EncodedWaveBankDecoder::FrameCount() const noexcept { return pcm_.size() / channels_; }

    std::size_t EncodedWaveBankDecoder::ReadFrames(std::span<std::int16_t> output)
    {
        if (output.size() % channels_ != 0)
            throw std::invalid_argument("WaveBank output is not a whole number of native frames");
        const auto count = static_cast<std::size_t>(std::min<std::uint64_t>(
            output.size() / channels_, FrameCount() - position_));
        std::copy_n(pcm_.data() + position_ * channels_, count * channels_, output.data());
        position_ += count;
        return count;
    }

    void EncodedWaveBankDecoder::SeekFrame(std::uint64_t frame)
    {
        if (frame > FrameCount()) throw std::out_of_range("WaveBank seek frame exceeds decoded length");
        position_ = frame;
    }

    std::vector<std::uint8_t> DownmixEncodedWaveBank51(std::span<const std::int16_t> pcm)
    {
        if (pcm.size() % 6 != 0 || pcm.size_bytes() > EncodedWaveBankByteLimit)
            throw std::runtime_error("WMA Pro PCM does not contain bounded whole 5.1 frames");
        // Owner policy GAP-05 A2: FAudio's measured XAudio2 default, destination-major.
        // FAudio src/matrix_defaults.inl:159-160; installed by FAudio.c:1384-1387.
        constexpr float matrix[2][6] = {
            {0.294545442f, 0.000000000f, 0.208181813f, 0.090909094f, 0.251818180f, 0.154545456f},
            {0.000000000f, 0.294545442f, 0.208181813f, 0.090909094f, 0.154545456f, 0.251818180f}
        };
        const auto frames = pcm.size() / 6;
        std::vector<std::uint8_t> stereo(frames * 4);
        for (std::size_t frame = 0; frame < frames; ++frame)
            for (std::size_t destination = 0; destination < 2; ++destination)
            {
                float sample = 0;
                for (std::size_t channel = 0; channel < 6; ++channel)
                    sample += static_cast<float>(pcm[frame * 6 + channel]) * matrix[destination][channel];
                const auto clipped = std::clamp(std::lround(sample), -32768L, 32767L);
                const auto word = static_cast<std::uint16_t>(clipped);
                stereo[frame * 4 + destination * 2] = static_cast<std::uint8_t>(word);
                stereo[frame * 4 + destination * 2 + 1] = static_cast<std::uint8_t>(word >> 8);
            }
        return stereo;
    }
}
