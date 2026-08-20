// SPDX-License-Identifier: MS-PL
// plans/plan_modern.md MOD-2093: spatial upscaling.
//
// The promise that matters most is the negative one -- at a 1:1 scale the pass changes nothing --
// because it is what makes the resolution dial calibratable: a frame rendered at full size with the
// pass in the chain has to be the same frame as one rendered without it. That is asserted exactly,
// pixel for pixel, not within a tolerance.
//
// The positive promises are measured rather than looked at: the adaptive path is compared against
// the bilinear stretch it claims to beat, on the same edge, with the same source, and the sharpen
// is checked against the neighbourhood it is clamped to.

#ifdef CNA_CNAEXT

#include <gtest/gtest.h>

#include "CNA/Graphics/SpatialUpscalePass.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"

#include <cmath>
#include <memory>
#include <stdexcept>
#include <vector>

namespace {

using CNA::Graphics::SpatialUpscalePass;
using Microsoft::Xna::Framework::Color;
using Microsoft::Xna::Framework::Graphics::GraphicsDevice;
using Microsoft::Xna::Framework::Graphics::RenderTarget2D;
using Microsoft::Xna::Framework::Graphics::Texture2D;

constexpr int kSource = 16;
constexpr int kTarget = 32;

std::vector<Color> Read(RenderTarget2D& target, const int width, const int height)
{
    std::vector<Color> pixels(static_cast<std::size_t>(width) * height, Color(0, 0, 0, 0));
    target.GetData(pixels.data(), static_cast<int>(pixels.size()));
    return pixels;
}

/// A diagonal edge in a small image: the pattern a bilinear stretch turns into a staircase and an
/// edge-aware filter is supposed to keep as a line.
std::vector<Color> DiagonalTexels(const int size, const int low, const int high)
{
    std::vector<Color> texels(static_cast<std::size_t>(size) * size, Color(0, 0, 0, 255));
    for (int y = 0; y < size; ++y)
        for (int x = 0; x < size; ++x)
        {
            const int value = (x < y) ? high : low;
            texels[static_cast<std::size_t>(y) * size + x] = Color(value, value, value, 255);
        }
    return texels;
}

std::unique_ptr<Texture2D> MakeSource(GraphicsDevice& gd, const std::vector<Color>& texels,
                                      const int size)
{
    auto texture = std::make_unique<Texture2D>(gd, size, size);
    texture->SetData(texels.data(), static_cast<int>(texels.size()));
    return texture;
}

/// Where the edge crosses the mid tone on each row, to sub-pixel precision. A perfect diagonal puts
/// these on a straight line; a staircase puts them on a flight of steps, and the deviation from the
/// best-fit line is exactly how much staircase is left.
std::vector<float> EdgeCrossings(const std::vector<Color>& pixels, const int size)
{
    std::vector<float> crossings;
    for (int y = 1; y < size - 1; ++y)
    {
        for (int x = 1; x < size - 1; ++x)
        {
            const float here = static_cast<float>(pixels[static_cast<std::size_t>(y) * size + x]
                                                      .getRProperty());
            const float next = static_cast<float>(pixels[static_cast<std::size_t>(y) * size + x + 1]
                                                      .getRProperty());
            if ((here - 128.0f) * (next - 128.0f) <= 0.0f && here != next)
            {
                crossings.push_back(static_cast<float>(x) + (128.0f - here) / (next - here));
                break;
            }
        }
    }
    return crossings;
}

/// Root-mean-square deviation of the crossings from their own least-squares line.
float StaircaseResidual(const std::vector<float>& crossings)
{
    const auto count = static_cast<float>(crossings.size());
    if (count < 3.0f) return 0.0f;

    float sumT = 0.0f, sumV = 0.0f, sumTT = 0.0f, sumTV = 0.0f;
    for (std::size_t i = 0; i < crossings.size(); ++i)
    {
        const float t = static_cast<float>(i);
        sumT += t;
        sumV += crossings[i];
        sumTT += t * t;
        sumTV += t * crossings[i];
    }
    const float denominator = count * sumTT - sumT * sumT;
    const float slope = (denominator == 0.0f) ? 0.0f : (count * sumTV - sumT * sumV) / denominator;
    const float intercept = (sumV - slope * sumT) / count;

    float squared = 0.0f;
    for (std::size_t i = 0; i < crossings.size(); ++i)
    {
        const float residual = crossings[i] - (intercept + slope * static_cast<float>(i));
        squared += residual * residual;
    }
    return std::sqrt(squared / count);
}

std::vector<Color> Upscale(GraphicsDevice& gd, SpatialUpscalePass& pass,
                           const std::vector<Color>& texels, const int sourceSize,
                           const int targetSize)
{
    const std::unique_ptr<Texture2D> source = MakeSource(gd, texels, sourceSize);
    RenderTarget2D destination(gd, targetSize, targetSize);
    gd.SetRenderTarget(&destination);
    gd.Clear(Color::Black);
    pass.draw(source.get(), sourceSize, sourceSize, targetSize, targetSize);
    gd.SetRenderTarget(nullptr);
    return Read(destination, targetSize, targetSize);
}

TEST(SpatialUpscalePassTest, TheIdentityScaleIsRecognisedByName)
{
    EXPECT_TRUE(SpatialUpscalePass::isIdentityScale(1280, 720, 1280, 720));
    EXPECT_FALSE(SpatialUpscalePass::isIdentityScale(853, 480, 1280, 720));
    EXPECT_FALSE(SpatialUpscalePass::isIdentityScale(1280, 480, 1280, 720))
        << "one axis matching is not the identity";
    EXPECT_FALSE(SpatialUpscalePass::isIdentityScale(853, 720, 1280, 720));
}

TEST(SpatialUpscalePassTest, TheDefaultsAreAnAdaptiveUpsampleAndAModerateSharpen)
{
    GraphicsDevice gd;
    const SpatialUpscalePass pass(gd);
    EXPECT_TRUE(pass.isEdgeAdaptive());
    EXPECT_GT(pass.getSharpness(), 0.0f);
    EXPECT_LE(pass.getSharpness(), 1.0f);
}

TEST(SpatialUpscalePassTest, SharpnessIsClampedAndBothSettingsRoundTrip)
{
    GraphicsDevice gd;
    SpatialUpscalePass pass(gd);

    pass.setSharpness(0.25f);
    EXPECT_FLOAT_EQ(pass.getSharpness(), 0.25f);
    pass.setSharpness(-3.0f);
    EXPECT_FLOAT_EQ(pass.getSharpness(), 0.0f);
    pass.setSharpness(9.0f);
    EXPECT_FLOAT_EQ(pass.getSharpness(), 1.0f);

    pass.setEdgeAdaptive(false);
    EXPECT_FALSE(pass.isEdgeAdaptive());
    pass.setEdgeAdaptive(true);
    EXPECT_TRUE(pass.isEdgeAdaptive());
}

TEST(SpatialUpscalePassTest, DrawingRefusesTheArgumentsItCannotUse)
{
    GraphicsDevice gd;
    SpatialUpscalePass pass(gd);
    Texture2D source(gd, 4, 4);

    EXPECT_THROW(pass.draw(nullptr, 4, 4, 8, 8), std::invalid_argument);
    EXPECT_THROW(pass.draw(&source, 0, 4, 8, 8), std::invalid_argument);
    EXPECT_THROW(pass.draw(&source, 4, -1, 8, 8), std::invalid_argument);
    EXPECT_THROW(pass.draw(&source, 4, 4, 0, 8), std::invalid_argument);
    EXPECT_THROW(pass.draw(&source, 4, 4, 8, -8), std::invalid_argument);
}

TEST(SpatialUpscalePassTest, AOneToOneScaleCopiesThroughPixelForPixel)
{
    GraphicsDevice gd;
    SpatialUpscalePass pass(gd);
    if (!pass.isSupported())
        GTEST_SKIP() << "this renderer does not execute effect source";

    // Sharpening left at its default on purpose: the identity has to hold for a pass a game left
    // configured, not only for one that was turned off first.
    const std::vector<Color> texels = DiagonalTexels(kSource, 30, 220);
    const std::vector<Color> result = Upscale(gd, pass, texels, kSource, kSource);

    ASSERT_EQ(result.size(), texels.size());
    for (std::size_t i = 0; i < texels.size(); ++i)
    {
        EXPECT_EQ(result[i].getRProperty(), texels[i].getRProperty()) << "at texel " << i;
        EXPECT_EQ(result[i].getGProperty(), texels[i].getGProperty()) << "at texel " << i;
        EXPECT_EQ(result[i].getBProperty(), texels[i].getBProperty()) << "at texel " << i;
    }
}

TEST(SpatialUpscalePassTest, TheAdaptivePathStraightensAnEdgeTheBilinearOneLeavesStepped)
{
    GraphicsDevice gd;
    SpatialUpscalePass pass(gd);
    if (!pass.isSupported())
        GTEST_SKIP() << "this renderer does not execute effect source";

    // The sharpen is off for the comparison: it acts on both paths and would confound which of the
    // two produced the difference.
    const std::vector<Color> texels = DiagonalTexels(kSource, 20, 235);
    pass.setSharpness(0.0f);

    pass.setEdgeAdaptive(false);
    const float bilinear = StaircaseResidual(EdgeCrossings(
        Upscale(gd, pass, texels, kSource, kTarget), kTarget));

    pass.setEdgeAdaptive(true);
    const float adaptive = StaircaseResidual(EdgeCrossings(
        Upscale(gd, pass, texels, kSource, kTarget), kTarget));

    ASSERT_GT(bilinear, 0.0f) << "the control produced a perfect edge, so there was nothing to beat";
    EXPECT_LT(adaptive, bilinear)
        << "the edge-adaptive filter left as much staircase as the stretch it replaces";
}

TEST(SpatialUpscalePassTest, TheSharpenNeverLeavesTheToneRangeItSharpenedFrom)
{
    GraphicsDevice gd;
    SpatialUpscalePass pass(gd);
    if (!pass.isSupported())
        GTEST_SKIP() << "this renderer does not execute effect source";

    // The source contains only two tones, so an unclamped sharpener would ring past both of them at
    // the edge -- a bright halo on one side and a dark one on the other. The clamp is what makes
    // the whole output stay inside [low, high].
    constexpr int kLow = 60;
    constexpr int kHigh = 190;
    const std::vector<Color> texels = DiagonalTexels(kSource, kLow, kHigh);
    pass.setSharpness(1.0f);

    for (const Color& texel : Upscale(gd, pass, texels, kSource, kTarget))
    {
        EXPECT_GE(texel.getRProperty(), kLow - 1);
        EXPECT_LE(texel.getRProperty(), kHigh + 1);
    }
}

TEST(SpatialUpscalePassTest, SharpeningIsWhatChangesWhenSharpnessChanges)
{
    GraphicsDevice gd;
    SpatialUpscalePass pass(gd);
    if (!pass.isSupported())
        GTEST_SKIP() << "this renderer does not execute effect source";

    const std::vector<Color> texels = DiagonalTexels(kSource, 40, 210);

    pass.setSharpness(0.0f);
    const std::vector<Color> soft = Upscale(gd, pass, texels, kSource, kTarget);
    pass.setSharpness(1.0f);
    const std::vector<Color> crisp = Upscale(gd, pass, texels, kSource, kTarget);

    ASSERT_EQ(soft.size(), crisp.size());
    int differing = 0;
    for (std::size_t i = 0; i < soft.size(); ++i)
        if (soft[i].getRProperty() != crisp[i].getRProperty()) ++differing;

    EXPECT_GT(differing, 0) << "the sharpness uniform is not reaching the shader";
}

} // namespace

#endif // CNA_CNAEXT
