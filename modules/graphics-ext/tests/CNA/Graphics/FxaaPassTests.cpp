// SPDX-License-Identifier: MS-PL
// plans/plan_modern.md MOD-600..MOD-610: FXAA.
//
// Anti-aliasing is measured here rather than looked at. A hard black/white edge is rendered, the
// pass runs, and the assertion is that pixels appear whose value lies strictly between the two --
// which is exactly what "the edge was smoothed" means and what a pass-through or a broken shader
// cannot produce. The flat-field case is the other half: a pass that smooths an image with no
// edges in it is a blur, not an edge filter.

#ifdef CNA_CNAEXT

#include <gtest/gtest.h>

#include "CNA/Graphics/FxaaPass.hpp"
#include "CNA/Graphics/PostProcessContext.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteSortMode.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"

#include <vector>

namespace {

using CNA::Graphics::FxaaPass;
using CNA::Graphics::PostProcessContext;
using Microsoft::Xna::Framework::Color;
using Microsoft::Xna::Framework::Rectangle;
using Microsoft::Xna::Framework::Graphics::BlendState;
using Microsoft::Xna::Framework::Graphics::GraphicsDevice;
using Microsoft::Xna::Framework::Graphics::RenderTarget2D;
using Microsoft::Xna::Framework::Graphics::SpriteBatch;
using Microsoft::Xna::Framework::Graphics::SpriteSortMode;
using Microsoft::Xna::Framework::Graphics::Texture2D;

constexpr int kSize = 32;

std::vector<Color> ReadTarget(RenderTarget2D& target)
{
    std::vector<Color> pixels(static_cast<std::size_t>(kSize) * kSize, Color(0, 0, 0, 0));
    target.GetData(pixels.data(), static_cast<int>(pixels.size()));
    return pixels;
}

/// A staircase edge: white blocks stepping diagonally across a black field, which is the aliasing
/// pattern FXAA exists to soften.
void DrawStaircase(GraphicsDevice& gd, RenderTarget2D& target)
{
    Texture2D white(gd, 1, 1);
    const Color texel(255, 255, 255, 255);
    white.SetData(&texel, 1);

    gd.SetRenderTarget(&target);
    gd.Clear(Color::Black);
    SpriteBatch batch(gd);
    batch.Begin(SpriteSortMode::Deferred, BlendState::Opaque);
    for (int step = 0; step < kSize / 4; ++step)
        batch.Draw(white, Rectangle(0, step * 4, (step + 1) * 4, 4), Color::White);
    batch.End();
    gd.SetRenderTarget(nullptr);
}

int CountIntermediateTones(const std::vector<Color>& pixels)
{
    int count = 0;
    for (const Color& texel : pixels)
    {
        const int value = texel.getRProperty();
        if (value > 8 && value < 247)
            ++count;
    }
    return count;
}

TEST(FxaaPassTest, AHardEdgeGainsIntermediateTones)
{
    GraphicsDevice gd;
    FxaaPass fxaa(gd);
    if (!fxaa.isSupported(gd))
        GTEST_SKIP() << "this renderer cannot run custom effects";

    RenderTarget2D source(gd, kSize, kSize);
    RenderTarget2D destination(gd, kSize, kSize);
    DrawStaircase(gd, source);

    const int before = CountIntermediateTones(ReadTarget(source));
    ASSERT_EQ(before, 0) << "the input must be strictly black and white for this to mean anything";

    PostProcessContext context;
    context.source      = &source;
    context.destination = &destination;
    context.width       = kSize;
    context.height      = kSize;
    fxaa.apply(context);

    EXPECT_GT(CountIntermediateTones(ReadTarget(destination)), 0)
        << "no pixel was blended; the pass ran as a copy";
}

TEST(FxaaPassTest, AFlatFieldIsLeftAlone)
{
    // The other half of the contract: an edge filter that also softens flat areas is a blur.
    GraphicsDevice gd;
    FxaaPass fxaa(gd);
    if (!fxaa.isSupported(gd))
        GTEST_SKIP() << "this renderer cannot run custom effects";

    RenderTarget2D source(gd, kSize, kSize);
    RenderTarget2D destination(gd, kSize, kSize);
    gd.SetRenderTarget(&source);
    gd.Clear(Color(90, 140, 200, 255));
    gd.SetRenderTarget(nullptr);

    PostProcessContext context;
    context.source      = &source;
    context.destination = &destination;
    context.width       = kSize;
    context.height      = kSize;
    fxaa.apply(context);

    for (const Color& texel : ReadTarget(destination))
    {
        EXPECT_NEAR(texel.getRProperty(), 90, 1);
        EXPECT_NEAR(texel.getGProperty(), 140, 1);
        EXPECT_NEAR(texel.getBProperty(), 200, 1);
    }
}

TEST(FxaaPassTest, AThresholdAboveEveryContrastDisablesTheFilter)
{
    // Proves the threshold is really consulted: set it beyond any contrast the image contains and
    // the edge must survive untouched.
    GraphicsDevice gd;
    FxaaPass fxaa(gd);
    if (!fxaa.isSupported(gd))
        GTEST_SKIP() << "this renderer cannot run custom effects";

    RenderTarget2D source(gd, kSize, kSize);
    RenderTarget2D destination(gd, kSize, kSize);
    DrawStaircase(gd, source);

    fxaa.setEdgeThreshold(10.0f);
    PostProcessContext context;
    context.source      = &source;
    context.destination = &destination;
    context.width       = kSize;
    context.height      = kSize;
    fxaa.apply(context);

    EXPECT_EQ(CountIntermediateTones(ReadTarget(destination)), 0);
}

TEST(FxaaPassTest, TheThresholdRoundTripsAndTheNameIsStable)
{
    GraphicsDevice gd;
    FxaaPass fxaa(gd);

    EXPECT_FLOAT_EQ(fxaa.getEdgeThreshold(), 0.125f);
    fxaa.setEdgeThreshold(0.25f);
    EXPECT_FLOAT_EQ(fxaa.getEdgeThreshold(), 0.25f);
    EXPECT_EQ(fxaa.getName(), "FXAA");
}

} // namespace

#endif // CNA_CNAEXT
