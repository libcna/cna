// SPDX-License-Identifier: MS-PL
#include "Microsoft/Xna/Framework/Content/Pipeline/Audio/AudioContent.hpp"

#include <exception>
#include <filesystem>

#include <algorithm>
#include <fstream>
#include <iterator>
#include <span>

#include "CNA/Content/Pipeline/BuildTimeMediaDecoder.hpp"
#include "CNA/Internal/Audio/MsAdpcmEncoder.hpp"
#include "CNA/Internal/Audio/WavFormatReader.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/InvalidContentException.hpp"

namespace Microsoft::Xna::Framework::Content::Pipeline::Audio
{
    namespace
    {
        /** @brief The bare file name XNA's messages carry, or the whole path when it has none. */
        [[nodiscard]] std::string FileNameOnly(const std::string& fileName)
        {
            try
            {
                return std::filesystem::path(fileName).filename().string();
            }
            catch (const std::exception&)
            {
                return fileName;
            }
        }

        /** @brief The message XNA gives for a file it cannot read as audio. */
        [[nodiscard]] std::string OpenFailure(const std::string& fileName)
        {
            return "Failed to open file " + FileNameOnly(fileName) +
                   ". Ensure the file is a valid audio file and is not DRM protected.";
        }
    }

    AudioContent::AudioContent(const std::string& audioFileName, const AudioFileType audioFileType)
        : fileName_(audioFileName), fileType_(audioFileType)
    {
        if (audioFileType == AudioFileType::Mp3 || audioFileType == AudioFileType::Wma)
        {
            ReadThroughMediaDecoder(audioFileName);
            return;
        }
        CNA::Internal::Audio::WavFormatAndData source;
        try
        {
            if (audioFileName.empty() || audioFileType != AudioFileType::Wav)
            {
                // Only the WAVE reader is available here; a file of any other named type is
                // refused with the same message XNA gives an unreadable one (measured,
                // refusals/wrong_file_type).
                throw std::runtime_error("unsupported");
            }
            std::ifstream file(audioFileName, std::ios::binary);
            if (!file)
            {
                throw std::runtime_error("unreadable");
            }
            const std::vector<char> bytes((std::istreambuf_iterator<char>(file)),
                                          std::istreambuf_iterator<char>());
            source = CNA::Internal::Audio::ReadWavFormatAndData(
                std::span<const std::uint8_t>(reinterpret_cast<const std::uint8_t*>(bytes.data()),
                                              bytes.size()),
                audioFileName);
        }
        catch (const std::exception&)
        {
            throw InvalidContentException(OpenFailure(audioFileName));
        }
        // The format fields are the file's own, whatever encoding they name: an ADPCM or an
        // extensible source is carried as it is rather than decoded (measured, wav/variants, where
        // the extensible tag even comes back as -2, the sixteen-bit 0xFFFE read as a signed int).
        format_ = std::make_shared<AudioFormat>(
            static_cast<SharpRuntime::intcs>(static_cast<std::int16_t>(source.formatTag)),
            static_cast<SharpRuntime::intcs>(source.channels),
            static_cast<SharpRuntime::intcs>(source.sampleRate),
            static_cast<SharpRuntime::intcs>(source.averageBytesPerSecond),
            static_cast<SharpRuntime::intcs>(source.blockAlign),
            static_cast<SharpRuntime::intcs>(source.bitsPerSample));
        data_ = std::vector<SharpRuntime::bytecs>(source.data.begin(), source.data.end());
        loopStart_ = static_cast<SharpRuntime::intcs>(source.loopStart);
        // A file with no loop region of its own answers its whole length in blocks, which is what
        // XNA answers for every variant measured -- frames for PCM, blocks for ADPCM.
        loopLength_ = source.loopLength > 0u
                          ? static_cast<SharpRuntime::intcs>(source.loopLength)
                          : static_cast<SharpRuntime::intcs>(source.data.size() / source.blockAlign);
        RecomputeDuration();
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

    void AudioContent::ReadThroughMediaDecoder(const std::string& audioFileName)
    {
        namespace Media = CNA::Content::Pipeline::BuildTimeMedia;
        if (!Media::IsAvailable())
        {
            // Not the unreadable-file sentence: this is a build that cannot read the format at
            // all, and telling a user their file is corrupt when the decoder is simply absent
            // sends them to fix the wrong thing.
            throw InvalidContentException("Failed to open file " + FileNameOnly(audioFileName) + ". " +
                                          Media::UnavailableReason());
        }
        Media::DecodedAudio decoded;
        try
        {
            if (audioFileName.empty())
            {
                throw std::runtime_error("empty name");
            }
            // 44100 whatever the source carries: that is what the genuine importer reports for
            // every MPEG version and every source rate from 8000 to 48000, with only the channel
            // count surviving (measured, tests/reference/xna40/media cases mp3/*;
            // docs/xna-content-pipeline-media.md section 2). WMA takes the same rate: it reaches
            // the same SongProcessor, and the genuine importer could not be asked here.
            decoded = Media::DecodeAudio(audioFileName, 44100,
                                         fileType_ == AudioFileType::Mp3
                                             ? Media::AudioSourceFormat::Mpeg
                                             : Media::AudioSourceFormat::WindowsMedia);
        }
        catch (const std::exception&)
        {
            throw InvalidContentException(OpenFailure(audioFileName));
        }
        format_ = std::make_shared<AudioFormat>(
            1, static_cast<SharpRuntime::intcs>(decoded.channels),
            static_cast<SharpRuntime::intcs>(decoded.sampleRate),
            static_cast<SharpRuntime::intcs>(decoded.sampleRate * decoded.channels * 2),
            static_cast<SharpRuntime::intcs>(decoded.channels * 2),
            static_cast<SharpRuntime::intcs>(decoded.bitsPerSample));
        data_ = std::vector<SharpRuntime::bytecs>(decoded.pcm.begin(), decoded.pcm.end());
        // Both are zero, where a WAV that names no loop answers 0 and its whole length
        // (measured, mp3/* answer loopStart=0 loopLength=0).
        loopStart_ = 0;
        loopLength_ = 0;
        RecomputeDuration();
    }

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
