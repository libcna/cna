// SPDX-License-Identifier: MS-PL
// Task 362: exhaustive default-value coverage for every BasicEffect property,
// against FNA's Graphics/Effect/StockEffects/BasicEffect.cs and
// Graphics/DirectionalLight.cs. Builds on Task 361's audit, which found and
// fixed 2 real default-value bugs (VertexColorEnabled, DirectionalLight0.Enabled)
// in BasicEffect.hpp/.cpp and corrected examples/basic_effect_test.cpp's
// pre-existing (wrong) assertions; this file is the dedicated, centralized,
// GTest-based lock-in for all 22 properties' defaults.

#include <gtest/gtest.h>

#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"

using Microsoft::Xna::Framework::Matrix;
using Microsoft::Xna::Framework::Vector3;
using Microsoft::Xna::Framework::Graphics::BasicEffect;
using Microsoft::Xna::Framework::Graphics::DirectionalLight;
using Microsoft::Xna::Framework::Graphics::GraphicsDevice;

namespace
{
    class BasicEffectDefaultsTest : public ::testing::Test
    {
    protected:
        GraphicsDevice gd;
        BasicEffect fx{gd};
    };
}

// -----------------------------------------------------------------------
// Matrices (IEffectMatrices) — FNA: world/view/projection = Matrix.Identity

TEST_F(BasicEffectDefaultsTest, WorldDefaultsToIdentity)
{
    EXPECT_EQ(fx.getWorldProperty(), Matrix::getIdentityProperty());
}

TEST_F(BasicEffectDefaultsTest, ViewDefaultsToIdentity)
{
    EXPECT_EQ(fx.getViewProperty(), Matrix::getIdentityProperty());
}

TEST_F(BasicEffectDefaultsTest, ProjectionDefaultsToIdentity)
{
    EXPECT_EQ(fx.getProjectionProperty(), Matrix::getIdentityProperty());
}

// -----------------------------------------------------------------------
// Material color / lighting (IEffectLights) — FNA: diffuseColor = Vector3.One,
// emissiveColor = Vector3.Zero, ambientLightColor = Vector3.Zero, alpha = 1,
// specularColor/-Power set explicitly in the FNA ctor to One/16.

TEST_F(BasicEffectDefaultsTest, DiffuseColorDefaultsToOne)
{
    EXPECT_EQ(fx.getDiffuseColorProperty(), Vector3(1.0f, 1.0f, 1.0f));
}

TEST_F(BasicEffectDefaultsTest, EmissiveColorDefaultsToZero)
{
    EXPECT_EQ(fx.getEmissiveColorProperty(), Vector3::Zero);
}

TEST_F(BasicEffectDefaultsTest, AmbientLightColorDefaultsToZero)
{
    EXPECT_EQ(fx.getAmbientLightColorProperty(), Vector3::Zero);
}

TEST_F(BasicEffectDefaultsTest, AlphaDefaultsToOne)
{
    EXPECT_FLOAT_EQ(fx.getAlphaProperty(), 1.0f);
}

TEST_F(BasicEffectDefaultsTest, SpecularColorDefaultsToOne)
{
    // FNA's BasicEffect(GraphicsDevice) ctor explicitly sets SpecularColor = Vector3.One.
    EXPECT_EQ(fx.getSpecularColorProperty(), Vector3(1.0f, 1.0f, 1.0f));
}

TEST_F(BasicEffectDefaultsTest, SpecularPowerDefaultsTo16)
{
    // FNA's BasicEffect(GraphicsDevice) ctor explicitly sets SpecularPower = 16.
    EXPECT_FLOAT_EQ(fx.getSpecularPowerProperty(), 16.0f);
}

TEST_F(BasicEffectDefaultsTest, LightingEnabledDefaultsToFalse)
{
    EXPECT_FALSE(fx.getLightingEnabledProperty());
}

TEST_F(BasicEffectDefaultsTest, PreferPerPixelLightingDefaultsToFalse)
{
    EXPECT_FALSE(fx.getPreferPerPixelLightingProperty());
}

// -----------------------------------------------------------------------
// Directional lights — FNA's DirectionalLight ctor defaults every field to
// its C# default(T) (Vector3.Zero / false) unless a cloneSource is given;
// BasicEffect's own ctor then explicitly flips DirectionalLight0.Enabled to
// true (Task 361's 2nd fix), leaving light1/light2 disabled.

TEST_F(BasicEffectDefaultsTest, DirectionalLight0EnabledDefaultsToTrue)
{
    EXPECT_TRUE(fx.getDirectionalLight0Property().getEnabledProperty());
}

TEST_F(BasicEffectDefaultsTest, DirectionalLight1EnabledDefaultsToFalse)
{
    EXPECT_FALSE(fx.getDirectionalLight1Property().getEnabledProperty());
}

TEST_F(BasicEffectDefaultsTest, DirectionalLight2EnabledDefaultsToFalse)
{
    EXPECT_FALSE(fx.getDirectionalLight2Property().getEnabledProperty());
}

TEST_F(BasicEffectDefaultsTest, AllThreeDirectionalLightsDefaultColorsAndDirectionToZero)
{
    for (DirectionalLight* light : {
             &fx.getDirectionalLight0Property(),
             &fx.getDirectionalLight1Property(),
             &fx.getDirectionalLight2Property()
         })
    {
        EXPECT_EQ(light->getDiffuseColorProperty(), Vector3::Zero);
        EXPECT_EQ(light->getSpecularColorProperty(), Vector3::Zero);
        EXPECT_EQ(light->getDirectionProperty(), Vector3::Zero);
    }
}

// -----------------------------------------------------------------------
// Fog (IEffectFog) — FNA: fogEnabled = false, fogStart = 0, fogEnd = 1,
// fogColor has no field initializer in FNA (backed by an EffectParameter
// whose compiled-shader default is black); CNA's fogColor_ defaults to
// Vector3::Zero, matching that same black default.

TEST_F(BasicEffectDefaultsTest, FogEnabledDefaultsToFalse)
{
    EXPECT_FALSE(fx.getFogEnabledProperty());
}

TEST_F(BasicEffectDefaultsTest, FogStartDefaultsToZero)
{
    EXPECT_FLOAT_EQ(fx.getFogStartProperty(), 0.0f);
}

TEST_F(BasicEffectDefaultsTest, FogEndDefaultsToOne)
{
    EXPECT_FLOAT_EQ(fx.getFogEndProperty(), 1.0f);
}

TEST_F(BasicEffectDefaultsTest, FogColorDefaultsToZero)
{
    EXPECT_EQ(fx.getFogColorProperty(), Vector3::Zero);
}

// -----------------------------------------------------------------------
// Texturing / vertex color — FNA: textureEnabled = false, vertexColorEnabled
// = false (Task 361's 1st fix), texture = null.

TEST_F(BasicEffectDefaultsTest, TextureEnabledDefaultsToFalse)
{
    EXPECT_FALSE(fx.getTextureEnabledProperty());
}

TEST_F(BasicEffectDefaultsTest, VertexColorEnabledDefaultsToFalse)
{
    EXPECT_FALSE(fx.VertexColorEnabled);
}

TEST_F(BasicEffectDefaultsTest, TextureDefaultsToNull)
{
    EXPECT_EQ(fx.getTextureProperty(), nullptr);
}

// -----------------------------------------------------------------------
// Task 363: EnableDefaultLighting() exact constants, cross-checked literal-
// for-literal against FNA's Graphics/Effect/StockEffects/EffectHelpers.cs
// (EnableDefaultLighting) and BasicEffect.cs (EnableDefaultLighting, which
// additionally sets LightingEnabled = true). Complements Task 194's existing
// EasyGL integration test (examples/easygl_basiceffect_default_lighting_test.cpp),
// which already caught and fixed 2 literal-value bugs in this same rig
// (Light2.SpecularColor, Light2.DiffuseColor.Y) — this is the GTest-level,
// GPU-independent lock-in for the same 3-light rig, using a tight epsilon
// since every value here is a hardcoded literal, not a computed approximation.

TEST_F(BasicEffectDefaultsTest, EnableDefaultLightingSetsLightingEnabled)
{
    fx.EnableDefaultLighting();
    EXPECT_TRUE(fx.getLightingEnabledProperty());
}

TEST_F(BasicEffectDefaultsTest, EnableDefaultLightingSetsAmbientLightColor)
{
    fx.EnableDefaultLighting();
    constexpr float kEps = 1e-6f;
    const Vector3 ambient = fx.getAmbientLightColorProperty();
    EXPECT_NEAR(ambient.X, 0.05333332f, kEps);
    EXPECT_NEAR(ambient.Y, 0.09882354f, kEps);
    EXPECT_NEAR(ambient.Z, 0.1819608f, kEps);
}

TEST_F(BasicEffectDefaultsTest, EnableDefaultLightingSetsKeyLightExactConstants)
{
    fx.EnableDefaultLighting();
    constexpr float kEps = 1e-6f;
    const DirectionalLight& light0 = fx.getDirectionalLight0Property();
    EXPECT_TRUE(light0.getEnabledProperty());

    const Vector3 dir = light0.getDirectionProperty();
    EXPECT_NEAR(dir.X, -0.5265408f, kEps);
    EXPECT_NEAR(dir.Y, -0.5735765f, kEps);
    EXPECT_NEAR(dir.Z, -0.6275069f, kEps);

    const Vector3 diffuse = light0.getDiffuseColorProperty();
    EXPECT_NEAR(diffuse.X, 1.0f, kEps);
    EXPECT_NEAR(diffuse.Y, 0.9607844f, kEps);
    EXPECT_NEAR(diffuse.Z, 0.8078432f, kEps);

    const Vector3 specular = light0.getSpecularColorProperty();
    EXPECT_NEAR(specular.X, 1.0f, kEps);
    EXPECT_NEAR(specular.Y, 0.9607844f, kEps);
    EXPECT_NEAR(specular.Z, 0.8078432f, kEps);
}

TEST_F(BasicEffectDefaultsTest, EnableDefaultLightingSetsFillLightExactConstants)
{
    fx.EnableDefaultLighting();
    constexpr float kEps = 1e-6f;
    const DirectionalLight& light1 = fx.getDirectionalLight1Property();
    EXPECT_TRUE(light1.getEnabledProperty());

    const Vector3 dir = light1.getDirectionProperty();
    EXPECT_NEAR(dir.X, 0.7198464f, kEps);
    EXPECT_NEAR(dir.Y, 0.3420201f, kEps);
    EXPECT_NEAR(dir.Z, 0.6040227f, kEps);

    const Vector3 diffuse = light1.getDiffuseColorProperty();
    EXPECT_NEAR(diffuse.X, 0.9647059f, kEps);
    EXPECT_NEAR(diffuse.Y, 0.7607844f, kEps);
    EXPECT_NEAR(diffuse.Z, 0.4078432f, kEps);

    // FNA sets light1.SpecularColor = Vector3.Zero explicitly (fill light has
    // no specular contribution) — distinct from light0/light2, which do.
    EXPECT_EQ(light1.getSpecularColorProperty(), Vector3::Zero);
}

TEST_F(BasicEffectDefaultsTest, EnableDefaultLightingSetsBackLightExactConstants)
{
    fx.EnableDefaultLighting();
    constexpr float kEps = 1e-6f;
    const DirectionalLight& light2 = fx.getDirectionalLight2Property();
    EXPECT_TRUE(light2.getEnabledProperty());

    const Vector3 dir = light2.getDirectionProperty();
    EXPECT_NEAR(dir.X, 0.4545195f, kEps);
    EXPECT_NEAR(dir.Y, -0.7660444f, kEps);
    EXPECT_NEAR(dir.Z, 0.4545195f, kEps);

    const Vector3 diffuse = light2.getDiffuseColorProperty();
    EXPECT_NEAR(diffuse.X, 0.3231373f, kEps);
    EXPECT_NEAR(diffuse.Y, 0.3607844f, kEps);
    EXPECT_NEAR(diffuse.Z, 0.3937255f, kEps);

    const Vector3 specular = light2.getSpecularColorProperty();
    EXPECT_NEAR(specular.X, 0.3231373f, kEps);
    EXPECT_NEAR(specular.Y, 0.3607844f, kEps);
    EXPECT_NEAR(specular.Z, 0.3937255f, kEps);
}
