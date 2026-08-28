// SPDX-License-Identifier: MS-PL

#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "CNA/Content/Cnb/CnbDocument.hpp"
#include "CNA/Content/Cnb/CnbAnimationClipCodec.hpp"
#include "CNA/Content/Cnb/CnbCurveCodec.hpp"
#include "CNA/Content/Cnb/CnbSpriteFontCodec.hpp"
#include "CNA/Content/Cnb/CnbTextureCodec.hpp"
#include "CNA/Content/Cnb/CnjToCnb.hpp"
#include "CNA/Content/Pipeline/CnjContentPipeline.hpp"
#include "CNA/Content/Pipeline/ModelContentPipeline.hpp"
#include "CNA/Content/Pipeline/SoundEffectContentPipeline.hpp"
#include "CNA/Content/Pipeline/Texture2DContentPipeline.hpp"
#include "CNA/Internal/Graphics/ImageLoader.hpp"
#include "CNA/DdsCubeFixtureEXT.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"

namespace Pipeline = CNA::Content::Pipeline;
namespace Cnb = CNA::Content::Cnb;

namespace
{
    class ScratchDirectory
    {
    public:
        explicit ScratchDirectory(const std::string& tag)
            : path_(std::filesystem::temp_directory_path() /
                    ("cna_pipeline_cnj_" + tag + "_" +
                     std::to_string(reinterpret_cast<std::uintptr_t>(this))))
        {
            std::filesystem::create_directories(path_);
        }

        ~ScratchDirectory()
        {
            std::error_code error;
            std::filesystem::remove_all(path_, error);
        }

        ScratchDirectory(const ScratchDirectory&) = delete;
        ScratchDirectory& operator=(const ScratchDirectory&) = delete;

        [[nodiscard]] const std::filesystem::path& Path() const { return path_; }

    private:
        std::filesystem::path path_;
    };

    void WriteBytes(const std::filesystem::path& path, const std::vector<std::uint8_t>& bytes)
    {
        std::ofstream stream(path, std::ios::binary);
        stream.write(reinterpret_cast<const char*>(bytes.data()),
                     static_cast<std::streamsize>(bytes.size()));
    }

    void WriteText(const std::filesystem::path& path, const std::string& text)
    {
        std::ofstream stream(path, std::ios::binary);
        stream << text;
    }

    std::vector<std::uint8_t> MakePng(std::uint32_t width, std::uint32_t height)
    {
        std::vector<std::uint8_t> pixels(
            static_cast<std::size_t>(width) * height * 4u);
        for (std::size_t index = 0u; index < pixels.size(); ++index)
        {
            pixels[index] = static_cast<std::uint8_t>(index * 19u + 7u);
        }
        return CNA::Internal::Graphics::ImageLoader::EncodePng(
            pixels.data(), static_cast<int>(width), static_cast<int>(height),
            static_cast<int>(width), static_cast<int>(height));
    }

    std::vector<std::uint8_t> MakeWav()
    {
        std::vector<std::uint8_t> output;
        const auto u16 = [&](std::uint16_t value)
        {
            output.push_back(static_cast<std::uint8_t>(value));
            output.push_back(static_cast<std::uint8_t>(value >> 8u));
        };
        const auto u32 = [&](std::uint32_t value)
        {
            for (unsigned int byte = 0u; byte < 4u; ++byte)
            {
                output.push_back(static_cast<std::uint8_t>(value >> (byte * 8u)));
            }
        };
        const auto tag = [&](const char* value)
        {
            for (std::size_t index = 0u; index < 4u; ++index)
            {
                output.push_back(static_cast<std::uint8_t>(value[index]));
            }
        };
        std::vector<std::uint8_t> samples(64u);
        for (std::size_t index = 0u; index < samples.size(); ++index)
        {
            samples[index] = static_cast<std::uint8_t>(index * 5u);
        }
        tag("RIFF"); u32(36u + static_cast<std::uint32_t>(samples.size())); tag("WAVE");
        tag("fmt "); u32(16u); u16(1u); u16(1u); u32(16000u); u32(32000u); u16(2u); u16(16u);
        tag("data"); u32(static_cast<std::uint32_t>(samples.size()));
        output.insert(output.end(), samples.begin(), samples.end());
        return output;
    }

    std::vector<std::uint8_t> MakeCubeDds()
    {
        using Microsoft::Xna::Framework::Color;
        const Color faces[6] = {Color(255, 0, 0, 255), Color(0, 255, 0, 255),
                                Color(0, 0, 255, 255), Color(255, 255, 0, 255),
                                Color(255, 0, 255, 255), Color(0, 255, 255, 255)};
        return CNA::TestSupport::BuildSolidColorCubeDds(4, faces, 1);
    }

    struct ClipBinWriter
    {
        std::vector<std::uint8_t> bytes;

        void I32(std::int32_t value) { Raw(&value, sizeof(value)); }
        void F32(float value) { Raw(&value, sizeof(value)); }
        void F64(double value) { Raw(&value, sizeof(value)); }

    private:
        void Raw(const void* value, std::size_t size)
        {
            const auto* first = static_cast<const std::uint8_t*>(value);
            bytes.insert(bytes.end(), first, first + size);
        }
    };

    std::shared_ptr<const Pipeline::ContentPipelineRegistry> MakeRegistry()
    {
        auto registry = std::make_shared<Pipeline::ContentPipelineRegistry>();
        Pipeline::RegisterTexture2DContentPipeline(*registry);
        Pipeline::RegisterSoundEffectContentPipeline(*registry);
        Pipeline::RegisterModelContentPipeline(*registry);
        Pipeline::RegisterCnjContentPipeline(*registry);
        return registry;
    }

    Pipeline::ContentBuildResult Build(const std::filesystem::path& root,
                                       const std::string& source,
                                       const std::string& logicalName)
    {
        const Pipeline::ContentPipeline pipeline(MakeRegistry());
        Pipeline::ContentBuildRequest request;
        request.sourceRoot = root;
        request.source = source;
        request.logicalName = logicalName;
        return pipeline.Build(request);
    }

    std::filesystem::path FindFixture(const std::string& name)
    {
        for (const char* prefix : {"tests/assets/gltf/", "../tests/assets/gltf/",
                                   "../../tests/assets/gltf/"})
        {
            const std::filesystem::path candidate = std::string(prefix) + name;
            if (std::filesystem::exists(candidate))
            {
                return std::filesystem::weakly_canonical(candidate);
            }
        }
        return {};
    }
}

TEST(CnjContentPipelineTest, Texture2DConvergesOnTheExistingTextureProcessorAndWriter)
{
    ScratchDirectory scratch("texture");
    WriteBytes(scratch.Path() / "source.png", MakePng(4u, 3u));
    WriteText(scratch.Path() / "wall.cnj",
              R"({"cnjVersion":1,"type":"Texture2D","sourceFile":"source.png","colorKey":[7,26,45]})");

    const Pipeline::ContentBuildResult result = Build(scratch.Path(), "wall.cnj", "Textures/wall");
    EXPECT_EQ(result.importer, (Pipeline::ContentComponentIdentity{"CNA.CnjImporter", "1"}));
    EXPECT_EQ(result.processor, (Pipeline::ContentComponentIdentity{"CNA.TextureProcessor", "1"}));
    EXPECT_EQ(result.writer,
              (Pipeline::ContentComponentIdentity{"CNA.Texture2DContentWriter", "1"}));
    ASSERT_EQ(result.dependencies.size(), 2u);
    EXPECT_EQ(std::filesystem::path(result.dependencies[1].identity).filename(), "source.png");

    const Cnb::CnjToCnbResult oracle = Cnb::CompileCnjToCnb(
        (scratch.Path() / "wall.cnj").string(), scratch.Path().string(), "Textures/wall");
    EXPECT_EQ(result.output.bytes, oracle.bytes);
}

TEST(CnjContentPipelineTest, SoundEffectConvergesOnTheExistingSoundProcessorAndWriter)
{
    ScratchDirectory scratch("sound");
    WriteBytes(scratch.Path() / "beep.wav", MakeWav());
    WriteText(scratch.Path() / "beep.cnj",
              R"({"cnjVersion":1,"type":"SoundEffect","sourceFile":"beep.wav"})");

    const Pipeline::ContentBuildResult result = Build(scratch.Path(), "beep.cnj", "Sounds/beep");
    EXPECT_EQ(result.processor,
              (Pipeline::ContentComponentIdentity{"CNA.SoundEffectProcessor", "1"}));
    EXPECT_EQ(result.writer,
              (Pipeline::ContentComponentIdentity{"CNA.SoundEffectContentWriter", "1"}));
    const Cnb::CnjToCnbResult oracle = Cnb::CompileCnjToCnb(
        (scratch.Path() / "beep.cnj").string(), scratch.Path().string(), "Sounds/beep");
    EXPECT_EQ(result.output.bytes, oracle.bytes);
}

TEST(CnjContentPipelineTest, SpriteFontUsesACanonicalIntermediateAndTheExistingEncoder)
{
    ScratchDirectory scratch("font");
    WriteBytes(scratch.Path() / "atlas.png", MakePng(8u, 4u));
    WriteText(scratch.Path() / "ui.cnj",
              R"({"cnjVersion":1,"type":"SpriteFont","texture":"atlas.png",)"
              R"("lineSpacing":12,"spacing":1.5,"defaultCharacter":"?","glyphs":[)"
              R"({"char":63,"source":[0,0,3,4],"crop":[0,1,3,4],"kerning":[0,3,0.5]},)"
              R"({"char":65,"source":[3,0,2,4],"crop":[1,0,2,4],"kerning":[-1,2,0]}]})");

    const Pipeline::ContentBuildResult result = Build(scratch.Path(), "ui.cnj", "Fonts/ui");
    EXPECT_EQ(result.processor,
              (Pipeline::ContentComponentIdentity{"CNA.SpriteFontProcessor", "1"}));
    EXPECT_EQ(result.writer,
              (Pipeline::ContentComponentIdentity{"CNA.SpriteFontContentWriter", "1"}));
    const Cnb::CnjToCnbResult oracle = Cnb::CompileCnjToCnb(
        (scratch.Path() / "ui.cnj").string(), scratch.Path().string(), "Fonts/ui");
    EXPECT_EQ(result.output.bytes, oracle.bytes);

    const Cnb::CnbSpriteFontData decoded = Cnb::DecodeSpriteFontFromCnb(
        Cnb::CnbDocument::Parse(result.output.bytes, "Fonts/ui.cnb"));
    EXPECT_EQ(decoded.characters, (std::vector<SharpRuntime::charcs>{u'?', u'A'}));
    EXPECT_EQ(decoded.atlas.width, 8u);
    EXPECT_TRUE(result.runtimeReferences.empty());
}

TEST(CnjContentPipelineTest, ModelConvergesOnTheExistingModelProcessorAndWriter)
{
#if !defined(CNA_GLTF_TO_CNJ_TOOL_PATH)
    GTEST_SKIP() << "the glTF-to-CNJ tool was not built";
#else
    const std::filesystem::path fixture = FindFixture("skin-four-weighted.gltf");
    if (fixture.empty()) { GTEST_SKIP() << "glTF fixture not found"; }
    ScratchDirectory scratch("model");
    const std::string command = std::string(CNA_GLTF_TO_CNJ_TOOL_PATH) + " " +
                                fixture.string() + " " + scratch.Path().string() +
                                " asset >/dev/null 2>&1";
    ASSERT_EQ(std::system(command.c_str()), 0);

    const Pipeline::ContentBuildResult result = Build(scratch.Path(), "asset.cnj", "Models/asset");
    EXPECT_EQ(result.processor, (Pipeline::ContentComponentIdentity{"CNA.ModelProcessor", "1"}));
    EXPECT_EQ(result.writer,
              (Pipeline::ContentComponentIdentity{"CNA.ModelContentWriter", "1"}));
    EXPECT_GT(result.dependencies.size(), 2u);
    const Cnb::CnjToCnbResult oracle = Cnb::CompileCnjToCnb(
        (scratch.Path() / "asset.cnj").string(), scratch.Path().string(), "Models/asset");
    EXPECT_EQ(result.output.bytes, oracle.bytes);
#endif
}

TEST(CnjContentPipelineTest, Texture3DUsesRawImportedPixelsAndTheExistingEncoder)
{
    ScratchDirectory scratch("texture3d");
    std::vector<std::uint8_t> pixels(3u * 2u * 2u * 4u);
    for (std::size_t index = 0u; index < pixels.size(); ++index)
    {
        pixels[index] = static_cast<std::uint8_t>(index * 11u);
    }
    WriteBytes(scratch.Path() / "volume.rgba", pixels);
    WriteText(scratch.Path() / "volume.cnj",
              R"({"cnjVersion":1,"type":"Texture3D","width":3,"height":2,"depth":2,"data":"volume.rgba"})");

    const Pipeline::ContentBuildResult result =
        Build(scratch.Path(), "volume.cnj", "Textures/volume");
    EXPECT_EQ(result.processor,
              (Pipeline::ContentComponentIdentity{"CNA.Texture3DProcessor", "1"}));
    EXPECT_EQ(result.writer,
              (Pipeline::ContentComponentIdentity{"CNA.Texture3DContentWriter", "1"}));
    const Cnb::CnjToCnbResult oracle = Cnb::CompileCnjToCnb(
        (scratch.Path() / "volume.cnj").string(), scratch.Path().string(), "Textures/volume");
    EXPECT_EQ(result.output.bytes, oracle.bytes);
    const Cnb::CnbTextureData decoded = Cnb::DecodeTexture3DFromCnb(
        Cnb::CnbDocument::Parse(result.output.bytes, "volume.cnb"));
    EXPECT_EQ(decoded.depth, 2u);
    EXPECT_EQ(decoded.representations[0].levels[0], pixels);
}

TEST(CnjContentPipelineTest, TextureCubeUsesTheSharedDdsImporterAndExistingEncoder)
{
    ScratchDirectory scratch("texture_cube");
    WriteBytes(scratch.Path() / "sky.dds", MakeCubeDds());
    WriteText(scratch.Path() / "sky.cnj",
              R"({"cnjVersion":1,"type":"TextureCube","sourceFile":"sky.dds"})");

    const Pipeline::ContentBuildResult result =
        Build(scratch.Path(), "sky.cnj", "Textures/sky");
    EXPECT_EQ(result.processor,
              (Pipeline::ContentComponentIdentity{"CNA.TextureCubeProcessor", "1"}));
    EXPECT_EQ(result.writer,
              (Pipeline::ContentComponentIdentity{"CNA.TextureCubeContentWriter", "1"}));
    const Cnb::CnjToCnbResult oracle = Cnb::CompileCnjToCnb(
        (scratch.Path() / "sky.cnj").string(), scratch.Path().string(), "Textures/sky");
    EXPECT_EQ(result.output.bytes, oracle.bytes);
    EXPECT_EQ(Cnb::DecodeTextureCubeFromCnb(
                  Cnb::CnbDocument::Parse(result.output.bytes, "sky.cnb")).faceCount,
              6u);
}

TEST(CnjContentPipelineTest, CurveUsesTheSharedCanonicalReaderAndExistingEncoder)
{
    ScratchDirectory scratch("curve");
    WriteText(scratch.Path() / "curve.cnj",
              R"({"cnjVersion":1,"type":"Curve","preLoop":"Oscillate","postLoop":"CycleOffset","keys":[{"position":0,"value":1.5,"tangentOut":-0.25},{"position":2.5,"value":7.25,"continuity":"Step"}]})");

    const Pipeline::ContentBuildResult result =
        Build(scratch.Path(), "curve.cnj", "Curves/curve");
    EXPECT_EQ(result.processor,
              (Pipeline::ContentComponentIdentity{"CNA.CurveProcessor", "1"}));
    EXPECT_EQ(result.writer,
              (Pipeline::ContentComponentIdentity{"CNA.CurveContentWriter", "1"}));
    const Cnb::CnjToCnbResult oracle = Cnb::CompileCnjToCnb(
        (scratch.Path() / "curve.cnj").string(), scratch.Path().string(), "Curves/curve");
    EXPECT_EQ(result.output.bytes, oracle.bytes);
    EXPECT_EQ(Cnb::DecodeCurveFromCnb(
                  Cnb::CnbDocument::Parse(result.output.bytes, "curve.cnb"))
                  .getKeysProperty().getCountProperty(),
              2);
}

TEST(CnjContentPipelineTest, InlineAnimationUsesTheSharedCanonicalReaderAndExistingEncoder)
{
    ScratchDirectory scratch("inline_clip");
    WriteText(scratch.Path() / "walk.cnj",
              R"({"cnjVersion":1,"type":"AnimationClip","duration":2,"targetSpace":"SceneNode","tracks":[{"boneIndex":2,"keys":[{"time":0,"translation":[1,2,3],"rotation":[0,0,0,1],"scale":[1,1,1]},{"time":1.5,"translation":[4,5,6]}]}]})");

    const Pipeline::ContentBuildResult result =
        Build(scratch.Path(), "walk.cnj", "Animations/walk");
    EXPECT_EQ(result.processor,
              (Pipeline::ContentComponentIdentity{"CNA.AnimationClipProcessor", "1"}));
    EXPECT_EQ(result.writer,
              (Pipeline::ContentComponentIdentity{"CNA.AnimationClipContentWriter", "1"}));
    const Cnb::CnjToCnbResult oracle = Cnb::CompileCnjToCnb(
        (scratch.Path() / "walk.cnj").string(), scratch.Path().string(), "Animations/walk");
    EXPECT_EQ(result.output.bytes, oracle.bytes);
    EXPECT_EQ(result.dependencies.size(), 1u);
}

TEST(CnjContentPipelineTest, SidecarAnimationRecordsTheClipAsABuildDependency)
{
    ScratchDirectory scratch("sidecar_clip");
    ClipBinWriter clip;
    clip.F64(1.25); clip.I32(1); clip.I32(9); clip.I32(1); clip.F64(0.0);
    clip.F32(1.0f); clip.F32(2.0f); clip.F32(3.0f);
    clip.F32(0.0f); clip.F32(0.0f); clip.F32(0.0f); clip.F32(1.0f);
    clip.F32(1.0f); clip.F32(1.0f); clip.F32(1.0f);
    WriteBytes(scratch.Path() / "walk.clip.bin", clip.bytes);
    WriteText(scratch.Path() / "walk.cnj",
              R"({"cnjVersion":1,"type":"AnimationClip","clipFile":"walk.clip.bin"})");

    const Pipeline::ContentBuildResult result =
        Build(scratch.Path(), "walk.cnj", "Animations/walk");
    ASSERT_EQ(result.dependencies.size(), 2u);
    EXPECT_EQ(std::filesystem::path(result.dependencies[1].identity).filename(), "walk.clip.bin");
    const Cnb::CnjToCnbResult oracle = Cnb::CompileCnjToCnb(
        (scratch.Path() / "walk.cnj").string(), scratch.Path().string(), "Animations/walk");
    EXPECT_EQ(result.output.bytes, oracle.bytes);
}
