// SPDX-License-Identifier: MS-PL
//
// plans/plan_gltf.md GLTF-029 / GLTF-033 / GLTF-090 / GLTF-091 / GLTF-092 / GLTF-094 / GLTF-255: the two
// file-level forms every fixture in the corpus depends on, the two weight encodings none of them
// uses, and the attributes CNA has nowhere to put.
//
// The `data:` buffer and the version check are load-bearing for the whole corpus -- every generated
// asset is self-contained, so a base64 decoder that mishandled padding would break every fixture at
// once and a version check that let a glTF 1.0 file through would decode a different format's
// object model as though it were this one.
//
// The weight encodings are the opposite situation: §3.7.3.3 allows `FLOAT`, normalized
// `UNSIGNED_BYTE` and normalized `UNSIGNED_SHORT`, and every fixture in the corpus authors FLOAT.
// The other two were entirely unexercised, and they are where GLTF-256's renormalisation meets
// quantisation -- a `u8` weight set can only sum to 1 when its four values happen to be
// representable, so "renormalise when the sum is off" and "do not report exporter quantisation as a
// broken file" collide precisely here.

#include <cmath>
#include <cstdint>
#include <cstring>
#include <gtest/gtest.h>
#include <string>
#include <vector>

#include "CNA/Internal/GltfImport/GltfImportCore.hpp"
#include "CNA/Internal/Graphics/VertexDeclarationFidelity.hpp"

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

    bool Parse(Parsed& out, const std::string& json)
    {
        cgltf_options options{};
        if (cgltf_parse(&options, json.data(), json.size(), &out.data) != cgltf_result_success)
        {
            return false;
        }
        return cgltf_load_buffers(&options, out.data, ".") == cgltf_result_success;
    }

    /// A file whose single buffer is `payload`, exposed as one bufferView and one SCALAR float
    /// accessor of `count` elements.
    std::string DataUriBufferDocument(const std::vector<std::uint8_t>& payload, std::size_t count)
    {
        return std::string(R"GLTF({
  "asset": { "version": "2.0" },
  "buffers": [ { "byteLength": )GLTF") + std::to_string(payload.size()) +
               R"GLTF(, "uri": "data:application/octet-stream;base64,)GLTF" + Base64(payload) +
               R"GLTF(" } ],
  "bufferViews": [ { "buffer": 0, "byteOffset": 0, "byteLength": )GLTF" +
               std::to_string(count * 4) + R"GLTF( } ],
  "accessors": [
    { "bufferView": 0, "componentType": 5126, "count": )GLTF" + std::to_string(count) +
               R"GLTF(, "type": "SCALAR" }
  ]
})GLTF";
    }

    /// A one-joint skinned primitive whose WEIGHTS_0 accessor has the given component type. The
    /// raw values are written at `componentBytes` each, little-endian.
    std::string WeightFormDocument(int componentType, std::size_t componentBytes,
                                    const std::vector<unsigned long long>& rawWeights)
    {
        std::vector<std::uint8_t> buffer;
        AppendFloats(buffer, {0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f});
        const std::size_t jointsOffset = buffer.size();
        for (int v = 0; v < 3; ++v) { buffer.insert(buffer.end(), {0, 0, 0, 0}); }
        const std::size_t weightsOffset = buffer.size();
        for (const unsigned long long value : rawWeights)
        {
            for (std::size_t b = 0; b < componentBytes; ++b)
            {
                buffer.push_back(static_cast<std::uint8_t>((value >> (8 * b)) & 0xFF));
            }
        }
        while (buffer.size() % 4 != 0) { buffer.push_back(0); }
        const std::size_t ibmOffset = buffer.size();
        AppendFloats(buffer, {1, 0, 0, 0,  0, 1, 0, 0,  0, 0, 1, 0,  0, 0, 0, 1});

        return std::string(R"GLTF({
  "asset": { "version": "2.0" },
  "scene": 0,
  "scenes": [ { "nodes": [0, 1] } ],
  "nodes": [ { "name": "Joint0" }, { "name": "SkinnedMeshNode", "mesh": 0, "skin": 0 } ],
  "skins": [ { "name": "Skin", "joints": [0], "inverseBindMatrices": 3 } ],
  "meshes": [ { "primitives": [ {
      "attributes": { "POSITION": 0, "JOINTS_0": 1, "WEIGHTS_0": 2 }, "mode": 4
  } ] } ],
  "buffers": [ { "byteLength": )GLTF") + std::to_string(buffer.size()) +
               R"GLTF(, "uri": "data:application/octet-stream;base64,)GLTF" + Base64(buffer) +
               R"GLTF(" } ],
  "bufferViews": [
    { "buffer": 0, "byteOffset": 0, "byteLength": 36 },
    { "buffer": 0, "byteOffset": )GLTF" + std::to_string(jointsOffset) + R"GLTF(, "byteLength": 12 },
    { "buffer": 0, "byteOffset": )GLTF" + std::to_string(weightsOffset) + R"GLTF(, "byteLength": )GLTF" +
               std::to_string(rawWeights.size() * componentBytes) + R"GLTF( },
    { "buffer": 0, "byteOffset": )GLTF" + std::to_string(ibmOffset) + R"GLTF(, "byteLength": 64 }
  ],
  "accessors": [
    { "bufferView": 0, "componentType": 5126, "count": 3, "type": "VEC3", "min": [0,0,0], "max": [1,1,0] },
    { "bufferView": 1, "componentType": 5121, "count": 3, "type": "VEC4" },
    { "bufferView": 2, "componentType": )GLTF" + std::to_string(componentType) +
               R"GLTF(, "count": 3, "type": "VEC4", "normalized": true },
    { "bufferView": 3, "componentType": 5126, "count": 1, "type": "MAT4" }
  ]
})GLTF";
    }

    std::vector<float> BlendWeightsOfVertex(const MeshOut& mesh, std::size_t vertex)
    {
        const CNA::Internal::Graphics::InferredVertexLayout layout =
            CNA::Internal::Graphics::InferredLayoutForStride(
                mesh.stride, CNA::Internal::Graphics::UnlistedStrideLayout::RendererRefusesIt);
        EXPECT_TRUE(layout.known);
        int offset = -1;
        for (std::size_t i = 0; layout.known && i < layout.count; ++i)
        {
            if (layout.elements[i].usage ==
                    Microsoft::Xna::Framework::Graphics::VertexElementUsage::BlendWeight &&
                layout.elements[i].usageIndex == 0)
            {
                offset = layout.elements[i].offset;
            }
        }
        EXPECT_GE(offset, 0);
        std::vector<float> weights(4);
        if (offset >= 0)
        {
            std::memcpy(weights.data(),
                        mesh.vertexBytes.data() + vertex * static_cast<std::size_t>(mesh.stride) +
                            static_cast<std::size_t>(offset),
                        4 * sizeof(float));
        }
        return weights;
    }

    MeshOut ExtractSkinned(const Parsed& parsed)
    {
        const SceneGraphOut scene = BuildSceneGraph(parsed.data);
        const SkeletonResult skeleton =
            BuildSkeleton(parsed.data->skins, scene, Matrix::getIdentityProperty(), 1.0f);
        return ExtractMesh(parsed.data, parsed.data->meshes[0].primitives[0], "probe", &skeleton,
                            1.0f);
    }
}

// --- GLTF-029: base64 buffers, at all three padding lengths -------------------------------------

// --- GLTF-468: the two core attribute storage forms no corpus asset used ------------------------

namespace
{
    /// A triangle whose `COLOR_0` is a **VEC3** float accessor, plus a `TEXCOORD_0` whose component
    /// type is `texcoordComponentType` (0 = FLOAT, otherwise the normalized integer type).
    ///
    /// Both forms are ordinary core glTF that the corpus had no asset for: §3.7.2.1's attribute table
    /// allows `COLOR_n` as **VEC3 or VEC4** and `TEXCOORD_n` as float, unsigned byte normalized or
    /// unsigned short normalized, and every colour fixture in the corpus is VEC4 while every UV
    /// fixture is float.
    std::string ColorAndTexcoordFormDocument(bool colorIsVec3, int texcoordComponentType)
    {
        std::vector<std::uint8_t> buffer;
        AppendFloats(buffer, {0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f});
        const std::size_t colorOffset = buffer.size();
        // Deliberately distinct per vertex AND per channel, so a decoder that read four components
        // from a three-component accessor would smear one vertex's blue into the next vertex's red
        // and fail differently on every vertex rather than uniformly.
        if (colorIsVec3)
        {
            AppendFloats(buffer, {1.0f, 0.25f, 0.5f,
                                  0.125f, 1.0f, 0.75f,
                                  0.375f, 0.625f, 1.0f});
        }
        else
        {
            AppendFloats(buffer, {1.0f, 0.25f, 0.5f, 0.25f,
                                  0.125f, 1.0f, 0.75f, 0.5f,
                                  0.375f, 0.625f, 1.0f, 0.75f});
        }
        const std::size_t texcoordOffset = buffer.size();
        std::size_t texcoordLength = 0;
        if (texcoordComponentType == 0)
        {
            AppendFloats(buffer, {0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f});
            texcoordLength = 24;
        }
        else if (texcoordComponentType == 5121)
        {
            // 0, 51/255 = 0.2, 255 -> 1.0. Values chosen so each decodes exactly.
            const std::uint8_t bytes[] = {0, 0, 255, 0, 0, 255};
            buffer.insert(buffer.end(), bytes, bytes + sizeof(bytes));
            texcoordLength = sizeof(bytes);
            while (buffer.size() % 4 != 0) { buffer.push_back(0); }
        }
        else
        {
            const std::uint16_t values[] = {0, 0, 65535, 0, 0, 65535};
            for (const std::uint16_t value : values)
            {
                buffer.push_back(static_cast<std::uint8_t>(value & 0xFF));
                buffer.push_back(static_cast<std::uint8_t>((value >> 8) & 0xFF));
            }
            texcoordLength = sizeof(values);
        }
        const std::size_t indexOffset = buffer.size();
        for (const std::uint16_t index : {0, 1, 2})
        {
            buffer.push_back(static_cast<std::uint8_t>(index & 0xFF));
            buffer.push_back(static_cast<std::uint8_t>((index >> 8) & 0xFF));
        }
        while (buffer.size() % 4 != 0) { buffer.push_back(0); }

        const std::string colorType = colorIsVec3 ? "VEC3" : "VEC4";
        const std::size_t colorLength = colorIsVec3 ? 36u : 48u;
        const std::string texcoordAccessor = texcoordComponentType == 0
            ? R"({ "bufferView": 2, "componentType": 5126, "count": 3, "type": "VEC2" })"
            : std::string(R"({ "bufferView": 2, "componentType": )") +
              std::to_string(texcoordComponentType) +
              R"(, "count": 3, "type": "VEC2", "normalized": true })";

        return std::string(R"GLTF({
  "asset": { "version": "2.0" },
  "scene": 0,
  "scenes": [ { "nodes": [0] } ],
  "nodes": [ { "name": "MeshNode", "mesh": 0 } ],
  "meshes": [ { "name": "FormTri", "primitives": [ {
      "attributes": { "POSITION": 0, "COLOR_0": 1, "TEXCOORD_0": 2 },
      "indices": 3, "mode": 4
  } ] } ],
  "buffers": [ { "byteLength": )GLTF") + std::to_string(buffer.size()) +
               R"GLTF(, "uri": "data:application/octet-stream;base64,)GLTF" + Base64(buffer) +
               R"GLTF(" } ],
  "bufferViews": [
    { "buffer": 0, "byteOffset": 0, "byteLength": 36 },
    { "buffer": 0, "byteOffset": )GLTF" + std::to_string(colorOffset) +
               R"GLTF(, "byteLength": )GLTF" + std::to_string(colorLength) + R"GLTF( },
    { "buffer": 0, "byteOffset": )GLTF" + std::to_string(texcoordOffset) +
               R"GLTF(, "byteLength": )GLTF" + std::to_string(texcoordLength) + R"GLTF( },
    { "buffer": 0, "byteOffset": )GLTF" + std::to_string(indexOffset) + R"GLTF(, "byteLength": 6 }
  ],
  "accessors": [
    { "bufferView": 0, "componentType": 5126, "count": 3, "type": "VEC3",
      "min": [0,0,0], "max": [1,1,0] },
    { "bufferView": 1, "componentType": 5126, "count": 3, "type": ")GLTF" + colorType +
               R"GLTF(" },
    )GLTF" + texcoordAccessor + R"GLTF(,
    { "bufferView": 3, "componentType": 5123, "count": 3, "type": "SCALAR" }
  ]
})GLTF";
    }

    int OffsetOfUsage(int stride, Microsoft::Xna::Framework::Graphics::VertexElementUsage usage)
    {
        const CNA::Internal::Graphics::InferredVertexLayout layout =
            CNA::Internal::Graphics::InferredLayoutForStride(
                stride, CNA::Internal::Graphics::UnlistedStrideLayout::RendererRefusesIt);
        EXPECT_TRUE(layout.known) << "stride " << stride << " is not in the canonical table";
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

TEST(GltfBufferAndWeightForm, AVec3Color0GetsAnOpaqueAlphaAndKeepsItsChannelsInOrder)
{
    // §3.7.2.1 allows `COLOR_n` as **VEC3 or VEC4**, and a VEC3 colour has no alpha -- the
    // specification's default is fully opaque. Every colour fixture in the corpus is VEC4, so the
    // three-component form had no asset behind it at all, and `GLTF-462` made that matter: the packed
    // colour is now a multiplier on base colour **including alpha**, so a VEC3 colour whose alpha came
    // back as 0 would turn the whole surface invisible rather than merely mis-tint it.
    //
    // The nine channel values are all distinct, which is the discrimination: a decoder that read four
    // floats per element from a three-float accessor would slide one vertex's blue into the next
    // vertex's red, and every vertex would then be wrong differently.
    Parsed parsed;
    ASSERT_TRUE(Parse(parsed, ColorAndTexcoordFormDocument(true, 0)));
    const MeshOut out = ExtractMesh(parsed.data, parsed.data->meshes[0].primitives[0], "probe",
                                    nullptr, 1.0f);

    ASSERT_GT(out.stride, 0);
    const int colorOffset =
        OffsetOfUsage(out.stride, Microsoft::Xna::Framework::Graphics::VertexElementUsage::Color);
    ASSERT_GE(colorOffset, 0);
    const std::size_t vertices =
        out.vertexBytes.size() / static_cast<std::size_t>(out.stride);
    ASSERT_EQ(3u, vertices);

    const float expected[3][3] = {{1.0f, 0.25f, 0.5f},
                                  {0.125f, 1.0f, 0.75f},
                                  {0.375f, 0.625f, 1.0f}};
    for (std::size_t v = 0; v < 3; ++v)
    {
        SCOPED_TRACE("vertex " + std::to_string(v));
        const std::uint8_t* rgba = out.vertexBytes.data() +
                                   v * static_cast<std::size_t>(out.stride) +
                                   static_cast<std::size_t>(colorOffset);
        for (std::size_t c = 0; c < 3; ++c)
        {
            const auto want = static_cast<int>(expected[v][c] * 255.0f + 0.5f);
            EXPECT_EQ(want, static_cast<int>(rgba[c])) << "channel " << c;
        }
        EXPECT_EQ(255, static_cast<int>(rgba[3]))
            << "a VEC3 COLOR_0 has no alpha, and §3.7.2.1's default is fully opaque -- an alpha of 0 "
               "would make this surface invisible now that the colour multiplies base-colour alpha";
    }
}

TEST(GltfBufferAndWeightForm, ANormalizedIntegerTexcoordDecodesToTheSameUvsAsItsFloatTwin)
{
    // §3.7.2.1 allows `TEXCOORD_n` as float, **unsigned byte normalized** or **unsigned short
    // normalized**; every UV in the corpus is a plain float, so two of the three legal storage forms
    // had no asset. This is not hypothetical -- quantised UVs are what a size-conscious exporter
    // emits, and the failure mode of reading one as raw integers is a texture coordinate of 255 or
    // 65535, which wraps to the same texel as 0 and so looks plausible on a tiling texture.
    //
    // The float twin is the oracle: all three forms author the same UVs, so the assertion is that the
    // packed bytes AGREE rather than that they match a restated constant.
    Parsed floatParsed;
    ASSERT_TRUE(Parse(floatParsed, ColorAndTexcoordFormDocument(false, 0)));
    const MeshOut floatOut = ExtractMesh(
        floatParsed.data, floatParsed.data->meshes[0].primitives[0], "probe", nullptr, 1.0f);
    const int uvOffset = OffsetOfUsage(
        floatOut.stride,
        Microsoft::Xna::Framework::Graphics::VertexElementUsage::TextureCoordinate);
    ASSERT_GE(uvOffset, 0);

    for (const int componentType : {5121, 5123})
    {
        SCOPED_TRACE("componentType " + std::to_string(componentType));
        Parsed parsed;
        ASSERT_TRUE(Parse(parsed, ColorAndTexcoordFormDocument(false, componentType)));
        const MeshOut out = ExtractMesh(parsed.data, parsed.data->meshes[0].primitives[0], "probe",
                                        nullptr, 1.0f);
        ASSERT_EQ(floatOut.stride, out.stride)
            << "the storage form of an attribute must not change the layout it lands in";
        ASSERT_EQ(floatOut.vertexBytes.size(), out.vertexBytes.size());

        for (std::size_t v = 0; v < 3; ++v)
        {
            SCOPED_TRACE("vertex " + std::to_string(v));
            float wanted[2];
            float got[2];
            const std::size_t at = v * static_cast<std::size_t>(out.stride) +
                                   static_cast<std::size_t>(uvOffset);
            std::memcpy(wanted, floatOut.vertexBytes.data() + at, sizeof(wanted));
            std::memcpy(got, out.vertexBytes.data() + at, sizeof(got));
            EXPECT_NEAR(wanted[0], got[0], kTolerance);
            EXPECT_NEAR(wanted[1], got[1], kTolerance);
            // And the values really are the authored 0/1 rather than the raw integers, which is what
            // separates "decoded" from "copied": 255 or 65535 would pass an agreement test against
            // another mis-decode but not this one.
            EXPECT_GE(got[0], 0.0f);
            EXPECT_LE(got[0], 1.0f);
            EXPECT_GE(got[1], 0.0f);
            EXPECT_LE(got[1], 1.0f);
        }
    }
}

TEST(GltfBufferAndWeightForm, ADataUriBufferDecodesAtEveryBase64PaddingLength)
{
    // A base64 payload ends with zero, one or two `=` depending on the byte count mod 3, and the
    // one-pad and two-pad cases are where a decoder loses or invents a trailing byte. Every asset
    // in this corpus is self-contained, so a padding bug would break all of them at once -- and
    // the failure would be a short buffer, which surfaces as a validation error about an accessor
    // rather than about the buffer.
    //
    // Three payload lengths, one per residue class: 12 bytes (no padding), 16 (two `=`) and 20
    // (one `=`). Each is a whole number of floats, so the accessor is well-formed in every case.
    for (const std::size_t floatCount : {3u, 4u, 5u})
    {
        SCOPED_TRACE(std::to_string(floatCount * 4) + "-byte payload");
        std::vector<std::uint8_t> payload;
        std::vector<float> values;
        for (std::size_t i = 0; i < floatCount; ++i) { values.push_back(static_cast<float>(i) + 1.5f); }
        AppendFloats(payload, values);

        Parsed parsed;
        ASSERT_TRUE(Parse(parsed, DataUriBufferDocument(payload, floatCount)));
        ASSERT_EQ(1u, static_cast<std::size_t>(parsed.data->buffers_count));
        ASSERT_NE(nullptr, parsed.data->buffers[0].data) << "the data: URI did not decode at all";
        ASSERT_EQ(payload.size(), static_cast<std::size_t>(parsed.data->buffers[0].size))
            << "the decoded buffer is the wrong length -- a padding case was mishandled";

        const std::uint8_t* decoded =
            static_cast<const std::uint8_t*>(parsed.data->buffers[0].data);
        for (std::size_t i = 0; i < payload.size(); ++i)
        {
            EXPECT_EQ(static_cast<int>(payload[i]), static_cast<int>(decoded[i])) << "byte " << i;
        }
    }
}

// --- GLTF-033: only glTF 2.0 ---------------------------------------------------------------------

TEST(GltfBufferAndWeightForm, AnOlderMajorVersionIsRefusedByTheParserBeforeAnythingIsDecoded)
{
    // glTF 1.0 is a different object model wearing the same file extension -- its materials are
    // GLSL techniques and its accessors index differently -- so decoding one as 2.0 does not fail
    // cleanly, it reads a structure that happens to parse. cgltf refuses it outright, so no CNA
    // code ever sees such a file.
    for (const char* version : {"1.0", "1.0.1", "0.8"})
    {
        SCOPED_TRACE(version);
        const std::string json = std::string(R"GLTF({ "asset": { "version": ")GLTF") + version +
                                 R"GLTF(" } })GLTF";
        cgltf_options options{};
        cgltf_data* data = nullptr;
        const cgltf_result result = cgltf_parse(&options, json.data(), json.size(), &data);
        if (data != nullptr) { cgltf_free(data); }
        EXPECT_NE(cgltf_result_success, result);
    }
}

TEST(GltfBufferAndWeightForm, ANewerMajorVersionParsesSoTheLoadersOwnCheckIsTheOnlyGate)
{
    // The finding that makes CNA's own `asset.version != "2.0"` comparison load-bearing rather than
    // belt-and-braces: cgltf gates on the major version being **at least** 2, so a hypothetical
    // glTF 3.0 file PARSES. A reader trusting the parser alone would then decode a future format's
    // object model as though it were this one -- which is the very failure the 1.0 refusal exists
    // to prevent, arriving from the other direction.
    //
    // Both loaders compare the string themselves, and this asserts the property that comparison
    // depends on: cgltf preserves `asset.version` verbatim, so the check is decidable at all.
    for (const char* version : {"3.0", "2.1"})
    {
        SCOPED_TRACE(version);
        Parsed parsed;
        const std::string json = std::string(R"GLTF({ "asset": { "version": ")GLTF") + version +
                                 R"GLTF(" } })GLTF";
        ASSERT_TRUE(Parse(parsed, json))
            << "if this ever starts failing, the parser has tightened and the note below is stale";
        ASSERT_NE(nullptr, parsed.data->asset.version);
        EXPECT_STREQ(version, parsed.data->asset.version)
            << "the version string was not preserved verbatim, so no loader could check it";
        EXPECT_STRNE("2.0", parsed.data->asset.version);
    }
}

TEST(GltfBufferAndWeightForm, TheVersionTwoPointZeroIsAcceptedSoTheGateIsNotSimplyAlwaysRejecting)
{
    // The control. Without it, a parser that rejected every file would satisfy the test above.
    Parsed parsed;
    ASSERT_TRUE(Parse(parsed, R"GLTF({ "asset": { "version": "2.0" } })GLTF"));
    EXPECT_STREQ("2.0", parsed.data->asset.version);
    std::vector<std::string> warnings;
    EXPECT_NO_THROW(ValidateGltfEXT(parsed.data, "ok.gltf", warnings));
}

// --- GLTF-094 / GLTF-255: the two weight encodings no corpus fixture uses -----------------------

TEST(GltfBufferAndWeightForm, NormalizedUnsignedByteWeightsDecodeAndSumToOne)
{
    // §3.7.3.3 allows FLOAT, normalized UNSIGNED_BYTE and normalized UNSIGNED_SHORT, and every
    // fixture in the corpus authors FLOAT -- so this encoding was entirely unexercised.
    //
    // 128 + 127 = 255, so c/255 gives 0.502 + 0.498 = exactly 1 and nothing is renormalised. That
    // is the point of choosing those two numbers: an off-by-one divisor (c/256) would sum to
    // 0.996, which GLTF-256 would then renormalise back to 1 -- hiding the divisor error behind
    // its own correction. Asserting the decoded values, not just their sum, is what separates them.
    Parsed parsed;
    ASSERT_TRUE(Parse(parsed, WeightFormDocument(5121, 1, {
        128, 127, 0, 0,   255, 0, 0, 0,   64, 191, 0, 0})));
    const MeshOut mesh = ExtractSkinned(parsed);
    ASSERT_EQ(3u, mesh.vertexBytes.size() / static_cast<std::size_t>(mesh.stride));
    EXPECT_EQ(0u, mesh.renormalisedWeightVertexCountEXT)
        << "a weight set that already sums to 1 was renormalised, which means the divisor is wrong "
           "and its own correction is hiding it";

    const std::vector<float> first = BlendWeightsOfVertex(mesh, 0);
    EXPECT_NEAR(128.0f / 255.0f, first[0], kTolerance);
    EXPECT_NEAR(127.0f / 255.0f, first[1], kTolerance);
    EXPECT_NEAR(0.0f, first[2], kTolerance);
    EXPECT_NEAR(0.0f, first[3], kTolerance);

    const std::vector<float> second = BlendWeightsOfVertex(mesh, 1);
    EXPECT_FLOAT_EQ(1.0f, second[0]) << "the maximum must map exactly to 1, not to 255/256";
}

TEST(GltfBufferAndWeightForm, NormalizedUnsignedShortWeightsDecodeAndSumToOne)
{
    // The same rule at 16 bits, where the divisor error (65536 instead of 65535) is 15 parts per
    // million -- far inside GLTF-256's own tolerance, so renormalisation would never fire and the
    // error would never be visible in a sum at all. Only the decoded value can catch it.
    Parsed parsed;
    ASSERT_TRUE(Parse(parsed, WeightFormDocument(5123, 2, {
        32768, 32767, 0, 0,   65535, 0, 0, 0,   16384, 49151, 0, 0})));
    const MeshOut mesh = ExtractSkinned(parsed);
    ASSERT_EQ(3u, mesh.vertexBytes.size() / static_cast<std::size_t>(mesh.stride));
    EXPECT_EQ(0u, mesh.renormalisedWeightVertexCountEXT);

    const std::vector<float> first = BlendWeightsOfVertex(mesh, 0);
    EXPECT_NEAR(32768.0f / 65535.0f, first[0], kTolerance);
    EXPECT_NEAR(32767.0f / 65535.0f, first[1], kTolerance);

    const std::vector<float> second = BlendWeightsOfVertex(mesh, 1);
    EXPECT_FLOAT_EQ(1.0f, second[0]) << "the maximum must map exactly to 1, not to 32768/65536";
}

TEST(GltfBufferAndWeightForm, AQuantisedWeightSetThatCannotSumToOneIsRenormalisedNotReportedTwice)
{
    // Where quantisation and GLTF-256 actually meet. Three equal `u8` weights cannot sum to 1: 85
    // each gives 255/255 exactly, but 84 each gives 252/255 = 0.988 -- a real exporter outcome, and
    // exactly the "slightly-off sum" GLTF-256 renormalises rather than refuses.
    //
    // What is asserted is that it is corrected AND counted: silently correcting it would hide a
    // genuinely broken file behind the same code path, and the count with its deviation is what
    // lets a caller tell 0.988 from 0.6.
    Parsed parsed;
    ASSERT_TRUE(Parse(parsed, WeightFormDocument(5121, 1, {
        84, 84, 84, 0,   255, 0, 0, 0,   255, 0, 0, 0})));
    const MeshOut mesh = ExtractSkinned(parsed);
    ASSERT_EQ(3u, mesh.vertexBytes.size() / static_cast<std::size_t>(mesh.stride));

    EXPECT_EQ(1u, mesh.renormalisedWeightVertexCountEXT)
        << "exactly the one quantised vertex should have been renormalised";
    EXPECT_LT(mesh.worstWeightSumDeviationEXT, 0.02f)
        << "the deviation is what tells exporter quantisation from a broken file, and 252/255 is "
           "the former";

    const std::vector<float> weights = BlendWeightsOfVertex(mesh, 0);
    const float sum = weights[0] + weights[1] + weights[2] + weights[3];
    EXPECT_NEAR(1.0f, sum, kTolerance)
        << "left at 0.988 the skin equation applies 98.8% of the vertex's transform, which drags it "
           "toward the origin -- H12 in miniature";
    for (std::size_t c = 0; c < 3; ++c)
    {
        EXPECT_NEAR(1.0f / 3.0f, weights[c], 1e-3f) << "component " << c;
    }
}

// --- GLTF-090 / GLTF-091 / GLTF-092: attributes with nowhere to go --------------------------------

TEST(GltfBufferAndWeightForm, ASecondColourSetAndACustomAttributeAreCountedRatherThanSilentlyLost)
{
    // Neither is an error. §3.7.2.1 reserves the `_` prefix for application-specific semantics
    // precisely so a reader may ignore them, and XNA's vertex layouts carry exactly one colour
    // channel. But both are data the file authored that nothing downstream can express, and a mesh
    // whose real tint lives in COLOR_1 imports looking like a mistake nobody can trace -- so each
    // is counted and named.
    //
    // The document below authors COLOR_0 red, COLOR_1 blue and a `_BATCHID` scalar.
    Parsed doc;
    ASSERT_TRUE(Parse(doc, R"GLTF({
  "asset": { "version": "2.0" },
  "scene": 0,
  "scenes": [ { "nodes": [0] } ],
  "nodes": [ { "mesh": 0 } ],
  "meshes": [ { "primitives": [ { "attributes": {
      "POSITION": 0, "COLOR_0": 1, "COLOR_1": 2, "_BATCHID": 3 }, "indices": 4 } ] } ],
  "buffers": [ { "byteLength": 152, "uri": "data:application/octet-stream;base64,AAAAAAAAAAAAAAAAAACAPwAAAAAAAAAAAAAAAAAAgD8AAAAAAACAPwAAAAAAAAAAAACAPwAAgD8AAAAAAAAAAAAAgD8AAIA/AAAAAAAAAAAAAIA/AAAAAAAAAAAAAIA/AACAPwAAAAAAAAAAAACAPwAAgD8AAAAAAAAAAAAAgD8AAIA/AADgQAAAAEEAABBBAAABAAIAAAA=" } ],
  "bufferViews": [
    { "buffer": 0, "byteOffset": 0,   "byteLength": 36 },
    { "buffer": 0, "byteOffset": 36,  "byteLength": 48 },
    { "buffer": 0, "byteOffset": 84,  "byteLength": 48 },
    { "buffer": 0, "byteOffset": 132, "byteLength": 12 },
    { "buffer": 0, "byteOffset": 144, "byteLength": 6 }
  ],
  "accessors": [
    { "bufferView": 0, "componentType": 5126, "count": 3, "type": "VEC3",
      "min": [0,0,0], "max": [1,1,0] },
    { "bufferView": 1, "componentType": 5126, "count": 3, "type": "VEC4" },
    { "bufferView": 2, "componentType": 5126, "count": 3, "type": "VEC4" },
    { "bufferView": 3, "componentType": 5126, "count": 3, "type": "SCALAR" },
    { "bufferView": 4, "componentType": 5123, "count": 3, "type": "SCALAR" }
  ]
})GLTF"));

    const MeshOut mesh =
        ExtractMesh(doc.data, doc.data->meshes[0].primitives[0], "probe", nullptr, 1.0f);

    EXPECT_TRUE(mesh.colored) << "COLOR_0 itself must still be imported";
    EXPECT_EQ(1, mesh.extraColorSetsEXT) << "COLOR_1 went unnoticed";
    ASSERT_EQ(1u, mesh.ignoredCustomAttributesEXT.size());
    EXPECT_EQ("_BATCHID", mesh.ignoredCustomAttributesEXT.front())
        << "a custom attribute must be named, not merely counted";

    // GLTF-090: and the colour that IS imported is quantised to a byte. The authored red is exact
    // at the endpoints -- which is the whole reason the quantisation is documented rather than
    // asserted only in the middle, where rounding rather than truncation is what distinguishes the
    // two rules (GLTF-154 owns that half).
    const CNA::Internal::Graphics::InferredVertexLayout layout =
        CNA::Internal::Graphics::InferredLayoutForStride(
            mesh.stride, CNA::Internal::Graphics::UnlistedStrideLayout::RendererRefusesIt);
    ASSERT_TRUE(layout.known);
    int colorOffset = -1;
    for (std::size_t e = 0; e < layout.count; ++e)
    {
        if (layout.elements[e].usage ==
            Microsoft::Xna::Framework::Graphics::VertexElementUsage::Color)
        {
            colorOffset = layout.elements[e].offset;
        }
    }
    ASSERT_GE(colorOffset, 0) << "this stride has no colour slot after all";
    const std::uint8_t* rgba =
        mesh.vertexBytes.data() + static_cast<std::size_t>(colorOffset);
    EXPECT_EQ(255, static_cast<int>(rgba[0])) << "COLOR_0's red endpoint did not survive";
    EXPECT_EQ(0, static_cast<int>(rgba[1]));
    EXPECT_EQ(0, static_cast<int>(rgba[2]))
        << "blue is COLOR_1's colour -- the second set was imported over the first";
    EXPECT_EQ(255, static_cast<int>(rgba[3]));
}
