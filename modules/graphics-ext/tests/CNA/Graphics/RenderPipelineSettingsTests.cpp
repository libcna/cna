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

TEST(RenderPipelineSettingsTest, OutOfRangeValuesAreStoredRatherThanRejected)
{
    // The settings bag stores; the passes clamp when they apply a value, and document their range.
    // Rejecting here would make a quality preset need to know every pass's limits.
    RenderPipelineSettings settings;

    settings.setBloomThreshold(-1.0f);
    settings.setBloomIterations(999);
    settings.setSSAOSampleCount(1);

    EXPECT_FLOAT_EQ(settings.getBloomThreshold(), -1.0f);
    EXPECT_EQ(settings.getBloomIterations(), 999);
    EXPECT_EQ(settings.getSSAOSampleCount(), 1);
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
