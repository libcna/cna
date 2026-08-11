// SPDX-License-Identifier: MS-PL
//
// plan_gltf.md GLTF-041: lock the attribute decode path that the forensic audit VERIFIED CORRECT.
//
// `UnpackAccessor()` -> `cgltf_accessor_unpack_floats` was proven right on the campaign baseline
// for `bufferView.byteOffset`, `accessor.byteOffset`, `byteStride`/interleaving, normalized
// integer conversion, and sparse *attribute* accessors including the null-base-bufferView case.
//
// This file exists so that later remediation cannot "fix" a layer that was already correct.
// **Do not rewrite this path.** Phase 2 adds fixtures and bounds checks *around* the decoder,
// never a replacement decoder. The genuinely broken index path (`cgltf_accessor_read_index`) is a
// different call site and belongs to GLTF-063.
//
// The lock has two halves, and the second is what gives it teeth:
//   * the L2 half asserts the decoded arrays against the generated manifests; and
//   * the L3 half asserts that the vertex data the PRODUCTION `ExtractMesh` packed -- which
//     reaches those accessors only through `UnpackAccessor` -- agrees with the L2 dump component
//     for component. A replacement decoder that behaved differently would break this even if it
//     happened to satisfy the L2 half on its own.

#include <algorithm>
#include <cmath>
#include <gtest/gtest.h>
#include <string>
#include <vector>

#include "GltfFixtureCorpus.hpp"
#include "GltfOracleEXT.hpp"

using namespace CnaTest::GltfOracle;
using CNA::Internal::JsonType;
using CNA::Internal::JsonValue;

namespace
{
    constexpr double kTolerance = 1e-5;

    /// The fixtures whose accessor behaviour plan_gltf.md §1.2 records as verified correct.
    const std::vector<std::string>& VerifiedFixtures()
    {
        static const std::vector<std::string> ids = {
            "interleaved-position-normal",  // f5: byteStride, both byte offsets
            "sparse-position",              // f6: sparse attribute, absent base bufferView
            "normalized-u8-color",          // f10: normalized UNSIGNED_BYTE
            "u8-idx",                       // f11: UNSIGNED_BYTE indices
            "skin-armature-ancestor",       // f9: MAT4 inverseBindMatrices
        };
        return ids;
    }

    const JsonValue& AccessorRecord(const LoadedFixture& fixture, const std::string& usage)
    {
        const JsonValue& accessors = Path(fixture.Expected(), "l2.accessors");
        for (const JsonValue& accessor : accessors.arrayValue)
        {
            if (StringOr(accessor, "usage", "") == usage) { return accessor; }
        }
        return JsonNull();
    }

    AccessorDump DumpByUsage(const LoadedFixture& fixture, const std::string& usage)
    {
        const JsonValue& record = AccessorRecord(fixture, usage);
        return DumpAccessorEXT(fixture.Data(),
                               static_cast<std::size_t>(NumberOr(record, "index", 0)));
    }

    void ExpectDecodedEquals(const LoadedFixture& fixture, const std::string& usage)
    {
        SCOPED_TRACE(fixture.Id() + " / " + usage);
        const JsonValue& record = AccessorRecord(fixture, usage);
        ASSERT_EQ(JsonType::Object, record.type) << "no manifest record for this accessor";
        const AccessorDump dump = DumpByUsage(fixture, usage);
        ASSERT_TRUE(dump.decoded) << dump.error;

        const std::vector<double> expected = Numbers(Member(record, "values"));
        ASSERT_EQ(expected.size(), dump.values.size()) << "decoded component count";
        for (std::size_t i = 0; i < expected.size(); ++i)
        {
            EXPECT_NEAR(expected[i], static_cast<double>(dump.values[i]), kTolerance)
                << "component[" << i << "] -- this path was verified correct on the campaign "
                   "baseline; a failure here means remediation changed a decoder it should not "
                   "have touched (plan_gltf.md GLTF-041)";
        }
    }
}

// --- The individually verified behaviours -------------------------------------------------------

TEST(GltfAccessorDecodeLock, InterleavedByteStrideAndBothByteOffsetsDecodeExactly)
{
    // plan_gltf.md §1.2 / GLTF-045 / GLTF-046 / GLTF-047: one bufferView at byteOffset 16 with
    // byteStride 24, POSITION at accessor byteOffset 0 and NORMAL at 12.
    const LoadedFixture fixture("interleaved-position-normal");
    ASSERT_TRUE(fixture.Ok()) << fixture.Error();
    ExpectDecodedEquals(fixture, "POSITION");
    ExpectDecodedEquals(fixture, "NORMAL");

    const AccessorDump position = DumpByUsage(fixture, "POSITION");
    const AccessorDump normal = DumpByUsage(fixture, "NORMAL");
    EXPECT_EQ(24, position.bufferViewByteStride);
    EXPECT_EQ(16, position.bufferViewByteOffset);
    EXPECT_EQ(12u, normal.byteOffset);
    EXPECT_EQ(position.bufferView, normal.bufferView);
}

TEST(GltfAccessorDecodeLock, SparseAttributeAccessorWithNoBaseBufferViewDecodesExactly)
{
    // plan_gltf.md §1.2 / GLTF-041: the base array initialises to zeros and only the listed
    // elements are displaced. This is also the evidence that D4 is an index-path defect: the very
    // same sparse machinery works correctly for an attribute.
    const LoadedFixture fixture("sparse-position");
    ASSERT_TRUE(fixture.Ok()) << fixture.Error();
    ExpectDecodedEquals(fixture, "POSITION");

    const AccessorDump position = DumpByUsage(fixture, "POSITION");
    EXPECT_TRUE(position.sparse);
    EXPECT_EQ(-1, position.bufferView) << "the fixture is supposed to have no base bufferView";
    ASSERT_EQ(9u, position.values.size());
    EXPECT_NEAR(0.0, static_cast<double>(position.values[0]), kTolerance)
        << "element 0 is not displaced, so it must come from the zero-initialised base array";
}

TEST(GltfAccessorDecodeLock, NormalizedUnsignedByteColorDecodesPerSpecAndRepacksByteExactly)
{
    // plan_gltf.md §1.2 / GLTF-052 / GLTF-056: c/255 per specification §3.6.2.2, and CNA's own
    // repack back to a byte must round-trip.
    const LoadedFixture fixture("normalized-u8-color");
    ASSERT_TRUE(fixture.Ok()) << fixture.Error();
    ExpectDecodedEquals(fixture, "COLOR_0");

    const AccessorDump color = DumpByUsage(fixture, "COLOR_0");
    ASSERT_EQ(12u, color.values.size());
    EXPECT_NEAR(128.0 / 255.0, static_cast<double>(color.values[7]), kTolerance)
        << "a mid alpha must use the c/255 divisor, not c/256";

    const std::vector<ExtractedPrimitive> extracted = ExtractSceneMeshesEXT(fixture.Data());
    ASSERT_EQ(1u, extracted.size());
    ASSERT_TRUE(extracted[0].extracted) << extracted[0].error;
    ASSERT_EQ(3u, extracted[0].dump.colors.size());
    const std::vector<std::array<std::uint8_t, 4>> expectedBytes = {
        {255, 0, 0, 255}, {0, 255, 0, 128}, {0, 0, 255, 0}};
    for (std::size_t v = 0; v < expectedBytes.size(); ++v)
    {
        for (std::size_t c = 0; c < 4; ++c)
        {
            EXPECT_EQ(static_cast<int>(expectedBytes[v][c]),
                      static_cast<int>(extracted[0].dump.colors[v][c]))
                << "vertex " << v << " channel " << c << " did not round-trip byte-exactly";
        }
    }
}

TEST(GltfAccessorDecodeLock, UnsignedByteIndicesDecodeExactly)
{
    // plan_gltf.md §1.2 / GLTF-052: the narrowest legal index component type.
    const LoadedFixture fixture("u8-idx");
    ASSERT_TRUE(fixture.Ok()) << fixture.Error();
    ExpectDecodedEquals(fixture, "indices");

    const std::vector<ExtractedPrimitive> extracted = ExtractSceneMeshesEXT(fixture.Data());
    ASSERT_EQ(1u, extracted.size());
    ASSERT_TRUE(extracted[0].extracted) << extracted[0].error;
    EXPECT_EQ(std::vector<std::uint32_t>({0, 1, 2}), extracted[0].dump.indices);
}

TEST(GltfAccessorDecodeLock, Mat4InverseBindMatrixAccessorDecodesExactly)
{
    // plan_gltf.md GLTF-059: f9 proved MAT4 read correctness. The skin *composition* built on top
    // of it is D8's problem; the accessor read itself is correct and stays locked here.
    const LoadedFixture fixture("skin-armature-ancestor");
    ASSERT_TRUE(fixture.Ok()) << fixture.Error();
    ExpectDecodedEquals(fixture, "inverseBindMatrices");

    const AccessorDump ibm = DumpByUsage(fixture, "inverseBindMatrices");
    EXPECT_EQ("MAT4", ibm.type);
    EXPECT_EQ(16u, ibm.componentsPerElement);
    ASSERT_EQ(16u, ibm.values.size());
    EXPECT_NEAR(-100.0, static_cast<double>(ibm.values[13]), kTolerance)
        << "the translation lives in the last column of a column-major matrix";
}

TEST(GltfAccessorDecodeLock, NonNormalizedJointIndicesStayIntegral)
{
    // plan_gltf.md GLTF-057: JOINTS_0 must NOT be normalized -- a joint index of 2 decodes to
    // 2.0, never to 2/255.
    const LoadedFixture fixture("skin-armature-ancestor");
    ASSERT_TRUE(fixture.Ok()) << fixture.Error();
    const AccessorDump joints = DumpByUsage(fixture, "JOINTS_0");
    ASSERT_TRUE(joints.decoded) << joints.error;
    EXPECT_FALSE(joints.normalized);
    for (const float value : joints.values)
    {
        EXPECT_NEAR(static_cast<double>(value), std::round(static_cast<double>(value)), kTolerance)
            << "a joint index decoded to a fractional value, which means it was normalized";
    }
}

// --- The lock proper: production output must agree with the L2 dump -------------------------------

TEST(GltfAccessorDecodeLock, ProductionExtractMeshAgreesWithTheL2DumpForEveryVerifiedFixture)
{
    // ExtractMesh reaches these accessors only through UnpackAccessor, so this compares the
    // production decoder against the L2 view of the same bytes. Any replacement decoder that
    // rounds differently, mis-strides, or resolves sparse data differently fails here -- which is
    // precisely what GLTF-041 is for.
    for (const std::string& id : VerifiedFixtures())
    {
        SCOPED_TRACE(id);
        const LoadedFixture fixture(id);
        ASSERT_TRUE(fixture.Ok()) << fixture.Error();

        const std::vector<ExtractedPrimitive> extracted = ExtractSceneMeshesEXT(fixture.Data());
        ASSERT_FALSE(extracted.empty());
        ASSERT_TRUE(extracted[0].extracted) << extracted[0].error;
        const MeshOutDump& dump = extracted[0].dump;

        const AccessorDump position = DumpByUsage(fixture, "POSITION");
        ASSERT_TRUE(position.decoded) << position.error;
        ASSERT_EQ(position.count, dump.vertexCount) << "vertex count";
        for (std::size_t v = 0; v < dump.vertexCount; ++v)
        {
            for (std::size_t c = 0; c < 3; ++c)
            {
                EXPECT_NEAR(static_cast<double>(position.values[v * 3 + c]),
                            static_cast<double>(dump.positions[v][c]), kTolerance)
                    << "position vertex " << v << " component " << c
                    << ": the production decoder disagrees with the L2 dump of the same accessor";
            }
        }

        const JsonValue& normalRecord = AccessorRecord(fixture, "NORMAL");
        if (normalRecord.type == JsonType::Object && !dump.normals.empty())
        {
            const AccessorDump normal = DumpByUsage(fixture, "NORMAL");
            ASSERT_TRUE(normal.decoded) << normal.error;
            ASSERT_EQ(normal.count, dump.normals.size());
            for (std::size_t v = 0; v < dump.normals.size(); ++v)
            {
                for (std::size_t c = 0; c < 3; ++c)
                {
                    EXPECT_NEAR(static_cast<double>(normal.values[v * 3 + c]),
                                static_cast<double>(dump.normals[v][c]), kTolerance)
                        << "normal vertex " << v << " component " << c;
                }
            }
        }

        const JsonValue& weightRecord = AccessorRecord(fixture, "WEIGHTS_0");
        if (weightRecord.type == JsonType::Object && !dump.weights.empty())
        {
            const AccessorDump weights = DumpByUsage(fixture, "WEIGHTS_0");
            ASSERT_TRUE(weights.decoded) << weights.error;
            ASSERT_EQ(weights.count, dump.weights.size());
            for (std::size_t v = 0; v < dump.weights.size(); ++v)
            {
                for (std::size_t c = 0; c < 4; ++c)
                {
                    EXPECT_NEAR(static_cast<double>(weights.values[v * 4 + c]),
                                static_cast<double>(dump.weights[v][c]), kTolerance)
                        << "weight vertex " << v << " component " << c;
                }
            }
        }
    }
}

TEST(GltfAccessorDecodeLock, EveryVerifiedFixtureStillCarriesNoKnownAccessorDefect)
{
    // A guard against the corpus drifting: if a fixture listed here ever acquires a defect record
    // whose first divergent layer is L2, the "verified correct" claim this file rests on is no
    // longer true and must be re-investigated rather than quietly worked around.
    for (const std::string& id : VerifiedFixtures())
    {
        SCOPED_TRACE(id);
        const LoadedFixture fixture(id);
        ASSERT_TRUE(fixture.Ok()) << fixture.Error();

        const JsonValue& defects = Member(fixture.Expected(), "defects");
        if (defects.type != JsonType::Array) { continue; }
        for (const JsonValue& defect : defects.arrayValue)
        {
            EXPECT_NE("L2", StringOr(defect, "firstDivergentLayer", ""))
                << "fixture " << id << " now records defect " << StringOr(defect, "id", "?")
                << " at L2, contradicting plan_gltf.md §1.2's verified-correct ledger. Stop and "
                   "investigate the contradiction -- do not weaken the expectation.";
        }
    }
}
