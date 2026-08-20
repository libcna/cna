// SPDX-License-Identifier: MS-PL
//
// plans/plan_gltf.md GLTF-123 / GLTF-133 / GLTF-135 / GLTF-143: which nodes a file's default scene
// actually contains.
//
// §3.5 makes `nodes` a flat pool and `scenes` the thing that selects from it, so "in the file" and
// "in the scene" are different questions -- and every failure mode here is a *quiet* one. A node
// reachable from two scenes imported twice would silently double every mesh under it. A file whose
// `scene` is 1 imported from scene 0 would load entirely the wrong content while looking perfectly
// healthy. An orphan node imported anyway drags geometry into the model that the author removed
// from the scene on purpose.
//
// These use scratch documents rather than corpus assets because each one is a *graph shape* -- two
// scenes sharing a node, a decoy scene, an orphan -- and needs no geometry beyond a placeholder to
// make its point. `scene-default-selection` covers the corpus side of GLTF-133 already.

#include <algorithm>
#include <cstdint>
#include <gtest/gtest.h>
#include <string>
#include <vector>

#include "CNA/Internal/GltfImport/GltfImportCore.hpp"

#include "GltfOracleEXT.hpp"

using namespace CNA::Internal::GltfImport;

namespace
{
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

    /// One triangle's worth of buffer, bufferViews and accessors -- enough for a structurally valid
    /// mesh, shared by every document below so the graph shape is the only thing that varies.
    const char* kMeshTail = R"GLTF(
  "meshes": [ { "primitives": [ { "attributes": { "POSITION": 0 }, "indices": 1, "mode": 4 } ] } ],
  "buffers": [ { "byteLength": 44, "uri": "data:application/octet-stream;base64,AAAAAAAAAAAAAAAAAACAPwAAAAAAAAAAAAAAAAAAgD8AAAAAAAABAAIAAAA=" } ],
  "bufferViews": [
    { "buffer": 0, "byteOffset": 0,  "byteLength": 36 },
    { "buffer": 0, "byteOffset": 36, "byteLength": 6 }
  ],
  "accessors": [
    { "bufferView": 0, "componentType": 5126, "count": 3, "type": "VEC3", "min": [0,0,0], "max": [1,1,0] },
    { "bufferView": 1, "componentType": 5123, "count": 3, "type": "SCALAR" }
  ]
)GLTF";

    /// How many times a node name appears in the built graph. The whole point of GLTF-123 is that
    /// this is 1 for a node two scenes both reach, not 2.
    std::size_t CountNodesNamed(const SceneGraphOut& scene, const std::string& name)
    {
        return static_cast<std::size_t>(
            std::count_if(scene.nodes.begin(), scene.nodes.end(),
                          [&](const SceneNodeOut& node) { return node.name == name; }));
    }

    bool HasNodeNamed(const SceneGraphOut& scene, const std::string& name)
    {
        return CountNodesNamed(scene, name) > 0;
    }
}

// --- GLTF-123: a node two scenes both reach ----------------------------------------------------

TEST(GltfSceneSelection, ANodeReachableFromTwoScenesIsImportedExactlyOnce)
{
    // §3.5.3 makes the node hierarchy a disjoint union of strict trees, so a node cannot have two
    // PARENTS -- cgltf rejects that outright and it is not the shape this row is about. What a file
    // legitimately can do is name the same node twice among a scene's roots, and list it as a root
    // of more than one scene. Both are de-duplicated; without that, every mesh under the node is
    // imported twice, which draws geometry on top of itself and reads as a lighting or z-fighting
    // artefact rather than as a graph bug.
    Parsed parsed;
    ASSERT_TRUE(Parse(parsed, std::string(R"GLTF({
  "asset": { "version": "2.0" },
  "scene": 0,
  "scenes": [ { "name": "Main", "nodes": [0, 0] }, { "name": "Alt", "nodes": [0] } ],
  "nodes": [ { "name": "Shared", "mesh": 0 } ],)GLTF") + kMeshTail + "}"));

    const SceneGraphOut scene = BuildSceneGraph(parsed.data);
    EXPECT_EQ(1u, CountNodesNamed(scene, "Shared"))
        << "a node named twice among a scene's roots was flattened twice, so its mesh imports twice";

    const std::vector<CnaTest::GltfOracle::ExtractedPrimitive> primitives =
        CnaTest::GltfOracle::ExtractSceneMeshesEXT(*parsed.data);
    EXPECT_EQ(1u, primitives.size()) << "the shared node's mesh was extracted once per reference";
}

// --- GLTF-133: the default scene is the one `scene` names --------------------------------------

TEST(GltfSceneSelection, TheSceneIndexIsHonouredAndTheOtherScenesContentIsExcluded)
{
    // `scene: 1`, with scene 0 holding a decoy. Defaulting to scene 0 -- the obvious shortcut --
    // loads entirely the wrong content while the file looks perfectly healthy, which is why the
    // decoy carries its own mesh rather than being an empty node.
    Parsed parsed;
    ASSERT_TRUE(Parse(parsed, std::string(R"GLTF({
  "asset": { "version": "2.0" },
  "scene": 1,
  "scenes": [ { "name": "Decoy", "nodes": [0] }, { "name": "Real", "nodes": [1] } ],
  "nodes": [
    { "name": "DecoyNode", "mesh": 0 },
    { "name": "RealNode", "mesh": 0 }
  ],)GLTF") + kMeshTail + "}"));

    const SceneGraphOut scene = BuildSceneGraph(parsed.data);
    EXPECT_TRUE(HasNodeNamed(scene, "RealNode"));
    EXPECT_FALSE(HasNodeNamed(scene, "DecoyNode"))
        << "scene 0's content was imported although the file names scene 1 as its default";

    // And the same selection through the mesh-extraction path, because a graph that excludes a node
    // is only useful if the geometry follows it.
    const std::vector<CnaTest::GltfOracle::ExtractedPrimitive> primitives =
        CnaTest::GltfOracle::ExtractSceneMeshesEXT(*parsed.data);
    EXPECT_EQ(1u, primitives.size()) << "the decoy's mesh was extracted too";
}

// --- GLTF-135: a node in the pool but in no scene ----------------------------------------------

TEST(GltfSceneSelection, ANodeInNoSceneAtAllIsExcluded)
{
    // §3.5: `nodes` is a flat pool and `scenes` selects from it. An exporter routinely leaves
    // helpers, proxies and hidden geometry in the pool without putting them in a scene -- importing
    // those drags content into the model that the author deliberately took out of it.
    Parsed parsed;
    ASSERT_TRUE(Parse(parsed, std::string(R"GLTF({
  "asset": { "version": "2.0" },
  "scene": 0,
  "scenes": [ { "nodes": [0] } ],
  "nodes": [
    { "name": "InScene", "mesh": 0 },
    { "name": "Orphan", "mesh": 0 }
  ],)GLTF") + kMeshTail + "}"));

    ASSERT_EQ(2u, static_cast<std::size_t>(parsed.data->nodes_count))
        << "the orphan must exist in the pool, or the test asserts nothing";

    const SceneGraphOut scene = BuildSceneGraph(parsed.data);
    EXPECT_TRUE(HasNodeNamed(scene, "InScene"));
    EXPECT_FALSE(HasNodeNamed(scene, "Orphan"))
        << "a node belonging to no scene was imported anyway";

    const std::vector<CnaTest::GltfOracle::ExtractedPrimitive> primitives =
        CnaTest::GltfOracle::ExtractSceneMeshesEXT(*parsed.data);
    EXPECT_EQ(1u, primitives.size()) << "the orphan's mesh was extracted";
}

// --- GLTF-143: meshes with no scene node referencing them --------------------------------------

TEST(GltfSceneSelection, AFileWithNoScenesAtAllStillImportsItsMeshes)
{
    // §3.5 permits a file with no `scenes` array at all -- it is then "a library of assets", not a
    // renderable scene. Importing nothing would be defensible reading of the letter and useless in
    // practice: this is the shape a mesh library or a single-object export takes, and a loader that
    // returned an empty model for it would look broken rather than pedantic.
    Parsed parsed;
    ASSERT_TRUE(Parse(parsed, std::string(R"GLTF({
  "asset": { "version": "2.0" },
  "nodes": [ { "name": "LooseNode", "mesh": 0 } ],)GLTF") + kMeshTail + "}"));

    ASSERT_EQ(0u, static_cast<std::size_t>(parsed.data->scenes_count));

    const std::vector<CnaTest::GltfOracle::ExtractedPrimitive> primitives =
        CnaTest::GltfOracle::ExtractSceneMeshesEXT(*parsed.data);
    EXPECT_EQ(1u, primitives.size())
        << "a file with no scenes imported nothing; the fallback is what makes a mesh library "
           "loadable at all";
    if (!primitives.empty()) { EXPECT_TRUE(primitives[0].extracted) << primitives[0].error; }
}

TEST(GltfSceneSelection, ExclusionIsBySceneReachabilityNotByMeshUsage)
{
    // The distinction that makes GLTF-135 and GLTF-143 consistent rather than contradictory: what
    // is excluded is a node *no scene reaches*, not a mesh *no node uses*. Here one mesh is
    // referenced only by an orphan node, and the file DOES have scenes -- so the fallback of the
    // previous test must not fire and rescue it.
    Parsed parsed;
    ASSERT_TRUE(Parse(parsed, std::string(R"GLTF({
  "asset": { "version": "2.0" },
  "scene": 0,
  "scenes": [ { "nodes": [0] } ],
  "nodes": [
    { "name": "InScene", "mesh": 0 },
    { "name": "Orphan", "mesh": 0 }
  ],)GLTF") + kMeshTail + "}"));

    const std::vector<CnaTest::GltfOracle::ExtractedPrimitive> primitives =
        CnaTest::GltfOracle::ExtractSceneMeshesEXT(*parsed.data);
    EXPECT_EQ(1u, primitives.size())
        << "the no-scenes fallback fired for a file that does have a scene, so an excluded node's "
           "mesh came back in through the other door";
}
