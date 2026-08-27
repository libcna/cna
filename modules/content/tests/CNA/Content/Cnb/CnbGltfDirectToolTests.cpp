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
