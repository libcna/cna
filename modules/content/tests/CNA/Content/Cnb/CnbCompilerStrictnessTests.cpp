// SPDX-License-Identifier: MS-PL
//
// plans/plan_cnb.md CNBF-H006: the compiler's strictness contract against malformed binary sidecars.
//
// The `.cnj` sidecars carry no length fields, no element widths and no magic of their own -- the
// runtime reader derives all of that by dividing a file's byte length and, where the division does
// not come out even, silently loads a shorter mesh. A build-time compiler is exactly where that
// must be loud instead: a `.cnb` declares its counts explicitly, so the compiler either resolves an
// ambiguity from authoritative `.cnj` metadata or refuses the input.
//
// Before this suite existed the compiler's checks were written but untested, which is the same as
// not having a contract. Each test corrupts exactly one thing in an otherwise-good asset, so a
// refusal can never be confused with a fixture that was never valid -- and the first test in the
// file proves the uncorrupted fixture compiles, so the rest are meaningful.

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <functional>
#include <limits>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "CNA/Content/Cnb/CnbDocument.hpp"
#include "CNA/Content/Cnb/CnbModelCodec.hpp"
#include "CNA/Content/Cnb/CnjToCnb.hpp"
#include "Microsoft/Xna/Framework/Content/ContentLoadException.hpp"

using CNA::Content::Cnb::CnbDocument;
using CNA::Content::Cnb::CnjToCnbResult;
using CNA::Content::Cnb::CompileCnjToCnb;
using CNA::Content::Cnb::DecodeModelFromCnb;
using Microsoft::Xna::Framework::Content::ContentLoadException;

namespace
{
    class ScratchDir
    {
    public:
        explicit ScratchDir(const std::string& tag)
            : dir_(std::filesystem::temp_directory_path() /
                   ("cna_cnb_strict_" + tag + "_" +
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

    void WriteText(const std::filesystem::path& path, const std::string& text)
    {
        std::ofstream f(path, std::ios::binary);
        f << text;
    }

    std::vector<std::uint8_t> ReadBytes(const std::filesystem::path& path)
    {
        std::ifstream f(path, std::ios::binary);
        std::ostringstream ss;
        ss << f.rdbuf();
        const std::string s = ss.str();
        return std::vector<std::uint8_t>(s.begin(), s.end());
    }

    /// Little-endian appenders matching the legacy sidecars' own byte order.
    struct Bin
    {
        std::vector<std::uint8_t> bytes;
        void I32(std::int32_t v) { Raw(&v, 4); }
        void F32(float v) { Raw(&v, 4); }
        void F64(double v) { Raw(&v, 8); }
        void Identity()
        {
            for (int r = 0; r < 4; ++r)
            {
                for (int c = 0; c < 4; ++c) { F32(r == c ? 1.0f : 0.0f); }
            }
        }

    private:
        void Raw(const void* p, std::size_t n)
        {
            const auto* b = static_cast<const std::uint8_t*>(p);
            bytes.insert(bytes.end(), b, b + n);
        }
    };

    constexpr std::uint32_t kStride = 32u;
    constexpr std::uint32_t kVertexCount = 6u;
    constexpr std::uint32_t kIndexCount = 6u;

    std::vector<std::uint8_t> GoodVerts()
    {
        std::vector<std::uint8_t> out(kStride * kVertexCount);
        for (std::size_t i = 0; i < out.size(); ++i)
        {
            out[i] = static_cast<std::uint8_t>((i * 5u + 1u) & 0xFFu);
        }
        return out;
    }

    std::vector<std::uint8_t> GoodIndices()
    {
        std::vector<std::uint8_t> out(2u * kIndexCount, 0u);
        for (std::uint32_t i = 0; i < kIndexCount; ++i)
        {
            out[i * 2u] = static_cast<std::uint8_t>(i);
        }
        return out;
    }

    std::vector<std::uint8_t> GoodSkeleton(std::int32_t boneCount = 2, bool withRootPrefix = true)
    {
        Bin b;
        b.I32(boneCount);
        for (std::int32_t i = 0; i < boneCount; ++i) { b.I32(i == 0 ? -1 : 0); }
        for (std::int32_t i = 0; i < boneCount; ++i) { b.Identity(); }   // bind pose
        for (std::int32_t i = 0; i < boneCount; ++i) { b.Identity(); }   // inverse bind pose
        if (withRootPrefix)
        {
            for (std::int32_t i = 0; i < boneCount; ++i) { b.Identity(); }
        }
        return b.bytes;
    }

    std::vector<std::uint8_t> GoodClip()
    {
        Bin b;
        b.F64(1.0);      // duration
        b.I32(1);        // one track
        b.I32(0);        // boneIndex
        b.I32(1);        // one key
        b.F64(0.0);
        b.F32(0.0f); b.F32(0.0f); b.F32(0.0f);
        b.F32(0.0f); b.F32(0.0f); b.F32(0.0f); b.F32(1.0f);
        b.F32(1.0f); b.F32(1.0f); b.F32(1.0f);
        return b.bytes;
    }

    std::vector<std::uint8_t> GoodMorph(bool withTangentTrailer = false)
    {
        Bin b;
        b.I32(1);                       // one target
        b.I32(static_cast<std::int32_t>(kVertexCount));
        for (std::uint32_t v = 0; v < kVertexCount; ++v) { b.F32(0.5f); b.F32(0.0f); b.F32(0.0f); }
        b.I32(0);                       // no normal deltas
        if (withTangentTrailer)
        {
            b.I32(0x4E41544D);          // 'MTAN'
            b.I32(1);                   // version
            b.I32(1);                   // target count
            b.I32(1);                   // this target has tangents
            for (std::uint32_t v = 0; v < kVertexCount; ++v) { b.F32(0.1f); b.F32(0.0f); b.F32(0.0f); }
        }
        return b.bytes;
    }

    /// Writes a complete, valid skinned Model .cnj asset with every sidecar kind present.
    void WriteGoodAsset(const std::filesystem::path& dir)
    {
        WriteBytes(dir / "m_verts.bin", GoodVerts());
        WriteBytes(dir / "m_idx.bin", GoodIndices());
        WriteBytes(dir / "m_morph.bin", GoodMorph(/*withTangentTrailer=*/true));
        WriteBytes(dir / "m.skeleton.bin", GoodSkeleton());
        WriteBytes(dir / "m_walk.clip.bin", GoodClip());
        WriteText(dir / "m.cnj", R"({
  "cnjVersion": 2,
  "type": "Model",
  "bones": [ { "name": "Root", "parent": -1 }, { "name": "Hips", "parent": 0 } ],
  "skeleton": "m.skeleton.bin",
  "animations": [ { "name": "Walk", "clip": "m_walk.clip.bin" } ],
  "meshes": [
    { "name": "Hull", "vertices": "m_verts.bin", "indices": "m_idx.bin",
      "vertexStride": 32, "effect": "BasicEffect", "parentBone": 1,
      "morphTargets": "m_morph.bin", "morphWeights": [0.0] }
  ]
})");
    }

    /// Compiles the asset in `dir` and returns whether it was accepted, capturing the message.
    bool TryCompile(const std::filesystem::path& dir, std::string& message)
    {
        try
        {
            (void)CompileCnjToCnb((dir / "m.cnj").string());
            message.clear();
            return true;
        }
        catch (const ContentLoadException& e)
        {
            message = e.what();
            return false;
        }
    }

    /// Corrupts one sidecar and asserts the compiler refuses the asset. `mutate` receives the
    /// good bytes and returns the bad ones.
    void ExpectRefusedAfter(const char* tag, const char* sidecar,
                            const std::function<std::vector<std::uint8_t>(
                                std::vector<std::uint8_t>)>& mutate)
    {
        ScratchDir dir(tag);
        WriteGoodAsset(dir.path());
        WriteBytes(dir.path() / sidecar, mutate(ReadBytes(dir.path() / sidecar)));

        std::string message;
        const bool accepted = TryCompile(dir.path(), message);
        EXPECT_FALSE(accepted) << tag << ": the compiler accepted a corrupt '" << sidecar << "'";
        if (!accepted)
        {
            EXPECT_FALSE(message.empty()) << tag << ": refusal carried no message";
        }
    }
}

// --------------------------------------------------------------------------------------------
// The positive control. Without this, every refusal below could be a broken fixture.
// --------------------------------------------------------------------------------------------

TEST(CnbCompilerStrictnessTest, TheUncorruptedFixtureCompilesAndCarriesEverySidecar)
{
    ScratchDir dir("good");
    WriteGoodAsset(dir.path());

    const CnjToCnbResult result = CompileCnjToCnb((dir.path() / "m.cnj").string());
    EXPECT_EQ(result.assetTypeId, CNA::Content::Cnb::CnbAssetTypeId::Model);

    // The .cnj itself plus all five sidecar kinds, named rather than merely counted -- a count
    // would still pass if the fixture stopped exercising one of them.
    const std::vector<std::string> expectedAbsorbed = {
        "m.cnj", "m.skeleton.bin", "m_walk.clip.bin", "m_verts.bin", "m_idx.bin", "m_morph.bin"};
    for (const std::string& expected : expectedAbsorbed)
    {
        EXPECT_NE(std::find(result.absorbedFiles.begin(), result.absorbedFiles.end(), expected),
                  result.absorbedFiles.end())
            << "'" << expected << "' was not reported as absorbed";
    }
    EXPECT_EQ(result.absorbedFiles.size(), expectedAbsorbed.size());
    EXPECT_TRUE(result.externalReferences.empty());

    const CNA::Content::Cnb::CnbModelData model =
        DecodeModelFromCnb(CnbDocument::Parse(result.bytes, "m.cnb"));
    ASSERT_EQ(model.parts.size(), 1u);
    EXPECT_EQ(model.parts[0].vertexCount, kVertexCount);
    EXPECT_EQ(model.parts[0].indexCount, kIndexCount);
    EXPECT_EQ(model.parts[0].indexElementSize, 2u);
    ASSERT_TRUE(model.parts[0].morph.has_value());
    ASSERT_EQ(model.parts[0].morph->targets.size(), 1u);
    EXPECT_EQ(model.parts[0].morph->targets[0].positionDeltas.size(), kVertexCount * 3u);
    EXPECT_EQ(model.parts[0].morph->targets[0].tangentDeltas.size(), kVertexCount * 3u);
    ASSERT_TRUE(model.skeleton.has_value());
    EXPECT_EQ(model.skeleton->hierarchy.size(), 2u);
    EXPECT_EQ(model.skeleton->rootPrefix.size(), 2u);
    ASSERT_EQ(model.animations.size(), 1u);
    EXPECT_EQ(model.animations[0].name, "Walk");
}

// --------------------------------------------------------------------------------------------
// Geometry sidecars: lengths must divide evenly by the declared element size
// --------------------------------------------------------------------------------------------

TEST(CnbCompilerStrictnessTest, RefusesAVertexSidecarThatIsNotAWholeNumberOfVertices)
{
    for (const int extra : {1, 5, 31})
    {
        ExpectRefusedAfter("verts_ragged", "m_verts.bin",
                           [extra](std::vector<std::uint8_t> bytes) {
                               bytes.resize(bytes.size() + static_cast<std::size_t>(extra), 0u);
                               return bytes;
                           });
    }
    ExpectRefusedAfter("verts_short", "m_verts.bin", [](std::vector<std::uint8_t> bytes) {
        bytes.pop_back();
        return bytes;
    });
}

TEST(CnbCompilerStrictnessTest, RefusesAnIndexSidecarThatIsNotAWholeNumberOfIndices)
{
    ExpectRefusedAfter("idx_odd", "m_idx.bin", [](std::vector<std::uint8_t> bytes) {
        bytes.push_back(0u);
        return bytes;
    });
    ExpectRefusedAfter("idx_short", "m_idx.bin", [](std::vector<std::uint8_t> bytes) {
        bytes.pop_back();
        return bytes;
    });
}

TEST(CnbCompilerStrictnessTest, TheCompilerIsStricterThanTheRuntimeCnjReaderAboutRaggedGeometry)
{
    // The contract this whole suite exists for, stated as a comparison rather than asserted in the
    // abstract. A vertex sidecar with three stray bytes is something the runtime .cnj path accepts
    // -- it divides by the stride and silently keeps the whole vertices -- because the sidecar
    // format has nowhere to state a count. A .cnb states its counts explicitly, so the compiler
    // has to decide what the file means, and "probably these bytes" is not an answer a build tool
    // should give.
    ScratchDir dir("stricter");
    WriteGoodAsset(dir.path());
    std::vector<std::uint8_t> ragged = ReadBytes(dir.path() / "m_verts.bin");
    ragged.insert(ragged.end(), {0xAAu, 0xBBu, 0xCCu});
    WriteBytes(dir.path() / "m_verts.bin", ragged);

    std::string message;
    EXPECT_FALSE(TryCompile(dir.path(), message));
    EXPECT_NE(message.find("whole number of"), std::string::npos) << message;
    EXPECT_NE(message.find("m_verts.bin"), std::string::npos) << message;
}

// --------------------------------------------------------------------------------------------
// Skeleton sidecar
// --------------------------------------------------------------------------------------------

TEST(CnbCompilerStrictnessTest, RefusesATruncatedSkeletonSidecar)
{
    for (const std::size_t drop : {std::size_t{1}, std::size_t{4}, std::size_t{64}})
    {
        ExpectRefusedAfter("skel_trunc", "m.skeleton.bin",
                           [drop](std::vector<std::uint8_t> bytes) {
                               bytes.resize(bytes.size() - drop);
                               return bytes;
                           });
    }
}

TEST(CnbCompilerStrictnessTest, RefusesASkeletonSidecarWithTrailingBytes)
{
    // The block that made "deliberately absent" and "truncated" indistinguishable in the sidecar
    // format is exactly one matrix block long, so a few stray bytes after it cannot be mistaken
    // for one -- and must not be ignored either.
    ExpectRefusedAfter("skel_tail", "m.skeleton.bin", [](std::vector<std::uint8_t> bytes) {
        bytes.insert(bytes.end(), 8u, 0x11u);
        return bytes;
    });
}

TEST(CnbCompilerStrictnessTest, RefusesAnAbsurdOrNegativeSkeletonBoneCount)
{
    for (const std::int32_t boneCount : {-1, 1000000})
    {
        ExpectRefusedAfter("skel_count", "m.skeleton.bin",
                           [boneCount](std::vector<std::uint8_t> bytes) {
                               for (int i = 0; i < 4; ++i)
                               {
                                   bytes[static_cast<std::size_t>(i)] = static_cast<std::uint8_t>(
                                       (static_cast<std::uint32_t>(boneCount) >> (8 * i)) & 0xFFu);
                               }
                               return bytes;
                           });
    }
}

TEST(CnbCompilerStrictnessTest, ASkeletonWithoutTheOptionalRootPrefixStillCompiles)
{
    // The other side of the same coin: the block really is optional, and omitting it must not be
    // mistaken for truncation.
    ScratchDir dir("skel_noprefix");
    WriteGoodAsset(dir.path());
    WriteBytes(dir.path() / "m.skeleton.bin", GoodSkeleton(2, /*withRootPrefix=*/false));

    const CnjToCnbResult result = CompileCnjToCnb((dir.path() / "m.cnj").string());
    const CNA::Content::Cnb::CnbModelData model =
        DecodeModelFromCnb(CnbDocument::Parse(result.bytes, "m.cnb"));
    ASSERT_TRUE(model.skeleton.has_value());
    EXPECT_EQ(model.skeleton->hierarchy.size(), 2u);
    EXPECT_TRUE(model.skeleton->rootPrefix.empty());
}

// --------------------------------------------------------------------------------------------
// Clip sidecar
// --------------------------------------------------------------------------------------------

TEST(CnbCompilerStrictnessTest, RefusesATruncatedOrOverlongClipSidecar)
{
    ExpectRefusedAfter("clip_trunc", "m_walk.clip.bin", [](std::vector<std::uint8_t> bytes) {
        bytes.resize(bytes.size() - 4u);
        return bytes;
    });
    ExpectRefusedAfter("clip_tail", "m_walk.clip.bin", [](std::vector<std::uint8_t> bytes) {
        bytes.insert(bytes.end(), 4u, 0x22u);
        return bytes;
    });
}

TEST(CnbCompilerStrictnessTest, RefusesAClipSidecarWithANegativeCountOrNonFiniteTime)
{
    ExpectRefusedAfter("clip_negtracks", "m_walk.clip.bin", [](std::vector<std::uint8_t> bytes) {
        for (int i = 0; i < 4; ++i) { bytes[8u + static_cast<std::size_t>(i)] = 0xFFu; }
        return bytes;
    });
    ExpectRefusedAfter("clip_nan", "m_walk.clip.bin", [](std::vector<std::uint8_t> bytes) {
        const double nan = std::numeric_limits<double>::quiet_NaN();
        std::memcpy(bytes.data(), &nan, 8);   // the duration
        return bytes;
    });
}

// --------------------------------------------------------------------------------------------
// Morph sidecar
// --------------------------------------------------------------------------------------------

TEST(CnbCompilerStrictnessTest, RefusesATruncatedOrOverlongMorphSidecar)
{
    ExpectRefusedAfter("morph_trunc", "m_morph.bin", [](std::vector<std::uint8_t> bytes) {
        bytes.resize(bytes.size() - 12u);
        return bytes;
    });
    ExpectRefusedAfter("morph_tail", "m_morph.bin", [](std::vector<std::uint8_t> bytes) {
        bytes.insert(bytes.end(), 3u, 0x33u);
        return bytes;
    });
}

TEST(CnbCompilerStrictnessTest, RefusesAMorphSidecarWhoseVertexCountDoesNotMatchItsMesh)
{
    ExpectRefusedAfter("morph_count", "m_morph.bin", [](std::vector<std::uint8_t> bytes) {
        // Target 0's declared vertex count sits immediately after the target count.
        const std::uint32_t wrong = kVertexCount + 1u;
        for (int i = 0; i < 4; ++i)
        {
            bytes[4u + static_cast<std::size_t>(i)] =
                static_cast<std::uint8_t>((wrong >> (8 * i)) & 0xFFu);
        }
        return bytes;
    });
}

TEST(CnbCompilerStrictnessTest, RefusesAnUnknownTrailingBlockOrABadTangentTrailer)
{
    // A trailing block that is not the one documented magic is not something to skip past.
    ExpectRefusedAfter("morph_badmagic", "m_morph.bin", [](std::vector<std::uint8_t> bytes) {
        const std::vector<std::uint8_t> good = GoodMorph(/*withTangentTrailer=*/false);
        std::vector<std::uint8_t> out = good;
        out.insert(out.end(), {0x00u, 0x00u, 0x00u, 0x00u, 0x01u, 0x00u, 0x00u, 0x00u});
        return out;
    });
    // Right magic, version this build does not implement.
    ExpectRefusedAfter("morph_badversion", "m_morph.bin", [](std::vector<std::uint8_t> bytes) {
        std::vector<std::uint8_t> out = GoodMorph(/*withTangentTrailer=*/true);
        const std::size_t versionAt = GoodMorph(/*withTangentTrailer=*/false).size() + 4u;
        out[versionAt] = 99u;
        return out;
    });
    // Right magic and version, trailer disagreeing with the prefix about the target count.
    ExpectRefusedAfter("morph_badtrailercount", "m_morph.bin",
                       [](std::vector<std::uint8_t> bytes) {
                           std::vector<std::uint8_t> out = GoodMorph(/*withTangentTrailer=*/true);
                           const std::size_t countAt =
                               GoodMorph(/*withTangentTrailer=*/false).size() + 8u;
                           out[countAt] = 5u;
                           return out;
                       });
}

// --------------------------------------------------------------------------------------------
// References
// --------------------------------------------------------------------------------------------

TEST(CnbCompilerStrictnessTest, RefusesAMissingSidecar)
{
    for (const char* sidecar : {"m_verts.bin", "m_idx.bin", "m.skeleton.bin", "m_walk.clip.bin",
                                "m_morph.bin"})
    {
        ScratchDir dir("missing");
        WriteGoodAsset(dir.path());
        std::filesystem::remove(dir.path() / sidecar);

        std::string message;
        EXPECT_FALSE(TryCompile(dir.path(), message))
            << "the compiler accepted an asset missing '" << sidecar << "'";
    }
}

TEST(CnbCompilerStrictnessTest, RefusesASidecarPathThatEscapesTheContentRoot)
{
    for (const char* escape : {"../outside.bin", "/etc/passwd"})
    {
        ScratchDir dir("escape");
        WriteGoodAsset(dir.path());
        std::string cnj = R"({
  "cnjVersion": 2,
  "type": "Model",
  "meshes": [
    { "name": "Hull", "vertices": "ESCAPE", "indices": "m_idx.bin",
      "vertexStride": 32, "effect": "BasicEffect" }
  ]
})";
        const std::size_t at = cnj.find("ESCAPE");
        cnj.replace(at, 6, escape);
        WriteText(dir.path() / "m.cnj", cnj);

        std::string message;
        EXPECT_FALSE(TryCompile(dir.path(), message))
            << "the compiler accepted a sidecar path of '" << escape << "'";
    }
}

TEST(CnbCompilerStrictnessTest, RefusesAMeshDeclaringAStrideItsGeometryCannotHave)
{
    ScratchDir dir("stride");
    WriteGoodAsset(dir.path());
    WriteText(dir.path() / "m.cnj", R"({
  "cnjVersion": 2,
  "type": "Model",
  "meshes": [
    { "name": "Hull", "vertices": "m_verts.bin", "indices": "m_idx.bin",
      "vertexStride": 0, "effect": "BasicEffect" }
  ]
})");
    std::string message;
    EXPECT_FALSE(TryCompile(dir.path(), message));
}
