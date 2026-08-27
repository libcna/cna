// SPDX-License-Identifier: MS-PL
//
// plans/plan_cnb.md CNBF-120: cna_tool_source_to_cnb, exercised as the real executable.
//
// It was the only CNB tool with no test wiring at all, so nothing had ever run the program: its
// argument handling, its exit codes and the files it leaves behind were entirely unexercised. That
// is where this suite is aimed. The import paths underneath it are covered by CnbProducerTests.cpp
// as library calls; what a subprocess adds -- and the only thing it can add -- is the tool's own
// contract: what it accepts, what it refuses, what it writes, and what it leaves on disk when it
// fails.

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include <gtest/gtest.h>
#include <fcntl.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>

#include "CnaToolAtomicWrite.hpp"
#include "CNA/Content/Cnb/CnbDocument.hpp"
#include "CNA/Content/Cnb/CnbFormat.hpp"
#include "CNA/Content/Cnb/CnbMediaCodec.hpp"
#include "CNA/Content/Cnb/CnbSoundEffectCodec.hpp"
#include "CNA/Content/Cnb/CnbTextureCodec.hpp"
#include "CNA/DdsCubeFixtureEXT.hpp"
#include "CNA/Internal/Graphics/ImageLoader.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"

extern char** environ;

using CNA::Content::Cnb::CnbDocument;
using Microsoft::Xna::Framework::Color;

namespace CnbAssetTypeId = CNA::Content::Cnb::CnbAssetTypeId;

namespace
{
    class ScratchDir
    {
    public:
        explicit ScratchDir(const std::string& tag)
            : dir_(std::filesystem::temp_directory_path() /
                   ("cna_cnb_srctool_" + tag + "_" +
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

    std::vector<std::uint8_t> ReadBytes(const std::filesystem::path& path)
    {
        std::ifstream f(path, std::ios::binary);
        std::ostringstream ss;
        ss << f.rdbuf();
        const std::string s = ss.str();
        return std::vector<std::uint8_t>(s.begin(), s.end());
    }

    /// Runs the tool with stdout and stderr captured together, so a test can assert on the reason
    /// a refusal gave rather than only on the exit code.
    int RunTool(const std::vector<std::string>& args, std::string& output)
    {
        const std::filesystem::path capture =
            std::filesystem::temp_directory_path() /
            ("cna_cnb_srctool_out_" + std::to_string(::getpid()) + "_" +
             std::to_string(reinterpret_cast<std::uintptr_t>(&args)));

        posix_spawn_file_actions_t actions;
        posix_spawn_file_actions_init(&actions);
        posix_spawn_file_actions_addopen(&actions, STDOUT_FILENO, capture.c_str(),
                                         O_WRONLY | O_CREAT | O_TRUNC, 0644);
        posix_spawn_file_actions_adddup2(&actions, STDOUT_FILENO, STDERR_FILENO);

        std::vector<char*> argv;
        argv.push_back(const_cast<char*>(CNA_SOURCE_TO_CNB_TOOL_PATH));
        for (const std::string& arg : args) { argv.push_back(const_cast<char*>(arg.c_str())); }
        argv.push_back(nullptr);

        pid_t pid = -1;
        const int rc = posix_spawn(&pid, CNA_SOURCE_TO_CNB_TOOL_PATH, &actions, nullptr,
                                    argv.data(), environ);
        posix_spawn_file_actions_destroy(&actions);
        if (rc != 0)
        {
            ADD_FAILURE() << "posix_spawn(" << CNA_SOURCE_TO_CNB_TOOL_PATH
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

    int RunTool(const std::vector<std::string>& args)
    {
        std::string ignored;
        return RunTool(args, ignored);
    }

    /// A 4x3 PNG whose every texel differs, so a transposed or truncated decode is visible.
    std::vector<std::uint8_t> MakePng()
    {
        std::vector<std::uint8_t> rgba(4u * 3u * 4u);
        for (int y = 0; y < 3; ++y)
        {
            for (int x = 0; x < 4; ++x)
            {
                const std::size_t i = (static_cast<std::size_t>(y) * 4u + x) * 4u;
                rgba[i] = static_cast<std::uint8_t>(20 + x * 10);
                rgba[i + 1u] = static_cast<std::uint8_t>(30 + y * 10);
                rgba[i + 2u] = static_cast<std::uint8_t>(40 + x + y);
                rgba[i + 3u] = 255u;
            }
        }
        return CNA::Internal::Graphics::ImageLoader::EncodePng(rgba.data(), 4, 3, 4, 3);
    }

    std::vector<std::uint8_t> MakeWav()
    {
        std::vector<std::uint8_t> out;
        const auto u32 = [&](std::uint32_t v)
        { for (int i = 0; i < 4; ++i) out.push_back(static_cast<std::uint8_t>((v >> (8 * i)) & 0xFFu)); };
        const auto u16 = [&](std::uint16_t v)
        { for (int i = 0; i < 2; ++i) out.push_back(static_cast<std::uint8_t>((v >> (8 * i)) & 0xFFu)); };
        const auto tag = [&](const char* s)
        { for (int i = 0; i < 4; ++i) out.push_back(static_cast<std::uint8_t>(s[i])); };

        std::vector<std::uint8_t> pcm(200u * 2u);
        for (std::size_t i = 0; i < 200u; ++i)
        {
            const auto v = static_cast<std::int16_t>((i * 61) % 20000 - 10000);
            pcm[i * 2u] = static_cast<std::uint8_t>(static_cast<std::uint16_t>(v) & 0xFFu);
            pcm[i * 2u + 1u] = static_cast<std::uint8_t>(static_cast<std::uint16_t>(v) >> 8);
        }
        tag("RIFF"); u32(4u + 24u + 8u + static_cast<std::uint32_t>(pcm.size())); tag("WAVE");
        tag("fmt "); u32(16u); u16(1u); u16(1u); u32(22050u); u32(22050u * 2u); u16(2u); u16(16u);
        tag("data"); u32(static_cast<std::uint32_t>(pcm.size()));
        out.insert(out.end(), pcm.begin(), pcm.end());
        return out;
    }

    std::vector<std::uint8_t> MakeDds()
    {
        const Color faces[6] = {Color(255, 0, 0, 255),   Color(0, 255, 0, 255),
                                Color(0, 0, 255, 255),   Color(255, 255, 0, 255),
                                Color(255, 0, 255, 255), Color(0, 255, 255, 255)};
        return CNA::TestSupport::BuildSolidColorCubeDds(8, faces, 1);
    }
}

// --------------------------------------------------------------------------------------------
// The success paths, through the real executable
// --------------------------------------------------------------------------------------------

TEST(CnbSourceToolTest, CompilesAnImageAWavAndADdsIntoLoadableAssets)
{
    ScratchDir dir("ok");
    WriteBytes(dir.path() / "hero.png", MakePng());
    WriteBytes(dir.path() / "beep.wav", MakeWav());
    WriteBytes(dir.path() / "sky.dds", MakeDds());

    struct Case
    {
        const char* input;
        const char* output;
        std::uint32_t assetTypeId;
    };
    for (const Case& c : {Case{"hero.png", "hero.cnb", CnbAssetTypeId::Texture2D},
                          Case{"beep.wav", "beep.cnb", CnbAssetTypeId::SoundEffect},
                          Case{"sky.dds", "sky.cnb", CnbAssetTypeId::TextureCube}})
    {
        std::string output;
        ASSERT_EQ(RunTool({(dir.path() / c.input).string(), (dir.path() / c.output).string()},
                          output),
                  0)
            << c.input << ": " << output;
        const CnbDocument document =
            CnbDocument::ParseFile((dir.path() / c.output).string());
        EXPECT_EQ(document.AssetTypeId(), c.assetTypeId) << c.input;
        EXPECT_TRUE(document.Metadata().present) << c.input;
        // The default logical name is the input's stem, and it reaches CMET.
        EXPECT_EQ(document.Metadata().contentName,
                  std::filesystem::path(c.input).stem().string());
    }
}

TEST(CnbSourceToolTest, CompilesSongAndVideoMetadataWithTheirRequiredArguments)
{
    ScratchDir dir("media");
    WriteBytes(dir.path() / "theme.ogg", std::vector<std::uint8_t>(16u, 0u));

    std::string output;
    ASSERT_EQ(RunTool({(dir.path() / "theme.ogg").string(), (dir.path() / "theme.cnb").string(),
                       "--as", "song", "--stream", "Music/theme.ogg", "--duration-ms", "185000",
                       "--title", "Main Theme"},
                      output),
              0)
        << output;
    const CnbDocument song = CnbDocument::ParseFile((dir.path() / "theme.cnb").string());
    EXPECT_EQ(song.AssetTypeId(), CnbAssetTypeId::Song);
    ASSERT_EQ(song.ExternalReferences().size(), 1u);
    EXPECT_EQ(song.ExternalReferences()[0].logicalName, "Music/theme.ogg");

    ASSERT_EQ(RunTool({(dir.path() / "theme.ogg").string(), (dir.path() / "intro.cnb").string(),
                       "--as", "video", "--stream", "Movies/intro.mp4", "--frame-size", "1920x1080",
                       "--fps", "29.97", "--soundtrack", "2"},
                      output),
              0)
        << output;
    const CnbDocument video = CnbDocument::ParseFile((dir.path() / "intro.cnb").string());
    EXPECT_EQ(video.AssetTypeId(), CnbAssetTypeId::Video);
    EXPECT_EQ(CNA::Content::Cnb::DecodeVideoFromCnb(video).width, 1920u);
}

TEST(CnbSourceToolTest, TwoSeparateProcessRunsProduceByteIdenticalOutput)
{
    // Two calls inside one process share allocator state, static-initialisation order and a warm
    // heap; two OS processes share none of that, which makes this a much stronger determinism
    // claim than an in-process comparison. Run over every kind the tool produces.
    ScratchDir dir("determinism");
    WriteBytes(dir.path() / "hero.png", MakePng());
    WriteBytes(dir.path() / "beep.wav", MakeWav());
    WriteBytes(dir.path() / "sky.dds", MakeDds());

    const std::vector<std::vector<std::string>> invocations = {
        {(dir.path() / "hero.png").string(), "", "--color-key", "20,30,40"},
        {(dir.path() / "beep.wav").string(), ""},
        {(dir.path() / "sky.dds").string(), ""},
        {(dir.path() / "hero.png").string(), "", "--as", "song", "--stream", "Music/x.ogg"},
    };
    for (std::size_t i = 0; i < invocations.size(); ++i)
    {
        std::vector<std::string> first = invocations[i];
        std::vector<std::string> second = invocations[i];
        first[1] = (dir.path() / ("a" + std::to_string(i) + ".cnb")).string();
        second[1] = (dir.path() / ("b" + std::to_string(i) + ".cnb")).string();
        std::string output;
        ASSERT_EQ(RunTool(first, output), 0) << output;
        ASSERT_EQ(RunTool(second, output), 0) << output;
        EXPECT_EQ(ReadBytes(first[1]), ReadBytes(second[1]))
            << "invocation " << i << " is not deterministic across processes";
    }
}

TEST(CnbSourceToolTest, CreatesTheOutputDirectoryItWasGiven)
{
    ScratchDir dir("mkdir");
    WriteBytes(dir.path() / "hero.png", MakePng());
    const std::filesystem::path nested = dir.path() / "out" / "textures" / "hero.cnb";
    std::string output;
    ASSERT_EQ(RunTool({(dir.path() / "hero.png").string(), nested.string()}, output), 0) << output;
    EXPECT_TRUE(std::filesystem::exists(nested));
}

// --------------------------------------------------------------------------------------------
// Malformed arguments, and failing without leaving a file behind
// --------------------------------------------------------------------------------------------

TEST(CnbSourceToolTest, NumericOptionsAreParsedStrictlyAndFully)
{
    ScratchDir dir("numbers");
    WriteBytes(dir.path() / "media.bin", std::vector<std::uint8_t>(8u, 0u));
    const std::string in = (dir.path() / "media.bin").string();
    const std::string out = (dir.path() / "out.cnb").string();

    struct Case { const char* what; std::vector<std::string> extra; };
    const std::vector<Case> refused = {
        // Suffix junk: std::stoul("500abc") is 500, so a typo compiled with a number nobody wrote.
        {"duration suffix junk", {"--duration-ms", "500abc"}},
        {"duration is a word", {"--duration-ms", "soon"}},
        // A negative for an unsigned option: std::stoul("-1") is the largest unsigned value, which
        // the narrowing cast then made 0xFFFFFFFF.
        {"negative duration", {"--duration-ms", "-1"}},
        {"explicitly positive duration", {"--duration-ms", "+5"}},
        // Overflow, and the INT32_MAX ceiling the schema itself now enforces.
        {"duration overflowing u32", {"--duration-ms", "99999999999999999999"}},
        {"duration above INT32_MAX", {"--duration-ms", "2147483648"}},
        {"empty duration", {"--duration-ms", ""}},
        {"soundtrack out of range", {"--soundtrack", "3"}},
        {"negative soundtrack", {"--soundtrack", "-1"}},
    };
    for (const Case& c : refused)
    {
        std::vector<std::string> args = {in, out, "--as", "song", "--stream", "Music/x.ogg"};
        args.insert(args.end(), c.extra.begin(), c.extra.end());
        std::string output;
        EXPECT_EQ(RunTool(args, output), 1) << c.what << " was accepted: " << output;
        EXPECT_FALSE(std::filesystem::exists(out))
            << c.what << ": a refused invocation left a file behind";
    }

    // The video-only float options, which std::stof accepted as "nan" and "inf" outright.
    const std::vector<Case> refusedVideo = {
        {"fps NaN", {"--fps", "nan"}},
        {"fps infinity", {"--fps", "inf"}},
        {"fps zero", {"--fps", "0"}},
        {"fps negative", {"--fps", "-30"}},
        {"fps suffix junk", {"--fps", "30fps"}},
        {"frame size suffix junk", {"--frame-size", "1920x1080junk"}},
        {"frame size with no x", {"--frame-size", "1920"}},
        {"frame size zero", {"--frame-size", "0x1080"}},
    };
    for (const Case& c : refusedVideo)
    {
        std::vector<std::string> args = {in,      out,          "--as",  "video",
                                          "--stream", "Movies/x.mp4", "--fps", "30",
                                          "--frame-size", "640x480"};
        args.insert(args.end(), c.extra.begin(), c.extra.end());
        std::string output;
        EXPECT_EQ(RunTool(args, output), 1) << c.what << " was accepted: " << output;
        EXPECT_FALSE(std::filesystem::exists(out)) << c.what;
    }

    // And the values just inside every boundary are still accepted.
    std::string output;
    EXPECT_EQ(RunTool({in, out, "--as", "song", "--stream", "Music/x.ogg", "--duration-ms",
                       "2147483647"},
                      output),
              0)
        << output;
}

TEST(CnbSourceToolTest, AColorKeyMustHaveExactlyThreeComponents)
{
    ScratchDir dir("colorkey");
    WriteBytes(dir.path() / "hero.png", MakePng());
    const std::string in = (dir.path() / "hero.png").string();
    const std::string out = (dir.path() / "hero.cnb").string();

    for (const char* malformed : {"1,2", "1", "", "1,2,3,4", "1,2,3,", ",1,2", "1,,3",
                                  "1,2,300", "1,2,-3", "1,2,3abc"})
    {
        std::string output;
        EXPECT_EQ(RunTool({in, out, "--color-key", malformed}, output), 1)
            << "--color-key '" << malformed << "' was accepted: " << output;
        EXPECT_FALSE(std::filesystem::exists(out)) << malformed;
    }

    std::string output;
    EXPECT_EQ(RunTool({in, out, "--color-key", "20,30,40"}, output), 0) << output;
    EXPECT_TRUE(std::filesystem::exists(out));
}

TEST(CnbSourceToolTest, AnOptionThatDoesNotApplyToTheChosenKindIsRefused)
{
    // Silently ignoring one leaves the caller believing something about the output that is not
    // true -- and a build script that mixed up two invocations would never find out.
    ScratchDir dir("applicability");
    WriteBytes(dir.path() / "hero.png", MakePng());
    WriteBytes(dir.path() / "beep.wav", MakeWav());
    const std::string png = (dir.path() / "hero.png").string();
    const std::string wav = (dir.path() / "beep.wav").string();
    const std::string out = (dir.path() / "out.cnb").string();

    struct Case { const char* what; std::vector<std::string> args; };
    const std::vector<Case> cases = {
        {"--color-key on a WAV", {wav, out, "--color-key", "1,2,3"}},
        {"--fps on an image", {png, out, "--fps", "30"}},
        {"--stream on an image", {png, out, "--stream", "Music/x.ogg"}},
        {"--duration-ms on an image", {png, out, "--duration-ms", "100"}},
        {"--title on an image", {png, out, "--title", "Nope"}},
        {"--frame-size on an image", {png, out, "--frame-size", "8x8"}},
        {"--soundtrack on an image", {png, out, "--soundtrack", "1"}},
        {"--color-key on a song", {png, out, "--as", "song", "--stream", "Music/x.ogg",
                                    "--color-key", "1,2,3"}},
        {"--title on a video", {png, out, "--as", "video", "--stream", "Movies/x.mp4",
                                 "--frame-size", "8x8", "--fps", "30", "--title", "Nope"}},
    };
    for (const Case& c : cases)
    {
        std::string output;
        EXPECT_EQ(RunTool(c.args, output), 1) << c.what << " was accepted: " << output;
        EXPECT_NE(output.find("does not apply"), std::string::npos) << c.what << ": " << output;
        EXPECT_FALSE(std::filesystem::exists(out)) << c.what;
    }
}

TEST(CnbSourceToolTest, StructurallyWrongInvocationsFailWithoutWritingAnything)
{
    ScratchDir dir("failures");
    WriteBytes(dir.path() / "hero.png", MakePng());
    const std::string out = (dir.path() / "out.cnb").string();

    struct Case { const char* what; std::vector<std::string> args; };
    const std::vector<Case> cases = {
        {"no arguments", {}},
        {"one positional", {(dir.path() / "hero.png").string()}},
        {"three positionals", {(dir.path() / "hero.png").string(), out, "extra"}},
        {"unknown option", {(dir.path() / "hero.png").string(), out, "--nope"}},
        {"missing input", {(dir.path() / "absent.png").string(), out}},
        {"unknown extension", {(dir.path() / "hero.png").string(), out, "--as", "nonsense"}},
        {"option with no value", {(dir.path() / "hero.png").string(), out, "--name"}},
        {"song with no stream", {(dir.path() / "hero.png").string(), out, "--as", "song"}},
        {"video with no frame size",
         {(dir.path() / "hero.png").string(), out, "--as", "video", "--stream", "Movies/x.mp4",
          "--fps", "30"}},
        {"stream escaping the content root",
         {(dir.path() / "hero.png").string(), out, "--as", "song", "--stream", "../secrets.ogg"}},
        {"absolute stream reference",
         {(dir.path() / "hero.png").string(), out, "--as", "song", "--stream", "/etc/passwd"}},
    };
    for (const Case& c : cases)
    {
        std::string output;
        EXPECT_EQ(RunTool(c.args, output), 1) << c.what << " succeeded: " << output;
        EXPECT_FALSE(std::filesystem::exists(out))
            << c.what << ": a failing run left a partial file behind";
    }

    // A decode failure part-way through must not leave a truncated .cnb either: the tool encodes
    // the whole image before it opens the output, so there is nothing to half-write.
    WriteBytes(dir.path() / "broken.png", std::vector<std::uint8_t>(64u, 0x7Fu));
    std::string output;
    EXPECT_EQ(RunTool({(dir.path() / "broken.png").string(), out}, output), 1) << output;
    EXPECT_FALSE(std::filesystem::exists(out));

    // --help succeeds and writes nothing.
    EXPECT_EQ(RunTool({"--help"}, output), 0);
    EXPECT_NE(output.find("Usage:"), std::string::npos);
}


// --------------------------------------------------------------------------------------------
// CNBF-122 -- the output file is produced all-or-nothing
// --------------------------------------------------------------------------------------------

namespace
{
    /// Every entry of `dir`, sorted, so a test can assert on what a run LEFT rather than only on
    /// what it produced. A temporary the tool failed to clean up shows here and nowhere else.
    std::vector<std::string> EntryNames(const std::filesystem::path& dir)
    {
        std::vector<std::string> names;
        for (const auto& entry : std::filesystem::directory_iterator(dir))
        {
            names.push_back(entry.path().filename().string());
        }
        std::sort(names.begin(), names.end());
        return names;
    }
}

TEST(CnbSourceToolTest, AFailingRunLeavesAnExistingOutputByteIdenticalAndNoTemporary)
{
    // plans/plan_cnb.md CNBF-122. The tool opened its destination with the implicit `trunc`, so
    // the previous build's output was destroyed at the moment the file was opened rather than at
    // the moment the new one was complete. It now writes a sibling temporary and renames it, so
    // the visible result is the complete new file or the untouched old one.
    //
    // Most of the failures below happen before the write, so they held under the old writer too:
    // they are the contract, not the discriminator. The last case IS the discriminator -- see the
    // end of this test. The failure a test cannot induce at all (a full disk, a process killed
    // mid-write) is what the rename itself covers, and the direct WriteFileAtomically tests below
    // exercise the guard that cleans up after it.
    ScratchDir dir("preserve");
    WriteBytes(dir.path() / "hero.png", MakePng());
    const std::filesystem::path out = dir.path() / "out.cnb";

    // A real, previously-built output at the destination.
    ASSERT_EQ(RunTool({(dir.path() / "hero.png").string(), out.string(), "--quiet"}), 0);
    const std::vector<std::uint8_t> previous = ReadBytes(out);
    ASSERT_FALSE(previous.empty());
    ASSERT_NO_THROW((void)CnbDocument::Parse(previous, "out.cnb"));

    WriteBytes(dir.path() / "broken.png", std::vector<std::uint8_t>(64u, 0x7Fu));

    struct Case { const char* what; std::vector<std::string> args; };
    const std::vector<Case> cases = {
        {"a source file that will not decode",
         {(dir.path() / "broken.png").string(), out.string()}},
        {"a missing input", {(dir.path() / "absent.png").string(), out.string()}},
        {"an option that does not apply",
         {(dir.path() / "hero.png").string(), out.string(), "--fps", "30"}},
        {"a malformed numeric option",
         {(dir.path() / "hero.png").string(), out.string(), "--as", "song", "--stream",
          "Songs/x.ogg", "--duration-ms", "12abc"}},
    };
    for (const Case& c : cases)
    {
        std::string output;
        EXPECT_EQ(RunTool(c.args, output), 1) << c.what << " succeeded: " << output;
        EXPECT_EQ(ReadBytes(out), previous)
            << c.what << ": the failing run changed the existing output";
        EXPECT_EQ(EntryNames(dir.path()),
                  (std::vector<std::string>{"broken.png", "hero.png", "out.cnb"}))
            << c.what << ": the failing run left something behind";
    }

    // The discriminating case: a run that CANNOT produce its output, with a perfectly valid input
    // and a destination that already exists. The output directory is read-only while the existing
    // file inside it stays writable, which on POSIX is exactly the state in which an in-place
    // `trunc` open succeeds and a sibling temporary cannot be created. The old writer rewrote the
    // file here and exited 0; the new one refuses, says why, and leaves the previous build alone.
    //
    // This is also the one thing the change gives up, stated where it is asserted: writing into a
    // directory that is not itself writable no longer works. That is inherent to replacing by
    // rename, and is the trade this task accepts.
    if (::geteuid() != 0)
    {
        ScratchDir locked("preserve_ro");
        WriteBytes(locked.path() / "hero.png", MakePng());
        const std::filesystem::path target = locked.path() / "sub" / "out.cnb";
        std::filesystem::create_directories(target.parent_path());
        ASSERT_EQ(RunTool({(locked.path() / "hero.png").string(), target.string(), "--quiet"}), 0);
        const std::vector<std::uint8_t> before = ReadBytes(target);
        ASSERT_FALSE(before.empty());

        std::filesystem::permissions(target.parent_path(),
                                     std::filesystem::perms::owner_read |
                                         std::filesystem::perms::owner_exec,
                                     std::filesystem::perm_options::replace);
        std::string output;
        const int code = RunTool({(locked.path() / "hero.png").string(), target.string(),
                                  "--quiet", "--name", "different"},
                                 output);
        std::filesystem::permissions(target.parent_path(), std::filesystem::perms::owner_all,
                                     std::filesystem::perm_options::replace);

        EXPECT_EQ(code, 1) << "a run that cannot write its output reported success: " << output;
        EXPECT_EQ(ReadBytes(target), before)
            << "the run rewrote an output it should not have been able to replace";
        EXPECT_EQ(EntryNames(target.parent_path()), (std::vector<std::string>{"out.cnb"}))
            << "the failed run left a temporary behind";
    }
}

TEST(CnbSourceToolTest, ASuccessfulRunReplacesAnExistingOutputWholly)
{
    // The other side of the rename: replacing must leave none of the previous file, which for a
    // longer predecessor is exactly what an in-place write would not guarantee.
    ScratchDir dir("replace");
    WriteBytes(dir.path() / "beep.wav", MakeWav());
    const std::filesystem::path out = dir.path() / "sound.cnb";

    // A sentinel longer than the .cnb that will replace it, so a partial overwrite would leave a
    // visible tail.
    WriteBytes(out, std::vector<std::uint8_t>(200000u, 0xEEu));

    ASSERT_EQ(RunTool({(dir.path() / "beep.wav").string(), out.string(), "--quiet"}), 0);
    const std::vector<std::uint8_t> produced = ReadBytes(out);
    EXPECT_LT(produced.size(), 200000u) << "the sentinel's tail survived the replacement";
    const CnbDocument document = CnbDocument::Parse(produced, "sound.cnb");
    EXPECT_EQ(document.AssetTypeId(), CnbAssetTypeId::SoundEffect);
    EXPECT_EQ(EntryNames(dir.path()), (std::vector<std::string>{"beep.wav", "sound.cnb"}))
        << "a successful run left a temporary behind";

    // And running it again over its own output is byte-identical, so replacement is idempotent
    // rather than merely successful.
    ASSERT_EQ(RunTool({(dir.path() / "beep.wav").string(), out.string(), "--quiet"}), 0);
    EXPECT_EQ(ReadBytes(out), produced);
}

TEST(CnbSourceToolTest, WriteFileAtomicallyPreservesTheDestinationWhenItCannotFinish)
{
    // The failure arm the subprocess tests cannot reach, exercised directly on the helper both
    // tools now use. A read-only directory is the closest reproducible stand-in for "the write
    // cannot complete": the temporary cannot be created, so nothing is written and nothing is
    // removed -- and the destination, which the old in-place write would have truncated the
    // instant it opened it, is untouched.
    if (::geteuid() == 0)
    {
        GTEST_SKIP() << "running as root, which ignores the directory permissions this test needs";
    }

    ScratchDir dir("atomic");
    const std::filesystem::path locked = dir.path() / "locked";
    std::filesystem::create_directories(locked);
    const std::filesystem::path destination = locked / "asset.cnb";
    const std::vector<std::uint8_t> previous(4096u, 0xA5u);
    WriteBytes(destination, previous);

    std::filesystem::permissions(locked, std::filesystem::perms::owner_read |
                                             std::filesystem::perms::owner_exec,
                                 std::filesystem::perm_options::replace);

    EXPECT_THROW(CNA::Tools::WriteFileAtomically(destination, std::vector<std::uint8_t>(16u, 1u)),
                 std::runtime_error);

    std::filesystem::permissions(locked, std::filesystem::perms::owner_all,
                                 std::filesystem::perm_options::replace);
    EXPECT_EQ(ReadBytes(destination), previous)
        << "a failed write changed the destination it could not replace";
    EXPECT_EQ(EntryNames(locked), (std::vector<std::string>{"asset.cnb"}))
        << "a failed write left a temporary behind";

    // A destination whose directory does not exist fails the same way, and creates nothing.
    const std::filesystem::path absent = dir.path() / "nope" / "asset.cnb";
    EXPECT_THROW(CNA::Tools::WriteFileAtomically(absent, std::vector<std::uint8_t>(16u, 1u)),
                 std::runtime_error);
    EXPECT_FALSE(std::filesystem::exists(dir.path() / "nope"));
}

TEST(CnbSourceToolTest, WriteFileAtomicallyStepsOverATakenTemporaryNameWithoutClobberingIt)
{
    // plans/plan_cnb.md CNBF-123. The helper walks 64 candidate temporary names and takes the
    // first free one, and the create is EXCLUSIVE so that "is this free?" and "claim it" are one
    // operation. Two things follow that were asserted nowhere: a file already sitting on the first
    // candidate name must survive untouched, and the write must still succeed by moving on.
    //
    // This also pins the naming scheme itself, which is what the debris check in every failing-run
    // test greps for -- if the prefix ever changed, those tests would stop looking at anything.
    ScratchDir dir("tempcollision");
    const std::filesystem::path destination = dir.path() / "asset.cnb";
    const std::string prefix = "asset.cnb.cnatmp-" + CNA::Tools::Detail::ProcessTag() + "-";

    // Squat on the first three candidates with content that must not be disturbed.
    for (int attempt = 0; attempt < 3; ++attempt)
    {
        WriteBytes(dir.path() / (prefix + std::to_string(attempt)),
                   std::vector<std::uint8_t>(8u, static_cast<std::uint8_t>(0xC0 + attempt)));
    }

    const std::vector<std::uint8_t> payload{1u, 2u, 3u, 4u};
    ASSERT_NO_THROW(CNA::Tools::WriteFileAtomically(destination, payload));
    EXPECT_EQ(ReadBytes(destination), payload);

    for (int attempt = 0; attempt < 3; ++attempt)
    {
        EXPECT_EQ(ReadBytes(dir.path() / (prefix + std::to_string(attempt))),
                  std::vector<std::uint8_t>(8u, static_cast<std::uint8_t>(0xC0 + attempt)))
            << "candidate " << attempt << " was overwritten instead of stepped over";
    }
    // The three squatters, the destination, and the one candidate the helper actually used -- which
    // it must have consumed by renaming, so it is not here.
    EXPECT_EQ(EntryNames(dir.path()),
              (std::vector<std::string>{"asset.cnb", prefix + "0", prefix + "1", prefix + "2"}))
        << "the successful write left its own temporary behind";
}

TEST(CnbSourceToolTest, WriteFileAtomicallyRefusesWhenEveryCandidateNameIsTaken)
{
    // The other end of that search: 64 candidates, and then a refusal rather than a silent
    // fallback to a non-exclusive open. Nothing may be created and nothing may be destroyed.
    ScratchDir dir("tempexhausted");
    const std::filesystem::path destination = dir.path() / "asset.cnb";
    const std::vector<std::uint8_t> previous(64u, 0xA5u);
    WriteBytes(destination, previous);

    const std::string prefix = "asset.cnb.cnatmp-" + CNA::Tools::Detail::ProcessTag() + "-";
    for (int attempt = 0; attempt < 64; ++attempt)
    {
        WriteBytes(dir.path() / (prefix + std::to_string(attempt)), {0u});
    }

    EXPECT_THROW(CNA::Tools::WriteFileAtomically(destination, std::vector<std::uint8_t>(4u, 7u)),
                 std::runtime_error);
    EXPECT_EQ(ReadBytes(destination), previous)
        << "a write that could not start still changed the destination";
    EXPECT_EQ(EntryNames(dir.path()).size(), 65u) << "the refusal created or removed something";
}

TEST(CnbSourceToolTest, WriteFileAtomicallyReplacesWhatIsThereAndWritesAnEmptyFile)
{
    ScratchDir dir("atomic_ok");
    const std::filesystem::path destination = dir.path() / "asset.bin";
    WriteBytes(destination, std::vector<std::uint8_t>(1024u, 0x11u));

    const std::vector<std::uint8_t> shorter{1u, 2u, 3u};
    ASSERT_NO_THROW(CNA::Tools::WriteFileAtomically(destination, shorter));
    EXPECT_EQ(ReadBytes(destination), shorter);
    EXPECT_EQ(EntryNames(dir.path()), (std::vector<std::string>{"asset.bin"}));

    // An empty payload is a legitimate file, not a no-op, and must not trip the write path.
    ASSERT_NO_THROW(CNA::Tools::WriteFileAtomically(destination, {}));
    EXPECT_TRUE(std::filesystem::exists(destination));
    EXPECT_EQ(std::filesystem::file_size(destination), 0u);
    EXPECT_EQ(EntryNames(dir.path()), (std::vector<std::string>{"asset.bin"}));
}
