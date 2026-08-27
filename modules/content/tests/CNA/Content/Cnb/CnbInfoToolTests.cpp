// SPDX-License-Identifier: MS-PL
//
// plans/plan_cnb.md CNBF-H013: the `.cnb` inspector's contract, which is its stdout and its exit code
// -- neither of which a library call exercises, so the real executable is spawned.
//
// The tool matters beyond convenience: `--refs` is how a build script asks what a compiled asset
// depends on WITHOUT understanding its schema, which is the property the container-level XREF table
// exists to provide. A test that proves a schema-blind tool can answer that question is a test of
// the format, not just of the tool.

#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include <gtest/gtest.h>
#include <fcntl.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>

#include "CNA/Content/Cnb/CnbCurveCodec.hpp"
#include "CNA/Content/Cnb/CnbFormat.hpp"
#include "CNA/Content/Cnb/CnbWriter.hpp"
#include "Microsoft/Xna/Framework/Curve.hpp"
#include "Microsoft/Xna/Framework/CurveKey.hpp"

extern char** environ;

using CNA::Content::Cnb::CnbWriter;
using CNA::Content::Cnb::EncodeCurveToCnb;
using Microsoft::Xna::Framework::Curve;
using Microsoft::Xna::Framework::CurveKey;

namespace CnbAssetTypeId = CNA::Content::Cnb::CnbAssetTypeId;
namespace CnbChunkFlags = CNA::Content::Cnb::CnbChunkFlags;

namespace
{
    class ScratchDir
    {
    public:
        ScratchDir()
            : dir_(std::filesystem::temp_directory_path() /
                   ("cna_cnb_info_test_" +
                    std::to_string(reinterpret_cast<std::uintptr_t>(this))))
        {
            std::filesystem::create_directories(dir_);
        }
        ~ScratchDir()
        {
            std::error_code ec;
            std::filesystem::remove_all(dir_, ec);
        }
        ScratchDir(const ScratchDir&) = delete;
        ScratchDir& operator=(const ScratchDir&) = delete;
        [[nodiscard]] const std::filesystem::path& path() const { return dir_; }

    private:
        std::filesystem::path dir_;
    };

    void WriteBytes(const std::filesystem::path& path, const std::vector<std::uint8_t>& bytes)
    {
        std::ofstream f(path, std::ios::binary);
        f.write(reinterpret_cast<const char*>(bytes.data()),
                static_cast<std::streamsize>(bytes.size()));
    }

    /// Runs the tool with stdout captured, so the test can assert on what it actually prints
    /// rather than only on whether it succeeded.
    int RunInfo(const std::vector<std::string>& args, std::string& output)
    {
        const std::filesystem::path capture =
            std::filesystem::temp_directory_path() /
            ("cna_cnb_info_out_" + std::to_string(::getpid()) + "_" +
             std::to_string(reinterpret_cast<std::uintptr_t>(&args)));

        posix_spawn_file_actions_t actions;
        posix_spawn_file_actions_init(&actions);
        posix_spawn_file_actions_addopen(&actions, STDOUT_FILENO, capture.c_str(),
                                         O_WRONLY | O_CREAT | O_TRUNC, 0644);

        std::vector<char*> argv;
        argv.push_back(const_cast<char*>(CNA_CNB_INFO_TOOL_PATH));
        for (const std::string& arg : args) { argv.push_back(const_cast<char*>(arg.c_str())); }
        argv.push_back(nullptr);

        pid_t pid = -1;
        const int rc =
            posix_spawn(&pid, CNA_CNB_INFO_TOOL_PATH, &actions, nullptr, argv.data(), environ);
        posix_spawn_file_actions_destroy(&actions);
        if (rc != 0)
        {
            ADD_FAILURE() << "posix_spawn(" << CNA_CNB_INFO_TOOL_PATH
                          << ") failed: " << std::strerror(rc);
            return -1;
        }
        int status = 0;
        waitpid(pid, &status, 0);

        std::ifstream captured(capture, std::ios::binary);
        std::ostringstream ss;
        ss << captured.rdbuf();
        output = ss.str();
        captured.close();
        std::error_code ec;
        std::filesystem::remove(capture, ec);

        return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
    }

    std::vector<std::uint8_t> SampleCurveFile()
    {
        Curve curve;
        curve.getKeysProperty().Add(CurveKey(0.0f, 1.0f));
        curve.getKeysProperty().Add(CurveKey(1.0f, 2.0f));
        return EncodeCurveToCnb(curve, "Curves/inspected");
    }
}

TEST(CnbInfoToolTest, ReportsTheHeaderMetadataAndChunkTableOfARealAsset)
{
    ScratchDir dir;
    WriteBytes(dir.path() / "curve.cnb", SampleCurveFile());

    std::string output;
    ASSERT_EQ(RunInfo({(dir.path() / "curve.cnb").string()}, output), 0) << output;

    EXPECT_NE(output.find("container       1.0"), std::string::npos) << output;
    EXPECT_NE(output.find("Curve (0x00000007)"), std::string::npos) << output;
    EXPECT_NE(output.find("schema version  1"), std::string::npos) << output;
    EXPECT_NE(output.find("Microsoft.Xna.Framework.Curve"), std::string::npos) << output;
    EXPECT_NE(output.find("Curves/inspected"), std::string::npos) << output;
    // Every chunk the file holds, by name, with its required/optional status.
    EXPECT_NE(output.find("CMET"), std::string::npos) << output;
    EXPECT_NE(output.find("CRVH"), std::string::npos) << output;
    EXPECT_NE(output.find("CRVK"), std::string::npos) << output;
    EXPECT_NE(output.find("required"), std::string::npos) << output;
}

TEST(CnbInfoToolTest, RefsPrintsTheDependencyListWithoutUnderstandingTheSchema)
{
    // The property the XREF table exists for: a tool with no knowledge of the Model schema -- or
    // of any schema -- can still answer "what does this asset need?". Built here as a file of a
    // type the tool has certainly never heard of, to make the point unambiguous.
    ScratchDir dir;
    CnbWriter writer(CnbAssetTypeId::Model, 1u);
    writer.SetMetadata("Microsoft.Xna.Framework.Graphics.Model", "Models/robot");
    writer.SetExternalReferences({
        {0u, CnbAssetTypeId::Texture2D, "Textures/skin"},
        {0u, CnbAssetTypeId::Texture2D, "Textures/normal"},
        {0u, CnbAssetTypeId::Effect, "Effects/water"},
    });
    writer.AddChunk(CNA::Content::Cnb::MakeChunkId('z', 'z', 'z', 'z'), {1, 2, 3},
                    CnbChunkFlags::None, 4u);
    WriteBytes(dir.path() / "robot.cnb", writer.Build());

    std::string output;
    ASSERT_EQ(RunInfo({(dir.path() / "robot.cnb").string(), "--refs"}, output), 0) << output;

    // Exactly the three names, one per line, and nothing else -- so the output pipes straight into
    // a build script.
    std::vector<std::string> lines;
    std::istringstream stream(output);
    std::string line;
    while (std::getline(stream, line))
    {
        if (!line.empty()) { lines.push_back(line); }
    }
    ASSERT_EQ(lines.size(), 3u) << output;
    EXPECT_EQ(lines[0], "Textures/skin");
    EXPECT_EQ(lines[1], "Textures/normal");
    EXPECT_EQ(lines[2], "Effects/water");
}

TEST(CnbInfoToolTest, DescribesAnAssetTypeThisBuildHasNoLoaderFor)
{
    // Inspection must not depend on being able to LOAD the file. A tool that could only describe
    // types it could decode would be useless for exactly the file someone needs to investigate.
    ScratchDir dir;
    CnbWriter writer(CNA::Content::Cnb::CnbAssetTypeIdFromName("SomeGame.Unheard"), 1u);
    writer.SetMetadata("SomeGame.Unheard", "levels/one");
    writer.AddChunk(CNA::Content::Cnb::MakeChunkId('l', 'v', 'l', '0'), {9, 9}, CnbChunkFlags::None,
                    4u);
    WriteBytes(dir.path() / "unheard.cnb", writer.Build());

    std::string output;
    ASSERT_EQ(RunInfo({(dir.path() / "unheard.cnb").string()}, output), 0) << output;
    EXPECT_NE(output.find("SomeGame.Unheard"), std::string::npos) << output;
    EXPECT_NE(output.find("custom type"), std::string::npos) << output;
    EXPECT_NE(output.find("lvl0"), std::string::npos) << output;
}

TEST(CnbInfoToolTest, ActsAsAValidatorReportingMalformedFilesThroughItsExitCode)
{
    ScratchDir dir;

    // Truncated.
    WriteBytes(dir.path() / "short.cnb", {0x43u, 0x4Eu, 0x42u, 0x1Au, 0x01u});
    std::string output;
    EXPECT_NE(RunInfo({(dir.path() / "short.cnb").string()}, output), 0);

    // Structurally valid but with one corrupted payload byte.
    std::vector<std::uint8_t> corrupt = SampleCurveFile();
    corrupt[corrupt.size() - 1u] ^= 0xFFu;
    WriteBytes(dir.path() / "corrupt.cnb", corrupt);
    EXPECT_NE(RunInfo({(dir.path() / "corrupt.cnb").string()}, output), 0);

    // Not a .cnb at all.
    WriteBytes(dir.path() / "notcnb.cnb", {'h', 'e', 'l', 'l', 'o'});
    EXPECT_NE(RunInfo({(dir.path() / "notcnb.cnb").string()}, output), 0);

    // Missing file, and no arguments at all.
    EXPECT_NE(RunInfo({(dir.path() / "absent.cnb").string()}, output), 0);
    EXPECT_NE(RunInfo({}, output), 0);

    // --quiet validates and says nothing on success, which is what a pipeline gate wants.
    WriteBytes(dir.path() / "good.cnb", SampleCurveFile());
    EXPECT_EQ(RunInfo({(dir.path() / "good.cnb").string(), "--quiet"}, output), 0);
    EXPECT_TRUE(output.empty()) << output;
}
