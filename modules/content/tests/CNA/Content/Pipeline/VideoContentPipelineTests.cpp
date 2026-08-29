// SPDX-License-Identifier: MS-PL

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#if !defined(_WIN32)
#include <spawn.h>
#include <sys/wait.h>
extern char** environ;
#endif

#include "CNA/Content/Cnb/CnbDocument.hpp"
#include "CNA/Content/Cnb/CnbMediaCodec.hpp"
#include "CNA/Content/Pipeline/VideoContentPipeline.hpp"
#include "CNA/Internal/ContentPath.hpp"
#include "Microsoft/Xna/Framework/Content/ContentManager.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Media/Video/Video.hpp"
#include "Microsoft/Xna/Framework/Media/VideoSoundtrackType.hpp"

namespace Pipeline = CNA::Content::Pipeline;
namespace Cnb = CNA::Content::Cnb;
using Microsoft::Xna::Framework::Content::ContentManager;
using Microsoft::Xna::Framework::Graphics::GraphicsDevice;
using Microsoft::Xna::Framework::Media::Video;
using Microsoft::Xna::Framework::Media::VideoSoundtrackType;

namespace
{
    class ScratchDirectory
    {
    public:
        explicit ScratchDirectory(const std::string& tag)
            : path_(std::filesystem::temp_directory_path() /
                    ("cna_pipeline_video_" + tag + "_" +
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

    void WriteBytes(const std::filesystem::path& path,
                    const std::vector<std::uint8_t>& bytes)
    {
        if (path.has_parent_path()) { std::filesystem::create_directories(path.parent_path()); }
        std::ofstream stream(path, std::ios::binary);
        stream.write(reinterpret_cast<const char*>(bytes.data()),
                     static_cast<std::streamsize>(bytes.size()));
    }

    std::vector<std::uint8_t> ReadBytes(const std::filesystem::path& path)
    {
        std::ifstream stream(path, std::ios::binary);
        return {std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
    }

    std::shared_ptr<const Pipeline::ContentPipelineRegistry> MakeRegistry()
    {
        auto registry = std::make_shared<Pipeline::ContentPipelineRegistry>();
        Pipeline::RegisterVideoContentPipeline(*registry);
        return registry;
    }

    Pipeline::ContentProcessorParameters VideoParameters()
    {
        Pipeline::ContentProcessorParameters parameters;
        parameters.Set(Pipeline::VideoStreamReferenceParameter,
                       std::string("Movies/intro.mp4"));
        parameters.Set(Pipeline::VideoDurationMsParameter, std::uint64_t{42000u});
        parameters.Set(Pipeline::VideoWidthParameter, std::uint64_t{1920u});
        parameters.Set(Pipeline::VideoHeightParameter, std::uint64_t{1080u});
        parameters.Set(Pipeline::VideoFramesPerSecondParameter, 29.97);
        parameters.Set(Pipeline::VideoSoundtrackTypeParameter, std::uint64_t{2u});
        return parameters;
    }

    Pipeline::ContentBuildResult BuildVideo(
        const std::filesystem::path& root,
        const Pipeline::ContentProcessorParameters& parameters = VideoParameters())
    {
        const Pipeline::ContentPipeline pipeline(MakeRegistry());
        Pipeline::ContentBuildRequest request;
        request.sourceRoot = root;
        request.source = "Movies/intro.mp4";
        request.logicalName = "Movies/intro";
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
        if (posix_spawn(&pid, CNA_SOURCE_TO_CNB_TOOL_PATH, nullptr, nullptr, argv.data(), environ) !=
            0)
        {
            return -1;
        }
        int status = 0;
        if (waitpid(pid, &status, 0) < 0) { return -1; }
        return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
    }
#endif
}

TEST(VideoContentPipelineTest, ComponentsDeclareStableUnambiguousRoutes)
{
    const Pipeline::VideoImporter importer;
    EXPECT_EQ(importer.Identity(),
              (Pipeline::ContentComponentIdentity{"CNA.VideoImporter", "2"}));
    EXPECT_EQ(importer.OutputTypes(),
              std::vector<std::string>{Pipeline::ImportedVideoSourceType});
    const std::vector<std::string> extensions = importer.SourceExtensions();
    EXPECT_NE(std::find(extensions.begin(), extensions.end(), ".mp4"), extensions.end());
    EXPECT_EQ(std::find(extensions.begin(), extensions.end(), ".ogg"), extensions.end());

    const Pipeline::VideoProcessor processor;
    EXPECT_EQ(processor.Identity(),
              (Pipeline::ContentComponentIdentity{"CNA.VideoProcessor", "2"}));
    EXPECT_EQ(processor.InputType(), Pipeline::ImportedVideoSourceType);
    EXPECT_EQ(processor.OutputType(), Pipeline::ProcessedVideoType);

    const Pipeline::VideoContentWriter writer;
    EXPECT_EQ(writer.Identity(),
              (Pipeline::ContentComponentIdentity{"CNA.VideoContentWriter", "1"}));
    EXPECT_EQ(writer.InputType(), Pipeline::ProcessedVideoType);
}

TEST(VideoContentPipelineTest, IsDeterministicAndByteIdenticalToExistingProducers)
{
    ScratchDirectory scratch("oracle");
    const std::filesystem::path media = scratch.Path() / "Movies" / "intro.mp4";
    WriteBytes(media, {0u, 0u, 0u, 24u, 'f', 't', 'y', 'p'});

    const Pipeline::ContentBuildResult first = BuildVideo(scratch.Path());
    const Pipeline::ContentBuildResult second = BuildVideo(scratch.Path());
    EXPECT_EQ(first.output.bytes, second.output.bytes);
    EXPECT_EQ(first.importer,
              (Pipeline::ContentComponentIdentity{"CNA.VideoImporter", "2"}));
    EXPECT_EQ(first.processor,
              (Pipeline::ContentComponentIdentity{"CNA.VideoProcessor", "2"}));
    EXPECT_EQ(first.writer,
              (Pipeline::ContentComponentIdentity{"CNA.VideoContentWriter", "1"}));
    EXPECT_EQ(first.output.assetTypeId, Cnb::CnbAssetTypeId::Video);
    ASSERT_EQ(first.dependencies.size(), 1u);
    EXPECT_EQ(first.dependencies.front().kind,
              Pipeline::ContentDependencyKind::PrimarySource);
    ASSERT_EQ(first.runtimeReferences.size(), 1u);
    EXPECT_EQ(first.runtimeReferences.front().logicalName, "Movies/intro.mp4");
    EXPECT_EQ(first.runtimeReferences.front().expectedAssetTypeId, 0u);
    ASSERT_EQ(first.deploymentFiles.size(), 1u);
    EXPECT_EQ(first.deploymentFiles.front().source, media);
    EXPECT_EQ(first.deploymentFiles.front().outputPath, "Movies/intro.mp4");

    Cnb::CnbVideoData expected;
    expected.streamReference = "Movies/intro.mp4";
    expected.durationMs = 42000u;
    expected.width = 1920u;
    expected.height = 1080u;
    expected.framesPerSecond = 29.97f;
    expected.soundtrackType = 2u;
    EXPECT_EQ(first.output.bytes, Cnb::EncodeVideoToCnb(expected, "Movies/intro"));

#if !defined(_WIN32)
    const std::filesystem::path oldToolOutput = scratch.Path() / "old-tool.cnb";
    ASSERT_EQ(RunSourceTool({media.string(), oldToolOutput.string(), "--as", "video", "--name",
                            "Movies/intro", "--stream", "Movies/intro.mp4", "--duration-ms",
                            "42000", "--frame-size", "1920x1080", "--fps", "29.97",
                            "--soundtrack", "2"}),
              0);
    EXPECT_EQ(first.output.bytes, ReadBytes(oldToolOutput));
#endif

    const Cnb::CnbVideoData decoded = Cnb::DecodeVideoFromCnb(
        Cnb::CnbDocument::Parse(first.output.bytes, "pipeline intro.cnb"));
    EXPECT_EQ(decoded.streamReference, expected.streamReference);
    EXPECT_EQ(decoded.durationMs, expected.durationMs);
    EXPECT_EQ(decoded.width, expected.width);
    EXPECT_EQ(decoded.height, expected.height);
    EXPECT_FLOAT_EQ(decoded.framesPerSecond, expected.framesPerSecond);
    EXPECT_EQ(decoded.soundtrackType, expected.soundtrackType);
}

TEST(VideoContentPipelineTest, RequiresFrameMetadataButDefaultsOptionalMetadata)
{
    const Pipeline::VideoProcessor processor;
    EXPECT_THROW(processor.ValidateParameters({}), std::invalid_argument);

    ScratchDirectory scratch("defaults");
    WriteBytes(scratch.Path() / "Movies" / "intro.mp4", {1u});
    Pipeline::ContentProcessorParameters parameters;
    parameters.Set(Pipeline::VideoWidthParameter, std::uint64_t{640u});
    parameters.Set(Pipeline::VideoHeightParameter, std::uint64_t{360u});
    parameters.Set(Pipeline::VideoFramesPerSecondParameter, 24.0);
    const Pipeline::ContentBuildResult result = BuildVideo(scratch.Path(), parameters);
    const Cnb::CnbVideoData decoded = Cnb::DecodeVideoFromCnb(
        Cnb::CnbDocument::Parse(result.output.bytes, "default video"));
    EXPECT_EQ(decoded.streamReference, "Movies/intro.mp4");
    EXPECT_EQ(decoded.durationMs, 0u);
    EXPECT_EQ(decoded.soundtrackType, 0u);
}

TEST(VideoContentPipelineTest, ProducedBytesLoadThroughRuntimeWithEquivalentMetadata)
{
    ScratchDirectory scratch("runtime");
    WriteBytes(scratch.Path() / "Movies" / "intro.mp4",
               {0u, 0u, 0u, 24u, 'f', 't', 'y', 'p'});
    const Pipeline::ContentBuildResult result = BuildVideo(scratch.Path());
    WriteBytes(scratch.Path() / "Movies" / "intro.cnb", result.output.bytes);

    GraphicsDevice graphicsDevice;
    ContentManager content(nullptr, scratch.Path().string());
    content.setGraphicsDevice(graphicsDevice);
    const Video video = content.Load<Video>("Movies/intro");
    EXPECT_EQ(video.getWidthProperty(), 1920);
    EXPECT_EQ(video.getHeightProperty(), 1080);
    EXPECT_FLOAT_EQ(video.getFramesPerSecondProperty(), 29.97f);
    EXPECT_EQ(video.getVideoSoundtrackTypeProperty(), VideoSoundtrackType::MusicAndDialog);
    EXPECT_NEAR(video.getDurationProperty().getTotalMillisecondsProperty(), 42000.0, 1.0);
}

TEST(VideoContentPipelineTest, RejectsInvalidParametersAndEmptySourcesAtTheirStages)
{
    const Pipeline::VideoProcessor processor;
    Pipeline::ContentProcessorParameters invalid = VideoParameters();
    invalid.Set("codec", std::string("h264"));
    EXPECT_THROW(processor.ValidateParameters(invalid), std::invalid_argument);
    invalid = VideoParameters();
    invalid.Set(Pipeline::VideoWidthParameter, std::uint64_t{0u});
    EXPECT_THROW(processor.ValidateParameters(invalid), std::invalid_argument);
    invalid = VideoParameters();
    invalid.Set(Pipeline::VideoHeightParameter,
                std::uint64_t{Cnb::CnbMaxVideoDimension + 1ull});
    EXPECT_THROW(processor.ValidateParameters(invalid), std::invalid_argument);
    invalid = VideoParameters();
    invalid.Set(Pipeline::VideoFramesPerSecondParameter, std::string("29.97"));
    EXPECT_THROW(processor.ValidateParameters(invalid), std::invalid_argument);
    invalid = VideoParameters();
    invalid.Set(Pipeline::VideoSoundtrackTypeParameter, std::uint64_t{3u});
    EXPECT_THROW(processor.ValidateParameters(invalid), std::invalid_argument);
    invalid = VideoParameters();
    invalid.Set(Pipeline::VideoStreamReferenceParameter, std::string("../escape.mp4"));
    EXPECT_THROW(processor.ValidateParameters(invalid), std::invalid_argument);

    ScratchDirectory scratch("empty");
    WriteBytes(scratch.Path() / "Movies" / "intro.mp4", {});
    try
    {
        static_cast<void>(BuildVideo(scratch.Path()));
        FAIL() << "empty streaming media should fail";
    }
    catch (const Pipeline::ContentPipelineError& error)
    {
        EXPECT_EQ(error.Stage(), Pipeline::ContentPipelineStage::Import);
        EXPECT_EQ(error.Component(), "CNA.VideoImporter");
        EXPECT_NE(std::string(error.what()).find("must not be empty"), std::string::npos);
    }
}

TEST(VideoContentPipelineTest, PreservesNativeUnicodePathsUntilTheXrefBoundary)
{
    ScratchDirectory scratch("unicode");
    const std::filesystem::path source =
        scratch.Path() / std::filesystem::path(u8"Filmy/úvod_映像.mp4");
    WriteBytes(source, {1u, 2u, 3u});

    const Pipeline::ContentPipeline pipeline(MakeRegistry());
    Pipeline::ContentBuildRequest request;
    request.sourceRoot = scratch.Path();
    request.source = source;
    request.logicalName = "Filmy/úvod_映像";
    request.parameters = VideoParameters();
    request.parameters.Set(Pipeline::VideoStreamReferenceParameter,
                           std::string("Filmy/úvod_映像.mp4"));
    const Pipeline::ContentBuildResult result = pipeline.Build(request);
    EXPECT_EQ(Cnb::DecodeVideoFromCnb(Cnb::CnbDocument::Parse(result.output.bytes, "unicode"))
                  .streamReference,
              "Filmy/úvod_映像.mp4");
    EXPECT_EQ(result.dependencies.front().identity,
              CNA::Internal::ContentPathToUtf8(std::filesystem::weakly_canonical(source)));
}
