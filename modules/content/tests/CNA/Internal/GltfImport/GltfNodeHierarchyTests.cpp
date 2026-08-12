// SPDX-License-Identifier: MS-PL
//
// plan_gltf.md GLTF-111 / GLTF-112 / GLTF-120 / GLTF-122 / GLTF-124 / GLTF-125 / GLTF-126.
//
// The node hierarchy is where a transform bug becomes a *placement* bug: nothing crashes, nothing
// is rejected, the model just stands somewhere else. Each row here pins one property of the walk
// that no other test would notice losing --
//
//   * an omitted TRS component is a specific value, not "whatever was left in the struct";
//   * a chain of transforms composes ONCE per node (applying a parent twice is the classic bug,
//     and its symptom -- geometry that drifts further the deeper it sits -- looks like bad art);
//   * a node with no mesh still places its children;
//   * every root of a multi-root scene arrives, not just the first;
//   * a malformed cyclic hierarchy terminates instead of hanging a game at load;
//   * a pathologically deep chain does not blow the stack;
//   * unnamed nodes get stable, distinct names, because the bone index space is addressed by name
//     in `.cnj` and by index everywhere else.

#include <cstdint>
#include <cstring>
#include <gtest/gtest.h>
#include <set>
#include <string>
#include <vector>

#include "CNA/Internal/GltfImport/GltfImportCore.hpp"

using namespace CNA::Internal::GltfImport;
using namespace Microsoft::Xna::Framework;

namespace
{
    constexpr float kTolerance = 1e-4f;

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

    std::string Base64(const std::vector<std::uint8_t>& bytes)
    {
        static const char* kAlphabet =
            "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
        std::string out;
        for (std::size_t i = 0; i < bytes.size(); i += 3)
        {
            const std::uint32_t chunk =
                (static_cast<std::uint32_t>(bytes[i]) << 16) |
                (i + 1 < bytes.size() ? static_cast<std::uint32_t>(bytes[i + 1]) << 8 : 0u) |
                (i + 2 < bytes.size() ? static_cast<std::uint32_t>(bytes[i + 2]) : 0u);
            out += kAlphabet[(chunk >> 18) & 0x3F];
            out += kAlphabet[(chunk >> 12) & 0x3F];
            out += (i + 1 < bytes.size()) ? kAlphabet[(chunk >> 6) & 0x3F] : '=';
            out += (i + 2 < bytes.size()) ? kAlphabet[chunk & 0x3F] : '=';
        }
        return out;
    }

    /// One triangle at the origin, as the buffer/bufferView/accessor trio every document below
    /// shares. Kept as a helper rather than pasted so a fixture's node list is the only thing that
    /// differs between these tests -- which is the variable each one is actually about.
    std::string TriangleTail()
    {
        std::vector<std::uint8_t> buffer;
        const float positions[9] = {0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f};
        for (const float value : positions)
        {
            std::uint8_t bytes[4];
            std::memcpy(bytes, &value, 4);
            buffer.insert(buffer.end(), bytes, bytes + 4);
        }
        return std::string(R"GLTF(,
  "meshes": [ { "primitives": [ { "attributes": { "POSITION": 0 } } ] } ],
  "buffers": [ { "byteLength": 36, "uri": "data:application/octet-stream;base64,)GLTF") +
               Base64(buffer) + R"GLTF(" } ],
  "bufferViews": [ { "buffer": 0, "byteOffset": 0, "byteLength": 36 } ],
  "accessors": [ { "bufferView": 0, "componentType": 5126, "count": 3, "type": "VEC3",
                   "min": [0, 0, 0], "max": [1, 1, 0] } ]
})GLTF";
    }

    Vector3 WorldOrigin(const SceneGraphOut& scene, std::size_t nodeIndex)
    {
        return Vector3::Transform(Vector3(0.0f, 0.0f, 0.0f), scene.nodes[nodeIndex].worldTransform);
    }
}

// --- GLTF-111: the defaults of an omitted TRS component ------------------------------------------

TEST(GltfNodeHierarchy, AnOmittedTrsComponentTakesItsSpecifiedDefaultRatherThanZero)
{
    // §3.5: `translation` defaults to (0,0,0), `rotation` to the identity quaternion (0,0,0,1) and
    // `scale` to (1,1,1). Only the scale default is dangerous to get wrong, and it is dangerous in
    // the most visible way possible: a zero-initialised scale collapses the node's whole subtree to
    // a point. A node declaring only a translation is the ordinary authoring shape, so this is not
    // a corner case -- it is most files.
    Parsed doc;
    ASSERT_TRUE(Parse(doc, R"GLTF({
  "asset": { "version": "2.0" },
  "scene": 0,
  "scenes": [ { "nodes": [0, 1] } ],
  "nodes": [
    { "name": "Bare" },
    { "name": "TranslationOnly", "translation": [3, 4, 5] }
  ]
})GLTF"));

    const SceneGraphOut scene = BuildSceneGraph(doc.data);
    ASSERT_EQ(3u, scene.nodes.size());

    // A node declaring nothing at all is the identity, component by component -- not merely
    // "something that leaves the origin at the origin", which a zero scale also does.
    const Matrix& bare = scene.nodes[1].localTransform;
    const Matrix expected = Matrix::getIdentityProperty();
    const float* actual = &bare.M11;
    const float* identity = &expected.M11;
    for (int i = 0; i < 16; ++i)
    {
        EXPECT_NEAR(identity[i], actual[i], kTolerance) << "component " << i;
    }

    // And a node declaring only a translation keeps unit scale: a point one unit out along each
    // axis must land one unit out from the node's own position.
    const Vector3 offset =
        Vector3::Transform(Vector3(1.0f, 1.0f, 1.0f), scene.nodes[2].localTransform);
    EXPECT_NEAR(4.0f, offset.X, kTolerance) << "the omitted scale did not default to 1";
    EXPECT_NEAR(5.0f, offset.Y, kTolerance);
    EXPECT_NEAR(6.0f, offset.Z, kTolerance);
}

// --- GLTF-112: composition happens exactly once per node -----------------------------------------

TEST(GltfNodeHierarchy, AChainOfTranslationsComposesOncePerNodeAndNotTwice)
{
    // A chain of +1, +2, +3 gives 6 at the deepest node. The two wrong answers are both plausible
    // implementations: 3 if a node's world transform is really just its local one (the parent never
    // applied), and 11 if each level re-applies the accumulated parent on top of an already-composed
    // child (1, then 1+2+... compounding). Only 6 is composition applied exactly once.
    Parsed doc;
    ASSERT_TRUE(Parse(doc, R"GLTF({
  "asset": { "version": "2.0" },
  "scene": 0,
  "scenes": [ { "nodes": [0] } ],
  "nodes": [
    { "name": "A", "translation": [1, 0, 0], "children": [1] },
    { "name": "B", "translation": [2, 0, 0], "children": [2] },
    { "name": "C", "translation": [3, 0, 0] }
  ]
})GLTF"));

    const SceneGraphOut scene = BuildSceneGraph(doc.data);
    ASSERT_EQ(4u, scene.nodes.size());
    EXPECT_NEAR(1.0f, WorldOrigin(scene, 1).X, kTolerance);
    EXPECT_NEAR(3.0f, WorldOrigin(scene, 2).X, kTolerance);
    EXPECT_NEAR(6.0f, WorldOrigin(scene, 3).X, kTolerance)
        << "3 means the parent chain was never applied; 11 means it was applied more than once";

    // The local transforms are untouched by composition -- the composed value lives beside the
    // authored one rather than overwriting it, which is what lets `AnimationPlayer` recompose from
    // locals without first having to undo a bake.
    EXPECT_NEAR(3.0f, Vector3::Transform(Vector3::Zero,
                                          scene.nodes[3].localTransform).X, kTolerance);
}

// --- GLTF-120: a node with no mesh still places its children -------------------------------------

TEST(GltfNodeHierarchy, ATransformOnlyNodePlacesItsMeshBearingChild)
{
    // Empties are how every DCC tool expresses a pivot, so a hierarchy whose interior nodes carry
    // no mesh is the common case rather than an oddity. An importer that walked `data->meshes` (or
    // skipped mesh-less nodes while walking the scene) would place this triangle at the origin --
    // a model that renders, in the wrong spot, with no diagnostic anywhere.
    Parsed doc;
    ASSERT_TRUE(Parse(doc, std::string(R"GLTF({
  "asset": { "version": "2.0" },
  "scene": 0,
  "scenes": [ { "nodes": [0] } ],
  "nodes": [
    { "name": "Pivot", "translation": [0, 7, 0], "children": [1] },
    { "name": "Geometry", "mesh": 0, "translation": [0, 0, 2] }
  ])GLTF") + TriangleTail()));

    const SceneGraphOut scene = BuildSceneGraph(doc.data);
    ASSERT_EQ(3u, scene.nodes.size());
    EXPECT_EQ(nullptr, scene.nodes[1].node->mesh) << "the fixture's pivot must carry no mesh";

    const std::vector<MeshGroup> groups = CollectMeshGroups(doc.data, scene);
    ASSERT_EQ(1u, groups.size());
    ASSERT_EQ(1u, groups[0].instances.size());
    const Vector3 placed =
        Vector3::Transform(Vector3::Zero, groups[0].instances[0].worldTransform);
    EXPECT_NEAR(0.0f, placed.X, kTolerance);
    EXPECT_NEAR(7.0f, placed.Y, kTolerance) << "the mesh-less parent's transform was dropped";
    EXPECT_NEAR(2.0f, placed.Z, kTolerance);
}

// --- GLTF-122: every root of a multi-root scene ---------------------------------------------------

TEST(GltfNodeHierarchy, EveryRootOfAMultiRootSceneIsImportedAndPlaced)
{
    // glTF scenes have a list of roots; CNA's `Model` has one. The mapping invents a synthetic
    // identity parent, and the failure it has to be tested against is the shortcut of treating
    // `scene.nodes[0]` as *the* root -- which imports the first third of this file and silently
    // discards the rest.
    Parsed doc;
    ASSERT_TRUE(Parse(doc, std::string(R"GLTF({
  "asset": { "version": "2.0" },
  "scene": 0,
  "scenes": [ { "nodes": [0, 1, 2] } ],
  "nodes": [
    { "name": "Left",   "mesh": 0, "translation": [-5, 0, 0] },
    { "name": "Middle", "mesh": 0, "translation": [0, 0, 0] },
    { "name": "Right",  "mesh": 0, "translation": [5, 0, 0] }
  ])GLTF") + TriangleTail()));

    const SceneGraphOut scene = BuildSceneGraph(doc.data);
    ASSERT_EQ(4u, scene.nodes.size()) << "a root went missing (or the synthetic root did not)";
    for (std::size_t i = 1; i < scene.nodes.size(); ++i)
    {
        EXPECT_EQ(0, scene.nodes[i].parentIndex) << "every scene root parents to the synthetic root";
    }

    // The order is the file's own, and the placements are the three distinct authored ones -- the
    // assertion that fails if the roots are imported but collapsed onto one transform.
    EXPECT_NEAR(-5.0f, WorldOrigin(scene, 1).X, kTolerance);
    EXPECT_NEAR(0.0f, WorldOrigin(scene, 2).X, kTolerance);
    EXPECT_NEAR(5.0f, WorldOrigin(scene, 3).X, kTolerance);

    // One mesh, three placements: the instancing shape GLTF-113 introduced, reached here through
    // the ordinary multi-root path rather than an explicitly shared-mesh fixture.
    const std::vector<MeshGroup> groups = CollectMeshGroups(doc.data, scene);
    ASSERT_EQ(1u, groups.size());
    EXPECT_EQ(3u, groups[0].instances.size()) << "one mesh instanced three times, three instances";
}

// --- GLTF-124: a cyclic hierarchy terminates ------------------------------------------------------

TEST(GltfNodeHierarchy, ACycleReachableFromASceneRootIsRefusedBeforeAnyTraversalRunsAtAll)
{
    // Two nodes each claiming the other as a child. §3.5.3 makes the hierarchy a disjoint union of
    // trees, but "forbidden" is not "impossible" -- a broken exporter can write this, and a naive
    // traversal spins forever on it. A game hanging at load with no error is worse than a rejected
    // file.
    //
    // A cycle that a scene root can reach necessarily gives that root a parent, and cgltf refuses
    // it while it is still resolving indices to pointers -- so this shape never reaches CNA at all.
    Parsed doc;
    EXPECT_FALSE(Parse(doc, R"GLTF({
  "asset": { "version": "2.0" },
  "scene": 0,
  "scenes": [ { "nodes": [0] } ],
  "nodes": [
    { "name": "A", "children": [1] },
    { "name": "B", "children": [0] }
  ]
})GLTF")) << "a scene root inside a cycle was accepted";
}

TEST(GltfNodeHierarchy, ACycleOutsideEverySceneParsesButIsRejectedAndNeverWalked)
{
    // The cycle that *does* get past the parser: one among nodes no scene lists. Every node in it
    // has exactly one parent and no scene root has any, so nothing the pointer fixup checks is
    // violated -- the file is structurally consistent and semantically impossible.
    //
    // Two independent things have to hold. `ValidateGltfEXT` must reject it, because a file this
    // malformed should not load. And the traversal must be safe regardless of validation order:
    // reaching the end of this test at all proves it terminated.
    Parsed doc;
    ASSERT_TRUE(Parse(doc, R"GLTF({
  "asset": { "version": "2.0" },
  "scene": 0,
  "scenes": [ { "nodes": [0] } ],
  "nodes": [
    { "name": "Reachable", "translation": [1, 0, 0] },
    { "name": "A", "children": [2] },
    { "name": "B", "children": [1] }
  ]
})GLTF"));

    const SceneGraphOut scene = BuildSceneGraph(doc.data);
    // Only the scene's own node is imported: an orphaned cycle is not reachable content, and
    // pulling it in "because it exists in the file" would draw geometry no scene asked for.
    ASSERT_EQ(2u, scene.nodes.size());
    EXPECT_EQ("Reachable", scene.nodes[1].name);

    std::vector<std::string> warnings;
    std::string message;
    try
    {
        ValidateGltfEXT(doc.data, "cycle.gltf", warnings);
    }
    catch (const std::exception& e)
    {
        message = e.what();
    }
    EXPECT_FALSE(message.empty())
        << "a cyclic hierarchy was accepted as valid; termination alone is not enough";
}

// --- GLTF-125: depth ------------------------------------------------------------------------------

TEST(GltfNodeHierarchy, ATenThousandDeepChainDoesNotOverflowTheStack)
{
    // The traversal is iterative for exactly this reason, and an iterative traversal is easy to
    // quietly turn back into a recursive one during a refactor -- the recursive version is shorter
    // and passes every other test in this directory. 10 000 levels is past any plausible frame
    // budget for recursion and trivially cheap for a stack-based walk.
    const int depth = 10000;
    std::string nodes;
    for (int i = 0; i < depth; ++i)
    {
        nodes += R"({ "translation": [1, 0, 0])";
        if (i + 1 < depth) { nodes += R"(, "children": [)" + std::to_string(i + 1) + "]"; }
        nodes += " }";
        if (i + 1 < depth) { nodes += ", "; }
    }

    Parsed doc;
    ASSERT_TRUE(Parse(doc, std::string(R"GLTF({
  "asset": { "version": "2.0" },
  "scene": 0,
  "scenes": [ { "nodes": [0] } ],
  "nodes": [)GLTF") + nodes + "]\n}"));

    const SceneGraphOut scene = BuildSceneGraph(doc.data);
    ASSERT_EQ(static_cast<std::size_t>(depth) + 1u, scene.nodes.size());
    // Every level contributed once, so the deepest node sits at exactly `depth` -- which also
    // proves the composition stayed correct rather than merely surviving the depth.
    EXPECT_NEAR(static_cast<float>(depth), WorldOrigin(scene, static_cast<std::size_t>(depth)).X,
                0.5f);
}

// --- GLTF-126: names ------------------------------------------------------------------------------

TEST(GltfNodeHierarchy, UnnamedNodesGetStableDistinctGeneratedNames)
{
    // `.cnj` serializes bones by name and the runtime addresses them by index; a generated name
    // that varied between runs, or repeated across two nodes, would break the round-trip in a way
    // that only shows up on a file with unnamed nodes -- which is most files exported without
    // "preserve names" ticked.
    const char* json = R"GLTF({
  "asset": { "version": "2.0" },
  "scene": 0,
  "scenes": [ { "nodes": [0, 2] } ],
  "nodes": [
    { "translation": [1, 0, 0], "children": [1] },
    { "name": "Named", "translation": [2, 0, 0] },
    { "translation": [3, 0, 0] }
  ]
})GLTF";

    Parsed first;
    ASSERT_TRUE(Parse(first, json));
    Parsed second;
    ASSERT_TRUE(Parse(second, json));

    const SceneGraphOut a = BuildSceneGraph(first.data);
    const SceneGraphOut b = BuildSceneGraph(second.data);
    ASSERT_EQ(4u, a.nodes.size());
    ASSERT_EQ(a.nodes.size(), b.nodes.size());

    std::set<std::string> names;
    for (std::size_t i = 0; i < a.nodes.size(); ++i)
    {
        EXPECT_FALSE(a.nodes[i].name.empty()) << "node " << i << " has no name at all";
        EXPECT_EQ(a.nodes[i].name, b.nodes[i].name)
            << "node " << i << "'s generated name differs between two builds of the same file";
        names.insert(a.nodes[i].name);
    }
    EXPECT_EQ(a.nodes.size(), names.size()) << "two nodes share a name; bone lookup is ambiguous";

    // The authored name survives untouched -- a generator that renamed everything uniformly would
    // pass every assertion above.
    EXPECT_EQ("Named", a.nodes[2].name);
}
