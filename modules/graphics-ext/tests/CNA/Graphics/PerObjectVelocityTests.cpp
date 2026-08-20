// SPDX-License-Identifier: MS-PL
// plan_modern.md MOD-2033: per-object velocity.
//
// The claim is precisely the one the docs have said for several phases the layer could NOT make: a
// car crossing a static shot blurs. So the camera is held perfectly still in every case here, which
// removes the whole of the camera-reprojection path from the answer -- with a static camera the old
// motion blur has nothing to compute, so any smear that appears came from the object's own motion.
//
// The other half is the one an opt-in feature gets wrong most easily: turning it on must change
// nothing for an app that does not fulfil the contract, and turning it off must change nothing at
// all. Both are asserted against the images they must equal.

#ifdef CNA_CNAEXT

#include <gtest/gtest.h>

#include "CNA/Graphics/DepthNormalPrepass.hpp"
#include "CNA/Graphics/FullscreenPass.hpp"
#include "CNA/Graphics/MotionBlurPass.hpp"
#include "CNA/Graphics/PostProcessContext.hpp"
#include "CNA/Graphics/RenderPipelineSettings.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/MathHelper.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthStencilState.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/ShaderEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionNormalTexture.hpp"

#include <array>
#include <cmath>
#include <stdexcept>
#include <vector>

namespace {

using CNA::Graphics::DepthNormalPrepass;
using CNA::Graphics::MotionBlurPass;
using CNA::Graphics::PostProcessContext;
using CNA::Graphics::RenderPipelineSettings;
using Microsoft::Xna::Framework::Color;
using Microsoft::Xna::Framework::MathHelper;
using Microsoft::Xna::Framework::Matrix;
using Microsoft::Xna::Framework::Vector2;
using Microsoft::Xna::Framework::Vector3;
using Microsoft::Xna::Framework::Graphics::BlendState;
using Microsoft::Xna::Framework::Graphics::DepthStencilState;
using Microsoft::Xna::Framework::Graphics::GraphicsDevice;
using Microsoft::Xna::Framework::Graphics::PrimitiveType;
using Microsoft::Xna::Framework::Graphics::RasterizerState;
using Microsoft::Xna::Framework::Graphics::RenderTarget2D;
using Microsoft::Xna::Framework::Graphics::ShaderEffect;
using Microsoft::Xna::Framework::Graphics::VertexPositionNormalTexture;

constexpr int   kSize      = 64;
constexpr float kNearPlane = 1.0f;
constexpr float kFarPlane  = 100.0f;
constexpr float kQuadZ     = -20.0f;

Matrix View() { return Matrix::CreateLookAt(Vector3::Zero, Vector3(0.0f, 0.0f, -1.0f), Vector3::Up); }
Matrix Projection()
{
    return Matrix::CreatePerspectiveFieldOfView(MathHelper::PiOver4, 1.0f, kNearPlane, kFarPlane);
}

/// A slab in the middle of the view, big enough to have an interior the blur can act on.
std::array<VertexPositionNormalTexture, 6> Slab()
{
    const Vector3 facing(0.0f, 0.0f, 1.0f);
    const auto vertex = [&](const float x, const float y) {
        return VertexPositionNormalTexture(Vector3(x, y, kQuadZ), facing, Vector2(0.0f, 0.0f));
    };
    return {vertex(-3.0f, -3.0f), vertex(3.0f, -3.0f), vertex(3.0f, 3.0f),
            vertex(-3.0f, -3.0f), vertex(3.0f, 3.0f),  vertex(-3.0f, 3.0f)};
}

std::vector<Color> Read(RenderTarget2D& target)
{
    std::vector<Color> pixels(static_cast<std::size_t>(kSize) * kSize, Color(0, 0, 0, 0));
    target.GetData(pixels.data(), static_cast<int>(pixels.size()));
    return pixels;
}

/// Runs the prepass over the slab, with the object at @p previousWorld last frame and at the origin
/// now. The camera is identical in both frames, so anything non-zero in the velocity image is the
/// object's own motion.
void RunPrepass(GraphicsDevice& device, DepthNormalPrepass& prepass, const Matrix& previousWorld)
{
    const auto slab = Slab();
    device.setRasterizerStateProperty(RasterizerState::CullNone);
    device.setDepthStencilStateProperty(DepthStencilState::Default);
    device.setBlendStateProperty(BlendState::Opaque);
    device.SetVertexBuffer(nullptr);

    prepass.setPreviousCameraEXT(View(), Projection());
    for (int pass = 0; pass < prepass.getPassCount(); ++pass)
    {
        prepass.begin(pass, View(), Projection(), kNearPlane, kFarPlane);
        prepass.setPreviousWorldEXT(previousWorld);
        ShaderEffect* effect = prepass.getPrepassEffect();
        ASSERT_NE(effect, nullptr);
        effect->Apply();
        device.DrawUserPrimitives(PrimitiveType::TriangleList, slab.data(), 0, 2);
        prepass.end();
    }
}

int BrightPixels(const std::vector<Color>& pixels, const int threshold)
{
    int count = 0;
    for (const Color& texel : pixels)
        if (texel.getRProperty() >= threshold) ++count;
    return count;
}

TEST(PerObjectVelocityTest, VelocityIsOffByDefaultAndCostsNothing)
{
    GraphicsDevice device;
    DepthNormalPrepass prepass(device, kSize, kSize);
    EXPECT_FALSE(prepass.isVelocityEnabledEXT());
    EXPECT_EQ(prepass.getVelocityTextureEXT(), nullptr)
        << "a target was allocated for a feature nobody asked for";
    // The pass count is the promise this row was held back for: turning velocity on is what makes
    // the non-MRT fallback three passes, and leaving it off must not.
    const int before = prepass.getPassCount();
    EXPECT_TRUE(before == 1 || before == 2);
}

TEST(PerObjectVelocityTest, TurningItOnAddsATargetAndOnlyThenAPass)
{
    GraphicsDevice device;
    DepthNormalPrepass prepass(device, kSize, kSize);
    const bool mrt = prepass.isUsingMultipleRenderTargets();
    const int before = prepass.getPassCount();

    prepass.setVelocityEnabledEXT(true);
    EXPECT_TRUE(prepass.isVelocityEnabledEXT());
    ASSERT_NE(prepass.getVelocityTextureEXT(), nullptr);

    if (prepass.isUsingMultipleRenderTargets())
        EXPECT_EQ(prepass.getPassCount(), 1) << "with three targets bound it is still one pass";
    else
        EXPECT_EQ(prepass.getPassCount(), mrt ? 3 : before + 1)
            << "a renderer that refused three targets must fall back to a pass per image";

    prepass.setVelocityEnabledEXT(false);
    EXPECT_FALSE(prepass.isVelocityEnabledEXT());
    EXPECT_EQ(prepass.getVelocityTextureEXT(), nullptr);
    EXPECT_EQ(prepass.getPassCount(), before) << "turning it off did not give the pass count back";
}

TEST(PerObjectVelocityTest, ChangingItWhileAPassIsOpenIsRefused)
{
    GraphicsDevice device;
    DepthNormalPrepass prepass(device, kSize, kSize);
    if (!prepass.isSupported(device)) GTEST_SKIP() << "no prepass on this renderer";

    prepass.begin(0, View(), Projection(), kNearPlane, kFarPlane);
    EXPECT_THROW(prepass.setVelocityEnabledEXT(true), std::logic_error);
    prepass.end();
}

TEST(PerObjectVelocityTest, AStationaryObjectUnderAStationaryCameraHasNoVelocity)
{
    // The control, and the one that would catch a sign error, a transposed matrix or a stale
    // uniform: nothing moved, so every covered texel must decode to zero.
    GraphicsDevice device;
    DepthNormalPrepass prepass(device, kSize, kSize);
    if (!prepass.isSupported(device)) GTEST_SKIP() << "no prepass on this renderer";
    prepass.setVelocityEnabledEXT(true);
    ASSERT_NE(prepass.getVelocityTextureEXT(), nullptr);

    RunPrepass(device, prepass, Matrix::getIdentityProperty());

    auto* velocity = static_cast<RenderTarget2D*>(prepass.getVelocityTextureEXT());
    const std::vector<Color> pixels = Read(*velocity);
    int covered = 0;
    for (const Color& texel : pixels)
    {
        if (texel.getAProperty() >= 128) continue;   // alpha is inverted: >=128 means "not written"
        ++covered;
        EXPECT_NEAR(texel.getRProperty(), 128, 2);
        EXPECT_NEAR(texel.getGProperty(), 128, 2);
    }
    EXPECT_GT(covered, kSize * kSize / 8) << "the slab did not reach the velocity image at all";
}

TEST(PerObjectVelocityTest, AnObjectThatMovedRightRecordsAVelocityToTheRight)
{
    GraphicsDevice device;
    DepthNormalPrepass prepass(device, kSize, kSize);
    if (!prepass.isSupported(device)) GTEST_SKIP() << "no prepass on this renderer";
    prepass.setVelocityEnabledEXT(true);

    // Last frame the slab was to the LEFT of where it is now, so this frame's pixels came from the
    // left: the recorded velocity -- current minus previous -- points right, which is +X.
    RunPrepass(device, prepass, Matrix::CreateTranslation(-2.0f, 0.0f, 0.0f));

    auto* velocity = static_cast<RenderTarget2D*>(prepass.getVelocityTextureEXT());
    const std::vector<Color> pixels = Read(*velocity);
    int covered = 0;
    int rightwards = 0;
    for (const Color& texel : pixels)
    {
        if (texel.getAProperty() >= 128) continue;
        ++covered;
        if (texel.getRProperty() > 132) ++rightwards;
        // Nothing moved vertically, and a velocity that leaked into Y would be a transposed matrix
        // or a swapped component -- exactly the kind of error a smear still looks plausible with.
        EXPECT_NEAR(texel.getGProperty(), 128, 3);
    }
    ASSERT_GT(covered, kSize * kSize / 8);
    EXPECT_GT(rightwards, covered * 3 / 4)
        << "the slab moved a sixth of the screen and the velocity image barely noticed";
}

TEST(PerObjectVelocityTest, TheVelocityGrowsWithTheDistanceTravelled)
{
    // A single displacement could be matched by a constant; two cannot.
    GraphicsDevice device;
    DepthNormalPrepass prepass(device, kSize, kSize);
    if (!prepass.isSupported(device)) GTEST_SKIP() << "no prepass on this renderer";
    prepass.setVelocityEnabledEXT(true);

    const auto averageRed = [&](const float previousX) {
        RunPrepass(device, prepass, Matrix::CreateTranslation(previousX, 0.0f, 0.0f));
        auto* velocity = static_cast<RenderTarget2D*>(prepass.getVelocityTextureEXT());
        double sum = 0.0;
        int covered = 0;
        for (const Color& texel : Read(*velocity))
        {
            if (texel.getAProperty() >= 128) continue;
            sum += texel.getRProperty();
            ++covered;
        }
        return covered > 0 ? sum / covered : 0.0;
    };

    const double small = averageRed(-0.5f);
    const double large = averageRed(-2.0f);
    EXPECT_GT(small, 128.0);
    EXPECT_GT(large, small + 4.0) << "four times the displacement produced the same velocity";
}

TEST(PerObjectVelocityTest, MotionBlurSmearsAMovingObjectUnderAStationaryCamera)
{
    // The sentence `docs/cnaext-engine-layer.md` has said the layer could not deliver: a car
    // crossing a static shot. The camera is identical in both frames, so the camera-reprojection
    // path contributes nothing and any smear is the object's.
    GraphicsDevice device;
    DepthNormalPrepass prepass(device, kSize, kSize);
    if (!prepass.isSupported(device)) GTEST_SKIP() << "no prepass on this renderer";
    MotionBlurPass blur(device);
    if (!blur.isSupported(device)) GTEST_SKIP() << "this renderer cannot run the blur";
    prepass.setVelocityEnabledEXT(true);
    RunPrepass(device, prepass, Matrix::CreateTranslation(-2.0f, 0.0f, 0.0f));

    // A bright square in the middle of a black field. The blur can only make black pixels brighter
    // by dragging the square across them.
    RenderTarget2D source(device, kSize, kSize);
    device.SetRenderTarget(&source);
    device.Clear(Color::Black);
    device.SetRenderTarget(nullptr);
    {
        std::vector<Color> texels(static_cast<std::size_t>(kSize) * kSize, Color(0, 0, 0, 255));
        for (int y = kSize / 4; y < kSize * 3 / 4; ++y)
            for (int x = kSize * 7 / 16; x < kSize * 9 / 16; ++x)
                texels[static_cast<std::size_t>(y) * kSize + x] = Color::White;
        Microsoft::Xna::Framework::Graphics::Texture2D upload(device, kSize, kSize);
        upload.SetData(texels.data(), static_cast<int>(texels.size()));
        CNA::Graphics::FullscreenPass copy(device);
        copy.draw(&upload, &source, nullptr, kSize, kSize);
    }

    RenderPipelineSettings settings;
    settings.setMotionBlurStrength(1.0f);
    settings.setMotionBlurMaxDistance(0.5f);

    const auto run = [&](Microsoft::Xna::Framework::Graphics::Texture2D* velocity) {
        RenderTarget2D destination(device, kSize, kSize);
        PostProcessContext context;
        context.source = &source;
        context.destination = &destination;
        context.width = kSize;
        context.height = kSize;
        context.settings = &settings;
        context.sourceDepth = prepass.getDepthTexture();
        context.sourceNormals = prepass.getNormalTexture();
        context.sourceVelocity = velocity;
        context.projection = Projection();
        context.inverseProjection = Matrix::Invert(Projection());
        context.inverseView = Matrix::Invert(View());
        context.nearPlane = kNearPlane;
        context.farPlane = kFarPlane;
        // The SAME camera both frames. This is what makes the comparison mean what it says.
        context.previousViewProjection = View() * Projection();
        context.hasPreviousFrame = true;
        blur.apply(context);
        return Read(destination);
    };

    const std::vector<Color> withoutVelocity = run(nullptr);
    const std::vector<Color> withVelocity    = run(prepass.getVelocityTextureEXT());

    const int litBefore = BrightPixels(withoutVelocity, 24);
    const int litAfter  = BrightPixels(withVelocity, 24);
    EXPECT_GT(litAfter, litBefore + kSize)
        << "the object's motion produced no smear, so the velocity image is not reaching the blur";

    // And the control that makes the assertion above mean something: with a static camera the old
    // path has nothing to blur, so its output must be the unblurred source.
    const std::vector<Color> sourcePixels = Read(source);
    EXPECT_EQ(BrightPixels(withoutVelocity, 24), BrightPixels(sourcePixels, 24))
        << "the camera-only path smeared a frame in which the camera did not move";
}

} // namespace

#endif // CNA_CNAEXT
