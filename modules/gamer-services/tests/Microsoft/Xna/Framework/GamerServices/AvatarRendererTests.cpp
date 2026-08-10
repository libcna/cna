// SPDX-License-Identifier: MS-PL
#include <gtest/gtest.h>

#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/GamerServices/AvatarAnimation.hpp"
#include "Microsoft/Xna/Framework/GamerServices/AvatarAppearanceEXT.hpp"
#include "Microsoft/Xna/Framework/GamerServices/AvatarRenderer.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "System/ArgumentException.hpp"
#include "System/ArgumentNullException.hpp"
#include "System/InvalidOperationException.hpp"
#include "System/ObjectDisposedException.hpp"
#include "System/TimeSpan.hpp"
#include "AvatarRendererTestAccess.hpp"

using namespace Microsoft::Xna::Framework::GamerServices;
using Microsoft::Xna::Framework::Color;
using Microsoft::Xna::Framework::Matrix;
using Microsoft::Xna::Framework::Graphics::GraphicsDevice;

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

// Task 1.5 / audit_net.md Medium finding: a null animation used to dereference unconditionally
// (undefined behavior) instead of throwing a catchable exception.
TEST(AvatarRendererTest, DrawWithNullAnimationThrowsArgumentNull) {
    AvatarRenderer renderer(nullptr);
    EXPECT_THROW(renderer.Draw(static_cast<IAvatarAnimation*>(nullptr)), System::ArgumentNullException);
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

// --- Real-rendering extension (NOXNA) ---
// EnableRealRenderingEXT itself needs a real GraphicsDevice, which (consistent with every
// other GPU-resource-touching type in this codebase) is exercised via an examples/ integration
// test, not a CnaTests unit test. These tests cover the parts reachable without a device.

TEST(AvatarRendererTest, RealRenderingDisabledByDefault) {
    AvatarRenderer renderer(nullptr);
    EXPECT_FALSE(renderer.IsRealRenderingEnabledEXT());
}

TEST(AvatarRendererTest, DrawRealThrowsInvalidOperationWhenNotEnabled) {
    AvatarRenderer renderer(nullptr);
    EXPECT_THROW(
        renderer.DrawRealEXT("Wave", System::TimeSpan::Zero, false),
        System::InvalidOperationException);
}

TEST(AvatarRendererTest, DrawRealThrowsAfterDispose) {
    AvatarRenderer renderer(nullptr);
    renderer.Dispose();
    EXPECT_THROW(
        renderer.DrawRealEXT("Wave", System::TimeSpan::Zero, false),
        System::ObjectDisposedException);
}

TEST(AvatarRendererTest, SetAppearanceDoesNotThrowWithoutRealRendering) {
    AvatarRenderer renderer(nullptr);
    AvatarAppearanceEXT appearance;
    EXPECT_NO_THROW(renderer.SetAppearanceEXT(appearance));
}

// Task 11.6: unlike DrawRealEXT/Draw/getStateProperty/getBindPoseProperty (which all already
// threw ObjectDisposedException), EnableRealRenderingEXT/SetAppearanceEXT used to silently
// succeed after Dispose() - EnableRealRenderingEXT even re-populated realDevice_/realModel_/
// realEffect_, effectively "undisposing" the object.
TEST(AvatarRendererTest, EnableRealRenderingThrowsAfterDispose) {
    GraphicsDevice device;
    AvatarRenderer renderer(nullptr);
    renderer.Dispose();
    EXPECT_THROW(
        renderer.EnableRealRenderingEXT(device, nullptr),
        System::ObjectDisposedException);
}

// Task 1.6 / audit_net.md Medium finding: a null/empty model used to be silently accepted here
// and only surfaced later, inside DrawRealEXT, as InvalidOperationException("real rendering is
// disabled") - misleading, since that exception says nothing about the null model actually
// passed. Confirms it is now rejected at this call site instead.
TEST(AvatarRendererTest, EnableRealRenderingThrowsArgumentNullForNullModel) {
    GraphicsDevice device;
    AvatarRenderer renderer(nullptr);
    EXPECT_THROW(
        renderer.EnableRealRenderingEXT(device, nullptr),
        System::ArgumentNullException);
    EXPECT_FALSE(renderer.IsRealRenderingEnabledEXT());
}

TEST(AvatarRendererTest, SetAppearanceThrowsAfterDispose) {
    AvatarRenderer renderer(nullptr);
    renderer.Dispose();
    AvatarAppearanceEXT appearance;
    EXPECT_THROW(renderer.SetAppearanceEXT(appearance), System::ObjectDisposedException);
}

// --- PartTintEXT substring-match routing (Task 13.1) ---
// The only other coverage (avatar_tint_routing_integration_test.cpp) goes through DrawRealEXT +
// real GPU pixel readback, and only ever exercised Hair/Shirt. These tests use
// AvatarRendererTestAccess for direct, GPU-independent coverage of every routing branch.

namespace {
    AvatarAppearanceEXT MakeDistinctAppearance() {
        AvatarAppearanceEXT appearance;
        appearance.setHairColorProperty(Color(10, 0, 0, 255));
        appearance.setShirtColorProperty(Color(0, 20, 0, 255));
        appearance.setPantsColorProperty(Color(0, 0, 30, 255));
        appearance.setShoesColorProperty(Color(40, 40, 0, 255));
        appearance.setSkinColorProperty(Color(0, 40, 40, 255));
        return appearance;
    }
}

TEST(AvatarRendererTest, PartTintRoutesHairSubstring) {
    AvatarRenderer renderer(nullptr);
    auto appearance = MakeDistinctAppearance();
    renderer.SetAppearanceEXT(appearance);
    EXPECT_EQ(appearance.getHairColorProperty(),
              AvatarRendererTestAccess::PartTintEXT(renderer, "CNAAvatarHair"));
}

TEST(AvatarRendererTest, PartTintRoutesShirtSubstring) {
    AvatarRenderer renderer(nullptr);
    auto appearance = MakeDistinctAppearance();
    renderer.SetAppearanceEXT(appearance);
    EXPECT_EQ(appearance.getShirtColorProperty(),
              AvatarRendererTestAccess::PartTintEXT(renderer, "CNAAvatarShirt"));
}

TEST(AvatarRendererTest, PartTintRoutesPantsSubstring) {
    AvatarRenderer renderer(nullptr);
    auto appearance = MakeDistinctAppearance();
    renderer.SetAppearanceEXT(appearance);
    EXPECT_EQ(appearance.getPantsColorProperty(),
              AvatarRendererTestAccess::PartTintEXT(renderer, "CNAAvatarPants"));
}

TEST(AvatarRendererTest, PartTintRoutesShoesSubstring) {
    AvatarRenderer renderer(nullptr);
    auto appearance = MakeDistinctAppearance();
    renderer.SetAppearanceEXT(appearance);
    EXPECT_EQ(appearance.getShoesColorProperty(),
              AvatarRendererTestAccess::PartTintEXT(renderer, "CNAAvatarShoes"));
}

TEST(AvatarRendererTest, PartTintFallsBackToSkinColorWhenNoKeywordMatches) {
    AvatarRenderer renderer(nullptr);
    auto appearance = MakeDistinctAppearance();
    renderer.SetAppearanceEXT(appearance);
    EXPECT_EQ(appearance.getSkinColorProperty(),
              AvatarRendererTestAccess::PartTintEXT(renderer, "CNAAvatarBody"));
}

// Task 11.17's own real-bug precedent: matching was once exact-equality against a lowercase
// literal ("hair"), which never matched real part names like "CNAAvatarHair". Confirms the fix
// (substring `find`) is still genuinely case-sensitive - a lowercase keyword must NOT match.
TEST(AvatarRendererTest, PartTintIsCaseSensitive) {
    AvatarRenderer renderer(nullptr);
    auto appearance = MakeDistinctAppearance();
    renderer.SetAppearanceEXT(appearance);
    EXPECT_EQ(appearance.getSkinColorProperty(),
              AvatarRendererTestAccess::PartTintEXT(renderer, "cnaavatarhair"));
}

// A part name containing more than one garment keyword must route to whichever keyword is
// checked first (Hair), not silently match a later branch instead.
TEST(AvatarRendererTest, PartTintFirstMatchWinsOnSubstringCollision) {
    AvatarRenderer renderer(nullptr);
    auto appearance = MakeDistinctAppearance();
    renderer.SetAppearanceEXT(appearance);
    EXPECT_EQ(appearance.getHairColorProperty(),
              AvatarRendererTestAccess::PartTintEXT(renderer, "CNAAvatarHairShirt"));
}
