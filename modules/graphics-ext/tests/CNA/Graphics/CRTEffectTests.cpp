// SPDX-License-Identifier: MS-PL
// CRTEffect is a NOXNA CNA extension (no XNA/FNA precedent) — like ShaderEffectTests.cpp and
// DepthEffectTests.cpp, these tests exercise the structural contract (parameter round-trip,
// clamping, Clone() independence, GetTypeName()) against a default-constructed GraphicsDevice
// with no real renderer, not GLSL compile/render correctness.

#ifdef CNA_NOXNA

#include <gtest/gtest.h>

#include <memory>

#include "CNA/Graphics/CRTEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/Effect.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"

using CNA::Graphics::CRTEffect;
using CNA::Graphics::CRTMaskType;
using Microsoft::Xna::Framework::Graphics::Effect;
using Microsoft::Xna::Framework::Graphics::GraphicsDevice;

TEST(CRTEffectTest, DefaultParametersAreModerate)
{
    GraphicsDevice gd;
    CRTEffect fx(gd);

    EXPECT_FLOAT_EQ(fx.getScanlineIntensity(), 0.3f);
    EXPECT_FLOAT_EQ(fx.getCurvature(), 0.08f);
    EXPECT_FLOAT_EQ(fx.getVignetteIntensity(), 0.25f);
    EXPECT_FLOAT_EQ(fx.getMaskIntensity(), 0.35f);
    EXPECT_EQ(fx.getMaskType(), CRTMaskType::ApertureGrille);
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

#endif // CNA_NOXNA
