// SPDX-License-Identifier: MS-PL

#include "CNA/Content/Pipeline/CnjContentPipeline.hpp"

#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>

#include "CNA/Content/Cnb/CnbFormat.hpp"
#include "CNA/Content/Cnb/CnbAnimationClipCodec.hpp"
#include "CNA/Content/Cnb/CnbCurveCodec.hpp"
#include "CNA/Content/Cnb/CnbSoundEffectCodec.hpp"
#include "CNA/Content/Cnb/CnbSourceImport.hpp"
#include "CNA/Content/Cnb/CnbSpriteFontCodec.hpp"
#include "CNA/Content/Cnb/CnbTextureCodec.hpp"
#include "CNA/Content/Import/ImportedSound.hpp"
#include "CNA/Content/Pipeline/ModelContentPipeline.hpp"
#include "CNA/Content/Pipeline/SoundEffectContentPipeline.hpp"
#include "CNA/Internal/CnjCanonicalRead.hpp"
#include "CNA/Internal/ContentPath.hpp"
#include "CNA/Internal/CnjEnvelope.hpp"
#include "CNA/Internal/Json.hpp"
#include "Microsoft/Xna/Framework/Content/ContentLoadException.hpp"

namespace CNA::Content::Pipeline
{
    namespace
    {
        using Microsoft::Xna::Framework::Content::ContentLoadException;

        constexpr const char* kCnjImporterName = "CNA.CnjImporter";
        constexpr const char* kTexture3DProcessorName = "CNA.Texture3DProcessor";
        constexpr const char* kTexture3DWriterName = "CNA.Texture3DContentWriter";
        constexpr const char* kTextureCubeProcessorName = "CNA.TextureCubeProcessor";
        constexpr const char* kTextureCubeWriterName = "CNA.TextureCubeContentWriter";
        constexpr const char* kCurveProcessorName = "CNA.CurveProcessor";
        constexpr const char* kCurveWriterName = "CNA.CurveContentWriter";
        constexpr const char* kAnimationClipProcessorName = "CNA.AnimationClipProcessor";
        constexpr const char* kAnimationClipWriterName = "CNA.AnimationClipContentWriter";
        constexpr const char* kSpriteFontProcessorName = "CNA.SpriteFontProcessor";
        constexpr const char* kSpriteFontWriterName = "CNA.SpriteFontContentWriter";

        std::string ReadText(const std::filesystem::path& path)
        {
            std::ifstream stream(path, std::ios::binary);
            if (!stream)
            {
                throw ContentLoadException("cannot open CNJ document '" +
                                           CNA::Internal::ContentPathToUtf8(path) + "'.");
            }
            std::ostringstream text;
            text << stream.rdbuf();
            return text.str();
        }

        std::vector<std::uint8_t> ReadBytes(const std::filesystem::path& path)
        {
            std::ifstream stream(path, std::ios::binary);
            if (!stream)
            {
                throw ContentLoadException("cannot open CNJ sidecar '" +
                                           CNA::Internal::ContentPathToUtf8(path) + "'.");
            }
            return {std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
        }

        void RejectParameters(const ContentProcessorParameters& parameters,
                              const char* component)
        {
            if (!parameters.Empty())
            {
                throw std::invalid_argument(std::string(component) +
                                            " does not accept processor parameters.");
            }
        }

        std::string RequireStringMember(const CNA::Internal::JsonValue& root,
                                        const char* member,
                                        const std::filesystem::path& source)
        {
            const CNA::Internal::JsonValue* value = root.FindMember(member);
            if (value == nullptr || !value->IsString() || value->stringValue.empty())
            {
                throw ContentLoadException("CNJ document '" +
                                           CNA::Internal::ContentPathToUtf8(source) +
                                           "' type member '" +
                                           member + "' must be a non-empty string.");
            }
            return value->stringValue;
        }

        std::filesystem::path ResolveSidecar(ContentImporterContext& context,
                                             const std::string& authored)
        {
            const std::filesystem::path recorded = context.ResolveSourceDependency(
                CNA::Internal::ContentPathFromUtf8(authored));
            if (recorded.extension() == ".cnj")
            {
                throw ContentLoadException("CNJ sourceFile '" + authored +
                                           "' names another .cnj file; chaining is not allowed.");
            }
            std::filesystem::path cnjSibling = recorded;
            cnjSibling += ".cnj";
            std::error_code siblingError;
            if (std::filesystem::exists(cnjSibling, siblingError) && !siblingError)
            {
                throw ContentLoadException("CNJ sourceFile '" + authored +
                                           "' would resolve to another .cnj file; chaining is "
                                           "not allowed.");
            }
            return recorded;
        }

        ImportedSpriteFont ImportSpriteFont(ContentImporterContext& context,
                                            const CNA::Internal::JsonValue& root)
        {
            const CNA::Internal::CnjSpriteFontDescription description =
                CNA::Internal::ReadCnjSpriteFontDescription(
                    root, "SpriteFont .cnj '" +
                              CNA::Internal::ContentPathToUtf8(context.SourcePath()) + "'");
            ImportedSpriteFont imported;
            imported.atlas = DecodeImportedImage(
                ResolveSidecar(context, description.textureName));
            imported.lineSpacing = description.lineSpacing;
            imported.spacing = description.spacing;
            imported.defaultCharacter = description.defaultCharacter;
            imported.glyphs.reserve(description.glyphs.size());
            for (const CNA::Internal::CnjSpriteFontGlyph& glyph : description.glyphs)
            {
                imported.glyphs.push_back(
                    {glyph.character, glyph.source, glyph.crop, glyph.kerning});
            }
            return imported;
        }
    }

    ContentComponentIdentity CnjImporter::Identity() const
    {
        return {kCnjImporterName, "1"};
    }

    std::vector<std::string> CnjImporter::SourceExtensions() const
    {
        return {".cnj"};
    }

    std::vector<std::string> CnjImporter::OutputTypes() const
    {
        return {ImportedImageType, ImportedModelDocumentType, ImportedSoundType,
                ImportedSpriteFontType, ImportedTexture3DType, ImportedTextureCubeType,
                ImportedCurveType, ImportedAnimationClipType};
    }

    ContentValue CnjImporter::Import(ContentImporterContext& context) const
    {
        const std::string json = ReadText(context.SourcePath());
        const CNA::Internal::CnjEnvelope envelope = CNA::Internal::ParseCnjEnvelope(json);
        const std::uint32_t maxVersion = envelope.type == "Model" ? 2u : 1u;
        CNA::Internal::ValidateCnjEnvelopeBaseline(
            envelope, CNA::Internal::ContentPathToUtf8(context.SourcePath()),
            static_cast<int>(maxVersion));
        const CNA::Internal::JsonValue root = CNA::Internal::ParseJson(json);
        if (envelope.hasSourceFile &&
            (envelope.type == "Model" || envelope.type == "SpriteFont" ||
             envelope.type == "Curve" || envelope.type == "AnimationClip"))
        {
            throw ContentLoadException(
                envelope.type + " .cnj documents are self-contained and do not support "
                "'sourceFile'.");
        }

        if (envelope.type == "Texture2D")
        {
            const std::string authored =
                RequireStringMember(root, "sourceFile", context.SourcePath());
            ImportedImage imported = DecodeImportedImage(ResolveSidecar(context, authored));
            imported.authoredColorKey = CNA::Internal::ReadCnjColorKey(
                root, "Texture2D .cnj '" +
                          CNA::Internal::ContentPathToUtf8(context.SourcePath()) + "'");
            context.LogInfo("imported Texture2D CNJ through the shared image front end.");
            return ContentValue::Create(ImportedImageType, std::move(imported));
        }
        if (envelope.type == "SoundEffect")
        {
            const std::string authored =
                RequireStringMember(root, "sourceFile", context.SourcePath());
            CNA::Content::Import::ImportedSound imported =
                Cnb::ImportWavAsImportedSound(ResolveSidecar(context, authored));
            context.LogInfo("imported SoundEffect CNJ through the shared WAV front end.");
            return ContentValue::Create(ImportedSoundType, std::move(imported));
        }
        if (envelope.type == "Model")
        {
            ImportedModelDocument imported;
            imported.document = context.SourcePath();
            imported.intermediateRoot = context.SourceRoot();
            imported.recordAuthoredSidecars = true;
            context.LogInfo("imported canonical Model CNJ document.");
            return ContentValue::Create(ImportedModelDocumentType, std::move(imported));
        }
        if (envelope.type == "SpriteFont")
        {
            ImportedSpriteFont imported = ImportSpriteFont(context, root);
            context.LogInfo("imported SpriteFont CNJ and decoded its atlas.");
            return ContentValue::Create(ImportedSpriteFontType, std::move(imported));
        }

        if (envelope.type == "Texture3D")
        {
            const CNA::Internal::CnjTexture3DDescription description =
                CNA::Internal::ReadCnjTexture3DDescription(
                    root, "Texture3D .cnj '" +
                              CNA::Internal::ContentPathToUtf8(context.SourcePath()) + "'");
            ImportedTexture3D imported;
            imported.width = description.width;
            imported.height = description.height;
            imported.depth = description.depth;
            imported.rgbaPixels = ReadBytes(ResolveSidecar(context, description.dataFile));
            if (imported.rgbaPixels.size() != description.expectedByteCount)
            {
                throw ContentLoadException(
                    "Texture3D .cnj '" +
                    CNA::Internal::ContentPathToUtf8(context.SourcePath()) + "' declares " +
                    std::to_string(description.width) + "x" +
                    std::to_string(description.height) + "x" +
                    std::to_string(description.depth) + ", which needs " +
                    std::to_string(description.expectedByteCount) +
                    " Rgba8 bytes, but its sidecar holds " +
                    std::to_string(imported.rgbaPixels.size()) + ".");
            }
            context.LogInfo("imported Texture3D CNJ raw pixel sidecar.");
            return ContentValue::Create(ImportedTexture3DType, std::move(imported));
        }
        if (envelope.type == "TextureCube")
        {
            const std::string authored =
                RequireStringMember(root, "sourceFile", context.SourcePath());
            ImportedTextureCube imported;
            imported.sourceData = Cnb::ImportDdsAsCnbTextureCube(
                ResolveSidecar(context, authored));
            context.LogInfo("imported TextureCube CNJ through the shared DDS front end.");
            return ContentValue::Create(ImportedTextureCubeType, std::move(imported));
        }
        if (envelope.type == "Curve")
        {
            ImportedCurve imported{CNA::Internal::ReadCnjCurve(
                root, CNA::Internal::ContentPathToUtf8(context.SourcePath()))};
            context.LogInfo("imported canonical Curve CNJ semantics.");
            return ContentValue::Create(ImportedCurveType, std::move(imported));
        }
        if (envelope.type == "AnimationClip")
        {
            ImportedAnimationClip imported{CNA::Internal::ReadCnjAnimationClip(
                root, CNA::Internal::ContentPathToUtf8(context.SourcePath()),
                [&](const std::string& authored) { return ResolveSidecar(context, authored); })};
            context.LogInfo("imported canonical AnimationClip CNJ semantics.");
            return ContentValue::Create(ImportedAnimationClipType, std::move(imported));
        }

        throw ContentLoadException(
            "CnjImporter does not support CNJ type '" + envelope.type + "'.");
    }

    ContentComponentIdentity Texture3DProcessor::Identity() const
    {
        return {kTexture3DProcessorName, "1"};
    }

    std::string Texture3DProcessor::InputType() const { return ImportedTexture3DType; }
    std::string Texture3DProcessor::OutputType() const { return ProcessedTexture3DType; }

    void Texture3DProcessor::ValidateParameters(
        const ContentProcessorParameters& parameters) const
    {
        RejectParameters(parameters, kTexture3DProcessorName);
    }

    ContentValue Texture3DProcessor::Process(const ContentValue& input,
                                             ContentProcessorContext& context) const
    {
        const ImportedTexture3D& imported = input.Get<ImportedTexture3D>();
        Cnb::CnbTextureData texture;
        texture.width = imported.width;
        texture.height = imported.height;
        texture.depth = imported.depth;
        texture.faceCount = 1u;
        texture.mipCount = 1u;
        Cnb::CnbTextureRepresentation representation;
        representation.format = Cnb::CnbTextureFormat::Rgba8;
        representation.levels.push_back(imported.rgbaPixels);
        representation.levels.insert(
            representation.levels.end(), imported.additionalRgbaMipLevels.begin(),
            imported.additionalRgbaMipLevels.end());
        texture.mipCount = static_cast<std::uint32_t>(representation.levels.size());
        texture.representations.push_back(std::move(representation));
        context.LogInfo("prepared one canonical Rgba8 Texture3D representation.");
        return ContentValue::Create(ProcessedTexture3DType, std::move(texture));
    }

    ContentComponentIdentity Texture3DContentWriter::Identity() const
    {
        return {kTexture3DWriterName, "1"};
    }

    std::vector<ContentWriterSchemaIdentity>
    Texture3DContentWriter::OutputSchemaIdentities() const
    {
        return {{Cnb::CnbAssetTypeId::Texture3D, Cnb::CnbTextureSchemaVersion,
                 "Microsoft.Xna.Framework.Graphics.Texture3D",
                 {"CNA.Cnb.EncodeTexture3DToCnb", "1"}}};
    }

    std::string Texture3DContentWriter::InputType() const { return ProcessedTexture3DType; }

    ContentWriteResult Texture3DContentWriter::Write(const ContentValue& input,
                                                     const std::string& logicalName) const
    {
        const Cnb::CnbTextureData& texture = input.Get<Cnb::CnbTextureData>();
        return {Cnb::EncodeTexture3DToCnb(texture, logicalName),
                Cnb::CnbAssetTypeId::Texture3D,
                "Microsoft.Xna.Framework.Graphics.Texture3D"};
    }

    ContentComponentIdentity TextureCubeProcessor::Identity() const
    {
        return {kTextureCubeProcessorName, "1"};
    }

    std::string TextureCubeProcessor::InputType() const { return ImportedTextureCubeType; }
    std::string TextureCubeProcessor::OutputType() const { return ProcessedTextureCubeType; }

    void TextureCubeProcessor::ValidateParameters(
        const ContentProcessorParameters& parameters) const
    {
        RejectParameters(parameters, kTextureCubeProcessorName);
    }

    ContentValue TextureCubeProcessor::Process(const ContentValue& input,
                                               ContentProcessorContext& context) const
    {
        const ImportedTextureCube& imported = input.Get<ImportedTextureCube>();
        context.LogInfo("preserved the shared DDS importer's canonical cube representations.");
        return ContentValue::Create(ProcessedTextureCubeType, imported.sourceData);
    }

    ContentComponentIdentity TextureCubeContentWriter::Identity() const
    {
        return {kTextureCubeWriterName, "1"};
    }

    std::vector<ContentWriterSchemaIdentity>
    TextureCubeContentWriter::OutputSchemaIdentities() const
    {
        return {{Cnb::CnbAssetTypeId::TextureCube, Cnb::CnbTextureSchemaVersion,
                 "Microsoft.Xna.Framework.Graphics.TextureCube",
                 {"CNA.Cnb.EncodeTextureCubeToCnb", "1"}}};
    }

    std::string TextureCubeContentWriter::InputType() const { return ProcessedTextureCubeType; }

    ContentWriteResult TextureCubeContentWriter::Write(const ContentValue& input,
                                                       const std::string& logicalName) const
    {
        const Cnb::CnbTextureData& texture = input.Get<Cnb::CnbTextureData>();
        return {Cnb::EncodeTextureCubeToCnb(texture, logicalName),
                Cnb::CnbAssetTypeId::TextureCube,
                "Microsoft.Xna.Framework.Graphics.TextureCube"};
    }

    ContentComponentIdentity CurveProcessor::Identity() const
    {
        return {kCurveProcessorName, "1"};
    }

    std::string CurveProcessor::InputType() const { return ImportedCurveType; }
    std::string CurveProcessor::OutputType() const { return ProcessedCurveType; }

    void CurveProcessor::ValidateParameters(const ContentProcessorParameters& parameters) const
    {
        RejectParameters(parameters, kCurveProcessorName);
    }

    ContentValue CurveProcessor::Process(const ContentValue& input,
                                         ContentProcessorContext& context) const
    {
        const ImportedCurve& imported = input.Get<ImportedCurve>();
        context.LogInfo("validated Curve source semantics for compiled content.");
        return ContentValue::Create(ProcessedCurveType, ProcessedCurve{imported.value});
    }

    ContentComponentIdentity CurveContentWriter::Identity() const
    {
        return {kCurveWriterName, "1"};
    }

    std::vector<ContentWriterSchemaIdentity>
    CurveContentWriter::OutputSchemaIdentities() const
    {
        return {{Cnb::CnbAssetTypeId::Curve, Cnb::CnbCurveSchemaVersion,
                 "Microsoft.Xna.Framework.Curve",
                 {"CNA.Cnb.EncodeCurveToCnb", "1"}}};
    }

    std::string CurveContentWriter::InputType() const { return ProcessedCurveType; }

    ContentWriteResult CurveContentWriter::Write(const ContentValue& input,
                                                 const std::string& logicalName) const
    {
        const ProcessedCurve& curve = input.Get<ProcessedCurve>();
        return {Cnb::EncodeCurveToCnb(curve.value, logicalName), Cnb::CnbAssetTypeId::Curve,
                "Microsoft.Xna.Framework.Curve"};
    }

    ContentComponentIdentity AnimationClipProcessor::Identity() const
    {
        return {kAnimationClipProcessorName, "1"};
    }

    std::string AnimationClipProcessor::InputType() const { return ImportedAnimationClipType; }
    std::string AnimationClipProcessor::OutputType() const { return ProcessedAnimationClipType; }

    void AnimationClipProcessor::ValidateParameters(
        const ContentProcessorParameters& parameters) const
    {
        RejectParameters(parameters, kAnimationClipProcessorName);
    }

    ContentValue AnimationClipProcessor::Process(const ContentValue& input,
                                                 ContentProcessorContext& context) const
    {
        const ImportedAnimationClip& imported = input.Get<ImportedAnimationClip>();
        context.LogInfo("validated AnimationClip source semantics for compiled content.");
        return ContentValue::Create(
            ProcessedAnimationClipType, ProcessedAnimationClip{imported.value});
    }

    ContentComponentIdentity AnimationClipContentWriter::Identity() const
    {
        return {kAnimationClipWriterName, "1"};
    }

    std::vector<ContentWriterSchemaIdentity>
    AnimationClipContentWriter::OutputSchemaIdentities() const
    {
        return {{Cnb::CnbAssetTypeId::AnimationClip, Cnb::CnbAnimationClipSchemaVersion,
                 "Microsoft.Xna.Framework.Graphics.AnimationClipEXT",
                 {"CNA.Cnb.EncodeAnimationClipToCnb", "1"}}};
    }

    std::string AnimationClipContentWriter::InputType() const
    {
        return ProcessedAnimationClipType;
    }

    ContentWriteResult AnimationClipContentWriter::Write(
        const ContentValue& input, const std::string& logicalName) const
    {
        const ProcessedAnimationClip& clip = input.Get<ProcessedAnimationClip>();
        return {Cnb::EncodeAnimationClipToCnb(clip.value, logicalName),
                Cnb::CnbAssetTypeId::AnimationClip,
                "Microsoft.Xna.Framework.Graphics.AnimationClipEXT"};
    }

    ContentComponentIdentity SpriteFontProcessor::Identity() const
    {
        return {kSpriteFontProcessorName, "1"};
    }

    std::string SpriteFontProcessor::InputType() const
    {
        return ImportedSpriteFontType;
    }

    std::string SpriteFontProcessor::OutputType() const
    {
        return ProcessedSpriteFontType;
    }

    void SpriteFontProcessor::ValidateParameters(
        const ContentProcessorParameters& parameters) const
    {
        if (!parameters.Empty())
        {
            throw std::invalid_argument("SpriteFontProcessor does not accept processor parameters.");
        }
    }

    ContentValue SpriteFontProcessor::Process(const ContentValue& input,
                                              ContentProcessorContext& context) const
    {
        const ImportedSpriteFont& imported = input.Get<ImportedSpriteFont>();
        Cnb::CnbSpriteFontData font;
        font.atlas.width = imported.atlas.width;
        font.atlas.height = imported.atlas.height;
        font.atlas.depth = 1u;
        font.atlas.faceCount = 1u;
        font.atlas.mipCount = static_cast<std::uint32_t>(
            1u + imported.atlas.additionalRgbaMipLevels.size());
        Cnb::CnbTextureRepresentation atlasRepresentation;
        atlasRepresentation.format = Cnb::CnbTextureFormat::Rgba8;
        atlasRepresentation.levels.push_back(imported.atlas.rgbaPixels);
        atlasRepresentation.levels.insert(
            atlasRepresentation.levels.end(),
            imported.atlas.additionalRgbaMipLevels.begin(),
            imported.atlas.additionalRgbaMipLevels.end());
        font.atlas.representations.push_back(std::move(atlasRepresentation));
        font.lineSpacing = imported.lineSpacing;
        font.spacing = imported.spacing;
        font.defaultCharacter = imported.defaultCharacter;
        font.characters.reserve(imported.glyphs.size());
        font.glyphBounds.reserve(imported.glyphs.size());
        font.cropping.reserve(imported.glyphs.size());
        font.kerning.reserve(imported.glyphs.size());
        for (const ImportedSpriteFontGlyph& glyph : imported.glyphs)
        {
            font.characters.push_back(glyph.character);
            font.glyphBounds.push_back(glyph.source);
            font.cropping.push_back(glyph.crop);
            font.kerning.push_back(glyph.kerning);
        }
        context.LogInfo("prepared SpriteFont with embedded Rgba8 atlas for CNB encoding.");
        return ContentValue::Create(ProcessedSpriteFontType, std::move(font));
    }

    ContentComponentIdentity SpriteFontContentWriter::Identity() const
    {
        return {kSpriteFontWriterName, "1"};
    }

    std::vector<ContentWriterSchemaIdentity>
    SpriteFontContentWriter::OutputSchemaIdentities() const
    {
        return {{Cnb::CnbAssetTypeId::SpriteFont, Cnb::CnbSpriteFontSchemaVersion,
                 "Microsoft.Xna.Framework.Graphics.SpriteFont",
                 {"CNA.Cnb.EncodeSpriteFontToCnb", "1"}}};
    }

    std::string SpriteFontContentWriter::InputType() const
    {
        return ProcessedSpriteFontType;
    }

    ContentWriteResult SpriteFontContentWriter::Write(const ContentValue& input,
                                                      const std::string& logicalName) const
    {
        const Cnb::CnbSpriteFontData& font = input.Get<Cnb::CnbSpriteFontData>();
        return {Cnb::EncodeSpriteFontToCnb(font, logicalName), Cnb::CnbAssetTypeId::SpriteFont,
                "Microsoft.Xna.Framework.Graphics.SpriteFont"};
    }

    void RegisterCnjContentPipeline(ContentPipelineRegistry& registry)
    {
        registry.RegisterImporter(std::make_shared<CnjImporter>());
        registry.RegisterProcessor(std::make_shared<Texture3DProcessor>());
        registry.RegisterWriter(std::make_shared<Texture3DContentWriter>());
        registry.RegisterProcessor(std::make_shared<TextureCubeProcessor>());
        registry.RegisterWriter(std::make_shared<TextureCubeContentWriter>());
        registry.RegisterProcessor(std::make_shared<CurveProcessor>());
        registry.RegisterWriter(std::make_shared<CurveContentWriter>());
        registry.RegisterProcessor(std::make_shared<AnimationClipProcessor>());
        registry.RegisterWriter(std::make_shared<AnimationClipContentWriter>());
        registry.RegisterProcessor(std::make_shared<SpriteFontProcessor>());
        registry.RegisterWriter(std::make_shared<SpriteFontContentWriter>());
    }
}
