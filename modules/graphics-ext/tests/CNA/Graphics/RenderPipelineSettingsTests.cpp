// SPDX-License-Identifier: MS-PL
// plan_modern.md MOD-21/MOD-22: the settings bag gains the fields the post-process passes read, and
// TonemappingMode gains Uncharted2. Both are additive by design -- the existing values are stored
// in settings and compared by ordinal, so appending is the only safe direction, and these tests
// pin that as much as they pin the new accessors.

#ifdef CNA_CNAEXT

#include <gtest/gtest.h>

#include "CNA/Graphics/RenderPipelineSettings.hpp"
#include "CNA/Graphics/RenderQuality.hpp"
#include "CNA/Graphics/ShadowQuality.hpp"
#include "CNA/Graphics/TonemappingMode.hpp"

namespace {

using CNA::Graphics::RenderPipelineSettings;
using CNA::Graphics::RenderQuality;
using CNA::Graphics::ShadowQuality;
using CNA::Graphics::TonemappingMode;

TEST(RenderPipelineSettingsTest, DefaultsAreTheInertPipeline)
{
    // Every default has to describe "do nothing", because RenderPipeline's contract is that a
    // freshly constructed settings bag produces output identical to not using the pipeline at all.
    const RenderPipelineSettings settings;

    EXPECT_FALSE(settings.isHDREnabled());
    EXPECT_FALSE(settings.isBloomEnabled());
    EXPECT_FALSE(settings.isSSAOEnabled());
    EXPECT_FALSE(settings.isFXAAEnabled());
    EXPECT_FALSE(settings.isShadowsEnabled());
    EXPECT_EQ(settings.getTonemappingMode(), TonemappingMode::None);
    EXPECT_EQ(settings.getShadowQuality(), ShadowQuality::Disabled);
    EXPECT_EQ(settings.getRenderQuality(), RenderQuality::Medium);
    EXPECT_FLOAT_EQ(settings.getExposure(), 1.0f);
    EXPECT_FLOAT_EQ(settings.getGamma(), 2.2f);
}

TEST(RenderPipelineSettingsTest, TheSsrFieldsRoundTripAndAreValidated)
{
    // plan_modern.md MOD-2004. Every field the pass reads, and the two whose out-of-range case is a
    // decision rather than an oversight: distances clamp at zero because a negative reflection
    // distance has no meaning, and the edge fade clamps at half the frame because a fade wider than
    // that would consume the whole image.
    RenderPipelineSettings settings;

    EXPECT_FALSE(settings.isSSREnabled()) << "SSR must be off by default";
    settings.setSSREnabled(true);
    EXPECT_TRUE(settings.isSSREnabled());

    settings.setSSRMaxDistance(12.5f);
    settings.setSSRStepCount(48);
    settings.setSSRThickness(1.5f);
    settings.setSSRDepthBias(0.2f);
    settings.setSSREdgeFade(0.25f);
    settings.setSSRIntensity(0.75f);
    EXPECT_FLOAT_EQ(settings.getSSRMaxDistance(), 12.5f);
    EXPECT_EQ(settings.getSSRStepCount(), 48);
    EXPECT_FLOAT_EQ(settings.getSSRThickness(), 1.5f);
    EXPECT_FLOAT_EQ(settings.getSSRDepthBias(), 0.2f);
    EXPECT_FLOAT_EQ(settings.getSSREdgeFade(), 0.25f);
    EXPECT_FLOAT_EQ(settings.getSSRIntensity(), 0.75f);

    settings.setSSRMaxDistance(-1.0f);
    settings.setSSRThickness(-1.0f);
    settings.setSSRDepthBias(-1.0f);
    settings.setSSRIntensity(-1.0f);
    EXPECT_FLOAT_EQ(settings.getSSRMaxDistance(), 0.0f);
    EXPECT_FLOAT_EQ(settings.getSSRThickness(), 0.0f);
    EXPECT_FLOAT_EQ(settings.getSSRDepthBias(), 0.0f);
    EXPECT_FLOAT_EQ(settings.getSSRIntensity(), 0.0f);

    settings.setSSREdgeFade(5.0f);
    EXPECT_FLOAT_EQ(settings.getSSREdgeFade(), 0.5f);
    settings.setSSREdgeFade(-5.0f);
    EXPECT_FLOAT_EQ(settings.getSSREdgeFade(), 0.0f);
}

TEST(RenderPipelineSettingsTest, TheNewPassFieldsHaveUsableDefaults)
{
    const RenderPipelineSettings settings;

    EXPECT_FLOAT_EQ(settings.getBloomThreshold(), 1.0f);   // "brighter than white" bloom
    EXPECT_EQ(settings.getBloomIterations(), 4);
    EXPECT_FLOAT_EQ(settings.getSSAORadius(), 0.5f);
    EXPECT_FLOAT_EQ(settings.getSSAOIntensity(), 1.0f);
    EXPECT_EQ(settings.getSSAOSampleCount(), 16);
}

TEST(RenderPipelineSettingsTest, EveryNewFieldRoundTrips)
{
    RenderPipelineSettings settings;

    settings.setBloomThreshold(0.25f);
    settings.setBloomIterations(6);
    settings.setSSAORadius(2.5f);
    settings.setSSAOIntensity(0.75f);
    settings.setSSAOSampleCount(32);
    settings.setFXAAEnabled(true);

    EXPECT_FLOAT_EQ(settings.getBloomThreshold(), 0.25f);
    EXPECT_EQ(settings.getBloomIterations(), 6);
    EXPECT_FLOAT_EQ(settings.getSSAORadius(), 2.5f);
    EXPECT_FLOAT_EQ(settings.getSSAOIntensity(), 0.75f);
    EXPECT_EQ(settings.getSSAOSampleCount(), 32);
    EXPECT_TRUE(settings.isFXAAEnabled());
}

TEST(RenderPipelineSettingsTest, ExtremeButMeaningfulValuesAreStoredRatherThanRejected)
{
    // The settings bag stores; the passes clamp when they apply a value, and document their range.
    // Rejecting here would make a quality preset need to know every pass's limits.
    //
    // plan_modern.md MOD-730 narrowed this rule and this test with it. It used to assert that a
    // bloom threshold of -1 was stored too, and that is now clamped: a *negative* threshold or
    // intensity is a sign error rather than a look, and the pass would have had to guard against
    // it anyway. What survives -- and is what MOD-22 was really about -- is that values which are
    // merely extreme go through untouched, because their limits are pass-specific.
    RenderPipelineSettings settings;

    settings.setBloomThreshold(100.0f);
    settings.setBloomIterations(999);
    settings.setSSAOSampleCount(1);

    EXPECT_FLOAT_EQ(settings.getBloomThreshold(), 100.0f);
    EXPECT_EQ(settings.getBloomIterations(), 999);
    EXPECT_EQ(settings.getSSAOSampleCount(), 1);
}

TEST(RenderPipelineSettingsTest, ValuesWhoseOutOfRangeCaseIsUndefinedAreClamped)
{
    // The other half of MOD-730's split, asserted next to the rule it narrows so the two are read
    // together rather than looking like a contradiction.
    RenderPipelineSettings settings;

    settings.setBloomThreshold(-1.0f);
    EXPECT_FLOAT_EQ(settings.getBloomThreshold(), 0.0f);
    settings.setGamma(0.0f);
    EXPECT_FLOAT_EQ(settings.getGamma(), RenderPipelineSettings::kMinimumGamma);
}

TEST(RenderPipelineSettingsTest, TheVolumetricsDefaultsAreOff)
{
    // MOD-2054. Three passes share section 20.6, and the thing they have in common is that a game
    // that has never heard of them must render exactly what it rendered before. Each one is turned
    // off by *its own* amount being zero rather than by an enable flag, so the zero is the switch
    // and it is what this pins. The other four fields are shapes, not amounts: they describe how
    // the effect would look if it were on, so their defaults are usable values rather than zeros.
    const RenderPipelineSettings settings;

    EXPECT_FLOAT_EQ(settings.getVolumetricFogDensity(), 0.0f);
    EXPECT_FLOAT_EQ(settings.getLightShaftIntensity(), 0.0f);
    EXPECT_FLOAT_EQ(settings.getHeightFogDensity(), 0.0f);

    EXPECT_FLOAT_EQ(settings.getLightShaftThreshold(), 0.7f);
    EXPECT_FLOAT_EQ(settings.getLightShaftDecay(), 0.92f);
    EXPECT_FLOAT_EQ(settings.getHeightFogFalloff(), 0.1f);
    EXPECT_FLOAT_EQ(settings.getHeightFogBaseHeight(), 0.0f);
}

TEST(RenderPipelineSettingsTest, TheVolumetricsFieldsRoundTripAndAreValidated)
{
    RenderPipelineSettings settings;

    settings.setVolumetricFogDensity(0.4f);
    settings.setLightShaftThreshold(0.55f);
    settings.setLightShaftIntensity(1.3f);
    settings.setLightShaftDecay(0.85f);
    settings.setHeightFogDensity(0.25f);
    settings.setHeightFogFalloff(0.3f);
    settings.setHeightFogBaseHeight(-12.5f);

    EXPECT_FLOAT_EQ(settings.getVolumetricFogDensity(), 0.4f);
    EXPECT_FLOAT_EQ(settings.getLightShaftThreshold(), 0.55f);
    EXPECT_FLOAT_EQ(settings.getLightShaftIntensity(), 1.3f);
    EXPECT_FLOAT_EQ(settings.getLightShaftDecay(), 0.85f);
    EXPECT_FLOAT_EQ(settings.getHeightFogDensity(), 0.25f);
    EXPECT_FLOAT_EQ(settings.getHeightFogFalloff(), 0.3f);
    EXPECT_FLOAT_EQ(settings.getHeightFogBaseHeight(), -12.5f)
        << "a base height below the origin is a valley floor, not a mistake";

    // Negative amounts have no meaning -- a medium cannot scatter a negative quantity of light --
    // so they land on the value that means "off" rather than being stored and inverted later.
    settings.setVolumetricFogDensity(-1.0f);
    settings.setLightShaftIntensity(-1.0f);
    settings.setLightShaftThreshold(-1.0f);
    settings.setHeightFogDensity(-1.0f);
    settings.setHeightFogFalloff(-1.0f);
    EXPECT_FLOAT_EQ(settings.getVolumetricFogDensity(), 0.0f);
    EXPECT_FLOAT_EQ(settings.getLightShaftIntensity(), 0.0f);
    EXPECT_FLOAT_EQ(settings.getLightShaftThreshold(), 0.0f);
    EXPECT_FLOAT_EQ(settings.getHeightFogDensity(), 0.0f);
    EXPECT_FLOAT_EQ(settings.getHeightFogFalloff(), 0.0f);

    // Decay is a per-step multiplier along the shaft walk, so above 1 it would brighten with
    // distance and diverge; it is clamped in both directions rather than merely floored.
    settings.setLightShaftDecay(2.0f);
    EXPECT_FLOAT_EQ(settings.getLightShaftDecay(), 1.0f);
    settings.setLightShaftDecay(-0.5f);
    EXPECT_FLOAT_EQ(settings.getLightShaftDecay(), 0.0f);
}

TEST(RenderPipelineSettingsTest, TonemappingModeOrdinalsAreStable)
{
    // Uncharted2 was appended, not inserted. A settings bag serialized by an earlier build must
    // still read back as the same operator.
    EXPECT_EQ(static_cast<int>(TonemappingMode::None), 0);
    EXPECT_EQ(static_cast<int>(TonemappingMode::Reinhard), 1);
    EXPECT_EQ(static_cast<int>(TonemappingMode::Filmic), 2);
    EXPECT_EQ(static_cast<int>(TonemappingMode::Aces), 3);
    EXPECT_EQ(static_cast<int>(TonemappingMode::Uncharted2), 4);
}

TEST(RenderPipelineSettingsTest, TonemappingModeRoundTripsIncludingTheNewOperator)
{
    RenderPipelineSettings settings;

    for (const TonemappingMode mode : {TonemappingMode::None, TonemappingMode::Reinhard,
                                       TonemappingMode::Filmic, TonemappingMode::Aces,
                                       TonemappingMode::Uncharted2})
    {
        settings.setTonemappingMode(mode);
        EXPECT_EQ(settings.getTonemappingMode(), mode);
    }
}

} // namespace

#endif // CNA_CNAEXT
