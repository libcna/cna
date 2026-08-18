// SPDX-License-Identifier: MS-PL
// plan_modern.md MOD-604: FXAA's quality mapping.

#ifdef CNA_CNAEXT

#include <gtest/gtest.h>

#include "CNA/Graphics/BloomPass.hpp"
#include "CNA/Graphics/FxaaPass.hpp"
#include "CNA/Graphics/SsaoPass.hpp"
#include "CNA/Graphics/RenderPipelineSettings.hpp"
#include "CNA/Graphics/RenderQuality.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"

namespace {

using CNA::Graphics::FxaaPass;
using CNA::Graphics::RenderPipelineSettings;
using CNA::Graphics::RenderQuality;
using Microsoft::Xna::Framework::Graphics::GraphicsDevice;

TEST(FxaaQualityTest, EachPresetMapsToItsDocumentedThreshold)
{
    EXPECT_FLOAT_EQ(FxaaPass::edgeThresholdForQuality(RenderQuality::Low), 0.250f);
    EXPECT_FLOAT_EQ(FxaaPass::edgeThresholdForQuality(RenderQuality::Medium), 0.125f);
    EXPECT_FLOAT_EQ(FxaaPass::edgeThresholdForQuality(RenderQuality::High), 0.0625f);
    EXPECT_FLOAT_EQ(FxaaPass::edgeThresholdForQuality(RenderQuality::Ultra), 0.0312f);
}

TEST(FxaaQualityTest, HigherQualityMeansALowerThreshold)
{
    // The direction is the part that is easy to invert, and inverting it would make "Ultra" the
    // preset that filters least -- a mistake no test of the numbers alone would catch, since all
    // four would still be present and distinct.
    EXPECT_GT(FxaaPass::edgeThresholdForQuality(RenderQuality::Low),
              FxaaPass::edgeThresholdForQuality(RenderQuality::Medium));
    EXPECT_GT(FxaaPass::edgeThresholdForQuality(RenderQuality::Medium),
              FxaaPass::edgeThresholdForQuality(RenderQuality::High));
    EXPECT_GT(FxaaPass::edgeThresholdForQuality(RenderQuality::High),
              FxaaPass::edgeThresholdForQuality(RenderQuality::Ultra));
}

TEST(FxaaQualityTest, EveryThresholdIsPositive)
{
    // A zero or negative threshold makes every texel an edge, which is a full-screen blur rather
    // than anti-aliasing.
    for (const RenderQuality quality :
         {RenderQuality::Low, RenderQuality::Medium, RenderQuality::High, RenderQuality::Ultra})
        EXPECT_GT(FxaaPass::edgeThresholdForQuality(quality), 0.0f);
}

TEST(FxaaQualityTest, AValueOutsideTheEnumGetsTheDefault)
{
    EXPECT_FLOAT_EQ(FxaaPass::edgeThresholdForQuality(static_cast<RenderQuality>(99)),
                    FxaaPass::edgeThresholdForQuality(RenderQuality::Medium));
}

TEST(FxaaQualityTest, ThePassDefaultIsTheMediumPreset)
{
    // The pass's own default and the Medium preset must be the same number, or applying Medium
    // would silently change a game that had never touched the setting.
    GraphicsDevice gd;
    const FxaaPass pass(gd);
    EXPECT_FLOAT_EQ(pass.getEdgeThreshold(),
                    FxaaPass::edgeThresholdForQuality(RenderQuality::Medium));

    const RenderPipelineSettings settings;
    EXPECT_FLOAT_EQ(settings.getFXAAEdgeThresholdEXT(),
                    FxaaPass::edgeThresholdForQuality(RenderQuality::Medium));
}

TEST(FxaaQualityTest, ApplyingThePresetSetsAllThreeSubsystems)
{
    RenderPipelineSettings settings;
    settings.setFXAAEdgeThresholdEXT(0.9f);
    settings.setRenderQuality(RenderQuality::Ultra);
    EXPECT_FLOAT_EQ(settings.getFXAAEdgeThresholdEXT(), 0.9f)
        << "setRenderQuality overwrote a tuned value";

    settings.applyRenderQualityPresetEXT();
    EXPECT_FLOAT_EQ(settings.getFXAAEdgeThresholdEXT(),
                    FxaaPass::edgeThresholdForQuality(RenderQuality::Ultra));
    EXPECT_EQ(settings.getSSAOSampleCount(),
              CNA::Graphics::SsaoPass::sampleCountForQuality(RenderQuality::Ultra));
    EXPECT_EQ(settings.getBloomIterations(),
              CNA::Graphics::BloomPass::iterationsForQuality(RenderQuality::Ultra));
}

TEST(FxaaQualityTest, TheSettingsBagOverridesThePassLocalDefault)
{
    // Every other pass reads its parameters from the settings when one is supplied; FXAA did not
    // until MOD-604, which meant a pipeline that applied a quality preset was silently overruled
    // by whatever the pass itself had been constructed with.
    GraphicsDevice gd;
    FxaaPass pass(gd);
    pass.setEdgeThreshold(0.5f);
    EXPECT_FLOAT_EQ(pass.getEdgeThreshold(), 0.5f);

    RenderPipelineSettings settings;
    settings.setRenderQuality(RenderQuality::Ultra);
    settings.applyRenderQualityPresetEXT();
    // The pass keeps its own value as the fallback; what the settings decide is what is *used*.
    // That is asserted through the rendered result in cnaext_fxaa_test, where the presets visibly
    // change the filtering; here what is pinned is that the two are separate stores.
    EXPECT_FLOAT_EQ(pass.getEdgeThreshold(), 0.5f);
    EXPECT_NE(settings.getFXAAEdgeThresholdEXT(), 0.5f);
}

} // namespace

#endif // CNA_CNAEXT
