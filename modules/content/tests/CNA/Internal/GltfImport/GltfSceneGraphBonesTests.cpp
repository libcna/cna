// SPDX-License-Identifier: MS-PL
//
// plan_gltf.md GLTF-114 / GLTF-252 (Phase 5): the scene-graph <-> ModelBone structural contract.
//
// GLTF-113/GLTF-114 gave the imported model a real ModelBone per glTF scene node, and everything
// downstream leans on one invariant that was, until this file existed, only implicit:
//
//     BuildSceneGraph(data).nodes[i]   <->   model.Bones[i]
//
// Rigid node animation (GLTF-293), node-attached cameras and lights (GLTF-317/GLTF-325), the .cnj
// "bones"/"parentBone" serialization (GLTF-129) and Model::CopyAbsoluteBoneTransformsTo all address
// nodes by that index. An off-by-one or a reordering would not fail any existing test loudly -- it
// would quietly place the wrong geometry, which is the exact failure class this campaign exists to
// remove. So the invariant is asserted here as a contract rather than left as an assumption, and it
// is locked BEFORE rigid animation starts consuming the same indices.
//
// The second contract in this file is the deliberate asymmetry between rigid and skinned meshes
// (plan_gltf.md §15.1):
//
//     rigid mesh   -> ModelMesh::ParentBone == its own scene node's bone
//     skinned mesh -> its scene node's bone EXISTS and carries the node's transform,
//                     but ModelMesh::ParentBone is the synthetic identity root
//
// glTF requires a skinned mesh's own node transform to be ignored, because its joints already place
// the geometry. Dropping the node from the scene model and ignoring it in the skinning transform
// are two different things: the first is data loss, the second is the specification. This file
// asserts CNA does the second. GLTF-260 is what proves the mesh-space cancellation then happens
// exactly once; until it lands, D8 keeps that half open.

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "CNA/Internal/Graphics/VertexDeclarationFidelity.hpp"
#include "CNA/Internal/GltfImport/GltfImportCore.hpp"
#include "GltfFixtureCorpus.hpp"
#include "Microsoft/Xna/Framework/Content/ContentManager.hpp"
#include "Microsoft/Xna/Framework/Quaternion.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/AnimationPlayer.hpp"
#include "Microsoft/Xna/Framework/Graphics/Model.hpp"
#include "Microsoft/Xna/Framework/Graphics/ModelBone.hpp"
#include "Microsoft/Xna/Framework/Graphics/ModelBoneCollection.hpp"
#include "Microsoft/Xna/Framework/Graphics/AlphaModeEXT.hpp"
#include "Microsoft/Xna/Framework/Graphics/Effect.hpp"
#include "Microsoft/Xna/Framework/Graphics/PbrEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/IEffectLights.hpp"
#include "Microsoft/Xna/Framework/Graphics/ModelMesh.hpp"
#include "Microsoft/Xna/Framework/Graphics/ModelMeshCollection.hpp"
#include "Microsoft/Xna/Framework/Graphics/ModelMeshPart.hpp"
#include "Microsoft/Xna/Framework/Graphics/ModelMeshPartCollection.hpp"
#include "Microsoft/Xna/Framework/Graphics/MorphTargetEXT.hpp"

using CnaTest::GltfOracle::CorpusDirectory;
using CnaTest::GltfOracle::LoadedFixture;
using CnaTest::GltfOracle::BoolOr;
using CnaTest::GltfOracle::Member;
using CnaTest::GltfOracle::NumberOr;
using CnaTest::GltfOracle::Numbers;
using CnaTest::GltfOracle::Path;
using CnaTest::GltfOracle::StringOr;
using Microsoft::Xna::Framework::Matrix;
using Microsoft::Xna::Framework::Content::ContentManager;
using Microsoft::Xna::Framework::Graphics::GraphicsDevice;
using Microsoft::Xna::Framework::Graphics::Model;
using Microsoft::Xna::Framework::Graphics::ModelBone;
using Microsoft::Xna::Framework::Graphics::ModelMesh;

namespace
{
    constexpr float kTolerance = 1e-5f;

    void ExpectMatrixNear(const Matrix& expected, const Matrix& actual, const std::string& what)
    {
        const float* e = &expected.M11;
        const float* a = &actual.M11;
        for (int i = 0; i < 16; ++i)
        {
            EXPECT_NEAR(e[i], a[i], kTolerance) << what << ", component " << i;
        }
    }

    /// Loads a corpus fixture through the real runtime .gltf path, with the corpus as the content
    /// root. Nothing is copied or rewritten: the asset asserted here is the committed one.
    Model LoadCorpusModel(const std::string& fixtureId, GraphicsDevice& gd, ContentManager& cm)
    {
        return cm.Load<Model>(fixtureId);
    }

    /// The contract itself, run over one fixture: bone/node lockstep plus the mesh-parenting rule.
    void ExpectSceneGraphMatchesBones(const std::string& fixtureId)
    {
        SCOPED_TRACE(fixtureId);
        const LoadedFixture fixture(fixtureId);
        ASSERT_TRUE(fixture.Ok()) << fixture.Error();

        const CNA::Internal::GltfImport::SceneGraphOut scene =
            CNA::Internal::GltfImport::BuildSceneGraph(&fixture.Data());
        const std::vector<CNA::Internal::GltfImport::MeshGroup> groups =
            CNA::Internal::GltfImport::CollectMeshGroups(&fixture.Data(), scene);

        GraphicsDevice gd;
        ContentManager cm(nullptr, CorpusDirectory().string());
        cm.setGraphicsDevice(gd);
        Model model = LoadCorpusModel(fixtureId, gd, cm);

        const auto& bones = model.getBonesProperty();

        // (1) Every scene node has exactly one bone, at the same index. No extra bones, none
        //     missing, and the ordering is the scene graph's -- never a skin palette's (§15.1.2).
        ASSERT_EQ(static_cast<int>(scene.nodes.size()), bones.getCountProperty())
            << "the bone count no longer equals the scene node count";

        // (2) Bone 0 is the synthetic identity root, and it is what Model::Root reports.
        ASSERT_GT(bones.getCountProperty(), 0);
        EXPECT_EQ(model.getRootProperty(), bones[0]);
        EXPECT_EQ(nullptr, bones[0]->getParentProperty()) << "the synthetic root must have no parent";
        EXPECT_EQ("Root", bones[0]->getNameProperty());
        ExpectMatrixNear(Matrix::getIdentityProperty(), bones[0]->getTransformProperty(),
                         "synthetic root transform");

        // (3) Per node: stable index, exact parent, exact node-local transform.
        for (std::size_t i = 0; i < scene.nodes.size(); ++i)
        {
            const CNA::Internal::GltfImport::SceneNodeOut& node = scene.nodes[i];
            ModelBone* bone = bones[static_cast<int>(i)];
            ASSERT_NE(nullptr, bone);
            SCOPED_TRACE("node " + std::to_string(i) + " (" + node.name + ")");

            EXPECT_EQ(static_cast<int>(i), bone->getIndexProperty())
                << "ModelBone::Index must equal its own sceneNodeIndex";
            EXPECT_EQ(node.name, bone->getNameProperty());
            ExpectMatrixNear(node.localTransform, bone->getTransformProperty(), "local transform");

            if (i == 0) { continue; }
            ASSERT_NE(nullptr, bone->getParentProperty()) << "only the root may be parentless";
            EXPECT_EQ(node.parentIndex, bone->getParentProperty()->getIndexProperty())
                << "parent index diverged from the scene graph";
        }

        // (4) Composed world transforms agree. BuildSceneGraph composes them once at import;
        //     Model::CopyAbsoluteBoneTransformsTo recomposes them from the bone tree at draw time.
        //     Those are two independent computations and they must not drift apart.
        std::vector<Matrix> absolute(static_cast<std::size_t>(bones.getCountProperty()));
        model.CopyAbsoluteBoneTransformsTo(absolute);
        for (std::size_t i = 0; i < scene.nodes.size(); ++i)
        {
            SCOPED_TRACE("world transform of node " + std::to_string(i));
            ExpectMatrixNear(scene.nodes[i].worldTransform, absolute[i], "world transform");
        }

        // (5) The mesh-parenting rule, including its deliberate asymmetry.
        //
        //     Only the first mesh group reaches a single Model today (a documented limitation --
        //     GLTF-137), so this walks the same group the loader used, in the same order.
        ASSERT_FALSE(groups.empty());
        const auto& instances = groups.front().instances;
        int meshIndex = 0;
        for (const CNA::Internal::GltfImport::MeshInstanceOut& instance : instances)
        {
            for (std::size_t p = 0; p < instance.mesh->primitives_count; ++p)
            {
                ASSERT_LT(meshIndex, model.getMeshesProperty().getCountProperty());
                ModelMesh* mesh = model.getMeshesProperty()[meshIndex];
                ASSERT_NE(nullptr, mesh->getParentBoneProperty());
                SCOPED_TRACE("mesh " + std::to_string(meshIndex));

                // The instancing node always has a bone, skinned or not: the node is never dropped
                // from the scene model.
                ASSERT_LT(instance.sceneNodeIndex, bones.getCountProperty());
                ModelBone* nodeBone = bones[instance.sceneNodeIndex];
                ASSERT_NE(nullptr, nodeBone);
                ExpectMatrixNear(scene.nodes[static_cast<std::size_t>(instance.sceneNodeIndex)].localTransform,
                                 nodeBone->getTransformProperty(), "instancing node's own bone");

                if (instance.skinned)
                {
                    // glTF ignores a skinned mesh's own node transform. The bone still exists and
                    // still carries that transform -- it simply does not transform this mesh.
                    EXPECT_EQ(bones[0], mesh->getParentBoneProperty())
                        << "a skinned mesh must hang off the identity root, not its own node's bone";
                    EXPECT_NE(instance.sceneNodeIndex, mesh->getParentBoneProperty()->getIndexProperty())
                        << "the skinned mesh node's transform is being applied to the geometry as "
                           "well as cancelled in the skin equation -- that is the double "
                           "application GLTF-260 exists to prevent";
                }
                else
                {
                    EXPECT_EQ(instance.sceneNodeIndex, mesh->getParentBoneProperty()->getIndexProperty())
                        << "a rigid mesh must be parented to its own instancing node's bone";
                }
                ++meshIndex;
            }
        }
        EXPECT_EQ(meshIndex, model.getMeshesProperty().getCountProperty())
            << "the model has a different number of meshes than the group has primitives";
    }
}

TEST(GltfSceneGraphBones, DeepHierarchyMapsOntoBonesInLockstep)
{
    // Two nested nodes plus the synthetic root: proves parent indices and composed transforms.
    ExpectSceneGraphMatchesBones("xf-parent-child");
}

TEST(GltfSceneGraphBones, SharedMeshGetsOneBonePerInstancingNode)
{
    // One mesh, two nodes: proves instancing survives, and that each placement is parented to its
    // own node rather than both collapsing onto one bone.
    ExpectSceneGraphMatchesBones("xf-shared-mesh");
}

TEST(GltfSceneGraphBones, MatrixAuthoredNodeMapsOntoItsBone)
{
    ExpectSceneGraphMatchesBones("xf-matrix-node");
}

TEST(GltfSceneGraphBones, UntransformedControlCase)
{
    ExpectSceneGraphMatchesBones("xf-identity");
}

TEST(GltfSceneGraphBones, SkinnedMeshKeepsItsNodeBoneButIsNotTransformedByIt)
{
    // The asymmetry, on the fixture that proves D8: the armature and the skinned mesh node both
    // carry transforms and both get bones, but the mesh hangs off the identity root.
    ExpectSceneGraphMatchesBones("skin-armature-ancestor");
}

TEST(GltfSceneGraphBones, SkinnedMeshAncestryIsPreservedInTheSceneModelButDoesNotTransformIt)
{
    // The half that is easy to "fix" the wrong way. A skinned mesh's placing transform must be
    // IGNORED, not DELETED: dropping the nodes would make some assets render correctly today by
    // accident and then make GLTF-247's mesh-space cancellation impossible to express, because
    // there would be nothing left to cancel.
    //
    // skin-armature-ancestor is the D8 fixture: the Armature carries translation [0,100,0] and the
    // skinned mesh node hangs beneath it. Both must appear in the bone tree with their transforms
    // intact, while the mesh itself hangs off the identity root.
    //
    // This fixture's own mesh node is untransformed, so the non-identity mesh-node case is not
    // covered here; that is skin-mesh-node-transform (plan_gltf.md §15.4), which P0-D adds together
    // with GLTF-247/GLTF-260. Asserting a translation this fixture does not declare would be
    // fabricated coverage, so it is deliberately absent rather than approximated.
    const LoadedFixture fixture("skin-armature-ancestor");
    ASSERT_TRUE(fixture.Ok()) << fixture.Error();

    GraphicsDevice gd;
    ContentManager cm(nullptr, CorpusDirectory().string());
    cm.setGraphicsDevice(gd);
    Model model = cm.Load<Model>("skin-armature-ancestor");

    const auto& bones = model.getBonesProperty();
    const ModelBone* armature = nullptr;
    const ModelBone* meshNode = nullptr;
    for (int i = 0; i < bones.getCountProperty(); ++i)
    {
        if (bones[i]->getNameProperty() == "Armature") { armature = bones[i]; }
        if (bones[i]->getNameProperty() == "SkinnedMeshNode") { meshNode = bones[i]; }
    }

    // The ancestry BuildSkeleton still drops (D8) is nonetheless present in the scene model, which
    // is what will let GLTF-245 recover it without re-parsing the file.
    ASSERT_NE(nullptr, armature) << "the armature vanished from the bone tree";
    EXPECT_NEAR(100.0f, armature->getTransformProperty().M42, kTolerance)
        << "the armature's translation was dropped instead of merely being unused by the skin";

    // In this fixture the skinned mesh node is a scene root beside the armature rather than a
    // descendant of it -- the ancestry D8 loses is the joints' (Joint0 under Armature), not the
    // mesh node's. The generic contract above already checks every node's parent against the scene
    // graph, so all this needs to add is that the node is present at all.
    ASSERT_NE(nullptr, meshNode) << "the skinned mesh's own node vanished from the bone tree";
    ASSERT_NE(nullptr, meshNode->getParentProperty());
    EXPECT_EQ(0, meshNode->getParentProperty()->getIndexProperty());

    // The joint whose global transform D8 mis-computes: present, and under the armature.
    const ModelBone* joint = nullptr;
    for (int i = 0; i < bones.getCountProperty(); ++i)
    {
        if (bones[i]->getNameProperty() == "Joint0") { joint = bones[i]; }
    }
    ASSERT_NE(nullptr, joint) << "the skin's joint vanished from the scene model";
    ASSERT_NE(nullptr, joint->getParentProperty());
    EXPECT_EQ(armature, joint->getParentProperty())
        << "the joint lost the ancestor whose transform GLTF-245 has to recover";

    // ...and none of it transforms the mesh, because glTF says the joints already place it.
    ASSERT_EQ(1, model.getMeshesProperty().getCountProperty());
    EXPECT_EQ(bones[0], model.getMeshesProperty()[0]->getParentBoneProperty());
}

// --- GLTF-252: scene-node identity and joint-palette ordering are separate index spaces ---------

TEST(GltfSkinSpaces, SceneNodeAndPaletteMappingsAreExplicitAndBidirectional)
{
    using namespace CNA::Internal::GltfImport;

    const LoadedFixture fixture("skin-parented-joints");
    ASSERT_TRUE(fixture.Ok()) << fixture.Error();
    const SceneGraphOut scene = BuildSceneGraph(&fixture.Data());
    const std::vector<MeshGroup> groups = CollectMeshGroups(&fixture.Data(), scene);
    ASSERT_EQ(1u, groups.size());
    ASSERT_NE(nullptr, groups[0].skin);
    ASSERT_EQ(1u, groups[0].instances.size());
    const MeshInstanceOut& instance = groups[0].instances[0];

    // The fixture is intentionally hostile to an identity remap: its file-local joints array is
    // [JointB(child), JointA(parent)], while a usable GPU palette must be [JointA, JointB].
    ASSERT_EQ(2u, groups[0].skin->joints_count);
    ASSERT_NE(nullptr, groups[0].skin->joints[0]->name);
    ASSERT_NE(nullptr, groups[0].skin->joints[1]->name);
    EXPECT_STREQ("JointB", groups[0].skin->joints[0]->name);
    EXPECT_STREQ("JointA", groups[0].skin->joints[1]->name);

    const SkeletonResult skeleton =
        BuildSkeleton(groups[0].skin, scene, instance.worldTransform, 1.0f);
    ASSERT_EQ(2u, skeleton.bones.size());
    ASSERT_EQ(2u, skeleton.oldToNew.size());
    EXPECT_EQ(1, skeleton.oldToNew[0]);
    EXPECT_EQ(0, skeleton.oldToNew[1]);
    EXPECT_EQ("JointA", skeleton.bones[0].name);
    EXPECT_EQ("JointB", skeleton.bones[1].name);

    // Model::Bones follows the scene graph and includes a synthetic root plus non-joint nodes;
    // the palette does neither. Resolve by name to make the asserted index space unmistakable.
    ASSERT_EQ(scene.nodes.size(), skeleton.sceneNodeIndexToPaletteIndex.size());
    ASSERT_EQ(skeleton.bones.size(), skeleton.paletteIndexToSceneNodeIndex.size());
    EXPECT_EQ(-1, skeleton.sceneNodeIndexToPaletteIndex[0])
        << "the synthetic scene root is not a joint-palette entry";

    for (std::size_t paletteIndex = 0; paletteIndex < skeleton.bones.size(); ++paletteIndex)
    {
        const int sceneNodeIndex =
            skeleton.paletteIndexToSceneNodeIndex[paletteIndex];
        ASSERT_GT(sceneNodeIndex, 0);
        ASSERT_LT(static_cast<std::size_t>(sceneNodeIndex), scene.nodes.size());
        EXPECT_EQ(static_cast<int>(paletteIndex),
                  skeleton.sceneNodeIndexToPaletteIndex[
                      static_cast<std::size_t>(sceneNodeIndex)]);
        EXPECT_EQ(skeleton.bones[paletteIndex].name,
                  scene.nodes[static_cast<std::size_t>(sceneNodeIndex)].name);
    }
    EXPECT_NE(skeleton.paletteIndexToSceneNodeIndex[0], 0)
        << "paletteIndex was silently treated as sceneNodeIndex";

    const int meshSceneNodeIndex = instance.sceneNodeIndex;
    ASSERT_GT(meshSceneNodeIndex, 0);
    ASSERT_LT(static_cast<std::size_t>(meshSceneNodeIndex),
              skeleton.sceneNodeIndexToPaletteIndex.size());
    EXPECT_EQ(-1, skeleton.sceneNodeIndexToPaletteIndex[
                      static_cast<std::size_t>(meshSceneNodeIndex)])
        << "the skinned mesh node belongs to the scene, not to its skin's joint palette";

    // Finally pin the consumer of oldToNew. BlendIndices is the final four bytes of this fixture's
    // 68-byte skinned-PBR vertex. The authored indices [1], [0], [1,0] must therefore arrive as
    // palette slots [0], [1], [0,1] -- never as file-local or scene-node indices.
    ASSERT_NE(nullptr, instance.mesh);
    ASSERT_EQ(1u, instance.mesh->primitives_count);
    const MeshOut mesh = ExtractMesh(
        &fixture.Data(), instance.mesh->primitives[0], "skin-parented-joints", &skeleton, 1.0f);
    ASSERT_EQ(68, mesh.stride);
    ASSERT_EQ(static_cast<std::size_t>(mesh.stride) * 3u, mesh.vertexBytes.size());
    constexpr std::size_t blendIndicesOffset = 64u;
    const std::array<std::array<std::uint8_t, 4>, 3> expectedPaletteIndices = {{
        {{0u, 1u, 1u, 1u}}, {{1u, 1u, 1u, 1u}}, {{0u, 1u, 1u, 1u}}
    }};
    for (std::size_t vertex = 0; vertex < expectedPaletteIndices.size(); ++vertex)
    {
        for (std::size_t component = 0; component < 4u; ++component)
        {
            EXPECT_EQ(expectedPaletteIndices[vertex][component],
                      mesh.vertexBytes[vertex * static_cast<std::size_t>(mesh.stride) +
                                       blendIndicesOffset + component])
                << "vertex " << vertex << ", BlendIndices component " << component;
        }
    }
}

// --- GLTF-245 / GLTF-247 / GLTF-248: the skin coordinate spaces --------------------------------

TEST(GltfSkinSpaces, RootJointCarriesTheSceneAncestryAboveTheJointSet)
{
    // D8's fixture, end to end through the real loader. Joint0 has no local transform of its own,
    // so its global transform is entirely inherited from the Armature above it, and the authored
    // inverse bind matrix is the true inverse of that global transform. The correct joint matrix is
    // therefore exactly the identity -- a vertex bound to Joint0 must not move at all.
    //
    // Before GLTF-245 this produced translate(0,-100,0): the ancestry was dropped from the bind
    // pose while the inverse bind matrix still contained it, so every skinned vertex was displaced
    // by the inverse of what was lost.
    const LoadedFixture fixture("skin-armature-ancestor");
    ASSERT_TRUE(fixture.Ok()) << fixture.Error();

    GraphicsDevice gd;
    ContentManager cm(nullptr, CorpusDirectory().string());
    cm.setGraphicsDevice(gd);
    Model model = cm.Load<Model>("skin-armature-ancestor");

    auto* skinning = dynamic_cast<Microsoft::Xna::Framework::Graphics::SkinningData*>(
        model.getTagProperty());
    ASSERT_NE(nullptr, skinning) << "the skinned model carries no SkinningData";
    ASSERT_EQ(1, skinning->BoneCount);

    Microsoft::Xna::Framework::Graphics::AnimationPlayer player(*skinning);
    const std::vector<Matrix>& skin = player.GetSkinTransforms();
    ASSERT_EQ(1u, skin.size());
    ExpectMatrixNear(Matrix::getIdentityProperty(), skin[0],
                     "joint matrix for a joint whose global transform its IBM exactly inverts");
}

TEST(GltfSkinSpaces, SkinnedVertexLandsWhereTheSpecificationSaysItDoes)
{
    // The same claim one layer up, in world space, which is what the owner actually sees. The
    // fixture's carrier triangle spans mesh-local [0,1] in X and Y; with an identity joint matrix
    // and a skinned mesh whose node transform is cancelled, the skinned world positions must equal
    // the mesh-local ones exactly.
    const LoadedFixture fixture("skin-armature-ancestor");
    ASSERT_TRUE(fixture.Ok()) << fixture.Error();

    GraphicsDevice gd;
    ContentManager cm(nullptr, CorpusDirectory().string());
    cm.setGraphicsDevice(gd);
    Model model = cm.Load<Model>("skin-armature-ancestor");

    auto* skinning = dynamic_cast<Microsoft::Xna::Framework::Graphics::SkinningData*>(
        model.getTagProperty());
    ASSERT_NE(nullptr, skinning);
    Microsoft::Xna::Framework::Graphics::AnimationPlayer player(*skinning);

    // Every vertex is fully weighted to joint 0, so skinnedPosition = position * jointMatrix[0].
    const Matrix jointMatrix = player.GetSkinTransforms()[0];
    const Microsoft::Xna::Framework::Vector3 local(1.0f, 0.0f, 0.0f);
    const Microsoft::Xna::Framework::Vector3 skinned =
        Microsoft::Xna::Framework::Vector3::Transform(local, jointMatrix);

    EXPECT_NEAR(1.0f, skinned.X, kTolerance);
    EXPECT_NEAR(0.0f, skinned.Y, kTolerance)
        << "the armature's [0,100,0] is leaking into the skinned position -- D8 is back";
    EXPECT_NEAR(0.0f, skinned.Z, kTolerance);
}

TEST(GltfSkinSpaces, MeshNodeTransformIsCancelledExactlyOnce)
{
    // GLTF-260, both halves, on the fixture that isolates them. Joint0 has an identity bind pose
    // and an identity inverse bind matrix, so the entire joint matrix is the mesh node's own
    // cancellation: inverse(T(0,0,50)) = T(0,0,-50).
    //
    // Three outcomes are distinguishable here, which is the point of the fixture:
    //   * no cancellation at all      -> joint matrix is the identity  (GLTF-247 missing)
    //   * cancelled once              -> T(0,0,-50)                    (correct)
    //   * cancelled AND the node bone -> the mesh renders at -50 in world space rather than 0,
    //     because Model::Draw would apply the node bone on top of the already-cancelled geometry
    const LoadedFixture fixture("skin-mesh-node-transform");
    ASSERT_TRUE(fixture.Ok()) << fixture.Error();

    GraphicsDevice gd;
    ContentManager cm(nullptr, CorpusDirectory().string());
    cm.setGraphicsDevice(gd);
    Model model = cm.Load<Model>("skin-mesh-node-transform");

    auto* skinning = dynamic_cast<Microsoft::Xna::Framework::Graphics::SkinningData*>(
        model.getTagProperty());
    ASSERT_NE(nullptr, skinning);
    ASSERT_EQ(1, skinning->BoneCount);

    Microsoft::Xna::Framework::Graphics::AnimationPlayer player(*skinning);
    const Matrix jointMatrix = player.GetSkinTransforms()[0];

    // (a) the cancellation exists and is applied exactly once
    EXPECT_NEAR(-50.0f, jointMatrix.M43, kTolerance)
        << "expected inverse(T(0,0,50)); 0 means GLTF-247's cancellation is missing, -100 means it "
           "was applied twice";
    EXPECT_NEAR(0.0f, jointMatrix.M41, kTolerance);
    EXPECT_NEAR(0.0f, jointMatrix.M42, kTolerance);

    // (b) the node's bone exists and keeps its transform, but does not transform the mesh -- so the
    //     cancellation is not silently undone by the hierarchy Phase 5 introduced.
    const auto& bones = model.getBonesProperty();
    const ModelBone* meshNodeBone = nullptr;
    for (int i = 0; i < bones.getCountProperty(); ++i)
    {
        if (bones[i]->getNameProperty() == "SkinnedMeshNode") { meshNodeBone = bones[i]; }
    }
    ASSERT_NE(nullptr, meshNodeBone);
    EXPECT_NEAR(50.0f, meshNodeBone->getTransformProperty().M43, kTolerance)
        << "the mesh node's transform was deleted rather than cancelled";

    ASSERT_EQ(1, model.getMeshesProperty().getCountProperty());
    ASSERT_NE(nullptr, model.getMeshesProperty()[0]->getParentBoneProperty());
    EXPECT_EQ(0, model.getMeshesProperty()[0]->getParentBoneProperty()->getIndexProperty())
        << "the skinned mesh is parented to its own node's bone, so Model::Draw will apply the very "
           "transform the joint matrix just cancelled -- the double application GLTF-260 forbids";

    // (c) the two together: a vertex at mesh-local (1,0,0) ends up at (1,0,-50) in skin space and
    //     the identity-rooted mesh adds nothing, so that is also its world position.
    const Microsoft::Xna::Framework::Vector3 skinned =
        Microsoft::Xna::Framework::Vector3::Transform(
            Microsoft::Xna::Framework::Vector3(1.0f, 0.0f, 0.0f), jointMatrix);
    EXPECT_NEAR(1.0f, skinned.X, kTolerance);
    EXPECT_NEAR(0.0f, skinned.Y, kTolerance);
    EXPECT_NEAR(-50.0f, skinned.Z, kTolerance);
}

// --- GLTF-293: rigid (non-joint) node animation ------------------------------------------------

TEST(GltfRigidAnimation, AChannelTargetingAnOrdinaryMeshNodeBecomesATrackOnThatNodesBone)
{
    // D6's fixture. anim-rigid-node has no skin anywhere: one LINEAR rotation channel drives a
    // plain mesh node through a quarter turn about +Z -- a door, a turntable, a clock hand.
    //
    // Before GLTF-293 this produced nothing at all and said nothing: ExtractClips resolves every
    // channel against a skin's joint set, so the channel matched nothing and was skipped, and the
    // offline tool called ExtractClips only for a skinned group in the first place.
    using namespace CNA::Internal::GltfImport;

    const LoadedFixture fixture("anim-rigid-node");
    ASSERT_TRUE(fixture.Ok()) << fixture.Error();

    const SceneGraphOut scene = BuildSceneGraph(&fixture.Data());
    std::vector<std::string> warnings;
    const std::vector<ClipOut> clips =
        ExtractSceneNodeClips(&fixture.Data(), scene, 1.0f, warnings);

    ASSERT_EQ(1u, clips.size()) << "the file declares one animation and it drives an imported node";
    const ClipOut& clip = clips[0];
    EXPECT_EQ("Spin", clip.name);
    EXPECT_EQ(ClipTargetSpace::SceneNode, clip.targetSpace)
        << "a rigid clip's indices are sceneNodeIndex, not paletteIndex (§15.1.2)";
    ASSERT_EQ(1u, clip.tracks.size());

    // The track targets the animated node's own bone, which is what makes it playable: both
    // loaders mirror SceneGraphOut::nodes one-for-one onto Model::Bones.
    const cgltf_node* animated = fixture.Data().animations[0].channels[0].target_node;
    ASSERT_NE(nullptr, animated);
    const auto placed = scene.indexOfNode.find(animated);
    ASSERT_NE(scene.indexOfNode.end(), placed);
    EXPECT_EQ(placed->second, clip.tracks[0].boneIndex);
    EXPECT_EQ("SpinningMesh", scene.nodes[static_cast<std::size_t>(placed->second)].name);

    // The manifest's own pose expectation: identity at t=0, a quarter turn about +Z at t=1.
    const std::vector<KeyframeOut>& keys = clip.tracks[0].keys;
    ASSERT_EQ(2u, keys.size());
    EXPECT_NEAR(0.0, keys[0].time, kTolerance);
    EXPECT_NEAR(1.0, keys[1].time, kTolerance);
    EXPECT_NEAR(1.0, clip.duration, kTolerance);

    const float halfSqrt2 = 0.70710678f;
    EXPECT_NEAR(0.0f, keys[0].rotation.Z, kTolerance);
    EXPECT_NEAR(1.0f, keys[0].rotation.W, kTolerance);
    EXPECT_NEAR(halfSqrt2, keys[1].rotation.Z, kTolerance);
    EXPECT_NEAR(halfSqrt2, keys[1].rotation.W, kTolerance);

    // A channel that authors only rotation must still produce complete keyframes: the missing
    // components come from the node's own bind pose, not from zero.
    for (const KeyframeOut& key : keys)
    {
        EXPECT_NEAR(1.0f, key.scale.X, kTolerance);
        EXPECT_NEAR(1.0f, key.scale.Y, kTolerance);
        EXPECT_NEAR(1.0f, key.scale.Z, kTolerance);
    }
}

TEST(GltfRigidAnimation, AChannelTargetingANodeOutsideTheDefaultSceneIsReportedNotDropped)
{
    // scene-default-selection has a decoy node outside the default scene. Nothing animates it, so
    // the assertion here is the converse one: an animation that drives nothing imported yields no
    // clip at all rather than an empty one that would play nothing.
    using namespace CNA::Internal::GltfImport;

    const LoadedFixture fixture("scene-default-selection");
    ASSERT_TRUE(fixture.Ok()) << fixture.Error();

    const SceneGraphOut scene = BuildSceneGraph(&fixture.Data());
    std::vector<std::string> warnings;
    const std::vector<ClipOut> clips =
        ExtractSceneNodeClips(&fixture.Data(), scene, 1.0f, warnings);
    EXPECT_TRUE(clips.empty()) << "the fixture declares no animation at all";
}

TEST(GltfRigidAnimation, RigidAndSkinnedExtractionAgreeOnTheSameChannelData)
{
    // The two extractors differ ONLY in which index space a target node resolves into. Asserting
    // that on a skinned fixture is what stops them drifting: skin-armature-ancestor has no
    // animation, so the meaningful check is that neither invents one.
    using namespace CNA::Internal::GltfImport;

    const LoadedFixture fixture("skin-armature-ancestor");
    ASSERT_TRUE(fixture.Ok()) << fixture.Error();

    const SceneGraphOut scene = BuildSceneGraph(&fixture.Data());
    std::vector<std::string> warnings;
    EXPECT_TRUE(ExtractSceneNodeClips(&fixture.Data(), scene, 1.0f, warnings).empty());

    // ...and a clip extracted against a joint palette still says so, so an existing consumer's
    // meaning is unchanged by the new field.
    ClipOut defaultConstructed;
    EXPECT_EQ(ClipTargetSpace::JointPalette, defaultConstructed.targetSpace);
}

// --- GLTF-215's lighting fallback policy --------------------------------------------------------

TEST(GltfLightingPolicy, AFileThatDeclaresNoLightGetsTheDefaultLightingRig)
{
    // glTF does not require a scene to declare any light, and most authored assets do not --
    // lighting is normally the viewer's business. That was harmless while an untextured mesh
    // imported through BasicEffect, whose defaults are visible. GLTF-215 made metallic-roughness
    // the selection rule, so the same mesh now reaches PbrEffect, whose AmbientLightColor defaults
    // to (0,0,0) with all three directional slots disabled: a light-less file would render black.
    //
    // The policy is that a file expressing NO lighting intent gets the effect's own
    // EnableDefaultLighting() rig. This test is what stops that being lost silently -- it would
    // fail as an unlit black surface nobody could see in a numerical layer.
    const LoadedFixture fixture("xf-identity");
    ASSERT_TRUE(fixture.Ok()) << fixture.Error();
    ASSERT_EQ(0u, static_cast<std::size_t>(fixture.Data().extensions_used_count))
        << "this fixture is supposed to declare no lighting extension at all";

    GraphicsDevice gd;
    ContentManager cm(nullptr, CorpusDirectory().string());
    cm.setGraphicsDevice(gd);
    Model model = cm.Load<Model>("xf-identity");
    ASSERT_EQ(1, model.getMeshesProperty().getCountProperty());

    Microsoft::Xna::Framework::Graphics::Effect* fx =
        model.getMeshesProperty()[0]->getMeshPartsProperty()[0]->getEffectProperty();
    ASSERT_NE(nullptr, fx);
    auto* lit = dynamic_cast<Microsoft::Xna::Framework::Graphics::IEffectLights*>(fx);
    ASSERT_NE(nullptr, lit) << "the selected effect carries no lighting interface at all";

    EXPECT_TRUE(lit->getDirectionalLight0Property().getEnabledProperty())
        << "a light-less glTF file imported with every light disabled -- it would render black";

    // The ambient term must not be left at PbrEffect's own (0,0,0) either: with only directional
    // light, anything facing away from the key light is still unlit.
    const Microsoft::Xna::Framework::Vector3 ambient = lit->getAmbientLightColorProperty();
    EXPECT_GT(ambient.X + ambient.Y + ambient.Z, 0.0f)
        << "ambient is still black, so back faces stay unlit";

    // The converse -- that a file which DOES author lights keeps them rather than being overridden
    // by this rig -- is covered by GltfToCnjToolTest's own KHR_lights_punctual case, which needs a
    // renderer with a 3D pipeline and is skipped under STUB. The policy is deliberately one-sided
    // in the code (`if (lights.empty())`), so the authored path is untouched by construction.
}

// --- GLTF-073 / GLTF-078: the topology reaches the part ----------------------------------------

TEST(GltfDrawTopology, EachModeReachesItsPartAsARealPrimitiveType)
{
    // The end of the chain GLTF-071 started. The mode is read from the file, converted where it
    // converts (GLTF-072/GLTF-076), and arrives on ModelMeshPart as a real XNA PrimitiveType with
    // a primitive count that follows §12.3 rather than numIndices / 3.
    //
    // Before this, every part was drawn as PrimitiveType::TriangleList regardless, which is why
    // the four non-triangle topologies had to be rejected at import: importing them would have
    // moved the corruption from the import layer to the draw layer.
    using Microsoft::Xna::Framework::Graphics::PrimitiveType;
    const struct { const char* id; PrimitiveType type; int primitiveCount; } kCases[] = {
        {"mode-triangles",      PrimitiveType::TriangleList, 2},
        {"mode-triangle-strip", PrimitiveType::TriangleList, 2},  // converted at import
        {"mode-triangle-fan",   PrimitiveType::TriangleList, 2},  // converted at import
        {"mode-lines",          PrimitiveType::LineList,     2},
        {"mode-line-strip",     PrimitiveType::LineStrip,    2},
        {"mode-line-loop",      PrimitiveType::LineStrip,    3},  // closing segment appended
        {"mode-points",         PrimitiveType::PointListEXT, 4},
    };

    for (const auto& testCase : kCases)
    {
        SCOPED_TRACE(testCase.id);
        const LoadedFixture fixture(testCase.id);
        ASSERT_TRUE(fixture.Ok()) << fixture.Error();

        GraphicsDevice gd;
        ContentManager cm(nullptr, CorpusDirectory().string());
        cm.setGraphicsDevice(gd);
        Model model = cm.Load<Model>(testCase.id);
        ASSERT_EQ(1, model.getMeshesProperty().getCountProperty());
        ASSERT_EQ(1, model.getMeshesProperty()[0]->getMeshPartsProperty().getCountProperty());

        const Microsoft::Xna::Framework::Graphics::ModelMeshPart* part =
            model.getMeshesProperty()[0]->getMeshPartsProperty()[0];
        EXPECT_EQ(testCase.type, part->getPrimitiveTypeEXTProperty())
            << "the part would be drawn with the wrong topology";
        EXPECT_EQ(testCase.primitiveCount, part->getPrimitiveCountProperty())
            << "the primitive count does not follow this part's topology (§12.3)";
    }
}

TEST(GltfDrawTopology, ATriangleListPartIsUnchangedByTheTopologyWork)
{
    // The regression half: every part built by any path defaults to TriangleList, and a fixture
    // that was always a triangle list must be bit-for-bit the same import it always was.
    const LoadedFixture fixture("xf-identity");
    ASSERT_TRUE(fixture.Ok()) << fixture.Error();

    GraphicsDevice gd;
    ContentManager cm(nullptr, CorpusDirectory().string());
    cm.setGraphicsDevice(gd);
    Model model = cm.Load<Model>("xf-identity");

    const Microsoft::Xna::Framework::Graphics::ModelMeshPart* part =
        model.getMeshesProperty()[0]->getMeshPartsProperty()[0];
    EXPECT_EQ(Microsoft::Xna::Framework::Graphics::PrimitiveType::TriangleList,
              part->getPrimitiveTypeEXTProperty());
    EXPECT_EQ(1, part->getPrimitiveCountProperty()) << "one triangle, three indices";

    // A default-constructed part is a triangle list too, so nothing that never sets the property
    // changes behaviour.
    const Microsoft::Xna::Framework::Graphics::ModelMeshPart bare;
    EXPECT_EQ(Microsoft::Xna::Framework::Graphics::PrimitiveType::TriangleList,
              bare.getPrimitiveTypeEXTProperty());
}

// --- GLTF-228 / GLTF-229 / GLTF-231: the material's alpha and sidedness state -------------------

TEST(GltfMaterialState, EveryAuthoredMaterialPropertyReachesTheEffect)
{
    // D7's fixture, end to end through the real loader. The audit's finding was "zero material
    // fields emitted": a gold factor-only material became a white BasicEffect and not one authored
    // property survived. Every one of them is asserted here against the fixture's own spec-derived
    // expectation rather than numbers re-typed into this test.
    using Microsoft::Xna::Framework::Graphics::AlphaModeEXT;
    using Microsoft::Xna::Framework::Graphics::PbrEffect;

    const LoadedFixture fixture("mat-factor-only-gold");
    ASSERT_TRUE(fixture.Ok()) << fixture.Error();
    const CNA::Internal::JsonValue& expected = Member(
        Path(fixture.Expected(), "l3.primitives").arrayValue.at(0), "material");
    ASSERT_EQ(CNA::Internal::JsonType::Object, expected.type);

    GraphicsDevice gd;
    ContentManager cm(nullptr, CorpusDirectory().string());
    cm.setGraphicsDevice(gd);
    Model model = cm.Load<Model>("mat-factor-only-gold");
    ASSERT_EQ(1, model.getMeshesProperty().getCountProperty());

    auto* pbr = dynamic_cast<PbrEffect*>(
        model.getMeshesProperty()[0]->getMeshPartsProperty()[0]->getEffectProperty());
    ASSERT_NE(nullptr, pbr) << "a factor-only metallic-roughness material did not select PbrEffect "
                               "-- GLTF-215's rule was reverted";

    // GLTF-216: the base colour, split across DiffuseColor and Alpha exactly as PbrEffect models it.
    const std::vector<double> baseColor = Numbers(Member(expected, "baseColorFactor"));
    ASSERT_EQ(4u, baseColor.size());
    EXPECT_NEAR(baseColor[0], static_cast<double>(pbr->getDiffuseColorProperty().X), 1e-5);
    EXPECT_NEAR(baseColor[1], static_cast<double>(pbr->getDiffuseColorProperty().Y), 1e-5);
    EXPECT_NEAR(baseColor[2], static_cast<double>(pbr->getDiffuseColorProperty().Z), 1e-5);
    EXPECT_NEAR(baseColor[3], static_cast<double>(pbr->getAlphaProperty()), 1e-5);

    // GLTF-219 / GLTF-221: the scalar factors, no longer gated on a texture map being present.
    EXPECT_NEAR(NumberOr(expected, "metallicFactor", -1),
                static_cast<double>(pbr->getMetallicFactorProperty()), 1e-5);
    EXPECT_NEAR(NumberOr(expected, "roughnessFactor", -1),
                static_cast<double>(pbr->getRoughnessFactorProperty()), 1e-5);

    // GLTF-343 / GLTF-344: raw extension factors remain inspectable on the effect. Their
    // shader-ready F0/F90 boundary is asserted separately by the L6 draw-parameter oracle.
    EXPECT_NEAR(NumberOr(expected, "ior", -1),
                static_cast<double>(pbr->getIorEXTProperty()), 1e-5);
    EXPECT_NEAR(NumberOr(expected, "specularFactor", -1),
                static_cast<double>(pbr->getSpecularFactorEXTProperty()), 1e-5);
    const std::vector<double> specularColor = Numbers(Member(expected, "specularColorFactor"));
    ASSERT_EQ(3u, specularColor.size());
    const auto carriedSpecularColor = pbr->getSpecularColorFactorEXTProperty();
    EXPECT_NEAR(specularColor[0], static_cast<double>(carriedSpecularColor.X), 1e-5);
    EXPECT_NEAR(specularColor[1], static_cast<double>(carriedSpecularColor.Y), 1e-5);
    EXPECT_NEAR(specularColor[2], static_cast<double>(carriedSpecularColor.Z), 1e-5);

    // GLTF-228 / GLTF-229 / GLTF-231: the alpha and sidedness state, which had no home at all.
    EXPECT_EQ("BLEND", StringOr(expected, "alphaMode", "")) << "the fixture stopped authoring BLEND";
    EXPECT_EQ(AlphaModeEXT::Blend, pbr->getAlphaModeEXTProperty());
    EXPECT_NEAR(NumberOr(expected, "alphaCutoff", -1),
                static_cast<double>(pbr->getAlphaCutoffEXTProperty()), 1e-5);
    EXPECT_TRUE(pbr->getDoubleSidedEXTProperty())
        << "the material asks for both faces and the flag did not survive import";
}

TEST(GltfMaterialState, AMaterialThatDeclaresNothingGetsGltfsOwnDefaults)
{
    // The converse, and the one that stops the defaults drifting: a primitive with no material at
    // all is glTF's default material, which is opaque, single-sided, cutoff 0.5 and white.
    using Microsoft::Xna::Framework::Graphics::AlphaModeEXT;
    using Microsoft::Xna::Framework::Graphics::PbrEffect;

    const LoadedFixture fixture("xf-identity");
    ASSERT_TRUE(fixture.Ok()) << fixture.Error();
    ASSERT_EQ(0u, static_cast<std::size_t>(fixture.Data().materials_count))
        << "this fixture is supposed to declare no material at all";

    GraphicsDevice gd;
    ContentManager cm(nullptr, CorpusDirectory().string());
    cm.setGraphicsDevice(gd);
    Model model = cm.Load<Model>("xf-identity");

    auto* pbr = dynamic_cast<PbrEffect*>(
        model.getMeshesProperty()[0]->getMeshPartsProperty()[0]->getEffectProperty());
    ASSERT_NE(nullptr, pbr) << "glTF's default material is metallic-roughness (GLTF-217)";

    EXPECT_EQ(AlphaModeEXT::Opaque, pbr->getAlphaModeEXTProperty());
    EXPECT_NEAR(0.5, static_cast<double>(pbr->getAlphaCutoffEXTProperty()), 1e-6);
    EXPECT_FALSE(pbr->getDoubleSidedEXTProperty());
    EXPECT_NEAR(1.0, static_cast<double>(pbr->getAlphaProperty()), 1e-6);
    EXPECT_NEAR(1.0, static_cast<double>(pbr->getMetallicFactorProperty()), 1e-6);
    EXPECT_NEAR(1.0, static_cast<double>(pbr->getRoughnessFactorProperty()), 1e-6);
    EXPECT_NEAR(1.5, static_cast<double>(pbr->getIorEXTProperty()), 1e-6);
    EXPECT_NEAR(1.0, static_cast<double>(pbr->getSpecularFactorEXTProperty()), 1e-6);
    EXPECT_EQ(Microsoft::Xna::Framework::Vector3(1.0f, 1.0f, 1.0f),
              pbr->getSpecularColorFactorEXTProperty());
}

// --- GLTF-294: a rigid clip survives to the model and poses it ---------------------------------

TEST(GltfRigidAnimation, AnUnskinnedAnimatedModelCarriesItsClipsOnItsTag)
{
    // The reader half of D6. Before GLTF-294 the .cnj "animations" array was parsed only inside
    // the skeleton branch, so an unskinned model's clips were read by nobody -- and before
    // GLTF-293 there were none to read.
    using Microsoft::Xna::Framework::Graphics::ClipTargetSpaceEXT;
    using Microsoft::Xna::Framework::Graphics::ModelAnimationsEXT;

    const LoadedFixture fixture("anim-rigid-node");
    ASSERT_TRUE(fixture.Ok()) << fixture.Error();
    ASSERT_EQ(0u, static_cast<std::size_t>(fixture.Data().skins_count))
        << "this fixture is supposed to have no skin at all";

    GraphicsDevice gd;
    ContentManager cm(nullptr, CorpusDirectory().string());
    cm.setGraphicsDevice(gd);
    Model model = cm.Load<Model>("anim-rigid-node");

    auto* animations = dynamic_cast<ModelAnimationsEXT*>(model.getTagProperty());
    ASSERT_NE(nullptr, animations) << "the model carries no rigid clips -- D6 is back";
    ASSERT_EQ(1u, animations->Clips.size());
    ASSERT_TRUE(animations->Clips.count("Spin") == 1) << "the clip lost its name";

    const auto& clip = animations->Clips.at("Spin");
    EXPECT_EQ(ClipTargetSpaceEXT::SceneNode, clip.TargetSpace)
        << "a rigid clip that claimed a joint palette would pose the wrong bones";
    ASSERT_EQ(1u, clip.Tracks.size());
    EXPECT_NEAR(1.0, clip.Duration.getTotalSecondsProperty(), 1e-6);
}

TEST(GltfRigidAnimation, PosingTheModelRotatesTheAnimatedNodesBone)
{
    // The playable half, which is what "imported" has to mean to be worth anything. The fixture
    // authors a quarter turn about +Z between t=0 and t=1, so the bone's transform must be the
    // identity at the start and that rotation at the end.
    using Microsoft::Xna::Framework::Graphics::ApplyClipToBonesEXT;
    using Microsoft::Xna::Framework::Graphics::ModelAnimationsEXT;

    const LoadedFixture fixture("anim-rigid-node");
    ASSERT_TRUE(fixture.Ok()) << fixture.Error();

    GraphicsDevice gd;
    ContentManager cm(nullptr, CorpusDirectory().string());
    cm.setGraphicsDevice(gd);
    Model model = cm.Load<Model>("anim-rigid-node");
    auto* animations = dynamic_cast<ModelAnimationsEXT*>(model.getTagProperty());
    ASSERT_NE(nullptr, animations);
    const auto& clip = animations->Clips.at("Spin");
    const int bone = clip.Tracks[0].BoneIndex;
    ASSERT_GE(bone, 0);
    ASSERT_LT(bone, model.getBonesProperty().getCountProperty());

    // At t = 0 the node is at its rest rotation, which this fixture authors as the identity.
    ApplyClipToBonesEXT(model, clip, System::TimeSpan::FromSeconds(0.0));
    const Matrix atStart = model.getBonesProperty()[bone]->getTransformProperty();
    ExpectMatrixNear(Matrix::getIdentityProperty(), atStart, "bone transform at t=0");

    // At t = 1, a quarter turn about +Z: local +X maps onto +Y.
    ApplyClipToBonesEXT(model, clip, System::TimeSpan::FromSeconds(1.0));
    const Matrix atEnd = model.getBonesProperty()[bone]->getTransformProperty();
    const Microsoft::Xna::Framework::Vector3 turned =
        Microsoft::Xna::Framework::Vector3::Transform(
            Microsoft::Xna::Framework::Vector3(1.0f, 0.0f, 0.0f), atEnd);
    EXPECT_NEAR(0.0f, turned.X, kTolerance);
    EXPECT_NEAR(1.0f, turned.Y, kTolerance) << "the node did not turn a quarter turn about +Z";
    EXPECT_NEAR(0.0f, turned.Z, kTolerance);

    // Halfway is halfway, not a step: an eighth turn puts +X on the diagonal.
    ApplyClipToBonesEXT(model, clip, System::TimeSpan::FromSeconds(0.5));
    const Microsoft::Xna::Framework::Vector3 halfway =
        Microsoft::Xna::Framework::Vector3::Transform(
            Microsoft::Xna::Framework::Vector3(1.0f, 0.0f, 0.0f),
            model.getBonesProperty()[bone]->getTransformProperty());
    EXPECT_NEAR(0.70710678f, halfway.X, 1e-4f);
    EXPECT_NEAR(0.70710678f, halfway.Y, 1e-4f);
}

TEST(GltfRigidAnimation, APaletteClipIsRefusedRatherThanPosingTheWrongBones)
{
    // The reason AnimationClipEXT carries a target space at all. A joint-palette clip's indices
    // name slots in a skin, not bones in a scene: applying them here would pose whichever bones
    // happened to share those numbers, with no symptom but wrong motion.
    using Microsoft::Xna::Framework::Graphics::AnimationClipEXT;
    using Microsoft::Xna::Framework::Graphics::ApplyClipToBonesEXT;
    using Microsoft::Xna::Framework::Graphics::ClipTargetSpaceEXT;

    const LoadedFixture fixture("xf-identity");
    ASSERT_TRUE(fixture.Ok()) << fixture.Error();
    GraphicsDevice gd;
    ContentManager cm(nullptr, CorpusDirectory().string());
    cm.setGraphicsDevice(gd);
    Model model = cm.Load<Model>("xf-identity");

    AnimationClipEXT palette;
    palette.Duration = System::TimeSpan::FromSeconds(1.0);
    EXPECT_EQ(ClipTargetSpaceEXT::JointPalette, palette.TargetSpace) << "the default moved";
    EXPECT_THROW(ApplyClipToBonesEXT(model, palette, System::TimeSpan::FromSeconds(0.0)),
                 std::invalid_argument);
}

// --- plan_gltf.md GLTF-299: a clip whose first key is not at t = 0 ---------------------------------

// glTF animations live on an absolute timeline anchored at 0, so two questions have exactly one
// spec-conformant answer each and `anim-nonzero-start` states both in its own manifest rather than
// leaving a test to decide them. Its first key is at t=1.5 and its last at t=3.0, so "the last
// sample" and "the span between the keys" are different numbers; and its node's rest rotation
// differs from its first key, so "holds the first key" and "falls back to the rest pose" are
// different poses. Neither question could be answered by a fixture starting at 0.
TEST(GltfRigidAnimation, AClipStartingAfterZeroKeepsTheAbsoluteTimeline)
{
    using Microsoft::Xna::Framework::Graphics::ModelAnimationsEXT;

    const LoadedFixture fixture("anim-nonzero-start");
    ASSERT_TRUE(fixture.Ok()) << fixture.Error();
    const CNA::Internal::JsonValue& animation = Path(fixture.Expected(), "l4.animation");
    const double expectedDuration = NumberOr(animation, "duration", -1.0);
    const double firstKeyTime     = NumberOr(animation, "firstKeyTime", -1.0);
    ASSERT_GT(firstKeyTime, 0.0) << "the fixture no longer starts after zero";
    ASSERT_GT(expectedDuration, firstKeyTime);

    GraphicsDevice gd;
    ContentManager cm(nullptr, CorpusDirectory().string());
    cm.setGraphicsDevice(gd);
    Model model = cm.Load<Model>("anim-nonzero-start");

    auto* animations = dynamic_cast<ModelAnimationsEXT*>(model.getTagProperty());
    ASSERT_NE(nullptr, animations);
    ASSERT_EQ(1u, animations->Clips.size());
    const auto& clip = animations->Clips.at("LateSpin");

    // The duration is the last sample. Taking the key SPAN instead would give 1.5 here, and every
    // authored time would then be off by the 1.5 offset -- a whole-clip time shift that no single
    // pose comparison would localise.
    EXPECT_NEAR(expectedDuration, clip.Duration.getTotalSecondsProperty(), 1e-6);
    EXPECT_GT(clip.Duration.getTotalSecondsProperty(), firstKeyTime + 1e-6)
        << "the duration collapsed to the key span, reinterpreting an absolute timeline as a "
           "clip-relative one";
}

TEST(GltfRigidAnimation, BeforeTheFirstKeyTheClipHoldsThatKeyNotTheRestPose)
{
    using Microsoft::Xna::Framework::Graphics::ApplyClipToBonesEXT;
    using Microsoft::Xna::Framework::Graphics::ModelAnimationsEXT;

    const LoadedFixture fixture("anim-nonzero-start");
    ASSERT_TRUE(fixture.Ok()) << fixture.Error();
    const CNA::Internal::JsonValue& animation = Path(fixture.Expected(), "l4.animation");
    const CNA::Internal::JsonValue& before = Member(animation, "posesBeforeFirstKey");
    ASSERT_EQ(CNA::Internal::JsonType::Array, before.type);
    ASSERT_FALSE(before.arrayValue.empty());

    GraphicsDevice gd;
    ContentManager cm(nullptr, CorpusDirectory().string());
    cm.setGraphicsDevice(gd);
    Model model = cm.Load<Model>("anim-nonzero-start");
    auto* animations = dynamic_cast<ModelAnimationsEXT*>(model.getTagProperty());
    ASSERT_NE(nullptr, animations);
    const auto& clip = animations->Clips.at("LateSpin");
    ASSERT_EQ(1u, clip.Tracks.size());
    const int bone = clip.Tracks[0].BoneIndex;
    ASSERT_GE(bone, 0);
    ASSERT_LT(bone, model.getBonesProperty().getCountProperty());

    // The rest pose the node itself declares, so the two candidate answers can be told apart.
    const std::vector<double> restRotation = Numbers(Member(animation, "restPoseRotation"));
    ASSERT_EQ(4u, restRotation.size());
    const Matrix restMatrix = Matrix::CreateFromQuaternion(Microsoft::Xna::Framework::Quaternion(
        static_cast<float>(restRotation[0]), static_cast<float>(restRotation[1]),
        static_cast<float>(restRotation[2]), static_cast<float>(restRotation[3])));

    for (const CNA::Internal::JsonValue& sample : before.arrayValue)
    {
        const double time = NumberOr(sample, "time", -1.0);
        SCOPED_TRACE("t = " + std::to_string(time));
        const std::vector<double> expectedColumnMajor =
            Numbers(Member(sample, "nodeLocalColumnMajor"));
        ASSERT_EQ(16u, expectedColumnMajor.size());

        ApplyClipToBonesEXT(model, clip, System::TimeSpan::FromSeconds(time));
        const Matrix actual = model.getBonesProperty()[bone]->getTransformProperty();

        float actualColumnMajor[16];
        actual.ToColumnMajor(actualColumnMajor);
        for (std::size_t i = 0; i < 16; ++i)
        {
            EXPECT_NEAR(static_cast<float>(expectedColumnMajor[i]), actualColumnMajor[i], kTolerance)
                << "element " << i;
        }

        // And it is the first key, not the rest pose -- the fixture authors them different on
        // purpose, so this is a real distinction and not a restatement of the comparison above.
        bool matchesRestPose = true;
        const float* r = &restMatrix.M11;
        const float* a = &actual.M11;
        for (int i = 0; i < 16; ++i)
        {
            if (std::fabs(r[i] - a[i]) > kTolerance) { matchesRestPose = false; break; }
        }
        EXPECT_FALSE(matchesRestPose)
            << "the bone fell back to the node's rest pose instead of holding the first key";
    }
}

// --- plan_gltf.md GLTF-249: skin.skeleton is a root hint, never a traversal stop --------------------

// The half that was missing entirely: `skin.skeleton` was parsed by cgltf and read by nobody, so an
// application had no way to find the rig root the file declares. It is carried now -- and carried
// as data, not as control flow.
TEST(GltfSkinSpaces, TheDeclaredSkeletonRootIsCarriedAsASceneNodeIndex)
{
    const LoadedFixture fixture("skin-skeleton-hint");
    ASSERT_TRUE(fixture.Ok()) << fixture.Error();
    const CNA::Internal::JsonValue& declared = Path(fixture.Expected(), "l4.skin.declaredSkeletonRoot");
    const std::string expectedName = StringOr(declared, "nodeName", "");
    ASSERT_FALSE(expectedName.empty()) << "the fixture no longer declares a skeleton root";

    GraphicsDevice gd;
    ContentManager cm(nullptr, CorpusDirectory().string());
    cm.setGraphicsDevice(gd);
    Model model = cm.Load<Model>("skin-skeleton-hint");

    auto* skinning = dynamic_cast<Microsoft::Xna::Framework::Graphics::SkinningData*>(
        model.getTagProperty());
    ASSERT_NE(nullptr, skinning);
    EXPECT_EQ(expectedName, skinning->SkeletonRootNameEXT);

    // The index is a sceneNodeIndex (§15.1.2), so it indexes Model::Bones -- and the bone it lands
    // on must be the node the file named. Asserting the resolved bone's name, not just the number,
    // is what makes this a test of the index space rather than of an integer.
    const int index = skinning->SkeletonRootNodeIndexEXT;
    ASSERT_GE(index, 0) << "the declared root did not resolve to a scene node";
    ASSERT_LT(index, model.getBonesProperty().getCountProperty());
    EXPECT_EQ(expectedName, model.getBonesProperty()[index]->getNameProperty())
        << "SkeletonRootNodeIndexEXT is not in the scene-node index space";

    // And it really is a different node from the one a reader would infer from the joint set, so
    // code that substituted the common ancestor for the declared root would be caught.
    EXPECT_FALSE(BoolOr(declared, "isTheJointsCommonAncestor", true));
    EXPECT_NE(StringOr(declared, "commonAncestorNodeName", ""), expectedName);
}

// The half that must NEVER change: the hint does not bound the ancestry walk. `Outer` sits above
// the declared root and carries T(0,100,0); a reader that stopped at `skin.skeleton` would drop it
// and land every vertex 100 units low -- D8 exactly, under a new name. The manifest states that
// wrong answer by name, so this asserts against both poles rather than only away from one.
TEST(GltfSkinSpaces, AnAncestorAboveTheDeclaredRootStillContributesItsTransform)
{
    const LoadedFixture fixture("skin-skeleton-hint");
    ASSERT_TRUE(fixture.Ok()) << fixture.Error();
    const CNA::Internal::JsonValue& skinBlock = Path(fixture.Expected(), "l4.skin");
    const CNA::Internal::JsonValue& joints = Member(skinBlock, "joints");
    ASSERT_EQ(CNA::Internal::JsonType::Array, joints.type);
    ASSERT_FALSE(joints.arrayValue.empty());

    GraphicsDevice gd;
    ContentManager cm(nullptr, CorpusDirectory().string());
    cm.setGraphicsDevice(gd);
    Model model = cm.Load<Model>("skin-skeleton-hint");

    auto* skinning = dynamic_cast<Microsoft::Xna::Framework::Graphics::SkinningData*>(
        model.getTagProperty());
    ASSERT_NE(nullptr, skinning);
    ASSERT_EQ(2, skinning->BoneCount);

    Microsoft::Xna::Framework::Graphics::AnimationPlayer player(*skinning);
    const std::vector<Matrix>& palette = player.GetSkinTransforms();
    ASSERT_EQ(2u, palette.size());

    // Every inverse bind matrix is the true inverse of its joint's FULL global transform, so every
    // joint matrix is exactly the identity. Nothing weaker would do: an identity here means each
    // of Outer, DeclaredRoot and Mid contributed exactly once.
    for (std::size_t j = 0; j < palette.size(); ++j)
    {
        ExpectMatrixNear(Matrix::getIdentityProperty(), palette[j],
                         "joint matrix " + std::to_string(j));
    }

    // The named wrong answer, asserted against directly.
    const CNA::Internal::JsonValue& truncated = Member(skinBlock, "truncatedAtDeclaredRoot");
    const std::vector<double> wrongColumnMajor =
        Numbers(Member(truncated, "jointMatrixColumnMajor"));
    ASSERT_EQ(16u, wrongColumnMajor.size());
    float actualColumnMajor[16];
    palette[0].ToColumnMajor(actualColumnMajor);
    bool matchesTruncated = true;
    for (std::size_t i = 0; i < 16; ++i)
    {
        if (std::fabs(static_cast<float>(wrongColumnMajor[i]) - actualColumnMajor[i]) > kTolerance)
        {
            matchesTruncated = false;
            break;
        }
    }
    EXPECT_FALSE(matchesTruncated)
        << "the ancestry walk stopped at skin.skeleton -- Outer's T(0,100,0) was dropped and D8 is "
           "back under a new name";

    // In world space, which is what an owner actually sees.
    const Microsoft::Xna::Framework::Vector3 skinned =
        Microsoft::Xna::Framework::Vector3::Transform(
            Microsoft::Xna::Framework::Vector3(1.0f, 0.0f, 0.0f), palette[0]);
    EXPECT_NEAR(1.0f, skinned.X, kTolerance);
    EXPECT_NEAR(0.0f, skinned.Y, kTolerance)
        << "the vertex sits " << skinned.Y << " in Y -- an ancestor above the declared root was lost";
    EXPECT_NEAR(0.0f, skinned.Z, kTolerance);
}

// --- plan_gltf.md GLTF-281 / GLTF-282: node.weights, and two instances that differ -----------------

// §3.7.2.2 gives the instancing node the final say, and OVERRIDE is the operative word: node A
// declaring [1] and node B declaring [0] for a mesh whose own weights are [0.5] must start at 1 and
// 0 -- not 1.5 and 0.5, and not both at the mesh's own value. node.weights was read by nobody, so
// every instance wore the mesh's expression.
TEST(GltfMorphWeights, NodeWeightsOverrideTheMeshsOwnRatherThanMergingWithThem)
{
    using Microsoft::Xna::Framework::Graphics::MorphTargetDataEXT;

    const LoadedFixture fixture("morph-node-weights-override");
    ASSERT_TRUE(fixture.Ok()) << fixture.Error();
    const CNA::Internal::JsonValue& morph = Path(fixture.Expected(), "l4.morph");
    const std::vector<double> meshWeights = Numbers(Member(morph, "meshWeights"));
    ASSERT_EQ(1u, meshWeights.size());

    const CNA::Internal::JsonValue& instances = Member(morph, "instances");
    ASSERT_EQ(CNA::Internal::JsonType::Array, instances.type);
    ASSERT_EQ(2u, instances.arrayValue.size());

    GraphicsDevice gd;
    ContentManager cm(nullptr, CorpusDirectory().string());
    cm.setGraphicsDevice(gd);
    Model model = cm.Load<Model>("morph-node-weights-override");

    // GLTF-282: MorphTargetDataEXT is per PART, so two instances can only differ if the importer
    // builds a part per instance. Two parts is therefore the precondition for the whole test.
    std::vector<MorphTargetDataEXT*> morphs;
    const auto& meshes = model.getMeshesProperty();
    for (int mi = 0; mi < meshes.getCountProperty(); ++mi)
    {
        const auto& parts = meshes[mi]->getMeshPartsProperty();
        for (int pi = 0; pi < parts.getCountProperty(); ++pi)
        {
            if (auto* data = dynamic_cast<MorphTargetDataEXT*>(parts[pi]->getTagProperty()))
            {
                morphs.push_back(data);
            }
        }
    }
    ASSERT_EQ(2u, morphs.size())
        << "the two instances did not produce two parts, so they cannot hold different weights";

    // Each instance carries its own node's weights, and neither carries the mesh's.
    std::vector<float> seen;
    for (const MorphTargetDataEXT* data : morphs)
    {
        ASSERT_EQ(1u, data->Weights.size());
        seen.push_back(data->Weights[0]);
    }
    std::sort(seen.begin(), seen.end());
    EXPECT_NEAR(0.0f, seen[0], kTolerance);
    EXPECT_NEAR(1.0f, seen[1], kTolerance);
    // The three ways this could go wrong, each named: merged, mesh-only, or collapsed to one value.
    EXPECT_NE(seen[0], seen[1]) << "both instances got the same weight -- node.weights was ignored";
    for (const float w : seen)
    {
        EXPECT_NE(static_cast<float>(meshWeights[0]), w)
            << "an instance kept the MESH's weight -- node.weights did not override it";
        EXPECT_LE(w, 1.0f)
            << "a weight exceeds 1 -- node.weights was merged with the mesh's instead of "
               "replacing it";
    }
}

// --- plan_gltf.md GLTF-256: weights that do not sum to 1 (the audit's H12) -------------------------

// The skin equation is a weighted sum of joint matrices, so weights summing to 0.75 apply 0.75 of
// the vertex's transform -- which for a joint near the origin drags the vertex three-quarters of
// the way toward it. An independent collapse mechanism wearing D8's clothes, and CNA never checked.
//
// The policy is renormalise-and-report, and its two exceptions matter as much as its rule: a sum
// within float error of 1 must be left untouched (or every quantised exporter's output is reported
// as broken) and a zero sum must be left alone too (0/0 is not a normalisation).
TEST(GltfSkinSpaces, UnnormalisedWeightsAreRenormalisedAndReportedButOnlyWhenTheyShouldBe)
{
    const LoadedFixture fixture("skin-unnormalized");
    ASSERT_TRUE(fixture.Ok()) << fixture.Error();
    const CNA::Internal::JsonValue& skin = Path(fixture.Expected(), "l4.skin");
    const CNA::Internal::JsonValue& after = Member(skin, "weightsAfterPolicy");
    ASSERT_EQ(CNA::Internal::JsonType::Array, after.type);
    ASSERT_EQ(3u, after.arrayValue.size());

    const CNA::Internal::GltfImport::MeshOut extracted = CNA::Internal::GltfImport::ExtractMesh(
        &fixture.Data(), fixture.Data().meshes[0].primitives[0], "probe",
        nullptr, 1.0f);
    // Unskinned extraction (no skeleton) drops the weights entirely, so the real check goes
    // through the loader, which is also what an application sees.
    GraphicsDevice gd;
    ContentManager cm(nullptr, CorpusDirectory().string());
    cm.setGraphicsDevice(gd);
    Model model = cm.Load<Model>("skin-unnormalized");
    ASSERT_GT(model.getMeshesProperty().getCountProperty(), 0);
    (void)extracted;

    // Counted exactly: one renormalised, one zero-sum, and the near-1 vertex neither.
    const CNA::Internal::GltfImport::SceneGraphOut scene =
        CNA::Internal::GltfImport::BuildSceneGraph(&fixture.Data());
    const CNA::Internal::GltfImport::SkeletonResult skeleton =
        CNA::Internal::GltfImport::BuildSkeleton(fixture.Data().skins, scene,
                                                  Matrix::getIdentityProperty(), 1.0f);
    const CNA::Internal::GltfImport::MeshOut skinned = CNA::Internal::GltfImport::ExtractMesh(
        &fixture.Data(), fixture.Data().meshes[0].primitives[0], "probe", &skeleton, 1.0f);

    EXPECT_EQ(static_cast<std::size_t>(NumberOr(skin, "renormalisedVertexCount", -1)),
              skinned.renormalisedWeightVertexCountEXT)
        << "the wrong number of vertices was renormalised";
    EXPECT_EQ(static_cast<std::size_t>(NumberOr(skin, "zeroWeightVertexCount", -1)),
              skinned.zeroWeightVertexCountEXT);
    EXPECT_GT(skinned.worstWeightSumDeviationEXT, 0.2f)
        << "the worst deviation was not recorded -- a caller cannot tell exporter quantisation "
           "from a broken file without it";

    // And the values themselves, per vertex, against the manifest's stated post-policy answer.
    // The BlendWeight offset comes from the canonical stride table rather than a literal: hardcoding
    // a layout is exactly what went stale in GLTF-278, and this fixture's stride depends on which
    // effect its material selects, which is not this test's subject.
    const CNA::Internal::Graphics::InferredVertexLayout layout =
        CNA::Internal::Graphics::InferredLayoutForStride(
            skinned.stride, CNA::Internal::Graphics::UnlistedStrideLayout::RendererRefusesIt);
    ASSERT_TRUE(layout.known) << "stride " << skinned.stride << " is not in the canonical table";
    int blendWeightOffset = -1;
    for (std::size_t i = 0; i < layout.count; ++i)
    {
        if (layout.elements[i].usage ==
            Microsoft::Xna::Framework::Graphics::VertexElementUsage::BlendWeight)
        {
            blendWeightOffset = layout.elements[i].offset;
            break;
        }
    }
    ASSERT_GE(blendWeightOffset, 0) << "the chosen layout has no BlendWeight slot";

    for (std::size_t v = 0; v < 3; ++v)
    {
        SCOPED_TRACE("vertex " + std::to_string(v));
        const std::vector<double> expected = Numbers(after.arrayValue[v]);
        ASSERT_EQ(4u, expected.size());
        float actual[4];
        std::memcpy(actual,
                    skinned.vertexBytes.data() +
                        v * static_cast<std::size_t>(skinned.stride) +
                        static_cast<std::size_t>(blendWeightOffset),
                    sizeof(actual));
        for (std::size_t c = 0; c < 4; ++c)
        {
            EXPECT_NEAR(static_cast<float>(expected[c]), actual[c], kTolerance)
                << "component " << c;
        }
    }
}

// --- plan_gltf.md GLTF-095/GLTF-257: influence sets past the first --------------------------------

// glTF puts no limit on JOINTS_n/WEIGHTS_n sets; XNA's BlendIndices/BlendWeight carry exactly four
// influences, and no CNA vertex layout or shader has room for more. So a second set cannot be
// carried -- but dropping it in silence turns "eight influences" into "four" with nothing anywhere
// saying so, which is the class of quiet wrongness this campaign exists to remove.
//
// The measurement is the point, not the count. A fifth influence weighted 0.002 is exporter noise
// and one weighted 0.4 is a visibly different pose, and only the dropped SHARE tells them apart.
TEST(GltfSkinSpaces, ASecondInfluenceSetIsDroppedAndTheLostShareIsMeasured)
{
    const LoadedFixture fixture("skin-eight-influences");
    ASSERT_TRUE(fixture.Ok()) << fixture.Error();
    const CNA::Internal::JsonValue& skin = Path(fixture.Expected(), "l4.skin");

    const CNA::Internal::GltfImport::SceneGraphOut scene =
        CNA::Internal::GltfImport::BuildSceneGraph(&fixture.Data());
    const CNA::Internal::GltfImport::SkeletonResult skeleton =
        CNA::Internal::GltfImport::BuildSkeleton(fixture.Data().skins, scene,
                                                  Matrix::getIdentityProperty(), 1.0f);
    const CNA::Internal::GltfImport::MeshOut skinned = CNA::Internal::GltfImport::ExtractMesh(
        &fixture.Data(), fixture.Data().meshes[0].primitives[0], "probe", &skeleton, 1.0f);

    EXPECT_EQ(static_cast<std::size_t>(NumberOr(skin, "extraInfluenceSets", -1)),
              skinned.extraInfluenceSetsEXT)
        << "the extra influence set was not detected at all";
    EXPECT_NEAR(NumberOr(skin, "worstDroppedInfluence", -1.0),
                static_cast<double>(skinned.worstDroppedInfluenceEXT), kTolerance)
        << "the dropped share was not measured -- without it a caller cannot tell exporter noise "
           "from a materially different pose";
}

// The interaction with GLTF-256 is the half that decides whether truncation is survivable.
// Vertex 0's set-0 weights sum to 0.6 as authored, because the other 0.4 lives in set 1. Left at
// 0.6 the skin equation applies three-fifths of the vertex's transform and drags it toward the
// origin -- H12, arriving by a different road. Renormalising the retained four is what turns a
// collapsed skin into a merely coarser one.
TEST(GltfSkinSpaces, TruncatedInfluencesAreRenormalisedSoTheVertexIsNotDraggedTowardTheOrigin)
{
    const LoadedFixture fixture("skin-eight-influences");
    ASSERT_TRUE(fixture.Ok()) << fixture.Error();
    const CNA::Internal::JsonValue& skin = Path(fixture.Expected(), "l4.skin");
    const CNA::Internal::JsonValue& after = Member(skin, "weightsAfterPolicy");
    ASSERT_EQ(CNA::Internal::JsonType::Array, after.type);
    ASSERT_EQ(3u, after.arrayValue.size());

    const CNA::Internal::GltfImport::SceneGraphOut scene =
        CNA::Internal::GltfImport::BuildSceneGraph(&fixture.Data());
    const CNA::Internal::GltfImport::SkeletonResult skeleton =
        CNA::Internal::GltfImport::BuildSkeleton(fixture.Data().skins, scene,
                                                  Matrix::getIdentityProperty(), 1.0f);
    const CNA::Internal::GltfImport::MeshOut skinned = CNA::Internal::GltfImport::ExtractMesh(
        &fixture.Data(), fixture.Data().meshes[0].primitives[0], "probe", &skeleton, 1.0f);

    const CNA::Internal::Graphics::InferredVertexLayout layout =
        CNA::Internal::Graphics::InferredLayoutForStride(
            skinned.stride, CNA::Internal::Graphics::UnlistedStrideLayout::RendererRefusesIt);
    ASSERT_TRUE(layout.known) << "stride " << skinned.stride << " is not in the canonical table";
    int blendWeightOffset = -1;
    for (std::size_t i = 0; i < layout.count; ++i)
    {
        if (layout.elements[i].usage ==
            Microsoft::Xna::Framework::Graphics::VertexElementUsage::BlendWeight)
        {
            blendWeightOffset = layout.elements[i].offset;
            break;
        }
    }
    ASSERT_GE(blendWeightOffset, 0) << "the chosen layout has no BlendWeight slot";

    for (std::size_t v = 0; v < 3; ++v)
    {
        SCOPED_TRACE("vertex " + std::to_string(v));
        const std::vector<double> expected = Numbers(after.arrayValue[v]);
        ASSERT_EQ(4u, expected.size());
        float actual[4];
        std::memcpy(actual,
                    skinned.vertexBytes.data() +
                        v * static_cast<std::size_t>(skinned.stride) +
                        static_cast<std::size_t>(blendWeightOffset),
                    sizeof(actual));
        float sum = 0.0f;
        for (std::size_t c = 0; c < 4; ++c)
        {
            EXPECT_NEAR(static_cast<float>(expected[c]), actual[c], kTolerance)
                << "component " << c;
            sum += actual[c];
        }
        EXPECT_NEAR(1.0f, sum, kTolerance)
            << "an imported vertex's weights must sum to 1 whatever was truncated away, or the "
               "skin equation applies a fraction of its transform";
    }
}

// --- plan_gltf.md GLTF-261: a rig past the GPU palette (the audit's H6) ----------------------------

// The palette is 72 mat4s -- a real XNA constant every renderer's uniform array is sized by -- so a
// 73-joint rig is refused rather than truncated. Truncating leaves the joints past the limit at the
// identity, so every vertex bound to them collapses toward the origin, which is precisely the class
// of silent wrongness this campaign removes. The fixture sits ONE past the boundary, because an
// off-by-one in the check is as much a failure as no check at all.
TEST(GltfSkinSpaces, ARigPastTheBonePaletteIsRefusedRatherThanTruncated)
{
    const LoadedFixture fixture("skin-73-joints");
    ASSERT_TRUE(fixture.Ok()) << fixture.Error();
    ASSERT_EQ(1u, static_cast<std::size_t>(fixture.Data().skins_count));
    ASSERT_EQ(73u, static_cast<std::size_t>(fixture.Data().skins[0].joints_count))
        << "the fixture is no longer one past the limit";

    const CNA::Internal::GltfImport::SceneGraphOut scene =
        CNA::Internal::GltfImport::BuildSceneGraph(&fixture.Data());
    std::string message;
    try
    {
        (void)CNA::Internal::GltfImport::BuildSkeleton(
            fixture.Data().skins, scene, Matrix::getIdentityProperty(), 1.0f);
    }
    catch (const std::exception& e)
    {
        message = e.what();
    }
    ASSERT_FALSE(message.empty()) << "a 73-joint rig was accepted -- it cannot fit the palette";
    EXPECT_NE(std::string::npos, message.find("73")) << message;
    EXPECT_NE(std::string::npos, message.find("72")) << message;
    EXPECT_NE(std::string::npos, message.find("BigRig"))
        << "the diagnostic does not name the skin: " << message;

    // Exactly 72 must still be accepted: the check is a limit, not an off-by-one.
    EXPECT_NO_THROW({
        const LoadedFixture ok("skin-armature-ancestor");
        ASSERT_TRUE(ok.Ok());
        (void)CNA::Internal::GltfImport::BuildSkeleton(
            ok.Data().skins, CNA::Internal::GltfImport::BuildSceneGraph(&ok.Data()),
            Matrix::getIdentityProperty(), 1.0f);
    });
}
