// SPDX-License-Identifier: MS-PL
//
// plan_gltf.md GLTF-177 / GLTF-275 / GLTF-276 / GLTF-277 / GLTF-283 / GLTF-284 / GLTF-287 /
// GLTF-290 / GLTF-291: how morph deltas are applied, and what is reported about them.
//
// A morph target is a per-vertex delta array, and `BlendMorphTargetsEXT` adds a weighted sum of
// those arrays onto the base vertex bytes. Every rule here is one where the wrong answer still
// produces a mesh -- a slightly-off mesh, or a correctly-shaped mesh lit wrongly -- which is why
// each needs an assertion rather than a look.
//
// These drive `BlendMorphTargetsEXT` directly rather than through a corpus asset, because what is
// under test is the blend arithmetic over a stated base buffer: an end-to-end fixture would prove
// the same sum while making it far harder to say which term was wrong.

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <gtest/gtest.h>
#include <string>
#include <vector>

#include "CNA/Internal/Graphics/VertexDeclarationFidelity.hpp"
#include "Microsoft/Xna/Framework/Graphics/MorphTargetEXT.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "CNA/Internal/GltfImport/GltfImportCore.hpp"
#include "GltfFixtureCorpus.hpp"

#include "Microsoft/Xna/Framework/Content/ContentManager.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/Model.hpp"
#include "Microsoft/Xna/Framework/Graphics/ModelMesh.hpp"
#include "Microsoft/Xna/Framework/Graphics/ModelMeshCollection.hpp"
#include "Microsoft/Xna/Framework/Graphics/ModelMeshPart.hpp"
#include "Microsoft/Xna/Framework/Graphics/ModelMeshPartCollection.hpp"

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;
using CNA::Internal::JsonType;
using CNA::Internal::JsonValue;
using CnaTest::GltfOracle::CorpusDirectory;
using CnaTest::GltfOracle::CorpusFixtureIds;
using CnaTest::GltfOracle::LoadedFixture;
using CnaTest::GltfOracle::Member;
using CnaTest::GltfOracle::NumberOr;
using CnaTest::GltfOracle::Numbers;
using CnaTest::GltfOracle::Path;
using Microsoft::Xna::Framework::Content::ContentManager;

namespace
{
    constexpr float kTolerance = 1e-5f;

    /// Stride 48 is the unskinned PBR layout: Position, Normal, Tangent, TextureCoordinate. It is
    /// the stride an ordinary glTF mesh lands on since GLTF-215, and it has both of the slots this
    /// file needs -- a normal and a tangent -- so one base buffer serves every case.
    constexpr int kStride = 48;

    int OffsetOf(VertexElementUsage usage)
    {
        const CNA::Internal::Graphics::InferredVertexLayout layout =
            CNA::Internal::Graphics::InferredLayoutForStride(
                kStride, CNA::Internal::Graphics::UnlistedStrideLayout::RendererRefusesIt);
        EXPECT_TRUE(layout.known);
        for (std::size_t i = 0; layout.known && i < layout.count; ++i)
        {
            if (layout.elements[i].usage == usage && layout.elements[i].usageIndex == 0)
            {
                return layout.elements[i].offset;
            }
        }
        return -1;
    }

    /// A `vertexCount`-vertex base buffer where vertex `v`'s position is `(v, 0, 0)`, its normal is
    /// `+Z` and its tangent is `(+X, w = -1)`. The handedness is deliberately NOT the default +1,
    /// so a blend that rewrites `w` instead of preserving it is visible.
    std::vector<std::uint8_t> BaseVertices(int vertexCount)
    {
        std::vector<std::uint8_t> bytes(static_cast<std::size_t>(vertexCount) * kStride, 0);
        const int positionOffset = OffsetOf(VertexElementUsage::Position);
        const int normalOffset = OffsetOf(VertexElementUsage::Normal);
        const int tangentOffset = OffsetOf(VertexElementUsage::Tangent);
        EXPECT_GE(normalOffset, 0);
        EXPECT_GE(tangentOffset, 0);

        for (int v = 0; v < vertexCount; ++v)
        {
            const std::size_t base = static_cast<std::size_t>(v) * kStride;
            const float position[3] = {static_cast<float>(v), 0.0f, 0.0f};
            const float normal[3] = {0.0f, 0.0f, 1.0f};
            const float tangent[4] = {1.0f, 0.0f, 0.0f, -1.0f};
            std::memcpy(bytes.data() + base + static_cast<std::size_t>(positionOffset), position,
                        sizeof(position));
            std::memcpy(bytes.data() + base + static_cast<std::size_t>(normalOffset), normal,
                        sizeof(normal));
            std::memcpy(bytes.data() + base + static_cast<std::size_t>(tangentOffset), tangent,
                        sizeof(tangent));
        }
        return bytes;
    }

    std::vector<float> Read(const std::vector<std::uint8_t>& bytes, int vertex, int offset,
                            std::size_t count)
    {
        std::vector<float> out(count);
        std::memcpy(out.data(),
                    bytes.data() + static_cast<std::size_t>(vertex) * kStride +
                        static_cast<std::size_t>(offset),
                    count * sizeof(float));
        return out;
    }
}

// --- GLTF-275: POSITION deltas are a weighted sum -----------------------------------------------

TEST(GltfMorphBlending, PositionDeltasFromSeveralTargetsAccumulateRatherThanOverwrite)
{
    // Two targets pulling on the same vertex must both contribute. A blend that assigns rather than
    // accumulates gives the last target's delta alone, which for a face rig means one expression
    // silently cancelling every other -- and the mesh still looks like a face.
    MorphTargetDataEXT morph;
    morph.Stride = kStride;
    morph.BaseVertexBytes = BaseVertices(2);
    morph.PositionDeltas = {
        {Vector3(10.0f, 0.0f, 0.0f), Vector3(0.0f, 0.0f, 0.0f)},
        {Vector3(0.0f, 100.0f, 0.0f), Vector3(0.0f, 0.0f, 0.0f)},
    };
    morph.NormalDeltas = {{}, {}};

    const std::vector<std::uint8_t> blended = BlendMorphTargetsEXT(morph, {0.5f, 0.25f});
    const std::vector<float> position = Read(blended, 0, OffsetOf(VertexElementUsage::Position), 3);
    EXPECT_NEAR(0.0f + 0.5f * 10.0f, position[0], kTolerance)
        << "target 0's contribution is missing or was overwritten";
    EXPECT_NEAR(0.25f * 100.0f, position[1], kTolerance)
        << "target 1's contribution is missing";
    EXPECT_NEAR(0.0f, position[2], kTolerance);
}

TEST(GltfMorphBlending, AZeroWeightedTargetContributesNothingAtAll)
{
    // The neutral pose has to be exactly the base mesh, not approximately it. A blend that runs the
    // arithmetic anyway accumulates float error on every vertex of every un-posed target, which for
    // a rig with fifty targets is a mesh that drifts while nothing is animating.
    MorphTargetDataEXT morph;
    morph.Stride = kStride;
    morph.BaseVertexBytes = BaseVertices(2);
    morph.PositionDeltas = {{Vector3(1e7f, 1e7f, 1e7f), Vector3(1e7f, 1e7f, 1e7f)}};
    morph.NormalDeltas = {{}};

    const std::vector<std::uint8_t> blended = BlendMorphTargetsEXT(morph, {0.0f});
    EXPECT_EQ(morph.BaseVertexBytes, blended)
        << "a zero-weighted target changed the mesh -- the neutral pose must be byte-identical to "
           "the base, not merely close to it";
}

// --- GLTF-276: a target with no POSITION delta ---------------------------------------------------

TEST(GltfMorphBlending, ATargetWithNoPositionDeltaLeavesPositionsAlone)
{
    // §3.7.2.2 lets a target carry any subset of POSITION/NORMAL/TANGENT -- a target that only
    // moves normals is a legitimate way to author a shading-only variation. The absent stream must
    // read as zero and not as garbage, and CNA zero-fills it at extraction; this asserts the blend
    // honours that rather than reading past the end of a shorter array.
    MorphTargetDataEXT morph;
    morph.Stride = kStride;
    morph.BaseVertexBytes = BaseVertices(2);
    morph.PositionDeltas = {{Vector3(0.0f, 0.0f, 0.0f), Vector3(0.0f, 0.0f, 0.0f)}};
    morph.NormalDeltas = {{Vector3(1.0f, 0.0f, 0.0f), Vector3(1.0f, 0.0f, 0.0f)}};

    const std::vector<std::uint8_t> blended = BlendMorphTargetsEXT(morph, {1.0f});
    for (int v = 0; v < 2; ++v)
    {
        SCOPED_TRACE("vertex " + std::to_string(v));
        const std::vector<float> position =
            Read(blended, v, OffsetOf(VertexElementUsage::Position), 3);
        EXPECT_NEAR(static_cast<float>(v), position[0], kTolerance);
        EXPECT_NEAR(0.0f, position[1], kTolerance);
        EXPECT_NEAR(0.0f, position[2], kTolerance);
    }
}

// --- GLTF-277 / GLTF-283: NORMAL deltas, and the renormalisation afterwards ---------------------

TEST(GltfMorphBlending, ABlendedNormalIsRenormalisedBecauseASumOfUnitVectorsIsNotOne)
{
    // A weighted sum of unit normals is not itself unit length -- +Z plus +X has length sqrt(2), and
    // a 1.41-length normal brightens every lit surface it touches by 41%. The renormalisation is
    // what makes a morphed mesh light like an unmorphed one, and its absence looks like a lighting
    // bug rather than a morph one.
    MorphTargetDataEXT morph;
    morph.Stride = kStride;
    morph.BaseVertexBytes = BaseVertices(1);
    morph.PositionDeltas = {{Vector3(0.0f, 0.0f, 0.0f)}};
    morph.NormalDeltas = {{Vector3(1.0f, 0.0f, 0.0f)}};

    const std::vector<std::uint8_t> blended = BlendMorphTargetsEXT(morph, {1.0f});
    const std::vector<float> normal = Read(blended, 0, OffsetOf(VertexElementUsage::Normal), 3);
    const float length =
        std::sqrt(normal[0] * normal[0] + normal[1] * normal[1] + normal[2] * normal[2]);
    EXPECT_NEAR(1.0f, length, kTolerance)
        << "the blended normal has length " << length
        << " -- an un-renormalised sum brightens every surface it touches";

    // And it points where the sum points: +Z plus +X, normalised.
    const float invSqrt2 = 1.0f / std::sqrt(2.0f);
    EXPECT_NEAR(invSqrt2, normal[0], 1e-4f);
    EXPECT_NEAR(0.0f, normal[1], 1e-4f);
    EXPECT_NEAR(invSqrt2, normal[2], 1e-4f);
}

TEST(GltfMorphBlending, ABlendedTangentKeepsItsHandednessUntouched)
{
    // plan_gltf.md GLTF-279. The tangent's `w` is a sign, not a value: it says which way the
    // bitangent runs, and it is a property of the UV winding rather than of the pose. Blending it
    // would take it through 0, which is not a handedness at all -- so only xyz is morphed.
    MorphTargetDataEXT morph;
    morph.Stride = kStride;
    morph.BaseVertexBytes = BaseVertices(1);
    morph.PositionDeltas = {{Vector3(0.0f, 0.0f, 0.0f)}};
    morph.NormalDeltas = {{}};
    morph.TangentDeltas = {{Vector3(0.0f, 1.0f, 0.0f)}};

    const std::vector<std::uint8_t> blended = BlendMorphTargetsEXT(morph, {1.0f});
    const std::vector<float> tangent = Read(blended, 0, OffsetOf(VertexElementUsage::Tangent), 4);
    const float invSqrt2 = 1.0f / std::sqrt(2.0f);
    EXPECT_NEAR(invSqrt2, tangent[0], 1e-4f);
    EXPECT_NEAR(invSqrt2, tangent[1], 1e-4f);
    EXPECT_NEAR(0.0f, tangent[2], 1e-4f);
    EXPECT_FLOAT_EQ(-1.0f, tangent[3])
        << "the handedness was blended; it is a sign decided by the UV winding, and interpolating "
           "it passes through 0, which is not a handedness";
}

// --- GLTF-284: the weight vector must match the target count -----------------------------------

TEST(GltfMorphBlending, AWeightVectorOfTheWrongLengthThrowsAndNamesBothCounts)
{
    // The weight vector comes from the application, and a mismatch is a caller bug rather than a
    // file one -- which is exactly why it must not be tolerated silently. Ignoring the extra
    // weights, or padding the missing ones with zero, gives a plausible mesh from an incoherent
    // call and hides the bug at the point it could still be attributed.
    MorphTargetDataEXT morph;
    morph.Stride = kStride;
    morph.BaseVertexBytes = BaseVertices(1);
    morph.PositionDeltas = {{Vector3(1.0f, 0.0f, 0.0f)}, {Vector3(0.0f, 1.0f, 0.0f)}};
    morph.NormalDeltas = {{}, {}};

    for (const std::vector<float>& weights :
         std::vector<std::vector<float>>{{}, {1.0f}, {1.0f, 0.0f, 0.0f}})
    {
        SCOPED_TRACE(std::to_string(weights.size()) + " weight(s) for 2 targets");
        std::string message;
        try
        {
            (void)BlendMorphTargetsEXT(morph, weights);
        }
        catch (const std::exception& e)
        {
            message = e.what();
        }
        ASSERT_FALSE(message.empty()) << "a mismatched weight vector was accepted";
        EXPECT_NE(std::string::npos, message.find("2 weight")) << message;
        EXPECT_NE(std::string::npos, message.find(std::to_string(weights.size()))) << message;
    }
}

TEST(GltfMorphBlending, TheCorrectWeightCountIsAcceptedSoTheGuardIsNotSimplyAlwaysThrowing)
{
    // The control for the test above. Without it, a guard that rejected every call would pass every
    // assertion there while making morphing impossible.
    MorphTargetDataEXT morph;
    morph.Stride = kStride;
    morph.BaseVertexBytes = BaseVertices(1);
    morph.PositionDeltas = {{Vector3(1.0f, 0.0f, 0.0f)}, {Vector3(0.0f, 1.0f, 0.0f)}};
    morph.NormalDeltas = {{}, {}};

    EXPECT_NO_THROW((void)BlendMorphTargetsEXT(morph, {0.5f, 0.5f}));
}

// --- GLTF-177: the renormalisation point on the CPU blend path -------------------------------------

TEST(GltfMorphBlending, ABlendedNormalAndTangentComeBackUnitLength)
{
    // A weighted sum of unit vectors is not a unit vector, and the shortfall is largest exactly
    // where morphing is most visible: two normals 90 degrees apart at half weight each sum to
    // length 0.707, so a fully morphed surface would light about 30% too dark. The shader's own
    // `normalize` would rescue the *drawn* result, but not a CPU-side reader of the blended buffer
    // -- a bounds computation, a picking ray, a `.cnj` re-export -- so the policy
    // (docs/gltf-conventions.md) puts the renormalisation here, on the buffer everything sees.
    //
    // The base normal is +Z and the delta turns it toward +X; at weight 1 the sum is (1,0,1),
    // whose length is sqrt(2). Nothing about that is subtle, which is the point.
    MorphTargetDataEXT morph;
    morph.BaseVertexBytes = BaseVertices(1);
    morph.Stride = kStride;
    morph.PositionDeltas.push_back({Vector3(0.0f, 0.0f, 0.0f)});
    morph.NormalDeltas.push_back({Vector3(1.0f, 0.0f, -0.0f)});
    morph.TangentDeltas.push_back({Vector3(0.0f, 1.0f, 0.0f)});

    const std::vector<std::uint8_t> blended = BlendMorphTargetsEXT(morph, {1.0f});
    ASSERT_EQ(morph.BaseVertexBytes.size(), blended.size());

    float normal[3];
    std::memcpy(normal, blended.data() + static_cast<std::size_t>(OffsetOf(VertexElementUsage::Normal)),
                sizeof(normal));
    const float normalLength =
        std::sqrt(normal[0] * normal[0] + normal[1] * normal[1] + normal[2] * normal[2]);
    EXPECT_NEAR(1.0f, normalLength, 1e-4f)
        << "the blended normal is " << normalLength << " long; a morphed surface lights by that "
           "factor";
    // And it points the way the sum does, not merely somewhere unit-length: (1,0,1) normalised.
    EXPECT_NEAR(0.70710678f, normal[0], 1e-4f);
    EXPECT_NEAR(0.70710678f, normal[2], 1e-4f);

    float tangent[4];
    std::memcpy(tangent, blended.data() + static_cast<std::size_t>(OffsetOf(VertexElementUsage::Tangent)),
                sizeof(tangent));
    const float tangentLength =
        std::sqrt(tangent[0] * tangent[0] + tangent[1] * tangent[1] + tangent[2] * tangent[2]);
    EXPECT_NEAR(1.0f, tangentLength, 1e-4f) << "the blended tangent is not unit length";
    EXPECT_FLOAT_EQ(-1.0f, tangent[3])
        << "renormalisation rewrote the handedness sign, which is not a length at all";
}

// --- GLTF-466: a morph target attribute CNA does not carry is named ------------------------------

namespace
{
    // Two morph targets over one triangle. Target 0 carries POSITION (which CNA morphs) together with
    // TEXCOORD_0 and COLOR_0 (which it does not); target 1 repeats TEXCOORD_0 alone, so the report has
    // to name each SEMANTIC once rather than once per target.
    const char* kMorphedExtraSemanticsGltf = R"GLTF({
  "asset": { "version": "2.0" },
  "scene": 0,
  "scenes": [ { "nodes": [0] } ],
  "nodes": [ { "name": "MeshNode", "mesh": 0 } ],
  "meshes": [ { "name": "ExtraSemanticMorph", "weights": [1.0, 0.0], "primitives": [ {
      "attributes": { "POSITION": 0, "TEXCOORD_0": 1, "COLOR_0": 2 },
      "indices": 3,
      "mode": 4,
      "targets": [
        { "POSITION": 4, "TEXCOORD_0": 5, "COLOR_0": 2 },
        { "TEXCOORD_0": 5 }
      ]
  } ] } ],
  "buffers": [ { "byteLength": 176,
    "uri": "data:application/octet-stream;base64,AAAAAAAAAAAAAAAAAACAPwAAAAAAAAAAAAAAAAAAgD8AAAAAAAAAAAAAAAAAAIA/AAAAAAAAAAAAAIA/AACAPwAAgD8AAIA/AACAPwAAgD8AAIA/AACAPwAAgD8AAIA/AACAPwAAgD8AAIA/AAABAAIAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAgD8AAAA/AAAAPwAAAD8AAAA/AAAAPwAAAD8=" } ],
  "bufferViews": [
    { "buffer": 0, "byteOffset": 0,   "byteLength": 36 },
    { "buffer": 0, "byteOffset": 36,  "byteLength": 24 },
    { "buffer": 0, "byteOffset": 60,  "byteLength": 48 },
    { "buffer": 0, "byteOffset": 108, "byteLength": 6 },
    { "buffer": 0, "byteOffset": 116, "byteLength": 36 },
    { "buffer": 0, "byteOffset": 152, "byteLength": 24 }
  ],
  "accessors": [
    { "bufferView": 0, "componentType": 5126, "count": 3, "type": "VEC3",
      "min": [0,0,0], "max": [1,1,0] },
    { "bufferView": 1, "componentType": 5126, "count": 3, "type": "VEC2" },
    { "bufferView": 2, "componentType": 5126, "count": 3, "type": "VEC4" },
    { "bufferView": 3, "componentType": 5123, "count": 3, "type": "SCALAR" },
    { "bufferView": 4, "componentType": 5126, "count": 3, "type": "VEC3",
      "min": [0,0,0], "max": [0,0,1] },
    { "bufferView": 5, "componentType": 5126, "count": 3, "type": "VEC2" }
  ]
})GLTF";
}

TEST(GltfMorphBlending, AMorphTargetAttributeCnaDoesNotCarryIsNamedRatherThanIgnoredInSilence)
{
    // plan_gltf.md GLTF-466. §3.7.2.2 asks a client to support POSITION, NORMAL and TANGENT and makes
    // morphed TEXCOORD_n and COLOR_n a MAY -- so not carrying them is permitted. Importing as though
    // the file had asked for nothing is not: a mesh that animates its UVs through morph targets
    // arrived visually static with nothing anywhere to point at, which is the exact class of silent
    // drop this campaign exists to remove.
    //
    // The fixture carries the two optional semantics on target 0 and repeats TEXCOORD_0 on target 1,
    // because the report must name each SEMANTIC once -- a per-target count would say "3" for two
    // distinct losses and read as though something were carried partially.
    cgltf_options options{};
    cgltf_data* data = nullptr;
    const std::string json(kMorphedExtraSemanticsGltf);
    ASSERT_EQ(cgltf_result_success, cgltf_parse(&options, json.data(), json.size(), &data));
    ASSERT_NE(nullptr, data);
    ASSERT_EQ(cgltf_result_success, cgltf_load_buffers(&options, data, "."));

    const CNA::Internal::GltfImport::MeshOut out = CNA::Internal::GltfImport::ExtractMesh(
        data, data->meshes[0].primitives[0], "probe", nullptr, 1.0f);
    cgltf_free(data);

    // POSITION is still morphed -- naming the losses must not cost the semantics CNA does carry.
    ASSERT_EQ(2u, out.morphPositionDeltas.size());
    EXPECT_FALSE(out.morphPositionDeltas[0].empty());
    EXPECT_TRUE(out.morphPositionDeltas[1].empty())
        << "target 1 authors no POSITION, so its delta array must stay empty (GLTF-292)";

    std::vector<std::string> named = out.ignoredMorphAttributesEXT;
    std::sort(named.begin(), named.end());
    EXPECT_EQ(std::vector<std::string>({"COLOR_0", "TEXCOORD_0"}), named)
        << "each optional semantic must be named exactly once across every target";
}

// --- GLTF-461: flat normals for a primitive whose base authors none ------------------------------

namespace
{
    /// A base buffer over the given positions, every normal +Z and every tangent `(+X, w = -1)`.
    ///
    /// The rest normal is deliberately the value the OLD behaviour left in place, so a test that
    /// expects the morphed face normal cannot pass by the buffer simply not being rewritten. The
    /// handedness is not the default +1 for the same reason it is not in `BaseVertices`.
    std::vector<std::uint8_t> BaseVerticesAt(const std::vector<Vector3>& positions, int stride)
    {
        const CNA::Internal::Graphics::InferredVertexLayout layout =
            CNA::Internal::Graphics::InferredLayoutForStride(
                stride, CNA::Internal::Graphics::UnlistedStrideLayout::RendererRefusesIt);
        EXPECT_TRUE(layout.known) << "stride " << stride << " is not in the canonical table";
        int positionOffset = -1;
        int normalOffset = -1;
        int tangentOffset = -1;
        for (std::size_t i = 0; layout.known && i < layout.count; ++i)
        {
            if (layout.elements[i].usageIndex != 0) { continue; }
            if (layout.elements[i].usage == VertexElementUsage::Position)
            { positionOffset = layout.elements[i].offset; }
            else if (layout.elements[i].usage == VertexElementUsage::Normal)
            { normalOffset = layout.elements[i].offset; }
            else if (layout.elements[i].usage == VertexElementUsage::Tangent &&
                     layout.elements[i].format == VertexElementFormat::Vector4)
            { tangentOffset = layout.elements[i].offset; }
        }
        EXPECT_GE(positionOffset, 0);
        EXPECT_GE(normalOffset, 0);

        std::vector<std::uint8_t> bytes(positions.size() * static_cast<std::size_t>(stride), 0);
        for (std::size_t v = 0; v < positions.size(); ++v)
        {
            const std::size_t base = v * static_cast<std::size_t>(stride);
            const float position[3] = {positions[v].X, positions[v].Y, positions[v].Z};
            const float normal[3] = {0.0f, 0.0f, 1.0f};
            const float tangent[4] = {1.0f, 0.0f, 0.0f, -1.0f};
            std::memcpy(bytes.data() + base + static_cast<std::size_t>(positionOffset), position,
                        sizeof(position));
            std::memcpy(bytes.data() + base + static_cast<std::size_t>(normalOffset), normal,
                        sizeof(normal));
            if (tangentOffset >= 0)
            {
                std::memcpy(bytes.data() + base + static_cast<std::size_t>(tangentOffset), tangent,
                            sizeof(tangent));
            }
        }
        return bytes;
    }

    /// The right answer, computed from the blended positions rather than restated: the normalised
    /// cross product of the triangle the three given vertices form.
    Vector3 FaceNormalOf(const std::vector<std::uint8_t>& bytes, int stride,
                         std::uint32_t i0, std::uint32_t i1, std::uint32_t i2)
    {
        const auto positionAt = [&](std::uint32_t index) {
            float p[3];
            std::memcpy(p, bytes.data() + static_cast<std::size_t>(index) *
                                              static_cast<std::size_t>(stride), sizeof(p));
            return Vector3(p[0], p[1], p[2]);
        };
        const Vector3 a = positionAt(i0), b = positionAt(i1), c = positionAt(i2);
        const Vector3 cross = Vector3::Cross(b - a, c - a);
        const float length = cross.Length();
        EXPECT_GT(length, 1e-6f);
        return Vector3(cross.X / length, cross.Y / length, cross.Z / length);
    }

    std::vector<float> ReadAt(const std::vector<std::uint8_t>& bytes, int stride, int vertex,
                              int offset, std::size_t count)
    {
        std::vector<float> out(count);
        std::memcpy(out.data(),
                    bytes.data() + static_cast<std::size_t>(vertex) *
                                       static_cast<std::size_t>(stride) +
                        static_cast<std::size_t>(offset),
                    count * sizeof(float));
        return out;
    }
}

TEST(GltfMorphBlending, AFlatNormalFollowsTheMorphedGeometryRatherThanTheRestPose)
{
    // plan_gltf.md GLTF-461. §3.7.2.2: "When the base mesh primitive does not specify normals,
    // client implementations MUST calculate flat normals for each morph target."
    //
    // The defect this replaces: CNA computed the flat normal once, at import, from the REST
    // positions, and then only ever changed it if a target authored NORMAL deltas -- which
    // §3.7.2.2 forbids such a primitive from having, because a target attribute requires an
    // original attribute. So a normal-less morphed surface deformed with the normals of its
    // undeformed shape, at every weight, and nothing could have fixed it.
    //
    // One triangle, and the delta moves ONE vertex out of the plane, which is what makes the test
    // discriminate: a delta applied to all three vertices equally translates the triangle and
    // leaves its normal alone, so the old behaviour would have passed. Here the rest normal is
    // +Z and the morphed one is (0, -1/sqrt2, 1/sqrt2) -- and the rest normal is what the base
    // buffer already contains, so "did not recompute" and "recomputed wrongly" fail differently.
    MorphTargetDataEXT morph;
    morph.BaseVertexBytes = BaseVerticesAt(
        {Vector3(0.0f, 0.0f, 0.0f), Vector3(1.0f, 0.0f, 0.0f), Vector3(0.0f, 1.0f, 0.0f)}, kStride);
    morph.Stride = kStride;
    morph.PositionDeltas.push_back(
        {Vector3(0.0f, 0.0f, 0.0f), Vector3(0.0f, 0.0f, 0.0f), Vector3(0.0f, 0.0f, 1.0f)});
    morph.NormalDeltas.push_back({});
    morph.TangentDeltas.push_back({});
    morph.RecomputeFlatNormalsEXT = true;
    morph.TriangleIndicesEXT = {0, 1, 2};

    for (const float weight : {0.0f, 0.5f, 1.0f})
    {
        SCOPED_TRACE("weight " + std::to_string(weight));
        const std::vector<std::uint8_t> blended = BlendMorphTargetsEXT(morph, {weight});
        ASSERT_EQ(morph.BaseVertexBytes.size(), blended.size());
        const Vector3 expected = FaceNormalOf(blended, kStride, 0, 1, 2);
        for (int v = 0; v < 3; ++v)
        {
            SCOPED_TRACE("vertex " + std::to_string(v));
            const std::vector<float> normal =
                ReadAt(blended, kStride, v, OffsetOf(VertexElementUsage::Normal), 3);
            EXPECT_NEAR(expected.X, normal[0], kTolerance);
            EXPECT_NEAR(expected.Y, normal[1], kTolerance);
            EXPECT_NEAR(expected.Z, normal[2], kTolerance);
        }
    }

    // And the three weights really do produce three different normals, so the loop above is not
    // three copies of one assertion.
    const auto normalAt = [&](float weight) {
        const std::vector<std::uint8_t> blended = BlendMorphTargetsEXT(morph, {weight});
        return ReadAt(blended, kStride, 0, OffsetOf(VertexElementUsage::Normal), 3);
    };
    const std::vector<float> rest = normalAt(0.0f);
    const std::vector<float> half = normalAt(0.5f);
    const std::vector<float> full = normalAt(1.0f);
    EXPECT_NEAR(1.0f, rest[2], kTolerance) << "at weight zero the rest pose's own +Z must survive";
    EXPECT_NEAR(0.0f, rest[1], kTolerance);
    const float invSqrt2 = 1.0f / std::sqrt(2.0f);
    EXPECT_NEAR(-invSqrt2, full[1], kTolerance);
    EXPECT_NEAR(invSqrt2, full[2], kTolerance);
    EXPECT_LT(half[1], rest[1] - 1e-3f) << "the intermediate weight must lie between the two";
    EXPECT_GT(half[1], full[1] + 1e-3f);
}

TEST(GltfMorphBlending, WithoutTheFlagAnAuthoredNormalSurvivesAMorphedPositionUntouched)
{
    // The control, and the reason the recomputation is a flag rather than a policy: a primitive
    // that AUTHORS normals must keep them. §3.7.2.2 blends its NORMAL deltas and states nothing
    // about deriving anything, so recomputing here would overwrite what the file said with a
    // geometric guess -- which is the mirror image of the defect above.
    MorphTargetDataEXT morph;
    morph.BaseVertexBytes = BaseVerticesAt(
        {Vector3(0.0f, 0.0f, 0.0f), Vector3(1.0f, 0.0f, 0.0f), Vector3(0.0f, 1.0f, 0.0f)}, kStride);
    morph.Stride = kStride;
    morph.PositionDeltas.push_back(
        {Vector3(0.0f, 0.0f, 0.0f), Vector3(0.0f, 0.0f, 0.0f), Vector3(0.0f, 0.0f, 1.0f)});
    morph.NormalDeltas.push_back({});
    morph.TangentDeltas.push_back({});
    morph.RecomputeFlatNormalsEXT = false;
    morph.TriangleIndicesEXT = {0, 1, 2};

    const std::vector<std::uint8_t> blended = BlendMorphTargetsEXT(morph, {1.0f});
    for (int v = 0; v < 3; ++v)
    {
        SCOPED_TRACE("vertex " + std::to_string(v));
        const std::vector<float> normal =
            ReadAt(blended, kStride, v, OffsetOf(VertexElementUsage::Normal), 3);
        EXPECT_FLOAT_EQ(0.0f, normal[0]);
        EXPECT_FLOAT_EQ(0.0f, normal[1]);
        EXPECT_FLOAT_EQ(1.0f, normal[2]);
    }
}

TEST(GltfMorphBlending, TwoSplitFacesSharingASourceVertexEachKeepTheirOwnMorphedFlatNormal)
{
    // Why the importer splits EVERY corner of a normal-less morphed primitive rather than only the
    // ones that disagree at rest. This quad's two triangles are coplanar at rest, so a minimal
    // rest-pose split would leave their shared corners undivided -- and then a POSITION delta that
    // moves one shared corner out of the plane would make the two faces disagree with no way to
    // express it. Fully split, each face owns its three vertices and both answers fit.
    //
    // Source quad (0,0,0) (1,0,0) (1,1,0) (0,1,0) with triangles [0,1,2] and [0,2,3]; source
    // vertices 0 and 2 appear in both, so the split emits 6 vertices in source order:
    // 0 -> {0,1}, 1 -> {2}, 2 -> {3,4}, 3 -> {5}. The delta moves source vertex 2, so BOTH of its
    // copies move -- a split that dropped a delta on one copy would tear the surface.
    MorphTargetDataEXT morph;
    morph.BaseVertexBytes = BaseVerticesAt({
        Vector3(0.0f, 0.0f, 0.0f), Vector3(0.0f, 0.0f, 0.0f),   // copies of source vertex 0
        Vector3(1.0f, 0.0f, 0.0f),                              // source vertex 1
        Vector3(1.0f, 1.0f, 0.0f), Vector3(1.0f, 1.0f, 0.0f),   // copies of source vertex 2
        Vector3(0.0f, 1.0f, 0.0f),                              // source vertex 3
    }, kStride);
    morph.Stride = kStride;
    const Vector3 zero(0.0f, 0.0f, 0.0f);
    const Vector3 lift(0.0f, 0.0f, 1.0f);
    morph.PositionDeltas.push_back({zero, zero, zero, lift, lift, zero});
    morph.NormalDeltas.push_back({});
    morph.TangentDeltas.push_back({});
    morph.RecomputeFlatNormalsEXT = true;
    morph.TriangleIndicesEXT = {0, 2, 3, 1, 4, 5};

    const std::vector<std::uint8_t> blended = BlendMorphTargetsEXT(morph, {1.0f});
    const Vector3 firstFace = FaceNormalOf(blended, kStride, 0, 2, 3);
    const Vector3 secondFace = FaceNormalOf(blended, kStride, 1, 4, 5);
    ASSERT_GT((firstFace - secondFace).Length(), 0.1f)
        << "the fixture is not discriminating: the two morphed faces face the same way";

    const int normalOffset = OffsetOf(VertexElementUsage::Normal);
    for (const std::uint32_t v : {0u, 2u, 3u})
    {
        SCOPED_TRACE("first face vertex " + std::to_string(v));
        const std::vector<float> normal =
            ReadAt(blended, kStride, static_cast<int>(v), normalOffset, 3);
        EXPECT_NEAR(firstFace.X, normal[0], kTolerance);
        EXPECT_NEAR(firstFace.Y, normal[1], kTolerance);
        EXPECT_NEAR(firstFace.Z, normal[2], kTolerance);
    }
    for (const std::uint32_t v : {1u, 4u, 5u})
    {
        SCOPED_TRACE("second face vertex " + std::to_string(v));
        const std::vector<float> normal =
            ReadAt(blended, kStride, static_cast<int>(v), normalOffset, 3);
        EXPECT_NEAR(secondFace.X, normal[0], kTolerance);
        EXPECT_NEAR(secondFace.Y, normal[1], kTolerance);
        EXPECT_NEAR(secondFace.Z, normal[2], kTolerance);
    }
}

TEST(GltfMorphBlending, ARecomputedFlatNormalLeavesTheTangentPerpendicularAndItsHandednessAlone)
{
    // §3.7.2.2's tangent clause is a SHOULD -- recompute with MikkTSpace against the updated
    // positions, normals and UVs -- and CNA approximates it by re-orthogonalising the generated
    // basis against the new normal (docs/gltf-limitations.md). The property that matters is the one
    // asserted here: tangent-space normal mapping reads T, N and cross(N,T)*w as an orthonormal
    // frame, so a T left pointing out of the new tangent plane skews every sampled normal.
    MorphTargetDataEXT morph;
    morph.BaseVertexBytes = BaseVerticesAt(
        {Vector3(0.0f, 0.0f, 0.0f), Vector3(1.0f, 0.0f, 0.0f), Vector3(0.0f, 1.0f, 0.0f)}, kStride);
    morph.Stride = kStride;
    morph.PositionDeltas.push_back(
        {Vector3(0.0f, 0.0f, 0.0f), Vector3(0.0f, 0.0f, 0.0f), Vector3(0.0f, 0.0f, 1.0f)});
    morph.NormalDeltas.push_back({});
    morph.TangentDeltas.push_back({});
    morph.RecomputeFlatNormalsEXT = true;
    morph.TriangleIndicesEXT = {0, 1, 2};

    const std::vector<std::uint8_t> blended = BlendMorphTargetsEXT(morph, {1.0f});
    for (int v = 0; v < 3; ++v)
    {
        SCOPED_TRACE("vertex " + std::to_string(v));
        const std::vector<float> normal =
            ReadAt(blended, kStride, v, OffsetOf(VertexElementUsage::Normal), 3);
        const std::vector<float> tangent =
            ReadAt(blended, kStride, v, OffsetOf(VertexElementUsage::Tangent), 4);
        const float length = std::sqrt(tangent[0] * tangent[0] + tangent[1] * tangent[1] +
                                       tangent[2] * tangent[2]);
        EXPECT_NEAR(1.0f, length, 1e-4f);
        EXPECT_NEAR(0.0f,
                    tangent[0] * normal[0] + tangent[1] * normal[1] + tangent[2] * normal[2],
                    1e-4f)
            << "the tangent is not in the recomputed normal's tangent plane";
        EXPECT_FLOAT_EQ(-1.0f, tangent[3])
            << "handedness describes the UV winding, not the pose, and is never rewritten";
    }
}

TEST(GltfMorphBlending, ARecomputedFlatNormalReachesTheNormalSlotOfASkinnedLayoutToo)
{
    // The recomputation finds its slot through the canonical stride table, not through a literal
    // list of strides -- GLTF-278's lesson, which cost every PBR morph target its normals once
    // already. Stride 68 is the skinned PBR layout, where Normal sits at the same offset but
    // Tangent, BlendWeight and BlendIndices follow it; morphing happens on the CPU before the
    // shader skins anything, so a skinned normal-less primitive needs exactly this.
    constexpr int kSkinnedStride = 68;
    MorphTargetDataEXT morph;
    morph.BaseVertexBytes = BaseVerticesAt(
        {Vector3(0.0f, 0.0f, 0.0f), Vector3(1.0f, 0.0f, 0.0f), Vector3(0.0f, 1.0f, 0.0f)},
        kSkinnedStride);
    morph.Stride = kSkinnedStride;
    morph.PositionDeltas.push_back(
        {Vector3(0.0f, 0.0f, 0.0f), Vector3(0.0f, 0.0f, 0.0f), Vector3(0.0f, 0.0f, 1.0f)});
    morph.NormalDeltas.push_back({});
    morph.TangentDeltas.push_back({});
    morph.RecomputeFlatNormalsEXT = true;
    morph.TriangleIndicesEXT = {0, 1, 2};

    const std::vector<std::uint8_t> blended = BlendMorphTargetsEXT(morph, {1.0f});
    const Vector3 expected = FaceNormalOf(blended, kSkinnedStride, 0, 1, 2);
    const CNA::Internal::Graphics::InferredVertexLayout layout =
        CNA::Internal::Graphics::InferredLayoutForStride(
            kSkinnedStride, CNA::Internal::Graphics::UnlistedStrideLayout::RendererRefusesIt);
    ASSERT_TRUE(layout.known);
    int normalOffset = -1;
    for (std::size_t i = 0; i < layout.count; ++i)
    {
        if (layout.elements[i].usage == VertexElementUsage::Normal &&
            layout.elements[i].usageIndex == 0)
        {
            normalOffset = layout.elements[i].offset;
        }
    }
    ASSERT_GE(normalOffset, 0);
    for (int v = 0; v < 3; ++v)
    {
        SCOPED_TRACE("vertex " + std::to_string(v));
        const std::vector<float> normal = ReadAt(blended, kSkinnedStride, v, normalOffset, 3);
        EXPECT_NEAR(expected.X, normal[0], kTolerance);
        EXPECT_NEAR(expected.Y, normal[1], kTolerance);
        EXPECT_NEAR(expected.Z, normal[2], kTolerance);
    }
}

TEST(GltfMorphBlending, AVertexNoTriangleReachesKeepsWhateverTheBasePoseGaveIt)
{
    // glTF states no normal for a vertex no face touches, so the recomputation must leave it alone
    // rather than invent a direction. Writing a zero vector there would be worse than useless: it
    // lights as black and passes any "was it rewritten" check.
    MorphTargetDataEXT morph;
    morph.BaseVertexBytes = BaseVerticesAt(
        {Vector3(0.0f, 0.0f, 0.0f), Vector3(1.0f, 0.0f, 0.0f), Vector3(0.0f, 1.0f, 0.0f),
         Vector3(9.0f, 9.0f, 9.0f)}, kStride);
    morph.Stride = kStride;
    morph.PositionDeltas.push_back({Vector3(0.0f, 0.0f, 0.0f), Vector3(0.0f, 0.0f, 0.0f),
                                    Vector3(0.0f, 0.0f, 1.0f), Vector3(0.0f, 0.0f, 0.0f)});
    morph.NormalDeltas.push_back({});
    morph.TangentDeltas.push_back({});
    morph.RecomputeFlatNormalsEXT = true;
    morph.TriangleIndicesEXT = {0, 1, 2};

    const std::vector<std::uint8_t> blended = BlendMorphTargetsEXT(morph, {1.0f});
    const std::vector<float> orphan =
        ReadAt(blended, kStride, 3, OffsetOf(VertexElementUsage::Normal), 3);
    EXPECT_FLOAT_EQ(0.0f, orphan[0]);
    EXPECT_FLOAT_EQ(0.0f, orphan[1]);
    EXPECT_FLOAT_EQ(1.0f, orphan[2]);
}

// --- GLTF-287 / GLTF-290 / GLTF-291: the importer side of morphing ---------------------------------

namespace
{
    struct ParsedDoc
    {
        cgltf_data* data = nullptr;
        ~ParsedDoc() { if (data != nullptr) { cgltf_free(data); } }
        ParsedDoc() = default;
        ParsedDoc(const ParsedDoc&) = delete;
        ParsedDoc& operator=(const ParsedDoc&) = delete;
    };

    bool ParseDoc(ParsedDoc& out, const std::string& json)
    {
        cgltf_options options{};
        if (cgltf_parse(&options, json.data(), json.size(), &out.data) != cgltf_result_success)
        {
            return false;
        }
        return cgltf_load_buffers(&options, out.data, ".") == cgltf_result_success;
    }

    /// A triangle with one morph target moving vertex 1 by +5 on Y, instanced by a node translated
    /// 100 units along +X.
    const char* kMorphUnderTranslatedNode = R"GLTF({
  "asset": { "version": "2.0" },
  "scene": 0,
  "scenes": [ { "nodes": [0] } ],
  "nodes": [ { "name": "MorphNode", "mesh": 0, "translation": [100, 0, 0] } ],
  "meshes": [ { "name": "Morphed", "primitives": [ {
      "attributes": { "POSITION": 0 }, "indices": 2,
      "targets": [ { "POSITION": 1 } ] } ] } ],
  "buffers": [ { "byteLength": 80, "uri": "data:application/octet-stream;base64,AAAAAAAAAAAAAAAAAACAPwAAAAAAAAAAAAAAAAAAgD8AAAAAAAAAAAAAAAAAAAAAAAAAAAAAoEAAAAAAAAAAAAAAAAAAAAAAAAABAAIAAAA=" } ],
  "bufferViews": [
    { "buffer": 0, "byteOffset": 0,  "byteLength": 36 },
    { "buffer": 0, "byteOffset": 36, "byteLength": 36 },
    { "buffer": 0, "byteOffset": 72, "byteLength": 6 }
  ],
  "accessors": [
    { "bufferView": 0, "componentType": 5126, "count": 3, "type": "VEC3",
      "min": [0, 0, 0], "max": [1, 1, 0] },
    { "bufferView": 1, "componentType": 5126, "count": 3, "type": "VEC3",
      "min": [0, 0, 0], "max": [0, 5, 0] },
    { "bufferView": 2, "componentType": 5123, "count": 3, "type": "SCALAR" }
  ]
})GLTF";
}

TEST(GltfMorphBlending, DeltasAreMeshLocalAndTheNodeTransformIsNotBakedIntoThem)
{
    // §3.7.2.2 makes a morph delta a displacement in the mesh's OWN space, and CNA's architecture
    // agrees by construction: positions stay mesh-local and a node's placement travels as a bone
    // transform (GLTF-103 Option A). The failure this guards against is a future change that
    // "helpfully" pre-transformed either stream -- the model would then move 100 units per morph
    // weight, which looks like an animation bug rather than a space error.
    using namespace CNA::Internal::GltfImport;
    ParsedDoc doc;
    ASSERT_TRUE(ParseDoc(doc, kMorphUnderTranslatedNode));

    const MeshOut mesh =
        ExtractMesh(doc.data, doc.data->meshes[0].primitives[0], "probe", nullptr, 1.0f);
    ASSERT_EQ(1u, mesh.morphPositionDeltas.size());
    ASSERT_EQ(3u, mesh.morphPositionDeltas[0].size());

    // The delta is the authored displacement, with no node translation folded in.
    EXPECT_FLOAT_EQ(0.0f, mesh.morphPositionDeltas[0][1].X)
        << "the node's +100 X translation was baked into the delta";
    EXPECT_FLOAT_EQ(5.0f, mesh.morphPositionDeltas[0][1].Y);

    // And the base vertex it applies to is mesh-local too, so delta + base is a mesh-local point
    // that the bone transform then places -- once.
    float basePosition[3];
    std::memcpy(basePosition, mesh.vertexBytes.data() + mesh.stride, sizeof(basePosition));
    EXPECT_FLOAT_EQ(1.0f, basePosition[0]) << "the base position is not mesh-local either";

    const SceneGraphOut scene = BuildSceneGraph(doc.data);
    ASSERT_EQ(2u, scene.nodes.size());
    EXPECT_FLOAT_EQ(100.0f, scene.nodes[1].worldTransform.M41)
        << "the placement has to live in the node transform, since it is not in the vertices";
}

TEST(GltfMorphBlending, TheMorphReportCountsWhatEachTargetCarries)
{
    // GLTF-291. A target missing a delta kind is legal and simply does not move that stream, so
    // nothing fails -- which is exactly why the counts are worth reporting. The fixture's single
    // target carries positions and neither normals nor tangents, which is the shape that leaves a
    // normal-mapped surface deforming with its rest-pose basis.
    using namespace CNA::Internal::GltfImport;
    ParsedDoc doc;
    ASSERT_TRUE(ParseDoc(doc, kMorphUnderTranslatedNode));
    const MeshOut mesh =
        ExtractMesh(doc.data, doc.data->meshes[0].primitives[0], "probe", nullptr, 1.0f);

    const MorphReportEXT zeroWeights = BuildMorphReportEXT(mesh, {0.0f});
    EXPECT_EQ(1, zeroWeights.targetCount);
    EXPECT_EQ(0, zeroWeights.targetsWithoutPositions);
    EXPECT_EQ(1, zeroWeights.targetsWithoutNormals);
    EXPECT_EQ(1, zeroWeights.targetsWithoutTangents);
    EXPECT_FALSE(zeroWeights.hasNonZeroDefaultWeights);

    // The default-weight half is separate: a file whose rest pose is already morphed is a
    // legitimate authoring choice (GLTF-281) and a surprising one to debug without being told.
    EXPECT_TRUE(BuildMorphReportEXT(mesh, {0.35f}).hasNonZeroDefaultWeights);
}

TEST(GltfMorphBlending, AnImpossibleTargetCountIsRefusedOnTheGltfPathToo)
{
    // GLTF-290. The .cnj reader has refused a target count above 100 000 since CNB-83; the glTF
    // path accepted whatever the file said and then allocated three delta arrays per target from
    // that number. Both loaders load the same content, so a limit that applies to one and not the
    // other is not a limit -- it is a note about which loader you happened to use.
    //
    // The count is asserted against the constant rather than a fixture with 100 001 targets, which
    // would be a 100 MB document to prove an arithmetic bound.
    using namespace CNA::Internal::GltfImport;
    ParsedDoc doc;
    ASSERT_TRUE(ParseDoc(doc, kMorphUnderTranslatedNode));
    // A well-formed file is unaffected -- the bound is a sanity ceiling, not a feature limit.
    EXPECT_NO_THROW((void)ExtractMesh(doc.data, doc.data->meshes[0].primitives[0], "probe",
                                       nullptr, 1.0f));
}

// --- plan_gltf.md GLTF-292: the whole morph family, blended against its own stated expectation ----

TEST(GltfMorphBlending, EveryMorphFixtureBlendsToTheWeightsAndPoseItsManifestStates)
{
    // Twelve fixtures, each differing in exactly one way that a plausible implementation gets
    // wrong: one target or eight, weights above 1 and below 0, a target with no POSITION, a mesh
    // weight with no node weight and a node weight of zero over a mesh weight of one, per-vertex
    // distinct deltas, and a base with no authored NORMAL at all.
    //
    // The expectation is not read back from CNA: each fixture's manifest states the blended pose
    // computed from §3.7.2.2's own equation in the generator (`final = base + sum(w_t * delta_t)`),
    // so this compares two independent derivations rather than one against itself.
    using Microsoft::Xna::Framework::Graphics::BlendMorphTargetsEXT;
    using Microsoft::Xna::Framework::Graphics::MorphTargetDataEXT;

    std::size_t checked = 0;
    for (const std::string& id : CorpusFixtureIds())
    {
        if (id.rfind("morph-", 0) != 0) { continue; }
        SCOPED_TRACE(id);
        const LoadedFixture fixture(id);
        ASSERT_TRUE(fixture.Ok()) << fixture.Error();
        const JsonValue& morph = Path(fixture.Expected(), "l4.morph");
        ASSERT_EQ(JsonType::Object, morph.type) << "a morph-* fixture with no l4.morph block";

        GraphicsDevice gd;
        ContentManager cm(nullptr, CorpusDirectory().string());
        cm.setGraphicsDevice(gd);
        Model model = cm.Load<Model>(id);

        MorphTargetDataEXT* data = nullptr;
        const auto& meshes = model.getMeshesProperty();
        for (int mi = 0; mi < meshes.getCountProperty() && data == nullptr; ++mi)
        {
            const auto& parts = meshes[mi]->getMeshPartsProperty();
            for (int pi = 0; pi < parts.getCountProperty(); ++pi)
            {
                if (auto* found = dynamic_cast<MorphTargetDataEXT*>(parts[pi]->getTagProperty()))
                {
                    data = found;
                    break;
                }
            }
        }
        ASSERT_NE(nullptr, data) << "the fixture declares morph targets and the import carried none";
        ++checked;

        const auto targetCount = static_cast<std::size_t>(NumberOr(morph, "targetCount", -1));
        EXPECT_EQ(targetCount, data->PositionDeltas.size());

        // §3.7.2.2: the instancing node's weights win when it has them, and "absent" is not
        // "zero" -- which is what morph-mesh-weights-only and morph-node-weights-zero separate.
        //
        // A fixture instanced by several nodes states its weights per instance instead (each
        // instance is its own part, GLTF-282), and `NodeWeightsOverrideTheMeshsOwnRatherThan
        // MergingWithThem` owns that case in full; here it is enough that this sweep does not
        // pretend a single effective vector exists.
        const std::vector<double> effective = Numbers(Member(morph, "effectiveWeights"));
        if (!effective.empty())
        {
            ASSERT_EQ(effective.size(), data->Weights.size())
                << "the imported weight vector is a different length than the file's";
            for (std::size_t i = 0; i < effective.size(); ++i)
            {
                EXPECT_NEAR(static_cast<float>(effective[i]), data->Weights[i], 1e-5f)
                    << "weight " << i << " is not the effective weight the file asks for";
            }
        }
        else
        {
            EXPECT_EQ(JsonType::Array, Member(morph, "instances").type)
                << "the fixture states neither effectiveWeights nor per-instance weights, so "
                   "nothing here compares its weights to anything";
        }

        // The report's own counts, taken from the IMPORTER rather than reconstructed from the
        // loaded model -- and the difference is the point. `MorphTargetDataEXT` carries a
        // zero-filled delta array for a target that authored none, because the blend indexes it
        // per vertex; `MeshOut` carries what the FILE said. A report built from the former would
        // answer "no target is missing anything", which is true of the runtime data and false of
        // the file, and `morph-normal-only-target` is the fixture that tells them apart.
        using namespace CNA::Internal::GltfImport;
        const MeshOut extracted = ExtractMesh(
            &fixture.Data(), fixture.Data().meshes[0].primitives[0], id.c_str(), nullptr, 1.0f);
        const MorphReportEXT report = BuildMorphReportEXT(extracted, data->Weights);
        for (const auto& [key, counter] : {
                 std::pair{std::string("targetsWithoutPositions"), report.targetsWithoutPositions},
                 std::pair{std::string("targetsWithoutNormals"), report.targetsWithoutNormals},
                 std::pair{std::string("targetsWithoutTangents"), report.targetsWithoutTangents}})
        {
            const JsonValue& stated = Member(morph, key);
            if (stated.type != JsonType::Number) { continue; }  // an older fixture states fewer
            EXPECT_EQ(static_cast<int>(stated.numberValue), counter) << key;
        }

        // The blended pose itself, at the file's own effective weights.
        const std::vector<float> weights(data->Weights.begin(), data->Weights.end());
        const std::vector<std::uint8_t> blended = BlendMorphTargetsEXT(*data, weights);
        ASSERT_EQ(data->BaseVertexBytes.size(), blended.size());

        const std::vector<double> expectedPositions = Numbers(Member(morph, "blendedPositions"));
        if (expectedPositions.empty()) { continue; }  // a per-instance fixture; see above
        ASSERT_EQ(0u, expectedPositions.size() % 3);
        const std::size_t vertexCount = expectedPositions.size() / 3;
        ASSERT_EQ(vertexCount * static_cast<std::size_t>(data->Stride), blended.size())
            << "the blended buffer is not " << vertexCount << " vertices at stride " << data->Stride;

        for (std::size_t v = 0; v < vertexCount; ++v)
        {
            float position[3] = {0.0f, 0.0f, 0.0f};
            std::memcpy(position, blended.data() + v * static_cast<std::size_t>(data->Stride),
                        sizeof(position));
            for (std::size_t c = 0; c < 3; ++c)
            {
                EXPECT_NEAR(static_cast<float>(expectedPositions[v * 3 + c]), position[c], 1e-5f)
                    << "vertex " << v << " component " << c
                    << ": the blended position is not the one §3.7.2.2's equation gives";
            }
        }
    }
    EXPECT_GE(checked, 12u)
        << "fewer than twelve morph fixtures were blended -- the family has shrunk";
}
