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
#include <memory>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "CNA/Content/Cnb/CnbDocument.hpp"
#include "CNA/Content/Cnb/CnbModelCodec.hpp"
#include "CNA/Content/Cnb/CnjToCnb.hpp"
#include "CNA/Internal/CnjCanonicalRead.hpp"
#include "CNA/Internal/Graphics/ImageLoader.hpp"
#include "Microsoft/Xna/Framework/Content/ContentLoadException.hpp"
#include "Microsoft/Xna/Framework/Content/ContentManager.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture3D.hpp"

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

// --------------------------------------------------------------------------------------------
// CNBF-118 -- one reading of a .cnj document, and strict numbers on the way through it
// --------------------------------------------------------------------------------------------

namespace
{
    /// A real 2x2 PNG, encoded through CNA's own image path so these tests carry no second PNG
    /// implementation.
    std::vector<std::uint8_t> TinyPng()
    {
        const std::vector<std::uint8_t> rgba = {
            10u, 20u, 30u, 255u,   40u, 50u, 60u, 255u,
            70u, 80u, 90u, 255u,   11u, 22u, 33u, 255u};
        return CNA::Internal::Graphics::ImageLoader::EncodePng(rgba.data(), 2, 2, 2, 2);
    }

    /// Compiles `document` written into a fresh scratch root beside a 2x2 atlas, and returns what
    /// the compiler said. Every SpriteFont/Texture2D case below is one edit away from a document
    /// this also compiles.
    void ExpectCompileRefused(const std::string& tag, const std::string& document,
                              const char* fragment)
    {
        ScratchDir dir(tag);
        WriteBytes(dir.path() / "atlas.png", TinyPng());
        WriteText(dir.path() / "a.cnj", document);
        try
        {
            (void)CompileCnjToCnb((dir.path() / "a.cnj").string());
            ADD_FAILURE() << tag << ": expected a refusal mentioning '" << fragment << "'";
        }
        catch (const ContentLoadException& e)
        {
            EXPECT_NE(std::string(e.what()).find(fragment), std::string::npos)
                << tag << ": " << e.what();
        }
    }

    /// The eight `.cnj` types the compiler supports, each as a minimal document body (without its
    /// envelope), so a version test can build one of every type from one table.
    struct SupportedType
    {
        const char* type;
        const char* body;
    };

    const std::vector<SupportedType>& SupportedTypes()
    {
        static const std::vector<SupportedType> types = {
            {"Curve", R"("preLoop":"Constant","postLoop":"Constant","keys":[])"},
            {"AnimationClip", R"("duration":1.0,"tracks":[])"},
            {"Texture2D", R"("sourceFile":"atlas.png")"},
            {"TextureCube", R"("sourceFile":"cube.dds")"},
            {"Texture3D", R"("width":1,"height":1,"depth":1,"data":"vol.bin")"},
            {"SpriteFont",
             R"("texture":"atlas.png","lineSpacing":8,"spacing":0.0,)"
             R"("glyphs":[{"char":65,"source":[0,0,1,1],"crop":[0,0,1,1],"kerning":[0,1,0]}])"},
            {"SoundEffect", R"("sourceFile":"beep.wav")"},
            {"Model", R"("meshes":[])"},
        };
        return types;
    }
}

TEST(CnbCompilerStrictnessTest, EveryNonModelTypeRefusesCnjVersionTwoLikeItsRuntimeReader)
{
    // The defect: the compiler applied a flat ceiling of 2 to every type, while only Model's
    // runtime reader accepts version 2. A "cnjVersion": 2 document of any other type therefore
    // COMPILED and then failed to load -- a build that succeeds and produces content the engine
    // refuses is the worst shape this class of bug can take.
    for (const SupportedType& supported : SupportedTypes())
    {
        if (std::string(supported.type) == "Model") { continue; }
        ScratchDir dir(std::string("v2_") + supported.type);
        WriteText(dir.path() / "a.cnj",
                  std::string(R"({"cnjVersion":2,"type":")") + supported.type + R"(",)" +
                      supported.body + "}");
        try
        {
            (void)CompileCnjToCnb((dir.path() / "a.cnj").string());
            ADD_FAILURE() << supported.type << ": cnjVersion 2 must be refused";
        }
        catch (const ContentLoadException& e)
        {
            EXPECT_NE(std::string(e.what()).find("cnjVersion"), std::string::npos)
                << supported.type << ": refused, but not for its version -- " << e.what();
        }
    }
}

TEST(CnbCompilerStrictnessTest, ModelStillAcceptsCnjVersionTwo)
{
    // The positive control for the test above. Model's runtime reader accepts version 2 (the
    // "bones" hierarchy and per-mesh "parentBone"), so the compiler must too -- otherwise the
    // per-type ceiling would be indistinguishable from a flat ceiling of 1.
    ScratchDir dir("model_v2");
    WriteText(dir.path() / "m.cnj",
              R"({"cnjVersion":2,"type":"Model","meshes":[],"bones":[]})");
    EXPECT_NO_THROW((void)CompileCnjToCnb((dir.path() / "m.cnj").string()));

    // And version 1 is still accepted, so the ceiling is a ceiling rather than an equality.
    WriteText(dir.path() / "m1.cnj", R"({"cnjVersion":1,"type":"Model","meshes":[]})");
    EXPECT_NO_THROW((void)CompileCnjToCnb((dir.path() / "m1.cnj").string()));

    // Version 3 is refused for every type including Model.
    WriteText(dir.path() / "m3.cnj", R"({"cnjVersion":3,"type":"Model","meshes":[]})");
    EXPECT_THROW((void)CompileCnjToCnb((dir.path() / "m3.cnj").string()), ContentLoadException);
}

TEST(CnbCompilerStrictnessTest, AColorKeyIsReadStrictlyRatherThanClampedOrDropped)
{
    const std::string prefix = R"({"cnjVersion":1,"type":"Texture2D","sourceFile":"atlas.png",)";

    // Clamping was the old behaviour: 300 became 255 and -5 became 0, so the compiler keyed out a
    // colour the author never named. A fractional component was truncated, and a malformed array
    // was DROPPED entirely -- the document asked for a colour key and got none.
    ExpectCompileRefused("ck_high", prefix + R"("colorKey":[300,0,0]})", "outside the accepted range");
    ExpectCompileRefused("ck_low", prefix + R"("colorKey":[-5,0,0]})", "outside the accepted range");
    ExpectCompileRefused("ck_frac", prefix + R"("colorKey":[1.5,0,0]})", "not a whole number");
    ExpectCompileRefused("ck_short", prefix + R"("colorKey":[1,2]})", "exactly 3 are required");
    ExpectCompileRefused("ck_long", prefix + R"("colorKey":[1,2,3,4]})", "exactly 3 are required");
    ExpectCompileRefused("ck_string", prefix + R"("colorKey":["1",2,3]})", "is not a number");
    ExpectCompileRefused("ck_notarray", prefix + R"("colorKey":255})", "is not an array");
    // 1e400 is valid JSON number GRAMMAR, and std::stod answered it by throwing std::out_of_range
    // -- an exception type nothing in the content pipeline catches, so it escaped past every
    // `catch (const ContentLoadException&)` a game has. The parser now refuses the token itself.
    ExpectCompileRefused("ck_inf", prefix + R"("colorKey":[1e400,0,0]})",
                         "outside the range a double can represent");

    // Absent is not an error, and a well-formed key still applies: the strictness is about
    // malformed documents, not about colour keys.
    ScratchDir dir("ck_ok");
    WriteBytes(dir.path() / "atlas.png", TinyPng());
    WriteText(dir.path() / "keyed.cnj", prefix + R"("colorKey":[10,20,30]})");
    const CnjToCnbResult keyed = CompileCnjToCnb((dir.path() / "keyed.cnj").string());
    WriteText(dir.path() / "plain.cnj", R"({"cnjVersion":1,"type":"Texture2D","sourceFile":"atlas.png"})");
    const CnjToCnbResult plain = CompileCnjToCnb((dir.path() / "plain.cnj").string());
    EXPECT_NE(keyed.bytes, plain.bytes) << "the colour key must actually change a pixel";
}

TEST(CnbCompilerStrictnessTest, ATexture3DDocumentsNumbersAreValidatedBeforeAnyCast)
{
    const std::string prefix = R"({"cnjVersion":1,"type":"Texture3D","data":"vol.bin",)";

    ExpectCompileRefused("t3_frac", prefix + R"("width":2.5,"height":1,"depth":1})",
                         "not a whole number");
    ExpectCompileRefused("t3_zero", prefix + R"("width":0,"height":1,"depth":1})",
                         "outside the accepted range");
    ExpectCompileRefused("t3_neg", prefix + R"("width":-4,"height":1,"depth":1})",
                         "outside the accepted range");
    ExpectCompileRefused("t3_string", prefix + R"("width":"4","height":1,"depth":1})",
                         "is not a number");
    ExpectCompileRefused("t3_inf", prefix + R"("width":1e400,"height":1,"depth":1})",
                         "outside the range a double can represent");
    ExpectCompileRefused("t3_missing", R"({"cnjVersion":1,"type":"Texture3D","data":"vol.bin","height":1,"depth":1})",
                         "is missing");

    // The overflow case the old code could not survive: three dimensions each within uint32 whose
    // product times four is not. Refused by the per-dimension ceiling before any multiplication,
    // rather than by a wrapped byte count that would then have "matched" a small sidecar.
    ExpectCompileRefused("t3_overflow",
                         prefix + R"("width":4294967295,"height":4294967295,"depth":4294967295})",
                         "outside the accepted range");
    ExpectCompileRefused("t3_overflow2",
                         prefix + R"("width":100000,"height":100000,"depth":100000})",
                         "outside the accepted range");

    // A well-formed volume still compiles, and its byte count is the one the reader computed.
    ScratchDir dir("t3_ok");
    WriteBytes(dir.path() / "vol.bin", std::vector<std::uint8_t>(2u * 3u * 4u * 4u, 7u));
    WriteText(dir.path() / "v.cnj", prefix + R"("width":2,"height":3,"depth":4})");
    EXPECT_NO_THROW((void)CompileCnjToCnb((dir.path() / "v.cnj").string()));
}

TEST(CnbCompilerStrictnessTest, ASpriteFontDocumentsGlyphNumbersAreValidated)
{
    const std::string prefix =
        R"({"cnjVersion":1,"type":"SpriteFont","texture":"atlas.png","lineSpacing":8,"spacing":0.0,"glyphs":[)";
    const std::string suffix = R"(]})";

    // A wrong element TYPE used to read as 0 through arrayValue[i].numberValue, so a rectangle
    // whose width was a string silently became a zero-width glyph.
    ExpectCompileRefused(
        "sf_srcstring",
        prefix + R"({"char":65,"source":[0,0,"16",24],"crop":[0,0,16,24],"kerning":[0,1,0]})" + suffix,
        "is not a number");
    ExpectCompileRefused(
        "sf_srcshort",
        prefix + R"({"char":65,"source":[0,0,16],"crop":[0,0,16,24],"kerning":[0,1,0]})" + suffix,
        "exactly 4 are required");
    ExpectCompileRefused(
        "sf_srcfrac",
        prefix + R"({"char":65,"source":[0,0,16.5,24],"crop":[0,0,16,24],"kerning":[0,1,0]})" + suffix,
        "not a whole number");
    ExpectCompileRefused(
        "sf_kerninf",
        prefix + R"({"char":65,"source":[0,0,16,24],"crop":[0,0,16,24],"kerning":[0,1e400,0]})" + suffix,
        "outside the range a double can represent");
    ExpectCompileRefused(
        "sf_kernstring",
        prefix + R"({"char":65,"source":[0,0,16,24],"crop":[0,0,16,24],"kerning":[0,"1",0]})" + suffix,
        "is not a number");

    // A character value that is not a Unicode scalar in the plane a charcs can hold. Both used to
    // be cast straight through, producing a glyph nothing could ever match.
    ExpectCompileRefused(
        "sf_surrogate",
        prefix + R"({"char":55296,"source":[0,0,16,24],"crop":[0,0,16,24],"kerning":[0,1,0]})" + suffix,
        "surrogate half");
    ExpectCompileRefused(
        "sf_astral",
        prefix + R"({"char":128512,"source":[0,0,16,24],"crop":[0,0,16,24],"kerning":[0,1,0]})" + suffix,
        "outside the accepted range");
    ExpectCompileRefused(
        "sf_negchar",
        prefix + R"({"char":-1,"source":[0,0,16,24],"crop":[0,0,16,24],"kerning":[0,1,0]})" + suffix,
        "outside the accepted range");

    // A non-finite spacing, which reaches a float cast rather than an integer one.
    ExpectCompileRefused(
        "sf_spacinginf",
        R"({"cnjVersion":1,"type":"SpriteFont","texture":"atlas.png","spacing":1e400,"glyphs":[)"
        R"({"char":65,"source":[0,0,16,24],"crop":[0,0,16,24],"kerning":[0,1,0]}]})",
        "outside the range a double can represent");
    ExpectCompileRefused(
        "sf_linespacingfrac",
        R"({"cnjVersion":1,"type":"SpriteFont","texture":"atlas.png","lineSpacing":8.25,"glyphs":[)"
        R"({"char":65,"source":[0,0,16,24],"crop":[0,0,16,24],"kerning":[0,1,0]}]})",
        "not a whole number");
}

TEST(CnbCompilerStrictnessTest, TheCompilerAndTheRuntimeAgreeOnEveryMalformedDocument)
{
    // The property the shared reader exists to guarantee, asserted directly rather than left to
    // follow from the code sharing: for each document below, the compiler and ContentManager's own
    // .cnj route must BOTH refuse it. A document one accepts and the other refuses is a build that
    // succeeds and a game that will not start.
    struct Case { const char* tag; const char* document; };
    const Case cases[] = {
        {"frac_width",
         R"({"cnjVersion":1,"type":"Texture3D","width":2.5,"height":1,"depth":1,"data":"vol.bin"})"},
        {"string_width",
         R"({"cnjVersion":1,"type":"Texture3D","width":"2","height":1,"depth":1,"data":"vol.bin"})"},
        {"zero_depth",
         R"({"cnjVersion":1,"type":"Texture3D","width":2,"height":1,"depth":0,"data":"vol.bin"})"},
        {"inf_height",
         R"({"cnjVersion":1,"type":"Texture3D","width":2,"height":1e400,"depth":1,"data":"vol.bin"})"},
        {"version_two",
         R"({"cnjVersion":2,"type":"Texture3D","width":1,"height":1,"depth":1,"data":"vol.bin"})"},
    };

    for (const Case& c : cases)
    {
        ScratchDir dir(std::string("agree_") + c.tag);
        WriteBytes(dir.path() / "vol.bin", std::vector<std::uint8_t>(4u, 0u));
        WriteText(dir.path() / "v.cnj", c.document);

        bool compilerRefused = false;
        try { (void)CompileCnjToCnb((dir.path() / "v.cnj").string()); }
        catch (const ContentLoadException&) { compilerRefused = true; }

        bool runtimeRefused = false;
        try
        {
            Microsoft::Xna::Framework::Content::ContentManager cm(nullptr, dir.path().string());
            (void)cm.Load<std::shared_ptr<Microsoft::Xna::Framework::Graphics::Texture3D>>("v.cnj");
        }
        catch (const ContentLoadException&) { runtimeRefused = true; }
        catch (const std::exception&) { runtimeRefused = true; }

        EXPECT_TRUE(compilerRefused) << c.tag << ": the compiler accepted it";
        EXPECT_TRUE(runtimeRefused) << c.tag << ": the runtime accepted it";
    }
}

TEST(CnbCompilerStrictnessTest, AbsorbedFilesCarryTheAuthoredPathNotJustItsBasename)
{
    // CnjToCnbResult::absorbedFiles documents "paths as they were written in the source .cnj", so
    // a build script can match the list against what it generated. Recording only filename() made
    // two files in different directories indistinguishable, and named neither of them usefully.
    ScratchDir dir("absorbed");
    std::filesystem::create_directories(dir.path() / "art" / "ui");
    std::filesystem::create_directories(dir.path() / "art" / "world");
    WriteBytes(dir.path() / "art" / "ui" / "hero.png", TinyPng());
    WriteBytes(dir.path() / "art" / "world" / "hero.png", TinyPng());

    WriteText(dir.path() / "ui.cnj",
              R"({"cnjVersion":1,"type":"Texture2D","sourceFile":"art/ui/hero.png"})");
    WriteText(dir.path() / "world.cnj",
              R"({"cnjVersion":1,"type":"Texture2D","sourceFile":"art/world/hero.png"})");

    const CnjToCnbResult ui = CompileCnjToCnb((dir.path() / "ui.cnj").string());
    const CnjToCnbResult world = CompileCnjToCnb((dir.path() / "world.cnj").string());

    ASSERT_EQ(ui.absorbedFiles.size(), 2u);
    ASSERT_EQ(world.absorbedFiles.size(), 2u);
    EXPECT_EQ(ui.absorbedFiles[0], "ui.cnj");
    EXPECT_EQ(world.absorbedFiles[0], "world.cnj");
    EXPECT_EQ(ui.absorbedFiles[1], "art/ui/hero.png");
    EXPECT_EQ(world.absorbedFiles[1], "art/world/hero.png");
    EXPECT_NE(ui.absorbedFiles[1], world.absorbedFiles[1])
        << "two files in different directories must not collapse to one name";
}

TEST(CnbCompilerStrictnessTest, TheCanonicalNumericHelpersRefuseWhatTheyPromiseTo)
{
    // The document-level tests above reach these through a .cnj, which means the JSON parser gets
    // first refusal on some inputs -- a non-finite number is now a parse error, so the finiteness
    // branch is never reached from a document. It is still part of these functions' contract, and
    // a caller could hand them a value from anywhere, so it is asserted directly.
    using CNA::Internal::RequireCnjFiniteNumber;
    using CNA::Internal::RequireCnjInteger;
    using CNA::Internal::RequireCnjSingle;
    using CNA::Internal::JsonValue;
    using CNA::Internal::JsonType;

    const auto number = [](double v)
    {
        JsonValue value;
        value.type = JsonType::Number;
        value.numberValue = v;
        return value;
    };

    const JsonValue infinite = number(std::numeric_limits<double>::infinity());
    const JsonValue notANumber = number(std::numeric_limits<double>::quiet_NaN());
    EXPECT_THROW((void)RequireCnjFiniteNumber(&infinite, "x"), ContentLoadException);
    EXPECT_THROW((void)RequireCnjFiniteNumber(&notANumber, "x"), ContentLoadException);
    EXPECT_THROW((void)RequireCnjFiniteNumber(nullptr, "x"), ContentLoadException);

    // A double well inside double's range but far outside float's would narrow to an infinity.
    const JsonValue tooBigForFloat = number(1e300);
    EXPECT_THROW((void)RequireCnjSingle(&tooBigForFloat, "x"), ContentLoadException);
    const JsonValue finePrecision = number(1.5);
    EXPECT_FLOAT_EQ(RequireCnjSingle(&finePrecision, "x"), 1.5f);

    // The integer range test is performed in double space, because converting an out-of-range
    // double to an int64 is undefined behaviour rather than a large answer.
    const JsonValue huge = number(1e300);
    EXPECT_THROW((void)RequireCnjInteger(&huge, "x", 0, 255), ContentLoadException);
    const JsonValue fractional = number(2.5);
    EXPECT_THROW((void)RequireCnjInteger(&fractional, "x", 0, 255), ContentLoadException);
    const JsonValue negative = number(-1.0);
    EXPECT_THROW((void)RequireCnjInteger(&negative, "x", 0, 255), ContentLoadException);
    const JsonValue top = number(255.0);
    const JsonValue bottom = number(0.0);
    const JsonValue signedValue = number(-3.0);
    EXPECT_EQ(RequireCnjInteger(&top, "x", 0, 255), 255);
    EXPECT_EQ(RequireCnjInteger(&bottom, "x", 0, 255), 0);
    EXPECT_EQ(RequireCnjInteger(&signedValue, "x", -10, 10), -3);
}
