// SPDX-License-Identifier: MS-PL
// plan_modern.md MOD-300..MOD-320: the tonemapper.
//
// The assertion that carries the weight is the last group: an HDR value is rendered through the
// real shader and the result is compared against the CPU implementation of the same curve. A test
// that only checked the CPU maths would prove the specification agrees with itself; this checks
// that the shader agrees with the specification, which is where the operators actually differ
// (a missed exposure multiply, gamma applied twice, a curve normalized against the wrong white).

#ifdef CNA_CNAEXT

#include <gtest/gtest.h>

#include "CNA/Graphics/PostProcessContext.hpp"
#include "CNA/Graphics/RenderPipelineSettings.hpp"
#include "CNA/Graphics/TonemapPass.hpp"
#include "CNA/Graphics/TonemappingMode.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"

#include <cmath>
#include <vector>

namespace {

using CNA::Graphics::PostProcessContext;
using CNA::Graphics::RenderPipelineSettings;
using CNA::Graphics::TonemapPass;
using CNA::Graphics::TonemappingMode;
using Microsoft::Xna::Framework::Color;
using Microsoft::Xna::Framework::Graphics::DepthFormat;
using Microsoft::Xna::Framework::Graphics::GraphicsDevice;
using Microsoft::Xna::Framework::Graphics::RenderTarget2D;
using Microsoft::Xna::Framework::Graphics::SurfaceFormat;

constexpr int kSize = 4;

/// Renders one HDR colour through the pass and returns the resulting display-encoded texel.
Color RenderThroughPass(GraphicsDevice& gd, TonemapPass& pass, const RenderPipelineSettings& settings,
                        const float r, const float g, const float b)
{
    RenderTarget2D source(gd, kSize, kSize, false, SurfaceFormat::Vector4, DepthFormat::None);
    RenderTarget2D destination(gd, kSize, kSize);

    gd.SetRenderTarget(&source);
    gd.Clear(r, g, b, 1.0f);
    gd.SetRenderTarget(nullptr);

    PostProcessContext context;
    context.source      = &source;
    context.destination = &destination;
    context.width       = kSize;
    context.height      = kSize;
    context.settings    = &settings;
    pass.apply(context);

    std::vector<Color> pixels(static_cast<std::size_t>(kSize) * kSize, Color(0, 0, 0, 0));
    destination.GetData(pixels.data(), static_cast<int>(pixels.size()));
    return pixels.front();
}

// ── The curves themselves ────────────────────────────────────────────────────

TEST(TonemapPassTest, NoneIsAClampAndNothingElse)
{
    // Gamma still applies: None means "no curve", not "no display encode".
    EXPECT_FLOAT_EQ(TonemapPass::tonemapChannel(TonemappingMode::None, 0.5f, 1.0f, 1.0f), 0.5f);
    EXPECT_FLOAT_EQ(TonemapPass::tonemapChannel(TonemappingMode::None, 4.0f, 1.0f, 1.0f), 1.0f);
    EXPECT_FLOAT_EQ(TonemapPass::tonemapChannel(TonemappingMode::None, -1.0f, 1.0f, 1.0f), 0.0f);
}

TEST(TonemapPassTest, ReinhardMatchesItsFormula)
{
    for (const float value : {0.0f, 0.25f, 1.0f, 4.0f, 20.0f})
    {
        EXPECT_NEAR(TonemapPass::tonemapChannel(TonemappingMode::Reinhard, value, 1.0f, 1.0f),
                    value / (1.0f + value), 1e-6f)
            << "value " << value;
    }
}

TEST(TonemapPassTest, EveryCurveIsMonotonicAndBounded)
{
    // The property that makes an operator usable at all: brighter input never produces a darker
    // pixel, and nothing escapes [0,1]. A sign slip or a bad white point breaks one of the two.
    for (const TonemappingMode mode : {TonemappingMode::None, TonemappingMode::Reinhard,
                                       TonemappingMode::Filmic, TonemappingMode::Aces,
                                       TonemappingMode::Uncharted2})
    {
        float previous = -1.0f;
        for (float value = 0.0f; value <= 24.0f; value += 0.125f)
        {
            const float mapped = TonemapPass::tonemapChannel(mode, value, 1.0f, 2.2f);
            EXPECT_GE(mapped, 0.0f) << "mode " << static_cast<int>(mode);
            EXPECT_LE(mapped, 1.0f) << "mode " << static_cast<int>(mode);
            EXPECT_GE(mapped, previous - 1e-5f)
                << "mode " << static_cast<int>(mode) << " went backwards at " << value;
            previous = mapped;
        }
    }
}

TEST(TonemapPassTest, TheFilmicCurveSkipsGammaAndTheOthersDoNot)
{
    // Filmic's published curve has the display encode baked in. Applying gamma to it as well was
    // the bug this asymmetry exists to prevent, and it is invisible except as a washed-out image.
    EXPECT_FLOAT_EQ(TonemapPass::tonemapChannel(TonemappingMode::Filmic, 1.0f, 1.0f, 2.2f),
                    TonemapPass::tonemapChannel(TonemappingMode::Filmic, 1.0f, 1.0f, 1.0f));

    EXPECT_NE(TonemapPass::tonemapChannel(TonemappingMode::Aces, 1.0f, 1.0f, 2.2f),
              TonemapPass::tonemapChannel(TonemappingMode::Aces, 1.0f, 1.0f, 1.0f));
}

TEST(TonemapPassTest, ExposureScalesBeforeTheCurve)
{
    // Doubling exposure must equal doubling the input, or exposure is being applied in the wrong
    // place -- which looks almost right for Reinhard and clearly wrong for the filmic curves.
    for (const TonemappingMode mode : {TonemappingMode::Reinhard, TonemappingMode::Aces,
                                       TonemappingMode::Uncharted2})
    {
        EXPECT_NEAR(TonemapPass::tonemapChannel(mode, 0.5f, 2.0f, 1.0f),
                    TonemapPass::tonemapChannel(mode, 1.0f, 1.0f, 1.0f), 1e-6f)
            << "mode " << static_cast<int>(mode);
    }
}

TEST(TonemapPassTest, GammaEncodesAfterTheCurve)
{
    const float linear  = TonemapPass::tonemapChannel(TonemappingMode::None, 0.25f, 1.0f, 1.0f);
    const float encoded = TonemapPass::tonemapChannel(TonemappingMode::None, 0.25f, 1.0f, 2.2f);

    EXPECT_FLOAT_EQ(linear, 0.25f);
    EXPECT_NEAR(encoded, std::pow(0.25f, 1.0f / 2.2f), 1e-6f);
}

TEST(TonemapPassTest, TheOperatorsDisagreeWithEachOther)
{
    // Four distinct curves, not one curve behind four names -- a real risk when they are added by
    // copy-and-edit, and one no individual formula test would catch.
    const float value = 3.0f;
    const float none       = TonemapPass::tonemapChannel(TonemappingMode::None, value, 1.0f, 1.0f);
    const float reinhard   = TonemapPass::tonemapChannel(TonemappingMode::Reinhard, value, 1.0f, 1.0f);
    const float filmic     = TonemapPass::tonemapChannel(TonemappingMode::Filmic, value, 1.0f, 1.0f);
    const float aces       = TonemapPass::tonemapChannel(TonemappingMode::Aces, value, 1.0f, 1.0f);
    const float uncharted2 = TonemapPass::tonemapChannel(TonemappingMode::Uncharted2, value, 1.0f, 1.0f);

    EXPECT_NE(none, reinhard);
    EXPECT_NE(reinhard, aces);
    EXPECT_NE(aces, uncharted2);
    EXPECT_NE(filmic, aces);
}

// ── The shader against the specification ─────────────────────────────────────

TEST(TonemapPassTest, TheShaderMatchesTheCpuReferenceForEveryOperator)
{
    GraphicsDevice gd;
    TonemapPass pass(gd);
    if (!pass.isSupported(gd))
        GTEST_SKIP() << "this renderer cannot run custom effects";
    if (!gd.SupportsSurfaceFormatAsRenderTargetEXT(SurfaceFormat::Vector4))
        GTEST_SKIP() << "this renderer/driver has no RGBA32F render targets, so there is no HDR input";

    RenderPipelineSettings settings;
    settings.setExposure(1.0f);
    settings.setGamma(2.2f);

    constexpr float kHdrValue = 3.0f;
    for (const TonemappingMode mode : {TonemappingMode::None, TonemappingMode::Reinhard,
                                       TonemappingMode::Filmic, TonemappingMode::Aces,
                                       TonemappingMode::Uncharted2})
    {
        settings.setTonemappingMode(mode);
        const Color rendered = RenderThroughPass(gd, pass, settings, kHdrValue, kHdrValue * 0.5f,
                                                 kHdrValue * 0.25f);

        const float expectedR = TonemapPass::tonemapChannel(mode, kHdrValue, 1.0f, 2.2f);
        const float expectedG = TonemapPass::tonemapChannel(mode, kHdrValue * 0.5f, 1.0f, 2.2f);
        const float expectedB = TonemapPass::tonemapChannel(mode, kHdrValue * 0.25f, 1.0f, 2.2f);

        // One 8-bit step of tolerance: the comparison is against an 8-bit destination, so anything
        // tighter would be asserting the rounding rather than the curve.
        EXPECT_NEAR(rendered.getRProperty() / 255.0f, expectedR, 1.5f / 255.0f)
            << "red, mode " << static_cast<int>(mode);
        EXPECT_NEAR(rendered.getGProperty() / 255.0f, expectedG, 1.5f / 255.0f)
            << "green, mode " << static_cast<int>(mode);
        EXPECT_NEAR(rendered.getBProperty() / 255.0f, expectedB, 1.5f / 255.0f)
            << "blue, mode " << static_cast<int>(mode);
    }
}

TEST(TonemapPassTest, TheShaderHonoursExposureFromSettings)
{
    GraphicsDevice gd;
    TonemapPass pass(gd);
    if (!pass.isSupported(gd))
        GTEST_SKIP() << "this renderer cannot run custom effects";
    if (!gd.SupportsSurfaceFormatAsRenderTargetEXT(SurfaceFormat::Vector4))
        GTEST_SKIP() << "this renderer/driver has no RGBA32F render targets";

    RenderPipelineSettings settings;
    settings.setTonemappingMode(TonemappingMode::Reinhard);
    settings.setGamma(1.0f);

    settings.setExposure(0.5f);
    const Color dim = RenderThroughPass(gd, pass, settings, 1.0f, 1.0f, 1.0f);
    settings.setExposure(4.0f);
    const Color bright = RenderThroughPass(gd, pass, settings, 1.0f, 1.0f, 1.0f);

    EXPECT_LT(dim.getRProperty(), bright.getRProperty());
    EXPECT_NEAR(dim.getRProperty() / 255.0f,
                TonemapPass::tonemapChannel(TonemappingMode::Reinhard, 1.0f, 0.5f, 1.0f),
                1.5f / 255.0f);
}

TEST(TonemapPassTest, AnLdrSourceIsLegalAndModeNoneLeavesItAlone)
{
    // The HDR-off pipeline runs this pass over a Color target; it must be a faithful copy there,
    // or turning HDR off would change the image.
    GraphicsDevice gd;
    TonemapPass pass(gd);
    if (!pass.isSupported(gd))
        GTEST_SKIP() << "this renderer cannot run custom effects";

    RenderPipelineSettings settings;
    settings.setTonemappingMode(TonemappingMode::None);
    settings.setGamma(1.0f);

    RenderTarget2D source(gd, kSize, kSize);
    RenderTarget2D destination(gd, kSize, kSize);
    gd.SetRenderTarget(&source);
    gd.Clear(Color(40, 80, 120, 255));
    gd.SetRenderTarget(nullptr);

    PostProcessContext context;
    context.source      = &source;
    context.destination = &destination;
    context.width       = kSize;
    context.height      = kSize;
    context.settings    = &settings;
    pass.apply(context);

    std::vector<Color> pixels(static_cast<std::size_t>(kSize) * kSize, Color(0, 0, 0, 0));
    destination.GetData(pixels.data(), static_cast<int>(pixels.size()));
    EXPECT_NEAR(pixels.front().getRProperty(), 40, 1);
    EXPECT_NEAR(pixels.front().getGProperty(), 80, 1);
    EXPECT_NEAR(pixels.front().getBProperty(), 120, 1);
}

TEST(TonemapPassTest, ThePassIsUsableWithoutSettings)
{
    // D9: a pass works standalone. Without a settings bag it uses its own values.
    GraphicsDevice gd;
    TonemapPass pass(gd);

    pass.setMode(TonemappingMode::Aces);
    pass.setExposure(2.0f);
    pass.setGamma(1.8f);

    EXPECT_EQ(pass.getMode(), TonemappingMode::Aces);
    EXPECT_FLOAT_EQ(pass.getExposure(), 2.0f);
    EXPECT_FLOAT_EQ(pass.getGamma(), 1.8f);
    EXPECT_EQ(pass.getName(), "Tonemap");
}

} // namespace

#endif // CNA_CNAEXT
