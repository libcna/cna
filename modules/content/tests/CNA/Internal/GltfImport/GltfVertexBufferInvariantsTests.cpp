// SPDX-License-Identifier: MS-PL
//
// plan_gltf.md GLTF-083 / GLTF-089 / GLTF-098 / GLTF-154: the invariants every packed vertex buffer
// holds, whatever stride it lands on.
//
// Each one is depended on somewhere that cannot check it. Position-at-offset-0 is assumed by every
// renderer's stride-keyed input layout. Vertex ORDER is what a morph target's delta array indexes
// by -- if extraction ever reordered vertices, every morph target in the project would apply its
// deltas to the wrong vertices, and nothing in the morph path could detect it. The colour
// quantisation rounds where an author's authored value lands on a byte boundary.

#include <cmath>
#include <cstdint>
#include <cstring>
#include <gtest/gtest.h>
#include <string>
#include <vector>

#include "CNA/Internal/GltfImport/GltfImportCore.hpp"
#include "CNA/Internal/Graphics/VertexDeclarationFidelity.hpp"

using namespace CNA::Internal::GltfImport;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
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

    /// A three-vertex primitive with POSITION and a FLOAT COLOR_0 of `colorComponents` components.
    /// A float colour is the shape that exercises the quantisation rule: a normalized-integer one
    /// is already a byte and round-trips whatever the rule is.
    std::string FloatColorDocument(const std::vector<float>& positions,
                                    const std::vector<float>& colors, int colorComponents)
    {
        std::vector<std::uint8_t> buffer;
        AppendFloats(buffer, positions);
        const std::size_t colorOffset = buffer.size();
        AppendFloats(buffer, colors);

        return std::string(R"GLTF({
  "asset": { "version": "2.0" },
  "scene": 0,
  "scenes": [ { "nodes": [0] } ],
  "nodes": [ { "name": "MeshNode", "mesh": 0 } ],
  "meshes": [ { "primitives": [ {
      "attributes": { "POSITION": 0, "COLOR_0": 1 }, "mode": 4
  } ] } ],
  "buffers": [ { "byteLength": )GLTF") + std::to_string(buffer.size()) +
               R"GLTF(, "uri": "data:application/octet-stream;base64,)GLTF" + Base64(buffer) +
               R"GLTF(" } ],
  "bufferViews": [
    { "buffer": 0, "byteOffset": 0, "byteLength": )GLTF" + std::to_string(colorOffset) +
               R"GLTF( },
    { "buffer": 0, "byteOffset": )GLTF" + std::to_string(colorOffset) +
               R"GLTF(, "byteLength": )GLTF" + std::to_string(colors.size() * 4) + R"GLTF( }
  ],
  "accessors": [
    { "bufferView": 0, "componentType": 5126, "count": 3, "type": "VEC3", "min": [0,0,0], "max": [1,1,0] },
    { "bufferView": 1, "componentType": 5126, "count": 3, "type": ")GLTF" +
               std::string(colorComponents == 4 ? "VEC4" : "VEC3") + R"GLTF(" }
  ]
})GLTF";
    }

    MeshOut ExtractFirst(const std::string& json)
    {
        Parsed parsed;
        EXPECT_TRUE(Parse(parsed, json)) << "the fixture document did not parse";
        if (parsed.data == nullptr || parsed.data->meshes_count == 0) { return MeshOut{}; }
        return ExtractMesh(parsed.data, parsed.data->meshes[0].primitives[0], "probe", nullptr,
                            1.0f);
    }

    int OffsetOf(const MeshOut& mesh, VertexElementUsage usage)
    {
        const CNA::Internal::Graphics::InferredVertexLayout layout =
            CNA::Internal::Graphics::InferredLayoutForStride(
                mesh.stride, CNA::Internal::Graphics::UnlistedStrideLayout::RendererRefusesIt);
        EXPECT_TRUE(layout.known) << "stride " << mesh.stride << " is not in the canonical table";
        for (std::size_t i = 0; layout.known && i < layout.count; ++i)
        {
            if (layout.elements[i].usage == usage && layout.elements[i].usageIndex == 0)
            {
                return layout.elements[i].offset;
            }
        }
        return -1;
    }
}

// --- GLTF-083: POSITION is required, and is always the first element -----------------------------

TEST(GltfVertexBufferInvariants, APrimitiveWithNoPositionIsRefusedRatherThanPackedEmpty)
{
    // §3.7.2.1 makes POSITION mandatory, and CNA depends on it beyond conformance: POSITION's
    // accessor count is what drives the packing loop, so a primitive without one has no vertex
    // count at all. The refusal has to happen rather than the loop running zero times and producing
    // a silently empty mesh.
    Parsed parsed;
    ASSERT_TRUE(Parse(parsed, R"GLTF({
  "asset": { "version": "2.0" },
  "scene": 0,
  "scenes": [ { "nodes": [0] } ],
  "nodes": [ { "name": "MeshNode", "mesh": 0 } ],
  "meshes": [ { "primitives": [ { "attributes": { "NORMAL": 0 }, "mode": 4 } ] } ],
  "buffers": [ { "byteLength": 36, "uri": "data:application/octet-stream;base64,AAAAAAAAAAAAAIA/AAAAAAAAAAAAAIA/AAAAAAAAAAAAAIA/" } ],
  "bufferViews": [ { "buffer": 0, "byteOffset": 0, "byteLength": 36 } ],
  "accessors": [
    { "bufferView": 0, "componentType": 5126, "count": 3, "type": "VEC3" }
  ]
})GLTF"));

    EXPECT_THROW((void)ExtractMesh(parsed.data, parsed.data->meshes[0].primitives[0], "probe",
                                    nullptr, 1.0f),
                 std::runtime_error);
}

TEST(GltfVertexBufferInvariants, PositionIsAtOffsetZeroAndCarriesItsAuthoredValues)
{
    // Every renderer's stride-keyed input layout assumes this, and GLTF-150 asserts it across the
    // canonical table in VertexDeclarationFidelityTests. What is added here is the other half: that
    // the bytes actually AT offset 0 are the authored positions, so the table's claim and the
    // packer's behaviour cannot drift apart.
    const MeshOut mesh = ExtractFirst(FloatColorDocument(
        {1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f, 7.0f, 8.0f, 9.0f},
        {1.0f, 0.0f, 0.0f, 1.0f,  0.0f, 1.0f, 0.0f, 1.0f,  0.0f, 0.0f, 1.0f, 1.0f}, 4));
    ASSERT_EQ(3u, mesh.vertexBytes.size() / static_cast<std::size_t>(mesh.stride));
    ASSERT_EQ(0, OffsetOf(mesh, VertexElementUsage::Position));

    for (std::size_t v = 0; v < 3; ++v)
    {
        float position[3];
        std::memcpy(position,
                    mesh.vertexBytes.data() + v * static_cast<std::size_t>(mesh.stride),
                    sizeof(position));
        EXPECT_FLOAT_EQ(static_cast<float>(v * 3 + 1), position[0]) << "vertex " << v;
        EXPECT_FLOAT_EQ(static_cast<float>(v * 3 + 2), position[1]) << "vertex " << v;
        EXPECT_FLOAT_EQ(static_cast<float>(v * 3 + 3), position[2]) << "vertex " << v;
    }
}

// --- GLTF-098: vertices are emitted in accessor order -------------------------------------------

TEST(GltfVertexBufferInvariants, VerticesAreEmittedInAccessorOrderBecauseMorphDeltasIndexByIt)
{
    // This is the invariant with the most dangerous failure mode in the file. A morph target's
    // delta array is indexed BY VERTEX POSITION IN THE BUFFER -- nothing in the morph path carries
    // an identity for a vertex. So any reordering during extraction (a cache-friendly sort, a
    // dedupe pass, a Draco round-trip that renumbers) applies every delta to the wrong vertex, and
    // no test in the morph path could see it: the deltas would still be the right values, just on
    // the wrong vertices.
    //
    // The positions are deliberately in DESCENDING X, so any sort would be visible immediately.
    const MeshOut mesh = ExtractFirst(FloatColorDocument(
        {9.0f, 0.0f, 0.0f,  5.0f, 0.0f, 0.0f,  1.0f, 0.0f, 0.0f},
        {1.0f, 0.0f, 0.0f, 1.0f,  0.0f, 1.0f, 0.0f, 1.0f,  0.0f, 0.0f, 1.0f, 1.0f}, 4));
    ASSERT_EQ(3u, mesh.vertexBytes.size() / static_cast<std::size_t>(mesh.stride));

    const float expectedX[3] = {9.0f, 5.0f, 1.0f};
    for (std::size_t v = 0; v < 3; ++v)
    {
        float x = 0.0f;
        std::memcpy(&x, mesh.vertexBytes.data() + v * static_cast<std::size_t>(mesh.stride),
                    sizeof(x));
        EXPECT_FLOAT_EQ(expectedX[v], x)
            << "vertex " << v << " is not the accessor's own element " << v
            << " -- every morph target in the project indexes deltas by this order";
    }
}

// --- GLTF-154: the colour quantisation rule -----------------------------------------------------

TEST(GltfVertexBufferInvariants, FloatColoursQuantiseByRoundingNotTruncation)
{
    // round(clamp(f, 0, 1) * 255). The alternative -- truncation -- is off by one for most inputs
    // and exactly right for 0 and 1, so only a mid value can tell them apart. 0.5 * 255 is 127.5:
    // rounding gives 128, truncating gives 127. A normalized-integer colour cannot test this at all,
    // because it is already a byte and round-trips under either rule.
    const MeshOut mesh = ExtractFirst(FloatColorDocument(
        {0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f},
        {0.5f, 0.0f, 1.0f, 0.5f,
         0.002f, 0.998f, 0.25f, 0.75f,
         0.0f, 1.0f, 0.5f, 1.0f}, 4));
    const int colorOffset = OffsetOf(mesh, VertexElementUsage::Color);
    ASSERT_GE(colorOffset, 0) << "the chosen layout has no Color slot";

    const auto channel = [&](std::size_t vertex, std::size_t index) {
        return static_cast<int>(
            mesh.vertexBytes[vertex * static_cast<std::size_t>(mesh.stride) +
                             static_cast<std::size_t>(colorOffset) + index]);
    };

    EXPECT_EQ(128, channel(0, 0)) << "0.5 must round to 128; truncation gives 127";
    EXPECT_EQ(0, channel(0, 1)) << "0 maps exactly to 0";
    EXPECT_EQ(255, channel(0, 2)) << "1 maps exactly to 255";
    EXPECT_EQ(128, channel(0, 3)) << "alpha follows the same rule as the colour channels";

    EXPECT_EQ(1, channel(1, 0)) << "0.002 * 255 is 0.51 -- rounds up to 1, truncates to 0";
    EXPECT_EQ(254, channel(1, 1)) << "0.998 * 255 is 254.49 -- rounds down to 254";
    EXPECT_EQ(64, channel(1, 2)) << "0.25 * 255 is 63.75";
    EXPECT_EQ(191, channel(1, 3)) << "0.75 * 255 is 191.25";
}

TEST(GltfVertexBufferInvariants, AColourOutsideTheUnitRangeIsClampedRatherThanWrapped)
{
    // The clamp half of round(CLAMP(f,0,1) * 255). Without it, 1.5 * 255 is 382, which narrows to
    // 126 -- a bright colour arriving as a mid grey, which looks like a material bug rather than an
    // out-of-range input. A negative value wraps to near-white by the same mechanism.
    const MeshOut mesh = ExtractFirst(FloatColorDocument(
        {0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f},
        {1.5f, -0.5f, 2.0f, 1.0f,
         1.0f, 1.0f, 1.0f, 1.0f,
         0.0f, 0.0f, 0.0f, 1.0f}, 4));
    const int colorOffset = OffsetOf(mesh, VertexElementUsage::Color);
    ASSERT_GE(colorOffset, 0);

    const auto channel = [&](std::size_t vertex, std::size_t index) {
        return static_cast<int>(
            mesh.vertexBytes[vertex * static_cast<std::size_t>(mesh.stride) +
                             static_cast<std::size_t>(colorOffset) + index]);
    };
    EXPECT_EQ(255, channel(0, 0)) << "1.5 must clamp to 255; unclamped it narrows to 126";
    EXPECT_EQ(0, channel(0, 1)) << "-0.5 must clamp to 0";
    EXPECT_EQ(255, channel(0, 2)) << "2.0 must clamp to 255";
}

// --- GLTF-089: COLOR_0 as VEC3 and as VEC4 ------------------------------------------------------

TEST(GltfVertexBufferInvariants, AVec3ColourGetsFullyOpaqueAlphaRatherThanZero)
{
    // §3.7.2.1 allows COLOR_0 to be VEC3, and the missing alpha defaults to 1 -- not to 0, which is
    // the value a zero-initialised buffer would supply and which would make every vertex of such a
    // mesh fully transparent. The two shapes are asserted against each other so the VEC3 path
    // cannot quietly diverge from the VEC4 one in the three channels they share.
    const std::vector<float> positions = {0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f};
    const MeshOut vec3 = ExtractFirst(FloatColorDocument(
        positions, {1.0f, 0.5f, 0.0f,  0.0f, 1.0f, 0.5f,  0.5f, 0.0f, 1.0f}, 3));
    const MeshOut vec4 = ExtractFirst(FloatColorDocument(
        positions, {1.0f, 0.5f, 0.0f, 1.0f,  0.0f, 1.0f, 0.5f, 1.0f,  0.5f, 0.0f, 1.0f, 1.0f}, 4));

    const int vec3Offset = OffsetOf(vec3, VertexElementUsage::Color);
    const int vec4Offset = OffsetOf(vec4, VertexElementUsage::Color);
    ASSERT_GE(vec3Offset, 0);
    ASSERT_GE(vec4Offset, 0);
    ASSERT_EQ(vec3.stride, vec4.stride) << "both shapes must land on the same vertex layout";

    for (std::size_t v = 0; v < 3; ++v)
    {
        SCOPED_TRACE("vertex " + std::to_string(v));
        for (std::size_t c = 0; c < 4; ++c)
        {
            const int fromVec3 =
                vec3.vertexBytes[v * static_cast<std::size_t>(vec3.stride) +
                                 static_cast<std::size_t>(vec3Offset) + c];
            const int fromVec4 =
                vec4.vertexBytes[v * static_cast<std::size_t>(vec4.stride) +
                                 static_cast<std::size_t>(vec4Offset) + c];
            EXPECT_EQ(fromVec4, fromVec3) << "channel " << c;
        }
        EXPECT_EQ(255, static_cast<int>(
                           vec3.vertexBytes[v * static_cast<std::size_t>(vec3.stride) +
                                            static_cast<std::size_t>(vec3Offset) + 3]))
            << "a VEC3 colour's implied alpha must be fully opaque, not zero";
    }
}

// --- GLTF-169 / GLTF-170 / GLTF-172: the tangent basis -----------------------------------------
//
// A PBR primitive needs a per-vertex tangent, and glTF leaves a reader two jobs: use the authored
// TANGENT verbatim when there is one, and generate a basis when there is not (§3.7.2.1 recommends
// exactly that). Both have a silent failure mode. An authored tangent quietly recomputed loses the
// handedness the author chose, mirroring every normal-mapped detail on half the surface. A
// generated one that divides by a degenerate UV area produces NaN, which propagates through the
// whole lighting term and renders as black or as nothing at all -- and NaN compares false against
// everything, so an ordinary value assertion will not catch it.

namespace
{
    /// A PBR primitive (base-colour + normal map, so the layout has a tangent slot) with POSITION,
    /// NORMAL, TEXCOORD_0 and optionally TANGENT.
    std::string TangentDocument(const std::vector<float>& positions,
                                 const std::vector<float>& normals,
                                 const std::vector<float>& uvs,
                                 const std::vector<float>& tangents)
    {
        std::vector<std::uint8_t> buffer;
        AppendFloats(buffer, positions);
        const std::size_t normalOffset = buffer.size();
        AppendFloats(buffer, normals);
        const std::size_t uvOffset = buffer.size();
        AppendFloats(buffer, uvs);
        const std::size_t tangentOffset = buffer.size();
        AppendFloats(buffer, tangents);

        std::string views =
            R"({ "buffer": 0, "byteOffset": 0, "byteLength": )" + std::to_string(normalOffset) +
            R"( },
    { "buffer": 0, "byteOffset": )" + std::to_string(normalOffset) + R"(, "byteLength": )" +
            std::to_string(uvOffset - normalOffset) + R"( },
    { "buffer": 0, "byteOffset": )" + std::to_string(uvOffset) + R"(, "byteLength": )" +
            std::to_string(tangentOffset - uvOffset) + " }";
        std::string accessors =
            R"({ "bufferView": 0, "componentType": 5126, "count": 3, "type": "VEC3", "min": [0,0,0], "max": [1,1,0] },
    { "bufferView": 1, "componentType": 5126, "count": 3, "type": "VEC3" },
    { "bufferView": 2, "componentType": 5126, "count": 3, "type": "VEC2" })";
        std::string attributes = R"("POSITION": 0, "NORMAL": 1, "TEXCOORD_0": 2)";
        if (!tangents.empty())
        {
            views += R"(,
    { "buffer": 0, "byteOffset": )" + std::to_string(tangentOffset) + R"(, "byteLength": )" +
                     std::to_string(tangents.size() * 4) + " }";
            accessors += R"(,
    { "bufferView": 3, "componentType": 5126, "count": 3, "type": "VEC4" })";
            attributes += R"(, "TANGENT": 3)";
        }

        return std::string(R"GLTF({
  "asset": { "version": "2.0" },
  "scene": 0,
  "scenes": [ { "nodes": [0] } ],
  "nodes": [ { "name": "MeshNode", "mesh": 0 } ],
  "meshes": [ { "primitives": [ { "attributes": { )GLTF") + attributes +
               R"GLTF( }, "material": 0, "mode": 4 } ] } ],
  "materials": [ { "pbrMetallicRoughness": { "baseColorTexture": { "index": 0 } },
                   "normalTexture": { "index": 0 } } ],
  "textures": [ { "source": 0 } ],
  "images": [ { "uri": "n.png", "mimeType": "image/png" } ],
  "buffers": [ { "byteLength": )GLTF" + std::to_string(buffer.size()) +
               R"GLTF(, "uri": "data:application/octet-stream;base64,)GLTF" + Base64(buffer) +
               R"GLTF(" } ],
  "bufferViews": [ )GLTF" + views + R"GLTF( ],
  "accessors": [ )GLTF" + accessors + R"GLTF( ]
})GLTF";
    }

    std::vector<float> TangentOfVertex(const MeshOut& mesh, std::size_t vertex)
    {
        const int offset = OffsetOf(mesh, VertexElementUsage::Tangent);
        EXPECT_GE(offset, 0) << "the chosen layout has no Tangent slot";
        std::vector<float> tangent(4);
        if (offset >= 0)
        {
            std::memcpy(tangent.data(),
                        mesh.vertexBytes.data() + vertex * static_cast<std::size_t>(mesh.stride) +
                            static_cast<std::size_t>(offset),
                        4 * sizeof(float));
        }
        return tangent;
    }
}

TEST(GltfVertexBufferInvariants, AnAuthoredTangentIsCarriedVerbatimIncludingItsHandedness)
{
    // The `w` is the half that gets lost. It is not a direction but a SIGN -- which way the
    // bitangent runs, decided by the UV winding the author baked in. Recomputing it, or dropping it
    // to a default +1, mirrors every normal-mapped detail on any surface whose UVs are flipped, and
    // that reads as an art bug rather than an importer one.
    //
    // The authored tangent here is deliberately NOT what generation would produce for these UVs:
    // (0,1,0) with w = -1, against a UV layout whose natural tangent runs along +X.
    const MeshOut mesh = ExtractFirst(TangentDocument(
        {0.0f, 0.0f, 0.0f,  1.0f, 0.0f, 0.0f,  0.0f, 1.0f, 0.0f},
        {0.0f, 0.0f, 1.0f,  0.0f, 0.0f, 1.0f,  0.0f, 0.0f, 1.0f},
        {0.0f, 0.0f,  1.0f, 0.0f,  0.0f, 1.0f},
        {0.0f, 1.0f, 0.0f, -1.0f,  0.0f, 1.0f, 0.0f, -1.0f,  0.0f, 1.0f, 0.0f, -1.0f}));
    ASSERT_EQ(3u, mesh.vertexBytes.size() / static_cast<std::size_t>(mesh.stride));

    for (std::size_t v = 0; v < 3; ++v)
    {
        SCOPED_TRACE("vertex " + std::to_string(v));
        const std::vector<float> tangent = TangentOfVertex(mesh, v);
        EXPECT_NEAR(0.0f, tangent[0], 1e-5f);
        EXPECT_NEAR(1.0f, tangent[1], 1e-5f)
            << "the authored tangent was recomputed instead of carried";
        EXPECT_NEAR(0.0f, tangent[2], 1e-5f);
        EXPECT_FLOAT_EQ(-1.0f, tangent[3])
            << "the authored handedness was replaced -- normal-mapped detail mirrors wherever the "
               "author's UV winding disagrees with the default";
    }
}

TEST(GltfVertexBufferInvariants, AnAbsentTangentIsGeneratedAlongTheUDirection)
{
    // §3.7.2.1 recommends generating a basis when TANGENT is absent, and the basis is defined by
    // the UVs: a tangent points along increasing U. These UVs put U along +X, so the generated
    // tangent must too -- and it must be unit length, because a Gram-Schmidt step that forgets to
    // renormalise leaves a shorter vector that silently dims the normal-map contribution.
    const MeshOut mesh = ExtractFirst(TangentDocument(
        {0.0f, 0.0f, 0.0f,  1.0f, 0.0f, 0.0f,  0.0f, 1.0f, 0.0f},
        {0.0f, 0.0f, 1.0f,  0.0f, 0.0f, 1.0f,  0.0f, 0.0f, 1.0f},
        {0.0f, 0.0f,  1.0f, 0.0f,  0.0f, 1.0f},
        {}));
    ASSERT_EQ(3u, mesh.vertexBytes.size() / static_cast<std::size_t>(mesh.stride));

    for (std::size_t v = 0; v < 3; ++v)
    {
        SCOPED_TRACE("vertex " + std::to_string(v));
        const std::vector<float> tangent = TangentOfVertex(mesh, v);
        EXPECT_NEAR(1.0f, tangent[0], 1e-4f) << "U runs along +X here, so the tangent must too";
        EXPECT_NEAR(0.0f, tangent[1], 1e-4f);
        EXPECT_NEAR(0.0f, tangent[2], 1e-4f);
        const float length = std::sqrt(tangent[0] * tangent[0] + tangent[1] * tangent[1] +
                                       tangent[2] * tangent[2]);
        EXPECT_NEAR(1.0f, length, 1e-4f)
            << "a non-unit tangent silently dims every normal-map contribution";
    }
}

TEST(GltfVertexBufferInvariants, ADegenerateUvTriangleProducesNoNaNInTheGeneratedTangent)
{
    // The tangent solve divides by the UV-space area, which is zero when all three vertices share a
    // UV -- a real authoring outcome for untextured or collapsed geometry. Unguarded that is 0/0:
    // NaN, which propagates through the entire lighting term and renders as black or as nothing.
    //
    // NaN is why this needs its own assertion rather than a value comparison: NaN compares false
    // against everything, so `EXPECT_NEAR(anything, nan)` fails with a confusing message and
    // `EXPECT_NE` passes. std::isfinite is the only honest check.
    const MeshOut mesh = ExtractFirst(TangentDocument(
        {0.0f, 0.0f, 0.0f,  1.0f, 0.0f, 0.0f,  0.0f, 1.0f, 0.0f},
        {0.0f, 0.0f, 1.0f,  0.0f, 0.0f, 1.0f,  0.0f, 0.0f, 1.0f},
        {0.5f, 0.5f,  0.5f, 0.5f,  0.5f, 0.5f},
        {}));
    ASSERT_EQ(3u, mesh.vertexBytes.size() / static_cast<std::size_t>(mesh.stride));

    for (std::size_t v = 0; v < 3; ++v)
    {
        SCOPED_TRACE("vertex " + std::to_string(v));
        const std::vector<float> tangent = TangentOfVertex(mesh, v);
        for (std::size_t c = 0; c < 4; ++c)
        {
            EXPECT_TRUE(std::isfinite(tangent[c]))
                << "component " << c << " is not finite -- a degenerate UV triangle divided by a "
                   "zero area and the NaN reached the vertex buffer";
        }
        EXPECT_TRUE(tangent[3] == 1.0f || tangent[3] == -1.0f)
            << "the handedness must still be a sign, even when the basis could not be solved";
    }
}
