// SPDX-License-Identifier: MS-PL
//
// plans/plan_xnapipeline.md XNAP-30/XNAP-31/XNAP-43/XNAP-44.
//
// Two things are guarded here.
//
// First, the committed CNA-generated XNB corpus must not drift: the generator is re-run into a
// scratch directory and every byte compared against what is in the tree. That corpus is what an
// XNA-capable machine would be pointed at, so a silent change in the writer's output would
// quietly invalidate any interoperability result somebody recorded earlier.
//
// Second, every fixture -- CNA's own and the externally produced ones -- is validated by
// tools/xnb/xnb_conformance.py, a second implementation of the format that shares no code with
// CNA. Self-consistency between CNA's writer and CNA's reader cannot catch a shared
// misunderstanding of the specification; an independent parser can.

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#if !defined(_WIN32)
#include <fcntl.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>
extern char** environ;
#endif

namespace
{
    const std::filesystem::path kCorpus =
        "tests/assets/xnb/cna/windows/uncompressed";
    const std::filesystem::path kConformanceParser = "tools/xnb/xnb_conformance.py";

    class ScratchDirectory
    {
    public:
        explicit ScratchDirectory(const std::string& tag)
            : path_(std::filesystem::temp_directory_path() /
                    ("cna_xnb_conformance_" + tag + "_" +
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

    std::vector<std::uint8_t> ReadBytes(const std::filesystem::path& path)
    {
        std::ifstream stream(path, std::ios::binary);
        return {std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
    }

#if !defined(_WIN32)
    /** @brief Runs a program with its output captured, returning its exit status. */
    int RunProgram(const std::string& executable, const std::vector<std::string>& arguments,
            std::string& output)
    {
        const std::filesystem::path capture =
            std::filesystem::temp_directory_path() /
            ("cna_xnb_conformance_out_" + std::to_string(::getpid()) + "_" +
             std::to_string(reinterpret_cast<std::uintptr_t>(&arguments)));

        posix_spawn_file_actions_t actions;
        posix_spawn_file_actions_init(&actions);
        posix_spawn_file_actions_addopen(&actions, STDOUT_FILENO, capture.c_str(),
                                         O_WRONLY | O_CREAT | O_TRUNC, 0644);
        posix_spawn_file_actions_adddup2(&actions, STDOUT_FILENO, STDERR_FILENO);

        std::vector<char*> argv;
        argv.push_back(const_cast<char*>(executable.c_str()));
        for (const std::string& argument : arguments)
        {
            argv.push_back(const_cast<char*>(argument.c_str()));
        }
        argv.push_back(nullptr);

        pid_t pid = -1;
        const int spawnResult =
            posix_spawnp(&pid, executable.c_str(), &actions, nullptr, argv.data(), environ);
        posix_spawn_file_actions_destroy(&actions);
        if (spawnResult != 0) { return -1; }
        int status = 0;
        if (waitpid(pid, &status, 0) < 0) { return -1; }

        std::ifstream stream(capture, std::ios::binary);
        output.assign(std::istreambuf_iterator<char>(stream),
                      std::istreambuf_iterator<char>());
        std::error_code error;
        std::filesystem::remove(capture, error);
        return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
    }

    /** @brief Returns whether a usable `python3` and the parser script are both present. */
    bool ConformanceParserAvailable()
    {
        if (!std::filesystem::exists(kConformanceParser)) { return false; }
        std::string ignored;
        return RunProgram("python3", {"--version"}, ignored) == 0;
    }
#endif
}

#if !defined(_WIN32) && defined(CNA_XNB_INTEROP_FIXTURE_TOOL_PATH)

TEST(XnbInteropCorpusTest, TheCommittedCorpusIsExactlyWhatTheGeneratorProducesToday)
{
    ASSERT_TRUE(std::filesystem::is_directory(kCorpus))
        << "the committed corpus is missing; run cna_tool_xnb_interop_fixtures";

    ScratchDirectory scratch("regenerate");
    std::string log;
    ASSERT_EQ(RunProgram(CNA_XNB_INTEROP_FIXTURE_TOOL_PATH, {scratch.Path().string()}, log), 0) << log;

    std::vector<std::string> committed;
    for (const std::filesystem::directory_entry& entry :
         std::filesystem::directory_iterator(kCorpus))
    {
        if (entry.is_regular_file()) { committed.push_back(entry.path().filename().string()); }
    }
    std::sort(committed.begin(), committed.end());
    ASSERT_FALSE(committed.empty());

    std::vector<std::string> regenerated;
    for (const std::filesystem::directory_entry& entry :
         std::filesystem::directory_iterator(scratch.Path()))
    {
        if (entry.is_regular_file()) { regenerated.push_back(entry.path().filename().string()); }
    }
    std::sort(regenerated.begin(), regenerated.end());
    EXPECT_EQ(committed, regenerated);

    for (const std::string& name : committed)
    {
        EXPECT_EQ(ReadBytes(kCorpus / name), ReadBytes(scratch.Path() / name))
            << name << " differs from the committed corpus; if the writer's output changed on "
               "purpose, regenerate the corpus and re-run the XNA interoperability harness";
    }
}

TEST(XnbInteropCorpusTest, RegeneratingTwiceProducesIdenticalBytes)
{
    ScratchDirectory first("determinism_a");
    ScratchDirectory second("determinism_b");
    std::string log;
    ASSERT_EQ(RunProgram(CNA_XNB_INTEROP_FIXTURE_TOOL_PATH, {first.Path().string()}, log), 0) << log;
    ASSERT_EQ(RunProgram(CNA_XNB_INTEROP_FIXTURE_TOOL_PATH, {second.Path().string()}, log), 0) << log;

    for (const std::filesystem::directory_entry& entry :
         std::filesystem::directory_iterator(first.Path()))
    {
        const std::filesystem::path name = entry.path().filename();
        EXPECT_EQ(ReadBytes(entry.path()), ReadBytes(second.Path() / name)) << name.string();
    }
}

#endif

#if !defined(_WIN32)

TEST(XnbConformanceTest, TheIndependentParserAcceptsEveryCnaGeneratedFixture)
{
    if (!ConformanceParserAvailable())
    {
        GTEST_SKIP() << "python3 or tools/xnb/xnb_conformance.py is unavailable";
    }
    std::string output;
    EXPECT_EQ(RunProgram("python3", {kConformanceParser.string(), kCorpus.string()}, output), 0)
        << output;
    EXPECT_EQ(output.find("FAIL"), std::string::npos) << output;
}

TEST(XnbConformanceTest, EveryCnaFixtureMatchesItsOwnExpectationManifest)
{
    if (!ConformanceParserAvailable())
    {
        GTEST_SKIP() << "python3 or tools/xnb/xnb_conformance.py is unavailable";
    }
    ASSERT_TRUE(std::filesystem::is_directory(kCorpus));
    std::size_t checked = 0u;
    for (const std::filesystem::directory_entry& entry :
         std::filesystem::directory_iterator(kCorpus))
    {
        if (entry.path().extension() != ".xnb") { continue; }
        std::filesystem::path expectation = entry.path();
        expectation.replace_extension(".expected.json");
        ASSERT_TRUE(std::filesystem::exists(expectation))
            << entry.path().filename().string() << " has no expectation manifest";
        std::string output;
        EXPECT_EQ(RunProgram("python3", {kConformanceParser.string(), "--expect",
                                  expectation.string(), entry.path().string()}, output),
                  0)
            << output;
        ++checked;
    }
    EXPECT_GE(checked, 6u);
}

TEST(XnbConformanceTest, TheIndependentParserAcceptsEveryExternallyProducedFixture)
{
    if (!ConformanceParserAvailable())
    {
        GTEST_SKIP() << "python3 or tools/xnb/xnb_conformance.py is unavailable";
    }
    std::string output;
    EXPECT_EQ(RunProgram("python3", {kConformanceParser.string(), "tests/assets/xnb"}, output), 0)
        << output;
    // The genuine Microsoft XNA 4.0 fixture must be among the files it accepted, and it must be
    // reported as an XNA-4.0-era platform rather than an extended-ecosystem one.
    EXPECT_NE(output.find("ContentManifestListStrings.xnb  platform=w (xna40)"),
              std::string::npos)
        << output;
    EXPECT_EQ(output.find("FAIL"), std::string::npos) << output;
}

TEST(XnbConformanceTest, TheIndependentParserRefusesAMalformedContainer)
{
    if (!ConformanceParserAvailable())
    {
        GTEST_SKIP() << "python3 or tools/xnb/xnb_conformance.py is unavailable";
    }
    ScratchDirectory scratch("malformed");

    // A file whose declared total length disagrees with its real size: the exact defect a
    // permissive parser would sail past.
    std::vector<std::uint8_t> corrupt = ReadBytes(kCorpus / "curve_two_keys.xnb");
    ASSERT_GE(corrupt.size(), 10u);
    corrupt[6] = static_cast<std::uint8_t>(corrupt[6] + 1u);
    const std::filesystem::path path = scratch.Path() / "corrupt.xnb";
    {
        std::ofstream stream(path, std::ios::binary);
        stream.write(reinterpret_cast<const char*>(corrupt.data()),
                     static_cast<std::streamsize>(corrupt.size()));
    }

    std::string output;
    EXPECT_EQ(RunProgram("python3", {kConformanceParser.string(), path.string()}, output), 1) << output;
    EXPECT_NE(output.find("header declares"), std::string::npos) << output;
}

TEST(XnbConformanceTest, TheIndependentParserRefusesTrailingBytes)
{
    if (!ConformanceParserAvailable())
    {
        GTEST_SKIP() << "python3 or tools/xnb/xnb_conformance.py is unavailable";
    }
    ScratchDirectory scratch("trailing");

    std::vector<std::uint8_t> padded = ReadBytes(kCorpus / "list_of_strings.xnb");
    padded.push_back(0u);
    padded[6] = static_cast<std::uint8_t>(padded[6] + 1u);
    const std::filesystem::path path = scratch.Path() / "padded.xnb";
    {
        std::ofstream stream(path, std::ios::binary);
        stream.write(reinterpret_cast<const char*>(padded.data()),
                     static_cast<std::streamsize>(padded.size()));
    }

    std::string output;
    EXPECT_EQ(RunProgram("python3", {kConformanceParser.string(), path.string()}, output), 1) << output;
    EXPECT_NE(output.find("remain after the object graph"), std::string::npos) << output;
}

#endif
