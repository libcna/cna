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
//   4. the four topologies that describe no triangles are rejected with their real mode named,
//      and no index list survives to reach the numIndices/3 path.
//
// Point 4 is a DRAW-PATH gap, not a decoding one: those modes decode fine and are rejected only
// because every loader still computes a triangle-list primitive count. GLTF-073/GLTF-077/GLTF-078
// own it, and a case that starts passing because a draw path landed belongs in their tests.

#include <array>
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
        {0, "POINTS",         PrimitiveTopology::Points,        false},
        {1, "LINES",          PrimitiveTopology::Lines,         false},
        {2, "LINE_LOOP",      PrimitiveTopology::LineLoop,      false},
        {3, "LINE_STRIP",     PrimitiveTopology::LineStrip,     false},
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

TEST(GltfPrimitiveTopology, OnlyTheTriangleProducingModesAreSupported)
{
    // Stated as its own assertion so widening support cannot happen by accident. The rule is not
    // "three of seven" but a property: a mode is importable exactly when it has an exact
    // triangle-list equivalent, because a triangle list is what every renderer already draws.
    int supported = 0;
    for (const ModeRow& row : kModeTable)
    {
        if (IsPrimitiveTopologySupported(row.topology)) { ++supported; }
    }
    EXPECT_EQ(3, supported);
    EXPECT_TRUE(IsPrimitiveTopologySupported(PrimitiveTopology::Triangles));
    EXPECT_TRUE(IsPrimitiveTopologySupported(PrimitiveTopology::TriangleStrip));
    EXPECT_TRUE(IsPrimitiveTopologySupported(PrimitiveTopology::TriangleFan));

    // The converse, as a property rather than a list: a supported mode must convert, and an
    // unsupported one must refuse to, so the two predicates cannot drift apart.
    for (const ModeRow& row : kModeTable)
    {
        SCOPED_TRACE(std::string(row.name));
        const std::vector<std::uint32_t> quad = {0, 1, 2, 3};
        if (row.supported)
        {
            EXPECT_NO_THROW((void)ConvertToTriangleList(quad, row.topology));
        }
        else
        {
            EXPECT_THROW((void)ConvertToTriangleList(quad, row.topology), std::runtime_error);
        }
    }
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

TEST(GltfPrimitiveTopology, EveryImportableCorpusFixtureCarriesTrianglesAtLayer3)
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
            // Whatever the file declared, what comes out is always a triangle list -- that is what
            // makes the conversion policy safe for every renderer downstream.
            EXPECT_EQ(4, primitive.dump.importedTopologyMode);
            EXPECT_EQ("TRIANGLES", primitive.dump.importedTopologyName);
            // ...and the source mode is not overwritten by the conversion, so a strip is still
            // reportable as a strip (GLTF-082).
            EXPECT_TRUE(primitive.dump.topologyMode == 4 || primitive.dump.topologyMode == 5 ||
                        primitive.dump.topologyMode == 6)
                << "an importable primitive carries source mode " << primitive.dump.topologyMode;
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

TEST(GltfPrimitiveTopology, EveryUnsupportedModeIsRejectedWithItsModeNamed)
{
    for (const ModeRow& row : kModeTable)
    {
        if (row.supported) { continue; }
        SCOPED_TRACE(std::string(row.name));
        ScratchDir dir;
        const Parsed parsed(QuadWithMode(", \"mode\": " + std::to_string(row.mode)), dir.path());
        ASSERT_NE(nullptr, parsed.data);

        std::string error;
        try
        {
            (void)ExtractMesh(parsed.data, parsed.data->meshes[0].primitives[0], "Quad", nullptr, 1.0f);
        }
        catch (const std::exception& e)
        {
            error = e.what();
        }

        ASSERT_FALSE(error.empty())
            << "mode " << row.mode << " imported silently -- if its draw path landed "
               "(GLTF-073/GLTF-077/GLTF-078), this case belongs in those tests, not here";
        // The diagnostic has to be actionable on its own: which primitive, which mode by number,
        // and which mode by the name the file format uses.
        EXPECT_NE(std::string::npos, error.find("Quad")) << error;
        EXPECT_NE(std::string::npos, error.find("mode " + std::to_string(row.mode))) << error;
        EXPECT_NE(std::string::npos, error.find(row.name)) << error;
    }
}

TEST(GltfPrimitiveTopology, ARejectedPrimitiveProducesNoIndexListAtAll)
{
    // The precise difference between "explicitly rejected" and "silently corrupted". This quad's
    // six indices would divide evenly into two triangles under the old path, so a reinterpreting
    // importer would have produced plausible-looking geometry from a strip, a fan or a point cloud
    // and said nothing at all.
    for (const ModeRow& row : kModeTable)
    {
        if (row.supported) { continue; }
        SCOPED_TRACE(std::string(row.name));
        ScratchDir dir;
        const Parsed parsed(QuadWithMode(", \"mode\": " + std::to_string(row.mode)), dir.path());
        ASSERT_NE(nullptr, parsed.data);

        MeshOut mesh;
        EXPECT_THROW(mesh = ExtractMesh(parsed.data, parsed.data->meshes[0].primitives[0], "Quad",
                                        nullptr, 1.0f),
                     std::runtime_error);
        EXPECT_TRUE(mesh.indexBytes.empty());
        EXPECT_TRUE(mesh.vertexBytes.empty());
        EXPECT_EQ(0u, DecodedIndices(mesh).size() / 3);
    }
}

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
