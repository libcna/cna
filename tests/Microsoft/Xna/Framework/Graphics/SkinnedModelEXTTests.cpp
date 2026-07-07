// SPDX-License-Identifier: MS-PL

#include <chrono>
#include <gtest/gtest.h>
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/IndexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/ModelMeshPart.hpp"
#include "Microsoft/Xna/Framework/Graphics/SkinnedModelEXT.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/MathHelper.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "System/ArgumentException.hpp"
#include "System/TimeSpan.hpp"

using Microsoft::Xna::Framework::Matrix;
using Microsoft::Xna::Framework::Quaternion;
using Microsoft::Xna::Framework::Vector3;
using Microsoft::Xna::Framework::Graphics::AnimationClipEXT;
using Microsoft::Xna::Framework::Graphics::BoneTrackEXT;
using Microsoft::Xna::Framework::Graphics::GraphicsDevice;
using Microsoft::Xna::Framework::Graphics::IndexBuffer;
using Microsoft::Xna::Framework::Graphics::KeyframeEXT;
using Microsoft::Xna::Framework::Graphics::ModelMeshPart;
using Microsoft::Xna::Framework::Graphics::SkinnedModelEXT;
using Microsoft::Xna::Framework::Graphics::Texture2D;
using Microsoft::Xna::Framework::Graphics::VertexBuffer;

namespace
{
    // 2-bone rig: bone 0 = root, bone 1 = child of bone 0, offset (0,1,0) in bind pose.
    // Bone 0's track moves it from (0,0,0) at t=0s to (2,0,0) at t=1s. Bind pose and inverse
    // bind pose are both identity so the test isolates hierarchy composition + interpolation.
    SkinnedModelEXT MakeTwoBoneModel()
    {
        SkinnedModelEXT model;
        model.BoneCount = 2;
        model.ParentBoneIndices = {-1, 0};
        model.BindPoseLocal = {Matrix::getIdentityProperty(), Matrix::CreateTranslation(Vector3(0, 1, 0))};
        model.InverseBindPoseGlobal = {Matrix::getIdentityProperty(), Matrix::getIdentityProperty()};

        BoneTrackEXT track;
        track.BoneIndex = 0;
        track.Keys.push_back(KeyframeEXT{System::TimeSpan::FromSeconds(0.0), Vector3(0, 0, 0)});
        track.Keys.push_back(KeyframeEXT{System::TimeSpan::FromSeconds(1.0), Vector3(2, 0, 0)});

        AnimationClipEXT clip;
        clip.Duration = System::TimeSpan::FromSeconds(1.0);
        clip.Tracks.push_back(track);
        model.Clips["Move"] = clip;
        return model;
    }
}

TEST(SkinnedModelEXTTest, SamplesStartOfClip)
{
    auto model = MakeTwoBoneModel();
    std::vector<Matrix> bones;
    model.ComputeBoneTransformsEXT("Move", System::TimeSpan::FromSeconds(0.0), false, bones);
    ASSERT_EQ(bones.size(), 2u);
    EXPECT_FLOAT_EQ(bones[0].getTranslationProperty().X, 0.0f);
}

TEST(SkinnedModelEXTTest, SamplesMidClipInterpolated)
{
    auto model = MakeTwoBoneModel();
    std::vector<Matrix> bones;
    model.ComputeBoneTransformsEXT("Move", System::TimeSpan::FromSeconds(0.5), false, bones);
    EXPECT_NEAR(bones[0].getTranslationProperty().X, 1.0f, 1e-4f);
}

TEST(SkinnedModelEXTTest, SamplesEndOfClip)
{
    auto model = MakeTwoBoneModel();
    std::vector<Matrix> bones;
    model.ComputeBoneTransformsEXT("Move", System::TimeSpan::FromSeconds(1.0), false, bones);
    EXPECT_NEAR(bones[0].getTranslationProperty().X, 2.0f, 1e-4f);
}

TEST(SkinnedModelEXTTest, ClampsPastEndWhenNotLooping)
{
    auto model = MakeTwoBoneModel();
    std::vector<Matrix> bones;
    model.ComputeBoneTransformsEXT("Move", System::TimeSpan::FromSeconds(5.0), false, bones);
    EXPECT_NEAR(bones[0].getTranslationProperty().X, 2.0f, 1e-4f);
}

TEST(SkinnedModelEXTTest, WrapsPastEndWhenLooping)
{
    auto model = MakeTwoBoneModel();
    std::vector<Matrix> bones;
    // 1.5s with a 1s clip loops to 0.5s -> interpolated midpoint again.
    model.ComputeBoneTransformsEXT("Move", System::TimeSpan::FromSeconds(1.5), true, bones);
    EXPECT_NEAR(bones[0].getTranslationProperty().X, 1.0f, 1e-4f);
}

// Task 11.1: the old wraparound reduced an out-of-range position by subtracting/adding one
// Duration at a time - an unbounded per-frame cost proportional to position/Duration. Using pure
// integer tick arithmetic (not FromSeconds' floating-point path) to keep the expected result
// exact: 1,000,000,000.5 clip-durations in, which would have looped ~1 billion times under the
// old algorithm.
TEST(SkinnedModelEXTTest, WrapsHugePositionInBoundedTime)
{
    auto model = MakeTwoBoneModel();
    std::vector<Matrix> bones;

    const auto durationTicks = System::TimeSpan::FromSeconds(1.0).getTicksProperty();
    const auto hugePos = System::TimeSpan::FromTicks(durationTicks * 1'000'000'000LL + durationTicks / 2);

    const auto start = std::chrono::steady_clock::now();
    model.ComputeBoneTransformsEXT("Move", hugePos, true, bones);
    const auto elapsed = std::chrono::steady_clock::now() - start;

    EXPECT_LT(elapsed, std::chrono::milliseconds(100))
        << "took too long - looks like the old proportional-to-position loop regressed";
    EXPECT_NEAR(bones[0].getTranslationProperty().X, 1.0f, 1e-4f);
}

// Task 13.2: confirms the defensive bone-index bounds check
// (`!track.Keys.empty() && track.BoneIndex >= 0 && track.BoneIndex < localTransforms.size()`)
// actually gets exercised - a track with an out-of-range or negative BoneIndex must be safely
// skipped (leaving that bone at its bind pose), not silently mis-happen or crash.
TEST(SkinnedModelEXTTest, OutOfRangeOrNegativeBoneIndexTrackIsSkippedSafely)
{
    auto model = MakeTwoBoneModel();

    BoneTrackEXT outOfRange;
    outOfRange.BoneIndex = 99; // way past BoneCount == 2
    outOfRange.Keys.push_back(KeyframeEXT{System::TimeSpan::FromSeconds(0.0), Vector3(100, 100, 100)});
    model.Clips["Move"].Tracks.push_back(outOfRange);

    BoneTrackEXT negative;
    negative.BoneIndex = -5;
    negative.Keys.push_back(KeyframeEXT{System::TimeSpan::FromSeconds(0.0), Vector3(200, 200, 200)});
    model.Clips["Move"].Tracks.push_back(negative);

    std::vector<Matrix> bones;
    EXPECT_NO_THROW(
        model.ComputeBoneTransformsEXT("Move", System::TimeSpan::FromSeconds(0.0), false, bones));

    ASSERT_EQ(bones.size(), 2u);
    // Bone 0's own valid track still drives it normally, unaffected by the invalid tracks.
    EXPECT_NEAR(bones[0].getTranslationProperty().X, 0.0f, 1e-4f);
    // Bone 1 has no valid track of its own - stays at its bind-pose offset (0,1,0), composed with
    // bone 0's (identity at t=0) world transform.
    EXPECT_NEAR(bones[1].getTranslationProperty().Y, 1.0f, 1e-4f);
}

TEST(SkinnedModelEXTTest, ParentHierarchyComposition)
{
    auto model = MakeTwoBoneModel();
    std::vector<Matrix> bones;
    model.ComputeBoneTransformsEXT("Move", System::TimeSpan::FromSeconds(1.0), false, bones);
    // Bone 1 has no track (holds bind pose local offset (0,1,0)) and its parent (bone 0)
    // has moved to (2,0,0) -> bone 1's world position should be (2,1,0).
    const Vector3 childWorld = bones[1].getTranslationProperty();
    EXPECT_NEAR(childWorld.X, 2.0f, 1e-4f);
    EXPECT_NEAR(childWorld.Y, 1.0f, 1e-4f);
}

TEST(SkinnedModelEXTTest, RotatingBoneKeepsItsOwnBindPivotFixed)
{
    // Task 11.20b regression test: ComputeBoneTransformsEXT's final
    // `InverseBindPoseGlobal[i] * worldTransforms[i]` product order matters once a bone
    // has BOTH a nontrivial bind-pose offset from its parent AND a real (non-identity)
    // rotation -- every other test in this file uses pure translations, which commute
    // with anything and so cannot distinguish the two possible multiplication orders.
    // A single root bone with bind-pose offset (1,0,0) that rotates 90 degrees in place
    // (translation held at its own bind value) must keep its own pivot -- the bind-world
    // point (1,0,0) -- exactly fixed in world space; only points OFFSET from that pivot
    // should move. The reversed order does not: it collapses to exactly the bare
    // rotation matrix (since T * R * Invert(T) with commuting-translation algebra
    // reduces to R alone when the *wrong* operand order is used), which visibly moves
    // the pivot itself -- reproducing the real-engine-only elongated-limb bug found
    // while verifying Task 11.20 (a real Wave/Celebrate animation looked correct in
    // Blender but rendered as a dramatically stretched, partially-detached forearm
    // through this exact code path).
    SkinnedModelEXT model;
    model.BoneCount = 1;
    model.ParentBoneIndices = {-1};
    model.BindPoseLocal = {Matrix::CreateTranslation(Vector3(1, 0, 0))};
    model.InverseBindPoseGlobal = {Matrix::Invert(model.BindPoseLocal[0])};

    BoneTrackEXT track;
    track.BoneIndex = 0;
    // Same translation as bind (1,0,0), but with a real 90-degree rotation added --
    // "rotate in place," not "rotate then also move."
    track.Keys.push_back(KeyframeEXT{
        System::TimeSpan::Zero, Vector3(1, 0, 0),
        Quaternion::CreateFromAxisAngle(Vector3(0, 0, 1), Microsoft::Xna::Framework::MathHelper::PiOver2)});

    AnimationClipEXT clip;
    clip.Duration = System::TimeSpan::Zero;
    clip.Tracks.push_back(track);
    model.Clips["Rotate"] = clip;

    std::vector<Matrix> bones;
    model.ComputeBoneTransformsEXT("Rotate", System::TimeSpan::Zero, false, bones);

    const Vector3 pivotBindWorld(1.0f, 0.0f, 0.0f);
    const Vector3 pivotPosed = Vector3::Transform(pivotBindWorld, bones[0]);
    EXPECT_NEAR(pivotPosed.X, 1.0f, 1e-3f);
    EXPECT_NEAR(pivotPosed.Y, 0.0f, 1e-3f);
    EXPECT_NEAR(pivotPosed.Z, 0.0f, 1e-3f);
}

TEST(SkinnedModelEXTTest, UnknownClipThrows)
{
    auto model = MakeTwoBoneModel();
    std::vector<Matrix> bones;
    EXPECT_THROW(
        model.ComputeBoneTransformsEXT("NoSuchClip", System::TimeSpan::Zero, false, bones),
        System::ArgumentException);
}

// Task 11.3: ParentBoneIndices/BindPoseLocal/InverseBindPoseGlobal are all indexed up to
// BoneCount with no check that their .size() actually matches - a corrupt/truncated
// .skeleton.bin (or, as here, a deliberately size-mismatched hand-built model) must be rejected
// cleanly instead of causing a real out-of-bounds std::vector::operator[] read.
TEST(SkinnedModelEXTTest, MismatchedArraySizesThrows)
{
    SkinnedModelEXT model;
    model.BoneCount = 2;
    model.ParentBoneIndices = {-1}; // only 1 entry, but BoneCount says 2
    model.BindPoseLocal = {Matrix::getIdentityProperty(), Matrix::getIdentityProperty()};
    model.InverseBindPoseGlobal = {Matrix::getIdentityProperty(), Matrix::getIdentityProperty()};

    AnimationClipEXT clip;
    clip.Duration = System::TimeSpan::Zero;
    model.Clips["Idle"] = clip;

    std::vector<Matrix> bones;
    EXPECT_THROW(
        model.ComputeBoneTransformsEXT("Idle", System::TimeSpan::Zero, false, bones),
        System::ArgumentException);
}

// Task 11.2: "topological order" is a convention enforced only by the content pipeline, never
// checked in ComputeBoneTransformsEXT itself - a forward-referenced parent (parent index >= the
// bone's own index) must be rejected instead of silently reading a not-yet-finalized
// worldTransforms entry.
TEST(SkinnedModelEXTTest, ForwardReferencedParentThrows)
{
    SkinnedModelEXT model;
    model.BoneCount = 2;
    // Bone 0's parent is bone 1, which is processed *after* bone 0 - not topological.
    model.ParentBoneIndices = {1, -1};
    model.BindPoseLocal = {Matrix::getIdentityProperty(), Matrix::getIdentityProperty()};
    model.InverseBindPoseGlobal = {Matrix::getIdentityProperty(), Matrix::getIdentityProperty()};

    AnimationClipEXT clip;
    clip.Duration = System::TimeSpan::Zero;
    model.Clips["Idle"] = clip;

    std::vector<Matrix> bones;
    EXPECT_THROW(
        model.ComputeBoneTransformsEXT("Idle", System::TimeSpan::Zero, false, bones),
        System::ArgumentException);
}

// A cyclic parent graph always contains at least one bone whose parent index is >= its own
// (the cycle member with the greatest index must point to another cycle member, which cannot
// have a strictly greater index without contradiction) - the same check above transitively
// rejects cycles too. The simplest possible cycle is a bone that is its own parent.
TEST(SkinnedModelEXTTest, SelfParentThrows)
{
    SkinnedModelEXT model;
    model.BoneCount = 1;
    model.ParentBoneIndices = {0};
    model.BindPoseLocal = {Matrix::getIdentityProperty()};
    model.InverseBindPoseGlobal = {Matrix::getIdentityProperty()};

    AnimationClipEXT clip;
    clip.Duration = System::TimeSpan::Zero;
    model.Clips["Idle"] = clip;

    std::vector<Matrix> bones;
    EXPECT_THROW(
        model.ComputeBoneTransformsEXT("Idle", System::TimeSpan::Zero, false, bones),
        System::ArgumentException);
}

TEST(SkinnedModelEXTTest, DefaultConstructedHasNoBonesOrClips)
{
    SkinnedModelEXT model;
    EXPECT_EQ(model.BoneCount, 0);
    EXPECT_TRUE(model.Clips.empty());
    EXPECT_TRUE(model.Parts.empty());
}

TEST(SkinnedModelEXTTest, AttachPartMismatchedBoneCountThrows)
{
    SkinnedModelEXT host;
    host.BoneCount = 19;
    SkinnedModelEXT wardrobePiece;
    wardrobePiece.BoneCount = 5;
    EXPECT_THROW(host.AttachPartEXT(std::move(wardrobePiece)), System::ArgumentException);
}

TEST(SkinnedModelEXTTest, AttachPartSameBoneCountNoPartsIsNoOp)
{
    SkinnedModelEXT host;
    host.BoneCount = 19;
    SkinnedModelEXT wardrobePiece;
    wardrobePiece.BoneCount = 19;
    EXPECT_NO_THROW(host.AttachPartEXT(std::move(wardrobePiece)));
    EXPECT_TRUE(host.Parts.empty());
}

// Tasks 11.4/11.5: real (GPU-backed) part attach/remove coverage. VertexBuffer/IndexBuffer
// require a real GraphicsDevice to construct (no fake/mock is available - see Task 13.3), but
// a plain, default-constructed GraphicsDevice works headlessly here exactly like the existing
// Texture2DTests.cpp fixtures already rely on elsewhere in this same test binary.
class SkinnedModelEXTPartTest : public ::testing::Test
{
protected:
    GraphicsDevice gd;
};

// Task 11.4: AttachPartEXT unconditionally appended every part with no duplicate/slot-replace
// handling - both shipped wardrobe pieces use the identical part name "CNAAvatarHair", so
// swapping hairstyles at runtime used to leave two overlapping parts both rendered.
TEST_F(SkinnedModelEXTPartTest, AttachingSameNamedPartReplacesTheOldOne)
{
    SkinnedModelEXT host;
    host.BoneCount = 1;
    host.AddPartEXT("CNAAvatarHair",
                     std::make_unique<VertexBuffer>(gd, 1),
                     std::make_unique<IndexBuffer>(gd, 1),
                     std::make_unique<ModelMeshPart>());
    ASSERT_EQ(1u, host.Parts.size());

    SkinnedModelEXT wardrobe;
    wardrobe.BoneCount = 1;
    wardrobe.AddPartEXT("CNAAvatarHair",
                         std::make_unique<VertexBuffer>(gd, 1),
                         std::make_unique<IndexBuffer>(gd, 1),
                         std::make_unique<ModelMeshPart>());

    host.AttachPartEXT(std::move(wardrobe));

    EXPECT_EQ(1u, host.Parts.size());
    EXPECT_EQ(1u, host.GetOwnedPartCountForTesting());
    EXPECT_EQ(1u, host.GetOwnedVertexBufferCountForTesting());
    EXPECT_EQ(1u, host.GetOwnedIndexBufferCountForTesting());
}

// Task 11.5: erasing directly from the public Parts vector (the previous only available
// approach) shrank Parts but never touched the corresponding owned GPU-resource vectors - a
// permanent leak of the old part's VertexBuffer/IndexBuffer/ModelMeshPart/Texture2D. Proves
// RemovePartEXT actually frees all 4, not just Parts.
TEST_F(SkinnedModelEXTPartTest, RemovePartFreesOwnedResources)
{
    SkinnedModelEXT model;
    model.BoneCount = 1;
    model.AddPartEXT("A",
                      std::make_unique<VertexBuffer>(gd, 1),
                      std::make_unique<IndexBuffer>(gd, 1),
                      std::make_unique<ModelMeshPart>(),
                      Texture2D(gd, 4, 4)); // part "A" owns a real texture
    model.AddPartEXT("B",
                      std::make_unique<VertexBuffer>(gd, 1),
                      std::make_unique<IndexBuffer>(gd, 1),
                      std::make_unique<ModelMeshPart>()); // part "B" has no texture

    ASSERT_EQ(2u, model.Parts.size());
    ASSERT_EQ(2u, model.GetOwnedPartCountForTesting());
    ASSERT_EQ(2u, model.GetOwnedVertexBufferCountForTesting());
    ASSERT_EQ(2u, model.GetOwnedIndexBufferCountForTesting());
    ASSERT_EQ(1u, model.GetOwnedTextureCountForTesting());

    model.RemovePartEXT("A");

    EXPECT_EQ(1u, model.Parts.size());
    EXPECT_EQ("B", model.Parts[0].Name);
    EXPECT_EQ(1u, model.GetOwnedPartCountForTesting());
    EXPECT_EQ(1u, model.GetOwnedVertexBufferCountForTesting());
    EXPECT_EQ(1u, model.GetOwnedIndexBufferCountForTesting());
    EXPECT_EQ(0u, model.GetOwnedTextureCountForTesting());
}

TEST_F(SkinnedModelEXTPartTest, RemovePartRemovesAllMatchingNamesNotJustFirst)
{
    SkinnedModelEXT model;
    model.BoneCount = 1;
    model.AddPartEXT("Dup",
                      std::make_unique<VertexBuffer>(gd, 1),
                      std::make_unique<IndexBuffer>(gd, 1),
                      std::make_unique<ModelMeshPart>());
    model.AddPartEXT("Dup",
                      std::make_unique<VertexBuffer>(gd, 1),
                      std::make_unique<IndexBuffer>(gd, 1),
                      std::make_unique<ModelMeshPart>());
    ASSERT_EQ(2u, model.Parts.size());

    model.RemovePartEXT("Dup");

    EXPECT_TRUE(model.Parts.empty());
    EXPECT_EQ(0u, model.GetOwnedPartCountForTesting());
    EXPECT_EQ(0u, model.GetOwnedVertexBufferCountForTesting());
    EXPECT_EQ(0u, model.GetOwnedIndexBufferCountForTesting());
}
