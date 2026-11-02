// SPDX-License-Identifier: MS-PL
//
// CNBF-034: deterministic, whole-container fuzzing.
//
// The negative tests elsewhere in this directory each break one invariant on purpose, which proves
// the checks that exist work. This proves something different and harder: that no combination of
// corrupt bytes gets through them. It mutates real, valid .cnb files -- header, table of contents
// and payload alike -- and asserts every result is either a successful load or a clean
// ContentLoadException. A crash, a hang, an out-of-memory, or any other exception type is a
// failure.
//
// The mutation stream is a fixed-seed LCG with no std::random_device and no clock, so a failure is
// reproducible from its seed alone. Same shape and the same reasoning as
// CNA/Internal/Xnb/XnbContainerFuzzTests.cpp, which does this for .xnb.
//
// Worth running under a sanitizer build (-DCNA_SANITIZE=address,undefined) as well: an
// exception-type check cannot see a read that stayed inside the heap but outside the buffer.

#include <algorithm>
#include <cstdint>
#include <exception>
#include <iostream>
#include <typeinfo>
#include <stdexcept>
#include <span>
#include "CNA/Content/Cnb/CnbCrc32c.hpp"
#include "CNA/Content/Cnb/CnbTextureCodec.hpp"
#include "CNA/Content/Cnb/CnbSpriteFontCodec.hpp"
#include "CNA/Content/Cnb/CnbSoundEffectCodec.hpp"
#include <gtest/gtest.h>
#include <string>
#include <vector>

#include "CNA/Content/Cnb/CnbAnimationClipCodec.hpp"
#include "CNA/Content/Cnb/CnbCurveCodec.hpp"
#include "CNA/Content/Cnb/CnbDocument.hpp"
#include "CNA/Content/Cnb/CnbModelCodec.hpp"
#include "CNA/Content/Cnb/CnbModelData.hpp"
#include "Microsoft/Xna/Framework/Content/ContentLoadException.hpp"
#include "Microsoft/Xna/Framework/Curve.hpp"
#include "Microsoft/Xna/Framework/CurveKey.hpp"

using CNA::Content::Cnb::CnbDocument;
using CNA::Content::Cnb::CnbEffectKind;
using CNA::Content::Cnb::CnbModelBone;
using CNA::Content::Cnb::CnbModelData;
using CNA::Content::Cnb::CnbModelMesh;
using CNA::Content::Cnb::CnbModelPart;
using CNA::Content::Cnb::CnbMorphData;
using CNA::Content::Cnb::CnbMorphTarget;
using Microsoft::Xna::Framework::Curve;
using Microsoft::Xna::Framework::CurveContinuity;
using Microsoft::Xna::Framework::CurveKey;
using Microsoft::Xna::Framework::CurveLoopType;
using Microsoft::Xna::Framework::Content::ContentLoadException;
using Microsoft::Xna::Framework::Graphics::AnimationClipEXT;
using Microsoft::Xna::Framework::Graphics::BoneTrackEXT;
using Microsoft::Xna::Framework::Graphics::KeyframeEXT;

namespace
{
    struct Rng
    {
        std::uint64_t state;
        explicit Rng(std::uint64_t seed) : state(seed) {}
        std::uint32_t next()
        {
            state = state * 6364136223846793005ULL + 1442695040888963407ULL;
            return static_cast<std::uint32_t>(state >> 33);
        }
        std::uint32_t below(std::uint32_t n) { return n == 0u ? 0u : next() % n; }
    };

    std::vector<std::uint8_t> ValidCurveFile()
    {
        Curve curve;
        curve.setPreLoopProperty(CurveLoopType::Oscillate);
        curve.setPostLoopProperty(CurveLoopType::Linear);
        for (int i = 0; i < 6; ++i)
        {
            curve.getKeysProperty().Add(CurveKey(static_cast<float>(i), static_cast<float>(i) * 2.0f,
                                                 0.5f, -0.5f,
                                                 i % 2 == 0 ? CurveContinuity::Smooth
                                                            : CurveContinuity::Step));
        }
        return CNA::Content::Cnb::EncodeCurveToCnb(curve, "Curves/fuzz");
    }

    std::vector<std::uint8_t> ValidClipFile()
    {
        AnimationClipEXT clip;
        clip.Duration = System::TimeSpan::FromSeconds(3.5);
        for (int t = 0; t < 4; ++t)
        {
            BoneTrackEXT track;
            track.BoneIndex = t;
            for (int k = 0; k < 5; ++k)
            {
                KeyframeEXT key;
                key.Time = System::TimeSpan::FromSeconds(k * 0.25);
                key.Translation = Microsoft::Xna::Framework::Vector3(
                    static_cast<float>(k), static_cast<float>(t), 1.0f);
                track.Keys.push_back(key);
            }
            clip.Tracks.push_back(std::move(track));
        }
        return CNA::Content::Cnb::EncodeAnimationClipToCnb(clip, "Clips/fuzz");
    }

    std::vector<std::uint8_t> ValidModelFile()
    {
        CnbModelData model;
        model.hasBoneHierarchy = true;
        model.appliesGltfLightingPolicy = true;
        model.bones = {CnbModelBone{"Root", -1, {}}, CnbModelBone{"Child", 0, {}}};

        CnbModelPart part;
        part.name = "Body";
        part.vertexStride = 32u;
        part.vertexCount = 6u;
        part.indexCount = 6u;
        part.indexElementSize = 2u;
        part.primitiveTopology = 4u;
        part.primitiveCount = 2u;
        part.effectKind = CnbEffectKind::PbrEffect;
        part.material.baseColorTexture = "Textures/fuzz";
        part.material.normalMap = "Textures/fuzz_n";
        part.vertexBytes.assign(32u * 6u, 0x5Au);
        // 0,1,2,3,4,5 as little-endian u16 -- indices must address vertices this part has.
        part.indexBytes.assign(2u * 6u, 0u);
        for (std::uint8_t i = 0; i < 6u; ++i) { part.indexBytes[i * 2u] = i; }

        CnbMorphData morph;
        morph.vertexCount = 6u;
        CnbMorphTarget target;
        target.positionDeltas.assign(18u, 0.25f);
        target.normalDeltas.assign(18u, -0.5f);
        morph.targets = {target};
        morph.weights = {0.5f};
        part.morph = morph;

        model.parts = {part};
        model.meshes = {CnbModelMesh{"Body", 1, {0u}}};

        CNA::Content::Cnb::CnbModelSkeleton skeleton;
        skeleton.hierarchy = {-1, 0};
        skeleton.bindPose.resize(2);
        skeleton.inverseBindPose.resize(2);
        skeleton.rootPrefix.resize(2);
        model.skeleton = skeleton;

        CNA::Content::Cnb::CnbModelAnimation animation;
        animation.name = "Walk";
        animation.clip.Duration = System::TimeSpan::FromSeconds(1.0);
        BoneTrackEXT track;
        track.BoneIndex = 0;
        KeyframeEXT key;
        track.Keys = {key};
        animation.clip.Tracks = {track};
        model.animations = {animation};
        model.lights = {CNA::Content::Cnb::CnbModelLight{}};

        return CNA::Content::Cnb::EncodeModelToCnb(model, "Models/fuzz");
    }

    /// Applies one of the mutation shapes a corrupt or adversarial file actually takes: a flipped
    /// bit, a wild byte, a whole clobbered 32-bit field (the shape that turns a length into a
    /// huge one), a truncation, and an extension.
    void Mutate(std::vector<std::uint8_t>& bytes, Rng& rng)
    {
        if (bytes.empty()) { return; }
        switch (rng.below(6))
        {
            case 0:
                bytes[rng.below(static_cast<std::uint32_t>(bytes.size()))] ^=
                    static_cast<std::uint8_t>(1u << rng.below(8));
                break;
            case 1:
                bytes[rng.below(static_cast<std::uint32_t>(bytes.size()))] =
                    static_cast<std::uint8_t>(rng.below(256));
                break;
            case 2:
            {
                // Overwrite a whole aligned u32 with a value chosen to be nasty: the extremes are
                // where length and count fields do damage.
                if (bytes.size() < 4u) { break; }
                const std::uint32_t at =
                    (rng.below(static_cast<std::uint32_t>(bytes.size() - 3u)) / 4u) * 4u;
                static const std::uint32_t kNasty[] = {0u, 1u, 0x7FFFFFFFu, 0x80000000u,
                                                       0xFFFFFFFFu, 0xFFFFFFF0u};
                const std::uint32_t value = kNasty[rng.below(6)];
                for (int i = 0; i < 4; ++i)
                {
                    bytes[at + static_cast<std::size_t>(i)] =
                        static_cast<std::uint8_t>((value >> (8 * i)) & 0xFFu);
                }
                break;
            }
            case 3:
                bytes.resize(rng.below(static_cast<std::uint32_t>(bytes.size())));
                break;
            case 4:
                bytes.insert(bytes.end(), 1u + rng.below(64u),
                             static_cast<std::uint8_t>(rng.below(256)));
                break;
            default:
                // A run of zeros, which is what a partially-written file looks like.
                {
                    const std::uint32_t at = rng.below(static_cast<std::uint32_t>(bytes.size()));
                    const std::uint32_t count =
                        std::min<std::uint32_t>(1u + rng.below(32u),
                                                static_cast<std::uint32_t>(bytes.size()) - at);
                    for (std::uint32_t i = 0; i < count; ++i) { bytes[at + i] = 0u; }
                }
                break;
        }
    }

    enum class Kind { Curve, Clip, Model };

    /// Loads mutated bytes exactly as production code would, and reports what escaped. Returns
    /// true when the input was accepted, so the caller can see the sweep is not merely rejecting
    /// everything.
    bool TryLoad(const std::vector<std::uint8_t>& bytes, Kind kind, std::uint64_t seed,
                 std::uint32_t iteration)
    {
        try
        {
            const CnbDocument document = CnbDocument::Parse(bytes, "fuzz.cnb");
            switch (kind)
            {
                case Kind::Curve: (void)CNA::Content::Cnb::DecodeCurveFromCnb(document); break;
                case Kind::Clip: (void)CNA::Content::Cnb::DecodeAnimationClipFromCnb(document); break;
                case Kind::Model: (void)CNA::Content::Cnb::DecodeModelFromCnb(document); break;
            }
            return true;
        }
        catch (const ContentLoadException&)
        {
            return false;
        }
        catch (const std::exception& e)
        {
            ADD_FAILURE() << "seed " << seed << " iteration " << iteration
                          << ": a malformed .cnb escaped as " << typeid(e).name() << ": "
                          << e.what();
            return false;
        }
        catch (...)
        {
            ADD_FAILURE() << "seed " << seed << " iteration " << iteration
                          << ": a malformed .cnb escaped as a non-std exception";
            return false;
        }
    }

    void FuzzOne(Kind kind, const std::vector<std::uint8_t>& valid, std::uint64_t seed,
                 std::uint32_t iterations, const char* label)
    {
        Rng rng(seed);
        int accepted = 0;
        for (std::uint32_t i = 0; i < iterations; ++i)
        {
            std::vector<std::uint8_t> mutated = valid;
            const std::uint32_t mutations = 1u + rng.below(4u);
            for (std::uint32_t m = 0; m < mutations; ++m) { Mutate(mutated, rng); }
            if (TryLoad(mutated, kind, seed, i)) { ++accepted; }
        }
        // A sweep that rejected literally everything would still "pass" while proving nothing
        // about the accepting path, and one that accepted everything would mean the mutations
        // never reached anything. Neither extreme is expected; both are worth seeing.
        std::cout << "[   FUZZ   ] " << label << ": " << accepted << " of " << iterations
                  << " mutated inputs still loaded\n";
    }
}

TEST(CnbContainerFuzzTest, MutatedCurveFilesNeverEscapeAsAnythingButAContentLoadException)
{
    const std::vector<std::uint8_t> valid = ValidCurveFile();
    ASSERT_NO_THROW((void)CnbDocument::Parse(valid, "fuzz.cnb")) << "the base fixture must be valid";
    FuzzOne(Kind::Curve, valid, 0x9E3779B97F4A7C15ull, 4000u, "Curve");
}

TEST(CnbContainerFuzzTest, MutatedAnimationClipFilesNeverEscapeAsAnythingButAContentLoadException)
{
    const std::vector<std::uint8_t> valid = ValidClipFile();
    ASSERT_NO_THROW((void)CnbDocument::Parse(valid, "fuzz.cnb")) << "the base fixture must be valid";
    FuzzOne(Kind::Clip, valid, 0xD1B54A32D192ED03ull, 4000u, "AnimationClip");
}

TEST(CnbContainerFuzzTest, MutatedModelFilesNeverEscapeAsAnythingButAContentLoadException)
{
    const std::vector<std::uint8_t> valid = ValidModelFile();
    ASSERT_NO_THROW((void)CnbDocument::Parse(valid, "fuzz.cnb")) << "the base fixture must be valid";
    FuzzOne(Kind::Model, valid, 0xA24BAED4963EE407ull, 6000u, "Model");
}

TEST(CnbContainerFuzzTest, ArbitraryBytesAreNeverMistakenForACnbFile)
{
    // Not a mutation of anything valid: pure noise, plus files that begin with the magic and then
    // hold nothing sensible -- the case where a reader is most tempted to trust a length.
    Rng rng(0x2545F4914F6CDD1Dull);
    for (std::uint32_t i = 0; i < 3000u; ++i)
    {
        std::vector<std::uint8_t> bytes(rng.below(512u));
        for (std::uint8_t& byte : bytes) { byte = static_cast<std::uint8_t>(rng.below(256)); }
        if (i % 2u == 0u && bytes.size() >= 4u)
        {
            bytes[0] = 0x43u; bytes[1] = 0x4Eu; bytes[2] = 0x42u; bytes[3] = 0x1Au;
        }
        (void)TryLoad(bytes, Kind::Model, 0x2545F4914F6CDD1Dull, i);
    }
}

namespace
{
    namespace Cnb = CNA::Content::Cnb;

    // Repairs CRCs after structured edits so semantic mutations reach the codec itself.
    struct StructuredCnb
    {
        std::vector<std::uint8_t> bytes;

        std::uint64_t Read(std::size_t at, std::size_t width) const
        {
            std::uint64_t value = 0;
            for (std::size_t i = 0; i < width; ++i) { value |= std::uint64_t(bytes.at(at + i)) << (8u * i); }
            return value;
        }
        void Put(std::size_t at, std::uint64_t value, std::size_t width = 4)
        {
            for (std::size_t i = 0; i < width; ++i) { bytes.at(at + i) = static_cast<std::uint8_t>(value >> (8u * i)); }
        }
        std::size_t Entry(std::size_t ordinal) const
        {
            return static_cast<std::size_t>(Read(32, 8)) + ordinal * Cnb::Format::TocEntrySize;
        }
        std::size_t Entry(Cnb::CnbChunkId id) const
        {
            for (std::size_t i = 0; i < Read(20, 4); ++i)
            {
                if (Read(Entry(i), 4) == id.value) { return Entry(i); }
            }
            throw std::logic_error("missing structured test chunk");
        }
        std::size_t Payload(Cnb::CnbChunkId id) const
        {
            return static_cast<std::size_t>(Read(Entry(id) + 8, 8));
        }
        void Repair(bool payloads = true)
        {
            const auto view = std::span<const std::uint8_t>(bytes);
            if (payloads)
            {
                for (std::size_t i = 0; i < Read(20, 4); ++i)
                {
                    const auto entry = Entry(i);
                    Put(entry + 32, Cnb::Crc32c(view.subspan(
                        static_cast<std::size_t>(Read(entry + 8, 8)),
                        static_cast<std::size_t>(Read(entry + 16, 8)))));
                }
            }
            Put(40, Cnb::Crc32c(view.subspan(static_cast<std::size_t>(Read(32, 8)),
                                             static_cast<std::size_t>(Read(20, 4)) * Cnb::Format::TocEntrySize)));
            Put(44, Cnb::Crc32c(view.first(Cnb::Format::HeaderChecksumCoverage)));
        }
    };

    Cnb::CnbReadLimits StructuredLimits()
    {
        Cnb::CnbReadLimits limits;
        limits.maxFileSize = 4u * 1024u * 1024u;
        limits.maxChunkSize = 2u * 1024u * 1024u;
        limits.maxTotalUncompressedSize = 32u * 1024u * 1024u;
        limits.maxChunkCount = 128;
        limits.maxArrayElementCount = 1024;
        return limits;
    }

    void CheckStructuredSeed(const std::vector<std::uint8_t>& bytes, Kind kind)
    {
        const auto document = CnbDocument::Parse(bytes, "recovery.cnb", StructuredLimits());
        if (kind == Kind::Curve)
        {
            const auto value = Cnb::DecodeCurveFromCnb(document);
            ASSERT_EQ(value.getKeysProperty().getCountProperty(), 6);
            EXPECT_FLOAT_EQ(value.getKeysProperty()[5].getValueProperty(), 10.0f);
        }
        else if (kind == Kind::Clip)
        {
            const auto value = Cnb::DecodeAnimationClipFromCnb(document);
            ASSERT_EQ(value.Tracks.size(), 4u);
            ASSERT_EQ(value.Tracks[3].Keys.size(), 5u);
            EXPECT_FLOAT_EQ(value.Tracks[3].Keys[4].Translation.X, 4.0f);
        }
        else
        {
            const auto value = Cnb::DecodeModelFromCnb(document);
            ASSERT_EQ(value.parts.size(), 1u);
            EXPECT_EQ(value.parts[0].vertexCount, 6u);
            ASSERT_EQ(value.bones.size(), 2u);
            EXPECT_EQ(value.bones[1].parent, 0);
            ASSERT_EQ(value.animations.size(), 1u);
            EXPECT_EQ(value.animations[0].clip.Tracks.size(), 1u);
        }
    }
}

TEST(CnbContainerFuzzTest, Safe002ContainerLimitsAcceptExactAndRefuseOnePastThenRecover)
{
    const auto seed = ValidCurveFile();
    const auto document = CnbDocument::Parse(seed, "safe002.cnb");
    for (const int field : {0, 1, 2, 3})
    {
        auto limits = StructuredLimits();
        std::uint64_t largest = 0, total = 0;
        for (std::size_t i = 0; i < document.ChunkCount(); ++i)
        {
            largest = std::max(largest, document.ChunkAt(i).uncompressedSize);
            total += document.ChunkAt(i).uncompressedSize;
        }
        if (field == 0) { limits.maxFileSize = seed.size(); }
        if (field == 1) { limits.maxChunkCount = static_cast<std::uint32_t>(document.ChunkCount()); }
        if (field == 2) { limits.maxChunkSize = largest; }
        if (field == 3) { limits.maxTotalUncompressedSize = total; }
        EXPECT_EQ(Cnb::DecodeCurveFromCnb(CnbDocument::Parse(seed, "exact.cnb", limits))
                      .getKeysProperty().getCountProperty(), 6);
        if (field == 0) { --limits.maxFileSize; }
        if (field == 1) { --limits.maxChunkCount; }
        if (field == 2) { --limits.maxChunkSize; }
        if (field == 3) { --limits.maxTotalUncompressedSize; }
        EXPECT_THROW((void)CnbDocument::Parse(seed, "past.cnb", limits), ContentLoadException);
        CheckStructuredSeed(seed, Kind::Curve);
    }
}

TEST(CnbContainerFuzzTest, Safe002RechecksummedTocRangesRefuseAndRecover)
{
    const auto seed = ValidCurveFile();
    for (const int mutation : {0, 1, 2, 3, 4, 5, 6})
    {
        SCOPED_TRACE(mutation);
        StructuredCnb candidate{seed};
        const auto entry = candidate.Entry(Cnb::CnbCurveChunk::Keys);
        if (mutation == 0) { candidate.Put(entry + 8, 0, 8); }
        if (mutation == 1) { candidate.Put(entry + 8, candidate.Read(32, 8), 8); }
        if (mutation == 2) { candidate.Put(entry + 8, (seed.size() + 7u) & ~7ull, 8); }
        if (mutation == 3) { candidate.Put(entry + 8, 0xFFFFFFFFFFFFFFFCull, 8); }
        if (mutation == 4) { candidate.Put(entry + 8, candidate.Read(candidate.Entry(Cnb::CnbCurveChunk::Header) + 8, 8), 8); }
        if (mutation == 5) { candidate.Put(entry + 24, candidate.Read(entry + 16, 8) + 1, 8); }
        if (mutation == 6) { candidate.Put(entry + 16, 0xFFFFFFFFFFFFFFFFull, 8); }
        candidate.Repair(false);
        EXPECT_THROW((void)CnbDocument::Parse(candidate.bytes, "ranges.cnb", StructuredLimits()),
                     ContentLoadException);
        CheckStructuredSeed(seed, Kind::Curve);
    }
    for (const std::uint32_t count : {129u, 0xFFFFFFFFu})
    {
        StructuredCnb candidate{seed};
        candidate.Put(20, count);
        candidate.Put(44, Cnb::Crc32c(std::span<const std::uint8_t>(candidate.bytes)
                                       .first(Cnb::Format::HeaderChecksumCoverage)));
        EXPECT_THROW((void)CnbDocument::Parse(candidate.bytes, "counts.cnb", StructuredLimits()),
                     ContentLoadException);
        CheckStructuredSeed(seed, Kind::Curve);
    }
}

TEST(CnbContainerFuzzTest, Safe002SemanticMutationsPassContainerChecksBeforeRefusalAndRecover)
{
    struct Edit { Kind kind; Cnb::CnbChunkId chunk; std::size_t offset; std::uint32_t value; };
    const Edit edits[] = {
        {Kind::Curve, Cnb::CnbCurveChunk::Header, 0, 5},
        {Kind::Curve, Cnb::CnbCurveChunk::Header, 8, 7},
        {Kind::Curve, Cnb::CnbCurveChunk::Header, 8, 0xFFFFFFFFu},
        {Kind::Curve, Cnb::CnbCurveChunk::Keys, 16, 2},
        {Kind::Clip, Cnb::CnbAnimationClipChunk::Header, 8, 2},
        {Kind::Clip, Cnb::CnbAnimationClipChunk::Header, 12, 5},
        {Kind::Clip, Cnb::CnbAnimationClipChunk::Header, 16, 0xFFFFFFFFu},
        {Kind::Clip, Cnb::CnbAnimationClipChunk::Tracks, 4, 0xFFFFFFFFu},
        {Kind::Clip, Cnb::CnbAnimationClipChunk::Tracks, 8, 0xFFFFFFFFu},
        {Kind::Model, Cnb::CnbModelChunk::Header, 4, 0xFFFFFFFFu},
        {Kind::Model, Cnb::CnbModelChunk::Bones, 4, 0}, // self-cycle
        {Kind::Model, Cnb::CnbModelChunk::Bones, 4, 1}, // root -> child -> root
        {Kind::Model, Cnb::CnbModelChunk::Bones, 76, 2}, // missing parent
        {Kind::Model, Cnb::CnbModelChunk::Meshes, 8, 0xFFFFFFFFu},
        {Kind::Model, Cnb::CnbModelChunk::Materials, 4, 2}, // missing XREF
        {Kind::Model, Cnb::CnbModelChunk::IndexData, 0, 6}};
    const auto curve = ValidCurveFile(), clip = ValidClipFile(), model = ValidModelFile();
    for (const auto& edit : edits)
    {
        SCOPED_TRACE(Cnb::ChunkIdToString(edit.chunk));
        SCOPED_TRACE(edit.offset);
        const auto& seed = edit.kind == Kind::Curve ? curve : edit.kind == Kind::Clip ? clip : model;
        StructuredCnb candidate{seed};
        candidate.Put(candidate.Payload(edit.chunk) + edit.offset, edit.value);
        candidate.Repair();
        const auto document = CnbDocument::Parse(candidate.bytes, "semantic.cnb", StructuredLimits());
        if (edit.kind == Kind::Curve) { EXPECT_THROW((void)Cnb::DecodeCurveFromCnb(document), ContentLoadException); }
        if (edit.kind == Kind::Clip) { EXPECT_THROW((void)Cnb::DecodeAnimationClipFromCnb(document), ContentLoadException); }
        if (edit.kind == Kind::Model) { EXPECT_THROW((void)Cnb::DecodeModelFromCnb(document), ContentLoadException); }
        CheckStructuredSeed(seed, edit.kind);
    }
    // Removing a required chunk's identity must get past the container, too.
    for (const auto& seed : {curve, clip, model})
    {
        const auto kind = seed == curve ? Kind::Curve : seed == clip ? Kind::Clip : Kind::Model;
        const auto id = kind == Kind::Curve ? Cnb::CnbCurveChunk::Keys :
                        kind == Kind::Clip ? Cnb::CnbAnimationClipChunk::Keys : Cnb::CnbModelChunk::VertexData;
        StructuredCnb candidate{seed};
        const auto entry = candidate.Entry(id);
        candidate.Put(entry, Cnb::MakeChunkId('M', 'I', 'S', 'S').value);
        candidate.Put(entry + 4, 0); // optional unknown chunk; the schema must notice the absence
        candidate.Repair();
        const auto document = CnbDocument::Parse(candidate.bytes, "missing.cnb", StructuredLimits());
        if (kind == Kind::Curve) { EXPECT_THROW((void)Cnb::DecodeCurveFromCnb(document), ContentLoadException); }
        if (kind == Kind::Clip) { EXPECT_THROW((void)Cnb::DecodeAnimationClipFromCnb(document), ContentLoadException); }
        if (kind == Kind::Model) { EXPECT_THROW((void)Cnb::DecodeModelFromCnb(document), ContentLoadException); }
        CheckStructuredSeed(seed, kind);
    }
}

TEST(CnbContainerFuzzTest, Safe002TextureCubeFontAndSoundReachSemanticDecodersAndRecover)
{
    const auto texture = Cnb::MakeRgba8Texture2DData(1, 1, {11, 22, 33, 255});
    auto cube = texture;
    cube.faceCount = 6;
    cube.representations[0].levels.assign(6, {11, 22, 33, 255});
    Cnb::CnbSpriteFontData font;
    font.atlas = texture;
    font.characters = {u'A'};
    font.glyphBounds.emplace_back(0, 0, 1, 1);
    font.cropping = font.glyphBounds;
    font.kerning.emplace_back(0.0f, 1.0f, 0.0f);
    font.lineSpacing = 1;
    Cnb::CnbSoundEffectData sound;
    sound.frameCount = 2;
    sound.samples = {0, 1, 2, 3};
    const std::vector<std::vector<std::uint8_t>> seeds = {
        Cnb::EncodeTexture2DToCnb(texture), Cnb::EncodeTextureCubeToCnb(cube),
        Cnb::EncodeSpriteFontToCnb(font), Cnb::EncodeSoundEffectToCnb(sound)};
    for (std::size_t kind = 0; kind < seeds.size(); ++kind)
    {
        for (const std::uint32_t value : {0u, 0xFFFFFFFFu})
        {
            SCOPED_TRACE(kind);
            SCOPED_TRACE(value);
            StructuredCnb candidate{seeds[kind]};
            const auto id = kind < 2 ? Cnb::CnbTextureChunk::Header : kind == 2 ?
                Cnb::CnbSpriteFontChunk::Header : Cnb::CnbSoundEffectChunk::Header;
            // Width, glyph count, or sample rate: valid seeds have a nonzero bounded value.
            const auto offset = kind == 3 ? 4u : 0u;
            candidate.Put(candidate.Payload(id) + offset, value);
            candidate.Repair();
            const auto document = CnbDocument::Parse(candidate.bytes, "semantic.cnb", StructuredLimits());
            if (kind == 0) { EXPECT_THROW((void)Cnb::DecodeTexture2DFromCnb(document), ContentLoadException); }
            if (kind == 1) { EXPECT_THROW((void)Cnb::DecodeTextureCubeFromCnb(document), ContentLoadException); }
            if (kind == 2) { EXPECT_THROW((void)Cnb::DecodeSpriteFontFromCnb(document), ContentLoadException); }
            if (kind == 3) { EXPECT_THROW((void)Cnb::DecodeSoundEffectFromCnb(document), ContentLoadException); }
            const auto recovered = CnbDocument::Parse(seeds[kind], "recovery.cnb", StructuredLimits());
            if (kind == 0) { EXPECT_EQ(Cnb::DecodeTexture2DFromCnb(recovered).representations[0].levels[0], texture.representations[0].levels[0]); }
            if (kind == 1) { EXPECT_EQ(Cnb::DecodeTextureCubeFromCnb(recovered).representations[0].levels, cube.representations[0].levels); }
            if (kind == 2) { EXPECT_EQ(Cnb::DecodeSpriteFontFromCnb(recovered).characters, font.characters); }
            if (kind == 3) { EXPECT_EQ(Cnb::DecodeSoundEffectFromCnb(recovered).samples, sound.samples); }
        }
    }
}
