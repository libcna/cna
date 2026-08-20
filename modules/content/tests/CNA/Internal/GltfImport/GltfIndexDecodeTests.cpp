// SPDX-License-Identifier: MS-PL
//
// plans/plan_gltf.md GLTF-063: the sparse-safe, bounds-checked index reader, and D4's regression witness.
//
// The defect these lock out (D4) was that CNA read primitive indices through
// cgltf_accessor_read_index, which returns 0 -- with no error channel -- for a sparse accessor and
// for one with no bufferView. A spec-legal sparse index accessor therefore decoded to all zeros
// and the primitive collapsed to a degenerate point, silently.
//
// Two kinds of test live here:
//
//   * the corpus witnesses, which assert the real fixture the forensic audit measured (`f3`
//     sparse-indices, `f11` u8-idx) now decodes exactly the spec-derived index list; and
//   * hand-authored accessors covering the shapes the corpus does not yet carry -- UNSIGNED_INT
//     indices, non-zero byte offsets on both the bufferView and the accessor, and each malformed
//     input the reader must turn into a named error rather than into wrong geometry.
//
// The attribute decode path is deliberately untouched by GLTF-063 and is locked separately by
// GltfAccessorDecodeLockTests.cpp (GLTF-041). Nothing here may be used to justify changing it.

#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>
#include <stdexcept>
#include <string>
#include <vector>

#include "CNA/Internal/GltfImport/GltfImportCore.hpp"

#include "GltfFixtureCorpus.hpp"
#include "GltfOracleEXT.hpp"

using namespace CNA::Internal::GltfImport;
using CnaTest::GltfOracle::ExtractedPrimitive;
using CnaTest::GltfOracle::ExtractSceneMeshesEXT;
using CnaTest::GltfOracle::LoadedFixture;
using CnaTest::GltfOracle::Member;
using CnaTest::GltfOracle::Numbers;
using CnaTest::GltfOracle::Path;
using CnaTest::GltfOracle::StringOr;

namespace
{
    class ScratchDir
    {
    public:
        ScratchDir()
            : dir_(std::filesystem::temp_directory_path() /
                   ("cna_gltf_index_test_" + std::to_string(reinterpret_cast<std::uintptr_t>(this))))
        {
            std::filesystem::create_directories(dir_);
        }
        ~ScratchDir()
        {
            std::error_code ec;
            std::filesystem::remove_all(dir_, ec);
        }
        ScratchDir(const ScratchDir&) = delete;
        ScratchDir& operator=(const ScratchDir&) = delete;
        [[nodiscard]] const std::filesystem::path& path() const { return dir_; }

    private:
        std::filesystem::path dir_;
    };

    void AppendFloat(std::vector<std::uint8_t>& out, float v)
    {
        std::uint8_t bytes[4];
        std::memcpy(bytes, &v, sizeof(bytes));
        out.insert(out.end(), bytes, bytes + sizeof(bytes));
    }

    void AppendU16(std::vector<std::uint8_t>& out, std::uint16_t v)
    {
        std::uint8_t bytes[2];
        std::memcpy(bytes, &v, sizeof(bytes));
        out.insert(out.end(), bytes, bytes + sizeof(bytes));
    }

    void AppendU32(std::vector<std::uint8_t>& out, std::uint32_t v)
    {
        std::uint8_t bytes[4];
        std::memcpy(bytes, &v, sizeof(bytes));
        out.insert(out.end(), bytes, bytes + sizeof(bytes));
    }

    std::string Base64(const std::vector<std::uint8_t>& bytes)
    {
        static const char* kAlphabet =
            "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
        std::string out;
        out.reserve(((bytes.size() + 2) / 3) * 4);
        for (std::size_t i = 0; i < bytes.size(); i += 3)
        {
            const std::uint32_t b0 = bytes[i];
            const std::uint32_t b1 = i + 1 < bytes.size() ? bytes[i + 1] : 0u;
            const std::uint32_t b2 = i + 2 < bytes.size() ? bytes[i + 2] : 0u;
            const std::uint32_t triple = (b0 << 16) | (b1 << 8) | b2;
            out += kAlphabet[(triple >> 18) & 0x3F];
            out += kAlphabet[(triple >> 12) & 0x3F];
            out += i + 1 < bytes.size() ? kAlphabet[(triple >> 6) & 0x3F] : '=';
            out += i + 2 < bytes.size() ? kAlphabet[triple & 0x3F] : '=';
        }
        return out;
    }

    /// The three positions every hand-authored fixture below shares, so a test's own subject is
    /// always the index accessor and never the geometry.
    std::vector<std::uint8_t> TrianglePositions()
    {
        std::vector<std::uint8_t> out;
        AppendFloat(out, 0.0f); AppendFloat(out, 0.0f); AppendFloat(out, 0.0f);
        AppendFloat(out, 1.0f); AppendFloat(out, 0.0f); AppendFloat(out, 0.0f);
        AppendFloat(out, 0.0f); AppendFloat(out, 1.0f); AppendFloat(out, 0.0f);
        return out;
    }

    /// Assembles a one-primitive glTF around a caller-supplied buffer, bufferView list, index
    /// accessor and primitive body. Accessor 0 is always the POSITION accessor over bufferView 0.
    std::string MakeGltf(const std::vector<std::uint8_t>& buffer, const std::string& extraViews,
                          const std::string& extraAccessors, const std::string& primitiveBody)
    {
        return std::string(R"({
  "asset": { "version": "2.0" },
  "scene": 0,
  "scenes": [ { "nodes": [0] } ],
  "nodes": [ { "name": "MeshNode", "mesh": 0 } ],
  "meshes": [ { "name": "Subject", "primitives": [ )") + primitiveBody + R"( ] } ],
  "buffers": [ { "byteLength": )" + std::to_string(buffer.size()) +
               R"(, "uri": "data:application/octet-stream;base64,)" + Base64(buffer) + R"(" } ],
  "bufferViews": [
    { "buffer": 0, "byteOffset": 0, "byteLength": 36})" + extraViews + R"(
  ],
  "accessors": [
    { "bufferView": 0, "componentType": 5126, "count": 3, "type": "VEC3",
      "min": [0,0,0], "max": [1,1,0] })" + extraAccessors + R"(
  ]
})";
    }

    /// Runs the production ExtractMesh over a hand-authored asset. Parse/load failures are
    /// reported as test failures rather than swallowed: a fixture this file cannot even parse is a
    /// bug in the fixture, not evidence about the reader.
    MeshOut ExtractPrimitive0(const std::string& gltfJson)
    {
        ScratchDir dir;
        const std::filesystem::path gltfPath = dir.path() / "fixture.gltf";
        {
            std::ofstream file(gltfPath, std::ios::binary);
            file << gltfJson;
        }

        cgltf_options options{};
        cgltf_data* data = nullptr;
        cgltf_result result = cgltf_parse_file(&options, gltfPath.string().c_str(), &data);
        EXPECT_EQ(cgltf_result_success, result) << "the hand-authored fixture does not parse";
        if (result != cgltf_result_success) { return MeshOut{}; }

        result = cgltf_load_buffers(&options, data, gltfPath.string().c_str());
        EXPECT_EQ(cgltf_result_success, result) << "the hand-authored fixture's buffer does not load";
        if (result != cgltf_result_success) { cgltf_free(data); return MeshOut{}; }

        MeshOut out;
        try
        {
            out = ExtractMesh(data, data->meshes[0].primitives[0], "Subject", nullptr, 1.0f);
        }
        catch (...)
        {
            cgltf_free(data);
            throw;
        }
        cgltf_free(data);
        return out;
    }

    /// The message ExtractMesh threw, or an empty string when it did not throw.
    std::string ExtractionError(const std::string& gltfJson)
    {
        try
        {
            (void)ExtractPrimitive0(gltfJson);
        }
        catch (const std::exception& e)
        {
            return e.what();
        }
        return {};
    }

    std::vector<std::uint32_t> DecodedIndices(const MeshOut& mesh)
    {
        std::vector<std::uint32_t> out;
        const std::size_t width = mesh.use32BitIndices ? 4u : 2u;
        for (std::size_t o = 0; o + width <= mesh.indexBytes.size(); o += width)
        {
            if (mesh.use32BitIndices)
            {
                std::uint32_t v = 0;
                std::memcpy(&v, mesh.indexBytes.data() + o, sizeof(v));
                out.push_back(v);
            }
            else
            {
                std::uint16_t v = 0;
                std::memcpy(&v, mesh.indexBytes.data() + o, sizeof(v));
                out.push_back(v);
            }
        }
        return out;
    }

    void ExpectIndices(const std::vector<std::uint32_t>& expected, const MeshOut& mesh)
    {
        const std::vector<std::uint32_t> actual = DecodedIndices(mesh);
        ASSERT_EQ(expected.size(), actual.size()) << "index count differs";
        for (std::size_t i = 0; i < expected.size(); ++i)
        {
            EXPECT_EQ(expected[i], actual[i]) << "index[" << i << "]";
        }
    }

    bool Contains(const std::string& haystack, const std::string& needle)
    {
        return haystack.find(needle) != std::string::npos;
    }
}

// --- The corpus witnesses -----------------------------------------------------------------------

TEST(GltfIndexDecode, SparseIndexAccessorResolvesItsOverrideExactly)
{
    // D4's own fixture, unchanged: base [0,1,2,0,2,0] with a sparse entry displacing element 5
    // with the value 3. Before GLTF-063 this decoded [0,0,0,0,0,0].
    const LoadedFixture fixture("sparse-indices");
    ASSERT_TRUE(fixture.Ok()) << fixture.Error();

    const std::vector<ExtractedPrimitive> extracted = ExtractSceneMeshesEXT(fixture.Data());
    ASSERT_EQ(1u, extracted.size());
    ASSERT_TRUE(extracted[0].extracted) << extracted[0].error;

    const std::vector<double> expected =
        Numbers(Member(Path(fixture.Expected(), "l3.primitives").arrayValue.at(0), "indices"));
    ASSERT_EQ(6u, expected.size());
    ASSERT_EQ(expected.size(), extracted[0].dump.indices.size());
    for (std::size_t i = 0; i < expected.size(); ++i)
    {
        EXPECT_EQ(static_cast<std::uint32_t>(expected[i]), extracted[0].dump.indices[i])
            << "indices[" << i << "] -- D4 has come back";
    }
}

TEST(GltfIndexDecode, TheCorpusRecordsD4AsFixedAndAgreesWithWhatCnaNowProduces)
{
    // The bookkeeping half of the flip: the defect record stays in the corpus as the regression
    // witness, and its currentActual must be the real, current output rather than a stale claim.
    const LoadedFixture fixture("sparse-indices");
    ASSERT_TRUE(fixture.Ok()) << fixture.Error();

    const CNA::Internal::JsonValue& defects = Member(fixture.Expected(), "defects");
    ASSERT_EQ(CNA::Internal::JsonType::Array, defects.type);
    ASSERT_EQ(1u, defects.arrayValue.size());
    const CNA::Internal::JsonValue& d4 = defects.arrayValue.at(0);
    EXPECT_EQ("D4", StringOr(d4, "id", ""));
    EXPECT_EQ("fixed", StringOr(d4, "status", ""));
    EXPECT_TRUE(Member(d4, "divergentFieldsByLayer").objectValue.empty())
        << "a fixed defect must suppress nothing";

    // The historical measurement is kept, not erased -- that is what makes the record traceable.
    const std::vector<double> prior = Numbers(Member(Member(d4, "priorActual"), "indices"));
    ASSERT_EQ(6u, prior.size());
    for (const double value : prior) { EXPECT_EQ(0.0, value) << "the recorded pre-fix output was all zeros"; }

    const std::vector<ExtractedPrimitive> extracted = ExtractSceneMeshesEXT(fixture.Data());
    ASSERT_EQ(1u, extracted.size());
    ASSERT_TRUE(extracted[0].extracted) << extracted[0].error;
    const std::vector<double> current = Numbers(Member(Member(d4, "currentActual"), "indices"));
    ASSERT_EQ(current.size(), extracted[0].dump.indices.size());
    for (std::size_t i = 0; i < current.size(); ++i)
    {
        EXPECT_EQ(static_cast<std::uint32_t>(current[i]), extracted[0].dump.indices[i])
            << "currentActual.indices[" << i << "] is not what CNA produces today";
    }
}

TEST(GltfIndexDecode, UnsignedByteIndicesDecodeExactly)
{
    // f11, the audit's own UNSIGNED_BYTE index fixture.
    const LoadedFixture fixture("u8-idx");
    ASSERT_TRUE(fixture.Ok()) << fixture.Error();

    const std::vector<ExtractedPrimitive> extracted = ExtractSceneMeshesEXT(fixture.Data());
    ASSERT_EQ(1u, extracted.size());
    ASSERT_TRUE(extracted[0].extracted) << extracted[0].error;
    EXPECT_EQ(std::vector<std::uint32_t>({0, 1, 2}), extracted[0].dump.indices);
}

// --- Component types --------------------------------------------------------------------------

TEST(GltfIndexDecode, UnsignedShortIndicesDecodeExactly)
{
    std::vector<std::uint8_t> buffer = TrianglePositions();
    AppendU16(buffer, 2); AppendU16(buffer, 0); AppendU16(buffer, 1);

    const MeshOut mesh = ExtractPrimitive0(MakeGltf(
        buffer,
        R"(,
    { "buffer": 0, "byteOffset": 36, "byteLength": 6 })",
        R"(,
    { "bufferView": 1, "componentType": 5123, "count": 3, "type": "SCALAR" })",
        R"({ "attributes": { "POSITION": 0 }, "indices": 1, "mode": 4 })"));

    ExpectIndices({2, 0, 1}, mesh);
}

TEST(GltfIndexDecode, UnsignedIntIndicesDecodeExactly)
{
    // UNSIGNED_INT is the one index component type the corpus does not yet carry, and the one a
    // 16-bit reader would silently mis-stride.
    std::vector<std::uint8_t> buffer = TrianglePositions();
    AppendU32(buffer, 1); AppendU32(buffer, 2); AppendU32(buffer, 0);

    const MeshOut mesh = ExtractPrimitive0(MakeGltf(
        buffer,
        R"(,
    { "buffer": 0, "byteOffset": 36, "byteLength": 12 })",
        R"(,
    { "bufferView": 1, "componentType": 5125, "count": 3, "type": "SCALAR" })",
        R"({ "attributes": { "POSITION": 0 }, "indices": 1, "mode": 4 })"));

    // Only three vertices, so the packed buffer stays 16-bit; the decode width and the storage
    // width are independent, and conflating them is exactly how a u32 accessor gets misread.
    EXPECT_FALSE(mesh.use32BitIndices);
    ExpectIndices({1, 2, 0}, mesh);
}

TEST(GltfIndexDecode, IndicesHonourBothTheBufferViewAndAccessorByteOffsets)
{
    // §8.1's effective-address equation with two independent non-zero terms: the view starts 8
    // bytes into the padded region and the accessor starts 4 bytes into the view. A reader that
    // dropped either term would return a different, plausible-looking triangle.
    std::vector<std::uint8_t> buffer = TrianglePositions();
    for (int i = 0; i < 8; ++i) { buffer.push_back(0xEE); }        // padding before the view
    AppendU16(buffer, 0xFFFF); AppendU16(buffer, 0xFFFF);           // skipped by accessor.byteOffset
    AppendU16(buffer, 1); AppendU16(buffer, 0); AppendU16(buffer, 2);

    const MeshOut mesh = ExtractPrimitive0(MakeGltf(
        buffer,
        R"(,
    { "buffer": 0, "byteOffset": 44, "byteLength": 10 })",
        R"(,
    { "bufferView": 1, "byteOffset": 4, "componentType": 5123, "count": 3, "type": "SCALAR" })",
        R"({ "attributes": { "POSITION": 0 }, "indices": 1, "mode": 4 })"));

    ExpectIndices({1, 0, 2}, mesh);
}

TEST(GltfIndexDecode, NonIndexedPrimitiveSynthesisesTheImplicitRange)
{
    const MeshOut mesh = ExtractPrimitive0(MakeGltf(
        TrianglePositions(), "", "",
        R"({ "attributes": { "POSITION": 0 }, "mode": 4 })"));

    ExpectIndices({0, 1, 2}, mesh);
}

// --- Sparse ------------------------------------------------------------------------------------

TEST(GltfIndexDecode, SparseIndexAccessorWithNoBaseBufferViewStartsFromZeros)
{
    // §3.6.2.3: with no base bufferView the base array is zero-initialised and only the listed
    // elements are displaced. This is the case cgltf_accessor_read_index could not express at all.
    std::vector<std::uint8_t> buffer = TrianglePositions();
    AppendU16(buffer, 1); AppendU16(buffer, 2);   // sparse indices: displace elements 1 and 2
    AppendU16(buffer, 1); AppendU16(buffer, 2);   // sparse values

    const MeshOut mesh = ExtractPrimitive0(MakeGltf(
        buffer,
        R"(,
    { "buffer": 0, "byteOffset": 36, "byteLength": 4 },
    { "buffer": 0, "byteOffset": 40, "byteLength": 4 })",
        R"(,
    { "componentType": 5123, "count": 3, "type": "SCALAR",
      "sparse": { "count": 2,
        "indices": { "bufferView": 1, "byteOffset": 0, "componentType": 5123 },
        "values": { "bufferView": 2, "byteOffset": 0 } } })",
        R"({ "attributes": { "POSITION": 0 }, "indices": 1, "mode": 4 })"));

    ExpectIndices({0, 1, 2}, mesh);
}

TEST(GltfIndexDecode, SparseIndicesMayUseADifferentComponentTypeFromTheAccessor)
{
    // The sparse index array carries its own componentType, independent of the accessor's.
    std::vector<std::uint8_t> buffer = TrianglePositions();
    AppendU32(buffer, 0); AppendU32(buffer, 1); AppendU32(buffer, 2);  // base [0,1,2] as u32
    buffer.push_back(2);                                                // sparse index (u8)
    // §3.6.2.4: the sparse values must start on a multiple of their own component size.
    buffer.push_back(0x00); buffer.push_back(0x00); buffer.push_back(0x00);
    AppendU32(buffer, 0);                                               // sparse value: element 2 -> 0

    const MeshOut mesh = ExtractPrimitive0(MakeGltf(
        buffer,
        R"(,
    { "buffer": 0, "byteOffset": 36, "byteLength": 12 },
    { "buffer": 0, "byteOffset": 48, "byteLength": 1 },
    { "buffer": 0, "byteOffset": 52, "byteLength": 4 })",
        R"(,
    { "bufferView": 1, "componentType": 5125, "count": 3, "type": "SCALAR",
      "sparse": { "count": 1,
        "indices": { "bufferView": 2, "byteOffset": 0, "componentType": 5121 },
        "values": { "bufferView": 3, "byteOffset": 0 } } })",
        R"({ "attributes": { "POSITION": 0 }, "indices": 1, "mode": 4 })"));

    ExpectIndices({0, 1, 0}, mesh);
}

// --- Malformed input becomes a named error, never wrong geometry -------------------------------

TEST(GltfIndexDecode, AnIndexBeyondTheVertexCountIsAnErrorNamingTheValue)
{
    std::vector<std::uint8_t> buffer = TrianglePositions();
    AppendU16(buffer, 0); AppendU16(buffer, 1); AppendU16(buffer, 7);

    const std::string error = ExtractionError(MakeGltf(
        buffer,
        R"(,
    { "buffer": 0, "byteOffset": 36, "byteLength": 6 })",
        R"(,
    { "bufferView": 1, "componentType": 5123, "count": 3, "type": "SCALAR" })",
        R"({ "attributes": { "POSITION": 0 }, "indices": 1, "mode": 4 })"));

    ASSERT_FALSE(error.empty()) << "an out-of-range index was accepted";
    EXPECT_TRUE(Contains(error, "7")) << error;          // the offending value
    EXPECT_TRUE(Contains(error, "3")) << error;          // the vertex count it exceeds
    EXPECT_TRUE(Contains(error, "Subject")) << error;    // which primitive
}

TEST(GltfIndexDecode, AnIndexAccessorWithNeitherBufferViewNorSparseDataIsAnError)
{
    const std::string error = ExtractionError(MakeGltf(
        TrianglePositions(), "",
        R"(,
    { "componentType": 5123, "count": 3, "type": "SCALAR" })",
        R"({ "attributes": { "POSITION": 0 }, "indices": 1, "mode": 4 })"));

    ASSERT_FALSE(error.empty()) << "an index accessor with no data at all was accepted";
    EXPECT_TRUE(Contains(error, "bufferView")) << error;
    EXPECT_TRUE(Contains(error, "sparse")) << error;
}

TEST(GltfIndexDecode, AFloatIndexAccessorIsAnErrorNamingTheComponentType)
{
    std::vector<std::uint8_t> buffer = TrianglePositions();
    AppendFloat(buffer, 0.0f); AppendFloat(buffer, 1.0f); AppendFloat(buffer, 2.0f);

    const std::string error = ExtractionError(MakeGltf(
        buffer,
        R"(,
    { "buffer": 0, "byteOffset": 36, "byteLength": 12 })",
        R"(,
    { "bufferView": 1, "componentType": 5126, "count": 3, "type": "SCALAR" })",
        R"({ "attributes": { "POSITION": 0 }, "indices": 1, "mode": 4 })"));

    ASSERT_FALSE(error.empty()) << "a FLOAT index accessor was accepted";
    EXPECT_TRUE(Contains(error, "FLOAT")) << error;
}

TEST(GltfIndexDecode, AnIndexAccessorReadingPastItsBufferViewIsAnError)
{
    // The accessor claims three UNSIGNED_INT indices but the view holds only two.
    std::vector<std::uint8_t> buffer = TrianglePositions();
    AppendU32(buffer, 0); AppendU32(buffer, 1);

    const std::string error = ExtractionError(MakeGltf(
        buffer,
        R"(,
    { "buffer": 0, "byteOffset": 36, "byteLength": 8 })",
        R"(,
    { "bufferView": 1, "componentType": 5125, "count": 3, "type": "SCALAR" })",
        R"({ "attributes": { "POSITION": 0 }, "indices": 1, "mode": 4 })"));

    ASSERT_FALSE(error.empty()) << "an index accessor was allowed to read past its bufferView";
    EXPECT_TRUE(Contains(error, "bufferView")) << error;
}

TEST(GltfIndexDecode, ASparseEntryBeyondTheAccessorCountIsAnError)
{
    std::vector<std::uint8_t> buffer = TrianglePositions();
    AppendU16(buffer, 0); AppendU16(buffer, 1); AppendU16(buffer, 2);  // base
    AppendU16(buffer, 9);                                              // sparse index: out of range
    AppendU16(buffer, 0);                                              // sparse value

    const std::string error = ExtractionError(MakeGltf(
        buffer,
        R"(,
    { "buffer": 0, "byteOffset": 36, "byteLength": 6 },
    { "buffer": 0, "byteOffset": 42, "byteLength": 2 },
    { "buffer": 0, "byteOffset": 44, "byteLength": 2 })",
        R"(,
    { "bufferView": 1, "componentType": 5123, "count": 3, "type": "SCALAR",
      "sparse": { "count": 1,
        "indices": { "bufferView": 2, "byteOffset": 0, "componentType": 5123 },
        "values": { "bufferView": 3, "byteOffset": 0 } } })",
        R"({ "attributes": { "POSITION": 0 }, "indices": 1, "mode": 4 })"));

    ASSERT_FALSE(error.empty()) << "a sparse entry displacing a non-existent element was accepted";
    EXPECT_TRUE(Contains(error, "9")) << error;
}

TEST(GltfIndexDecode, ANormalizedIndexAccessorIsAnError)
{
    std::vector<std::uint8_t> buffer = TrianglePositions();
    AppendU16(buffer, 0); AppendU16(buffer, 1); AppendU16(buffer, 2);

    const std::string error = ExtractionError(MakeGltf(
        buffer,
        R"(,
    { "buffer": 0, "byteOffset": 36, "byteLength": 6 })",
        R"(,
    { "bufferView": 1, "componentType": 5123, "count": 3, "type": "SCALAR",
      "normalized": true })",
        R"({ "attributes": { "POSITION": 0 }, "indices": 1, "mode": 4 })"));

    ASSERT_FALSE(error.empty()) << "a normalized index accessor was accepted";
    EXPECT_TRUE(Contains(error, "normalized")) << error;
}

TEST(GltfIndexDecode, AVectorTypedIndexAccessorIsAnError)
{
    std::vector<std::uint8_t> buffer = TrianglePositions();
    AppendU16(buffer, 0); AppendU16(buffer, 1); AppendU16(buffer, 2);
    AppendU16(buffer, 0); AppendU16(buffer, 1); AppendU16(buffer, 2);

    const std::string error = ExtractionError(MakeGltf(
        buffer,
        R"(,
    { "buffer": 0, "byteOffset": 36, "byteLength": 12 })",
        R"(,
    { "bufferView": 1, "componentType": 5123, "count": 3, "type": "VEC2" })",
        R"({ "attributes": { "POSITION": 0 }, "indices": 1, "mode": 4 })"));

    ASSERT_FALSE(error.empty()) << "a VEC2 index accessor was accepted";
    EXPECT_TRUE(Contains(error, "SCALAR")) << error;
}
