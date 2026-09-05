// SPDX-License-Identifier: MS-PL
#include "Microsoft/Xna/Framework/Content/Pipeline/Audio/AudioContent.hpp"

#include <exception>
#include <filesystem>

#include <algorithm>

#include "CNA/Content/Cnb/CnbSourceImport.hpp"
#include "CNA/Internal/Audio/MsAdpcmEncoder.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/InvalidContentException.hpp"

namespace Microsoft::Xna::Framework::Content::Pipeline::Audio
{
    namespace
    {
        namespace Import = CNA::Content::Import;

        /** @brief The `WAVEFORMATEX` tag an imported encoding is written under. */
        [[nodiscard]] SharpRuntime::intcs FormatTagOf(const Import::ImportedPcmEncoding encoding)
        {
            // Three is IEEE float; everything else here is integer PCM.
            return encoding == Import::ImportedPcmEncoding::Float32LittleEndian ? 3 : 1;
        }

        /** @brief The message XNA gives for a file it cannot read as audio. */
        [[nodiscard]] std::string OpenFailure(const std::string& fileName)
        {
            std::string name;
            try
            {
                name = std::filesystem::path(fileName).filename().string();
            }
            catch (const std::exception&)
            {
                name = fileName;
            }
            return "Failed to open file " + name +
                   ". Ensure the file is a valid audio file and is not DRM protected.";
        }
    }

    AudioContent::AudioContent(const std::string& audioFileName, const AudioFileType audioFileType)
        : fileName_(audioFileName), fileType_(audioFileType)
    {
        Import::ImportedSound imported;
        try
        {
            if (audioFileName.empty() || audioFileType != AudioFileType::Wav)
            {
                // Only the WAVE reader is available here; a file of any other named type is
                // refused with the same message XNA gives an unreadable one (measured,
                // refusals/wrong_file_type).
                throw std::runtime_error("unsupported");
            }
            imported = CNA::Content::Cnb::ImportWavAsImportedSound(audioFileName);
        }
        catch (const std::exception&)
        {
            throw InvalidContentException(OpenFailure(audioFileName));
        }
        const auto sampleBytes =
            static_cast<SharpRuntime::intcs>(Import::ImportedPcmSampleBytes(imported.encoding));
        const auto channels = static_cast<SharpRuntime::intcs>(imported.channels);
        const auto sampleRate = static_cast<SharpRuntime::intcs>(imported.sampleRate);
        const SharpRuntime::intcs blockAlign = sampleBytes * channels;
        format_ = std::make_shared<AudioFormat>(FormatTagOf(imported.encoding), channels, sampleRate,
                                                sampleRate * blockAlign, blockAlign, sampleBytes * 8);
        data_ = std::move(imported.samples);
        loopStart_ = static_cast<SharpRuntime::intcs>(imported.loopStart);
        // A file with no loop region of its own answers the whole sound, which is what XNA does
        // (measured, audiocontent/mono_pcm16 answers 800 frames for an 800-frame file).
        loopLength_ = imported.loopLength > 0u ? static_cast<SharpRuntime::intcs>(imported.loopLength)
                                               : static_cast<SharpRuntime::intcs>(imported.frameCount);
        // The duration is whole milliseconds, and the remainder is dropped: 13228 bytes at
        // 132300 bytes a second answers 99, not 100 (measured, convert/pcm_medium).
        const SharpRuntime::intcs bytesPerSecond = format_->getAverageBytesPerSecondProperty();
        const auto milliseconds =
            bytesPerSecond <= 0
                ? SharpRuntime::longcs{0}
                : static_cast<SharpRuntime::longcs>(data_.size()) * 1000 / bytesPerSecond;
        duration_ = System::TimeSpan::FromTicks(milliseconds * 10000);
    }

    AudioContent::~AudioContent() { data_.clear(); }

    const std::vector<SharpRuntime::bytecs>& AudioContent::getDataProperty() const
    {
        RequireNotDisposed("Data");
        return data_;
    }

    System::TimeSpan AudioContent::getDurationProperty() const noexcept { return duration_; }

    const std::string& AudioContent::getFileNameProperty() const noexcept { return fileName_; }

    AudioFileType AudioContent::getFileTypeProperty() const noexcept { return fileType_; }

    const std::shared_ptr<AudioFormat>& AudioContent::getFormatProperty() const noexcept { return format_; }

    SharpRuntime::intcs AudioContent::getLoopLengthProperty() const noexcept { return loopLength_; }

    SharpRuntime::intcs AudioContent::getLoopStartProperty() const noexcept { return loopStart_; }

    void AudioContent::ConvertFormat(const ConversionFormat formatType, const ConversionQuality quality,
                                     const std::string& targetFileName)
    {
        RequireNotDisposed("ConvertFormat");
        (void)targetFileName;
        if (formatType == ConversionFormat::WindowsMedia || formatType == ConversionFormat::Xma)
        {
            // Neither encoder can be reached from here: Windows Media is a Windows codec and XMA
            // an Xbox 360 one, and neither could even be measured (see the audio oracle).
            throw InvalidContentException(
                std::string("AudioContent::ConvertFormat cannot produce ") +
                (formatType == ConversionFormat::WindowsMedia ? "Windows Media" : "XMA") +
                " audio: that encoder is not available outside the platform that owns it.");
        }
        if (format_ == nullptr)
        {
            return;
        }
        const SharpRuntime::intcs channels = format_->getChannelCountProperty();
        const SharpRuntime::intcs sourceRate = format_->getSampleRateProperty();
        const SharpRuntime::intcs sourceBits = format_->getBitsPerSampleProperty();
        if (channels <= 0 || sourceRate <= 0 || sourceBits <= 0)
        {
            return;
        }
        if (formatType == ConversionFormat::Pcm)
        {
            // The best quality keeps the source exactly, bit depth included: an eight-bit source
            // stays eight-bit and its bytes are untouched (measured, convert/pcm_best and
            // convert/pcm_from_8bit).
            const SharpRuntime::intcs targetRate = TargetRate(sourceRate, quality);
            if (targetRate == sourceRate)
            {
                return;
            }
            Resample(targetRate);
            return;
        }
        EncodeAdpcm();
    }

    SharpRuntime::intcs AudioContent::TargetRate(const SharpRuntime::intcs sourceRate,
                                                 const ConversionQuality quality)
    {
        // Half the rate at the lowest quality and three quarters in the middle (measured,
        // convert/pcm_low and convert/pcm_medium answer 22050 and 33075 for a 44100 source).
        switch (quality)
        {
            case ConversionQuality::Low: return sourceRate / 2;
            case ConversionQuality::Medium: return sourceRate * 3 / 4;
            case ConversionQuality::Best: break;
        }
        return sourceRate;
    }

    void AudioContent::Resample(const SharpRuntime::intcs targetRate)
    {
        const SharpRuntime::intcs channels = format_->getChannelCountProperty();
        const SharpRuntime::intcs sourceRate = format_->getSampleRateProperty();
        const SharpRuntime::intcs bytesPerSample = format_->getBitsPerSampleProperty() / 8;
        const SharpRuntime::intcs blockAlign = bytesPerSample * channels;
        if (blockAlign <= 0)
        {
            return;
        }
        const auto sourceFrames = static_cast<SharpRuntime::intcs>(data_.size()) / blockAlign;
        const auto targetFrames = static_cast<SharpRuntime::intcs>(
            static_cast<SharpRuntime::longcs>(sourceFrames) * targetRate / sourceRate);
        std::vector<SharpRuntime::bytecs> resampled(static_cast<std::size_t>(targetFrames) *
                                                    static_cast<std::size_t>(blockAlign));
        // The sample values are this host's own: XNA's resampler is inside its native helper, and
        // only the shape of its answer -- the rate, the depth and the frame count -- is measured.
        for (SharpRuntime::intcs frame = 0; frame < targetFrames; ++frame)
        {
            const auto source = static_cast<SharpRuntime::intcs>(
                static_cast<SharpRuntime::longcs>(frame) * sourceRate / targetRate);
            const auto from = static_cast<std::size_t>(std::min(source, sourceFrames - 1)) *
                              static_cast<std::size_t>(blockAlign);
            std::copy_n(data_.begin() + static_cast<std::ptrdiff_t>(from), static_cast<std::size_t>(blockAlign),
                        resampled.begin() + static_cast<std::ptrdiff_t>(
                                                static_cast<std::size_t>(frame) *
                                                static_cast<std::size_t>(blockAlign)));
        }
        data_ = std::move(resampled);
        format_ = std::make_shared<AudioFormat>(format_->getFormatProperty(), channels, targetRate,
                                                targetRate * blockAlign, blockAlign,
                                                format_->getBitsPerSampleProperty());
        // The loop comes back one frame short of the data, which is what XNA answers for a
        // resampled sound (measured, convert/pcm_low: 2205 frames and a loop of 2204).
        loopStart_ = 0;
        loopLength_ = targetFrames > 0 ? targetFrames - 1 : 0;
        RecomputeDuration();
    }

    void AudioContent::EncodeAdpcm()
    {
        const auto channels = static_cast<std::uint16_t>(format_->getChannelCountProperty());
        const SharpRuntime::intcs sourceRate = format_->getSampleRateProperty();
        std::vector<std::int16_t> samples = SamplesAsPcm16();
        constexpr std::uint16_t samplesPerBlock = 128u;
        const CNA::Internal::Audio::EncodedMsAdpcm encoded =
            CNA::Internal::Audio::EncodeMsAdpcm(samples, channels, samplesPerBlock);
        data_.assign(encoded.bytes.begin(), encoded.bytes.end());
        const auto blockAlign = static_cast<SharpRuntime::intcs>(encoded.blockAlign);
        const SharpRuntime::intcs bytesPerSecond =
            blockAlign <= 0 ? 0
                            : static_cast<SharpRuntime::intcs>(
                                  static_cast<SharpRuntime::longcs>(sourceRate) * blockAlign / samplesPerBlock);
        format_ = std::make_shared<AudioFormat>(2, channels, sourceRate, bytesPerSecond, blockAlign, 4,
                                                CNA::Internal::Audio::MsAdpcmFormatExtension(samplesPerBlock));
        loopStart_ = 0;
        loopLength_ = static_cast<SharpRuntime::intcs>(encoded.frameCount);
        RecomputeDuration();
    }

    std::vector<std::int16_t> AudioContent::SamplesAsPcm16() const
    {
        const SharpRuntime::intcs bits = format_->getBitsPerSampleProperty();
        std::vector<std::int16_t> samples;
        if (bits == 8)
        {
            // Eight-bit WAVE samples are unsigned around 128; sixteen-bit ones are signed.
            samples.reserve(data_.size());
            for (const SharpRuntime::bytecs value : data_)
            {
                samples.push_back(static_cast<std::int16_t>((static_cast<int>(value) - 128) * 256));
            }
            return samples;
        }
        samples.reserve(data_.size() / 2);
        for (std::size_t i = 0; i + 1 < data_.size(); i += 2)
        {
            samples.push_back(static_cast<std::int16_t>(static_cast<std::uint16_t>(data_[i]) |
                                                        static_cast<std::uint16_t>(data_[i + 1] << 8)));
        }
        return samples;
    }

    void AudioContent::RecomputeDuration()
    {
        const SharpRuntime::intcs bytesPerSecond = format_->getAverageBytesPerSecondProperty();
        const auto milliseconds =
            bytesPerSecond <= 0 ? SharpRuntime::longcs{0}
                                : static_cast<SharpRuntime::longcs>(data_.size()) * 1000 / bytesPerSecond;
        duration_ = System::TimeSpan::FromTicks(milliseconds * 10000);
    }

    void AudioContent::Dispose()
    {
        // Disposing twice does nothing, and the format, the file name and the duration keep
        // answering afterwards; only the samples go (measured, refusals/after_dispose).
        disposed_ = true;
        data_.clear();
        data_.shrink_to_fit();
    }

    void AudioContent::RequireNotDisposed(const std::string& member) const
    {
        if (disposed_)
        {
            throw InvalidContentException("AudioContent." + member +
                                          " cannot be used after the content has been disposed.");
        }
    }

    const std::string& AudioContent::GetTypeName() const
    {
        static const std::string name(XnaTypeName);
        return name;
    }
}
