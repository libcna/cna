// SPDX-License-Identifier: MS-PL

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <numeric>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "CNA/Content/Cnb/CnbByteWriter.hpp"
#include "CNA/Content/Cnb/CnbDocument.hpp"
#include "CNA/Content/Cnb/CnbFormat.hpp"
#include "CNA/Content/Cnb/CnbLoaderRegistry.hpp"
#include "CNA/Content/Cnb/CnbWriter.hpp"
#include "CNA/Content/Pipeline/CnjContentPipeline.hpp"
#include "CNA/Content/Pipeline/ContentCompiler.hpp"
#include "CNA/Content/Pipeline/ContentPipeline.hpp"
#include "CNA/Content/Pipeline/ModelContentPipeline.hpp"
#include "CNA/Content/Pipeline/SongContentPipeline.hpp"
#include "CNA/Content/Pipeline/SoundEffectContentPipeline.hpp"
#include "CNA/Content/Pipeline/Texture2DContentPipeline.hpp"
#include "CNA/Content/Pipeline/VideoContentPipeline.hpp"
#include "Microsoft/Xna/Framework/Content/ContentManager.hpp"

namespace Pipeline = CNA::Content::Pipeline;
namespace Cnb = CNA::Content::Cnb;
using Microsoft::Xna::Framework::Content::ContentManager;

namespace
{
    static_assert(Pipeline::ContentPipelineExtensionApiIsExperimental);

    constexpr const char* kCanonicalTypeName = "ExampleGame.WorldLevel";
    constexpr const char* kImportedType = "ExampleGame.Pipeline.ImportedWorldLevel";
    constexpr const char* kCompiledType = "ExampleGame.Pipeline.CompiledWorldLevel";
    constexpr Cnb::CnbChunkId kLevelChunk = Cnb::MakeChunkId('L', 'V', 'L', '0');

    struct ImportedWorldLevel
    {
        std::uint32_t width = 0u;
        std::uint32_t height = 0u;
        std::string tileset;
        std::vector<std::uint8_t> collision;
    };

    struct CompiledWorldLevel
    {
        std::uint32_t width = 0u;
        std::uint32_t height = 0u;
        std::string tileset;
        std::vector<std::uint8_t> collision;
        std::uint32_t solidTileCount = 0u;
    };

    struct WorldLevel
    {
        std::uint32_t width = 0u;
        std::uint32_t height = 0u;
        std::string tileset;
        std::uint32_t solidTileCount = 0u;
        std::vector<std::uint8_t> collision;
    };

    std::uint32_t WorldLevelAssetTypeId()
    {
        return Cnb::CnbAssetTypeIdFromName(kCanonicalTypeName);
    }

    std::vector<std::uint8_t> EncodeWorldLevelToCnb(
        const CompiledWorldLevel& level, const std::string& logicalName)
    {
        Cnb::CnbByteWriter payload;
        payload.WriteU32(level.width);
        payload.WriteU32(level.height);
        payload.WriteU32(level.solidTileCount);
        payload.WriteU32(0u); // tileset XREF index
        payload.WriteU32(static_cast<std::uint32_t>(level.collision.size()));
        payload.WriteBytes(level.collision);

        Cnb::CnbWriter writer(WorldLevelAssetTypeId(), 1u);
        writer.SetMetadata(kCanonicalTypeName, logicalName);
        writer.SetExternalReferences({Cnb::CnbExternalReference{
            0u, Cnb::CnbAssetTypeId::Texture2D, level.tileset}});
        writer.AddChunk(kLevelChunk, payload.Take(), Cnb::CnbChunkFlags::Mandatory, 4u);
        return writer.Build();
    }

    WorldLevel DecodeWorldLevelFromCnb(const Cnb::CnbDocument& document)
    {
        document.RequireAsset(WorldLevelAssetTypeId(), 1u);
        const Cnb::CnbChunkId understood[] = {kLevelChunk};
        document.RequireMandatoryChunksUnderstood(understood);
        Cnb::CnbByteReader reader = document.OpenChunk(document.RequireSingle(kLevelChunk));
        WorldLevel level;
        level.width = reader.ReadU32();
        level.height = reader.ReadU32();
        level.solidTileCount = reader.ReadU32();
        const std::uint32_t tilesetIndex = reader.ReadU32();
        const std::uint32_t byteCount = reader.ReadU32();
        const std::span<const std::uint8_t> collision = reader.ReadBytes(byteCount);
        level.collision.assign(collision.begin(), collision.end());
        reader.RequireExhausted();
        level.tileset = document.ExternalReferenceAt(tilesetIndex, "level tileset").logicalName;
        return level;
    }

    class WorldLevelImporter final : public Pipeline::ContentImporter
    {
    public:
        [[nodiscard]] Pipeline::ContentComponentIdentity Identity() const override
        {
            return {"ExampleGame.WorldLevelImporter", "3"};
        }

        [[nodiscard]] std::vector<std::string> SourceExtensions() const override
        {
            return {".level"};
        }

        [[nodiscard]] std::vector<std::string> OutputTypes() const override
        {
            return {kImportedType};
        }

        [[nodiscard]] Pipeline::ContentValue Import(
            Pipeline::ContentImporterContext& context) const override
        {
            std::ifstream source(context.SourcePath(), std::ios::binary);
            ImportedWorldLevel level;
            std::string collisionFile;
            if (!(source >> level.width >> level.height >> level.tileset >> collisionFile))
            {
                throw std::runtime_error(
                    "expected: <width> <height> <tileset-logical-name> <collision-sidecar>");
            }

            const std::filesystem::path dependency =
                context.ResolveSourceDependency(collisionFile);
            std::ifstream collision(dependency, std::ios::binary);
            level.collision.assign(std::istreambuf_iterator<char>(collision),
                                   std::istreambuf_iterator<char>());
            context.LogInfo("imported level dimensions, tileset identity and collision sidecar.");
            return Pipeline::ContentValue::Create(kImportedType, std::move(level));
        }
    };

    class WorldLevelProcessor final : public Pipeline::ContentProcessor
    {
    public:
        [[nodiscard]] Pipeline::ContentComponentIdentity Identity() const override
        {
            return {"ExampleGame.WorldLevelProcessor", "5"};
        }

        [[nodiscard]] std::string InputType() const override { return kImportedType; }
        [[nodiscard]] std::string OutputType() const override { return kCompiledType; }

        void ValidateParameters(const Pipeline::ContentProcessorParameters& parameters) const override
        {
            for (const auto& [name, value] : parameters.Values())
            {
                if (name != "solidBorder" || !std::holds_alternative<bool>(value))
                {
                    throw std::invalid_argument(
                        "WorldLevelProcessor accepts only boolean 'solidBorder'.");
                }
            }
        }

        [[nodiscard]] Pipeline::ContentValue Process(
            const Pipeline::ContentValue& input,
            Pipeline::ContentProcessorContext& context) const override
        {
            const ImportedWorldLevel& imported = input.Get<ImportedWorldLevel>();
            if (imported.width == 0u || imported.height == 0u ||
                imported.collision.size() !=
                    static_cast<std::size_t>(imported.width) * imported.height)
            {
                throw std::runtime_error("collision sidecar size does not match level dimensions.");
            }

            CompiledWorldLevel level{imported.width, imported.height, imported.tileset,
                                     imported.collision, 0u};
            const auto* parameter = context.Parameters().Find("solidBorder");
            if (parameter != nullptr && std::get<bool>(*parameter))
            {
                for (std::uint32_t y = 0u; y < level.height; ++y)
                {
                    for (std::uint32_t x = 0u; x < level.width; ++x)
                    {
                        if (x == 0u || y == 0u || x + 1u == level.width || y + 1u == level.height)
                        {
                            level.collision[static_cast<std::size_t>(y) * level.width + x] = 1u;
                        }
                    }
                }
            }
            level.solidTileCount = static_cast<std::uint32_t>(std::count_if(
                level.collision.begin(), level.collision.end(),
                [](std::uint8_t value) { return value != 0u; }));
            context.AddRuntimeReference(level.tileset, Cnb::CnbAssetTypeId::Texture2D);
            context.LogInfo("validated collision cells and precomputed the solid-tile count.");
            return Pipeline::ContentValue::Create(kCompiledType, std::move(level));
        }
    };

    class WorldLevelWriter final : public Pipeline::ContentTypeWriter
    {
    public:
        [[nodiscard]] Pipeline::ContentComponentIdentity Identity() const override
        {
            return {"ExampleGame.WorldLevelWriter", "2"};
        }

        [[nodiscard]] std::vector<Pipeline::ContentWriterSchemaIdentity>
        OutputSchemaIdentities() const override
        {
            return {{WorldLevelAssetTypeId(), 1u, kCanonicalTypeName,
                     {"ExampleGame.EncodeWorldLevelToCnb", "1"}}};
        }

        [[nodiscard]] std::string InputType() const override { return kCompiledType; }

        [[nodiscard]] Pipeline::ContentWriteResult Write(
            const Pipeline::ContentValue& input, const std::string& logicalName) const override
        {
            return {EncodeWorldLevelToCnb(input.Get<CompiledWorldLevel>(), logicalName),
                    WorldLevelAssetTypeId(), kCanonicalTypeName};
        }
    };

    class ScratchDirectory
    {
    public:
        ScratchDirectory()
            : path_(std::filesystem::temp_directory_path() /
                    ("cna_custom_pipeline_" +
                     std::to_string(reinterpret_cast<std::uintptr_t>(this))))
        {
            std::filesystem::create_directories(path_);
        }

        ~ScratchDirectory()
        {
            std::error_code error;
            std::filesystem::remove_all(path_, error);
        }

        [[nodiscard]] const std::filesystem::path& Path() const { return path_; }

    private:
        std::filesystem::path path_;
    };

    void WriteBytes(const std::filesystem::path& path, std::span<const std::uint8_t> bytes)
    {
        std::ofstream stream(path, std::ios::binary);
        stream.write(reinterpret_cast<const char*>(bytes.data()),
                     static_cast<std::streamsize>(bytes.size()));
    }
}

TEST(CustomContentPipelineTest, BuiltInRegistrationIsExplicitAndComplete)
{
    Pipeline::ContentPipelineRegistry registry;
    Pipeline::RegisterBuiltInContentPipeline(registry);

    EXPECT_EQ(registry.ResolveImporter("texture.png")->Identity().name, "CNA.ImageImporter");
    EXPECT_EQ(registry.ResolveImporter("sound.wav")->Identity().name, "CNA.WavImporter");
    EXPECT_EQ(registry.ResolveImporter("song.mp3")->Identity().name, "CNA.SongImporter");
    EXPECT_EQ(registry.ResolveImporter("video.mp4")->Identity().name, "CNA.VideoImporter");
    EXPECT_EQ(registry.ResolveImporter("model.gltf")->Identity().name, "CNA.GltfImporter");
    EXPECT_EQ(registry.ResolveImporter("asset.cnj")->Identity().name, "CNA.CnjImporter");
}

TEST(CustomContentPipelineTest, BuiltInWritersDeclareStableSchemaAndCodecIdentities)
{
    Pipeline::ContentPipelineRegistry registry;
    Pipeline::RegisterBuiltInContentPipeline(registry);
    struct Expected
    {
        const char* inputType;
        std::uint32_t assetTypeId;
        const char* assetTypeName;
        const char* codecName;
    };
    const std::vector<Expected> expected = {
        {Pipeline::ProcessedTexture2DType, Cnb::CnbAssetTypeId::Texture2D,
         "Microsoft.Xna.Framework.Graphics.Texture2D", "CNA.Cnb.EncodeTexture2DToCnb"},
        {Pipeline::ProcessedSoundEffectType, Cnb::CnbAssetTypeId::SoundEffect,
         "Microsoft.Xna.Framework.Audio.SoundEffect", "CNA.Cnb.EncodeSoundEffectToCnb"},
        {Pipeline::ProcessedSongType, Cnb::CnbAssetTypeId::Song,
         "Microsoft.Xna.Framework.Media.Song", "CNA.Cnb.EncodeSongToCnb"},
        {Pipeline::ProcessedVideoType, Cnb::CnbAssetTypeId::Video,
         "Microsoft.Xna.Framework.Media.Video", "CNA.Cnb.EncodeVideoToCnb"},
        {Pipeline::ProcessedTexture3DType, Cnb::CnbAssetTypeId::Texture3D,
         "Microsoft.Xna.Framework.Graphics.Texture3D", "CNA.Cnb.EncodeTexture3DToCnb"},
        {Pipeline::ProcessedTextureCubeType, Cnb::CnbAssetTypeId::TextureCube,
         "Microsoft.Xna.Framework.Graphics.TextureCube", "CNA.Cnb.EncodeTextureCubeToCnb"},
        {Pipeline::ProcessedCurveType, Cnb::CnbAssetTypeId::Curve,
         "Microsoft.Xna.Framework.Curve", "CNA.Cnb.EncodeCurveToCnb"},
        {Pipeline::ProcessedAnimationClipType, Cnb::CnbAssetTypeId::AnimationClip,
         "Microsoft.Xna.Framework.Graphics.AnimationClipEXT",
         "CNA.Cnb.EncodeAnimationClipToCnb"},
        {Pipeline::ProcessedSpriteFontType, Cnb::CnbAssetTypeId::SpriteFont,
         "Microsoft.Xna.Framework.Graphics.SpriteFont", "CNA.Cnb.EncodeSpriteFontToCnb"},
    };

    for (const Expected& item : expected)
    {
        const auto schemas = registry.ResolveWriter(item.inputType)->OutputSchemaIdentities();
        ASSERT_EQ(schemas.size(), 1u) << item.inputType;
        EXPECT_EQ(schemas[0].assetTypeId, item.assetTypeId) << item.inputType;
        EXPECT_EQ(schemas[0].assetSchemaVersion, 1u) << item.inputType;
        EXPECT_EQ(schemas[0].assetTypeName, item.assetTypeName) << item.inputType;
        EXPECT_EQ(schemas[0].codec.name, item.codecName) << item.inputType;
        EXPECT_EQ(schemas[0].codec.version, "1") << item.inputType;
    }

    const auto modelSchemas =
        registry.ResolveWriter(Pipeline::ProcessedModelType)->OutputSchemaIdentities();
    ASSERT_EQ(modelSchemas.size(), 3u);
    EXPECT_EQ(modelSchemas[0].assetTypeId, Cnb::CnbAssetTypeId::Texture2D);
    EXPECT_EQ(modelSchemas[0].codec.name, "CNA.Cnb.EncodeTexture2DToCnb");
    EXPECT_EQ(modelSchemas[1].assetTypeId, Cnb::CnbAssetTypeId::Model);
    EXPECT_EQ(modelSchemas[1].codec.name, "CNA.Cnb.EncodeModelToCnb");
    EXPECT_EQ(modelSchemas[2].assetTypeId, Cnb::CnbAssetTypeId::AnimationClip);
    EXPECT_EQ(modelSchemas[2].codec.name, "CNA.Cnb.EncodeAnimationClipToCnb");
    for (const Pipeline::ContentWriterSchemaIdentity& schema : modelSchemas)
    {
        EXPECT_EQ(schema.assetSchemaVersion, 1u);
        EXPECT_EQ(schema.codec.version, "1");
    }
}

TEST(CustomContentPipelineTest, CompilerEmbeddingRejectsANullRegistry)
{
    EXPECT_THROW(static_cast<void>(Pipeline::RunContentCompiler({}, nullptr)),
                 std::invalid_argument);
}

TEST(CustomContentPipelineTest, GameComponentsBuildAndLoadACustomCnbAssetEndToEnd)
{
    ScratchDirectory scratch;
    {
        std::ofstream source(scratch.Path() / "arena.level", std::ios::binary);
        source << "4 3 Textures/dungeon arena.collision\n";
    }
    WriteBytes(scratch.Path() / "arena.collision",
               std::vector<std::uint8_t>{0u, 0u, 0u, 0u, 0u, 1u,
                                         0u, 0u, 0u, 0u, 0u, 0u});

    auto registry = std::make_shared<Pipeline::ContentPipelineRegistry>();
    registry->RegisterImporter(std::make_shared<WorldLevelImporter>());
    registry->RegisterProcessor(std::make_shared<WorldLevelProcessor>());
    registry->RegisterWriter(std::make_shared<WorldLevelWriter>());
    const Pipeline::ContentPipeline pipeline(registry);

    Pipeline::ContentBuildRequest request;
    request.sourceRoot = scratch.Path();
    request.source = "arena.level";
    request.logicalName = "Levels/arena";
    request.parameters.Set("solidBorder", true);
    const Pipeline::ContentBuildResult built = pipeline.Build(request);
    const Pipeline::ContentBuildResult repeated = pipeline.Build(request);

    EXPECT_EQ(built.output.bytes, repeated.output.bytes);
    EXPECT_EQ(built.importer.name, "ExampleGame.WorldLevelImporter");
    EXPECT_EQ(built.processor.name, "ExampleGame.WorldLevelProcessor");
    EXPECT_EQ(built.writer.name, "ExampleGame.WorldLevelWriter");
    EXPECT_EQ(built.writerSchemas,
              (std::vector<Pipeline::ContentWriterSchemaIdentity>{
                  {WorldLevelAssetTypeId(), 1u, kCanonicalTypeName,
                   {"ExampleGame.EncodeWorldLevelToCnb", "1"}}}));
    ASSERT_EQ(built.dependencies.size(), 2u);
    EXPECT_EQ(built.dependencies[1].kind, Pipeline::ContentDependencyKind::SourceFile);
    ASSERT_EQ(built.runtimeReferences.size(), 1u);
    EXPECT_EQ(built.runtimeReferences[0].logicalName, "Textures/dungeon");

    const Cnb::CnbDocument document = Cnb::CnbDocument::Parse(built.output.bytes, "arena.cnb");
    EXPECT_EQ(document.Metadata().assetTypeName, kCanonicalTypeName);
    ASSERT_EQ(document.ExternalReferences().size(), 1u);
    EXPECT_EQ(document.ExternalReferences()[0].logicalName, "Textures/dungeon");

    const std::uint32_t assetTypeId = WorldLevelAssetTypeId();
    CNA::Content::CnbLoaderRegistry::Remove(assetTypeId);
    ContentManager::RegisterCnbLoaderEXT<WorldLevel>(
        assetTypeId, kCanonicalTypeName,
        [](const Cnb::CnbDocument& source, ContentManager&) -> WorldLevel
        { return DecodeWorldLevelFromCnb(source); });
    WriteBytes(scratch.Path() / "arena.cnb", built.output.bytes);

    ContentManager content(nullptr, scratch.Path().string());
    const WorldLevel loaded = content.Load<WorldLevel>("arena");
    EXPECT_EQ(loaded.width, 4u);
    EXPECT_EQ(loaded.height, 3u);
    EXPECT_EQ(loaded.tileset, "Textures/dungeon");
    EXPECT_EQ(loaded.solidTileCount, 11u);
    EXPECT_EQ(loaded.collision[5], 1u);
    EXPECT_TRUE(CNA::Content::CnbLoaderRegistry::Remove(assetTypeId));
}

TEST(CustomContentPipelineTest, CustomProcessorRejectsUntypedConfigurationAtItsBoundary)
{
    ScratchDirectory scratch;
    {
        std::ofstream source(scratch.Path() / "arena.level", std::ios::binary);
        source << "1 1 Textures/dungeon arena.collision\n";
    }
    WriteBytes(scratch.Path() / "arena.collision", std::vector<std::uint8_t>{0u});

    auto registry = std::make_shared<Pipeline::ContentPipelineRegistry>();
    registry->RegisterImporter(std::make_shared<WorldLevelImporter>());
    registry->RegisterProcessor(std::make_shared<WorldLevelProcessor>());
    registry->RegisterWriter(std::make_shared<WorldLevelWriter>());
    const Pipeline::ContentPipeline pipeline(registry);
    Pipeline::ContentBuildRequest request;
    request.sourceRoot = scratch.Path();
    request.source = "arena.level";
    request.logicalName = "Levels/arena";
    request.parameters.Set("solidBorder", std::string("yes"));

    try
    {
        static_cast<void>(pipeline.Build(request));
        FAIL() << "invalid custom processor configuration was accepted";
    }
    catch (const Pipeline::ContentPipelineError& error)
    {
        EXPECT_EQ(error.Stage(), Pipeline::ContentPipelineStage::Process);
        EXPECT_EQ(error.Component(), "ExampleGame.WorldLevelProcessor");
        EXPECT_NE(std::string(error.what()).find("solidBorder"), std::string::npos);
    }
}
