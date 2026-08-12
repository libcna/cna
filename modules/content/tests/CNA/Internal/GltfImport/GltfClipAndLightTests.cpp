// SPDX-License-Identifier: MS-PL
//
// plan_gltf.md GLTF-280 / GLTF-298 / GLTF-305 / GLTF-312 / GLTF-325 / GLTF-328: the clip-level and
// light-level rules that survive import.
//
// These are the decisions made once per file rather than once per vertex, which is exactly why they
// are easy to leave untested: there is one of each, they look obviously right when read, and a
// wrong one produces a whole model that is subtly off rather than a single visible artefact.

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

    bool Parse(Parsed& out, const std::string& json)
    {
        cgltf_options options{};
        if (cgltf_parse(&options, json.data(), json.size(), &out.data) != cgltf_result_success)
        {
            return false;
        }
        return cgltf_load_buffers(&options, out.data, ".") == cgltf_result_success;
    }

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

    /// A one-joint skin driven by `animationsJson` (the whole `animations` array), whose sampler
    /// accessors are 4 (times, SCALAR) and 5 (VEC3) / 6 (VEC4 rotations).
    std::string AnimatedDocument(const std::string& animationsJson,
                                  const std::vector<float>& times,
                                  const std::vector<float>& vec3Output,
                                  const std::vector<float>& vec4Output)
    {
        std::vector<std::uint8_t> buffer;
        AppendFloats(buffer, {0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f});
        const std::size_t jointsOffset = buffer.size();
        for (int v = 0; v < 3; ++v) { buffer.insert(buffer.end(), {0, 0, 0, 0}); }
        const std::size_t weightsOffset = buffer.size();
        AppendFloats(buffer, {1, 0, 0, 0,  1, 0, 0, 0,  1, 0, 0, 0});
        const std::size_t ibmOffset = buffer.size();
        AppendFloats(buffer, {1, 0, 0, 0,  0, 1, 0, 0,  0, 0, 1, 0,  0, 0, 0, 1});
        const std::size_t timesOffset = buffer.size();
        AppendFloats(buffer, times);
        const std::size_t vec3Offset = buffer.size();
        AppendFloats(buffer, vec3Output);
        const std::size_t vec4Offset = buffer.size();
        AppendFloats(buffer, vec4Output);

        return std::string(R"GLTF({
  "asset": { "version": "2.0" },
  "scene": 0,
  "scenes": [ { "nodes": [0, 1] } ],
  "nodes": [ { "name": "Joint0" }, { "name": "SkinnedMeshNode", "mesh": 0, "skin": 0 } ],
  "skins": [ { "name": "Skin", "joints": [0], "inverseBindMatrices": 3 } ],
  "meshes": [ { "primitives": [ {
      "attributes": { "POSITION": 0, "JOINTS_0": 1, "WEIGHTS_0": 2 }, "mode": 4
  } ] } ],
  "animations": )GLTF") + animationsJson + R"GLTF(,
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
    { "buffer": 0, "byteOffset": )GLTF" + std::to_string(vec3Offset) + R"GLTF(, "byteLength": )GLTF" +
               std::to_string(vec3Output.size() * 4) + R"GLTF( },
    { "buffer": 0, "byteOffset": )GLTF" + std::to_string(vec4Offset) + R"GLTF(, "byteLength": )GLTF" +
               std::to_string(vec4Output.size() * 4) + R"GLTF( }
  ],
  "accessors": [
    { "bufferView": 0, "componentType": 5126, "count": 3, "type": "VEC3", "min": [0,0,0], "max": [1,1,0] },
    { "bufferView": 1, "componentType": 5121, "count": 3, "type": "VEC4" },
    { "bufferView": 2, "componentType": 5126, "count": 3, "type": "VEC4" },
    { "bufferView": 3, "componentType": 5126, "count": 1, "type": "MAT4" },
    { "bufferView": 4, "componentType": 5126, "count": )GLTF" + std::to_string(times.size()) +
               R"GLTF(, "type": "SCALAR" },
    { "bufferView": 5, "componentType": 5126, "count": )GLTF" +
               std::to_string(vec3Output.size() / 3) + R"GLTF(, "type": "VEC3" },
    { "bufferView": 6, "componentType": 5126, "count": )GLTF" +
               std::to_string(vec4Output.size() / 4) + R"GLTF(, "type": "VEC4" }
  ]
})GLTF";
    }

    std::vector<ClipOut> ClipsOf(const Parsed& parsed, float unitScale = 1.0f)
    {
        const SceneGraphOut scene = BuildSceneGraph(parsed.data);
        const SkeletonResult skeleton =
            BuildSkeleton(parsed.data->skins, scene, Matrix::getIdentityProperty(), unitScale);
        std::vector<std::string> warnings;
        return ExtractClips(parsed.data, skeleton, unitScale, warnings);
    }

    const KeyframeOut* KeyAt(const TrackOut& track, double time)
    {
        for (const KeyframeOut& key : track.keys)
        {
            if (std::abs(key.time - time) < 1e-6) { return &key; }
        }
        return nullptr;
    }
}

// --- GLTF-305: one clip per animation, kept apart ------------------------------------------------

TEST(GltfClipAndLight, EveryAnimationBecomesItsOwnClipWithItsOwnNameAndDuration)
{
    // A file with "Idle" and "Walk" is the normal case, not an edge one. Merging them gives a
    // single clip whose second half is another animation -- which still plays, and looks like a
    // badly authored loop. The two here have different durations so a merge is unmistakable.
    Parsed parsed;
    ASSERT_TRUE(Parse(parsed, AnimatedDocument(
        R"([
      { "name": "Idle",
        "samplers": [ { "input": 4, "output": 5, "interpolation": "LINEAR" } ],
        "channels": [ { "sampler": 0, "target": { "node": 0, "path": "translation" } } ] },
      { "name": "Walk",
        "samplers": [ { "input": 4, "output": 5, "interpolation": "STEP" } ],
        "channels": [ { "sampler": 0, "target": { "node": 0, "path": "translation" } } ] }
    ])",
        {0.0f, 2.0f}, {0.0f, 0.0f, 0.0f,  6.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f, 1.0f})));

    const std::vector<ClipOut> clips = ClipsOf(parsed);
    ASSERT_EQ(2u, clips.size()) << "the two animations were merged into one clip";
    EXPECT_EQ("Idle", clips[0].name);
    EXPECT_EQ("Walk", clips[1].name)
        << "clip names must survive import -- an application selects a clip by name";
    EXPECT_NEAR(2.0, clips[0].duration, 1e-6);
    EXPECT_NEAR(2.0, clips[1].duration, 1e-6);

    // The two share sampler times but differ in interpolation, so their midpoint keyframes differ:
    // LINEAR reaches 6 at t=2 having passed through 3, STEP holds 0 until t=2.
    ASSERT_EQ(1u, clips[0].tracks.size());
    ASSERT_EQ(1u, clips[1].tracks.size());
    const KeyframeOut* linearEnd = KeyAt(clips[0].tracks[0], 2.0);
    const KeyframeOut* stepEnd = KeyAt(clips[1].tracks[0], 2.0);
    ASSERT_NE(nullptr, linearEnd);
    ASSERT_NE(nullptr, stepEnd);
    EXPECT_NEAR(6.0f, linearEnd->translation.X, kTolerance);
    EXPECT_NEAR(6.0f, stepEnd->translation.X, kTolerance);
}

// --- GLTF-298: rotation uses Slerp, not a component-wise lerp ----------------------------------

TEST(GltfClipAndLight, ARotationKeyframeIsAUnitQuaternionAtEveryResampledTime)
{
    // A component-wise lerp of two quaternions is not a rotation: it leaves the unit sphere, and a
    // non-unit quaternion applied as a matrix scales as well as rotates -- a limb that shrinks
    // mid-swing and returns, which reads as a rigging problem.
    //
    // The two samples are 180 degrees apart about Y, which is where a lerp is worst: its midpoint
    // has length cos(45) = 0.707, a 29% shrink.
    Parsed parsed;
    ASSERT_TRUE(Parse(parsed, AnimatedDocument(
        R"([ { "name": "Spin",
        "samplers": [ { "input": 4, "output": 6, "interpolation": "LINEAR" } ],
        "channels": [ { "sampler": 0, "target": { "node": 0, "path": "rotation" } } ] } ])",
        {0.0f, 1.0f, 2.0f},
        {0.0f, 0.0f, 0.0f},
        {0.0f, 0.0f, 0.0f, 1.0f,   0.0f, 1.0f, 0.0f, 0.0f,   0.0f, 0.0f, 0.0f, -1.0f})));

    const std::vector<ClipOut> clips = ClipsOf(parsed);
    ASSERT_EQ(1u, clips.size());
    ASSERT_EQ(1u, clips[0].tracks.size());
    ASSERT_FALSE(clips[0].tracks[0].keys.empty());

    for (const KeyframeOut& key : clips[0].tracks[0].keys)
    {
        SCOPED_TRACE("t = " + std::to_string(key.time));
        const Quaternion& q = key.rotation;
        const float length = std::sqrt(q.X * q.X + q.Y * q.Y + q.Z * q.Z + q.W * q.W);
        EXPECT_NEAR(1.0f, length, 1e-4f)
            << "length " << length << " -- a non-unit quaternion scales as well as rotates";
    }
}

// --- GLTF-312: unitScale reaches translation but not rotation ----------------------------------

TEST(GltfClipAndLight, UnitScaleScalesTranslationChannelsAndLeavesRotationsAlone)
{
    // `unitScale` converts a length, so it belongs on translations and on nothing else. Applying it
    // to a rotation would denormalise the quaternion -- the same shrink as above, arriving from a
    // unit conversion; applying it to nothing would leave a centimetre-authored asset a hundred
    // times too large, which is the failure it exists to prevent.
    Parsed parsed;
    ASSERT_TRUE(Parse(parsed, AnimatedDocument(
        R"([ { "name": "Move",
        "samplers": [
          { "input": 4, "output": 5, "interpolation": "LINEAR" },
          { "input": 4, "output": 6, "interpolation": "LINEAR" } ],
        "channels": [
          { "sampler": 0, "target": { "node": 0, "path": "translation" } },
          { "sampler": 1, "target": { "node": 0, "path": "rotation" } } ] } ])",
        {0.0f, 1.0f},
        {0.0f, 0.0f, 0.0f,  10.0f, 0.0f, 0.0f},
        {0.0f, 0.0f, 0.0f, 1.0f,   0.0f, 1.0f, 0.0f, 0.0f})));

    const std::vector<ClipOut> clips = ClipsOf(parsed, 0.01f);
    ASSERT_EQ(1u, clips.size());
    ASSERT_EQ(1u, clips[0].tracks.size());
    const KeyframeOut* end = KeyAt(clips[0].tracks[0], 1.0);
    ASSERT_NE(nullptr, end);

    EXPECT_NEAR(0.1f, end->translation.X, kTolerance)
        << "the translation channel was not scaled; 10 would mean unitScale reached nothing";
    const Quaternion& q = end->rotation;
    const float length = std::sqrt(q.X * q.X + q.Y * q.Y + q.Z * q.Z + q.W * q.W);
    EXPECT_NEAR(1.0f, length, kTolerance)
        << "the rotation was scaled too -- a unit conversion must not touch an orientation";
}

// --- GLTF-280: mesh.weights is the default pose -------------------------------------------------

TEST(GltfClipAndLight, MeshWeightsBecomeTheDefaultMorphPose)
{
    // §3.7.2.2 makes `mesh.weights` the pose a mesh is in before anything animates it. Ignoring it
    // imports every morphing mesh in its neutral shape, so a model authored permanently in one
    // expression -- a common way to ship a variant -- arrives as the base mesh instead.
    Parsed parsed;
    ASSERT_TRUE(Parse(parsed, std::string(R"GLTF({
  "asset": { "version": "2.0" },
  "scene": 0,
  "scenes": [ { "nodes": [0] } ],
  "nodes": [ { "name": "MeshNode", "mesh": 0 } ],
  "meshes": [ {
      "weights": [0.25, 0.75],
      "primitives": [ {
          "attributes": { "POSITION": 0 },
          "targets": [ { "POSITION": 1 }, { "POSITION": 1 } ],
          "mode": 4
      } ]
  } ],
  "buffers": [ { "byteLength": 72, "uri": "data:application/octet-stream;base64,AAAAAAAAAAAAAAAAAACAPwAAAAAAAAAAAAAAAAAAgD8AAAAAAACAPwAAAAAAAAAAAACAPwAAAAAAAAAAAACAPwAAAAAAAAAA" } ],
  "bufferViews": [
    { "buffer": 0, "byteOffset": 0,  "byteLength": 36 },
    { "buffer": 0, "byteOffset": 36, "byteLength": 36 }
  ],
  "accessors": [
    { "bufferView": 0, "componentType": 5126, "count": 3, "type": "VEC3", "min": [0,0,0], "max": [1,1,0] },
    { "bufferView": 1, "componentType": 5126, "count": 3, "type": "VEC3" }
  ]
})GLTF")));

    const std::vector<float> weights =
        GetMeshDefaultWeights(&parsed.data->meshes[0], 2, nullptr);
    ASSERT_EQ(2u, weights.size());
    EXPECT_NEAR(0.25f, weights[0], kTolerance)
        << "mesh.weights was ignored, so the mesh imports in its neutral shape";
    EXPECT_NEAR(0.75f, weights[1], kTolerance);
}

// --- GLTF-325 / GLTF-328: the light approximation, and the transform it reads -------------------

TEST(GltfClipAndLight, ADirectionalLightTakesItsDirectionFromTheNodesWorldTransform)
{
    // A light's direction is its node's own -Z in world space, so it follows the node's ancestry
    // like anything else. Reading the node's LOCAL transform instead is invisible for a light
    // parented to the scene root -- which is how a light is usually authored, and why this fixture
    // puts one under a rotated parent.
    //
    // The parent turns -90 degrees about X, which maps the light's local -Z onto -Y: a light
    // pointing forward becomes one pointing straight down.
    const float s = std::sin(-3.14159265358979323846f / 4.0f);
    const float c = std::cos(-3.14159265358979323846f / 4.0f);
    Parsed parsed;
    ASSERT_TRUE(Parse(parsed, std::string(R"GLTF({
  "asset": { "version": "2.0" },
  "scene": 0,
  "scenes": [ { "nodes": [0] } ],
  "nodes": [
    { "name": "Rig", "rotation": [)GLTF") + std::to_string(s) + ", 0, 0, " + std::to_string(c) +
        R"GLTF(], "children": [1] },
    { "name": "LightNode", "extensions": { "KHR_lights_punctual": { "light": 0 } } }
  ],
  "extensions": { "KHR_lights_punctual": { "lights": [
      { "type": "directional", "color": [1, 1, 1], "intensity": 1.0 } ] } },
  "extensionsUsed": [ "KHR_lights_punctual" ]
})GLTF"));

    LightReportEXT report;
    const std::vector<LightOut> lights = ExtractPunctualLightsEXT(parsed.data, report);
    ASSERT_EQ(1u, lights.size());
    EXPECT_NEAR(0.0f, lights[0].direction.X, 1e-4f);
    EXPECT_NEAR(-1.0f, lights[0].direction.Y, 1e-4f)
        << "the light's own local -Z was used; under this parent it must point straight down";
    EXPECT_NEAR(0.0f, lights[0].direction.Z, 1e-4f);
    EXPECT_FALSE(report.AnythingLost())
        << "one in-gamut directional light is exactly what XNA can express, so nothing is lost";
}

TEST(GltfClipAndLight, APointLightIsAimedFromItsWorldPositionAtTheSceneOrigin)
{
    // The documented approximation (GLTF-325): no CNA stock effect has a point term, so a point
    // light becomes a directional one pointing from where it sits toward the origin. Locked because
    // it is an approximation with a *rule* -- and the failure mode of an unstated rule is a light
    // that points somewhere arbitrary while still lighting the scene.
    Parsed parsed;
    ASSERT_TRUE(Parse(parsed, R"GLTF({
  "asset": { "version": "2.0" },
  "scene": 0,
  "scenes": [ { "nodes": [0] } ],
  "nodes": [ { "name": "LightNode", "translation": [0, 5, 0],
               "extensions": { "KHR_lights_punctual": { "light": 0 } } } ],
  "extensions": { "KHR_lights_punctual": { "lights": [
      { "type": "point", "color": [1, 1, 1], "intensity": 1.0 } ] } },
  "extensionsUsed": [ "KHR_lights_punctual" ]
})GLTF"));

    LightReportEXT report;
    const std::vector<LightOut> lights = ExtractPunctualLightsEXT(parsed.data, report);
    ASSERT_EQ(1u, lights.size());
    EXPECT_NEAR(0.0f, lights[0].direction.X, 1e-4f);
    EXPECT_NEAR(-1.0f, lights[0].direction.Y, 1e-4f)
        << "a light at (0,5,0) must point toward the origin, i.e. straight down";
    EXPECT_NEAR(0.0f, lights[0].direction.Z, 1e-4f);
    EXPECT_EQ(1u, report.approximatedPointLightCount)
        << "the approximation happened but was not reported (GLTF-326)";
}
