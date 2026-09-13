// SPDX-License-Identifier: MS-PL
// CRTEffect is a CNAEXT CNA extension (no XNA/FNA precedent). The structural contract lives beside
// pixel oracles for every independent shader parameter so a renderer cannot merely accept the
// effect or collapse its named uniforms onto one native slot and still pass.

#ifdef CNA_CNAEXT

#include <gtest/gtest.h>

#include <memory>
#include <vector>

#include "CNA/Graphics/CRTEffect.hpp"
#include "CNA/Graphics/EffectPass.hpp"
#include "CNA/Graphics/PostProcessContext.hpp"
#include "CNA/GraphicsCapability.hpp"
#include "CNA/ShaderLanguageEXT.hpp"
#include "EngineTestSupport.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Graphics/Effect.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"

using CNA::Graphics::CRTEffect;
using CNA::Graphics::CRTMaskType;
using CNA::Graphics::EffectPass;
using CNA::Graphics::PostProcessContext;
using Microsoft::Xna::Framework::Color;
using Microsoft::Xna::Framework::Graphics::Effect;
using Microsoft::Xna::Framework::Graphics::GraphicsDevice;
using Microsoft::Xna::Framework::Graphics::RenderTarget2D;

namespace {

[[nodiscard]] std::vector<Color> RenderWhite(
    GraphicsDevice& gd, CRTEffect& effect, const int width, const int height)
{
    RenderTarget2D source(gd, width, height);
    RenderTarget2D destination(gd, width, height);
    gd.SetRenderTarget(&source);
    gd.Clear(Color::White);
    gd.SetRenderTarget(nullptr);

    EffectPass pass(gd, &effect, "CRT");
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

[[nodiscard]] std::size_t At(const int x, const int y, const int width)
{
    return static_cast<std::size_t>(y * width + x);
}

} // namespace

#define CNA_REQUIRE_PORTABLE_CRT(device, effect)                                            \
    do                                                                                       \
    {                                                                                        \
        if (!(device).SupportsCapability(CNA::GraphicsCapability::CustomEffects))            \
            GTEST_SKIP() << "this renderer accepts no custom effects";                       \
        ASSERT_TRUE((effect).IsEffectValid()) << (effect).GetCompileErrorEXT();               \
        CNA_SKIP_WITHOUT_RENDER_TARGET_READBACK(device);                                     \
    } while (false)

TEST(CRTEffectTest, DefaultParametersAreModerate)
{
    GraphicsDevice gd;
    CRTEffect fx(gd);

    EXPECT_FLOAT_EQ(fx.getScanlineIntensity(), 0.3f);
    EXPECT_FLOAT_EQ(fx.getCurvature(), 0.08f);
    EXPECT_FLOAT_EQ(fx.getVignetteIntensity(), 0.25f);
    EXPECT_FLOAT_EQ(fx.getMaskIntensity(), 0.35f);
    EXPECT_EQ(fx.getMaskType(), CRTMaskType::ApertureGrille);
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

TEST(CRTEffectTest, SetScanlineIntensityRoundTripsAndClamps)
{
    GraphicsDevice gd;
    CRTEffect fx(gd);

    fx.setScanlineIntensity(0.6f);
    EXPECT_FLOAT_EQ(fx.getScanlineIntensity(), 0.6f);

    fx.setScanlineIntensity(-1.0f);
    EXPECT_FLOAT_EQ(fx.getScanlineIntensity(), 0.0f);

    fx.setScanlineIntensity(2.0f);
    EXPECT_FLOAT_EQ(fx.getScanlineIntensity(), 1.0f);
}

TEST(CRTEffectTest, SetCurvatureRoundTripsAndClamps)
{
    GraphicsDevice gd;
    CRTEffect fx(gd);

    fx.setCurvature(0.5f);
    EXPECT_FLOAT_EQ(fx.getCurvature(), 0.5f);

    fx.setCurvature(-0.2f);
    EXPECT_FLOAT_EQ(fx.getCurvature(), 0.0f);

    fx.setCurvature(1.5f);
    EXPECT_FLOAT_EQ(fx.getCurvature(), 1.0f);
}

TEST(CRTEffectTest, SetVignetteIntensityRoundTripsAndClamps)
{
    GraphicsDevice gd;
    CRTEffect fx(gd);

    fx.setVignetteIntensity(0.7f);
    EXPECT_FLOAT_EQ(fx.getVignetteIntensity(), 0.7f);

    fx.setVignetteIntensity(-3.0f);
    EXPECT_FLOAT_EQ(fx.getVignetteIntensity(), 0.0f);

    fx.setVignetteIntensity(3.0f);
    EXPECT_FLOAT_EQ(fx.getVignetteIntensity(), 1.0f);
}

TEST(CRTEffectTest, SetMaskIntensityRoundTripsAndClamps)
{
    GraphicsDevice gd;
    CRTEffect fx(gd);

    fx.setMaskIntensity(0.9f);
    EXPECT_FLOAT_EQ(fx.getMaskIntensity(), 0.9f);

    fx.setMaskIntensity(-0.5f);
    EXPECT_FLOAT_EQ(fx.getMaskIntensity(), 0.0f);

    fx.setMaskIntensity(4.0f);
    EXPECT_FLOAT_EQ(fx.getMaskIntensity(), 1.0f);
}

TEST(CRTEffectTest, SetMaskTypeRoundTripsForEveryType)
{
    GraphicsDevice gd;
    CRTEffect fx(gd);

    const CRTMaskType maskTypes[] = {
        CRTMaskType::None,
        CRTMaskType::ApertureGrille,
        CRTMaskType::ShadowMask,
    };

    for (const auto maskType : maskTypes)
    {
        fx.setMaskType(maskType);
        EXPECT_EQ(fx.getMaskType(), maskType);
    }
}

TEST(CRTEffectTest, ApplyDoesNotCrashWithoutARenderer)
{
    GraphicsDevice gd;
    CRTEffect fx(gd);

    fx.setMaskType(CRTMaskType::ShadowMask);
    EXPECT_NO_THROW(fx.Apply());
}

TEST(CRTEffectTest, GetTypeNameReturnsCnaGraphicsCRTEffect)
{
    GraphicsDevice gd;
    CRTEffect fx(gd);

    EXPECT_EQ(fx.GetTypeName(), "CNA.Graphics.CRTEffect");
}

TEST(CRTEffectTest, CloneReturnsIndependentCRTEffectWithSameParameters)
{
    GraphicsDevice gd;
    CRTEffect fx(gd);
    fx.setScanlineIntensity(0.5f);
    fx.setCurvature(0.2f);
    fx.setVignetteIntensity(0.6f);
    fx.setMaskIntensity(0.7f);
    fx.setMaskType(CRTMaskType::ShadowMask);

    std::unique_ptr<Effect> cloned(fx.Clone());
    auto* clone = dynamic_cast<CRTEffect*>(cloned.get());

    ASSERT_NE(clone, nullptr);
    EXPECT_NE(static_cast<Effect*>(clone), static_cast<Effect*>(&fx));
    EXPECT_FLOAT_EQ(clone->getScanlineIntensity(), 0.5f);
    EXPECT_FLOAT_EQ(clone->getCurvature(), 0.2f);
    EXPECT_FLOAT_EQ(clone->getVignetteIntensity(), 0.6f);
    EXPECT_FLOAT_EQ(clone->getMaskIntensity(), 0.7f);
    EXPECT_EQ(clone->getMaskType(), CRTMaskType::ShadowMask);

    // Mutating the clone must not affect the original.
    clone->setScanlineIntensity(0.9f);
    clone->setMaskType(CRTMaskType::None);
    EXPECT_FLOAT_EQ(fx.getScanlineIntensity(), 0.5f);
    EXPECT_EQ(fx.getMaskType(), CRTMaskType::ShadowMask);
}

TEST(CRTEffectTest, DisabledParametersCopyTheSourceExactly)
{
    GraphicsDevice gd;
    CRTEffect fx(gd);
    CNA_REQUIRE_PORTABLE_CRT(gd, fx);
    fx.setScanlineIntensity(0.0f);
    fx.setCurvature(0.0f);
    fx.setVignetteIntensity(0.0f);
    fx.setMaskIntensity(0.0f);
    fx.setMaskType(CRTMaskType::None);

    const std::vector<Color> pixels = RenderWhite(gd, fx, 9, 7);
    for (std::size_t index = 0; index < pixels.size(); ++index)
        EXPECT_EQ(pixels[index].getPackedValueProperty(), Color::White.getPackedValueProperty())
            << "disabled CRT changed pixel " << index;
}

TEST(CRTEffectTest, ScanlinesAlternateInEasyGlScreenOrder)
{
    GraphicsDevice gd;
    CRTEffect fx(gd);
    CNA_REQUIRE_PORTABLE_CRT(gd, fx);
    fx.setScanlineIntensity(0.5f);
    fx.setCurvature(0.0f);
    fx.setVignetteIntensity(0.0f);
    fx.setMaskIntensity(0.0f);
    fx.setMaskType(CRTMaskType::None);

    constexpr int width = 9;
    const std::vector<Color> pixels = RenderWhite(gd, fx, width, 7);
    EXPECT_GE(pixels[At(4, 0, width)].getRProperty(), 250);
    EXPECT_NEAR(pixels[At(4, 1, width)].getRProperty(), 128, 2);
    EXPECT_GE(pixels[At(4, 2, width)].getRProperty(), 250);
}

TEST(CRTEffectTest, ApertureGrilleSelectsRedGreenBlueColumns)
{
    GraphicsDevice gd;
    CRTEffect fx(gd);
    CNA_REQUIRE_PORTABLE_CRT(gd, fx);
    fx.setScanlineIntensity(0.0f);
    fx.setCurvature(0.0f);
    fx.setVignetteIntensity(0.0f);
    fx.setMaskIntensity(1.0f);
    fx.setMaskType(CRTMaskType::ApertureGrille);

    constexpr int width = 6;
    const std::vector<Color> pixels = RenderWhite(gd, fx, width, 7);
    const Color red = pixels[At(0, 0, width)];
    const Color green = pixels[At(1, 0, width)];
    const Color blue = pixels[At(2, 0, width)];
    EXPECT_GE(red.getRProperty(), 250);
    EXPECT_LE(red.getGProperty(), 2);
    EXPECT_GE(green.getGProperty(), 250);
    EXPECT_LE(green.getBProperty(), 2);
    EXPECT_GE(blue.getBProperty(), 250);
    EXPECT_LE(blue.getRProperty(), 2);
}

TEST(CRTEffectTest, ShadowMaskStaggersItsTriadByPhysicalRowGroup)
{
    GraphicsDevice gd;
    CRTEffect fx(gd);
    CNA_REQUIRE_PORTABLE_CRT(gd, fx);
    fx.setScanlineIntensity(0.0f);
    fx.setCurvature(0.0f);
    fx.setVignetteIntensity(0.0f);
    fx.setMaskIntensity(1.0f);
    fx.setMaskType(CRTMaskType::ShadowMask);

    constexpr int width = 6;
    const std::vector<Color> pixels = RenderWhite(gd, fx, width, 7);
    const Color topLeft = pixels[At(0, 0, width)];
    EXPECT_LE(topLeft.getRProperty(), 2);
    EXPECT_GE(topLeft.getGProperty(), 250);
    EXPECT_LE(topLeft.getBProperty(), 2);
}

TEST(CRTEffectTest, CurvatureClipsCornersButKeepsTheCentre)
{
    GraphicsDevice gd;
    CRTEffect fx(gd);
    CNA_REQUIRE_PORTABLE_CRT(gd, fx);
    fx.setScanlineIntensity(0.0f);
    fx.setCurvature(1.0f);
    fx.setVignetteIntensity(0.0f);
    fx.setMaskIntensity(0.0f);
    fx.setMaskType(CRTMaskType::None);

    constexpr int size = 32;
    const std::vector<Color> pixels = RenderWhite(gd, fx, size, size);
    EXPECT_LE(pixels[At(0, 0, size)].getRProperty(), 2);
    EXPECT_GE(pixels[At(size / 2, size / 2, size)].getRProperty(), 250);
}

TEST(CRTEffectTest, VignetteDarkensCornersMoreThanTheCentre)
{
    GraphicsDevice gd;
    CRTEffect fx(gd);
    CNA_REQUIRE_PORTABLE_CRT(gd, fx);
    fx.setScanlineIntensity(0.0f);
    fx.setCurvature(0.0f);
    fx.setVignetteIntensity(1.0f);
    fx.setMaskIntensity(0.0f);
    fx.setMaskType(CRTMaskType::None);

    constexpr int size = 32;
    const std::vector<Color> pixels = RenderWhite(gd, fx, size, size);
    const int corner = pixels[At(0, 0, size)].getRProperty();
    const int centre = pixels[At(size / 2, size / 2, size)].getRProperty();
    EXPECT_LT(corner, 32);
    EXPECT_GT(centre, 245);
    EXPECT_LT(corner, centre);
}

#endif // CNA_CNAEXT
