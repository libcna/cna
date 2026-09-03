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
#include "CNA/Content/Cnb/CnbTextureCodec.hpp"
#include "CNA/Content/Pipeline/ContentBuildConfiguration.hpp"
#include "CNA/Content/Pipeline/ModelContentPipeline.hpp"
#include "CNA/Content/Pipeline/SoundEffectContentPipeline.hpp"
#include "CNA/Content/Pipeline/Texture2DContentPipeline.hpp"
#include "CNA/Content/Pipeline/XnbOutputContentPipeline.hpp"
#include "CNA/Internal/Graphics/ImageLoader.hpp"
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

TEST(XnbOutputContentPipelineTest, ASchemaOneModelIsRefusedWithAnActionableDiagnostic)
{
    // CNB Model schema 1 records a vertex stride but no VertexDeclaration, which an XNA Model
    // cannot do without. The refusal has to say that, and say where the fix is tracked.
    Pipeline::ProcessedModelBundle bundle;
    bundle.primary = Cnb::CnbModelData{};

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
        FAIL() << "a schema-1 Model must be refused for XNB output";
    }
    catch (const Xnb::XnbWriteException& error)
    {
        const std::string message = error.what();
        EXPECT_NE(message.find("VertexDeclaration"), std::string::npos) << message;
        EXPECT_NE(message.find("XNAP-56"), std::string::npos) << message;
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
