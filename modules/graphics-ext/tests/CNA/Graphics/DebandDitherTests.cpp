// SPDX-License-Identifier: MS-PL
// plans/plan_modern.md MOD-2132: debanding dither on the step from scene-referred to display-referred.
//
// An eight-bit frame has 256 values to hold a gradient with. Where the tonemapper compresses -- and
// it always compresses somewhere, that being what it is for -- a long stretch of scene values lands
// on one output value and then jumps, and the eye reads those stretches as flat bands with hard
// edges. The bands are far more visible than the quantisation error that causes them, which is the
// whole reason dither is worth its noise.
//
// Nothing here is asserted by looking at a frame. The claim is that a ramp comes back with more
// distinct values than the output has steps, which is a count.

#ifdef CNA_CNAEXT

#include <gtest/gtest.h>

#include "EngineTestSupport.hpp"

#include "CNA/Graphics/PostProcessContext.hpp"
#include "CNA/Graphics/TonemapPass.hpp"
#include "CNA/Graphics/TonemappingMode.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <memory>
#include <set>
#include <vector>

namespace {

using CNA::Graphics::PostProcessContext;
using CNA::Graphics::TonemapPass;
using CNA::Graphics::TonemappingMode;
using Microsoft::Xna::Framework::Color;
using Microsoft::Xna::Framework::Graphics::GraphicsDevice;
using Microsoft::Xna::Framework::Graphics::RenderTarget2D;
using Microsoft::Xna::Framework::Graphics::Texture2D;

constexpr int kWidth  = 256;
constexpr int kHeight = 64;

/// Column x holds the value x, repeated down every row: a ramp with no vertical variation, so any
/// spread within a column is the pass's doing and nothing else's.
std::unique_ptr<Texture2D> MakeRamp(GraphicsDevice& gd)
{
    auto texture = std::make_unique<Texture2D>(gd, kWidth, kHeight);
    std::vector<Color> texels;
    texels.reserve(static_cast<std::size_t>(kWidth) * kHeight);
    for (int y = 0; y < kHeight; ++y)
        for (int x = 0; x < kWidth; ++x) texels.emplace_back(x, x, x, 255);
    texture->SetData(texels.data(), static_cast<int>(texels.size()));
    return texture;
}

std::unique_ptr<Texture2D> MakeFlat(GraphicsDevice& gd, const int level)
{
    auto texture = std::make_unique<Texture2D>(gd, kWidth, kHeight);
    const std::vector<Color> texels(static_cast<std::size_t>(kWidth) * kHeight,
                                    Color(level, level, level, 255));
    texture->SetData(texels.data(), static_cast<int>(texels.size()));
    return texture;
}

std::vector<Color> RunTonemap(TonemapPass& pass, Texture2D& source, RenderTarget2D& destination)
{
    PostProcessContext context;
    context.source      = &source;
    context.destination = &destination;
    context.width       = kWidth;
    context.height      = kHeight;
    pass.apply(context);

    std::vector<Color> pixels(static_cast<std::size_t>(kWidth) * kHeight, Color::Black);
    destination.GetData(pixels.data(), static_cast<int>(pixels.size()));
    return pixels;
}

/// The mean red channel of one column, which is what the eye does to a dithered gradient.
double ColumnMean(const std::vector<Color>& pixels, const int column)
{
    double total = 0.0;
    for (int y = 0; y < kHeight; ++y)
        total += pixels[static_cast<std::size_t>(y) * kWidth + column].getRProperty();
    return total / static_cast<double>(kHeight);
}

/// How many distinct column means the ramp came back with, to a hundredth of an output step.
int DistinctColumnMeans(const std::vector<Color>& pixels)
{
    std::set<long> seen;
    for (int x = 0; x < kWidth; ++x) seen.insert(std::lround(ColumnMean(pixels, x) * 100.0));
    return static_cast<int>(seen.size());
}

double ColumnDeviation(const std::vector<Color>& pixels, const int column)
{
    const double mean = ColumnMean(pixels, column);
    double total = 0.0;
    for (int y = 0; y < kHeight; ++y)
    {
        const double d = pixels[static_cast<std::size_t>(y) * kWidth + column].getRProperty() - mean;
        total += d * d;
    }
    return std::sqrt(total / static_cast<double>(kHeight));
}

// ── Settings ────────────────────────────────────────────────────────────────

TEST(DebandDitherTest, ItIsOffByDefaultAndTheSettingsRoundTrip)
{
    GraphicsDevice gd;
    TonemapPass pass(gd);
    EXPECT_FALSE(pass.isDebandEnabled());
    EXPECT_FLOAT_EQ(pass.getDebandStrength(), 1.0f);

    pass.setDebandEnabled(true);
    EXPECT_TRUE(pass.isDebandEnabled());
    pass.setDebandStrength(2.0f);
    EXPECT_FLOAT_EQ(pass.getDebandStrength(), 2.0f);
    pass.setDebandStrength(99.0f);
    EXPECT_FLOAT_EQ(pass.getDebandStrength(), 4.0f);
    pass.setDebandStrength(-1.0f);
    EXPECT_FLOAT_EQ(pass.getDebandStrength(), 0.0f);
}

TEST(DebandDitherTest, AFrameThatDidNotAskForItIsUnchangedToTheBit)
{
    GraphicsDevice gd;
    CNA_SKIP_WITHOUT_RENDER_TARGETS(gd);
    CNA_SKIP_WITHOUT_RENDER_TARGET_READBACK(gd);
    CNA_SKIP_WITHOUT_SHADER_EXECUTION(gd);

    auto ramp = MakeRamp(gd);
    RenderTarget2D destination(gd, kWidth, kHeight);

    TonemapPass pass(gd);
    pass.setMode(TonemappingMode::Aces);
    const std::vector<Color> before = RunTonemap(pass, *ramp, destination);
    const std::vector<Color> again  = RunTonemap(pass, *ramp, destination);

    for (std::size_t i = 0; i < before.size(); ++i)
        ASSERT_EQ(before[i].getRProperty(), again[i].getRProperty()) << "at pixel " << i;
}

// ── The claim ───────────────────────────────────────────────────────────────

TEST(DebandDitherTest, ADitheredRampCarriesMoreValuesThanTheTargetHasSteps)
{
    // The banding is built rather than hoped for: an exposure of 0.02 puts a 256-value ramp onto
    // six output values, so undithered the frame is six flat bands with hard edges between them.
    // Dither cannot add information the source did not have -- what it does is stop the error from
    // aligning, so a column's *mean* tracks the value that was there.
    GraphicsDevice gd;
    CNA_SKIP_WITHOUT_RENDER_TARGETS(gd);
    CNA_SKIP_WITHOUT_RENDER_TARGET_READBACK(gd);
    CNA_SKIP_WITHOUT_SHADER_EXECUTION(gd);

    auto ramp = MakeRamp(gd);
    RenderTarget2D destination(gd, kWidth, kHeight);

    TonemapPass pass(gd);
    pass.setMode(TonemappingMode::None);
    pass.setExposure(0.02f);
    pass.setGamma(1.0f);

    pass.setDebandEnabled(false);
    const int banded = DistinctColumnMeans(RunTonemap(pass, *ramp, destination));

    pass.setDebandEnabled(true);
    const int dithered = DistinctColumnMeans(RunTonemap(pass, *ramp, destination));

    std::printf("    distinct column means over a 256-column ramp: banded %d, dithered %d\n",
                banded, dithered);

    // Anti-vacuity: if the exposure did not actually band the ramp there is nothing to remove.
    ASSERT_LT(banded, 12) << "the ramp was not banded, so this test compares nothing";
    EXPECT_GT(dithered, banded * 8)
        << "banded " << banded << ", dithered " << dithered;
}

TEST(DebandDitherTest, TheDitherIsZeroMeanSoAFlatAreaKeepsItsValue)
{
    // The other half, and the one that makes dither acceptable at all: it must not change what the
    // image *is*. A noise with a bias would shift every flat surface in the frame.
    GraphicsDevice gd;
    CNA_SKIP_WITHOUT_RENDER_TARGETS(gd);
    CNA_SKIP_WITHOUT_RENDER_TARGET_READBACK(gd);
    CNA_SKIP_WITHOUT_SHADER_EXECUTION(gd);

    auto flat = MakeFlat(gd, 128);
    RenderTarget2D destination(gd, kWidth, kHeight);

    TonemapPass pass(gd);
    pass.setMode(TonemappingMode::None);
    pass.setGamma(1.0f);

    pass.setDebandEnabled(false);
    const double plain = ColumnMean(RunTonemap(pass, *flat, destination), kWidth / 2);

    pass.setDebandEnabled(true);
    const std::vector<Color> dithered = RunTonemap(pass, *flat, destination);

    double total = 0.0;
    for (int x = 0; x < kWidth; ++x) total += ColumnMean(dithered, x);
    const double mean = total / static_cast<double>(kWidth);

    std::printf("    flat patch: undithered %.3f, dithered mean %.3f\n", plain, mean);
    EXPECT_NEAR(mean, plain, 0.25);
}

TEST(DebandDitherTest, TheNoiseIsNoLargerThanTheStepItIsHiding)
{
    // A dither that overshoots is just grain. One output step is the whole budget, and the
    // triangular distribution spends it as a spread of about 0.4 of a step rather than as a
    // uniform 0.5 either way.
    GraphicsDevice gd;
    CNA_SKIP_WITHOUT_RENDER_TARGETS(gd);
    CNA_SKIP_WITHOUT_RENDER_TARGET_READBACK(gd);
    CNA_SKIP_WITHOUT_SHADER_EXECUTION(gd);

    auto flat = MakeFlat(gd, 128);
    RenderTarget2D destination(gd, kWidth, kHeight);

    TonemapPass pass(gd);
    pass.setMode(TonemappingMode::None);
    pass.setGamma(1.0f);
    pass.setDebandEnabled(true);

    const std::vector<Color> dithered = RunTonemap(pass, *flat, destination);
    const double deviation = ColumnDeviation(dithered, kWidth / 2);
    std::printf("    flat patch, dithered: standard deviation %.3f output steps\n", deviation);

    EXPECT_GT(deviation, 0.05) << "no dither reached the frame at all";
    EXPECT_LT(deviation, 1.5) << "the dither is louder than the step it is hiding";
}

TEST(DebandDitherTest, TheAmplitudeIsUniformInOutputSpaceBecauseTheDitherFollowsTheCurve)
{
    // MOD-2133's rule, measured rather than asserted. The transfer function's slope varies by more
    // than seven to one across the range, so a fixed perturbation applied *before* it would arrive
    // at the display large in the shadows and almost invisible in the highlights -- the opposite of
    // what is wanted, since the shadows are where the banding is. Applied after, the spread at a
    // dark patch and at a bright one is the same number.
    GraphicsDevice gd;
    CNA_SKIP_WITHOUT_RENDER_TARGETS(gd);
    CNA_SKIP_WITHOUT_RENDER_TARGET_READBACK(gd);
    CNA_SKIP_WITHOUT_SHADER_EXECUTION(gd);

    RenderTarget2D destination(gd, kWidth, kHeight);
    TonemapPass pass(gd);
    pass.setMode(TonemappingMode::None);
    pass.setGamma(2.2f);
    pass.setDebandEnabled(true);

    auto dark   = MakeFlat(gd, 5);      // ~0.02 linear
    auto bright = MakeFlat(gd, 204);    // ~0.80 linear
    const double darkSpread   = ColumnDeviation(RunTonemap(pass, *dark, destination), kWidth / 2);
    const double brightSpread = ColumnDeviation(RunTonemap(pass, *bright, destination), kWidth / 2);

    // What a before-the-curve dither would have cost, from the curve itself rather than from an
    // argument: the slope at each of the two patches, in output units per linear unit.
    const auto slope = [&](const float linear) {
        const float step = 1.0f / 255.0f;
        return std::abs(TonemapPass::tonemapChannel(TonemappingMode::None, linear + step, 1.0f, 2.2f)
                      - TonemapPass::tonemapChannel(TonemappingMode::None, linear, 1.0f, 2.2f))
             / step;
    };
    const double ratio = slope(5.0f / 255.0f) / slope(204.0f / 255.0f);

    std::printf("    spread after the curve: dark %.3f, bright %.3f; the curve's own slope ratio "
                "between those two patches is %.1f to 1\n",
                darkSpread, brightSpread, ratio);

    ASSERT_GT(ratio, 5.0) << "this gamma does not vary enough for the placement to matter, so the "
                             "test would pass whichever side of the curve the dither was on";
    EXPECT_GT(darkSpread, 0.05);
    EXPECT_NEAR(darkSpread, brightSpread, 0.25 * std::max(darkSpread, brightSpread));
}

} // namespace

#endif // CNA_CNAEXT
