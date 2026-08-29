// SPDX-License-Identifier: MS-PL

#include <algorithm>
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
#include "CNA/Content/Cnb/CnbMediaCodec.hpp"
#include "CNA/Content/Pipeline/ContentBuildConfiguration.hpp"
#include "CNA/Content/Pipeline/ContentBuildManifest.hpp"
#include "CNA/Content/Pipeline/ContentPipeline.hpp"
#include "CNA/Content/Pipeline/Texture2DContentPipeline.hpp"
#include "CNA/Internal/ContentPath.hpp"
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

    void WriteText(const std::filesystem::path& path, const std::string& text)
    {
        WriteBytes(path, std::vector<std::uint8_t>(text.begin(), text.end()));
    }

    std::vector<std::uint8_t> ReadBytes(const std::filesystem::path& path)
    {
        std::ifstream stream(path, std::ios::binary);
        return {std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
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

    std::string first;
    ASSERT_EQ(RunTool({"build", source.string(), "-o", output.string()}, first), 0) << first;
    EXPECT_NE(first.find("[BUILD] Music/theme"), std::string::npos) << first;
    EXPECT_NE(first.find("[BUILD] Textures/wall"), std::string::npos) << first;
    const std::filesystem::path songOutput = output / "Music" / "theme.cnb";
    EXPECT_TRUE(std::filesystem::is_regular_file(songOutput));
    EXPECT_FALSE(std::filesystem::exists(output / "Music" / "theme.ogg"));

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

    std::string second;
    ASSERT_EQ(RunTool({"build", source.string(), "-o", output.string()}, second), 0) << second;
    EXPECT_NE(second.find("[SKIP] Music/theme"), std::string::npos) << second;
    EXPECT_NE(second.find("[SKIP] Textures/wall"), std::string::npos) << second;
    EXPECT_EQ(ReadBytes(songOutput), songBytes);

    WriteBytes(media, {0x4Fu, 0x67u, 0x67u, 0x53u, 9u, 8u, 7u});
    std::string changed;
    ASSERT_EQ(RunTool({"build", source.string(), "-o", output.string()}, changed), 0)
        << changed;
    EXPECT_NE(changed.find("[BUILD] Music/theme"), std::string::npos) << changed;
    EXPECT_NE(changed.find("[SKIP] Textures/wall"), std::string::npos) << changed;
    EXPECT_EQ(ReadBytes(songOutput), songBytes);
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

    std::string first;
    ASSERT_EQ(RunTool({"build", source.string(), "-o", output.string()}, first), 0) << first;
    EXPECT_NE(first.find("[BUILD] Movies/intro"), std::string::npos) << first;
    EXPECT_NE(first.find("[BUILD] Textures/wall"), std::string::npos) << first;
    const std::filesystem::path videoOutput = output / "Movies" / "intro.cnb";
    EXPECT_TRUE(std::filesystem::is_regular_file(videoOutput));
    EXPECT_FALSE(std::filesystem::exists(output / "Movies" / "intro.mp4"));

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

    std::string second;
    ASSERT_EQ(RunTool({"build", source.string(), "-o", output.string()}, second), 0) << second;
    EXPECT_NE(second.find("[SKIP] Movies/intro"), std::string::npos) << second;
    EXPECT_NE(second.find("[SKIP] Textures/wall"), std::string::npos) << second;

    WriteBytes(media, {0u, 0u, 0u, 24u, 'f', 't', 'y', 'p', 9u});
    std::string changed;
    ASSERT_EQ(RunTool({"build", source.string(), "-o", output.string()}, changed), 0)
        << changed;
    EXPECT_NE(changed.find("[BUILD] Movies/intro"), std::string::npos) << changed;
    EXPECT_NE(changed.find("[SKIP] Textures/wall"), std::string::npos) << changed;
    EXPECT_EQ(ReadBytes(videoOutput), videoBytes);
}

#if defined(CNA_CUSTOM_CONTENT_COMPILER_PATH)
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
    ASSERT_EQ(greetingEntry->outputs.size(), 2u);
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
