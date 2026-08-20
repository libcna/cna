// SPDX-License-Identifier: MS-PL
//
// plans/plan_gltf.md GLTF-007: the L5 golden vertex/index buffer comparator, and the corpus asserted
// through it.
//
// Two halves, in the order they have to be trusted:
//
//   1. the comparator proving itself. A byte-identical pair must pass, and a deliberate one-byte
//      perturbation must fail *at the right offset, naming the right vertex and the right semantic
//      field*. An oracle that reports "something differs" is worth very little; the whole reason
//      L5 exists is that the byte it names tells you which part of the packing layer is wrong.
//
//   2. the corpus asserted at L5: for every fixture CNA imports, the bytes ExtractMesh produced
//      must equal the committed golden exactly.
//
// The goldens are generated (tools/gltf_fixtures/l5.py) from the fixture's own spec-derived L3
// values laid out per CNA's vertex stride ABI. That ABI is a CNA decision rather than a
// specification one, so the C++ side carries its own independent copy of the layout table and a
// test below asserts the two agree -- a table stated only once cannot catch its own drift.

#include <algorithm>
#include <cstdint>
#include <gtest/gtest.h>
#include <string>
#include <vector>

#include "CNA/Internal/GltfImport/GltfImportCore.hpp"

#include "GltfBufferOracleEXT.hpp"
#include "GltfFixtureCorpus.hpp"
#include "GltfOracleEXT.hpp"

using namespace CnaTest::GltfOracle;
using CNA::Internal::JsonType;
using CNA::Internal::JsonValue;

namespace
{
    /// A fixture with a Position+Normal layout, used by the self-proving half below. Any
    /// importable fixture would do; naming one keeps the offsets in those tests concrete.
    ///
    /// Its stride is 48, not 32: GLTF-215 made metallic-roughness the selection rule, and a
    /// primitive with no material at all still gets glTF's default material, which is
    /// metallic-roughness. Stride 32 no longer occurs anywhere in the corpus.
    constexpr const char* kPositionNormalFixture = "u8-idx";
    /// Position(12) + Normal(12) + Tangent(16) + TextureCoordinate(8).
    constexpr int kPositionNormalStride = 48;

    const GoldenBufferPart* FirstPart(const GoldenBuffers& golden)
    {
        return golden.parts.empty() ? nullptr : &golden.parts.front();
    }

    const ExtractedPrimitive* FindExtracted(const std::vector<ExtractedPrimitive>& extracted,
                                            int mesh, int primitive)
    {
        for (const ExtractedPrimitive& entry : extracted)
        {
            if (entry.mesh == mesh && entry.primitive == primitive) { return &entry; }
        }
        return nullptr;
    }

    /// Runs the production import path and returns the MeshOut for a (mesh, primitive) pair, which
    /// is where the bytes L5 compares actually come from.
    bool ExtractMeshOut(const cgltf_data& data, int mesh, int primitive,
                        CNA::Internal::GltfImport::MeshOut& out, std::string& error)
    {
        using namespace CNA::Internal::GltfImport;
        for (const MeshGroup& group : CollectMeshGroups(&data))
        {
            SkeletonResult skeleton;
            const bool hasSkin = group.skin != nullptr;
            if (hasSkin) { skeleton = BuildSkeleton(group.skin, 1.0f); }
            for (const MeshInstanceOut& placement : group.instances)
            {
                const cgltf_mesh* candidate = placement.mesh;
                if (candidate == nullptr) { continue; }
                if (static_cast<int>(candidate - data.meshes) != mesh) { continue; }
                if (static_cast<std::size_t>(primitive) >= candidate->primitives_count) { continue; }
                try
                {
                    out = ExtractMesh(&data, candidate->primitives[primitive],
                                      candidate->name != nullptr ? candidate->name : "primitive",
                                      hasSkin ? &skeleton : nullptr, 1.0f);
                    return true;
                }
                catch (const std::exception& e)
                {
                    error = e.what();
                    return false;
                }
            }
        }
        error = "the import path produced no such primitive";
        return false;
    }
}

// --- The comparator proving itself ---------------------------------------------------------------

TEST(GltfBufferOracle, IdenticalBuffersCompareEqual)
{
    const LoadedFixture fixture(kPositionNormalFixture);
    ASSERT_TRUE(fixture.Ok()) << fixture.Error();
    const GoldenBuffers golden = LoadGoldenBuffersEXT(fixture);
    ASSERT_TRUE(golden.ok) << golden.error;
    ASSERT_TRUE(golden.supported);
    const GoldenBufferPart* part = FirstPart(golden);
    ASSERT_NE(nullptr, part);

    const BufferDifference vb = CompareVertexBytesEXT(fixture.Id(), part->stride,
                                                      part->vertexBytes, part->vertexBytes);
    EXPECT_TRUE(vb.equal) << vb.report;
    EXPECT_TRUE(vb.report.empty());

    const BufferDifference ib = CompareIndexBytesEXT(fixture.Id(), part->indexElementSize,
                                                     part->indexBytes, part->indexBytes);
    EXPECT_TRUE(ib.equal) << ib.report;
}

TEST(GltfBufferOracle, AOneByteVertexPerturbationIsReportedAtTheRightOffsetFieldAndVertex)
{
    // The acceptance criterion of GLTF-007, stated directly. Vertex 1's Normal begins at byte
    // 48 + 12 = 60 in this layout, so perturbing byte 61 must be reported as vertex 1, Normal,
    // one byte in.
    const LoadedFixture fixture(kPositionNormalFixture);
    ASSERT_TRUE(fixture.Ok()) << fixture.Error();
    const GoldenBuffers golden = LoadGoldenBuffersEXT(fixture);
    ASSERT_TRUE(golden.ok) << golden.error;
    const GoldenBufferPart* part = FirstPart(golden);
    ASSERT_NE(nullptr, part);
    ASSERT_EQ(kPositionNormalStride, part->stride);
    ASSERT_GE(part->vertexBytes.size(), 62u);

    constexpr std::size_t kPerturbed = 61;
    std::vector<std::uint8_t> mutated = part->vertexBytes;
    mutated[kPerturbed] = static_cast<std::uint8_t>(mutated[kPerturbed] ^ 0xFF);

    const BufferDifference diff =
        CompareVertexBytesEXT(fixture.Id(), part->stride, part->vertexBytes, mutated);

    ASSERT_FALSE(diff.equal) << "a perturbed byte compared equal";
    EXPECT_FALSE(diff.sizeDiffers);
    EXPECT_EQ(kPerturbed, diff.offset);
    EXPECT_EQ(1, diff.element);
    EXPECT_EQ("Normal", diff.field);
    EXPECT_EQ(1u, diff.fieldByteOffset);
    EXPECT_EQ(static_cast<int>(part->vertexBytes[kPerturbed]), diff.expectedByte);
    EXPECT_EQ(static_cast<int>(mutated[kPerturbed]), diff.actualByte);
    EXPECT_EQ(BufferKind::Vertex, diff.kind);

    // The report has to carry all of it, since that is what a failing test actually prints.
    EXPECT_NE(std::string::npos, diff.report.find(fixture.Id()));
    EXPECT_NE(std::string::npos, diff.report.find("VB"));
    EXPECT_NE(std::string::npos, diff.report.find("vertex 1"));
    EXPECT_NE(std::string::npos, diff.report.find("Normal"));
    EXPECT_NE(std::string::npos, diff.report.find(std::to_string(kPerturbed)));
}

TEST(GltfBufferOracle, EveryByteOfAVertexIsAttributedToTheFieldThatOwnsIt)
{
    // Sweeping every offset of one vertex proves the attribution is a real mapping rather than a
    // lucky case: each byte must name the field whose span contains it, and no other.
    const LoadedFixture fixture(kPositionNormalFixture);
    ASSERT_TRUE(fixture.Ok()) << fixture.Error();
    const GoldenBuffers golden = LoadGoldenBuffersEXT(fixture);
    ASSERT_TRUE(golden.ok) << golden.error;
    const GoldenBufferPart* part = FirstPart(golden);
    ASSERT_NE(nullptr, part);

    const VertexLayout layout = VertexLayoutForStrideEXT(part->stride);
    ASSERT_TRUE(layout.known);

    for (const VertexField& field : layout.fields)
    {
        for (std::size_t within = 0; within < field.size; ++within)
        {
            const std::size_t offset = field.offset + within;
            SCOPED_TRACE("byte " + std::to_string(offset));
            std::vector<std::uint8_t> mutated = part->vertexBytes;
            ASSERT_GT(mutated.size(), offset);
            mutated[offset] = static_cast<std::uint8_t>(mutated[offset] ^ 0xFF);

            const BufferDifference diff =
                CompareVertexBytesEXT(fixture.Id(), part->stride, part->vertexBytes, mutated);
            ASSERT_FALSE(diff.equal);
            EXPECT_EQ(offset, diff.offset);
            EXPECT_EQ(0, diff.element);
            EXPECT_EQ(field.name, diff.field);
            EXPECT_EQ(within, diff.fieldByteOffset);
        }
    }
}

TEST(GltfBufferOracle, AOneByteIndexPerturbationNamesTheIndexItBelongsTo)
{
    const LoadedFixture fixture(kPositionNormalFixture);
    ASSERT_TRUE(fixture.Ok()) << fixture.Error();
    const GoldenBuffers golden = LoadGoldenBuffersEXT(fixture);
    ASSERT_TRUE(golden.ok) << golden.error;
    const GoldenBufferPart* part = FirstPart(golden);
    ASSERT_NE(nullptr, part);
    ASSERT_EQ(2u, part->indexElementSize);
    ASSERT_GE(part->indexBytes.size(), 6u);

    // Byte 4 is the low half of index 2 in a 16-bit index buffer.
    std::vector<std::uint8_t> mutated = part->indexBytes;
    mutated[4] = static_cast<std::uint8_t>(mutated[4] ^ 0xFF);

    const BufferDifference diff =
        CompareIndexBytesEXT(fixture.Id(), part->indexElementSize, part->indexBytes, mutated);
    ASSERT_FALSE(diff.equal);
    EXPECT_EQ(4u, diff.offset);
    EXPECT_EQ(2, diff.element);
    EXPECT_EQ("uint16 index", diff.field);
    EXPECT_EQ(0u, diff.fieldByteOffset);
    EXPECT_NE(std::string::npos, diff.report.find("IB"));
    EXPECT_NE(std::string::npos, diff.report.find("index 2"));
}

TEST(GltfBufferOracle, ATruncatedBufferIsReportedAsASizeDifferenceAtTheFirstMissingByte)
{
    const LoadedFixture fixture(kPositionNormalFixture);
    ASSERT_TRUE(fixture.Ok()) << fixture.Error();
    const GoldenBuffers golden = LoadGoldenBuffersEXT(fixture);
    ASSERT_TRUE(golden.ok) << golden.error;
    const GoldenBufferPart* part = FirstPart(golden);
    ASSERT_NE(nullptr, part);

    // One whole vertex short: the common prefix matches, so only the length can reveal it.
    std::vector<std::uint8_t> truncated = part->vertexBytes;
    truncated.resize(truncated.size() - static_cast<std::size_t>(part->stride));

    const BufferDifference diff =
        CompareVertexBytesEXT(fixture.Id(), part->stride, part->vertexBytes, truncated);
    ASSERT_FALSE(diff.equal);
    EXPECT_TRUE(diff.sizeDiffers);
    EXPECT_EQ(truncated.size(), diff.offset);
    EXPECT_EQ(-1, diff.actualByte);
    EXPECT_NE(-1, diff.expectedByte);
    EXPECT_NE(std::string::npos, diff.report.find("size"));
}

TEST(GltfBufferOracle, TheCppLayoutTableAgreesWithTheOneTheGeneratorPackedWith)
{
    // Two independent statements of the same ABI. If they ever disagree, one of them is what some
    // renderer's ApplyLayout is actually written against, and this is the cheapest place to find
    // that out.
    for (const std::string& id : CorpusFixtureIds())
    {
        SCOPED_TRACE(id);
        const LoadedFixture fixture(id);
        ASSERT_TRUE(fixture.Ok()) << fixture.Error();
        const GoldenBuffers golden = LoadGoldenBuffersEXT(fixture);
        ASSERT_TRUE(golden.ok) << golden.error;
        if (!golden.supported) { continue; }

        for (const GoldenBufferPart& part : golden.parts)
        {
            const VertexLayout layout = VertexLayoutForStrideEXT(part.stride);
            ASSERT_TRUE(layout.known) << "stride " << part.stride << " is not in the C++ ABI table";
            ASSERT_EQ(part.declaredFields.size(), layout.fields.size())
                << "field count differs for stride " << part.stride;
            for (std::size_t i = 0; i < layout.fields.size(); ++i)
            {
                EXPECT_EQ(part.declaredFields[i].name, layout.fields[i].name);
                EXPECT_EQ(part.declaredFields[i].offset, layout.fields[i].offset);
                EXPECT_EQ(part.declaredFields[i].size, layout.fields[i].size);
            }
            // The layout must also tile the stride exactly, or a byte would belong to no field.
            std::size_t covered = 0;
            for (const VertexField& field : layout.fields) { covered += field.size; }
            EXPECT_EQ(static_cast<std::size_t>(part.stride), covered)
                << "the stride-" << part.stride << " layout does not tile its own stride";
        }
    }
}

TEST(GltfBufferOracle, AnUnknownStrideIsReportedRatherThanGuessedAt)
{
    const VertexLayout layout = VertexLayoutForStrideEXT(37);
    EXPECT_FALSE(layout.known);
    EXPECT_TRUE(layout.fields.empty());

    const std::vector<std::uint8_t> expected(37, 0x00);
    std::vector<std::uint8_t> actual = expected;
    actual[5] = 0x01;
    const BufferDifference diff = CompareVertexBytesEXT("synthetic", 37, expected, actual);
    ASSERT_FALSE(diff.equal);
    EXPECT_EQ(5u, diff.offset);
    EXPECT_EQ("<unknown stride layout>", diff.field);
}

// --- The corpus at L5 -----------------------------------------------------------------------------

TEST(GltfConformanceL5, GeneratedBuffersMatchTheGoldenBytesExactly)
{
    for (const std::string& id : CorpusFixtureIds())
    {
        SCOPED_TRACE(id);
        const LoadedFixture fixture(id);
        ASSERT_TRUE(fixture.Ok()) << fixture.Error();

        // The same committed goldens are exercised by the decoder-enabled build. In the explicit
        // decoder-free configuration, required-extension refusal is the correct outcome and is
        // asserted by GltfContainerValidation rather than misreported here as an L5 byte defect.
        if (RequiresUnavailableDraco(fixture.Expected())) { continue; }

        const GoldenBuffers golden = LoadGoldenBuffersEXT(fixture);
        ASSERT_TRUE(golden.declared) << "the fixture's manifest carries no l5 block at all";
        ASSERT_TRUE(golden.ok) << golden.error;

        if (!golden.supported)
        {
            // A fixture with no golden must say why and name what would produce one, so the layer
            // is visibly absent rather than quietly unasserted.
            EXPECT_FALSE(golden.reason.empty()) << "no golden and no reason given";
            EXPECT_FALSE(golden.blockedBy.empty()) << "no golden and no task named";
            EXPECT_TRUE(golden.parts.empty());
            continue;
        }

        ASSERT_FALSE(golden.parts.empty());
        for (const GoldenBufferPart& part : golden.parts)
        {
            SCOPED_TRACE("mesh " + std::to_string(part.mesh) + " primitive " +
                         std::to_string(part.primitive));

            CNA::Internal::GltfImport::MeshOut mesh;
            std::string error;
            ASSERT_TRUE(ExtractMeshOut(fixture.Data(), part.mesh, part.primitive, mesh, error))
                << "ExtractMesh: " << error;

            EXPECT_EQ(part.stride, mesh.stride) << "the selected vertex stride changed";
            EXPECT_EQ(part.vertexCount * static_cast<std::size_t>(part.stride),
                      mesh.vertexBytes.size());
            EXPECT_EQ(part.indexElementSize, mesh.use32BitIndices ? 4u : 2u)
                << "the index element size changed";
            EXPECT_EQ(part.indexCount * part.indexElementSize, mesh.indexBytes.size());

            const BufferDifference vb =
                CompareVertexBytesEXT(id, part.stride, part.vertexBytes, mesh.vertexBytes);
            EXPECT_TRUE(vb.equal) << vb.report;

            const BufferDifference ib =
                CompareIndexBytesEXT(id, part.indexElementSize, part.indexBytes, mesh.indexBytes);
            EXPECT_TRUE(ib.equal) << ib.report;

            // The topology the buffer is actually in, and the draw-call count derived from it
            // (§12.3, GLTF-078). Both are read from the manifest rather than assumed, which is the
            // whole point: every loader used to compute numIndices / 3 unconditionally, and that is
            // right for exactly one of the seven topologies.
            EXPECT_EQ(part.topology, std::string(CNA::Internal::GltfImport::PrimitiveTopologyName(
                                          mesh.topology)))
                << "the buffer is not in the topology the manifest says it is";
            EXPECT_EQ(part.primitiveCount,
                      CNA::Internal::GltfImport::PrimitiveCountForTopology(
                          mesh.topology, static_cast<std::size_t>(part.indexCount)))
                << "the primitive count no longer follows the part's own topology";
        }
    }
}

TEST(GltfConformanceL5, SparseIndicesProducesTheGoldenIndexBuffer)
{
    // D4's fixture at the byte level: the layer at which "the indices decode correctly" becomes
    // "the bytes handed to IndexBuffer::SetData are the right bytes". Before GLTF-063 this buffer
    // was twelve zero bytes.
    const LoadedFixture fixture("sparse-indices");
    ASSERT_TRUE(fixture.Ok()) << fixture.Error();
    const GoldenBuffers golden = LoadGoldenBuffersEXT(fixture);
    ASSERT_TRUE(golden.ok) << golden.error;
    ASSERT_TRUE(golden.supported);
    const GoldenBufferPart* part = FirstPart(golden);
    ASSERT_NE(nullptr, part);

    CNA::Internal::GltfImport::MeshOut mesh;
    std::string error;
    ASSERT_TRUE(ExtractMeshOut(fixture.Data(), part->mesh, part->primitive, mesh, error)) << error;

    const BufferDifference ib = CompareIndexBytesEXT("sparse-indices", part->indexElementSize,
                                                     part->indexBytes, mesh.indexBytes);
    EXPECT_TRUE(ib.equal) << ib.report;
    EXPECT_FALSE(std::all_of(mesh.indexBytes.begin(), mesh.indexBytes.end(),
                             [](std::uint8_t b) { return b == 0; }))
        << "the index buffer is all zeros again -- D4 has come back at the byte level";
}

TEST(GltfConformanceL5, EveryTopologyProducesAGoldenAndItsOwnPrimitiveCount)
{
    // Replaces the "a rejected topology has no golden" case: since GLTF-073/GLTF-076/GLTF-078 all
    // seven modes import, so all seven have goldens. What is asserted instead is the property that
    // makes those goldens meaningful -- each carries its own topology and its own §12.3 count,
    // rather than every buffer being described as a triangle list.
    const struct { const char* id; const char* topology; int primitiveCount; } kCases[] = {
        {"mode-triangles",      "TRIANGLES",  2},
        {"mode-triangle-strip", "TRIANGLES",  2},   // converted (GLTF-072)
        {"mode-triangle-fan",   "TRIANGLES",  2},   // converted (GLTF-072)
        {"mode-lines",          "LINES",      2},   // 4 indices -> 2 segments
        {"mode-line-strip",     "LINE_STRIP", 2},   // 3 indices -> 2 segments
        {"mode-line-loop",      "LINE_STRIP", 3},   // 3 + closing index -> 3 segments (GLTF-076)
        {"mode-points",         "POINTS",     4},   // 4 indices -> 4 points
    };

    for (const auto& testCase : kCases)
    {
        SCOPED_TRACE(testCase.id);
        const LoadedFixture fixture(testCase.id);
        ASSERT_TRUE(fixture.Ok()) << fixture.Error();
        const GoldenBuffers golden = LoadGoldenBuffersEXT(fixture);
        ASSERT_TRUE(golden.declared);
        ASSERT_TRUE(golden.ok) << golden.error;
        ASSERT_TRUE(golden.supported) << "this topology no longer imports";
        ASSERT_EQ(1u, golden.parts.size());

        EXPECT_EQ(testCase.topology, golden.parts[0].topology);
        EXPECT_EQ(testCase.primitiveCount, golden.parts[0].primitiveCount);
    }
}

TEST(GltfConformanceL5, AConvertedTopologyProducesTheSameBufferAsAnExplicitTriangleList)
{
    // GLTF-072's byte-level statement. mode-triangle-strip and mode-triangles author the same quad
    // by different routes -- a four-index strip and an explicit six-index list -- so a correct
    // conversion makes their index buffers identical byte for byte. mode-triangle-fan authors the
    // same four indices as the strip under the other rule, and must NOT match, which is what stops
    // this passing under a conversion that ignored the mode.
    const auto indexBytesOf = [](const std::string& id) {
        const LoadedFixture fixture(id);
        EXPECT_TRUE(fixture.Ok()) << fixture.Error();
        const GoldenBuffers golden = LoadGoldenBuffersEXT(fixture);
        EXPECT_TRUE(golden.supported) << id << " has no golden -- its conversion did not land";
        CNA::Internal::GltfImport::MeshOut mesh;
        std::string error;
        EXPECT_TRUE(golden.parts.size() == 1u) << id << " has " << golden.parts.size() << " parts";
        EXPECT_TRUE(ExtractMeshOut(fixture.Data(), golden.parts[0].mesh, golden.parts[0].primitive,
                                   mesh, error)) << error;
        return mesh.indexBytes;
    };

    const std::vector<std::uint8_t> fromStrip = indexBytesOf("mode-triangle-strip");
    const std::vector<std::uint8_t> fromList = indexBytesOf("mode-triangles");
    const std::vector<std::uint8_t> fromFan = indexBytesOf("mode-triangle-fan");

    EXPECT_EQ(fromList, fromStrip)
        << "a converted strip must be byte-identical to the triangle list a file could author "
           "directly -- that equivalence is the whole justification for converting at import";
    EXPECT_NE(fromList, fromFan)
        << "the fan rule produced the strip's triangles, so the mode is not actually being read";
}
