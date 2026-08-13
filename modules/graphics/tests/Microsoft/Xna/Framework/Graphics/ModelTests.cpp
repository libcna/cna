// SPDX-License-Identifier: MS-PL
// Task 435: Model::CopyAbsoluteBoneTransformsTo unit tests, against a known 3-bone hierarchy
// (root -> child -> grandchild).
//
// GraphicsDevice* is confirmed completely unused by Model's 3-arg constructor (Model.cpp), so
// nullptr is safe here, matching the established ModelMeshTests.cpp/ModelMeshCollectionTests.cpp
// convention of never touching a real GraphicsDevice for pure-data tests.

#include <atomic>
#include <gtest/gtest.h>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/Model.hpp"
#include "Microsoft/Xna/Framework/Graphics/ModelBone.hpp"
#include "Microsoft/Xna/Framework/Graphics/ModelMesh.hpp"
#include "Microsoft/Xna/Framework/Graphics/ModelMeshPart.hpp"

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    // root --Translate(1,0,0)--> child --Scale(2)--> grandchild --Translate(0,1,0)-->
    //
    // Scale+translate (not just translation-only) deliberately chosen so the absolute-transform
    // multiply ORDER is genuinely discriminating: bone.Transform * dest[parentIndex] (FNA's real
    // order) gives a different result than the swapped dest[parentIndex] * bone.Transform would.
    struct KnownHierarchy
    {
        ModelBone root  { 0, "Root" };
        ModelBone child { 1, "Child" };
        ModelBone grandchild { 2, "Grandchild" };
        Model model;

        KnownHierarchy()
            : model(nullptr, { &root, &child, &grandchild }, {})
        {
            root.setTransformProperty(Matrix::CreateTranslation(1.0f, 0.0f, 0.0f));
            child.setTransformProperty(Matrix::CreateScale(2.0f));
            grandchild.setTransformProperty(Matrix::CreateTranslation(0.0f, 1.0f, 0.0f));

            root.AddChild(&child);
            child.AddChild(&grandchild);
        }
    };

    bool VectorNear(Vector3 a, Vector3 b, float tol = 0.0001f)
    {
        return std::abs(a.X - b.X) <= tol && std::abs(a.Y - b.Y) <= tol && std::abs(a.Z - b.Z) <= tol;
    }
}

TEST(ModelTest, CopyAbsoluteBoneTransformsToRootUsesItsOwnTransform)
{
    KnownHierarchy h;
    std::vector<Matrix> dest(3);
    h.model.CopyAbsoluteBoneTransformsTo(dest);

    // Root has no parent -> absolute transform is exactly its own local transform.
    Vector3 origin = Vector3::Transform(Vector3(0, 0, 0), dest[0]);
    EXPECT_TRUE(VectorNear(origin, Vector3(1.0f, 0.0f, 0.0f)));
}

TEST(ModelTest, AModelWithoutImportedMaterialVariantsKeepsTheAdditiveApiAtItsDefault)
{
    Model model;
    EXPECT_TRUE(model.getMaterialVariantNamesEXTProperty().empty());
    EXPECT_EQ(-1, model.getMaterialVariantEXTProperty());
    EXPECT_NO_THROW(model.setMaterialVariantEXTProperty(-1));
    EXPECT_THROW(model.setMaterialVariantEXTProperty(0), std::out_of_range);
    EXPECT_EQ(-1, model.getMaterialVariantEXTProperty());
}

TEST(ModelTest, CopyAbsoluteBoneTransformsToChildComposesWithParent)
{
    KnownHierarchy h;
    std::vector<Matrix> dest(3);
    h.model.CopyAbsoluteBoneTransformsTo(dest);

    // child.Transform = Scale(2), composed with root's Translate(1,0,0).
    // point (1,0,0) -> Scale(2) -> (2,0,0) -> Translate(1,0,0) -> (3,0,0).
    Vector3 p = Vector3::Transform(Vector3(1.0f, 0.0f, 0.0f), dest[1]);
    EXPECT_TRUE(VectorNear(p, Vector3(3.0f, 0.0f, 0.0f)));
}

TEST(ModelTest, CopyAbsoluteBoneTransformsToGrandchildComposesInCorrectMultiplyOrder)
{
    KnownHierarchy h;
    std::vector<Matrix> dest(3);
    h.model.CopyAbsoluteBoneTransformsTo(dest);

    // Correct order (bone.Transform * dest[parentIndex], applied left-to-right in XNA's
    // row-vector convention): point (1,0,0) -> Translate(0,1,0) -> (1,1,0)
    //                                       -> Scale(2)          -> (2,2,0)
    //                                       -> Translate(1,0,0)  -> (3,2,0)
    Vector3 p = Vector3::Transform(Vector3(1.0f, 0.0f, 0.0f), dest[2]);
    EXPECT_TRUE(VectorNear(p, Vector3(3.0f, 2.0f, 0.0f)));

    // The swapped multiply order (dest[parentIndex] * bone.Transform) would instead give (3,1,0)
    // for this same input point -- confirming this test point genuinely discriminates order.
    EXPECT_FALSE(VectorNear(p, Vector3(3.0f, 1.0f, 0.0f)));
}

TEST(ModelTest, CopyAbsoluteBoneTransformsToThrowsWhenDestinationTooSmall)
{
    KnownHierarchy h;
    std::vector<Matrix> dest(2); // fewer than the 3 bones in this hierarchy
    EXPECT_THROW(h.model.CopyAbsoluteBoneTransformsTo(dest), std::out_of_range);
}

// --- Task 436: Model::CopyBoneTransformsFrom ---
//
// Real, previously-undocumented FNA deviation found while designing these tests (added to
// CHECKLIST.md): FNA's CopyBoneTransformsFrom/CopyBoneTransformsTo both loop by the CALLER-
// SUPPLIED array's own Length, not Bones.Count -- so a caller-supplied array LARGER than
// Bones.Count makes FNA throw partway through the loop (Bones[i] for i >= Bones.Count throws via
// ReadOnlyCollection<T>'s own indexer). CNA's Model.cpp loops by bones_.getCountProperty() for
// both methods instead, so a too-large source/destination vector is simply accepted with its
// extra elements ignored -- a safer, deliberately-kept deviation, not a bug (matches the same
// class of "intentional C++ safety improvement" already documented for Model::Draw/
// ModelMesh::Draw in Task 431's own CHECKLIST.md additions).

TEST(ModelTest, CopyBoneTransformsFromUpdatesEachBonesTransform)
{
    KnownHierarchy h;
    std::vector<Matrix> src = {
        Matrix::CreateTranslation(5.0f, 0.0f, 0.0f),
        Matrix::CreateTranslation(0.0f, 6.0f, 0.0f),
        Matrix::CreateTranslation(0.0f, 0.0f, 7.0f),
    };
    h.model.CopyBoneTransformsFrom(src);

    EXPECT_TRUE(VectorNear(Vector3::Transform(Vector3(0, 0, 0), h.root.getTransformProperty()),
                            Vector3(5.0f, 0.0f, 0.0f)));
    EXPECT_TRUE(VectorNear(Vector3::Transform(Vector3(0, 0, 0), h.child.getTransformProperty()),
                            Vector3(0.0f, 6.0f, 0.0f)));
    EXPECT_TRUE(VectorNear(Vector3::Transform(Vector3(0, 0, 0), h.grandchild.getTransformProperty()),
                            Vector3(0.0f, 0.0f, 7.0f)));
}

TEST(ModelTest, CopyBoneTransformsFromThrowsWhenSourceTooSmall)
{
    KnownHierarchy h;
    std::vector<Matrix> src(2); // fewer than the 3 bones in this hierarchy
    EXPECT_THROW(h.model.CopyBoneTransformsFrom(src), std::out_of_range);
}

TEST(ModelTest, CopyBoneTransformsFromAcceptsALargerSourceIgnoringExtraElements)
{
    // Deliberate CNA-vs-FNA deviation (see comment above this section): FNA would throw here
    // (its loop bound is sourceBoneTransforms.Length, not Bones.Count); CNA safely ignores the
    // extra element instead.
    KnownHierarchy h;
    std::vector<Matrix> src = {
        Matrix::CreateTranslation(5.0f, 0.0f, 0.0f),
        Matrix::CreateTranslation(0.0f, 6.0f, 0.0f),
        Matrix::CreateTranslation(0.0f, 0.0f, 7.0f),
        Matrix::CreateTranslation(9.0f, 9.0f, 9.0f), // extra, ignored
    };
    EXPECT_NO_THROW(h.model.CopyBoneTransformsFrom(src));
    EXPECT_TRUE(VectorNear(Vector3::Transform(Vector3(0, 0, 0), h.root.getTransformProperty()),
                            Vector3(5.0f, 0.0f, 0.0f)));
}

// --- Task 437: Model::CopyBoneTransformsTo ---
//
// The sibling method to Task 436's CopyBoneTransformsFrom -- same already-documented FNA loop-
// bound deviation applies (see the comment above the Task 436 section): FNA loops by the
// destination array's own Length, CNA loops by Bones.Count.

TEST(ModelTest, CopyBoneTransformsToReadsEachBonesOwnLocalTransform)
{
    KnownHierarchy h;
    std::vector<Matrix> dest(3);
    h.model.CopyBoneTransformsTo(dest);

    // Unlike CopyAbsoluteBoneTransformsTo, this reads each bone's own LOCAL Transform directly --
    // no parent composition at all.
    EXPECT_TRUE(VectorNear(Vector3::Transform(Vector3(0, 0, 0), dest[0]), Vector3(1.0f, 0.0f, 0.0f)));
    EXPECT_TRUE(VectorNear(Vector3::Transform(Vector3(1, 0, 0), dest[1]), Vector3(2.0f, 0.0f, 0.0f)));
    EXPECT_TRUE(VectorNear(Vector3::Transform(Vector3(0, 0, 0), dest[2]), Vector3(0.0f, 1.0f, 0.0f)));
}

TEST(ModelTest, CopyBoneTransformsToThrowsWhenDestinationTooSmall)
{
    KnownHierarchy h;
    std::vector<Matrix> dest(2); // fewer than the 3 bones in this hierarchy
    EXPECT_THROW(h.model.CopyBoneTransformsTo(dest), std::out_of_range);
}

TEST(ModelTest, CopyBoneTransformsToAcceptsALargerDestinationIgnoringExtraElements)
{
    // Deliberate CNA-vs-FNA deviation (see the Task 436 section comment above): FNA would throw
    // here (its loop bound is destinationBoneTransforms.Length); CNA safely leaves the extra
    // element untouched instead.
    KnownHierarchy h;
    std::vector<Matrix> dest(4, Matrix::CreateTranslation(42.0f, 42.0f, 42.0f)); // sentinel value
    EXPECT_NO_THROW(h.model.CopyBoneTransformsTo(dest));
    EXPECT_TRUE(VectorNear(Vector3::Transform(Vector3(0, 0, 0), dest[0]), Vector3(1.0f, 0.0f, 0.0f)));
    // The 4th (extra) element must remain the untouched sentinel value.
    EXPECT_TRUE(VectorNear(Vector3::Transform(Vector3(0, 0, 0), dest[3]), Vector3(42.0f, 42.0f, 42.0f)));
}

// Task 439 finding: ModelMesh::parentBone_ (declares `friend class Model;` for exactly this
// purpose) was never actually set by any public construction path -- not even Model's own 3-arg
// constructor -- so ModelMesh::ParentBone was permanently nullptr for every hand-built model,
// making Model::Draw's `mesh->getParentBoneProperty() ? ... : 0` branch dead code. Fixed via a new
// 4-arg Model constructor overload taking a parallel per-mesh parent-bone vector.
TEST(ModelTest, FourArgConstructorSetsMeshParentBone)
{
    ModelBone root{ 0, "Root" };
    ModelBone child{ 1, "Child" };
    root.AddChild(&child);
    ModelMesh mesh(nullptr, {});

    Model model(nullptr, { &root, &child }, { &mesh }, { &child });

    EXPECT_EQ(mesh.getParentBoneProperty(), &child);
}

TEST(ModelTest, FourArgConstructorEmptyMeshParentBonesLeavesParentBoneNull)
{
    ModelBone root{ 0, "Root" };
    ModelMesh mesh(nullptr, {});

    Model model(nullptr, { &root }, { &mesh }, {});

    EXPECT_EQ(mesh.getParentBoneProperty(), nullptr);
}

TEST(ModelTest, FourArgConstructorThrowsWhenMeshParentBonesSizeMismatches)
{
    ModelBone root{ 0, "Root" };
    ModelMesh meshA(nullptr, {});
    ModelMesh meshB(nullptr, {});

    EXPECT_THROW(Model(nullptr, { &root }, { &meshA, &meshB }, { &root }), std::out_of_range);
}

// Task 916 finding: FNA's real Model constructor never sets Root at all -- ModelReader assigns it
// externally afterward, from an explicit rootBoneIndex naming any bone (src/Content/ContentReaders/
// ModelReader.cs: `model.Root = bones[rootBoneIndex]`). CNA's constructors always defaulted Root to
// bones[0] with no way to specify otherwise. Fixed via a new optional rootBoneIndex parameter
// (defaulting to 0) on the 4-arg constructor.

TEST(ModelTest, FiveArgConstructorDefaultRootBoneIndexMatchesFourArgBehavior)
{
    ModelBone root{ 0, "Root" };
    ModelBone child{ 1, "Child" };

    Model model(nullptr, { &root, &child }, {}, {});

    EXPECT_EQ(model.getRootProperty(), &root);
}

TEST(ModelTest, FiveArgConstructorHonorsNonZeroRootBoneIndex)
{
    ModelBone bone0{ 0, "Bone0" };
    ModelBone bone1{ 1, "Bone1" };
    ModelBone bone2{ 2, "Bone2" };

    Model model(nullptr, { &bone0, &bone1, &bone2 }, {}, {}, 2);

    EXPECT_EQ(model.getRootProperty(), &bone2);
    EXPECT_NE(model.getRootProperty(), &bone0);
}

TEST(ModelTest, FiveArgConstructorThrowsWhenRootBoneIndexOutOfRange)
{
    ModelBone root{ 0, "Root" };

    EXPECT_THROW(Model(nullptr, { &root }, {}, {}, 1), std::out_of_range);
}

TEST(ModelTest, FiveArgConstructorEmptyBonesLeavesRootNullEvenWithDefaultIndex)
{
    Model model(nullptr, {}, {}, {});

    EXPECT_EQ(model.getRootProperty(), nullptr);
}

// --- plan_gltf.md GLTF-128: one live whole-model bounding sphere -----------------------------

TEST(ModelTest, BoundingSphereEXTIsEmptyOnlyWhenTheModelHasNoMeshes)
{
    Model model(nullptr, {}, {});
    EXPECT_FALSE(model.getBoundingSphereEXTProperty().has_value());

    // A zero-radius mesh is valid point geometry, not the empty sentinel.
    ModelMesh point(nullptr, {});
    point.setBoundingSphereProperty(BoundingSphere(Vector3(2.0f, 3.0f, 4.0f), 0.0f));
    Model pointModel(nullptr, {}, {&point});
    const auto bounds = pointModel.getBoundingSphereEXTProperty();
    ASSERT_TRUE(bounds.has_value());
    EXPECT_TRUE(VectorNear(bounds->Center, Vector3(2.0f, 3.0f, 4.0f)));
    EXPECT_FLOAT_EQ(0.0f, bounds->Radius);
}

TEST(ModelTest, BoundingSphereEXTMergesMeshBoundsAfterTheirCurrentAbsoluteBoneTransforms)
{
    ModelBone root{0, "Root"};
    ModelBone left{1, "Left"};
    ModelBone right{2, "Right"};
    root.AddChild(&left);
    root.AddChild(&right);

    // The root scale makes this a real absolute-transform test rather than two translations that
    // would look the same if the parent chain were skipped. In XNA's row-vector convention the
    // children land at -10 and +14; their radii become 2 and 4.
    root.setTransformProperty(Matrix::CreateScale(2.0f));
    left.setTransformProperty(Matrix::CreateTranslation(-5.0f, 0.0f, 0.0f));
    right.setTransformProperty(Matrix::CreateTranslation(7.0f, 0.0f, 0.0f));

    ModelMesh leftMesh(nullptr, {});
    ModelMesh rightMesh(nullptr, {});
    leftMesh.setBoundingSphereProperty(BoundingSphere(Vector3::Zero, 1.0f));
    rightMesh.setBoundingSphereProperty(BoundingSphere(Vector3::Zero, 2.0f));
    Model model(nullptr, {&root, &left, &right}, {&leftMesh, &rightMesh},
                {&left, &right});

    const auto first = model.getBoundingSphereEXTProperty();
    ASSERT_TRUE(first.has_value());
    EXPECT_TRUE(VectorNear(first->Center, Vector3(3.0f, 0.0f, 0.0f)));
    EXPECT_NEAR(15.0f, first->Radius, 1e-5f);

    // The accessor is deliberately live for rigid animation. A cached import-time sphere would
    // keep returning (3, 15); recomposition after this pose change yields (13, 25).
    right.setTransformProperty(Matrix::CreateTranslation(17.0f, 0.0f, 0.0f));
    const auto posed = model.getBoundingSphereEXTProperty();
    ASSERT_TRUE(posed.has_value());
    EXPECT_TRUE(VectorNear(posed->Center, Vector3(13.0f, 0.0f, 0.0f)));
    EXPECT_NEAR(25.0f, posed->Radius, 1e-5f);
}

TEST(ModelTest, BoundingSphereEXTStaysConservativeForAShearedAbsoluteBoneTransform)
{
    // A rotated child below a non-uniformly scaled parent can produce shear in a perfectly valid
    // glTF hierarchy. This explicit equivalent has basis lengths sqrt(2) and sqrt(1.81), but it
    // stretches the unit direction (1,1,0)/sqrt(2) to length about 1.95. Using only the longest
    // basis vector -- XNA BoundingSphere::Transform's ordinary TRS rule -- returns about 1.41 and
    // excludes that point.
    Matrix shear = Matrix::getIdentityProperty();
    shear.M11 = 1.0f;
    shear.M12 = 1.0f;
    shear.M21 = 1.0f;
    shear.M22 = 0.9f;

    ModelBone root{0, "Root"};
    root.setTransformProperty(shear);
    ModelMesh mesh(nullptr, {});
    mesh.setBoundingSphereProperty(BoundingSphere(Vector3::Zero, 1.0f));
    Model model(nullptr, {&root}, {&mesh}, {&root});

    const auto bounds = model.getBoundingSphereEXTProperty();
    ASSERT_TRUE(bounds.has_value());
    constexpr float inverseSqrtTwo = 0.7071067811865475f;
    const Vector3 pointOnLocalSphere(inverseSqrtTwo, inverseSqrtTwo, 0.0f);
    const Vector3 transformed = Vector3::Transform(pointOnLocalSphere, shear);
    EXPECT_LE(Vector3::Distance(bounds->Center, transformed), bounds->Radius)
        << "the whole-model sphere stopped being a bound after hierarchy composition introduced "
           "shear";
    EXPECT_GT(bounds->Radius,
              mesh.getBoundingSphereProperty().Transform(shear).Radius)
        << "the test no longer distinguishes a shear-safe radius from XNA's single-TRS rule";
}

// --- plan_gltf.md GLTF-444: Model::Draw's shared bone scratch buffer ----------------------------
//
// FNA's Model.Draw shares one static Matrix[] across every Model in the process. Two threads
// drawing different models both resize and rewrite it, and a resize while another thread holds a
// reference into it is a use-after-free -- not merely a wrong matrix. CNA makes the buffer
// thread_local, which is observably identical for any single-threaded caller (it is pure scratch,
// fully overwritten at the top of every Draw and never read afterwards) and removes the race.
//
// The assertion has to read a value that PASSED THROUGH the buffer, or it proves nothing: an
// earlier draft of this test read the bone transforms back with CopyAbsoluteBoneTransformsTo,
// which reads `bones_` directly and never touches the scratch at all -- it passed just as happily
// with the shared static in place. The only value Draw pushes through the buffer is the effect's
// World matrix, so that is what is checked here.
//
// The two bone counts differ by a large factor on purpose, so a shared buffer would reallocate on
// nearly every alternation between the two thread groups rather than settling at one size. That
// is what makes this a real probe under ASan rather than a hopeful one.

TEST(ModelTest, ConcurrentDrawsOnDifferentModelsDoNotShareTheBoneScratchBuffer)
{
    GraphicsDevice device;

    constexpr int kThreads = 4;
    constexpr int kIterations = 400;

    // Everything is built on this thread, before any of them start: constructing an Effect touches
    // the device, and a race there would be a different bug wearing this test's clothes.
    struct PerThread
    {
        std::vector<ModelBone> boneStorage;
        std::vector<ModelBone*> bones;
        std::unique_ptr<BasicEffect> effect;
        std::unique_ptr<ModelMeshPart> part;
        std::unique_ptr<ModelMesh> mesh;
        std::unique_ptr<Model> model;
        float offset = 0.0f;
        int boneIndex = 0;
    };

    std::vector<std::unique_ptr<PerThread>> fixtures;
    for (int t = 0; t < kThreads; ++t)
    {
        auto fixture = std::make_unique<PerThread>();
        // Threads 0 and 2 get a 2-bone model; 1 and 3 get a 150-bone one, so a shared buffer would
        // be resized back and forth between the two sizes for the whole run.
        const int boneCount = (t % 2 == 0) ? 2 : 150;
        fixture->offset = static_cast<float>(t + 1) * 1000.0f;
        // The LAST bone, so a leaked buffer from the 2-bone model would also be too short for the
        // 150-bone one's parent-bone index and not merely hold the wrong value.
        fixture->boneIndex = boneCount - 1;

        fixture->boneStorage.reserve(static_cast<std::size_t>(boneCount));
        for (int i = 0; i < boneCount; ++i)
        {
            fixture->boneStorage.emplace_back(i, "B" + std::to_string(i));
        }
        for (int i = 0; i < boneCount; ++i)
        {
            // Flat hierarchy: a bone's absolute transform is its own, so the expected World needs
            // no traversal and the test stays about the buffer rather than about bones.
            fixture->boneStorage[static_cast<std::size_t>(i)].setTransformProperty(
                Matrix::CreateTranslation(fixture->offset + static_cast<float>(i), 0.0f, 0.0f));
            fixture->bones.push_back(&fixture->boneStorage[static_cast<std::size_t>(i)]);
        }

        fixture->effect = std::make_unique<BasicEffect>(device);
        // primitiveCount 0, so ModelMesh::Draw skips the part entirely and no vertex buffer, index
        // buffer or device work is involved -- Model::Draw's matrix binding is all that runs.
        fixture->part = std::make_unique<ModelMeshPart>(nullptr, nullptr, 0, 0, 0, 0);
        fixture->mesh = std::make_unique<ModelMesh>(nullptr, std::vector<ModelMeshPart*>{
            fixture->part.get()});
        fixture->part->setEffectProperty(fixture->effect.get());
        fixture->mesh->setParentBoneProperty(
            fixture->bones[static_cast<std::size_t>(fixture->boneIndex)]);
        fixture->model = std::make_unique<Model>(nullptr, fixture->bones,
                                                  std::vector<ModelMesh*>{fixture->mesh.get()});
        fixtures.push_back(std::move(fixture));
    }

    std::atomic<bool> mismatch{false};
    std::atomic<int> ready{0};
    std::vector<std::thread> threads;
    threads.reserve(kThreads);

    for (int t = 0; t < kThreads; ++t)
    {
        threads.emplace_back([&fixtures, &mismatch, &ready, t] {
            PerThread& fixture = *fixtures[static_cast<std::size_t>(t)];
            const float expected = fixture.offset + static_cast<float>(fixture.boneIndex);

            ready.fetch_add(1);
            while (ready.load() < kThreads) { std::this_thread::yield(); }

            for (int iteration = 0; iteration < kIterations; ++iteration)
            {
                fixture.model->Draw(Matrix::getIdentityProperty(), Matrix::getIdentityProperty(),
                                     Matrix::getIdentityProperty());
                // Draw pushes sharedDrawBoneMatrices_[parentBoneIndex] * world into the effect's
                // World. With one buffer per process, another thread's transforms land here.
                if (fixture.effect->getWorldProperty().M41 != expected)
                {
                    mismatch.store(true);
                }
            }
        });
    }

    for (std::thread& thread : threads) { thread.join(); }
    EXPECT_FALSE(mismatch.load())
        << "an effect was bound with another thread's bone transform -- Model::Draw's scratch "
           "buffer is shared across threads again (plan_gltf.md GLTF-444)";
}
