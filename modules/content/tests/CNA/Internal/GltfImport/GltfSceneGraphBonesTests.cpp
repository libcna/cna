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

#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>
#include <string>
#include <vector>

#include "CNA/Internal/GltfImport/GltfImportCore.hpp"
#include "GltfFixtureCorpus.hpp"
#include "Microsoft/Xna/Framework/Content/ContentManager.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/AnimationPlayer.hpp"
#include "Microsoft/Xna/Framework/Graphics/Model.hpp"
#include "Microsoft/Xna/Framework/Graphics/ModelBone.hpp"
#include "Microsoft/Xna/Framework/Graphics/ModelBoneCollection.hpp"
#include "Microsoft/Xna/Framework/Graphics/ModelMesh.hpp"
#include "Microsoft/Xna/Framework/Graphics/ModelMeshCollection.hpp"

using CnaTest::GltfOracle::CorpusDirectory;
using CnaTest::GltfOracle::LoadedFixture;
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
