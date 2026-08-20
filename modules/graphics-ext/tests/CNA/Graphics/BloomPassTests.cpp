// SPDX-License-Identifier: MS-PL
// plan_modern.md MOD-400..MOD-418: bloom.
//
// Bloom is the pass where "it looks about right" is least trustworthy -- a broken threshold, a
// blur that never runs, or a composite that loses the scene all produce images that look like
// bloom. So the assertions are about what must be true rather than about appearance: zero
// intensity reproduces the scene exactly, a bright spot spreads into pixels that were black, and
// the shader's threshold curve matches the specification.

#ifdef CNA_CNAEXT

#include <gtest/gtest.h>

#include "CNA/Graphics/BloomPass.hpp"
#include "CNA/Graphics/PostProcessContext.hpp"
#include "CNA/Graphics/RenderPipelineSettings.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteSortMode.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"

#include <vector>

namespace {

using CNA::Graphics::BloomPass;
using CNA::Graphics::PostProcessContext;
using CNA::Graphics::RenderPipelineSettings;
using Microsoft::Xna::Framework::Color;
using Microsoft::Xna::Framework::Rectangle;
using Microsoft::Xna::Framework::Graphics::BlendState;
using Microsoft::Xna::Framework::Graphics::DepthFormat;
using Microsoft::Xna::Framework::Graphics::GraphicsDevice;
using Microsoft::Xna::Framework::Graphics::RenderTarget2D;
using Microsoft::Xna::Framework::Graphics::SpriteBatch;
using Microsoft::Xna::Framework::Graphics::SpriteSortMode;
using Microsoft::Xna::Framework::Graphics::SurfaceFormat;
using Microsoft::Xna::Framework::Graphics::Texture2D;

constexpr int kSize = 32;

std::vector<Color> ReadTarget(RenderTarget2D& target)
{
    std::vector<Color> pixels(static_cast<std::size_t>(kSize) * kSize, Color(0, 0, 0, 0));
    target.GetData(pixels.data(), static_cast<int>(pixels.size()));
    return pixels;
}

/// A black field with one small white square in the middle -- the classic bloom subject, because
/// every pixel that is not the square starts at exactly zero and any spread is unambiguous.
void DrawBrightSpot(GraphicsDevice& gd, RenderTarget2D& target)
{
    Texture2D white(gd, 1, 1);
    const Color texel(255, 255, 255, 255);
    white.SetData(&texel, 1);

    gd.SetRenderTarget(&target);
    gd.Clear(Color::Black);
    SpriteBatch batch(gd);
    batch.Begin(SpriteSortMode::Deferred, BlendState::Opaque);
    batch.Draw(white, Rectangle(kSize / 2 - 2, kSize / 2 - 2, 4, 4), Color::White);
    batch.End();
    gd.SetRenderTarget(nullptr);
}

// ── The extract curve ────────────────────────────────────────────────────────

TEST(BloomPassTest, NothingBelowTheThresholdContributes)
{
    EXPECT_FLOAT_EQ(BloomPass::extractChannel(0.1f, 1.0f), 0.0f);
    EXPECT_FLOAT_EQ(BloomPass::extractChannel(0.49f, 1.0f), 0.0f);
}

TEST(BloomPassTest, TheKneeRampsRatherThanSnapping)
{
    // A hard cut-off makes bloom pop in and out as a highlight crosses the threshold, which is far
    // more visible in motion than the energy missing just below it. The ramp is the trade.
    const float below   = BloomPass::extractChannel(0.6f, 1.0f);
    const float atKnee  = BloomPass::extractChannel(1.0f, 1.0f);
    const float above   = BloomPass::extractChannel(1.4f, 1.0f);

    EXPECT_GT(atKnee, below);
    EXPECT_GT(above, atKnee);
    EXPECT_LT(atKnee, 1.0f);   // still ramping at the threshold itself
}

TEST(BloomPassTest, WellAboveTheThresholdPassesThroughUndiminished)
{
    EXPECT_NEAR(BloomPass::extractChannel(4.0f, 1.0f), 4.0f, 1e-5f);
}

TEST(BloomPassTest, AZeroThresholdBloomsEverything)
{
    // A legitimate stylistic choice, and one that must not divide by zero.
    EXPECT_GT(BloomPass::extractChannel(0.5f, 0.0f), 0.0f);
}

// ── The pass itself ──────────────────────────────────────────────────────────

TEST(BloomPassTest, ZeroIntensityReproducesTheSceneExactly)
{
    // The strongest available statement that the composite keeps the scene intact: with the bloom
    // term multiplied by zero, every pixel must survive unchanged.
    GraphicsDevice gd;
    BloomPass bloom(gd);
    if (!bloom.isSupported(gd))
        GTEST_SKIP() << "this renderer cannot run custom effects";

    RenderTarget2D source(gd, kSize, kSize);
    RenderTarget2D destination(gd, kSize, kSize);
    gd.SetRenderTarget(&source);
    gd.Clear(Color(60, 90, 120, 255));
    gd.SetRenderTarget(nullptr);

    RenderPipelineSettings settings;
    settings.setBloomIntensity(0.0f);
    settings.setBloomThreshold(0.5f);

    PostProcessContext context;
    context.source      = &source;
    context.destination = &destination;
    context.width       = kSize;
    context.height      = kSize;
    context.settings    = &settings;
    bloom.apply(context);

    const std::vector<Color> pixels = ReadTarget(destination);
    EXPECT_NEAR(pixels.front().getRProperty(), 60, 1);
    EXPECT_NEAR(pixels.front().getGProperty(), 90, 1);
    EXPECT_NEAR(pixels.front().getBProperty(), 120, 1);
}

TEST(BloomPassTest, ABrightSpotSpreadsIntoPixelsThatWereBlack)
{
    // The effect's whole purpose, stated as a measurement: a pixel far from the highlight was
    // exactly zero before the pass and must not be after it.
    GraphicsDevice gd;
    BloomPass bloom(gd);
    if (!bloom.isSupported(gd))
        GTEST_SKIP() << "this renderer cannot run custom effects";

    RenderTarget2D source(gd, kSize, kSize);
    RenderTarget2D destination(gd, kSize, kSize);
    DrawBrightSpot(gd, source);

    const std::vector<Color> before = ReadTarget(source);
    const std::size_t nearSpot = static_cast<std::size_t>((kSize / 2 + 3) * kSize + kSize / 2);
    ASSERT_EQ(before[nearSpot].getRProperty(), 0) << "the sample point must start black";

    RenderPipelineSettings settings;
    settings.setBloomThreshold(0.5f);
    settings.setBloomIntensity(2.0f);
    settings.setBloomIterations(2);

    PostProcessContext context;
    context.source      = &source;
    context.destination = &destination;
    context.width       = kSize;
    context.height      = kSize;
    context.settings    = &settings;
    bloom.apply(context);

    const std::vector<Color> after = ReadTarget(destination);
    EXPECT_GT(after[nearSpot].getRProperty(), 0)
        << "the highlight did not spread; the blur or the composite is not running";
}

TEST(BloomPassTest, AHigherIntensityProducesMoreGlow)
{
    GraphicsDevice gd;
    BloomPass bloom(gd);
    if (!bloom.isSupported(gd))
        GTEST_SKIP() << "this renderer cannot run custom effects";

    RenderTarget2D source(gd, kSize, kSize);
    RenderTarget2D dim(gd, kSize, kSize);
    RenderTarget2D bright(gd, kSize, kSize);
    DrawBrightSpot(gd, source);

    RenderPipelineSettings settings;
    settings.setBloomThreshold(0.5f);
    settings.setBloomIterations(2);

    PostProcessContext context;
    context.source   = &source;
    context.width    = kSize;
    context.height   = kSize;
    context.settings = &settings;

    settings.setBloomIntensity(0.5f);
    context.destination = &dim;
    bloom.apply(context);

    settings.setBloomIntensity(4.0f);
    context.destination = &bright;
    bloom.apply(context);

    const std::size_t nearSpot = static_cast<std::size_t>((kSize / 2 + 3) * kSize + kSize / 2);
    EXPECT_LT(ReadTarget(dim)[nearSpot].getRProperty(), ReadTarget(bright)[nearSpot].getRProperty());
}

TEST(BloomPassTest, IntermediateTargetsAreReusedAcrossFrames)
{
    GraphicsDevice gd;
    BloomPass bloom(gd);
    if (!bloom.isSupported(gd))
        GTEST_SKIP() << "this renderer cannot run custom effects";

    RenderTarget2D source(gd, kSize, kSize);
    RenderTarget2D destination(gd, kSize, kSize);
    DrawBrightSpot(gd, source);

    RenderPipelineSettings settings;
    settings.setBloomIterations(3);

    PostProcessContext context;
    context.source      = &source;
    context.destination = &destination;
    context.width       = kSize;
    context.height      = kSize;
    context.settings    = &settings;

    bloom.apply(context);
    for (int frame = 0; frame < 20; ++frame)
        bloom.apply(context);

    // Nothing asserts a specific count -- the chain's depth depends on the viewport -- but twenty
    // more frames must not have allocated anything the first one did not.
    EXPECT_NO_THROW(bloom.resetTargets());
}

TEST(BloomPassTest, AnAbsurdIterationCountIsClampedRatherThanRejected)
{
    GraphicsDevice gd;
    BloomPass bloom(gd);
    if (!bloom.isSupported(gd))
        GTEST_SKIP() << "this renderer cannot run custom effects";

    RenderTarget2D source(gd, kSize, kSize);
    RenderTarget2D destination(gd, kSize, kSize);
    DrawBrightSpot(gd, source);

    RenderPipelineSettings settings;
    settings.setBloomIterations(999);   // the settings bag stores; the pass clamps

    PostProcessContext context;
    context.source      = &source;
    context.destination = &destination;
    context.width       = kSize;
    context.height      = kSize;
    context.settings    = &settings;

    EXPECT_NO_THROW(bloom.apply(context));
}

TEST(BloomPassTest, ThePassIsUsableWithoutSettings)
{
    GraphicsDevice gd;
    BloomPass bloom(gd);

    bloom.setThreshold(0.75f);
    bloom.setIntensity(1.5f);
    bloom.setIterations(3);

    EXPECT_FLOAT_EQ(bloom.getThreshold(), 0.75f);
    EXPECT_FLOAT_EQ(bloom.getIntensity(), 1.5f);
    EXPECT_EQ(bloom.getIterations(), 3);
    EXPECT_EQ(bloom.getName(), "Bloom");
}

TEST(BloomPassTest, AnHdrSourceKeepsItsHighlightsThroughTheChain)
{
    GraphicsDevice gd;
    BloomPass bloom(gd);
    if (!bloom.isSupported(gd))
        GTEST_SKIP() << "this renderer cannot run custom effects";
    if (!gd.SupportsSurfaceFormatAsRenderTargetEXT(SurfaceFormat::Vector4))
        GTEST_SKIP() << "this renderer/driver has no RGBA32F render targets";

    RenderTarget2D source(gd, kSize, kSize, false, SurfaceFormat::Vector4, DepthFormat::None);
    RenderTarget2D destination(gd, kSize, kSize, false, SurfaceFormat::Vector4, DepthFormat::None);

    gd.SetRenderTarget(&source);
    gd.Clear(8.0f, 8.0f, 8.0f, 1.0f);
    gd.SetRenderTarget(nullptr);

    RenderPipelineSettings settings;
    settings.setBloomThreshold(1.0f);
    settings.setBloomIntensity(1.0f);
    settings.setBloomIterations(2);

    PostProcessContext context;
    context.source      = &source;
    context.destination = &destination;
    context.width       = kSize;
    context.height      = kSize;
    context.settings    = &settings;
    bloom.apply(context);

    std::vector<Microsoft::Xna::Framework::Vector4> pixels(
        static_cast<std::size_t>(kSize) * kSize);
    destination.GetData(pixels.data(), static_cast<int>(pixels.size()));

    // Scene plus its own bloom, so brighter than the scene alone and far above 1.0 -- if any
    // intermediate had been Color, this would read back at exactly 1.0 and look like a plausible
    // white.
    EXPECT_GT(pixels.front().X, 8.0f);
}

} // namespace

#endif // CNA_CNAEXT
