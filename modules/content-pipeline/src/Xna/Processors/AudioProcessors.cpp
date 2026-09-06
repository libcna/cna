// SPDX-License-Identifier: MS-PL
#include "Microsoft/Xna/Framework/Content/Pipeline/Processors/AudioProcessors.hpp"

#include <filesystem>

#include "Microsoft/Xna/Framework/Content/Pipeline/ContentProcessorContext.hpp"

#include "Microsoft/Xna/Framework/Content/Pipeline/InvalidContentException.hpp"
#include "System/ArgumentNullException.hpp"

namespace Microsoft::Xna::Framework::Content::Pipeline::Processors
{
    SoundEffectContent::SoundEffectContent(std::shared_ptr<Audio::AudioFormat> format,
                                           std::vector<SharpRuntime::bytecs> data,
                                           const SharpRuntime::intcs loopStart,
                                           const SharpRuntime::intcs loopLength,
                                           const System::TimeSpan duration)
        : format_(std::move(format)), data_(std::move(data)), loopStart_(loopStart), loopLength_(loopLength),
          duration_(duration)
    {
    }

    const std::shared_ptr<Audio::AudioFormat>& SoundEffectContent::Format() const noexcept { return format_; }

    const std::vector<SharpRuntime::bytecs>& SoundEffectContent::Data() const noexcept { return data_; }

    SharpRuntime::intcs SoundEffectContent::LoopStart() const noexcept { return loopStart_; }

    SharpRuntime::intcs SoundEffectContent::LoopLength() const noexcept { return loopLength_; }

    System::TimeSpan SoundEffectContent::Duration() const noexcept { return duration_; }

    const std::string& SoundEffectContent::GetTypeName() const
    {
        static const std::string name(XnaTypeName);
        return name;
    }

    SongContent::SongContent(std::string fileName, const System::TimeSpan duration)
        : fileName_(std::move(fileName)), duration_(duration)
    {
    }

    const std::string& SongContent::FileName() const noexcept { return fileName_; }

    System::TimeSpan SongContent::Duration() const noexcept { return duration_; }

    const std::string& SongContent::GetTypeName() const
    {
        static const std::string name(XnaTypeName);
        return name;
    }

    Audio::ConversionQuality SoundEffectProcessor::getQualityProperty() const noexcept { return quality_; }

    void SoundEffectProcessor::setQualityProperty(const Audio::ConversionQuality value) noexcept
    {
        quality_ = value;
    }

    std::shared_ptr<SoundEffectContent> SoundEffectProcessor::Process(
        const std::shared_ptr<Audio::AudioContent>& input, ContentProcessorContext& context)
    {
        (void)context;
        if (input == nullptr)
        {
            throw System::ArgumentNullException("input");
        }
        // The best quality leaves the source alone; the two below it compress to ADPCM at that
        // quality (measured, soundeffectprocessor/process_best, _medium and _low, whose answers
        // are the plain conversions of the same names).
        input->ConvertFormat(quality_ == Audio::ConversionQuality::Best ? Audio::ConversionFormat::Pcm
                                                                       : Audio::ConversionFormat::Adpcm,
                             quality_, "");
        return std::make_shared<SoundEffectContent>(input->getFormatProperty(), input->getDataProperty(),
                                                    input->getLoopStartProperty(),
                                                    input->getLoopLengthProperty(),
                                                    input->getDurationProperty());
    }

    const std::string& SoundEffectProcessor::GetTypeName() const
    {
        static const std::string name(XnaTypeName);
        return name;
    }

    Audio::ConversionQuality SongProcessor::getQualityProperty() const noexcept { return quality_; }

    void SongProcessor::setQualityProperty(const Audio::ConversionQuality value) noexcept { quality_ = value; }

    std::shared_ptr<SongContent> SongProcessor::Process(const std::shared_ptr<Audio::AudioContent>& input,
                                                        ContentProcessorContext& context)
    {
        if (input == nullptr)
        {
            throw System::ArgumentNullException("input");
        }
        // A song is a file the runtime streams, not a payload the .xnb carries: the asset names a
        // Windows Media file beside it, and this is where that file is written. XNA's own encoder
        // could not be measured (its Windows Media path never returns under the oracle's Wine
        // prefix), so what is reproduced is the shape -- a .wma beside the output asset, named
        // after it -- and the refusal text, rather than Microsoft's exact bytes.
        const std::filesystem::path output(context.getOutputFilenameProperty());
        std::filesystem::path song = output;
        song.replace_extension(".wma");
        std::error_code error;
        if (!song.parent_path().empty())
        {
            std::filesystem::create_directories(song.parent_path(), error);
        }
        input->ConvertFormat(Audio::ConversionFormat::WindowsMedia, quality_, song.string());
        context.AddOutputFile(song.string());
        return std::make_shared<SongContent>(song.filename().string(), input->getDurationProperty());
    }

    const std::string& SongProcessor::GetTypeName() const
    {
        static const std::string name(XnaTypeName);
        return name;
    }
}
