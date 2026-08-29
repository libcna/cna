// SPDX-License-Identifier: MS-PL

#include "CNA/Content/Pipeline/SongContentPipeline.hpp"

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
        constexpr const char* kSongImporterName = "CNA.SongImporter";
        constexpr const char* kSongProcessorName = "CNA.SongProcessor";
        constexpr const char* kSongWriterName = "CNA.SongContentWriter";
        constexpr std::uint64_t kMaxMediaDurationMs = 0x7FFFFFFFu;

        const std::string* OptionalString(const ContentProcessorParameters& parameters,
                                          const char* name)
        {
            const ContentProcessorParameterValue* value = parameters.Find(name);
            if (value == nullptr) { return nullptr; }
            const std::string* text = std::get_if<std::string>(value);
            if (text == nullptr)
            {
                throw std::invalid_argument("SongProcessor parameter '" + std::string(name) +
                                            "' must be a string.");
            }
            return text;
        }

        std::optional<std::uint32_t> ConfiguredDurationMs(
            const ContentProcessorParameters& parameters)
        {
            const ContentProcessorParameterValue* value =
                parameters.Find(SongDurationMsParameter);
            if (value == nullptr) { return std::nullopt; }
            const std::uint64_t* duration = std::get_if<std::uint64_t>(value);
            if (duration == nullptr)
            {
                throw std::invalid_argument(
                    "SongProcessor parameter 'durationMs' must be a u64 value.");
            }
            if (*duration > kMaxMediaDurationMs)
            {
                throw std::invalid_argument(
                    "SongProcessor parameter 'durationMs' must be between 0 and 2147483647.");
            }
            return static_cast<std::uint32_t>(*duration);
        }

        std::string StreamReference(const ImportedSongSource& imported,
                                    const ContentProcessorParameters& parameters)
        {
            const std::string* configured =
                OptionalString(parameters, SongStreamReferenceParameter);
            const std::string& reference = configured == nullptr ? imported.streamReference
                                                                  : *configured;
            const std::string problem = Cnb::CnbLogicalNameProblem(reference);
            if (!problem.empty())
            {
                throw std::invalid_argument("SongProcessor parameter 'streamReference' is " +
                                            problem + ".");
            }
            return reference;
        }
    }

    ContentComponentIdentity SongImporter::Identity() const
    {
        return {kSongImporterName, "1"};
    }

    std::vector<std::string> SongImporter::SourceExtensions() const
    {
        // .wav remains the existing SoundEffect route and .ogg remains audio, avoiding ambiguous
        // convention routing even though a runtime Video reader can probe an authored .ogg.
        return {".mp3", ".ogg", ".oga", ".qoa", ".flac", ".opus", ".aac", ".wma"};
    }

    std::vector<std::string> SongImporter::OutputTypes() const
    {
        return {ImportedSongSourceType};
    }

    ContentValue SongImporter::Import(ContentImporterContext& context) const
    {
        std::error_code error;
        const std::uintmax_t size = std::filesystem::file_size(context.SourcePath(), error);
        if (error)
        {
            throw std::runtime_error("cannot inspect streaming audio source size: " +
                                     error.message() + ".");
        }
        if (size == 0u)
        {
            throw std::invalid_argument("streaming audio source must not be empty.");
        }
        if constexpr (sizeof(std::uintmax_t) > sizeof(std::uint64_t))
        {
            if (size > std::numeric_limits<std::uint64_t>::max())
            {
                throw std::overflow_error(
                    "streaming audio source size exceeds the pipeline range.");
            }
        }

        const std::filesystem::path relative =
            std::filesystem::relative(context.SourcePath(), context.SourceRoot(), error);
        if (error || relative.empty())
        {
            throw std::runtime_error("cannot form a root-relative streaming audio reference" +
                                     (error ? ": " + error.message() : std::string{}) + ".");
        }
        ImportedSongSource imported;
        imported.streamReference = CNA::Internal::ContentPathToUtf8(relative);
        imported.byteSize = static_cast<std::uint64_t>(size);
        if (const std::string problem = Cnb::CnbLogicalNameProblem(imported.streamReference);
            !problem.empty())
        {
            throw std::invalid_argument("root-relative streaming audio reference is " + problem +
                                        ".");
        }
        context.LogInfo("recorded " + std::to_string(imported.byteSize) +
                        "-byte external streaming audio source; media bytes remain external.");
        return ContentValue::Create(ImportedSongSourceType, std::move(imported));
    }

    ContentComponentIdentity SongProcessor::Identity() const
    {
        return {kSongProcessorName, "1"};
    }

    std::string SongProcessor::InputType() const
    {
        return ImportedSongSourceType;
    }

    std::string SongProcessor::OutputType() const
    {
        return ProcessedSongType;
    }

    void SongProcessor::ValidateParameters(const ContentProcessorParameters& parameters) const
    {
        for (const auto& [name, value] : parameters.Values())
        {
            static_cast<void>(value);
            if (name != SongStreamReferenceParameter && name != SongNameParameter &&
                name != SongDurationMsParameter)
            {
                throw std::invalid_argument("SongProcessor does not recognize parameter '" + name +
                                            "'.");
            }
        }
        static_cast<void>(OptionalString(parameters, SongNameParameter));
        static_cast<void>(ConfiguredDurationMs(parameters));
        if (const std::string* stream =
                OptionalString(parameters, SongStreamReferenceParameter))
        {
            const std::string problem = Cnb::CnbLogicalNameProblem(*stream);
            if (!problem.empty())
            {
                throw std::invalid_argument("SongProcessor parameter 'streamReference' is " +
                                            problem + ".");
            }
        }
    }

    ContentValue SongProcessor::Process(const ContentValue& input,
                                        ContentProcessorContext& context) const
    {
        const ImportedSongSource& imported = input.Get<ImportedSongSource>();
        Cnb::CnbSongData song;
        song.streamReference = StreamReference(imported, context.Parameters());
        if (const std::string* name = OptionalString(context.Parameters(), SongNameParameter))
        {
            song.name = *name;
        }
        else if (imported.authoredName.has_value())
        {
            song.name = *imported.authoredName;
        }
        const std::optional<std::uint32_t> configuredDuration =
            ConfiguredDurationMs(context.Parameters());
        song.durationMs = configuredDuration.value_or(
            imported.authoredDurationMs.value_or(0u));
        context.AddRuntimeReference(song.streamReference);
        context.LogInfo("prepared Song metadata and external media XREF for CNB encoding.");
        return ContentValue::Create(ProcessedSongType, std::move(song));
    }

    ContentComponentIdentity SongContentWriter::Identity() const
    {
        return {kSongWriterName, "1"};
    }

    std::string SongContentWriter::InputType() const
    {
        return ProcessedSongType;
    }

    ContentWriteResult SongContentWriter::Write(const ContentValue& input,
                                                const std::string& logicalName) const
    {
        const Cnb::CnbSongData& song = input.Get<Cnb::CnbSongData>();
        return {Cnb::EncodeSongToCnb(song, logicalName), Cnb::CnbAssetTypeId::Song,
                "Microsoft.Xna.Framework.Media.Song"};
    }

    void RegisterSongContentPipeline(ContentPipelineRegistry& registry)
    {
        registry.RegisterImporter(std::make_shared<SongImporter>());
        registry.RegisterProcessor(std::make_shared<SongProcessor>());
        registry.RegisterWriter(std::make_shared<SongContentWriter>());
    }
}
