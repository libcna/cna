// SPDX-License-Identifier: MS-PL
// plans/plan_modern.md MOD-2051: light shafts.
//
// A shaft is the *shape of an occluder*, not a glow: what makes it read as light through a gap is
// that the pixels behind the occluder got nothing to gather. So the tests put an occluder in the
// way and check the absence, not just the presence.

#ifdef CNA_CNAEXT

#include <gtest/gtest.h>

#include "CNA/Graphics/LightShaftPass.hpp"
#include "CNA/Graphics/PostProcessContext.hpp"
#include "CNA/Graphics/RenderPipelineSettings.hpp"
#include "EngineTestSupport.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"

#include <functional>
#include <memory>
#include <vector>

namespace {

using CNA::Graphics::LightShaftPass;
using CNA::Graphics::PostProcessContext;
using Microsoft::Xna::Framework::Color;
using Microsoft::Xna::Framework::Vector2;
using Microsoft::Xna::Framework::Graphics::GraphicsDevice;
using Microsoft::Xna::Framework::Graphics::RenderTarget2D;
using Microsoft::Xna::Framework::Graphics::Texture2D;

constexpr int kSize = 64;

/// The row a shader sees at this sampled V, and the reason it is not the obvious one: **sampling a
/// render target is vertically flipped** relative to the rows a `Texture2D` was filled with
/// (`MOD-2000`). Every image here is described in the coordinates the pass reads it in, so a light
/// placed at V = 0.05 really is at the top of what the shader sees rather than the bottom.
int RowForSampledV(const int v) { return kSize - 1 - v; }

/// @param colourAt Takes (x, v) where v is the row *as the shader samples it*.
std::unique_ptr<RenderTarget2D> MakeImage(GraphicsDevice& gd,
                                          const std::function<Color(int, int)>& colourAt)
{
    auto staging = std::make_unique<Texture2D>(gd, kSize, kSize);
    std::vector<Color> texels(static_cast<std::size_t>(kSize) * kSize, Color(0, 0, 0, 255));
    for (int v = 0; v < kSize; ++v)
        for (int x = 0; x < kSize; ++x)
            texels[static_cast<std::size_t>(RowForSampledV(v)) * kSize + x] = colourAt(x, v);
    staging->SetData(texels.data(), static_cast<int>(texels.size()));
    auto target = std::make_unique<RenderTarget2D>(gd, kSize, kSize);
    CNA::Graphics::FullscreenPass blit(gd);
    blit.draw(staging.get(), target.get(), nullptr, kSize, kSize);
    return target;
}

std::vector<Color> ReadTarget(RenderTarget2D& target)
{
    std::vector<Color> pixels(static_cast<std::size_t>(kSize) * kSize, Color(0, 0, 0, 0));
    target.GetData(pixels.data(), static_cast<int>(pixels.size()));
    return pixels;
}

/// Destination rows map to the V the shader wrote at, unflipped -- only *sampling* a target flips.
std::size_t At(const int x, const int v) { return static_cast<std::size_t>(v) * kSize + x; }

PostProcessContext MakeContext(RenderTarget2D& source, RenderTarget2D& destination)
{
    PostProcessContext context;
    context.source      = &source;
    context.destination = &destination;
    context.width       = kSize;
    context.height      = kSize;
    return context;
}

/// A bright disc at the top-left, with a dark bar hiding the lower-left quarter of the path to it.
/// The bar is the occluder whose *shadow in the shafts* the tests look for.
std::unique_ptr<RenderTarget2D> MakeLightAndOccluder(GraphicsDevice& gd)
{
    return MakeImage(gd, [](const int x, const int y) {
        const bool bright = x < 20 && y < 20;
        if (bright) return Color(255, 255, 255, 255);
        // A bar across the lower half, in the way of anything gathering upward from below it.
        const bool bar = y >= 34 && y < 40;
        if (bar) return Color(0, 0, 0, 255);
        return Color(0, 0, 0, 255);
    });
}

TEST(LightShaftTest, PixelsOnTheClearPathToTheLightBrighten)
{
    // The path from a pixel to the light passes through the bright region near it, so what the walk
    // gathers piles up. A pixel with nothing bright on its path gathers nothing.
    GraphicsDevice gd;
    LightShaftPass pass(gd);
    CNA_SKIP_WITHOUT_SHADER_EXECUTION(gd);
    CNA_SKIP_WITHOUT_RENDER_TARGET_READBACK(gd);

    auto source = MakeLightAndOccluder(gd);
    RenderTarget2D destination(gd, kSize, kSize);

    pass.setLightScreenPosition(Vector2(0.15f, 0.15f));
    pass.setThreshold(0.5f);
    pass.setIntensity(3.0f);
    pass.setDecay(0.95f);

    PostProcessContext context = MakeContext(*source, destination);
    pass.apply(context);

    const std::vector<Color> pixels = ReadTarget(destination);

    // Just outside the bright disc, on the line towards it: the walk crosses the disc.
    const int nearTheLight = pixels[At(26, 26)].getRProperty();
    // The far corner: its path to the light is the long diagonal, and the disc is at the very end,
    // by which point the decay has taken most of it.
    const int farCorner = pixels[At(60, 60)].getRProperty();

    EXPECT_GT(nearTheLight, 20) << "a pixel with a clear path to the light gathered nothing";
    EXPECT_GT(nearTheLight, farCorner)
        << "distance from the light made no difference: " << nearTheLight << " against " << farCorner;
}

TEST(LightShaftTest, AnOccluderLeavesItsShapeInTheShafts)
{
    // The claim that separates a shaft from a glow. A pixel whose path to the light is blocked by
    // the bar gathers nothing across it, so it stays darker than a neighbour at the same distance
    // whose path is clear. The absence is the effect.
    GraphicsDevice gd;
    LightShaftPass pass(gd);
    CNA_SKIP_WITHOUT_SHADER_EXECUTION(gd);
    CNA_SKIP_WITHOUT_RENDER_TARGET_READBACK(gd);

    // The occluder has to take *light* away, not merely be dark: a black bar on a black background
    // subtracts nothing, because a walk gathers brightness and there was none there to lose. The
    // first version of this test made exactly that mistake and both sides came back identical.
    //
    // So: the light fills the top half, and the occluder is a bar cut out of it over the left half.
    // Two pixels on the same row then differ in how much of the lit region their walk crosses.
    auto source = MakeImage(gd, [](const int x, const int v) {
        const bool lit = v < kSize / 2;
        const bool occluded = v >= 8 && v < 20 && x < kSize / 2;
        return (lit && !occluded) ? Color(255, 255, 255, 255) : Color(0, 0, 0, 255);
    });
    RenderTarget2D destination(gd, kSize, kSize);

    pass.setLightScreenPosition(Vector2(0.5f, 0.02f));
    pass.setThreshold(0.5f);
    pass.setIntensity(3.0f);
    pass.setDecay(0.97f);

    PostProcessContext context = MakeContext(*source, destination);
    pass.apply(context);

    const std::vector<Color> pixels = ReadTarget(destination);
    // Two pixels on the same row, equally far from the light. The left one's walk crosses the
    // cut-out; the right one's crosses lit sky the whole way.
    const int behindTheBar = pixels[At(10, 50)].getRProperty();
    const int besideIt     = pixels[At(54, 50)].getRProperty();

    EXPECT_GT(besideIt, 10) << "the unblocked side gathered nothing, so nothing was compared";
    EXPECT_LT(behindTheBar, besideIt)
        << "the occluder left no shadow in the shafts: " << behindTheBar << " against " << besideIt;
}

TEST(LightShaftTest, ALightWellOffScreenStopsContributing)
{
    // A light past the edge still throws shafts inward, and the effect has to fade with how far
    // outside it is. A hard border test would switch the whole effect off in one frame as the sun
    // leaves the view, which is the giveaway this avoids.
    GraphicsDevice gd;
    LightShaftPass pass(gd);
    CNA_SKIP_WITHOUT_SHADER_EXECUTION(gd);
    CNA_SKIP_WITHOUT_RENDER_TARGET_READBACK(gd);

    auto source = MakeImage(gd, [](int, const int y) {
        return y < 12 ? Color(255, 255, 255, 255) : Color(0, 0, 0, 255);
    });
    RenderTarget2D destination(gd, kSize, kSize);
    pass.setThreshold(0.5f);
    pass.setIntensity(3.0f);
    pass.setDecay(0.97f);

    const auto brightnessWithLightAt = [&](const float v) {
        pass.setLightScreenPosition(Vector2(0.5f, v));
        PostProcessContext context = MakeContext(*source, destination);
        pass.apply(context);
        return static_cast<int>(ReadTarget(destination)[At(32, 40)].getRProperty());
    };

    const int onScreen   = brightnessWithLightAt(0.05f);
    const int justPast   = brightnessWithLightAt(-0.15f);
    const int wellPast   = brightnessWithLightAt(-0.9f);

    EXPECT_GT(onScreen, 10) << "an on-screen light produced no shafts, so nothing was compared";
    EXPECT_LT(justPast, onScreen) << "leaving the frame did not weaken the shafts";
    EXPECT_EQ(wellPast, 0) << "a light far outside the frame still contributed";
}

TEST(LightShaftTest, ZeroIntensityLeavesTheFrameAlone)
{
    GraphicsDevice gd;
    LightShaftPass pass(gd);
    CNA_SKIP_WITHOUT_RENDER_TARGET_READBACK(gd);

    auto source = MakeLightAndOccluder(gd);
    RenderTarget2D destination(gd, kSize, kSize);

    PostProcessContext context = MakeContext(*source, destination);
    pass.apply(context);

    EXPECT_LT(ReadTarget(destination)[At(40, 40)].getRProperty(), 4)
        << "a disabled pass still added shafts";
}

TEST(LightShaftTest, TheSettingsRoundTripAndTheNameIsStable)
{
    GraphicsDevice gd;
    LightShaftPass pass(gd);
    EXPECT_EQ(pass.getName(), "LightShafts");
    EXPECT_FLOAT_EQ(pass.getIntensity(), 0.0f) << "the effect must be off by default";

    pass.setLightScreenPosition(Vector2(1.4f, -0.2f));
    EXPECT_FLOAT_EQ(pass.getLightScreenPosition().X, 1.4f)
        << "a position outside the frame is meaningful and must not be clamped away";
    EXPECT_FLOAT_EQ(pass.getLightScreenPosition().Y, -0.2f);

    pass.setThreshold(1.5f);
    pass.setIntensity(0.8f);
    pass.setDecay(0.5f);
    EXPECT_FLOAT_EQ(pass.getThreshold(), 1.5f);
    EXPECT_FLOAT_EQ(pass.getIntensity(), 0.8f);
    EXPECT_FLOAT_EQ(pass.getDecay(), 0.5f);

    pass.setThreshold(-1.0f);
    pass.setIntensity(-1.0f);
    EXPECT_FLOAT_EQ(pass.getThreshold(), 1.5f);
    EXPECT_FLOAT_EQ(pass.getIntensity(), 0.8f);
    pass.setDecay(9.0f);
    EXPECT_FLOAT_EQ(pass.getDecay(), 1.0f);
    pass.setDecay(-9.0f);
    EXPECT_FLOAT_EQ(pass.getDecay(), 0.0f);
}

} // namespace

#endif // CNA_CNAEXT
