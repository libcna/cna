// SPDX-License-Identifier: MS-PL
// Task 940: AnimationPlayer/AnimationClip/Keyframe/SkinningData unit tests. Mirrors
// SkinnedModelEXTTests.cpp's own identical fixture shape and coverage (same underlying
// keyframe-interpolation math, reused via the Keyframe/AnimationClip aliases) since
// AnimationPlayer is the real-Model-facing counterpart of SkinnedModelEXT's own
// ComputeBoneTransformsEXT, just wrapped as a stateful player instead of a stateless function.

#include <gtest/gtest.h>
#include "Microsoft/Xna/Framework/Graphics/AnimationPlayer.hpp"
#include "Microsoft/Xna/Framework/Graphics/Model.hpp"
#include "Microsoft/Xna/Framework/Graphics/ModelBone.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "System/ArgumentException.hpp"
#include "System/TimeSpan.hpp"

using Microsoft::Xna::Framework::Matrix;
using Microsoft::Xna::Framework::Vector3;
using Microsoft::Xna::Framework::Graphics::AnimationClip;
using Microsoft::Xna::Framework::Graphics::AnimationPlayer;
using Microsoft::Xna::Framework::Graphics::BoneTrackEXT;
using Microsoft::Xna::Framework::Graphics::Keyframe;
using Microsoft::Xna::Framework::Graphics::SkinningData;

namespace
{
    // 2-bone rig: bone 0 = root, bone 1 = child of bone 0, offset (0,1,0) in bind pose.
    // Bone 0's track moves it from (0,0,0) at t=0s to (2,0,0) at t=1s. Bind pose and inverse
    // bind pose are both identity so the test isolates hierarchy composition + interpolation
    // (mirrors SkinnedModelEXTTests.cpp's own MakeTwoBoneModel fixture exactly).
    SkinningData MakeTwoBoneSkinningData()
    {
        SkinningData data;
        data.BoneCount = 2;
        data.SkeletonHierarchy = {-1, 0};
        data.BindPose = {Matrix::getIdentityProperty(), Matrix::CreateTranslation(Vector3(0, 1, 0))};
        data.InverseBindPose = {Matrix::getIdentityProperty(), Matrix::getIdentityProperty()};

        BoneTrackEXT track;
        track.BoneIndex = 0;
        track.Keys.push_back(Keyframe{System::TimeSpan::FromSeconds(0.0), Vector3(0, 0, 0)});
        track.Keys.push_back(Keyframe{System::TimeSpan::FromSeconds(1.0), Vector3(2, 0, 0)});

        AnimationClip clip;
        clip.Duration = System::TimeSpan::FromSeconds(1.0);
        clip.Tracks.push_back(track);
        data.AnimationClips["Move"] = clip;
        return data;
    }
}

TEST(AnimationPlayerTest, SamplesStartOfClip)
{
    auto data = MakeTwoBoneSkinningData();
    AnimationPlayer player(data);
    player.StartClip(data.AnimationClips["Move"]);
    player.Update(System::TimeSpan::FromSeconds(0.0), false, false);
    ASSERT_EQ(player.GetWorldTransforms().size(), 2u);
    EXPECT_FLOAT_EQ(player.GetWorldTransforms()[0].getTranslationProperty().X, 0.0f);
}

TEST(AnimationPlayerTest, WrongTargetAndUnorderedMutableKeysAreRefused)
{
    auto data = MakeTwoBoneSkinningData();
    AnimationPlayer player(data);
    auto& clip = data.AnimationClips.at("Move");
    clip.TargetSpace = Microsoft::Xna::Framework::Graphics::ClipTargetSpaceEXT::SceneNode;
    EXPECT_THROW(player.StartClip(clip), System::ArgumentException);
    EXPECT_EQ(player.getCurrentClipProperty(), nullptr);
    clip.TargetSpace = Microsoft::Xna::Framework::Graphics::ClipTargetSpaceEXT::JointPalette;
    player.StartClip(clip);
    std::swap(clip.Tracks[0].Keys[0], clip.Tracks[0].Keys[1]);
    EXPECT_THROW(player.Update(System::TimeSpan::Zero, false), System::ArgumentException);
}

TEST(AnimationPlayerTest, SamplesFullTickRangeAndRefusesNegativeBoneCountBeforeAllocation)
{
    auto data = MakeTwoBoneSkinningData();
    AnimationPlayer player(data);
    auto& clip = data.AnimationClips.at("Move");
    clip.Tracks[0].Keys[0].Time = System::TimeSpan::MinValue;
    clip.Tracks[0].Keys[1].Time = System::TimeSpan::MaxValue;
    EXPECT_NO_THROW(player.StartClip(clip));
    EXPECT_NEAR(player.GetSkinTransforms()[0].M41, 1.0f, 1e-5f);
    data.BoneCount = -1;
    EXPECT_THROW(AnimationPlayer bad(data), System::ArgumentException);
}

TEST(AnimationPlayerTest, RigidSceneSamplingUsesTheSameValidatedInterpolation)
{
    using namespace Microsoft::Xna::Framework::Graphics;
    ModelBone bone(0, "root");
    Model model(nullptr, {&bone}, {});
    auto data = MakeTwoBoneSkinningData();
    auto clip = data.AnimationClips.at("Move");
    clip.TargetSpace = ClipTargetSpaceEXT::SceneNode;
    ApplyClipToBonesEXT(model, clip, System::TimeSpan::FromSeconds(0.5));
    EXPECT_NEAR(bone.getTransformProperty().M41, 1.0f, 1e-5f);
    const auto before = bone.getTransformProperty();
    std::swap(clip.Tracks[0].Keys[0], clip.Tracks[0].Keys[1]);
    EXPECT_THROW(ApplyClipToBonesEXT(model, clip, System::TimeSpan::Zero), System::ArgumentException);
    EXPECT_EQ(bone.getTransformProperty(), before);
}

TEST(AnimationPlayerTest, SamplesMidClipInterpolated)
{
    auto data = MakeTwoBoneSkinningData();
    AnimationPlayer player(data);
    player.StartClip(data.AnimationClips["Move"]);
    player.Update(System::TimeSpan::FromSeconds(0.5), false, false);
    EXPECT_NEAR(player.GetWorldTransforms()[0].getTranslationProperty().X, 1.0f, 1e-4f);
}

TEST(AnimationPlayerTest, SamplesEndOfClip)
{
    auto data = MakeTwoBoneSkinningData();
    AnimationPlayer player(data);
    player.StartClip(data.AnimationClips["Move"]);
    player.Update(System::TimeSpan::FromSeconds(1.0), false, false);
    EXPECT_NEAR(player.GetWorldTransforms()[0].getTranslationProperty().X, 2.0f, 1e-4f);
}

TEST(AnimationPlayerTest, ClampsPastEndWhenNotLooping)
{
    auto data = MakeTwoBoneSkinningData();
    AnimationPlayer player(data);
    player.StartClip(data.AnimationClips["Move"]);
    player.Update(System::TimeSpan::FromSeconds(5.0), false, false);
    EXPECT_NEAR(player.GetWorldTransforms()[0].getTranslationProperty().X, 2.0f, 1e-4f);
}

TEST(AnimationPlayerTest, WrapsPastEndWhenLooping)
{
    auto data = MakeTwoBoneSkinningData();
    AnimationPlayer player(data);
    player.StartClip(data.AnimationClips["Move"]);
    // 1.5s with a 1s clip loops to 0.5s -> interpolated midpoint again.
    player.Update(System::TimeSpan::FromSeconds(1.5), false, true);
    EXPECT_NEAR(player.GetWorldTransforms()[0].getTranslationProperty().X, 1.0f, 1e-4f);
}

TEST(AnimationPlayerTest, RelativeUpdateAccumulatesPosition)
{
    auto data = MakeTwoBoneSkinningData();
    AnimationPlayer player(data);
    player.StartClip(data.AnimationClips["Move"]);
    player.Update(System::TimeSpan::FromSeconds(0.25), true, false);
    player.Update(System::TimeSpan::FromSeconds(0.25), true, false);
    EXPECT_NEAR(player.getCurrentPositionProperty().getTotalSecondsProperty(), 0.5, 1e-9);
    EXPECT_NEAR(player.GetWorldTransforms()[0].getTranslationProperty().X, 1.0f, 1e-4f);
}

TEST(AnimationPlayerTest, ParentHierarchyComposition)
{
    auto data = MakeTwoBoneSkinningData();
    AnimationPlayer player(data);
    player.StartClip(data.AnimationClips["Move"]);
    player.Update(System::TimeSpan::FromSeconds(1.0), false, false);
    // Bone 1's world transform = its own bind-pose local (0,1,0) composed with bone 0's
    // world translation (2,0,0) -> (2,1,0).
    const Vector3 childWorld = player.GetWorldTransforms()[1].getTranslationProperty();
    EXPECT_NEAR(childWorld.X, 2.0f, 1e-4f);
    EXPECT_NEAR(childWorld.Y, 1.0f, 1e-4f);
}

TEST(AnimationPlayerTest, BoneTransformsReflectBindPoseWhenNoClipStarted)
{
    auto data = MakeTwoBoneSkinningData();
    AnimationPlayer player(data);
    // StartClip() was never called -- every bone should stay at its bind pose.
    EXPECT_FLOAT_EQ(player.GetBoneTransforms()[1].getTranslationProperty().Y, 1.0f);
    EXPECT_EQ(player.getCurrentClipProperty(), nullptr);
}

TEST(AnimationPlayerTest, GetSkinTransformsAppliesInverseBindPose)
{
    SkinningData data;
    data.BoneCount = 1;
    data.SkeletonHierarchy = {-1};
    data.BindPose = {Matrix::CreateTranslation(Vector3(5, 0, 0))};
    // Inverse bind pose undoes the bind-pose offset, so the skin transform for an unanimated
    // bone (still at its own bind pose) should collapse to identity.
    data.InverseBindPose = {Matrix::CreateTranslation(Vector3(-5, 0, 0))};

    AnimationPlayer player(data);
    const Matrix& skin = player.GetSkinTransforms()[0];
    EXPECT_NEAR(skin.getTranslationProperty().X, 0.0f, 1e-4f);
}

TEST(AnimationPlayerTest, MismatchedArraySizesThrows)
{
    SkinningData data;
    data.BoneCount = 2;
    data.SkeletonHierarchy = {-1}; // only 1 entry, BoneCount says 2
    data.BindPose = {Matrix::getIdentityProperty(), Matrix::getIdentityProperty()};
    data.InverseBindPose = {Matrix::getIdentityProperty(), Matrix::getIdentityProperty()};

    EXPECT_THROW(AnimationPlayer player(data), System::ArgumentException);
}

TEST(AnimationPlayerTest, ForwardReferencedParentThrows)
{
    SkinningData data;
    data.BoneCount = 2;
    data.SkeletonHierarchy = {1, -1}; // bone 0's parent (1) is not less than its own index
    data.BindPose = {Matrix::getIdentityProperty(), Matrix::getIdentityProperty()};
    data.InverseBindPose = {Matrix::getIdentityProperty(), Matrix::getIdentityProperty()};

    EXPECT_THROW(AnimationPlayer player(data), System::ArgumentException);
}

TEST(AnimationPlayerTest, DefaultConstructedSkinningDataHasNoBones)
{
    SkinningData data;
    EXPECT_EQ(data.BoneCount, 0);
    EXPECT_TRUE(data.SkeletonHierarchy.empty());
    EXPECT_TRUE(data.AnimationClips.empty());
}

// CNA-DEMOS-ISSUE-096: SkeletonRootPrefix is empty (all identity) or holds BoneCount entries; any
// other size is refused like the other parallel skeleton arrays, at construction and at every
// later recomputation, instead of silently reading as all identity.
TEST(AnimationPlayerTest, CompleteAndEmptyRootPrefixesPlaceTheRoot)
{
    auto data = MakeTwoBoneSkinningData();
    data.SkeletonRootPrefix = {Matrix::CreateTranslation(Vector3(4, 0, 0)), Matrix::getIdentityProperty()};
    AnimationPlayer prefixed(data);
    EXPECT_NEAR(prefixed.GetWorldTransforms()[0].getTranslationProperty().X, 4.0f, 1e-4f);
    EXPECT_NEAR(prefixed.GetWorldTransforms()[1].getTranslationProperty().X, 4.0f, 1e-4f);

    data.SkeletonRootPrefix.clear();
    AnimationPlayer identity(data);
    EXPECT_NEAR(identity.GetWorldTransforms()[0].getTranslationProperty().X, 0.0f, 1e-4f);
}

TEST(AnimationPlayerTest, PartialRootPrefixThrowsAtConstruction)
{
    auto data = MakeTwoBoneSkinningData();
    data.SkeletonRootPrefix = {Matrix::CreateTranslation(Vector3(4, 0, 0))};
    EXPECT_THROW(AnimationPlayer player(data), System::ArgumentException);

    data.SkeletonRootPrefix.assign(3, Matrix::getIdentityProperty());
    EXPECT_THROW(AnimationPlayer player(data), System::ArgumentException);
}

TEST(AnimationPlayerTest, PartialRootPrefixThrowsWhenTheBorrowedDataChanges)
{
    auto data = MakeTwoBoneSkinningData();
    AnimationPlayer player(data);
    player.StartClip(data.AnimationClips["Move"]);

    data.SkeletonRootPrefix = {Matrix::CreateTranslation(Vector3(4, 0, 0))};
    EXPECT_THROW(player.Update(System::TimeSpan::FromSeconds(0.5), true, false),
                 System::ArgumentException);

    data.SkeletonRootPrefix.clear();
    EXPECT_NO_THROW(player.Update(System::TimeSpan::FromSeconds(0.5), true, false));
}
