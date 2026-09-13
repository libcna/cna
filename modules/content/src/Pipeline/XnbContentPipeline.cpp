// SPDX-License-Identifier: MS-PL
#include "CNA/Content/Pipeline/XnbContentPipeline.hpp"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <limits>
#include <optional>
#include <stdexcept>

#include "CNA/Content/Cnb/CnbFormat.hpp"
#include "CNA/Content/Import/ImportedSound.hpp"
#include "CNA/Content/Pipeline/CnjContentPipeline.hpp"
#include "CNA/Content/Pipeline/ModelContentPipeline.hpp"
#include "CNA/Content/Pipeline/SongContentPipeline.hpp"
#include "CNA/Content/Pipeline/SoundEffectContentPipeline.hpp"
#include "CNA/Content/Pipeline/Texture2DContentPipeline.hpp"
#include "CNA/Content/Pipeline/VideoContentPipeline.hpp"
#include "CNA/Internal/ContentPath.hpp"
#include "CNA/Internal/PathContainment.hpp"
#include "CNA/Internal/Xnb/XnbCanonicalData.hpp"
#include "Microsoft/Xna/Framework/Content/ContentLoadException.hpp"

namespace CNA::Content::Pipeline
{
    namespace
    {
        using CNA::Internal::Xnb::ConvertXnbSoundToImportedSound;
        using CNA::Internal::Xnb::ConvertXnbTextureToCnbRgba8;
        using CNA::Internal::Xnb::DecodeXnbCanonicalAsset;
        using CNA::Internal::Xnb::XnbCanonicalAsset;
        using CNA::Internal::Xnb::XnbSongData;
        using CNA::Internal::Xnb::XnbSoundEffectData;
        using CNA::Internal::Xnb::XnbSpriteFontData;
        using CNA::Internal::Xnb::XnbTextureData;
        using CNA::Internal::Xnb::XnbVideoData;
        using CNA::Internal::Xnb::XnbModelData;
        using Microsoft::Xna::Framework::Content::ContentLoadException;

        constexpr const char* XnbImporterName = "CNA.XnbImporter";
        constexpr const char* XnbVideoProcessorName = "CNA.XnbVideoProcessor";
        constexpr std::uint64_t MaxMediaDurationMs = 0x7FFFFFFFu;

        [[nodiscard]] ImportedImage ImportedImageFromTexture(
            const Cnb::CnbTextureData& texture)
        {
            if (texture.representations.size() != 1u ||
                texture.representations.front().format != Cnb::CnbTextureFormat::Rgba8 ||
                texture.representations.front().levels.empty())
            {
                throw ContentLoadException(
                    "XnbImporter: canonical Texture2D does not contain one Rgba8 representation.");
            }
            ImportedImage image;
            image.width = texture.width;
            image.height = texture.height;
            image.rgbaPixels = texture.representations.front().levels.front();
            image.additionalRgbaMipLevels.assign(
                texture.representations.front().levels.begin() + 1,
                texture.representations.front().levels.end());
            // plans/plan_xnapipeline.md XNAP-96: these pixels came out of an already-built .xnb,
            // so whatever alpha policy produced them has been applied once already. The
            // processor's XNA-compatible default would apply it a second time, which is not a
            // policy choice but a corruption -- so this source declares its own answer.
            image.authoredPremultiplyAlpha = false;
            return image;
        }

        [[nodiscard]] std::filesystem::path SelectMediaDependency(
            ContentImporterContext& context, const std::string& authored,
            const std::vector<std::string>& extensions)
        {
            std::string genericAuthored = authored;
            std::replace(genericAuthored.begin(), genericAuthored.end(), '\\', '/');
            const std::filesystem::path authoredPath =
                CNA::Internal::ContentPathFromUtf8(genericAuthored);
            if (authoredPath.empty() ||
                CNA::Internal::IsDisallowedAbsolutePath(genericAuthored))
            {
                throw ContentLoadException(
                    "XnbImporter: external media path must be a non-empty relative path.");
            }
            const std::filesystem::path normalized = authoredPath.lexically_normal();
            if (normalized == ".." ||
                (!normalized.empty() && *normalized.begin() == ".."))
            {
                throw ContentLoadException(
                    "XnbImporter: external media path resolves outside the source root.");
            }

            std::filesystem::path selected = normalized;
            const std::filesystem::path exact = context.SourcePath().parent_path() / selected;
            std::error_code error;
            if (!std::filesystem::is_regular_file(exact, error) && genericAuthored.size() > 4u)
            {
                const std::string withoutExtension =
                    genericAuthored.substr(0u, genericAuthored.size() - 4u);
                for (const std::string& extension : extensions)
                {
                    const std::filesystem::path candidate =
                        CNA::Internal::ContentPathFromUtf8(withoutExtension + extension);
                    error.clear();
                    if (std::filesystem::is_regular_file(
                            context.SourcePath().parent_path() / candidate, error))
                    {
                        selected = candidate;
                        break;
                    }
                }
            }

            const std::filesystem::path resolved =
                context.ResolveSourceDependency(selected);
            error.clear();
            if (!std::filesystem::is_regular_file(resolved, error))
            {
                throw ContentLoadException(
                    "XnbImporter: external media dependency '" + authored +
                    "' does not resolve to a regular file.");
            }
            return resolved;
        }

        [[nodiscard]] std::string RootRelativeReference(
            const std::filesystem::path& root, const std::filesystem::path& dependency)
        {
            std::error_code error;
            const std::filesystem::path relative =
                std::filesystem::relative(dependency, root, error);
            if (error || relative.empty())
            {
                throw ContentLoadException(
                    "XnbImporter: cannot form a root-relative external media reference.");
            }
            const std::string result = CNA::Internal::ContentPathToUtf8(relative);
            if (const std::string problem = Cnb::CnbLogicalNameProblem(result); !problem.empty())
            {
                throw ContentLoadException(
                    "XnbImporter: external media reference is " + problem + ".");
            }
            return result;
        }

        [[nodiscard]] std::string ResolveModelTextureReference(
            const std::string& modelLogicalName, const std::string& authored)
        {
            std::string normalizedAuthored = authored;
            std::replace(normalizedAuthored.begin(), normalizedAuthored.end(), '\\', '/');
            if (normalizedAuthored.empty() ||
                CNA::Internal::IsDisallowedAbsolutePath(normalizedAuthored))
            {
                throw ContentLoadException(
                    "XnbImporter: Model texture reference must be a non-empty relative logical name.");
            }
            const std::filesystem::path resolved =
                (CNA::Internal::ContentPathFromUtf8(modelLogicalName).parent_path() /
                 CNA::Internal::ContentPathFromUtf8(normalizedAuthored)).lexically_normal();
            const std::string logical = CNA::Internal::ContentPathToUtf8(resolved);
            if (const std::string problem = Cnb::CnbLogicalNameProblem(logical); !problem.empty())
            {
                throw ContentLoadException(
                    "XnbImporter: Model texture reference '" + authored + "' is " + problem + ".");
            }
            return logical;
        }

        [[nodiscard]] std::uint64_t MediaSize(const std::filesystem::path& path)
        {
            std::error_code error;
            const std::uintmax_t size = std::filesystem::file_size(path, error);
            if (error || size == 0u)
            {
                throw ContentLoadException(
                    "XnbImporter: external media dependency is empty or unreadable.");
            }
            if constexpr (sizeof(std::uintmax_t) > sizeof(std::uint64_t))
            {
                if (size > std::numeric_limits<std::uint64_t>::max())
                {
                    throw ContentLoadException(
                        "XnbImporter: external media dependency exceeds the supported size.");
                }
            }
            return static_cast<std::uint64_t>(size);
        }

        [[nodiscard]] const std::string* OptionalString(
            const ContentProcessorParameters& parameters, const char* name)
        {
            const ContentProcessorParameterValue* value = parameters.Find(name);
            if (value == nullptr) { return nullptr; }
            const std::string* text = std::get_if<std::string>(value);
            if (text == nullptr)
            {
                throw std::invalid_argument(
                    "XnbVideoProcessor parameter '" + std::string(name) +
                    "' must be a string.");
            }
            return text;
        }

        [[nodiscard]] std::optional<std::uint64_t> OptionalUnsigned(
            const ContentProcessorParameters& parameters, const char* name)
        {
            const ContentProcessorParameterValue* value = parameters.Find(name);
            if (value == nullptr) { return std::nullopt; }
            const std::uint64_t* number = std::get_if<std::uint64_t>(value);
            if (number == nullptr)
            {
                throw std::invalid_argument(
                    "XnbVideoProcessor parameter '" + std::string(name) +
                    "' must be a u64 value.");
            }
            return *number;
        }

        [[nodiscard]] std::optional<double> OptionalDouble(
            const ContentProcessorParameters& parameters, const char* name)
        {
            const ContentProcessorParameterValue* value = parameters.Find(name);
            if (value == nullptr) { return std::nullopt; }
            const double* number = std::get_if<double>(value);
            if (number == nullptr)
            {
                throw std::invalid_argument(
                    "XnbVideoProcessor parameter '" + std::string(name) +
                    "' must be an f64 value.");
            }
            return *number;
        }
    }

    ContentComponentIdentity XnbImporter::Identity() const
    {
        return {XnbImporterName, "3"};
    }

    std::vector<std::string> XnbImporter::SourceExtensions() const
    {
        return {".xnb"};
    }

    std::vector<std::string> XnbImporter::OutputTypes() const
    {
        return {ImportedImageType, ImportedSpriteFontType, ImportedSoundType,
                ImportedTexture3DType, ImportedTextureCubeType, ImportedCurveType,
                ImportedSongSourceType, ImportedXnbVideoType, ImportedModelDocumentType};
    }

    ContentValue XnbImporter::Import(ContentImporterContext& context) const
    {
        const XnbCanonicalAsset asset = DecodeXnbCanonicalAsset(context.SourcePath());
        context.LogInfo(
            "validated XNB root ContentTypeReader '" + asset.rootReader + "'.");

        if (asset.rootReader == "Microsoft.Xna.Framework.Content.Texture2DReader")
        {
            const Cnb::CnbTextureData texture = ConvertXnbTextureToCnbRgba8(
                std::get<XnbTextureData>(asset.value));
            return ContentValue::Create(
                ImportedImageType, ImportedImageFromTexture(texture));
        }
        if (asset.rootReader == "Microsoft.Xna.Framework.Content.Texture3DReader")
        {
            const Cnb::CnbTextureData texture = ConvertXnbTextureToCnbRgba8(
                std::get<XnbTextureData>(asset.value));
            ImportedTexture3D imported;
            imported.width = texture.width;
            imported.height = texture.height;
            imported.depth = texture.depth;
            imported.rgbaPixels = texture.representations.front().levels.front();
            imported.additionalRgbaMipLevels.assign(
                texture.representations.front().levels.begin() + 1,
                texture.representations.front().levels.end());
            return ContentValue::Create(ImportedTexture3DType, std::move(imported));
        }
        if (asset.rootReader == "Microsoft.Xna.Framework.Content.TextureCubeReader")
        {
            ImportedTextureCube imported;
            imported.sourceData = ConvertXnbTextureToCnbRgba8(
                std::get<XnbTextureData>(asset.value));
            return ContentValue::Create(ImportedTextureCubeType, std::move(imported));
        }
        if (asset.rootReader == "Microsoft.Xna.Framework.Content.SpriteFontReader")
        {
            const XnbSpriteFontData& source = std::get<XnbSpriteFontData>(asset.value);
            ImportedSpriteFont imported;
            imported.atlas = ImportedImageFromTexture(
                ConvertXnbTextureToCnbRgba8(source.atlas));
            imported.lineSpacing = source.lineSpacing;
            imported.spacing = source.spacing;
            imported.defaultCharacter = source.defaultCharacter;
            imported.glyphs.reserve(source.characters.size());
            for (std::size_t index = 0u; index < source.characters.size(); ++index)
            {
                imported.glyphs.push_back(
                    {source.characters[index], source.glyphs[index], source.cropping[index],
                     source.kerning[index]});
            }
            return ContentValue::Create(ImportedSpriteFontType, std::move(imported));
        }
        if (asset.rootReader == "Microsoft.Xna.Framework.Content.SoundEffectReader")
        {
            CNA::Content::Import::ImportedSound imported = ConvertXnbSoundToImportedSound(
                std::get<XnbSoundEffectData>(asset.value),
                CNA::Internal::ContentPathToUtf8(context.SourcePath()));
            return ContentValue::Create(ImportedSoundType, std::move(imported));
        }
        if (asset.rootReader == "Microsoft.Xna.Framework.Content.CurveReader")
        {
            return ContentValue::Create(
                ImportedCurveType,
                ImportedCurve{std::get<Microsoft::Xna::Framework::Curve>(asset.value)});
        }
        if (asset.rootReader == "Microsoft.Xna.Framework.Content.SongReader")
        {
            const XnbSongData& source = std::get<XnbSongData>(asset.value);
            const std::filesystem::path media = SelectMediaDependency(
                context, source.mediaPath, {".ogg", ".oga", ".qoa"});
            if (source.durationMs < 0)
            {
                throw ContentLoadException("XnbImporter: Song duration must not be negative.");
            }
            ImportedSongSource imported;
            imported.mediaSource = media;
            imported.streamReference =
                RootRelativeReference(context.SourceRoot(), media);
            imported.byteSize = MediaSize(media);
            imported.authoredName = context.LogicalName();
            imported.authoredDurationMs = static_cast<std::uint32_t>(source.durationMs);
            return ContentValue::Create(ImportedSongSourceType, std::move(imported));
        }
        if (asset.rootReader == "Microsoft.Xna.Framework.Content.VideoReader")
        {
            const XnbVideoData& source = std::get<XnbVideoData>(asset.value);
            const std::filesystem::path media = SelectMediaDependency(
                context, source.mediaPath, {".ogv", ".ogg"});
            if (source.durationMs < 0 || source.width <= 0 || source.height <= 0 ||
                source.width > static_cast<std::int32_t>(Cnb::CnbMaxVideoDimension) ||
                source.height > static_cast<std::int32_t>(Cnb::CnbMaxVideoDimension) ||
                !std::isfinite(source.framesPerSecond) || source.framesPerSecond <= 0.0f ||
                source.soundtrackType < 0 || source.soundtrackType > 2)
            {
                throw ContentLoadException(
                    "XnbImporter: Video metadata is outside native CNB ranges.");
            }
            ImportedXnbVideo imported;
            imported.mediaSource = media;
            imported.data.streamReference =
                RootRelativeReference(context.SourceRoot(), media);
            imported.data.durationMs = static_cast<std::uint32_t>(source.durationMs);
            imported.data.width = static_cast<std::uint32_t>(source.width);
            imported.data.height = static_cast<std::uint32_t>(source.height);
            imported.data.framesPerSecond = source.framesPerSecond;
            imported.data.soundtrackType = static_cast<std::uint32_t>(source.soundtrackType);
            return ContentValue::Create(ImportedXnbVideoType, std::move(imported));
        }
        if (asset.rootReader == "Microsoft.Xna.Framework.Content.ModelReader")
        {
            ImportedModelDocument imported;
            const XnbModelData& model = std::get<XnbModelData>(asset.value);
            const auto resolve = [&context](const std::string& authored)
            {
                return ResolveModelTextureReference(context.LogicalName(), authored);
            };
            try
            {
                imported.canonicalModel = CNA::Internal::Xnb::ConvertXnbModelToCnb(
                    model, resolve);
                context.LogInfo(
                    "selected frozen Model schema 1 because every XNB semantic fits exactly.");
            }
            catch (const ContentLoadException& schema1Failure)
            {
                imported.canonicalModel = CNA::Internal::Xnb::ConvertXnbModelToCnbV2(
                    model, resolve);
                context.LogInfo(
                    "selected Model schema 2 after the schema-1 fidelity check: " +
                    std::string(schema1Failure.what()));
            }
            return ContentValue::Create(ImportedModelDocumentType, std::move(imported));
        }
        throw ContentLoadException(
            "XnbImporter: validated root dispatch produced no supported canonical route.");
    }

    ContentComponentIdentity XnbVideoProcessor::Identity() const
    {
        return {XnbVideoProcessorName, "2"};
    }

    std::string XnbVideoProcessor::InputType() const
    {
        return ImportedXnbVideoType;
    }

    std::string XnbVideoProcessor::OutputType() const
    {
        return ProcessedVideoType;
    }

    void XnbVideoProcessor::ValidateParameters(
        const ContentProcessorParameters& parameters) const
    {
        for (const auto& [name, value] : parameters.Values())
        {
            static_cast<void>(value);
            if (name != VideoStreamReferenceParameter && name != VideoDurationMsParameter &&
                name != VideoWidthParameter && name != VideoHeightParameter &&
                name != VideoFramesPerSecondParameter &&
                name != VideoSoundtrackTypeParameter)
            {
                throw ContentParameterError(
                    ContentParameterFault::UnknownName, name,
                    "XnbVideoProcessor does not recognize parameter '" + name + "'.");
            }
        }
        if (const std::string* stream =
                OptionalString(parameters, VideoStreamReferenceParameter))
        {
            if (const std::string problem = Cnb::CnbLogicalNameProblem(*stream); !problem.empty())
            {
                throw std::invalid_argument(
                    "XnbVideoProcessor parameter 'streamReference' is " + problem + ".");
            }
        }
        if (const auto duration = OptionalUnsigned(parameters, VideoDurationMsParameter);
            duration.has_value() && *duration > MaxMediaDurationMs)
        {
            throw std::invalid_argument(
                "XnbVideoProcessor parameter 'durationMs' is out of range.");
        }
        for (const char* name : {VideoWidthParameter, VideoHeightParameter})
        {
            if (const auto dimension = OptionalUnsigned(parameters, name);
                dimension.has_value() &&
                (*dimension == 0u || *dimension > Cnb::CnbMaxVideoDimension))
            {
                throw std::invalid_argument(
                    "XnbVideoProcessor dimension override is out of range.");
            }
        }
        if (const auto rate = OptionalDouble(parameters, VideoFramesPerSecondParameter);
            rate.has_value() && (!std::isfinite(*rate) || *rate <= 0.0 ||
                                 *rate > std::numeric_limits<float>::max()))
        {
            throw std::invalid_argument(
                "XnbVideoProcessor parameter 'framesPerSecond' is out of range.");
        }
        if (const auto soundtrack = OptionalUnsigned(parameters, VideoSoundtrackTypeParameter);
            soundtrack.has_value() && *soundtrack > 2u)
        {
            throw std::invalid_argument(
                "XnbVideoProcessor parameter 'soundtrackType' must be 0, 1, or 2.");
        }
    }

    ContentValue XnbVideoProcessor::Process(
        const ContentValue& input, ContentProcessorContext& context) const
    {
        const ImportedXnbVideo& imported = input.Get<ImportedXnbVideo>();
        Cnb::CnbVideoData video = imported.data;
        if (const std::string* stream =
                OptionalString(context.Parameters(), VideoStreamReferenceParameter))
        {
            video.streamReference = *stream;
        }
        if (const auto value = OptionalUnsigned(context.Parameters(), VideoDurationMsParameter))
        {
            video.durationMs = static_cast<std::uint32_t>(*value);
        }
        if (const auto value = OptionalUnsigned(context.Parameters(), VideoWidthParameter))
        {
            video.width = static_cast<std::uint32_t>(*value);
        }
        if (const auto value = OptionalUnsigned(context.Parameters(), VideoHeightParameter))
        {
            video.height = static_cast<std::uint32_t>(*value);
        }
        if (const auto value = OptionalDouble(
                context.Parameters(), VideoFramesPerSecondParameter))
        {
            video.framesPerSecond = static_cast<float>(*value);
        }
        if (const auto value = OptionalUnsigned(
                context.Parameters(), VideoSoundtrackTypeParameter))
        {
            video.soundtrackType = static_cast<std::uint32_t>(*value);
        }
        context.AddDeploymentFile(imported.mediaSource, video.streamReference);
        context.AddRuntimeReference(video.streamReference);
        context.LogInfo(
            "preserved XNB Video metadata, external media XREF and deployment-support file.");
        return ContentValue::Create(ProcessedVideoType, std::move(video));
    }

    void RegisterXnbContentPipeline(ContentPipelineRegistry& registry)
    {
        registry.RegisterImporter(std::make_shared<XnbImporter>());
        registry.RegisterProcessor(std::make_shared<XnbVideoProcessor>());
    }
}
