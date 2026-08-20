// SPDX-License-Identifier: MS-PL
// Task 372: exhaustive default-value coverage for every AlphaTestEffect
// property, against FNA's Graphics/Effect/StockEffects/AlphaTestEffect.cs.
// Builds on Task 371's audit, which found zero default-value bugs; this file
// is the dedicated, centralized, GTest-based lock-in for all properties'
// defaults that Task 371 itself found no existing coverage for.
//
// Task 376 extends this file with a direct, GPU-independent lock-in of the
// ReferenceAlpha 0-255-int -> 0-1-float scaling formula (a common bug
// source per plans/plan_graphics.md's own note), including out-of-range inputs.

#include <gtest/gtest.h>

#include <memory>

#include "CNA/Internal/Renderers/Common/IGraphicsRenderer.hpp"
#include "Microsoft/Xna/Framework/Graphics/AlphaTestEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/CompareFunction.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"

using Microsoft::Xna::Framework::Matrix;
using Microsoft::Xna::Framework::Vector3;
using Microsoft::Xna::Framework::Graphics::AlphaTestEffect;
using Microsoft::Xna::Framework::Graphics::CompareFunction;
using Microsoft::Xna::Framework::Graphics::GraphicsDevice;
using Microsoft::Xna::Framework::Graphics::Texture2D;
using CNA::Internal::Renderers::GpuDrawParams;

namespace
{
    class AlphaTestEffectDefaultsTest : public ::testing::Test
    {
    protected:
        GraphicsDevice gd;
        AlphaTestEffect fx{gd};
    };
}

// -----------------------------------------------------------------------
// Matrices (IEffectMatrices) — FNA: world/view/projection = Matrix.Identity

TEST_F(AlphaTestEffectDefaultsTest, WorldDefaultsToIdentity)
{
    EXPECT_EQ(fx.getWorldProperty(), Matrix::getIdentityProperty());
}

TEST_F(AlphaTestEffectDefaultsTest, ViewDefaultsToIdentity)
{
    EXPECT_EQ(fx.getViewProperty(), Matrix::getIdentityProperty());
}

TEST_F(AlphaTestEffectDefaultsTest, ProjectionDefaultsToIdentity)
{
    EXPECT_EQ(fx.getProjectionProperty(), Matrix::getIdentityProperty());
}

// -----------------------------------------------------------------------
// Material color — FNA: diffuseColor = Vector3.One, alpha = 1.

TEST_F(AlphaTestEffectDefaultsTest, DiffuseColorDefaultsToOne)
{
    EXPECT_EQ(fx.getDiffuseColorProperty(), Vector3(1.0f, 1.0f, 1.0f));
}

TEST_F(AlphaTestEffectDefaultsTest, AlphaDefaultsToOne)
{
    EXPECT_FLOAT_EQ(fx.getAlphaProperty(), 1.0f);
}

// -----------------------------------------------------------------------
// Fog (IEffectFog) — FNA: fogEnabled = false, fogStart = 0, fogEnd = 1;
// fogColor has no field initializer in FNA (backed by an EffectParameter
// whose compiled-shader default is black); CNA's fogColor_ defaults to
// Vector3::Zero, matching that same black default.

TEST_F(AlphaTestEffectDefaultsTest, FogEnabledDefaultsToFalse)
{
    EXPECT_FALSE(fx.getFogEnabledProperty());
}

TEST_F(AlphaTestEffectDefaultsTest, FogStartDefaultsToZero)
{
    EXPECT_FLOAT_EQ(fx.getFogStartProperty(), 0.0f);
}

TEST_F(AlphaTestEffectDefaultsTest, FogEndDefaultsToOne)
{
    EXPECT_FLOAT_EQ(fx.getFogEndProperty(), 1.0f);
}

TEST_F(AlphaTestEffectDefaultsTest, FogColorDefaultsToZero)
{
    EXPECT_EQ(fx.getFogColorProperty(), Vector3::Zero);
}

// -----------------------------------------------------------------------
// Texturing / vertex color — FNA: textureParam/vertexColorEnabled default to
// null/false (texture is an EffectParameter defaulting to no bound texture;
// vertexColorEnabled is a plain bool field defaulting to false).

TEST_F(AlphaTestEffectDefaultsTest, TextureDefaultsToNull)
{
    EXPECT_EQ(fx.getTextureProperty(), nullptr);
}

// plans/plan_xnb.md XNB-32: SetOwnedTexture() -- content-pipeline-loaded effects need to keep their own
// texture reference alive (matching real XNA's GC-tracked Effect.Texture), unlike
// setTextureProperty(Texture2D*)'s non-owning pointer used by Model's shared texture pool.

TEST_F(AlphaTestEffectDefaultsTest, SetOwnedTextureKeepsTextureAliveAndVisibleThroughGetter)
{
    auto owned = std::make_shared<Texture2D>(gd, 2, 2);
    Texture2D* raw = owned.get();

    fx.SetOwnedTexture(owned);

    EXPECT_EQ(fx.getTextureProperty(), raw);
}

TEST_F(AlphaTestEffectDefaultsTest, CloneSharesOwnedTextureOwnership)
{
    fx.SetOwnedTexture(std::make_shared<Texture2D>(gd, 2, 2));
    Texture2D* originalTexturePtr = fx.getTextureProperty();

    std::unique_ptr<Microsoft::Xna::Framework::Graphics::Effect> cloned(fx.Clone());
    auto* clone = dynamic_cast<AlphaTestEffect*>(cloned.get());
    ASSERT_NE(clone, nullptr);

    EXPECT_EQ(clone->getTextureProperty(), originalTexturePtr);
}

TEST_F(AlphaTestEffectDefaultsTest, VertexColorEnabledDefaultsToFalse)
{
    EXPECT_FALSE(fx.getVertexColorEnabledProperty());
}

// -----------------------------------------------------------------------
// Alpha test — FNA: alphaFunction = CompareFunction.Greater, referenceAlpha
// = 0 (C# int field default, unclamped).

TEST_F(AlphaTestEffectDefaultsTest, AlphaFunctionDefaultsToGreater)
{
    EXPECT_EQ(fx.getAlphaFunctionProperty(), CompareFunction::Greater);
}

TEST_F(AlphaTestEffectDefaultsTest, ReferenceAlphaDefaultsToZero)
{
    EXPECT_EQ(fx.getReferenceAlphaProperty(), 0);
}

// -----------------------------------------------------------------------
// Setters round-trip (every property must actually store what it's given).

TEST_F(AlphaTestEffectDefaultsTest, WorldRoundTrips)
{
    const Matrix m = Matrix::CreateTranslation(1.0f, 2.0f, 3.0f);
    fx.setWorldProperty(m);
    EXPECT_EQ(fx.getWorldProperty(), m);
}

TEST_F(AlphaTestEffectDefaultsTest, ViewRoundTrips)
{
    const Matrix m = Matrix::CreateTranslation(4.0f, 5.0f, 6.0f);
    fx.setViewProperty(m);
    EXPECT_EQ(fx.getViewProperty(), m);
}

TEST_F(AlphaTestEffectDefaultsTest, ProjectionRoundTrips)
{
    const Matrix m = Matrix::CreateTranslation(7.0f, 8.0f, 9.0f);
    fx.setProjectionProperty(m);
    EXPECT_EQ(fx.getProjectionProperty(), m);
}

TEST_F(AlphaTestEffectDefaultsTest, DiffuseColorRoundTrips)
{
    const Vector3 c(0.2f, 0.4f, 0.6f);
    fx.setDiffuseColorProperty(c);
    EXPECT_EQ(fx.getDiffuseColorProperty(), c);
}

TEST_F(AlphaTestEffectDefaultsTest, AlphaRoundTrips)
{
    fx.setAlphaProperty(0.5f);
    EXPECT_FLOAT_EQ(fx.getAlphaProperty(), 0.5f);
}

TEST_F(AlphaTestEffectDefaultsTest, FogEnabledRoundTrips)
{
    fx.setFogEnabledProperty(true);
    EXPECT_TRUE(fx.getFogEnabledProperty());
}

TEST_F(AlphaTestEffectDefaultsTest, FogStartRoundTrips)
{
    fx.setFogStartProperty(10.0f);
    EXPECT_FLOAT_EQ(fx.getFogStartProperty(), 10.0f);
}

TEST_F(AlphaTestEffectDefaultsTest, FogEndRoundTrips)
{
    fx.setFogEndProperty(20.0f);
    EXPECT_FLOAT_EQ(fx.getFogEndProperty(), 20.0f);
}

TEST_F(AlphaTestEffectDefaultsTest, FogColorRoundTrips)
{
    const Vector3 c(0.1f, 0.2f, 0.3f);
    fx.setFogColorProperty(c);
    EXPECT_EQ(fx.getFogColorProperty(), c);
}

TEST_F(AlphaTestEffectDefaultsTest, VertexColorEnabledRoundTrips)
{
    fx.setVertexColorEnabledProperty(true);
    EXPECT_TRUE(fx.getVertexColorEnabledProperty());
}

TEST_F(AlphaTestEffectDefaultsTest, AlphaFunctionRoundTrips)
{
    fx.setAlphaFunctionProperty(CompareFunction::LessEqual);
    EXPECT_EQ(fx.getAlphaFunctionProperty(), CompareFunction::LessEqual);
}

TEST_F(AlphaTestEffectDefaultsTest, ReferenceAlphaRoundTrips)
{
    fx.setReferenceAlphaProperty(128);
    EXPECT_EQ(fx.getReferenceAlphaProperty(), 128);
}

// -----------------------------------------------------------------------
// Clone — FNA's clone constructor copies every field except isEqNe (always
// safely recomputed lazily); confirm CNA's Clone() matches.

TEST_F(AlphaTestEffectDefaultsTest, CloneCopiesAllProperties)
{
    fx.setDiffuseColorProperty(Vector3(0.1f, 0.2f, 0.3f));
    fx.setAlphaProperty(0.7f);
    fx.setFogEnabledProperty(true);
    fx.setFogStartProperty(1.0f);
    fx.setFogEndProperty(2.0f);
    fx.setFogColorProperty(Vector3(0.4f, 0.5f, 0.6f));
    fx.setVertexColorEnabledProperty(true);
    fx.setAlphaFunctionProperty(CompareFunction::Equal);
    fx.setReferenceAlphaProperty(200);

    std::unique_ptr<Microsoft::Xna::Framework::Graphics::Effect> cloned(fx.Clone());
    auto* clone = dynamic_cast<AlphaTestEffect*>(cloned.get());
    ASSERT_NE(clone, nullptr);

    EXPECT_EQ(clone->getDiffuseColorProperty(), Vector3(0.1f, 0.2f, 0.3f));
    EXPECT_FLOAT_EQ(clone->getAlphaProperty(), 0.7f);
    EXPECT_TRUE(clone->getFogEnabledProperty());
    EXPECT_FLOAT_EQ(clone->getFogStartProperty(), 1.0f);
    EXPECT_FLOAT_EQ(clone->getFogEndProperty(), 2.0f);
    EXPECT_EQ(clone->getFogColorProperty(), Vector3(0.4f, 0.5f, 0.6f));
    EXPECT_TRUE(clone->getVertexColorEnabledProperty());
    EXPECT_EQ(clone->getAlphaFunctionProperty(), CompareFunction::Equal);
    EXPECT_EQ(clone->getReferenceAlphaProperty(), 200);
}

// -----------------------------------------------------------------------
// GetTypeName — CNAEXT extension, reports the fully-qualified FNA type name.

TEST_F(AlphaTestEffectDefaultsTest, GetTypeNameReturnsFullyQualifiedName)
{
    EXPECT_EQ(fx.GetTypeName(), "Microsoft.Xna.Framework.Graphics.AlphaTestEffect");
}

// -----------------------------------------------------------------------
// Task 376: ReferenceAlpha 0-255-int -> 0-1-float scaling, direct on
// FillGpuDrawParams()'s alphaTest[0]/[1] output — no GPU/pixel readback
// needed, this is pure CPU-side arithmetic. FNA's AlphaTestEffect.cs
// OnApply(): `reference = (float)referenceAlpha / 255f`, `threshold =
// 0.5f / 255f`. CompareFunction::Greater puts `reference+threshold` in X
// (Y unused); CompareFunction::Equal puts plain `reference` in X and
// `threshold` in Y. Both switch-case shapes are exercised below. FNA's own
// `referenceAlpha` field is a plain, unclamped C# int — CNA's
// `referenceAlpha_` must behave identically for out-of-range inputs.

namespace
{
    constexpr float kThreshold = 0.5f / 255.0f;

    float ExpectedReference(int referenceAlpha)
    {
        return static_cast<float>(referenceAlpha) / 255.0f;
    }
}

class AlphaTestReferenceScalingTest : public ::testing::TestWithParam<int>
{
protected:
    GraphicsDevice gd;
    AlphaTestEffect fx{gd};
};

TEST_P(AlphaTestReferenceScalingTest, GreaterPutsReferencePlusThresholdInX)
{
    const int referenceAlpha = GetParam();
    fx.setAlphaFunctionProperty(CompareFunction::Greater);
    fx.setReferenceAlphaProperty(referenceAlpha);

    GpuDrawParams params;
    fx.FillGpuDrawParams(params);

    EXPECT_NEAR(params.alphaTest[0], ExpectedReference(referenceAlpha) + kThreshold, 1e-6f);
    EXPECT_FLOAT_EQ(params.alphaTest[1], 0.0f);
}

TEST_P(AlphaTestReferenceScalingTest, EqualPutsPlainReferenceInXAndThresholdInY)
{
    const int referenceAlpha = GetParam();
    fx.setAlphaFunctionProperty(CompareFunction::Equal);
    fx.setReferenceAlphaProperty(referenceAlpha);

    GpuDrawParams params;
    fx.FillGpuDrawParams(params);

    EXPECT_NEAR(params.alphaTest[0], ExpectedReference(referenceAlpha), 1e-6f);
    EXPECT_NEAR(params.alphaTest[1], kThreshold, 1e-6f);
}

// Boundary values (0, 1, 254, 255) and out-of-range values (-10, 300) — FNA
// never clamps ReferenceAlpha, so CNA must not either (Task 371 finding).
INSTANTIATE_TEST_SUITE_P(BoundaryAndOutOfRange, AlphaTestReferenceScalingTest,
    ::testing::Values(-10, 0, 1, 64, 128, 254, 255, 300));

TEST_F(AlphaTestEffectDefaultsTest, ReferenceAlphaIsNotClampedOnSet)
{
    fx.setReferenceAlphaProperty(-10);
    EXPECT_EQ(fx.getReferenceAlphaProperty(), -10);

    fx.setReferenceAlphaProperty(300);
    EXPECT_EQ(fx.getReferenceAlphaProperty(), 300);
}
