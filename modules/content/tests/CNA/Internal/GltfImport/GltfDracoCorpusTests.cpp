// SPDX-License-Identifier: MS-PL
//
// plans/plan_gltf.md GLTF-271/288/353/360/361/363: real pinned-Draco corpus parity.

#include <algorithm>
#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "CNA/Internal/GltfImport/GltfImportCore.hpp"
#include "GltfFixtureCorpus.hpp"
#include "GltfOracleEXT.hpp"

#ifdef CNA_DRACO_AVAILABLE
#include "draco/compression/config/compression_shared.h"
#include "draco/compression/encode.h"
#include "draco/mesh/triangle_soup_mesh_builder.h"
#endif

using CnaTest::GltfOracle::ExtractedPrimitive;
using CnaTest::GltfOracle::ExtractSceneMeshesEXT;
using CnaTest::GltfOracle::LoadedFixture;

namespace
{
    const ExtractedPrimitive* Find(const std::vector<ExtractedPrimitive>& extracted,
                                   int mesh, int primitive)
    {
        for (const ExtractedPrimitive& entry : extracted)
        {
            if (entry.mesh == mesh && entry.primitive == primitive) { return &entry; }
        }
        return nullptr;
    }

    void ExpectExactPair(const LoadedFixture& fixture, int first, int second)
    {
        const std::vector<ExtractedPrimitive> extracted = ExtractSceneMeshesEXT(fixture.Data());
        const ExtractedPrimitive* a = Find(extracted, 0, first);
        const ExtractedPrimitive* b = Find(extracted, 0, second);
        ASSERT_NE(nullptr, a);
        ASSERT_NE(nullptr, b);
        ASSERT_TRUE(a->extracted) << a->error;
        ASSERT_TRUE(b->extracted) << b->error;
        EXPECT_EQ(a->vertexBytes, b->vertexBytes)
            << "compressed and ordinary vertex streams differ";
        EXPECT_EQ(a->indexBytes, b->indexBytes)
            << "compressed face connectivity differs from ordinary indices";
        EXPECT_EQ(a->dump.stride, b->dump.stride);
        EXPECT_EQ(a->dump.positions, b->dump.positions);
        EXPECT_EQ(a->dump.normals, b->dump.normals);
        EXPECT_EQ(a->dump.tangents, b->dump.tangents);
        EXPECT_EQ(a->dump.texcoords, b->dump.texcoords);
        EXPECT_EQ(a->dump.texcoords1, b->dump.texcoords1);
        EXPECT_EQ(a->dump.colors, b->dump.colors);
        EXPECT_EQ(a->dump.joints, b->dump.joints);
        EXPECT_EQ(a->dump.weights, b->dump.weights);
        EXPECT_EQ(a->dump.indices, b->dump.indices);
    }

#ifdef CNA_DRACO_AVAILABLE
    enum class StreamKind { Basic, Pbr, Color, Skinned };

    template <typename T, std::size_t N>
    void Add(draco::TriangleSoupMeshBuilder& builder,
             draco::GeometryAttribute::Type type, draco::DataType dataType,
             std::uint32_t uniqueId, const std::array<std::array<T, N>, 3>& values,
             bool normalized = false)
    {
        const int id = builder.AddAttribute(
            type, static_cast<std::int8_t>(N), dataType, normalized);
        ASSERT_GE(id, 0);
        builder.SetAttributeUniqueId(id, uniqueId);
        builder.SetAttributeValuesForFace(
            id, draco::FaceIndex(0), values[0].data(), values[1].data(), values[2].data());
    }

    std::vector<std::uint8_t> Encode(StreamKind kind)
    {
        const std::array<std::array<float, 3>, 3> positions{{
            {{0.0f, 0.0f, 0.0f}}, {{1.0f, 0.0f, 0.0f}}, {{0.0f, 1.0f, 0.0f}}}};
        const std::array<std::array<float, 3>, 3> normals{{
            {{0.0f, 0.0f, 1.0f}}, {{0.0f, 0.0f, 1.0f}}, {{0.0f, 0.0f, 1.0f}}}};
        const std::array<std::array<float, 4>, 3> tangents{{
            {{1.0f, 0.0f, 0.0f, 1.0f}}, {{1.0f, 0.0f, 0.0f, 1.0f}},
            {{1.0f, 0.0f, 0.0f, 1.0f}}}};
        const std::array<std::array<float, 2>, 3> texcoords{{
            {{0.0f, 0.0f}}, {{1.0f, 0.0f}}, {{0.0f, 1.0f}}}};
        const std::array<std::array<std::uint8_t, 4>, 3> colors{{
            {{255, 0, 0, 255}}, {{0, 255, 0, 128}}, {{0, 0, 255, 64}}}};
        const std::array<std::array<std::uint16_t, 4>, 3> joints{{
            {{0, 1, 0, 0}}, {{1, 0, 0, 0}}, {{0, 1, 0, 0}}}};
        const std::array<std::array<float, 4>, 3> weights{{
            {{0.75f, 0.25f, 0.0f, 0.0f}}, {{1.0f, 0.0f, 0.0f, 0.0f}},
            {{0.5f, 0.5f, 0.0f, 0.0f}}}};

        draco::TriangleSoupMeshBuilder builder;
        builder.Start(1);
        Add(builder, draco::GeometryAttribute::POSITION, draco::DT_FLOAT32, 0, positions);
        if (kind == StreamKind::Color)
        {
            Add(builder, draco::GeometryAttribute::TEX_COORD, draco::DT_FLOAT32, 1, texcoords);
            Add(builder, draco::GeometryAttribute::COLOR, draco::DT_UINT8, 2, colors, true);
        }
        else
        {
            Add(builder, draco::GeometryAttribute::NORMAL, draco::DT_FLOAT32, 1, normals);
            if (kind == StreamKind::Basic)
            {
                Add(builder, draco::GeometryAttribute::TEX_COORD, draco::DT_FLOAT32, 2,
                    texcoords);
            }
            else
            {
                Add(builder, draco::GeometryAttribute::GENERIC, draco::DT_FLOAT32, 2, tangents);
                Add(builder, draco::GeometryAttribute::TEX_COORD, draco::DT_FLOAT32, 3,
                    texcoords);
                if (kind == StreamKind::Skinned)
                {
                    Add(builder, draco::GeometryAttribute::GENERIC, draco::DT_UINT16, 4, joints);
                    Add(builder, draco::GeometryAttribute::GENERIC, draco::DT_FLOAT32, 5, weights);
                }
            }
        }

        std::unique_ptr<draco::Mesh> mesh = builder.Finalize();
        EXPECT_NE(nullptr, mesh);
        if (mesh == nullptr) { return {}; }
        draco::Encoder encoder;
        encoder.SetEncodingMethod(draco::MESH_SEQUENTIAL_ENCODING);
        encoder.SetSpeedOptions(5, 5);
        draco::EncoderBuffer encoded;
        const draco::Status status = encoder.EncodeMeshToBuffer(*mesh, &encoded);
        EXPECT_TRUE(status.ok()) << status.error_msg_string();
        if (!status.ok()) { return {}; }
        const auto* begin = reinterpret_cast<const std::uint8_t*>(encoded.data());
        return {begin, begin + encoded.size()};
    }

    std::vector<std::uint8_t> CompressedBytes(const LoadedFixture& fixture, int primitive)
    {
        const cgltf_primitive& source = fixture.Data().meshes[0].primitives[primitive];
        EXPECT_TRUE(source.has_draco_mesh_compression);
        const cgltf_buffer_view* view = source.draco_mesh_compression.buffer_view;
        EXPECT_NE(nullptr, view);
        if (view == nullptr) { return {}; }
        const std::uint8_t* begin = cgltf_buffer_view_data(view);
        EXPECT_NE(nullptr, begin);
        if (begin == nullptr) { return {}; }
        return {begin, begin + view->size};
    }
#endif
}

#ifdef CNA_DRACO_AVAILABLE
TEST(GltfDracoParity, RigidStreamsAndConnectivityMatchTheirOrdinaryTwinsExactly)
{
    const LoadedFixture fixture("draco-vs-uncompressed-pair");
    ASSERT_TRUE(fixture.Ok()) << fixture.Error();
    ExpectExactPair(fixture, 0, 1); // POSITION/NORMAL/TANGENT/TEXCOORD_0
    ExpectExactPair(fixture, 2, 3); // POSITION/TEXCOORD_0/COLOR_0
}

TEST(GltfDracoParity, SkinJointsWeightsAndPbrStreamsMatchTheirOrdinaryTwinExactly)
{
    const LoadedFixture fixture("draco-skinned");
    ASSERT_TRUE(fixture.Ok()) << fixture.Error();
    ExpectExactPair(fixture, 0, 1);
}

TEST(GltfDracoParity, MorphBaseAndEveryDeltaStreamMatchTheirOrdinaryTwinExactly)
{
    using namespace CNA::Internal::GltfImport;
    const LoadedFixture fixture("draco-morph");
    ASSERT_TRUE(fixture.Ok()) << fixture.Error();
    ExpectExactPair(fixture, 0, 1);

    const cgltf_data& data = fixture.Data();
    const MeshOut compressed = ExtractMesh(
        &data, data.meshes[0].primitives[0], "compressed morph", nullptr, 1.0f);
    const MeshOut ordinary = ExtractMesh(
        &data, data.meshes[0].primitives[1], "ordinary morph", nullptr, 1.0f);
    ASSERT_EQ(compressed.morphPositionDeltas.size(), ordinary.morphPositionDeltas.size());
    ASSERT_EQ(compressed.morphNormalDeltas.size(), ordinary.morphNormalDeltas.size());
    ASSERT_EQ(compressed.morphTangentDeltas.size(), ordinary.morphTangentDeltas.size());
    ASSERT_EQ(1u, compressed.morphPositionDeltas.size());
    for (std::size_t vertex = 0; vertex < 3; ++vertex)
    {
        SCOPED_TRACE("vertex " + std::to_string(vertex));
        EXPECT_EQ(compressed.morphPositionDeltas[0][vertex],
                  ordinary.morphPositionDeltas[0][vertex]);
        EXPECT_EQ(compressed.morphNormalDeltas[0][vertex],
                  ordinary.morphNormalDeltas[0][vertex]);
        EXPECT_EQ(compressed.morphTangentDeltas[0][vertex],
                  ordinary.morphTangentDeltas[0][vertex]);
    }
}

TEST(GltfDracoParity, DecodedPointCountMustMatchTheDeclaredPositionAccessor)
{
    // GLTF-355: mutate only the metadata after a valid stream has parsed. The decoder still
    // returns its three real points; POSITION now claims two, and extraction must reject before
    // reserving/packing a plausible prefix of the mesh.
    using namespace CNA::Internal::GltfImport;
    const LoadedFixture fixture("draco-triangle");
    ASSERT_TRUE(fixture.Ok()) << fixture.Error();
    cgltf_data& data = const_cast<cgltf_data&>(fixture.Data());
    cgltf_primitive& primitive = data.meshes[0].primitives[0];
    ASSERT_GT(primitive.attributes_count, 0u);
    ASSERT_EQ(cgltf_attribute_type_position, primitive.attributes[0].type);
    for (cgltf_size i = 0; i < primitive.attributes_count; ++i)
    {
        cgltf_accessor* accessor = primitive.attributes[i].data;
        ASSERT_NE(nullptr, accessor);
        ASSERT_EQ(3u, accessor->count);
        accessor->count = 2;
    }

    try
    {
        (void)ExtractMesh(&data, primitive, "draco-count-mismatch", nullptr, 1.0f);
        FAIL() << "mismatched Draco point metadata produced a MeshOut";
    }
    catch (const std::runtime_error& e)
    {
        const std::string message = e.what();
        EXPECT_NE(std::string::npos, message.find("Draco-decoded point count")) << message;
        EXPECT_NE(std::string::npos, message.find("POSITION accessor count")) << message;
    }
}

TEST(GltfDracoParity, CorruptCompressedBytesFailCleanlyBeforeProducingAnyMesh)
{
    // GLTF-364: mutate a real, otherwise-valid corpus stream so the test reaches libdraco's error
    // handling rather than only CNA's missing-extension or container validation paths. This test
    // runs under the glTF ASan+UBSan lane as well as the ordinary build.
    using namespace CNA::Internal::GltfImport;
    const LoadedFixture fixture("draco-triangle");
    ASSERT_TRUE(fixture.Ok()) << fixture.Error();
    const cgltf_data& data = fixture.Data();
    const cgltf_primitive& primitive = data.meshes[0].primitives[0];
    const cgltf_buffer_view* view = primitive.draco_mesh_compression.buffer_view;
    ASSERT_NE(nullptr, view);
    std::uint8_t* bytes = const_cast<std::uint8_t*>(cgltf_buffer_view_data(view));
    ASSERT_NE(nullptr, bytes);
    ASSERT_GT(view->size, 16u);
    std::fill(bytes, bytes + 16, static_cast<std::uint8_t>(0xFF));

    std::string error;
    try
    {
        (void)ExtractMesh(&data, primitive, "corrupt-draco", nullptr, 1.0f);
        ADD_FAILURE() << "a corrupt Draco stream produced a mesh";
    }
    catch (const std::runtime_error& e)
    {
        error = e.what();
    }
    EXPECT_NE(std::string::npos, error.find("failed Draco decoding")) << error;
    EXPECT_NE(std::string::npos, error.find("corrupt-draco")) << error;
}

#ifdef CNA_VENDORED_DRACO
TEST(GltfDracoEncoderPin, CommittedStreamsAreByteExactOutputOfVendoredDraco157)
{
    const LoadedFixture triangle("draco-triangle");
    const LoadedFixture pair("draco-vs-uncompressed-pair");
    const LoadedFixture skinned("draco-skinned");
    const LoadedFixture morph("draco-morph");
    ASSERT_TRUE(triangle.Ok()) << triangle.Error();
    ASSERT_TRUE(pair.Ok()) << pair.Error();
    ASSERT_TRUE(skinned.Ok()) << skinned.Error();
    ASSERT_TRUE(morph.Ok()) << morph.Error();

    EXPECT_EQ(Encode(StreamKind::Basic), CompressedBytes(triangle, 0));
    EXPECT_EQ(Encode(StreamKind::Pbr), CompressedBytes(pair, 0));
    EXPECT_EQ(Encode(StreamKind::Color), CompressedBytes(pair, 2));
    EXPECT_EQ(Encode(StreamKind::Skinned), CompressedBytes(skinned, 0));
    EXPECT_EQ(Encode(StreamKind::Pbr), CompressedBytes(morph, 0));
    EXPECT_EQ(Encode(StreamKind::Pbr), Encode(StreamKind::Pbr))
        << "two encodes in one process must be deterministic too";
}
#endif
#endif
