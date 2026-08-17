// SPDX-License-Identifier: MS-PL
//
// plan_gltf.md GLTF-005 / GLTF-006: the oracle helpers' own acceptance tests.
//
// These prove the harness before the harness is used to judge anything: that the dumps are
// deterministic and round-trip, that they expose enough structure to localise an addressing
// error rather than merely reporting different numbers, that the world-position oracle returns
// the manifest values for the control fixture, and -- critically for this batch -- that using the
// oracle does not change what the production import path produces.

#include <cmath>
#include <gtest/gtest.h>
#include <string>
#include <vector>

#include "CNA/Internal/Json.hpp"

#include "GltfFixtureCorpus.hpp"
#include "GltfOracleEXT.hpp"

using namespace CnaTest::GltfOracle;
using CNA::Internal::JsonType;
using CNA::Internal::JsonValue;

namespace
{
    constexpr double kTolerance = 1e-5;

    /// The accessor whose manifest `usage` matches, so a test names the attribute it means rather
    /// than an index that shifts whenever a fixture gains an accessor.
    std::size_t AccessorIndexByUsage(const LoadedFixture& fixture, const std::string& usage)
    {
        const JsonValue& accessors = Path(fixture.Expected(), "l2.accessors");
        for (const JsonValue& accessor : accessors.arrayValue)
        {
            if (StringOr(accessor, "usage", "") == usage)
            {
                return static_cast<std::size_t>(NumberOr(accessor, "index", 0));
            }
        }
        return static_cast<std::size_t>(-1);
    }
}

// --- L2 dumper ---------------------------------------------------------------------------------

TEST(GltfOracleEXT, DumpAccessorEXTSerialisationIsStableAndRoundTrips)
{
    const LoadedFixture fixture("interleaved-position-normal");
    ASSERT_TRUE(fixture.Ok()) << fixture.Error();

    const AccessorDump dump = DumpAccessorEXT(fixture.Data(), AccessorIndexByUsage(fixture, "NORMAL"));
    ASSERT_TRUE(dump.decoded) << dump.error;

    const std::string first = ToJson(dump);
    EXPECT_EQ(first, ToJson(DumpAccessorEXT(fixture.Data(),
                                            AccessorIndexByUsage(fixture, "NORMAL"))))
        << "two dumps of the same accessor must serialise identically";

    JsonValue parsed;
    ASSERT_NO_THROW(parsed = CNA::Internal::ParseJson(first));
    EXPECT_EQ(dump.type, StringOr(parsed, "type", ""));
    EXPECT_EQ(dump.componentType, static_cast<int>(NumberOr(parsed, "componentType", -1)));
    EXPECT_EQ(dump.count, static_cast<std::size_t>(NumberOr(parsed, "count", -1)));
    EXPECT_EQ(dump.normalized, BoolOr(parsed, "normalized", !dump.normalized));
    EXPECT_EQ(dump.sparse, BoolOr(parsed, "sparse", !dump.sparse));
    EXPECT_EQ(dump.byteOffset, static_cast<std::size_t>(NumberOr(parsed, "byteOffset", -1)));

    const std::vector<double> values = Numbers(Member(parsed, "values"));
    ASSERT_EQ(dump.values.size(), values.size());
    for (std::size_t i = 0; i < values.size(); ++i)
    {
        // %.9g round-trips binary32 exactly, so this is an equality claim, not an approximation.
        EXPECT_EQ(static_cast<double>(dump.values[i]), values[i]) << "values[" << i << "]";
    }
}

TEST(GltfOracleEXT, DumpAccessorEXTExposesTheStructureNeededToLocaliseAnAddressingError)
{
    // plan_gltf.md GLTF-005: the L2 dump must distinguish an offset error from a stride error from
    // a component-type error from a normalization error from a sparse-resolution error. Each of
    // those is a distinct field here, on fixtures that actually exercise it.
    const LoadedFixture interleaved("interleaved-position-normal");
    ASSERT_TRUE(interleaved.Ok()) << interleaved.Error();

    const AccessorDump position =
        DumpAccessorEXT(interleaved.Data(), AccessorIndexByUsage(interleaved, "POSITION"));
    const AccessorDump normal =
        DumpAccessorEXT(interleaved.Data(), AccessorIndexByUsage(interleaved, "NORMAL"));
    ASSERT_TRUE(position.decoded && normal.decoded);

    EXPECT_EQ(16, position.bufferViewByteOffset) << "bufferView.byteOffset must be visible";
    EXPECT_EQ(24, position.bufferViewByteStride) << "byteStride must be visible";
    EXPECT_EQ(0u, position.byteOffset);
    EXPECT_EQ(12u, normal.byteOffset) << "accessor.byteOffset must be visible";
    EXPECT_EQ(position.bufferView, normal.bufferView) << "both share the interleaved view";
    EXPECT_EQ("FLOAT", position.componentTypeName);
    EXPECT_FALSE(position.normalized);
    EXPECT_FALSE(position.sparse);

    const LoadedFixture colored("normalized-u8-color");
    ASSERT_TRUE(colored.Ok()) << colored.Error();
    const AccessorDump color = DumpAccessorEXT(colored.Data(), AccessorIndexByUsage(colored, "COLOR_0"));
    ASSERT_TRUE(color.decoded) << color.error;
    EXPECT_EQ("UNSIGNED_BYTE", color.componentTypeName);
    EXPECT_TRUE(color.normalized) << "normalization must be visible";

    const LoadedFixture sparse("sparse-position");
    ASSERT_TRUE(sparse.Ok()) << sparse.Error();
    const AccessorDump sparsePosition =
        DumpAccessorEXT(sparse.Data(), AccessorIndexByUsage(sparse, "POSITION"));
    ASSERT_TRUE(sparsePosition.decoded) << sparsePosition.error;
    EXPECT_TRUE(sparsePosition.sparse) << "sparse resolution must be visible";
    EXPECT_EQ(-1, sparsePosition.bufferView) << "an absent base bufferView must be visible";
}

TEST(GltfOracleEXT, DumpAccessorEXTReportsAnOutOfRangeIndexInsteadOfReadingPastTheArray)
{
    const LoadedFixture fixture("xf-identity");
    ASSERT_TRUE(fixture.Ok()) << fixture.Error();

    const AccessorDump dump = DumpAccessorEXT(fixture.Data(), fixture.Data().accessors_count + 5);
    EXPECT_FALSE(dump.decoded);
    EXPECT_FALSE(dump.error.empty()) << "a bounds failure must be diagnosable, not silent";
    EXPECT_TRUE(dump.values.empty());
}

// --- L3 dumper ---------------------------------------------------------------------------------

TEST(GltfOracleEXT, DumpMeshOutEXTSerialisationIsStableAndRoundTrips)
{
    const LoadedFixture fixture("skin-armature-ancestor");
    ASSERT_TRUE(fixture.Ok()) << fixture.Error();

    const std::vector<ExtractedPrimitive> extracted = ExtractSceneMeshesEXT(fixture.Data());
    ASSERT_EQ(1u, extracted.size());
    ASSERT_TRUE(extracted[0].extracted) << extracted[0].error;

    const std::string first = ToJson(extracted[0].dump);
    EXPECT_EQ(first, ToJson(ExtractSceneMeshesEXT(fixture.Data())[0].dump))
        << "two dumps of the same primitive must serialise identically";

    JsonValue parsed;
    ASSERT_NO_THROW(parsed = CNA::Internal::ParseJson(first));
    EXPECT_EQ(extracted[0].dump.stride, static_cast<int>(NumberOr(parsed, "stride", -1)));
    EXPECT_EQ(extracted[0].dump.vertexCount,
              static_cast<std::size_t>(NumberOr(parsed, "vertexCount", -1)));
    EXPECT_EQ(extracted[0].dump.skinned, BoolOr(parsed, "skinned", false));
    EXPECT_EQ(extracted[0].dump.positions.size() * 3, Numbers(Member(parsed, "positions")).size());
}

TEST(GltfOracleEXT, DumpMeshOutEXTExposesEverySemanticStreamOfTheSelectedLayout)
{
    // A skinned primitive selects stride 52, whose layout carries position, normal, UV, blend
    // weights and blend indices. The dump must recover each of those as a named stream, or an L3
    // failure could only be reported as a byte offset.
    const LoadedFixture fixture("skin-armature-ancestor");
    ASSERT_TRUE(fixture.Ok()) << fixture.Error();
    const std::vector<ExtractedPrimitive> extracted = ExtractSceneMeshesEXT(fixture.Data());
    ASSERT_EQ(1u, extracted.size());
    const MeshOutDump& dump = extracted[0].dump;

    // Stride 68, not 52: GLTF-215 made metallic-roughness the selection rule, and this fixture's
    // primitive has no material at all -- which in glTF means the default material, which IS
    // metallic-roughness. A skinned PBR primitive is the stride-68 layout.
    EXPECT_EQ(68, dump.stride);
    EXPECT_TRUE(dump.skinned);
    EXPECT_EQ(3u, dump.vertexCount);
    EXPECT_EQ(3u, dump.positions.size());
    EXPECT_EQ(3u, dump.normals.size()) << "stride 68 has a normal slot";
    EXPECT_EQ(3u, dump.texcoords.size()) << "stride 68 has a UV slot";
    EXPECT_EQ(3u, dump.weights.size());
    EXPECT_EQ(3u, dump.joints.size());
    EXPECT_EQ(3u, dump.tangents.size()) << "stride 68 has a tangent slot";
    EXPECT_TRUE(dump.colors.empty()) << "stride 68 has no colour slot";
    EXPECT_EQ(3u, dump.indices.size());

    // Every vertex is bound entirely to joint 0 in this fixture.
    for (std::size_t v = 0; v < dump.vertexCount; ++v)
    {
        EXPECT_NEAR(1.0, static_cast<double>(dump.weights[v][0]), kTolerance);
        EXPECT_EQ(0, static_cast<int>(dump.joints[v][0]));
    }
}

TEST(GltfOracleEXT, DumpMeshOutEXTExposesBothPackedUvStreams)
{
    const LoadedFixture fixture("uv1-material");
    ASSERT_TRUE(fixture.Ok()) << fixture.Error();
    const std::vector<ExtractedPrimitive> extracted = ExtractSceneMeshesEXT(fixture.Data());
    ASSERT_EQ(1u, extracted.size());
    ASSERT_TRUE(extracted[0].extracted) << extracted[0].error;
    const MeshOutDump& dump = extracted[0].dump;

    EXPECT_EQ(60, dump.stride);
    EXPECT_EQ(dump.vertexCount, dump.texcoords.size());
    EXPECT_EQ(dump.vertexCount, dump.texcoords1.size());
    ASSERT_FALSE(dump.texcoords.empty());
    ASSERT_FALSE(dump.texcoords1.empty());
    EXPECT_NE(dump.texcoords.front(), dump.texcoords1.front());
}

// --- L4 world-position oracle -----------------------------------------------------------------

TEST(GltfOracleEXT, EvaluateWorldPositionsEXTReturnsTheManifestValuesForXfIdentity)
{
    // plan_gltf.md GLTF-006's stated acceptance criterion.
    const LoadedFixture fixture("xf-identity");
    ASSERT_TRUE(fixture.Ok()) << fixture.Error();

    const WorldPositions world = EvaluateWorldPositionsEXT(fixture.Data());
    ASSERT_TRUE(world.selfCheckPassed);
    ASSERT_EQ(1u, world.instances.size());

    const JsonValue& instances = Path(fixture.Expected(), "l4.instances");
    ASSERT_EQ(JsonType::Array, instances.type);
    ASSERT_EQ(1u, instances.arrayValue.size());
    const JsonValue& expected = instances.arrayValue.at(0);

    EXPECT_EQ(StringOr(expected, "nodeName", ""), world.instances[0].nodeName);
    const std::vector<double> expectedPositions = Numbers(Member(expected, "worldPositions"));
    ASSERT_EQ(expectedPositions.size(), world.instances[0].worldPositions.size() * 3);
    for (std::size_t i = 0; i < expectedPositions.size(); ++i)
    {
        EXPECT_NEAR(expectedPositions[i],
                    static_cast<double>(world.instances[0].worldPositions[i / 3][i % 3]),
                    kTolerance) << "worldPositions component[" << i << "]";
    }
}

TEST(GltfOracleEXT, EvaluateWorldPositionsEXTAgreesWithCgltfOnEveryFixture)
{
    // The oracle composes node transforms itself rather than delegating, so that it is a genuine
    // second opinion. This test is what keeps that independence honest.
    //
    // A fixture the importer refuses has no world geometry to agree about, and the oracle is an
    // oracle for conforming files only: `bad-accessor-count-overflow` declares 2**62 elements, so
    // decoding its POSITION accessor at all is an allocation no reader should attempt. Such a
    // fixture is allowed to throw here -- its refusal is asserted in full by
    // GltfContainerValidation -- and everything else must both evaluate and self-check.
    std::size_t evaluated = 0;
    for (const std::string& id : CorpusFixtureIds())
    {
        SCOPED_TRACE(id);
        const LoadedFixture fixture(id);
        ASSERT_TRUE(fixture.Ok()) << fixture.Error();
        if (IsRejectionFixture(fixture.Expected()))
        {
            try { (void)EvaluateWorldPositionsEXT(fixture.Data()); } catch (const std::exception&) {}
            continue;
        }
        EXPECT_TRUE(EvaluateWorldPositionsEXT(fixture.Data()).selfCheckPassed);
        ++evaluated;
    }
    EXPECT_GT(evaluated, 50u) << "the sweep has shrunk -- it no longer covers the corpus";
}

TEST(GltfOracleEXT, EvaluateWorldPositionsEXTDoesNotAlterProductionBehaviour)
{
    // The hazard this test was written to catch: an oracle that "fixes" node transforms by feeding
    // its own composition back into the import path. The measurement is taken twice around a call
    // to the oracle, and production output must be identical both times.
    //
    // Its second half used to assert that CNA still diverged from the specification on this
    // fixture, because D1 was open. GLTF-113/GLTF-114 closed D1, so that assertion has been
    // replaced by its opposite: the two now agree, and the agreement is asserted here as well as
    // in GltfConformanceL4. What remains unchanged, and is the reason this test still exists, is
    // that the agreement comes from the production import path -- not from the oracle mutating it.
    const LoadedFixture fixture("xf-shared-mesh");
    ASSERT_TRUE(fixture.Ok()) << fixture.Error();

    const std::vector<ExtractedPrimitive> before = ExtractSceneMeshesEXT(fixture.Data());
    const WorldPositions expectedWorld = EvaluateWorldPositionsEXT(fixture.Data());
    const std::vector<ExtractedPrimitive> after = ExtractSceneMeshesEXT(fixture.Data());

    ASSERT_EQ(before.size(), after.size());
    for (std::size_t i = 0; i < before.size(); ++i)
    {
        EXPECT_EQ(ToJson(before[i].dump), ToJson(after[i].dump))
            << "evaluating the oracle changed what the production import path produces";
    }

    ASSERT_TRUE(expectedWorld.hasBounds);
    const WorldPositions cnaWorld = EvaluateCnaWorldPositionsEXT(fixture.Data());
    ASSERT_TRUE(cnaWorld.hasBounds);
    for (std::size_t c = 0; c < 3; ++c)
    {
        EXPECT_NEAR(expectedWorld.min[c], cnaWorld.min[c], 1e-4f) << "world bounds min component " << c;
        EXPECT_NEAR(expectedWorld.max[c], cnaWorld.max[c], 1e-4f) << "world bounds max component " << c;
    }
    // The specific number D1 was about: two instances of one mesh, 10 units apart.
    EXPECT_NEAR(11.0f, cnaWorld.max[0], 1e-4f)
        << "the two instances of xf-shared-mesh are no longer placed 10 units apart -- D1 is back";
}
