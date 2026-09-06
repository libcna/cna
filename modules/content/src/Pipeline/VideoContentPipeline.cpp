// SPDX-License-Identifier: MS-PL

#include "CNA/Content/Pipeline/VideoContentPipeline.hpp"

#include <utility>

#include <cmath>
#include <filesystem>
#include <limits>
#include <stdexcept>

#include "CNA/Content/Cnb/CnbFormat.hpp"
#include "CNA/Content/Cnb/CnbMediaCodec.hpp"
#include "CNA/Internal/ContentPath.hpp"

namespace CNA::Content::Pipeline
{
    namespace
    {
        constexpr const char* kVideoImporterName = "CNA.VideoImporter";
        constexpr const char* kVideoProcessorName = "CNA.VideoProcessor";
        constexpr const char* kVideoWriterName = "CNA.VideoContentWriter";
        constexpr std::uint64_t kMaxMediaDurationMs = 0x7FFFFFFFu;

        const std::string* OptionalString(const ContentProcessorParameters& parameters,
                                          const char* name)
        {
            const ContentProcessorParameterValue* value = parameters.Find(name);
            if (value == nullptr) { return nullptr; }
            const std::string* text = std::get_if<std::string>(value);
            if (text == nullptr)
            {
                throw std::invalid_argument("VideoProcessor parameter '" + std::string(name) +
                                            "' must be a string.");
            }
            return text;
        }

        std::uint64_t UnsignedParameter(const ContentProcessorParameters& parameters,
                                        const char* name, bool required,
                                        std::uint64_t defaultValue = 0u)
        {
            const ContentProcessorParameterValue* value = parameters.Find(name);
            if (value == nullptr)
            {
                if (required)
                {
                    throw std::invalid_argument("VideoProcessor requires u64 parameter '" +
                                                std::string(name) + "'.");
                }
                return defaultValue;
            }
            const std::uint64_t* result = std::get_if<std::uint64_t>(value);
            if (result == nullptr)
            {
                throw std::invalid_argument("VideoProcessor parameter '" + std::string(name) +
                                            "' must be a u64 value.");
            }
            return *result;
        }

        std::uint32_t DurationMs(const ContentProcessorParameters& parameters)
        {
            const std::uint64_t duration =
                UnsignedParameter(parameters, VideoDurationMsParameter, false);
            if (duration > kMaxMediaDurationMs)
            {
                throw std::invalid_argument(
                    "VideoProcessor parameter 'durationMs' must be between 0 and 2147483647.");
            }
            return static_cast<std::uint32_t>(duration);
        }

        std::uint32_t Dimension(const ContentProcessorParameters& parameters, const char* name,
                                const std::uint32_t fallback)
        {
            if (fallback != 0u && parameters.Find(name) == nullptr) { return fallback; }
            const std::uint64_t value = UnsignedParameter(parameters, name, true);
            if (value == 0u || value > Cnb::CnbMaxVideoDimension)
            {
                throw std::invalid_argument("VideoProcessor parameter '" + std::string(name) +
                                            "' must be between 1 and " +
                                            std::to_string(Cnb::CnbMaxVideoDimension) + ".");
            }
            return static_cast<std::uint32_t>(value);
        }

        float FramesPerSecond(const ContentProcessorParameters& parameters, const float fallback)
        {
            const ContentProcessorParameterValue* value =
                parameters.Find(VideoFramesPerSecondParameter);
            if (value == nullptr && fallback > 0.0f) { return fallback; }
            if (value == nullptr)
            {
                throw std::invalid_argument(
                    "VideoProcessor requires f64 parameter 'framesPerSecond'.");
            }
            const double* rate = std::get_if<double>(value);
            if (rate == nullptr)
            {
                throw std::invalid_argument(
                    "VideoProcessor parameter 'framesPerSecond' must be an f64 value.");
            }
            if (*rate <= 0.0 || *rate > std::numeric_limits<float>::max())
            {
                throw std::invalid_argument(
                    "VideoProcessor parameter 'framesPerSecond' must be positive and fit f32.");
            }
            const float result = static_cast<float>(*rate);
            if (!std::isfinite(result) || result <= 0.0f)
            {
                throw std::invalid_argument(
                    "VideoProcessor parameter 'framesPerSecond' must remain positive in f32.");
            }
            return result;
        }

        std::uint32_t SoundtrackType(const ContentProcessorParameters& parameters)
        {
            const std::uint64_t soundtrack =
                UnsignedParameter(parameters, VideoSoundtrackTypeParameter, false);
            if (soundtrack > 2u)
            {
                throw std::invalid_argument(
                    "VideoProcessor parameter 'soundtrackType' must be 0, 1, or 2.");
            }
            return static_cast<std::uint32_t>(soundtrack);
        }

        std::string StreamReference(const ImportedVideoSource& imported,
                                    const ContentProcessorParameters& parameters)
        {
            const std::string* configured =
                OptionalString(parameters, VideoStreamReferenceParameter);
            const std::string& reference = configured == nullptr ? imported.streamReference
                                                                  : *configured;
            const std::string problem = Cnb::CnbLogicalNameProblem(reference);
            if (!problem.empty())
            {
                throw std::invalid_argument("VideoProcessor parameter 'streamReference' " +
                                            problem + ".");
            }
            return reference;
        }

        /** @brief What the probe read for this source, or an all-zero fallback. */
        [[nodiscard]] ProbedVideoMetadata ProbedOrNothing(const ImportedVideoSource* imported)
        {
            if (imported == nullptr || !imported->probed.has_value()) { return {}; }
            return *imported->probed;
        }

        Cnb::CnbVideoData ReadMetadata(const ImportedVideoSource* imported,
                                       const ContentProcessorParameters& parameters)
        {
            // A parameter always wins over the probe: the project said so, and XNA lets a project
            // override what its own build read from the file.
            const ProbedVideoMetadata probed = ProbedOrNothing(imported);
            Cnb::CnbVideoData video;
            if (imported != nullptr) { video.streamReference = StreamReference(*imported, parameters); }
            else if (const std::string* stream =
                         OptionalString(parameters, VideoStreamReferenceParameter))
            {
                const std::string problem = Cnb::CnbLogicalNameProblem(*stream);
                if (!problem.empty())
                {
                    throw std::invalid_argument(
                        "VideoProcessor parameter 'streamReference' " + problem + ".");
                }
            }
            video.durationMs = parameters.Find(VideoDurationMsParameter) == nullptr
                                   ? probed.durationMs
                                   : DurationMs(parameters);
            video.width = Dimension(parameters, VideoWidthParameter, probed.width);
            video.height = Dimension(parameters, VideoHeightParameter, probed.height);
            video.framesPerSecond = FramesPerSecond(parameters, probed.framesPerSecond);
            video.soundtrackType = parameters.Find(VideoSoundtrackTypeParameter) == nullptr
                                       ? probed.soundtrackType
                                       : SoundtrackType(parameters);
            return video;
        }
    }

    VideoImporter::VideoImporter(VideoMetadataProbe probe) : probe_(std::move(probe)) {}

    ContentComponentIdentity VideoImporter::Identity() const
    {
        return {kVideoImporterName, "2"};
    }

    std::vector<std::string> VideoImporter::SourceExtensions() const
    {
        // `.wmv` is the one XNA itself accepts, and it was the one missing: the XNA façade's
        // WmvImporter maps onto this importer, so a `.wmv` that the façade accepted had no
        // canonical route and never reached an `.xnb`. This importer does not decode -- it records
        // a reference to media that stays external -- so the container makes no difference to it
        // (plans/plan_xnapipeline_parity.md XNAPP-021).
        return {".wmv", ".mp4", ".ogv", ".webm", ".mkv", ".avi", ".mov"};
    }

    std::vector<std::string> VideoImporter::OutputTypes() const
    {
        return {ImportedVideoSourceType};
    }

    ContentValue VideoImporter::Import(ContentImporterContext& context) const
    {
        std::error_code error;
        const std::uintmax_t size = std::filesystem::file_size(context.SourcePath(), error);
        if (error)
        {
            throw std::runtime_error("cannot inspect streaming video source size: " +
                                     error.message() + ".");
        }
        if (size == 0u)
        {
            throw std::invalid_argument("streaming video source must not be empty.");
        }
        if constexpr (sizeof(std::uintmax_t) > sizeof(std::uint64_t))
        {
            if (size > std::numeric_limits<std::uint64_t>::max())
            {
                throw std::overflow_error(
                    "streaming video source size exceeds the pipeline range.");
            }
        }

        const std::filesystem::path relative =
            std::filesystem::relative(context.SourcePath(), context.SourceRoot(), error);
        if (error || relative.empty())
        {
            throw std::runtime_error("cannot form a root-relative streaming video reference" +
                                     (error ? ": " + error.message() : std::string{}) + ".");
        }
        ImportedVideoSource imported;
        if (probe_)
        {
            // A probe that cannot read this particular file answers nothing rather than throwing:
            // the metadata may still arrive as parameters, and refusing here would turn a build
            // that used to work into one that does not.
            imported.probed = probe_(context.SourcePath());
        }
        imported.mediaSource = context.SourcePath();
        imported.streamReference = CNA::Internal::ContentPathToUtf8(relative);
        imported.byteSize = static_cast<std::uint64_t>(size);
        if (const std::string problem = Cnb::CnbLogicalNameProblem(imported.streamReference);
            !problem.empty())
        {
            throw std::invalid_argument("root-relative streaming video reference is " + problem +
                                        ".");
        }
        context.LogInfo("recorded " + std::to_string(imported.byteSize) +
                        "-byte external streaming video source; media bytes remain external.");
        return ContentValue::Create(ImportedVideoSourceType, std::move(imported));
    }

    VideoProcessor::VideoProcessor(const bool metadataFromSource)
        : metadataFromSource_(metadataFromSource)
    {
    }

    ContentComponentIdentity VideoProcessor::Identity() const
    {
        return {kVideoProcessorName, "2"};
    }

    std::string VideoProcessor::InputType() const
    {
        return ImportedVideoSourceType;
    }

    std::string VideoProcessor::OutputType() const
    {
        return ProcessedVideoType;
    }

    void VideoProcessor::ValidateParameters(const ContentProcessorParameters& parameters) const
    {
        for (const auto& [name, value] : parameters.Values())
        {
            static_cast<void>(value);
            if (name != VideoStreamReferenceParameter && name != VideoDurationMsParameter &&
                name != VideoWidthParameter && name != VideoHeightParameter &&
                name != VideoFramesPerSecondParameter && name != VideoSoundtrackTypeParameter)
            {
                throw std::invalid_argument("VideoProcessor does not recognize parameter '" +
                                            name + "'.");
            }
        }
        if (metadataFromSource_)
        {
            // The importer probes the file, so the three frame parameters are optional here; what
            // is present still has to be well formed, and Process() refuses a source the probe
            // could not read either.
            ImportedVideoSource assumed;
            // A name that passes, because what is under validation here is the parameters; the
            // real reference comes from the imported source when Process() runs.
            assumed.streamReference = "probe";
            assumed.probed = ProbedVideoMetadata{1u, 1u, 1.0f, 0u, 0u};
            static_cast<void>(ReadMetadata(&assumed, parameters));
            return;
        }
        static_cast<void>(ReadMetadata(nullptr, parameters));
    }

    ContentValue VideoProcessor::Process(const ContentValue& input,
                                         ContentProcessorContext& context) const
    {
        const ImportedVideoSource& imported = input.Get<ImportedVideoSource>();
        if (metadataFromSource_ && !imported.probed.has_value() &&
            (context.Parameters().Find(VideoWidthParameter) == nullptr ||
             context.Parameters().Find(VideoHeightParameter) == nullptr ||
             context.Parameters().Find(VideoFramesPerSecondParameter) == nullptr))
        {
            // This build can read a video and could not read this one. Saying that is the useful
            // half of the message; naming the parameters that would let the build proceed anyway
            // is the other half, because a project may know what a file this decoder cannot open
            // actually contains.
            throw std::invalid_argument(
                "VideoProcessor could not read the frame metadata of this video. Give 'width', "
                "'height' and 'framesPerSecond' as processor parameters, or replace the file with "
                "one this build can read.");
        }
        Cnb::CnbVideoData video = ReadMetadata(&imported, context.Parameters());
        context.AddDeploymentFile(imported.mediaSource, video.streamReference);
        context.AddRuntimeReference(video.streamReference);
        context.LogInfo(
            "prepared Video metadata, external media XREF and deployment-support file.");
        return ContentValue::Create(ProcessedVideoType, std::move(video));
    }

    ContentComponentIdentity VideoContentWriter::Identity() const
    {
        return {kVideoWriterName, "1"};
    }

    std::vector<ContentWriterSchemaIdentity>
    VideoContentWriter::OutputSchemaIdentities() const
    {
        return {{Cnb::CnbAssetTypeId::Video, Cnb::CnbMediaSchemaVersion,
                 "Microsoft.Xna.Framework.Media.Video",
                 {"CNA.Cnb.EncodeVideoToCnb", "1"}}};
    }

    std::string VideoContentWriter::InputType() const
    {
        return ProcessedVideoType;
    }

    ContentWriteResult VideoContentWriter::Write(const ContentValue& input,
                                                 const std::string& logicalName) const
    {
        const Cnb::CnbVideoData& video = input.Get<Cnb::CnbVideoData>();
        return {Cnb::EncodeVideoToCnb(video, logicalName), Cnb::CnbAssetTypeId::Video,
                "Microsoft.Xna.Framework.Media.Video", Cnb::CnbMediaSchemaVersion};
    }

    void RegisterVideoContentPipeline(ContentPipelineRegistry& registry, VideoMetadataProbe probe)
    {
        const bool hasProbe = static_cast<bool>(probe);
        registry.RegisterImporter(std::make_shared<VideoImporter>(std::move(probe)));
        registry.RegisterProcessor(std::make_shared<VideoProcessor>(hasProbe));
        registry.RegisterWriter(std::make_shared<VideoContentWriter>());
    }
}
