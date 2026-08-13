// SPDX-License-Identifier: MS-PL
//
// plan_gltf.md GLTF-087 / GLTF-088 / GLTF-096 / GLTF-185 / GLTF-187: which UV channel a primitive
// bakes, and what is baked into it.
//
// CNA carries one UV channel (`GLTF-181`'s documented limit), so the choice of *which* glTF
// TEXCOORD set fills it, and which `KHR_texture_transform` is applied to it, are decisions made
// once per primitive and invisible afterwards. Choosing wrong does not fail: it samples a texture
// with somebody else's coordinates and produces a plausible, wrong image.

#include <cmath>
#include <cstdint>
#include <cstring>
#include <gtest/gtest.h>
#include <string>
#include <vector>

#include "CNA/Internal/GltfImport/GltfImportCore.hpp"
#include "CNA/Internal/Graphics/VertexDeclarationFidelity.hpp"
#include "GltfFixtureCorpus.hpp"

using namespace CNA::Internal::GltfImport;
using namespace Microsoft::Xna::Framework::Graphics;

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

    //: TEXCOORD_0 and TEXCOORD_1 hold deliberately disjoint values, so which set was baked is
    //: readable off any single vertex rather than needing the whole array compared.
    const std::vector<float> kUv0 = {0.10f, 0.20f,  0.30f, 0.40f,  0.50f, 0.60f};
    const std::vector<float> kUv1 = {0.75f, 0.80f,  0.85f, 0.90f,  0.95f, 0.99f};

    /// A primitive with POSITION, TEXCOORD_0 and TEXCOORD_1, and a base-colour texture whose
    /// `texCoord` / `KHR_texture_transform` block is `baseColorExtras`.
    std::string TwoUvSetDocument(const std::string& baseColorExtras)
    {
        std::vector<std::uint8_t> buffer;
        AppendFloats(buffer, {0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f});
        const std::size_t uv0Offset = buffer.size();
        AppendFloats(buffer, kUv0);
        const std::size_t uv1Offset = buffer.size();
        AppendFloats(buffer, kUv1);

        return std::string(R"GLTF({
  "asset": { "version": "2.0" },
  "scene": 0,
  "scenes": [ { "nodes": [0] } ],
  "nodes": [ { "name": "MeshNode", "mesh": 0 } ],
  "extensionsUsed": [ "KHR_texture_transform" ],
  "meshes": [ { "primitives": [ {
      "attributes": { "POSITION": 0, "TEXCOORD_0": 1, "TEXCOORD_1": 2 },
      "material": 0, "mode": 4
  } ] } ],
  "materials": [ { "pbrMetallicRoughness": {
      "baseColorTexture": { "index": 0)GLTF") + baseColorExtras + R"GLTF( }
  } } ],
  "textures": [ { "source": 0 } ],
  "images": [ { "uri": "t.png", "mimeType": "image/png" } ],
  "buffers": [ { "byteLength": )GLTF" + std::to_string(buffer.size()) +
               R"GLTF(, "uri": "data:application/octet-stream;base64,)GLTF" + Base64(buffer) +
               R"GLTF(" } ],
  "bufferViews": [
    { "buffer": 0, "byteOffset": 0, "byteLength": 36 },
    { "buffer": 0, "byteOffset": )GLTF" + std::to_string(uv0Offset) + R"GLTF(, "byteLength": 24 },
    { "buffer": 0, "byteOffset": )GLTF" + std::to_string(uv1Offset) + R"GLTF(, "byteLength": 24 }
  ],
  "accessors": [
    { "bufferView": 0, "componentType": 5126, "count": 3, "type": "VEC3", "min": [0,0,0], "max": [1,1,0] },
    { "bufferView": 1, "componentType": 5126, "count": 3, "type": "VEC2" },
    { "bufferView": 2, "componentType": 5126, "count": 3, "type": "VEC2" }
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

    MeshOut ExtractFirst(const std::string& json)
    {
        Parsed parsed;
        EXPECT_TRUE(Parse(parsed, json)) << "the fixture document did not parse";
        if (parsed.data == nullptr || parsed.data->meshes_count == 0) { return MeshOut{}; }
        return ExtractMesh(parsed.data, parsed.data->meshes[0].primitives[0], "probe", nullptr,
                            1.0f);
    }

    std::vector<float> UvOfVertex(const MeshOut& mesh, std::size_t vertex)
    {
        const CNA::Internal::Graphics::InferredVertexLayout layout =
            CNA::Internal::Graphics::InferredLayoutForStride(
                mesh.stride, CNA::Internal::Graphics::UnlistedStrideLayout::RendererRefusesIt);
        EXPECT_TRUE(layout.known);
        int offset = -1;
        for (std::size_t i = 0; layout.known && i < layout.count; ++i)
        {
            if (layout.elements[i].usage == VertexElementUsage::TextureCoordinate &&
                layout.elements[i].usageIndex == 0)
            {
                offset = layout.elements[i].offset;
            }
        }
        EXPECT_GE(offset, 0);
        std::vector<float> uv(2);
        if (offset >= 0)
        {
            std::memcpy(uv.data(),
                        mesh.vertexBytes.data() + vertex * static_cast<std::size_t>(mesh.stride) +
                            static_cast<std::size_t>(offset),
                        2 * sizeof(float));
        }
        return uv;
    }
}

// --- GLTF-087: TEXCOORD_0 arrives verbatim ------------------------------------------------------

TEST(GltfUvChannel, TexcoordZeroIsBakedVerbatimWhenNothingSelectsAnotherSet)
{
    // The default, and the case every other test here is a deviation from.
    const MeshOut mesh = ExtractFirst(TwoUvSetDocument(""));
    ASSERT_EQ(3u, mesh.vertexBytes.size() / static_cast<std::size_t>(mesh.stride));
    for (std::size_t v = 0; v < 3; ++v)
    {
        SCOPED_TRACE("vertex " + std::to_string(v));
        const std::vector<float> uv = UvOfVertex(mesh, v);
        EXPECT_NEAR(kUv0[v * 2], uv[0], kTolerance);
        EXPECT_NEAR(kUv0[v * 2 + 1], uv[1], kTolerance);
    }
}

// --- GLTF-088 / GLTF-096: the base-colour texture selects the set -------------------------------

TEST(GltfUvChannel, TheBaseColourTexturesTexCoordSelectsWhichSetIsBaked)
{
    // §3.9.2's `texCoord` names which TEXCOORD_n a texture samples. With one UV channel to fill,
    // that choice decides the whole primitive's UVs -- and a hardcoded TEXCOORD_0 would silently
    // sample a lightmap-authored texture with the diffuse set's coordinates.
    //
    // The two sets are disjoint, so which one was baked is readable off any single vertex.
    const MeshOut mesh = ExtractFirst(TwoUvSetDocument(R"(, "texCoord": 1)"));
    ASSERT_EQ(3u, mesh.vertexBytes.size() / static_cast<std::size_t>(mesh.stride));
    for (std::size_t v = 0; v < 3; ++v)
    {
        SCOPED_TRACE("vertex " + std::to_string(v));
        const std::vector<float> uv = UvOfVertex(mesh, v);
        EXPECT_NEAR(kUv1[v * 2], uv[0], kTolerance)
            << "TEXCOORD_0 was baked although the base-colour texture names set 1";
        EXPECT_NEAR(kUv1[v * 2 + 1], uv[1], kTolerance);
    }
}

// --- GLTF-187: the transform's own texCoord overrides the view's --------------------------------

TEST(GltfUvChannel, TheTextureTransformsOwnTexCoordOverridesTheTextureViews)
{
    // KHR_texture_transform may carry its own `texCoord`, and the extension makes it override the
    // view's. Here they disagree deliberately: the view says set 0 and the transform says set 1, so
    // an implementation that reads only the view bakes exactly the wrong one -- and the result is a
    // fully textured mesh, just with the other set's coordinates.
    const MeshOut mesh = ExtractFirst(TwoUvSetDocument(
        R"(, "texCoord": 0, "extensions": { "KHR_texture_transform": { "texCoord": 1 } })"));
    ASSERT_EQ(3u, mesh.vertexBytes.size() / static_cast<std::size_t>(mesh.stride));
    const std::vector<float> uv = UvOfVertex(mesh, 0);
    EXPECT_NEAR(kUv1[0], uv[0], kTolerance)
        << "the view's texCoord won over the transform's; the extension says the opposite";
    EXPECT_NEAR(kUv1[1], uv[1], kTolerance);
}

// --- GLTF-185: the transform's own arithmetic ---------------------------------------------------

TEST(GltfUvChannel, TheTextureTransformScalesThenRotatesThenTranslates)
{
    // The spec's reference formula is scale, then rotate, then translate, and the order is the
    // whole content of the rule: any other order gives a different image for the same three
    // numbers. The parameters here are chosen so no two orders agree -- a non-uniform scale, a
    // quarter turn, and an offset that is not symmetric.
    //
    // Vertex 0's UV is (0.10, 0.20). Scale by (2, 4) gives (0.20, 0.80). Rotating by -90 degrees
    // about the origin -- glTF's rotation is clockwise in UV space, hence the sign -- maps (u, v)
    // to (u*cos + v*sin, -u*sin + v*cos) with cos = 0, sin = 1, giving (0.80, -0.20). Translating
    // by (0.5, 0.25) gives (1.30, 0.05).
    const float pi = 3.14159265358979323846f;
    const MeshOut mesh = ExtractFirst(TwoUvSetDocument(
        R"(, "extensions": { "KHR_texture_transform": {
              "scale": [2, 4], "rotation": )" + std::to_string(pi / 2.0f) +
        R"(, "offset": [0.5, 0.25] } })"));
    ASSERT_EQ(3u, mesh.vertexBytes.size() / static_cast<std::size_t>(mesh.stride));

    const std::vector<float> uv = UvOfVertex(mesh, 0);
    const float c = std::cos(pi / 2.0f);
    const float sn = std::sin(pi / 2.0f);
    const float su = kUv0[0] * 2.0f;
    const float sv = kUv0[1] * 4.0f;
    const float expectedU = c * su - sn * sv + 0.5f;
    const float expectedV = sn * su + c * sv + 0.25f;
    EXPECT_NEAR(expectedU, uv[0], kTolerance)
        << "the transform was not applied as scale, then rotate, then translate";
    EXPECT_NEAR(expectedV, uv[1], kTolerance);
}

TEST(GltfUvChannel, AnIdentityTextureTransformLeavesTheUvsExactlyAlone)
{
    // The control. A transform declaring the extension's own defaults -- unit scale, no rotation,
    // no offset -- must be a no-op, or every material that mentions KHR_texture_transform without
    // meaning to change anything shifts its textures.
    const MeshOut mesh = ExtractFirst(TwoUvSetDocument(
        R"(, "extensions": { "KHR_texture_transform": { } })"));
    const std::vector<float> uv = UvOfVertex(mesh, 0);
    EXPECT_NEAR(kUv0[0], uv[0], kTolerance);
    EXPECT_NEAR(kUv0[1], uv[1], kTolerance);
}

TEST(GltfUvChannel, CorpusPerMapTransformNamesTheMapThatCannotShareTheBakedTransform)
{
    // This is `GLTF-184`'s documented limit in a permanent corpus asset. The base colour and
    // normal map both use TEXCOORD_0 but author different non-neutral transforms. One shared UV
    // stream can carry only the base colour's baked result, so the normal map must be named rather
    // than silently sampled with somebody else's transform.
    const CnaTest::GltfOracle::LoadedFixture fixture("texture-transform-per-map");
    ASSERT_TRUE(fixture.Ok()) << fixture.Error();
    ASSERT_GT(fixture.Data().meshes_count, 0u);
    ASSERT_GT(fixture.Data().meshes[0].primitives_count, 0u);

    const MeshOut mesh = ExtractMesh(&fixture.Data(), fixture.Data().meshes[0].primitives[0],
                                     "probe", nullptr, 1.0f);
    ASSERT_EQ(1u, mesh.unbakedTextureTransformsEXT.size());
    EXPECT_EQ("normal", mesh.unbakedTextureTransformsEXT.front());
}

// --- GLTF-174: tangent generation uses the generated normals ---------------------------------------

TEST(GltfUvChannel, TangentGenerationUsesTheGeneratedNormalRatherThanAFabricatedPlusZ)
{
    // Two independent fallbacks meet here. §3.7.2.1 says a primitive with no NORMAL gets flat
    // normals (`GLTF-173`), and a PBR primitive with no TANGENT gets a generated basis
    // (`ComputeTangentsEXT`). `ComputeTangentsEXT` takes the normals as an argument and falls back
    // to (0,0,1) when the array is empty -- so if generation ran in the wrong order, the tangent
    // basis would be built against a fabricated +Z normal while the vertex buffer carried the real
    // one. The two would then disagree, and a normal-mapped surface would light from a basis that
    // does not match its own normal: subtly, plausibly wrong.
    //
    // The triangle here faces -Z, so a fabricated +Z normal is the exact opposite of the truth and
    // the two orders cannot be confused.
    Parsed doc;
    ASSERT_TRUE(Parse(doc, R"GLTF({
  "asset": { "version": "2.0" },
  "scene": 0,
  "scenes": [ { "nodes": [0] } ],
  "nodes": [ { "mesh": 0 } ],
  "materials": [ { "pbrMetallicRoughness": { "metallicFactor": 1.0 } } ],
  "meshes": [ { "primitives": [ { "attributes": { "POSITION": 0, "TEXCOORD_0": 1 },
                                  "indices": 2, "material": 0 } ] } ],
  "buffers": [ { "byteLength": 68, "uri": "data:application/octet-stream;base64,AAAAAAAAAAAAAAAAAAAAAAAAAAAAAIA/AAAAAAAAgD8AAAAAAAAAAAAAAAAAAIA/AAAAAAAAAAAAAIA/AAABAAIAAAA=" } ],
  "bufferViews": [
    { "buffer": 0, "byteOffset": 0,  "byteLength": 36 },
    { "buffer": 0, "byteOffset": 36, "byteLength": 24 },
    { "buffer": 0, "byteOffset": 60, "byteLength": 6 }
  ],
  "accessors": [
    { "bufferView": 0, "componentType": 5126, "count": 3, "type": "VEC3",
      "min": [0, 0, 0], "max": [0, 1, 1] },
    { "bufferView": 1, "componentType": 5126, "count": 3, "type": "VEC2" },
    { "bufferView": 2, "componentType": 5123, "count": 3, "type": "SCALAR" }
  ]
})GLTF"));

    const MeshOut mesh = ExtractMesh(doc.data, doc.data->meshes[0].primitives[0], "probe",
                                      nullptr, 1.0f);
    ASSERT_EQ(48, mesh.stride) << "this primitive must land on a layout that has a tangent";
    EXPECT_TRUE(mesh.generatedNormalsEXT) << "the fixture must author no NORMAL";

    const CNA::Internal::Graphics::InferredVertexLayout layout =
        CNA::Internal::Graphics::InferredLayoutForStride(
            mesh.stride, CNA::Internal::Graphics::UnlistedStrideLayout::RendererRefusesIt);
    ASSERT_TRUE(layout.known);
    int normalOffset = -1, tangentOffset = -1;
    for (std::size_t e = 0; e < layout.count; ++e)
    {
        if (layout.elements[e].usage == VertexElementUsage::Normal)
        {
            normalOffset = layout.elements[e].offset;
        }
        if (layout.elements[e].usage == VertexElementUsage::Tangent)
        {
            tangentOffset = layout.elements[e].offset;
        }
    }
    ASSERT_GE(normalOffset, 0);
    ASSERT_GE(tangentOffset, 0);

    for (std::size_t v = 0; v < 3; ++v)
    {
        SCOPED_TRACE("vertex " + std::to_string(v));
        float normal[3];
        float tangent[4];
        std::memcpy(normal, mesh.vertexBytes.data() + v * 48u +
                                static_cast<std::size_t>(normalOffset), sizeof(normal));
        std::memcpy(tangent, mesh.vertexBytes.data() + v * 48u +
                                 static_cast<std::size_t>(tangentOffset), sizeof(tangent));

        // The generated normal is the triangle's own, which for this winding is -X.
        EXPECT_NEAR(-1.0f, normal[0], kTolerance)
            << "the generated normal is not the triangle's own facing";

        // And the tangent is perpendicular to it. Built against a fabricated (0,0,1) instead, the
        // tangent would lie in the XY plane and this dot product would not vanish.
        const float dot = normal[0] * tangent[0] + normal[1] * tangent[1] + normal[2] * tangent[2];
        EXPECT_NEAR(0.0f, dot, 1e-3f)
            << "the tangent basis was built against a different normal than the one packed";

        // A degenerate tangent would satisfy perpendicularity trivially.
        const float length = std::sqrt(tangent[0] * tangent[0] + tangent[1] * tangent[1] +
                                        tangent[2] * tangent[2]);
        EXPECT_GT(length, 0.5f) << "the generated tangent is degenerate";
        EXPECT_TRUE(tangent[3] == 1.0f || tangent[3] == -1.0f)
            << "handedness must be a sign, not a scale: " << tangent[3];
    }
}

// --- GLTF-177: what import does NOT renormalise ----------------------------------------------------

TEST(GltfUvChannel, AnAuthoredNonUnitNormalSurvivesImportUnchanged)
{
    // The other half of the renormalisation policy (docs/gltf-conventions.md), and the one that
    // looks wrong until it is written down: a normal the file authored at length 2 arrives in the
    // vertex buffer at length 2. §3.7.2.1 requires unit length, so such a file is malformed -- but
    // "fixing" it at import would hide a broken export AND make the vertex bytes disagree with the
    // accessor they came from, which is exactly what the L5 goldens compare. The shader's own
    // `normalize` makes it harmless at draw time.
    Parsed doc;
    ASSERT_TRUE(Parse(doc, R"GLTF({
  "asset": { "version": "2.0" },
  "scene": 0,
  "scenes": [ { "nodes": [0] } ],
  "nodes": [ { "mesh": 0 } ],
  "meshes": [ { "primitives": [ { "attributes": { "POSITION": 0, "NORMAL": 1 },
                                  "indices": 2 } ] } ],
  "buffers": [ { "byteLength": 80, "uri": "data:application/octet-stream;base64,AAAAAAAAAAAAAAAAAACAPwAAAAAAAAAAAAAAAAAAgD8AAAAAAAAAAAAAAAAAAABAAAAAAAAAAAAAAABAAAAAAAAAAAAAAABAAAABAAIAAAA=" } ],
  "bufferViews": [
    { "buffer": 0, "byteOffset": 0,  "byteLength": 36 },
    { "buffer": 0, "byteOffset": 36, "byteLength": 36 },
    { "buffer": 0, "byteOffset": 72, "byteLength": 6 }
  ],
  "accessors": [
    { "bufferView": 0, "componentType": 5126, "count": 3, "type": "VEC3",
      "min": [0, 0, 0], "max": [1, 1, 0] },
    { "bufferView": 1, "componentType": 5126, "count": 3, "type": "VEC3" },
    { "bufferView": 2, "componentType": 5123, "count": 3, "type": "SCALAR" }
  ]
})GLTF"));

    const MeshOut mesh = ExtractMesh(doc.data, doc.data->meshes[0].primitives[0], "probe",
                                      nullptr, 1.0f);
    EXPECT_FALSE(mesh.generatedNormalsEXT) << "the fixture authors its own normals";

    const CNA::Internal::Graphics::InferredVertexLayout layout =
        CNA::Internal::Graphics::InferredLayoutForStride(
            mesh.stride, CNA::Internal::Graphics::UnlistedStrideLayout::RendererRefusesIt);
    ASSERT_TRUE(layout.known);
    int normalOffset = -1;
    for (std::size_t e = 0; e < layout.count; ++e)
    {
        if (layout.elements[e].usage == VertexElementUsage::Normal)
        {
            normalOffset = layout.elements[e].offset;
        }
    }
    ASSERT_GE(normalOffset, 0);

    float normal[3];
    std::memcpy(normal, mesh.vertexBytes.data() + static_cast<std::size_t>(normalOffset),
                sizeof(normal));
    EXPECT_NEAR(2.0f, normal[2], kTolerance)
        << "import renormalised an authored normal, so the vertex bytes no longer say what the "
           "accessor said";
}
