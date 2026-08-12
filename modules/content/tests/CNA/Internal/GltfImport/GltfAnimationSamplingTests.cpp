// SPDX-License-Identifier: MS-PL
//
// plan_gltf.md GLTF-298 / GLTF-300 / GLTF-301 / GLTF-302 / GLTF-304 / GLTF-309 / GLTF-311:
// how a channel's samples become keyframes.
//
// glTF gives an animation sampler three interpolation modes and lets each of a node's three
// components be driven independently. `ExtractClips` resamples every channel onto the union of one
// bone's own channel times, so each keyframe carries a full TRS -- which means the sampler has to
// answer for times it has no sample at, and every mode answers differently. Getting one of them
// wrong is not a crash: it is an animation that eases where it should snap, or snaps where it
// should ease, which looks like an authoring choice.
//
// These are scratch documents rather than corpus assets because what varies is a sampler's
// `interpolation` string and its keyframe times -- a table, not a scene.

#include <cmath>
#include <cstdint>
#include <cstring>
#include <gtest/gtest.h>
#include <string>
#include <vector>

#include "CNA/Internal/GltfImport/GltfImportCore.hpp"

using namespace CNA::Internal::GltfImport;
using namespace Microsoft::Xna::Framework;

namespace
{
    constexpr float kTolerance = 1e-4f;

    struct Parsed
    {
        cgltf_data* data = nullptr;
        ~Parsed() { if (data != nullptr) { cgltf_free(data); } }
        Parsed() = default;
        Parsed(const Parsed&) = delete;
        Parsed& operator=(const Parsed&) = delete;
    };

    std::string Base64(const std::vector<std::uint8_t>& bytes)
    {
        static const char* kAlphabet =
            "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
        std::string out;
        for (std::size_t i = 0; i < bytes.size(); i += 3)
        {
            const std::uint32_t chunk =
                (static_cast<std::uint32_t>(bytes[i]) << 16) |
                (i + 1 < bytes.size() ? static_cast<std::uint32_t>(bytes[i + 1]) << 8 : 0u) |
                (i + 2 < bytes.size() ? static_cast<std::uint32_t>(bytes[i + 2]) : 0u);
            out += kAlphabet[(chunk >> 18) & 0x3F];
            out += kAlphabet[(chunk >> 12) & 0x3F];
            out += (i + 1 < bytes.size()) ? kAlphabet[(chunk >> 6) & 0x3F] : '=';
            out += (i + 2 < bytes.size()) ? kAlphabet[chunk & 0x3F] : '=';
        }
        return out;
    }

    void AppendFloats(std::vector<std::uint8_t>& buffer, const std::vector<float>& values)
    {
        for (const float value : values)
        {
            std::uint8_t bytes[4];
            std::memcpy(bytes, &value, 4);
            buffer.insert(buffer.end(), bytes, bytes + 4);
        }
    }

    /// A one-joint skin whose single joint is driven by one translation channel.
    ///
    /// The mesh is the minimum a skin needs to be extracted at all; every case below varies only
    /// `interpolation` and the sampler's own input/output arrays.
    std::string AnimatedJointDocument(const std::string& interpolation,
                                       const std::vector<float>& times,
                                       const std::vector<float>& translations)
    {
        std::vector<std::uint8_t> buffer;
        // 0: three POSITION vertices.
        AppendFloats(buffer, {0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f});
        // 1: three JOINTS_0, as UNSIGNED_BYTE VEC4 -- all bound to joint 0.
        const std::size_t jointsOffset = buffer.size();
        for (int v = 0; v < 3; ++v)
        {
            buffer.insert(buffer.end(), {0, 0, 0, 0});
        }
        // 2: three WEIGHTS_0.
        const std::size_t weightsOffset = buffer.size();
        AppendFloats(buffer, {1, 0, 0, 0,  1, 0, 0, 0,  1, 0, 0, 0});
        // 3: one MAT4 inverse bind matrix (identity).
        const std::size_t ibmOffset = buffer.size();
        AppendFloats(buffer, {1, 0, 0, 0,  0, 1, 0, 0,  0, 0, 1, 0,  0, 0, 0, 1});
        // 4: the sampler input times.
        const std::size_t timesOffset = buffer.size();
        AppendFloats(buffer, times);
        // 5: the sampler output translations.
        const std::size_t outputOffset = buffer.size();
        AppendFloats(buffer, translations);

        return std::string(R"GLTF({
  "asset": { "version": "2.0" },
  "scene": 0,
  "scenes": [ { "nodes": [0, 1] } ],
  "nodes": [
    { "name": "Joint0" },
    { "name": "SkinnedMeshNode", "mesh": 0, "skin": 0 }
  ],
  "skins": [ { "name": "Skin", "joints": [0], "inverseBindMatrices": 3 } ],
  "meshes": [ { "primitives": [ {
      "attributes": { "POSITION": 0, "JOINTS_0": 1, "WEIGHTS_0": 2 }, "mode": 4
  } ] } ],
  "animations": [ {
      "name": "Clip",
      "samplers": [ { "input": 4, "output": 5, "interpolation": ")GLTF") + interpolation +
               R"GLTF(" } ],
      "channels": [ { "sampler": 0, "target": { "node": 0, "path": "translation" } } ]
  } ],
  "buffers": [ { "byteLength": )GLTF" + std::to_string(buffer.size()) +
               R"GLTF(, "uri": "data:application/octet-stream;base64,)GLTF" + Base64(buffer) +
               R"GLTF(" } ],
  "bufferViews": [
    { "buffer": 0, "byteOffset": 0, "byteLength": 36 },
    { "buffer": 0, "byteOffset": )GLTF" + std::to_string(jointsOffset) + R"GLTF(, "byteLength": 12 },
    { "buffer": 0, "byteOffset": )GLTF" + std::to_string(weightsOffset) + R"GLTF(, "byteLength": 48 },
    { "buffer": 0, "byteOffset": )GLTF" + std::to_string(ibmOffset) + R"GLTF(, "byteLength": 64 },
    { "buffer": 0, "byteOffset": )GLTF" + std::to_string(timesOffset) + R"GLTF(, "byteLength": )GLTF" +
               std::to_string(times.size() * 4) + R"GLTF( },
    { "buffer": 0, "byteOffset": )GLTF" + std::to_string(outputOffset) +
               R"GLTF(, "byteLength": )GLTF" + std::to_string(translations.size() * 4) + R"GLTF( }
  ],
  "accessors": [
    { "bufferView": 0, "componentType": 5126, "count": 3, "type": "VEC3", "min": [0,0,0], "max": [1,1,0] },
    { "bufferView": 1, "componentType": 5121, "count": 3, "type": "VEC4" },
    { "bufferView": 2, "componentType": 5126, "count": 3, "type": "VEC4" },
    { "bufferView": 3, "componentType": 5126, "count": 1, "type": "MAT4" },
    { "bufferView": 4, "componentType": 5126, "count": )GLTF" + std::to_string(times.size()) +
               R"GLTF(, "type": "SCALAR" },
    { "bufferView": 5, "componentType": 5126, "count": )GLTF" +
               std::to_string(translations.size() / 3) + R"GLTF(, "type": "VEC3" }
  ]
})GLTF";
    }

    bool Parse(Parsed& out, const std::string& json)
    {
        cgltf_options options{};
        if (cgltf_parse(&options, json.data(), json.size(), &out.data) != cgltf_result_success)
        {
            return false;
        }
        return cgltf_load_buffers(&options, out.data, ".") == cgltf_result_success;
    }

    /// The single clip a document above produces, with its one track.
    std::vector<ClipOut> ClipsOf(const Parsed& parsed)
    {
        const SceneGraphOut scene = BuildSceneGraph(parsed.data);
        const SkeletonResult skeleton =
            BuildSkeleton(parsed.data->skins, scene, Matrix::getIdentityProperty(), 1.0f);
        std::vector<std::string> warnings;
        return ExtractClips(parsed.data, skeleton, 1.0f, warnings);
    }

    /// The keyframe at `time`, or a null pointer when the clip has none there. Resampling is what
    /// makes this a lookup rather than an interpolation: every channel's own times end up as
    /// keyframes of the bone's track.
    const KeyframeOut* KeyAt(const TrackOut& track, double time)
    {
        for (const KeyframeOut& key : track.keys)
        {
            if (std::abs(key.time - time) < 1e-6) { return &key; }
        }
        return nullptr;
    }
}

// --- GLTF-300 / GLTF-309: LINEAR, and the time domain -------------------------------------------

TEST(GltfAnimationSampling, LinearTranslationReachesItsAuthoredEndpointsExactly)
{
    // The endpoints are what an interpolation cannot get wrong by rounding: at each authored time
    // the sampler must return that sample verbatim, not a blend of its neighbours. And the times
    // are seconds throughout (GLTF-309) -- a millisecond or tick domain would put the duration
    // three orders of magnitude out, which reads as an animation that never plays.
    Parsed parsed;
    ASSERT_TRUE(Parse(parsed, AnimatedJointDocument(
        "LINEAR", {0.0f, 1.0f}, {0.0f, 0.0f, 0.0f,  5.0f, 0.0f, 0.0f})));

    const std::vector<ClipOut> clips = ClipsOf(parsed);
    ASSERT_EQ(1u, clips.size());
    EXPECT_NEAR(1.0, clips[0].duration, 1e-6) << "the duration is the last sample time, in seconds";
    ASSERT_EQ(1u, clips[0].tracks.size());
    const TrackOut& track = clips[0].tracks[0];

    const KeyframeOut* start = KeyAt(track, 0.0);
    const KeyframeOut* end = KeyAt(track, 1.0);
    ASSERT_NE(nullptr, start);
    ASSERT_NE(nullptr, end);
    EXPECT_NEAR(0.0f, start->translation.X, kTolerance);
    EXPECT_NEAR(5.0f, end->translation.X, kTolerance)
        << "the authored endpoint was not reached; a sampler that interpolates AT a sample time is "
           "blending a value with itself and something else";
}

// --- GLTF-301: STEP holds ------------------------------------------------------------------------

TEST(GltfAnimationSampling, StepInterpolationHoldsTheEarlierSampleAcrossTheWholeInterval)
{
    // STEP is the mode a resampler is most likely to break, because resampling is interpolation by
    // nature and STEP's whole point is that it does not interpolate. The failure is not a crash: a
    // snapped animation that eases instead looks like an authoring choice, so nothing reports it.
    //
    // The third channel time (2.0) exists only to force a resample at a moment the STEP channel has
    // no sample of its own -- otherwise every keyframe lands exactly on an authored sample and the
    // holding behaviour is never exercised at all.
    Parsed parsed;
    ASSERT_TRUE(Parse(parsed, AnimatedJointDocument(
        "STEP", {0.0f, 1.0f, 2.0f},
        {0.0f, 0.0f, 0.0f,  10.0f, 0.0f, 0.0f,  20.0f, 0.0f, 0.0f})));

    const std::vector<ClipOut> clips = ClipsOf(parsed);
    ASSERT_EQ(1u, clips.size());
    ASSERT_EQ(1u, clips[0].tracks.size());
    const TrackOut& track = clips[0].tracks[0];

    for (const auto& [time, expected] : std::vector<std::pair<double, float>>{
             {0.0, 0.0f}, {1.0, 10.0f}, {2.0, 20.0f}})
    {
        SCOPED_TRACE("t = " + std::to_string(time));
        const KeyframeOut* key = KeyAt(track, time);
        ASSERT_NE(nullptr, key);
        EXPECT_NEAR(expected, key->translation.X, kTolerance);
    }
}

// --- GLTF-304: an unanimated component falls back to the bind pose -------------------------------

TEST(GltfAnimationSampling, AComponentWithNoChannelKeepsItsBindPoseValueRatherThanZero)
{
    // glTF drives each of a node's three components independently, so a clip that animates only
    // translation says nothing at all about rotation and scale. Every keyframe still carries a full
    // TRS, and the value that belongs in the other two slots is the node's own bind pose -- NOT the
    // zero and identity a default-constructed keyframe would hold. A scale silently falling to zero
    // collapses the bone and everything under it to a point, which is exactly the failure family
    // this campaign exists to remove.
    //
    // The joint therefore carries a non-default scale and translation of its own, so bind-pose
    // fallback and default-construction give visibly different answers.
    Parsed parsed;
    ASSERT_TRUE(Parse(parsed, [] {
        std::string json = AnimatedJointDocument(
            "LINEAR", {0.0f, 1.0f}, {0.0f, 0.0f, 0.0f,  5.0f, 0.0f, 0.0f});
        const std::string from = R"({ "name": "Joint0" })";
        const std::string to = R"({ "name": "Joint0", "scale": [2, 3, 4] })";
        const std::size_t at = json.find(from);
        EXPECT_NE(std::string::npos, at);
        return json.replace(at, from.size(), to);
    }()));

    const std::vector<ClipOut> clips = ClipsOf(parsed);
    ASSERT_EQ(1u, clips.size());
    ASSERT_EQ(1u, clips[0].tracks.size());
    ASSERT_FALSE(clips[0].tracks[0].keys.empty());

    for (const KeyframeOut& key : clips[0].tracks[0].keys)
    {
        SCOPED_TRACE("t = " + std::to_string(key.time));
        EXPECT_NEAR(2.0f, key.scale.X, kTolerance)
            << "the unanimated scale fell back to a default instead of the bind pose -- a scale of "
               "1 here would be wrong, and a scale of 0 would collapse the bone to a point";
        EXPECT_NEAR(3.0f, key.scale.Y, kTolerance);
        EXPECT_NEAR(4.0f, key.scale.Z, kTolerance);
    }
}

// --- GLTF-311: a single-keyframe channel ---------------------------------------------------------

TEST(GltfAnimationSampling, ASingleKeyframeChannelIsAConstantPoseNotAnEmptyTrack)
{
    // One sample is a legal channel and means "hold this value" -- a common way to author a static
    // offset alongside animated siblings. Every bracket-finding routine has an off-by-one waiting
    // in it for a one-element array: reading `keys[i + 1]` walks off the end, and returning nothing
    // drops the pose entirely.
    Parsed parsed;
    ASSERT_TRUE(Parse(parsed, AnimatedJointDocument(
        "LINEAR", {0.5f}, {7.0f, 8.0f, 9.0f})));

    const std::vector<ClipOut> clips = ClipsOf(parsed);
    ASSERT_EQ(1u, clips.size());
    ASSERT_EQ(1u, clips[0].tracks.size());
    const TrackOut& track = clips[0].tracks[0];
    ASSERT_FALSE(track.keys.empty()) << "a one-sample channel produced no track at all";

    for (const KeyframeOut& key : track.keys)
    {
        EXPECT_NEAR(7.0f, key.translation.X, kTolerance);
        EXPECT_NEAR(8.0f, key.translation.Y, kTolerance);
        EXPECT_NEAR(9.0f, key.translation.Z, kTolerance);
    }
}

// --- GLTF-302: CUBICSPLINE's tangent packing -----------------------------------------------------

TEST(GltfAnimationSampling, CubicSplineReadsTheValueOfEachTripletNotTheInTangent)
{
    // §3.6 packs a CUBICSPLINE output as (inTangent, value, outTangent) per sample -- three times
    // as many elements as a LINEAR channel with the same times. Reading it as a flat value array
    // takes the FIRST element of each triplet, which is the in-tangent: an animation driven by
    // slopes instead of positions, which is wrong in a way that still moves and still loops.
    //
    // The in-tangents here are deliberately huge and the values small, so the two readings cannot
    // be confused.
    Parsed parsed;
    ASSERT_TRUE(Parse(parsed, AnimatedJointDocument(
        "CUBICSPLINE", {0.0f, 1.0f},
        {  // sample 0: inTangent, value, outTangent
           100.0f, 0.0f, 0.0f,   1.0f, 0.0f, 0.0f,   0.0f, 0.0f, 0.0f,
           // sample 1
           0.0f, 0.0f, 0.0f,     2.0f, 0.0f, 0.0f,   -100.0f, 0.0f, 0.0f})));

    const std::vector<ClipOut> clips = ClipsOf(parsed);
    ASSERT_EQ(1u, clips.size());
    ASSERT_EQ(1u, clips[0].tracks.size());
    const TrackOut& track = clips[0].tracks[0];

    const KeyframeOut* start = KeyAt(track, 0.0);
    const KeyframeOut* end = KeyAt(track, 1.0);
    ASSERT_NE(nullptr, start);
    ASSERT_NE(nullptr, end);
    EXPECT_NEAR(1.0f, start->translation.X, kTolerance)
        << "100 here would mean the in-tangent was read as the value";
    EXPECT_NEAR(2.0f, end->translation.X, kTolerance);
}
