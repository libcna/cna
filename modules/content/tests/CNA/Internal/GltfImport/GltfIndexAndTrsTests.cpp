// SPDX-License-Identifier: MS-PL
//
// plan_gltf.md GLTF-054 / GLTF-069 / GLTF-079 / GLTF-109 / GLTF-110.
//
// Two unrelated groups that share a property: each rule has a boundary, and the boundary is where
// the wrong implementation still produces a model. A 16/32-bit index decision made one vertex early
// truncates every index in a large mesh; a degenerate primitive read as though it were whole runs
// off the end of its own index array; and TRS applied in the wrong order gives a transform that is
// perfectly valid and in the wrong place.

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

    bool Parse(Parsed& out, const std::string& json)
    {
        cgltf_options options{};
        if (cgltf_parse(&options, json.data(), json.size(), &out.data) != cgltf_result_success)
        {
            return false;
        }
        return cgltf_load_buffers(&options, out.data, ".") == cgltf_result_success;
    }

    /// `vertexCount` positions along +X and `indices` of the given component type. Big meshes are
    /// generated rather than embedded, because the whole point of GLTF-069 is a vertex count either
    /// side of 65535 and that is not something to paste into a literal.
    std::string IndexedDocument(int vertexCount, int indexComponentType,
                                 const std::vector<std::uint32_t>& indices, int mode = 4)
    {
        std::vector<std::uint8_t> buffer;
        std::vector<float> positions;
        positions.reserve(static_cast<std::size_t>(vertexCount) * 3);
        for (int v = 0; v < vertexCount; ++v)
        {
            positions.push_back(static_cast<float>(v));
            positions.push_back(0.0f);
            positions.push_back(0.0f);
        }
        AppendFloats(buffer, positions);

        const std::size_t indexOffset = buffer.size();
        const std::size_t componentBytes =
            indexComponentType == 5125 ? 4u : (indexComponentType == 5123 ? 2u : 1u);
        for (const std::uint32_t index : indices)
        {
            for (std::size_t b = 0; b < componentBytes; ++b)
            {
                buffer.push_back(static_cast<std::uint8_t>((index >> (8 * b)) & 0xFF));
            }
        }
        while (buffer.size() % 4 != 0) { buffer.push_back(0); }

        const std::string indexAccessor = indices.empty()
            ? std::string()
            : R"(,
    { "bufferView": 1, "componentType": )" + std::to_string(indexComponentType) +
                  R"(, "count": )" + std::to_string(indices.size()) + R"(, "type": "SCALAR" })";
        const std::string indexView = indices.empty()
            ? std::string()
            : R"(,
    { "buffer": 0, "byteOffset": )" + std::to_string(indexOffset) + R"(, "byteLength": )" +
                  std::to_string(indices.size() * componentBytes) + " }";
        const std::string indicesField = indices.empty() ? std::string() : R"(, "indices": 1)";

        return std::string(R"GLTF({
  "asset": { "version": "2.0" },
  "scene": 0,
  "scenes": [ { "nodes": [0] } ],
  "nodes": [ { "name": "MeshNode", "mesh": 0 } ],
  "meshes": [ { "primitives": [ { "attributes": { "POSITION": 0 })GLTF") + indicesField +
               R"GLTF(, "mode": )GLTF" + std::to_string(mode) + R"GLTF( } ] } ],
  "buffers": [ { "byteLength": )GLTF" + std::to_string(buffer.size()) +
               R"GLTF(, "uri": "data:application/octet-stream;base64,)GLTF" + Base64(buffer) +
               R"GLTF(" } ],
  "bufferViews": [
    { "buffer": 0, "byteOffset": 0, "byteLength": )GLTF" +
               std::to_string(positions.size() * 4) + R"GLTF( })GLTF" + indexView + R"GLTF(
  ],
  "accessors": [
    { "bufferView": 0, "componentType": 5126, "count": )GLTF" + std::to_string(vertexCount) +
               R"GLTF(, "type": "VEC3", "min": [0, 0, 0], "max": [)GLTF" +
               std::to_string(vertexCount - 1) + R"GLTF(, 0, 0] })GLTF" + indexAccessor + R"GLTF(
  ]
})GLTF";
    }

    MeshOut ExtractFirst(const std::string& json, bool* parsed = nullptr)
    {
        Parsed doc;
        const bool ok = Parse(doc, json);
        if (parsed != nullptr) { *parsed = ok; }
        EXPECT_TRUE(ok) << "the fixture document did not parse";
        if (!ok || doc.data->meshes_count == 0) { return MeshOut{}; }
        return ExtractMesh(doc.data, doc.data->meshes[0].primitives[0], "probe", nullptr, 1.0f);
    }
}

// --- GLTF-054: UNSIGNED_INT indices --------------------------------------------------------------

TEST(GltfIndexForm, UnsignedIntIndicesDecodeExactlyIncludingValuesAboveSixteenBits)
{
    // §3.7.2.1 allows UNSIGNED_INT for indices and nothing else, and it is the only component type
    // that can address a mesh past 65535 vertices. A read that narrowed to 16 bits would give
    // index 70000 as 4464 -- a valid index into a smaller part of the same mesh, so the model still
    // draws, with a fold in it.
    const int vertexCount = 70001;
    const std::vector<std::uint32_t> indices = {0, 70000, 65536};
    const MeshOut mesh = ExtractFirst(IndexedDocument(vertexCount, 5125, indices));

    ASSERT_EQ(3u, mesh.indexBytes.size() / 4u) << "32-bit indices must stay 32-bit";
    std::vector<std::uint32_t> decoded(3);
    std::memcpy(decoded.data(), mesh.indexBytes.data(), mesh.indexBytes.size());
    EXPECT_EQ(0u, decoded[0]);
    EXPECT_EQ(70000u, decoded[1])
        << "narrowed to 16 bits this would be 4464 -- a valid index, so the mesh still draws";
    EXPECT_EQ(65536u, decoded[2]) << "the first value that does not fit in 16 bits at all";
}

// --- GLTF-069: which index width the buffer gets -------------------------------------------------

TEST(GltfIndexForm, TheIndexWidthFollowsTheVertexCountAtBothSidesOfTheBoundary)
{
    // `use32BitIndices = vertexCount > 65535`. The rule is about the VERTEX count, not the largest
    // index actually used -- an index buffer must be able to address every vertex the part owns,
    // whether or not some triangle happens to reference the last one.
    //
    // 65536 vertices is the first count that cannot be addressed by a 16-bit index, so the two
    // cases below sit exactly either side of it. Off by one and every index in a 65536-vertex mesh
    // wraps.
    {
        const MeshOut small = ExtractFirst(IndexedDocument(65535, 5123, {0, 1, 2}));
        EXPECT_FALSE(small.use32BitIndices)
            << "65535 vertices are addressable by a 16-bit index; widening wastes half the buffer";
        EXPECT_EQ(3u * 2u, small.indexBytes.size());
    }
    {
        const MeshOut large = ExtractFirst(IndexedDocument(65536, 5125, {0, 1, 2}));
        EXPECT_TRUE(large.use32BitIndices)
            << "65536 vertices cannot all be addressed by a 16-bit index";
        EXPECT_EQ(3u * 4u, large.indexBytes.size());
    }
}

// --- GLTF-079: degenerate and empty primitives ---------------------------------------------------

TEST(GltfIndexForm, AnIndexCountThatIsNotAWholeNumberOfTrianglesDropsTheTailAndCountsIt)
{
    // Four indices is one triangle and a leftover. Reading the leftover as a second triangle reads
    // two indices past the end of the run; keeping it and letting the draw call divide by three
    // leaves the same trailing index in the buffer for every later consumer to trip over. The tail
    // is dropped -- and counted, because a model quietly missing a face is the kind of bug that
    // gets blamed on the exporter for a week.
    Parsed doc;
    ASSERT_TRUE(Parse(doc, IndexedDocument(4, 5123, {0, 1, 2, 3})));
    const MeshOut mesh =
        ExtractMesh(doc.data, doc.data->meshes[0].primitives[0], "probe", nullptr, 1.0f);
    EXPECT_EQ(3u, mesh.indexBytes.size() / 2u) << "the incomplete triangle was kept";
    EXPECT_EQ(1u, mesh.droppedIncompleteIndicesEXT)
        << "the drop happened but was not reported, so nothing downstream can mention it";
}

TEST(GltfIndexForm, AWholeNumberOfTrianglesDropsNothing)
{
    // The other side of the same rule: a well-formed primitive must report a drop count of zero,
    // or the field means nothing. A trimmer that took a triple off every mesh would still satisfy
    // the assertion above.
    const MeshOut mesh = ExtractFirst(IndexedDocument(6, 5123, {0, 1, 2, 3, 4, 5}));
    EXPECT_EQ(6u, mesh.indexBytes.size() / 2u);
    EXPECT_EQ(0u, mesh.droppedIncompleteIndicesEXT);
}

TEST(GltfIndexForm, AnOddIndexCountOnALineListDropsTheDanglingIndex)
{
    // The rule is per-mode, not per-triangle: `LINES` is a whole number of PAIRS. An importer that
    // hardcoded 3 would leave a dangling index here and trim a perfectly valid line list to two
    // thirds of its segments elsewhere.
    const MeshOut mesh = ExtractFirst(IndexedDocument(5, 5123, {0, 1, 2, 3, 4}, /*mode=*/1));
    EXPECT_EQ(4u, mesh.indexBytes.size() / 2u);
    EXPECT_EQ(1u, mesh.droppedIncompleteIndicesEXT);
}

TEST(GltfIndexForm, AStripTooShortToFormOnePrimitiveDrawsNothingAndSaysSo)
{
    // A strip is one run rather than a sequence of primitives, so it has no remainder -- only the
    // case of being too short to describe a single triangle. Two indices is not a triangle strip;
    // the conversion already returned nothing for it, and the point here is that "nothing" is now
    // accompanied by a count instead of being indistinguishable from an empty mesh.
    const MeshOut mesh = ExtractFirst(IndexedDocument(3, 5123, {0, 1}, /*mode=*/5));
    EXPECT_TRUE(mesh.indexBytes.empty());
    EXPECT_EQ(2u, mesh.droppedIncompleteIndicesEXT);
}

TEST(GltfIndexForm, AnIndexlessPrimitiveIsMaterialisedAsASequentialIndexBuffer)
{
    // §3.7.2 makes `indices` optional -- the vertices are then drawn in order. CNA always
    // materialises an index buffer (GLTF-070) so the GPU layer stays uniform, and the generated one
    // has to be 0,1,2,... rather than empty: an empty index buffer draws nothing at all, which is a
    // model that vanishes rather than one that errors.
    const MeshOut mesh = ExtractFirst(IndexedDocument(3, 5123, {}));
    ASSERT_EQ(3u, mesh.indexBytes.size() / 2u) << "no index buffer was generated";
    std::vector<std::uint16_t> decoded(3);
    std::memcpy(decoded.data(), mesh.indexBytes.data(), mesh.indexBytes.size());
    EXPECT_EQ(0u, decoded[0]);
    EXPECT_EQ(1u, decoded[1]);
    EXPECT_EQ(2u, decoded[2]);
}

TEST(GltfIndexForm, AnIndexPastTheVertexCountIsRefusedRatherThanReadOutOfBounds)
{
    // An index is a promise about the vertex array, and a broken one is not a rendering artefact --
    // it is a read past the end of the decoded position stream during packing. `cgltf_validate`
    // does not check it, so extraction must.
    Parsed doc;
    ASSERT_TRUE(Parse(doc, IndexedDocument(3, 5123, {0, 1, 7})));
    std::string message;
    try
    {
        (void)ExtractMesh(doc.data, doc.data->meshes[0].primitives[0], "probe", nullptr, 1.0f);
    }
    catch (const std::exception& e)
    {
        message = e.what();
    }
    ASSERT_FALSE(message.empty()) << "an out-of-range index was accepted";
    EXPECT_NE(std::string::npos, message.find("7")) << message;
}

// --- GLTF-109 / GLTF-110: TRS order, and its equivalence with `matrix` -------------------------

TEST(GltfNodeTransformOrder, TrsComposesAsScaleThenRotateThenTranslate)
{
    // §3.5's order, and the reason it needs its own assertion: every other order produces a
    // perfectly valid transform in the wrong place. Scaling after rotating gives a sheared basis;
    // translating before scaling multiplies the offset by the scale.
    //
    // The parameters are chosen so all six orderings differ. Scale (2,3,4), a quarter turn about X
    // (which maps +Y to +Z), then translate (10,20,30). A point at (0,1,0) scales to (0,3,0),
    // rotates to (0,0,3), and translates to (10,20,33).
    const float s = std::sin(3.14159265358979323846f / 4.0f);
    const float c = std::cos(3.14159265358979323846f / 4.0f);
    Parsed doc;
    ASSERT_TRUE(Parse(doc, std::string(R"GLTF({
  "asset": { "version": "2.0" },
  "scene": 0,
  "scenes": [ { "nodes": [0] } ],
  "nodes": [ { "name": "TrsNode",
               "translation": [10, 20, 30],
               "rotation": [)GLTF") + std::to_string(s) + ", 0, 0, " + std::to_string(c) +
        R"GLTF(],
               "scale": [2, 3, 4] } ]
})GLTF"));

    const SceneGraphOut scene = BuildSceneGraph(doc.data);
    ASSERT_GE(scene.nodes.size(), 2u);
    const Vector3 moved = Vector3::Transform(Vector3(0.0f, 1.0f, 0.0f),
                                              scene.nodes[1].localTransform);
    EXPECT_NEAR(10.0f, moved.X, kTolerance);
    EXPECT_NEAR(20.0f, moved.Y, kTolerance)
        << "rotating before scaling would put the scale on the wrong axis here";
    EXPECT_NEAR(33.0f, moved.Z, kTolerance)
        << "expected 30 + 3: scale by 3 on Y, then the quarter turn moves it to Z, then translate";
}

TEST(GltfNodeTransformOrder, ANodeDeclaringBothMatrixAndTrsIsResolvedByTheSpecsOwnExclusivityRule)
{
    // §3.5 makes `matrix` and TRS mutually exclusive, and a file declaring both is malformed. The
    // resolution has to be deterministic rather than dependent on JSON key order -- otherwise the
    // same file imports differently depending on how it was serialised, which is the worst kind of
    // bug to chase.
    //
    // cgltf applies the rule for us (`cgltf_node_transform_local` honours `has_matrix`), and this
    // asserts the property CNA relies on: the result is the MATRIX, and it is the same whichever
    // order the two appear in the document.
    const char* matrixFirst = R"GLTF({
  "asset": { "version": "2.0" },
  "scene": 0,
  "scenes": [ { "nodes": [0] } ],
  "nodes": [ { "name": "Both",
               "matrix": [1,0,0,0, 0,1,0,0, 0,0,1,0, 7,8,9,1],
               "translation": [100, 200, 300] } ]
})GLTF";
    const char* trsFirst = R"GLTF({
  "asset": { "version": "2.0" },
  "scene": 0,
  "scenes": [ { "nodes": [0] } ],
  "nodes": [ { "name": "Both",
               "translation": [100, 200, 300],
               "matrix": [1,0,0,0, 0,1,0,0, 0,0,1,0, 7,8,9,1] } ]
})GLTF";

    for (const char* json : {matrixFirst, trsFirst})
    {
        SCOPED_TRACE(json == matrixFirst ? "matrix first" : "TRS first");
        Parsed doc;
        ASSERT_TRUE(Parse(doc, json));
        const SceneGraphOut scene = BuildSceneGraph(doc.data);
        ASSERT_GE(scene.nodes.size(), 2u);
        const Vector3 origin =
            Vector3::Transform(Vector3(0.0f, 0.0f, 0.0f), scene.nodes[1].localTransform);
        EXPECT_NEAR(7.0f, origin.X, kTolerance)
            << "the TRS won, or the two authorings disagree by document order";
        EXPECT_NEAR(8.0f, origin.Y, kTolerance);
        EXPECT_NEAR(9.0f, origin.Z, kTolerance);
    }
}
