// SPDX-License-Identifier: MS-PL
//
// plan_gltf.md GLTF-071: mesh.primitive.mode is read, classified, and never silently reinterpreted.
//
// The defect (D5) was that cgltf_primitive_type had zero occurrences in CNA production code. Every
// primitive, whatever its declared topology, was decoded into a flat index list that all three
// loaders then divided by three and drew as a triangle list. A four-index TRIANGLE_STRIP became one
// triangle with vertex 3 unreachable; a four-vertex POINTS cloud became one arbitrary triangle.
// Neither produced a warning.
//
// What GLTF-071 owns is reading and classifying, not converting. So these tests assert three
// things and deliberately not a fourth:
//
//   1. all seven glTF modes classify, by number and by specification name;
//   2. TRIANGLES imports exactly as it did before, byte for byte, and now carries its topology;
//   3. every other mode is rejected with its real mode named in the diagnostic, and no index list
//      survives to reach the numIndices/3 path.
//
// The fourth -- converting strips and fans to triangle lists, carrying the line topologies, and
// deciding what a point primitive becomes -- is GLTF-072, and a test here must not pre-empt it. A
// case that starts passing because conversion landed belongs in a GLTF-072 test, not this file.

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
        {5, "TRIANGLE_STRIP", PrimitiveTopology::TriangleStrip, false},
        {6, "TRIANGLE_FAN",   PrimitiveTopology::TriangleFan,   false},
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

TEST(GltfPrimitiveTopology, ExactlyOneModeIsSupportedTodayAndItIsTriangles)
{
    // Stated as its own assertion so widening support cannot happen by accident: a mode becoming
    // importable is GLTF-072's decision and must arrive with its conversion, not before it.
    int supported = 0;
    for (const ModeRow& row : kModeTable)
    {
        if (IsPrimitiveTopologySupported(row.topology)) { ++supported; }
    }
    EXPECT_EQ(1, supported);
    EXPECT_TRUE(IsPrimitiveTopologySupported(PrimitiveTopology::Triangles));
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
    EXPECT_EQ(32, mesh.stride);
    EXPECT_EQ(4u * 32u, mesh.vertexBytes.size());
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
            EXPECT_EQ(4, primitive.dump.topologyMode);
            EXPECT_EQ("TRIANGLES", primitive.dump.topologyName);
        }
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
            << "mode " << row.mode << " imported silently -- if GLTF-072 landed, this case belongs "
               "in its tests, not here";
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
