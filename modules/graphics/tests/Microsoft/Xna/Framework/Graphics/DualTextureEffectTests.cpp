// SPDX-License-Identifier: MS-PL
// Task 382: exhaustive default-value coverage for every DualTextureEffect
// property, against FNA's Graphics/Effect/StockEffects/DualTextureEffect.cs.
// Builds on Task 381's audit, which found zero default-value bugs; this file
// is the dedicated, centralized, GTest-based lock-in for all properties'
// defaults that Task 381 itself found no existing coverage for.

#include <gtest/gtest.h>

#include <memory>

#include "CNA/Internal/Renderers/Common/IGraphicsRenderer.hpp"
#include "Microsoft/Xna/Framework/Graphics/DualTextureEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"

using Microsoft::Xna::Framework::Matrix;
using Microsoft::Xna::Framework::Vector3;
using Microsoft::Xna::Framework::Graphics::DualTextureEffect;
using Microsoft::Xna::Framework::Graphics::GraphicsDevice;
using Microsoft::Xna::Framework::Graphics::Texture2D;
using CNA::Internal::Renderers::GpuDrawParams;

namespace
{
    class DualTextureEffectDefaultsTest : public ::testing::Test
    {
    protected:
        GraphicsDevice gd;
        DualTextureEffect fx{gd};
    };
}

// -----------------------------------------------------------------------
// Matrices (IEffectMatrices) — FNA: world/view/projection = Matrix.Identity

TEST_F(DualTextureEffectDefaultsTest, WorldDefaultsToIdentity)
{
    EXPECT_EQ(fx.getWorldProperty(), Matrix::getIdentityProperty());
}

TEST_F(DualTextureEffectDefaultsTest, ViewDefaultsToIdentity)
{
    EXPECT_EQ(fx.getViewProperty(), Matrix::getIdentityProperty());
}

TEST_F(DualTextureEffectDefaultsTest, ProjectionDefaultsToIdentity)
{
    EXPECT_EQ(fx.getProjectionProperty(), Matrix::getIdentityProperty());
}

// -----------------------------------------------------------------------
// Material color — FNA: diffuseColor = Vector3.One, alpha = 1.

TEST_F(DualTextureEffectDefaultsTest, DiffuseColorDefaultsToOne)
{
    EXPECT_EQ(fx.getDiffuseColorProperty(), Vector3(1.0f, 1.0f, 1.0f));
}

TEST_F(DualTextureEffectDefaultsTest, AlphaDefaultsToOne)
{
    EXPECT_FLOAT_EQ(fx.getAlphaProperty(), 1.0f);
}

// -----------------------------------------------------------------------
// Fog (IEffectFog) — FNA: fogEnabled = false, fogStart = 0, fogEnd = 1;
// fogColor has no field initializer in FNA (backed by an EffectParameter
// whose compiled-shader default is black); CNA's fogColorParam_ defaults to
// Vector3::Zero, matching that same black default.

TEST_F(DualTextureEffectDefaultsTest, FogEnabledDefaultsToFalse)
{
    EXPECT_FALSE(fx.getFogEnabledProperty());
}

TEST_F(DualTextureEffectDefaultsTest, FogStartDefaultsToZero)
{
    EXPECT_FLOAT_EQ(fx.getFogStartProperty(), 0.0f);
}

TEST_F(DualTextureEffectDefaultsTest, FogEndDefaultsToOne)
{
    EXPECT_FLOAT_EQ(fx.getFogEndProperty(), 1.0f);
}

TEST_F(DualTextureEffectDefaultsTest, FogColorDefaultsToZero)
{
    EXPECT_EQ(fx.getFogColorProperty(), Vector3::Zero);
}

// -----------------------------------------------------------------------
// Texturing / vertex color — FNA: textureParam/texture2Param/
// vertexColorEnabled default to null/null/false.

TEST_F(DualTextureEffectDefaultsTest, TextureDefaultsToNull)
{
    EXPECT_EQ(fx.getTextureProperty(), nullptr);
}

TEST_F(DualTextureEffectDefaultsTest, Texture2DefaultsToNull)
{
    EXPECT_EQ(fx.getTexture2Property(), nullptr);
}

// plans/plan_xnb.md XNB-32: SetOwnedTexture()/SetOwnedTexture2() -- content-pipeline-loaded effects
// need to keep their own texture references alive (matching real XNA's GC-tracked
// Effect.Texture), unlike setTextureProperty(Texture2D*)'s non-owning pointer used by Model's
// shared texture pool.

TEST_F(DualTextureEffectDefaultsTest, SetOwnedTextureKeepsTextureAliveAndVisibleThroughGetter)
{
    auto owned = std::make_shared<Texture2D>(gd, 2, 2);
    Texture2D* raw = owned.get();

    fx.SetOwnedTexture(owned);

    EXPECT_EQ(fx.getTextureProperty(), raw);
}

TEST_F(DualTextureEffectDefaultsTest, SetOwnedTexture2KeepsTextureAliveAndVisibleThroughGetter)
{
    auto owned = std::make_shared<Texture2D>(gd, 2, 2);
    Texture2D* raw = owned.get();

    fx.SetOwnedTexture2(owned);

    EXPECT_EQ(fx.getTexture2Property(), raw);
}

TEST_F(DualTextureEffectDefaultsTest, CloneSharesOwnedTextureOwnership)
{
    fx.SetOwnedTexture(std::make_shared<Texture2D>(gd, 2, 2));
    fx.SetOwnedTexture2(std::make_shared<Texture2D>(gd, 2, 2));
    Texture2D* originalTexturePtr = fx.getTextureProperty();
    Texture2D* originalTexture2Ptr = fx.getTexture2Property();

    std::unique_ptr<Microsoft::Xna::Framework::Graphics::Effect> cloned(fx.Clone());
    auto* clone = dynamic_cast<DualTextureEffect*>(cloned.get());
    ASSERT_NE(clone, nullptr);

    EXPECT_EQ(clone->getTextureProperty(), originalTexturePtr);
    EXPECT_EQ(clone->getTexture2Property(), originalTexture2Ptr);
}

TEST_F(DualTextureEffectDefaultsTest, VertexColorEnabledDefaultsToFalse)
{
    EXPECT_FALSE(fx.getVertexColorEnabledProperty());
}

// -----------------------------------------------------------------------
// Setters round-trip (every property must actually store what it's given).

TEST_F(DualTextureEffectDefaultsTest, WorldRoundTrips)
{
    const Matrix m = Matrix::CreateTranslation(1.0f, 2.0f, 3.0f);
    fx.setWorldProperty(m);
    EXPECT_EQ(fx.getWorldProperty(), m);
}

TEST_F(DualTextureEffectDefaultsTest, ViewRoundTrips)
{
    const Matrix m = Matrix::CreateTranslation(4.0f, 5.0f, 6.0f);
    fx.setViewProperty(m);
    EXPECT_EQ(fx.getViewProperty(), m);
}

TEST_F(DualTextureEffectDefaultsTest, ProjectionRoundTrips)
{
    const Matrix m = Matrix::CreateTranslation(7.0f, 8.0f, 9.0f);
    fx.setProjectionProperty(m);
    EXPECT_EQ(fx.getProjectionProperty(), m);
}

TEST_F(DualTextureEffectDefaultsTest, DiffuseColorRoundTrips)
{
    const Vector3 c(0.2f, 0.4f, 0.6f);
    fx.setDiffuseColorProperty(c);
    EXPECT_EQ(fx.getDiffuseColorProperty(), c);
}

TEST_F(DualTextureEffectDefaultsTest, AlphaRoundTrips)
{
    fx.setAlphaProperty(0.5f);
    EXPECT_FLOAT_EQ(fx.getAlphaProperty(), 0.5f);
}

TEST_F(DualTextureEffectDefaultsTest, FogEnabledRoundTrips)
{
    fx.setFogEnabledProperty(true);
    EXPECT_TRUE(fx.getFogEnabledProperty());
}

TEST_F(DualTextureEffectDefaultsTest, FogStartRoundTrips)
{
    fx.setFogStartProperty(10.0f);
    EXPECT_FLOAT_EQ(fx.getFogStartProperty(), 10.0f);
}

TEST_F(DualTextureEffectDefaultsTest, FogEndRoundTrips)
{
    fx.setFogEndProperty(20.0f);
    EXPECT_FLOAT_EQ(fx.getFogEndProperty(), 20.0f);
}

TEST_F(DualTextureEffectDefaultsTest, FogColorRoundTrips)
{
    const Vector3 c(0.1f, 0.2f, 0.3f);
    fx.setFogColorProperty(c);
    EXPECT_EQ(fx.getFogColorProperty(), c);
}

TEST_F(DualTextureEffectDefaultsTest, VertexColorEnabledRoundTrips)
{
    fx.setVertexColorEnabledProperty(true);
    EXPECT_TRUE(fx.getVertexColorEnabledProperty());
}

TEST_F(DualTextureEffectDefaultsTest, TextureRoundTrips)
{
    Texture2D tex(gd, 1, 1);
    fx.setTextureProperty(&tex);
    EXPECT_EQ(fx.getTextureProperty(), &tex);
}

TEST_F(DualTextureEffectDefaultsTest, Texture2RoundTrips)
{
    Texture2D tex(gd, 1, 1);
    fx.setTexture2Property(&tex);
    EXPECT_EQ(fx.getTexture2Property(), &tex);
}

// -----------------------------------------------------------------------
// Clone — FNA's clone constructor copies fogEnabled, vertexColorEnabled,
// world/view/projection, diffuseColor, alpha, fogStart, fogEnd, plus
// Texture/Texture2 (via the base Effect(cloneSource) constructor's
// Parameters[i].texture copy loop; CNA copies texture_/texture2_ directly
// since they are raw fields rather than EffectParameter-backed). Confirm
// CNA's Clone() matches for every one of these fields.

TEST_F(DualTextureEffectDefaultsTest, CloneCopiesAllProperties)
{
    Texture2D tex1(gd, 1, 1);
    Texture2D tex2(gd, 1, 1);

    fx.setDiffuseColorProperty(Vector3(0.1f, 0.2f, 0.3f));
    fx.setAlphaProperty(0.7f);
    fx.setFogEnabledProperty(true);
    fx.setFogStartProperty(1.0f);
    fx.setFogEndProperty(2.0f);
    fx.setFogColorProperty(Vector3(0.4f, 0.5f, 0.6f));
    fx.setVertexColorEnabledProperty(true);
    fx.setTextureProperty(&tex1);
    fx.setTexture2Property(&tex2);

    std::unique_ptr<Microsoft::Xna::Framework::Graphics::Effect> cloned(fx.Clone());
    auto* clone = dynamic_cast<DualTextureEffect*>(cloned.get());
    ASSERT_NE(clone, nullptr);

    EXPECT_EQ(clone->getDiffuseColorProperty(), Vector3(0.1f, 0.2f, 0.3f));
    EXPECT_FLOAT_EQ(clone->getAlphaProperty(), 0.7f);
    EXPECT_TRUE(clone->getFogEnabledProperty());
    EXPECT_FLOAT_EQ(clone->getFogStartProperty(), 1.0f);
    EXPECT_FLOAT_EQ(clone->getFogEndProperty(), 2.0f);
    EXPECT_EQ(clone->getFogColorProperty(), Vector3(0.4f, 0.5f, 0.6f));
    EXPECT_TRUE(clone->getVertexColorEnabledProperty());
    EXPECT_EQ(clone->getTextureProperty(), &tex1);
    EXPECT_EQ(clone->getTexture2Property(), &tex2);
}

// -----------------------------------------------------------------------
// GetTypeName — CNAEXT extension, reports the fully-qualified FNA type name.

TEST_F(DualTextureEffectDefaultsTest, GetTypeNameReturnsFullyQualifiedName)
{
    EXPECT_EQ(fx.GetTypeName(), "Microsoft.Xna.Framework.Graphics.DualTextureEffect");
}

// -----------------------------------------------------------------------
// Task 385: Alpha's effect on the forwarded GPU diffuseColor parameter —
// direct, GPU-independent lock-in of FNA's OnApply() formula:
//   diffuseColorParam.SetValue(new Vector4(diffuseColor * alpha, alpha));
// i.e. the forwarded RGB is alpha-premultiplied (DiffuseColor*Alpha) and the
// forwarded alpha channel is the plain Alpha value. Checked directly on
// FillGpuDrawParams()'s output rather than via a GPU pixel readback, because
// Vulkan's BlendState support is known-fake/hardcoded (Task 868/870) and
// would make a real blended pixel-readback test spuriously fail there for a
// reason unrelated to DualTextureEffect itself — see the pixel-test files
// for the real end-to-end GPU verification on EasyGL/Bgfx (and the
// alpha-channel-only verification on Vulkan that works around that gap).

TEST_F(DualTextureEffectDefaultsTest, AlphaPremultipliesForwardedDiffuseRgb)
{
    fx.setDiffuseColorProperty(Vector3(0.8f, 0.4f, 0.2f));
    fx.setAlphaProperty(0.5f);

    GpuDrawParams params;
    fx.FillGpuDrawParams(params);

    EXPECT_FLOAT_EQ(params.diffuseColor[0], 0.4f); // 0.8 * 0.5
    EXPECT_FLOAT_EQ(params.diffuseColor[1], 0.2f); // 0.4 * 0.5
    EXPECT_FLOAT_EQ(params.diffuseColor[2], 0.1f); // 0.2 * 0.5
    EXPECT_FLOAT_EQ(params.diffuseColor[3], 0.5f); // alpha itself, unscaled
}

TEST_F(DualTextureEffectDefaultsTest, AlphaOneLeavesDiffuseRgbUnscaled)
{
    fx.setDiffuseColorProperty(Vector3(0.8f, 0.4f, 0.2f));
    fx.setAlphaProperty(1.0f);

    GpuDrawParams params;
    fx.FillGpuDrawParams(params);

    EXPECT_FLOAT_EQ(params.diffuseColor[0], 0.8f);
    EXPECT_FLOAT_EQ(params.diffuseColor[1], 0.4f);
    EXPECT_FLOAT_EQ(params.diffuseColor[2], 0.2f);
    EXPECT_FLOAT_EQ(params.diffuseColor[3], 1.0f);
}

TEST_F(DualTextureEffectDefaultsTest, AlphaZeroZeroesDiffuseRgbButNotStored)
{
    fx.setDiffuseColorProperty(Vector3(0.8f, 0.4f, 0.2f));
    fx.setAlphaProperty(0.0f);

    GpuDrawParams params;
    fx.FillGpuDrawParams(params);

    EXPECT_FLOAT_EQ(params.diffuseColor[0], 0.0f);
    EXPECT_FLOAT_EQ(params.diffuseColor[1], 0.0f);
    EXPECT_FLOAT_EQ(params.diffuseColor[2], 0.0f);
    EXPECT_FLOAT_EQ(params.diffuseColor[3], 0.0f);
    // DiffuseColor itself is untouched by Alpha -- only the forwarded GPU
    // parameter is premultiplied.
    EXPECT_EQ(fx.getDiffuseColorProperty(), Vector3(0.8f, 0.4f, 0.2f));
}
