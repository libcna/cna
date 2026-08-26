// SPDX-License-Identifier: MS-PL
//
// plans/plan_cnb.md CNBF-034: deterministic, whole-container fuzzing.
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
        part.indexBytes.assign(2u * 6u, 0x03u);

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
