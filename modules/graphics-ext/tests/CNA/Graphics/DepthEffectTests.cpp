// SPDX-License-Identifier: MS-PL
// DepthEffect is a CNAEXT CNA extension (no XNA/FNA precedent). The structural contract lives
// beside exact pixel oracles for every quantizer, both palettes, and both Bayer matrices so the
// EasyGL and Vulkan package variants must agree on the complete effect rather than merely compile.

#ifdef CNA_CNAEXT

#include <gtest/gtest.h>

#include <array>
#include <cmath>
#include <memory>
#include <vector>

#include "CNA/Graphics/DepthEffect.hpp"
#include "CNA/Graphics/DitherMode.hpp"
#include "CNA/Graphics/EffectPass.hpp"
#include "CNA/Graphics/PostProcessContext.hpp"
#include "CNA/GraphicsCapability.hpp"
#include "CNA/ShaderLanguageEXT.hpp"
#include "EngineTestSupport.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Graphics/Effect.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"

using CNA::Graphics::DepthEffect;
using CNA::Graphics::DepthEffectMode;
using CNA::Graphics::DitherMode;
using CNA::Graphics::EffectPass;
using CNA::Graphics::PostProcessContext;
using Microsoft::Xna::Framework::Color;
using Microsoft::Xna::Framework::Graphics::Effect;
using Microsoft::Xna::Framework::Graphics::GraphicsDevice;
using Microsoft::Xna::Framework::Graphics::RenderTarget2D;

namespace {

[[nodiscard]] std::vector<Color> RenderColor(
    GraphicsDevice& gd, DepthEffect& effect, const Color& sourceColor,
    const int width = 1, const int height = 1)
{
    RenderTarget2D source(gd, width, height);
    RenderTarget2D destination(gd, width, height);
    gd.SetRenderTarget(&source);
    gd.Clear(sourceColor);
    gd.SetRenderTarget(nullptr);

    EffectPass pass(gd, &effect, "Depth");
    PostProcessContext context;
    context.source = &source;
    context.destination = &destination;
    context.width = width;
    context.height = height;
    pass.apply(context);

    std::vector<Color> pixels(static_cast<std::size_t>(width * height), Color::Black);
    destination.GetData(pixels.data(), static_cast<int>(pixels.size()));
    return pixels;
}

[[nodiscard]] int QuantizedByte(const int value, const int levels)
{
    const double normalized = static_cast<double>(value) / 255.0;
    return static_cast<int>(std::lround(
        std::round(normalized * static_cast<double>(levels - 1))
        / static_cast<double>(levels - 1) * 255.0));
}

void ExpectNearColor(const Color& actual, const Color& expected, const int tolerance = 1)
{
    EXPECT_NEAR(actual.getRProperty(), expected.getRProperty(), tolerance);
    EXPECT_NEAR(actual.getGProperty(), expected.getGProperty(), tolerance);
    EXPECT_NEAR(actual.getBProperty(), expected.getBProperty(), tolerance);
    EXPECT_NEAR(actual.getAProperty(), expected.getAProperty(), tolerance);
}

[[nodiscard]] std::size_t At(const int x, const int y, const int width)
{
    return static_cast<std::size_t>(y * width + x);
}

} // namespace

#define CNA_REQUIRE_PORTABLE_DEPTH(device, effect)                                          \
    do                                                                                       \
    {                                                                                        \
        if (!(device).SupportsCapability(CNA::GraphicsCapability::CustomEffects))            \
            GTEST_SKIP() << "this renderer accepts no custom effects";                       \
        ASSERT_TRUE((effect).IsEffectValid()) << (effect).GetCompileErrorEXT();               \
        CNA_SKIP_WITHOUT_RENDER_TARGET_READBACK(device);                                     \
    } while (false)

TEST(DepthEffectTest, DefaultModeIsColor16Bit)
{
    GraphicsDevice gd;
    DepthEffect fx(gd);

    EXPECT_EQ(fx.getMode(), DepthEffectMode::Color16Bit);
}

TEST(DepthEffectTest, SelectsTheBestShaderPackageVariant)
{
    GraphicsDevice gd;
    DepthEffect fx(gd);

    if (gd.SupportsShaderLanguageEXT(CNA::ShaderLanguageEXT::SpirV,
                                     CNA::ShaderStageEXT::Vertex)
        && gd.SupportsShaderLanguageEXT(CNA::ShaderLanguageEXT::SpirV,
                                        CNA::ShaderStageEXT::Fragment))
        EXPECT_EQ(fx.GetSelectedShaderLanguageEXT(), CNA::ShaderLanguageEXT::SpirV);
    else if (gd.SupportsShaderLanguageEXT(CNA::ShaderLanguageEXT::GlslDesktop,
                                          CNA::ShaderStageEXT::Vertex)
             && gd.SupportsShaderLanguageEXT(CNA::ShaderLanguageEXT::GlslDesktop,
                                             CNA::ShaderStageEXT::Fragment))
        EXPECT_EQ(fx.GetSelectedShaderLanguageEXT(), CNA::ShaderLanguageEXT::GlslDesktop);
    else if (gd.SupportsShaderLanguageEXT(CNA::ShaderLanguageEXT::GlslEs,
                                          CNA::ShaderStageEXT::Vertex)
             && gd.SupportsShaderLanguageEXT(CNA::ShaderLanguageEXT::GlslEs,
                                             CNA::ShaderStageEXT::Fragment))
        EXPECT_EQ(fx.GetSelectedShaderLanguageEXT(), CNA::ShaderLanguageEXT::GlslEs);
}

TEST(DepthEffectTest, DefaultDitherModeIsNone)
{
    GraphicsDevice gd;
    DepthEffect fx(gd);

    EXPECT_EQ(fx.getDitherMode(), DitherMode::None);
}

TEST(DepthEffectTest, SetDitherModeRoundTripsForEveryMode)
{
    GraphicsDevice gd;
    DepthEffect fx(gd);

    const DitherMode ditherModes[] = {
        DitherMode::None,
        DitherMode::Bayer4x4,
        DitherMode::Bayer8x8,
    };

    for (const auto ditherMode : ditherModes)
    {
        fx.setDitherMode(ditherMode);
        EXPECT_EQ(fx.getDitherMode(), ditherMode);
    }
}

TEST(DepthEffectTest, SetModeRoundTripsForEveryMode)
{
    GraphicsDevice gd;
    DepthEffect fx(gd);

    const DepthEffectMode modes[] = {
        DepthEffectMode::Color16Bit,
        DepthEffectMode::Color8Bit,
        DepthEffectMode::Grayscale4Bit,
        DepthEffectMode::Grayscale2Bit,
        DepthEffectMode::Grayscale1Bit,
        DepthEffectMode::Palette256,
        DepthEffectMode::Palette16,
    };

    for (const auto mode : modes)
    {
        fx.setMode(mode);
        EXPECT_EQ(fx.getMode(), mode);
    }
}

TEST(DepthEffectTest, ApplyDoesNotCrashWithoutARenderer)
{
    GraphicsDevice gd;
    DepthEffect fx(gd);

    fx.setMode(DepthEffectMode::Grayscale1Bit);
    fx.setDitherMode(DitherMode::Bayer8x8);
    EXPECT_NO_THROW(fx.Apply());
}

// Palette256/Palette16 lazily build their lookup textures inside OnApply() (via Apply()) --
// this must stay a no-op (not throw) when there is no working renderer, same as every other
// mode, rather than crashing on GraphicsDevice::GetRenderer()'s "no renderer" exception.
TEST(DepthEffectTest, ApplyDoesNotCrashForPaletteModesWithoutARenderer)
{
    GraphicsDevice gd;
    DepthEffect fx(gd);

    fx.setMode(DepthEffectMode::Palette256);
    EXPECT_NO_THROW(fx.Apply());

    fx.setMode(DepthEffectMode::Palette16);
    EXPECT_NO_THROW(fx.Apply());
}

TEST(DepthEffectTest, GetTypeNameReturnsCnaGraphicsDepthEffect)
{
    GraphicsDevice gd;
    DepthEffect fx(gd);

    EXPECT_EQ(fx.GetTypeName(), "CNA.Graphics.DepthEffect");
}

TEST(DepthEffectTest, CloneReturnsIndependentDepthEffectWithSameModeAndDitherMode)
{
    GraphicsDevice gd;
    DepthEffect fx(gd);
    fx.setMode(DepthEffectMode::Grayscale2Bit);
    fx.setDitherMode(DitherMode::Bayer4x4);

    std::unique_ptr<Effect> cloned(fx.Clone());
    auto* clone = dynamic_cast<DepthEffect*>(cloned.get());

    ASSERT_NE(clone, nullptr);
    EXPECT_NE(static_cast<Effect*>(clone), static_cast<Effect*>(&fx));
    EXPECT_EQ(clone->getMode(), DepthEffectMode::Grayscale2Bit);
    EXPECT_EQ(clone->getDitherMode(), DitherMode::Bayer4x4);

    // Mutating the clone must not affect the original.
    clone->setMode(DepthEffectMode::Color8Bit);
    clone->setDitherMode(DitherMode::Bayer8x8);
    EXPECT_EQ(fx.getMode(), DepthEffectMode::Grayscale2Bit);
    EXPECT_EQ(fx.getDitherMode(), DitherMode::Bayer4x4);
}

TEST(DepthEffectTest, CloneReturnsIndependentDepthEffectWithSamePaletteMode)
{
    GraphicsDevice gd;
    DepthEffect fx(gd);
    fx.setMode(DepthEffectMode::Palette16);

    std::unique_ptr<Effect> cloned(fx.Clone());
    auto* clone = dynamic_cast<DepthEffect*>(cloned.get());

    ASSERT_NE(clone, nullptr);
    EXPECT_EQ(clone->getMode(), DepthEffectMode::Palette16);
}

TEST(DepthEffectTest, RgbBitDepthModesUseTheirExactChannelLayoutsAndPreserveAlpha)
{
    GraphicsDevice gd;
    DepthEffect fx(gd);
    CNA_REQUIRE_PORTABLE_DEPTH(gd, fx);
    fx.setDitherMode(DitherMode::None);
    const Color source(123, 87, 201, 173);

    fx.setMode(DepthEffectMode::Color16Bit);
    const Color color16 = RenderColor(gd, fx, source).front();
    ExpectNearColor(
        color16,
        Color(QuantizedByte(123, 32), QuantizedByte(87, 64), QuantizedByte(201, 32), 173));

    fx.setMode(DepthEffectMode::Color8Bit);
    const Color color8 = RenderColor(gd, fx, source).front();
    ExpectNearColor(
        color8,
        Color(QuantizedByte(123, 8), QuantizedByte(87, 8), QuantizedByte(201, 4), 173));
}

TEST(DepthEffectTest, GrayscaleModesUseBt601LumaAndTheirExactLevelCounts)
{
    GraphicsDevice gd;
    DepthEffect fx(gd);
    CNA_REQUIRE_PORTABLE_DEPTH(gd, fx);
    fx.setDitherMode(DitherMode::None);
    const Color source(123, 87, 201, 255);
    const int lumaByte = static_cast<int>(std::lround(
        0.299 * 123.0 + 0.587 * 87.0 + 0.114 * 201.0));

    struct Case
    {
        DepthEffectMode mode;
        int levels;
    };
    constexpr std::array<Case, 3> cases = {{
        {DepthEffectMode::Grayscale4Bit, 16},
        {DepthEffectMode::Grayscale2Bit, 4},
        {DepthEffectMode::Grayscale1Bit, 2},
    }};

    for (const Case& item : cases)
    {
        fx.setMode(item.mode);
        const Color actual = RenderColor(gd, fx, source).front();
        const int gray = QuantizedByte(lumaByte, item.levels);
        ExpectNearColor(actual, Color(gray, gray, gray, 255));
    }
}

TEST(DepthEffectTest, PaletteModesSelectTheActualNearestWebSafeAndEgaEntries)
{
    GraphicsDevice gd;
    DepthEffect fx(gd);
    CNA_REQUIRE_PORTABLE_DEPTH(gd, fx);
    fx.setDitherMode(DitherMode::None);

    fx.setMode(DepthEffectMode::Palette256);
    ExpectNearColor(
        RenderColor(gd, fx, Color(123, 87, 201, 255)).front(),
        Color(102, 102, 204, 255), 0);

    // Reuse the effect after its 216-entry texture was bound. This makes the assertion cover both
    // the EGA table and the palette texture/size switch rather than only first-use construction.
    fx.setMode(DepthEffectMode::Palette16);
    ExpectNearColor(
        RenderColor(gd, fx, Color(160, 80, 10, 255)).front(),
        Color(170, 85, 0, 255), 0);
}

TEST(DepthEffectTest, Bayer4x4UsesTheEasyGlPhysicalRowOrientation)
{
    GraphicsDevice gd;
    DepthEffect fx(gd);
    CNA_REQUIRE_PORTABLE_DEPTH(gd, fx);
    fx.setMode(DepthEffectMode::Grayscale1Bit);
    fx.setDitherMode(DitherMode::Bayer4x4);

    constexpr int size = 4;
    constexpr std::array<int, 16> matrix = {
         0,  8,  2, 10,
        12,  4, 14,  6,
         3, 11,  1,  9,
        15,  7, 13,  5,
    };
    const std::vector<Color> pixels = RenderColor(gd, fx, Color(128, 128, 128, 255), size, size);
    for (int y = 0; y < size; ++y)
        for (int x = 0; x < size; ++x)
        {
            const int physicalY = size - 1 - y;
            const bool expectedWhite = matrix[At(x, physicalY, size)] >= 8;
            const Color actual = pixels[At(x, y, size)];
            EXPECT_EQ(actual.getRProperty() >= 254, expectedWhite) << "pixel " << x << ',' << y;
            EXPECT_EQ(actual.getGProperty() >= 254, expectedWhite) << "pixel " << x << ',' << y;
            EXPECT_EQ(actual.getBProperty() >= 254, expectedWhite) << "pixel " << x << ',' << y;
        }
}

TEST(DepthEffectTest, Bayer8x8UsesAllSixtyFourOrderedThresholds)
{
    GraphicsDevice gd;
    DepthEffect fx(gd);
    CNA_REQUIRE_PORTABLE_DEPTH(gd, fx);
    fx.setMode(DepthEffectMode::Grayscale1Bit);
    fx.setDitherMode(DitherMode::Bayer8x8);

    constexpr int size = 8;
    constexpr std::array<int, 64> matrix = {
         0, 32,  8, 40,  2, 34, 10, 42,
        48, 16, 56, 24, 50, 18, 58, 26,
        12, 44,  4, 36, 14, 46,  6, 38,
        60, 28, 52, 20, 62, 30, 54, 22,
         3, 35, 11, 43,  1, 33,  9, 41,
        51, 19, 59, 27, 49, 17, 57, 25,
        15, 47,  7, 39, 13, 45,  5, 37,
        63, 31, 55, 23, 61, 29, 53, 21,
    };
    const std::vector<Color> pixels = RenderColor(gd, fx, Color(128, 128, 128, 255), size, size);
    for (int y = 0; y < size; ++y)
        for (int x = 0; x < size; ++x)
        {
            const int physicalY = size - 1 - y;
            const bool expectedWhite = matrix[At(x, physicalY, size)] >= 32;
            const Color actual = pixels[At(x, y, size)];
            EXPECT_EQ(actual.getRProperty() >= 254, expectedWhite) << "pixel " << x << ',' << y;
            EXPECT_EQ(actual.getGProperty() >= 254, expectedWhite) << "pixel " << x << ',' << y;
            EXPECT_EQ(actual.getBProperty() >= 254, expectedWhite) << "pixel " << x << ',' << y;
        }
}

#endif // CNA_CNAEXT
