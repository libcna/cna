// SPDX-License-Identifier: MS-PL
// plan_cnj.md CNB-56/60 (Phase 13A): default-value and getter/setter coverage for PbrEffect, the
// new NOXNA metallic-roughness PBR effect (no FNA/XNA equivalent to audit against -- real XNA
// predates the PBR content pipeline entirely). See PbrEffect.hpp's own doc comment for the design
// rationale (glTF 2.0's own reference BRDF, CNA's established 3-light + ambient convention).

#include <gtest/gtest.h>
#include <stdexcept>

#include "CNA/Internal/Renderers/Common/IGraphicsRenderer.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/PbrEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"

using Microsoft::Xna::Framework::Matrix;
using Microsoft::Xna::Framework::Vector3;
using Microsoft::Xna::Framework::Graphics::GraphicsDevice;
using Microsoft::Xna::Framework::Graphics::PbrEffect;
using Microsoft::Xna::Framework::Graphics::Texture2D;
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

TEST_F(PbrEffectDefaultsTest, EmissiveFactorDefaultsToZero)
{
    EXPECT_EQ(fx.getEmissiveFactorProperty(), Vector3::Zero);
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

    auto* cloned = dynamic_cast<PbrEffect*>(fx.Clone());
    ASSERT_NE(cloned, nullptr);
    EXPECT_FLOAT_EQ(cloned->getMetallicFactorProperty(), 0.3f);
    EXPECT_FLOAT_EQ(cloned->getRoughnessFactorProperty(), 0.6f);
    EXPECT_EQ(cloned->getEmissiveFactorProperty(), Vector3(0.5f, 0.5f, 0.5f));
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
