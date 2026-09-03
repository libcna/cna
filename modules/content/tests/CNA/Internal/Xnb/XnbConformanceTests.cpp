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
#include <iterator>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "CNA/Internal/Xnb/XnbAssetTypeWriters.hpp"
#include "CNA/Internal/Xnb/XnbAssetWriter.hpp"
#include "CNA/Internal/Xnb/XnbCanonicalData.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Quaternion.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Vector4.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"

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

TEST(XnbConformanceTest, TheIndependentParserReadsBackEveryPrimitiveAndCollectionRoot)
{
    if (!ConformanceParserAvailable())
    {
        GTEST_SKIP() << "python3 or tools/xnb/xnb_conformance.py is unavailable";
    }
    using namespace CNA::Internal::Xnb;
    using namespace Microsoft::Xna::Framework;

    ScratchDirectory scratch("roots");
    // Each root is written by CNA and then read by a parser that shares no code with it, so the
    // value coming back out is evidence about the bytes rather than about CNA's own reader
    // agreeing with CNA's own writer. The expected text is the parser's JSON rendering.
    const std::vector<std::pair<std::string, std::vector<std::uint8_t>>> roots{
        {"true", WriteXnbAsset(true)},
        {"200", WriteXnbAsset(std::uint8_t{200})},
        {"-42", WriteXnbAsset(std::int8_t{-42})},
        {"-1234", WriteXnbAsset(std::int16_t{-1234})},
        {"60000", WriteXnbAsset(std::uint16_t{60000})},
        {"-70000", WriteXnbAsset(std::int32_t{-70000})},
        {"4000000000", WriteXnbAsset(std::uint32_t{4000000000u})},
        {"-9000000000", WriteXnbAsset(std::int64_t{-9000000000LL})},
        {"18000000000", WriteXnbAsset(std::uint64_t{18000000000ull})},
        {"1.5", WriteXnbAsset(1.5f)},
        {"2.25", WriteXnbAsset(2.25)},
        {"\"hello\"", WriteXnbAsset(std::string("hello"))},
        {"\"Z\"", WriteXnbAsset(SharpRuntime::charcs{u'Z'})},
        {"\"one\"", WriteXnbAsset(std::vector<std::string>{"one", "two"})},
        {"7", WriteXnbAsset(std::vector<std::int32_t>{7, 8, 9})},
        {"\"q\"", WriteXnbAsset(std::vector<SharpRuntime::charcs>{u'q', u'r'})},
    };

    for (std::size_t index = 0; index < roots.size(); ++index)
    {
        const std::filesystem::path path =
            scratch.Path() / ("root_" + std::to_string(index) + ".xnb");
        {
            std::ofstream stream(path, std::ios::binary);
            stream.write(reinterpret_cast<const char*>(roots[index].second.data()),
                         static_cast<std::streamsize>(roots[index].second.size()));
        }
        std::string output;
        ASSERT_EQ(RunProgram("python3",
                             {kConformanceParser.string(), "--json", path.string()}, output),
                  0)
            << output;
        EXPECT_NE(output.find(roots[index].first), std::string::npos)
            << "root " << index << " rendered as " << output;
    }
}

TEST(XnbConformanceTest, TheIndependentParserReadsBackTheFrameworkValueTypesItKnows)
{
    if (!ConformanceParserAvailable())
    {
        GTEST_SKIP() << "python3 or tools/xnb/xnb_conformance.py is unavailable";
    }
    using namespace CNA::Internal::Xnb;
    using namespace Microsoft::Xna::Framework;

    ScratchDirectory scratch("values");
    const std::vector<std::pair<std::string, std::vector<std::uint8_t>>> roots{
        {"3.5", WriteXnbAsset(Vector3{1.0f, -2.0f, 3.5f})},
        {"[3,4,5,6]", WriteXnbAsset(Rectangle{3, 4, 5, 6})},
        {"7.0", WriteXnbAsset(Matrix::CreateTranslation(Vector3{7.0f, 8.0f, 9.0f}))},
        {"[[1,2,3,4],[5,6,7,8]]",
         WriteXnbAsset(std::vector<Rectangle>{Rectangle{1, 2, 3, 4}, Rectangle{5, 6, 7, 8}})},
        {"[[1.0,2.0,3.0]]", WriteXnbAsset(std::vector<Vector3>{Vector3{1.0f, 2.0f, 3.0f}})},
    };

    for (std::size_t index = 0; index < roots.size(); ++index)
    {
        const std::filesystem::path path =
            scratch.Path() / ("value_" + std::to_string(index) + ".xnb");
        {
            std::ofstream stream(path, std::ios::binary);
            stream.write(reinterpret_cast<const char*>(roots[index].second.data()),
                         static_cast<std::streamsize>(roots[index].second.size()));
        }
        std::string output;
        ASSERT_EQ(RunProgram("python3",
                             {kConformanceParser.string(), "--json", path.string()}, output),
                  0)
            << output;
        // The parser pretty-prints, so the expectation is matched against the whitespace-free
        // rendering rather than against one particular indentation.
        std::string compact;
        std::copy_if(output.begin(), output.end(), std::back_inserter(compact),
                     [](const char character)
                     { return character != ' ' && character != '\n' && character != '\r'; });
        EXPECT_NE(compact.find(roots[index].first), std::string::npos)
            << "value " << index << " rendered as " << output;
    }
}

namespace
{
    /** @brief An EffectMaterial with one of every parameter shape the writer can emit. */
    CNA::Internal::Xnb::XnbEffectMaterialData MakeEffectMaterial()
    {
        using namespace Microsoft::Xna::Framework;
        CNA::Internal::Xnb::XnbEffectMaterialData material;
        material.effectReference = "Effects/Water";
        material.parameters.values.emplace("Alpha", 0.5f);
        material.parameters.values.emplace("Enabled", true);
        material.parameters.values.emplace("Passes", std::int32_t{2});
        material.parameters.values.emplace("Diffuse", Vector3{0.25f, 0.5f, 0.75f});
        material.parameters.values.emplace(
            "NormalMap", CNA::Internal::Xnb::XnbExternalAssetReference{"Textures/WaterNormal"});
        return material;
    }
}

TEST(XnbConformanceTest, TheIndependentParserReadsBackTheAssetRootsOutsideTheCommittedCorpus)
{
    if (!ConformanceParserAvailable())
    {
        GTEST_SKIP() << "python3 or tools/xnb/xnb_conformance.py is unavailable";
    }
    using namespace CNA::Internal::Xnb;
    using namespace Microsoft::Xna::Framework::Graphics;

    // The committed interop corpus carries the six assets an XNA harness would load. These four
    // are not in it -- a volume texture and a cube map need a device to be interesting, and Song
    // and Video name external media a harness cannot ship -- but their bytes can still be put in
    // front of the independent parser, which is what this test does.
    ScratchDirectory scratch("assets");

    XnbTextureData volume;
    volume.kind = XnbTextureKind::Texture3D;
    volume.width = 2u;
    volume.height = 2u;
    volume.depth = 2u;
    volume.faceCount = 1u;
    volume.mipCount = 1u;
    volume.surfaceFormat = SurfaceFormat::Color;
    volume.levels = {std::vector<std::uint8_t>(2u * 2u * 2u * 4u, 0x40u)};

    XnbTextureData cube;
    cube.kind = XnbTextureKind::TextureCube;
    cube.width = 2u;
    cube.height = 2u;
    cube.depth = 1u;
    cube.faceCount = 6u;
    cube.mipCount = 1u;
    cube.surfaceFormat = SurfaceFormat::Color;
    for (std::uint8_t face = 0; face < 6u; ++face)
    {
        cube.levels.push_back(std::vector<std::uint8_t>(2u * 2u * 4u,
                                                        static_cast<std::uint8_t>(face + 1u)));
    }

    XnbSongData song;
    song.mediaPath = "Music/theme.wma";
    song.durationMs = 3005;

    XnbVideoData video;
    video.mediaPath = "Movies/intro.wmv";
    video.durationMs = 12000;
    video.width = 320;
    video.height = 240;
    video.framesPerSecond = 30.0f;
    video.soundtrackType = 0;

    const std::vector<std::pair<std::string, std::vector<std::uint8_t>>> roots{
        {"\"kind\": \"Texture3D\"", WriteXnbAsset(XnbTexture3DContent{volume})},
        {"\"faceCount\": 6", WriteXnbAsset(XnbTextureCubeContent{cube})},
        {"Music/theme.wma", WriteXnbAsset(song)},
        {"Movies/intro.wmv", WriteXnbAsset(video)},
        {"\"bytecodeByteCount\": 8",
         WriteXnbAsset(XnbCompiledEffectContent{
             std::vector<std::uint8_t>{0x01u, 0x02u, 0x03u, 0x04u, 0x05u, 0x06u, 0x07u, 0x08u}})},
        {"Effects/Water", WriteXnbAsset(MakeEffectMaterial())},
    };

    for (std::size_t index = 0; index < roots.size(); ++index)
    {
        const std::filesystem::path path =
            scratch.Path() / ("asset_" + std::to_string(index) + ".xnb");
        {
            std::ofstream stream(path, std::ios::binary);
            stream.write(reinterpret_cast<const char*>(roots[index].second.data()),
                         static_cast<std::streamsize>(roots[index].second.size()));
        }
        std::string output;
        ASSERT_EQ(RunProgram("python3",
                             {kConformanceParser.string(), "--json", path.string()}, output),
                  0)
            << output;
        EXPECT_NE(output.find(roots[index].first), std::string::npos)
            << "asset " << index << " rendered as " << output;
    }
}

TEST(XnbConformanceTest, TheIndependentParserDecompressesAndValidatesAnLz4File)
{
    if (!ConformanceParserAvailable())
    {
        GTEST_SKIP() << "python3 or tools/xnb/xnb_conformance.py is unavailable";
    }
    using namespace CNA::Internal::Xnb;
    using Microsoft::Xna::Framework::Graphics::SurfaceFormat;

    // plans/plan_xnapipeline.md XNAP-80. The parser has its own LZ4 block decoder, written from
    // the published grammar and sharing nothing with CNA's encoder or CNA's decoder, so a
    // compressed file this writer produces is validated by a genuinely second implementation
    // rather than only by the one that made it.
    ScratchDirectory scratch("lz4");
    XnbTextureData texture;
    texture.kind = XnbTextureKind::Texture2D;
    texture.surfaceFormat = SurfaceFormat::Color;
    texture.width = 16u;
    texture.height = 16u;
    texture.mipCount = 1u;
    texture.levels = {std::vector<std::uint8_t>(16u * 16u * 4u, 0x33u)};

    XnbFileOptions options;
    options.platform = XnbTargetPlatform::DesktopGL;
    options.compression = XnbOutputCompression::Lz4;

    const std::filesystem::path path = scratch.Path() / "flat.xnb";
    {
        const std::vector<std::uint8_t> file =
            WriteXnbAsset(XnbTexture2DContent{texture}, options, "flat");
        std::ofstream stream(path, std::ios::binary);
        stream.write(reinterpret_cast<const char*>(file.data()),
                     static_cast<std::streamsize>(file.size()));
    }

    std::string output;
    ASSERT_EQ(RunProgram("python3", {kConformanceParser.string(), "--json", path.string()},
                         output),
              0)
        << output;
    EXPECT_NE(output.find("\"compression\": \"lz4\""), std::string::npos) << output;
    EXPECT_NE(output.find("\"surfaceFormat\": \"Color\""), std::string::npos) << output;
    // "container-only" is what the parser reports for a payload it did not decompress; seeing it
    // here would mean the whole point of this test quietly did not happen.
    EXPECT_EQ(output.find("container-only"), std::string::npos) << output;
    EXPECT_NE(output.find("\"status\": \"ok\""), std::string::npos) << output;
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
