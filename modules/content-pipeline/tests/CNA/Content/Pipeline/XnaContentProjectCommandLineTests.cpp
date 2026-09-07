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

    // Named by the project's own `Name`, not by the file's stem, and written as `.xnb` because a
    // content project is an XNA project whatever the tool's own default container is.
    EXPECT_TRUE(std::filesystem::is_regular_file(project.Output() / "probe.xnb")) << invocation.output;
    EXPECT_TRUE(std::filesystem::is_regular_file(project.Output() / "tone.xnb")) << invocation.output;
    EXPECT_NE(invocation.output.find("Assets: 2"), std::string::npos) << invocation.output;
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
