// SPDX-License-Identifier: MS-PL
// plan_cnj.md CNB-56/60 (Phase 13A): default-value and getter/setter coverage for PbrEffect, the
// new CNAEXT metallic-roughness PBR effect (no FNA/XNA equivalent to audit against -- real XNA
// predates the PBR content pipeline entirely). See PbrEffect.hpp's own doc comment for the design
// rationale (glTF 2.0's own reference BRDF, CNA's established 3-light + ambient convention).

#include <gtest/gtest.h>
#include <memory>
#include <stdexcept>

#include "CNA/Internal/Renderers/Common/IGraphicsRenderer.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/PbrEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/TextureTransformEXT.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"

using Microsoft::Xna::Framework::Matrix;
using Microsoft::Xna::Framework::Vector2;
using Microsoft::Xna::Framework::Vector3;
using Microsoft::Xna::Framework::Graphics::GraphicsDevice;
using Microsoft::Xna::Framework::Graphics::PbrEffect;
using Microsoft::Xna::Framework::Graphics::Texture2D;
using Microsoft::Xna::Framework::Graphics::TextureTransformEXT;
using CNA::Internal::Renderers::GpuDrawParams;

namespace
{
    class PbrEffectDefaultsTest : public ::testing::Test
    {
    protected:
        GraphicsDevice gd;
        PbrEffect fx{gd};
    };
}

// -----------------------------------------------------------------------
// Defaults

TEST_F(PbrEffectDefaultsTest, WorldDefaultsToIdentity)
{
    EXPECT_EQ(fx.getWorldProperty(), Matrix::getIdentityProperty());
}

TEST_F(PbrEffectDefaultsTest, ViewDefaultsToIdentity)
{
    EXPECT_EQ(fx.getViewProperty(), Matrix::getIdentityProperty());
}

TEST_F(PbrEffectDefaultsTest, ProjectionDefaultsToIdentity)
{
    EXPECT_EQ(fx.getProjectionProperty(), Matrix::getIdentityProperty());
}

TEST_F(PbrEffectDefaultsTest, DiffuseColorDefaultsToOne)
{
    EXPECT_EQ(fx.getDiffuseColorProperty(), Vector3(1.0f, 1.0f, 1.0f));
}

TEST_F(PbrEffectDefaultsTest, AlphaDefaultsToOne)
{
    EXPECT_FLOAT_EQ(fx.getAlphaProperty(), 1.0f);
}

TEST_F(PbrEffectDefaultsTest, AmbientLightColorDefaultsToZero)
{
    EXPECT_EQ(fx.getAmbientLightColorProperty(), Vector3::Zero);
}

TEST_F(PbrEffectDefaultsTest, LightingEnabledIsAlwaysTrue)
{
    EXPECT_TRUE(fx.getLightingEnabledProperty());
}

TEST_F(PbrEffectDefaultsTest, SetLightingEnabledFalseThrows)
{
    EXPECT_THROW(fx.setLightingEnabledProperty(false), std::runtime_error);
}

TEST_F(PbrEffectDefaultsTest, SetLightingEnabledTrueDoesNotThrow)
{
    EXPECT_NO_THROW(fx.setLightingEnabledProperty(true));
}

TEST_F(PbrEffectDefaultsTest, FogEnabledDefaultsToFalse)
{
    EXPECT_FALSE(fx.getFogEnabledProperty());
}

TEST_F(PbrEffectDefaultsTest, FogStartDefaultsToZero)
{
    EXPECT_FLOAT_EQ(fx.getFogStartProperty(), 0.0f);
}

TEST_F(PbrEffectDefaultsTest, FogEndDefaultsToOne)
{
    EXPECT_FLOAT_EQ(fx.getFogEndProperty(), 1.0f);
}

TEST_F(PbrEffectDefaultsTest, TextureDefaultsToNull)
{
    EXPECT_EQ(fx.getTextureProperty(), nullptr);
}

TEST_F(PbrEffectDefaultsTest, NormalMapDefaultsToNull)
{
    EXPECT_EQ(fx.getNormalMapProperty(), nullptr);
}

TEST_F(PbrEffectDefaultsTest, MetallicRoughnessMapDefaultsToNull)
{
    EXPECT_EQ(fx.getMetallicRoughnessMapProperty(), nullptr);
}

TEST_F(PbrEffectDefaultsTest, EmissiveMapDefaultsToNull)
{
    EXPECT_EQ(fx.getEmissiveMapProperty(), nullptr);
}

TEST_F(PbrEffectDefaultsTest, OcclusionMapDefaultsToNull)
{
    EXPECT_EQ(fx.getOcclusionMapProperty(), nullptr);
}

TEST_F(PbrEffectDefaultsTest, MetallicFactorDefaultsToOne)
{
    EXPECT_FLOAT_EQ(fx.getMetallicFactorProperty(), 1.0f);
}

TEST_F(PbrEffectDefaultsTest, RoughnessFactorDefaultsToOne)
{
    EXPECT_FLOAT_EQ(fx.getRoughnessFactorProperty(), 1.0f);
}

TEST_F(PbrEffectDefaultsTest, DielectricFresnelFactorsDefaultToCoreGltf)
{
    EXPECT_FLOAT_EQ(fx.getIorEXTProperty(), 1.5f);
    EXPECT_FLOAT_EQ(fx.getSpecularFactorEXTProperty(), 1.0f);
    EXPECT_EQ(fx.getSpecularColorFactorEXTProperty(), Vector3(1.0f, 1.0f, 1.0f));

    GpuDrawParams params;
    fx.FillGpuDrawParams(params);
    EXPECT_FLOAT_EQ(params.pbrDielectricF0[0], 0.04f);
    EXPECT_FLOAT_EQ(params.pbrDielectricF0[1], 0.04f);
    EXPECT_FLOAT_EQ(params.pbrDielectricF0[2], 0.04f);
    EXPECT_FLOAT_EQ(params.pbrDielectricF90, 1.0f);
}

TEST_F(PbrEffectDefaultsTest, SpecularTextureInputsHaveNoOpDefaults)
{
    EXPECT_EQ(fx.getSpecularMapEXTProperty(), nullptr);
    EXPECT_EQ(fx.getSpecularColorMapEXTProperty(), nullptr);
    EXPECT_EQ(fx.getSpecularTextureCoordinateSetEXTProperty(), 0);
    EXPECT_EQ(fx.getSpecularColorTextureCoordinateSetEXTProperty(), 0);
    EXPECT_EQ(fx.getSpecularTextureTransformEXTProperty(), TextureTransformEXT{});
    EXPECT_EQ(fx.getSpecularColorTextureTransformEXTProperty(), TextureTransformEXT{});
    EXPECT_TRUE(fx.getSpecularColorTextureIsSrgbEXTProperty());

    GpuDrawParams params;
    fx.FillGpuDrawParams(params);
    EXPECT_EQ(params.pbrSpecularMap, nullptr);
    EXPECT_EQ(params.pbrSpecularColorMap, nullptr);
    EXPECT_EQ(params.pbrTextureCoordinateSetMask & 0b1100000u, 0u);
    EXPECT_FLOAT_EQ(params.pbrSpecularTextureTransformRows[0][0], 1.0f);
    EXPECT_FLOAT_EQ(params.pbrSpecularTextureTransformRows[1][1], 1.0f);
    EXPECT_FLOAT_EQ(params.pbrSpecularTextureTransformRows[2][0], 1.0f);
    EXPECT_FLOAT_EQ(params.pbrSpecularTextureTransformRows[3][1], 1.0f);
    EXPECT_TRUE(params.pbrSpecularColorTextureIsSrgb);
}

TEST_F(PbrEffectDefaultsTest, EmissiveFactorDefaultsToZero)
{
    EXPECT_EQ(fx.getEmissiveFactorProperty(), Vector3::Zero);
}

TEST_F(PbrEffectDefaultsTest, TextureCoordinateSelectorsDefaultValidateAndReachDrawParams)
{
    EXPECT_EQ(fx.getTextureCoordinateSetsEXTProperty(), (std::array<int, 5>{0, 0, 0, 0, 0}));

    fx.setTextureCoordinateSetEXTProperty(0, 1);
    fx.setTextureCoordinateSetEXTProperty(2, 1);
    fx.setTextureCoordinateSetEXTProperty(4, 1);
    EXPECT_EQ(fx.getTextureCoordinateSetsEXTProperty(), (std::array<int, 5>{1, 0, 1, 0, 1}));

    GpuDrawParams params;
    fx.FillGpuDrawParams(params);
    EXPECT_EQ(params.pbrTextureCoordinateSetMask, 0b10101u);

    EXPECT_THROW(fx.setTextureCoordinateSetEXTProperty(-1, 0), std::out_of_range);
    EXPECT_THROW(fx.setTextureCoordinateSetEXTProperty(5, 0), std::out_of_range);
    EXPECT_THROW(fx.setTextureCoordinateSetEXTProperty(0, -1), std::out_of_range);
    EXPECT_THROW(fx.setTextureCoordinateSetEXTProperty(0, 2), std::out_of_range);
}

TEST_F(PbrEffectDefaultsTest, TextureTransformsDefaultValidateAndReachAffineDrawParams)
{
    const TextureTransformEXT identity;
    EXPECT_EQ(fx.getTextureTransformsEXTProperty(),
              (std::array<TextureTransformEXT, 5>{identity, identity, identity, identity, identity}));

    const TextureTransformEXT transform{
        Vector2{0.25f, -0.5f}, Vector2{2.0f, 3.0f}, 1.5707963267948966f};
    fx.setTextureTransformEXTProperty(2, transform);
    EXPECT_EQ(fx.getTextureTransformsEXTProperty()[2], transform);

    GpuDrawParams params;
    fx.FillGpuDrawParams(params);
    EXPECT_NEAR(params.pbrTextureTransformRows[4][0], 0.0f, 1e-6f);
    EXPECT_NEAR(params.pbrTextureTransformRows[4][1], -3.0f, 1e-6f);
    EXPECT_FLOAT_EQ(params.pbrTextureTransformRows[4][2], 0.25f);
    EXPECT_FLOAT_EQ(params.pbrTextureTransformRows[4][3], 0.0f);
    EXPECT_NEAR(params.pbrTextureTransformRows[5][0], 2.0f, 1e-6f);
    EXPECT_NEAR(params.pbrTextureTransformRows[5][1], 0.0f, 1e-6f);
    EXPECT_FLOAT_EQ(params.pbrTextureTransformRows[5][2], -0.5f);
    EXPECT_FLOAT_EQ(params.pbrTextureTransformRows[5][3], 0.0f);
    EXPECT_FLOAT_EQ(params.pbrTextureTransformRows[0][0], 1.0f);
    EXPECT_FLOAT_EQ(params.pbrTextureTransformRows[0][1], 0.0f);
    EXPECT_FLOAT_EQ(params.pbrTextureTransformRows[1][0], 0.0f);
    EXPECT_FLOAT_EQ(params.pbrTextureTransformRows[1][1], 1.0f);

    EXPECT_THROW(fx.setTextureTransformEXTProperty(-1, identity), std::out_of_range);
    EXPECT_THROW(fx.setTextureTransformEXTProperty(5, identity), std::out_of_range);
}

TEST_F(PbrEffectDefaultsTest, DirectionalLight0DefaultsToDisabled)
{
    EXPECT_FALSE(fx.DirectionalLight0.getEnabledProperty());
}

// -----------------------------------------------------------------------
// Setters / getters round-trip

TEST_F(PbrEffectDefaultsTest, SetWorldRoundTrips)
{
    const Matrix m = Matrix::CreateTranslation(Vector3(1, 2, 3));
    fx.setWorldProperty(m);
    EXPECT_EQ(fx.getWorldProperty(), m);
}

TEST_F(PbrEffectDefaultsTest, SetDiffuseColorRoundTrips)
{
    fx.setDiffuseColorProperty(Vector3(0.2f, 0.4f, 0.6f));
    EXPECT_EQ(fx.getDiffuseColorProperty(), Vector3(0.2f, 0.4f, 0.6f));
}

TEST_F(PbrEffectDefaultsTest, SetMetallicFactorRoundTrips)
{
    fx.setMetallicFactorProperty(0.25f);
    EXPECT_FLOAT_EQ(fx.getMetallicFactorProperty(), 0.25f);
}

TEST_F(PbrEffectDefaultsTest, SetRoughnessFactorRoundTrips)
{
    fx.setRoughnessFactorProperty(0.75f);
    EXPECT_FLOAT_EQ(fx.getRoughnessFactorProperty(), 0.75f);
}

TEST_F(PbrEffectDefaultsTest, IorAndSpecularFactorsRoundTripAndReachDrawParams)
{
    fx.setIorEXTProperty(2.0f);
    fx.setSpecularFactorEXTProperty(0.3f);
    fx.setSpecularColorFactorEXTProperty(Vector3(0.25f, 1.0f, 12.0f));

    EXPECT_FLOAT_EQ(fx.getIorEXTProperty(), 2.0f);
    EXPECT_FLOAT_EQ(fx.getSpecularFactorEXTProperty(), 0.3f);
    EXPECT_EQ(fx.getSpecularColorFactorEXTProperty(), Vector3(0.25f, 1.0f, 12.0f));

    GpuDrawParams params;
    fx.FillGpuDrawParams(params);
    EXPECT_NEAR(params.pbrDielectricF0[0], 1.0f / 120.0f, 1e-7f);
    EXPECT_NEAR(params.pbrDielectricF0[1], 1.0f / 30.0f, 1e-7f);
    EXPECT_NEAR(params.pbrDielectricF0[2], 0.3f, 1e-7f)
        << "the colour product must clamp before specularFactor is applied";
    EXPECT_FLOAT_EQ(params.pbrDielectricF90, 0.3f);
}

TEST_F(PbrEffectDefaultsTest, SpecularTextureInputsRoundTripAndReachDrawParams)
{
    auto strength = std::make_shared<Texture2D>(gd, 2, 2);
    auto color = std::make_shared<Texture2D>(gd, 2, 2);
    const TextureTransformEXT strengthTransform{
        Vector2{0.25f, -0.5f}, Vector2{2.0f, 3.0f}, 1.5707963267948966f};
    const TextureTransformEXT colorTransform{
        Vector2{-0.75f, 0.5f}, Vector2{0.5f, 4.0f}, 0.0f};

    fx.SetOwnedSpecularMapEXT(strength);
    fx.SetOwnedSpecularColorMapEXT(color);
    fx.setSpecularTextureCoordinateSetEXTProperty(1);
    fx.setSpecularColorTextureCoordinateSetEXTProperty(1);
    fx.setSpecularTextureTransformEXTProperty(strengthTransform);
    fx.setSpecularColorTextureTransformEXTProperty(colorTransform);
    fx.setSpecularColorTextureIsSrgbEXTProperty(false);

    EXPECT_EQ(fx.getSpecularMapEXTProperty(), strength.get());
    EXPECT_EQ(fx.getSpecularColorMapEXTProperty(), color.get());
    EXPECT_EQ(fx.getSpecularTextureCoordinateSetEXTProperty(), 1);
    EXPECT_EQ(fx.getSpecularColorTextureCoordinateSetEXTProperty(), 1);
    EXPECT_EQ(fx.getSpecularTextureTransformEXTProperty(), strengthTransform);
    EXPECT_EQ(fx.getSpecularColorTextureTransformEXTProperty(), colorTransform);
    EXPECT_FALSE(fx.getSpecularColorTextureIsSrgbEXTProperty());
    EXPECT_THROW(fx.setSpecularTextureCoordinateSetEXTProperty(-1), std::out_of_range);
    EXPECT_THROW(fx.setSpecularColorTextureCoordinateSetEXTProperty(2), std::out_of_range);

    GpuDrawParams params;
    fx.FillGpuDrawParams(params);
    EXPECT_EQ(params.pbrSpecularMap, &strength->GetRenderer());
    EXPECT_EQ(params.pbrSpecularColorMap, &color->GetRenderer());
    EXPECT_EQ(params.pbrTextureCoordinateSetMask & 0b1100000u, 0b1100000u);
    EXPECT_NEAR(params.pbrSpecularTextureTransformRows[0][0], 0.0f, 1e-6f);
    EXPECT_NEAR(params.pbrSpecularTextureTransformRows[0][1], -3.0f, 1e-6f);
    EXPECT_FLOAT_EQ(params.pbrSpecularTextureTransformRows[0][2], 0.25f);
    EXPECT_NEAR(params.pbrSpecularTextureTransformRows[1][0], 2.0f, 1e-6f);
    EXPECT_FLOAT_EQ(params.pbrSpecularTextureTransformRows[1][2], -0.5f);
    EXPECT_FLOAT_EQ(params.pbrSpecularTextureTransformRows[2][0], 0.5f);
    EXPECT_FLOAT_EQ(params.pbrSpecularTextureTransformRows[2][2], -0.75f);
    EXPECT_FLOAT_EQ(params.pbrSpecularTextureTransformRows[3][1], 4.0f);
    EXPECT_FLOAT_EQ(params.pbrSpecularTextureTransformRows[3][2], 0.5f);
    EXPECT_FALSE(params.pbrSpecularColorTextureIsSrgb);
}

TEST_F(PbrEffectDefaultsTest, SetEmissiveFactorRoundTrips)
{
    fx.setEmissiveFactorProperty(Vector3(0.1f, 0.2f, 0.3f));
    EXPECT_EQ(fx.getEmissiveFactorProperty(), Vector3(0.1f, 0.2f, 0.3f));
}

TEST_F(PbrEffectDefaultsTest, SetTextureRoundTrips)
{
    const std::vector<std::uint8_t> px = {255, 255, 255, 255};
    Texture2D tex = Texture2D::CreateFromPixels(gd, 1, 1, px);
    fx.setTextureProperty(&tex);
    EXPECT_EQ(fx.getTextureProperty(), &tex);
}

TEST_F(PbrEffectDefaultsTest, SetNormalMapRoundTrips)
{
    const std::vector<std::uint8_t> px = {128, 128, 255, 255};
    Texture2D tex = Texture2D::CreateFromPixels(gd, 1, 1, px);
    fx.setNormalMapProperty(&tex);
    EXPECT_EQ(fx.getNormalMapProperty(), &tex);
}

TEST_F(PbrEffectDefaultsTest, EnableDefaultLightingEnablesAllThreeLightsAndSetsAmbient)
{
    fx.EnableDefaultLighting();
    EXPECT_TRUE(fx.DirectionalLight0.getEnabledProperty());
    EXPECT_TRUE(fx.DirectionalLight1.getEnabledProperty());
    EXPECT_TRUE(fx.DirectionalLight2.getEnabledProperty());
    EXPECT_NE(fx.getAmbientLightColorProperty(), Vector3::Zero);
}

// -----------------------------------------------------------------------
// Clone

TEST_F(PbrEffectDefaultsTest, CloneCopiesMaterialState)
{
    fx.setMetallicFactorProperty(0.3f);
    fx.setRoughnessFactorProperty(0.6f);
    fx.setEmissiveFactorProperty(Vector3(0.5f, 0.5f, 0.5f));
    fx.setIorEXTProperty(1.8f);
    fx.setSpecularFactorEXTProperty(0.4f);
    fx.setSpecularColorFactorEXTProperty(Vector3(0.2f, 0.3f, 0.4f));
    fx.setTextureCoordinateSetEXTProperty(3, 1);
    const TextureTransformEXT transform{
        Vector2{0.2f, 0.4f}, Vector2{0.5f, 0.75f}, 0.3f};
    fx.setTextureTransformEXTProperty(3, transform);
    auto specular = std::make_shared<Texture2D>(gd, 2, 2);
    fx.SetOwnedSpecularMapEXT(specular);
    fx.setSpecularTextureCoordinateSetEXTProperty(1);
    fx.setSpecularTextureTransformEXTProperty(transform);
    fx.setSpecularColorTextureIsSrgbEXTProperty(false);

    auto* cloned = dynamic_cast<PbrEffect*>(fx.Clone());
    ASSERT_NE(cloned, nullptr);
    EXPECT_FLOAT_EQ(cloned->getMetallicFactorProperty(), 0.3f);
    EXPECT_FLOAT_EQ(cloned->getRoughnessFactorProperty(), 0.6f);
    EXPECT_EQ(cloned->getEmissiveFactorProperty(), Vector3(0.5f, 0.5f, 0.5f));
    EXPECT_FLOAT_EQ(cloned->getIorEXTProperty(), 1.8f);
    EXPECT_FLOAT_EQ(cloned->getSpecularFactorEXTProperty(), 0.4f);
    EXPECT_EQ(cloned->getSpecularColorFactorEXTProperty(), Vector3(0.2f, 0.3f, 0.4f));
    EXPECT_EQ(cloned->getTextureCoordinateSetsEXTProperty(),
              (std::array<int, 5>{0, 0, 0, 1, 0}));
    EXPECT_EQ(cloned->getTextureTransformsEXTProperty()[3], transform);
    EXPECT_EQ(cloned->getSpecularMapEXTProperty(), specular.get());
    EXPECT_EQ(cloned->getSpecularTextureCoordinateSetEXTProperty(), 1);
    EXPECT_EQ(cloned->getSpecularTextureTransformEXTProperty(), transform);
    EXPECT_FALSE(cloned->getSpecularColorTextureIsSrgbEXTProperty());
    delete cloned;
}

// -----------------------------------------------------------------------
// GetTypeName / FillGpuDrawParams

TEST_F(PbrEffectDefaultsTest, GetTypeNameIsCorrect)
{
    EXPECT_EQ(fx.GetTypeName(), "Microsoft.Xna.Framework.Graphics.PbrEffect");
}

TEST_F(PbrEffectDefaultsTest, FillGpuDrawParamsSetsThePbrFlag)
{
    GpuDrawParams params;
    fx.FillGpuDrawParams(params);
    EXPECT_TRUE(params.pbr);
}

TEST_F(PbrEffectDefaultsTest, FillGpuDrawParamsCarriesFactorsAndFogState)
{
    fx.setMetallicFactorProperty(0.2f);
    fx.setRoughnessFactorProperty(0.9f);
    fx.setFogEnabledProperty(true);
    fx.setFogStartProperty(2.0f);
    fx.setFogEndProperty(8.0f);

    GpuDrawParams params;
    fx.FillGpuDrawParams(params);
    EXPECT_FLOAT_EQ(params.pbrMetallicFactor, 0.2f);
    EXPECT_FLOAT_EQ(params.pbrRoughnessFactor, 0.9f);
    EXPECT_TRUE(params.fogEnabled);
    EXPECT_FLOAT_EQ(params.fogVector[0], 0.0f);
    EXPECT_FLOAT_EQ(params.fogVector[1], 0.0f);
    EXPECT_FLOAT_EQ(params.fogVector[2], -1.0f / 6.0f);
    EXPECT_FLOAT_EQ(params.fogVector[3], -1.0f / 3.0f);
}
