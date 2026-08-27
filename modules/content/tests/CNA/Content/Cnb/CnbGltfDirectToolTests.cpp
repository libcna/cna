// SPDX-License-Identifier: MS-PL
//
// plans/plan_cnb.md CNBF-106: direct glTF -> .cnb.
//
// The property that matters is not "the tool runs". It is that the direct route and the two-step
// route produce the SAME BYTES -- because the requirement behind this task was never a second
// command, it was that CNA must not grow a second interpretation of glTF. The tool is built from
// the .cnj tool's own translation unit, so that is true by construction; this suite is what would
// notice if someone ever "helpfully" gave it a parser of its own.

#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>
#include <string>
#include <vector>

namespace
{
#if defined(CNA_GLTF_TO_CNB_TOOL_PATH) && defined(CNA_GLTF_TO_CNJ_TOOL_PATH) && \
    defined(CNA_CNJ_TO_CNB_TOOL_PATH)
    constexpr bool kToolsAvailable = true;
    const char* const kDirectTool = CNA_GLTF_TO_CNB_TOOL_PATH;
    const char* const kGltfTool = CNA_GLTF_TO_CNJ_TOOL_PATH;
    const char* const kCnjTool = CNA_CNJ_TO_CNB_TOOL_PATH;
#else
    constexpr bool kToolsAvailable = false;
    const char* const kDirectTool = "";
    const char* const kGltfTool = "";
    const char* const kCnjTool = "";
#endif

    class ScratchDir
    {
    public:
        explicit ScratchDir(const std::string& tag)
            : path_(std::filesystem::temp_directory_path() /
                    ("cna_cnb_gltf_direct_" + tag + "_" +
                     std::to_string(reinterpret_cast<std::uintptr_t>(this))))
        {
            std::filesystem::create_directories(path_);
        }
        ~ScratchDir() { std::error_code ignored; std::filesystem::remove_all(path_, ignored); }
        ScratchDir(const ScratchDir&) = delete;
        ScratchDir& operator=(const ScratchDir&) = delete;
        [[nodiscard]] const std::filesystem::path& path() const { return path_; }

    private:
        std::filesystem::path path_;
    };

    std::filesystem::path FindFixture(const std::string& name)
    {
        for (const char* prefix : {"tests/assets/gltf/", "../tests/assets/gltf/",
                                    "../../tests/assets/gltf/"})
        {
            const std::filesystem::path candidate = std::string(prefix) + name;
            if (std::filesystem::exists(candidate)) { return candidate; }
        }
        return {};
    }

    int RunTool(const std::string& command)
    {
        return std::system((command + " >/dev/null 2>&1").c_str());
    }

    std::vector<std::uint8_t> ReadFile(const std::filesystem::path& path)
    {
        std::ifstream in(path, std::ios::binary);
        return std::vector<std::uint8_t>(std::istreambuf_iterator<char>(in),
                                          std::istreambuf_iterator<char>());
    }
}

TEST(CnbGltfDirectToolTest, TheDirectRouteProducesTheSameBytesAsTheTwoStepRoute)
{
    if (!kToolsAvailable) { GTEST_SKIP() << "the content tools were not built"; }
    const std::filesystem::path fixture = FindFixture("skin-four-weighted.gltf");
    if (fixture.empty()) { GTEST_SKIP() << "fixture not found (run from the source root)"; }

    ScratchDir twoStep("two");
    ScratchDir direct("direct");

    ASSERT_EQ(RunTool(std::string(kGltfTool) + " " + fixture.string() + " " +
                  twoStep.path().string() + " asset"), 0);
    ASSERT_EQ(RunTool(std::string(kCnjTool) + " " + (twoStep.path() / "asset.cnj").string() + " " +
                  (twoStep.path() / "asset.cnb").string() + " --quiet"), 0);
    ASSERT_EQ(RunTool(std::string(kDirectTool) + " " + fixture.string() + " " +
                  direct.path().string() + " asset --quiet"), 0);

    const std::vector<std::uint8_t> expected = ReadFile(twoStep.path() / "asset.cnb");
    const std::vector<std::uint8_t> actual = ReadFile(direct.path() / "asset.cnb");
    ASSERT_FALSE(expected.empty()) << "the two-step route produced nothing";
    EXPECT_EQ(expected.size(), actual.size());
    EXPECT_EQ(expected, actual)
        << "the direct route disagreed with the two-step route. Both must run the same glTF "
           "interpretation; a divergence here means one of them grew its own.";
}

TEST(CnbGltfDirectToolTest, TheDirectRouteLeavesNoIntermediateFilesBehind)
{
    if (!kToolsAvailable) { GTEST_SKIP() << "the content tools were not built"; }
    const std::filesystem::path fixture = FindFixture("skin-four-weighted.gltf");
    if (fixture.empty()) { GTEST_SKIP() << "fixture not found (run from the source root)"; }

    ScratchDir out("clean");
    ASSERT_EQ(RunTool(std::string(kDirectTool) + " " + fixture.string() + " " + out.path().string() +
                  " asset --quiet"), 0);

    // The point of the tool is that a project never has to keep .cnj files it does not want.
    std::vector<std::string> produced;
    for (const auto& entry : std::filesystem::directory_iterator(out.path()))
    {
        produced.push_back(entry.path().filename().string());
    }
    ASSERT_EQ(produced.size(), 1u) << "expected exactly one .cnb and nothing else";
    EXPECT_EQ(produced[0], "asset.cnb");
}

TEST(CnbGltfDirectToolTest, KeepCnjMakesTheIntermediateVisibleWhenAskedFor)
{
    if (!kToolsAvailable) { GTEST_SKIP() << "the content tools were not built"; }
    const std::filesystem::path fixture = FindFixture("skin-four-weighted.gltf");
    if (fixture.empty()) { GTEST_SKIP() << "fixture not found (run from the source root)"; }

    ScratchDir out("keep");
    ScratchDir cnj("cnjout");
    ASSERT_EQ(RunTool(std::string(kDirectTool) + " " + fixture.string() + " " + out.path().string() +
                  " asset --quiet --keep-cnj " + cnj.path().string()), 0);

    EXPECT_TRUE(std::filesystem::exists(out.path() / "asset.cnb"));
    EXPECT_TRUE(std::filesystem::exists(cnj.path() / "asset.cnj"))
        << "--keep-cnj must make the staging document real output";
}

TEST(CnbGltfDirectToolTest, AMissingInputIsAnErrorRatherThanAnEmptyOutput)
{
    if (!kToolsAvailable) { GTEST_SKIP() << "the content tools were not built"; }
    ScratchDir out("missing");
    EXPECT_NE(RunTool(std::string(kDirectTool) + " does-not-exist.gltf " + out.path().string() +
                  " asset --quiet"), 0);
    EXPECT_FALSE(std::filesystem::exists(out.path() / "asset.cnb"));
}

// --------------------------------------------------------------------------------------------
// CNBF-120 -- the tool's own arguments, and the failures it used to report as success
// --------------------------------------------------------------------------------------------

TEST(CnbGltfDirectToolTest, ABaseNameThatIsNotOneFileNameComponentIsRefused)
{
    // baseName becomes part of every output file name AND of the staging directory's name, and
    // neither was checked. A separator would write outside outputDir; "." and ".." name a
    // directory rather than an asset. Refused before the conversion runs, so nothing is produced.
    if (!kToolsAvailable) { GTEST_SKIP() << "the content tools were not built"; }
    const std::filesystem::path fixture = FindFixture("skin-four-weighted.gltf");
    if (fixture.empty()) { GTEST_SKIP() << "fixture not found (run from the source root)"; }

    for (const char* bad : {"sub/asset", "../asset", ".", "..", "a/b", "/abs"})
    {
        ScratchDir out(std::string("basename"));
        EXPECT_NE(RunTool(std::string(kDirectTool) + " " + fixture.string() + " " +
                              out.path().string() + " '" + bad + "' --quiet"),
                  0)
            << "baseName '" << bad << "' was accepted";
        EXPECT_TRUE(std::filesystem::is_empty(out.path()))
            << "baseName '" << bad << "' produced output";
    }

    // An empty baseName is a positional argument that is present but says nothing.
    ScratchDir empty("basename-empty");
    EXPECT_NE(RunTool(std::string(kDirectTool) + " " + fixture.string() + " " +
                          empty.path().string() + " '' --quiet"),
              0);

    // An ordinary name still works, so this is a boundary rather than a blanket refusal.
    ScratchDir good("basename-ok");
    EXPECT_EQ(RunTool(std::string(kDirectTool) + " " + fixture.string() + " " +
                          good.path().string() + " asset-1_v2 --quiet"),
              0);
    EXPECT_TRUE(std::filesystem::exists(good.path() / "asset-1_v2.cnb"));
}

TEST(CnbGltfDirectToolTest, UnitScaleMustBeAFiniteFullyConsumedPositiveNumber)
{
    // std::stof consumed a PREFIX, so "0.01m" was 0.01 and "1.0abc" was 1.0 -- a typo scaled a
    // model silently. It also accepts "nan" and "inf" by their literal spellings, either of which
    // multiplies every position and bone translation into a value nothing can render. Zero
    // collapses the model to a point and a negative mirrors it.
    if (!kToolsAvailable) { GTEST_SKIP() << "the content tools were not built"; }
    const std::filesystem::path fixture = FindFixture("skin-four-weighted.gltf");
    if (fixture.empty()) { GTEST_SKIP() << "fixture not found (run from the source root)"; }

    for (const char* bad : {"1.0abc", "0.01m", "nan", "inf", "-inf", "0", "-1", "", "abc"})
    {
        ScratchDir out("unitscale");
        EXPECT_NE(RunTool(std::string(kDirectTool) + " " + fixture.string() + " " +
                              out.path().string() + " asset --unit-scale '" + bad + "' --quiet"),
                  0)
            << "--unit-scale '" << bad << "' was accepted";
        EXPECT_FALSE(std::filesystem::exists(out.path() / "asset.cnb"));
    }

    ScratchDir good("unitscale-ok");
    EXPECT_EQ(RunTool(std::string(kDirectTool) + " " + fixture.string() + " " +
                          good.path().string() + " asset --unit-scale 0.01 --quiet"),
              0);
    EXPECT_TRUE(std::filesystem::exists(good.path() / "asset.cnb"));
}

TEST(CnbGltfDirectToolTest, KeepCnjReportsAFailedCopyInsteadOfSucceeding)
{
    // Every error from create_directories and copy was discarded, so --keep-cnj exited 0 having
    // copied nothing at all. The .cnb files are correct at that point, which is exactly why a
    // silent success is the wrong answer: a build script would move on believing the intermediate
    // was there.
    if (!kToolsAvailable) { GTEST_SKIP() << "the content tools were not built"; }
    const std::filesystem::path fixture = FindFixture("skin-four-weighted.gltf");
    if (fixture.empty()) { GTEST_SKIP() << "fixture not found (run from the source root)"; }

    ScratchDir out("keepfail");
    // A regular FILE where the directory should be: create_directories cannot make one there, and
    // the copies that follow have nowhere to go.
    const std::filesystem::path blocked = out.path() / "blocked";
    { std::ofstream f(blocked, std::ios::binary); f << "not a directory"; }

    EXPECT_NE(RunTool(std::string(kDirectTool) + " " + fixture.string() + " " +
                          out.path().string() + " asset --quiet --keep-cnj " + blocked.string()),
              0)
        << "--keep-cnj reported success after copying nothing";
    // The .cnb is still produced -- the failure is about the intermediate, and the tool says so
    // rather than pretending either way.
    EXPECT_TRUE(std::filesystem::exists(out.path() / "asset.cnb"));
}
