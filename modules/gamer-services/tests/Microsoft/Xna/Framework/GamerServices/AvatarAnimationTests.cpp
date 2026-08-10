// SPDX-License-Identifier: MS-PL
#include <gtest/gtest.h>

#include "Microsoft/Xna/Framework/GamerServices/AvatarAnimation.hpp"
#include "System/ObjectDisposedException.hpp"

using namespace Microsoft::Xna::Framework::GamerServices;
using Microsoft::Xna::Framework::Matrix;

TEST(AvatarAnimationTest, ConstructorIgnoresPreset) {
    // The real XNA implementation never reads its animationPreset argument - every instance
    // behaves identically regardless of which preset was requested. Preserved exactly.
    AvatarAnimation stand(AvatarAnimationPreset::Stand0);
    AvatarAnimation wave(AvatarAnimationPreset::Wave);

    EXPECT_EQ(stand.getLengthProperty(), wave.getLengthProperty());
    EXPECT_EQ(stand.getCurrentPositionProperty(), wave.getCurrentPositionProperty());
    EXPECT_EQ(stand.getBoneTransformsProperty().getCountProperty(),
              wave.getBoneTransformsProperty().getCountProperty());
}

TEST(AvatarAnimationTest, BoneTransformsHas71ZeroMatrices) {
    AvatarAnimation animation(AvatarAnimationPreset::Stand0);
    const auto bones = animation.getBoneTransformsProperty();
    ASSERT_EQ(bones.getCountProperty(), 71);
    for (int i = 0; i < bones.getCountProperty(); ++i) {
        EXPECT_EQ(bones[i], Matrix());
    }
}

TEST(AvatarAnimationTest, LengthIsAlwaysZero) {
    AvatarAnimation animation(AvatarAnimationPreset::Celebrate);
    EXPECT_EQ(animation.getLengthProperty(), System::TimeSpan::Zero);
}

TEST(AvatarAnimationTest, CurrentPositionStartsAtZero) {
    AvatarAnimation animation(AvatarAnimationPreset::Clap);
    EXPECT_EQ(animation.getCurrentPositionProperty(), System::TimeSpan::Zero);
}

TEST(AvatarAnimationTest, SetCurrentPositionAlwaysCollapsesToZero) {
    // Since Length is always Zero (see the constructor's own known quirk), every
    // set_CurrentPosition immediately re-clamps back to Zero - matching the real
    // implementation's set_CurrentPosition -> Update(Zero, false) call chain.
    AvatarAnimation animation(AvatarAnimationPreset::Wave);

    animation.setCurrentPositionProperty(System::TimeSpan::FromSeconds(5.0));
    EXPECT_EQ(animation.getCurrentPositionProperty(), System::TimeSpan::Zero);

    animation.setCurrentPositionProperty(System::TimeSpan::FromSeconds(-5.0));
    EXPECT_EQ(animation.getCurrentPositionProperty(), System::TimeSpan::Zero);
}

TEST(AvatarAnimationTest, UpdateWithPositiveElapsedClampsToZero) {
    AvatarAnimation animation(AvatarAnimationPreset::Stand1);
    animation.Update(System::TimeSpan::FromSeconds(1.0), false);
    EXPECT_EQ(animation.getCurrentPositionProperty(), System::TimeSpan::Zero);
}

TEST(AvatarAnimationTest, UpdateWithLoopStillClampsToZeroSinceLengthIsZero) {
    // loop=true only changes behavior when Length is non-zero; since it's always Zero here,
    // the loop flag has no observable effect - preserved exactly, not "fixed."
    AvatarAnimation animation(AvatarAnimationPreset::Stand2);
    animation.Update(System::TimeSpan::FromSeconds(1.0), true);
    EXPECT_EQ(animation.getCurrentPositionProperty(), System::TimeSpan::Zero);
}

TEST(AvatarAnimationTest, UpdateWithNegativeElapsedClampsToZero) {
    AvatarAnimation animation(AvatarAnimationPreset::Stand3);
    animation.Update(System::TimeSpan::FromSeconds(-1.0), false);
    EXPECT_EQ(animation.getCurrentPositionProperty(), System::TimeSpan::Zero);
}

TEST(AvatarAnimationTest, ExpressionDefaultsToNeutral) {
    AvatarAnimation animation(AvatarAnimationPreset::Stand4);
    AvatarExpression expression = animation.getExpressionProperty();
    EXPECT_EQ(expression.getMouthProperty(), AvatarMouth::Neutral);
    EXPECT_EQ(expression.getLeftEyeProperty(), AvatarEye::Neutral);
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
    EXPECT_EQ(asInterface.getLengthProperty(), System::TimeSpan::Zero);
    EXPECT_EQ(asInterface.getBoneTransformsProperty().getCountProperty(), 71);
}

// --- Real-rendering extension (NOXNA) ---

TEST(AvatarAnimationTest, RealClipNameDefaultsToPresetName) {
    AvatarAnimation wave(AvatarAnimationPreset::Wave);
    EXPECT_EQ(wave.GetRealClipNameEXT(), "Wave");

    AvatarAnimation nails(AvatarAnimationPreset::FemaleIdleCheckNails);
    EXPECT_EQ(nails.GetRealClipNameEXT(), "FemaleIdleCheckNails");
}

TEST(AvatarAnimationTest, RealClipNameRoundTrips) {
    AvatarAnimation animation(AvatarAnimationPreset::Stand0);
    animation.SetRealClipNameEXT("SubstituteIdle");
    EXPECT_EQ(animation.GetRealClipNameEXT(), "SubstituteIdle");
}
