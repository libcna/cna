// SPDX-License-Identifier: MS-PL
#include <gtest/gtest.h>

#include "Microsoft/Xna/Framework/GamerServices/AvatarAnimation.hpp"
#include "Microsoft/Xna/Framework/GamerServices/AvatarBone.hpp"
#include "Microsoft/Xna/Framework/Quaternion.hpp"
#include "System/ObjectDisposedException.hpp"

#include <cmath>

using namespace Microsoft::Xna::Framework::GamerServices;
using Microsoft::Xna::Framework::Matrix;

namespace {
using System::TimeSpan;

TimeSpan Seconds(double seconds) { return TimeSpan::FromTicks(static_cast<long long>(seconds * 1.0e7)); }

bool Near(const Matrix& a, const Matrix& b, float tolerance = 1e-4f) {
    const float* x = &a.M11;
    const float* y = &b.M11;
    for (int i = 0; i < 16; ++i) {
        if (std::fabs(x[i] - y[i]) > tolerance) return false;
    }
    return true;
}

Matrix Bone(const AvatarAnimation& animation, int index) {
    const auto bones = animation.getBoneTransformsProperty();
    return bones[index];
}
}

TEST(AvatarAnimationTest, EveryPresetHasARealLengthAndDecomposableBones) {
    for (int preset = 0; preset <= static_cast<int>(AvatarAnimationPreset::MaleYawn); ++preset) {
        AvatarAnimation animation(static_cast<AvatarAnimationPreset>(preset));
        EXPECT_GE(animation.getLengthProperty(), Seconds(2.5)) << preset;
        EXPECT_LE(animation.getLengthProperty(), Seconds(10.0)) << preset;
        for (double t : {0.0, 0.37, 1.1, 2.2}) {
            animation.setCurrentPositionProperty(Seconds(t));
            const auto bones = animation.getBoneTransformsProperty();
            ASSERT_EQ(bones.getCountProperty(), 71);
            for (int bone = 0; bone < 71; ++bone) {
                Microsoft::Xna::Framework::Vector3 scale, translation;
                Microsoft::Xna::Framework::Quaternion rotation;
                ASSERT_TRUE(bones[bone].Decompose(scale, rotation, translation)) << preset << " bone " << bone;
                EXPECT_NEAR(scale.X, 1.0f, 1e-3f);
            }
        }
    }
}

TEST(AvatarAnimationTest, PresetsAreDistinctMotions) {
    AvatarAnimation stand(AvatarAnimationPreset::Stand0);
    AvatarAnimation wave(AvatarAnimationPreset::Wave);
    EXPECT_NE(stand.getLengthProperty(), wave.getLengthProperty());
    wave.setCurrentPositionProperty(Seconds(1.2));
    stand.setCurrentPositionProperty(Seconds(1.2));
    EXPECT_FALSE(Near(Bone(wave, static_cast<int>(AvatarBone::ShoulderRight)), Bone(stand, static_cast<int>(AvatarBone::ShoulderRight))));
}

TEST(AvatarAnimationTest, BonesMoveOverTime) {
    AvatarAnimation wave(AvatarAnimationPreset::Wave);
    const Matrix rest = Bone(wave, static_cast<int>(AvatarBone::ElbowRight));
    wave.Update(Seconds(1.2), false);
    EXPECT_FALSE(Near(rest, Bone(wave, static_cast<int>(AvatarBone::ElbowRight)), 1e-2f));
}

TEST(AvatarAnimationTest, BoneTransformsAreLocalToTheParent) {
    AvatarAnimation animation(AvatarAnimationPreset::Stand0);
    const Matrix head = Bone(animation, static_cast<int>(AvatarBone::Head));
    // The head sits a few centimetres above the neck, not at its model-space height.
    const float offset = head.getTranslationProperty().Length();
    EXPECT_GT(offset, 0.02f);
    EXPECT_LT(offset, 0.2f);
    // The root carries the whole avatar at the floor.
    EXPECT_LT(Bone(animation, 0).getTranslationProperty().Length(), 0.05f);
}

TEST(AvatarAnimationTest, CurrentPositionIsClampedIntoTheLength) {
    AvatarAnimation animation(AvatarAnimationPreset::Clap);
    const auto length = animation.getLengthProperty();
    animation.setCurrentPositionProperty(length + Seconds(1.0));
    EXPECT_EQ(animation.getCurrentPositionProperty(), length);
    animation.setCurrentPositionProperty(Seconds(-1.0));
    EXPECT_EQ(animation.getCurrentPositionProperty(), TimeSpan::Zero);
    animation.setCurrentPositionProperty(Seconds(1.5));
    EXPECT_EQ(animation.getCurrentPositionProperty(), Seconds(1.5));
}

TEST(AvatarAnimationTest, UpdateAdvancesAndClampsWithoutLoop) {
    AvatarAnimation animation(AvatarAnimationPreset::Wave);
    animation.Update(Seconds(1.0), false);
    EXPECT_EQ(animation.getCurrentPositionProperty(), Seconds(1.0));
    animation.Update(Seconds(100.0), false);
    EXPECT_EQ(animation.getCurrentPositionProperty(), animation.getLengthProperty());
    animation.Update(Seconds(-200.0), false);
    EXPECT_EQ(animation.getCurrentPositionProperty(), TimeSpan::Zero);
}

TEST(AvatarAnimationTest, LoopingWrapsByTheRemainderOfTheLength) {
    AvatarAnimation animation(AvatarAnimationPreset::Wave);
    const long long length = animation.getLengthProperty().getTicksProperty();
    animation.Update(TimeSpan::FromTicks(length * 2 + 12345), true);
    EXPECT_EQ(animation.getCurrentPositionProperty().getTicksProperty(), 12345);
    // Backward past zero continues from the end.
    animation.Update(TimeSpan::FromTicks(-12345 - 1000), true);
    EXPECT_EQ(animation.getCurrentPositionProperty().getTicksProperty(), length - 1000);
    // Exactly Length is kept, not wrapped to zero.
    animation.setCurrentPositionProperty(animation.getLengthProperty());
    animation.Update(TimeSpan::Zero, true);
    EXPECT_EQ(animation.getCurrentPositionProperty().getTicksProperty(), length);
}

TEST(AvatarAnimationTest, OverflowingUpdateClampsToTheEnd) {
    AvatarAnimation animation(AvatarAnimationPreset::Stand3);
    animation.Update(Seconds(1.0), false);
    animation.Update(TimeSpan::MaxValue, false);
    EXPECT_EQ(animation.getCurrentPositionProperty(), animation.getLengthProperty());
    animation.Update(TimeSpan::MinValue, false);
    EXPECT_EQ(animation.getCurrentPositionProperty(), TimeSpan::Zero);
}

TEST(AvatarAnimationTest, IdlesLoopSeamlessly) {
    for (auto preset : {AvatarAnimationPreset::Stand0, AvatarAnimationPreset::Stand4, AvatarAnimationPreset::Stand7}) {
        AvatarAnimation animation(preset);
        const auto start = animation.getBoneTransformsProperty();
        animation.setCurrentPositionProperty(animation.getLengthProperty());
        const auto end = animation.getBoneTransformsProperty();
        for (int bone = 0; bone < 71; ++bone) {
            EXPECT_TRUE(Near(start[bone], end[bone], 2e-3f)) << static_cast<int>(preset) << " bone " << bone;
        }
    }
}

TEST(AvatarAnimationTest, ExpressionFollowsTheClip) {
    AvatarAnimation celebrate(AvatarAnimationPreset::Celebrate);
    EXPECT_EQ(celebrate.getExpressionProperty().getMouthProperty(), AvatarMouth::Happy);
    celebrate.setCurrentPositionProperty(Seconds(1.0));
    EXPECT_EQ(celebrate.getExpressionProperty().getMouthProperty(), AvatarMouth::Laughing);
    EXPECT_EQ(celebrate.getExpressionProperty().getLeftEyeProperty(), AvatarEye::Laughing);
    EXPECT_EQ(celebrate.getExpressionProperty().getLeftEyebrowProperty(), AvatarEyebrow::Raised);

    AvatarAnimation idle(AvatarAnimationPreset::Stand0);
    idle.setCurrentPositionProperty(Seconds(1.45));
    EXPECT_EQ(idle.getExpressionProperty().getLeftEyeProperty(), AvatarEye::Blink);
    EXPECT_EQ(idle.getExpressionProperty().getRightEyeProperty(), AvatarEye::Blink);
    idle.setCurrentPositionProperty(Seconds(1.7));
    EXPECT_EQ(idle.getExpressionProperty().getLeftEyeProperty(), AvatarEye::Neutral);

    AvatarAnimation confused(AvatarAnimationPreset::FemaleConfused);
    confused.setCurrentPositionProperty(Seconds(1.0));
    EXPECT_EQ(confused.getExpressionProperty().getLeftEyebrowProperty(), AvatarEyebrow::Confused);
    EXPECT_EQ(confused.getExpressionProperty().getMouthProperty(), AvatarMouth::Confused);
}

TEST(AvatarAnimationTest, AnUndefinedPresetIsAZeroLengthBindPose) {
    AvatarAnimation animation(static_cast<AvatarAnimationPreset>(999));
    EXPECT_EQ(animation.getLengthProperty(), TimeSpan::Zero);
    animation.Update(Seconds(3.0), true);
    EXPECT_EQ(animation.getCurrentPositionProperty(), TimeSpan::Zero);
    AvatarAnimation stand(AvatarAnimationPreset::Stand0);
    EXPECT_EQ(Bone(animation, static_cast<int>(AvatarBone::Head)).getTranslationProperty(),
              Bone(stand, static_cast<int>(AvatarBone::Head)).getTranslationProperty());
    EXPECT_EQ(animation.getExpressionProperty().getMouthProperty(), AvatarMouth::Neutral);
}

TEST(AvatarAnimationTest, IsDisposedDefaultsFalse) {
    AvatarAnimation animation(AvatarAnimationPreset::Stand5);
    EXPECT_FALSE(animation.getIsDisposedProperty());
}

TEST(AvatarAnimationTest, DisposeSetsIsDisposed) {
    AvatarAnimation animation(AvatarAnimationPreset::Stand6);
    animation.Dispose();
    EXPECT_TRUE(animation.getIsDisposedProperty());
}

TEST(AvatarAnimationTest, DisposeIsIdempotent) {
    AvatarAnimation animation(AvatarAnimationPreset::Stand7);
    animation.Dispose();
    EXPECT_NO_THROW(animation.Dispose());
    EXPECT_TRUE(animation.getIsDisposedProperty());
}

TEST(AvatarAnimationTest, UpdateThrowsAfterDispose) {
    AvatarAnimation animation(AvatarAnimationPreset::FemaleLaugh);
    animation.Dispose();
    EXPECT_THROW(animation.Update(System::TimeSpan::Zero, false), System::ObjectDisposedException);
}

TEST(AvatarAnimationTest, ImplementsIAvatarAnimationInterface) {
    AvatarAnimation animation(AvatarAnimationPreset::MaleLaugh);
    IAvatarAnimation& asInterface = animation;
    EXPECT_EQ(asInterface.getLengthProperty(), animation.getLengthProperty());
    EXPECT_GT(asInterface.getLengthProperty(), System::TimeSpan::Zero);
    EXPECT_EQ(asInterface.getBoneTransformsProperty().getCountProperty(), 71);
}
