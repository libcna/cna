// SPDX-License-Identifier: MS-PL

#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#if !defined(_WIN32)
#include <spawn.h>
#include <sys/wait.h>
extern char** environ;
#endif

#include "CNA/Content/Cnb/CnbDocument.hpp"
#include "CNA/Content/Cnb/CnbSourceImport.hpp"
#include "CNA/Content/Cnb/CnbTextureCodec.hpp"
#include "CNA/Content/Pipeline/Texture2DContentPipeline.hpp"
#include "CNA/Internal/ContentPath.hpp"
#include "CNA/Internal/Graphics/ImageLoader.hpp"
#include "Microsoft/Xna/Framework/Content/ContentManager.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"

namespace Pipeline = CNA::Content::Pipeline;
namespace Cnb = CNA::Content::Cnb;

using Microsoft::Xna::Framework::Content::ContentManager;
using Microsoft::Xna::Framework::Graphics::GraphicsDevice;

namespace
{
    class ScratchDirectory
    {
    public:
        explicit ScratchDirectory(const std::string& tag)
            : path_(std::filesystem::temp_directory_path() /
                    ("cna_pipeline_texture_" + tag + "_" +
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
        std::filesystem::create_directories(path.parent_path());
        std::ofstream stream(path, std::ios::binary);
        stream.write(reinterpret_cast<const char*>(bytes.data()),
                     static_cast<std::streamsize>(bytes.size()));
    }

    std::vector<std::uint8_t> ReadBytes(const std::filesystem::path& path)
    {
        std::ifstream stream(path, std::ios::binary);
        return {std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
    }

    std::vector<std::uint8_t> DistinctPixels(int width, int height)
    {
        std::vector<std::uint8_t> pixels(static_cast<std::size_t>(width) * height * 4u);
        for (int y = 0; y < height; ++y)
        {
            for (int x = 0; x < width; ++x)
            {
                const std::size_t offset =
                    (static_cast<std::size_t>(y) * width + x) * 4u;
                pixels[offset] = static_cast<std::uint8_t>(x * 11 + 1);
                pixels[offset + 1u] = static_cast<std::uint8_t>(y * 13 + 2);
                pixels[offset + 2u] = static_cast<std::uint8_t>((x + y) * 7 + 3);
                pixels[offset + 3u] = 255u;
            }
        }
        return pixels;
    }

    std::vector<std::uint8_t> MakePng(const std::vector<std::uint8_t>& pixels,
                                      int width, int height)
    {
        return CNA::Internal::Graphics::ImageLoader::EncodePng(
            pixels.data(), width, height, width, height);
    }

    std::shared_ptr<const Pipeline::ContentPipelineRegistry> MakeRegistry()
    {
        auto registry = std::make_shared<Pipeline::ContentPipelineRegistry>();
        Pipeline::RegisterTexture2DContentPipeline(*registry);
        return registry;
    }

    Pipeline::ContentBuildResult BuildTexture(
        const std::filesystem::path& root, const Pipeline::ContentProcessorParameters& parameters = {})
    {
        const Pipeline::ContentPipeline pipeline(MakeRegistry());
        Pipeline::ContentBuildRequest request;
        request.sourceRoot = root;
        request.source = "wall.png";
        request.logicalName = "Textures/wall";
        request.parameters = parameters;
        return pipeline.Build(request);
    }

#if !defined(_WIN32)
    int RunSourceTool(const std::vector<std::string>& arguments)
    {
        std::vector<char*> argv;
        argv.push_back(const_cast<char*>(CNA_SOURCE_TO_CNB_TOOL_PATH));
        for (const std::string& argument : arguments)
        {
            argv.push_back(const_cast<char*>(argument.c_str()));
        }
        argv.push_back(nullptr);

        pid_t pid = -1;
        const int spawnResult =
            posix_spawn(&pid, CNA_SOURCE_TO_CNB_TOOL_PATH, nullptr, nullptr, argv.data(), environ);
        if (spawnResult != 0) { return -1; }
        int status = 0;
        if (waitpid(pid, &status, 0) < 0) { return -1; }
        return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
    }
#endif
}

TEST(Texture2DContentPipelineTest, BuildsHeadlesslyThroughDistinctImporterProcessorAndWriter)
{
    ScratchDirectory scratch("stages");
    const std::vector<std::uint8_t> sourcePixels = DistinctPixels(7, 5);
    WriteBytes(scratch.Path() / "wall.png", MakePng(sourcePixels, 7, 5));

    const Pipeline::ContentBuildResult result = BuildTexture(scratch.Path());
    EXPECT_EQ(result.importer, (Pipeline::ContentComponentIdentity{"CNA.ImageImporter", "1"}));
    EXPECT_EQ(result.processor,
              (Pipeline::ContentComponentIdentity{"CNA.TextureProcessor", "1"}));
    EXPECT_EQ(result.writer,
              (Pipeline::ContentComponentIdentity{"CNA.Texture2DContentWriter", "1"}));
    EXPECT_EQ(result.output.assetTypeId, Cnb::CnbAssetTypeId::Texture2D);
    EXPECT_EQ(result.output.assetTypeName, "Microsoft.Xna.Framework.Graphics.Texture2D");
    ASSERT_EQ(result.dependencies.size(), 1u);
    EXPECT_EQ(result.dependencies[0].kind, Pipeline::ContentDependencyKind::PrimarySource);
    EXPECT_TRUE(result.runtimeReferences.empty());
    ASSERT_EQ(result.messages.size(), 2u);
    EXPECT_EQ(result.messages[0].stage, Pipeline::ContentPipelineStage::Import);
    EXPECT_EQ(result.messages[0].component, "CNA.ImageImporter");
    EXPECT_EQ(result.messages[1].stage, Pipeline::ContentPipelineStage::Process);
    EXPECT_EQ(result.messages[1].component, "CNA.TextureProcessor");

    const Cnb::CnbDocument document =
        Cnb::CnbDocument::Parse(result.output.bytes, "pipeline wall.cnb");
    EXPECT_EQ(document.Metadata().contentName, "Textures/wall");
    const Cnb::CnbTextureData decoded = Cnb::DecodeTexture2DFromCnb(document);
    EXPECT_EQ(decoded.width, 7u);
    EXPECT_EQ(decoded.height, 5u);
    ASSERT_EQ(decoded.representations.size(), 1u);
    ASSERT_EQ(decoded.representations[0].levels.size(), 1u);
    EXPECT_EQ(decoded.representations[0].levels[0], sourcePixels);
}

TEST(Texture2DContentPipelineTest, ReadsANativeNonAsciiFilesystemPathWithoutNarrowing)
{
    ScratchDirectory scratch("unicode");
    const std::filesystem::path relative =
        std::filesystem::path(u8"Textury") / std::filesystem::path(u8"žluťoučký_壁.png");
    const std::filesystem::path source = scratch.Path() / relative;
    const std::vector<std::uint8_t> pixels = DistinctPixels(3, 2);
    WriteBytes(source, MakePng(pixels, 3, 2));

    const Pipeline::ContentPipeline pipeline(MakeRegistry());
    Pipeline::ContentBuildRequest request;
    request.sourceRoot = scratch.Path();
    request.source = source;
    request.logicalName = "Textury/žluťoučký_壁";
    const Pipeline::ContentBuildResult result = pipeline.Build(request);

    ASSERT_EQ(result.dependencies.size(), 1u);
    EXPECT_EQ(result.dependencies.front().identity,
              CNA::Internal::ContentPathToUtf8(std::filesystem::weakly_canonical(source)));
    const Cnb::CnbDocument document =
        Cnb::CnbDocument::Parse(result.output.bytes, "unicode path pipeline output");
    EXPECT_EQ(document.Metadata().contentName, "Textury/žluťoučký_壁");
    EXPECT_EQ(Cnb::DecodeTexture2DFromCnb(document).representations[0].levels[0], pixels);

    const Cnb::CnbTextureData compatibility = Cnb::ImportImageAsCnbTexture2D(source);
    EXPECT_EQ(Cnb::EncodeTexture2DToCnb(compatibility, request.logicalName), result.output.bytes);
}

TEST(Texture2DContentPipelineTest, IsByteIdenticalToTheUnchangedSourceProducer)
{
    ScratchDirectory scratch("oracle");
    const std::vector<std::uint8_t> sourcePixels = DistinctPixels(4, 3);
    const std::filesystem::path source = scratch.Path() / "wall.png";
    WriteBytes(source, MakePng(sourcePixels, 4, 3));

    const Pipeline::ContentBuildResult first = BuildTexture(scratch.Path());
    const Pipeline::ContentBuildResult second = BuildTexture(scratch.Path());
    EXPECT_EQ(first.output.bytes, second.output.bytes);

    const Cnb::CnbTextureData oldData = Cnb::ImportImageAsCnbTexture2D(source.string());
    const std::vector<std::uint8_t> oldLibraryBytes =
        Cnb::EncodeTexture2DToCnb(oldData, "Textures/wall");
    EXPECT_EQ(first.output.bytes, oldLibraryBytes);

#if !defined(_WIN32)
    const std::filesystem::path oldToolOutput = scratch.Path() / "old-tool.cnb";
    ASSERT_EQ(RunSourceTool({source.string(), oldToolOutput.string(), "--name", "Textures/wall"}),
              0);
    EXPECT_EQ(first.output.bytes, ReadBytes(oldToolOutput));
#endif
}

TEST(Texture2DContentPipelineTest, ColorKeyPolicyMatchesTheUnchangedProducerExactly)
{
    ScratchDirectory scratch("color_key");
    std::vector<std::uint8_t> sourcePixels = DistinctPixels(4, 4);
    sourcePixels[0] = 200u;
    sourcePixels[1] = 100u;
    sourcePixels[2] = 50u;
    sourcePixels[3] = 255u;
    const std::filesystem::path source = scratch.Path() / "wall.png";
    WriteBytes(source, MakePng(sourcePixels, 4, 4));

    Pipeline::ContentProcessorParameters parameters;
    parameters.Set(Pipeline::TextureColorKeyParameter, std::string("200,100,50"));
    const Pipeline::ContentBuildResult result = BuildTexture(scratch.Path(), parameters);

    Cnb::CnbImageImportOptions oldOptions;
    oldOptions.colorKey = std::array<std::uint8_t, 3>{200u, 100u, 50u};
    const std::vector<std::uint8_t> oldLibraryBytes = Cnb::EncodeTexture2DToCnb(
        Cnb::ImportImageAsCnbTexture2D(source.string(), oldOptions), "Textures/wall");
    EXPECT_EQ(result.output.bytes, oldLibraryBytes);

    const Cnb::CnbTextureData decoded = Cnb::DecodeTexture2DFromCnb(
        Cnb::CnbDocument::Parse(result.output.bytes, "keyed pipeline wall.cnb"));
    ASSERT_FALSE(decoded.representations.empty());
    ASSERT_FALSE(decoded.representations[0].levels.empty());
    EXPECT_EQ(decoded.representations[0].levels[0][0], 200u);
    EXPECT_EQ(decoded.representations[0].levels[0][1], 100u);
    EXPECT_EQ(decoded.representations[0].levels[0][2], 50u);
    EXPECT_EQ(decoded.representations[0].levels[0][3], 0u);

#if !defined(_WIN32)
    const std::filesystem::path oldToolOutput = scratch.Path() / "old-tool-keyed.cnb";
    ASSERT_EQ(RunSourceTool({source.string(), oldToolOutput.string(), "--name", "Textures/wall",
                             "--color-key", "200,100,50"}),
              0);
    EXPECT_EQ(result.output.bytes, ReadBytes(oldToolOutput));
#endif
}

TEST(Texture2DContentPipelineTest, ResultLoadsThroughTheExistingContentManagerRuntimePath)
{
    ScratchDirectory scratch("runtime");
    const std::vector<std::uint8_t> sourcePixels = DistinctPixels(3, 2);
    WriteBytes(scratch.Path() / "wall.png", MakePng(sourcePixels, 3, 2));
    const Pipeline::ContentBuildResult result = BuildTexture(scratch.Path());
    WriteBytes(scratch.Path() / "Textures" / "wall.cnb", result.output.bytes);

    GraphicsDevice device;
    ContentManager content(nullptr, scratch.Path().string());
    content.setGraphicsDevice(device);
    auto loaded = content.Load<Microsoft::Xna::Framework::Graphics::Texture2D>("Textures/wall");
    EXPECT_EQ(loaded.getWidthProperty(), 3);
    EXPECT_EQ(loaded.getHeightProperty(), 2);

    std::vector<Microsoft::Xna::Framework::Color> loadedPixels(6u);
    loaded.GetData(loadedPixels.data(), static_cast<int>(loadedPixels.size()));
    for (std::size_t index = 0u; index < loadedPixels.size(); ++index)
    {
        EXPECT_EQ(loadedPixels[index].getRProperty(), sourcePixels[index * 4u]);
        EXPECT_EQ(loadedPixels[index].getGProperty(), sourcePixels[index * 4u + 1u]);
        EXPECT_EQ(loadedPixels[index].getBProperty(), sourcePixels[index * 4u + 2u]);
        EXPECT_EQ(loadedPixels[index].getAProperty(), sourcePixels[index * 4u + 3u]);
    }
}

TEST(Texture2DContentPipelineTest, RejectsUnknownMistypedAndMalformedProcessorParameters)
{
    ScratchDirectory scratch("parameters");
    WriteBytes(scratch.Path() / "wall.png", MakePng(DistinctPixels(2, 2), 2, 2));

    for (const auto& [name, value] :
         std::vector<std::pair<std::string, Pipeline::ContentProcessorParameterValue>>{
             {"mystery", true},
             {Pipeline::TextureColorKeyParameter, std::uint64_t{1u}},
             {Pipeline::TextureColorKeyParameter, std::string("1,2")},
             {Pipeline::TextureColorKeyParameter, std::string("1,2,256")},
             {Pipeline::TextureColorKeyParameter, std::string("1,2,3,4")}})
    {
        Pipeline::ContentProcessorParameters parameters;
        parameters.Set(name, value);
        try
        {
            static_cast<void>(BuildTexture(scratch.Path(), parameters));
            ADD_FAILURE() << "parameter should have failed: " << name;
        }
        catch (const Pipeline::ContentPipelineError& error)
        {
            EXPECT_EQ(error.Stage(), Pipeline::ContentPipelineStage::Process);
            EXPECT_EQ(error.Component(), "CNA.TextureProcessor");
        }
    }
}
