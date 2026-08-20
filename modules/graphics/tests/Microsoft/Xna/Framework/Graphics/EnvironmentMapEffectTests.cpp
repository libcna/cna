// SPDX-License-Identifier: MS-PL
// Task 392: exhaustive default-value coverage for every EnvironmentMapEffect
// property, against FNA's Graphics/Effect/StockEffects/EnvironmentMapEffect.cs.
// Builds on Task 391's audit, which found zero default-value bugs; this file
// is the dedicated, centralized, GTest-based lock-in for all properties'
// defaults that Task 391 itself found no existing coverage for.

#include <gtest/gtest.h>

#include <memory>
#include <stdexcept>

#include "Microsoft/Xna/Framework/Graphics/DirectionalLight.hpp"
#include "Microsoft/Xna/Framework/Graphics/EnvironmentMapEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/TextureCube.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"

using Microsoft::Xna::Framework::Matrix;
using Microsoft::Xna::Framework::Vector3;
using Microsoft::Xna::Framework::Graphics::DirectionalLight;
using Microsoft::Xna::Framework::Graphics::EnvironmentMapEffect;
using Microsoft::Xna::Framework::Graphics::GraphicsDevice;
using Microsoft::Xna::Framework::Graphics::SurfaceFormat;
using Microsoft::Xna::Framework::Graphics::Texture2D;
using Microsoft::Xna::Framework::Graphics::TextureCube;

namespace
{
    class EnvironmentMapEffectDefaultsTest : public ::testing::Test
    {
    protected:
        GraphicsDevice gd;
        EnvironmentMapEffect fx{gd};
    };
}

// -----------------------------------------------------------------------
// Matrices (IEffectMatrices) — FNA: world/view/projection = Matrix.Identity

TEST_F(EnvironmentMapEffectDefaultsTest, WorldDefaultsToIdentity)
{
    EXPECT_EQ(fx.getWorldProperty(), Matrix::getIdentityProperty());
}

TEST_F(EnvironmentMapEffectDefaultsTest, ViewDefaultsToIdentity)
{
    EXPECT_EQ(fx.getViewProperty(), Matrix::getIdentityProperty());
}

TEST_F(EnvironmentMapEffectDefaultsTest, ProjectionDefaultsToIdentity)
{
    EXPECT_EQ(fx.getProjectionProperty(), Matrix::getIdentityProperty());
}

// -----------------------------------------------------------------------
// Material color — FNA: diffuseColor=Vector3.One, emissiveColor=Zero, alpha=1,
// ambientLightColor=Zero.

TEST_F(EnvironmentMapEffectDefaultsTest, DiffuseColorDefaultsToOne)
{
    EXPECT_EQ(fx.getDiffuseColorProperty(), Vector3(1.0f, 1.0f, 1.0f));
}

TEST_F(EnvironmentMapEffectDefaultsTest, EmissiveColorDefaultsToZero)
{
    EXPECT_EQ(fx.getEmissiveColorProperty(), Vector3::Zero);
}

TEST_F(EnvironmentMapEffectDefaultsTest, AlphaDefaultsToOne)
{
    EXPECT_FLOAT_EQ(fx.getAlphaProperty(), 1.0f);
}

TEST_F(EnvironmentMapEffectDefaultsTest, AmbientLightColorDefaultsToZero)
{
    EXPECT_EQ(fx.getAmbientLightColorProperty(), Vector3::Zero);
}

// -----------------------------------------------------------------------
// Lighting — FNA's EnvironmentMapEffect hard-locks LightingEnabled=true via
// an explicit IEffectLights interface implementation that throws on `false`.

TEST_F(EnvironmentMapEffectDefaultsTest, LightingEnabledIsAlwaysTrue)
{
    EXPECT_TRUE(fx.getLightingEnabledProperty());
}

TEST_F(EnvironmentMapEffectDefaultsTest, SetLightingEnabledFalseThrows)
{
    EXPECT_THROW(fx.setLightingEnabledProperty(false), std::runtime_error);
}

TEST_F(EnvironmentMapEffectDefaultsTest, SetLightingEnabledTrueDoesNotThrow)
{
    EXPECT_NO_THROW(fx.setLightingEnabledProperty(true));
    EXPECT_TRUE(fx.getLightingEnabledProperty());
}

// -----------------------------------------------------------------------
// Directional lights — FNA's constructor explicitly sets
// DirectionalLight0.Enabled=true; DirectionalLight1/2 stay at DirectionalLight's
// own class defaults (Enabled=false).

TEST_F(EnvironmentMapEffectDefaultsTest, DirectionalLight0EnabledDefaultsToTrue)
{
    EXPECT_TRUE(fx.getDirectionalLight0Property().getEnabledProperty());
}

TEST_F(EnvironmentMapEffectDefaultsTest, DirectionalLight1EnabledDefaultsToFalse)
{
    EXPECT_FALSE(fx.getDirectionalLight1Property().getEnabledProperty());
}

TEST_F(EnvironmentMapEffectDefaultsTest, DirectionalLight2EnabledDefaultsToFalse)
{
    EXPECT_FALSE(fx.getDirectionalLight2Property().getEnabledProperty());
}

TEST_F(EnvironmentMapEffectDefaultsTest, EnableDefaultLightingSetsAmbientLightColor)
{
    fx.EnableDefaultLighting();
    const Vector3 ambient = fx.getAmbientLightColorProperty();
    EXPECT_NEAR(ambient.X, 0.05333332f, 1e-6f);
    EXPECT_NEAR(ambient.Y, 0.09882354f, 1e-6f);
    EXPECT_NEAR(ambient.Z, 0.1819608f, 1e-6f);
}

TEST_F(EnvironmentMapEffectDefaultsTest, EnableDefaultLightingSetsAllThreeLightsEnabled)
{
    fx.EnableDefaultLighting();
    EXPECT_TRUE(fx.getDirectionalLight0Property().getEnabledProperty());
    EXPECT_TRUE(fx.getDirectionalLight1Property().getEnabledProperty());
    EXPECT_TRUE(fx.getDirectionalLight2Property().getEnabledProperty());
}

// -----------------------------------------------------------------------
// Fog (IEffectFog) — FNA: fogEnabled=false, fogStart=0, fogEnd=1; fogColor
// backed by an EffectParameter defaulting to black.

TEST_F(EnvironmentMapEffectDefaultsTest, FogEnabledDefaultsToFalse)
{
    EXPECT_FALSE(fx.getFogEnabledProperty());
}

TEST_F(EnvironmentMapEffectDefaultsTest, FogStartDefaultsToZero)
{
    EXPECT_FLOAT_EQ(fx.getFogStartProperty(), 0.0f);
}

TEST_F(EnvironmentMapEffectDefaultsTest, FogEndDefaultsToOne)
{
    EXPECT_FLOAT_EQ(fx.getFogEndProperty(), 1.0f);
}

TEST_F(EnvironmentMapEffectDefaultsTest, FogColorDefaultsToZero)
{
    EXPECT_EQ(fx.getFogColorProperty(), Vector3::Zero);
}

// -----------------------------------------------------------------------
// Textures — FNA: Texture/EnvironmentMap default to null.

TEST_F(EnvironmentMapEffectDefaultsTest, TextureDefaultsToNull)
{
    EXPECT_EQ(fx.getTextureProperty(), nullptr);
}

TEST_F(EnvironmentMapEffectDefaultsTest, EnvironmentMapDefaultsToNull)
{
    EXPECT_EQ(fx.getEnvironmentMapProperty(), nullptr);
}

// plans/plan_xnb.md XNB-32: SetOwnedTexture()/SetOwnedEnvironmentMap() -- content-pipeline-loaded
// effects need to keep their own texture references alive (matching real XNA's GC-tracked
// Effect.Texture), unlike setTextureProperty(Texture2D*)/setEnvironmentMapProperty(TextureCube*)'s
// non-owning pointers used by Model's shared texture pool.

TEST_F(EnvironmentMapEffectDefaultsTest, SetOwnedTextureKeepsTextureAliveAndVisibleThroughGetter)
{
    auto owned = std::make_shared<Texture2D>(gd, 2, 2);
    Texture2D* raw = owned.get();

    fx.SetOwnedTexture(owned);

    EXPECT_EQ(fx.getTextureProperty(), raw);
}

TEST_F(EnvironmentMapEffectDefaultsTest, SetOwnedEnvironmentMapKeepsCubeAliveAndVisibleThroughGetter)
{
    auto owned = std::make_shared<TextureCube>(gd, 4, false, SurfaceFormat::Color);
    TextureCube* raw = owned.get();

    fx.SetOwnedEnvironmentMap(owned);

    EXPECT_EQ(fx.getEnvironmentMapProperty(), raw);
}

TEST_F(EnvironmentMapEffectDefaultsTest, CloneSharesOwnedTextureOwnership)
{
    fx.SetOwnedTexture(std::make_shared<Texture2D>(gd, 2, 2));
    fx.SetOwnedEnvironmentMap(std::make_shared<TextureCube>(gd, 4, false, SurfaceFormat::Color));
    Texture2D* originalTexturePtr = fx.getTextureProperty();
    TextureCube* originalCubePtr = fx.getEnvironmentMapProperty();

    std::unique_ptr<Microsoft::Xna::Framework::Graphics::Effect> cloned(fx.Clone());
    auto* clone = dynamic_cast<EnvironmentMapEffect*>(cloned.get());
    ASSERT_NE(clone, nullptr);

    EXPECT_EQ(clone->getTextureProperty(), originalTexturePtr);
    EXPECT_EQ(clone->getEnvironmentMapProperty(), originalCubePtr);
}

// -----------------------------------------------------------------------
// Environment map parameters — FNA's constructor explicitly sets these
// (not just class-field defaults): EnvironmentMapAmount=1,
// EnvironmentMapSpecular=Zero, FresnelFactor=1.

TEST_F(EnvironmentMapEffectDefaultsTest, EnvironmentMapAmountDefaultsToOne)
{
    EXPECT_FLOAT_EQ(fx.getEnvironmentMapAmountProperty(), 1.0f);
}

TEST_F(EnvironmentMapEffectDefaultsTest, EnvironmentMapSpecularDefaultsToZero)
{
    EXPECT_EQ(fx.getEnvironmentMapSpecularProperty(), Vector3::Zero);
}

TEST_F(EnvironmentMapEffectDefaultsTest, FresnelFactorDefaultsToOne)
{
    EXPECT_FLOAT_EQ(fx.getFresnelFactorProperty(), 1.0f);
}

// -----------------------------------------------------------------------
// Setters round-trip (every property must actually store what it's given).

TEST_F(EnvironmentMapEffectDefaultsTest, WorldRoundTrips)
{
    const Matrix m = Matrix::CreateTranslation(1.0f, 2.0f, 3.0f);
    fx.setWorldProperty(m);
    EXPECT_EQ(fx.getWorldProperty(), m);
}

TEST_F(EnvironmentMapEffectDefaultsTest, ViewRoundTrips)
{
    const Matrix m = Matrix::CreateTranslation(4.0f, 5.0f, 6.0f);
    fx.setViewProperty(m);
    EXPECT_EQ(fx.getViewProperty(), m);
}

TEST_F(EnvironmentMapEffectDefaultsTest, ProjectionRoundTrips)
{
    const Matrix m = Matrix::CreateTranslation(7.0f, 8.0f, 9.0f);
    fx.setProjectionProperty(m);
    EXPECT_EQ(fx.getProjectionProperty(), m);
}

TEST_F(EnvironmentMapEffectDefaultsTest, DiffuseColorRoundTrips)
{
    const Vector3 c(0.2f, 0.4f, 0.6f);
    fx.setDiffuseColorProperty(c);
    EXPECT_EQ(fx.getDiffuseColorProperty(), c);
}

TEST_F(EnvironmentMapEffectDefaultsTest, EmissiveColorRoundTrips)
{
    const Vector3 c(0.1f, 0.2f, 0.3f);
    fx.setEmissiveColorProperty(c);
    EXPECT_EQ(fx.getEmissiveColorProperty(), c);
}

TEST_F(EnvironmentMapEffectDefaultsTest, AlphaRoundTrips)
{
    fx.setAlphaProperty(0.5f);
    EXPECT_FLOAT_EQ(fx.getAlphaProperty(), 0.5f);
}

TEST_F(EnvironmentMapEffectDefaultsTest, AmbientLightColorRoundTrips)
{
    const Vector3 c(0.3f, 0.3f, 0.3f);
    fx.setAmbientLightColorProperty(c);
    EXPECT_EQ(fx.getAmbientLightColorProperty(), c);
}

TEST_F(EnvironmentMapEffectDefaultsTest, FogEnabledRoundTrips)
{
    fx.setFogEnabledProperty(true);
    EXPECT_TRUE(fx.getFogEnabledProperty());
}

TEST_F(EnvironmentMapEffectDefaultsTest, FogStartRoundTrips)
{
    fx.setFogStartProperty(10.0f);
    EXPECT_FLOAT_EQ(fx.getFogStartProperty(), 10.0f);
}

TEST_F(EnvironmentMapEffectDefaultsTest, FogEndRoundTrips)
{
    fx.setFogEndProperty(20.0f);
    EXPECT_FLOAT_EQ(fx.getFogEndProperty(), 20.0f);
}

TEST_F(EnvironmentMapEffectDefaultsTest, FogColorRoundTrips)
{
    const Vector3 c(0.1f, 0.2f, 0.3f);
    fx.setFogColorProperty(c);
    EXPECT_EQ(fx.getFogColorProperty(), c);
}

TEST_F(EnvironmentMapEffectDefaultsTest, TextureRoundTrips)
{
    Texture2D tex(gd, 1, 1);
    fx.setTextureProperty(&tex);
    EXPECT_EQ(fx.getTextureProperty(), &tex);
}

TEST_F(EnvironmentMapEffectDefaultsTest, EnvironmentMapRoundTrips)
{
    TextureCube cube(gd, 1, false, SurfaceFormat::Color);
    fx.setEnvironmentMapProperty(&cube);
    EXPECT_EQ(fx.getEnvironmentMapProperty(), &cube);
}

TEST_F(EnvironmentMapEffectDefaultsTest, EnvironmentMapAmountRoundTrips)
{
    fx.setEnvironmentMapAmountProperty(0.5f);
    EXPECT_FLOAT_EQ(fx.getEnvironmentMapAmountProperty(), 0.5f);
}

TEST_F(EnvironmentMapEffectDefaultsTest, EnvironmentMapSpecularRoundTrips)
{
    const Vector3 c(0.7f, 0.8f, 0.9f);
    fx.setEnvironmentMapSpecularProperty(c);
    EXPECT_EQ(fx.getEnvironmentMapSpecularProperty(), c);
}

TEST_F(EnvironmentMapEffectDefaultsTest, FresnelFactorRoundTrips)
{
    fx.setFresnelFactorProperty(0.25f);
    EXPECT_FLOAT_EQ(fx.getFresnelFactorProperty(), 0.25f);
}

// -----------------------------------------------------------------------
// Clone — FNA's clone constructor copies fogEnabled, fresnelEnabled,
// specularEnabled, world/view/projection, diffuseColor, emissiveColor,
// ambientLightColor, alpha, fogStart, fogEnd (plus Texture/EnvironmentMap and
// EnvironmentMapAmount/EnvironmentMapSpecular/FresnelFactor, carried via
// FNA's native effect-data clone; CNA copies these explicitly instead, per
// Task 391's audit finding).

TEST_F(EnvironmentMapEffectDefaultsTest, CloneCopiesAllProperties)
{
    Texture2D tex(gd, 1, 1);
    TextureCube cube(gd, 1, false, SurfaceFormat::Color);

    fx.setDiffuseColorProperty(Vector3(0.1f, 0.2f, 0.3f));
    fx.setEmissiveColorProperty(Vector3(0.4f, 0.5f, 0.6f));
    fx.setAmbientLightColorProperty(Vector3(0.05f, 0.1f, 0.15f));
    fx.setAlphaProperty(0.7f);
    fx.setFogEnabledProperty(true);
    fx.setFogStartProperty(1.0f);
    fx.setFogEndProperty(2.0f);
    fx.setFogColorProperty(Vector3(0.2f, 0.3f, 0.4f));
    fx.setTextureProperty(&tex);
    fx.setEnvironmentMapProperty(&cube);
    fx.setEnvironmentMapAmountProperty(0.6f);
    fx.setEnvironmentMapSpecularProperty(Vector3(0.1f, 0.1f, 0.1f));
    fx.setFresnelFactorProperty(0.3f);

    std::unique_ptr<Microsoft::Xna::Framework::Graphics::Effect> cloned(fx.Clone());
    auto* clone = dynamic_cast<EnvironmentMapEffect*>(cloned.get());
    ASSERT_NE(clone, nullptr);

    EXPECT_EQ(clone->getDiffuseColorProperty(), Vector3(0.1f, 0.2f, 0.3f));
    EXPECT_EQ(clone->getEmissiveColorProperty(), Vector3(0.4f, 0.5f, 0.6f));
    EXPECT_EQ(clone->getAmbientLightColorProperty(), Vector3(0.05f, 0.1f, 0.15f));
    EXPECT_FLOAT_EQ(clone->getAlphaProperty(), 0.7f);
    EXPECT_TRUE(clone->getFogEnabledProperty());
    EXPECT_FLOAT_EQ(clone->getFogStartProperty(), 1.0f);
    EXPECT_FLOAT_EQ(clone->getFogEndProperty(), 2.0f);
    EXPECT_EQ(clone->getFogColorProperty(), Vector3(0.2f, 0.3f, 0.4f));
    EXPECT_EQ(clone->getTextureProperty(), &tex);
    EXPECT_EQ(clone->getEnvironmentMapProperty(), &cube);
    EXPECT_FLOAT_EQ(clone->getEnvironmentMapAmountProperty(), 0.6f);
    EXPECT_EQ(clone->getEnvironmentMapSpecularProperty(), Vector3(0.1f, 0.1f, 0.1f));
    EXPECT_FLOAT_EQ(clone->getFresnelFactorProperty(), 0.3f);
}

// -----------------------------------------------------------------------
// GetTypeName — CNAEXT extension, reports the fully-qualified FNA type name.

TEST_F(EnvironmentMapEffectDefaultsTest, GetTypeNameReturnsFullyQualifiedName)
{
    EXPECT_EQ(fx.GetTypeName(), "Microsoft.Xna.Framework.Graphics.EnvironmentMapEffect");
}
