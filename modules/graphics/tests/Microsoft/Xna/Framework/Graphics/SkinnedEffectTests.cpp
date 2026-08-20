// SPDX-License-Identifier: MS-PL
// Task 402: exhaustive default-value coverage for every SkinnedEffect
// property, against FNA's Graphics/Effect/StockEffects/SkinnedEffect.cs.
// Builds on Task 401's audit, which found zero default-value bugs but a
// real Clone()-drops-SpecularColor/SpecularPower bug (fixed in that same
// task); this file is the dedicated, centralized, GTest-based lock-in for
// all properties' defaults and the regression guard for that fix.

#include <gtest/gtest.h>

#include <memory>
#include <stdexcept>
#include <vector>

#include "CNA/Internal/Renderers/Common/IGraphicsRenderer.hpp"
#include "Microsoft/Xna/Framework/Graphics/DirectionalLight.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/IEffectLights.hpp"
#include "Microsoft/Xna/Framework/Graphics/SkinnedEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"

using CNA::Internal::Renderers::GpuDrawParams;
using Microsoft::Xna::Framework::Matrix;
using Microsoft::Xna::Framework::Vector3;
using Microsoft::Xna::Framework::Graphics::DirectionalLight;
using Microsoft::Xna::Framework::Graphics::GraphicsDevice;
using Microsoft::Xna::Framework::Graphics::IEffectLights;
using Microsoft::Xna::Framework::Graphics::SkinnedEffect;
using Microsoft::Xna::Framework::Graphics::Texture2D;

namespace
{
    class SkinnedEffectDefaultsTest : public ::testing::Test
    {
    protected:
        GraphicsDevice gd;
        SkinnedEffect fx{gd};
    };
}

// -----------------------------------------------------------------------
// Matrices (IEffectMatrices) — FNA: world/view/projection = Matrix.Identity

TEST_F(SkinnedEffectDefaultsTest, WorldDefaultsToIdentity)
{
    EXPECT_EQ(fx.getWorldProperty(), Matrix::getIdentityProperty());
}

TEST_F(SkinnedEffectDefaultsTest, ViewDefaultsToIdentity)
{
    EXPECT_EQ(fx.getViewProperty(), Matrix::getIdentityProperty());
}

TEST_F(SkinnedEffectDefaultsTest, ProjectionDefaultsToIdentity)
{
    EXPECT_EQ(fx.getProjectionProperty(), Matrix::getIdentityProperty());
}

// -----------------------------------------------------------------------
// Material color — FNA: diffuseColor=Vector3.One, emissiveColor=Zero,
// alpha=1, ambientLightColor=Zero. SpecularColor/SpecularPower are
// constructor-assigned (One / 16), not just class-field defaults.

TEST_F(SkinnedEffectDefaultsTest, DiffuseColorDefaultsToOne)
{
    EXPECT_EQ(fx.getDiffuseColorProperty(), Vector3(1.0f, 1.0f, 1.0f));
}

TEST_F(SkinnedEffectDefaultsTest, EmissiveColorDefaultsToZero)
{
    EXPECT_EQ(fx.getEmissiveColorProperty(), Vector3::Zero);
}

TEST_F(SkinnedEffectDefaultsTest, SpecularColorDefaultsToOne)
{
    EXPECT_EQ(fx.getSpecularColorProperty(), Vector3(1.0f, 1.0f, 1.0f));
}

TEST_F(SkinnedEffectDefaultsTest, SpecularPowerDefaultsTo16)
{
    EXPECT_FLOAT_EQ(fx.getSpecularPowerProperty(), 16.0f);
}

TEST_F(SkinnedEffectDefaultsTest, AlphaDefaultsToOne)
{
    EXPECT_FLOAT_EQ(fx.getAlphaProperty(), 1.0f);
}

TEST_F(SkinnedEffectDefaultsTest, AmbientLightColorDefaultsToZero)
{
    EXPECT_EQ(fx.getAmbientLightColorProperty(), Vector3::Zero);
}

TEST_F(SkinnedEffectDefaultsTest, PreferPerPixelLightingDefaultsToFalse)
{
    EXPECT_FALSE(fx.getPreferPerPixelLightingProperty());
}

// -----------------------------------------------------------------------
// Lighting — FNA's SkinnedEffect hard-locks LightingEnabled=true via an
// explicit IEffectLights interface implementation that throws on `false`.

TEST_F(SkinnedEffectDefaultsTest, LightingEnabledIsAlwaysTrue)
{
    EXPECT_TRUE(fx.getLightingEnabledProperty());
}

TEST_F(SkinnedEffectDefaultsTest, SetLightingEnabledFalseThrows)
{
    EXPECT_THROW(fx.setLightingEnabledProperty(false), std::runtime_error);
}

TEST_F(SkinnedEffectDefaultsTest, SetLightingEnabledTrueDoesNotThrow)
{
    EXPECT_NO_THROW(fx.setLightingEnabledProperty(true));
    EXPECT_TRUE(fx.getLightingEnabledProperty());
}

// REMED-GFX-088: FNA implements SkinnedEffect's LightingEnabled member explicitly
// through IEffectLights. Lock the contract at that same public-interface boundary:
// unlike BasicEffect, SkinnedEffect has no observable unlit rendering state.
TEST_F(SkinnedEffectDefaultsTest, LightingEnabledFalseThrowsThroughIEffectLights)
{
    IEffectLights& lights = fx;

    EXPECT_TRUE(lights.getLightingEnabledProperty());
    EXPECT_THROW(lights.setLightingEnabledProperty(false), std::runtime_error);
    EXPECT_TRUE(lights.getLightingEnabledProperty());
}

// REMED-GFX-088: direct renderer tests must use the state tuple that the public
// SkinnedEffect can actually produce. FNA's always-lit material upload is:
//   DiffuseShader  = DiffuseColor * Alpha
//   EmissiveShader = (EmissiveColor + AmbientLightColor * DiffuseColor) * Alpha
// Directional lights are separately enabled-gated. There is deliberately no
// LightingEnabled=false / BasicEffect-style unlit material preparation here.
TEST_F(SkinnedEffectDefaultsTest, FillGpuDrawParamsUsesAlwaysLitPrefoldedMaterialContract)
{
    Texture2D texture(gd, 1, 1);
    fx.setTextureProperty(&texture);
    fx.setDiffuseColorProperty(Vector3(0.5f, 0.25f, 0.75f));
    fx.setEmissiveColorProperty(Vector3(0.125f, 0.25f, 0.0625f));
    fx.setAmbientLightColorProperty(Vector3(0.25f, 0.5f, 0.125f));
    fx.setAlphaProperty(0.5f);

    // Give every light non-zero public colors, then disable it. The GPU-facing
    // contribution must be zero while the always-lit permutation remains selected.
    fx.DirectionalLight0.setDiffuseColorProperty(Vector3(0.75f, 0.5f, 0.25f));
    fx.DirectionalLight0.setSpecularColorProperty(Vector3(0.25f, 0.5f, 0.75f));
    fx.DirectionalLight0.setEnabledProperty(false);
    fx.DirectionalLight1.setDiffuseColorProperty(Vector3(0.125f, 0.25f, 0.5f));
    fx.DirectionalLight1.setSpecularColorProperty(Vector3(0.5f, 0.25f, 0.125f));
    fx.DirectionalLight1.setEnabledProperty(false);
    fx.DirectionalLight2.setDiffuseColorProperty(Vector3(0.375f, 0.625f, 0.875f));
    fx.DirectionalLight2.setSpecularColorProperty(Vector3(0.875f, 0.625f, 0.375f));
    fx.DirectionalLight2.setEnabledProperty(false);

    GpuDrawParams params;
    fx.FillGpuDrawParams(params);

    EXPECT_TRUE(params.skinned);
    EXPECT_TRUE(params.textureEnabled);
    EXPECT_TRUE(params.lightingEnabled);
    EXPECT_EQ(params.texture0, &texture.GetRenderer());

    EXPECT_FLOAT_EQ(params.diffuseColor[0], 0.25f);
    EXPECT_FLOAT_EQ(params.diffuseColor[1], 0.125f);
    EXPECT_FLOAT_EQ(params.diffuseColor[2], 0.375f);
    EXPECT_FLOAT_EQ(params.diffuseColor[3], 0.5f);

    EXPECT_FLOAT_EQ(params.emissiveColor[0], 0.125f);
    EXPECT_FLOAT_EQ(params.emissiveColor[1], 0.1875f);
    EXPECT_FLOAT_EQ(params.emissiveColor[2], 0.078125f);

    for (int channel = 0; channel < 3; ++channel)
    {
        EXPECT_FLOAT_EQ(params.light0Diffuse[channel], 0.0f);
        EXPECT_FLOAT_EQ(params.light1Diffuse[channel], 0.0f);
        EXPECT_FLOAT_EQ(params.light2Diffuse[channel], 0.0f);
        EXPECT_FLOAT_EQ(params.light0Specular[channel], 0.0f);
        EXPECT_FLOAT_EQ(params.light1Specular[channel], 0.0f);
        EXPECT_FLOAT_EQ(params.light2Specular[channel], 0.0f);
    }
}

// -----------------------------------------------------------------------
// Directional lights — FNA's constructor explicitly sets
// DirectionalLight0.Enabled=true; DirectionalLight1/2 stay at
// DirectionalLight's own class defaults (Enabled=false).

TEST_F(SkinnedEffectDefaultsTest, DirectionalLight0EnabledDefaultsToTrue)
{
    EXPECT_TRUE(fx.getDirectionalLight0Property().getEnabledProperty());
}

TEST_F(SkinnedEffectDefaultsTest, DirectionalLight1EnabledDefaultsToFalse)
{
    EXPECT_FALSE(fx.getDirectionalLight1Property().getEnabledProperty());
}

TEST_F(SkinnedEffectDefaultsTest, DirectionalLight2EnabledDefaultsToFalse)
{
    EXPECT_FALSE(fx.getDirectionalLight2Property().getEnabledProperty());
}

TEST_F(SkinnedEffectDefaultsTest, EnableDefaultLightingSetsAmbientLightColor)
{
    fx.EnableDefaultLighting();
    const Vector3 ambient = fx.getAmbientLightColorProperty();
    EXPECT_NEAR(ambient.X, 0.05333332f, 1e-6f);
    EXPECT_NEAR(ambient.Y, 0.09882354f, 1e-6f);
    EXPECT_NEAR(ambient.Z, 0.1819608f, 1e-6f);
}

TEST_F(SkinnedEffectDefaultsTest, EnableDefaultLightingSetsAllThreeLightsEnabled)
{
    fx.EnableDefaultLighting();
    EXPECT_TRUE(fx.getDirectionalLight0Property().getEnabledProperty());
    EXPECT_TRUE(fx.getDirectionalLight1Property().getEnabledProperty());
    EXPECT_TRUE(fx.getDirectionalLight2Property().getEnabledProperty());
}

// -----------------------------------------------------------------------
// Fog (IEffectFog) — FNA: fogEnabled=false, fogStart=0, fogEnd=1; fogColor
// backed by an EffectParameter defaulting to black.

TEST_F(SkinnedEffectDefaultsTest, FogEnabledDefaultsToFalse)
{
    EXPECT_FALSE(fx.getFogEnabledProperty());
}

TEST_F(SkinnedEffectDefaultsTest, FogStartDefaultsToZero)
{
    EXPECT_FLOAT_EQ(fx.getFogStartProperty(), 0.0f);
}

TEST_F(SkinnedEffectDefaultsTest, FogEndDefaultsToOne)
{
    EXPECT_FLOAT_EQ(fx.getFogEndProperty(), 1.0f);
}

TEST_F(SkinnedEffectDefaultsTest, FogColorDefaultsToZero)
{
    EXPECT_EQ(fx.getFogColorProperty(), Vector3::Zero);
}

// -----------------------------------------------------------------------
// Texture — FNA: Texture defaults to null.

TEST_F(SkinnedEffectDefaultsTest, TextureDefaultsToNull)
{
    EXPECT_EQ(fx.getTextureProperty(), nullptr);
}

// plans/plan_xnb.md XNB-32: SetOwnedTexture() -- content-pipeline-loaded effects need to keep their own
// texture reference alive (matching real XNA's GC-tracked Effect.Texture), unlike
// setTextureProperty(Texture2D*)'s non-owning pointer used by Model's shared texture pool.

TEST_F(SkinnedEffectDefaultsTest, SetOwnedTextureKeepsTextureAliveAndVisibleThroughGetter)
{
    auto owned = std::make_shared<Texture2D>(gd, 2, 2);
    Texture2D* raw = owned.get();

    fx.SetOwnedTexture(owned);

    EXPECT_EQ(fx.getTextureProperty(), raw);
}

TEST_F(SkinnedEffectDefaultsTest, CloneSharesOwnedTextureOwnership)
{
    fx.SetOwnedTexture(std::make_shared<Texture2D>(gd, 2, 2));
    Texture2D* originalTexturePtr = fx.getTextureProperty();

    std::unique_ptr<Microsoft::Xna::Framework::Graphics::Effect> cloned(fx.Clone());
    auto* clone = dynamic_cast<SkinnedEffect*>(cloned.get());
    ASSERT_NE(clone, nullptr);

    EXPECT_EQ(clone->getTextureProperty(), originalTexturePtr);
}

// -----------------------------------------------------------------------
// Skinning — FNA: MaxBones=72 (constant), WeightsPerVertex=4, constructor
// initializes all 72 bone slots to Matrix.Identity via SetBoneTransforms.

TEST_F(SkinnedEffectDefaultsTest, MaxBonesIs72)
{
    EXPECT_EQ(SkinnedEffect::MaxBones, 72);
}

TEST_F(SkinnedEffectDefaultsTest, WeightsPerVertexDefaultsToFour)
{
    EXPECT_EQ(fx.getWeightsPerVertexProperty(), 4);
}

TEST_F(SkinnedEffectDefaultsTest, BoneTransformsDefaultToIdentity)
{
    const std::vector<Matrix> bones = fx.GetBoneTransforms(SkinnedEffect::MaxBones);
    ASSERT_EQ(bones.size(), static_cast<size_t>(SkinnedEffect::MaxBones));
    for (const Matrix& m : bones)
        EXPECT_EQ(m, Matrix::getIdentityProperty());
}

// Task 404: FNA's GetBoneTransforms(count) calls bonesParam.GetValueMatrixArray(count),
// which allocates a fresh Matrix[] each call rather than aliasing the parameter's internal
// storage -- mutating the returned array must not affect the effect's actual bone state.
TEST_F(SkinnedEffectDefaultsTest, GetBoneTransformsReturnsIndependentCopy)
{
    std::vector<Matrix> first = fx.GetBoneTransforms(SkinnedEffect::MaxBones);
    ASSERT_FALSE(first.empty());
    first[0] = Matrix::CreateTranslation(100.0f, 200.0f, 300.0f);

    const std::vector<Matrix> second = fx.GetBoneTransforms(SkinnedEffect::MaxBones);
    EXPECT_EQ(second[0], Matrix::getIdentityProperty());
}

TEST_F(SkinnedEffectDefaultsTest, SetBoneTransformsThrowsOnEmpty)
{
    EXPECT_THROW(fx.SetBoneTransforms(std::vector<Matrix>{}), std::invalid_argument);
}

TEST_F(SkinnedEffectDefaultsTest, SetBoneTransformsThrowsWhenExceedingMaxBones)
{
    std::vector<Matrix> tooMany(SkinnedEffect::MaxBones + 1, Matrix::getIdentityProperty());
    EXPECT_THROW(fx.SetBoneTransforms(tooMany), std::invalid_argument);
}

TEST_F(SkinnedEffectDefaultsTest, SetBoneTransformsAcceptsExactlyMaxBones)
{
    std::vector<Matrix> exact(SkinnedEffect::MaxBones, Matrix::CreateTranslation(1.0f, 0.0f, 0.0f));
    EXPECT_NO_THROW(fx.SetBoneTransforms(exact));
}

TEST_F(SkinnedEffectDefaultsTest, GetBoneTransformsThrowsOnZeroCount)
{
    EXPECT_THROW((void)fx.GetBoneTransforms(0), std::out_of_range);
}

TEST_F(SkinnedEffectDefaultsTest, GetBoneTransformsThrowsOnNegativeCount)
{
    EXPECT_THROW((void)fx.GetBoneTransforms(-1), std::out_of_range);
}

TEST_F(SkinnedEffectDefaultsTest, GetBoneTransformsThrowsWhenExceedingMaxBones)
{
    EXPECT_THROW((void)fx.GetBoneTransforms(SkinnedEffect::MaxBones + 1), std::out_of_range);
}

TEST_F(SkinnedEffectDefaultsTest, WeightsPerVertexAcceptsOneTwoAndFour)
{
    EXPECT_NO_THROW(fx.setWeightsPerVertexProperty(1));
    EXPECT_EQ(fx.getWeightsPerVertexProperty(), 1);
    EXPECT_NO_THROW(fx.setWeightsPerVertexProperty(2));
    EXPECT_EQ(fx.getWeightsPerVertexProperty(), 2);
    EXPECT_NO_THROW(fx.setWeightsPerVertexProperty(4));
    EXPECT_EQ(fx.getWeightsPerVertexProperty(), 4);
}

TEST_F(SkinnedEffectDefaultsTest, WeightsPerVertexThrowsOnInvalidValue)
{
    EXPECT_THROW(fx.setWeightsPerVertexProperty(3), std::out_of_range);
    EXPECT_THROW(fx.setWeightsPerVertexProperty(0), std::out_of_range);
}

// -----------------------------------------------------------------------
// Setters round-trip (every property must actually store what it's given).

TEST_F(SkinnedEffectDefaultsTest, WorldRoundTrips)
{
    const Matrix m = Matrix::CreateTranslation(1.0f, 2.0f, 3.0f);
    fx.setWorldProperty(m);
    EXPECT_EQ(fx.getWorldProperty(), m);
}

TEST_F(SkinnedEffectDefaultsTest, ViewRoundTrips)
{
    const Matrix m = Matrix::CreateTranslation(4.0f, 5.0f, 6.0f);
    fx.setViewProperty(m);
    EXPECT_EQ(fx.getViewProperty(), m);
}

TEST_F(SkinnedEffectDefaultsTest, ProjectionRoundTrips)
{
    const Matrix m = Matrix::CreateTranslation(7.0f, 8.0f, 9.0f);
    fx.setProjectionProperty(m);
    EXPECT_EQ(fx.getProjectionProperty(), m);
}

TEST_F(SkinnedEffectDefaultsTest, DiffuseColorRoundTrips)
{
    const Vector3 c(0.2f, 0.4f, 0.6f);
    fx.setDiffuseColorProperty(c);
    EXPECT_EQ(fx.getDiffuseColorProperty(), c);
}

TEST_F(SkinnedEffectDefaultsTest, EmissiveColorRoundTrips)
{
    const Vector3 c(0.1f, 0.2f, 0.3f);
    fx.setEmissiveColorProperty(c);
    EXPECT_EQ(fx.getEmissiveColorProperty(), c);
}

TEST_F(SkinnedEffectDefaultsTest, SpecularColorRoundTrips)
{
    const Vector3 c(0.7f, 0.8f, 0.9f);
    fx.setSpecularColorProperty(c);
    EXPECT_EQ(fx.getSpecularColorProperty(), c);
}

TEST_F(SkinnedEffectDefaultsTest, SpecularPowerRoundTrips)
{
    fx.setSpecularPowerProperty(32.0f);
    EXPECT_FLOAT_EQ(fx.getSpecularPowerProperty(), 32.0f);
}

TEST_F(SkinnedEffectDefaultsTest, AlphaRoundTrips)
{
    fx.setAlphaProperty(0.5f);
    EXPECT_FLOAT_EQ(fx.getAlphaProperty(), 0.5f);
}

TEST_F(SkinnedEffectDefaultsTest, AmbientLightColorRoundTrips)
{
    const Vector3 c(0.3f, 0.3f, 0.3f);
    fx.setAmbientLightColorProperty(c);
    EXPECT_EQ(fx.getAmbientLightColorProperty(), c);
}

TEST_F(SkinnedEffectDefaultsTest, PreferPerPixelLightingRoundTrips)
{
    fx.setPreferPerPixelLightingProperty(true);
    EXPECT_TRUE(fx.getPreferPerPixelLightingProperty());
}

TEST_F(SkinnedEffectDefaultsTest, FogEnabledRoundTrips)
{
    fx.setFogEnabledProperty(true);
    EXPECT_TRUE(fx.getFogEnabledProperty());
}

TEST_F(SkinnedEffectDefaultsTest, FogStartRoundTrips)
{
    fx.setFogStartProperty(10.0f);
    EXPECT_FLOAT_EQ(fx.getFogStartProperty(), 10.0f);
}

TEST_F(SkinnedEffectDefaultsTest, FogEndRoundTrips)
{
    fx.setFogEndProperty(20.0f);
    EXPECT_FLOAT_EQ(fx.getFogEndProperty(), 20.0f);
}

TEST_F(SkinnedEffectDefaultsTest, FogColorRoundTrips)
{
    const Vector3 c(0.1f, 0.2f, 0.3f);
    fx.setFogColorProperty(c);
    EXPECT_EQ(fx.getFogColorProperty(), c);
}

TEST_F(SkinnedEffectDefaultsTest, TextureRoundTrips)
{
    Texture2D tex(gd, 1, 1);
    fx.setTextureProperty(&tex);
    EXPECT_EQ(fx.getTextureProperty(), &tex);
}

TEST_F(SkinnedEffectDefaultsTest, BoneTransformsRoundTrip)
{
    std::vector<Matrix> bones(SkinnedEffect::MaxBones, Matrix::getIdentityProperty());
    bones[0] = Matrix::CreateTranslation(5.0f, 6.0f, 7.0f);
    fx.SetBoneTransforms(bones);

    const std::vector<Matrix> got = fx.GetBoneTransforms(SkinnedEffect::MaxBones);
    ASSERT_EQ(got.size(), bones.size());
    EXPECT_EQ(got[0], bones[0]);
    EXPECT_EQ(got[1], Matrix::getIdentityProperty());
}

// -----------------------------------------------------------------------
// Clone — FNA's clone constructor copies preferPerPixelLighting, fogEnabled,
// world/view/projection, diffuseColor, emissiveColor, ambientLightColor,
// alpha, fogStart, fogEnd, weightsPerVertex (plus SpecularColor/
// SpecularPower/Texture/Bones, carried via FNA's native effect-data clone;
// CNA copies these explicitly instead, per Task 401's audit finding).
//
// Task 401 found and fixed a real bug here: Clone() never actually
// preserved SpecularColor/SpecularPower (the copy constructor updated a
// dead cache field instead of the backing EffectParameter) -- this test
// is the regression guard for that fix.

TEST_F(SkinnedEffectDefaultsTest, CloneCopiesAllProperties)
{
    Texture2D tex(gd, 1, 1);

    fx.setDiffuseColorProperty(Vector3(0.1f, 0.2f, 0.3f));
    fx.setEmissiveColorProperty(Vector3(0.4f, 0.5f, 0.6f));
    fx.setSpecularColorProperty(Vector3(0.7f, 0.8f, 0.9f));
    fx.setSpecularPowerProperty(64.0f);
    fx.setAmbientLightColorProperty(Vector3(0.05f, 0.1f, 0.15f));
    fx.setAlphaProperty(0.7f);
    fx.setPreferPerPixelLightingProperty(true);
    fx.setFogEnabledProperty(true);
    fx.setFogStartProperty(1.0f);
    fx.setFogEndProperty(2.0f);
    fx.setFogColorProperty(Vector3(0.2f, 0.3f, 0.4f));
    fx.setTextureProperty(&tex);
    fx.setWeightsPerVertexProperty(2);

    std::unique_ptr<Microsoft::Xna::Framework::Graphics::Effect> cloned(fx.Clone());
    auto* clone = dynamic_cast<SkinnedEffect*>(cloned.get());
    ASSERT_NE(clone, nullptr);

    EXPECT_EQ(clone->getDiffuseColorProperty(), Vector3(0.1f, 0.2f, 0.3f));
    EXPECT_EQ(clone->getEmissiveColorProperty(), Vector3(0.4f, 0.5f, 0.6f));
    EXPECT_EQ(clone->getSpecularColorProperty(), Vector3(0.7f, 0.8f, 0.9f));
    EXPECT_FLOAT_EQ(clone->getSpecularPowerProperty(), 64.0f);
    EXPECT_EQ(clone->getAmbientLightColorProperty(), Vector3(0.05f, 0.1f, 0.15f));
    EXPECT_FLOAT_EQ(clone->getAlphaProperty(), 0.7f);
    EXPECT_TRUE(clone->getPreferPerPixelLightingProperty());
    EXPECT_TRUE(clone->getFogEnabledProperty());
    EXPECT_FLOAT_EQ(clone->getFogStartProperty(), 1.0f);
    EXPECT_FLOAT_EQ(clone->getFogEndProperty(), 2.0f);
    EXPECT_EQ(clone->getFogColorProperty(), Vector3(0.2f, 0.3f, 0.4f));
    EXPECT_EQ(clone->getTextureProperty(), &tex);
    EXPECT_EQ(clone->getWeightsPerVertexProperty(), 2);
}

// -----------------------------------------------------------------------
// GetTypeName — CNAEXT extension, reports the fully-qualified FNA type name.

TEST_F(SkinnedEffectDefaultsTest, GetTypeNameReturnsFullyQualifiedName)
{
    EXPECT_EQ(fx.GetTypeName(), "Microsoft.Xna.Framework.Graphics.SkinnedEffect");
}
