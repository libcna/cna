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
#include "CNA/Content/Pipeline/SongContentPipeline.hpp"
#include "CNA/Internal/ContentPath.hpp"
#include "Microsoft/Xna/Framework/Content/ContentManager.hpp"
#include "Microsoft/Xna/Framework/Media/Song.hpp"

namespace Pipeline = CNA::Content::Pipeline;
namespace Cnb = CNA::Content::Cnb;
using Microsoft::Xna::Framework::Content::ContentManager;

namespace
{
    class ScratchDirectory
    {
    public:
        explicit ScratchDirectory(const std::string& tag)
            : path_(std::filesystem::temp_directory_path() /
                    ("cna_pipeline_song_" + tag + "_" +
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
        Pipeline::RegisterSongContentPipeline(*registry);
        return registry;
    }

    Pipeline::ContentProcessorParameters SongParameters()
    {
        Pipeline::ContentProcessorParameters parameters;
        parameters.Set(Pipeline::SongStreamReferenceParameter,
                       std::string("Music/theme.ogg"));
        parameters.Set(Pipeline::SongNameParameter, std::string("Main Theme"));
        parameters.Set(Pipeline::SongDurationMsParameter, std::uint64_t{185000u});
        return parameters;
    }

    Pipeline::ContentBuildResult BuildSong(
        const std::filesystem::path& root,
        const Pipeline::ContentProcessorParameters& parameters = SongParameters())
    {
        const Pipeline::ContentPipeline pipeline(MakeRegistry());
        Pipeline::ContentBuildRequest request;
        request.sourceRoot = root;
        request.source = "Music/theme.ogg";
        request.logicalName = "Music/theme";
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

TEST(SongContentPipelineTest, ComponentsDeclareStableUnambiguousRoutes)
{
    const Pipeline::SongImporter importer;
    EXPECT_EQ(importer.Identity(),
              (Pipeline::ContentComponentIdentity{"CNA.SongImporter", "2"}));
    EXPECT_EQ(importer.OutputTypes(),
              std::vector<std::string>{Pipeline::ImportedSongSourceType});
    const std::vector<std::string> extensions = importer.SourceExtensions();
    EXPECT_NE(std::find(extensions.begin(), extensions.end(), ".ogg"), extensions.end());
    EXPECT_EQ(std::find(extensions.begin(), extensions.end(), ".wav"), extensions.end());

    const Pipeline::SongProcessor processor;
    EXPECT_EQ(processor.Identity(),
              (Pipeline::ContentComponentIdentity{"CNA.SongProcessor", "2"}));
    EXPECT_EQ(processor.InputType(), Pipeline::ImportedSongSourceType);
    EXPECT_EQ(processor.OutputType(), Pipeline::ProcessedSongType);

    const Pipeline::SongContentWriter writer;
    EXPECT_EQ(writer.Identity(),
              (Pipeline::ContentComponentIdentity{"CNA.SongContentWriter", "1"}));
    EXPECT_EQ(writer.InputType(), Pipeline::ProcessedSongType);
}

TEST(SongContentPipelineTest, IsDeterministicAndByteIdenticalToExistingProducers)
{
    ScratchDirectory scratch("oracle");
    const std::filesystem::path media = scratch.Path() / "Music" / "theme.ogg";
    WriteBytes(media, {0x4Fu, 0x67u, 0x67u, 0x53u, 1u, 2u, 3u});

    const Pipeline::ContentBuildResult first = BuildSong(scratch.Path());
    const Pipeline::ContentBuildResult second = BuildSong(scratch.Path());
    EXPECT_EQ(first.output.bytes, second.output.bytes);
    EXPECT_EQ(first.importer,
              (Pipeline::ContentComponentIdentity{"CNA.SongImporter", "2"}));
    EXPECT_EQ(first.processor,
              (Pipeline::ContentComponentIdentity{"CNA.SongProcessor", "2"}));
    EXPECT_EQ(first.writer,
              (Pipeline::ContentComponentIdentity{"CNA.SongContentWriter", "1"}));
    EXPECT_EQ(first.output.assetTypeId, Cnb::CnbAssetTypeId::Song);
    ASSERT_EQ(first.dependencies.size(), 1u);
    EXPECT_EQ(first.dependencies.front().kind,
              Pipeline::ContentDependencyKind::PrimarySource);
    ASSERT_EQ(first.runtimeReferences.size(), 1u);
    EXPECT_EQ(first.runtimeReferences.front().logicalName, "Music/theme.ogg");
    EXPECT_EQ(first.runtimeReferences.front().expectedAssetTypeId, 0u);
    ASSERT_EQ(first.deploymentFiles.size(), 1u);
    EXPECT_EQ(first.deploymentFiles.front().source,
              std::filesystem::weakly_canonical(media));
    EXPECT_EQ(first.deploymentFiles.front().outputPath, "Music/theme.ogg");

    Cnb::CnbSongData expected;
    expected.streamReference = "Music/theme.ogg";
    expected.name = "Main Theme";
    expected.durationMs = 185000u;
    EXPECT_EQ(first.output.bytes, Cnb::EncodeSongToCnb(expected, "Music/theme"));

#if !defined(_WIN32)
    const std::filesystem::path oldToolOutput = scratch.Path() / "old-tool.cnb";
    ASSERT_EQ(RunSourceTool({media.string(), oldToolOutput.string(), "--as", "song", "--name",
                            "Music/theme", "--stream", "Music/theme.ogg", "--title",
                            "Main Theme", "--duration-ms", "185000"}),
              0);
    EXPECT_EQ(first.output.bytes, ReadBytes(oldToolOutput));
#endif

    const Cnb::CnbDocument document =
        Cnb::CnbDocument::Parse(first.output.bytes, "pipeline theme.cnb");
    EXPECT_EQ(document.Metadata().contentName, "Music/theme");
    EXPECT_EQ(Cnb::DecodeSongFromCnb(document).streamReference, "Music/theme.ogg");
}

TEST(SongContentPipelineTest, DefaultsToTheRootRelativeSourceAndUnknownMetadata)
{
    ScratchDirectory scratch("defaults");
    WriteBytes(scratch.Path() / "Music" / "theme.ogg", {1u});
    const Pipeline::ContentProcessorParameters parameters;
    const Pipeline::ContentBuildResult result = BuildSong(scratch.Path(), parameters);
    const Cnb::CnbSongData decoded = Cnb::DecodeSongFromCnb(
        Cnb::CnbDocument::Parse(result.output.bytes, "default song"));
    EXPECT_EQ(decoded.streamReference, "Music/theme.ogg");
    EXPECT_TRUE(decoded.name.empty());
    EXPECT_EQ(decoded.durationMs, 0u);
}

TEST(SongContentPipelineTest, ProducedBytesLoadThroughTheRuntimeWithEquivalentMetadata)
{
    ScratchDirectory scratch("runtime");
    const std::filesystem::path media = scratch.Path() / "Music" / "theme.ogg";
    WriteBytes(media, {0x4Fu, 0x67u, 0x67u, 0x53u});
    const Pipeline::ContentBuildResult result = BuildSong(scratch.Path());
    WriteBytes(scratch.Path() / "Music" / "theme.cnb", result.output.bytes);

    ContentManager content(nullptr, scratch.Path().string());
    const auto song = content.Load<Microsoft::Xna::Framework::Media::Song>("Music/theme");
    EXPECT_EQ(song.getNameProperty(), "Main Theme");
    EXPECT_NEAR(song.getDurationProperty().getTotalMillisecondsProperty(), 185000.0, 1.0);
}

TEST(SongContentPipelineTest, RejectsInvalidParametersAndEmptySourcesAtTheirStages)
{
    const Pipeline::SongProcessor processor;
    Pipeline::ContentProcessorParameters invalid;
    invalid.Set("sampleRate", std::uint64_t{44100u});
    EXPECT_THROW(processor.ValidateParameters(invalid), std::invalid_argument);
    invalid = {};
    invalid.Set(Pipeline::SongNameParameter, std::uint64_t{1u});
    EXPECT_THROW(processor.ValidateParameters(invalid), std::invalid_argument);
    invalid = {};
    invalid.Set(Pipeline::SongDurationMsParameter, std::uint64_t{2147483648ull});
    EXPECT_THROW(processor.ValidateParameters(invalid), std::invalid_argument);
    invalid = {};
    invalid.Set(Pipeline::SongStreamReferenceParameter, std::string("../escape.ogg"));
    EXPECT_THROW(processor.ValidateParameters(invalid), std::invalid_argument);

    ScratchDirectory scratch("empty");
    WriteBytes(scratch.Path() / "Music" / "theme.ogg", {});
    try
    {
        static_cast<void>(BuildSong(scratch.Path(), {}));
        FAIL() << "empty streaming media should fail";
    }
    catch (const Pipeline::ContentPipelineError& error)
    {
        EXPECT_EQ(error.Stage(), Pipeline::ContentPipelineStage::Import);
        EXPECT_EQ(error.Component(), "CNA.SongImporter");
        EXPECT_NE(std::string(error.what()).find("must not be empty"), std::string::npos);
    }
}

TEST(SongContentPipelineTest, PreservesNativeUnicodePathsUntilTheXrefBoundary)
{
    ScratchDirectory scratch("unicode");
    const std::filesystem::path source =
        scratch.Path() / std::filesystem::path(u8"Hudba/žluťoučká_音.ogg");
    WriteBytes(source, {1u, 2u, 3u});

    const Pipeline::ContentPipeline pipeline(MakeRegistry());
    Pipeline::ContentBuildRequest request;
    request.sourceRoot = scratch.Path();
    request.source = source;
    request.logicalName = "Hudba/žluťoučká_音";
    const Pipeline::ContentBuildResult result = pipeline.Build(request);
    EXPECT_EQ(Cnb::DecodeSongFromCnb(Cnb::CnbDocument::Parse(result.output.bytes, "unicode"))
                  .streamReference,
              "Hudba/žluťoučká_音.ogg");
    EXPECT_EQ(result.dependencies.front().identity,
              CNA::Internal::ContentPathToUtf8(std::filesystem::weakly_canonical(source)));
}
