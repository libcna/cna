// SPDX-License-Identifier: MS-PL
//
// plans/plan_xnapipeline_parity.md XNAPP-241: the four MSBuild tasks, as tasks a caller can drive.
//
// These are HOST_SUBSTITUTION rows, and the substitution is worth stating precisely: every input
// property, every output property and the `bool Execute()` contract are reproduced, and what is
// not is MSBuild's engine -- nothing sets these properties for you and nothing calls Execute() for
// you. So the tests drive them the way an MSBuild project would, and check what a project would
// read back.
//
// They also hold the two refusals that matter more than the successes: a project naming a pipeline
// assembly is refused rather than silently built without its own importers, and BuildXact
// validates its projects before reporting that Microsoft's compiler is absent.
#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "Microsoft/Xna/Framework/Content/Pipeline/Tasks/BuildContent.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/Tasks/BuildXact.hpp"
#include "Microsoft/Xna/Framework/Content/Pipeline/Tasks/CleanContent.hpp"

namespace Tasks = Microsoft::Xna::Framework::Content::Pipeline::Tasks;

namespace
{
    std::filesystem::path Locate(const std::filesystem::path& relative)
    {
        for (std::filesystem::path dir = std::filesystem::current_path(); !dir.empty();
             dir = dir.parent_path())
        {
            if (std::filesystem::exists(dir / relative))
            {
                return dir / relative;
            }
            if (dir == dir.root_path())
            {
                break;
            }
        }
        for (std::filesystem::path dir = std::filesystem::path(__FILE__).parent_path(); !dir.empty();
             dir = dir.parent_path())
        {
            if (std::filesystem::exists(dir / relative))
            {
                return dir / relative;
            }
            if (dir == dir.root_path())
            {
                break;
            }
        }
        return relative;
    }

    /** @brief A content project laid out on disk, removed when the test ends. */
    class Project
    {
    public:
        explicit Project(const std::string& name)
            : root_(std::filesystem::temp_directory_path() / ("cna_xnapp241_" + name))
        {
            std::filesystem::remove_all(root_);
            std::filesystem::create_directories(Source());
            std::filesystem::create_directories(Output());
            std::filesystem::create_directories(Intermediate());
        }
        ~Project()
        {
            std::error_code error;
            std::filesystem::remove_all(root_, error);
        }
        Project(const Project&) = delete;
        Project& operator=(const Project&) = delete;

        [[nodiscard]] std::filesystem::path Source() const { return root_ / "Content"; }
        [[nodiscard]] std::filesystem::path Output() const { return root_ / "bin"; }
        [[nodiscard]] std::filesystem::path Intermediate() const { return root_ / "obj"; }

        /** @brief Copies a committed fixture into the project and answers its path. */
        [[nodiscard]] std::string Add(const std::filesystem::path& fixture, const std::string& as) const
        {
            const std::filesystem::path target = Source() / as;
            std::filesystem::create_directories(target.parent_path());
            std::filesystem::copy_file(fixture, target,
                                       std::filesystem::copy_options::overwrite_existing);
            return target.string();
        }

        [[nodiscard]] std::string Write(const std::string& as, const std::string& text) const
        {
            const std::filesystem::path target = Source() / as;
            std::filesystem::create_directories(target.parent_path());
            std::ofstream file(target, std::ios::binary);
            file << text;
            return target.string();
        }

    private:
        std::filesystem::path root_;
    };

    Tasks::BuildContent MakeBuild(const Project& project)
    {
        Tasks::BuildContent task;
        task.setRootDirectoryProperty(project.Source().string());
        task.setOutputDirectoryProperty(project.Output().string());
        task.setIntermediateDirectoryProperty(project.Intermediate().string());
        task.setBuildConfigurationProperty("Release");
        task.setTargetPlatformProperty("Windows");
        task.setTargetProfileProperty("HiDef");
        task.setContentProjectGUIDProperty("{4C0E8D7A-0000-0000-0000-000000000001}");
        return task;
    }

    bool Names(const std::vector<Tasks::TaskItem>& items, const std::string& suffix)
    {
        return std::any_of(items.begin(), items.end(), [&suffix](const Tasks::TaskItem& item)
                           {
                               const std::string& spec = item.getItemSpecProperty();
                               return spec.size() >= suffix.size() &&
                                      spec.compare(spec.size() - suffix.size(), suffix.size(), suffix) == 0;
                           });
    }
}

// A TaskItem is an ItemSpec and a case-insensitive metadata table, which is ITaskItem's whole
// developer-visible contract.
TEST(XnaTaskItem, CarriesAPathAndCaseInsensitiveMetadata)
{
    Tasks::TaskItem item("Content/Tree.png");
    EXPECT_EQ(item.getItemSpecProperty(), "Content/Tree.png");
    EXPECT_EQ(item.GetMetadata("Processor"), "");
    EXPECT_FALSE(item.HasMetadata("Processor"));
    item.SetMetadata("Processor", "TextureProcessor");
    EXPECT_EQ(item.GetMetadata("processor"), "TextureProcessor");
    EXPECT_EQ(item.GetMetadata("PROCESSOR"), "TextureProcessor");
    EXPECT_TRUE(item.HasMetadata("pRoCeSsOr"));
    item.SetMetadata("Name", "");
    // A name present with an empty value is present, as MSBuild's is.
    EXPECT_TRUE(item.HasMetadata("Name"));
    EXPECT_EQ(item.MetadataNames().size(), 2u);
}

TEST(XnaBuildContent, TheCancelEventNameIsXnasOwnTemplate)
{
    // Read from the assemblies' metadata, verbatim: a string.Format template whose one argument is
    // the content project's GUID.
    EXPECT_EQ(Tasks::BuildContent::CancelEventNameFormat,
              "Local\\Microsoft.Xna.GameStudio.ContentPipeline.CancelBuildEvent+{0}");
}

TEST(XnaBuildContent, EveryPropertyRoundTripsAndTheOutputsStartEmpty)
{
    Tasks::BuildContent task;
    EXPECT_FALSE(task.getCompressContentProperty());
    EXPECT_FALSE(task.getRebuildAllProperty());
    EXPECT_TRUE(task.getSourceAssetsProperty().empty());
    EXPECT_TRUE(task.getOutputContentFilesProperty().empty());
    EXPECT_TRUE(task.getIntermediateFilesProperty().empty());
    EXPECT_TRUE(task.getRebuiltContentFilesProperty().empty());

    task.setBuildConfigurationProperty("Debug");
    task.setCompressContentProperty(true);
    task.setContentProjectGUIDProperty("{GUID}");
    task.setIntermediateDirectoryProperty("obj");
    task.setLoggerRootDirectoryProperty("..");
    task.setOutputDirectoryProperty("bin");
    task.setRebuildAllProperty(true);
    task.setRootDirectoryProperty("Content");
    task.setTargetPlatformProperty("Xbox360");
    task.setTargetProfileProperty("Reach");
    task.setSourceAssetsProperty({Tasks::TaskItem("a.png")});
    task.setPipelineAssembliesProperty({Tasks::TaskItem("Custom.dll")});
    task.setPipelineAssemblyDependenciesProperty({Tasks::TaskItem("Helper.dll")});

    EXPECT_EQ(task.getBuildConfigurationProperty(), "Debug");
    EXPECT_TRUE(task.getCompressContentProperty());
    EXPECT_EQ(task.getContentProjectGUIDProperty(), "{GUID}");
    EXPECT_EQ(task.getIntermediateDirectoryProperty(), "obj");
    EXPECT_EQ(task.getLoggerRootDirectoryProperty(), "..");
    EXPECT_EQ(task.getOutputDirectoryProperty(), "bin");
    EXPECT_TRUE(task.getRebuildAllProperty());
    EXPECT_EQ(task.getRootDirectoryProperty(), "Content");
    EXPECT_EQ(task.getTargetPlatformProperty(), "Xbox360");
    EXPECT_EQ(task.getTargetProfileProperty(), "Reach");
    EXPECT_EQ(task.getSourceAssetsProperty().size(), 1u);
    EXPECT_EQ(task.getPipelineAssembliesProperty().size(), 1u);
    EXPECT_EQ(task.getPipelineAssemblyDependenciesProperty().size(), 1u);
}

TEST(XnaBuildContent, AnEmptyProjectSucceedsAndAMisconfiguredOneDoesNot)
{
    Tasks::BuildContent nothing;
    nothing.setRootDirectoryProperty("Content");
    nothing.setOutputDirectoryProperty("bin");
    // A content project with nothing in it builds nothing and succeeds, as MSBuild's task does for
    // an empty item list.
    EXPECT_TRUE(nothing.Execute());
    EXPECT_TRUE(nothing.ErrorsEXT().empty());

    Tasks::BuildContent unrooted;
    unrooted.setSourceAssetsProperty({Tasks::TaskItem("a.png")});
    EXPECT_FALSE(unrooted.Execute());
    ASSERT_FALSE(unrooted.ErrorsEXT().empty());
    EXPECT_NE(unrooted.ErrorsEXT().front().find("RootDirectory"), std::string::npos);
}

TEST(XnaBuildContent, APipelineAssemblyIsRefusedRatherThanIgnored)
{
    Project project("assembly");
    Tasks::BuildContent task = MakeBuild(project);
    task.setSourceAssetsProperty(
        {Tasks::TaskItem(project.Add(Locate("tests/assets/xna40/texture/probe.png"), "Tree.png"))});
    task.setPipelineAssembliesProperty({Tasks::TaskItem("MyGame.Pipeline.dll")});
    // A project naming its own pipeline assembly expects its own importers to run. Accepting the
    // list silently would let it believe they did.
    EXPECT_FALSE(task.Execute());
    ASSERT_FALSE(task.ErrorsEXT().empty());
    EXPECT_NE(task.ErrorsEXT().front().find("RegisterXnaImporter"), std::string::npos);
}

TEST(XnaBuildContent, ASourceAssetThatIsNotThereIsNamed)
{
    Project project("missing");
    Tasks::BuildContent task = MakeBuild(project);
    task.setSourceAssetsProperty({Tasks::TaskItem("NoSuchTexture.png")});
    EXPECT_FALSE(task.Execute());
    ASSERT_FALSE(task.ErrorsEXT().empty());
    EXPECT_NE(task.ErrorsEXT().front().find("NoSuchTexture.png"), std::string::npos);
}

// The whole point of the task: a project's assets become .xnb files, and the three output
// properties say what happened.
TEST(XnaBuildContent, AProjectBuildsToXnbAndFillsItsOutputProperties)
{
    Project project("build");
    Tasks::BuildContent task = MakeBuild(project);
    Tasks::TaskItem texture(project.Add(Locate("tests/assets/xna40/texture/probe.png"), "Tree.png"));
    texture.SetMetadata("Name", "Tree");
    texture.SetMetadata("Importer", "TextureImporter");
    texture.SetMetadata("Processor", "TextureProcessor");
    Tasks::TaskItem sound(project.Add(Locate("tests/assets/xna40/media/tone_mono_44100.wav"), "Beep.wav"));
    sound.SetMetadata("Name", "Beep");
    task.setSourceAssetsProperty({texture, sound});

    ASSERT_TRUE(task.Execute()) << (task.ErrorsEXT().empty() ? "" : task.ErrorsEXT().front());
    EXPECT_TRUE(Names(task.getOutputContentFilesProperty(), ".xnb"));
    EXPECT_GE(task.getOutputContentFilesProperty().size(), 2u);
    // Nothing was there before, so everything was rebuilt.
    EXPECT_EQ(task.getRebuiltContentFilesProperty().size(),
              task.getOutputContentFilesProperty().size());
    // The build configuration this task wrote, and the manifest the build left, are its
    // intermediate files.
    EXPECT_GE(task.getIntermediateFilesProperty().size(), 2u);
    for (const Tasks::TaskItem& item : task.getOutputContentFilesProperty())
    {
        EXPECT_TRUE(std::filesystem::exists(item.getItemSpecProperty()))
            << item.getItemSpecProperty();
        EXPECT_FALSE(item.GetMetadata("Name").empty());
    }

    // A second run with nothing changed rebuilds nothing, and still reports every output: which is
    // exactly what an incremental project needs to deploy what is there.
    Tasks::BuildContent again = MakeBuild(project);
    again.setSourceAssetsProperty({texture, sound});
    ASSERT_TRUE(again.Execute()) << (again.ErrorsEXT().empty() ? "" : again.ErrorsEXT().front());
    EXPECT_EQ(again.getOutputContentFilesProperty().size(),
              task.getOutputContentFilesProperty().size());
    EXPECT_TRUE(again.getRebuiltContentFilesProperty().empty());

    // RebuildAll ignores that state.
    Tasks::BuildContent forced = MakeBuild(project);
    forced.setSourceAssetsProperty({texture, sound});
    forced.setRebuildAllProperty(true);
    ASSERT_TRUE(forced.Execute()) << (forced.ErrorsEXT().empty() ? "" : forced.ErrorsEXT().front());
    EXPECT_EQ(forced.getRebuiltContentFilesProperty().size(),
              forced.getOutputContentFilesProperty().size());
}

TEST(XnaGetLastOutputs, AnswersWhatThePreviousBuildLeftAndNothingWhenThereWasNone)
{
    Project project("lastoutputs");
    Tasks::GetLastOutputs before;
    before.setOutputDirectoryEXT(project.Output().string());
    // No previous build is not a failure; it is an empty answer.
    EXPECT_TRUE(before.Execute());
    EXPECT_TRUE(before.getOutputContentFilesProperty().empty());

    Tasks::BuildContent build = MakeBuild(project);
    Tasks::TaskItem texture(project.Add(Locate("tests/assets/xna40/texture/probe.png"), "Tree.png"));
    texture.SetMetadata("Name", "Tree");
    build.setSourceAssetsProperty({texture});
    ASSERT_TRUE(build.Execute()) << (build.ErrorsEXT().empty() ? "" : build.ErrorsEXT().front());

    Tasks::GetLastOutputs after;
    after.setOutputDirectoryEXT(project.Output().string());
    after.setContentProjectGUIDProperty("{4C0E8D7A-0000-0000-0000-000000000001}");
    ASSERT_TRUE(after.Execute());
    EXPECT_EQ(after.getOutputContentFilesProperty().size(),
              build.getOutputContentFilesProperty().size());
    EXPECT_TRUE(Names(after.getOutputContentFilesProperty(), ".xnb"));

    Tasks::GetLastOutputs unrooted;
    EXPECT_FALSE(unrooted.Execute());
    EXPECT_FALSE(unrooted.ErrorsEXT().empty());
}

TEST(XnaCleanContent, RemovesWhatTheBuildOwnedAndSucceedsOnADirectoryThatIsNotThere)
{
    Project project("clean");
    Tasks::BuildContent build = MakeBuild(project);
    Tasks::TaskItem texture(project.Add(Locate("tests/assets/xna40/texture/probe.png"), "Tree.png"));
    texture.SetMetadata("Name", "Tree");
    build.setSourceAssetsProperty({texture});
    ASSERT_TRUE(build.Execute()) << (build.ErrorsEXT().empty() ? "" : build.ErrorsEXT().front());
    const std::string produced = build.getOutputContentFilesProperty().front().getItemSpecProperty();
    ASSERT_TRUE(std::filesystem::exists(produced));

    Tasks::CleanContent clean;
    clean.setRootDirectoryProperty(project.Source().string());
    clean.setOutputDirectoryProperty(project.Output().string());
    clean.setIntermediateDirectoryProperty(project.Intermediate().string());
    clean.setTargetPlatformProperty("Windows");
    clean.setTargetProfileProperty("HiDef");
    ASSERT_TRUE(clean.Execute()) << (clean.ErrorsEXT().empty() ? "" : clean.ErrorsEXT().front());
    EXPECT_FALSE(std::filesystem::exists(produced));

    // Cleaning twice must not fail the second time.
    ASSERT_TRUE(clean.Execute());

    Tasks::CleanContent absent;
    absent.setOutputDirectoryProperty((project.Output() / "not-there").string());
    EXPECT_TRUE(absent.Execute());

    Tasks::CleanContent unrooted;
    EXPECT_FALSE(unrooted.Execute());
}

// BuildXact is the one place an external Microsoft tool is genuinely needed. Everything up to
// invoking it is here and runs, which is what the parity row claims.
TEST(XnaBuildXact, ValidatesItsProjectsBeforeReportingThatTheCompilerIsAbsent)
{
    Project project("xact");
    Tasks::BuildXact task;
    task.setRootDirectoryProperty(project.Source().string());
    task.setOutputDirectoryProperty(project.Output().string());
    task.setIntermediateDirectoryProperty(project.Intermediate().string());
    task.setTargetPlatformProperty("Windows");
    task.setXnaFrameworkVersionProperty("v4.0");

    // No projects: nothing to do, and that is a success.
    EXPECT_TRUE(task.Execute());

    // A project that is not there is named, whether or not a compiler exists.
    task.setXactProjectsProperty({Tasks::TaskItem("NoSuchSounds.xap")});
    EXPECT_FALSE(task.Execute());
    ASSERT_FALSE(task.ErrorsEXT().empty());
    EXPECT_NE(task.ErrorsEXT().back().find("NoSuchSounds.xap"), std::string::npos);

    // A file that is not an XACT project is refused for what it is, not for the missing tool.
    task.setXactProjectsProperty(
        {Tasks::TaskItem(project.Write("NotReally.xap", "this is not an XACT project\n"))});
    EXPECT_FALSE(task.Execute());
    EXPECT_NE(task.ErrorsEXT().back().find("does not look like an XACT project"), std::string::npos);

    // A real-looking one gets all the way to the tool, and the refusal names it.
    task.setXactProjectsProperty({Tasks::TaskItem(project.Write(
        "Sounds.xap", "Signature = XACT2;\nVersion = 17;\nContent Version = 46;\n"))});
    const bool hasCompiler = task.HasXactCompilerEXT();
    const bool built = task.Execute();
    if (!hasCompiler)
    {
        EXPECT_FALSE(built);
        EXPECT_NE(task.ErrorsEXT().back().find("XactBld3.exe"), std::string::npos);
        EXPECT_NE(task.ErrorsEXT().back().find("validated 1 XACT project"), std::string::npos);
    }
}

TEST(XnaBuildXact, EveryPropertyRoundTrips)
{
    Tasks::BuildXact task;
    EXPECT_FALSE(task.getRebuildAllProperty());
    EXPECT_TRUE(task.getXactProjectsProperty().empty());
    EXPECT_TRUE(task.getOutputXactFilesProperty().empty());
    EXPECT_TRUE(task.getRebuiltXactFilesProperty().empty());
    EXPECT_TRUE(task.getIntermediateFilesProperty().empty());

    task.setBuildConfigurationProperty("Release");
    task.setContentProjectGUIDProperty("{GUID}");
    task.setIntermediateDirectoryProperty("obj");
    task.setLoggerRootDirectoryProperty("..");
    task.setOutputDirectoryProperty("bin");
    task.setRebuildAllProperty(true);
    task.setRootDirectoryProperty("Content");
    task.setTargetPlatformProperty("Windows");
    task.setTargetProfileProperty("Reach");
    task.setXactProjectsProperty({Tasks::TaskItem("Sounds.xap")});
    task.setXnaFrameworkVersionProperty("v4.0");

    EXPECT_EQ(task.getBuildConfigurationProperty(), "Release");
    EXPECT_EQ(task.getContentProjectGUIDProperty(), "{GUID}");
    EXPECT_EQ(task.getIntermediateDirectoryProperty(), "obj");
    EXPECT_EQ(task.getLoggerRootDirectoryProperty(), "..");
    EXPECT_EQ(task.getOutputDirectoryProperty(), "bin");
    EXPECT_TRUE(task.getRebuildAllProperty());
    EXPECT_EQ(task.getRootDirectoryProperty(), "Content");
    EXPECT_EQ(task.getTargetPlatformProperty(), "Windows");
    EXPECT_EQ(task.getTargetProfileProperty(), "Reach");
    EXPECT_EQ(task.getXactProjectsProperty().size(), 1u);
    EXPECT_EQ(task.getXnaFrameworkVersionProperty(), "v4.0");
}

TEST(XnaCleanContent, EveryPropertyRoundTrips)
{
    Tasks::CleanContent task;
    task.setBuildConfigurationProperty("Debug");
    task.setContentProjectGUIDProperty("{GUID}");
    task.setIntermediateDirectoryProperty("obj");
    task.setOutputDirectoryProperty("bin");
    task.setRootDirectoryProperty("Content");
    task.setTargetPlatformProperty("WindowsPhone");
    task.setTargetProfileProperty("Reach");
    EXPECT_EQ(task.getBuildConfigurationProperty(), "Debug");
    EXPECT_EQ(task.getContentProjectGUIDProperty(), "{GUID}");
    EXPECT_EQ(task.getIntermediateDirectoryProperty(), "obj");
    EXPECT_EQ(task.getOutputDirectoryProperty(), "bin");
    EXPECT_EQ(task.getRootDirectoryProperty(), "Content");
    EXPECT_EQ(task.getTargetPlatformProperty(), "WindowsPhone");
    EXPECT_EQ(task.getTargetProfileProperty(), "Reach");

    Tasks::GetLastOutputs outputs;
    outputs.setContentProjectGUIDProperty("{GUID}");
    outputs.setIntermediateDirectoryProperty("obj");
    EXPECT_EQ(outputs.getContentProjectGUIDProperty(), "{GUID}");
    EXPECT_EQ(outputs.getIntermediateDirectoryProperty(), "obj");
    EXPECT_TRUE(outputs.getOutputContentFilesProperty().empty());
}
