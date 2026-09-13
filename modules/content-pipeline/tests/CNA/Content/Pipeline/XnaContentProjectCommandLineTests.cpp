// SPDX-License-Identifier: MS-PL
//
// plans/plan_xnapipeline_parity.md XNAPP-240: `cna-content build Foo.contentproj`.
//
// A `.contentproj` is what an XNA developer has. Reading one was already done -- the whole §18
// schema, the conditions, the metadata -- and what was missing was the two things a command line
// has to do that a task does not: take a project as its source, and copy the `Content`/`None`
// items, which XNA's targets copy rather than build. There is no second build engine behind this:
// the project is handed to `Tasks::BuildContent`, the task XNA's own targets drive, and that task
// runs the same coordinator every other build here runs.
#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include "CNA/Content/Pipeline/ContentCompiler.hpp"
#include "CNA/Content/Pipeline/ContentPipeline.hpp"

namespace Pipeline = CNA::Content::Pipeline;

namespace
{
    std::filesystem::path Locate(const std::filesystem::path& relative)
    {
        for (std::filesystem::path dir = std::filesystem::current_path(); !dir.empty();
             dir = dir.parent_path())
        {
            if (std::filesystem::exists(dir / relative)) { return dir / relative; }
            if (dir == dir.root_path()) { break; }
        }
        for (std::filesystem::path dir = std::filesystem::path(__FILE__).parent_path(); !dir.empty();
             dir = dir.parent_path())
        {
            if (std::filesystem::exists(dir / relative)) { return dir / relative; }
            if (dir == dir.root_path()) { break; }
        }
        return relative;
    }

    /** @brief A project, its sources and an output directory, removed when the test ends. */
    class Project
    {
    public:
        explicit Project(const std::string& label)
            : root_(std::filesystem::temp_directory_path() /
                    ("cna_xnapp240_" + label + "_" +
                     std::to_string(reinterpret_cast<std::uintptr_t>(this))))
        {
            std::filesystem::remove_all(root_);
            std::filesystem::create_directories(Source() / "nested");
            std::filesystem::create_directories(Output());
            std::filesystem::copy_file(Locate("tests/assets/xna40/texture/probe.png"),
                                       Source() / "probe.png");
            std::filesystem::copy_file(Locate("tests/assets/xna40/media/tone_mono_44100.wav"),
                                       Source() / "tone.wav");
            { std::ofstream(Source() / "readme.txt") << "a readme\n"; }
            { std::ofstream(Source() / "nested" / "notes.txt") << "notes\n"; }
        }
        ~Project()
        {
            std::error_code error;
            std::filesystem::remove_all(root_, error);
        }
        Project(const Project&) = delete;
        Project& operator=(const Project&) = delete;

        [[nodiscard]] std::filesystem::path Source() const { return root_ / "src"; }
        [[nodiscard]] std::filesystem::path Output() const { return root_ / "out"; }
        [[nodiscard]] std::filesystem::path File() const { return Source() / "Game.contentproj"; }

        /** @brief Writes the project file with the item block a test wants. */
        void Write(const std::string& items, const std::string& properties = "") const
        {
            std::ofstream stream(File());
            stream << "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n"
                   << "<Project ToolsVersion=\"4.0\" DefaultTargets=\"Build\" "
                      "xmlns=\"http://schemas.microsoft.com/developer/msbuild/2003\">\n"
                   << "  <PropertyGroup>\n"
                   << "    <ProjectGuid>{11111111-2222-3333-4444-555555555555}</ProjectGuid>\n"
                   << "    <XnaFrameworkVersion>v4.0</XnaFrameworkVersion>\n"
                   << "    <ContentRootDirectory>Content</ContentRootDirectory>\n"
                   << properties
                   << "  </PropertyGroup>\n"
                   << "  <ItemGroup>\n" << items << "  </ItemGroup>\n"
                   << "</Project>\n";
        }

    private:
        std::filesystem::path root_;
    };

    struct Invocation
    {
        int exitCode = -1;
        std::string output;
    };

    /** @brief Runs the coordinator in-process with everything it printed captured. */
    Invocation Build(const Project& project, std::vector<std::string> extra = {})
    {
        std::vector<std::filesystem::path> arguments{"build", project.File(), "-o", project.Output()};
        for (const std::string& argument : extra) { arguments.emplace_back(argument); }

        std::ostringstream captured;
        std::streambuf* const previousOut = std::cout.rdbuf(captured.rdbuf());
        std::streambuf* const previousErr = std::cerr.rdbuf(captured.rdbuf());
        Invocation invocation;
        try
        {
            invocation.exitCode = Pipeline::RunContentCompiler(
                arguments, [](const Pipeline::ContentCompilerOptions& options)
                {
                    auto registry = std::make_shared<Pipeline::ContentPipelineRegistry>();
                    Pipeline::RegisterBuiltInContentPipeline(*registry, options);
                    return registry;
                });
        }
        catch (...)
        {
            std::cout.rdbuf(previousOut);
            std::cerr.rdbuf(previousErr);
            throw;
        }
        std::cout.rdbuf(previousOut);
        std::cerr.rdbuf(previousErr);
        invocation.output = captured.str();
        return invocation;
    }

    const char* kTwoAssetsAndACopy = R"(    <Compile Include="probe.png">
      <Name>probe</Name>
      <Importer>TextureImporter</Importer>
      <Processor>TextureProcessor</Processor>
    </Compile>
    <Compile Include="tone.wav">
      <Name>tone</Name>
      <Importer>WavImporter</Importer>
      <Processor>SoundEffectProcessor</Processor>
    </Compile>
    <Content Include="readme.txt">
      <CopyToOutputDirectory>PreserveNewest</CopyToOutputDirectory>
    </Content>
)";
}

// The whole point of the row: a project is a source this tool builds.
TEST(XnaContentProjectCommandLine, AProjectIsBuiltWithItsOwnImportersProcessorsAndNames)
{
    const Project project("build");
    project.Write(kTwoAssetsAndACopy);
    const Invocation invocation = Build(project);
    ASSERT_EQ(invocation.exitCode, 0) << invocation.output;

    // Named by the item's own directory plus its `Name`, which for these two is the source stem
    // at the project root, and written as `.xnb` because a content project is an XNA project
    // whatever the tool's own default container is. The rule itself is
    // `TheContentNameIsTheItemsDirectoryPlusItsName` below.
    EXPECT_TRUE(std::filesystem::is_regular_file(project.Output() / "probe.xnb")) << invocation.output;
    EXPECT_TRUE(std::filesystem::is_regular_file(project.Output() / "tone.xnb")) << invocation.output;
    EXPECT_NE(invocation.output.find("Assets: 2"), std::string::npos) << invocation.output;
}

// plans/plan_xnapipeline_parity.md XNAPP-320. Every `.contentproj` a game actually has spells its
// item paths the way MSBuild does, with a backslash, because every one of them was written on
// Windows. The copy path had always resolved that; the *build* path had not, so on a POSIX host a
// real project failed with "the source asset \"Textures\\hero.png\" does not exist" -- a true
// statement about a path nobody meant, and one that made the whole route unusable for the input it
// exists to accept. Both halves resolve it now: the item spec, and the `Link` that redirects one.
TEST(XnaContentProjectCommandLine, AnItemPathSpelledTheWayMsBuildSpellsItResolves)
{
    const Project project("separator");
    std::filesystem::copy_file(project.Source() / "probe.png",
                               project.Source() / "nested" / "probe.png");
    project.Write("    <Compile Include=\"nested\\probe.png\">\n"
                  "      <Importer>TextureImporter</Importer>\n"
                  "      <Processor>TextureProcessor</Processor>\n"
                  "    </Compile>\n");
    const Invocation invocation = Build(project);
    ASSERT_EQ(invocation.exitCode, 0) << invocation.output;

    // The asset lands where the project's own directory structure says it does, whichever
    // separator the project used to say it.
    EXPECT_TRUE(std::filesystem::is_regular_file(project.Output() / "nested" / "probe.xnb"))
        << invocation.output;
}

// plans/plan_xnapipeline_parity.md XNAPP-330. An item's `Name` metadata is MSBuild's `%(Filename)`:
// 8790 of the 8795 `Compile` items in the sample corpus carry a `Name` that is exactly the source
// file's stem, and the five that differ are what settles what it means. Reading it as the asset's
// content name flattened every asset in a subdirectory into the output root, and refused outright a
// project with two same-named files in two folders -- which the Platformer sample has five times
// over, five `Sprites\<Monster>\Idle.png` items all named `Idle`. XNA's own builds answer the rule:
// `Sprites\MonsterA\Idle.png` was built to `Content/Sprites/MonsterA/Idle.xnb` (directory kept),
// `cat_depth.jpg` with `Name` `cat_normalmap` to `Content/cat_normalmap.xnb` (name honoured), and
// `Textures\Backgrounds\honeycombRush_instructions.png` with `Name` `instructions` to
// `Content/Textures/Backgrounds/instructions.xnb`, which is both at once.
TEST(XnaContentProjectCommandLine, TheContentNameIsTheItemsDirectoryPlusItsName)
{
    const Project project("contentname");
    std::filesystem::create_directories(project.Source() / "nested" / "deeper");
    std::filesystem::copy_file(project.Source() / "probe.png",
                               project.Source() / "nested" / "deeper" / "probe.png");
    project.Write("    <Compile Include=\"nested\\deeper\\probe.png\">\n"
                  "      <Name>probe</Name>\n"
                  "      <Importer>TextureImporter</Importer>\n"
                  "      <Processor>TextureProcessor</Processor>\n"
                  "    </Compile>\n"
                  "    <Compile Include=\"probe.png\">\n"
                  "      <Name>renamed</Name>\n"
                  "      <Importer>TextureImporter</Importer>\n"
                  "      <Processor>TextureProcessor</Processor>\n"
                  "    </Compile>\n");
    const Invocation invocation = Build(project);
    ASSERT_EQ(invocation.exitCode, 0) << invocation.output;

    // The directory is kept, and two files of the same name in two directories do not collide.
    EXPECT_TRUE(std::filesystem::is_regular_file(project.Output() / "nested" / "deeper" /
                                                 "probe.xnb")) << invocation.output;
    // And a `Name` that is not the file's stem is honoured, at the item's own directory.
    EXPECT_TRUE(std::filesystem::is_regular_file(project.Output() / "renamed.xnb"))
        << invocation.output;
    EXPECT_FALSE(std::filesystem::exists(project.Output() / "probe.xnb"));
}

// plans/plan_xnapipeline_parity.md XNAPP-330. A project is its item list. A source file sitting in
// the same folder that the project does not name is not part of it: XNA never built it, and
// building it here produces an asset XNA has not got -- or, when the unlisted neighbour shares a
// name with a listed one, refuses a project XNA builds. The Platformer sample is exactly that:
// `Sounds/ExitReached.wav` sits beside the `Sounds/ExitReached.wma` the project lists.
TEST(XnaContentProjectCommandLine, AFileTheProjectDoesNotListIsNotBuilt)
{
    const Project project("unlisted");
    project.Write("    <Compile Include=\"probe.png\">\n"
                  "      <Name>probe</Name>\n"
                  "      <Importer>TextureImporter</Importer>\n"
                  "      <Processor>TextureProcessor</Processor>\n"
                  "    </Compile>\n");
    const Invocation invocation = Build(project);
    ASSERT_EQ(invocation.exitCode, 0) << invocation.output;

    // `tone.wav` is in the project's own directory and has a perfectly good route; the project
    // does not name it, so it is not an asset of this project.
    EXPECT_TRUE(std::filesystem::is_regular_file(project.Output() / "probe.xnb"))
        << invocation.output;
    EXPECT_FALSE(std::filesystem::exists(project.Output() / "tone.xnb")) << invocation.output;
    EXPECT_NE(invocation.output.find("Assets: 1"), std::string::npos) << invocation.output;
}

// plans/plan_xnapipeline_parity.md XNAPP-332. XNA has one `AudioContent`, so its audio importers
// feed either processor: 14 of the sample corpus's `.wma` items ask for `SoundEffectProcessor` and
// 14 of its `.wav` items ask for `SongProcessor`. CNA has two imported types, so both crossings
// used to be refused with "processor 'CNA.SoundEffectProcessor' does not accept imported type
// 'CNA.Content.Pipeline.ImportedSongSource'" -- which broke the Platformer sample's seven sound
// effects. The pair of names now selects the reading, and a convention build of either extension is
// unchanged.
TEST(XnaContentProjectCommandLine, TheProcessorDecidesWhichReadingOfAnAudioSourceIsBuilt)
{
    const Project project("audiopair");
    std::filesystem::copy_file(Locate("tests/assets/xna40/media/wma_mono_44100.wma"),
                               project.Source() / "music.wma");
    project.Write("    <Compile Include=\"music.wma\">\n"
                  "      <Name>music</Name>\n"
                  "      <Importer>WmaImporter</Importer>\n"
                  "      <Processor>SoundEffectProcessor</Processor>\n"
                  "    </Compile>\n"
                  "    <Compile Include=\"tone.wav\">\n"
                  "      <Name>tone</Name>\n"
                  "      <Importer>WavImporter</Importer>\n"
                  "      <Processor>SongProcessor</Processor>\n"
                  "    </Compile>\n");
    const Invocation invocation = Build(project);
    ASSERT_EQ(invocation.exitCode, 0) << invocation.output;

    ASSERT_TRUE(std::filesystem::is_regular_file(project.Output() / "music.xnb"))
        << invocation.output;
    ASSERT_TRUE(std::filesystem::is_regular_file(project.Output() / "tone.xnb"))
        << invocation.output;

    // Each is the asset the *processor* names, whatever the source extension usually means: the
    // compressed source became a SoundEffect and the WAV became a Song.
    const auto reader = [](const std::filesystem::path& asset)
    {
        std::ifstream stream(asset, std::ios::binary);
        const std::string bytes{std::istreambuf_iterator<char>(stream),
                                std::istreambuf_iterator<char>()};
        return bytes;
    };
    EXPECT_NE(reader(project.Output() / "music.xnb")
                  .find("Microsoft.Xna.Framework.Content.SoundEffectReader"),
              std::string::npos);
    EXPECT_NE(reader(project.Output() / "tone.xnb")
                  .find("Microsoft.Xna.Framework.Content.SongReader"),
              std::string::npos);
}

// plans/plan_xnapipeline_parity.md XNAPP-332. An `.xnb` carries the media file's path relative to
// the `.xnb` that names it, not to the content root: XNA's own `Content/Sounds/Music.xnb` names
// `Music.wma`, `Content-phone/Sounds/NinjAcademy.xnb` names `NinjAcademy_Music.wma`, and
// NetRumble's root-level song names `One Step Beyond.wma`. CNA's own reader resolves it that way
// too, so a root-relative spelling was wrong on both sides at once -- a song in a subdirectory
// resolved to `Content/Sounds/Sounds/Music.wma`, and only a song at the content root worked.
TEST(XnaContentProjectCommandLine, ASongsMediaPathIsRelativeToTheAssetThatNamesIt)
{
    const Project project("songpath");
    std::filesystem::create_directories(project.Source() / "Music");
    std::filesystem::copy_file(Locate("tests/assets/xna40/media/wma_mono_44100.wma"),
                               project.Source() / "Music" / "theme.wma");
    project.Write("    <Compile Include=\"Music\\theme.wma\">\n"
                  "      <Name>theme</Name>\n"
                  "      <Importer>WmaImporter</Importer>\n"
                  "      <Processor>SongProcessor</Processor>\n"
                  "    </Compile>\n");
    const Invocation invocation = Build(project);
    ASSERT_EQ(invocation.exitCode, 0) << invocation.output;

    const std::filesystem::path asset = project.Output() / "Music" / "theme.xnb";
    ASSERT_TRUE(std::filesystem::is_regular_file(asset)) << invocation.output;
    std::ifstream stream(asset, std::ios::binary);
    const std::string bytes{std::istreambuf_iterator<char>(stream),
                            std::istreambuf_iterator<char>()};
    EXPECT_NE(bytes.find("theme.wma"), std::string::npos);
    EXPECT_EQ(bytes.find("Music/theme.wma"), std::string::npos) << "the path is root-relative";
}

// The other half of the same rule: nothing about a directory build changes. A `.wma` with no
// project to name a processor is still a song and a `.wav` is still a sound effect, because the
// second reading of each extension is registered to be asked for by name and takes no part in
// choosing a route.
TEST(XnaContentProjectCommandLine, AConventionBuildStillReadsEachAudioExtensionOneWay)
{
    const Project project("audioconvention");
    std::filesystem::copy_file(Locate("tests/assets/xna40/media/wma_mono_44100.wma"),
                               project.Source() / "music.wma");
    project.Write("    <Compile Include=\"music.wma\">\n"
                  "      <Name>music</Name>\n"
                  "    </Compile>\n"
                  "    <Compile Include=\"tone.wav\">\n"
                  "      <Name>tone</Name>\n"
                  "    </Compile>\n");
    const Invocation invocation = Build(project);
    ASSERT_EQ(invocation.exitCode, 0) << invocation.output;

    const auto reader = [](const std::filesystem::path& asset)
    {
        std::ifstream stream(asset, std::ios::binary);
        return std::string{std::istreambuf_iterator<char>(stream),
                           std::istreambuf_iterator<char>()};
    };
    EXPECT_NE(reader(project.Output() / "music.xnb")
                  .find("Microsoft.Xna.Framework.Content.SongReader"),
              std::string::npos);
    EXPECT_NE(reader(project.Output() / "tone.xnb")
                  .find("Microsoft.Xna.Framework.Content.SoundEffectReader"),
              std::string::npos);
}

// The items XNA's targets copy rather than build.
TEST(XnaContentProjectCommandLine, ContentAndNoneItemsAreCopiedAsTheirMetadataSays)
{
    const Project project("copy");
    project.Write(std::string(kTwoAssetsAndACopy) +
                  "    <None Include=\"nested\\notes.txt\">\n"
                  "      <CopyToOutputDirectory>Always</CopyToOutputDirectory>\n"
                  "      <Link>notes.txt</Link>\n"
                  "    </None>\n");
    const Invocation first = Build(project);
    ASSERT_EQ(first.exitCode, 0) << first.output;

    // `Include` says where the file is; `Link` says where the copy lands.
    EXPECT_TRUE(std::filesystem::is_regular_file(project.Output() / "readme.txt")) << first.output;
    EXPECT_TRUE(std::filesystem::is_regular_file(project.Output() / "notes.txt")) << first.output;
    EXPECT_FALSE(std::filesystem::exists(project.Output() / "nested" / "notes.txt"));
    EXPECT_NE(first.output.find("Copied: 2"), std::string::npos) << first.output;

    // `PreserveNewest` means what it says on a second run, and `Always` means what it says.
    const Invocation second = Build(project);
    ASSERT_EQ(second.exitCode, 0) << second.output;
    EXPECT_NE(second.output.find("[SKIP] readme.txt (copy, up to date)"), std::string::npos)
        << second.output;
    EXPECT_NE(second.output.find("[COPY] notes.txt"), std::string::npos) << second.output;
    EXPECT_NE(second.output.find("Copied: 1"), std::string::npos) << second.output;
    // And nothing was rebuilt, because a project build is incremental like any other.
    EXPECT_NE(second.output.find("Built: 0  Assets: 2"), std::string::npos) << second.output;
}

// The project's platform, profile and compression reach the container, which is the difference
// between building a project and building a directory that happens to hold the same files.
TEST(XnaContentProjectCommandLine, TheProjectsOwnPlatformAndProfileReachTheContainer)
{
    const Project project("platform");
    project.Write("    <Compile Include=\"probe.png\">\n"
                  "      <Name>probe</Name>\n"
                  "      <Importer>TextureImporter</Importer>\n"
                  "      <Processor>TextureProcessor</Processor>\n"
                  "    </Compile>\n",
                  "    <XnaPlatform>Windows Phone</XnaPlatform>\n"
                  "    <XnaProfile>Reach</XnaProfile>\n");
    const Invocation invocation = Build(project);
    ASSERT_EQ(invocation.exitCode, 0) << invocation.output;

    std::ifstream stream(project.Output() / "probe.xnb", std::ios::binary);
    const std::vector<char> bytes{std::istreambuf_iterator<char>(stream),
                                  std::istreambuf_iterator<char>()};
    ASSERT_GE(bytes.size(), 6u);
    // `Windows Phone` with the space is how a project spells it; `m` is the platform byte.
    EXPECT_EQ(bytes[3], 'm');
    EXPECT_EQ(bytes[5] & 0x01, 0) << "Reach";
}

// A project naming a component this build has not got is refused, with all of them named at once.
TEST(XnaContentProjectCommandLine, AProjectNeedingACustomComponentIsRefusedByName)
{
    const Project project("custom");
    project.Write("    <Compile Include=\"probe.png\">\n"
                  "      <Name>probe</Name>\n"
                  "      <Importer>TextureImporter</Importer>\n"
                  "      <Processor>SkyProcessor</Processor>\n"
                  "    </Compile>\n"
                  "    <Compile Include=\"tone.wav\">\n"
                  "      <Name>tone</Name>\n"
                  "      <Importer>SongImporter</Importer>\n"
                  "      <Processor>SoundEffectProcessor</Processor>\n"
                  "    </Compile>\n");
    const Invocation invocation = Build(project);
    EXPECT_NE(invocation.exitCode, 0) << invocation.output;
    EXPECT_NE(invocation.output.find("SkyProcessor"), std::string::npos) << invocation.output;
    EXPECT_NE(invocation.output.find("SongImporter"), std::string::npos)
        << "both are named in one run, not one build at a time\n" << invocation.output;
    EXPECT_FALSE(std::filesystem::exists(project.Output() / "probe.xnb"));
}

// A processor parameter the processor has not got is XNA's own leniency, which a project build
// takes and the plain tool does not: it warns and builds with the default (XNAPP-267).
TEST(XnaContentProjectCommandLine, AProjectBuildTakesXnasLeniencyWhereTheToolWouldRefuse)
{
    const Project project("lenient");
    project.Write("    <Compile Include=\"probe.png\">\n"
                  "      <Name>probe</Name>\n"
                  "      <Importer>TextureImporter</Importer>\n"
                  "      <Processor>TextureProcessor</Processor>\n"
                  "      <ProcessorParameters_NoSuchProperty>7</ProcessorParameters_NoSuchProperty>\n"
                  "    </Compile>\n");
    const Invocation invocation = Build(project);
    EXPECT_EQ(invocation.exitCode, 0) << invocation.output;
    EXPECT_TRUE(std::filesystem::is_regular_file(project.Output() / "probe.xnb")) << invocation.output;
    EXPECT_NE(invocation.output.find("NoSuchProperty"), std::string::npos)
        << "the parameter is dropped with a warning, not in silence\n" << invocation.output;
}

// -- what a command line selects reaches a project build (plan_xna_sample_xnb_sweep.md) ---------

namespace
{
    /** @brief The container header a `.xnb` carries: platform byte, version, flags. */
    struct XnbHeader
    {
        char platform = '\0';
        unsigned version = 0u;
        unsigned flags = 0u;
    };

    XnbHeader ReadXnbHeader(const std::filesystem::path& path)
    {
        std::ifstream stream(path, std::ios::binary);
        char bytes[6] = {};
        stream.read(bytes, sizeof(bytes));
        XnbHeader header;
        if (stream.gcount() == static_cast<std::streamsize>(sizeof(bytes)) && bytes[0] == 'X' &&
            bytes[1] == 'N' && bytes[2] == 'B')
        {
            header.platform = bytes[3];
            header.version = static_cast<unsigned char>(bytes[4]);
            header.flags = static_cast<unsigned char>(bytes[5]);
        }
        return header;
    }
}

// plans/plan_xna_sample_xnb_sweep.md XNASWEEP-102. A project carries its own target, and a command
// line that names one overrides it -- the way `msbuild /p:XnaPlatform=...` overrides the property
// in the file. Before this the options were parsed, accepted and then dropped: `BuildContent`
// writes its own command line for the coordinator and nothing the outer one selected was in it, so
// every `.contentproj` built Windows/HiDef whatever was asked for. The sample corpus is 1690
// Windows Phone and 238 Xbox 360 references against 5800 Windows ones, none of which could be
// reproduced through the project route at all.
TEST(XnaContentProjectCommandLine, TheTargetOnTheCommandLineOverridesTheProjectsOwn)
{
    const Project project("target_override");
    project.Write("    <Compile Include=\"probe.png\">\n"
                  "      <Importer>TextureImporter</Importer>\n"
                  "      <Processor>TextureProcessor</Processor>\n"
                  "    </Compile>\n");

    const Invocation defaulted = Build(project);
    ASSERT_EQ(defaulted.exitCode, 0) << defaulted.output;
    const XnbHeader before = ReadXnbHeader(project.Output() / "probe.xnb");
    EXPECT_EQ(before.platform, 'w');
    EXPECT_EQ(before.version, 5u);
    EXPECT_EQ(before.flags & 0x01u, 0x01u) << "a project with no XnaProfile is HiDef";

    const Invocation selected =
        Build(project, {"--xnb-platform", "windowsphone", "--xnb-profile", "reach"});
    ASSERT_EQ(selected.exitCode, 0) << selected.output;
    const XnbHeader after = ReadXnbHeader(project.Output() / "probe.xnb");
    EXPECT_EQ(after.platform, 'm');
    EXPECT_EQ(after.version, 5u);
    EXPECT_EQ(after.flags & 0x01u, 0x00u);
}

// plans/plan_xna_sample_xnb_sweep.md XNASWEEP-102, the compression half of the same defect.
TEST(XnaContentProjectCommandLine, CompressionOnTheCommandLineOverridesTheProjectsOwn)
{
    const Project project("compress_override");
    project.Write("    <Compile Include=\"probe.png\">\n"
                  "      <Importer>TextureImporter</Importer>\n"
                  "      <Processor>TextureProcessor</Processor>\n"
                  "    </Compile>\n");

    const Invocation compressed = Build(project, {"--xnb-compress", "lzx"});
    ASSERT_EQ(compressed.exitCode, 0) << compressed.output;
    EXPECT_EQ(ReadXnbHeader(project.Output() / "probe.xnb").flags & 0x80u, 0x80u);

    const Invocation plain = Build(project, {"--xnb-compress", "none"});
    ASSERT_EQ(plain.exitCode, 0) << plain.output;
    EXPECT_EQ(ReadXnbHeader(project.Output() / "probe.xnb").flags & 0x80u, 0x00u);
}

// plans/plan_xna_sample_xnb_sweep.md XNASWEEP-101. `BuildContent` writes its own command line, so
// the build-tool *services* the outer one selected -- the effect compiler, the XMA encoder, the
// font search -- reached nothing. A project with a `.spritefont` naming a family that is not
// installed on the build machine could not be built at all, however the machine was told where the
// font was, and that is 260 of the sample corpus's assets.
TEST(XnaContentProjectCommandLine, AFontDirectoryOnTheCommandLineReachesAProjectBuild)
{
    const Project project("font_directory");
    const std::filesystem::path font = Locate("tests/assets/fonts/LiberationMono-Regular.ttf");
    if (!std::filesystem::exists(font)) { GTEST_SKIP() << "the vendored test font is missing"; }
    // Deliberately *not* beside the description: this is the case a build machine has, where the
    // fonts a game ships are in a directory of their own.
    const std::filesystem::path fonts = project.Source().parent_path() / "fonts";
    std::filesystem::create_directories(fonts);
    std::filesystem::copy_file(font, fonts / font.filename(),
                               std::filesystem::copy_options::overwrite_existing);
    {
        std::ofstream stream(project.Source() / "Hud.spritefont");
        stream << "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n"
               << "<XnaContent xmlns:Graphics=\"Microsoft.Xna.Framework.Content.Pipeline.Graphics\">\n"
               << "  <Asset Type=\"Graphics:FontDescription\">\n"
               << "    <FontName>Liberation Mono</FontName>\n"
               << "    <Size>12</Size>\n"
               << "    <Spacing>0</Spacing>\n"
               << "    <UseKerning>true</UseKerning>\n"
               << "    <Style>Regular</Style>\n"
               << "    <CharacterRegions>\n"
               << "      <CharacterRegion><Start>&#32;</Start><End>&#64;</End></CharacterRegion>\n"
               << "    </CharacterRegions>\n"
               << "  </Asset>\n"
               << "</XnaContent>\n";
    }
    project.Write("    <Compile Include=\"Hud.spritefont\">\n"
                  "      <Importer>FontDescriptionImporter</Importer>\n"
                  "      <Processor>FontDescriptionProcessor</Processor>\n"
                  "    </Compile>\n");

    // Asserted on where the font came from rather than on the build failing without the option:
    // this machine may well have the family installed, and then the build succeeds either way and
    // the only observable difference is which file it read.
    const Invocation without = Build(project);
    EXPECT_EQ(without.output.find(fonts.string()), std::string::npos)
        << "nothing told this build about that directory: " << without.output;

    const Invocation with = Build(project, {"--font-directory", fonts.string()});
    ASSERT_EQ(with.exitCode, 0) << with.output;
    EXPECT_TRUE(std::filesystem::is_regular_file(project.Output() / "Hud.xnb")) << with.output;
    EXPECT_NE(with.output.find((fonts / font.filename()).string()), std::string::npos)
        << "the configured directory is searched ahead of the platform's own: " << with.output;
    EXPECT_NE(with.output.find("told to search"), std::string::npos) << with.output;
}

// plans/plan_xna_sample_xnb_sweep.md XNASWEEP-103. A project is its item list, and XNA builds every
// item it can: `BuildContent` fails the ones whose processor it cannot find, keeps the ones it
// can, and returns false. CNA refused the whole project instead -- once for the aggregate
// diagnostic, once for the `PipelineAssemblies` item, and once more inside the task at the first
// asset it could not route -- so one game-defined processor cost every other asset. Across the
// public XNA sample corpus that was 107 of 328 content projects producing nothing at all, most of
// them over three or four assets out of dozens.
TEST(XnaContentProjectCommandLine, AnAssetWithNoComponentFailsWithoutTakingTheProjectWithIt)
{
    const Project project("partial_build");
    std::filesystem::copy_file(project.Source() / "probe.png",
                               project.Source() / "nested" / "probe.png");
    project.Write("    <Compile Include=\"probe.png\">\n"
                  "      <Importer>TextureImporter</Importer>\n"
                  "      <Processor>TextureProcessor</Processor>\n"
                  "    </Compile>\n"
                  "    <Compile Include=\"nested\\probe.png\">\n"
                  "      <Name>custom</Name>\n"
                  "      <Importer>TextureImporter</Importer>\n"
                  "      <Processor>SkyProcessor</Processor>\n"
                  "    </Compile>\n");

    const Invocation invocation = Build(project);
    EXPECT_NE(invocation.exitCode, 0) << "the project did not build completely: "
                                      << invocation.output;
    EXPECT_NE(invocation.output.find("SkyProcessor"), std::string::npos) << invocation.output;
    EXPECT_TRUE(std::filesystem::is_regular_file(project.Output() / "probe.xnb"))
        << "the asset whose components are built-in is still built: " << invocation.output;
    EXPECT_FALSE(std::filesystem::exists(project.Output() / "nested" / "custom.xnb"))
        << invocation.output;
}

// plans/plan_xna_sample_xnb_sweep.md XNASWEEP-103, the `PipelineAssemblies` half. Refusing beats
// ignoring -- a project naming its own pipeline assembly expects its own components to run and
// this build has no assembly to load -- but the refusal belongs after building what it can, not
// instead of it. 162 of the corpus's 328 build units reference a pipeline assembly, and most of
// their assets route to built-in components.
TEST(XnaContentProjectCommandLine, APipelineAssemblyFailsTheBuildAfterItProducesWhatItCan)
{
    const Project project("pipeline_assembly");
    project.Write("    <Compile Include=\"probe.png\">\n"
                  "      <Importer>TextureImporter</Importer>\n"
                  "      <Processor>TextureProcessor</Processor>\n"
                  "    </Compile>\n"
                  "    <ProjectReference Include=\"..\\Pipeline\\Pipeline.csproj\">\n"
                  "      <Name>Pipeline</Name>\n"
                  "    </ProjectReference>\n");

    const Invocation invocation = Build(project);
    EXPECT_NE(invocation.exitCode, 0) << invocation.output;
    EXPECT_NE(invocation.output.find("pipeline assemblies"), std::string::npos) << invocation.output;
    EXPECT_TRUE(std::filesystem::is_regular_file(project.Output() / "probe.xnb")) << invocation.output;
}

// plans/plan_xna_sample_xnb_sweep.md XNASWEEP-110. A content build must not need write access to
// the content it is reading. `BuildContent` wrote its own configuration into the source root and
// deleted it again, because the coordinator required a configuration to live inside the root it
// was reading -- so a read-only source tree could not be built at all, and every build that did
// succeed changed the modification time of the project's directory. Vendored content, a checkout
// mounted read-only and somebody else's sample corpus are all ordinary things to build; sweeping
// the public XNA sample corpus changed the timestamp of 130 directories in it before this.
TEST(XnaContentProjectCommandLine, AReadOnlySourceTreeBuildsAndIsNotWrittenTo)
{
    const Project project("read_only");
    project.Write("    <Compile Include=\"probe.png\">\n"
                  "      <Importer>TextureImporter</Importer>\n"
                  "      <Processor>TextureProcessor</Processor>\n"
                  "    </Compile>\n");

    const auto contents = [&project]
    {
        std::vector<std::string> names;
        for (const auto& entry : std::filesystem::directory_iterator(project.Source()))
        {
            names.push_back(entry.path().filename().string());
        }
        std::sort(names.begin(), names.end());
        return names;
    };
    const std::vector<std::string> before = contents();

    // Restored however the test ends, so a failure cannot leave a directory the harness cannot
    // clean up.
    struct Writable
    {
        std::filesystem::path path;
        std::filesystem::perms original;
        ~Writable()
        {
            std::error_code ignored;
            std::filesystem::permissions(path, original, ignored);
        }
    };
    std::error_code error;
    const std::filesystem::perms original =
        std::filesystem::status(project.Source(), error).permissions();
    const Writable restore{project.Source(), original};
    std::filesystem::permissions(project.Source(),
                                 std::filesystem::perms::owner_write |
                                     std::filesystem::perms::group_write |
                                     std::filesystem::perms::others_write,
                                 std::filesystem::perm_options::remove, error);
    ASSERT_FALSE(error) << error.message();

    const Invocation invocation = Build(project);
    EXPECT_EQ(invocation.exitCode, 0) << invocation.output;
    EXPECT_TRUE(std::filesystem::is_regular_file(project.Output() / "probe.xnb")) << invocation.output;
    EXPECT_EQ(contents(), before) << "the build left something in the source tree";
}
