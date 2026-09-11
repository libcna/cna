// SPDX-License-Identifier: MS-PL
#include "Microsoft/Xna/Framework/Content/Pipeline/Audio/AudioFormat.hpp"

namespace Microsoft::Xna::Framework::Content::Pipeline::Audio
{
    namespace
    {
        /** @brief Appends a little-endian sixteen-bit field, as a `WAVEFORMATEX` carries it. */
        void AppendUInt16(std::vector<SharpRuntime::bytecs>& bytes, const SharpRuntime::intcs value)
        {
            const auto word = static_cast<std::uint32_t>(value);
            bytes.push_back(static_cast<SharpRuntime::bytecs>(word & 0xFFu));
            bytes.push_back(static_cast<SharpRuntime::bytecs>((word >> 8) & 0xFFu));
        }

        /** @brief Appends a little-endian thirty-two-bit field. */
        void AppendUInt32(std::vector<SharpRuntime::bytecs>& bytes, const SharpRuntime::intcs value)
        {
            const auto word = static_cast<std::uint32_t>(value);
            for (int shift = 0; shift < 32; shift += 8)
            {
                bytes.push_back(static_cast<SharpRuntime::bytecs>((word >> shift) & 0xFFu));
            }
        }
    }

    AudioFormat::AudioFormat(const SharpRuntime::intcs format, const SharpRuntime::intcs channelCount,
                             const SharpRuntime::intcs sampleRate,
                             const SharpRuntime::intcs averageBytesPerSecond,
                             const SharpRuntime::intcs blockAlign, const SharpRuntime::intcs bitsPerSample,
                             std::vector<SharpRuntime::bytecs> extension)
        : format_(format), channelCount_(channelCount), sampleRate_(sampleRate),
          averageBytesPerSecond_(averageBytesPerSecond), blockAlign_(blockAlign),
          bitsPerSample_(bitsPerSample)
    {
        // The eighteen bytes XNA answers are a WAVEFORMATEX with its cbSize, even for PCM, where
        // the extension is empty (measured, audiocontent/mono_pcm16).
        AppendUInt16(native_, format_);
        AppendUInt16(native_, channelCount_);
        AppendUInt32(native_, sampleRate_);
        AppendUInt32(native_, averageBytesPerSecond_);
        AppendUInt16(native_, blockAlign_);
        AppendUInt16(native_, bitsPerSample_);
        AppendUInt16(native_, static_cast<SharpRuntime::intcs>(extension.size()));
        native_.insert(native_.end(), extension.begin(), extension.end());
    }

    SharpRuntime::intcs AudioFormat::getAverageBytesPerSecondProperty() const noexcept
    {
        return averageBytesPerSecond_;
    }

    SharpRuntime::intcs AudioFormat::getBitsPerSampleProperty() const noexcept { return bitsPerSample_; }

    SharpRuntime::intcs AudioFormat::getBlockAlignProperty() const noexcept { return blockAlign_; }

    SharpRuntime::intcs AudioFormat::getChannelCountProperty() const noexcept { return channelCount_; }

    SharpRuntime::intcs AudioFormat::getFormatProperty() const noexcept { return format_; }

    const std::vector<SharpRuntime::bytecs>& AudioFormat::getNativeWaveFormatProperty() const noexcept
    {
        return native_;
    }

    SharpRuntime::intcs AudioFormat::getSampleRateProperty() const noexcept { return sampleRate_; }

    const std::string& AudioFormat::GetTypeName() const
    {
        static const std::string name(XnaTypeName);
        return name;
    }
}
