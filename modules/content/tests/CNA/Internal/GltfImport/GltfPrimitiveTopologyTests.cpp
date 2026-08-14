// SPDX-License-Identifier: MS-PL
//
// plan_gltf.md GLTF-071 / GLTF-072: mesh.primitive.mode is read, classified, converted where it
// has an exact triangle-list equivalent, and never silently reinterpreted.
//
// The defect (D5) was that cgltf_primitive_type had zero occurrences in CNA production code. Every
// primitive, whatever its declared topology, was decoded into a flat index list that all three
// loaders then divided by three and drew as a triangle list. A four-index TRIANGLE_STRIP became one
// triangle with vertex 3 unreachable; a four-vertex POINTS cloud became one arbitrary triangle.
// Neither produced a warning.
//
// GLTF-071 owns reading and classifying; GLTF-072 owns the conversion policy of §10.1. Together
// these tests assert:
//
//   1. all seven glTF modes classify, by number and by specification name;
//   2. TRIANGLES imports exactly as it did before, byte for byte, and carries its topology;
//   3. TRIANGLE_STRIP and TRIANGLE_FAN convert to an equivalent triangle list with winding
//      preserved, and the source mode survives the conversion so it can still be reported;
//   4. the four topologies that describe no triangles import as THEMSELVES -- a LINE_LOOP as a
//      LINE_STRIP carrying the closing segment glTF leaves implicit in the mode (GLTF-076), the
//      rest untouched -- each with its own §12.3 primitive count rather than numIndices/3
//      (GLTF-078) and a real PrimitiveType on the part (GLTF-073).

#include <array>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <gtest/gtest.h>
#include <stdexcept>
#include <string>
#include <vector>

#include "CNA/Internal/GltfImport/GltfImportCore.hpp"

#include "GltfFixtureCorpus.hpp"
#include "GltfOracleEXT.hpp"

using namespace CNA::Internal::GltfImport;
using Microsoft::Xna::Framework::Graphics::PrimitiveType;
using CnaTest::GltfOracle::ExtractedPrimitive;
using CnaTest::GltfOracle::ExtractSceneMeshesEXT;
using CnaTest::GltfOracle::LoadedFixture;

namespace
{
    /// The specification's own mode table (§3.7.2.1), written out once so a classification change
    /// has to disagree with the file format rather than merely with itself.
    struct ModeRow
    {
        int mode;
        const char* name;
        PrimitiveTopology topology;
        bool supported;
    };

    constexpr std::array<ModeRow, 7> kModeTable = {{
        {0, "POINTS",         PrimitiveTopology::Points,        true},
        {1, "LINES",          PrimitiveTopology::Lines,         true},
        {2, "LINE_LOOP",      PrimitiveTopology::LineLoop,      true},
        {3, "LINE_STRIP",     PrimitiveTopology::LineStrip,     true},
        {4, "TRIANGLES",      PrimitiveTopology::Triangles,     true},
        {5, "TRIANGLE_STRIP", PrimitiveTopology::TriangleStrip, true},
        {6, "TRIANGLE_FAN",   PrimitiveTopology::TriangleFan,   true},
    }};

    class ScratchDir
    {
    public:
        ScratchDir()
            : dir_(std::filesystem::temp_directory_path() /
                   ("cna_gltf_topology_test_" + std::to_string(reinterpret_cast<std::uintptr_t>(this))))
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

    /// One indexed quad -- four vertices, six indices -- authored once and re-declared under each
    /// mode in turn, so the only thing that varies between the cases below is `mode` itself.
    /// Six indices divide evenly by three, so a reinterpreting importer would happily produce two
    /// triangles from any of the seven modes and report nothing: exactly the silent path D5 was.
    /// `modeJson` is empty for the "no mode at all" case, whose glTF default is TRIANGLES.
    std::string QuadWithMode(const std::string& modeJson)
    {
        return std::string(R"({
  "asset": { "version": "2.0" },
  "scene": 0,
  "scenes": [ { "nodes": [0] } ],
  "nodes": [ { "name": "MeshNode", "mesh": 0 } ],
  "meshes": [ { "name": "Quad", "primitives": [
    { "attributes": { "POSITION": 0 }, "indices": 1)") + modeJson + R"( }
  ] } ],
  "buffers": [ { "byteLength": 60,
    "uri": "data:application/octet-stream;base64,AAAAAAAAAAAAAAAAAACAPwAAAAAAAAAAAACAPwAAgD8AAAAAAAAAAAAAgD8AAAAAAAABAAIAAAACAAMA" } ],
  "bufferViews": [
    { "buffer": 0, "byteOffset": 0,  "byteLength": 48 },
    { "buffer": 0, "byteOffset": 48, "byteLength": 12 }
  ],
  "accessors": [
    { "bufferView": 0, "componentType": 5126, "count": 4, "type": "VEC3",
      "min": [0,0,0], "max": [1,1,0] },
    { "bufferView": 1, "componentType": 5123, "count": 6, "type": "SCALAR" }
  ]
})";
    }

    std::string DracoQuadWithMode(int mode)
    {
        std::string json = QuadWithMode(
            ", \"mode\": " + std::to_string(mode) +
            ", \"extensions\": { \"KHR_draco_mesh_compression\": { "
            "\"bufferView\": 0, \"attributes\": { \"POSITION\": 0 } } }");
        const std::string asset = "\"asset\": { \"version\": \"2.0\" },";
        const std::size_t at = json.find(asset);
        if (at != std::string::npos)
        {
            json.insert(at + asset.size(),
                        "\n  \"extensionsUsed\": [ \"KHR_draco_mesh_compression\" ],");
        }
        return json;
    }

    struct Parsed
    {
        cgltf_data* data = nullptr;

        explicit Parsed(const std::string& gltfJson, const std::filesystem::path& dir)
        {
            const std::filesystem::path gltfPath = dir / "fixture.gltf";
            {
                std::ofstream file(gltfPath, std::ios::binary);
                file << gltfJson;
            }
            cgltf_options options{};
            if (cgltf_parse_file(&options, gltfPath.string().c_str(), &data) != cgltf_result_success)
            {
                data = nullptr;
                return;
            }
            if (cgltf_load_buffers(&options, data, gltfPath.string().c_str()) != cgltf_result_success)
            {
                cgltf_free(data);
                data = nullptr;
            }
        }
        ~Parsed() { if (data != nullptr) { cgltf_free(data); } }
        Parsed(const Parsed&) = delete;
        Parsed& operator=(const Parsed&) = delete;
    };

    std::vector<std::uint32_t> DecodedIndices(const MeshOut& mesh)
    {
        std::vector<std::uint32_t> out;
        const std::size_t width = mesh.use32BitIndices ? 4u : 2u;
        for (std::size_t o = 0; o + width <= mesh.indexBytes.size(); o += width)
        {
            std::uint32_t v = 0;
            if (mesh.use32BitIndices) { std::memcpy(&v, mesh.indexBytes.data() + o, 4); }
            else
            {
                std::uint16_t narrow = 0;
                std::memcpy(&narrow, mesh.indexBytes.data() + o, 2);
                v = narrow;
            }
            out.push_back(v);
        }
        return out;
    }
}

// --- The classification table -------------------------------------------------------------------

TEST(GltfPrimitiveTopology, EveryGltfModeClassifiesByNumberAndName)
{
    for (const ModeRow& row : kModeTable)
    {
        SCOPED_TRACE(std::string(row.name));
        EXPECT_EQ(row.mode, PrimitiveTopologyMode(row.topology));
        EXPECT_EQ(std::string(row.name), std::string(PrimitiveTopologyName(row.topology)));
        EXPECT_EQ(row.supported, IsPrimitiveTopologySupported(row.topology));
    }
}

TEST(GltfPrimitiveTopology, EveryModeImportsAndOnlyTriangleModesConvert)
{
    // Two distinct properties, stated separately because conflating them is what GLTF-072's scope
    // boundary was about. EVERY mode is importable now -- the four that describe no triangles have
    // a draw path since GLTF-073/GLTF-076/GLTF-078. But only three CONVERT: asking a triangle
    // converter for a line run is a caller error, and it says so rather than passing the run
    // through, because a function that quietly returned its input would make its own name a lie.
    for (const ModeRow& row : kModeTable)
    {
        SCOPED_TRACE(std::string(row.name));
        EXPECT_TRUE(IsPrimitiveTopologySupported(row.topology));

        const std::vector<std::uint32_t> quad = {0, 1, 2, 3};
        const bool convertible = row.topology == PrimitiveTopology::Triangles
                              || row.topology == PrimitiveTopology::TriangleStrip
                              || row.topology == PrimitiveTopology::TriangleFan;
        if (convertible)
        {
            EXPECT_NO_THROW((void)ConvertToTriangleList(quad, row.topology));
        }
        else
        {
            EXPECT_THROW((void)ConvertToTriangleList(quad, row.topology), std::runtime_error);
        }
    }
}

TEST(GltfPrimitiveTopology, ThePrimitiveCountFollowsTheTopologyRatherThanDividingByThree)
{
    // plan_gltf.md §12.3, stated as the table it is. All three loaders hardcoded numIndices / 3,
    // which is right for a triangle list and silently wrong for every other topology -- and was
    // written out three times, so the three could drift.
    EXPECT_EQ(2, PrimitiveCountForTopology(PrimitiveTopology::Triangles, 6));
    EXPECT_EQ(2, PrimitiveCountForTopology(PrimitiveTopology::Lines, 4));
    EXPECT_EQ(3, PrimitiveCountForTopology(PrimitiveTopology::LineStrip, 4));
    EXPECT_EQ(4, PrimitiveCountForTopology(PrimitiveTopology::Points, 4));

    // A run too short to describe one primitive is zero, never negative -- the n-1 and n-2
    // formulas are the two that could underflow.
    EXPECT_EQ(0, PrimitiveCountForTopology(PrimitiveTopology::LineStrip, 0));
    EXPECT_EQ(0, PrimitiveCountForTopology(PrimitiveTopology::TriangleStrip, 2));
    EXPECT_EQ(0, PrimitiveCountForTopology(PrimitiveTopology::Triangles, 2));
}

TEST(GltfPrimitiveTopology, ALineLoopGainsItsClosingSegmentAndBecomesAStrip)
{
    // GLTF-076. XNA has no LineLoop, and the difference between a loop and a strip is exactly one
    // segment -- the one back to the first vertex, which glTF leaves implicit in the mode. Dropping
    // the mode therefore lost a whole segment before it lost the topology.
    ScratchDir dir;
    const Parsed parsed(QuadWithMode(", \"mode\": 2"), dir.path());
    ASSERT_NE(nullptr, parsed.data);

    const MeshOut mesh =
        ExtractMesh(parsed.data, parsed.data->meshes[0].primitives[0], "Quad", nullptr, 1.0f);

    EXPECT_EQ(PrimitiveTopology::LineLoop, mesh.sourceTopology);
    EXPECT_EQ(PrimitiveTopology::LineStrip, mesh.topology)
        << "a LINE_LOOP must arrive as a LINE_STRIP -- XNA has no loop topology";

    // The authored run is [0,1,2,0,2,3]; closing it appends the first index again.
    EXPECT_EQ(std::vector<std::uint32_t>({0, 1, 2, 0, 2, 3, 0}), DecodedIndices(mesh));
    EXPECT_EQ(PrimitiveType::LineStrip, PrimitiveTypeForTopology(mesh.topology));
}

TEST(GltfPrimitiveTopology, ClassifyReadsTheModeTheFileActuallyDeclares)
{
    for (const ModeRow& row : kModeTable)
    {
        SCOPED_TRACE(std::string(row.name));
        ScratchDir dir;
        const Parsed parsed(QuadWithMode(", \"mode\": " + std::to_string(row.mode)), dir.path());
        ASSERT_NE(nullptr, parsed.data);
        ASSERT_EQ(1u, static_cast<std::size_t>(parsed.data->meshes_count));

        const PrimitiveTopology topology =
            ClassifyPrimitiveTopology(parsed.data->meshes[0].primitives[0], "Quad");
        EXPECT_EQ(row.topology, topology);
        EXPECT_EQ(row.mode, PrimitiveTopologyMode(topology));
        EXPECT_EQ(std::string(row.name), std::string(PrimitiveTopologyName(topology)));
    }
}

TEST(GltfPrimitiveTopology, AnAbsentModeIsTrianglesPerTheSpecificationDefault)
{
    ScratchDir dir;
    const Parsed parsed(QuadWithMode(""), dir.path());
    ASSERT_NE(nullptr, parsed.data);
    EXPECT_EQ(PrimitiveTopology::Triangles,
              ClassifyPrimitiveTopology(parsed.data->meshes[0].primitives[0], "Quad"));
}

// --- TRIANGLES still imports exactly as before ---------------------------------------------------

TEST(GltfPrimitiveTopology, TrianglesImportUnchangedAndCarryTheirTopology)
{
    ScratchDir dir;
    const Parsed parsed(QuadWithMode(", \"mode\": 4"), dir.path());
    ASSERT_NE(nullptr, parsed.data);

    const MeshOut mesh =
        ExtractMesh(parsed.data, parsed.data->meshes[0].primitives[0], "Quad", nullptr, 1.0f);

    EXPECT_EQ(PrimitiveTopology::Triangles, mesh.topology);
    EXPECT_EQ(PrimitiveTopology::Triangles, mesh.sourceTopology);
    // Stride 48: this quad declares no material, and glTF's default material is
    // metallic-roughness, so GLTF-215 selects the PBR layout.
    EXPECT_EQ(48, mesh.stride);
    EXPECT_EQ(4u * 48u, mesh.vertexBytes.size());
    EXPECT_EQ(std::vector<std::uint32_t>({0, 1, 2, 0, 2, 3}), DecodedIndices(mesh));

    // The same asset with no "mode" key at all must produce byte-identical output: the default is
    // TRIANGLES, so reading the mode may not have changed what an ordinary asset imports as.
    ScratchDir defaultDir;
    const Parsed noMode(QuadWithMode(""), defaultDir.path());
    ASSERT_NE(nullptr, noMode.data);
    const MeshOut implicit =
        ExtractMesh(noMode.data, noMode.data->meshes[0].primitives[0], "Quad", nullptr, 1.0f);
    EXPECT_EQ(mesh.topology, implicit.topology);
    EXPECT_EQ(mesh.stride, implicit.stride);
    EXPECT_EQ(mesh.vertexBytes, implicit.vertexBytes);
    EXPECT_EQ(mesh.indexBytes, implicit.indexBytes);
}

TEST(GltfPrimitiveTopology, EveryImportedCorpusFixtureCarriesADrawableTopologyAtLayer3)
{
    // The corpus-wide statement of the same thing: no fixture imports without a topology, and the
    // only topology that reaches L3 today is the one the file declares.
    for (const std::string& id : CnaTest::GltfOracle::CorpusFixtureIds())
    {
        SCOPED_TRACE(id);
        const LoadedFixture fixture(id);
        ASSERT_TRUE(fixture.Ok()) << fixture.Error();

        for (const ExtractedPrimitive& primitive : ExtractSceneMeshesEXT(fixture.Data()))
        {
            if (!primitive.extracted) { continue; }   // rejected topologies: see the D5 tests
            EXPECT_TRUE(primitive.dump.topologyCarried);
            // A triangle mode always comes out as a triangle list; a LINE_LOOP comes out as a
            // LINE_STRIP; everything else comes out as itself. The source mode is never
            // overwritten by any of that, so a strip is still reportable as a strip (GLTF-082).
            const int source = primitive.dump.topologyMode;
            const int imported = primitive.dump.importedTopologyMode;
            if (source == 4 || source == 5 || source == 6) { EXPECT_EQ(4, imported); }
            else if (source == 2)                          { EXPECT_EQ(3, imported); }
            else                                           { EXPECT_EQ(source, imported); }
        }
    }
}

// --- GLTF-072: strips and fans convert, with winding preserved -----------------------------------

TEST(GltfPrimitiveTopology, StripConversionSwapsTheOddTrianglesCorners)
{
    // §3.7.2.1's rule, on the smallest input that can tell a correct conversion from a plausible
    // one. Emitting (i, i+1, i+2) for every triangle would give [1,2,3] as the second, which has
    // the opposite winding and would be culled away by a renderer the author expected to draw it.
    EXPECT_EQ(std::vector<std::uint32_t>({0, 1, 2, 2, 1, 3}),
              ConvertToTriangleList({0, 1, 2, 3}, PrimitiveTopology::TriangleStrip));

    // Five indices, three triangles: the alternation has to continue, not just apply once.
    EXPECT_EQ(std::vector<std::uint32_t>({0, 1, 2, 2, 1, 3, 2, 3, 4}),
              ConvertToTriangleList({0, 1, 2, 3, 4}, PrimitiveTopology::TriangleStrip));
}

TEST(GltfPrimitiveTopology, FanConversionKeepsEveryTriangleAnchoredToTheFirstVertex)
{
    EXPECT_EQ(std::vector<std::uint32_t>({0, 1, 2, 0, 2, 3}),
              ConvertToTriangleList({0, 1, 2, 3}, PrimitiveTopology::TriangleFan));
    EXPECT_EQ(std::vector<std::uint32_t>({0, 1, 2, 0, 2, 3, 0, 3, 4}),
              ConvertToTriangleList({0, 1, 2, 3, 4}, PrimitiveTopology::TriangleFan));

    // The two rules must not coincide: given identical input they produce different triangles, so
    // a fixture cannot pass under the wrong one.
    EXPECT_NE(ConvertToTriangleList({0, 1, 2, 3}, PrimitiveTopology::TriangleFan),
              ConvertToTriangleList({0, 1, 2, 3}, PrimitiveTopology::TriangleStrip));
}

TEST(GltfPrimitiveTopology, ATriangleListIsReturnedVerbatim)
{
    // Including a trailing partial triple: what a malformed index count becomes is GLTF-079's
    // decision, and trimming it here would silently change every file CNA already imports.
    EXPECT_EQ(std::vector<std::uint32_t>({0, 1, 2, 2, 1, 3}),
              ConvertToTriangleList({0, 1, 2, 2, 1, 3}, PrimitiveTopology::Triangles));
    EXPECT_EQ(std::vector<std::uint32_t>({0, 1, 2, 3}),
              ConvertToTriangleList({0, 1, 2, 3}, PrimitiveTopology::Triangles));
}

TEST(GltfPrimitiveTopology, AnIndexRunTooShortForOneTriangleConvertsToNothing)
{
    for (const PrimitiveTopology topology :
         {PrimitiveTopology::TriangleStrip, PrimitiveTopology::TriangleFan})
    {
        EXPECT_TRUE(ConvertToTriangleList({}, topology).empty());
        EXPECT_TRUE(ConvertToTriangleList({0}, topology).empty());
        EXPECT_TRUE(ConvertToTriangleList({0, 1}, topology).empty());
    }
}

TEST(GltfPrimitiveTopology, StripAndFanImportThroughExtractMeshAsTriangleLists)
{
    // End to end: ExtractMesh applies exactly the documented rule to the exact index run the file
    // authors. The rules themselves are pinned by hand-written values in the tests above, so
    // composing them here states the claim that actually belongs to ExtractMesh -- that it
    // converts, and converts by the same rule -- without restating the arithmetic.
    const std::vector<std::uint32_t> authored = {0, 1, 2, 0, 2, 3};
    for (const PrimitiveTopology source : {PrimitiveTopology::Triangles,
                                           PrimitiveTopology::TriangleStrip,
                                           PrimitiveTopology::TriangleFan})
    {
        const int mode = PrimitiveTopologyMode(source);
        SCOPED_TRACE("mode " + std::to_string(mode));
        ScratchDir dir;
        const Parsed parsed(QuadWithMode(", \"mode\": " + std::to_string(mode)), dir.path());
        ASSERT_NE(nullptr, parsed.data);

        const MeshOut mesh =
            ExtractMesh(parsed.data, parsed.data->meshes[0].primitives[0], "Quad", nullptr, 1.0f);

        // The conversion happened, and it is visible: the emitted list is a triangle list, while
        // the source topology still says what the file declared.
        EXPECT_EQ(PrimitiveTopology::Triangles, mesh.topology);
        EXPECT_EQ(source, mesh.sourceTopology);
        EXPECT_EQ(ConvertToTriangleList(authored, source), DecodedIndices(mesh));
        EXPECT_EQ(0u, DecodedIndices(mesh).size() % 3)
            << "a converted list must always divide evenly into triangles";

        // The vertex buffer is untouched by a topology conversion -- only the index list is
        // rewritten, which is precisely why no renderer needs to change.
        EXPECT_EQ(4u * 48u, mesh.vertexBytes.size());
    }
}

// --- Everything else is rejected, by name --------------------------------------------------------



TEST(GltfPrimitiveTopology, AModeOutsideTheSpecifiedRangeIsRejectedRatherThanAssumedTriangles)
{
    ScratchDir dir;
    const Parsed parsed(QuadWithMode(", \"mode\": 9"), dir.path());
    ASSERT_NE(nullptr, parsed.data);

    // cgltf leaves the type invalid for a mode the specification does not define. Guessing
    // "probably triangles" for it is the same reflex that produced D5 in the first place.
    EXPECT_THROW((void)ClassifyPrimitiveTopology(parsed.data->meshes[0].primitives[0], "Quad"),
                 std::runtime_error);
    EXPECT_THROW((void)ExtractMesh(parsed.data, parsed.data->meshes[0].primitives[0], "Quad",
                                   nullptr, 1.0f),
                 std::runtime_error);
}

// --- plan_gltf.md GLTF-080: topology and Draco cannot disagree -------------------------------------

// KHR_draco_mesh_compression normatively permits exactly TRIANGLES and TRIANGLE_STRIP. That is
// narrower than core glTF's three triangle-producing topologies: TRIANGLE_FAN is invalid too.
// Refused rather than silently drawn as triangles.
//
// Asserted both as a complete classification partition and through five parsed extension-bearing
// primitives. Building a valid compressed payload needs libdraco at generation time, but rejecting
// a forbidden source mode is intentionally earlier than decoding and needs no such dependency.
TEST(GltfPrimitiveTopology, DracoIsRefusedForEveryModeItCannotEncode)
{
    using CNA::Internal::GltfImport::PrimitiveTopology;
    using CNA::Internal::GltfImport::IsDracoTopologyAllowedEXT;

    const PrimitiveTopology allowedModes[] = {
        PrimitiveTopology::Triangles, PrimitiveTopology::TriangleStrip,
    };
    const PrimitiveTopology refusedModes[] = {
        PrimitiveTopology::Points, PrimitiveTopology::Lines,
        PrimitiveTopology::LineLoop, PrimitiveTopology::LineStrip,
        PrimitiveTopology::TriangleFan,
    };

    for (const PrimitiveTopology topology : allowedModes)
    {
        EXPECT_TRUE(IsDracoTopologyAllowedEXT(topology))
            << PrimitiveTopologyName(topology) << " is permitted by the extension";
    }
    for (const PrimitiveTopology topology : refusedModes)
    {
        EXPECT_FALSE(IsDracoTopologyAllowedEXT(topology))
            << PrimitiveTopologyName(topology) << " is not permitted by the extension";
    }

    // And the two sets together are every mode there is -- so the rule has no gap for a mode to
    // fall through.
    EXPECT_EQ(7u, std::size(allowedModes) + std::size(refusedModes));
}

TEST(GltfPrimitiveTopology, InvalidDracoModesAreRejectedBeforeDecoderAvailabilityMatters)
{
    const int refusedModes[] = {0, 1, 2, 3, 6};
    for (const int mode : refusedModes)
    {
        SCOPED_TRACE(std::string("mode ") + std::to_string(mode));
        ScratchDir dir;
        const Parsed parsed(DracoQuadWithMode(mode), dir.path());
        ASSERT_NE(nullptr, parsed.data);
        ASSERT_TRUE(parsed.data->meshes[0].primitives[0].has_draco_mesh_compression);

        try
        {
            (void)ExtractMesh(parsed.data, parsed.data->meshes[0].primitives[0],
                              "DracoQuad", nullptr, 1.0f);
            FAIL() << "an invalid Draco mode reached import";
        }
        catch (const std::runtime_error& error)
        {
            const std::string message = error.what();
            EXPECT_NE(std::string::npos,
                      message.find("permits only TRIANGLES or TRIANGLE_STRIP"))
                << message;
        }
    }
}

TEST(GltfPrimitiveTopology, DecodedDracoStripFacesAreNotConvertedAsASecondStrip)
{
    // A decoded draco::Mesh exposes explicit faces. These two face triples describe a quad. If
    // they were re-read as one six-index TRIANGLE_STRIP, conversion would invent four overlapping
    // triangles instead of preserving these two.
    const std::vector<std::uint32_t> decodedFaces = {0, 1, 2, 0, 2, 3};
    EXPECT_EQ(decodedFaces,
              NormalizeTriangleIndicesEXT(
                  decodedFaces, PrimitiveTopology::TriangleStrip, true));

    const auto ordinaryStrip = NormalizeTriangleIndicesEXT(
        std::vector<std::uint32_t>{0, 1, 2, 3}, PrimitiveTopology::TriangleStrip, false);
    EXPECT_EQ(std::vector<std::uint32_t>({0, 1, 2, 2, 1, 3}), ordinaryStrip)
        << "an ordinary glTF strip still needs its normal conversion";

    EXPECT_THROW(
        (void)NormalizeTriangleIndicesEXT(
            decodedFaces, PrimitiveTopology::TriangleFan, true),
        std::runtime_error);
}

// --- GLTF-081 / GLTF-082: conversion must not disturb the vertices, and must say it happened ----

TEST(GltfPrimitiveTopology, ConvertingAStripRewritesTheIndicesAndLeavesTheVertexOrderAlone)
{
    // Morph deltas are addressed per VERTEX, by position in the target accessor, while the strip
    // conversion rewrites the INDEX list. The two only coexist if the conversion leaves the vertex
    // order completely alone -- and the tempting optimisation is exactly the one that breaks it:
    // vertices 1 and 2 appear in both of a four-index strip's triangles, so de-duplicating or
    // compacting would still produce a mesh, still produce a morph, and put every delta on the
    // wrong vertex. A plausible deformation of the wrong shape, with nothing to indicate it.
    using namespace CNA::Internal::GltfImport;

    const CnaTest::GltfOracle::LoadedFixture fixture("mode-triangle-strip-morph");
    ASSERT_TRUE(fixture.Ok()) << fixture.Error();
    const CNA::Internal::JsonValue& expected =
        CnaTest::GltfOracle::Path(fixture.Expected(), "l4.topologyMorph");

    const cgltf_data& data = fixture.Data();
    const MeshOut out = ExtractMesh(&data, data.meshes[0].primitives[0], "probe", nullptr, 1.0f);

    EXPECT_EQ(PrimitiveTopology::TriangleStrip, out.sourceTopology);
    EXPECT_EQ(PrimitiveTopology::Triangles, out.topology);

    // The vertex COUNT is the first half of "order unchanged": a compaction would shrink it.
    const auto expectedVertexCount =
        static_cast<std::size_t>(CnaTest::GltfOracle::NumberOr(expected, "vertexCount", -1.0));
    ASSERT_GT(out.stride, 0);
    EXPECT_EQ(expectedVertexCount, out.vertexBytes.size() / static_cast<std::size_t>(out.stride));

    ASSERT_EQ(expectedVertexCount * static_cast<std::size_t>(out.stride), out.vertexBytes.size());

    // The morph deltas must line up with those same vertices, one for one.
    ASSERT_EQ(1u, out.morphPositionDeltas.size());
    EXPECT_EQ(expectedVertexCount, out.morphPositionDeltas[0].size())
        << "the delta count no longer matches the vertex count, so the conversion changed one of "
           "them -- and the two are addressed by the same index";

    const std::vector<CNA::Internal::JsonValue>& deltas =
        CnaTest::GltfOracle::Member(expected, "morphDeltas").arrayValue;
    ASSERT_EQ(expectedVertexCount, deltas.size());
    for (std::size_t v = 0; v < deltas.size(); ++v)
    {
        SCOPED_TRACE("vertex " + std::to_string(v));
        const std::vector<double> d = CnaTest::GltfOracle::Numbers(deltas[v]);
        ASSERT_EQ(3u, d.size());
        // Each delta is a different distance along +Z, so a permutation is a different staircase
        // rather than a subtly different surface.
        EXPECT_NEAR(static_cast<float>(d[2]), out.morphPositionDeltas[0][v].Z, 1e-5f)
            << "delta " << v << " landed on the wrong vertex";
    }
}

TEST(GltfPrimitiveTopology, AConvertedPrimitiveRecordsBothTopologiesSoTheRewriteIsVisible)
{
    // GLTF-082. The conversion is exact and loses nothing, but it does renumber: the triangle a
    // consumer draws is not at the index the file put it at, so anything mapping a picked triangle
    // or a debug index back to the source primitive is wrong without knowing. Recording both
    // topologies is what makes that knowable, and the assertion is that they DIFFER for a
    // converted primitive and AGREE for one that was already a list.
    using namespace CNA::Internal::GltfImport;

    struct Case { const char* id; PrimitiveTopology source; bool converted; };
    for (const Case& c : {Case{"mode-triangle-strip", PrimitiveTopology::TriangleStrip, true},
                          Case{"mode-triangle-fan", PrimitiveTopology::TriangleFan, true},
                          Case{"mode-triangles", PrimitiveTopology::Triangles, false},
                          Case{"mode-lines", PrimitiveTopology::Lines, false}})
    {
        SCOPED_TRACE(c.id);
        const CnaTest::GltfOracle::LoadedFixture fixture(c.id);
        ASSERT_TRUE(fixture.Ok()) << fixture.Error();
        const cgltf_data& data = fixture.Data();
        const MeshOut out = ExtractMesh(&data, data.meshes[0].primitives[0], "probe", nullptr, 1.0f);

        EXPECT_EQ(c.source, out.sourceTopology) << "the declared mode was not preserved";
        EXPECT_EQ(c.converted, out.sourceTopology != out.topology)
            << "sourceTopology != topology is the signal a consumer reads to know the index list "
               "was rewritten; it must be true exactly when it was";
    }
}
