// SPDX-License-Identifier: MS-PL

#include <gtest/gtest.h>
#include "Microsoft/Xna/Framework/Graphics/SkinnedModelEXT.hpp"
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
using Microsoft::Xna::Framework::Graphics::KeyframeEXT;
using Microsoft::Xna::Framework::Graphics::SkinnedModelEXT;

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
