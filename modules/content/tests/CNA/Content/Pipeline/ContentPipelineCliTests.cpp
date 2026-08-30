// SPDX-License-Identifier: MS-PL

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include <gtest/gtest.h>
#include <fcntl.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>

#include "CNA/Content/Cnb/CnbDocument.hpp"
#include "CNA/Content/Cnb/CnbMediaCodec.hpp"
#include "CNA/Content/Cnb/CnbTextureCodec.hpp"
#include "CNA/Content/Pipeline/ContentBuildConfiguration.hpp"
#include "CNA/Content/Pipeline/ContentBuildManifest.hpp"
#include "CNA/Content/Pipeline/ContentPipeline.hpp"
#include "CNA/Content/Pipeline/Texture2DContentPipeline.hpp"
#include "CNA/Internal/ContentPath.hpp"
#include "CNA/Internal/Graphics/ImageLoader.hpp"
#include "CnaContentStaging.hpp"

extern char** environ;

namespace Pipeline = CNA::Content::Pipeline;
namespace Cnb = CNA::Content::Cnb;

namespace
{
    class ScratchDirectory
    {
    public:
        explicit ScratchDirectory(const std::string& tag)
            : path_(std::filesystem::temp_directory_path() /
                    ("cna_content_cli_" + tag + "_" +
                     std::to_string(::getpid()) + "_" +
                     std::to_string(reinterpret_cast<std::uintptr_t>(this))))
        {
            std::error_code error;
            std::filesystem::remove_all(path_, error);
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

    void WriteText(const std::filesystem::path& path, const std::string& text)
    {
        WriteBytes(path, std::vector<std::uint8_t>(text.begin(), text.end()));
    }

    std::vector<std::uint8_t> ReadBytes(const std::filesystem::path& path)
    {
        std::ifstream stream(path, std::ios::binary);
        return {std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
    }

    using FileTreeSnapshot = std::map<std::string, std::vector<std::uint8_t>>;

    FileTreeSnapshot SnapshotFileTree(const std::filesystem::path& root)
    {
        FileTreeSnapshot snapshot;
        if (!std::filesystem::exists(root)) { return snapshot; }
        for (const std::filesystem::directory_entry& entry :
             std::filesystem::recursive_directory_iterator(root))
        {
            if (!entry.is_regular_file()) { continue; }
            snapshot.emplace(
                CNA::Internal::ContentPathToUtf8(
                    std::filesystem::relative(entry.path(), root)),
                ReadBytes(entry.path()));
        }
        return snapshot;
    }

    void RestoreFileTree(const std::filesystem::path& root,
                         const FileTreeSnapshot& snapshot)
    {
        std::error_code error;
        std::filesystem::remove_all(root, error);
        ASSERT_FALSE(error);
        for (const auto& [relative, bytes] : snapshot)
        {
            WriteBytes(root / CNA::Internal::ContentPathFromUtf8(relative), bytes);
        }
    }

    std::size_t CountOccurrences(const std::string& text, const std::string& needle)
    {
        std::size_t count = 0u;
        std::size_t position = 0u;
        while ((position = text.find(needle, position)) != std::string::npos)
        {
            ++count;
            position += needle.size();
        }
        return count;
    }

    std::vector<std::uint8_t> MakePng(int width, int height)
    {
        std::vector<std::uint8_t> pixels(static_cast<std::size_t>(width) * height * 4u);
        for (std::size_t index = 0u; index < pixels.size(); ++index)
        {
            pixels[index] = static_cast<std::uint8_t>(index * 17u + 3u);
        }
        return CNA::Internal::Graphics::ImageLoader::EncodePng(
            pixels.data(), width, height, width, height);
    }

    std::vector<std::uint8_t> MakeWav()
    {
        std::vector<std::uint8_t> output;
        const auto writeU16 = [&](std::uint16_t value)
        {
            output.push_back(static_cast<std::uint8_t>(value & 0xFFu));
            output.push_back(static_cast<std::uint8_t>(value >> 8u));
        };
        const auto writeU32 = [&](std::uint32_t value)
        {
            for (int byte = 0; byte < 4; ++byte)
            {
                output.push_back(static_cast<std::uint8_t>(value >> (byte * 8)));
            }
        };
        const auto writeTag = [&](const char* tag)
        {
            for (int index = 0; index < 4; ++index)
            {
                output.push_back(static_cast<std::uint8_t>(tag[index]));
            }
        };

        std::vector<std::uint8_t> samples(40u);
        for (std::size_t index = 0u; index < samples.size(); ++index)
        {
            samples[index] = static_cast<std::uint8_t>(index * 9u);
        }
        writeTag("RIFF");
        writeU32(4u + 24u + 8u + static_cast<std::uint32_t>(samples.size()));
        writeTag("WAVE");
        writeTag("fmt ");
        writeU32(16u);
        writeU16(1u);
        writeU16(1u);
        writeU32(22050u);
        writeU32(44100u);
        writeU16(2u);
        writeU16(16u);
        writeTag("data");
        writeU32(static_cast<std::uint32_t>(samples.size()));
        output.insert(output.end(), samples.begin(), samples.end());
        return output;
    }

    std::filesystem::path FindGltfFixture(const std::string& name)
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

    std::filesystem::path FindXnbFixture(const std::string& relative)
    {
        for (const char* prefix : {"tests/assets/xnb/", "../tests/assets/xnb/",
                                   "../../tests/assets/xnb/"})
        {
            const std::filesystem::path candidate = std::string(prefix) + relative;
            if (std::filesystem::exists(candidate))
            {
                return std::filesystem::weakly_canonical(candidate);
            }
        }
        return {};
    }

    int RunExecutable(const char* executable, const std::vector<std::string>& arguments,
                      std::string& output)
    {
        const std::filesystem::path capture =
            std::filesystem::temp_directory_path() /
            ("cna_content_cli_capture_" + std::to_string(::getpid()) + "_" +
             std::to_string(reinterpret_cast<std::uintptr_t>(&arguments)));

        posix_spawn_file_actions_t actions;
        posix_spawn_file_actions_init(&actions);
        posix_spawn_file_actions_addopen(&actions, STDOUT_FILENO, capture.c_str(),
                                         O_WRONLY | O_CREAT | O_TRUNC, 0644);
        posix_spawn_file_actions_adddup2(&actions, STDOUT_FILENO, STDERR_FILENO);

        std::vector<char*> argv;
        argv.push_back(const_cast<char*>(executable));
        for (const std::string& argument : arguments)
        {
            argv.push_back(const_cast<char*>(argument.c_str()));
        }
        argv.push_back(nullptr);

        pid_t pid = -1;
        const int spawnResult = posix_spawn(&pid, executable, &actions, nullptr,
                                            argv.data(), environ);
        posix_spawn_file_actions_destroy(&actions);
        if (spawnResult != 0)
        {
            ADD_FAILURE() << "posix_spawn failed: " << std::strerror(spawnResult);
            return -1;
        }
        int status = 0;
        waitpid(pid, &status, 0);

        std::ifstream stream(capture, std::ios::binary);
        std::ostringstream text;
        text << stream.rdbuf();
        output = text.str();
        stream.close();
        std::error_code error;
        std::filesystem::remove(capture, error);
        return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
    }

    int RunTool(const std::vector<std::string>& arguments, std::string& output)
    {
        return RunExecutable(CNA_CONTENT_TOOL_PATH, arguments, output);
    }

    int RunTool(const std::vector<std::string>& arguments)
    {
        std::string ignored;
        return RunTool(arguments, ignored);
    }

    std::vector<std::filesystem::path> TemporaryFilesBeside(
        const std::filesystem::path& output)
    {
        std::vector<std::filesystem::path> matches;
        const std::string prefix = output.filename().string() + ".cnatmp-";
        for (const std::filesystem::directory_entry& entry :
             std::filesystem::directory_iterator(output.parent_path()))
        {
            if (entry.path().filename().string().starts_with(prefix))
            {
                matches.push_back(entry.path());
            }
        }
        return matches;
    }
}

TEST(ContentPipelineCliTest, StagingScavengerRemovesOnlyOldUnlockedValidatedDirectories)
{
    ScratchDirectory scratch("staging_scavenger");
    const std::filesystem::path parent = scratch.Path() / "temporary";
    const std::filesystem::path authoredSource = scratch.Path() / "ContentSource";
    std::filesystem::create_directories(parent);
    WriteText(authoredSource / "keep.txt", "user-authored\n");

    const std::int64_t now = 2'000'000;
    const std::int64_t old = now - CNA::Tools::ContentStagingMinimumAgeSeconds - 1;
    CNA::Tools::ContentBuildStagingDirectory active(parent, old);

    const auto createCandidate = [&](const std::string& pid, const std::string& token,
                                     const std::string& attempt, const std::int64_t created)
    {
        const CNA::Tools::ContentStagingDetail::CandidateIdentity identity{
            pid, token, attempt};
        const std::string name = std::string(CNA::Tools::ContentStagingDirectoryPrefix) +
                                 pid + "_" + token + "_" + attempt;
        const std::filesystem::path path = parent / name;
        std::filesystem::create_directory(path);
        WriteText(
            path / CNA::Tools::ContentStagingMetadataFile,
            CNA::Tools::ContentStagingDetail::MetadataText(name, identity, created));
        WriteText(path / CNA::Tools::ContentStagingLeaseFile, "");
        WriteText(path / "0.cnb", "staged bytes");
        return path;
    };

    const std::filesystem::path stale = createCandidate(
        CNA::Tools::Detail::ProcessTag(), "0000000000000001", "0", old);
    const std::filesystem::path recent = createCandidate(
        "999991", "0000000000000002", "0", now - 5);
    const std::filesystem::path malformed =
        parent / "cna_content_stage_v1_999992_0000000000000003_0";
    std::filesystem::create_directory(malformed);
    WriteText(malformed / CNA::Tools::ContentStagingMetadataFile, "not pipeline metadata\n");
    WriteText(malformed / CNA::Tools::ContentStagingLeaseFile, "");
    const std::filesystem::path malformedName =
        parent / "cna_content_stage_v1_not-a-valid-session";
    std::filesystem::create_directory(malformedName);

    const std::filesystem::path symlinkCandidate =
        parent / "cna_content_stage_v1_999993_0000000000000004_0";
    std::error_code symlinkError;
    std::filesystem::create_directory_symlink(
        authoredSource, symlinkCandidate, symlinkError);
    WriteText(parent / "ordinary-user-file.txt", "keep\n");

    const CNA::Tools::ContentStagingScavengeResult result =
        CNA::Tools::ScavengeContentStagingDirectories(parent, now);
    EXPECT_EQ(result.removedDirectories, 1u);
    EXPECT_EQ(result.activeDirectories, 1u);
    EXPECT_EQ(result.recentDirectories, 1u);
    EXPECT_GE(result.conservativeSkips, symlinkError ? 2u : 3u);
    EXPECT_FALSE(std::filesystem::exists(stale));
    EXPECT_TRUE(std::filesystem::exists(active.Path()));
    EXPECT_TRUE(std::filesystem::exists(recent));
    EXPECT_TRUE(std::filesystem::exists(malformed));
    EXPECT_TRUE(std::filesystem::exists(malformedName));
    if (!symlinkError) { EXPECT_TRUE(std::filesystem::is_symlink(symlinkCandidate)); }
    EXPECT_EQ(ReadBytes(authoredSource / "keep.txt"),
              (std::vector<std::uint8_t>{'u', 's', 'e', 'r', '-', 'a', 'u', 't', 'h', 'o',
                                         'r', 'e', 'd', '\n'}));
    EXPECT_TRUE(std::filesystem::exists(parent / "ordinary-user-file.txt"));
    EXPECT_TRUE(std::is_sorted(result.diagnostics.begin(), result.diagnostics.end()));
}

TEST(ContentPipelineCliTest, StagingScavengingIsBoundedAndNormalLifetimeCleansUp)
{
    ScratchDirectory scratch("staging_bounded");
    const std::filesystem::path parent = scratch.Path() / "temporary";
    std::filesystem::create_directories(parent);
    const std::int64_t now = 3'000'000;
    const std::int64_t old = now - CNA::Tools::ContentStagingMinimumAgeSeconds - 1;
    for (int candidate = 0; candidate < 3; ++candidate)
    {
        const std::string token = "000000000000000" + std::to_string(candidate + 1);
        const CNA::Tools::ContentStagingDetail::CandidateIdentity identity{
            "999994", token, "0"};
        const std::string name = std::string(CNA::Tools::ContentStagingDirectoryPrefix) +
                                 identity.pid + "_" + identity.token + "_" + identity.attempt;
        const std::filesystem::path path = parent / name;
        std::filesystem::create_directory(path);
        WriteText(
            path / CNA::Tools::ContentStagingMetadataFile,
            CNA::Tools::ContentStagingDetail::MetadataText(name, identity, old));
        WriteText(path / CNA::Tools::ContentStagingLeaseFile, "");
    }

    const CNA::Tools::ContentStagingScavengeResult bounded =
        CNA::Tools::ScavengeContentStagingDirectories(parent, now, 1, 4096u, 1u);
    EXPECT_EQ(bounded.removedDirectories, 1u);
    EXPECT_TRUE(bounded.scanLimitReached);
    EXPECT_EQ(std::ranges::count_if(
                  std::filesystem::directory_iterator(parent), [](const auto& entry)
                  {
                      return entry.path().filename().string().starts_with(
                          CNA::Tools::ContentStagingDirectoryPrefix);
                  }),
              2);

    std::filesystem::path normalPath;
    {
        CNA::Tools::ContentBuildStagingDirectory normal(parent, now);
        normalPath = normal.Path();
        EXPECT_TRUE(std::filesystem::is_directory(normalPath));
        EXPECT_TRUE(std::filesystem::is_regular_file(
            normalPath / CNA::Tools::ContentStagingMetadataFile));
        EXPECT_TRUE(std::filesystem::is_regular_file(
            normalPath / CNA::Tools::ContentStagingLeaseFile));
    }
    EXPECT_FALSE(std::filesystem::exists(normalPath));
}

TEST(ContentPipelineCliTest, SingleAssetBuildPublishesThePipelineBytesAtomically)
{
    ScratchDirectory scratch("single");
    const std::filesystem::path source = scratch.Path() / "wall.png";
    const std::filesystem::path output = scratch.Path() / "nested" / "wall.cnb";
    WriteBytes(source, MakePng(4, 3));

    ASSERT_EQ(RunTool({"build", source.string(), "-o", output.string(), "--quiet"}), 0);
    ASSERT_TRUE(std::filesystem::is_regular_file(output));
    EXPECT_TRUE(TemporaryFilesBeside(output).empty());

    auto registry = std::make_shared<Pipeline::ContentPipelineRegistry>();
    Pipeline::RegisterTexture2DContentPipeline(*registry);
    const Pipeline::ContentPipeline pipeline(registry);
    Pipeline::ContentBuildRequest request;
    request.sourceRoot = scratch.Path();
    request.source = source;
    request.logicalName = "wall";
    EXPECT_EQ(ReadBytes(output), pipeline.Build(request).output.bytes);
}

TEST(ContentPipelineCliTest, XnbSingleFileUsesTheOrdinaryIncrementalAndOutputVerificationPath)
{
    const std::filesystem::path fixture =
        FindXnbFixture("monogame/windows/uncompressed/white-1.xnb");
    ASSERT_FALSE(fixture.empty());
    ScratchDirectory scratch("xnb_single");
    const std::filesystem::path source = scratch.Path() / "legacy_texture.xnb";
    const std::filesystem::path output = scratch.Path() / "legacy_texture.cnb";
    WriteBytes(source, ReadBytes(fixture));

    std::string first;
    ASSERT_EQ(RunTool({"build", source.string(), "-o", output.string()}, first), 0) << first;
    EXPECT_NE(first.find("[BUILD] legacy_texture"), std::string::npos) << first;
    const std::vector<std::uint8_t> expected = ReadBytes(output);
    EXPECT_EQ(Cnb::CnbDocument::Parse(expected, "XNB CLI output").AssetTypeId(),
              Cnb::CnbAssetTypeId::Texture2D);

    std::string second;
    ASSERT_EQ(RunTool({"build", source.string(), "-o", output.string()}, second), 0) << second;
    EXPECT_NE(second.find("[SKIP] legacy_texture"), std::string::npos) << second;

    WriteBytes(output, {'b', 'a', 'd'});
    std::string repaired;
    ASSERT_EQ(RunTool({"build", source.string(), "-o", output.string(), "--explain"}, repaired), 0)
        << repaired;
    EXPECT_NE(repaired.find("[BUILD] legacy_texture"), std::string::npos) << repaired;
    EXPECT_EQ(ReadBytes(output), expected);

    WriteText(
        scratch.Path() / Pipeline::ContentBuildConfigurationFileName,
        R"json({"format":"CNA.ContentPipeline.Config","version":1,"assets":{"legacy_texture.xnb":{"parameters":{"colorKey":{"type":"string","value":"255,255,255"}}}}})json");
    std::string configured;
    ASSERT_EQ(RunTool({"build", source.string(), "-o", output.string()}, configured), 0)
        << configured;
    EXPECT_NE(configured.find("[BUILD] legacy_texture"), std::string::npos) << configured;
    const Cnb::CnbTextureData keyed = Cnb::DecodeTexture2DFromCnb(
        Cnb::CnbDocument::Parse(ReadBytes(output), "configured XNB CLI output"));
    ASSERT_EQ(keyed.representations.size(), 1u);
    ASSERT_EQ(keyed.representations[0].levels.size(), 1u);
    ASSERT_EQ(keyed.representations[0].levels[0].size(), 4u);
    EXPECT_EQ(keyed.representations[0].levels[0][3], 0u);

    std::string configuredNoOp;
    ASSERT_EQ(RunTool({"build", source.string(), "-o", output.string()}, configuredNoOp), 0)
        << configuredNoOp;
    EXPECT_NE(configuredNoOp.find("[SKIP] legacy_texture"), std::string::npos)
        << configuredNoOp;
}

TEST(ContentPipelineCliTest, XnbDirectoryBuildIsDeterministicAcrossWorkerCountsAndRebuildsChanges)
{
    const std::filesystem::path texture =
        FindXnbFixture("monogame/windows/uncompressed/white-1.xnb");
    const std::filesystem::path changedTexture =
        FindXnbFixture("monogame/windows/lzx/Explosion.xnb");
    const std::filesystem::path font =
        FindXnbFixture("monogame/windows/uncompressed/Default.xnb");
    const std::filesystem::path sound = FindXnbFixture(
        "monogame/windows/uncompressed/audio/tone_mono_44khz_16bit.xnb");
    ASSERT_FALSE(texture.empty());
    ASSERT_FALSE(changedTexture.empty());
    ASSERT_FALSE(font.empty());
    ASSERT_FALSE(sound.empty());

    ScratchDirectory scratch("xnb_directory");
    const std::filesystem::path source = scratch.Path() / "ContentSource";
    WriteBytes(source / "legacy" / "texture.xnb", ReadBytes(texture));
    WriteBytes(source / "legacy" / "font.xnb", ReadBytes(font));
    WriteBytes(source / "legacy" / "sound.xnb", ReadBytes(sound));

    FileTreeSnapshot reference;
    for (const std::size_t workers : {1u, 2u, 4u})
    {
        const std::filesystem::path output =
            scratch.Path() / ("Content" + std::to_string(workers));
        std::string log;
        ASSERT_EQ(RunTool({"build", source.string(), "-o", output.string(), "--workers",
                           std::to_string(workers)}, log),
                  0)
            << log;
        EXPECT_NE(log.find("Built: 3  Skipped: 0  Failed: 0"), std::string::npos) << log;
        EXPECT_TRUE(std::filesystem::is_regular_file(output / "legacy" / "texture.cnb"));
        EXPECT_TRUE(std::filesystem::is_regular_file(output / "legacy" / "font.cnb"));
        EXPECT_TRUE(std::filesystem::is_regular_file(output / "legacy" / "sound.cnb"));
        if (reference.empty()) { reference = SnapshotFileTree(output); }
        else { EXPECT_EQ(SnapshotFileTree(output), reference); }
    }

    const std::filesystem::path output = scratch.Path() / "Content4";
    std::string noOp;
    ASSERT_EQ(RunTool({"build", source.string(), "-o", output.string(), "--workers", "4"}, noOp),
              0)
        << noOp;
    EXPECT_NE(noOp.find("Built: 0  Skipped: 3  Failed: 0"), std::string::npos) << noOp;

    WriteBytes(source / "legacy" / "texture.xnb", ReadBytes(changedTexture));
    std::string changed;
    ASSERT_EQ(RunTool({"build", source.string(), "-o", output.string(), "--workers", "4"}, changed),
              0)
        << changed;
    EXPECT_NE(changed.find("[BUILD] legacy/texture"), std::string::npos) << changed;
    EXPECT_NE(changed.find("[SKIP] legacy/font"), std::string::npos) << changed;
    EXPECT_NE(changed.find("[SKIP] legacy/sound"), std::string::npos) << changed;
}

TEST(ContentPipelineCliTest, UnsupportedXnbModelSubsetFailsClearlyWithoutPublishing)
{
    const std::filesystem::path model =
        FindXnbFixture("monogame/windows/uncompressed/BlenderDefaultCube.xnb");
    ASSERT_FALSE(model.empty());
    ScratchDirectory scratch("xnb_unsupported");
    const std::filesystem::path output = scratch.Path() / "model.cnb";
    std::string log;
    EXPECT_EQ(RunTool({"build", model.string(), "-o", output.string()}, log), 1) << log;
    EXPECT_NE(log.find("Model cannot be transcoded losslessly"), std::string::npos) << log;
    EXPECT_NE(log.find("VertexDeclaration"), std::string::npos) << log;
    EXPECT_NE(log.find("stride 24 reconstructs 3"), std::string::npos) << log;
    EXPECT_FALSE(std::filesystem::exists(output));
}

TEST(ContentPipelineCliTest, XnbSongExternalMediaBytesParticipateInIncrementalFingerprint)
{
    const std::filesystem::path fixture = FindXnbFixture(
        "monogame/windows/uncompressed/song/one_two_three.xnb");
    ASSERT_FALSE(fixture.empty());
    const std::filesystem::path media = fixture.parent_path() / "one_two_three.ogg";
    ASSERT_TRUE(std::filesystem::is_regular_file(media));

    ScratchDirectory scratch("xnb_song_incremental");
    const std::filesystem::path source = scratch.Path() / "Source" / "legacy_song.xnb";
    const std::filesystem::path support = scratch.Path() / "Source" / "one_two_three.ogg";
    const std::filesystem::path output = scratch.Path() / "Content" / "legacy_song.cnb";
    const std::filesystem::path deployed =
        scratch.Path() / "Content" / "one_two_three.ogg";
    WriteBytes(source, ReadBytes(fixture));
    WriteBytes(support, ReadBytes(media));

    std::string first;
    ASSERT_EQ(RunTool({"build", source.string(), "-o", output.string()}, first), 0) << first;
    const std::vector<std::uint8_t> native = ReadBytes(output);
    EXPECT_EQ(Cnb::DecodeSongFromCnb(
                  Cnb::CnbDocument::Parse(native, "XNB Song CLI output")).streamReference,
              "one_two_three.ogg");
    EXPECT_EQ(ReadBytes(deployed), ReadBytes(support));

    std::string noOp;
    ASSERT_EQ(RunTool({"build", source.string(), "-o", output.string()}, noOp), 0) << noOp;
    EXPECT_NE(noOp.find("[SKIP] legacy_song"), std::string::npos) << noOp;

    std::vector<std::uint8_t> changedMedia = ReadBytes(support);
    changedMedia.push_back(0u);
    WriteBytes(support, changedMedia);
    std::string changed;
    ASSERT_EQ(RunTool({"build", source.string(), "-o", output.string()}, changed), 0) << changed;
    EXPECT_NE(changed.find("[BUILD] legacy_song"), std::string::npos) << changed;
    EXPECT_EQ(ReadBytes(output), native);
    EXPECT_EQ(ReadBytes(deployed), ReadBytes(support));
}

TEST(ContentPipelineCliTest, WorkerCountIsStrictBoundedAndHasASerialFallback)
{
    ScratchDirectory scratch("workers_syntax");
    const std::filesystem::path source = scratch.Path() / "wall.png";
    const std::filesystem::path output = scratch.Path() / "wall.cnb";
    WriteBytes(source, MakePng(2, 2));

    for (const std::vector<std::string>& suffix :
         {std::vector<std::string>{"--workers"},
          std::vector<std::string>{"--workers", "0"},
          std::vector<std::string>{"--workers", "-1"},
          std::vector<std::string>{"--workers", "1x"},
          std::vector<std::string>{"--workers", "65"},
          std::vector<std::string>{"--workers", "2", "--workers", "3"}})
    {
        std::vector<std::string> arguments{
            "build", source.string(), "-o", output.string()};
        arguments.insert(arguments.end(), suffix.begin(), suffix.end());
        std::string log;
        EXPECT_EQ(RunTool(arguments, log), 2) << log;
        EXPECT_FALSE(std::filesystem::exists(output));
    }

    std::string serialLog;
    ASSERT_EQ(RunTool({"build", source.string(), "-o", output.string(),
                       "--workers", "1"}, serialLog),
              0)
        << serialLog;
    EXPECT_NE(serialLog.find("Built: 1  Skipped: 0  Failed: 0"), std::string::npos)
        << serialLog;
}

TEST(ContentPipelineCliTest, DirectoryBuildIsSortedAndPreservesLogicalRelativePaths)
{
    ScratchDirectory scratch("directory");
    const std::filesystem::path source = scratch.Path() / "ContentSource";
    const std::filesystem::path output = scratch.Path() / "Content";
    WriteBytes(source / "Sounds" / "explosion.wav", MakeWav());
    WriteBytes(source / "Textures" / "wall.png", MakePng(3, 2));

    std::string log;
    ASSERT_EQ(RunTool({"build", source.string(), "--output", output.string()}, log), 0) << log;
    const std::size_t soundPosition = log.find("[BUILD] Sounds/explosion");
    const std::size_t texturePosition = log.find("[BUILD] Textures/wall");
    ASSERT_NE(soundPosition, std::string::npos) << log;
    ASSERT_NE(texturePosition, std::string::npos) << log;
    EXPECT_LT(soundPosition, texturePosition) << log;

    const Cnb::CnbDocument sound =
        Cnb::CnbDocument::ParseFile((output / "Sounds" / "explosion.cnb").string());
    const Cnb::CnbDocument texture =
        Cnb::CnbDocument::ParseFile((output / "Textures" / "wall.cnb").string());
    EXPECT_EQ(sound.Metadata().contentName, "Sounds/explosion");
    EXPECT_EQ(texture.Metadata().contentName, "Textures/wall");

    const std::filesystem::path manifestPath =
        output / Pipeline::ContentBuildManifestFileName;
    ASSERT_TRUE(std::filesystem::is_regular_file(manifestPath));
    const std::vector<std::uint8_t> manifestBytes = ReadBytes(manifestPath);
    const Pipeline::ContentBuildManifest manifest = Pipeline::ContentBuildManifest::Parse(
        std::string(manifestBytes.begin(), manifestBytes.end()));
    ASSERT_EQ(manifest.Entries().size(), 2u);
    EXPECT_NE(manifest.Find("Sounds/explosion"), nullptr);
    EXPECT_NE(manifest.Find("Textures/wall"), nullptr);
}

TEST(ContentPipelineCliTest, ExplicitConfigurationOverridesRouteParametersAndLogicalOutput)
{
    ScratchDirectory scratch("configured");
    const std::filesystem::path source = scratch.Path() / "ContentSource";
    const std::filesystem::path output = scratch.Path() / "Content";
    const std::filesystem::path image = source / "Textures" / "wall.png";
    const std::filesystem::path configuration = source / "custom-content.json";
    WriteBytes(image, MakePng(3, 2));
    WriteText(configuration,
              R"json({"format":"CNA.ContentPipeline.Config","version":1,"assets":{"Textures/wall.png":{"logicalName":"Environment/stone","importer":"CNA.ImageImporter","processor":"CNA.TextureProcessor","writer":"CNA.Texture2DContentWriter","parameters":{"colorKey":{"type":"string","value":"3,20,37"}}}}})json");

    std::string log;
    ASSERT_EQ(RunTool({"build", source.string(), "-o", output.string(), "--config",
                       configuration.string()},
                      log),
              0)
        << log;
    const std::filesystem::path configuredOutput = output / "Environment" / "stone.cnb";
    ASSERT_TRUE(std::filesystem::is_regular_file(configuredOutput));
    EXPECT_FALSE(std::filesystem::exists(output / "Textures" / "wall.cnb"));

    auto registry = std::make_shared<Pipeline::ContentPipelineRegistry>();
    Pipeline::RegisterTexture2DContentPipeline(*registry);
    const Pipeline::ContentPipeline pipeline(registry);
    Pipeline::ContentBuildRequest request;
    request.sourceRoot = source;
    request.source = image;
    request.logicalName = "Environment/stone";
    request.importer = "CNA.ImageImporter";
    request.processor = "CNA.TextureProcessor";
    request.writer = "CNA.Texture2DContentWriter";
    request.parameters.Set(Pipeline::TextureColorKeyParameter, std::string("3,20,37"));
    EXPECT_EQ(ReadBytes(configuredOutput), pipeline.Build(request).output.bytes);

    const std::vector<std::uint8_t> manifestBytes =
        ReadBytes(output / Pipeline::ContentBuildManifestFileName);
    const Pipeline::ContentBuildManifest manifest = Pipeline::ContentBuildManifest::Parse(
        std::string(manifestBytes.begin(), manifestBytes.end()));
    const Pipeline::ContentBuildManifestEntry* entry = manifest.Find("Environment/stone");
    ASSERT_NE(entry, nullptr);
    ASSERT_NE(entry->parameters.Find(Pipeline::TextureColorKeyParameter), nullptr);
    EXPECT_EQ(std::get<std::string>(
                  *entry->parameters.Find(Pipeline::TextureColorKeyParameter)),
              "3,20,37");
}

TEST(ContentPipelineCliTest, ConfigurationChangeInvalidatesOnlyTheAffectedAsset)
{
    ScratchDirectory scratch("config_incremental");
    const std::filesystem::path source = scratch.Path() / "ContentSource";
    const std::filesystem::path output = scratch.Path() / "Content";
    const std::filesystem::path configuration =
        source / Pipeline::ContentBuildConfigurationFileName;
    WriteBytes(source / "wall.png", MakePng(3, 2));
    WriteBytes(source / "floor.png", MakePng(2, 2));
    WriteText(configuration,
              R"json({"format":"CNA.ContentPipeline.Config","version":1,"assets":{"wall.png":{"parameters":{"colorKey":{"type":"string","value":"3,20,37"}}}}})json");

    std::string log;
    ASSERT_EQ(RunTool({"build", source.string(), "-o", output.string()}, log), 0) << log;
    ASSERT_NE(log.find("[BUILD] floor"), std::string::npos) << log;
    ASSERT_NE(log.find("[BUILD] wall"), std::string::npos) << log;

    WriteText(configuration,
              R"json({"format":"CNA.ContentPipeline.Config","version":1,"assets":{"wall.png":{"parameters":{"colorKey":{"type":"string","value":"4,20,37"}}}}})json");
    ASSERT_EQ(RunTool({"build", source.string(), "-o", output.string()}, log), 0) << log;
    EXPECT_NE(log.find("[SKIP] floor"), std::string::npos) << log;
    EXPECT_NE(log.find("[BUILD] wall"), std::string::npos) << log;
}

TEST(ContentPipelineCliTest, ConfigurationRejectsUnknownAssetsComponentsAndOptions)
{
    ScratchDirectory scratch("config_errors");
    const std::filesystem::path source = scratch.Path() / "ContentSource";
    const std::filesystem::path output = scratch.Path() / "Content";
    const std::filesystem::path configuration =
        source / Pipeline::ContentBuildConfigurationFileName;
    WriteBytes(source / "wall.png", MakePng(3, 2));

    struct Case
    {
        const char* label;
        const char* assets;
        const char* expected;
    };
    const Case cases[] = {
        {"unknown asset", R"json("missing.png":{})json", "does not name"},
        {"unknown processor", R"json("wall.png":{"processor":"Missing.Processor"})json",
         "unknown processor"},
        {"unknown option",
         R"json("wall.png":{"parameters":{"unknown":{"type":"bool","value":true}}})json",
         "does not recognize"},
        {"wrong option type",
         R"json("wall.png":{"parameters":{"colorKey":{"type":"bool","value":true}}})json",
         "must be a string"},
    };

    for (const Case& item : cases)
    {
        WriteText(configuration,
                  std::string("{\"format\":\"CNA.ContentPipeline.Config\",\"version\":1,"
                              "\"assets\":{") +
                      item.assets + "}}");
        std::string log;
        EXPECT_NE(RunTool({"build", source.string(), "-o", output.string()}, log), 0)
            << item.label << "\n" << log;
        EXPECT_NE(log.find(item.expected), std::string::npos) << item.label << "\n" << log;
        EXPECT_FALSE(std::filesystem::exists(output / "wall.cnb")) << item.label;
    }
}

TEST(ContentPipelineCliTest, ConfigurationPathMustRemainInsideTheSourceRoot)
{
    ScratchDirectory scratch("config_containment");
    const std::filesystem::path source = scratch.Path() / "ContentSource";
    const std::filesystem::path output = scratch.Path() / "Content";
    const std::filesystem::path outside = scratch.Path() / "outside.json";
    WriteBytes(source / "wall.png", MakePng(3, 2));
    WriteText(outside,
              R"json({"format":"CNA.ContentPipeline.Config","version":1,"assets":{}})json");

    std::string log;
    EXPECT_NE(RunTool({"build", source.string(), "-o", output.string(), "--config",
                       outside.string()},
                      log),
              0);
    EXPECT_NE(log.find("must remain inside source root"), std::string::npos) << log;
}

TEST(ContentPipelineCliTest, NonAsciiDirectoryBuildPreservesUtf8LogicalNamesAndSkips)
{
    ScratchDirectory scratch("unicode");
    const std::filesystem::path source =
        scratch.Path() / std::filesystem::path(u8"Zdrojový obsah");
    const std::filesystem::path output =
        scratch.Path() / std::filesystem::path(u8"Přeložený obsah");
    const std::filesystem::path relative =
        std::filesystem::path(u8"Textury") / std::filesystem::path(u8"žluťoučký_壁.png");
    WriteBytes(source / relative, MakePng(3, 2));

    std::string first;
    ASSERT_EQ(RunTool({"build", CNA::Internal::ContentPathToUtf8(source), "-o",
                       CNA::Internal::ContentPathToUtf8(output)},
                      first),
              0)
        << first;
    EXPECT_NE(first.find("[BUILD] Textury/žluťoučký_壁"), std::string::npos) << first;

    std::filesystem::path artifact = output / relative;
    artifact.replace_extension(".cnb");
    const Cnb::CnbDocument document =
        Cnb::CnbDocument::Parse(ReadBytes(artifact), "non-ASCII CLI output");
    EXPECT_EQ(document.Metadata().contentName, "Textury/žluťoučký_壁");

    const std::filesystem::path manifestPath =
        output / Pipeline::ContentBuildManifestFileName;
    const std::vector<std::uint8_t> manifestBytes = ReadBytes(manifestPath);
    const Pipeline::ContentBuildManifest manifest = Pipeline::ContentBuildManifest::Parse(
        std::string(manifestBytes.begin(), manifestBytes.end()));
    const Pipeline::ContentBuildManifestEntry* entry =
        manifest.Find("Textury/žluťoučký_壁");
    ASSERT_NE(entry, nullptr);
    EXPECT_EQ(entry->source, "Textury/žluťoučký_壁.png");
    ASSERT_EQ(entry->outputs.size(), 1u);
    EXPECT_EQ(entry->outputs.front().path, "Textury/žluťoučký_壁.cnb");

    std::string second;
    ASSERT_EQ(RunTool({"build", CNA::Internal::ContentPathToUtf8(source), "-o",
                       CNA::Internal::ContentPathToUtf8(output)},
                      second),
              0)
        << second;
    EXPECT_NE(second.find("[SKIP] Textury/žluťoučký_壁"), std::string::npos) << second;
}

TEST(ContentPipelineCliTest, DirectoryBuildIgnoresUnregisteredSupportFiles)
{
    ScratchDirectory scratch("support_files");
    const std::filesystem::path source = scratch.Path() / "ContentSource";
    const std::filesystem::path output = scratch.Path() / "Content";
    WriteBytes(source / "Textures" / "wall.png", MakePng(3, 2));
    WriteBytes(source / "Models" / "geometry.bin", {1u, 2u, 3u, 4u});

    std::string log;
    ASSERT_EQ(RunTool({"build", source.string(), "-o", output.string()}, log), 0) << log;
    EXPECT_NE(log.find("[BUILD] Textures/wall"), std::string::npos) << log;
    EXPECT_EQ(log.find("geometry.bin"), std::string::npos) << log;
    EXPECT_TRUE(std::filesystem::is_regular_file(output / "Textures" / "wall.cnb"));
    EXPECT_FALSE(std::filesystem::exists(output / "Models" / "geometry.cnb"));
}

TEST(ContentPipelineCliTest, DirectoryBuildCompilesSpriteFontCnjAndItsAtlas)
{
    ScratchDirectory scratch("font_cnj");
    const std::filesystem::path source = scratch.Path() / "ContentSource";
    const std::filesystem::path output = scratch.Path() / "Content";
    WriteBytes(source / "Fonts" / "atlas.png", MakePng(8, 4));
    const std::string font =
        R"({"cnjVersion":1,"type":"SpriteFont","texture":"atlas.png",)"
        R"("lineSpacing":12,"spacing":1.5,"defaultCharacter":"?","glyphs":[)"
        R"({"char":63,"source":[0,0,3,4],"crop":[0,1,3,4],"kerning":[0,3,0.5]},)"
        R"({"char":65,"source":[3,0,2,4],"crop":[1,0,2,4],"kerning":[-1,2,0]}]})";
    WriteBytes(source / "Fonts" / "ui.cnj",
               std::vector<std::uint8_t>(font.begin(), font.end()));

    std::string log;
    ASSERT_EQ(RunTool({"build", source.string(), "-o", output.string()}, log), 0) << log;
    EXPECT_NE(log.find("[BUILD] Fonts/atlas"), std::string::npos) << log;
    EXPECT_NE(log.find("[BUILD] Fonts/ui"), std::string::npos) << log;

    const Cnb::CnbDocument document =
        Cnb::CnbDocument::ParseFile((output / "Fonts" / "ui.cnb").string());
    EXPECT_EQ(document.Metadata().contentName, "Fonts/ui");
    EXPECT_EQ(document.AssetTypeId(), Cnb::CnbAssetTypeId::SpriteFont);

    std::string second;
    ASSERT_EQ(RunTool({"build", source.string(), "-o", output.string()}, second), 0) << second;
    EXPECT_NE(second.find("[SKIP] Fonts/atlas"), std::string::npos) << second;
    EXPECT_NE(second.find("[SKIP] Fonts/ui"), std::string::npos) << second;
}

TEST(ContentPipelineCliTest, NamedExternalRootBuildsCnjAndRemapsByStableAlias)
{
    ScratchDirectory scratch("external_cnj");
    const std::filesystem::path source = scratch.Path() / "ContentSource";
    const std::filesystem::path sharedA = scratch.Path() / "SharedA";
    const std::filesystem::path sharedB = scratch.Path() / "SharedB";
    const std::filesystem::path output = scratch.Path() / "Content";
    const std::string document =
        R"({"cnjVersion":1,"type":"Texture2D","sourceFile":"@shared/Textures/wall.png"})";
    WriteText(source / "Textures" / "wall.cnj", document);
    const std::vector<std::uint8_t> image = MakePng(4, 3);
    WriteBytes(sharedA / "Textures" / "wall.png", image);
    WriteBytes(sharedB / "Textures" / "wall.png", image);
    const auto configure = [&](const std::filesystem::path& root)
    {
        WriteText(source / Pipeline::ContentBuildConfigurationFileName,
                  std::string(R"({"format":"CNA.ContentPipeline.Config","version":1,"sourceRoots":{"shared":")") +
                      CNA::Internal::ContentPathToUtf8(root) + R"("},"assets":{}})");
    };
    configure(sharedA);

    std::string log;
    ASSERT_EQ(RunTool({"build", source.string(), "-o", output.string(),
                       "--workers", "1", "--explain"}, log), 0)
        << log;
    EXPECT_NE(log.find("[BUILD] Textures/wall"), std::string::npos) << log;
    const std::vector<std::uint8_t> firstOutput =
        ReadBytes(output / "Textures" / "wall.cnb");
    const std::vector<std::uint8_t> firstManifest =
        ReadBytes(output / Pipeline::ContentBuildManifestFileName);
    const Pipeline::ContentBuildManifest manifest = Pipeline::ContentBuildManifest::Parse(
        std::string(firstManifest.begin(), firstManifest.end()));
    const Pipeline::ContentBuildManifestEntry* entry = manifest.Find("Textures/wall");
    ASSERT_NE(entry, nullptr);
    const auto external = std::find_if(
        entry->dependencies.begin(), entry->dependencies.end(),
        [](const Pipeline::ContentDependency& dependency)
        { return dependency.sourceRoot == "shared"; });
    ASSERT_NE(external, entry->dependencies.end());
    EXPECT_EQ(external->identity, "Textures/wall.png");
    EXPECT_EQ(std::string(firstManifest.begin(), firstManifest.end()).find(
                  CNA::Internal::ContentPathToUtf8(sharedA)),
              std::string::npos);

    configure(sharedB);
    log.clear();
    ASSERT_EQ(RunTool({"build", source.string(), "-o", output.string(),
                       "--workers", "4", "--explain"}, log), 0)
        << log;
    EXPECT_NE(log.find("[SKIP] Textures/wall"), std::string::npos) << log;
    EXPECT_EQ(ReadBytes(output / Pipeline::ContentBuildManifestFileName), firstManifest);
    EXPECT_EQ(ReadBytes(output / "Textures" / "wall.cnb"), firstOutput);

    WriteBytes(sharedB / "Textures" / "wall.png", MakePng(5, 2));
    log.clear();
    ASSERT_EQ(RunTool({"build", source.string(), "-o", output.string(),
                       "--workers", "4", "--explain"}, log), 0)
        << log;
    EXPECT_NE(log.find("reason: source dependency bytes changed"), std::string::npos) << log;
    EXPECT_NE(ReadBytes(output / "Textures" / "wall.cnb"), firstOutput);
}

TEST(ContentPipelineCliTest, ExternalRootConfigurationRejectsEveryBypassAndOverlap)
{
    ScratchDirectory scratch("external_security");
    const std::filesystem::path source = scratch.Path() / "Project" / "ContentSource";
    const std::filesystem::path shared = scratch.Path() / "Project" / "Shared";
    const std::filesystem::path other = scratch.Path() / "Project" / "Other";
    WriteBytes(shared / "safe.png", MakePng(2, 2));
    WriteBytes(other / "secret.png", MakePng(3, 2));
    WriteText(shared / "sentinel.keep", "external sentinel\n");
    WriteText(scratch.Path() / "root-is-file", "not a directory\n");
    std::filesystem::create_directories(shared / "Nested");
    std::filesystem::create_directories(source / "NestedExternal");

    std::error_code symlinkError;
    std::filesystem::create_symlink(other / "secret.png", shared / "escape.png",
                                    symlinkError);

    std::size_t index = 0u;
    const auto fail = [&](const std::string& reference,
                          const std::string& sourceRoots,
                          const std::filesystem::path& output = {})
    {
        WriteText(source / "wall.cnj",
                  std::string(R"({"cnjVersion":1,"type":"Texture2D","sourceFile":")") +
                      reference + R"("})");
        const std::filesystem::path configuration =
            source / Pipeline::ContentBuildConfigurationFileName;
        if (sourceRoots.empty())
        {
            std::error_code error;
            std::filesystem::remove(configuration, error);
        }
        else
        {
            WriteText(configuration,
                      std::string(R"({"format":"CNA.ContentPipeline.Config","version":1,"sourceRoots":{)") +
                          sourceRoots + R"(},"assets":{}})");
        }
        const std::filesystem::path selectedOutput = output.empty()
            ? scratch.Path() / ("Rejected" + std::to_string(index++))
            : output;
        std::string log;
        EXPECT_EQ(RunTool({"build", source.string(), "-o", selectedOutput.string()}, log), 1)
            << reference << "\n" << sourceRoots << "\n" << log;
    };
    const std::string sharedEntry =
        std::string("\"shared\":\"") + CNA::Internal::ContentPathToUtf8(shared) + "\"";

    fail("../../Shared/safe.png", {});
    fail("@unknown/safe.png", sharedEntry);
    fail("@shared/../Other/secret.png", sharedEntry);
    fail(CNA::Internal::ContentPathToUtf8(other / "secret.png"), sharedEntry);
    fail("@shared/safe.png", sharedEntry + "," + sharedEntry);
    fail("@shared/safe.png",
         sharedEntry + ",\"same\":\"" +
             CNA::Internal::ContentPathToUtf8(shared) + "\"");
    fail("@shared/safe.png", "\"shared\":\"" +
         CNA::Internal::ContentPathToUtf8(scratch.Path() / "root-is-file") + "\"");
    fail("@shared/safe.png", "\"shared\":\"" +
         CNA::Internal::ContentPathToUtf8(scratch.Path() / "missing-root") + "\"");
    fail("@shared/safe.png", "\"shared\":\"" +
         CNA::Internal::ContentPathToUtf8(source / "NestedExternal") + "\"");
    fail("@shared/safe.png", "\"shared\":\"" +
         CNA::Internal::ContentPathToUtf8(source.parent_path()) + "\"");
    fail("@shared/safe.png",
         sharedEntry + ",\"nested\":\"" +
             CNA::Internal::ContentPathToUtf8(shared / "Nested") + "\"");
    fail("@shared/safe.png", sharedEntry, shared);
    fail("@shared/safe.png", sharedEntry, shared / "Output");
    fail("@shared/safe.png", sharedEntry, shared.parent_path());
    if (!symlinkError) { fail("@shared/escape.png", sharedEntry); }

    EXPECT_EQ(ReadBytes(shared / "sentinel.keep"),
              (std::vector<std::uint8_t>{'e','x','t','e','r','n','a','l',' ','s','e','n','t','i','n','e','l','\n'}));
}

TEST(ContentPipelineCliTest, GltfImageDependencyInvalidatesOnlyItsRelevantAssets)
{
    const std::filesystem::path fixture = FindGltfFixture("gltf-external-image.gltf");
    const std::filesystem::path image =
        FindGltfFixture("gltf-external-image.texture.png");
    if (fixture.empty() || image.empty()) { GTEST_SKIP() << "glTF fixture not found"; }

    ScratchDirectory scratch("gltf_dependency");
    const std::filesystem::path source = scratch.Path() / "ContentSource";
    const std::filesystem::path output = scratch.Path() / "Content";
    std::filesystem::create_directories(source / "Models");
    std::filesystem::copy_file(fixture, source / "Models" / "car.gltf");
    std::filesystem::copy_file(image,
                               source / "Models" / "gltf-external-image.texture.png");
    WriteBytes(source / "Textures" / "independent.png", MakePng(2, 2));

    ASSERT_EQ(RunTool({"build", source.string(), "-o", output.string(), "--quiet"}), 0);
    const std::vector<std::uint8_t> independent =
        ReadBytes(output / "Textures" / "independent.cnb");

    WriteBytes(source / "Models" / "gltf-external-image.texture.png", MakePng(2, 2));
    std::string log;
    ASSERT_EQ(RunTool({"build", source.string(), "-o", output.string(), "--explain"}, log), 0)
        << log;
    EXPECT_NE(log.find("[BUILD] Models/car"), std::string::npos) << log;
    EXPECT_NE(log.find("[BUILD] Models/gltf-external-image.texture"), std::string::npos) << log;
    EXPECT_NE(log.find("[SKIP] Textures/independent"), std::string::npos) << log;
    EXPECT_NE(log.find("reason: source dependency bytes changed"), std::string::npos)
        << log;
    EXPECT_EQ(ReadBytes(output / "Textures" / "independent.cnb"), independent);
}

TEST(ContentPipelineCliTest, GltfGeneratedChildrenAreAtomicOwnedAndWorkerDeterministic)
{
    const std::vector<std::string> fixtureFiles = {
        "anim-two-clips.gltf", "anim-two-clips.vb.bin", "anim-two-clips.ib.bin",
        "gltf-data-uri-image.gltf", "gltf-data-uri-image.vb.bin",
        "gltf-data-uri-image.ib.bin", "skin-plus-static-mesh.gltf",
        "skin-plus-static-mesh.vb.bin", "skin-plus-static-mesh.ib.bin",
        "skin-plus-static-mesh.p1.vb.bin", "skin-plus-static-mesh.p1.ib.bin"};
    for (const std::string& file : fixtureFiles)
    {
        if (FindGltfFixture(file).empty())
        {
            GTEST_SKIP() << "glTF fixture not found: " << file;
        }
    }

    ScratchDirectory scratch("gltf_children");
    const std::filesystem::path source = scratch.Path() / "ContentSource";
    for (const std::string& file : fixtureFiles)
    {
        const std::filesystem::path destination = source / "Models" / file;
        std::filesystem::create_directories(destination.parent_path());
        std::filesystem::copy_file(FindGltfFixture(file), destination);
    }

    const auto writeConfiguration = [&](bool animationChildren, bool textureChildren)
    {
        WriteText(
            source / Pipeline::ContentBuildConfigurationFileName,
            std::string(
                R"json({"format":"CNA.ContentPipeline.Config","version":1,"assets":{)json") +
            R"json("Models/anim-two-clips.gltf":{"parameters":{"generateChildAssets":{"type":"bool","value":)json" +
            (animationChildren ? "true" : "false") +
            R"json(}}},"Models/gltf-data-uri-image.gltf":{"parameters":{"generateChildAssets":{"type":"bool","value":)json" +
            (textureChildren ? "true" : "false") +
            R"json(}}},"Models/skin-plus-static-mesh.gltf":{"parameters":{"generateChildAssets":{"type":"bool","value":true}}}}})json");
    };
    writeConfiguration(true, true);

    FileTreeSnapshot reference;
    for (const char* workers : {"1", "2", "4"})
    {
        const std::filesystem::path output =
            scratch.Path() / (std::string("Content-") + workers);
        std::string log;
        ASSERT_EQ(RunTool({"build", source.string(), "-o", output.string(),
                           "--workers", workers}, log), 0) << log;
        EXPECT_TRUE(std::filesystem::is_regular_file(
            output / "Models" / "anim-two-clips_Clip1.cnb"));
        EXPECT_TRUE(std::filesystem::is_regular_file(
            output / "Models" / "anim-two-clips_Walk.cnb"));
        EXPECT_TRUE(std::filesystem::is_regular_file(
            output / "Models" / "gltf-data-uri-image_tex0.png.cnb"));
        EXPECT_TRUE(std::filesystem::is_regular_file(
            output / "Models" / "skin-plus-static-mesh_static.cnb"));
        if (reference.empty()) { reference = SnapshotFileTree(output); }
        else { EXPECT_EQ(SnapshotFileTree(output), reference) << "workers=" << workers; }

        std::string noOp;
        ASSERT_EQ(RunTool({"build", source.string(), "-o", output.string(),
                           "--workers", workers}, noOp), 0) << noOp;
        EXPECT_EQ(CountOccurrences(noOp, "[SKIP] Models/"), 3u) << noOp;
        EXPECT_EQ(SnapshotFileTree(output), reference) << "workers=" << workers;
    }

    const std::filesystem::path contracted = scratch.Path() / "Content-1";
    writeConfiguration(false, false);
    std::string contraction;
    ASSERT_EQ(RunTool({"build", source.string(), "-o", contracted.string(),
                       "--workers", "4"}, contraction), 0) << contraction;
    EXPECT_FALSE(std::filesystem::exists(
        contracted / "Models" / "anim-two-clips_Clip1.cnb"));
    EXPECT_FALSE(std::filesystem::exists(
        contracted / "Models" / "anim-two-clips_Walk.cnb"));
    EXPECT_FALSE(std::filesystem::exists(
        contracted / "Models" / "gltf-data-uri-image_tex0.png.cnb"));
    EXPECT_TRUE(std::filesystem::is_regular_file(
        contracted / "Models" / "skin-plus-static-mesh_static.cnb"));
    EXPECT_TRUE(std::filesystem::is_regular_file(
        contracted / "Models" / "anim-two-clips.cnb"));
    EXPECT_TRUE(std::filesystem::is_regular_file(
        contracted / "Models" / "gltf-data-uri-image.cnb"));
}

TEST(ContentPipelineCliTest, RepeatedBuildSkipsWithoutChangingOutputOrManifest)
{
    ScratchDirectory scratch("deterministic");
    const std::filesystem::path source = scratch.Path() / "wall.png";
    const std::filesystem::path output = scratch.Path() / "wall.cnb";
    WriteBytes(source, MakePng(5, 4));

    ASSERT_EQ(RunTool({"build", source.string(), "-o", output.string(), "--quiet"}), 0);
    const std::vector<std::uint8_t> first = ReadBytes(output);
    const std::filesystem::path manifest =
        output.parent_path() / Pipeline::ContentBuildManifestFileName;
    const std::vector<std::uint8_t> firstManifest = ReadBytes(manifest);
    std::string log;
    ASSERT_EQ(RunTool({"build", source.string(), "-o", output.string()}, log), 0) << log;
    EXPECT_NE(log.find("[SKIP] wall"), std::string::npos) << log;
    EXPECT_NE(log.find("Built: 0  Skipped: 1  Failed: 0"), std::string::npos) << log;
    EXPECT_EQ(ReadBytes(output), first);
    EXPECT_EQ(ReadBytes(manifest), firstManifest);
}

TEST(ContentPipelineCliTest, ExplainReportsNewAndUnchangedAssetsAndQuietSuppressesIt)
{
    ScratchDirectory scratch("explain_new_skip_quiet");
    const std::filesystem::path source = scratch.Path() / "ContentSource";
    const std::filesystem::path output = scratch.Path() / "Content";
    WriteBytes(source / "first.png", MakePng(2, 2));

    std::string log;
    ASSERT_EQ(RunTool({"build", source.string(), "-o", output.string(), "--explain"}, log), 0)
        << log;
    EXPECT_NE(log.find("reason: manifest unavailable"), std::string::npos) << log;
    EXPECT_NE(log.find("reason: new asset: first.png"), std::string::npos) << log;

    log.clear();
    ASSERT_EQ(RunTool({"build", source.string(), "-o", output.string(), "--explain"}, log), 0)
        << log;
    EXPECT_NE(log.find("[SKIP] first"), std::string::npos) << log;
    EXPECT_NE(log.find("reason: fingerprint and published output digests unchanged"),
              std::string::npos)
        << log;

    WriteBytes(source / "second.png", MakePng(3, 2));
    log.clear();
    ASSERT_EQ(RunTool({"build", source.string(), "-o", output.string(), "--explain"}, log), 0)
        << log;
    EXPECT_NE(log.find("[BUILD] second"), std::string::npos) << log;
    EXPECT_NE(log.find("reason: new asset: second.png"), std::string::npos) << log;

    log.clear();
    ASSERT_EQ(RunTool({"build", source.string(), "-o", output.string(), "--explain",
                       "--quiet"},
                      log),
              0)
        << log;
    EXPECT_TRUE(log.empty()) << log;
}

TEST(ContentPipelineCliTest, ExplainClassifiesSourceParametersAndLogicalOutputChanges)
{
    ScratchDirectory scratch("explain_direct_changes");
    const std::filesystem::path source = scratch.Path() / "ContentSource";
    const std::filesystem::path output = scratch.Path() / "Content";
    const std::filesystem::path configuration =
        source / Pipeline::ContentBuildConfigurationFileName;
    WriteBytes(source / "wall.png", MakePng(2, 2));
    std::string log;
    ASSERT_EQ(RunTool({"build", source.string(), "-o", output.string(), "--quiet"}), 0);

    WriteBytes(source / "wall.png", MakePng(3, 2));
    ASSERT_EQ(RunTool({"build", source.string(), "-o", output.string(), "--explain"}, log), 0)
        << log;
    EXPECT_NE(log.find("reason: primary source bytes changed: wall.png"),
              std::string::npos)
        << log;

    WriteText(configuration,
              R"json({"format":"CNA.ContentPipeline.Config","version":1,"assets":{"wall.png":{"parameters":{"colorKey":{"type":"string","value":"3,20,37"}}}}})json");
    log.clear();
    ASSERT_EQ(RunTool({"build", source.string(), "-o", output.string(), "--explain"}, log), 0)
        << log;
    EXPECT_NE(log.find("reason: processor parameters changed"), std::string::npos) << log;

    WriteText(configuration,
              R"json({"format":"CNA.ContentPipeline.Config","version":1,"assets":{"wall.png":{"logicalName":"Environment/stone","parameters":{"colorKey":{"type":"string","value":"3,20,37"}}}}})json");
    log.clear();
    ASSERT_EQ(RunTool({"build", source.string(), "-o", output.string(), "--explain"}, log), 0)
        << log;
    EXPECT_NE(log.find("reason: logical asset/output identity changed"), std::string::npos)
        << log;
    EXPECT_NE(log.find("reason: compiled output definition set changed"),
              std::string::npos)
        << log;
    EXPECT_TRUE(std::filesystem::is_regular_file(output / "Environment" / "stone.cnb"));
}

TEST(ContentPipelineCliTest, ExplainClassifiesSourceDependencySetChanges)
{
    ScratchDirectory scratch("explain_source_dependency_set");
    const std::filesystem::path source = scratch.Path() / "ContentSource";
    const std::filesystem::path output = scratch.Path() / "Content";
    WriteBytes(source / "Fonts" / "atlas-a.png", MakePng(4, 4));
    WriteBytes(source / "Fonts" / "atlas-b.png", MakePng(4, 4));
    const auto writeFont = [&](const char* atlas)
    {
        WriteText(
            source / "Fonts" / "ui.cnj",
            std::string(
                R"json({"cnjVersion":1,"type":"SpriteFont","texture":")json") + atlas +
                R"json(","lineSpacing":8,"spacing":0,"defaultCharacter":"?","glyphs":[{"char":63,"source":[0,0,2,2],"crop":[0,0,2,2],"kerning":[0,2,0]}]})json");
    };
    writeFont("atlas-a.png");
    ASSERT_EQ(RunTool({"build", source.string(), "-o", output.string(), "--quiet"}), 0);

    writeFont("atlas-b.png");
    std::string log;
    ASSERT_EQ(RunTool({"build", source.string(), "-o", output.string(), "--explain"}, log), 0)
        << log;
    EXPECT_NE(log.find("reason: source dependency set changed"), std::string::npos)
        << log;
}

TEST(ContentPipelineCliTest, ChangingOneIndependentSourceRebuildsOnlyThatAsset)
{
    ScratchDirectory scratch("independent");
    const std::filesystem::path source = scratch.Path() / "ContentSource";
    const std::filesystem::path output = scratch.Path() / "Content";
    WriteBytes(source / "Sounds" / "explosion.wav", MakeWav());
    WriteBytes(source / "Textures" / "wall.png", MakePng(3, 2));
    ASSERT_EQ(RunTool({"build", source.string(), "-o", output.string(), "--quiet"}), 0);
    const std::vector<std::uint8_t> sound = ReadBytes(output / "Sounds" / "explosion.cnb");

    WriteBytes(source / "Textures" / "wall.png", MakePng(4, 3));
    std::string log;
    ASSERT_EQ(RunTool({"build", source.string(), "-o", output.string()}, log), 0) << log;
    EXPECT_NE(log.find("[SKIP] Sounds/explosion"), std::string::npos) << log;
    EXPECT_NE(log.find("[BUILD] Textures/wall"), std::string::npos) << log;
    EXPECT_NE(log.find("Built: 1  Skipped: 1  Failed: 0"), std::string::npos) << log;
    EXPECT_EQ(ReadBytes(output / "Sounds" / "explosion.cnb"), sound);
}

TEST(ContentPipelineCliTest, MissingOrTamperedOutputForcesARebuild)
{
    ScratchDirectory scratch("tampered_output");
    const std::filesystem::path source = scratch.Path() / "wall.png";
    const std::filesystem::path output = scratch.Path() / "wall.cnb";
    WriteBytes(source, MakePng(3, 2));
    ASSERT_EQ(RunTool({"build", source.string(), "-o", output.string(), "--quiet"}), 0);
    const std::vector<std::uint8_t> expected = ReadBytes(output);

    WriteBytes(output, {'b', 'a', 'd'});
    std::string log;
    ASSERT_EQ(RunTool({"build", source.string(), "-o", output.string(), "--explain"}, log), 0)
        << log;
    EXPECT_NE(log.find("[BUILD] wall"), std::string::npos) << log;
    EXPECT_NE(log.find("reason: compiled output digest mismatch: wall.cnb"),
              std::string::npos)
        << log;
    EXPECT_EQ(ReadBytes(output), expected);

    std::filesystem::remove(output);
    ASSERT_EQ(RunTool({"build", source.string(), "-o", output.string(), "--explain"}, log), 0)
        << log;
    EXPECT_NE(log.find("reason: compiled output missing: wall.cnb"), std::string::npos)
        << log;
    EXPECT_EQ(ReadBytes(output), expected);
}

TEST(ContentPipelineCliTest, CorruptManifestSafelyRebuildsAndIsReplaced)
{
    ScratchDirectory scratch("corrupt_manifest");
    const std::filesystem::path source = scratch.Path() / "wall.png";
    const std::filesystem::path output = scratch.Path() / "wall.cnb";
    const std::filesystem::path manifest =
        output.parent_path() / Pipeline::ContentBuildManifestFileName;
    WriteBytes(source, MakePng(3, 2));
    ASSERT_EQ(RunTool({"build", source.string(), "-o", output.string(), "--quiet"}), 0);
    WriteBytes(manifest, {'b', 'a', 'd'});

    std::string log;
    ASSERT_EQ(RunTool({"build", source.string(), "-o", output.string(), "--explain"}, log), 0)
        << log;
    EXPECT_NE(log.find("[WARN] Ignoring incompatible or corrupt manifest"), std::string::npos)
        << log;
    EXPECT_NE(log.find("[BUILD] wall"), std::string::npos) << log;
    EXPECT_NE(log.find("reason: manifest corrupt"), std::string::npos) << log;
    const std::vector<std::uint8_t> manifestBytes = ReadBytes(manifest);
    EXPECT_NO_THROW((void)Pipeline::ContentBuildManifest::Parse(
        std::string(manifestBytes.begin(), manifestBytes.end())));
}

TEST(ContentPipelineCliTest, VersionSixManifestRebuildsWithoutAuthorizingOldOutputDeletion)
{
    ScratchDirectory scratch("manifest_v6_transition");
    const std::filesystem::path source = scratch.Path() / "ContentSource";
    const std::filesystem::path output = scratch.Path() / "Content";
    const std::filesystem::path manifest =
        output / Pipeline::ContentBuildManifestFileName;
    WriteBytes(source / "current.png", MakePng(2, 2));
    WriteBytes(source / "legacy.png", MakePng(3, 2));
    std::string log;
    ASSERT_EQ(RunTool({"build", source.string(), "-o", output.string(), "--explain"}, log), 0)
        << log;
    const std::vector<std::uint8_t> legacyOutput = ReadBytes(output / "legacy.cnb");

    const std::vector<std::uint8_t> oldManifestBytes = ReadBytes(manifest);
    std::string oldManifest(oldManifestBytes.begin(), oldManifestBytes.end());
    const std::size_t version = oldManifest.find("\"version\":7");
    ASSERT_NE(version, std::string::npos);
    oldManifest.replace(version, std::string("\"version\":7").size(), "\"version\":6");
    WriteText(manifest, oldManifest);
    ASSERT_TRUE(std::filesystem::remove(source / "legacy.png"));

    log.clear();
    ASSERT_EQ(RunTool({"build", source.string(), "-o", output.string(), "--explain"}, log), 0)
        << log;
    EXPECT_NE(log.find("[WARN] Ignoring incompatible or corrupt manifest"), std::string::npos)
        << log;
    EXPECT_NE(log.find("[BUILD] current"), std::string::npos) << log;
    EXPECT_NE(log.find("reason: manifest format changed or is incompatible"),
              std::string::npos)
        << log;
    EXPECT_EQ(ReadBytes(output / "legacy.cnb"), legacyOutput);
    const std::vector<std::uint8_t> currentManifestBytes = ReadBytes(manifest);
    const Pipeline::ContentBuildManifest current = Pipeline::ContentBuildManifest::Parse(
        std::string(currentManifestBytes.begin(), currentManifestBytes.end()));
    EXPECT_NE(current.Find("current"), nullptr);
    EXPECT_EQ(current.Find("legacy"), nullptr);
}

TEST(ContentPipelineCliTest, FailedRebuildPreservesTheOldValidOutputAndLeavesNoTemporary)
{
    ScratchDirectory scratch("preserve");
    const std::filesystem::path source = scratch.Path() / "wall.png";
    const std::filesystem::path output = scratch.Path() / "wall.cnb";
    WriteBytes(source, MakePng(2, 2));
    ASSERT_EQ(RunTool({"build", source.string(), "-o", output.string(), "--quiet"}), 0);
    const std::vector<std::uint8_t> oldBytes = ReadBytes(output);
    const std::filesystem::path manifest =
        output.parent_path() / Pipeline::ContentBuildManifestFileName;
    const std::vector<std::uint8_t> oldManifest = ReadBytes(manifest);

    WriteBytes(source, std::vector<std::uint8_t>{'n', 'o', 't', 'p', 'n', 'g'});
    std::string log;
    EXPECT_EQ(RunTool({"build", source.string(), "-o", output.string(), "--quiet"}, log), 1);
    EXPECT_NE(log.find("Import (CNA.ImageImporter)"), std::string::npos) << log;
    EXPECT_EQ(ReadBytes(output), oldBytes);
    EXPECT_EQ(ReadBytes(manifest), oldManifest);
    EXPECT_TRUE(TemporaryFilesBeside(output).empty());
    EXPECT_TRUE(TemporaryFilesBeside(manifest).empty());
}

TEST(ContentPipelineCliTest, SourceDeletionCollectsOnlyManifestOwnedOutputsDeterministically)
{
    ScratchDirectory scratch("orphan_source_deletion");
    const std::filesystem::path source = scratch.Path() / "ContentSource";
    WriteBytes(source / "keep.png", MakePng(2, 2));
    WriteBytes(source / "obsolete.png", MakePng(3, 2));
    WriteBytes(source / "Music" / "theme.ogg", {1u, 2u, 3u, 4u});

    FileTreeSnapshot reference;
    for (const std::size_t workers : {1u, 2u, 4u})
    {
        const std::filesystem::path output =
            scratch.Path() / ("Content" + std::to_string(workers));
        std::string log;
        ASSERT_EQ(RunTool({"build", source.string(), "-o", output.string(), "--workers",
                           std::to_string(workers)},
                          log),
                  0)
            << log;
        WriteBytes(output / "Manual" / "preserved.cnb", {'u', 's', 'e', 'r'});
    }

    ASSERT_TRUE(std::filesystem::remove(source / "obsolete.png"));
    for (const std::size_t workers : {1u, 2u, 4u})
    {
        const std::filesystem::path output =
            scratch.Path() / ("Content" + std::to_string(workers));
        std::string log;
        ASSERT_EQ(RunTool({"build", source.string(), "-o", output.string(), "--workers",
                           std::to_string(workers)},
                          log),
                  0)
            << log;
        EXPECT_NE(log.find("[CLEAN] obsolete.cnb (obsolete manifest-owned output)"),
                  std::string::npos)
            << log;
        EXPECT_EQ(CountOccurrences(log, "[CLEAN]"), 1u) << log;
        EXPECT_NE(log.find("Built: 0  Skipped: 2  Failed: 0"), std::string::npos) << log;
        EXPECT_FALSE(std::filesystem::exists(output / "obsolete.cnb"));
        EXPECT_TRUE(std::filesystem::is_regular_file(output / "keep.cnb"));
        EXPECT_EQ(ReadBytes(output / "Music" / "theme.ogg"),
                  ReadBytes(source / "Music" / "theme.ogg"));
        EXPECT_EQ(ReadBytes(output / "Manual" / "preserved.cnb"),
                  (std::vector<std::uint8_t>{'u', 's', 'e', 'r'}));

        const FileTreeSnapshot snapshot = SnapshotFileTree(output);
        if (reference.empty())
        {
            reference = snapshot;
        }
        else
        {
            EXPECT_EQ(snapshot, reference);
        }
    }
}

TEST(ContentPipelineCliTest, ConfigurationRenameCollectsTheFormerOwnedOutput)
{
    ScratchDirectory scratch("orphan_config_rename");
    const std::filesystem::path source = scratch.Path() / "ContentSource";
    const std::filesystem::path output = scratch.Path() / "Content";
    const std::filesystem::path configuration =
        source / Pipeline::ContentBuildConfigurationFileName;
    WriteBytes(source / "Textures" / "wall.png", MakePng(2, 2));

    std::string log;
    ASSERT_EQ(RunTool({"build", source.string(), "-o", output.string()}, log), 0) << log;
    ASSERT_TRUE(std::filesystem::is_regular_file(output / "Textures" / "wall.cnb"));
    WriteText(configuration,
              R"json({"format":"CNA.ContentPipeline.Config","version":1,"assets":{"Textures/wall.png":{"logicalName":"Environment/stone"}}})json");

    ASSERT_EQ(RunTool({"build", source.string(), "-o", output.string()}, log), 0) << log;
    EXPECT_NE(log.find("[CLEAN] Textures/wall.cnb (obsolete manifest-owned output)"),
              std::string::npos)
        << log;
    EXPECT_FALSE(std::filesystem::exists(output / "Textures" / "wall.cnb"));
    EXPECT_TRUE(std::filesystem::is_regular_file(output / "Environment" / "stone.cnb"));
}

TEST(ContentPipelineCliTest, CorruptManifestNeverAuthorizesOrphanDeletion)
{
    ScratchDirectory scratch("orphan_corrupt_manifest");
    const std::filesystem::path source = scratch.Path() / "ContentSource";
    const std::filesystem::path output = scratch.Path() / "Content";
    const std::filesystem::path manifest =
        output / Pipeline::ContentBuildManifestFileName;
    WriteBytes(source / "old.png", MakePng(2, 2));
    std::string log;
    ASSERT_EQ(RunTool({"build", source.string(), "-o", output.string()}, log), 0) << log;
    const std::vector<std::uint8_t> oldOutput = ReadBytes(output / "old.cnb");
    WriteBytes(manifest, {'b', 'a', 'd'});
    ASSERT_TRUE(std::filesystem::remove(source / "old.png"));

    ASSERT_EQ(RunTool({"build", source.string(), "-o", output.string()}, log), 0) << log;
    EXPECT_NE(log.find("[WARN] Ignoring incompatible or corrupt manifest"), std::string::npos)
        << log;
    EXPECT_EQ(ReadBytes(output / "old.cnb"), oldOutput);
    const std::vector<std::uint8_t> manifestBytes = ReadBytes(manifest);
    const Pipeline::ContentBuildManifest parsed = Pipeline::ContentBuildManifest::Parse(
        std::string(manifestBytes.begin(), manifestBytes.end()));
    EXPECT_TRUE(parsed.Entries().empty());
}

TEST(ContentPipelineCliTest, FailedBuildPreservesOutputsOwnedOnlyByTheOldManifest)
{
    ScratchDirectory scratch("orphan_failed_build");
    const std::filesystem::path source = scratch.Path() / "ContentSource";
    const std::filesystem::path output = scratch.Path() / "Content";
    const std::filesystem::path manifest =
        output / Pipeline::ContentBuildManifestFileName;
    WriteBytes(source / "bad.png", MakePng(2, 2));
    WriteBytes(source / "old.png", MakePng(3, 2));
    std::string log;
    ASSERT_EQ(RunTool({"build", source.string(), "-o", output.string()}, log), 0) << log;
    const std::vector<std::uint8_t> oldOutput = ReadBytes(output / "old.cnb");
    const std::vector<std::uint8_t> oldManifest = ReadBytes(manifest);
    ASSERT_TRUE(std::filesystem::remove(source / "old.png"));
    WriteBytes(source / "bad.png", {'n', 'o', 't', 'p', 'n', 'g'});

    EXPECT_EQ(RunTool({"build", source.string(), "-o", output.string()}, log), 1) << log;
    EXPECT_EQ(ReadBytes(output / "old.cnb"), oldOutput);
    EXPECT_EQ(ReadBytes(manifest), oldManifest);
    EXPECT_EQ(log.find("[CLEAN]"), std::string::npos) << log;
}

TEST(ContentPipelineCliTest, ChangedOrSymlinkedObsoleteOutputsAreNeverDeleted)
{
    ScratchDirectory scratch("orphan_destructive_guards");
    const std::filesystem::path source = scratch.Path() / "ContentSource";
    const std::filesystem::path output = scratch.Path() / "Content";
    const std::filesystem::path manifest =
        output / Pipeline::ContentBuildManifestFileName;
    WriteBytes(source / "changed.png", MakePng(2, 2));
    std::string log;
    ASSERT_EQ(RunTool({"build", source.string(), "-o", output.string()}, log), 0) << log;
    const std::vector<std::uint8_t> oldManifest = ReadBytes(manifest);
    WriteBytes(output / "changed.cnb", {'u', 's', 'e', 'r'});
    ASSERT_TRUE(std::filesystem::remove(source / "changed.png"));

    EXPECT_EQ(RunTool({"build", source.string(), "-o", output.string()}, log), 1) << log;
    EXPECT_NE(log.find("bytes no longer match the previous manifest"), std::string::npos)
        << log;
    EXPECT_EQ(ReadBytes(output / "changed.cnb"),
              (std::vector<std::uint8_t>{'u', 's', 'e', 'r'}));
    EXPECT_EQ(ReadBytes(manifest), oldManifest);

    ASSERT_TRUE(std::filesystem::remove(output / "changed.cnb"));
    ASSERT_EQ(RunTool({"build", source.string(), "-o", output.string()}, log), 0) << log;

    WriteBytes(source / "Nested" / "linked.png", MakePng(2, 2));
    ASSERT_EQ(RunTool({"build", source.string(), "-o", output.string()}, log), 0) << log;
    const std::vector<std::uint8_t> linkedManifest = ReadBytes(manifest);
    ASSERT_TRUE(std::filesystem::remove(source / "Nested" / "linked.png"));
    ASSERT_TRUE(std::filesystem::remove(output / "Nested" / "linked.cnb"));
    ASSERT_TRUE(std::filesystem::remove(output / "Nested"));
    const std::filesystem::path outside = scratch.Path() / "outside";
    WriteBytes(outside / "linked.cnb", {'s', 'a', 'f', 'e'});
    std::filesystem::create_directory_symlink(outside, output / "Nested");

    EXPECT_EQ(RunTool({"build", source.string(), "-o", output.string()}, log), 1) << log;
    EXPECT_NE(log.find("output-path parent is not a real directory"), std::string::npos)
        << log;
    EXPECT_EQ(ReadBytes(outside / "linked.cnb"),
              (std::vector<std::uint8_t>{'s', 'a', 'f', 'e'}));
    EXPECT_EQ(ReadBytes(manifest), linkedManifest);
}

TEST(ContentPipelineCliTest, CleanSyntaxIsNarrowAndANonexistentRootIsANoOp)
{
    ScratchDirectory scratch("clean_syntax");
    const std::filesystem::path missing = scratch.Path() / "missing";
    std::string log;
    EXPECT_EQ(RunTool({"clean"}, log), 2) << log;
    EXPECT_NE(log.find("clean requires an output directory"), std::string::npos) << log;
    EXPECT_EQ(RunTool({"clean", missing.string(), "--workers", "2"}, log), 2) << log;
    EXPECT_NE(log.find("unknown clean option '--workers'"), std::string::npos) << log;
    EXPECT_EQ(RunTool({"clean", missing.string(), "--explain"}, log), 2) << log;
    EXPECT_NE(log.find("unknown clean option '--explain'"), std::string::npos) << log;

    ASSERT_EQ(RunTool({"clean", missing.string()}, log), 0) << log;
    EXPECT_NE(log.find("Cleaned: 0  Failed: 0"), std::string::npos) << log;
    EXPECT_FALSE(std::filesystem::exists(missing));

    const std::filesystem::path file = scratch.Path() / "not-a-directory";
    WriteText(file, "keep\n");
    EXPECT_EQ(RunTool({"clean", file.string()}, log), 1) << log;
    EXPECT_NE(log.find("must be a real directory"), std::string::npos) << log;
    EXPECT_EQ(ReadBytes(file),
              (std::vector<std::uint8_t>{'k', 'e', 'e', 'p', '\n'}));
}

TEST(ContentPipelineCliTest, CleanRemovesOnlyManifestOwnedCompiledAndDeploymentFiles)
{
    ScratchDirectory scratch("clean_owned_outputs");
    const std::filesystem::path source = scratch.Path() / "ContentSource";
    const std::filesystem::path output = scratch.Path() / "Content";
    WriteBytes(source / "Textures" / "wall.png", MakePng(2, 2));
    WriteBytes(source / "Music" / "theme.ogg", {0x4Fu, 0x67u, 0x67u, 0x53u, 1u});

    std::string log;
    ASSERT_EQ(RunTool({"build", source.string(), "-o", output.string(), "--workers", "4"},
                      log),
              0)
        << log;
    WriteBytes(output / "Manual" / "preserved.cnb", {'u', 's', 'e', 'r'});
    ASSERT_TRUE(std::filesystem::is_regular_file(output / "Textures" / "wall.cnb"));
    ASSERT_TRUE(std::filesystem::is_regular_file(output / "Music" / "theme.cnb"));
    ASSERT_TRUE(std::filesystem::is_regular_file(output / "Music" / "theme.ogg"));
    ASSERT_TRUE(std::filesystem::is_regular_file(
        output / Pipeline::ContentBuildManifestFileName));
    ASSERT_TRUE(std::filesystem::is_regular_file(
        output / CNA::Tools::ContentOutputLeaseFile));

    ASSERT_EQ(RunTool({"clean", output.string()}, log), 0) << log;
    EXPECT_EQ(CountOccurrences(log, "[CLEAN]"), 4u) << log;
    EXPECT_NE(log.find("[CLEAN] Textures/wall.cnb (manifest-owned output)"),
              std::string::npos)
        << log;
    EXPECT_NE(log.find("[CLEAN] Music/theme.ogg (manifest-owned output)"),
              std::string::npos)
        << log;
    EXPECT_NE(log.find("Cleaned: 3  Failed: 0"), std::string::npos) << log;
    EXPECT_FALSE(std::filesystem::exists(output / "Textures" / "wall.cnb"));
    EXPECT_FALSE(std::filesystem::exists(output / "Music" / "theme.cnb"));
    EXPECT_FALSE(std::filesystem::exists(output / "Music" / "theme.ogg"));
    EXPECT_FALSE(std::filesystem::exists(
        output / Pipeline::ContentBuildManifestFileName));
    EXPECT_EQ(ReadBytes(output / "Manual" / "preserved.cnb"),
              (std::vector<std::uint8_t>{'u', 's', 'e', 'r'}));
    EXPECT_TRUE(std::filesystem::is_regular_file(
        output / CNA::Tools::ContentOutputLeaseFile));
    EXPECT_TRUE(std::filesystem::is_regular_file(source / "Textures" / "wall.png"));
    EXPECT_TRUE(std::filesystem::is_regular_file(source / "Music" / "theme.ogg"));

    ASSERT_EQ(RunTool({"clean", output.string()}, log), 0) << log;
    EXPECT_NE(log.find("Cleaned: 0  Failed: 0"), std::string::npos) << log;
    EXPECT_EQ(ReadBytes(output / "Manual" / "preserved.cnb"),
              (std::vector<std::uint8_t>{'u', 's', 'e', 'r'}));
}

TEST(ContentPipelineCliTest, CleanRejectsUntrustedOwnershipWithoutDeletingAnything)
{
    ScratchDirectory scratch("clean_guards");
    const std::filesystem::path source = scratch.Path() / "ContentSource";
    const std::filesystem::path output = scratch.Path() / "Content";
    const std::filesystem::path manifest =
        output / Pipeline::ContentBuildManifestFileName;
    WriteBytes(source / "a.png", MakePng(2, 2));
    WriteBytes(source / "b.png", MakePng(3, 2));

    std::string log;
    ASSERT_EQ(RunTool({"build", source.string(), "-o", output.string()}, log), 0) << log;
    const std::vector<std::uint8_t> manifestBytes = ReadBytes(manifest);
    const std::vector<std::uint8_t> aBytes = ReadBytes(output / "a.cnb");
    const std::vector<std::uint8_t> bBytes = ReadBytes(output / "b.cnb");

    const std::filesystem::path outputAlias = scratch.Path() / "ContentAlias";
    std::filesystem::create_directory_symlink(output, outputAlias);
    EXPECT_EQ(RunTool({"clean", outputAlias.string()}, log), 1) << log;
    EXPECT_NE(log.find("must be a real directory"), std::string::npos) << log;
    EXPECT_EQ(ReadBytes(output / "a.cnb"), aBytes);
    EXPECT_EQ(ReadBytes(output / "b.cnb"), bBytes);

    const std::filesystem::path outsideManifest = scratch.Path() / "outside-manifest.json";
    WriteBytes(outsideManifest, manifestBytes);
    ASSERT_TRUE(std::filesystem::remove(manifest));
    std::filesystem::create_symlink(outsideManifest, manifest);
    EXPECT_EQ(RunTool({"clean", output.string()}, log), 1) << log;
    EXPECT_NE(log.find("ownership manifest is unreadable, symlinked"), std::string::npos)
        << log;
    EXPECT_EQ(ReadBytes(output / "a.cnb"), aBytes);
    EXPECT_EQ(ReadBytes(output / "b.cnb"), bBytes);
    EXPECT_EQ(ReadBytes(outsideManifest), manifestBytes);
    ASSERT_TRUE(std::filesystem::remove(manifest));

    WriteBytes(manifest, {'b', 'a', 'd'});
    EXPECT_EQ(RunTool({"clean", output.string()}, log), 1) << log;
    EXPECT_TRUE(std::filesystem::is_regular_file(output / "a.cnb"));
    EXPECT_TRUE(std::filesystem::is_regular_file(output / "b.cnb"));

    WriteBytes(manifest, manifestBytes);
    WriteBytes(output / "a.cnb", {'u', 's', 'e', 'r'});
    EXPECT_EQ(RunTool({"clean", output.string()}, log), 1) << log;
    EXPECT_NE(log.find("bytes no longer match"), std::string::npos) << log;
    EXPECT_EQ(ReadBytes(output / "a.cnb"),
              (std::vector<std::uint8_t>{'u', 's', 'e', 'r'}));
    EXPECT_EQ(ReadBytes(output / "b.cnb"), bBytes);
    EXPECT_EQ(ReadBytes(manifest), manifestBytes);

    WriteBytes(output / "a.cnb", aBytes);
    ASSERT_TRUE(std::filesystem::remove(output / "b.cnb"));
    const std::filesystem::path outside = scratch.Path() / "outside.cnb";
    WriteBytes(outside, {'s', 'a', 'f', 'e'});
    std::filesystem::create_symlink(outside, output / "b.cnb");
    EXPECT_EQ(RunTool({"clean", output.string()}, log), 1) << log;
    EXPECT_NE(log.find("not a real regular file"), std::string::npos) << log;
    EXPECT_EQ(ReadBytes(output / "a.cnb"), aBytes);
    EXPECT_EQ(ReadBytes(outside),
              (std::vector<std::uint8_t>{'s', 'a', 'f', 'e'}));
    EXPECT_EQ(ReadBytes(manifest), manifestBytes);

    ASSERT_TRUE(std::filesystem::remove(output / "b.cnb"));
    WriteBytes(output / "b.cnb", bBytes);
    ASSERT_EQ(RunTool({"clean", output.string(), "--quiet"}, log), 0) << log;
    EXPECT_TRUE(log.empty()) << log;
    EXPECT_FALSE(std::filesystem::exists(output / "a.cnb"));
    EXPECT_FALSE(std::filesystem::exists(output / "b.cnb"));
    EXPECT_FALSE(std::filesystem::exists(manifest));
}

TEST(ContentPipelineCliTest, OutputLeaseSerializesBuildAndCleanForOneRoot)
{
    ScratchDirectory scratch("output_lease");
    const std::filesystem::path source = scratch.Path() / "wall.png";
    const std::filesystem::path outputRoot = scratch.Path() / "Content";
    const std::filesystem::path output = outputRoot / "wall.cnb";
    WriteBytes(source, MakePng(2, 2));
    std::filesystem::create_directories(outputRoot);

    CNA::Tools::ContentStagingDetail::LeaseHandle active;
    std::string reason;
    ASSERT_TRUE(active.CreateAndHold(
        outputRoot / CNA::Tools::ContentOutputLeaseFile, reason)) << reason;

    std::string log;
    EXPECT_EQ(RunTool({"build", source.string(), "-o", output.string()}, log), 1) << log;
    EXPECT_NE(log.find("another content build or clean operation is active"),
              std::string::npos)
        << log;
    EXPECT_FALSE(std::filesystem::exists(output));
    EXPECT_EQ(RunTool({"clean", outputRoot.string()}, log), 1) << log;
    EXPECT_NE(log.find("another content build or clean operation is active"),
              std::string::npos)
        << log;

    active.Close();
    ASSERT_TRUE(std::filesystem::remove(
        outputRoot / CNA::Tools::ContentOutputLeaseFile));
    const std::filesystem::path outsideLease = scratch.Path() / "outside.lock";
    WriteText(outsideLease, "user\n");
    std::filesystem::create_symlink(
        outsideLease, outputRoot / CNA::Tools::ContentOutputLeaseFile);
    EXPECT_EQ(RunTool({"build", source.string(), "-o", output.string()}, log), 1) << log;
    EXPECT_NE(log.find("content output lease is unsafe"), std::string::npos) << log;
    EXPECT_EQ(ReadBytes(outsideLease),
              (std::vector<std::uint8_t>{'u', 's', 'e', 'r', '\n'}));
    ASSERT_TRUE(std::filesystem::remove(
        outputRoot / CNA::Tools::ContentOutputLeaseFile));

    ASSERT_EQ(RunTool({"build", source.string(), "-o", output.string()}, log), 0) << log;
    ASSERT_TRUE(std::filesystem::is_regular_file(output));
    ASSERT_EQ(RunTool({"clean", outputRoot.string()}, log), 0) << log;
    EXPECT_FALSE(std::filesystem::exists(output));
}

TEST(ContentPipelineCliTest, UnknownExtensionsFailWithoutPublishingAnOutput)
{
    ScratchDirectory scratch("unknown");
    const std::filesystem::path source = scratch.Path() / "level.unknown";
    const std::filesystem::path output = scratch.Path() / "level.cnb";
    WriteBytes(source, {1u, 2u, 3u});

    std::string log;
    EXPECT_EQ(RunTool({"build", source.string(), "-o", output.string()}, log), 1);
    EXPECT_NE(log.find("no importer is registered"), std::string::npos) << log;
    EXPECT_FALSE(std::filesystem::exists(output));
}

TEST(ContentPipelineCliTest, RefusesDestructiveOrSelfDiscoveringOutputLayouts)
{
    ScratchDirectory scratch("layout");
    const std::filesystem::path sourceFile = scratch.Path() / "wall.png";
    WriteBytes(sourceFile, MakePng(2, 2));
    EXPECT_EQ(RunTool({"build", sourceFile.string(), "-o", sourceFile.string()}), 1);
    EXPECT_EQ(ReadBytes(sourceFile), MakePng(2, 2));

    const std::filesystem::path sourceDirectory = scratch.Path() / "ContentSource";
    WriteBytes(sourceDirectory / "wall.png", MakePng(2, 2));
    EXPECT_EQ(RunTool({"build", sourceDirectory.string(), "-o",
                       (sourceDirectory / "Generated").string()}),
              1);
    EXPECT_FALSE(std::filesystem::exists(sourceDirectory / "Generated"));
}

TEST(ContentPipelineCliTest, SongSingleAndDirectoryBuildsPreserveExternalMediaSemantics)
{
    ScratchDirectory scratch("song_routes");
    const std::filesystem::path source = scratch.Path() / "ContentSource";
    const std::filesystem::path output = scratch.Path() / "Content";
    const std::filesystem::path media = source / "Music" / "theme.ogg";
    WriteBytes(media, {0x4Fu, 0x67u, 0x67u, 0x53u, 1u, 2u, 3u});
    WriteBytes(source / "Textures" / "wall.png", MakePng(2, 2));
    WriteText(
        source / Pipeline::ContentBuildConfigurationFileName,
        R"json({"format":"CNA.ContentPipeline.Config","version":1,"assets":{"Music/theme.ogg":{"parameters":{"name":{"type":"string","value":"Main Theme"},"durationMs":{"type":"u64","value":"185000"}}}}})json");
    WriteText(
        media.parent_path() / Pipeline::ContentBuildConfigurationFileName,
        R"json({"format":"CNA.ContentPipeline.Config","version":1,"assets":{"theme.ogg":{"parameters":{"streamReference":{"type":"string","value":"Music/theme.ogg"},"name":{"type":"string","value":"Main Theme"},"durationMs":{"type":"u64","value":"185000"}}}}})json");

    const std::filesystem::path single = scratch.Path() / "Single" / "theme.cnb";
    std::string singleLog;
    ASSERT_EQ(RunTool({"build", media.string(), "-o", single.string()}, singleLog), 0)
        << singleLog;
    const Cnb::CnbSongData singleSong = Cnb::DecodeSongFromCnb(
        Cnb::CnbDocument::Parse(ReadBytes(single), "single Song route"));
    EXPECT_EQ(singleSong.streamReference, "Music/theme.ogg");
    EXPECT_EQ(singleSong.name, "Main Theme");
    EXPECT_EQ(singleSong.durationMs, 185000u);
    EXPECT_EQ(ReadBytes(scratch.Path() / "Single" / "Music" / "theme.ogg"),
              ReadBytes(media));

    std::string first;
    ASSERT_EQ(RunTool({"build", source.string(), "-o", output.string()}, first), 0) << first;
    EXPECT_NE(first.find("[BUILD] Music/theme"), std::string::npos) << first;
    EXPECT_NE(first.find("[BUILD] Textures/wall"), std::string::npos) << first;
    const std::filesystem::path songOutput = output / "Music" / "theme.cnb";
    const std::filesystem::path deployedMedia = output / "Music" / "theme.ogg";
    EXPECT_TRUE(std::filesystem::is_regular_file(songOutput));
    ASSERT_TRUE(std::filesystem::is_regular_file(deployedMedia));
    EXPECT_EQ(ReadBytes(deployedMedia), ReadBytes(media));

    const std::vector<std::uint8_t> songBytes = ReadBytes(songOutput);
    Cnb::CnbSongData expected;
    expected.streamReference = "Music/theme.ogg";
    expected.name = "Main Theme";
    expected.durationMs = 185000u;
    EXPECT_EQ(songBytes, Cnb::EncodeSongToCnb(expected, "Music/theme"));

    const std::filesystem::path manifestPath =
        output / Pipeline::ContentBuildManifestFileName;
    const std::vector<std::uint8_t> manifestBytes = ReadBytes(manifestPath);
    const Pipeline::ContentBuildManifest manifest = Pipeline::ContentBuildManifest::Parse(
        std::string(manifestBytes.begin(), manifestBytes.end()));
    const Pipeline::ContentBuildManifestEntry* entry = manifest.Find("Music/theme");
    ASSERT_NE(entry, nullptr);
    ASSERT_EQ(entry->runtimeReferences.size(), 1u);
    EXPECT_EQ(entry->runtimeReferences.front().logicalName, "Music/theme.ogg");
    EXPECT_EQ(entry->runtimeReferences.front().expectedAssetTypeId, 0u);
    ASSERT_EQ(entry->deploymentFiles.size(), 1u);
    EXPECT_EQ(entry->deploymentFiles.front().source, "Music/theme.ogg");
    EXPECT_EQ(entry->deploymentFiles.front().path, "Music/theme.ogg");
    EXPECT_EQ(entry->deploymentFiles.front().sha256,
              Pipeline::ContentFileSha256(media));

    std::string second;
    ASSERT_EQ(RunTool({"build", source.string(), "-o", output.string()}, second), 0) << second;
    EXPECT_NE(second.find("[SKIP] Music/theme"), std::string::npos) << second;
    EXPECT_NE(second.find("[SKIP] Textures/wall"), std::string::npos) << second;
    EXPECT_EQ(ReadBytes(songOutput), songBytes);

    WriteBytes(deployedMedia, {'b', 'a', 'd'});
    std::string repaired;
    ASSERT_EQ(RunTool({"build", source.string(), "-o", output.string(), "--explain"}, repaired), 0)
        << repaired;
    EXPECT_NE(repaired.find("[BUILD] Music/theme"), std::string::npos) << repaired;
    EXPECT_NE(repaired.find("[SKIP] Textures/wall"), std::string::npos) << repaired;
    EXPECT_EQ(ReadBytes(deployedMedia), ReadBytes(media));
    EXPECT_EQ(ReadBytes(songOutput), songBytes);

    WriteBytes(media, {0x4Fu, 0x67u, 0x67u, 0x53u, 9u, 8u, 7u});
    std::string changed;
    ASSERT_EQ(RunTool({"build", source.string(), "-o", output.string()}, changed), 0)
        << changed;
    EXPECT_NE(changed.find("[BUILD] Music/theme"), std::string::npos) << changed;
    EXPECT_NE(changed.find("[SKIP] Textures/wall"), std::string::npos) << changed;
    EXPECT_EQ(ReadBytes(songOutput), songBytes);
    EXPECT_EQ(ReadBytes(deployedMedia), ReadBytes(media));
}

TEST(ContentPipelineCliTest, VideoBuildRequiresExplicitFrameMetadata)
{
    ScratchDirectory scratch("video_metadata_required");
    const std::filesystem::path source = scratch.Path() / "intro.mp4";
    const std::filesystem::path output = scratch.Path() / "intro.cnb";
    WriteBytes(source, {0u, 0u, 0u, 24u, 'f', 't', 'y', 'p'});

    std::string log;
    EXPECT_NE(RunTool({"build", source.string(), "-o", output.string()}, log), 0);
    EXPECT_NE(log.find("Process (CNA.VideoProcessor)"), std::string::npos) << log;
    EXPECT_NE(log.find("requires u64 parameter 'width'"), std::string::npos) << log;
    EXPECT_FALSE(std::filesystem::exists(output));
}

TEST(ContentPipelineCliTest, VideoSingleAndDirectoryBuildsUseConfiguredMetadataAndXref)
{
    ScratchDirectory scratch("video_routes");
    const std::filesystem::path source = scratch.Path() / "ContentSource";
    const std::filesystem::path output = scratch.Path() / "Content";
    const std::filesystem::path media = source / "Movies" / "intro.mp4";
    WriteBytes(media, {0u, 0u, 0u, 24u, 'f', 't', 'y', 'p'});
    WriteBytes(source / "Textures" / "wall.png", MakePng(2, 2));
    const std::string metadata =
        R"json("durationMs":{"type":"u64","value":"42000"},"width":{"type":"u64","value":"1920"},"height":{"type":"u64","value":"1080"},"framesPerSecond":{"type":"f64","value":"29.97"},"soundtrackType":{"type":"u64","value":"2"})json";
    WriteText(source / Pipeline::ContentBuildConfigurationFileName,
              std::string(R"json({"format":"CNA.ContentPipeline.Config","version":1,"assets":{"Movies/intro.mp4":{"parameters":{)json") +
                  metadata + "}}}}");
    WriteText(media.parent_path() / Pipeline::ContentBuildConfigurationFileName,
              std::string(R"json({"format":"CNA.ContentPipeline.Config","version":1,"assets":{"intro.mp4":{"parameters":{"streamReference":{"type":"string","value":"Movies/intro.mp4"},)json") +
                  metadata + "}}}}");

    const std::filesystem::path single = scratch.Path() / "Single" / "intro.cnb";
    std::string singleLog;
    ASSERT_EQ(RunTool({"build", media.string(), "-o", single.string()}, singleLog), 0)
        << singleLog;
    const Cnb::CnbVideoData singleVideo = Cnb::DecodeVideoFromCnb(
        Cnb::CnbDocument::Parse(ReadBytes(single), "single Video route"));
    EXPECT_EQ(singleVideo.streamReference, "Movies/intro.mp4");
    EXPECT_EQ(singleVideo.width, 1920u);
    EXPECT_EQ(singleVideo.height, 1080u);
    EXPECT_FLOAT_EQ(singleVideo.framesPerSecond, 29.97f);
    EXPECT_EQ(singleVideo.soundtrackType, 2u);
    EXPECT_EQ(ReadBytes(scratch.Path() / "Single" / "Movies" / "intro.mp4"),
              ReadBytes(media));

    std::string first;
    ASSERT_EQ(RunTool({"build", source.string(), "-o", output.string()}, first), 0) << first;
    EXPECT_NE(first.find("[BUILD] Movies/intro"), std::string::npos) << first;
    EXPECT_NE(first.find("[BUILD] Textures/wall"), std::string::npos) << first;
    const std::filesystem::path videoOutput = output / "Movies" / "intro.cnb";
    const std::filesystem::path deployedMedia = output / "Movies" / "intro.mp4";
    EXPECT_TRUE(std::filesystem::is_regular_file(videoOutput));
    ASSERT_TRUE(std::filesystem::is_regular_file(deployedMedia));
    EXPECT_EQ(ReadBytes(deployedMedia), ReadBytes(media));

    const std::vector<std::uint8_t> videoBytes = ReadBytes(videoOutput);
    Cnb::CnbVideoData expected;
    expected.streamReference = "Movies/intro.mp4";
    expected.durationMs = 42000u;
    expected.width = 1920u;
    expected.height = 1080u;
    expected.framesPerSecond = 29.97f;
    expected.soundtrackType = 2u;
    EXPECT_EQ(videoBytes, Cnb::EncodeVideoToCnb(expected, "Movies/intro"));

    const std::vector<std::uint8_t> manifestBytes =
        ReadBytes(output / Pipeline::ContentBuildManifestFileName);
    const Pipeline::ContentBuildManifest manifest = Pipeline::ContentBuildManifest::Parse(
        std::string(manifestBytes.begin(), manifestBytes.end()));
    const Pipeline::ContentBuildManifestEntry* entry = manifest.Find("Movies/intro");
    ASSERT_NE(entry, nullptr);
    ASSERT_EQ(entry->runtimeReferences.size(), 1u);
    EXPECT_EQ(entry->runtimeReferences.front().logicalName, "Movies/intro.mp4");
    ASSERT_EQ(entry->deploymentFiles.size(), 1u);
    EXPECT_EQ(entry->deploymentFiles.front().source, "Movies/intro.mp4");
    EXPECT_EQ(entry->deploymentFiles.front().path, "Movies/intro.mp4");
    EXPECT_EQ(entry->deploymentFiles.front().sha256,
              Pipeline::ContentFileSha256(media));

    std::string second;
    ASSERT_EQ(RunTool({"build", source.string(), "-o", output.string()}, second), 0) << second;
    EXPECT_NE(second.find("[SKIP] Movies/intro"), std::string::npos) << second;
    EXPECT_NE(second.find("[SKIP] Textures/wall"), std::string::npos) << second;

    WriteBytes(deployedMedia, {'b', 'a', 'd'});
    std::string repaired;
    ASSERT_EQ(RunTool({"build", source.string(), "-o", output.string(), "--explain"}, repaired), 0)
        << repaired;
    EXPECT_NE(repaired.find("[BUILD] Movies/intro"), std::string::npos) << repaired;
    EXPECT_NE(repaired.find("[SKIP] Textures/wall"), std::string::npos) << repaired;
    EXPECT_NE(repaired.find(
                  "reason: deployment output digest mismatch: Movies/intro.mp4"),
              std::string::npos)
        << repaired;
    EXPECT_EQ(ReadBytes(deployedMedia), ReadBytes(media));
    EXPECT_EQ(ReadBytes(videoOutput), videoBytes);

    ASSERT_TRUE(std::filesystem::remove(deployedMedia));
    repaired.clear();
    ASSERT_EQ(RunTool({"build", source.string(), "-o", output.string(), "--explain"}, repaired), 0)
        << repaired;
    EXPECT_NE(repaired.find("reason: deployment output missing: Movies/intro.mp4"),
              std::string::npos)
        << repaired;
    EXPECT_EQ(ReadBytes(deployedMedia), ReadBytes(media));

    WriteBytes(media, {0u, 0u, 0u, 24u, 'f', 't', 'y', 'p', 9u});
    std::string changed;
    ASSERT_EQ(RunTool({"build", source.string(), "-o", output.string()}, changed), 0)
        << changed;
    EXPECT_NE(changed.find("[BUILD] Movies/intro"), std::string::npos) << changed;
    EXPECT_NE(changed.find("[SKIP] Textures/wall"), std::string::npos) << changed;
    EXPECT_EQ(ReadBytes(videoOutput), videoBytes);
    EXPECT_EQ(ReadBytes(deployedMedia), ReadBytes(media));
}

TEST(ContentPipelineCliTest, DeploymentFilesFollowConfiguredPathsAndOwnedGarbageCollection)
{
    ScratchDirectory scratch("deployment_path_gc");
    const std::filesystem::path source = scratch.Path() / "ContentSource";
    const std::filesystem::path output = scratch.Path() / "Content";
    const std::filesystem::path media = source / "Music" / "theme.ogg";
    const std::filesystem::path configuration =
        source / Pipeline::ContentBuildConfigurationFileName;
    WriteBytes(media, {0x4Fu, 0x67u, 0x67u, 0x53u, 1u, 2u, 3u});
    WriteBytes(source / "keep.png", MakePng(2, 2));
    WriteBytes(output / "Manual" / "preserved.cnb", {'u', 's', 'e', 'r'});
    WriteText(configuration,
              R"json({"format":"CNA.ContentPipeline.Config","version":1,"assets":{"Music/theme.ogg":{"parameters":{"streamReference":{"type":"string","value":"Deploy/theme.ogg"}}}}})json");

    std::string log;
    ASSERT_EQ(RunTool({"build", source.string(), "-o", output.string(), "--workers", "2"},
                      log),
              0)
        << log;
    EXPECT_EQ(ReadBytes(output / "Deploy" / "theme.ogg"), ReadBytes(media));
    EXPECT_EQ(Cnb::DecodeSongFromCnb(Cnb::CnbDocument::ParseFile(
                  (output / "Music" / "theme.cnb").string())).streamReference,
              "Deploy/theme.ogg");

    WriteText(configuration,
              R"json({"format":"CNA.ContentPipeline.Config","version":1,"assets":{"Music/theme.ogg":{"parameters":{"streamReference":{"type":"string","value":"Streaming/theme.ogg"}}}}})json");
    ASSERT_EQ(RunTool({"build", source.string(), "-o", output.string(), "--workers", "4",
                       "--explain"},
                      log),
              0)
        << log;
    EXPECT_NE(log.find("reason: processor parameters changed"), std::string::npos) << log;
    EXPECT_NE(log.find("reason: deployment definition set changed"), std::string::npos)
        << log;
    EXPECT_NE(log.find("[CLEAN] Deploy/theme.ogg (obsolete manifest-owned output)"),
              std::string::npos)
        << log;
    EXPECT_FALSE(std::filesystem::exists(output / "Deploy" / "theme.ogg"));
    EXPECT_EQ(ReadBytes(output / "Streaming" / "theme.ogg"), ReadBytes(media));
    EXPECT_EQ(Cnb::DecodeSongFromCnb(Cnb::CnbDocument::ParseFile(
                  (output / "Music" / "theme.cnb").string())).streamReference,
              "Streaming/theme.ogg");

    ASSERT_TRUE(std::filesystem::remove(media));
    WriteText(configuration,
              R"json({"format":"CNA.ContentPipeline.Config","version":1,"assets":{}})json");
    ASSERT_EQ(RunTool({"build", source.string(), "-o", output.string()}, log), 0) << log;
    EXPECT_FALSE(std::filesystem::exists(output / "Music" / "theme.cnb"));
    EXPECT_FALSE(std::filesystem::exists(output / "Streaming" / "theme.ogg"));
    EXPECT_TRUE(std::filesystem::is_regular_file(output / "keep.cnb"));
    EXPECT_EQ(ReadBytes(output / "Manual" / "preserved.cnb"),
              (std::vector<std::uint8_t>{'u', 's', 'e', 'r'}));
}

TEST(ContentPipelineCliTest, DeploymentFilesNeverOverwriteCompiledOrAuthoredSources)
{
    ScratchDirectory scratch("deployment_collisions");
    const std::filesystem::path source = scratch.Path() / "ContentSource";
    const std::filesystem::path output = scratch.Path() / "Content";
    const std::filesystem::path media = source / "Music" / "theme.ogg";
    WriteBytes(media, {0x4Fu, 0x67u, 0x67u, 0x53u});
    WriteText(source / Pipeline::ContentBuildConfigurationFileName,
              R"json({"format":"CNA.ContentPipeline.Config","version":1,"assets":{"Music/theme.ogg":{"parameters":{"streamReference":{"type":"string","value":"Music/theme.cnb"}}}}})json");
    std::string log;
    EXPECT_EQ(RunTool({"build", source.string(), "-o", output.string()}, log), 1) << log;
    EXPECT_NE(log.find("resolves multiple outputs to path"), std::string::npos) << log;
    EXPECT_FALSE(std::filesystem::exists(output / "Music" / "theme.cnb"));
    EXPECT_FALSE(std::filesystem::exists(output / Pipeline::ContentBuildManifestFileName));

    const std::filesystem::path sharedSource = scratch.Path() / "SharedSource";
    const std::filesystem::path sharedOutput = scratch.Path() / "SharedOutput";
    WriteBytes(sharedSource / "a.ogg", {1u});
    WriteBytes(sharedSource / "b.ogg", {2u});
    WriteText(sharedSource / Pipeline::ContentBuildConfigurationFileName,
              R"json({"format":"CNA.ContentPipeline.Config","version":1,"assets":{"a.ogg":{"parameters":{"streamReference":{"type":"string","value":"Shared/audio.ogg"}}},"b.ogg":{"parameters":{"streamReference":{"type":"string","value":"Shared/audio.ogg"}}}}})json");
    EXPECT_EQ(RunTool({"build", sharedSource.string(), "-o", sharedOutput.string(),
                       "--workers", "4"},
                      log),
              1)
        << log;
    EXPECT_NE(log.find("resolve outputs to the same path"), std::string::npos) << log;
    EXPECT_FALSE(std::filesystem::exists(
        sharedOutput / Pipeline::ContentBuildManifestFileName));
    EXPECT_FALSE(std::filesystem::exists(sharedOutput / "a.cnb"));
    EXPECT_FALSE(std::filesystem::exists(sharedOutput / "b.cnb"));

    const std::filesystem::path singleRoot = scratch.Path() / "SingleSource";
    const std::filesystem::path direct = singleRoot / "direct.ogg";
    const std::filesystem::path directOutput = singleRoot / "direct.cnb";
    WriteBytes(direct, {1u, 2u, 3u});
    ASSERT_EQ(RunTool({"build", direct.string(), "-o", directOutput.string()}, log), 0) << log;
    EXPECT_EQ(ReadBytes(direct), (std::vector<std::uint8_t>{1u, 2u, 3u}));
    const std::vector<std::uint8_t> manifestBytes =
        ReadBytes(singleRoot / Pipeline::ContentBuildManifestFileName);
    const Pipeline::ContentBuildManifest manifest = Pipeline::ContentBuildManifest::Parse(
        std::string(manifestBytes.begin(), manifestBytes.end()));
    ASSERT_NE(manifest.Find("direct"), nullptr);
    EXPECT_TRUE(manifest.Find("direct")->deploymentFiles.empty());

    WriteBytes(singleRoot / "authored.ogg", {'k', 'e', 'e', 'p'});
    WriteText(singleRoot / Pipeline::ContentBuildConfigurationFileName,
              R"json({"format":"CNA.ContentPipeline.Config","version":1,"assets":{"direct.ogg":{"parameters":{"streamReference":{"type":"string","value":"authored.ogg"}}}}})json");
    EXPECT_EQ(RunTool({"build", direct.string(), "-o", directOutput.string()}, log), 1) << log;
    EXPECT_NE(log.find("inside the source root and could overwrite authored content"),
              std::string::npos)
        << log;
    EXPECT_EQ(ReadBytes(singleRoot / "authored.ogg"),
              (std::vector<std::uint8_t>{'k', 'e', 'e', 'p'}));
}

TEST(ContentPipelineCliTest, DeploymentPublicationFailureKeepsOldManifestAndRecovers)
{
    if (::geteuid() == 0)
    {
        GTEST_SKIP() << "running as root, which ignores the directory permissions this test needs";
    }

    ScratchDirectory scratch("deployment_recovery");
    const std::filesystem::path source = scratch.Path() / "ContentSource";
    const std::filesystem::path output = scratch.Path() / "Content";
    const std::filesystem::path media = source / "Music" / "theme.ogg";
    const std::filesystem::path deployed = output / "Deploy" / "theme.ogg";
    const std::filesystem::path manifest =
        output / Pipeline::ContentBuildManifestFileName;
    WriteBytes(media, {1u, 2u, 3u});
    WriteText(source / Pipeline::ContentBuildConfigurationFileName,
              R"json({"format":"CNA.ContentPipeline.Config","version":1,"assets":{"Music/theme.ogg":{"parameters":{"streamReference":{"type":"string","value":"Deploy/theme.ogg"}}}}})json");
    std::string log;
    ASSERT_EQ(RunTool({"build", source.string(), "-o", output.string()}, log), 0) << log;
    const std::vector<std::uint8_t> oldDeployment = ReadBytes(deployed);
    const std::vector<std::uint8_t> oldManifest = ReadBytes(manifest);

    WriteBytes(media, {4u, 5u, 6u, 7u});
    std::error_code permissionError;
    std::filesystem::permissions(deployed.parent_path(), std::filesystem::perms::owner_write,
                                 std::filesystem::perm_options::remove, permissionError);
    ASSERT_FALSE(permissionError);
    const int failed =
        RunTool({"build", source.string(), "-o", output.string(), "--quiet"}, log);
    std::filesystem::permissions(deployed.parent_path(), std::filesystem::perms::owner_write,
                                 std::filesystem::perm_options::add, permissionError);
    ASSERT_FALSE(permissionError);

    EXPECT_EQ(failed, 1) << log;
    EXPECT_NE(log.find("Publish (CNA.AtomicPublisher)"), std::string::npos) << log;
    EXPECT_EQ(ReadBytes(deployed), oldDeployment);
    EXPECT_EQ(ReadBytes(manifest), oldManifest);

    ASSERT_EQ(RunTool({"build", source.string(), "-o", output.string()}, log), 0) << log;
    EXPECT_EQ(ReadBytes(deployed), ReadBytes(media));
    EXPECT_NE(ReadBytes(manifest), oldManifest);
    ASSERT_EQ(RunTool({"build", source.string(), "-o", output.string()}, log), 0) << log;
    EXPECT_NE(log.find("[SKIP] Music/theme"), std::string::npos) << log;
}

#if defined(CNA_CUSTOM_CONTENT_COMPILER_PATH)
TEST(ContentPipelineCliTest, ExplainClassifiesContentDependencySetChanges)
{
    ScratchDirectory scratch("explain_content_dependency_set");
    const std::filesystem::path source = scratch.Path() / "ContentSource";
    const std::filesystem::path output = scratch.Path() / "Content";
    const std::filesystem::path configuration =
        source / Pipeline::ContentBuildConfigurationFileName;
    WriteText(source / "A" / "dependent.greeting", "dependent\n");
    WriteText(source / "Z" / "shared.greeting", "shared\n");
    std::string log;
    ASSERT_EQ(RunExecutable(CNA_CUSTOM_CONTENT_COMPILER_PATH,
                            {"build", source.string(), "-o", output.string(),
                             "--quiet"}, log),
              0);

    WriteText(
        configuration,
        R"json({"format":"CNA.ContentPipeline.Config","version":1,"assets":{"A/dependent.greeting":{"parameters":{"dependsOn":{"type":"string","value":"Z/shared"}}}}})json");
    log.clear();
    ASSERT_EQ(RunExecutable(CNA_CUSTOM_CONTENT_COMPILER_PATH,
                            {"build", source.string(), "-o", output.string(),
                             "--workers", "4", "--explain"}, log),
              0)
        << log;
    EXPECT_NE(log.find("reason: content-build dependency set changed"),
              std::string::npos)
        << log;
}

TEST(ContentPipelineCliTest, WorkerCountsProduceIdenticalColdNoOpAndDependencyRebuilds)
{
    ScratchDirectory scratch("worker_determinism");
    const std::filesystem::path source = scratch.Path() / "ContentSource";
    const std::filesystem::path output = scratch.Path() / "Content";
    WriteText(source / "A" / "first.greeting", "first\n");
    WriteText(source / "B" / "second.greeting", "second\n");
    WriteText(source / "C" / "independent.greeting", "independent\n");
    WriteText(source / "Z" / "shared.greeting", "shared-v1\n");
    WriteBytes(source / "Sounds" / "effect.wav", MakeWav());
    WriteBytes(source / "Textures" / "badge.png", MakePng(5, 4));
    WriteText(
        source / Pipeline::ContentBuildConfigurationFileName,
        R"json({"format":"CNA.ContentPipeline.Config","version":1,"assets":{"A/first.greeting":{"parameters":{"dependsOn":{"type":"string","value":"Z/shared"}}},"B/second.greeting":{"parameters":{"dependsOn":{"type":"string","value":"Z/shared"}}}}})json");

    const auto run = [&](std::size_t workers, std::string& log)
    {
        return RunExecutable(
            CNA_CUSTOM_CONTENT_COMPILER_PATH,
            {"build", source.string(), "-o", output.string(), "--workers",
             std::to_string(workers), "--explain"},
            log);
    };

    std::string serialColdLog;
    ASSERT_EQ(run(1u, serialColdLog), 0) << serialColdLog;
    const FileTreeSnapshot coldSnapshot = SnapshotFileTree(output);
    ASSERT_FALSE(coldSnapshot.empty());
    const std::size_t sharedPosition = serialColdLog.find("[BUILD] Z/shared");
    const std::size_t firstPosition = serialColdLog.find("[BUILD] A/first");
    const std::size_t secondPosition = serialColdLog.find("[BUILD] B/second");
    ASSERT_NE(sharedPosition, std::string::npos) << serialColdLog;
    ASSERT_NE(firstPosition, std::string::npos) << serialColdLog;
    ASSERT_NE(secondPosition, std::string::npos) << serialColdLog;
    EXPECT_LT(sharedPosition, firstPosition);
    EXPECT_LT(sharedPosition, secondPosition);
    EXPECT_NE(serialColdLog.find("reason: manifest unavailable"), std::string::npos)
        << serialColdLog;

    for (const std::size_t workers : {2u, 4u})
    {
        RestoreFileTree(output, {});
        std::string parallelColdLog;
        ASSERT_EQ(run(workers, parallelColdLog), 0) << parallelColdLog;
        EXPECT_EQ(parallelColdLog, serialColdLog);
        EXPECT_EQ(SnapshotFileTree(output), coldSnapshot);
    }

    const FileTreeSnapshot noOpBefore = SnapshotFileTree(output);
    std::string serialNoOpLog;
    ASSERT_EQ(run(1u, serialNoOpLog), 0) << serialNoOpLog;
    EXPECT_NE(serialNoOpLog.find("Built: 0  Skipped: 6  Failed: 0"), std::string::npos)
        << serialNoOpLog;
    EXPECT_EQ(CountOccurrences(
                  serialNoOpLog,
                  "reason: fingerprint and published output digests unchanged"),
              6u)
        << serialNoOpLog;
    for (const std::size_t workers : {2u, 4u})
    {
        std::string parallelNoOpLog;
        ASSERT_EQ(run(workers, parallelNoOpLog), 0) << parallelNoOpLog;
        EXPECT_EQ(parallelNoOpLog, serialNoOpLog);
        EXPECT_EQ(SnapshotFileTree(output), noOpBefore);
    }

    WriteText(source / "Z" / "shared.greeting", "shared-v2\n");
    std::string serialChangedLog;
    ASSERT_EQ(run(1u, serialChangedLog), 0) << serialChangedLog;
    const FileTreeSnapshot changedSnapshot = SnapshotFileTree(output);
    EXPECT_EQ(CountOccurrences(serialChangedLog, "[BUILD] Z/shared"), 1u)
        << serialChangedLog;
    EXPECT_EQ(CountOccurrences(serialChangedLog, "[BUILD] A/first"), 1u)
        << serialChangedLog;
    EXPECT_EQ(CountOccurrences(serialChangedLog, "[BUILD] B/second"), 1u)
        << serialChangedLog;
    EXPECT_EQ(CountOccurrences(
                  serialChangedLog,
                  "reason: content-build dependency effective fingerprint changed"),
              2u)
        << serialChangedLog;

    RestoreFileTree(output, noOpBefore);
    std::string parallelChangedLog;
    ASSERT_EQ(run(4u, parallelChangedLog), 0) << parallelChangedLog;
    EXPECT_EQ(parallelChangedLog, serialChangedLog);
    EXPECT_EQ(SnapshotFileTree(output), changedSnapshot);
}

TEST(ContentPipelineCliTest, UserBuiltCompilerCombinesCustomAndBuiltInRoutesEndToEnd)
{
    ScratchDirectory scratch("custom_compiler");
    const std::filesystem::path source = scratch.Path() / "ContentSource";
    const std::filesystem::path output = scratch.Path() / "Content";
    WriteText(source / "Messages" / "welcome.greeting", "CNA\n");
    WriteBytes(source / "Textures" / "badge.png", MakePng(2, 2));
    WriteText(
        source / Pipeline::ContentBuildConfigurationFileName,
        R"json({"format":"CNA.ContentPipeline.Config","version":1,"assets":{"Messages/welcome.greeting":{"parameters":{"prefix":{"type":"string","value":"Hello, "}}}}})json");

    std::string firstLog;
    ASSERT_EQ(RunExecutable(CNA_CUSTOM_CONTENT_COMPILER_PATH,
                            {"build", source.string(), "-o", output.string()}, firstLog),
              0)
        << firstLog;
    EXPECT_NE(firstLog.find("ExampleGame.GreetingImporter -> "
                            "ExampleGame.GreetingProcessor -> ExampleGame.GreetingWriter"),
              std::string::npos)
        << firstLog;
    EXPECT_NE(firstLog.find("CNA.ImageImporter -> CNA.TextureProcessor -> "
                            "CNA.Texture2DContentWriter"),
              std::string::npos)
        << firstLog;

    const std::filesystem::path greetingPath = output / "Messages" / "welcome.cnb";
    const std::filesystem::path replyPath =
        output / "Generated" / "Messages" / "welcome-reply.cnb";
    const std::filesystem::path texturePath = output / "Textures" / "badge.cnb";
    ASSERT_TRUE(std::filesystem::is_regular_file(greetingPath));
    ASSERT_TRUE(std::filesystem::is_regular_file(replyPath));
    ASSERT_TRUE(std::filesystem::is_regular_file(texturePath));

    const Cnb::CnbDocument greeting =
        Cnb::CnbDocument::Parse(ReadBytes(greetingPath), "custom compiler greeting");
    EXPECT_EQ(greeting.AssetTypeId(), Cnb::CnbAssetTypeIdFromName("ExampleGame.Greeting"));
    EXPECT_EQ(greeting.AssetSchemaVersion(), 1u);
    EXPECT_EQ(greeting.Metadata().assetTypeName, "ExampleGame.Greeting");
    EXPECT_EQ(greeting.Metadata().contentName, "Messages/welcome");
    Cnb::CnbByteReader text =
        greeting.OpenChunk(greeting.RequireSingle(Cnb::MakeChunkId('T', 'X', 'T', '0')));
    EXPECT_EQ(text.ReadString(), "Hello, CNA");
    EXPECT_NO_THROW(text.RequireExhausted());

    const std::vector<std::uint8_t> replyBytes = ReadBytes(replyPath);
    const Cnb::CnbDocument reply =
        Cnb::CnbDocument::Parse(replyBytes, "custom compiler generated reply");
    EXPECT_EQ(reply.Metadata().contentName, "Generated/Messages/welcome-reply");
    Cnb::CnbByteReader replyText =
        reply.OpenChunk(reply.RequireSingle(Cnb::MakeChunkId('T', 'X', 'T', '0')));
    EXPECT_EQ(replyText.ReadString(), "Reply: Hello, CNA");
    EXPECT_NO_THROW(replyText.RequireExhausted());

    const Cnb::CnbDocument texture =
        Cnb::CnbDocument::Parse(ReadBytes(texturePath), "custom compiler built-in texture");
    EXPECT_EQ(texture.AssetTypeId(), Cnb::CnbAssetTypeId::Texture2D);

    const std::filesystem::path manifestPath =
        output / Pipeline::ContentBuildManifestFileName;
    const std::vector<std::uint8_t> manifestBytes = ReadBytes(manifestPath);
    const Pipeline::ContentBuildManifest manifest = Pipeline::ContentBuildManifest::Parse(
        std::string(manifestBytes.begin(), manifestBytes.end()));
    const Pipeline::ContentBuildManifestEntry* greetingEntry =
        manifest.Find("Messages/welcome");
    ASSERT_NE(greetingEntry, nullptr);
    EXPECT_EQ(greetingEntry->importer.name, "ExampleGame.GreetingImporter");
    EXPECT_EQ(greetingEntry->processor.name, "ExampleGame.GreetingProcessor");
    EXPECT_EQ(greetingEntry->writer.name, "ExampleGame.GreetingWriter");
    EXPECT_EQ(greetingEntry->writer.version, "2");
    EXPECT_EQ(greetingEntry->writerSchemas,
              (std::vector<Pipeline::ContentWriterSchemaIdentity>{
                  {Cnb::CnbAssetTypeIdFromName("ExampleGame.Greeting"), 1u,
                   "ExampleGame.Greeting",
                   {"ExampleGame.EncodeGreetingToCnb", "1"}}}));
    ASSERT_EQ(greetingEntry->outputs.size(), 2u);
    EXPECT_EQ(greetingEntry->outputs.front().assetSchemaVersion, 1u);
    EXPECT_EQ(greetingEntry->outputs.front().assetTypeName, "ExampleGame.Greeting");
    const auto generated = std::find_if(
        greetingEntry->outputs.begin(), greetingEntry->outputs.end(), [](const auto& value)
    {
        return value.logicalName == "Generated/Messages/welcome-reply";
    });
    ASSERT_NE(generated, greetingEntry->outputs.end());
    EXPECT_EQ(generated->path, "Generated/Messages/welcome-reply.cnb");
    ASSERT_NE(greetingEntry->parameters.Find("prefix"), nullptr);
    EXPECT_EQ(std::get<std::string>(*greetingEntry->parameters.Find("prefix")), "Hello, ");

    const std::vector<std::uint8_t> greetingBytes = ReadBytes(greetingPath);
    std::string secondLog;
    ASSERT_EQ(RunExecutable(CNA_CUSTOM_CONTENT_COMPILER_PATH,
                            {"build", source.string(), "-o", output.string()}, secondLog),
              0)
        << secondLog;
    EXPECT_NE(secondLog.find("[SKIP] Messages/welcome"), std::string::npos) << secondLog;
    EXPECT_NE(secondLog.find("[SKIP] Textures/badge"), std::string::npos) << secondLog;
    EXPECT_EQ(ReadBytes(greetingPath), greetingBytes);
    EXPECT_EQ(ReadBytes(manifestPath), manifestBytes);

    WriteBytes(replyPath, {0xBAu, 0xD0u});
    std::string repairLog;
    ASSERT_EQ(RunExecutable(CNA_CUSTOM_CONTENT_COMPILER_PATH,
                            {"build", source.string(), "-o", output.string()}, repairLog),
              0)
        << repairLog;
    EXPECT_NE(repairLog.find("[BUILD] Messages/welcome"), std::string::npos) << repairLog;
    EXPECT_NE(repairLog.find("[SKIP] Textures/badge"), std::string::npos) << repairLog;
    EXPECT_EQ(ReadBytes(replyPath), replyBytes);
    EXPECT_EQ(ReadBytes(manifestPath), manifestBytes);

    WriteBytes(output / "manual.cnb", {'k', 'e', 'e', 'p'});
    std::string cleanLog;
    ASSERT_EQ(RunExecutable(CNA_CUSTOM_CONTENT_COMPILER_PATH,
                            {"clean", output.string()}, cleanLog),
              0)
        << cleanLog;
    EXPECT_NE(cleanLog.find(
                  "[CLEAN] Generated/Messages/welcome-reply.cnb (manifest-owned output)"),
              std::string::npos)
        << cleanLog;
    EXPECT_NE(cleanLog.find("Cleaned: 3  Failed: 0"), std::string::npos) << cleanLog;
    EXPECT_FALSE(std::filesystem::exists(greetingPath));
    EXPECT_FALSE(std::filesystem::exists(replyPath));
    EXPECT_FALSE(std::filesystem::exists(texturePath));
    EXPECT_FALSE(std::filesystem::exists(manifestPath));
    EXPECT_EQ(ReadBytes(output / "manual.cnb"),
              (std::vector<std::uint8_t>{'k', 'e', 'e', 'p'}));
}

TEST(ContentPipelineCliTest, ExternalDeploymentIsExplicitStableAndNeverOwnsItsSourceRoot)
{
    ScratchDirectory scratch("external_deployment");
    const std::filesystem::path source = scratch.Path() / "ContentSource";
    const std::filesystem::path sharedA = scratch.Path() / "SharedA";
    const std::filesystem::path sharedB = scratch.Path() / "SharedB";
    const std::filesystem::path outputOne = scratch.Path() / "Content1";
    const std::filesystem::path outputFour = scratch.Path() / "Content4";
    WriteText(source / "welcome.greeting", "CNA\n");
    WriteText(sharedA / "Support" / "message.txt", "shared support\n");
    WriteText(sharedB / "Support" / "message.txt", "shared support\n");
    WriteText(sharedA / "sentinel.keep", "never owned\n");
    WriteText(sharedB / "sentinel.keep", "never owned\n");

    const auto configure = [&](const std::filesystem::path& root, bool deploy)
    {
        const std::string parameters = deploy
            ? R"("parameters":{"deploymentSource":{"type":"string","value":"@shared/Support/message.txt"},"deploymentOutput":{"type":"string","value":"Support/message.txt"}})"
            : std::string{};
        WriteText(source / Pipeline::ContentBuildConfigurationFileName,
                  std::string(R"({"format":"CNA.ContentPipeline.Config","version":1,"sourceRoots":{"shared":")") +
                      CNA::Internal::ContentPathToUtf8(root) +
                      R"("},"assets":{"welcome.greeting":{)" + parameters + "}}}");
    };
    configure(sharedA, true);

    std::string log;
    ASSERT_EQ(RunExecutable(CNA_CUSTOM_CONTENT_COMPILER_PATH,
                            {"build", source.string(), "-o", outputOne.string(),
                             "--workers", "1", "--explain"}, log), 0)
        << log;
    ASSERT_EQ(RunExecutable(CNA_CUSTOM_CONTENT_COMPILER_PATH,
                            {"build", source.string(), "-o", outputFour.string(),
                             "--workers", "4", "--explain"}, log), 0)
        << log;
    EXPECT_EQ(SnapshotFileTree(outputOne), SnapshotFileTree(outputFour));
    EXPECT_EQ(ReadBytes(outputOne / "Support" / "message.txt"),
              ReadBytes(sharedA / "Support" / "message.txt"));

    const std::filesystem::path manifestPath =
        outputOne / Pipeline::ContentBuildManifestFileName;
    const std::vector<std::uint8_t> firstManifest = ReadBytes(manifestPath);
    const Pipeline::ContentBuildManifest manifest = Pipeline::ContentBuildManifest::Parse(
        std::string(firstManifest.begin(), firstManifest.end()));
    const Pipeline::ContentBuildManifestEntry* entry = manifest.Find("welcome");
    ASSERT_NE(entry, nullptr);
    ASSERT_EQ(entry->deploymentFiles.size(), 1u);
    EXPECT_EQ(entry->deploymentFiles[0].sourceRoot, "shared");
    EXPECT_EQ(entry->deploymentFiles[0].source, "Support/message.txt");
    EXPECT_EQ(entry->deploymentFiles[0].path, "Support/message.txt");

    configure(sharedB, true);
    log.clear();
    ASSERT_EQ(RunExecutable(CNA_CUSTOM_CONTENT_COMPILER_PATH,
                            {"build", source.string(), "-o", outputOne.string(),
                             "--workers", "4", "--explain"}, log), 0)
        << log;
    EXPECT_NE(log.find("[SKIP] welcome"), std::string::npos) << log;
    EXPECT_EQ(ReadBytes(manifestPath), firstManifest);

    WriteText(sharedB / "Support" / "message.txt", "changed support\n");
    log.clear();
    ASSERT_EQ(RunExecutable(CNA_CUSTOM_CONTENT_COMPILER_PATH,
                            {"build", source.string(), "-o", outputOne.string(),
                             "--workers", "4", "--explain"}, log), 0)
        << log;
    EXPECT_NE(log.find("reason: source dependency bytes changed"), std::string::npos) << log;
    EXPECT_EQ(ReadBytes(outputOne / "Support" / "message.txt"),
              ReadBytes(sharedB / "Support" / "message.txt"));

    configure(sharedB, false);
    log.clear();
    ASSERT_EQ(RunExecutable(CNA_CUSTOM_CONTENT_COMPILER_PATH,
                            {"build", source.string(), "-o", outputOne.string()}, log), 0)
        << log;
    EXPECT_FALSE(std::filesystem::exists(outputOne / "Support" / "message.txt"));
    EXPECT_TRUE(std::filesystem::is_regular_file(sharedB / "Support" / "message.txt"));

    log.clear();
    ASSERT_EQ(RunExecutable(CNA_CUSTOM_CONTENT_COMPILER_PATH,
                            {"clean", outputFour.string()}, log), 0)
        << log;
    EXPECT_TRUE(std::filesystem::is_regular_file(sharedA / "Support" / "message.txt"));
    EXPECT_EQ(ReadBytes(sharedA / "sentinel.keep"),
              (std::vector<std::uint8_t>{'n','e','v','e','r',' ','o','w','n','e','d','\n'}));
    EXPECT_EQ(ReadBytes(sharedB / "sentinel.keep"),
              (std::vector<std::uint8_t>{'n','e','v','e','r',' ','o','w','n','e','d','\n'}));
}

TEST(ContentPipelineCliTest, CustomWriterSchemaAndCodecEvolutionCannotSkipStaleOutput)
{
    ScratchDirectory scratch("custom_writer_fingerprint");
    const std::filesystem::path source = scratch.Path() / "ContentSource";
    const std::filesystem::path output = scratch.Path() / "Content";
    WriteText(source / "welcome.greeting", "CNA\n");

    const auto run = [&](std::string& log)
    {
        return RunExecutable(CNA_CUSTOM_CONTENT_COMPILER_PATH,
                             {"build", source.string(), "-o", output.string(),
                              "--explain"}, log);
    };
    std::string log;
    ASSERT_EQ(run(log), 0) << log;
    const std::filesystem::path manifestPath =
        output / Pipeline::ContentBuildManifestFileName;
    const std::filesystem::path artifact = output / "welcome.cnb";
    const std::vector<std::uint8_t> originalArtifact = ReadBytes(artifact);

    enum class Evolution
    {
        AssetType,
        Schema,
        TypeName,
        Codec,
    };
    for (const Evolution evolution : {Evolution::AssetType, Evolution::Schema,
                                      Evolution::TypeName, Evolution::Codec})
    {
        const std::vector<std::uint8_t> currentManifestBytes = ReadBytes(manifestPath);
        Pipeline::ContentBuildManifest stale = Pipeline::ContentBuildManifest::Parse(
            std::string(currentManifestBytes.begin(), currentManifestBytes.end()));
        Pipeline::ContentBuildManifestEntry entry = *stale.Find("welcome");
        ASSERT_EQ(entry.writerSchemas.size(), 1u);
        if (evolution == Evolution::AssetType)
        {
            ++entry.writerSchemas[0].assetTypeId;
            for (auto& owned : entry.outputs) { ++owned.assetTypeId; }
        }
        else if (evolution == Evolution::Schema)
        {
            entry.writerSchemas[0].assetSchemaVersion = 2u;
            for (auto& owned : entry.outputs) { owned.assetSchemaVersion = 2u; }
        }
        else if (evolution == Evolution::TypeName)
        {
            entry.writerSchemas[0].assetTypeName = "ExampleGame.GreetingV2";
            for (auto& owned : entry.outputs)
            {
                owned.assetTypeName = "ExampleGame.GreetingV2";
            }
        }
        else
        {
            entry.writerSchemas[0].codec.version = "2";
        }
        Pipeline::RefreshContentBuildDirectFingerprint(entry, source);
        Pipeline::RefreshContentBuildEffectiveFingerprint(entry);
        stale.Set(std::move(entry));
        WriteText(manifestPath, stale.Serialize());

        log.clear();
        ASSERT_EQ(run(log), 0) << log;
        EXPECT_NE(log.find("[BUILD] welcome"), std::string::npos) << log;
        if (evolution == Evolution::Codec)
        {
            EXPECT_NE(log.find("reason: writer codec identity/version changed"),
                      std::string::npos)
                << log;
        }
        else
        {
            EXPECT_NE(log.find("reason: writer asset schema identity changed"),
                      std::string::npos)
                << log;
        }
        EXPECT_EQ(ReadBytes(artifact), originalArtifact);
        const std::vector<std::uint8_t> repairedBytes = ReadBytes(manifestPath);
        const Pipeline::ContentBuildManifest repaired =
            Pipeline::ContentBuildManifest::Parse(
                std::string(repairedBytes.begin(), repairedBytes.end()));
        ASSERT_EQ(repaired.Find("welcome")->writerSchemas.size(), 1u);
        EXPECT_EQ(repaired.Find("welcome")->writerSchemas[0],
                  (Pipeline::ContentWriterSchemaIdentity{
                      Cnb::CnbAssetTypeIdFromName("ExampleGame.Greeting"), 1u,
                      "ExampleGame.Greeting",
                      {"ExampleGame.EncodeGreetingToCnb", "1"}}));
    }

    log.clear();
    ASSERT_EQ(run(log), 0) << log;
    EXPECT_NE(log.find("[SKIP] welcome"), std::string::npos) << log;
}

TEST(ContentPipelineCliTest, ExplainClassifiesImporterProcessorAndWriterVersionChanges)
{
    ScratchDirectory scratch("explain_component_versions");
    const std::filesystem::path source = scratch.Path() / "ContentSource";
    const std::filesystem::path output = scratch.Path() / "Content";
    const std::filesystem::path manifestPath =
        output / Pipeline::ContentBuildManifestFileName;
    WriteText(source / "welcome.greeting", "CNA\n");
    std::string log;
    ASSERT_EQ(RunExecutable(CNA_CUSTOM_CONTENT_COMPILER_PATH,
                            {"build", source.string(), "-o", output.string()}, log),
              0)
        << log;

    enum class Component
    {
        Importer,
        Processor,
        Writer,
    };
    for (const Component component :
         {Component::Importer, Component::Processor, Component::Writer})
    {
        const std::vector<std::uint8_t> manifestBytes = ReadBytes(manifestPath);
        Pipeline::ContentBuildManifest stale = Pipeline::ContentBuildManifest::Parse(
            std::string(manifestBytes.begin(), manifestBytes.end()));
        Pipeline::ContentBuildManifestEntry entry = *stale.Find("welcome");
        if (component == Component::Importer) { entry.importer.version = "stale"; }
        else if (component == Component::Processor) { entry.processor.version = "stale"; }
        else { entry.writer.version = "stale"; }
        Pipeline::RefreshContentBuildDirectFingerprint(entry, source);
        Pipeline::RefreshContentBuildEffectiveFingerprint(entry);
        stale.Set(std::move(entry));
        WriteText(manifestPath, stale.Serialize());

        log.clear();
        ASSERT_EQ(RunExecutable(CNA_CUSTOM_CONTENT_COMPILER_PATH,
                                {"build", source.string(), "-o", output.string(),
                                 "--explain"}, log),
                  0)
            << log;
        const char* expected = component == Component::Importer
                                   ? "reason: importer identity/version changed"
                               : component == Component::Processor
                                   ? "reason: processor identity/version changed"
                                   : "reason: writer identity/version changed";
        EXPECT_NE(log.find(expected), std::string::npos) << log;
    }
}

TEST(ContentPipelineCliTest, MultiOutputContractionCollectsOnlyTheObsoleteChild)
{
    ScratchDirectory scratch("orphan_multi_output_contraction");
    const std::filesystem::path source = scratch.Path() / "ContentSource";
    const std::filesystem::path output = scratch.Path() / "Content";
    WriteText(source / "welcome.greeting", "first\n");
    std::string log;
    ASSERT_EQ(RunExecutable(CNA_CUSTOM_CONTENT_COMPILER_PATH,
                            {"build", source.string(), "-o", output.string()}, log),
              0)
        << log;
    const std::filesystem::path primary = output / "welcome.cnb";
    const std::filesystem::path child =
        output / "Generated" / "welcome-reply.cnb";
    ASSERT_TRUE(std::filesystem::is_regular_file(primary));
    ASSERT_TRUE(std::filesystem::is_regular_file(child));

    ASSERT_TRUE(std::filesystem::remove(source / "welcome.greeting"));
    WriteBytes(source / "welcome.png", MakePng(2, 2));
    ASSERT_EQ(RunExecutable(CNA_CUSTOM_CONTENT_COMPILER_PATH,
                            {"build", source.string(), "-o", output.string(),
                             "--explain"}, log),
              0)
        << log;
    EXPECT_NE(log.find("reason: compiled output definition set changed"),
              std::string::npos)
        << log;
    EXPECT_NE(log.find("[CLEAN] Generated/welcome-reply.cnb "
                       "(obsolete manifest-owned output)"),
              std::string::npos)
        << log;
    EXPECT_TRUE(std::filesystem::is_regular_file(primary));
    EXPECT_FALSE(std::filesystem::exists(child));
    const Cnb::CnbDocument replacement =
        Cnb::CnbDocument::Parse(ReadBytes(primary), "contracted primary");
    EXPECT_EQ(replacement.AssetTypeId(), Cnb::CnbAssetTypeId::Texture2D);

    const std::vector<std::uint8_t> manifestBytes =
        ReadBytes(output / Pipeline::ContentBuildManifestFileName);
    const Pipeline::ContentBuildManifest manifest = Pipeline::ContentBuildManifest::Parse(
        std::string(manifestBytes.begin(), manifestBytes.end()));
    const Pipeline::ContentBuildManifestEntry* entry = manifest.Find("welcome");
    ASSERT_NE(entry, nullptr);
    EXPECT_EQ(entry->outputs.size(), 1u);
}

TEST(ContentPipelineCliTest, AdditionalOutputCannotClaimAnotherBuildNodesIdentity)
{
    ScratchDirectory scratch("multi_output_collision");
    const std::filesystem::path source = scratch.Path() / "ContentSource";
    const std::filesystem::path output = scratch.Path() / "Content";
    WriteText(source / "welcome.greeting", "first\n");
    WriteText(source / "Generated" / "welcome-reply.greeting", "second\n");

    std::string log;
    EXPECT_EQ(RunExecutable(CNA_CUSTOM_CONTENT_COMPILER_PATH,
                            {"build", source.string(), "-o", output.string()}, log),
              1)
        << log;
    EXPECT_NE(log.find("both own output logical name 'Generated/welcome-reply'"),
              std::string::npos)
        << log;
    EXPECT_FALSE(std::filesystem::exists(output / Pipeline::ContentBuildManifestFileName));
    EXPECT_FALSE(std::filesystem::exists(output / "welcome.cnb"));
}

TEST(ContentPipelineCliTest, ContentBuildGraphOrdersSharedDependenciesAndPropagatesChanges)
{
    ScratchDirectory scratch("content_build_graph");
    const std::filesystem::path source = scratch.Path() / "ContentSource";
    const std::filesystem::path output = scratch.Path() / "Content";
    WriteText(source / "A" / "first.greeting", "first\n");
    WriteText(source / "B" / "second.greeting", "second\n");
    WriteText(source / "Z" / "shared.greeting", "shared-v1\n");
    WriteText(
        source / Pipeline::ContentBuildConfigurationFileName,
        R"json({"format":"CNA.ContentPipeline.Config","version":1,"assets":{"A/first.greeting":{"parameters":{"dependsOn":{"type":"string","value":"Z/shared"}}},"B/second.greeting":{"parameters":{"dependsOn":{"type":"string","value":"Z/shared"}}}}})json");

    std::string firstLog;
    ASSERT_EQ(RunExecutable(CNA_CUSTOM_CONTENT_COMPILER_PATH,
                            {"build", source.string(), "-o", output.string()}, firstLog),
              0)
        << firstLog;
    const std::size_t sharedPosition = firstLog.find("[BUILD] Z/shared");
    const std::size_t firstPosition = firstLog.find("[BUILD] A/first");
    const std::size_t secondPosition = firstLog.find("[BUILD] B/second");
    ASSERT_NE(sharedPosition, std::string::npos) << firstLog;
    ASSERT_NE(firstPosition, std::string::npos) << firstLog;
    ASSERT_NE(secondPosition, std::string::npos) << firstLog;
    EXPECT_LT(sharedPosition, firstPosition);
    EXPECT_LT(sharedPosition, secondPosition);
    EXPECT_EQ(CountOccurrences(firstLog, "[BUILD] Z/shared"), 1u);

    const std::filesystem::path manifestPath =
        output / Pipeline::ContentBuildManifestFileName;
    const auto readManifest = [&]()
    {
        const std::vector<std::uint8_t> bytes = ReadBytes(manifestPath);
        return Pipeline::ContentBuildManifest::Parse(
            std::string(bytes.begin(), bytes.end()));
    };
    const Pipeline::ContentBuildManifest initial = readManifest();
    const Pipeline::ContentBuildManifestEntry* initialFirst = initial.Find("A/first");
    ASSERT_NE(initialFirst, nullptr);
    EXPECT_NE(std::find(initialFirst->dependencies.begin(), initialFirst->dependencies.end(),
                        Pipeline::ContentDependency{
                            Pipeline::ContentDependencyKind::ContentBuild, "Z/shared"}),
              initialFirst->dependencies.end());
    const std::string firstDirectFingerprint = initialFirst->directFingerprint;
    const std::string firstEffectiveFingerprint = initialFirst->fingerprint;
    const std::vector<std::uint8_t> firstOutput = ReadBytes(output / "A" / "first.cnb");

    std::string skipLog;
    ASSERT_EQ(RunExecutable(CNA_CUSTOM_CONTENT_COMPILER_PATH,
                            {"build", source.string(), "-o", output.string()}, skipLog),
              0)
        << skipLog;
    EXPECT_EQ(CountOccurrences(skipLog, "[SKIP] Z/shared"), 1u) << skipLog;
    EXPECT_NE(skipLog.find("[SKIP] A/first"), std::string::npos) << skipLog;
    EXPECT_NE(skipLog.find("[SKIP] B/second"), std::string::npos) << skipLog;

    WriteText(source / "Z" / "shared.greeting", "shared-v2\n");
    std::string changedLog;
    ASSERT_EQ(RunExecutable(CNA_CUSTOM_CONTENT_COMPILER_PATH,
                            {"build", source.string(), "-o", output.string()}, changedLog),
              0)
        << changedLog;
    EXPECT_EQ(CountOccurrences(changedLog, "[BUILD] Z/shared"), 1u) << changedLog;
    EXPECT_EQ(CountOccurrences(changedLog, "[BUILD] A/first"), 1u) << changedLog;
    EXPECT_EQ(CountOccurrences(changedLog, "[BUILD] B/second"), 1u) << changedLog;
    EXPECT_EQ(ReadBytes(output / "A" / "first.cnb"), firstOutput);

    const Pipeline::ContentBuildManifest changed = readManifest();
    const Pipeline::ContentBuildManifestEntry* changedFirst = changed.Find("A/first");
    ASSERT_NE(changedFirst, nullptr);
    EXPECT_EQ(changedFirst->directFingerprint, firstDirectFingerprint);
    EXPECT_NE(changedFirst->fingerprint, firstEffectiveFingerprint);
}

TEST(ContentPipelineCliTest, ContentBuildDependencyFailurePropagatesWithoutPublication)
{
    ScratchDirectory scratch("content_build_failure");
    const std::filesystem::path source = scratch.Path() / "ContentSource";
    const std::filesystem::path output = scratch.Path() / "Content";
    WriteText(source / "A" / "dependent.greeting", "dependent\n");
    WriteText(source / "Z" / "broken.greeting", "\n");
    WriteText(
        source / Pipeline::ContentBuildConfigurationFileName,
        R"json({"format":"CNA.ContentPipeline.Config","version":1,"assets":{"A/dependent.greeting":{"parameters":{"dependsOn":{"type":"string","value":"Z/broken"}}}}})json");

    std::string failedLog;
    EXPECT_EQ(RunExecutable(CNA_CUSTOM_CONTENT_COMPILER_PATH,
                            {"build", source.string(), "-o", output.string()}, failedLog),
              1)
        << failedLog;
    EXPECT_NE(failedLog.find("Graph (CNA.ContentBuildGraph)"), std::string::npos) << failedLog;
    EXPECT_NE(failedLog.find("dependency 'Z/broken' failed"), std::string::npos) << failedLog;
    EXPECT_NE(failedLog.find("Import (ExampleGame.GreetingImporter)"), std::string::npos)
        << failedLog;
    EXPECT_FALSE(std::filesystem::exists(output / "A" / "dependent.cnb"));
    EXPECT_FALSE(std::filesystem::exists(output / Pipeline::ContentBuildManifestFileName));

    WriteText(source / "Z" / "broken.greeting", "repaired\n");
    std::string recoveredLog;
    ASSERT_EQ(RunExecutable(CNA_CUSTOM_CONTENT_COMPILER_PATH,
                            {"build", source.string(), "-o", output.string()}, recoveredLog),
              0)
        << recoveredLog;
    EXPECT_NE(recoveredLog.find("[BUILD] Z/broken"), std::string::npos) << recoveredLog;
    EXPECT_NE(recoveredLog.find("[BUILD] A/dependent"), std::string::npos) << recoveredLog;
}

TEST(ContentPipelineCliTest, ContentBuildDependencyMustNameADiscoveredPrimaryNode)
{
    ScratchDirectory scratch("content_build_missing");
    const std::filesystem::path source = scratch.Path() / "ContentSource";
    const std::filesystem::path output = scratch.Path() / "Content";
    WriteText(source / "dependent.greeting", "dependent\n");
    WriteText(
        source / Pipeline::ContentBuildConfigurationFileName,
        R"json({"format":"CNA.ContentPipeline.Config","version":1,"assets":{"dependent.greeting":{"parameters":{"dependsOn":{"type":"string","value":"Generated/not-a-node"}}}}})json");

    std::string log;
    EXPECT_EQ(RunExecutable(CNA_CUSTOM_CONTENT_COMPILER_PATH,
                            {"build", source.string(), "-o", output.string()}, log),
              1)
        << log;
    EXPECT_NE(log.find("Graph (CNA.ContentBuildGraph)"), std::string::npos) << log;
    EXPECT_NE(log.find("does not name a discovered primary build node"), std::string::npos)
        << log;
    EXPECT_FALSE(std::filesystem::exists(output / "dependent.cnb"));
    EXPECT_FALSE(std::filesystem::exists(output / Pipeline::ContentBuildManifestFileName));
}

TEST(ContentPipelineCliTest, VeryDeepAcyclicGraphBuildsAndDeepCycleRemainsDeterministic)
{
    constexpr std::size_t depth = 4096u;
    ScratchDirectory scratch("content_build_deep_graph");
    const std::filesystem::path source = scratch.Path() / "ContentSource";
    const std::filesystem::path output = scratch.Path() / "Content";
    const std::filesystem::path configuration =
        source / Pipeline::ContentBuildConfigurationFileName;

    for (std::size_t index = 0u; index < depth; ++index)
    {
        WriteText(source / ("N" + std::to_string(index) + ".greeting"), "node\n");
    }
    const auto writeConfiguration = [&](bool closeCycle)
    {
        std::ostringstream text;
        text << R"json({"format":"CNA.ContentPipeline.Config","version":1,"assets":{)json";
        for (std::size_t index = 0u; index < depth; ++index)
        {
            if (index + 1u == depth && !closeCycle) { break; }
            if (index != 0u) { text << ','; }
            const std::size_t dependency = index + 1u == depth ? 0u : index + 1u;
            text << "\"N" << index
                 << ".greeting\":{\"parameters\":{\"dependsOn\":{\"type\":\"string\","
                    "\"value\":\"N"
                 << dependency << "\"}}}";
        }
        text << "}}";
        WriteText(configuration, text.str());
    };

    writeConfiguration(false);
    std::string builtLog;
    ASSERT_EQ(RunExecutable(CNA_CUSTOM_CONTENT_COMPILER_PATH,
                            {"build", source.string(), "-o", output.string(),
                             "--workers", "4", "--quiet"},
                            builtLog),
              0)
        << builtLog;
    EXPECT_TRUE(std::filesystem::is_regular_file(output / "N0.cnb"));
    EXPECT_TRUE(std::filesystem::is_regular_file(
        output / ("N" + std::to_string(depth - 1u) + ".cnb")));
    const std::filesystem::path manifestPath =
        output / Pipeline::ContentBuildManifestFileName;
    const std::vector<std::uint8_t> manifestBeforeCycle = ReadBytes(manifestPath);
    EXPECT_EQ(Pipeline::ContentBuildManifest::Parse(
                  std::string(manifestBeforeCycle.begin(), manifestBeforeCycle.end()))
                  .Entries()
                  .size(),
              depth);

    writeConfiguration(true);
    std::string cycleLog;
    EXPECT_EQ(RunExecutable(CNA_CUSTOM_CONTENT_COMPILER_PATH,
                            {"build", source.string(), "-o", output.string(),
                             "--workers", "4", "--quiet"},
                            cycleLog),
              1)
        << cycleLog;
    EXPECT_EQ(CountOccurrences(cycleLog, "content-build dependency cycle:"), 1u)
        << cycleLog;
    EXPECT_NE(cycleLog.find("content-build dependency cycle:\n  N0\n  -> N1"),
              std::string::npos)
        << cycleLog;
    EXPECT_NE(cycleLog.find("  -> N" + std::to_string(depth - 1u) + "\n  -> N0"),
              std::string::npos)
        << cycleLog;
    EXPECT_NE(cycleLog.find("Built: 0  Skipped: 0  Failed: " + std::to_string(depth)),
              std::string::npos)
        << cycleLog;
    EXPECT_EQ(ReadBytes(manifestPath), manifestBeforeCycle);
}

TEST(ContentPipelineCliTest, ContentBuildGraphReportsASelfCycleChainOnce)
{
    ScratchDirectory scratch("content_build_self_cycle");
    const std::filesystem::path source = scratch.Path() / "ContentSource";
    const std::filesystem::path output = scratch.Path() / "Content";
    WriteText(source / "A" / "self.greeting", "self\n");
    WriteText(
        source / Pipeline::ContentBuildConfigurationFileName,
        R"json({"format":"CNA.ContentPipeline.Config","version":1,"assets":{"A/self.greeting":{"parameters":{"dependsOn":{"type":"string","value":"A/self"}}}}})json");

    std::string log;
    EXPECT_EQ(RunExecutable(CNA_CUSTOM_CONTENT_COMPILER_PATH,
                            {"build", source.string(), "-o", output.string()}, log),
              1)
        << log;
    EXPECT_EQ(CountOccurrences(log, "content-build dependency cycle:"), 1u) << log;
    EXPECT_NE(log.find("content-build dependency cycle:\n  A/self\n  -> A/self"),
              std::string::npos)
        << log;
    EXPECT_NE(log.find("Built: 0  Skipped: 0  Failed: 1"), std::string::npos) << log;
    EXPECT_FALSE(std::filesystem::exists(output / "A" / "self.cnb"));
    EXPECT_FALSE(std::filesystem::exists(output / Pipeline::ContentBuildManifestFileName));
}

TEST(ContentPipelineCliTest, ContentBuildGraphReportsATwoNodeCycleChainOnce)
{
    ScratchDirectory scratch("content_build_two_cycle");
    const std::filesystem::path source = scratch.Path() / "ContentSource";
    const std::filesystem::path output = scratch.Path() / "Content";
    WriteText(source / "A.greeting", "a\n");
    WriteText(source / "B.greeting", "b\n");
    WriteText(
        source / Pipeline::ContentBuildConfigurationFileName,
        R"json({"format":"CNA.ContentPipeline.Config","version":1,"assets":{"A.greeting":{"parameters":{"dependsOn":{"type":"string","value":"B"}}},"B.greeting":{"parameters":{"dependsOn":{"type":"string","value":"A"}}}}})json");

    std::string log;
    EXPECT_EQ(RunExecutable(CNA_CUSTOM_CONTENT_COMPILER_PATH,
                            {"build", source.string(), "-o", output.string()}, log),
              1)
        << log;
    EXPECT_EQ(CountOccurrences(log, "content-build dependency cycle:"), 1u) << log;
    EXPECT_NE(log.find("content-build dependency cycle:\n  A\n  -> B\n  -> A"),
              std::string::npos)
        << log;
    EXPECT_NE(log.find("Built: 0  Skipped: 0  Failed: 2"), std::string::npos) << log;
    EXPECT_FALSE(std::filesystem::exists(output / Pipeline::ContentBuildManifestFileName));
}

TEST(ContentPipelineCliTest, ContentBuildGraphReportsALongCycleDeterministically)
{
    ScratchDirectory scratch("content_build_long_cycle");
    const std::filesystem::path source = scratch.Path() / "ContentSource";
    const std::filesystem::path output = scratch.Path() / "Content";
    WriteText(source / "A" / "first.greeting", "a\n");
    WriteText(source / "B" / "second.greeting", "b\n");
    WriteText(source / "C" / "third.greeting", "c\n");
    WriteText(
        source / Pipeline::ContentBuildConfigurationFileName,
        R"json({"format":"CNA.ContentPipeline.Config","version":1,"assets":{"A/first.greeting":{"parameters":{"dependsOn":{"type":"string","value":"B/second"}}},"B/second.greeting":{"parameters":{"dependsOn":{"type":"string","value":"C/third"}}},"C/third.greeting":{"parameters":{"dependsOn":{"type":"string","value":"A/first"}}}}})json");

    std::string firstLog;
    EXPECT_EQ(RunExecutable(CNA_CUSTOM_CONTENT_COMPILER_PATH,
                            {"build", source.string(), "-o", output.string()}, firstLog),
              1)
        << firstLog;
    EXPECT_EQ(CountOccurrences(firstLog, "content-build dependency cycle:"), 1u) << firstLog;
    EXPECT_NE(firstLog.find("content-build dependency cycle:\n  A/first\n  -> B/second\n  "
                            "-> C/third\n  -> A/first"),
              std::string::npos)
        << firstLog;
    EXPECT_NE(firstLog.find("Built: 0  Skipped: 0  Failed: 3"), std::string::npos) << firstLog;

    std::string secondLog;
    EXPECT_EQ(RunExecutable(CNA_CUSTOM_CONTENT_COMPILER_PATH,
                            {"build", source.string(), "-o", output.string(),
                             "--workers", "4"}, secondLog),
              1)
        << secondLog;
    EXPECT_EQ(secondLog, firstLog);
    EXPECT_FALSE(std::filesystem::exists(output / Pipeline::ContentBuildManifestFileName));
}

TEST(ContentPipelineCliTest, MultiOutputFailureLeavesTheOldManifestAndRecoversSafely)
{
    ScratchDirectory scratch("multi_output_recovery");
    const std::filesystem::path source = scratch.Path() / "ContentSource" / "welcome.greeting";
    const std::filesystem::path primary = scratch.Path() / "Content" / "welcome.cnb";
    const std::filesystem::path reply =
        scratch.Path() / "Content" / "Generated" / "welcome-reply.cnb";
    const std::filesystem::path manifest =
        primary.parent_path() / Pipeline::ContentBuildManifestFileName;
    WriteText(source, "first\n");

    std::string firstLog;
    ASSERT_EQ(RunExecutable(CNA_CUSTOM_CONTENT_COMPILER_PATH,
                            {"build", source.string(), "-o", primary.string()}, firstLog),
              0)
        << firstLog;
    EXPECT_NE(firstLog.find("2 output(s)"), std::string::npos) << firstLog;
    const std::vector<std::uint8_t> oldPrimary = ReadBytes(primary);
    const std::vector<std::uint8_t> oldReply = ReadBytes(reply);
    const std::vector<std::uint8_t> oldManifest = ReadBytes(manifest);

    WriteText(source, "second\n");
    std::error_code permissionError;
    std::filesystem::permissions(reply.parent_path(), std::filesystem::perms::owner_write,
                                 std::filesystem::perm_options::remove, permissionError);
    ASSERT_FALSE(permissionError);
    std::string failedLog;
    const int failed = RunExecutable(
        CNA_CUSTOM_CONTENT_COMPILER_PATH,
        {"build", source.string(), "-o", primary.string(), "--quiet"}, failedLog);
    std::filesystem::permissions(reply.parent_path(), std::filesystem::perms::owner_write,
                                 std::filesystem::perm_options::add, permissionError);
    ASSERT_FALSE(permissionError);

    EXPECT_EQ(failed, 1) << failedLog;
    EXPECT_NE(failedLog.find("Publish (CNA.AtomicPublisher)"), std::string::npos) << failedLog;
    EXPECT_NE(ReadBytes(primary), oldPrimary);
    EXPECT_EQ(ReadBytes(reply), oldReply);
    EXPECT_EQ(ReadBytes(manifest), oldManifest);

    std::string recoveryLog;
    ASSERT_EQ(RunExecutable(CNA_CUSTOM_CONTENT_COMPILER_PATH,
                            {"build", source.string(), "-o", primary.string()}, recoveryLog),
              0)
        << recoveryLog;
    EXPECT_NE(recoveryLog.find("[BUILD] welcome"), std::string::npos) << recoveryLog;
    EXPECT_NE(ReadBytes(reply), oldReply);
    EXPECT_NE(ReadBytes(manifest), oldManifest);

    std::string skipLog;
    ASSERT_EQ(RunExecutable(CNA_CUSTOM_CONTENT_COMPILER_PATH,
                            {"build", source.string(), "-o", primary.string()}, skipLog),
              0)
        << skipLog;
    EXPECT_NE(skipLog.find("[SKIP] welcome"), std::string::npos) << skipLog;
}
#endif
