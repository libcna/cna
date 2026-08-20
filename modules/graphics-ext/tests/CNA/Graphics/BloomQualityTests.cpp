// SPDX-License-Identifier: MS-PL
// plan_modern.md MOD-405, MOD-407, MOD-409: the bloom pyramid's shape, its filter fallback, and
// the quality mapping.
//
// The pyramid's failures are all "still looks like bloom": a single composite of the smallest level
// gives a wide, flat glow; a nearest-sampled upsample gives a stair-stepped one; a quality preset
// that maps to the wrong number gives a halo of the wrong size. None of those is an error, so each
// is checked by measuring the glow rather than by looking at it.

#ifdef CNA_CNAEXT

#include <gtest/gtest.h>

#include "CNA/Graphics/BloomPass.hpp"
#include "CNA/Graphics/PostProcessContext.hpp"
#include "CNA/Graphics/RenderPipelineSettings.hpp"
#include "CNA/Graphics/RenderQuality.hpp"
#include "EngineTestSupport.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"

#include <cmath>
#include <memory>
#include <vector>

namespace {

using CNA::Graphics::BloomPass;
using CNA::Graphics::PostProcessContext;
using CNA::Graphics::RenderPipelineSettings;
using CNA::Graphics::RenderQuality;
using Microsoft::Xna::Framework::Color;
using Microsoft::Xna::Framework::Graphics::GraphicsDevice;
using Microsoft::Xna::Framework::Graphics::RenderTarget2D;
using Microsoft::Xna::Framework::Graphics::Texture2D;

constexpr int kSize   = 64;
constexpr int kSprite = 8;

/// A bright square in the middle of a black field: the one scene whose bloom can be measured by
/// how far the light reaches, because the scene itself puts no light outside the square.
/// A plain Texture2D rather than a RenderTarget2D: the pass only ever *samples* its source, and an
/// ordinary texture upload is the fixture with the fewest moving parts -- no SpriteBatch, no shader,
/// and no dependence on a render target being readable as a texture immediately after a clear.
[[nodiscard]] std::unique_ptr<Texture2D> MakeBrightSquare(GraphicsDevice& device)
{
    auto texture = std::make_unique<Texture2D>(device, kSize, kSize);

    std::vector<Color> pixels(static_cast<std::size_t>(kSize) * kSize, Color::Black);
    const int low  = (kSize - kSprite) / 2;
    for (int y = low; y < low + kSprite; ++y)
        for (int x = low; x < low + kSprite; ++x)
            pixels[static_cast<std::size_t>(y) * kSize + x] = Color::White;
    texture->SetData(pixels.data(), static_cast<int>(pixels.size()));
    return texture;
}

/// Total light outside the square: what bloom adds and nothing else in this scene can produce.
[[nodiscard]] long GlowOutside(const std::vector<Color>& pixels)
{
    const int low  = (kSize - kSprite) / 2;
    const int high = low + kSprite;
    long glow = 0;
    for (int y = 0; y < kSize; ++y)
        for (int x = 0; x < kSize; ++x)
        {
            if (x >= low && x < high && y >= low && y < high) continue;
            glow += pixels[static_cast<std::size_t>(y) * kSize + x].getRProperty();
        }
    return glow;
}

/// How far from the square the light still reaches, in pixels along the centre row. This is the
/// measurement a single-composite pyramid and a progressive one differ on.
[[nodiscard]] int GlowReach(const std::vector<Color>& pixels)
{
    const int centre = kSize / 2;
    const int low    = (kSize - kSprite) / 2;
    int reach = 0;
    for (int x = 0; x < low; ++x)
        if (pixels[static_cast<std::size_t>(centre) * kSize + static_cast<std::size_t>(x)]
                .getRProperty() > 2)
        { reach = low - x; break; }
    return reach;
}

[[nodiscard]] std::vector<Color> RunBloom(GraphicsDevice& device, Texture2D& source,
                                          const RenderPipelineSettings& settings)
{
    BloomPass pass(device);
    RenderTarget2D destination(device, kSize, kSize);

    PostProcessContext context;
    context.source      = &source;
    context.destination = &destination;
    context.width       = kSize;
    context.height      = kSize;
    context.settings    = &settings;
    pass.apply(context);

    std::vector<Color> pixels(static_cast<std::size_t>(kSize) * kSize, Color::Black);
    destination.GetData(pixels.data(), static_cast<int>(pixels.size()));
    return pixels;
}

// =====================================================================================
// MOD-409: the quality mapping
// =====================================================================================

TEST(BloomQualityTest, EachPresetMapsToItsDocumentedLevelCount)
{
    // The numbers are documented in BloomPass.hpp and measured in docs/cnaext-perf.md, so they are
    // pinned here rather than left to drift away from both.
    EXPECT_EQ(BloomPass::iterationsForQuality(RenderQuality::Low), 2);
    EXPECT_EQ(BloomPass::iterationsForQuality(RenderQuality::Medium), 3);
    EXPECT_EQ(BloomPass::iterationsForQuality(RenderQuality::High), 5);
    EXPECT_EQ(BloomPass::iterationsForQuality(RenderQuality::Ultra), 7);
}

TEST(BloomQualityTest, ThePresetsAreOrderedAndWithinTheAcceptedRange)
{
    // Ordering is the property a preset table has to keep whatever the numbers become; the range is
    // what stops a preset from being silently clamped by apply() into a different preset.
    const int low    = BloomPass::iterationsForQuality(RenderQuality::Low);
    const int medium = BloomPass::iterationsForQuality(RenderQuality::Medium);
    const int high   = BloomPass::iterationsForQuality(RenderQuality::High);
    const int ultra  = BloomPass::iterationsForQuality(RenderQuality::Ultra);

    EXPECT_LT(low, medium);
    EXPECT_LT(medium, high);
    EXPECT_LT(high, ultra);
    for (const int levels : {low, medium, high, ultra})
    {
        EXPECT_GE(levels, 1) << "a preset below the accepted minimum would be clamped up";
        EXPECT_LE(levels, 8) << "a preset above the accepted maximum would be clamped down";
    }
}

TEST(BloomQualityTest, AValueOutsideTheEnumGetsTheDefault)
{
    EXPECT_EQ(BloomPass::iterationsForQuality(static_cast<RenderQuality>(99)),
              BloomPass::iterationsForQuality(RenderQuality::Medium));
}

TEST(BloomQualityTest, SettingTheQualityChangesNothingUntilThePresetIsApplied)
{
    // The separation MOD-409 is built on: a game that tuned bloomIterations by hand must not have
    // that value rewritten because something set the quality.
    RenderPipelineSettings settings;
    settings.setBloomIterations(6);
    settings.setRenderQuality(RenderQuality::Low);
    EXPECT_EQ(settings.getBloomIterations(), 6) << "setRenderQuality overwrote a tuned value";

    settings.applyRenderQualityPresetEXT();
    EXPECT_EQ(settings.getBloomIterations(), BloomPass::iterationsForQuality(RenderQuality::Low));
}

TEST(BloomQualityTest, ApplyingThePresetIsIdempotent)
{
    RenderPipelineSettings settings;
    settings.setRenderQuality(RenderQuality::High);
    settings.applyRenderQualityPresetEXT();
    const int once = settings.getBloomIterations();
    settings.applyRenderQualityPresetEXT();
    EXPECT_EQ(settings.getBloomIterations(), once);
}

// =====================================================================================
// MOD-405: the pyramid is walked back up
// =====================================================================================

TEST(BloomPyramidTest, MoreLevelsReachFurtherFromTheSource)
{
    // The property progressive upsampling exists to give. A single composite of the smallest level
    // would produce a glow whose *reach* barely changed with the level count, because the wide,
    // flat smallest level would dominate every configuration.
    GraphicsDevice gd;
    CNA_SKIP_WITHOUT_RENDER_TARGET_READBACK(gd);

    BloomPass probe(gd);
    if (!probe.isSupported(gd))
        GTEST_SKIP() << "this renderer cannot run the bloom shaders";

    auto source = MakeBrightSquare(gd);

    RenderPipelineSettings settings;
    settings.setBloomThreshold(0.5f);
    settings.setBloomIntensity(1.5f);

    settings.setBloomIterations(1);
    const int narrowReach = GlowReach(RunBloom(gd, *source, settings));

    settings.setBloomIterations(5);
    const int wideReach = GlowReach(RunBloom(gd, *source, settings));

    EXPECT_GT(narrowReach, 0) << "even one level must put light outside the square";
    EXPECT_GT(wideReach, narrowReach)
        << "more levels did not reach further -- the upward walk is not adding the small levels "
           "into the large ones";
}

TEST(BloomPyramidTest, EveryLevelContributesRatherThanOnlyTheSmallest)
{
    // The other half of the same property, measured as total energy rather than reach: a pyramid
    // that composited only its smallest level would lose the tight core the large levels carry.
    GraphicsDevice gd;
    CNA_SKIP_WITHOUT_RENDER_TARGET_READBACK(gd);

    BloomPass probe(gd);
    if (!probe.isSupported(gd))
        GTEST_SKIP() << "this renderer cannot run the bloom shaders";

    auto source = MakeBrightSquare(gd);
    RenderPipelineSettings settings;
    settings.setBloomThreshold(0.5f);
    settings.setBloomIntensity(1.0f);

    long previous = 0;
    for (const int levels : {1, 2, 3, 4})
    {
        settings.setBloomIterations(levels);
        const long glow = GlowOutside(RunBloom(gd, *source, settings));
        EXPECT_GT(glow, 0) << "at " << levels << " levels the pass produced no glow at all";
        EXPECT_GE(glow, previous) << "adding level " << levels << " removed light";
        previous = glow;
    }
}

TEST(BloomPyramidTest, TheGlowIsSymmetricAboutTheSource)
{
    // A pyramid whose upsample is offset by half a texel drifts the halo to one side -- an error
    // that looks like a lighting choice rather than a bug.
    GraphicsDevice gd;
    CNA_SKIP_WITHOUT_RENDER_TARGET_READBACK(gd);

    BloomPass probe(gd);
    if (!probe.isSupported(gd))
        GTEST_SKIP() << "this renderer cannot run the bloom shaders";

    auto source = MakeBrightSquare(gd);
    RenderPipelineSettings settings;
    settings.setBloomThreshold(0.5f);
    settings.setBloomIntensity(1.5f);
    settings.setBloomIterations(3);

    const std::vector<Color> bloomed = RunBloom(gd, *source, settings);

    const int centre = kSize / 2;
    long left = 0, right = 0;
    for (int x = 0; x < centre; ++x)
        left += bloomed[static_cast<std::size_t>(centre) * kSize + static_cast<std::size_t>(x)]
                    .getRProperty();
    for (int x = centre; x < kSize; ++x)
        right += bloomed[static_cast<std::size_t>(centre) * kSize + static_cast<std::size_t>(x)]
                     .getRProperty();

    const long total = left + right;
    if (total == 0) GTEST_SKIP() << "this renderer produced no glow to measure";
    const double imbalance = std::abs(static_cast<double>(left - right)) / static_cast<double>(total);
    EXPECT_LT(imbalance, 0.15) << "the halo is lopsided: left " << left << ", right " << right;
}

} // namespace

#endif // CNA_CNAEXT
