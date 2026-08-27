// SPDX-License-Identifier: MS-PL
//
// plans/plan_cnb.md CNBF-061/CNBF-062/CNBF-064 (Phase D tests): the .cnj -> .cnb content compiler,
// exercised both as a library call and as the real cna_tool_cnj_to_cnb executable
// (CNA_CNJ_TO_CNB_TOOL_PATH, baked in by cmake/UnitTests.cmake).
//
// Spawning the real tool matters for exactly one assertion -- cross-process determinism. Two calls
// inside one process share allocator state, static initialisation order and a warm heap; two
// separate OS processes share none of that, so a byte-identical result from two independent runs
// is a much stronger claim. Same reasoning, and the same POSIX-only spawn machinery, as
// GltfToCnjToolTests.cpp.

#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>
#include <spawn.h>
#include <sstream>
#include <string>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>

#include "CNA/Content/Cnb/CnbAnimationClipCodec.hpp"
#include "CNA/Content/Cnb/CnbCurveCodec.hpp"
#include "CNA/Content/Cnb/CnbDocument.hpp"
#include "CNA/Content/Cnb/CnjToCnb.hpp"
#include "Microsoft/Xna/Framework/Content/ContentLoadException.hpp"
#include "Microsoft/Xna/Framework/Content/ContentManager.hpp"
#include "Microsoft/Xna/Framework/Curve.hpp"
#include "Microsoft/Xna/Framework/Graphics/SkinnedModelEXT.hpp"

extern char** environ;

using CNA::Content::Cnb::CnbDocument;
using CNA::Content::Cnb::CnjToCnbResult;
using CNA::Content::Cnb::CompileCnjToCnb;
using CNA::Content::Cnb::DecodeAnimationClipFromCnb;
using CNA::Content::Cnb::DecodeCurveFromCnb;
using Microsoft::Xna::Framework::Curve;
using Microsoft::Xna::Framework::CurveContinuity;
using Microsoft::Xna::Framework::CurveLoopType;
using Microsoft::Xna::Framework::Content::ContentLoadException;
using Microsoft::Xna::Framework::Content::ContentManager;
using Microsoft::Xna::Framework::Graphics::AnimationClipEXT;
using Microsoft::Xna::Framework::Graphics::ClipTargetSpaceEXT;

namespace CnbAssetTypeId = CNA::Content::Cnb::CnbAssetTypeId;

namespace
{
    class ScratchDir
    {
    public:
        ScratchDir()
            : dir_(std::filesystem::temp_directory_path() /
                   ("cna_cnb_compiler_test_" +
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

    void WriteText(const std::filesystem::path& path, const std::string& text)
    {
        std::ofstream f(path, std::ios::binary);
        f << text;
    }

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

    /// Appends f64/f32/i32 in the .clip.bin sidecar's own little-endian layout, so the compiler is
    /// fed a genuine legacy sidecar rather than something invented for the test.
    struct ClipBinWriter
    {
        std::vector<std::uint8_t> bytes;

        void I32(std::int32_t v) { Raw(&v, sizeof(v)); }
        void F32(float v) { Raw(&v, sizeof(v)); }
        void F64(double v) { Raw(&v, sizeof(v)); }

    private:
        void Raw(const void* p, std::size_t n)
        {
            const auto* b = static_cast<const std::uint8_t*>(p);
            bytes.insert(bytes.end(), b, b + n);
        }
    };

    int RunCompiler(const std::vector<std::string>& args)
    {
        std::vector<char*> argv;
        argv.push_back(const_cast<char*>(CNA_CNJ_TO_CNB_TOOL_PATH));
        for (const std::string& arg : args) { argv.push_back(const_cast<char*>(arg.c_str())); }
        argv.push_back(nullptr);

        pid_t pid = -1;
        const int rc =
            posix_spawn(&pid, CNA_CNJ_TO_CNB_TOOL_PATH, nullptr, nullptr, argv.data(), environ);
        if (rc != 0)
        {
            ADD_FAILURE() << "posix_spawn(" << CNA_CNJ_TO_CNB_TOOL_PATH
                          << ") failed: " << std::strerror(rc);
            return -1;
        }
        int status = 0;
        waitpid(pid, &status, 0);
        return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
    }

    constexpr const char* kCurveCnj = R"({
  "cnjVersion": 1,
  "type": "Curve",
  "preLoop": "Oscillate",
  "postLoop": "CycleOffset",
  "keys": [
    { "position": 0.0, "value": 1.5, "tangentIn": 0.25, "tangentOut": -0.25 },
    { "position": 1.0, "value": -3.0, "continuity": "Step" },
    { "position": 2.5, "value": 7.25, "tangentIn": -1.0, "tangentOut": 1.0 }
  ]
})";

    constexpr const char* kInlineClipCnj = R"({
  "cnjVersion": 1,
  "type": "AnimationClip",
  "duration": 2.0,
  "targetSpace": "SceneNode",
  "tracks": [
    { "boneIndex": 2, "keys": [
        { "time": 0.0, "translation": [1.0, 2.0, 3.0], "rotation": [0.0, 0.0, 0.0, 1.0], "scale": [1.0, 1.0, 1.0] },
        { "time": 1.5, "translation": [4.0, 5.0, 6.0] }
      ] },
    { "boneIndex": 5, "keys": [
        { "time": 0.5, "scale": [2.0, 2.0, 2.0] }
      ] }
  ]
})";
}

// --------------------------------------------------------------------------------------------
// CNBF-061 -- Curve
// --------------------------------------------------------------------------------------------

TEST(CnbCompilerTest, CompilesACurveCnjAndTheResultLoadsIdentically)
{
    ScratchDir dir;
    WriteText(dir.path() / "wobble.cnj", kCurveCnj);

    const CnjToCnbResult result = CompileCnjToCnb((dir.path() / "wobble.cnj").string());
    EXPECT_EQ(result.assetTypeId, CnbAssetTypeId::Curve);
    EXPECT_EQ(result.assetTypeName, "Microsoft.Xna.Framework.Curve");
    ASSERT_EQ(result.absorbedFiles.size(), 1u);
    EXPECT_EQ(result.absorbedFiles[0], "wobble.cnj");
    EXPECT_TRUE(result.externalReferences.empty());

    // The reference: the very same document loaded through the existing .cnj reader.
    ContentManager cm(nullptr, dir.path().string());
    const Curve fromCnj = cm.Load<Curve>("wobble");
    const Curve fromCnb = DecodeCurveFromCnb(CnbDocument::Parse(result.bytes, "wobble.cnb"));

    EXPECT_EQ(fromCnb.getPreLoopProperty(), fromCnj.getPreLoopProperty());
    EXPECT_EQ(fromCnb.getPostLoopProperty(), fromCnj.getPostLoopProperty());
    ASSERT_EQ(fromCnb.getKeysProperty().getCountProperty(),
              fromCnj.getKeysProperty().getCountProperty());
    for (float t = -1.0f; t <= 4.0f; t += 0.125f)
    {
        EXPECT_FLOAT_EQ(fromCnb.Evaluate(t), fromCnj.Evaluate(t)) << "t=" << t;
    }
}

TEST(CnbCompilerTest, ACompiledCurveIsSmallerThanItsSourceDocument)
{
    ScratchDir dir;
    WriteText(dir.path() / "wobble.cnj", kCurveCnj);
    const CnjToCnbResult result = CompileCnjToCnb((dir.path() / "wobble.cnj").string());

    const std::size_t sourceSize = std::string(kCurveCnj).size();
    EXPECT_LT(result.bytes.size(), sourceSize)
        << "compiled " << result.bytes.size() << " bytes vs source " << sourceSize;
}

// --------------------------------------------------------------------------------------------
// CNBF-062 -- AnimationClip, both .cnj forms
// --------------------------------------------------------------------------------------------

TEST(CnbCompilerTest, CompilesAnInlineAnimationClipCnj)
{
    ScratchDir dir;
    WriteText(dir.path() / "walk.cnj", kInlineClipCnj);

    const CnjToCnbResult result = CompileCnjToCnb((dir.path() / "walk.cnj").string());
    EXPECT_EQ(result.assetTypeId, CnbAssetTypeId::AnimationClip);
    ASSERT_EQ(result.absorbedFiles.size(), 1u);

    ContentManager cm(nullptr, dir.path().string());
    const AnimationClipEXT fromCnj = cm.Load<AnimationClipEXT>("walk");
    const AnimationClipEXT fromCnb =
        DecodeAnimationClipFromCnb(CnbDocument::Parse(result.bytes, "walk.cnb"));

    EXPECT_EQ(fromCnb.Duration.getTicksProperty(), fromCnj.Duration.getTicksProperty());
    EXPECT_EQ(fromCnb.TargetSpace, ClipTargetSpaceEXT::SceneNode);
    ASSERT_EQ(fromCnb.Tracks.size(), fromCnj.Tracks.size());
    for (std::size_t t = 0; t < fromCnj.Tracks.size(); ++t)
    {
        EXPECT_EQ(fromCnb.Tracks[t].BoneIndex, fromCnj.Tracks[t].BoneIndex);
        ASSERT_EQ(fromCnb.Tracks[t].Keys.size(), fromCnj.Tracks[t].Keys.size());
        for (std::size_t k = 0; k < fromCnj.Tracks[t].Keys.size(); ++k)
        {
            EXPECT_EQ(fromCnb.Tracks[t].Keys[k].Time.getTicksProperty(),
                      fromCnj.Tracks[t].Keys[k].Time.getTicksProperty());
            EXPECT_FLOAT_EQ(fromCnb.Tracks[t].Keys[k].Translation.X,
                            fromCnj.Tracks[t].Keys[k].Translation.X);
            EXPECT_FLOAT_EQ(fromCnb.Tracks[t].Keys[k].Scale.Z,
                            fromCnj.Tracks[t].Keys[k].Scale.Z);
            EXPECT_FLOAT_EQ(fromCnb.Tracks[t].Keys[k].Rotation.W,
                            fromCnj.Tracks[t].Keys[k].Rotation.W);
        }
    }
}

TEST(CnbCompilerTest, AbsorbsAClipBinSidecarSoTheCompiledAssetIsOneFile)
{
    // This is the compiler's real job in miniature: a two-file .cnj asset becomes a one-file
    // .cnb asset, and the sidecar is reported as absorbed so a build script knows not to ship it.
    ScratchDir dir;
    ClipBinWriter clip;
    clip.F64(1.25);          // duration
    clip.I32(1);             // one track
    clip.I32(9);             // boneIndex
    clip.I32(2);             // two keys
    clip.F64(0.0);
    clip.F32(1.0f); clip.F32(2.0f); clip.F32(3.0f);
    clip.F32(0.0f); clip.F32(0.0f); clip.F32(0.0f); clip.F32(1.0f);
    clip.F32(1.0f); clip.F32(1.0f); clip.F32(1.0f);
    clip.F64(1.25);
    clip.F32(4.0f); clip.F32(5.0f); clip.F32(6.0f);
    clip.F32(0.0f); clip.F32(0.0f); clip.F32(0.0f); clip.F32(1.0f);
    clip.F32(2.0f); clip.F32(2.0f); clip.F32(2.0f);
    WriteBytes(dir.path() / "walk.clip.bin", clip.bytes);
    WriteText(dir.path() / "walk.cnj",
              R"({"cnjVersion":1,"type":"AnimationClip","clipFile":"walk.clip.bin"})");

    const CnjToCnbResult result = CompileCnjToCnb((dir.path() / "walk.cnj").string());
    ASSERT_EQ(result.absorbedFiles.size(), 2u);
    EXPECT_EQ(result.absorbedFiles[0], "walk.cnj");
    EXPECT_EQ(result.absorbedFiles[1], "walk.clip.bin");

    // Prove the absorption is real: delete BOTH source files, then decode. Nothing the compiled
    // asset needs is left on disk.
    WriteBytes(dir.path() / "walk.cnb", result.bytes);
    std::filesystem::remove(dir.path() / "walk.cnj");
    std::filesystem::remove(dir.path() / "walk.clip.bin");

    ContentManager cm(nullptr, dir.path().string());
    const AnimationClipEXT loaded = cm.Load<AnimationClipEXT>("walk");
    EXPECT_DOUBLE_EQ(loaded.Duration.getTotalSecondsProperty(), 1.25);
    ASSERT_EQ(loaded.Tracks.size(), 1u);
    EXPECT_EQ(loaded.Tracks[0].BoneIndex, 9);
    ASSERT_EQ(loaded.Tracks[0].Keys.size(), 2u);
    EXPECT_FLOAT_EQ(loaded.Tracks[0].Keys[1].Translation.X, 4.0f);
    EXPECT_FLOAT_EQ(loaded.Tracks[0].Keys[1].Scale.Y, 2.0f);
}

// --------------------------------------------------------------------------------------------
// Refusals
// --------------------------------------------------------------------------------------------

TEST(CnbCompilerTest, RefusesAnUnsupportedCnjTypeByName)
{
    ScratchDir dir;
    WriteText(dir.path() / "font.cnj", R"({"cnjVersion":1,"type":"SpriteFont"})");
    try
    {
        (void)CompileCnjToCnb((dir.path() / "font.cnj").string());
        FAIL() << "expected a ContentLoadException";
    }
    catch (const ContentLoadException& e)
    {
        EXPECT_NE(std::string(e.what()).find("SpriteFont"), std::string::npos) << e.what();
    }
}

TEST(CnbCompilerTest, RefusesAMalformedOrMissingSourceDocument)
{
    ScratchDir dir;
    EXPECT_THROW((void)CompileCnjToCnb((dir.path() / "nope.cnj").string()), ContentLoadException);

    WriteText(dir.path() / "bad.cnj", "{ this is not json");
    EXPECT_THROW((void)CompileCnjToCnb((dir.path() / "bad.cnj").string()), ContentLoadException);

    WriteText(dir.path() / "notype.cnj", R"({"cnjVersion":1})");
    EXPECT_THROW((void)CompileCnjToCnb((dir.path() / "notype.cnj").string()), ContentLoadException);
}

TEST(CnbCompilerTest, CompilesFromTheCnjEvenWhenACompiledSiblingAlreadyExists)
{
    // Recompiling in place must read the .cnj, not the .cnb the previous run left next to it --
    // otherwise a rebuild would silently copy the stale artifact forward and edits to the source
    // would stop taking effect.
    ScratchDir dir;
    WriteText(dir.path() / "wobble.cnj", kCurveCnj);
    WriteBytes(dir.path() / "wobble.cnb",
               CompileCnjToCnb((dir.path() / "wobble.cnj").string()).bytes);

    WriteText(dir.path() / "wobble.cnj",
              R"({"cnjVersion":1,"type":"Curve","keys":[{"position":0.0,"value":42.0}]})");

    const CnjToCnbResult recompiled = CompileCnjToCnb((dir.path() / "wobble.cnj").string());
    const Curve curve = DecodeCurveFromCnb(CnbDocument::Parse(recompiled.bytes, "wobble.cnb"));
    ASSERT_EQ(curve.getKeysProperty().getCountProperty(), 1);
    EXPECT_FLOAT_EQ(curve.getKeysProperty()[0].getValueProperty(), 42.0f);
}

// --------------------------------------------------------------------------------------------
// CNBF-064 -- the real executable, and cross-process determinism
// --------------------------------------------------------------------------------------------

TEST(CnbCompilerToolTest, TheToolCompilesACurveAndTheOutputLoadsThroughContentManager)
{
    ScratchDir dir;
    WriteText(dir.path() / "wobble.cnj", kCurveCnj);

    ASSERT_EQ(RunCompiler({(dir.path() / "wobble.cnj").string(), "--quiet"}), 0);
    ASSERT_TRUE(std::filesystem::exists(dir.path() / "wobble.cnb"));

    std::filesystem::remove(dir.path() / "wobble.cnj");
    ContentManager cm(nullptr, dir.path().string());
    const Curve curve = cm.Load<Curve>("wobble");
    EXPECT_EQ(curve.getKeysProperty().getCountProperty(), 3);
    EXPECT_EQ(curve.getPreLoopProperty(), CurveLoopType::Oscillate);
    EXPECT_EQ(curve.getKeysProperty()[1].getContinuityProperty(), CurveContinuity::Step);
}

TEST(CnbCompilerToolTest, TwoSeparateProcessRunsProduceByteIdenticalOutput)
{
    ScratchDir dir;
    WriteText(dir.path() / "walk.cnj", kInlineClipCnj);

    ASSERT_EQ(RunCompiler({(dir.path() / "walk.cnj").string(),
                           (dir.path() / "first.cnb").string(), "--quiet"}), 0);
    ASSERT_EQ(RunCompiler({(dir.path() / "walk.cnj").string(),
                           (dir.path() / "second.cnb").string(), "--quiet"}), 0);

    const std::vector<std::uint8_t> first = ReadBytes(dir.path() / "first.cnb");
    const std::vector<std::uint8_t> second = ReadBytes(dir.path() / "second.cnb");
    ASSERT_FALSE(first.empty());
    EXPECT_EQ(first, second);
}

TEST(CnbCompilerToolTest, TheToolReportsFailureWithANonZeroExitCode)
{
    ScratchDir dir;
    EXPECT_NE(RunCompiler({(dir.path() / "missing.cnj").string(), "--quiet"}), 0);

    WriteText(dir.path() / "font.cnj", R"({"cnjVersion":1,"type":"SpriteFont"})");
    EXPECT_NE(RunCompiler({(dir.path() / "font.cnj").string(), "--quiet"}), 0);

    // No arguments at all is a usage error, not a silent success.
    EXPECT_NE(RunCompiler({}), 0);
}

TEST(CnbCompilerToolTest, TheToolHonoursAnExplicitOutputPathAndLogicalName)
{
    ScratchDir dir;
    WriteText(dir.path() / "wobble.cnj", kCurveCnj);
    const std::filesystem::path out = dir.path() / "compiled" / "curve.cnb";
    std::filesystem::create_directories(out.parent_path());

    ASSERT_EQ(RunCompiler({(dir.path() / "wobble.cnj").string(), out.string(),
                           "--name", "Curves/wobble", "--quiet"}), 0);
    ASSERT_TRUE(std::filesystem::exists(out));

    const CnbDocument doc = CnbDocument::ParseFile(out.string());
    EXPECT_EQ(doc.AssetTypeId(), CnbAssetTypeId::Curve);
    ASSERT_TRUE(doc.Metadata().present);
    EXPECT_EQ(doc.Metadata().contentName, "Curves/wobble");
}
