// SPDX-License-Identifier: MS-PL
//
// plans/plan_gltf.md GLTF-116 / GLTF-117 / GLTF-119 / GLTF-232: mirroring.
//
// A mirroring transform is the one placement that changes something a world matrix multiplication
// does not express. Positions mirror the way any affine transform moves them -- every L4 assertion
// in the corpus passes -- while the triangle's *facing* reverses, so the model renders inside-out.
// §3.7.4 asks for the winding to be reversed when the node's global transform has a negative
// determinant, and §3.7.2.1's `TANGENT.w` handedness flips with it.
//
// CNA carries the fact rather than applying it, and the reason is architectural rather than a
// shortcut: vertex positions stay mesh-local (GLTF-103 Option A), so one mesh can be instanced by
// a mirrored node and an unmirrored one -- `xf-mirror-child` is exactly that file -- and those two
// draws share a single index buffer and a single vertex buffer. Reversing either at import fixes
// one placement by breaking the other. The reversal is a per-draw `RasterizerState::CullMode`
// decision, which is the boundary GLTF-231 already drew for `doubleSided`.
//
// So what is asserted here is: the detection is right (including through a hierarchy, where the
// obvious wrong implementation reads the instancing node's own scale), and the shared buffers are
// genuinely untouched, so the day the raster state is applied it has correct data to apply it to.

#include <cmath>
#include <cstring>
#include <gtest/gtest.h>
#include <string>
#include <vector>

#include "CNA/Internal/Graphics/VertexDeclarationFidelity.hpp"
#include "CNA/Internal/GltfImport/GltfImportCore.hpp"
#include "GltfFixtureCorpus.hpp"

using namespace CNA::Internal::GltfImport;
using namespace Microsoft::Xna::Framework;
using CnaTest::GltfOracle::LoadedFixture;

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

    /// The determinant of a matrix's linear part, computed here independently of the importer's
    /// own helper so the test is a second opinion rather than a restatement.
    float Determinant3x3(const Matrix& m)
    {
        return m.M11 * (m.M22 * m.M33 - m.M23 * m.M32)
             - m.M12 * (m.M21 * m.M33 - m.M23 * m.M31)
             + m.M13 * (m.M21 * m.M32 - m.M22 * m.M31);
    }

    /// Every instance of every group, flattened -- mirroring is per placement, and the group a
    /// placement lands in (by skin) is irrelevant to it.
    std::vector<MeshInstanceOut> AllInstances(const cgltf_data* data)
    {
        std::vector<MeshInstanceOut> out;
        for (const MeshGroup& group : CollectMeshGroups(data))
        {
            for (const MeshInstanceOut& instance : group.instances) { out.push_back(instance); }
        }
        return out;
    }

    const MeshInstanceOut* InstanceNamed(const std::vector<MeshInstanceOut>& instances,
                                          const std::string& nodeName)
    {
        for (const MeshInstanceOut& instance : instances)
        {
            if (instance.node != nullptr && instance.node->name != nullptr &&
                nodeName == instance.node->name)
            {
                return &instance;
            }
        }
        return nullptr;
    }
}

// --- GLTF-116: a mirroring placement is detected --------------------------------------------------

TEST(GltfMirroring, ANegativelyScaledNodeIsRecordedAsMirrored)
{
    const LoadedFixture fixture("xf-negative-scale");
    ASSERT_TRUE(fixture.Ok()) << fixture.Error();

    const std::vector<MeshInstanceOut> instances = AllInstances(&fixture.Data());
    ASSERT_EQ(1u, instances.size());
    EXPECT_TRUE(instances[0].mirroredEXT)
        << "a [-1,1,1] scale mirrors; without this the placement renders inside-out with nothing "
           "to point at, because every position assertion still passes";
    EXPECT_LT(Determinant3x3(instances[0].worldTransform), 0.0f);
}

TEST(GltfMirroring, AnOrdinaryPlacementIsNotMirrored)
{
    // The control, and not a formality: a flag that were always true would satisfy every
    // assertion above while marking the entire corpus inside-out.
    for (const char* id : {"xf-identity", "xf-parent-child", "xf-scale-nonuniform"})
    {
        SCOPED_TRACE(id);
        const LoadedFixture fixture(id);
        ASSERT_TRUE(fixture.Ok()) << fixture.Error();
        for (const MeshInstanceOut& instance : AllInstances(&fixture.Data()))
        {
            EXPECT_FALSE(instance.mirroredEXT);
            EXPECT_GT(Determinant3x3(instance.worldTransform), 0.0f);
        }
    }
}

TEST(GltfMirroring, ARotationOfOneHundredEightyDegreesIsNotMirroring)
{
    // The discriminator that separates "the determinant" from "does any scale component look
    // negative". A half turn about Z sends (x,y) to (-x,-y) -- two axes negated, which is exactly
    // what a naive per-component test reports as a mirror, and its determinant is +1. Geometry
    // rotated onto its back is still the same handedness, and reversing its winding would break a
    // model that was perfectly fine.
    Parsed doc;
    ASSERT_TRUE(Parse(doc, R"GLTF({
  "asset": { "version": "2.0" },
  "scene": 0,
  "scenes": [ { "nodes": [0] } ],
  "nodes": [ { "name": "HalfTurn", "rotation": [0, 0, 1, 0] } ]
})GLTF"));

    const SceneGraphOut scene = BuildSceneGraph(doc.data);
    ASSERT_EQ(2u, scene.nodes.size());
    const Matrix& local = scene.nodes[1].localTransform;
    // The rotation really is the two-axis-negating one, so the test cannot pass by having
    // authored something harmless.
    EXPECT_NEAR(-1.0f, Vector3::Transform(Vector3(1.0f, 0.0f, 0.0f), local).X, kTolerance);
    EXPECT_NEAR(-1.0f, Vector3::Transform(Vector3(0.0f, 1.0f, 0.0f), local).Y, kTolerance);
    EXPECT_GT(Determinant3x3(local), 0.0f) << "a rotation never mirrors, whatever its components";
}

// --- GLTF-117: mirroring composes through the hierarchy ------------------------------------------

TEST(GltfMirroring, MirroringFollowsTheComposedTransformAndNotTheInstancingNodesOwnScale)
{
    // `xf-mirror-child` places one mesh three times down a chain: an unmirrored root, a child
    // scaled [-1,1,1], and a grandchild scaled [-1,1,1] again. The determinant sign is
    // multiplicative, so the answers are +, -, + -- and the deepest placement is the one that
    // catches the shortcut, because its OWN scale is negative while its composed transform is not.
    const LoadedFixture fixture("xf-mirror-child");
    ASSERT_TRUE(fixture.Ok()) << fixture.Error();

    const std::vector<MeshInstanceOut> instances = AllInstances(&fixture.Data());
    ASSERT_EQ(3u, instances.size());

    const MeshInstanceOut* unmirrored = InstanceNamed(instances, "Unmirrored");
    const MeshInstanceOut* once = InstanceNamed(instances, "MirroredOnce");
    const MeshInstanceOut* twice = InstanceNamed(instances, "MirroredTwice");
    ASSERT_NE(nullptr, unmirrored);
    ASSERT_NE(nullptr, once);
    ASSERT_NE(nullptr, twice);

    EXPECT_FALSE(unmirrored->mirroredEXT);
    EXPECT_TRUE(once->mirroredEXT);
    EXPECT_FALSE(twice->mirroredEXT)
        << "two mirrors cancel -- reading the instancing node's own scale gets exactly this "
           "placement wrong, and renders it inside-out";

    // The independently computed determinants agree, sign for sign.
    EXPECT_GT(Determinant3x3(unmirrored->worldTransform), 0.0f);
    EXPECT_LT(Determinant3x3(once->worldTransform), 0.0f);
    EXPECT_GT(Determinant3x3(twice->worldTransform), 0.0f);
}

// --- GLTF-116 / GLTF-119: what mirroring must NOT do to the shared buffers -----------------------

TEST(GltfMirroring, TheSharedIndexAndVertexBuffersAreIdenticalForMirroredAndUnmirroredPlacements)
{
    // The reason the winding reversal cannot happen at import, stated as an assertion rather than
    // as a comment. All three placements in `xf-mirror-child` instance ONE mesh; extraction is a
    // property of the mesh, so it produces the same bytes regardless of which node is placing it.
    // If a future change reversed the index order for a mirrored instance, it would reverse it for
    // the unmirrored ones too -- this test is what makes that visible immediately rather than as a
    // rendering report weeks later.
    const LoadedFixture fixture("xf-mirror-child");
    ASSERT_TRUE(fixture.Ok()) << fixture.Error();

    const std::vector<MeshInstanceOut> instances = AllInstances(&fixture.Data());
    ASSERT_EQ(3u, instances.size());
    for (const MeshInstanceOut& instance : instances)
    {
        ASSERT_EQ(instances[0].mesh, instance.mesh) << "the fixture must share one mesh";
    }

    const MeshOut first = ExtractMesh(&fixture.Data(), instances[0].mesh->primitives[0], "probe",
                                       nullptr, 1.0f);
    ASSERT_FALSE(first.indexBytes.empty());
    for (std::size_t i = 1; i < instances.size(); ++i)
    {
        SCOPED_TRACE("instance " + std::to_string(i));
        const MeshOut other = ExtractMesh(&fixture.Data(), instances[i].mesh->primitives[0],
                                           "probe", nullptr, 1.0f);
        EXPECT_EQ(first.indexBytes, other.indexBytes)
            << "the index buffer became placement-dependent; one shared buffer cannot carry two "
               "windings";
        EXPECT_EQ(first.vertexBytes, other.vertexBytes)
            << "the vertex buffer became placement-dependent; TANGENT.w cannot be flipped per "
               "instance in a shared buffer either (GLTF-119)";
    }
}

TEST(GltfMirroring, AnAuthoredTangentHandednessSurvivesImportUnchanged)
{
    // GLTF-119's importable half. `TANGENT.w` is ±1 handedness (§3.7.2.1), and the mirroring sign
    // flip belongs with the winding reversal -- per draw, not per import, for the shared-buffer
    // reason above. What import owns is carrying the authored value through *exactly*: a w that
    // arrived as +1 when the file said -1 would flip the bitangent and light every normal-mapped
    // surface from the wrong side, with no sign of where it came from.
    //
    // Stride 48 is the unskinned PBR layout: position, normal, tangent(4), uv.
    const char* json = R"GLTF({
  "asset": { "version": "2.0" },
  "scene": 0,
  "scenes": [ { "nodes": [0] } ],
  "nodes": [ { "name": "TangentNode", "mesh": 0 } ],
  "materials": [ { "pbrMetallicRoughness": { "metallicFactor": 1.0 } } ],
  "meshes": [ { "primitives": [ { "attributes": {
      "POSITION": 0, "NORMAL": 1, "TANGENT": 2, "TEXCOORD_0": 3 }, "material": 0 } ] } ],
  "buffers": [ { "byteLength": 144, "uri": "data:application/octet-stream;base64,AAAAAAAAAAAAAAAAAACAPwAAAAAAAAAAAAAAAAAAgD8AAAAAAAAAAAAAAAAAAIA/AAAAAAAAAAAAAIA/AAAAAAAAAAAAAIA/AACAPwAAAAAAAAAAAACAvwAAgD8AAAAAAAAAAAAAgL8AAIA/AAAAAAAAAAAAAIC/AAAAAAAAAAAAAIA/AAAAAAAAAAAAAIA/" } ],
  "bufferViews": [
    { "buffer": 0, "byteOffset": 0,   "byteLength": 36 },
    { "buffer": 0, "byteOffset": 36,  "byteLength": 36 },
    { "buffer": 0, "byteOffset": 72,  "byteLength": 48 },
    { "buffer": 0, "byteOffset": 120, "byteLength": 24 }
  ],
  "accessors": [
    { "bufferView": 0, "componentType": 5126, "count": 3, "type": "VEC3",
      "min": [0,0,0], "max": [1,1,0] },
    { "bufferView": 1, "componentType": 5126, "count": 3, "type": "VEC3" },
    { "bufferView": 2, "componentType": 5126, "count": 3, "type": "VEC4" },
    { "bufferView": 3, "componentType": 5126, "count": 3, "type": "VEC2" }
  ]
})GLTF";

    Parsed doc;
    ASSERT_TRUE(Parse(doc, json));
    const MeshOut mesh =
        ExtractMesh(doc.data, doc.data->meshes[0].primitives[0], "probe", nullptr, 1.0f);
    ASSERT_EQ(48, mesh.stride) << "this fixture must land on the PBR layout that has a tangent";
    ASSERT_EQ(3u, mesh.vertexBytes.size() / 48u);

    // The tangent's byte offset comes from the canonical stride table, never from a number typed
    // here: GLTF-278 was exactly the failure of assuming one.
    const CNA::Internal::Graphics::InferredVertexLayout layout =
        CNA::Internal::Graphics::InferredLayoutForStride(
            mesh.stride, CNA::Internal::Graphics::UnlistedStrideLayout::RendererRefusesIt);
    ASSERT_TRUE(layout.known);
    int tangentOffset = -1;
    for (std::size_t e = 0; e < layout.count; ++e)
    {
        if (layout.elements[e].usage ==
            Microsoft::Xna::Framework::Graphics::VertexElementUsage::Tangent)
        {
            tangentOffset = layout.elements[e].offset;
        }
    }
    ASSERT_GE(tangentOffset, 0) << "stride " << mesh.stride << " has no tangent slot after all";

    for (std::size_t v = 0; v < 3; ++v)
    {
        SCOPED_TRACE("vertex " + std::to_string(v));
        float tangent[4];
        std::memcpy(tangent, mesh.vertexBytes.data() + v * 48u +
                                 static_cast<std::size_t>(tangentOffset), sizeof(tangent));
        EXPECT_NEAR(-1.0f, tangent[3], kTolerance)
            << "the authored handedness was replaced; a flipped w lights every normal-mapped "
               "surface from the wrong side";
    }
}

// --- GLTF-232: doubleSided and mirroring are independent facts ------------------------------------

TEST(GltfMirroring, MirroringAndDoubleSidednessAreCarriedIndependentlyOfEachOther)
{
    // The two interact at draw time and must not be conflated at import. A **double-sided**
    // mirrored primitive is unaffected by the missing winding reversal -- both faces are drawn
    // either way -- while a **single-sided** mirrored one shows its back face under the default
    // cull mode. So the pair of flags is exactly what a renderer needs to decide, and collapsing
    // them (say, marking a mirrored primitive double-sided to hide the problem) would silently
    // change what the file asked for: a single-sided material is a modelling decision, not a
    // workaround slot.
    //
    // Both fixtures below use the same mesh; only the material's `doubleSided` differs, so the
    // test cannot pass by coincidence of geometry.
    const char* singleSided = R"GLTF({
  "asset": { "version": "2.0" },
  "scene": 0,
  "scenes": [ { "nodes": [0] } ],
  "nodes": [ { "name": "MirroredNode", "mesh": 0, "scale": [-1, 1, 1] } ],
  "materials": [ { "name": "SingleSided", "doubleSided": false } ],
  "meshes": [ { "primitives": [ { "attributes": { "POSITION": 0 }, "material": 0 } ] } ],
  "buffers": [ { "byteLength": 36, "uri": "data:application/octet-stream;base64,AAAAAAAAAAAAAAAAAACAPwAAAAAAAAAAAAAAAAAAgD8AAAAA" } ],
  "bufferViews": [ { "buffer": 0, "byteOffset": 0, "byteLength": 36 } ],
  "accessors": [ { "bufferView": 0, "componentType": 5126, "count": 3, "type": "VEC3",
                   "min": [0,0,0], "max": [1,1,0] } ]
})GLTF";
    const char* doubleSided = R"GLTF({
  "asset": { "version": "2.0" },
  "scene": 0,
  "scenes": [ { "nodes": [0] } ],
  "nodes": [ { "name": "MirroredNode", "mesh": 0, "scale": [-1, 1, 1] } ],
  "materials": [ { "name": "DoubleSided", "doubleSided": true } ],
  "meshes": [ { "primitives": [ { "attributes": { "POSITION": 0 }, "material": 0 } ] } ],
  "buffers": [ { "byteLength": 36, "uri": "data:application/octet-stream;base64,AAAAAAAAAAAAAAAAAACAPwAAAAAAAAAAAAAAAAAAgD8AAAAA" } ],
  "bufferViews": [ { "buffer": 0, "byteOffset": 0, "byteLength": 36 } ],
  "accessors": [ { "bufferView": 0, "componentType": 5126, "count": 3, "type": "VEC3",
                   "min": [0,0,0], "max": [1,1,0] } ]
})GLTF";

    for (const auto& [json, expectDoubleSided] :
         {std::pair{singleSided, false}, std::pair{doubleSided, true}})
    {
        SCOPED_TRACE(expectDoubleSided ? "doubleSided" : "singleSided");
        Parsed doc;
        ASSERT_TRUE(Parse(doc, json));

        const SceneGraphOut scene = BuildSceneGraph(doc.data);
        const std::vector<MeshGroup> groups = CollectMeshGroups(doc.data, scene);
        ASSERT_EQ(1u, groups.size());
        ASSERT_EQ(1u, groups[0].instances.size());
        EXPECT_TRUE(groups[0].instances[0].mirroredEXT)
            << "the placement is mirrored regardless of what the material says";

        const MeshOut mesh =
            ExtractMesh(doc.data, doc.data->meshes[0].primitives[0], "probe", nullptr, 1.0f);
        EXPECT_EQ(expectDoubleSided, mesh.material.doubleSided)
            << "the material's own sidedness was changed by the placement's mirroring";
    }
}
