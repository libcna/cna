// SPDX-License-Identifier: MS-PL
// PBR + skinning combo: default-value and getter/setter coverage for SkinnedPbrEffect, the new
// CNAEXT effect combining PbrEffect's own metallic-roughness BRDF with SkinnedEffect's own
// bone-transform API (no FNA/XNA equivalent to audit against -- real XNA predates PBR entirely).
// See SkinnedPbrEffect.hpp's own doc comment for the design rationale.

#include <gtest/gtest.h>
#include <memory>
#include <stdexcept>

#include "CNA/Internal/Renderers/Common/IGraphicsRenderer.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/ImageBasedLightEXT.hpp"
#include "Microsoft/Xna/Framework/Graphics/SkinnedPbrEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "Microsoft/Xna/Framework/Graphics/TextureCube.hpp"
#include "Microsoft/Xna/Framework/Graphics/TextureTransformEXT.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"

using Microsoft::Xna::Framework::Matrix;
using Microsoft::Xna::Framework::Vector2;
using Microsoft::Xna::Framework::Vector3;
using Microsoft::Xna::Framework::Graphics::GraphicsDevice;
using Microsoft::Xna::Framework::Graphics::SkinnedPbrEffect;
using Microsoft::Xna::Framework::Graphics::ImageBasedLightEXT;
using Microsoft::Xna::Framework::Graphics::SurfaceFormat;
using Microsoft::Xna::Framework::Graphics::Texture2D;
using Microsoft::Xna::Framework::Graphics::TextureCube;
using Microsoft::Xna::Framework::Graphics::TextureTransformEXT;
using CNA::Internal::Renderers::GpuDrawParams;

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

TEST_F(SkinnedPbrEffectDefaultsTest, TextureCoordinateSelectorsDefaultValidateAndReachDrawParams)
{
    EXPECT_EQ(fx.getTextureCoordinateSetsEXTProperty(), (std::array<int, 5>{0, 0, 0, 0, 0}));

    fx.setTextureCoordinateSetEXTProperty(1, 1);
    fx.setTextureCoordinateSetEXTProperty(3, 1);
    EXPECT_EQ(fx.getTextureCoordinateSetsEXTProperty(), (std::array<int, 5>{0, 1, 0, 1, 0}));

    GpuDrawParams params;
    fx.FillGpuDrawParams(params);
    EXPECT_EQ(params.pbrTextureCoordinateSetMask, 0b01010u);

    EXPECT_THROW(fx.setTextureCoordinateSetEXTProperty(-1, 0), std::out_of_range);
    EXPECT_THROW(fx.setTextureCoordinateSetEXTProperty(5, 0), std::out_of_range);
    EXPECT_THROW(fx.setTextureCoordinateSetEXTProperty(0, -1), std::out_of_range);
    EXPECT_THROW(fx.setTextureCoordinateSetEXTProperty(0, 2), std::out_of_range);
}

TEST_F(SkinnedPbrEffectDefaultsTest, TextureTransformsDefaultValidateAndReachAffineDrawParams)
{
    const TextureTransformEXT identity;
    EXPECT_EQ(fx.getTextureTransformsEXTProperty(),
              (std::array<TextureTransformEXT, 5>{identity, identity, identity, identity, identity}));

    const TextureTransformEXT transform{
        Vector2{-0.25f, 0.75f}, Vector2{4.0f, 0.5f}, 1.5707963267948966f};
    fx.setTextureTransformEXTProperty(4, transform);
    EXPECT_EQ(fx.getTextureTransformsEXTProperty()[4], transform);

    GpuDrawParams params;
    fx.FillGpuDrawParams(params);
    EXPECT_NEAR(params.pbrTextureTransformRows[8][0], 0.0f, 1e-6f);
    EXPECT_NEAR(params.pbrTextureTransformRows[8][1], -0.5f, 1e-6f);
    EXPECT_FLOAT_EQ(params.pbrTextureTransformRows[8][2], -0.25f);
    EXPECT_FLOAT_EQ(params.pbrTextureTransformRows[8][3], 0.0f);
    EXPECT_NEAR(params.pbrTextureTransformRows[9][0], 4.0f, 1e-6f);
    EXPECT_NEAR(params.pbrTextureTransformRows[9][1], 0.0f, 1e-6f);
    EXPECT_FLOAT_EQ(params.pbrTextureTransformRows[9][2], 0.75f);
    EXPECT_FLOAT_EQ(params.pbrTextureTransformRows[9][3], 0.0f);

    EXPECT_THROW(fx.setTextureTransformEXTProperty(-1, identity), std::out_of_range);
    EXPECT_THROW(fx.setTextureTransformEXTProperty(5, identity), std::out_of_range);
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

TEST_F(SkinnedPbrEffectDefaultsTest, DielectricFresnelFactorsDefaultToCoreGltf)
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

TEST_F(SkinnedPbrEffectDefaultsTest, SpecularTextureInputsHaveNoOpDefaults)
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

TEST_F(SkinnedPbrEffectDefaultsTest, IorAndSpecularFactorsRoundTripAndReachDrawParams)
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
    EXPECT_NEAR(params.pbrDielectricF0Unclamped[0], 1.0f / 36.0f, 1e-7f);
    EXPECT_NEAR(params.pbrDielectricF0Unclamped[1], 1.0f / 9.0f, 1e-7f);
    EXPECT_NEAR(params.pbrDielectricF0Unclamped[2], 4.0f / 3.0f, 1e-6f);
    EXPECT_FLOAT_EQ(params.pbrSpecularFactor, 0.3f);
}

TEST_F(SkinnedPbrEffectDefaultsTest, SpecularTextureInputsRoundTripAndReachDrawParams)
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
    fx.setIorEXTProperty(1.8f);
    fx.setSpecularFactorEXTProperty(0.4f);
    fx.setSpecularColorFactorEXTProperty(Vector3(0.2f, 0.3f, 0.4f));
    fx.setTextureCoordinateSetEXTProperty(1, 1);
    const TextureTransformEXT transform{
        Vector2{0.1f, 0.2f}, Vector2{0.3f, 0.4f}, -0.5f};
    fx.setTextureTransformEXTProperty(1, transform);
    auto specularColor = std::make_shared<Texture2D>(gd, 2, 2);
    fx.SetOwnedSpecularColorMapEXT(specularColor);
    fx.setSpecularColorTextureCoordinateSetEXTProperty(1);
    fx.setSpecularColorTextureTransformEXTProperty(transform);
    fx.setSpecularColorTextureIsSrgbEXTProperty(false);
    std::vector<Matrix> bones(SkinnedPbrEffect::MaxBones, Matrix::getIdentityProperty());
    bones[1] = Matrix::CreateTranslation(Vector3(4, 5, 6));
    fx.SetBoneTransforms(bones);

    auto* cloned = dynamic_cast<SkinnedPbrEffect*>(fx.Clone());
    ASSERT_NE(cloned, nullptr);
    EXPECT_FLOAT_EQ(cloned->getMetallicFactorProperty(), 0.3f);
    EXPECT_FLOAT_EQ(cloned->getRoughnessFactorProperty(), 0.6f);
    EXPECT_EQ(cloned->getEmissiveFactorProperty(), Vector3(0.5f, 0.5f, 0.5f));
    EXPECT_FLOAT_EQ(cloned->getIorEXTProperty(), 1.8f);
    EXPECT_FLOAT_EQ(cloned->getSpecularFactorEXTProperty(), 0.4f);
    EXPECT_EQ(cloned->getSpecularColorFactorEXTProperty(), Vector3(0.2f, 0.3f, 0.4f));
    EXPECT_EQ(cloned->getTextureCoordinateSetsEXTProperty(),
              (std::array<int, 5>{0, 1, 0, 0, 0}));
    EXPECT_EQ(cloned->getTextureTransformsEXTProperty()[1], transform);
    EXPECT_EQ(cloned->getSpecularColorMapEXTProperty(), specularColor.get());
    EXPECT_EQ(cloned->getSpecularColorTextureCoordinateSetEXTProperty(), 1);
    EXPECT_EQ(cloned->getSpecularColorTextureTransformEXTProperty(), transform);
    EXPECT_FALSE(cloned->getSpecularColorTextureIsSrgbEXTProperty());
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

TEST_F(SkinnedPbrEffectDefaultsTest, VertexColorEnabledDefaultsOffAndOverridesTheDrawParamDefault)
{
    // plan_gltf.md GLTF-465. §3.7.2.1: a COLOR_0 attribute "acts as an additional linear multiplier
    // to base color", so a renderer needs to know whether the colour slot in the vertex record means
    // anything. Since GLTF-462 the rigid PBR record (stride 60) and since GLTF-463 the skinned one
    // (stride 80) ALWAYS carry that slot -- filled with the authored colour, or with opaque white --
    // and this flag is the only thing that distinguishes the two cases at the renderer boundary.
    //
    // The two defaults deliberately disagree, and that is the property worth pinning:
    // GpuDrawParams::vertexColorEnabled defaults TRUE (BasicEffect's own XNA-visible default reaches
    // it that way), while SkinnedPbrEffect::VertexColorEnabledEXT defaults FALSE. So a PBR draw that never
    // touched this flag would inherit "enabled" from the draw-params default and multiply by whatever
    // the colour slot happened to hold -- which is exactly why FillGpuDrawParams must WRITE it rather
    // than leave it, and why this asserts the false case as hard as the true one.
    EXPECT_FALSE(fx.VertexColorEnabledEXT);

    GpuDrawParams defaults;
    EXPECT_TRUE(defaults.vertexColorEnabled)
        << "the draw-param default changed; the disagreement this test pins no longer exists";

    GpuDrawParams params;
    params.vertexColorEnabled = true;
    fx.FillGpuDrawParams(params);
    EXPECT_FALSE(params.vertexColorEnabled)
        << "the effect's own switch was left off but the draw still says the colour is enabled";

    fx.VertexColorEnabledEXT = true;
    GpuDrawParams enabled;
    enabled.vertexColorEnabled = false;
    fx.FillGpuDrawParams(enabled);
    EXPECT_TRUE(enabled.vertexColorEnabled);
}

// -----------------------------------------------------------------------
// Image-based lighting (plan_modern.md MOD-1223): the same surface PbrEffect carries, because a
// skinned character standing in a scene must be lit by the same environment as everything else.

TEST_F(SkinnedPbrEffectDefaultsTest, ImageBasedLightRoundTripsAndReachesTheDrawParams)
{
    EXPECT_FALSE(fx.getImageBasedLightEXT().IsValidEXT());

    TextureCube irradiance(gd, 4, false, SurfaceFormat::Color);
    TextureCube specular(gd, 8, true, SurfaceFormat::Color);
    Texture2D lut(gd, 8, 8);
    ImageBasedLightEXT light;
    light.Irradiance          = &irradiance;
    light.PrefilteredSpecular = &specular;
    light.BrdfLut             = &lut;
    light.PrefilteredMipCount = 5;
    light.Intensity           = 0.5f;

    fx.setAmbientLightColorProperty(Vector3(0.4f, 0.4f, 0.4f));
    fx.setImageBasedLightEXT(light);
    EXPECT_TRUE(fx.getImageBasedLightEXT().IsValidEXT());

    GpuDrawParams params;
    fx.FillGpuDrawParams(params);
    EXPECT_TRUE(params.iblEnabled);
    EXPECT_EQ(params.iblIrradiance, &irradiance.GetRenderer());
    EXPECT_EQ(params.iblPrefilteredSpecular, &specular.GetRenderer());
    EXPECT_EQ(params.iblBrdfLut, &lut.GetRenderer());
    EXPECT_EQ(params.iblPrefilteredMipCount, 5);
    EXPECT_FLOAT_EQ(params.iblIntensity, 0.5f);
    EXPECT_FLOAT_EQ(params.ambientColor[0], 0.0f);
}
