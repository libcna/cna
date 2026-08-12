// SPDX-License-Identifier: MS-PL
//
// plan_gltf.md GLTF-134 / GLTF-139 / GLTF-140 / GLTF-141 / GLTF-142 / GLTF-144 / GLTF-238.
//
// What shape the imported `Model` has, as distinct from where its geometry is. Every row here is
// about a mapping decision rather than a number, and each has one wrong answer that renders
// perfectly and still misleads whoever iterates `Model.Meshes`:
//
//   * a `ModelMesh` per PRIMITIVE makes a two-material object look like two objects, and gives
//     each half its own `ParentBone` and `BoundingSphere` (GLTF-139);
//   * an ordering that depends on a hash or a pointer makes a golden byte comparison flap
//     (GLTF-140);
//   * a name of "mesh3" cannot be traced back to anything in the file (GLTF-141);
//   * an empty primitive that reaches buffer creation is a zero-size allocation, and one that is
//     dropped silently is a model missing a part nobody can account for (GLTF-142);
//   * a camera or light node that becomes a mesh instance draws whatever mesh index it collides
//     with (GLTF-144);
//   * a file with no `scenes` at all still has meshes, and refusing it or importing nothing are
//     both worse than the documented fallback (GLTF-134).

#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>
#include <string>
#include <system_error>
#include <vector>

#include "CNA/Internal/GltfImport/GltfImportCore.hpp"
#include "GltfFixtureCorpus.hpp"
#include "Microsoft/Xna/Framework/Content/ContentManager.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/Model.hpp"
#include "Microsoft/Xna/Framework/Graphics/ModelMesh.hpp"
#include "Microsoft/Xna/Framework/Graphics/ModelMeshCollection.hpp"
#include "Microsoft/Xna/Framework/Graphics/ModelMeshPart.hpp"
#include "Microsoft/Xna/Framework/Graphics/ModelMeshPartCollection.hpp"
#include "Microsoft/Xna/Framework/Graphics/ModelBone.hpp"

using namespace CNA::Internal::GltfImport;
using CnaTest::GltfOracle::CorpusDirectory;
using CnaTest::GltfOracle::LoadedFixture;
using Microsoft::Xna::Framework::Content::ContentManager;
using Microsoft::Xna::Framework::Graphics::GraphicsDevice;
using Microsoft::Xna::Framework::Graphics::Model;
using Microsoft::Xna::Framework::Graphics::ModelMesh;

namespace
{
    class ScratchDir
    {
    public:
        ScratchDir()
            : dir_(std::filesystem::temp_directory_path()
                   / ("cna_gltf_model_shape_"
                      + std::to_string(reinterpret_cast<std::uintptr_t>(this))))
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

    struct Parsed
    {
        cgltf_data* data = nullptr;
        ~Parsed() { if (data != nullptr) { cgltf_free(data); } }
        Parsed() = default;
        Parsed(const Parsed&) = delete;
        Parsed& operator=(const Parsed&) = delete;
    };

    bool Parse(Parsed& out, const std::string& json)
    {
        cgltf_options options{};
        if (cgltf_parse(&options, json.data(), json.size(), &out.data) != cgltf_result_success)
        {
            return false;
        }
        return cgltf_load_buffers(&options, out.data, ".") == cgltf_result_success;
    }

    /// Writes @p json as `<dir>/shape.gltf` and loads it through the real runtime path.
    Model LoadModel(const ScratchDir& dir, const std::string& json, GraphicsDevice& gd,
                    ContentManager& cm)
    {
        std::ofstream(dir.path() / "shape.gltf", std::ios::binary) << json;
        (void)gd;
        return cm.Load<Model>("shape");
    }

    /// Two triangles as two primitives of ONE glTF mesh, each with its own material -- the shape
    /// every exporter produces for a single object with two materials, and the one that separates
    /// "a ModelMesh per mesh" from "a ModelMesh per primitive".
    std::string TwoPrimitiveDocument()
    {
        // 6 positions: triangle A at the origin, triangle B offset along +X.
        std::vector<std::uint8_t> buffer;
        const float positions[18] = {
            0.0f, 0.0f, 0.0f,  1.0f, 0.0f, 0.0f,  0.0f, 1.0f, 0.0f,
            5.0f, 0.0f, 0.0f,  6.0f, 0.0f, 0.0f,  5.0f, 1.0f, 0.0f,
        };
        for (const float value : positions)
        {
            std::uint8_t bytes[4];
            std::memcpy(bytes, &value, 4);
            buffer.insert(buffer.end(), bytes, bytes + 4);
        }
        static const char* kAlphabet =
            "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
        std::string base64;
        for (std::size_t i = 0; i < buffer.size(); i += 3)
        {
            const std::uint32_t chunk =
                (static_cast<std::uint32_t>(buffer[i]) << 16) |
                (i + 1 < buffer.size() ? static_cast<std::uint32_t>(buffer[i + 1]) << 8 : 0u) |
                (i + 2 < buffer.size() ? static_cast<std::uint32_t>(buffer[i + 2]) : 0u);
            base64 += kAlphabet[(chunk >> 18) & 0x3F];
            base64 += kAlphabet[(chunk >> 12) & 0x3F];
            base64 += (i + 1 < buffer.size()) ? kAlphabet[(chunk >> 6) & 0x3F] : '=';
            base64 += (i + 2 < buffer.size()) ? kAlphabet[chunk & 0x3F] : '=';
        }

        return std::string(R"GLTF({
  "asset": { "version": "2.0" },
  "scene": 0,
  "scenes": [ { "nodes": [0] } ],
  "nodes": [ { "name": "TwoMaterialNode", "mesh": 0 } ],
  "materials": [
    { "name": "Red",  "pbrMetallicRoughness": { "baseColorFactor": [1, 0, 0, 1] } },
    { "name": "Blue", "pbrMetallicRoughness": { "baseColorFactor": [0, 0, 1, 1] } }
  ],
  "meshes": [ { "name": "TwoMaterialMesh", "primitives": [
    { "attributes": { "POSITION": 0 }, "material": 0 },
    { "attributes": { "POSITION": 1 }, "material": 1 }
  ] } ],
  "buffers": [ { "byteLength": 72, "uri": "data:application/octet-stream;base64,)GLTF") +
               base64 + R"GLTF(" } ],
  "bufferViews": [
    { "buffer": 0, "byteOffset": 0,  "byteLength": 36 },
    { "buffer": 0, "byteOffset": 36, "byteLength": 36 }
  ],
  "accessors": [
    { "bufferView": 0, "componentType": 5126, "count": 3, "type": "VEC3",
      "min": [0, 0, 0], "max": [1, 1, 0] },
    { "bufferView": 1, "componentType": 5126, "count": 3, "type": "VEC3",
      "min": [5, 0, 0], "max": [6, 1, 0] }
  ]
})GLTF";
    }
}

// --- GLTF-139: one ModelMesh per mesh, one ModelMeshPart per primitive ---------------------------

TEST(GltfModelShape, AMultiPrimitiveMeshBecomesOneModelMeshWithOnePartPerPrimitive)
{
    // XNA's own shape, and the reason it matters beyond tidiness: a ModelMesh owns the ParentBone
    // and the BoundingSphere its parts share. Splitting one glTF mesh across two ModelMeshes gives
    // each primitive its own of each, so a caller that culls by mesh bounds, or looks up a bone by
    // mesh, sees two objects where the file describes one.
    const ScratchDir dir;
    GraphicsDevice gd;
    ContentManager cm(nullptr, dir.path().string());
    cm.setGraphicsDevice(gd);
    Model model = LoadModel(dir, TwoPrimitiveDocument(), gd, cm);

    ASSERT_EQ(1, model.getMeshesProperty().getCountProperty())
        << "two primitives of one mesh became two ModelMeshes";
    ModelMesh* mesh = model.getMeshesProperty()[0];
    EXPECT_EQ(2, mesh->getMeshPartsProperty().getCountProperty())
        << "the second primitive was dropped rather than becoming a second part";

    // Both parts share the one mesh's bone -- the property that is lost by splitting.
    ASSERT_NE(nullptr, mesh->getParentBoneProperty());
    EXPECT_EQ("TwoMaterialNode", mesh->getParentBoneProperty()->getNameProperty());

    // And each part kept its own effect, so the two materials did not collapse into one.
    ASSERT_NE(nullptr, mesh->getMeshPartsProperty()[0]->getEffectProperty());
    ASSERT_NE(nullptr, mesh->getMeshPartsProperty()[1]->getEffectProperty());
    EXPECT_NE(mesh->getMeshPartsProperty()[0]->getEffectProperty(),
              mesh->getMeshPartsProperty()[1]->getEffectProperty())
        << "both parts share one effect; the second material was lost";

    // The mesh's Effects collection is what Model::Draw binds matrices through. A part whose
    // effect never reached it draws with whatever matrices the effect happened to hold.
    EXPECT_EQ(2, mesh->getEffectsProperty().getCountProperty())
        << "an effect never registered on its owning mesh; Model::Draw would bind it nothing";
}

// --- GLTF-140: deterministic ordering ------------------------------------------------------------

TEST(GltfModelShape, MeshAndPartOrderIsTheFilesOwnAndIsStableAcrossLoads)
{
    // Required by every golden comparison downstream. The ordering CollectMeshGroups documents is
    // the glTF node array's; what must never happen is an order that depends on a hash of a
    // pointer, which is stable within a run and different in the next one -- the failure mode that
    // makes a golden test flap without any change to the code.
    const ScratchDir dir;
    GraphicsDevice gd;
    ContentManager cm(nullptr, dir.path().string());
    cm.setGraphicsDevice(gd);
    const std::string json = TwoPrimitiveDocument();

    std::vector<std::string> firstNames;
    {
        Model model = LoadModel(dir, json, gd, cm);
        for (int i = 0; i < model.getMeshesProperty().getCountProperty(); ++i)
        {
            firstNames.push_back(model.getMeshesProperty()[i]->getNameProperty());
        }
    }

    ContentManager second(nullptr, dir.path().string());
    second.setGraphicsDevice(gd);
    Model reloaded = second.Load<Model>("shape");
    ASSERT_EQ(static_cast<int>(firstNames.size()), reloaded.getMeshesProperty().getCountProperty());
    for (std::size_t i = 0; i < firstNames.size(); ++i)
    {
        EXPECT_EQ(firstNames[i], reloaded.getMeshesProperty()[static_cast<int>(i)]->getNameProperty())
            << "mesh " << i << " changed identity between two loads of the same file";
    }
}

// --- GLTF-141: names trace back to the file -------------------------------------------------------

TEST(GltfModelShape, AMeshIsNamedAfterTheGltfMeshItInstances)
{
    const ScratchDir dir;
    GraphicsDevice gd;
    ContentManager cm(nullptr, dir.path().string());
    cm.setGraphicsDevice(gd);
    Model model = LoadModel(dir, TwoPrimitiveDocument(), gd, cm);

    ASSERT_EQ(1, model.getMeshesProperty().getCountProperty());
    EXPECT_EQ("TwoMaterialMesh", model.getMeshesProperty()[0]->getNameProperty())
        << "a generated name cannot be traced back to anything in the file";
}

TEST(GltfModelShape, TwoPlacementsOfOneMeshAreNamedApartByTheirNodes)
{
    // One mesh, two nodes -- the instancing shape. Both ModelMeshes are the same geometry, so a
    // name taken from the mesh alone gives two identically named meshes and `Model.Meshes["Tri"]`
    // becomes a coin flip. The node is what distinguishes them, so the node is what the name
    // carries.
    const LoadedFixture fixture("xf-shared-mesh");
    ASSERT_TRUE(fixture.Ok()) << fixture.Error();

    GraphicsDevice gd;
    ContentManager cm(nullptr, CorpusDirectory().string());
    cm.setGraphicsDevice(gd);
    Model model = cm.Load<Model>("xf-shared-mesh");

    ASSERT_EQ(2, model.getMeshesProperty().getCountProperty());
    const std::string first = model.getMeshesProperty()[0]->getNameProperty();
    const std::string second = model.getMeshesProperty()[1]->getNameProperty();
    EXPECT_NE(first, second) << "two placements of one mesh share a name";
    EXPECT_NE(std::string::npos, first.find("SharedTri")) << first;
    EXPECT_NE(std::string::npos, second.find("SharedTri")) << second;
    EXPECT_NE(std::string::npos, second.find("InstanceTranslatedX10")) << second;
}

// --- GLTF-142: empty meshes and empty primitives --------------------------------------------------

TEST(GltfModelShape, AFileWhoseOnlyMeshHasNoPrimitivesIsRefusedByName)
{
    // §3.7.1 requires at least one primitive, so this is malformed -- but cgltf's own validation
    // stops at "primitives_count == 0 is invalid" only for a mesh it reaches. What matters here is
    // that the outcome is a named error rather than an empty Model that draws nothing and reports
    // nothing, which is indistinguishable from a camera pointing the wrong way.
    const ScratchDir dir;
    GraphicsDevice gd;
    ContentManager cm(nullptr, dir.path().string());
    cm.setGraphicsDevice(gd);

    const std::string json = R"GLTF({
  "asset": { "version": "2.0" },
  "scene": 0,
  "scenes": [ { "nodes": [0] } ],
  "nodes": [ { "name": "EmptyMeshNode", "mesh": 0 } ],
  "meshes": [ { "name": "Empty", "primitives": [] } ]
})GLTF";
    std::ofstream(dir.path() / "shape.gltf", std::ios::binary) << json;

    std::string message;
    try
    {
        Model model = cm.Load<Model>("shape");
    }
    catch (const std::exception& e)
    {
        message = e.what();
    }
    EXPECT_FALSE(message.empty())
        << "a file with no importable primitive produced a Model that silently draws nothing";
}

// --- GLTF-144: camera and light nodes are not mesh instances -------------------------------------

TEST(GltfModelShape, ACameraOrLightNodeNeverBecomesAMeshInstance)
{
    // A node carries at most one of mesh/camera/light in ordinary files, and the placement walk
    // keys on `node.mesh` -- but it is a scene walk, and a walk that counted NODES instead would
    // produce a placement for the camera too. That placement has no mesh, so it would either
    // crash on a null dereference or draw whatever mesh index 0 happens to be.
    for (const char* id : {"camera-perspective", "camera-orthographic", "camera-animated-node"})
    {
        SCOPED_TRACE(id);
        const LoadedFixture fixture(id);
        ASSERT_TRUE(fixture.Ok()) << fixture.Error();

        const SceneGraphOut scene = BuildSceneGraph(&fixture.Data());
        std::size_t nonMeshNodes = 0;
        for (const SceneNodeOut& node : scene.nodes)
        {
            if (node.node == nullptr) { continue; }
            if (node.node->camera != nullptr || node.node->light != nullptr) { ++nonMeshNodes; }
        }
        EXPECT_GT(nonMeshNodes, 0u) << "the fixture no longer has a camera or light node at all";

        for (const MeshGroup& group : CollectMeshGroups(&fixture.Data(), scene))
        {
            for (const MeshInstanceOut& instance : group.instances)
            {
                ASSERT_NE(nullptr, instance.mesh) << "a placement with no mesh was created";
                ASSERT_NE(nullptr, instance.node);
                EXPECT_NE(nullptr, instance.node->mesh)
                    << "node '"
                    << (instance.node->name != nullptr ? instance.node->name : "<unnamed>")
                    << "' instances no mesh, yet a placement was created for it";
            }
        }
    }
}

// --- GLTF-134: a file with no scenes at all -------------------------------------------------------

TEST(GltfModelShape, AFileWithNoScenesFallsBackToEveryMeshInIt)
{
    // §3.9 makes `scenes` optional: a file may be a library of meshes with no scene at all, which
    // is what an exporter writes when asked for geometry rather than a scene. Refusing it, or
    // importing nothing from it, are both worse than the documented fallback -- and the fallback
    // has to place the meshes at the identity, because there is no node to place them by.
    Parsed doc;
    ASSERT_TRUE(Parse(doc, R"GLTF({
  "asset": { "version": "2.0" },
  "meshes": [
    { "name": "A", "primitives": [ { "attributes": { "POSITION": 0 } } ] },
    { "name": "B", "primitives": [ { "attributes": { "POSITION": 0 } } ] }
  ],
  "buffers": [ { "byteLength": 36,
    "uri": "data:application/octet-stream;base64,AAAAAAAAAAAAAAAAAACAPwAAAAAAAAAAAAAAAAAAgD8AAAAA" } ],
  "bufferViews": [ { "buffer": 0, "byteOffset": 0, "byteLength": 36 } ],
  "accessors": [ { "bufferView": 0, "componentType": 5126, "count": 3, "type": "VEC3",
                   "min": [0, 0, 0], "max": [1, 1, 0] } ]
})GLTF"));

    const SceneGraphOut scene = BuildSceneGraph(doc.data);
    EXPECT_EQ(1u, scene.nodes.size()) << "with no scenes there is only the synthetic root";

    const std::vector<MeshGroup> groups = CollectMeshGroups(doc.data, scene);
    ASSERT_EQ(1u, groups.size());
    ASSERT_EQ(2u, groups[0].instances.size()) << "both meshes must be imported, not neither";
    for (const MeshInstanceOut& instance : groups[0].instances)
    {
        EXPECT_EQ(nullptr, instance.node) << "there is no node to attribute this placement to";
        EXPECT_EQ(0, instance.sceneNodeIndex) << "the fallback places every mesh at the root";
    }
    EXPECT_EQ("A", std::string(groups[0].instances[0].mesh->name));
    EXPECT_EQ("B", std::string(groups[0].instances[1].mesh->name));
}

// --- GLTF-238: one Effect per material, not per primitive ------------------------------------------

TEST(GltfModelShape, TwoPrimitivesOfOneMaterialShareASingleEffect)
{
    // A file states a material once and may use it on any number of primitives. Giving each its
    // own Effect copies every factor and every texture binding per primitive, and costs a
    // redundant state change per draw for a material the GPU is already set up for -- XNA's own
    // content pipeline shares them, and so does this now.
    //
    // The document below is the two-primitive mesh with BOTH primitives pointing at material 0.
    const ScratchDir dir;
    GraphicsDevice gd;
    ContentManager cm(nullptr, dir.path().string());
    cm.setGraphicsDevice(gd);

    std::string json = TwoPrimitiveDocument();
    const std::string second = R"("attributes": { "POSITION": 1 }, "material": 1)";
    const std::size_t at = json.find(second);
    ASSERT_NE(std::string::npos, at) << "the shared fixture changed shape";
    json.replace(at, second.size(), R"("attributes": { "POSITION": 1 }, "material": 0)");

    Model model = LoadModel(dir, json, gd, cm);
    ASSERT_EQ(1, model.getMeshesProperty().getCountProperty());
    ModelMesh* mesh = model.getMeshesProperty()[0];
    ASSERT_EQ(2, mesh->getMeshPartsProperty().getCountProperty());

    EXPECT_EQ(mesh->getMeshPartsProperty()[0]->getEffectProperty(),
              mesh->getMeshPartsProperty()[1]->getEffectProperty())
        << "one material produced two effects";
    EXPECT_EQ(1, mesh->getEffectsProperty().getCountProperty())
        << "ModelMesh::Effects counts the shared effect twice";
}

TEST(GltfModelShape, TwoMaterialsStillGetTwoEffects)
{
    // The control that keeps the sharing honest: a cache keyed too coarsely -- on the mesh, say,
    // or on the effect class -- would collapse these two and give the second primitive the first
    // material's colour. That is a rendering bug with no error anywhere.
    const ScratchDir dir;
    GraphicsDevice gd;
    ContentManager cm(nullptr, dir.path().string());
    cm.setGraphicsDevice(gd);
    Model model = LoadModel(dir, TwoPrimitiveDocument(), gd, cm);

    ModelMesh* mesh = model.getMeshesProperty()[0];
    ASSERT_EQ(2, mesh->getMeshPartsProperty().getCountProperty());
    EXPECT_NE(mesh->getMeshPartsProperty()[0]->getEffectProperty(),
              mesh->getMeshPartsProperty()[1]->getEffectProperty());
    EXPECT_EQ(2, mesh->getEffectsProperty().getCountProperty());
}
