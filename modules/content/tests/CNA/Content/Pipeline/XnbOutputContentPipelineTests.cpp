// SPDX-License-Identifier: MS-PL
//
// plans/plan_xnapipeline.md XNAP-60..65: the XNB output format axis.
//
// The point of these tests is that nothing about the source side changes: the same importers and
// processors that produce a .cnb produce a .xnb, and only the writer differs.

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <variant>
#include <vector>

#include <gtest/gtest.h>

#if !defined(_WIN32)
#include <fcntl.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>
extern char** environ;
#endif

#include "CNA/Content/Cnb/CnbDocument.hpp"
#include "CNA/Content/Cnb/CnbModelData.hpp"
#include "CNA/Content/Cnb/CnbTextureCodec.hpp"
#include "CNA/Content/Pipeline/ContentBuildConfiguration.hpp"
#include "CNA/Content/Pipeline/ModelContentPipeline.hpp"
#include "CNA/Content/Pipeline/SoundEffectContentPipeline.hpp"
#include "CNA/Content/Pipeline/Texture2DContentPipeline.hpp"
#include "CNA/Content/Pipeline/XnbOutputContentPipeline.hpp"
#include "CNA/Internal/Graphics/ImageLoader.hpp"
#include "CNA/Internal/Graphics/VertexDeclarationFidelity.hpp"
#include "CNA/Internal/Xnb/XnbByteWriter.hpp"
#include "CNA/Internal/Xnb/XnbCanonicalData.hpp"

namespace Pipeline = CNA::Content::Pipeline;
namespace Cnb = CNA::Content::Cnb;
namespace Xnb = CNA::Internal::Xnb;

namespace
{
    class ScratchDirectory
    {
    public:
        explicit ScratchDirectory(const std::string& tag)
            : path_(std::filesystem::temp_directory_path() /
                    ("cna_pipeline_xnbout_" + tag + "_" +
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
        if (path.has_parent_path()) { std::filesystem::create_directories(path.parent_path()); }
        std::ofstream stream(path, std::ios::binary);
        stream.write(reinterpret_cast<const char*>(bytes.data()),
                     static_cast<std::streamsize>(bytes.size()));
    }

    std::vector<std::uint8_t> MakePng(const std::vector<std::uint8_t>& pixels, const int width,
                                      const int height)
    {
        return CNA::Internal::Graphics::ImageLoader::EncodePng(pixels.data(), width, height,
                                                               width, height);
    }

    std::vector<std::uint8_t> MakeWav(const std::uint16_t channels,
                                      const std::uint32_t sampleRate,
                                      const std::vector<std::uint8_t>& samples)
    {
        std::vector<std::uint8_t> output;
        const auto writeU16 = [&](const std::uint16_t value)
        {
            output.push_back(static_cast<std::uint8_t>(value & 0xFFu));
            output.push_back(static_cast<std::uint8_t>(value >> 8u));
        };
        const auto writeU32 = [&](const std::uint32_t value)
        {
            for (int byte = 0; byte < 4; ++byte)
            {
                output.push_back(static_cast<std::uint8_t>(value >> (byte * 8)));
            }
        };
        const auto writeTag = [&](const char* text)
        {
            for (int index = 0; index < 4; ++index)
            {
                output.push_back(static_cast<std::uint8_t>(text[index]));
            }
        };
        const auto blockAlign = static_cast<std::uint16_t>(channels * 2u);
        writeTag("RIFF");
        writeU32(4u + 24u + 8u + static_cast<std::uint32_t>(samples.size()));
        writeTag("WAVE");
        writeTag("fmt ");
        writeU32(16u);
        writeU16(1u);
        writeU16(channels);
        writeU32(sampleRate);
        writeU32(sampleRate * blockAlign);
        writeU16(blockAlign);
        writeU16(16u);
        writeTag("data");
        writeU32(static_cast<std::uint32_t>(samples.size()));
        output.insert(output.end(), samples.begin(), samples.end());
        return output;
    }

    /** @brief A registry serving both containers from one shared set of source components. */
    std::shared_ptr<const Pipeline::ContentPipelineRegistry> MakeDualFormatRegistry(
        const Xnb::XnbFileOptions& options = {})
    {
        auto registry = std::make_shared<Pipeline::ContentPipelineRegistry>();
        Pipeline::RegisterTexture2DContentPipeline(*registry);
        Pipeline::RegisterSoundEffectContentPipeline(*registry);
        Pipeline::RegisterModelContentPipeline(*registry);
        Pipeline::RegisterXnbOutputContentPipeline(*registry, options);
        return registry;
    }

    Pipeline::ContentBuildResult Build(
        const std::filesystem::path& root, const std::string& source,
        const std::string& logicalName, const Pipeline::ContentOutputFormat format,
        const Xnb::XnbFileOptions& options = {})
    {
        const Pipeline::ContentPipeline pipeline(MakeDualFormatRegistry(options));
        Pipeline::ContentBuildRequest request;
        request.sourceRoot = root;
        request.source = source;
        request.logicalName = logicalName;
        request.outputFormat = format;
        return pipeline.Build(request);
    }

    Xnb::XnbCanonicalAsset DecodeResult(const ScratchDirectory& scratch,
                                        const Pipeline::ContentBuildResult& result,
                                        const std::string& name)
    {
        const std::filesystem::path path = scratch.Path() / (name + ".xnb");
        WriteBytes(path, result.output.bytes);
        return Xnb::DecodeXnbCanonicalAsset(path);
    }
}

TEST(XnbOutputContentPipelineTest, AnImageSourceBuildsToTexture2DXnbThroughTheSameImporter)
{
    ScratchDirectory scratch("texture");
    std::vector<std::uint8_t> pixels(4u * 3u * 4u);
    for (std::size_t index = 0u; index < pixels.size(); ++index)
    {
        pixels[index] = static_cast<std::uint8_t>((index * 11u) & 0xFFu);
    }
    WriteBytes(scratch.Path() / "wall.png", MakePng(pixels, 4, 3));

    const Pipeline::ContentBuildResult result =
        Build(scratch.Path(), "wall.png", "Textures/wall",
              Pipeline::ContentOutputFormat::Xnb);

    EXPECT_EQ(result.importer.name, "CNA.ImageImporter");
    EXPECT_EQ(result.processor.name, "CNA.TextureProcessor");
    EXPECT_EQ(result.writer.name, "CNA.XnbTexture2DWriter");
    EXPECT_EQ(result.outputFormat, Pipeline::ContentOutputFormat::Xnb);
    EXPECT_EQ(result.output.assetTypeName, "Microsoft.Xna.Framework.Graphics.Texture2D");

    const Xnb::XnbCanonicalAsset asset = DecodeResult(scratch, result, "wall");
    EXPECT_EQ(asset.rootReader, "Microsoft.Xna.Framework.Content.Texture2DReader");
    const auto& texture = std::get<Xnb::XnbTextureData>(asset.value);
    EXPECT_EQ(texture.width, 4u);
    EXPECT_EQ(texture.height, 3u);
    ASSERT_EQ(texture.levels.size(), 1u);
    EXPECT_EQ(texture.levels[0], pixels);
}

TEST(XnbOutputContentPipelineTest, TheSameSourceBuildsToBothContainersFromOneRegistry)
{
    ScratchDirectory scratch("both");
    const std::vector<std::uint8_t> pixels(2u * 2u * 4u, 0x42u);
    WriteBytes(scratch.Path() / "flat.png", MakePng(pixels, 2, 2));

    const Pipeline::ContentBuildResult cnb =
        Build(scratch.Path(), "flat.png", "flat", Pipeline::ContentOutputFormat::Cnb);
    const Pipeline::ContentBuildResult xnb =
        Build(scratch.Path(), "flat.png", "flat", Pipeline::ContentOutputFormat::Xnb);

    EXPECT_EQ(cnb.importer, xnb.importer);
    EXPECT_EQ(cnb.processor, xnb.processor);
    EXPECT_NE(cnb.writer.name, xnb.writer.name);
    EXPECT_EQ(cnb.outputFormat, Pipeline::ContentOutputFormat::Cnb);
    EXPECT_EQ(xnb.outputFormat, Pipeline::ContentOutputFormat::Xnb);

    ASSERT_GE(cnb.output.bytes.size(), 3u);
    EXPECT_EQ(cnb.output.bytes[0], 'C');
    ASSERT_GE(xnb.output.bytes.size(), 3u);
    EXPECT_EQ(xnb.output.bytes[0], 'X');
    EXPECT_EQ(xnb.output.bytes[1], 'N');
    EXPECT_EQ(xnb.output.bytes[2], 'B');
}

TEST(XnbOutputContentPipelineTest, AWavSourceBuildsToSoundEffectXnb)
{
    ScratchDirectory scratch("sound");
    std::vector<std::uint8_t> samples(400u);
    for (std::size_t index = 0u; index < samples.size(); ++index)
    {
        samples[index] = static_cast<std::uint8_t>((index * 7u) & 0xFFu);
    }
    WriteBytes(scratch.Path() / "beep.wav", MakeWav(1u, 22050u, samples));

    const Pipeline::ContentBuildResult result =
        Build(scratch.Path(), "beep.wav", "Sounds/beep",
              Pipeline::ContentOutputFormat::Xnb);
    EXPECT_EQ(result.writer.name, "CNA.XnbSoundEffectWriter");

    const Xnb::XnbCanonicalAsset asset = DecodeResult(scratch, result, "beep");
    EXPECT_EQ(asset.rootReader, "Microsoft.Xna.Framework.Content.SoundEffectReader");
    const auto& sound = std::get<Xnb::XnbSoundEffectData>(asset.value);
    EXPECT_EQ(sound.formatTag, 1u);
    EXPECT_EQ(sound.channels, 1u);
    EXPECT_EQ(sound.sampleRate, 22050u);
    EXPECT_EQ(sound.bitsPerSample, 16u);
    EXPECT_EQ(sound.blockAlign, 2u);
    EXPECT_EQ(sound.averageBytesPerSecond, 44100u);
    EXPECT_EQ(sound.samples, samples);
}

TEST(XnbOutputContentPipelineTest, ContainerOptionsChangeTheWriterIdentityAndTheBytes)
{
    ScratchDirectory scratch("options");
    const std::vector<std::uint8_t> pixels(2u * 2u * 4u, 0x11u);
    WriteBytes(scratch.Path() / "flat.png", MakePng(pixels, 2, 2));

    Xnb::XnbFileOptions windows;
    Xnb::XnbFileOptions desktop;
    desktop.platform = Xnb::XnbTargetPlatform::DesktopGL;

    const Pipeline::ContentBuildResult first =
        Build(scratch.Path(), "flat.png", "flat", Pipeline::ContentOutputFormat::Xnb, windows);
    const Pipeline::ContentBuildResult second =
        Build(scratch.Path(), "flat.png", "flat", Pipeline::ContentOutputFormat::Xnb, desktop);

    // The incremental manifest keys on the writer identity and its schema declarations, so the
    // options have to reach both or a platform switch would silently reuse stale artifacts.
    EXPECT_EQ(first.writer.name, second.writer.name);
    EXPECT_NE(first.writer.version, second.writer.version);
    ASSERT_EQ(first.writerSchemas.size(), 1u);
    ASSERT_EQ(second.writerSchemas.size(), 1u);
    EXPECT_NE(first.writerSchemas[0].codec.version, second.writerSchemas[0].codec.version);
    EXPECT_NE(first.output.bytes, second.output.bytes);
    EXPECT_EQ(first.output.bytes[3], 'w');
    EXPECT_EQ(second.output.bytes[3], 'd');
}

TEST(XnbOutputContentPipelineTest, RepeatedBuildsOfOneSourceAreByteIdentical)
{
    ScratchDirectory scratch("deterministic");
    const std::vector<std::uint8_t> pixels(3u * 3u * 4u, 0x5Au);
    WriteBytes(scratch.Path() / "flat.png", MakePng(pixels, 3, 3));

    const Pipeline::ContentBuildResult first =
        Build(scratch.Path(), "flat.png", "flat", Pipeline::ContentOutputFormat::Xnb);
    const Pipeline::ContentBuildResult second =
        Build(scratch.Path(), "flat.png", "flat", Pipeline::ContentOutputFormat::Xnb);
    EXPECT_EQ(first.output.bytes, second.output.bytes);
}

TEST(XnbOutputContentPipelineTest, AModelNamingAnExternalEffectIsRefusedWithAnActionableDiagnostic)
{
    // A schema-1 Model is written by deriving each part's VertexDeclaration from CNA's own
    // canonical stride table, so schema 1 is no longer a refusal. What still cannot be written is
    // a part that names an external compiled Effect: CNA cannot produce XNA-compatible effect
    // bytecode, so emitting the reference would produce a Model that fails to load.
    Cnb::CnbModelPart part;
    part.name = "Hull";
    part.vertexStride = 32u;
    part.vertexCount = 3u;
    part.vertexBytes.assign(96u, 0u);
    part.indexElementSize = 2u;
    part.indexCount = 3u;
    part.indexBytes = {0u, 0u, 1u, 0u, 2u, 0u};
    part.primitiveCount = 1u;
    part.primitiveTopology = 4u;
    part.effectKind = Cnb::CnbEffectKind::External;
    part.externalEffect = "Effects/custom";

    Cnb::CnbModelMesh mesh;
    mesh.name = "Hull";
    mesh.parentBone = -1;
    mesh.partIndices = {0u};

    Cnb::CnbModelData model;
    model.parts.push_back(part);
    model.meshes.push_back(mesh);

    Pipeline::ProcessedModelBundle bundle;
    bundle.primary = model;

    auto registry = std::make_shared<Pipeline::ContentPipelineRegistry>();
    Pipeline::RegisterXnbOutputContentPipeline(*registry, {});
    registry->Freeze();
    const auto writer = registry->ResolveWriter(Pipeline::ProcessedModelType, {},
                                                Pipeline::ContentOutputFormat::Xnb);
    ASSERT_NE(writer, nullptr);

    try
    {
        (void)writer->Write(
            Pipeline::ContentValue::Create(Pipeline::ProcessedModelType, bundle), "ship");
        FAIL() << "a part naming an external Effect must be refused";
    }
    catch (const Xnb::XnbWriteException& error)
    {
        const std::string message = error.what();
        EXPECT_NE(message.find("Effects/custom"), std::string::npos) << message;
        EXPECT_NE(message.find("XNAP-84"), std::string::npos) << message;
    }
}

TEST(XnbOutputContentPipelineTest, AModelPartWithAnUnrecognizedStrideIsRefused)
{
    Cnb::CnbModelPart part;
    part.name = "Odd";
    part.vertexStride = 13u;
    part.vertexCount = 2u;
    part.vertexBytes.assign(26u, 0u);
    part.indexElementSize = 2u;
    part.indexCount = 3u;
    part.indexBytes = {0u, 0u, 1u, 0u, 2u, 0u};
    part.primitiveCount = 1u;
    part.primitiveTopology = 4u;

    Cnb::CnbModelMesh mesh;
    mesh.name = "Odd";
    mesh.parentBone = -1;
    mesh.partIndices = {0u};

    Cnb::CnbModelData model;
    model.parts.push_back(part);
    model.meshes.push_back(mesh);

    Pipeline::ProcessedModelBundle bundle;
    bundle.primary = model;

    auto registry = std::make_shared<Pipeline::ContentPipelineRegistry>();
    Pipeline::RegisterXnbOutputContentPipeline(*registry, {});
    registry->Freeze();
    const auto writer = registry->ResolveWriter(Pipeline::ProcessedModelType, {},
                                                Pipeline::ContentOutputFormat::Xnb);
    try
    {
        (void)writer->Write(
            Pipeline::ContentValue::Create(Pipeline::ProcessedModelType, bundle), "odd");
        FAIL() << "a stride with no canonical layout must be refused";
    }
    catch (const Xnb::XnbWriteException& error)
    {
        const std::string message = error.what();
        EXPECT_NE(message.find("stride 13"), std::string::npos) << message;
        EXPECT_NE(message.find("VertexDeclaration"), std::string::npos) << message;
    }
}

TEST(XnbOutputContentPipelineTest, WriterResolutionIsKeyedByFormatSoNeitherShadowsTheOther)
{
    auto registry = std::make_shared<Pipeline::ContentPipelineRegistry>();
    Pipeline::RegisterTexture2DContentPipeline(*registry);
    Pipeline::RegisterXnbOutputContentPipeline(*registry, {});
    registry->Freeze();

    EXPECT_EQ(registry->ResolveWriter(Pipeline::ProcessedTexture2DType, {},
                                      Pipeline::ContentOutputFormat::Cnb)->Identity().name,
              "CNA.Texture2DContentWriter");
    EXPECT_EQ(registry->ResolveWriter(Pipeline::ProcessedTexture2DType, {},
                                      Pipeline::ContentOutputFormat::Xnb)->Identity().name,
              "CNA.XnbTexture2DWriter");
}

TEST(XnbOutputContentPipelineTest, AProjectFileCanSelectTheContainerGloballyAndPerAsset)
{
    const Pipeline::ContentBuildConfiguration configuration =
        Pipeline::ContentBuildConfiguration::Parse(R"({
            "format": "CNA.ContentPipeline.Config",
            "version": 1,
            "outputFormat": "xnb",
            "assets": {
                "keep.png": { "outputFormat": "cnb" },
                "ship.png": { "logicalName": "Ships/ship" }
            }
        })");

    ASSERT_TRUE(configuration.OutputFormat().has_value());
    EXPECT_EQ(*configuration.OutputFormat(), Pipeline::ContentOutputFormat::Xnb);
    ASSERT_NE(configuration.Find("keep.png"), nullptr);
    ASSERT_TRUE(configuration.Find("keep.png")->outputFormat.has_value());
    EXPECT_EQ(*configuration.Find("keep.png")->outputFormat,
              Pipeline::ContentOutputFormat::Cnb);
    ASSERT_NE(configuration.Find("ship.png"), nullptr);
    EXPECT_FALSE(configuration.Find("ship.png")->outputFormat.has_value());
}

TEST(XnbOutputContentPipelineTest, AnUnknownProjectOutputFormatIsRejected)
{
    EXPECT_THROW((void)Pipeline::ContentBuildConfiguration::Parse(R"({
            "format": "CNA.ContentPipeline.Config",
            "version": 1,
            "outputFormat": "wad",
            "assets": {}
        })"),
                 std::runtime_error);
}

// -- cna-content command line (XNAP-62, XNAP-64) -----------------------------------------------

#if !defined(_WIN32) && defined(CNA_CONTENT_TOOL_PATH)

namespace
{
    /** @brief Runs the real `cna-content` executable and captures its combined output. */
    int RunContentTool(const std::vector<std::string>& arguments, std::string& output)
    {
        const std::filesystem::path capture =
            std::filesystem::temp_directory_path() /
            ("cna_xnb_cli_capture_" + std::to_string(::getpid()) + "_" +
             std::to_string(reinterpret_cast<std::uintptr_t>(&arguments)));

        posix_spawn_file_actions_t actions;
        posix_spawn_file_actions_init(&actions);
        posix_spawn_file_actions_addopen(&actions, STDOUT_FILENO, capture.c_str(),
                                         O_WRONLY | O_CREAT | O_TRUNC, 0644);
        posix_spawn_file_actions_adddup2(&actions, STDOUT_FILENO, STDERR_FILENO);

        std::vector<char*> argv;
        argv.push_back(const_cast<char*>(CNA_CONTENT_TOOL_PATH));
        for (const std::string& argument : arguments)
        {
            argv.push_back(const_cast<char*>(argument.c_str()));
        }
        argv.push_back(nullptr);

        pid_t pid = -1;
        const int spawnResult =
            posix_spawn(&pid, CNA_CONTENT_TOOL_PATH, &actions, nullptr, argv.data(), environ);
        posix_spawn_file_actions_destroy(&actions);
        if (spawnResult != 0)
        {
            ADD_FAILURE() << "posix_spawn failed";
            return -1;
        }
        int status = 0;
        if (waitpid(pid, &status, 0) < 0) { return -1; }

        std::ifstream stream(capture, std::ios::binary);
        output.assign(std::istreambuf_iterator<char>(stream),
                      std::istreambuf_iterator<char>());
        std::error_code error;
        std::filesystem::remove(capture, error);
        return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
    }
}

TEST(XnbOutputCliTest, ADirectoryBuildEmitsXnbFilesAndIsIncremental)
{
    ScratchDirectory scratch("cli_directory");
    const std::filesystem::path source = scratch.Path() / "Source";
    const std::filesystem::path output = scratch.Path() / "Content";
    const std::vector<std::uint8_t> pixels(2u * 2u * 4u, 0x77u);
    WriteBytes(source / "flat.png", MakePng(pixels, 2, 2));

    std::string log;
    ASSERT_EQ(RunContentTool({"build", source.string(), "-o", output.string(),
                              "--format", "xnb"}, log), 0)
        << log;
    EXPECT_NE(log.find("CNA.XnbTexture2DWriter"), std::string::npos) << log;
    ASSERT_TRUE(std::filesystem::exists(output / "flat.xnb")) << log;
    EXPECT_FALSE(std::filesystem::exists(output / "flat.cnb"));

    const Xnb::XnbCanonicalAsset asset =
        Xnb::DecodeXnbCanonicalAsset(output / "flat.xnb");
    EXPECT_EQ(asset.rootReader, "Microsoft.Xna.Framework.Content.Texture2DReader");
    EXPECT_EQ(asset.platform, 'w');
    EXPECT_EQ(asset.version, 5);

    std::string skipLog;
    ASSERT_EQ(RunContentTool({"build", source.string(), "-o", output.string(),
                              "--format", "xnb"}, skipLog), 0)
        << skipLog;
    EXPECT_NE(skipLog.find("Built: 0  Skipped: 1"), std::string::npos) << skipLog;
}

TEST(XnbOutputCliTest, ChangingTheTargetPlatformRebuildsRatherThanReusingStaleBytes)
{
    ScratchDirectory scratch("cli_platform");
    const std::filesystem::path source = scratch.Path() / "Source";
    const std::filesystem::path output = scratch.Path() / "Content";
    const std::vector<std::uint8_t> pixels(2u * 2u * 4u, 0x21u);
    WriteBytes(source / "flat.png", MakePng(pixels, 2, 2));

    std::string first;
    ASSERT_EQ(RunContentTool({"build", source.string(), "-o", output.string(),
                              "--format", "xnb"}, first), 0)
        << first;
    EXPECT_EQ(Xnb::DecodeXnbCanonicalAsset(output / "flat.xnb").platform, 'w');

    std::string second;
    ASSERT_EQ(RunContentTool({"build", source.string(), "-o", output.string(), "--format", "xnb",
                              "--xnb-platform", "desktopgl"}, second), 0)
        << second;
    EXPECT_NE(second.find("Built: 1"), std::string::npos) << second;
    EXPECT_EQ(Xnb::DecodeXnbCanonicalAsset(output / "flat.xnb").platform, 'd');
}

TEST(XnbOutputCliTest, ASingleFileBuildRequiresAnExtensionMatchingTheSelectedFormat)
{
    ScratchDirectory scratch("cli_extension");
    const std::filesystem::path source = scratch.Path() / "flat.png";
    WriteBytes(source, MakePng(std::vector<std::uint8_t>(4u * 4u, 0x33u), 2, 2));

    std::string log;
    EXPECT_EQ(RunContentTool({"build", source.string(), "-o",
                              (scratch.Path() / "flat.cnb").string(), "--format", "xnb"}, log),
              1)
        << log;
    EXPECT_NE(log.find("ending in '.xnb'"), std::string::npos) << log;

    std::string good;
    EXPECT_EQ(RunContentTool({"build", source.string(), "-o",
                              (scratch.Path() / "flat.xnb").string(), "--format", "xnb"}, good),
              0)
        << good;
    EXPECT_TRUE(std::filesystem::exists(scratch.Path() / "flat.xnb"));
}

TEST(XnbOutputCliTest, AProjectFileCanSelectXnbForOneAssetWhileTheRestStayCnb)
{
    ScratchDirectory scratch("cli_project");
    const std::filesystem::path source = scratch.Path() / "Source";
    const std::filesystem::path output = scratch.Path() / "Content";
    WriteBytes(source / "native.png", MakePng(std::vector<std::uint8_t>(16u, 0x10u), 2, 2));
    WriteBytes(source / "legacy.png", MakePng(std::vector<std::uint8_t>(16u, 0x20u), 2, 2));

    const std::string configuration = R"({
        "format": "CNA.ContentPipeline.Config",
        "version": 1,
        "assets": {
            "legacy.png": { "outputFormat": "xnb" }
        }
    })";
    WriteBytes(source / ".cna-content.json",
               std::vector<std::uint8_t>(configuration.begin(), configuration.end()));

    std::string log;
    ASSERT_EQ(RunContentTool({"build", source.string(), "-o", output.string()}, log), 0) << log;
    EXPECT_TRUE(std::filesystem::exists(output / "native.cnb")) << log;
    EXPECT_TRUE(std::filesystem::exists(output / "legacy.xnb")) << log;
    EXPECT_FALSE(std::filesystem::exists(output / "legacy.cnb"));
    EXPECT_EQ(Xnb::DecodeXnbCanonicalAsset(output / "legacy.xnb").rootReader,
              "Microsoft.Xna.Framework.Content.Texture2DReader");
}

TEST(XnbOutputCliTest, AnUnknownTargetPlatformListsTheKnownOnes)
{
    ScratchDirectory scratch("cli_bad_platform");
    std::string log;
    EXPECT_EQ(RunContentTool({"build", scratch.Path().string(), "-o",
                              (scratch.Path() / "out").string(), "--format", "xnb",
                              "--xnb-platform", "dreamcast"}, log),
              2)
        << log;
    EXPECT_NE(log.find("windows"), std::string::npos) << log;
    EXPECT_NE(log.find("xbox360"), std::string::npos) << log;
}

TEST(XnbOutputCliTest, TheUsageTextSeparatesXnaTargetsFromExtendedEcosystemTargets)
{
    std::string log;
    EXPECT_EQ(RunContentTool({}, log), 2) << log;
    EXPECT_NE(log.find("XNA 4.0 target platforms: windows, windowsphone, xbox360"),
              std::string::npos)
        << log;
    EXPECT_NE(log.find("extended"), std::string::npos) << log;
}

#endif

// -- glTF to XNA Model (XNAP-56, XNAP-57, XNAP-58) ---------------------------------------------
//
// Named XnbModelSourceRouteTest rather than anything with "Gltf" in it on purpose: these assert
// what the XNB *writer* does with a model, not how CNA interprets glTF. plan_gltf.md's conformance
// ladder governs every Gltf* suite and would have to claim this one, which would file an XNB
// output test under a glTF conformance rung it does not belong to.

namespace
{
    /** @brief Copies one committed glTF fixture into a scratch source root. */
    void CopyGltfFixture(const ScratchDirectory& scratch, const std::string& name)
    {
        const std::filesystem::path source = std::filesystem::path("tests/assets/gltf") / name;
        ASSERT_TRUE(std::filesystem::exists(source)) << source.string();
        std::filesystem::copy_file(source, scratch.Path() / name,
                                   std::filesystem::copy_options::overwrite_existing);
    }

    /** @brief Builds one glTF fixture to XNB and decodes it back through CNA's own reader. */
    Xnb::XnbCanonicalAsset BuildGltfModel(const ScratchDirectory& scratch,
                                          const std::string& name,
                                          Pipeline::ContentBuildResult& result)
    {
        result = Build(scratch.Path(), name, "models/asset",
                       Pipeline::ContentOutputFormat::Xnb);
        const std::filesystem::path path = scratch.Path() / "asset.xnb";
        WriteBytes(path, result.output.bytes);
        return Xnb::DecodeXnbCanonicalAsset(path);
    }
}

TEST(XnbModelSourceRouteTest, ATriangleListGltfBecomesACompleteXnaModel)
{
    ScratchDirectory scratch("gltf_model");
    CopyGltfFixture(scratch, "mode-triangles.glb");

    Pipeline::ContentBuildResult result;
    const Xnb::XnbCanonicalAsset asset =
        BuildGltfModel(scratch, "mode-triangles.glb", result);

    EXPECT_EQ(result.importer.name, "CNA.GltfImporter");
    EXPECT_EQ(result.processor.name, "CNA.ModelProcessor");
    EXPECT_EQ(result.writer.name, "CNA.XnbModelWriter");
    EXPECT_EQ(asset.rootReader, "Microsoft.Xna.Framework.Content.ModelReader");

    const auto& model = std::get<Xnb::XnbModelData>(asset.value);
    ASSERT_GE(model.bones.size(), 1u);
    EXPECT_EQ(model.rootBone, 0);
    EXPECT_EQ(model.bones[0].parent, -1);
    ASSERT_EQ(model.meshes.size(), 1u);
    ASSERT_EQ(model.meshes[0].parts.size(), 1u);
    EXPECT_GT(model.meshes[0].parts[0].primitiveCount, 0);
    EXPECT_GT(model.meshes[0].boundingSphere.Radius, 0.0f);

    // One vertex buffer, one index buffer and one effect: the three resources an XNA mesh part
    // must name.
    ASSERT_EQ(model.sharedResources.size(), 3u);
    const auto& vertexBuffer =
        std::get<Xnb::XnbVertexBufferData>(model.sharedResources[0].value);
    EXPECT_GT(vertexBuffer.declaration.stride, 0);
    ASSERT_FALSE(vertexBuffer.declaration.elements.empty());
    EXPECT_EQ(vertexBuffer.declaration.elements[0].getVertexElementUsageProperty(),
              Microsoft::Xna::Framework::Graphics::VertexElementUsage::Position);
    EXPECT_EQ(vertexBuffer.declaration.elements[0].getOffsetProperty(), 0);
    EXPECT_EQ(vertexBuffer.bytes.size(),
              static_cast<std::size_t>(vertexBuffer.declaration.stride) *
                  vertexBuffer.vertexCount);
    EXPECT_TRUE(std::holds_alternative<Xnb::XnbIndexBufferData>(
        model.sharedResources[1].value));
    EXPECT_TRUE(std::holds_alternative<Xnb::XnbBasicEffectData>(
        model.sharedResources[2].value));
}

TEST(XnbModelSourceRouteTest, TheVertexDeclarationIsDerivedFromCnasOwnCanonicalStrideTable)
{
    ScratchDirectory scratch("gltf_declaration");
    CopyGltfFixture(scratch, "mode-triangles.glb");

    Pipeline::ContentBuildResult result;
    const Xnb::XnbCanonicalAsset asset =
        BuildGltfModel(scratch, "mode-triangles.glb", result);
    const auto& model = std::get<Xnb::XnbModelData>(asset.value);
    const auto& declaration =
        std::get<Xnb::XnbVertexBufferData>(model.sharedResources[0].value).declaration;

    // CNB Model schema 1 records only a stride. The declaration is recovered from
    // InferredLayoutForStride(), the same table every CNA renderer interprets those bytes with,
    // so the XNB says exactly what the bytes already mean rather than guessing.
    const CNA::Internal::Graphics::InferredVertexLayout expected =
        CNA::Internal::Graphics::InferredLayoutForStride(
            declaration.stride,
            CNA::Internal::Graphics::UnlistedStrideLayout::RendererRefusesIt);
    ASSERT_TRUE(expected.known);
    ASSERT_EQ(declaration.elements.size(), expected.count);
    for (std::size_t index = 0u; index < expected.count; ++index)
    {
        EXPECT_EQ(declaration.elements[index].getOffsetProperty(),
                  expected.elements[index].offset);
        EXPECT_EQ(declaration.elements[index].getVertexElementFormatProperty(),
                  expected.elements[index].format);
        EXPECT_EQ(declaration.elements[index].getVertexElementUsageProperty(),
                  expected.elements[index].usage);
        EXPECT_EQ(declaration.elements[index].getUsageIndexProperty(),
                  expected.elements[index].usageIndex);
    }
}

TEST(XnbModelSourceRouteTest, ANonTriangleTopologyIsRefusedByName)
{
    ScratchDirectory scratch("gltf_lines");
    CopyGltfFixture(scratch, "mode-lines.glb");

    try
    {
        Pipeline::ContentBuildResult result;
        result = Build(scratch.Path(), "mode-lines.glb", "models/lines",
                       Pipeline::ContentOutputFormat::Xnb);
        FAIL() << "a line-list primitive cannot be an XNA Model mesh part";
    }
    catch (const Pipeline::ContentPipelineError& error)
    {
        const std::string message = error.what();
        EXPECT_NE(message.find("triangle list"), std::string::npos) << message;
        EXPECT_EQ(error.Stage(), Pipeline::ContentPipelineStage::Write);
    }
}

TEST(XnbModelSourceRouteTest, APbrMaterialIsDowngradedAndTheLossIsNamed)
{
    ScratchDirectory scratch("gltf_pbr");
    CopyGltfFixture(scratch, "mat-vertex-color-pbr.glb");

    Pipeline::ContentBuildResult result;
    const Xnb::XnbCanonicalAsset asset =
        BuildGltfModel(scratch, "mat-vertex-color-pbr.glb", result);

    const auto& model = std::get<Xnb::XnbModelData>(asset.value);
    ASSERT_EQ(model.sharedResources.size(), 3u);
    EXPECT_TRUE(std::holds_alternative<Xnb::XnbBasicEffectData>(
        model.sharedResources[2].value));

    // The downgrade must be stated, and it must say what was dropped rather than merely that
    // something was.
    ASSERT_FALSE(result.output.warnings.empty());
    const std::string warning = result.output.warnings[0];
    EXPECT_NE(warning.find("downgraded to XNA's BasicEffect"), std::string::npos) << warning;
    EXPECT_NE(warning.find("metallicFactor"), std::string::npos) << warning;
    EXPECT_NE(warning.find("roughnessFactor"), std::string::npos) << warning;

    // A warning is a build-log message, not just a field on the result.
    bool logged = false;
    for (const Pipeline::ContentLogMessage& message : result.messages)
    {
        if (message.level == Pipeline::ContentLogLevel::Warning &&
            message.stage == Pipeline::ContentPipelineStage::Write &&
            message.component == "CNA.XnbModelWriter")
        {
            logged = true;
        }
    }
    EXPECT_TRUE(logged);
}

TEST(XnbModelSourceRouteTest, ASkinnedModelStatesThatItsSkeletonIsNotWritten)
{
    ScratchDirectory scratch("gltf_skin");
    CopyGltfFixture(scratch, "skin-unlit.glb");

    Pipeline::ContentBuildResult result;
    const Xnb::XnbCanonicalAsset asset = BuildGltfModel(scratch, "skin-unlit.glb", result);
    EXPECT_EQ(asset.rootReader, "Microsoft.Xna.Framework.Content.ModelReader");

    bool mentionsSkeleton = false;
    for (const std::string& warning : result.output.warnings)
    {
        if (warning.find("skinning skeleton") != std::string::npos) { mentionsSkeleton = true; }
    }
    EXPECT_TRUE(mentionsSkeleton) << "an XNA Model has no skeleton; dropping one must be stated";
}

TEST(XnbModelSourceRouteTest, TheSameGltfSourceStillBuildsToCnbUnchanged)
{
    ScratchDirectory scratch("gltf_both");
    CopyGltfFixture(scratch, "mode-triangles.glb");

    const Pipeline::ContentBuildResult cnb =
        Build(scratch.Path(), "mode-triangles.glb", "models/asset",
              Pipeline::ContentOutputFormat::Cnb);
    EXPECT_EQ(cnb.writer.name, "CNA.ModelContentWriter");
    ASSERT_GE(cnb.output.bytes.size(), 3u);
    EXPECT_EQ(cnb.output.bytes[0], 'C');
    EXPECT_TRUE(cnb.output.warnings.empty())
        << "CNB carries the whole model, so it has nothing to warn about";
}
