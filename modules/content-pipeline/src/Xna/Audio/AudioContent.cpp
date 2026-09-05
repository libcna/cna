// SPDX-License-Identifier: MS-PL
#include "Microsoft/Xna/Framework/Content/Pipeline/Audio/AudioContent.hpp"

#include <exception>
#include <filesystem>

#include "CNA/Content/Cnb/CnbSourceImport.hpp"
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
        (void)formatType;
        (void)quality;
        (void)targetFileName;
        throw InvalidContentException(
            "AudioContent::ConvertFormat is not available in this build (plans/"
            "plan_xnapipeline_parity.md XNAPP-161).");
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
