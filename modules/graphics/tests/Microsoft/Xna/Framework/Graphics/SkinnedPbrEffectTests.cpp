// SPDX-License-Identifier: MS-PL
// PBR + skinning combo: default-value and getter/setter coverage for SkinnedPbrEffect, the new
// NOXNA effect combining PbrEffect's own metallic-roughness BRDF with SkinnedEffect's own
// bone-transform API (no FNA/XNA equivalent to audit against -- real XNA predates PBR entirely).
// See SkinnedPbrEffect.hpp's own doc comment for the design rationale.

#include <gtest/gtest.h>
#include <stdexcept>

#include "CNA/Internal/Backends/Common/IGraphicsBackend.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/SkinnedPbrEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"

using Microsoft::Xna::Framework::Matrix;
using Microsoft::Xna::Framework::Vector3;
using Microsoft::Xna::Framework::Graphics::GraphicsDevice;
using Microsoft::Xna::Framework::Graphics::SkinnedPbrEffect;
using Microsoft::Xna::Framework::Graphics::Texture2D;
using CNA::Internal::Backends::GpuDrawParams;

namespace
{
    class SkinnedPbrEffectDefaultsTest : public ::testing::Test
    {
    protected:
        GraphicsDevice gd;
        SkinnedPbrEffect fx{gd};
    };
}

// -----------------------------------------------------------------------
// Defaults

TEST_F(SkinnedPbrEffectDefaultsTest, WorldDefaultsToIdentity)
{
    EXPECT_EQ(fx.getWorldProperty(), Matrix::getIdentityProperty());
}

TEST_F(SkinnedPbrEffectDefaultsTest, ViewDefaultsToIdentity)
{
    EXPECT_EQ(fx.getViewProperty(), Matrix::getIdentityProperty());
}

TEST_F(SkinnedPbrEffectDefaultsTest, ProjectionDefaultsToIdentity)
{
    EXPECT_EQ(fx.getProjectionProperty(), Matrix::getIdentityProperty());
}

TEST_F(SkinnedPbrEffectDefaultsTest, DiffuseColorDefaultsToOne)
{
    EXPECT_EQ(fx.getDiffuseColorProperty(), Vector3(1.0f, 1.0f, 1.0f));
}

TEST_F(SkinnedPbrEffectDefaultsTest, AlphaDefaultsToOne)
{
    EXPECT_FLOAT_EQ(fx.getAlphaProperty(), 1.0f);
}

TEST_F(SkinnedPbrEffectDefaultsTest, AmbientLightColorDefaultsToZero)
{
    EXPECT_EQ(fx.getAmbientLightColorProperty(), Vector3::Zero);
}

TEST_F(SkinnedPbrEffectDefaultsTest, LightingEnabledIsAlwaysTrue)
{
    EXPECT_TRUE(fx.getLightingEnabledProperty());
}

TEST_F(SkinnedPbrEffectDefaultsTest, SetLightingEnabledFalseThrows)
{
    EXPECT_THROW(fx.setLightingEnabledProperty(false), std::runtime_error);
}

TEST_F(SkinnedPbrEffectDefaultsTest, SetLightingEnabledTrueDoesNotThrow)
{
    EXPECT_NO_THROW(fx.setLightingEnabledProperty(true));
}

TEST_F(SkinnedPbrEffectDefaultsTest, FogEnabledDefaultsToFalse)
{
    EXPECT_FALSE(fx.getFogEnabledProperty());
}

TEST_F(SkinnedPbrEffectDefaultsTest, FogStartDefaultsToZero)
{
    EXPECT_FLOAT_EQ(fx.getFogStartProperty(), 0.0f);
}

TEST_F(SkinnedPbrEffectDefaultsTest, FogEndDefaultsToOne)
{
    EXPECT_FLOAT_EQ(fx.getFogEndProperty(), 1.0f);
}

TEST_F(SkinnedPbrEffectDefaultsTest, TextureDefaultsToNull)
{
    EXPECT_EQ(fx.getTextureProperty(), nullptr);
}

TEST_F(SkinnedPbrEffectDefaultsTest, NormalMapDefaultsToNull)
{
    EXPECT_EQ(fx.getNormalMapProperty(), nullptr);
}

TEST_F(SkinnedPbrEffectDefaultsTest, MetallicRoughnessMapDefaultsToNull)
{
    EXPECT_EQ(fx.getMetallicRoughnessMapProperty(), nullptr);
}

TEST_F(SkinnedPbrEffectDefaultsTest, EmissiveMapDefaultsToNull)
{
    EXPECT_EQ(fx.getEmissiveMapProperty(), nullptr);
}

TEST_F(SkinnedPbrEffectDefaultsTest, OcclusionMapDefaultsToNull)
{
    EXPECT_EQ(fx.getOcclusionMapProperty(), nullptr);
}

TEST_F(SkinnedPbrEffectDefaultsTest, MetallicFactorDefaultsToOne)
{
    EXPECT_FLOAT_EQ(fx.getMetallicFactorProperty(), 1.0f);
}

TEST_F(SkinnedPbrEffectDefaultsTest, RoughnessFactorDefaultsToOne)
{
    EXPECT_FLOAT_EQ(fx.getRoughnessFactorProperty(), 1.0f);
}

TEST_F(SkinnedPbrEffectDefaultsTest, EmissiveFactorDefaultsToZero)
{
    EXPECT_EQ(fx.getEmissiveFactorProperty(), Vector3::Zero);
}

TEST_F(SkinnedPbrEffectDefaultsTest, DirectionalLight0DefaultsToDisabled)
{
    EXPECT_FALSE(fx.DirectionalLight0.getEnabledProperty());
}

// -----------------------------------------------------------------------
// Setters / getters round-trip

TEST_F(SkinnedPbrEffectDefaultsTest, SetWorldRoundTrips)
{
    const Matrix m = Matrix::CreateTranslation(Vector3(1, 2, 3));
    fx.setWorldProperty(m);
    EXPECT_EQ(fx.getWorldProperty(), m);
}

TEST_F(SkinnedPbrEffectDefaultsTest, SetDiffuseColorRoundTrips)
{
    fx.setDiffuseColorProperty(Vector3(0.2f, 0.4f, 0.6f));
    EXPECT_EQ(fx.getDiffuseColorProperty(), Vector3(0.2f, 0.4f, 0.6f));
}

TEST_F(SkinnedPbrEffectDefaultsTest, SetMetallicFactorRoundTrips)
{
    fx.setMetallicFactorProperty(0.25f);
    EXPECT_FLOAT_EQ(fx.getMetallicFactorProperty(), 0.25f);
}

TEST_F(SkinnedPbrEffectDefaultsTest, SetRoughnessFactorRoundTrips)
{
    fx.setRoughnessFactorProperty(0.75f);
    EXPECT_FLOAT_EQ(fx.getRoughnessFactorProperty(), 0.75f);
}

TEST_F(SkinnedPbrEffectDefaultsTest, SetEmissiveFactorRoundTrips)
{
    fx.setEmissiveFactorProperty(Vector3(0.1f, 0.2f, 0.3f));
    EXPECT_EQ(fx.getEmissiveFactorProperty(), Vector3(0.1f, 0.2f, 0.3f));
}

TEST_F(SkinnedPbrEffectDefaultsTest, SetTextureRoundTrips)
{
    const std::vector<std::uint8_t> px = {255, 255, 255, 255};
    Texture2D tex = Texture2D::CreateFromPixels(gd, 1, 1, px);
    fx.setTextureProperty(&tex);
    EXPECT_EQ(fx.getTextureProperty(), &tex);
}

TEST_F(SkinnedPbrEffectDefaultsTest, SetNormalMapRoundTrips)
{
    const std::vector<std::uint8_t> px = {128, 128, 255, 255};
    Texture2D tex = Texture2D::CreateFromPixels(gd, 1, 1, px);
    fx.setNormalMapProperty(&tex);
    EXPECT_EQ(fx.getNormalMapProperty(), &tex);
}

TEST_F(SkinnedPbrEffectDefaultsTest, EnableDefaultLightingEnablesAllThreeLightsAndSetsAmbient)
{
    fx.EnableDefaultLighting();
    EXPECT_TRUE(fx.DirectionalLight0.getEnabledProperty());
    EXPECT_TRUE(fx.DirectionalLight1.getEnabledProperty());
    EXPECT_TRUE(fx.DirectionalLight2.getEnabledProperty());
    EXPECT_NE(fx.getAmbientLightColorProperty(), Vector3::Zero);
}

// -----------------------------------------------------------------------
// Skinning — MaxBones=72, WeightsPerVertex=4, constructor initializes all 72
// bone slots to Matrix.Identity via SetBoneTransforms (mirrors SkinnedEffect's own tests).

TEST_F(SkinnedPbrEffectDefaultsTest, MaxBonesIs72)
{
    EXPECT_EQ(SkinnedPbrEffect::MaxBones, 72);
}

TEST_F(SkinnedPbrEffectDefaultsTest, WeightsPerVertexDefaultsToFour)
{
    EXPECT_EQ(fx.getWeightsPerVertexProperty(), 4);
}

TEST_F(SkinnedPbrEffectDefaultsTest, BoneTransformsDefaultToIdentity)
{
    const std::vector<Matrix> bones = fx.GetBoneTransforms(SkinnedPbrEffect::MaxBones);
    ASSERT_EQ(bones.size(), static_cast<size_t>(SkinnedPbrEffect::MaxBones));
    for (const Matrix& m : bones)
    {
        EXPECT_EQ(m, Matrix::getIdentityProperty());
    }
}

TEST_F(SkinnedPbrEffectDefaultsTest, GetBoneTransformsReturnsIndependentCopy)
{
    std::vector<Matrix> first = fx.GetBoneTransforms(SkinnedPbrEffect::MaxBones);
    first[0] = Matrix::CreateTranslation(Vector3(9, 9, 9));

    const std::vector<Matrix> second = fx.GetBoneTransforms(SkinnedPbrEffect::MaxBones);
    EXPECT_EQ(second[0], Matrix::getIdentityProperty());
}

TEST_F(SkinnedPbrEffectDefaultsTest, SetBoneTransformsThrowsOnEmpty)
{
    EXPECT_THROW(fx.SetBoneTransforms(std::vector<Matrix>{}), std::invalid_argument);
}

TEST_F(SkinnedPbrEffectDefaultsTest, SetBoneTransformsThrowsWhenExceedingMaxBones)
{
    std::vector<Matrix> tooMany(SkinnedPbrEffect::MaxBones + 1, Matrix::getIdentityProperty());
    EXPECT_THROW(fx.SetBoneTransforms(tooMany), std::invalid_argument);
}

TEST_F(SkinnedPbrEffectDefaultsTest, SetBoneTransformsAcceptsExactlyMaxBones)
{
    std::vector<Matrix> exact(SkinnedPbrEffect::MaxBones, Matrix::CreateTranslation(1.0f, 0.0f, 0.0f));
    EXPECT_NO_THROW(fx.SetBoneTransforms(exact));
}

TEST_F(SkinnedPbrEffectDefaultsTest, GetBoneTransformsThrowsOnZeroCount)
{
    EXPECT_THROW((void)fx.GetBoneTransforms(0), std::out_of_range);
}

TEST_F(SkinnedPbrEffectDefaultsTest, GetBoneTransformsThrowsOnNegativeCount)
{
    EXPECT_THROW((void)fx.GetBoneTransforms(-1), std::out_of_range);
}

TEST_F(SkinnedPbrEffectDefaultsTest, GetBoneTransformsThrowsWhenExceedingMaxBones)
{
    EXPECT_THROW((void)fx.GetBoneTransforms(SkinnedPbrEffect::MaxBones + 1), std::out_of_range);
}

TEST_F(SkinnedPbrEffectDefaultsTest, WeightsPerVertexAcceptsOneTwoAndFour)
{
    EXPECT_NO_THROW(fx.setWeightsPerVertexProperty(1));
    EXPECT_EQ(fx.getWeightsPerVertexProperty(), 1);
    EXPECT_NO_THROW(fx.setWeightsPerVertexProperty(2));
    EXPECT_EQ(fx.getWeightsPerVertexProperty(), 2);
    EXPECT_NO_THROW(fx.setWeightsPerVertexProperty(4));
    EXPECT_EQ(fx.getWeightsPerVertexProperty(), 4);
}

TEST_F(SkinnedPbrEffectDefaultsTest, WeightsPerVertexThrowsOnInvalidValue)
{
    EXPECT_THROW(fx.setWeightsPerVertexProperty(3), std::out_of_range);
    EXPECT_THROW(fx.setWeightsPerVertexProperty(0), std::out_of_range);
}

TEST_F(SkinnedPbrEffectDefaultsTest, BoneTransformsRoundTrip)
{
    std::vector<Matrix> bones(SkinnedPbrEffect::MaxBones, Matrix::getIdentityProperty());
    bones[5] = Matrix::CreateTranslation(Vector3(1, 2, 3));
    fx.SetBoneTransforms(bones);

    const std::vector<Matrix> got = fx.GetBoneTransforms(SkinnedPbrEffect::MaxBones);
    EXPECT_EQ(got[5], Matrix::CreateTranslation(Vector3(1, 2, 3)));
    EXPECT_EQ(got[0], Matrix::getIdentityProperty());
}

// -----------------------------------------------------------------------
// Clone

TEST_F(SkinnedPbrEffectDefaultsTest, CloneCopiesMaterialAndBoneState)
{
    fx.setMetallicFactorProperty(0.3f);
    fx.setRoughnessFactorProperty(0.6f);
    fx.setEmissiveFactorProperty(Vector3(0.5f, 0.5f, 0.5f));
    std::vector<Matrix> bones(SkinnedPbrEffect::MaxBones, Matrix::getIdentityProperty());
    bones[1] = Matrix::CreateTranslation(Vector3(4, 5, 6));
    fx.SetBoneTransforms(bones);

    auto* cloned = dynamic_cast<SkinnedPbrEffect*>(fx.Clone());
    ASSERT_NE(cloned, nullptr);
    EXPECT_FLOAT_EQ(cloned->getMetallicFactorProperty(), 0.3f);
    EXPECT_FLOAT_EQ(cloned->getRoughnessFactorProperty(), 0.6f);
    EXPECT_EQ(cloned->getEmissiveFactorProperty(), Vector3(0.5f, 0.5f, 0.5f));
    const std::vector<Matrix> clonedBones = cloned->GetBoneTransforms(SkinnedPbrEffect::MaxBones);
    EXPECT_EQ(clonedBones[1], Matrix::CreateTranslation(Vector3(4, 5, 6)));
    delete cloned;
}

// -----------------------------------------------------------------------
// GetTypeName / FillGpuDrawParams

TEST_F(SkinnedPbrEffectDefaultsTest, GetTypeNameIsCorrect)
{
    EXPECT_EQ(fx.GetTypeName(), "Microsoft.Xna.Framework.Graphics.SkinnedPbrEffect");
}

TEST_F(SkinnedPbrEffectDefaultsTest, FillGpuDrawParamsSetsThePbrAndSkinnedFlags)
{
    GpuDrawParams params;
    fx.FillGpuDrawParams(params);
    EXPECT_TRUE(params.pbr);
    EXPECT_TRUE(params.skinned);
}

TEST_F(SkinnedPbrEffectDefaultsTest, FillGpuDrawParamsCarriesFactorsFogAndBoneCount)
{
    fx.setMetallicFactorProperty(0.2f);
    fx.setRoughnessFactorProperty(0.9f);
    fx.setFogEnabledProperty(true);
    fx.setFogStartProperty(2.0f);
    fx.setFogEndProperty(8.0f);
    fx.setWeightsPerVertexProperty(2);

    GpuDrawParams params;
    fx.FillGpuDrawParams(params);
    EXPECT_FLOAT_EQ(params.pbrMetallicFactor, 0.2f);
    EXPECT_FLOAT_EQ(params.pbrRoughnessFactor, 0.9f);
    EXPECT_TRUE(params.fogEnabled);
    EXPECT_FLOAT_EQ(params.fogVector[0], 0.0f);
    EXPECT_FLOAT_EQ(params.fogVector[1], 0.0f);
    EXPECT_FLOAT_EQ(params.fogVector[2], -1.0f / 6.0f);
    EXPECT_FLOAT_EQ(params.fogVector[3], -1.0f / 3.0f);
    EXPECT_EQ(params.boneCount, SkinnedPbrEffect::MaxBones);
    EXPECT_EQ(params.weightsPerVertex, 2);
}
