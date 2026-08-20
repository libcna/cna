// SPDX-License-Identifier: MS-PL
// plans/plan_modern.md MOD-515..MOD-529: screen-space ambient occlusion.
//
// SSAO is tested against synthetic depth and normal images rather than a rendered scene. That is
// not a shortcut: it is the only way to state what the pass must do without also depending on a
// prepass, a camera and a mesh. A flat wall must come back unoccluded, a depth discontinuity must
// darken the surface beside it, and a frame with no depth supplied at all must pass through
// untouched. Each is a specific claim about the estimator.

#ifdef CNA_CNAEXT

#include <gtest/gtest.h>

#include "EngineTestSupport.hpp"

#include "CNA/Graphics/PostProcessContext.hpp"
#include "CNA/Graphics/RenderPipelineSettings.hpp"
#include "CNA/Graphics/SsaoPass.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"

#include <cmath>
#include <memory>
#include <vector>

namespace {

using CNA::Graphics::PostProcessContext;
using CNA::Graphics::RenderPipelineSettings;
using CNA::Graphics::SsaoPass;
using Microsoft::Xna::Framework::Color;
using Microsoft::Xna::Framework::Vector3;
using Microsoft::Xna::Framework::Graphics::GraphicsDevice;
using Microsoft::Xna::Framework::Graphics::RenderTarget2D;
using Microsoft::Xna::Framework::Graphics::Texture2D;
using CnaTest::EngineLayer::DepthTexelFromByte;

constexpr int kSize = 32;

/// A depth image where every pixel sits at the same distance: a wall facing the camera.
std::unique_ptr<Texture2D> MakeFlatDepth(GraphicsDevice& gd, const int depthByte)
{
    auto texture = std::make_unique<Texture2D>(gd, kSize, kSize);
    std::vector<Color> texels(static_cast<std::size_t>(kSize) * kSize,
                              DepthTexelFromByte(gd, depthByte));
    texture->SetData(texels.data(), static_cast<int>(texels.size()));
    return texture;
}

/// A depth image split down the middle: near on the left, far on the right. The step is the
/// occluder, and pixels on the far side just past it are the ones that must darken.
std::unique_ptr<Texture2D> MakeStepDepth(GraphicsDevice& gd)
{
    auto texture = std::make_unique<Texture2D>(gd, kSize, kSize);
    std::vector<Color> texels;
    texels.reserve(static_cast<std::size_t>(kSize) * kSize);
    for (int y = 0; y < kSize; ++y)
        for (int x = 0; x < kSize; ++x)
        {
            const int depth = x < kSize / 2 ? 60 : 200;
            texels.push_back(DepthTexelFromByte(gd, depth));
        }
    texture->SetData(texels.data(), static_cast<int>(texels.size()));
    return texture;
}

/// Every normal pointing at the camera: encoded (0,0,1) is (128,128,255).
std::unique_ptr<Texture2D> MakeFacingNormals(GraphicsDevice& gd)
{
    auto texture = std::make_unique<Texture2D>(gd, kSize, kSize);
    std::vector<Color> texels(static_cast<std::size_t>(kSize) * kSize, Color(128, 128, 255, 255));
    texture->SetData(texels.data(), static_cast<int>(texels.size()));
    return texture;
}

std::vector<Color> ReadTarget(RenderTarget2D& target)
{
    std::vector<Color> pixels(static_cast<std::size_t>(kSize) * kSize, Color(0, 0, 0, 0));
    target.GetData(pixels.data(), static_cast<int>(pixels.size()));
    return pixels;
}

// ── The sampling kernel ──────────────────────────────────────────────────────

TEST(SsaoPassTest, EverySampleLiesInTheHemisphereAboveTheSurface)
{
    // A sample with negative Z is below the surface -- it would count geometry behind the wall as
    // occluding the wall, which darkens flat surfaces for no reason.
    GraphicsDevice gd;
    SsaoPass pass(gd);

    ASSERT_FALSE(pass.getKernel().empty());
    for (const Vector3& sample : pass.getKernel())
    {
        EXPECT_GE(sample.Z, 0.0f);
        const float length = std::sqrt(sample.X * sample.X + sample.Y * sample.Y + sample.Z * sample.Z);
        EXPECT_LE(length, 1.0f + 1e-5f);
    }
}

TEST(SsaoPassTest, TheKernelIsBiasedTowardTheOrigin)
{
    // Contact shadows come from nearby geometry. An evenly spread kernel washes them into a
    // uniform grey, which reads as "the image got darker" rather than as occlusion.
    GraphicsDevice gd;
    SsaoPass pass(gd);

    const auto& kernel = pass.getKernel();
    ASSERT_GE(kernel.size(), 16u);

    const auto lengthOf = [](const Vector3& v) {
        return std::sqrt(v.X * v.X + v.Y * v.Y + v.Z * v.Z);
    };

    float earlyAverage = 0.0f;
    float lateAverage  = 0.0f;
    const std::size_t span = kernel.size() / 4;
    for (std::size_t i = 0; i < span; ++i)
        earlyAverage += lengthOf(kernel[i]);
    for (std::size_t i = kernel.size() - span; i < kernel.size(); ++i)
        lateAverage += lengthOf(kernel[i]);

    EXPECT_LT(earlyAverage / static_cast<float>(span), lateAverage / static_cast<float>(span));
}

TEST(SsaoPassTest, TheKernelIsDeterministic)
{
    // Two passes must produce the same image. A randomly seeded kernel would make every golden
    // comparison and every visual regression check meaningless.
    GraphicsDevice gd;
    SsaoPass first(gd);
    SsaoPass second(gd);

    ASSERT_EQ(first.getKernel().size(), second.getKernel().size());
    for (std::size_t i = 0; i < first.getKernel().size(); ++i)
    {
        EXPECT_FLOAT_EQ(first.getKernel()[i].X, second.getKernel()[i].X);
        EXPECT_FLOAT_EQ(first.getKernel()[i].Y, second.getKernel()[i].Y);
        EXPECT_FLOAT_EQ(first.getKernel()[i].Z, second.getKernel()[i].Z);
    }
}

// ── The estimator ────────────────────────────────────────────────────────────

TEST(SsaoPassTest, AFlatSurfaceIsLeftUnoccluded)
{
    // The failure this catches is the common one: a bias or hemisphere error that darkens every
    // flat surface, which looks like "SSAO is working" until you notice the whole scene is grey.
    GraphicsDevice gd;
    SsaoPass pass(gd);
    if (!pass.isSupported(gd))
        GTEST_SKIP() << "this renderer cannot run custom effects";

    auto depth   = MakeFlatDepth(gd, 128);
    auto normals = MakeFacingNormals(gd);
    RenderTarget2D source(gd, kSize, kSize);
    RenderTarget2D destination(gd, kSize, kSize);
    gd.SetRenderTarget(&source);
    gd.Clear(Color(200, 200, 200, 255));
    gd.SetRenderTarget(nullptr);

    PostProcessContext context;
    context.source        = &source;
    context.sourceDepth   = depth.get();
    context.sourceNormals = normals.get();
    context.destination   = &destination;
    context.width         = kSize;
    context.height        = kSize;
    pass.apply(context);

    const std::vector<Color> pixels = ReadTarget(destination);
    const std::size_t middle = static_cast<std::size_t>(kSize / 2) * kSize + kSize / 2;
    EXPECT_GE(pixels[middle].getRProperty(), 190) << "a flat wall must not shade itself";
}

TEST(SsaoPassTest, TheClearedSkyIsNotDarkenedBesideASilhouette)
{
    // plans/plan_modern.md MOD-2009. `DepthNormalPrepass` clears depth to white, so an empty pixel reads
    // as 1.0 -- the far plane -- while this pass tests for "nothing here" by comparing against
    // *zero*. Its early-out therefore never fires for the sky, and each sky pixel is estimated as a
    // surface at the far plane with every near object in front of it counting as an occluder. What
    // stops that becoming a dark halo around every silhouette is the **range check**, not the
    // early-out: an occluder that far in front contributes essentially nothing. That is a real
    // property of the estimator resting on a coincidence of two unrelated guards, so it is asserted
    // here rather than left to be rediscovered -- `SsrPass` had no equivalent second guard and did
    // ship the corresponding bug until this task measured the clear.
    GraphicsDevice gd;
    SsaoPass pass(gd);
    if (!pass.isSupported(gd))
        GTEST_SKIP() << "this renderer cannot run custom effects";

    // A near object on the left, cleared sky on the right, in the prepass's own convention.
    auto depth = std::make_unique<Texture2D>(gd, kSize, kSize);
    std::vector<Color> texels;
    texels.reserve(static_cast<std::size_t>(kSize) * kSize);
    for (int y = 0; y < kSize; ++y)
        for (int x = 0; x < kSize; ++x)
        {
            const int value = x < kSize / 2 ? 60 : 255;
            texels.push_back(DepthTexelFromByte(gd, value));
        }
    depth->SetData(texels.data(), static_cast<int>(texels.size()));

    auto normals = MakeFacingNormals(gd);
    RenderTarget2D source(gd, kSize, kSize);
    RenderTarget2D destination(gd, kSize, kSize);
    gd.SetRenderTarget(&source);
    gd.Clear(Color(200, 200, 200, 255));
    gd.SetRenderTarget(nullptr);

    RenderPipelineSettings settings;
    settings.setSSAORadius(0.25f);
    settings.setSSAOIntensity(2.0f);
    settings.setSSAOSampleCount(32);

    PostProcessContext context;
    context.source        = &source;
    context.sourceDepth   = depth.get();
    context.sourceNormals = normals.get();
    context.destination   = &destination;
    context.width         = kSize;
    context.height        = kSize;
    context.settings      = &settings;
    pass.apply(context);

    const std::vector<Color> pixels = ReadTarget(destination);
    // A few texels clear of the silhouette, still well inside the sky.
    const std::size_t skyBesideTheEdge =
        static_cast<std::size_t>(kSize / 2) * kSize + kSize / 2 + 3;
    EXPECT_GE(pixels[skyBesideTheEdge].getRProperty(), 190)
        << "the sky was darkened beside the silhouette in front of it";
}

TEST(SsaoPassTest, ADepthDiscontinuityDarkensTheSurfaceBesideIt)
{
    GraphicsDevice gd;
    SsaoPass pass(gd);
    if (!pass.isSupported(gd))
        GTEST_SKIP() << "this renderer cannot run custom effects";

    auto depth   = MakeStepDepth(gd);
    auto normals = MakeFacingNormals(gd);
    RenderTarget2D source(gd, kSize, kSize);
    RenderTarget2D destination(gd, kSize, kSize);
    gd.SetRenderTarget(&source);
    gd.Clear(Color(200, 200, 200, 255));
    gd.SetRenderTarget(nullptr);

    RenderPipelineSettings settings;
    settings.setSSAORadius(0.25f);
    settings.setSSAOIntensity(2.0f);
    settings.setSSAOSampleCount(32);

    PostProcessContext context;
    context.source        = &source;
    context.sourceDepth   = depth.get();
    context.sourceNormals = normals.get();
    context.destination   = &destination;
    context.width         = kSize;
    context.height        = kSize;
    context.settings      = &settings;
    pass.apply(context);

    const std::vector<Color> pixels = ReadTarget(destination);
    const int row = kSize / 2;
    const int nearStep = pixels[static_cast<std::size_t>(row) * kSize + kSize / 2 + 1].getRProperty();
    const int farAway  = pixels[static_cast<std::size_t>(row) * kSize + kSize - 1].getRProperty();

    EXPECT_LT(nearStep, farAway)
        << "the pixel beside the depth step should be darker than one far from it";
}

TEST(SsaoPassTest, AHigherIntensityDarkensMore)
{
    GraphicsDevice gd;
    SsaoPass pass(gd);
    if (!pass.isSupported(gd))
        GTEST_SKIP() << "this renderer cannot run custom effects";

    auto depth   = MakeStepDepth(gd);
    auto normals = MakeFacingNormals(gd);
    RenderTarget2D source(gd, kSize, kSize);
    RenderTarget2D weak(gd, kSize, kSize);
    RenderTarget2D strong(gd, kSize, kSize);
    gd.SetRenderTarget(&source);
    gd.Clear(Color(200, 200, 200, 255));
    gd.SetRenderTarget(nullptr);

    RenderPipelineSettings settings;
    settings.setSSAORadius(0.25f);
    settings.setSSAOSampleCount(32);

    PostProcessContext context;
    context.source        = &source;
    context.sourceDepth   = depth.get();
    context.sourceNormals = normals.get();
    context.width         = kSize;
    context.height        = kSize;
    context.settings      = &settings;

    settings.setSSAOIntensity(0.5f);
    context.destination = &weak;
    pass.apply(context);

    settings.setSSAOIntensity(4.0f);
    context.destination = &strong;
    pass.apply(context);

    const std::size_t beside =
        static_cast<std::size_t>(kSize / 2) * kSize + kSize / 2 + 1;
    // Strictly less, not "no brighter". The weaker form passes when the pass produces no
    // occlusion at all, which is precisely the state it should be catching: every intensity then
    // yields the same untouched frame, and the only test that notices is the one beside this.
    EXPECT_LT(ReadTarget(strong)[beside].getRProperty(), ReadTarget(weak)[beside].getRProperty());
}

TEST(SsaoPassTest, WithoutDepthAndNormalsTheFrameIsPassedThroughUnchanged)
{
    // A pipeline that enables SSAO without running a prepass is misconfigured, not broken: it
    // should render an unoccluded frame rather than throw or produce a black screen.
    GraphicsDevice gd;
    SsaoPass pass(gd);
    if (!pass.isSupported(gd))
        GTEST_SKIP() << "this renderer cannot run custom effects";

    RenderTarget2D source(gd, kSize, kSize);
    RenderTarget2D destination(gd, kSize, kSize);
    gd.SetRenderTarget(&source);
    gd.Clear(Color(123, 45, 67, 255));
    gd.SetRenderTarget(nullptr);

    PostProcessContext context;
    context.source      = &source;
    context.destination = &destination;
    context.width       = kSize;
    context.height      = kSize;
    EXPECT_NO_THROW(pass.apply(context));

    const std::vector<Color> pixels = ReadTarget(destination);
    EXPECT_NEAR(pixels.front().getRProperty(), 123, 1);
    EXPECT_NEAR(pixels.front().getGProperty(), 45, 1);
    EXPECT_NEAR(pixels.front().getBProperty(), 67, 1);
}

TEST(SsaoPassTest, SettingsRoundTripAndSampleCountsAreClampedOnUse)
{
    GraphicsDevice gd;
    SsaoPass pass(gd);

    pass.setRadius(1.5f);
    pass.setIntensity(0.25f);
    pass.setSampleCount(48);

    EXPECT_FLOAT_EQ(pass.getRadius(), 1.5f);
    EXPECT_FLOAT_EQ(pass.getIntensity(), 0.25f);
    EXPECT_EQ(pass.getSampleCount(), 48);
    EXPECT_EQ(pass.getName(), "SSAO");

    // The settings bag stores anything; the pass clamps when it applies it, so an absurd count is
    // a quality choice rather than an error.
    RenderPipelineSettings settings;
    settings.setSSAOSampleCount(4096);
    if (!pass.isSupported(gd))
        GTEST_SKIP() << "this renderer cannot run custom effects";

    auto depth   = MakeFlatDepth(gd, 128);
    auto normals = MakeFacingNormals(gd);
    RenderTarget2D source(gd, kSize, kSize);
    RenderTarget2D destination(gd, kSize, kSize);

    PostProcessContext context;
    context.source        = &source;
    context.sourceDepth   = depth.get();
    context.sourceNormals = normals.get();
    context.destination   = &destination;
    context.width         = kSize;
    context.height        = kSize;
    context.settings      = &settings;
    EXPECT_NO_THROW(pass.apply(context));
}


} // namespace

#endif // CNA_CNAEXT
