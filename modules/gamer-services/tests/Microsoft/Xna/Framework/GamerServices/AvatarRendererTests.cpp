// SPDX-License-Identifier: MS-PL
#include <gtest/gtest.h>

#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/GamerServices/AvatarAnimation.hpp"
#include "Microsoft/Xna/Framework/GamerServices/AvatarRenderer.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionNormalTextureSkinned.hpp"
#include "System/ArgumentException.hpp"
#include "System/ArgumentNullException.hpp"
#include "System/InvalidOperationException.hpp"
#include "System/ObjectDisposedException.hpp"
#include "CNA/Internal/GamerServices/AvatarDescriptionCodec.hpp"
#include "CNA/Internal/GamerServices/AvatarAssets.hpp"
#include "CNA/Internal/GamerServices/DispatcherGraphics.hpp"
#include "Microsoft/Xna/Framework/GamerServices/AvatarBone.hpp"
#include "Microsoft/Xna/Framework/GamerServices/AvatarDescription.hpp"
#include "Microsoft/Xna/Framework/GamerServices/GamerServicesDispatcher.hpp"
#include "Microsoft/Xna/Framework/Quaternion.hpp"
#include "CNA/GraphicsCapability.hpp"

#include <chrono>
#include <random>
#include <stdexcept>
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
    // The XNA avatar faces -Z, so its left is -X (the catalogs are authored facing +Z).
    EXPECT_LT(BindWorld(tallRenderer, static_cast<int>(AvatarBone::WristLeft)).X, -0.1f);
    EXPECT_GT(BindWorld(tallRenderer, static_cast<int>(AvatarBone::WristRight)).X, 0.1f);
}

TEST(AvatarRendererTest, DrawRejectsANonDecomposableBoneOnceReady) {
    auto description = Describe(AvatarBodyType::Male, 1750);
    AvatarRenderer renderer(&description);
    ASSERT_EQ(WaitUntilLoaded(renderer), AvatarRendererState::Ready);
    std::vector<Matrix> bones(71, Matrix::getIdentityProperty());
    bones[20] = Matrix();
    EXPECT_THROW(renderer.Draw(bones, AvatarExpression()), System::InvalidOperationException);
}

TEST(AvatarRendererTest, ALoadCompletingInsideDrawWaitsUntilTheCallerCanUpdateItsAnimation) {
    auto description = Describe(AvatarBodyType::Female, 1710);

    // Keep an identical model alive in the cache so the renderer under test starts Loading with
    // an already-complete asynchronous result. This deterministically places the Loading->Ready
    // transition inside its first Draw rather than depending on a worker-thread race.
    AvatarRenderer cachePrimer(&description, false);
    ASSERT_EQ(WaitUntilLoaded(cachePrimer), AvatarRendererState::Ready);
    AvatarRenderer renderer(&description, false);
    std::vector<Matrix> notYetUpdated(71);

    EXPECT_NO_THROW(renderer.Draw(notYetUpdated, AvatarExpression()));
    ASSERT_EQ(renderer.getStateProperty(), AvatarRendererState::Ready);
    EXPECT_THROW(renderer.Draw(notYetUpdated, AvatarExpression()), System::InvalidOperationException);
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

TEST(AvatarRendererTest, CachedCompletionReleasesItsLoadOnlyAfterUnlocking) {
    auto description = Describe(AvatarBodyType::Female, 1730);
    AvatarRenderer cachePrimer(&description, false);
    ASSERT_EQ(WaitUntilLoaded(cachePrimer), AvatarRendererState::Ready);

    // The live primer retains the cached model. This renderer therefore owns its
    // completed AvatarLoad alone: no worker can accidentally extend its lifetime.
    // tools/graphics/avatar_load_lifetime.gdb checks the actual free/unlock order
    // on this path, including the unlock in the platform's uninstrumented libc.
    AvatarRenderer renderer(&description, false);
    ASSERT_EQ(renderer.getStateProperty(), AvatarRendererState::Ready);
    const auto actual = renderer.getBindPoseProperty();
    const auto expected = cachePrimer.getBindPoseProperty();
    ASSERT_EQ(actual.getCountProperty(), AvatarRenderer::BoneCount);
    for (int bone = 0; bone < AvatarRenderer::BoneCount; ++bone) {
        EXPECT_EQ(static_cast<Matrix>(actual[bone]), static_cast<Matrix>(expected[bone]));
    }
    cachePrimer.Dispose();
    EXPECT_EQ(renderer.getStateProperty(), AvatarRendererState::Ready);
    EXPECT_EQ(renderer.getBindPoseProperty().getCountProperty(), AvatarRenderer::BoneCount);
}

TEST(AvatarRendererTest, DrawsEveryPresetAndExpressionOnTheDispatcherDevice) {
    GraphicsDevice device;
    if (!device.SupportsCapability(CNA::GraphicsCapability::ThreeD))
        GTEST_SKIP() << "renderer has no 3D pipeline (GraphicsCapability::ThreeD is false)";
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

TEST(AvatarRendererTest, EveryFormat3FeatureDrawsAndANewDeviceGetsItsOwnResources) {
    // An avatar using everything description format 3 adds (eyebrows, ears, every accessory on a
    // side, every makeup style, the extra colours, proportions, the second face block) draws on the
    // dispatcher's device; when the game's device is replaced, the renderer builds its resources
    // again on the new one rather than drawing with the old one's.
    namespace Avatars = CNA::Internal::GamerServices::Avatars;
    const auto catalog = Avatars::embeddedManifest(Avatars::BaseCatalogVersion);
    std::mt19937 random(9);
    auto descriptor = Avatars::randomDescriptor(1, random);
    auto first = [&](Avatars::AvatarItemSlot slot) { return catalog->itemsOf(slot).front()->id; };
    descriptor.eyebrows = first(Avatars::AvatarItemSlot::Eyebrows);
    descriptor.ears = catalog->itemsOf(Avatars::AvatarItemSlot::Ears).back()->id;
    for (const auto slot : {Avatars::AvatarItemSlot::Earrings, Avatars::AvatarItemSlot::Wristwear, Avatars::AvatarItemSlot::Rings}) {
        descriptor.accessories[Avatars::accessoryIndex(slot)] = first(slot);
    }
    descriptor.sides[Avatars::accessoryIndex(Avatars::AvatarItemSlot::Wristwear)] = Avatars::AvatarSides::Left;
    descriptor.sides[Avatars::accessoryIndex(Avatars::AvatarItemSlot::Rings)] = Avatars::AvatarSides::Right;
    for (const auto slot : {Avatars::AvatarItemSlot::EyeShadow, Avatars::AvatarItemSlot::Eyeliner, Avatars::AvatarItemSlot::Blush,
                            Avatars::AvatarItemSlot::Lips}) {
        descriptor.makeup[Avatars::makeupIndex(slot)] = first(slot);
    }
    descriptor.colors[static_cast<std::size_t>(Avatars::AvatarColorSlot::Lips)] = {180, 60, 70};
    descriptor.proportion = 90;
    descriptor.face[24] = 200;
    const auto bytes = Avatars::encode(descriptor);
    ASSERT_EQ(bytes[0], Avatars::ExtendedFormatVersion);
    AvatarDescription description(std::vector<SharpRuntime::bytecs>(bytes.begin(), bytes.end()));
    AvatarRenderer renderer(&description);
    ASSERT_EQ(WaitUntilLoaded(renderer), AvatarRendererState::Ready);
    AvatarAnimation wave(AvatarAnimationPreset::Wave);
    wave.Update(System::TimeSpan::FromSeconds(0.8), true);
    {
        GraphicsDevice device;
        if (!device.SupportsCapability(CNA::GraphicsCapability::ThreeD))
            GTEST_SKIP() << "renderer has no 3D pipeline (GraphicsCapability::ThreeD is false)";
        TestingDevice testing(&device);
        EXPECT_NO_THROW(renderer.Draw(&wave));
        AvatarExpression blink;
        blink.setLeftEyeProperty(AvatarEye::Blink);
        blink.setRightEyeProperty(AvatarEye::Blink);
        const auto bones = wave.getBoneTransformsProperty();
        EXPECT_NO_THROW(renderer.Draw(std::vector<Matrix>(bones.begin(), bones.end()), blink));
    }
    GraphicsDevice replacement;
    TestingDevice testing(&replacement);
    EXPECT_NO_THROW(renderer.Draw(&wave));
    EXPECT_NO_THROW(renderer.Draw(&wave));
}

// plans/plan_apple_m4.md AM4-008: the GPU upload used to set its "uploaded" marker before the
// buffers existed, so a renderer that refuses vertex buffers left a half-built upload behind a
// complete-looking marker and the second Draw indexed past the end of the part list. Every Draw
// must now fail the same clean way, which only a renderer that refuses vertex buffers can provoke:
// the retired 2D-only SDL_RENDERER did; STUB, without a 3D pipeline too, accepts them and draws
// nothing (plans/plan_avatar_acceptance.md AVA-02).
TEST(AvatarRendererTest, AFailedGpuUploadFailsEveryDrawCleanlyRatherThanHalfBuilding) {
    GraphicsDevice device;
    bool refused = false;
    try {
        namespace Graphics = Microsoft::Xna::Framework::Graphics;
        Graphics::VertexBuffer probe(device, Graphics::VertexPositionNormalTextureSkinned::getVertexDeclarationStatic(), 3,
                                     Graphics::BufferUsage::WriteOnly);
    } catch (const std::exception&) {
        refused = true;
    }
    if (!refused)
        GTEST_SKIP() << "needs a renderer that refuses vertex buffers to make the upload fail";
    TestingDevice testing(&device);
    auto description = Describe(AvatarBodyType::Female, 1650);
    AvatarRenderer renderer(&description);
    ASSERT_EQ(WaitUntilLoaded(renderer), AvatarRendererState::Ready);
    AvatarAnimation stand(AvatarAnimationPreset::Stand0);
    for (int attempt = 0; attempt < 3; ++attempt) {
        EXPECT_THROW(renderer.Draw(&stand), std::runtime_error) << attempt;
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

TEST(AvatarRendererTest, PublicBindPoseUsesTheSameSpaceAsPublicAnimations) {
    namespace Assets = CNA::Internal::GamerServices::Avatars;
    auto description = Describe(AvatarBodyType::Male, 1800);
    AvatarRenderer renderer(&description);
    ASSERT_EQ(WaitUntilLoaded(renderer), AvatarRendererState::Ready);
    const auto descriptor = Assets::decode(description.getDescriptionProperty());
    ASSERT_TRUE(descriptor.has_value());
    const auto model = Assets::buildAvatarModel(*descriptor);
    const auto pose = renderer.getBindPoseProperty();
    for (int bone = 0; bone < 71; ++bone) {
        const auto offset = pose[bone].getTranslationProperty();
        EXPECT_FLOAT_EQ(offset.X, -model->bindTranslations[bone].X);
        EXPECT_FLOAT_EQ(offset.Y, model->bindTranslations[bone].Y);
        EXPECT_FLOAT_EQ(offset.Z, -model->bindTranslations[bone].Z);
    }
}

TEST(AvatarRendererTest, AttachmentCompositionAgreesWithDrawnJointsForEveryPresetAndBody) {
    namespace Assets = CNA::Internal::GamerServices::Avatars;
    using Microsoft::Xna::Framework::Vector3;
    using Microsoft::Xna::Framework::Quaternion;
    const auto& library = Assets::clipLibrary();
    const Matrix callerWorld = Matrix::CreateRotationY(0.73f) * Matrix::CreateTranslation(2.0f, -0.4f, 1.3f);
    for (auto body : {AvatarBodyType::Male, AvatarBodyType::Female}) {
        auto description = Describe(body, 1930);
        AvatarRenderer renderer(&description);
        ASSERT_EQ(WaitUntilLoaded(renderer), AvatarRendererState::Ready);
        const auto model = Assets::buildAvatarModel(*Assets::decode(description.getDescriptionProperty()));
        const auto bind = renderer.getBindPoseProperty();
        const auto parents = renderer.getParentBonesProperty();
        for (int preset = 0; preset < static_cast<int>(library.clips.size()); ++preset) {
            AvatarAnimation animation(static_cast<AvatarAnimationPreset>(preset));
            for (double t : {0.37, 1.1}) {
                animation.setCurrentPositionProperty(System::TimeSpan::FromSeconds(t));
                const auto bones = animation.getBoneTransformsProperty();
                std::array<Quaternion, 71> rotations;
                Vector3 root;
                Assets::sampleClip(library.clips[preset], t, rotations, root);
                std::array<Matrix, 71> attachment, drawn;
                for (int bone = 0; bone < 71; ++bone) {
                    const int parent = parents[bone];
                    attachment[bone] = bones[bone] * bind[bone] * (parent < 0 ? callerWorld : attachment[parent]);
                    const Matrix local = Matrix::CreateFromQuaternion(rotations[bone]) *
                        Matrix::CreateTranslation(bone == 0 ? root : model->bindTranslations[bone]);
                    drawn[bone] = parent < 0 ? local : local * drawn[parent];
                    // Frozen catalog joints are turned about Y before the caller's World.
                    const auto assetPoint = drawn[bone].getTranslationProperty();
                    const auto expected = Vector3::Transform(Vector3(-assetPoint.X, assetPoint.Y, -assetPoint.Z), callerWorld);
                    const auto actual = attachment[bone].getTranslationProperty();
                    EXPECT_NEAR(actual.X, expected.X, 1e-4f) << preset << " bone " << bone;
                    EXPECT_NEAR(actual.Y, expected.Y, 1e-4f);
                    EXPECT_NEAR(actual.Z, expected.Z, 1e-4f);
                }
            }
        }
    }
}

// plans/plan_avatar_acceptance.md AVH-05: an avatar a renderer still draws is reused at once, and
// once every renderer drawing it is disposed the caches let go of it and of its textures. The load
// times are recorded as test properties; avatar_acceptance.py runs this test alone, so the first
// load is the process's first (docs/avatars.md, Assets, states what to expect).
TEST(AvatarRendererTest, AnAvatarIsReusedWhileDrawnAndReleasedWithItsTexturesAfterwards) {
    namespace Assets = CNA::Internal::GamerServices::Avatars;
    using Clock = std::chrono::steady_clock;
    const auto since = [](Clock::time_point start) {
        return std::chrono::duration<double, std::milli>(Clock::now() - start).count();
    };
    const auto before = Assets::avatarCacheStatisticsForTesting();
    auto first = Describe(AvatarBodyType::Female, 1613);
    auto second = Describe(AvatarBodyType::Male, 1787);

    auto start = Clock::now();
    auto drawn = std::make_unique<AvatarRenderer>(&first, false);
    ASSERT_EQ(WaitUntilLoaded(*drawn), AvatarRendererState::Ready);
    const double firstMilliseconds = since(start);
    start = Clock::now();
    auto other = std::make_unique<AvatarRenderer>(&second, false);
    ASSERT_EQ(WaitUntilLoaded(*other), AvatarRendererState::Ready);
    const double nextMilliseconds = since(start);
    start = Clock::now();
    auto again = std::make_unique<AvatarRenderer>(&first, false);
    EXPECT_EQ(again->getStateProperty(), AvatarRendererState::Ready);
    const double reusedMilliseconds = since(start);

    const auto held = Assets::avatarCacheStatisticsForTesting();
    EXPECT_GE(held.liveModels, before.liveModels + 2);
    EXPECT_GT(held.liveTextures, before.liveTextures);
    EXPECT_GE(held.faceAtlases, 1u);
    EXPECT_LE(held.parsedModels, 96u);

    drawn->Dispose();
    again->Dispose();
    other->Dispose();
    drawn.reset();
    again.reset();
    other.reset();
    const auto released = Assets::avatarCacheStatisticsForTesting();
    EXPECT_LE(released.liveModels, before.liveModels);
    EXPECT_LE(released.liveTextures, before.liveTextures);
    EXPECT_LE(released.parsedModels, 96u);
    // A new avatar removes the keys of avatars nobody draws any more.
    auto third = Describe(AvatarBodyType::Female, 1702);
    AvatarRenderer later(&third, false);
    ASSERT_EQ(WaitUntilLoaded(later), AvatarRendererState::Ready);
    EXPECT_LE(Assets::avatarCacheStatisticsForTesting().modelKeys, before.liveModels + 1);

    RecordProperty("coldProcess", before.parsedModels == 0 && before.faceAtlases == 0 ? "yes" : "no");
    RecordProperty("firstAvatarMilliseconds", std::to_string(static_cast<int>(firstMilliseconds + 0.5)));
    RecordProperty("nextAvatarMilliseconds", std::to_string(static_cast<int>(nextMilliseconds + 0.5)));
    RecordProperty("reusedAvatarMicroseconds", std::to_string(static_cast<int>(reusedMilliseconds * 1000.0 + 0.5)));
    RecordProperty("liveTexturesWhileDrawn", std::to_string(held.liveTextures - before.liveTextures));
    RecordProperty("parsedModelsKept", std::to_string(released.parsedModels));
}
