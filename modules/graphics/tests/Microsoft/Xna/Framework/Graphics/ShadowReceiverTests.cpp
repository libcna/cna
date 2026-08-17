// SPDX-License-Identifier: MS-PL
// plan_modern.md MOD-820..MOD-826: the receiving half of shadows.
//
// Two things are worth pinning here and neither is visible in an image. First, the four lit
// effects really do carry the shadow state into GpuDrawParams -- a setter that stores a value the
// renderer never sees is the failure this whole seam exists to avoid. Second, the defaults are
// inert: an effect that is never given a shadow map must fill the params exactly as it did before
// shadow support existed, or every game that does not use shadows pays for them.

#include <gtest/gtest.h>

#include "CNA/Internal/Renderers/Common/IGraphicsRenderer.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/IShadowReceiverEXT.hpp"
#include "Microsoft/Xna/Framework/Graphics/PbrEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/SkinnedEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/SkinnedPbrEffect.hpp"

namespace {

using CNA::Internal::Renderers::GpuDrawParams;
using Microsoft::Xna::Framework::Matrix;
using Microsoft::Xna::Framework::Graphics::BasicEffect;
using Microsoft::Xna::Framework::Graphics::GraphicsDevice;
using Microsoft::Xna::Framework::Graphics::IShadowReceiverEXT;
using Microsoft::Xna::Framework::Graphics::PbrEffect;
using Microsoft::Xna::Framework::Graphics::RenderTarget2D;
using Microsoft::Xna::Framework::Graphics::SkinnedEffect;
using Microsoft::Xna::Framework::Graphics::SkinnedPbrEffect;

/// Exercises the interface through a base-class reference, which is how the engine layer will use
/// it -- a shadow subsystem should not need to know which effect it is talking to.
template <typename EffectType>
void ExpectShadowStateRoundTrips(GraphicsDevice& gd)
{
    EffectType effect(gd);
    IShadowReceiverEXT& receiver = effect;

    EXPECT_EQ(receiver.getShadowMapEXT(), nullptr);
    EXPECT_FALSE(receiver.isShadowsEnabledEXT());
    EXPECT_GT(receiver.getShadowDepthBiasEXT(), 0.0f);

    RenderTarget2D shadowMap(gd, 8, 8);
    Matrix lightViewProjection = Matrix::getIdentityProperty();
    lightViewProjection.M41 = 7.0f;

    receiver.setShadowMapEXT(&shadowMap);
    receiver.setLightViewProjectionEXT(lightViewProjection);
    receiver.setShadowsEnabledEXT(true);
    receiver.setShadowDepthBiasEXT(0.01f);

    EXPECT_EQ(receiver.getShadowMapEXT(), &shadowMap);
    EXPECT_TRUE(receiver.isShadowsEnabledEXT());
    EXPECT_FLOAT_EQ(receiver.getShadowDepthBiasEXT(), 0.01f);
    EXPECT_FLOAT_EQ(receiver.getLightViewProjectionEXT().M41, 7.0f);

    receiver.setShadowMapEXT(nullptr);
    EXPECT_EQ(receiver.getShadowMapEXT(), nullptr);
}

TEST(ShadowReceiverTest, BasicEffectCarriesShadowState)
{
    GraphicsDevice gd;
    ExpectShadowStateRoundTrips<BasicEffect>(gd);
}

TEST(ShadowReceiverTest, SkinnedEffectCarriesShadowState)
{
    GraphicsDevice gd;
    ExpectShadowStateRoundTrips<SkinnedEffect>(gd);
}

TEST(ShadowReceiverTest, PbrEffectCarriesShadowState)
{
    GraphicsDevice gd;
    ExpectShadowStateRoundTrips<PbrEffect>(gd);
}

TEST(ShadowReceiverTest, SkinnedPbrEffectCarriesShadowState)
{
    GraphicsDevice gd;
    ExpectShadowStateRoundTrips<SkinnedPbrEffect>(gd);
}

TEST(ShadowReceiverTest, TheDefaultsAreInertInGpuDrawParams)
{
    // The property that keeps this free for everyone who does not use it.
    GraphicsDevice gd;
    BasicEffect effect(gd);

    GpuDrawParams params;
    effect.FillGpuDrawParams(params);

    EXPECT_FALSE(params.shadowsEnabled);
    EXPECT_EQ(params.shadowMap, nullptr);
    EXPECT_FLOAT_EQ(params.lightViewProjColMajor[0], 1.0f);   // still identity
    EXPECT_FLOAT_EQ(params.lightViewProjColMajor[12], 0.0f);
}

TEST(ShadowReceiverTest, EnabledShadowStateReachesGpuDrawParams)
{
    GraphicsDevice gd;
    BasicEffect effect(gd);
    RenderTarget2D shadowMap(gd, 8, 8);

    Matrix lightViewProjection = Matrix::getIdentityProperty();
    lightViewProjection.M41 = 3.0f;

    effect.setShadowMapEXT(&shadowMap);
    effect.setLightViewProjectionEXT(lightViewProjection);
    effect.setShadowsEnabledEXT(true);
    effect.setShadowDepthBiasEXT(0.02f);

    GpuDrawParams params;
    effect.FillGpuDrawParams(params);

    EXPECT_TRUE(params.shadowsEnabled);
    EXPECT_NE(params.shadowMap, nullptr);
    EXPECT_FLOAT_EQ(params.shadowDepthBias, 0.02f);
    EXPECT_FLOAT_EQ(params.lightViewProjColMajor[12], 3.0f);
}

TEST(ShadowReceiverTest, EnablingWithoutAMapDoesNotClaimShadows)
{
    // Enabled-but-unattached is a misconfiguration, and the renderer must not be told to sample a
    // texture that is not there.
    GraphicsDevice gd;
    BasicEffect effect(gd);
    effect.setShadowsEnabledEXT(true);

    GpuDrawParams params;
    effect.FillGpuDrawParams(params);

    EXPECT_FALSE(params.shadowsEnabled);
    EXPECT_EQ(params.shadowMap, nullptr);
}

} // namespace
