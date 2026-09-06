// SPDX-License-Identifier: MS-PL
//
// plans/plan_xnapipeline_parity.md XNAPP-240, XNAPP-242: reading an XNA content project.
//
// A `.contentproj` is an MSBuild project and CNA does not host MSBuild. What it is, for building
// content, is a list of source assets with their importer, processor and parameters plus a few
// project properties -- which is exactly a BuildContent task's inputs. So the reader fills a task
// and the task runs the canonical coordinator: no second build engine, no second project format,
// and `.cna-content.json` stays CNA's own.
//
// The corpus is not synthetic. XNAPP-242 asks for the public XNA samples' own projects, and this
// machine has them; the sweep below reads every one it finds and reports what routes, so a claim
// about compatibility is a count over real projects rather than over a fixture somebody wrote to
// pass.
#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <map>
#include <string>
#include <vector>

#include "Microsoft/Xna/Framework/Content/Pipeline/InvalidContentException.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/Tasks/ContentProject.hpp"

namespace Tasks = Microsoft::Xna::Framework::Content::Pipeline::Tasks;
using Microsoft::Xna::Framework::Content::Pipeline::InvalidContentException;

namespace
{
    /** @brief A project written to a scratch file, removed when the test ends. */
    class Written
    {
    public:
        Written(const std::string& name, const std::string& text)
            : path_(std::filesystem::temp_directory_path() / ("cna_xnapp240_" + name))
        {
            std::filesystem::create_directories(path_);
            file_ = path_ / "Content.contentproj";
            std::ofstream out(file_, std::ios::binary);
            out << text;
        }
        ~Written()
        {
            std::error_code error;
            std::filesystem::remove_all(path_, error);
        }
        Written(const Written&) = delete;
        Written& operator=(const Written&) = delete;
        [[nodiscard]] std::string Path() const { return file_.string(); }
        [[nodiscard]] const std::filesystem::path& Directory() const { return path_; }

    private:
        std::filesystem::path path_;
        std::filesystem::path file_;
    };

    const char* const Typical = R"(<?xml version="1.0" encoding="utf-8"?>
<Project DefaultTargets="Build" xmlns="http://schemas.microsoft.com/developer/msbuild/2003" ToolsVersion="4.0">
  <PropertyGroup>
    <ProjectGuid>{02B8AE9F-0D1C-4C02-B0EE-6CC0FA5660FA}</ProjectGuid>
    <Configuration Condition=" '$(Configuration)' == '' ">Debug</Configuration>
    <XnaFrameworkVersion>v4.0</XnaFrameworkVersion>
    <XnaPlatform>Windows Phone</XnaPlatform>
    <XnaProfile>Reach</XnaProfile>
    <XnaCompressContent>true</XnaCompressContent>
    <ContentRootDirectory>Content</ContentRootDirectory>
  </PropertyGroup>
  <ItemGroup>
    <Compile Include="hudFont.spritefont">
      <Name>hudFont</Name>
      <Importer>FontDescriptionImporter</Importer>
      <Processor>FontDescriptionProcessor</Processor>
    </Compile>
    <Compile Include="Textures\cursor.png">
      <Name>cursor</Name>
      <Importer>TextureImporter</Importer>
      <Processor>TextureProcessor</Processor>
      <ProcessorParameters_PremultiplyAlpha>False</ProcessorParameters_PremultiplyAlpha>
      <ProcessorParameters_GenerateMipmaps>True</ProcessorParameters_GenerateMipmaps>
    </Compile>
    <Compile Include="..\Shared\table.FBX">
      <Name>table</Name>
      <Link>table.FBX</Link>
      <Importer>FbxImporter</Importer>
      <Processor>ModelProcessor</Processor>
    </Compile>
  </ItemGroup>
  <ItemGroup>
    <Content Include="readme.txt">
      <CopyToOutputDirectory>PreserveNewest</CopyToOutputDirectory>
    </Content>
    <None Include="notes.md" />
  </ItemGroup>
  <ItemGroup>
    <Reference Include="Microsoft.Xna.Framework.Content.Pipeline, Version=4.0.0.0" />
  </ItemGroup>
  <Import Project="$(MSBuildExtensionsPath)\Microsoft\XNA Game Studio\Microsoft.Xna.GameStudio.ContentPipeline.targets" />
</Project>
)";
}

TEST(XnaContentProject, ReadsEveryPartOfTheSchema)
{
    const Written written("typical", Typical);
    const Tasks::ContentProject project = Tasks::ContentProject::Load(written.Path());

    EXPECT_EQ(project.Property("ProjectGuid"), "{02B8AE9F-0D1C-4C02-B0EE-6CC0FA5660FA}");
    EXPECT_EQ(project.Property("XnaFrameworkVersion"), "v4.0");
    EXPECT_EQ(project.Property("XnaPlatform"), "Windows Phone");
    EXPECT_EQ(project.Property("XnaProfile"), "Reach");
    EXPECT_EQ(project.Property("ContentRootDirectory"), "Content");
    EXPECT_EQ(project.Property("NoSuchProperty"), "");
    // MSBuild's names are case-insensitive and so are these.
    EXPECT_EQ(project.Property("xnaplatform"), "Windows Phone");

    const std::vector<Tasks::ContentProject::Item> assets = project.SourceAssets();
    ASSERT_EQ(assets.size(), 3u);
    const Tasks::ContentProject::Item& texture = assets[1];
    EXPECT_EQ(texture.include, "Textures\\cursor.png");
    EXPECT_EQ(texture.Get("Name"), "cursor");
    EXPECT_EQ(texture.Get("Importer"), "TextureImporter");
    EXPECT_EQ(texture.Get("ProcessorParameters_PremultiplyAlpha"), "False");
    EXPECT_EQ(texture.Get("processorparameters_generatemipmaps"), "True");

    // A file outside the project directory carries a Link saying where it belongs.
    EXPECT_EQ(assets[2].Get("Link"), "table.FBX");

    // Content and None are copied rather than built, and are told apart from what is built.
    const std::vector<Tasks::ContentProject::Item> copied = project.CopiedFiles();
    ASSERT_EQ(copied.size(), 2u);
    EXPECT_EQ(copied[0].include, "readme.txt");
    EXPECT_EQ(copied[0].Get("CopyToOutputDirectory"), "PreserveNewest");
    EXPECT_EQ(copied[1].include, "notes.md");

    EXPECT_TRUE(project.UnroutableEXT().empty());
}

TEST(XnaContentProject, FillsABuildContentTaskWithWhatTheProjectSays)
{
    const Written written("task", Typical);
    const Tasks::ContentProject project = Tasks::ContentProject::Load(written.Path());
    const Tasks::BuildContent task =
        project.ToBuildContentEXT((written.Directory() / "bin").string(),
                                  (written.Directory() / "obj").string());

    EXPECT_EQ(task.getRootDirectoryProperty(), written.Directory().string());
    EXPECT_EQ(task.getContentProjectGUIDProperty(), "{02B8AE9F-0D1C-4C02-B0EE-6CC0FA5660FA}");
    // `Windows Phone` is how a project spells it and `WindowsPhone` is how the pipeline does.
    EXPECT_EQ(task.getTargetPlatformProperty(), "WindowsPhone");
    EXPECT_EQ(task.getTargetProfileProperty(), "Reach");
    EXPECT_TRUE(task.getCompressContentProperty());
    ASSERT_EQ(task.getSourceAssetsProperty().size(), 3u);
    EXPECT_EQ(task.getSourceAssetsProperty()[0].GetMetadata("Name"), "hudFont");
    EXPECT_EQ(task.getSourceAssetsProperty()[1].GetMetadata("ProcessorParameters_PremultiplyAlpha"),
              "False");
    // Microsoft's own pipeline assemblies are the framework, not a custom component, so a project
    // naming them is an ordinary project and not one this build must refuse.
    EXPECT_TRUE(task.getPipelineAssembliesProperty().empty());
}

TEST(XnaContentProject, AProjectThatNamesItsOwnPipelineAssemblyIsRefusedByTheTask)
{
    const Written written("custom", R"(<?xml version="1.0" encoding="utf-8"?>
<Project xmlns="http://schemas.microsoft.com/developer/msbuild/2003">
  <ItemGroup>
    <ProjectReference Include="..\MyGame.Pipeline\MyGame.Pipeline.csproj" />
  </ItemGroup>
</Project>
)");
    const Tasks::ContentProject project = Tasks::ContentProject::Load(written.Path());
    Tasks::BuildContent task = project.ToBuildContentEXT("bin", "obj");
    ASSERT_EQ(task.getPipelineAssembliesProperty().size(), 1u);
    EXPECT_FALSE(task.Execute());
    ASSERT_FALSE(task.ErrorsEXT().empty());
    EXPECT_NE(task.ErrorsEXT().front().find("RegisterXnaImporter"), std::string::npos);
}

TEST(XnaContentProject, ACustomComponentIsNamedRatherThanSilentlyDropped)
{
    const Written written("unroutable", R"(<?xml version="1.0" encoding="utf-8"?>
<Project xmlns="http://schemas.microsoft.com/developer/msbuild/2003">
  <ItemGroup>
    <Compile Include="level.lvl">
      <Name>level</Name>
      <Importer>LevelImporter</Importer>
      <Processor>LevelProcessor</Processor>
    </Compile>
    <Compile Include="ok.png">
      <Name>ok</Name>
      <Importer>TextureImporter</Importer>
      <Processor>TextureProcessor</Processor>
    </Compile>
  </ItemGroup>
</Project>
)");
    const Tasks::ContentProject project = Tasks::ContentProject::Load(written.Path());
    const std::vector<std::string> reasons = project.UnroutableEXT();
    // Both halves of the one unroutable asset are named, and the routable one is not.
    ASSERT_EQ(reasons.size(), 2u);
    EXPECT_NE(reasons[0].find("level.lvl"), std::string::npos);
    EXPECT_NE(reasons[0].find("LevelImporter"), std::string::npos);
    EXPECT_NE(reasons[1].find("LevelProcessor"), std::string::npos);
}

TEST(XnaContentProject, RefusalsNameWhatIsWrong)
{
    EXPECT_THROW((void)Tasks::ContentProject::Load("no_such_project.contentproj"),
                 InvalidContentException);

    // Not XML at all.
    try
    {
        (void)Tasks::ContentProject::Parse("this is not xml", "probe");
        ADD_FAILURE() << "text that is not XML was accepted";
    }
    catch (const InvalidContentException& error)
    {
        EXPECT_NE(error.getMessageProperty().find("not readable XML"), std::string::npos);
    }

    // XML, but not an MSBuild project.
    try
    {
        (void)Tasks::ContentProject::Parse("<XnaContent><Asset /></XnaContent>", "probe");
        ADD_FAILURE() << "a document that is not a project was accepted";
    }
    catch (const InvalidContentException& error)
    {
        EXPECT_NE(error.getMessageProperty().find("MSBuild <Project> root"), std::string::npos);
    }

    // A condition this reader will not guess at is refused rather than silently dropping items.
    try
    {
        (void)Tasks::ContentProject::Parse(
            R"(<Project xmlns="http://schemas.microsoft.com/developer/msbuild/2003">
                 <ItemGroup Condition=" Exists('extra.targets') ">
                   <Compile Include="only_sometimes.png"><Name>x</Name></Compile>
                 </ItemGroup>
               </Project>)",
            "probe");
        ADD_FAILURE() << "an undecidable condition was silently taken or dropped";
    }
    catch (const InvalidContentException& error)
    {
        EXPECT_NE(error.getMessageProperty().find("will not guess at"), std::string::npos);
    }
}

// XNAPP-242: the public samples' own projects, if this machine has them. Not a fixture written to
// pass -- a sweep over whatever is there, reporting what routes and what does not.
// The conditions a real project actually uses -- a default-value guard and a
// Configuration|Platform comparison -- are decided in document order, so the guard means what it
// says and a group for another configuration is dropped rather than refused.
TEST(XnaContentProject, TheConditionsRealProjectsUseAreDecidedInDocumentOrder)
{
    const Tasks::ContentProject project = Tasks::ContentProject::Parse(
        R"(<Project xmlns="http://schemas.microsoft.com/developer/msbuild/2003">
             <PropertyGroup>
               <Configuration Condition=" '$(Configuration)' == '' ">Debug</Configuration>
               <Platform Condition=" '$(Platform)' == '' ">x86</Platform>
             </PropertyGroup>
             <PropertyGroup Condition=" '$(Configuration)|$(Platform)' == 'Debug|x86' ">
               <XnaCompressContent>false</XnaCompressContent>
             </PropertyGroup>
             <PropertyGroup Condition=" '$(Configuration)|$(Platform)' == 'Release|Xbox 360' ">
               <XnaCompressContent>true</XnaCompressContent>
               <XnaPlatform>Xbox 360</XnaPlatform>
             </PropertyGroup>
             <ItemGroup Condition=" '$(Platform)' != 'x86' ">
               <Compile Include="only_off_x86.png"><Name>x</Name></Compile>
             </ItemGroup>
           </Project>)",
        "probe");
    EXPECT_EQ(project.Property("Configuration"), "Debug");
    EXPECT_EQ(project.Property("Platform"), "x86");
    // The Debug|x86 group applied and the Release|Xbox 360 one did not.
    EXPECT_EQ(project.Property("XnaCompressContent"), "false");
    EXPECT_EQ(project.Property("XnaPlatform"), "");
    // And the item group guarded off this platform contributed nothing.
    EXPECT_TRUE(project.SourceAssets().empty());
}

TEST(XnaContentProject, ThePublicSamplesProjectsAreReadAndTheirRoutesReported)
{
    const std::filesystem::path samples("/rv/tmp/samples");
    std::error_code error;
    if (!std::filesystem::exists(samples, error) || error)
    {
        GTEST_SKIP() << "the public XNA samples are not on this machine";
    }
    std::vector<std::filesystem::path> projects;
    for (std::filesystem::recursive_directory_iterator it(
             samples, std::filesystem::directory_options::skip_permission_denied, error);
         it != std::filesystem::recursive_directory_iterator(); it.increment(error))
    {
        if (error)
        {
            break;
        }
        if (it->is_regular_file(error) && it->path().extension() == ".contentproj")
        {
            projects.push_back(it->path());
        }
    }
    if (projects.empty())
    {
        GTEST_SKIP() << "no .contentproj files were found under " << samples;
    }
    std::size_t read = 0;
    std::size_t refused = 0;
    std::size_t assets = 0;
    std::size_t routable = 0;
    std::map<std::string, std::size_t> unroutable;
    std::vector<std::string> refusals;
    for (const std::filesystem::path& one : projects)
    {
        try
        {
            const Tasks::ContentProject project = Tasks::ContentProject::Load(one.string());
            ++read;
            assets += project.SourceAssets().size();
            const std::vector<std::string> reasons = project.UnroutableEXT();
            if (reasons.empty())
            {
                ++routable;
            }
            for (const std::string& reason : reasons)
            {
                unroutable[reason.substr(reason.find(": ") + 2u)]++;
            }
        }
        catch (const InvalidContentException& failure)
        {
            ++refused;
            if (refusals.size() < 5u)
            {
                refusals.push_back(one.filename().string() + ": " + failure.getMessageProperty());
            }
        }
    }
    std::string report = "read " + std::to_string(read) + " of " + std::to_string(projects.size()) +
                         " projects (" + std::to_string(assets) + " source assets); " +
                         std::to_string(routable) + " route entirely";
    for (const auto& [reason, count] : unroutable)
    {
        report += "\n  " + std::to_string(count) + "x " + reason;
    }
    for (const std::string& refusal : refusals)
    {
        report += "\n  refused: " + refusal;
    }
    // Every project must be readable: a refusal here is a schema this reader does not handle, and
    // the message says which. The routing count is reported rather than asserted, because a
    // sample that uses its own processor is not a defect in this reader.
    EXPECT_EQ(refused, 0u) << report;
    EXPECT_GT(assets, 0u) << report;
    std::cout << "[  SAMPLES ] " << report << std::endl;
}
