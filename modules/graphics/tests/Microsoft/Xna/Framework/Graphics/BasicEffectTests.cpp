// SPDX-License-Identifier: MS-PL
// Task 362: exhaustive default-value coverage for every BasicEffect property,
// against FNA's Graphics/Effect/StockEffects/BasicEffect.cs and
// Graphics/DirectionalLight.cs. Builds on Task 361's audit, which found and
// fixed 2 real default-value bugs (VertexColorEnabled, DirectionalLight0.Enabled)
// in BasicEffect.hpp/.cpp and corrected modules/graphics/examples/basic_effect_test.cpp's
// pre-existing (wrong) assertions; this file is the dedicated, centralized,
// GTest-based lock-in for all 22 properties' defaults.

#include <gtest/gtest.h>

#include <array>
#include <memory>
#include <string>

#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/Effect.hpp"
#include "Microsoft/Xna/Framework/Graphics/EffectParameter.hpp"
#include "Microsoft/Xna/Framework/Graphics/EffectParameterClass.hpp"
#include "Microsoft/Xna/Framework/Graphics/EffectParameterCollection.hpp"
#include "Microsoft/Xna/Framework/Graphics/EffectParameterType.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Vector4.hpp"

using Microsoft::Xna::Framework::Matrix;
using Microsoft::Xna::Framework::Vector3;
using Microsoft::Xna::Framework::Vector4;
using Microsoft::Xna::Framework::Graphics::BasicEffect;
using Microsoft::Xna::Framework::Graphics::DirectionalLight;
using Microsoft::Xna::Framework::Graphics::Effect;
using Microsoft::Xna::Framework::Graphics::EffectParameter;
using Microsoft::Xna::Framework::Graphics::EffectParameterClass;
using Microsoft::Xna::Framework::Graphics::EffectParameterType;
using Microsoft::Xna::Framework::Graphics::GraphicsDevice;
using Microsoft::Xna::Framework::Graphics::Texture2D;

namespace
{
    class BasicEffectDefaultsTest : public ::testing::Test
    {
    protected:
        GraphicsDevice gd;
        BasicEffect fx{gd};
    };
}

TEST_F(BasicEffectDefaultsTest, TechniqueMetadataMatchesXnaEmbeddedBasicEffect)
{
    const auto& techniques = fx.getTechniquesProperty();
    ASSERT_EQ(techniques.getCountProperty(), 1);
    ASSERT_NE(fx.getCurrentTechniqueProperty(), nullptr);
    EXPECT_EQ(fx.getCurrentTechniqueProperty()->getNameProperty(), "BasicEffect");
    EXPECT_EQ(techniques["BasicEffect"], fx.getCurrentTechniqueProperty());
    EXPECT_EQ(techniques["Default"], nullptr);

    std::unique_ptr<Effect> clone(fx.Clone());
    ASSERT_NE(clone->getCurrentTechniqueProperty(), nullptr);
    EXPECT_EQ(clone->getCurrentTechniqueProperty()->getNameProperty(), "BasicEffect");
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
// Directional lights — the Microsoft XNA 4.0 reference implementation initializes
// each DirectionalLight to Down/One/Zero/disabled. BasicEffect's own constructor
// then enables DirectionalLight0, leaving light1/light2 disabled.

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

TEST_F(BasicEffectDefaultsTest, AllThreeDirectionalLightsUseXnaDefaultValues)
{
    for (DirectionalLight* light : {
             &fx.getDirectionalLight0Property(),
             &fx.getDirectionalLight1Property(),
             &fx.getDirectionalLight2Property()
         })
    {
        EXPECT_EQ(light->getDiffuseColorProperty(), Vector3::One);
        EXPECT_EQ(light->getSpecularColorProperty(), Vector3::Zero);
        EXPECT_EQ(light->getDirectionProperty(), Vector3::Down);
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

TEST_F(BasicEffectDefaultsTest, ExposesAuthenticXnaParameterGraph)
{
    struct ExpectedParameter
    {
        const char* name;
        EffectParameterClass parameterClass;
        EffectParameterType parameterType;
        int rows;
        int columns;
    };

    constexpr std::array<ExpectedParameter, 21> expected{{
        {"Texture", EffectParameterClass::Object, EffectParameterType::Texture2D, 0, 0},
        {"DiffuseColor", EffectParameterClass::Vector, EffectParameterType::Single, 1, 4},
        {"EmissiveColor", EffectParameterClass::Vector, EffectParameterType::Single, 1, 3},
        {"SpecularColor", EffectParameterClass::Vector, EffectParameterType::Single, 1, 3},
        {"SpecularPower", EffectParameterClass::Scalar, EffectParameterType::Single, 1, 1},
        {"DirLight0Direction", EffectParameterClass::Vector, EffectParameterType::Single, 1, 3},
        {"DirLight0DiffuseColor", EffectParameterClass::Vector, EffectParameterType::Single, 1, 3},
        {"DirLight0SpecularColor", EffectParameterClass::Vector, EffectParameterType::Single, 1, 3},
        {"DirLight1Direction", EffectParameterClass::Vector, EffectParameterType::Single, 1, 3},
        {"DirLight1DiffuseColor", EffectParameterClass::Vector, EffectParameterType::Single, 1, 3},
        {"DirLight1SpecularColor", EffectParameterClass::Vector, EffectParameterType::Single, 1, 3},
        {"DirLight2Direction", EffectParameterClass::Vector, EffectParameterType::Single, 1, 3},
        {"DirLight2DiffuseColor", EffectParameterClass::Vector, EffectParameterType::Single, 1, 3},
        {"DirLight2SpecularColor", EffectParameterClass::Vector, EffectParameterType::Single, 1, 3},
        {"EyePosition", EffectParameterClass::Vector, EffectParameterType::Single, 1, 3},
        {"FogColor", EffectParameterClass::Vector, EffectParameterType::Single, 1, 3},
        {"FogVector", EffectParameterClass::Vector, EffectParameterType::Single, 1, 4},
        {"World", EffectParameterClass::Matrix, EffectParameterType::Single, 4, 4},
        {"WorldInverseTranspose", EffectParameterClass::Matrix, EffectParameterType::Single, 3, 3},
        {"WorldViewProj", EffectParameterClass::Matrix, EffectParameterType::Single, 4, 4},
        {"ShaderIndex", EffectParameterClass::Scalar, EffectParameterType::Int32, 1, 1},
    }};

    const auto& parameters = fx.getParametersProperty();
    ASSERT_EQ(parameters.getCountProperty(), static_cast<int>(expected.size()));
    for (int i = 0; i < parameters.getCountProperty(); ++i)
    {
        const EffectParameter& actual = parameters[i];
        const ExpectedParameter& wanted = expected[static_cast<std::size_t>(i)];
        EXPECT_EQ(actual.getNameProperty(), wanted.name) << "parameter " << i;
        EXPECT_EQ(actual.getParameterClassProperty(), wanted.parameterClass) << wanted.name;
        EXPECT_EQ(actual.getParameterTypeProperty(), wanted.parameterType) << wanted.name;
        EXPECT_EQ(actual.getRowCountProperty(), wanted.rows) << wanted.name;
        EXPECT_EQ(actual.getColumnCountProperty(), wanted.columns) << wanted.name;
    }
}

TEST_F(BasicEffectDefaultsTest, ParameterBackedPropertiesStaySynchronized)
{
    Texture2D texture(gd, 2, 2);
    auto& parameters = fx.getParametersProperty();

    fx.setSpecularColorProperty(Vector3{0.1f, 0.2f, 0.3f});
    EXPECT_EQ(parameters["SpecularColor"]->GetValueVector3(), Vector3(0.1f, 0.2f, 0.3f));
    parameters["SpecularColor"]->SetValue(Vector3{0.4f, 0.5f, 0.6f});
    EXPECT_EQ(fx.getSpecularColorProperty(), Vector3(0.4f, 0.5f, 0.6f));

    fx.setSpecularPowerProperty(7.5f);
    EXPECT_FLOAT_EQ(parameters["SpecularPower"]->GetValueSingle(), 7.5f);
    parameters["SpecularPower"]->SetValue(12.0f);
    EXPECT_FLOAT_EQ(fx.getSpecularPowerProperty(), 12.0f);

    fx.setFogColorProperty(Vector3{0.7f, 0.8f, 0.9f});
    EXPECT_EQ(parameters["FogColor"]->GetValueVector3(), Vector3(0.7f, 0.8f, 0.9f));
    parameters["FogColor"]->SetValue(Vector3{0.3f, 0.2f, 0.1f});
    EXPECT_EQ(fx.getFogColorProperty(), Vector3(0.3f, 0.2f, 0.1f));

    fx.setTextureProperty(&texture);
    EXPECT_EQ(parameters["Texture"]->GetValueTexture2D(), &texture);
    parameters["Texture"]->SetValue(static_cast<Texture2D*>(nullptr));
    EXPECT_EQ(fx.getTextureProperty(), nullptr);
}

TEST_F(BasicEffectDefaultsTest, ApplyPopulatesDerivedParameterValues)
{
    fx.World = Matrix::CreateTranslation(2.0f, 3.0f, 4.0f);
    fx.setDiffuseColorProperty(Vector3{0.2f, 0.4f, 0.6f});
    fx.setEmissiveColorProperty(Vector3{0.1f, 0.1f, 0.1f});
    fx.setAlphaProperty(0.5f);

    fx.Apply();

    auto& parameters = fx.getParametersProperty();
    EXPECT_EQ(parameters["WorldViewProj"]->GetValueMatrix(), fx.World);
    EXPECT_EQ(parameters["FogVector"]->GetValueVector4(), Vector4::Zero);
    const Vector4 unlitDiffuse = parameters["DiffuseColor"]->GetValueVector4();
    EXPECT_FLOAT_EQ(unlitDiffuse.X, 0.15f);
    EXPECT_FLOAT_EQ(unlitDiffuse.Y, 0.25f);
    EXPECT_FLOAT_EQ(unlitDiffuse.Z, 0.35f);
    EXPECT_FLOAT_EQ(unlitDiffuse.W, 0.5f);
    EXPECT_EQ(parameters["DirLight0Direction"]->GetValueVector3(), Vector3::Down);
    EXPECT_EQ(parameters["DirLight0DiffuseColor"]->GetValueVector3(), Vector3::One);
    EXPECT_EQ(parameters["DirLight1DiffuseColor"]->GetValueVector3(), Vector3::Zero);
    EXPECT_EQ(parameters["ShaderIndex"]->GetValueInt32(), 1);

    fx.setLightingEnabledProperty(true);
    fx.setAmbientLightColorProperty(Vector3{0.5f, 0.25f, 0.0f});
    fx.Apply();

    EXPECT_EQ(parameters["World"]->GetValueMatrix(), fx.World);
    EXPECT_EQ(parameters["EyePosition"]->GetValueVector3(), Vector3::Zero);
    const Vector4 litDiffuse = parameters["DiffuseColor"]->GetValueVector4();
    EXPECT_FLOAT_EQ(litDiffuse.X, 0.1f);
    EXPECT_FLOAT_EQ(litDiffuse.Y, 0.2f);
    EXPECT_FLOAT_EQ(litDiffuse.Z, 0.3f);
    EXPECT_FLOAT_EQ(litDiffuse.W, 0.5f);
    const Vector3 litEmissive = parameters["EmissiveColor"]->GetValueVector3();
    EXPECT_FLOAT_EQ(litEmissive.X, 0.1f);
    EXPECT_FLOAT_EQ(litEmissive.Y, 0.1f);
    EXPECT_FLOAT_EQ(litEmissive.Z, 0.05f);
    EXPECT_EQ(parameters["ShaderIndex"]->GetValueInt32(), 17);
}

// -----------------------------------------------------------------------
// plans/plan_xnb.md XNB-32: SetOwnedTexture() -- content-pipeline-loaded effects need to keep their own
// texture reference alive (matching real XNA's GC-tracked Effect.Texture), unlike
// setTextureProperty(Texture2D*)'s non-owning pointer used by Model's shared texture pool.

TEST_F(BasicEffectDefaultsTest, SetOwnedTextureKeepsTextureAliveAndVisibleThroughGetter)
{
    auto owned = std::make_shared<Texture2D>(gd, 2, 2);
    Texture2D* raw = owned.get();

    fx.SetOwnedTexture(owned);

    EXPECT_EQ(fx.getTextureProperty(), raw);
}

TEST_F(BasicEffectDefaultsTest, CloneSharesOwnedTextureOwnership)
{
    fx.SetOwnedTexture(std::make_shared<Texture2D>(gd, 2, 2));
    Texture2D* originalTexturePtr = fx.getTextureProperty();

    std::unique_ptr<Effect> cloned(fx.Clone());
    auto* clone = dynamic_cast<BasicEffect*>(cloned.get());
    ASSERT_NE(clone, nullptr);

    // Clone() shares ownership of the same underlying texture (a shallow copy of the shared_ptr),
    // not a fresh copy of the Texture2D -- matches how the raw, non-owning texture_ pointer was
    // already shared across clones before this task.
    EXPECT_EQ(clone->getTextureProperty(), originalTexturePtr);
}

// -----------------------------------------------------------------------
// Task 363: EnableDefaultLighting() exact constants, cross-checked literal-
// for-literal against FNA's Graphics/Effect/StockEffects/EffectHelpers.cs
// (EnableDefaultLighting) and BasicEffect.cs (EnableDefaultLighting, which
// additionally sets LightingEnabled = true). Complements Task 194's existing
// EasyGL integration test (modules/renderers/easygl/examples/easygl_basiceffect_default_lighting_test.cpp),
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

// -----------------------------------------------------------------------
// Task 883: Clone() — copies every property; mutating the clone must not
// affect the original and vice versa (matches the established per-stock-
// -effect Clone test pattern, e.g. AlphaTestEffectTests.cpp).

TEST_F(BasicEffectDefaultsTest, CloneCopiesAllProperties)
{
    Texture2D tex(gd, 4, 4);

    fx.World      = Matrix::getIdentityProperty() * 2.0f;
    fx.View       = Matrix::getIdentityProperty() * 3.0f;
    fx.Projection = Matrix::getIdentityProperty() * 4.0f;
    fx.VertexColorEnabled = true;
    fx.setDiffuseColorProperty(Vector3(0.1f, 0.2f, 0.3f));
    fx.setEmissiveColorProperty(Vector3(0.4f, 0.5f, 0.6f));
    fx.setSpecularColorProperty(Vector3(0.7f, 0.8f, 0.9f));
    fx.setSpecularPowerProperty(32.0f);
    fx.setAmbientLightColorProperty(Vector3(0.11f, 0.22f, 0.33f));
    fx.setAlphaProperty(0.5f);
    fx.setLightingEnabledProperty(true);
    fx.setPreferPerPixelLightingProperty(true);
    fx.setTextureEnabledProperty(true);
    fx.setTextureProperty(&tex);
    fx.setFogEnabledProperty(true);
    fx.setFogColorProperty(Vector3(0.9f, 0.8f, 0.7f));
    fx.setFogStartProperty(1.5f);
    fx.setFogEndProperty(9.5f);
    fx.DirectionalLight1.setEnabledProperty(true);
    fx.DirectionalLight2.setEnabledProperty(true);

    std::unique_ptr<Effect> cloned(fx.Clone());
    auto* clone = dynamic_cast<BasicEffect*>(cloned.get());
    ASSERT_NE(clone, nullptr);
    EXPECT_NE(static_cast<Effect*>(clone), static_cast<Effect*>(&fx));

    EXPECT_EQ(clone->World, fx.World);
    EXPECT_EQ(clone->View, fx.View);
    EXPECT_EQ(clone->Projection, fx.Projection);
    EXPECT_TRUE(clone->VertexColorEnabled);
    EXPECT_EQ(clone->getDiffuseColorProperty(), Vector3(0.1f, 0.2f, 0.3f));
    EXPECT_EQ(clone->getEmissiveColorProperty(), Vector3(0.4f, 0.5f, 0.6f));
    EXPECT_EQ(clone->getSpecularColorProperty(), Vector3(0.7f, 0.8f, 0.9f));
    EXPECT_FLOAT_EQ(clone->getSpecularPowerProperty(), 32.0f);
    EXPECT_EQ(clone->getAmbientLightColorProperty(), Vector3(0.11f, 0.22f, 0.33f));
    EXPECT_FLOAT_EQ(clone->getAlphaProperty(), 0.5f);
    EXPECT_TRUE(clone->getLightingEnabledProperty());
    EXPECT_TRUE(clone->getPreferPerPixelLightingProperty());
    EXPECT_TRUE(clone->getTextureEnabledProperty());
    EXPECT_EQ(clone->getTextureProperty(), &tex);
    EXPECT_TRUE(clone->getFogEnabledProperty());
    EXPECT_EQ(clone->getFogColorProperty(), Vector3(0.9f, 0.8f, 0.7f));
    EXPECT_FLOAT_EQ(clone->getFogStartProperty(), 1.5f);
    EXPECT_FLOAT_EQ(clone->getFogEndProperty(), 9.5f);
    EXPECT_TRUE(clone->getDirectionalLight1Property().getEnabledProperty());
    EXPECT_TRUE(clone->getDirectionalLight2Property().getEnabledProperty());

    // Independence: mutating the clone must not affect the original, and vice versa.
    clone->setAlphaProperty(0.9f);
    clone->setDiffuseColorProperty(Vector3::Zero);
    EXPECT_FLOAT_EQ(fx.getAlphaProperty(), 0.5f);
    EXPECT_EQ(fx.getDiffuseColorProperty(), Vector3(0.1f, 0.2f, 0.3f));

    fx.setAlphaProperty(0.05f);
    EXPECT_FLOAT_EQ(clone->getAlphaProperty(), 0.9f);
}
