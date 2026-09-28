// SPDX-License-Identifier: MS-PL
#include <gtest/gtest.h>

#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/GamerServices/AvatarAnimation.hpp"
#include "Microsoft/Xna/Framework/GamerServices/AvatarRenderer.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "System/ArgumentException.hpp"
#include "System/ArgumentNullException.hpp"
#include "System/InvalidOperationException.hpp"
#include "System/ObjectDisposedException.hpp"
#include "CNA/Internal/GamerServices/AvatarDescriptionCodec.hpp"
#include "CNA/Internal/GamerServices/DispatcherGraphics.hpp"
#include "Microsoft/Xna/Framework/GamerServices/AvatarBone.hpp"
#include "Microsoft/Xna/Framework/GamerServices/AvatarDescription.hpp"
#include "Microsoft/Xna/Framework/GamerServices/GamerServicesDispatcher.hpp"
#include "Microsoft/Xna/Framework/Quaternion.hpp"

#include <chrono>
#include <random>
#include <thread>

using namespace Microsoft::Xna::Framework::GamerServices;
using Microsoft::Xna::Framework::Color;
using Microsoft::Xna::Framework::Matrix;
using Microsoft::Xna::Framework::Graphics::GraphicsDevice;

namespace {
AvatarDescription& NoAvatar() {
    static AvatarDescription none(std::vector<SharpRuntime::bytecs>(1021, 0));
    return none;
}

AvatarDescription Describe(AvatarBodyType body, int heightMillimeters) {
    namespace Avatars = CNA::Internal::GamerServices::Avatars;
    std::mt19937 random(static_cast<unsigned>(heightMillimeters));
    auto descriptor = Avatars::randomDescriptor(static_cast<std::uint8_t>(body), random);
    descriptor.heightMillimeters = static_cast<std::uint16_t>(heightMillimeters);
    return AvatarDescription(Avatars::encode(descriptor));
}

AvatarRendererState WaitUntilLoaded(AvatarRenderer& renderer) {
    const auto limit = std::chrono::steady_clock::now() + std::chrono::seconds(20);
    while (renderer.getStateProperty() == AvatarRendererState::Loading && std::chrono::steady_clock::now() < limit) {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    return renderer.getStateProperty();
}

Microsoft::Xna::Framework::Vector3 BindWorld(AvatarRenderer& renderer, int bone) {
    const auto pose = renderer.getBindPoseProperty();
    const auto parents = renderer.getParentBonesProperty();
    Microsoft::Xna::Framework::Vector3 position;
    for (int current = bone; current >= 0; current = parents[current]) {
        position += pose[current].getTranslationProperty();
    }
    return position;
}

struct TestingDevice {
    explicit TestingDevice(GraphicsDevice* device) { CNA::Internal::GamerServices::setDispatcherGraphicsDeviceForTesting(device); }
    ~TestingDevice() { CNA::Internal::GamerServices::setDispatcherGraphicsDeviceForTesting(nullptr); }
};
}

TEST(AvatarRendererTest, BoneCountIs71) {
    EXPECT_EQ(AvatarRenderer::BoneCount, 71);
}

TEST(AvatarRendererTest, ConstructorRejectsANullDescription) {
    EXPECT_THROW(AvatarRenderer renderer(nullptr), System::ArgumentNullException);
    EXPECT_THROW(AvatarRenderer renderer(nullptr, false), System::ArgumentNullException);
}

TEST(AvatarRendererTest, ParentBonesHas71Entries) {
    AvatarRenderer renderer(&NoAvatar());
    EXPECT_EQ(renderer.getParentBonesProperty().getCountProperty(), 71);
}

TEST(AvatarRendererTest, ParentBonesRootHasNoParent) {
    AvatarRenderer renderer(&NoAvatar());
    const auto parentBones = renderer.getParentBonesProperty();
    EXPECT_EQ(parentBones[0], -1);
}

TEST(AvatarRendererTest, ParentBonesExactValuesMatchReferenceAssembly) {
    // Exact values decoded from the real XNA reference assembly, not derived or guessed.
    AvatarRenderer renderer(&NoAvatar());
    const auto parentBones = renderer.getParentBonesProperty();
    EXPECT_EQ(parentBones[1], 0);
    EXPECT_EQ(parentBones[5], 1);
    EXPECT_EQ(parentBones[37], 33);
    EXPECT_EQ(parentBones[70], 60);
}

TEST(AvatarRendererTest, ADescriptionWithoutAnAvatarIsUnavailable) {
    AvatarRenderer renderer(&NoAvatar());
    EXPECT_EQ(renderer.getStateProperty(), AvatarRendererState::Unavailable);
    EXPECT_THROW((void)renderer.getBindPoseProperty(), System::InvalidOperationException);
    // Nothing to draw, and nothing needs a device for it.
    std::vector<Matrix> bones(71);
    EXPECT_NO_THROW(renderer.Draw(bones, AvatarExpression()));
}

TEST(AvatarRendererTest, AnUnreadableDescriptionIsUnavailable) {
    std::vector<SharpRuntime::bytecs> foreign(1021, 7);
    AvatarDescription description(foreign);
    ASSERT_TRUE(description.getIsValidProperty());
    AvatarRenderer renderer(&description);
    EXPECT_EQ(renderer.getStateProperty(), AvatarRendererState::Unavailable);
}

TEST(AvatarRendererTest, AValidDescriptionLoadsAndBecomesReady) {
    auto description = Describe(AvatarBodyType::Male, 1800);
    AvatarRenderer renderer(&description);
    const auto first = renderer.getStateProperty();
    EXPECT_TRUE(first == AvatarRendererState::Loading || first == AvatarRendererState::Ready);
    ASSERT_EQ(WaitUntilLoaded(renderer), AvatarRendererState::Ready);
    const auto pose = renderer.getBindPoseProperty();
    ASSERT_EQ(pose.getCountProperty(), 71);
    for (int bone = 0; bone < 71; ++bone) {
        Microsoft::Xna::Framework::Vector3 scale, translation;
        Microsoft::Xna::Framework::Quaternion rotation;
        ASSERT_TRUE(pose[bone].Decompose(scale, rotation, translation));
        EXPECT_NEAR(rotation.W, 1.0f, 1e-5f) << bone;
    }
    // Local offsets: the head joint sits in the upper part of an avatar 1.8 m tall.
    const auto head = BindWorld(renderer, static_cast<int>(AvatarBone::Head));
    EXPECT_GT(head.Y, 1.8f * 0.7f);
    EXPECT_LT(head.Y, 1.8f);
    EXPECT_LT(pose[static_cast<int>(AvatarBone::Head)].getTranslationProperty().Length(), 0.2f);
}

TEST(AvatarRendererTest, TheBindPoseFollowsTheDescriptionHeight) {
    auto tall = Describe(AvatarBodyType::Female, 2000);
    auto short_ = Describe(AvatarBodyType::Female, 1500);
    AvatarRenderer tallRenderer(&tall);
    AvatarRenderer shortRenderer(&short_);
    ASSERT_EQ(WaitUntilLoaded(tallRenderer), AvatarRendererState::Ready);
    ASSERT_EQ(WaitUntilLoaded(shortRenderer), AvatarRendererState::Ready);
    const float ratio = BindWorld(tallRenderer, static_cast<int>(AvatarBone::Neck)).Y /
                        BindWorld(shortRenderer, static_cast<int>(AvatarBone::Neck)).Y;
    EXPECT_NEAR(ratio, 2000.0f / 1500.0f, 1e-3f);
    // A left bone is on the avatar's left (+X).
    EXPECT_GT(BindWorld(tallRenderer, static_cast<int>(AvatarBone::WristLeft)).X, 0.1f);
    EXPECT_LT(BindWorld(tallRenderer, static_cast<int>(AvatarBone::WristRight)).X, -0.1f);
}

TEST(AvatarRendererTest, DrawRejectsANonDecomposableBoneOnceReady) {
    auto description = Describe(AvatarBodyType::Male, 1750);
    AvatarRenderer renderer(&description);
    ASSERT_EQ(WaitUntilLoaded(renderer), AvatarRendererState::Ready);
    std::vector<Matrix> bones(71, Matrix::getIdentityProperty());
    bones[20] = Matrix();
    EXPECT_THROW(renderer.Draw(bones, AvatarExpression()), System::InvalidOperationException);
}

TEST(AvatarRendererTest, DrawingNeedsInitializedGamerServices) {
    if (GamerServicesDispatcher::getIsInitializedProperty()) {
        GTEST_SKIP() << "another test initialized gamer services in this process";
    }
    auto description = Describe(AvatarBodyType::Female, 1650);
    AvatarRenderer renderer(&description);
    ASSERT_EQ(WaitUntilLoaded(renderer), AvatarRendererState::Ready);
    AvatarAnimation animation(AvatarAnimationPreset::Stand0);
    EXPECT_THROW(renderer.Draw(&animation), System::InvalidOperationException);
}

TEST(AvatarRendererTest, DrawsEveryPresetAndExpressionOnTheDispatcherDevice) {
    GraphicsDevice device;
    TestingDevice testing(&device);
    auto description = Describe(AvatarBodyType::Male, 1820);
    AvatarRenderer renderer(&description);
    // Loading draws the standard effect (or nothing yet) without failing.
    AvatarAnimation stand(AvatarAnimationPreset::Stand0);
    EXPECT_NO_THROW(renderer.Draw(&stand));
    ASSERT_EQ(WaitUntilLoaded(renderer), AvatarRendererState::Ready);
    for (int preset = 0; preset <= static_cast<int>(AvatarAnimationPreset::MaleYawn); ++preset) {
        AvatarAnimation animation(static_cast<AvatarAnimationPreset>(preset));
        animation.Update(System::TimeSpan::FromSeconds(1.0), true);
        EXPECT_NO_THROW(renderer.Draw(&animation)) << preset;
    }
    std::vector<Matrix> bones(71, Matrix::getIdentityProperty());
    for (int state = 0; state < 14; ++state) {
        AvatarExpression expression;
        expression.setLeftEyeProperty(static_cast<AvatarEye>(state));
        expression.setRightEyeProperty(static_cast<AvatarEye>(13 - state));
        expression.setMouthProperty(static_cast<AvatarMouth>(state));
        expression.setLeftEyebrowProperty(static_cast<AvatarEyebrow>(state % 5));
        expression.setRightEyebrowProperty(static_cast<AvatarEyebrow>((state + 2) % 5));
        EXPECT_NO_THROW(renderer.Draw(bones, expression));
    }
}

TEST(AvatarRendererTest, DisposeWhileLoadingIsSafe) {
    for (int attempt = 0; attempt < 4; ++attempt) {
        auto description = AvatarDescription::CreateRandom();
        AvatarRenderer renderer(&description);
        renderer.Dispose();
        EXPECT_THROW((void)renderer.getStateProperty(), System::ObjectDisposedException);
    }
}

TEST(AvatarRendererTest, WorldViewProjectionDefaultToIdentity) {
    AvatarRenderer renderer(&NoAvatar());
    EXPECT_EQ(renderer.getWorldProperty(), Matrix::getIdentityProperty());
    EXPECT_EQ(renderer.getViewProperty(), Matrix::getIdentityProperty());
    EXPECT_EQ(renderer.getProjectionProperty(), Matrix::getIdentityProperty());
}

TEST(AvatarRendererTest, WorldGetSet) {
    AvatarRenderer renderer(&NoAvatar());
    Matrix scaled = Matrix::CreateScale(2.0f, 2.0f, 2.0f);
    renderer.setWorldProperty(scaled);
    EXPECT_EQ(renderer.getWorldProperty(), scaled);
}

TEST(AvatarRendererTest, ViewGetSet) {
    AvatarRenderer renderer(&NoAvatar());
    Matrix scaled = Matrix::CreateScale(3.0f, 3.0f, 3.0f);
    renderer.setViewProperty(scaled);
    EXPECT_EQ(renderer.getViewProperty(), scaled);
}

TEST(AvatarRendererTest, ProjectionGetSet) {
    AvatarRenderer renderer(&NoAvatar());
    Matrix scaled = Matrix::CreateScale(4.0f, 4.0f, 4.0f);
    renderer.setProjectionProperty(scaled);
    EXPECT_EQ(renderer.getProjectionProperty(), scaled);
}

TEST(AvatarRendererTest, LightColorGetSet) {
    AvatarRenderer renderer(&NoAvatar());
    Microsoft::Xna::Framework::Vector3 color(1.0f, 0.5f, 0.25f);
    renderer.setLightColorProperty(color);
    EXPECT_EQ(renderer.getLightColorProperty(), color);
}

TEST(AvatarRendererTest, LightDirectionGetSet) {
    AvatarRenderer renderer(&NoAvatar());
    Microsoft::Xna::Framework::Vector3 direction(0.0f, -1.0f, 0.0f);
    renderer.setLightDirectionProperty(direction);
    EXPECT_EQ(renderer.getLightDirectionProperty(), direction);
}

TEST(AvatarRendererTest, AmbientLightColorGetSet) {
    AvatarRenderer renderer(&NoAvatar());
    Microsoft::Xna::Framework::Vector3 ambient(0.1f, 0.1f, 0.1f);
    renderer.setAmbientLightColorProperty(ambient);
    EXPECT_EQ(renderer.getAmbientLightColorProperty(), ambient);
}

TEST(AvatarRendererTest, IsDisposedDefaultsFalse) {
    AvatarRenderer renderer(&NoAvatar());
    EXPECT_FALSE(renderer.getIsDisposedProperty());
}

TEST(AvatarRendererTest, DrawWithWrongBoneCountThrows) {
    AvatarRenderer renderer(&NoAvatar());
    std::vector<Matrix> tooFew(70);
    AvatarExpression expression;
    EXPECT_THROW(renderer.Draw(tooFew, expression), System::ArgumentException);
}

// Task 1.5 / audit_net.md Medium finding: a null animation used to dereference unconditionally
// (undefined behavior) instead of throwing a catchable exception.
TEST(AvatarRendererTest, DrawWithNullAnimationThrowsArgumentNull) {
    AvatarRenderer renderer(&NoAvatar());
    EXPECT_THROW(renderer.Draw(static_cast<IAvatarAnimation*>(nullptr)), System::ArgumentNullException);
}

TEST(AvatarRendererTest, DisposeSetsIsDisposed) {
    AvatarRenderer renderer(&NoAvatar());
    renderer.Dispose();
    EXPECT_TRUE(renderer.getIsDisposedProperty());
}

TEST(AvatarRendererTest, DisposeIsIdempotent) {
    AvatarRenderer renderer(&NoAvatar());
    renderer.Dispose();
    EXPECT_NO_THROW(renderer.Dispose());
    EXPECT_TRUE(renderer.getIsDisposedProperty());
}

TEST(AvatarRendererTest, StateThrowsAfterDispose) {
    AvatarRenderer renderer(&NoAvatar());
    renderer.Dispose();
    EXPECT_THROW((void)renderer.getStateProperty(), System::ObjectDisposedException);
}

TEST(AvatarRendererTest, BindPoseThrowsAfterDispose) {
    AvatarRenderer renderer(&NoAvatar());
    renderer.Dispose();
    EXPECT_THROW((void)renderer.getBindPoseProperty(), System::ObjectDisposedException);
}

TEST(AvatarRendererTest, DrawThrowsAfterDispose) {
    AvatarRenderer renderer(&NoAvatar());
    renderer.Dispose();
    std::vector<Matrix> bones(71);
    AvatarExpression expression;
    EXPECT_THROW(renderer.Draw(bones, expression), System::ObjectDisposedException);
}
