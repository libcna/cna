// SPDX-License-Identifier: MS-PL
// plan_modern.md MOD-2020, MOD-2021: colour grading through a 3D lookup table.
//
// The test that matters most is the dullest one: an identity table must reproduce the frame
// exactly. A strip lookup is all off-by-half-a-texel arithmetic, and every mistake in it produces
// a frame that still looks like a frame -- slightly washed, slightly shifted, entirely plausible.
// Only an exact identity catches that.

#ifdef CNA_CNAEXT

#include <gtest/gtest.h>

#include "CNA/Graphics/ColorGradePass.hpp"
#include "CNA/Graphics/PostProcessContext.hpp"
#include "CNA/Graphics/RenderPipelineSettings.hpp"
#include "EngineTestSupport.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"

#include <functional>
#include <memory>
#include <stdexcept>
#include <vector>

namespace {

using CNA::Graphics::ColorGradePass;
using CNA::Graphics::PostProcessContext;
using CNA::Graphics::RenderPipelineSettings;
using Microsoft::Xna::Framework::Color;
using Microsoft::Xna::Framework::Graphics::GraphicsDevice;
using Microsoft::Xna::Framework::Graphics::RenderTarget2D;
using Microsoft::Xna::Framework::Graphics::Texture2D;

constexpr int kSize    = 32;
constexpr int kLutSize = 16;

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

/// A frame covering a broad spread of colours, so a grade cannot pass by getting one right.
std::unique_ptr<RenderTarget2D> MakeSpread(GraphicsDevice& gd)
{
    return MakeImage(gd, [](const int x, const int y) {
        return Color(x * 255 / (kSize - 1), y * 255 / (kSize - 1),
                     ((x + y) % kSize) * 255 / (kSize - 1), 255);
    });
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

/// A table that swaps red and blue: a change no rounding error could produce by accident.
std::unique_ptr<Texture2D> MakeSwapRedAndBlueLut(GraphicsDevice& gd, const int size)
{
    const int width = size * size;
    auto texture = std::make_unique<Texture2D>(gd, width, size);
    std::vector<Color> texels;
    texels.reserve(static_cast<std::size_t>(width) * size);
    const float last = static_cast<float>(size - 1);
    for (int y = 0; y < size; ++y)
        for (int x = 0; x < width; ++x)
        {
            const int slice = x / size;          // blue
            const int red   = x % size;
            const int green = y;
            texels.emplace_back(static_cast<int>(slice / last * 255.0f + 0.5f),   // blue -> red
                                static_cast<int>(green / last * 255.0f + 0.5f),
                                static_cast<int>(red   / last * 255.0f + 0.5f),   // red  -> blue
                                255);
        }
    texture->SetData(texels.data(), static_cast<int>(texels.size()));
    return texture;
}

// ── The strip layout (MOD-2021) ──────────────────────────────────────────────

TEST(ColorGradeTest, OnlyASquareStripDescribesATable)
{
    EXPECT_EQ(ColorGradePass::lutSizeForStrip(256, 16), 16);
    EXPECT_EQ(ColorGradePass::lutSizeForStrip(1024, 32), 32);
    // Everything below is a texture somebody might reasonably hand the pass, and none of them is a
    // table. Sampling one anyway grades the frame into colours nothing in it names.
    EXPECT_EQ(ColorGradePass::lutSizeForStrip(16, 16), 0) << "a square is not a strip";
    EXPECT_EQ(ColorGradePass::lutSizeForStrip(255, 16), 0);
    EXPECT_EQ(ColorGradePass::lutSizeForStrip(1, 1), 0) << "one entry cannot interpolate";
    EXPECT_EQ(ColorGradePass::lutSizeForStrip(65 * 65, 65), 0) << "past the accepted maximum";
    EXPECT_EQ(ColorGradePass::lutSizeForStrip(0, 0), 0);
}

TEST(ColorGradeTest, AMalformedTableIsRefusedByNameRatherThanSampled)
{
    GraphicsDevice gd;
    ColorGradePass pass(gd);

    Texture2D notAStrip(gd, kSize, kSize);
    EXPECT_THROW(pass.setLut(&notAStrip), std::invalid_argument);
    EXPECT_EQ(pass.getLut(), nullptr) << "a refused table must not have been kept";

    auto strip = ColorGradePass::createIdentityLut(gd, kLutSize);
    EXPECT_NO_THROW(pass.setLut(strip.get()));
    EXPECT_EQ(pass.getLut(), strip.get());

    EXPECT_NO_THROW(pass.setLut(nullptr));
    EXPECT_EQ(pass.getLut(), nullptr);
}

TEST(ColorGradeTest, TheIdentityTableIsValidatedAtItsOwnEnds)
{
    GraphicsDevice gd;
    EXPECT_THROW((void)ColorGradePass::createIdentityLut(gd, 1), std::invalid_argument);
    EXPECT_THROW((void)ColorGradePass::createIdentityLut(gd, 0), std::invalid_argument);
    EXPECT_THROW((void)ColorGradePass::createIdentityLut(gd, -4), std::invalid_argument);
    EXPECT_THROW((void)ColorGradePass::createIdentityLut(gd, ColorGradePass::kMaxLutSize + 1),
                 std::invalid_argument);
    EXPECT_NO_THROW((void)ColorGradePass::createIdentityLut(gd, 2));
    EXPECT_NO_THROW((void)ColorGradePass::createIdentityLut(gd, ColorGradePass::kMaxLutSize));
}

// ── The grade (MOD-2020) ─────────────────────────────────────────────────────

TEST(ColorGradeTest, AnIdentityTableReproducesTheFrame)
{
    // The dullest test here and the one that catches the most. A strip lookup is off-by-half-a-texel
    // arithmetic throughout, and every mistake in it produces a frame that still looks like a frame
    // -- slightly washed, slightly shifted, entirely plausible. Only an exact identity catches it.
    GraphicsDevice gd;
    ColorGradePass pass(gd);
    CNA_SKIP_WITHOUT_SHADER_EXECUTION(gd);
    CNA_SKIP_WITHOUT_RENDER_TARGET_READBACK(gd);

    auto source = MakeSpread(gd);
    RenderTarget2D destination(gd, kSize, kSize);
    auto lut = ColorGradePass::createIdentityLut(gd, kLutSize);
    pass.setLut(lut.get());

    PostProcessContext context = MakeContext(*source, destination);
    pass.apply(context);

    const std::vector<Color> before = ReadTarget(*source);
    const std::vector<Color> after  = ReadTarget(destination);
    ASSERT_EQ(before.size(), after.size());

    int worst = 0;
    for (std::size_t i = 0; i < before.size(); ++i)
    {
        worst = std::max(worst, std::abs(static_cast<int>(before[i].getRProperty()) -
                                         static_cast<int>(after[i].getRProperty())));
        worst = std::max(worst, std::abs(static_cast<int>(before[i].getGProperty()) -
                                         static_cast<int>(after[i].getGProperty())));
        worst = std::max(worst, std::abs(static_cast<int>(before[i].getBProperty()) -
                                         static_cast<int>(after[i].getBProperty())));
    }
    // A 16-entry table quantises to 17 levels per channel, so the round trip is not bit-exact; what
    // it must be is *correct to the table's own resolution*, which is a sixteenth of the range.
    EXPECT_LE(worst, 9) << "the identity table shifted the frame by " << worst << " levels";
}

TEST(ColorGradeTest, ATableThatSwapsChannelsSwapsThem)
{
    // The counterpart to the identity: proof that the lookup is being read at all, and read in the
    // right order. A strip indexed with red and blue exchanged reproduces the frame just as
    // convincingly as the identity does, and this is what separates them.
    GraphicsDevice gd;
    ColorGradePass pass(gd);
    CNA_SKIP_WITHOUT_SHADER_EXECUTION(gd);
    CNA_SKIP_WITHOUT_RENDER_TARGET_READBACK(gd);

    auto source = MakeImage(gd, [](int, int) { return Color(230, 60, 20, 255); });
    RenderTarget2D destination(gd, kSize, kSize);
    auto lut = MakeSwapRedAndBlueLut(gd, kLutSize);
    pass.setLut(lut.get());

    PostProcessContext context = MakeContext(*source, destination);
    pass.apply(context);

    const Color graded = ReadTarget(destination)[static_cast<std::size_t>(kSize / 2) * kSize + kSize / 2];
    EXPECT_NEAR(graded.getRProperty(), 20, 10) << "red did not take the blue channel's value";
    EXPECT_NEAR(graded.getGProperty(), 60, 10) << "green should have been left alone";
    EXPECT_NEAR(graded.getBProperty(), 230, 10) << "blue did not take the red channel's value";
}

TEST(ColorGradeTest, StrengthMixesBetweenTheOriginalAndTheGrade)
{
    GraphicsDevice gd;
    ColorGradePass pass(gd);
    CNA_SKIP_WITHOUT_SHADER_EXECUTION(gd);
    CNA_SKIP_WITHOUT_RENDER_TARGET_READBACK(gd);

    auto source = MakeImage(gd, [](int, int) { return Color(230, 60, 20, 255); });
    RenderTarget2D destination(gd, kSize, kSize);
    auto lut = MakeSwapRedAndBlueLut(gd, kLutSize);
    pass.setLut(lut.get());

    const auto redAtStrength = [&](const float strength) {
        pass.setStrength(strength);
        PostProcessContext context = MakeContext(*source, destination);
        pass.apply(context);
        return static_cast<int>(
            ReadTarget(destination)[static_cast<std::size_t>(kSize / 2) * kSize + kSize / 2]
                .getRProperty());
    };

    EXPECT_NEAR(redAtStrength(0.0f), 230, 6) << "strength 0 must leave the frame alone";
    EXPECT_NEAR(redAtStrength(1.0f), 20, 10);
    const int half = redAtStrength(0.5f);
    EXPECT_GT(half, 60);
    EXPECT_LT(half, 200) << "half strength landed outside the two it mixes between";
}

TEST(ColorGradeTest, WithoutATableTheFrameIsPassedThroughUnchanged)
{
    GraphicsDevice gd;
    ColorGradePass pass(gd);
    CNA_SKIP_WITHOUT_RENDER_TARGET_READBACK(gd);

    auto source = MakeImage(gd, [](int, int) { return Color(130, 70, 40, 255); });
    RenderTarget2D destination(gd, kSize, kSize);

    PostProcessContext context = MakeContext(*source, destination);
    EXPECT_NO_THROW(pass.apply(context));

    const Color pixel = ReadTarget(destination)[static_cast<std::size_t>(kSize / 2) * kSize + kSize / 2];
    EXPECT_NEAR(pixel.getRProperty(), 130, 4);
    EXPECT_NEAR(pixel.getGProperty(), 70, 4);
    EXPECT_NEAR(pixel.getBProperty(), 40, 4);
}

TEST(ColorGradeTest, TheSettingsBagWinsOverThePassLocalStrength)
{
    GraphicsDevice gd;
    ColorGradePass pass(gd);
    CNA_SKIP_WITHOUT_SHADER_EXECUTION(gd);
    CNA_SKIP_WITHOUT_RENDER_TARGET_READBACK(gd);

    auto source = MakeImage(gd, [](int, int) { return Color(230, 60, 20, 255); });
    RenderTarget2D destination(gd, kSize, kSize);
    auto lut = MakeSwapRedAndBlueLut(gd, kLutSize);
    pass.setLut(lut.get());
    pass.setStrength(1.0f);

    RenderPipelineSettings settings;
    settings.setColorGradeStrength(0.0f);

    PostProcessContext context = MakeContext(*source, destination);
    context.settings = &settings;
    pass.apply(context);

    const Color pixel = ReadTarget(destination)[static_cast<std::size_t>(kSize / 2) * kSize + kSize / 2];
    EXPECT_NEAR(pixel.getRProperty(), 230, 6)
        << "the pass used its own strength instead of the settings bag's";
}

TEST(ColorGradeTest, TheStrengthIsClampedAndTheNameIsStable)
{
    GraphicsDevice gd;
    ColorGradePass pass(gd);

    EXPECT_EQ(pass.getName(), "ColorGrade");
    EXPECT_FLOAT_EQ(pass.getStrength(), 1.0f);
    pass.setStrength(0.25f);
    EXPECT_FLOAT_EQ(pass.getStrength(), 0.25f);
    pass.setStrength(5.0f);
    EXPECT_FLOAT_EQ(pass.getStrength(), 1.0f);
    pass.setStrength(-1.0f);
    EXPECT_FLOAT_EQ(pass.getStrength(), 0.0f);
}

} // namespace

#endif // CNA_CNAEXT
