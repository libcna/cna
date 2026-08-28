// SPDX-License-Identifier: MS-PL

#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
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
#include "CNA/Content/Pipeline/ContentBuildManifest.hpp"
#include "CNA/Content/Pipeline/ContentPipeline.hpp"
#include "CNA/Content/Pipeline/Texture2DContentPipeline.hpp"
#include "CNA/Internal/Graphics/ImageLoader.hpp"

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

    std::vector<std::uint8_t> ReadBytes(const std::filesystem::path& path)
    {
        std::ifstream stream(path, std::ios::binary);
        return {std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
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

    int RunTool(const std::vector<std::string>& arguments, std::string& output)
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
        argv.push_back(const_cast<char*>(CNA_CONTENT_TOOL_PATH));
        for (const std::string& argument : arguments)
        {
            argv.push_back(const_cast<char*>(argument.c_str()));
        }
        argv.push_back(nullptr);

        pid_t pid = -1;
        const int spawnResult = posix_spawn(&pid, CNA_CONTENT_TOOL_PATH, &actions, nullptr,
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
    ASSERT_EQ(RunTool({"build", source.string(), "-o", output.string()}, log), 0) << log;
    EXPECT_NE(log.find("[BUILD] Models/car"), std::string::npos) << log;
    EXPECT_NE(log.find("[BUILD] Models/gltf-external-image.texture"), std::string::npos) << log;
    EXPECT_NE(log.find("[SKIP] Textures/independent"), std::string::npos) << log;
    EXPECT_EQ(ReadBytes(output / "Textures" / "independent.cnb"), independent);
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
    ASSERT_EQ(RunTool({"build", source.string(), "-o", output.string()}, log), 0) << log;
    EXPECT_NE(log.find("[BUILD] wall"), std::string::npos) << log;
    EXPECT_EQ(ReadBytes(output), expected);

    std::filesystem::remove(output);
    ASSERT_EQ(RunTool({"build", source.string(), "-o", output.string()}, log), 0) << log;
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
    ASSERT_EQ(RunTool({"build", source.string(), "-o", output.string()}, log), 0) << log;
    EXPECT_NE(log.find("[WARN] Ignoring incompatible or corrupt manifest"), std::string::npos)
        << log;
    EXPECT_NE(log.find("[BUILD] wall"), std::string::npos) << log;
    const std::vector<std::uint8_t> manifestBytes = ReadBytes(manifest);
    EXPECT_NO_THROW((void)Pipeline::ContentBuildManifest::Parse(
        std::string(manifestBytes.begin(), manifestBytes.end())));
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
