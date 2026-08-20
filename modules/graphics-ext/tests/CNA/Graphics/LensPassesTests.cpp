// SPDX-License-Identifier: MS-PL
// plans/plan_modern.md MOD-2022..MOD-2024: chromatic aberration, film grain, lens flare.
//
// Each of the three is off by default, so the first thing every one of them is asked is whether it
// leaves an untouched frame untouched. That is not a formality: three passes added to a chain that
// a game did not opt into is exactly how a "no visual change" release stops being one.

#ifdef CNA_CNAEXT

#include <gtest/gtest.h>

#include "CNA/Graphics/ChromaticAberrationPass.hpp"
#include "CNA/Graphics/FilmGrainPass.hpp"
#include "CNA/Graphics/LensFlarePass.hpp"
#include "CNA/Graphics/PostProcessContext.hpp"
#include "CNA/Graphics/RenderPipelineSettings.hpp"
#include "EngineTestSupport.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"

#include <cmath>
#include <functional>
#include <memory>
#include <vector>

namespace {

using CNA::Graphics::ChromaticAberrationPass;
using CNA::Graphics::FilmGrainPass;
using CNA::Graphics::LensFlarePass;
using CNA::Graphics::PostProcessContext;
using Microsoft::Xna::Framework::Color;
using Microsoft::Xna::Framework::Graphics::GraphicsDevice;
using Microsoft::Xna::Framework::Graphics::RenderTarget2D;
using Microsoft::Xna::Framework::Graphics::Texture2D;

constexpr int kSize = 64;

std::unique_ptr<RenderTarget2D> MakeImage(GraphicsDevice& gd,
                                          const std::function<Color(int, int)>& colourAt)
{
    auto staging = std::make_unique<Texture2D>(gd, kSize, kSize);
    std::vector<Color> texels;
    texels.reserve(static_cast<std::size_t>(kSize) * kSize);
    for (int y = 0; y < kSize; ++y)
        for (int x = 0; x < kSize; ++x)
            texels.push_back(colourAt(x, y));
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

PostProcessContext MakeContext(RenderTarget2D& source, RenderTarget2D& destination)
{
    PostProcessContext context;
    context.source      = &source;
    context.destination = &destination;
    context.width       = kSize;
    context.height      = kSize;
    return context;
}

std::size_t At(const int x, const int y) { return static_cast<std::size_t>(y) * kSize + x; }

// ── Chromatic aberration (MOD-2022) ──────────────────────────────────────────

TEST(ChromaticAberrationTest, TheCentreIsUntouchedAndTheCornersFringe)
{
    // The defining property, and the one that makes the effect read as a lens: the error grows with
    // distance from the axis, so the middle of the frame is sharp however strong the setting is. A
    // pass that offset by a constant would fringe the centre too, and would look like a mistake.
    //
    // The scene is vertical stripes across the *whole* frame rather than one edge, and that is not
    // decoration. The offset is radial, so near the centre column it has almost no horizontal
    // component -- a single edge down the middle of the frame is the one place the effect cannot
    // show itself, and testing there measures nothing. Stripes give the sampler something to
    // separate at every radius.
    GraphicsDevice gd;
    ChromaticAberrationPass pass(gd);
    CNA_SKIP_WITHOUT_SHADER_EXECUTION(gd);
    CNA_SKIP_WITHOUT_RENDER_TARGET_READBACK(gd);

    auto source = MakeImage(gd, [](const int x, int) {
        return (x / 4) % 2 == 0 ? Color(255, 255, 255, 255) : Color(0, 0, 0, 255);
    });
    RenderTarget2D destination(gd, kSize, kSize);

    pass.setStrength(0.1f);
    PostProcessContext context = MakeContext(*source, destination);
    pass.apply(context);

    const std::vector<Color> pixels = ReadTarget(destination);

    const auto worstSeparationIn = [&](const int x0, const int x1) {
        int worst = 0;
        for (int y = 4; y < kSize - 4; ++y)
            for (int x = x0; x < x1; ++x)
            {
                const Color p = pixels[At(x, y)];
                worst = std::max(worst, std::abs(static_cast<int>(p.getRProperty()) -
                                                 static_cast<int>(p.getBProperty())));
            }
        return worst;
    };

    // Two columns either side of the axis: the offset there is a fraction of a texel.
    EXPECT_LE(worstSeparationIn(kSize / 2 - 1, kSize / 2 + 1), 40)
        << "the centre of the frame fringed as hard as the edge, so the offset is not radial";
    // Well out towards the left edge, where the same setting moves red and blue several texels
    // apart in opposite directions.
    EXPECT_GT(worstSeparationIn(4, 14), 120)
        << "the channels did not separate away from the axis";
}

TEST(ChromaticAberrationTest, ZeroStrengthLeavesTheFrameAlone)
{
    GraphicsDevice gd;
    ChromaticAberrationPass pass(gd);
    CNA_SKIP_WITHOUT_RENDER_TARGET_READBACK(gd);

    auto source = MakeImage(gd, [](const int x, int) {
        return x < kSize / 2 ? Color(255, 255, 255, 255) : Color(0, 0, 0, 255);
    });
    RenderTarget2D destination(gd, kSize, kSize);

    PostProcessContext context = MakeContext(*source, destination);
    pass.apply(context);

    const std::vector<Color> pixels = ReadTarget(destination);
    const Color corner = pixels[At(4, 4)];
    EXPECT_EQ(corner.getRProperty(), corner.getBProperty())
        << "a disabled pass still fringed the frame";
}

TEST(ChromaticAberrationTest, TheStrengthIsClampedAndTheNameIsStable)
{
    GraphicsDevice gd;
    ChromaticAberrationPass pass(gd);
    EXPECT_EQ(pass.getName(), "ChromaticAberration");
    EXPECT_FLOAT_EQ(pass.getStrength(), 0.0f) << "the effect must be off by default";
    pass.setStrength(0.03f);
    EXPECT_FLOAT_EQ(pass.getStrength(), 0.03f);
    pass.setStrength(5.0f);
    EXPECT_FLOAT_EQ(pass.getStrength(), 0.1f);
    pass.setStrength(-1.0f);
    EXPECT_FLOAT_EQ(pass.getStrength(), 0.0f);
}

// ── Film grain (MOD-2023) ────────────────────────────────────────────────────

TEST(FilmGrainTest, TheGrainIsDeterministicForAGivenTime)
{
    // A rendered sequence has to be reproducible, and a pass seeded by anything but its inputs is
    // not. Two applications at the same time must agree exactly.
    GraphicsDevice gd;
    FilmGrainPass pass(gd);
    CNA_SKIP_WITHOUT_SHADER_EXECUTION(gd);
    CNA_SKIP_WITHOUT_RENDER_TARGET_READBACK(gd);

    auto source = MakeImage(gd, [](int, int) { return Color(128, 128, 128, 255); });
    RenderTarget2D destination(gd, kSize, kSize);
    pass.setIntensity(0.5f);

    const auto frameAt = [&](const float seconds) {
        PostProcessContext context = MakeContext(*source, destination);
        context.elapsedSeconds = seconds;
        pass.apply(context);
        return ReadTarget(destination);
    };

    const std::vector<Color> first  = frameAt(1.25f);
    const std::vector<Color> second = frameAt(1.25f);
    const std::vector<Color> later  = frameAt(2.5f);

    for (std::size_t i = 0; i < first.size(); ++i)
        ASSERT_EQ(first[i].getRProperty(), second[i].getRProperty())
            << "the same time produced different grain at pixel " << i;

    int differences = 0;
    for (std::size_t i = 0; i < first.size(); ++i)
        if (first[i].getRProperty() != later[i].getRProperty()) ++differences;
    EXPECT_GT(differences, kSize * kSize / 4) << "the grain did not move with time";
}

TEST(FilmGrainTest, TheMidtonesCarryMoreGrainThanTheBlacks)
{
    // What separates grain from noise. Real grain is buried in blacks and invisible in blown
    // highlights; uniform noise across the range reads as a broken sensor.
    GraphicsDevice gd;
    FilmGrainPass pass(gd);
    CNA_SKIP_WITHOUT_SHADER_EXECUTION(gd);
    CNA_SKIP_WITHOUT_RENDER_TARGET_READBACK(gd);

    // Top half near black, bottom half mid grey.
    auto source = MakeImage(gd, [](int, const int y) {
        const int value = y < kSize / 2 ? 4 : 128;
        return Color(value, value, value, 255);
    });
    RenderTarget2D destination(gd, kSize, kSize);
    pass.setIntensity(0.5f);

    PostProcessContext context = MakeContext(*source, destination);
    context.elapsedSeconds = 0.75f;
    pass.apply(context);

    const std::vector<Color> pixels = ReadTarget(destination);
    const auto spreadOver = [&](const int firstRow, const int lastRow) {
        double sum = 0.0, sumSquares = 0.0;
        int count = 0;
        for (int y = firstRow; y <= lastRow; ++y)
            for (int x = 0; x < kSize; ++x)
            {
                const double v = pixels[At(x, y)].getRProperty();
                sum += v; sumSquares += v * v; ++count;
            }
        const double mean = sum / count;
        return std::sqrt(sumSquares / count - mean * mean);
    };

    const double blacks   = spreadOver(2, kSize / 2 - 2);
    const double midtones = spreadOver(kSize / 2 + 2, kSize - 2);
    EXPECT_GT(midtones, blacks * 2.0)
        << "the grain was as strong in the blacks as in the midtones: " << blacks
        << " against " << midtones;
}

TEST(FilmGrainTest, ZeroIntensityLeavesTheFrameAlone)
{
    GraphicsDevice gd;
    FilmGrainPass pass(gd);
    CNA_SKIP_WITHOUT_RENDER_TARGET_READBACK(gd);

    auto source = MakeImage(gd, [](int, int) { return Color(128, 128, 128, 255); });
    RenderTarget2D destination(gd, kSize, kSize);

    PostProcessContext context = MakeContext(*source, destination);
    context.elapsedSeconds = 3.0f;
    pass.apply(context);

    for (const Color& pixel : ReadTarget(destination))
        ASSERT_NEAR(pixel.getRProperty(), 128, 3) << "a disabled pass still added grain";
}

TEST(FilmGrainTest, TheIntensityIsClampedAndTheNameIsStable)
{
    GraphicsDevice gd;
    FilmGrainPass pass(gd);
    EXPECT_EQ(pass.getName(), "FilmGrain");
    EXPECT_FLOAT_EQ(pass.getIntensity(), 0.0f) << "the effect must be off by default";
    pass.setIntensity(0.3f);
    EXPECT_FLOAT_EQ(pass.getIntensity(), 0.3f);
    pass.setIntensity(9.0f);
    EXPECT_FLOAT_EQ(pass.getIntensity(), 1.0f);
    pass.setIntensity(-2.0f);
    EXPECT_FLOAT_EQ(pass.getIntensity(), 0.0f);
}

// ── Lens flare (MOD-2024) ────────────────────────────────────────────────────

TEST(LensFlareTest, TheGhostsLandOnTheOppositeSideOfTheCentre)
{
    // The one property that makes flare read as a lens rather than as a smear. A bright spot in one
    // corner throws its reflections through the optical axis and into the opposite corner; a pass
    // that stepped the other way would pile the ghosts on top of the light that made them.
    GraphicsDevice gd;
    LensFlarePass pass(gd);
    CNA_SKIP_WITHOUT_SHADER_EXECUTION(gd);
    CNA_SKIP_WITHOUT_RENDER_TARGET_READBACK(gd);

    // A bright block in the top-left quadrant, on an otherwise black frame.
    auto source = MakeImage(gd, [](const int x, const int y) {
        const bool bright = x >= 8 && x < 16 && y >= 8 && y < 16;
        return bright ? Color(255, 255, 255, 255) : Color(0, 0, 0, 255);
    });
    RenderTarget2D destination(gd, kSize, kSize);

    pass.setThreshold(0.5f);
    pass.setIntensity(1.0f);
    pass.setDispersal(0.35f);

    PostProcessContext context = MakeContext(*source, destination);
    pass.apply(context);

    const std::vector<Color> pixels = ReadTarget(destination);

    const auto brightestIn = [&](const int x0, const int y0, const int x1, const int y1) {
        int best = 0;
        for (int y = y0; y < y1; ++y)
            for (int x = x0; x < x1; ++x)
                best = std::max(best, static_cast<int>(pixels[At(x, y)].getRProperty()));
        return best;
    };

    // The opposite quadrant was black in the source and must not be now.
    const int opposite = brightestIn(kSize / 2, kSize / 2, kSize, kSize);
    EXPECT_GT(opposite, 30) << "no ghost reached the far side of the centre";

    // And the quadrant *beyond* the light, away from the centre, must have stayed black: that is
    // where the ghosts would be if the step ran the wrong way.
    const int behindTheLight = brightestIn(0, 0, 8, 8);
    EXPECT_LT(behindTheLight, opposite)
        << "the ghosts landed on the light's own side of the frame";
}

TEST(LensFlareTest, AFrameBelowTheThresholdIsUnchanged)
{
    // The threshold is what separates a light from a bright wall. Without it every white surface in
    // the frame throws ghosts and the image turns to soup.
    GraphicsDevice gd;
    LensFlarePass pass(gd);
    CNA_SKIP_WITHOUT_SHADER_EXECUTION(gd);
    CNA_SKIP_WITHOUT_RENDER_TARGET_READBACK(gd);

    auto source = MakeImage(gd, [](int, int) { return Color(90, 90, 90, 255); });
    RenderTarget2D destination(gd, kSize, kSize);

    pass.setThreshold(0.9f);   // well above the frame's own 0.35
    pass.setIntensity(1.0f);

    PostProcessContext context = MakeContext(*source, destination);
    pass.apply(context);

    for (const Color& pixel : ReadTarget(destination))
        ASSERT_NEAR(pixel.getRProperty(), 90, 3) << "a frame below the threshold still flared";
}

TEST(LensFlareTest, ZeroIntensityLeavesTheFrameAlone)
{
    GraphicsDevice gd;
    LensFlarePass pass(gd);
    CNA_SKIP_WITHOUT_RENDER_TARGET_READBACK(gd);

    auto source = MakeImage(gd, [](const int x, const int y) {
        const bool bright = x >= 8 && x < 16 && y >= 8 && y < 16;
        return bright ? Color(255, 255, 255, 255) : Color(0, 0, 0, 255);
    });
    RenderTarget2D destination(gd, kSize, kSize);

    PostProcessContext context = MakeContext(*source, destination);
    pass.apply(context);

    const std::vector<Color> pixels = ReadTarget(destination);
    EXPECT_LT(pixels[At(kSize - 10, kSize - 10)].getRProperty(), 4)
        << "a disabled pass still cast ghosts";
}

TEST(LensFlareTest, TheSettingsRoundTripAndTheNameIsStable)
{
    GraphicsDevice gd;
    LensFlarePass pass(gd);
    EXPECT_EQ(pass.getName(), "LensFlare");
    EXPECT_FLOAT_EQ(pass.getIntensity(), 0.0f) << "the effect must be off by default";

    pass.setThreshold(2.0f);
    pass.setIntensity(0.6f);
    pass.setDispersal(0.5f);
    EXPECT_FLOAT_EQ(pass.getThreshold(), 2.0f);
    EXPECT_FLOAT_EQ(pass.getIntensity(), 0.6f);
    EXPECT_FLOAT_EQ(pass.getDispersal(), 0.5f);

    pass.setThreshold(-1.0f);
    pass.setIntensity(-1.0f);
    EXPECT_FLOAT_EQ(pass.getThreshold(), 2.0f);
    EXPECT_FLOAT_EQ(pass.getIntensity(), 0.6f);
    pass.setDispersal(9.0f);
    EXPECT_FLOAT_EQ(pass.getDispersal(), 1.0f);
}

} // namespace

#endif // CNA_CNAEXT
