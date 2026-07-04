// SPDX-License-Identifier: MS-PL
#include <gtest/gtest.h>

#include "Microsoft/Xna/Framework/GamerServices/AvatarAnimation.hpp"
#include "Microsoft/Xna/Framework/GamerServices/AvatarRenderer.hpp"
#include "System/ArgumentException.hpp"
#include "System/InvalidOperationException.hpp"
#include "System/ObjectDisposedException.hpp"

using namespace Microsoft::Xna::Framework::GamerServices;
using Microsoft::Xna::Framework::Matrix;

TEST(AvatarRendererTest, BoneCountIs71) {
    EXPECT_EQ(AvatarRenderer::BoneCount, 71);
}

TEST(AvatarRendererTest, ConstructorsIgnoreTheirArguments) {
    // The real XNA implementation never reads either constructor's arguments - every instance
    // ends up in an identical, permanently Unavailable state. Preserved exactly, not "fixed."
    AvatarRenderer oneArg(nullptr);
    AvatarRenderer twoArgFalse(nullptr, false);
    AvatarRenderer twoArgTrue(nullptr, true);

    EXPECT_EQ(oneArg.getStateProperty(), twoArgFalse.getStateProperty());
    EXPECT_EQ(twoArgFalse.getStateProperty(), twoArgTrue.getStateProperty());
    EXPECT_EQ(oneArg.getParentBonesProperty().getCountProperty(),
              twoArgTrue.getParentBonesProperty().getCountProperty());
}

TEST(AvatarRendererTest, ParentBonesHas71Entries) {
    AvatarRenderer renderer(nullptr);
    EXPECT_EQ(renderer.getParentBonesProperty().getCountProperty(), 71);
}

TEST(AvatarRendererTest, ParentBonesRootHasNoParent) {
    AvatarRenderer renderer(nullptr);
    const auto parentBones = renderer.getParentBonesProperty();
    EXPECT_EQ(parentBones[0], -1);
}

TEST(AvatarRendererTest, ParentBonesExactValuesMatchReferenceAssembly) {
    // Exact values decoded from the real XNA reference assembly, not derived or guessed.
    AvatarRenderer renderer(nullptr);
    const auto parentBones = renderer.getParentBonesProperty();
    EXPECT_EQ(parentBones[1], 0);
    EXPECT_EQ(parentBones[5], 1);
    EXPECT_EQ(parentBones[37], 33);
    EXPECT_EQ(parentBones[70], 60);
}

TEST(AvatarRendererTest, StateStartsUnavailable) {
    AvatarRenderer renderer(nullptr);
    EXPECT_EQ(renderer.getStateProperty(), AvatarRendererState::Unavailable);
}

TEST(AvatarRendererTest, StateStaysUnavailableOnRepeatedReads) {
    // get_State() forces itself to Unavailable on every single read in the real implementation -
    // not just an initial value. Nothing anywhere ever sets it to Ready or Loading. Verified via
    // repeated reads, matching that surprising but real behavior exactly.
    AvatarRenderer renderer(nullptr);
    EXPECT_EQ(renderer.getStateProperty(), AvatarRendererState::Unavailable);
    EXPECT_EQ(renderer.getStateProperty(), AvatarRendererState::Unavailable);
    EXPECT_EQ(renderer.getStateProperty(), AvatarRendererState::Unavailable);
}

TEST(AvatarRendererTest, BindPoseThrowsSinceStateIsNeverReady) {
    AvatarRenderer renderer(nullptr);
    EXPECT_THROW((void)renderer.getBindPoseProperty(), System::InvalidOperationException);
}

TEST(AvatarRendererTest, WorldViewProjectionDefaultToIdentity) {
    AvatarRenderer renderer(nullptr);
    EXPECT_EQ(renderer.getWorldProperty(), Matrix::getIdentityProperty());
    EXPECT_EQ(renderer.getViewProperty(), Matrix::getIdentityProperty());
    EXPECT_EQ(renderer.getProjectionProperty(), Matrix::getIdentityProperty());
}

TEST(AvatarRendererTest, WorldGetSet) {
    AvatarRenderer renderer(nullptr);
    Matrix scaled = Matrix::CreateScale(2.0f, 2.0f, 2.0f);
    renderer.setWorldProperty(scaled);
    EXPECT_EQ(renderer.getWorldProperty(), scaled);
}

TEST(AvatarRendererTest, ViewGetSet) {
    AvatarRenderer renderer(nullptr);
    Matrix scaled = Matrix::CreateScale(3.0f, 3.0f, 3.0f);
    renderer.setViewProperty(scaled);
    EXPECT_EQ(renderer.getViewProperty(), scaled);
}

TEST(AvatarRendererTest, ProjectionGetSet) {
    AvatarRenderer renderer(nullptr);
    Matrix scaled = Matrix::CreateScale(4.0f, 4.0f, 4.0f);
    renderer.setProjectionProperty(scaled);
    EXPECT_EQ(renderer.getProjectionProperty(), scaled);
}

TEST(AvatarRendererTest, LightColorGetSet) {
    AvatarRenderer renderer(nullptr);
    Microsoft::Xna::Framework::Vector3 color(1.0f, 0.5f, 0.25f);
    renderer.setLightColorProperty(color);
    EXPECT_EQ(renderer.getLightColorProperty(), color);
}

TEST(AvatarRendererTest, LightDirectionGetSet) {
    AvatarRenderer renderer(nullptr);
    Microsoft::Xna::Framework::Vector3 direction(0.0f, -1.0f, 0.0f);
    renderer.setLightDirectionProperty(direction);
    EXPECT_EQ(renderer.getLightDirectionProperty(), direction);
}

TEST(AvatarRendererTest, AmbientLightColorGetSet) {
    AvatarRenderer renderer(nullptr);
    Microsoft::Xna::Framework::Vector3 ambient(0.1f, 0.1f, 0.1f);
    renderer.setAmbientLightColorProperty(ambient);
    EXPECT_EQ(renderer.getAmbientLightColorProperty(), ambient);
}

TEST(AvatarRendererTest, IsDisposedDefaultsFalse) {
    AvatarRenderer renderer(nullptr);
    EXPECT_FALSE(renderer.getIsDisposedProperty());
}

TEST(AvatarRendererTest, DrawWithWrongBoneCountThrows) {
    AvatarRenderer renderer(nullptr);
    std::vector<Matrix> tooFew(70);
    AvatarExpression expression;
    EXPECT_THROW(renderer.Draw(tooFew, expression), System::ArgumentException);
}

TEST(AvatarRendererTest, DrawWithCorrectBoneCountIsNoOp) {
    AvatarRenderer renderer(nullptr);
    std::vector<Matrix> bones(71);
    AvatarExpression expression;
    EXPECT_NO_THROW(renderer.Draw(bones, expression));
}

TEST(AvatarRendererTest, DrawFromAvatarAnimationDelegatesCorrectly) {
    AvatarRenderer renderer(nullptr);
    AvatarAnimation animation(AvatarAnimationPreset::Stand0);
    EXPECT_NO_THROW(renderer.Draw(static_cast<IAvatarAnimation*>(&animation)));
}

TEST(AvatarRendererTest, DisposeSetsIsDisposed) {
    AvatarRenderer renderer(nullptr);
    renderer.Dispose();
    EXPECT_TRUE(renderer.getIsDisposedProperty());
}

TEST(AvatarRendererTest, DisposeIsIdempotent) {
    AvatarRenderer renderer(nullptr);
    renderer.Dispose();
    EXPECT_NO_THROW(renderer.Dispose());
    EXPECT_TRUE(renderer.getIsDisposedProperty());
}

TEST(AvatarRendererTest, StateThrowsAfterDispose) {
    AvatarRenderer renderer(nullptr);
    renderer.Dispose();
    EXPECT_THROW((void)renderer.getStateProperty(), System::ObjectDisposedException);
}

TEST(AvatarRendererTest, BindPoseThrowsAfterDispose) {
    AvatarRenderer renderer(nullptr);
    renderer.Dispose();
    EXPECT_THROW((void)renderer.getBindPoseProperty(), System::ObjectDisposedException);
}

TEST(AvatarRendererTest, DrawThrowsAfterDispose) {
    AvatarRenderer renderer(nullptr);
    renderer.Dispose();
    std::vector<Matrix> bones(71);
    AvatarExpression expression;
    EXPECT_THROW(renderer.Draw(bones, expression), System::ObjectDisposedException);
}
