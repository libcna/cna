// SPDX-License-Identifier: MS-PL
// plan_modern.md MOD-713, MOD-717, MOD-730, MOD-731: the pipeline's failure path, its statistics,
// and what a settings bag does with values it should not have been given.

#ifdef CNA_CNAEXT

#include <gtest/gtest.h>

#include "CNA/Graphics/PostProcessContext.hpp"
#include "CNA/Graphics/PostProcessPass.hpp"
#include "CNA/Graphics/RenderPipeline.hpp"
#include "CNA/Graphics/RenderPipelineSettings.hpp"
#include "CNA/Graphics/RenderQuality.hpp"
#include "CNA/Graphics/TonemappingMode.hpp"
#include "EngineTestSupport.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTargetBinding.hpp"
#include "System/EventArgs.hpp"

#include <stdexcept>
#include <string>

namespace {

using CNA::Graphics::PostProcessContext;
using CNA::Graphics::RenderPipeline;
using CNA::Graphics::RenderPipelineSettings;
using CNA::Graphics::RenderQuality;
using CNA::Graphics::TonemappingMode;
using Microsoft::Xna::Framework::Color;
using Microsoft::Xna::Framework::Graphics::GraphicsDevice;

constexpr int kWidth  = 32;
constexpr int kHeight = 32;

[[nodiscard]] const void* BoundTarget(GraphicsDevice& device)
{
    const auto bindings = device.GetRenderTargets();
    return bindings.empty() ? nullptr
                            : static_cast<const void*>(bindings[0].getRenderTargetProperty());
}

/// Throws from inside the chain, which is the one failure a pipeline cannot prevent and must
/// survive: a user pass with a bug in it.
class ThrowingPass : public CNA::Graphics::PostProcessPass
{
public:
    void apply(const PostProcessContext&) override
    {
        ++applyCount_;
        if (armed_) throw std::runtime_error("a user pass with a bug in it");
    }

    [[nodiscard]] const std::string& getName() const override
    {
        static const std::string name = "Throwing";
        return name;
    }

    [[nodiscard]] bool isSupported(GraphicsDevice&) const override { return true; }

    void disarm() { armed_ = false; }
    [[nodiscard]] int applyCount() const { return applyCount_; }

private:
    bool armed_ = true;
    int  applyCount_ = 0;
};

// =====================================================================================
// MOD-713: an exception inside a pass
// =====================================================================================

TEST(PipelineExceptionSafetyTest, AThrowingPassLeavesNoTargetBoundAndTheNextFrameRendersNormally)
{
    GraphicsDevice gd;
    CNA_SKIP_WITHOUT_RENDER_TARGETS(gd);

    RenderPipeline pipeline(gd);
    pipeline.resize(kWidth, kHeight);
    auto& settings = pipeline.getSettings();
    settings.setHDREnabled(false);
    settings.setTonemappingMode(TonemappingMode::None);
    settings.setBloomEnabled(false);
    settings.setSSAOEnabled(false);
    settings.setFXAAEnabled(false);

    ThrowingPass thrower;
    pipeline.addUserPass(&thrower);

    ASSERT_EQ(BoundTarget(gd), nullptr);
    pipeline.begin(Color::Black);
    EXPECT_THROW(pipeline.end(), std::runtime_error);

    // The property that matters: the frame's target did not stay bound. Without it, everything
    // drawn afterwards goes into the pipeline's scene target and the screen appears to freeze --
    // and the next Present refuses outright.
    EXPECT_EQ(BoundTarget(gd), nullptr)
        << "an exception inside a pass left a render target bound";

    // And the pipeline is still usable: a throw is not a one-way door into a broken object.
    thrower.disarm();
    EXPECT_NO_THROW({
        pipeline.begin(Color::Black);
        pipeline.end();
    }) << "the pipeline did not recover from a failed frame";
    EXPECT_EQ(thrower.applyCount(), 2);
    EXPECT_EQ(BoundTarget(gd), nullptr);
}

TEST(PipelineExceptionSafetyTest, AFailedFrameDoesNotLeaveTheFrameOpen)
{
    // The other half of recovery: if end() threw with frameOpen_ still set, the next begin() would
    // refuse and the pipeline would be permanently unusable -- the same trap CubeShadowMap and
    // ShadowMap both had at their own begin().
    GraphicsDevice gd;
    CNA_SKIP_WITHOUT_RENDER_TARGETS(gd);

    RenderPipeline pipeline(gd);
    pipeline.resize(kWidth, kHeight);
    ThrowingPass thrower;
    pipeline.addUserPass(&thrower);

    pipeline.begin(Color::Black);
    EXPECT_THROW(pipeline.end(), std::runtime_error);
    EXPECT_NO_THROW(pipeline.begin(Color::Black)) << "the frame was left open by the failure";
    thrower.disarm();
    EXPECT_NO_THROW(pipeline.end());
}

// =====================================================================================
// MOD-717: statistics
// =====================================================================================

TEST(PipelineStatisticsTest, EverythingIsZeroBeforeTheFirstFrame)
{
    GraphicsDevice gd;
    const RenderPipeline pipeline(gd);
    const auto statistics = pipeline.getStatistics();
    EXPECT_EQ(statistics.passesRun, 0);
    EXPECT_EQ(statistics.targetSwitches, 0);
    EXPECT_FALSE(statistics.usedSceneTarget);
    EXPECT_FALSE(statistics.drewSkybox);
}

TEST(PipelineStatisticsTest, AnInertFrameReportsNoWorkAtAll)
{
    GraphicsDevice gd;
    CNA_SKIP_WITHOUT_RENDER_TARGETS(gd);

    RenderPipeline pipeline(gd);
    pipeline.resize(kWidth, kHeight);
    auto& settings = pipeline.getSettings();
    settings.setHDREnabled(false);
    settings.setTonemappingMode(TonemappingMode::None);
    settings.setBloomEnabled(false);
    settings.setSSAOEnabled(false);
    settings.setFXAAEnabled(false);

    pipeline.begin(Color::Black);
    pipeline.end();

    const auto statistics = pipeline.getStatistics();
    EXPECT_EQ(statistics.passesRun, 0);
    EXPECT_EQ(statistics.targetSwitches, 0) << "a pass-through frame bound nothing off-screen";
    EXPECT_FALSE(statistics.usedSceneTarget);
    EXPECT_EQ(statistics.gpuMemoryEstimateBytes, 0u);
}

TEST(PipelineStatisticsTest, TheCountsFollowWhatWasEnabled)
{
    GraphicsDevice gd;
    CNA_SKIP_WITHOUT_RENDER_TARGETS(gd);

    RenderPipeline pipeline(gd);
    pipeline.resize(kWidth, kHeight);
    auto& settings = pipeline.getSettings();
    settings.setHDREnabled(false);
    settings.setTonemappingMode(TonemappingMode::None);
    settings.setSSAOEnabled(false);
    settings.setBloomEnabled(true);
    settings.setFXAAEnabled(true);

    pipeline.begin(Color::Black);
    pipeline.end();

    const auto statistics = pipeline.getStatistics();
    EXPECT_EQ(statistics.passesRun, 2);
    // Two binds for the scene target itself (in and out) plus one per pass.
    EXPECT_EQ(statistics.targetSwitches, 2 + statistics.passesRun);
    EXPECT_TRUE(statistics.usedSceneTarget);
    EXPECT_GT(statistics.gpuMemoryEstimateBytes, 0u);
}

TEST(PipelineStatisticsTest, TheSnapshotIsAValueAndDoesNotChangeUnderTheCaller)
{
    // A POD rather than accessors, so a caller can keep last frame's numbers to compare with this
    // frame's. That only works if the snapshot is a copy.
    GraphicsDevice gd;
    CNA_SKIP_WITHOUT_RENDER_TARGETS(gd);

    RenderPipeline pipeline(gd);
    pipeline.resize(kWidth, kHeight);
    pipeline.getSettings().setBloomEnabled(true);

    pipeline.begin(Color::Black);
    pipeline.end();
    const auto first = pipeline.getStatistics();

    pipeline.getSettings().setBloomEnabled(false);
    pipeline.getSettings().setHDREnabled(false);
    pipeline.getSettings().setTonemappingMode(TonemappingMode::None);
    pipeline.begin(Color::Black);
    pipeline.end();

    EXPECT_GT(first.passesRun, 0) << "the kept snapshot changed under the caller";
    EXPECT_NE(first.passesRun, pipeline.getStatistics().passesRun);
}

// =====================================================================================
// MOD-730: what a settings bag does with values it should not have been given
// =====================================================================================

TEST(SettingsValidationTest, TheUndefinedCasesAreClamped)
{
    // Only the values whose out-of-range case is *undefined*: a gamma of zero is a division by
    // zero, and a negative exposure or intensity is a sign error rather than a look.
    RenderPipelineSettings settings;

    settings.setGamma(0.0f);
    EXPECT_FLOAT_EQ(settings.getGamma(), RenderPipelineSettings::kMinimumGamma);
    settings.setGamma(-5.0f);
    EXPECT_FLOAT_EQ(settings.getGamma(), RenderPipelineSettings::kMinimumGamma);

    settings.setExposure(-1.0f);
    EXPECT_FLOAT_EQ(settings.getExposure(), 0.0f);
    settings.setBloomIntensity(-1.0f);
    EXPECT_FLOAT_EQ(settings.getBloomIntensity(), 0.0f);
    settings.setBloomThreshold(-1.0f);
    EXPECT_FLOAT_EQ(settings.getBloomThreshold(), 0.0f);
    settings.setSSAORadius(-1.0f);
    EXPECT_FLOAT_EQ(settings.getSSAORadius(), 0.0f);
    settings.setSSAOIntensity(-1.0f);
    EXPECT_FLOAT_EQ(settings.getSSAOIntensity(), 0.0f);
    settings.setFXAAEdgeThresholdEXT(0.0f);
    EXPECT_FLOAT_EQ(settings.getFXAAEdgeThresholdEXT(),
                    RenderPipelineSettings::kMinimumFxaaEdgeThreshold);
}

TEST(SettingsValidationTest, MerelyExtremeValuesAreStoredAsGiven)
{
    // The deliberate other half (MOD-22): a bloom threshold of 100 is useless but meaningful, and
    // the passes clamp what they *apply*. A bag that clamped to one pass's limits would change the
    // number a caller reads back and force a quality preset to know every pass's range.
    RenderPipelineSettings settings;
    settings.setBloomThreshold(100.0f);
    EXPECT_FLOAT_EQ(settings.getBloomThreshold(), 100.0f);
    settings.setBloomIterations(500);
    EXPECT_EQ(settings.getBloomIterations(), 500);
    settings.setSSAOSampleCount(9999);
    EXPECT_EQ(settings.getSSAOSampleCount(), 9999);
    settings.setExposure(1000.0f);
    EXPECT_FLOAT_EQ(settings.getExposure(), 1000.0f);
}

TEST(SettingsValidationTest, TheBoundaryItselfIsAccepted)
{
    RenderPipelineSettings settings;
    settings.setGamma(RenderPipelineSettings::kMinimumGamma);
    EXPECT_FLOAT_EQ(settings.getGamma(), RenderPipelineSettings::kMinimumGamma);
    settings.setExposure(0.0f);
    EXPECT_FLOAT_EQ(settings.getExposure(), 0.0f);
}

// =====================================================================================
// MOD-731: serialization
// =====================================================================================

TEST(SettingsSerializationTest, EveryFieldRoundTrips)
{
    RenderPipelineSettings written;
    written.setHDREnabled(true);
    written.setExposure(2.5f);
    written.setGamma(2.2f);
    written.setTonemappingMode(TonemappingMode::Uncharted2);
    written.setBloomEnabled(true);
    written.setBloomIntensity(1.75f);
    written.setBloomThreshold(0.8f);
    written.setBloomIterations(6);
    written.setSSAOEnabled(true);
    written.setSSAORadius(0.35f);
    written.setSSAOIntensity(1.25f);
    written.setSSAOSampleCount(48);
    written.setFXAAEnabled(true);
    written.setFXAAEdgeThresholdEXT(0.0625f);
    written.setRenderQuality(RenderQuality::High);

    RenderPipelineSettings read;
    const int applied = read.applyFromStringEXT(written.toStringEXT());
    EXPECT_EQ(applied, 15) << "a field was written but not read back";

    EXPECT_EQ(read.isHDREnabled(), written.isHDREnabled());
    EXPECT_FLOAT_EQ(read.getExposure(), written.getExposure());
    EXPECT_FLOAT_EQ(read.getGamma(), written.getGamma());
    EXPECT_EQ(read.getTonemappingMode(), written.getTonemappingMode());
    EXPECT_EQ(read.isBloomEnabled(), written.isBloomEnabled());
    EXPECT_FLOAT_EQ(read.getBloomIntensity(), written.getBloomIntensity());
    EXPECT_FLOAT_EQ(read.getBloomThreshold(), written.getBloomThreshold());
    EXPECT_EQ(read.getBloomIterations(), written.getBloomIterations());
    EXPECT_EQ(read.isSSAOEnabled(), written.isSSAOEnabled());
    EXPECT_FLOAT_EQ(read.getSSAORadius(), written.getSSAORadius());
    EXPECT_FLOAT_EQ(read.getSSAOIntensity(), written.getSSAOIntensity());
    EXPECT_EQ(read.getSSAOSampleCount(), written.getSSAOSampleCount());
    EXPECT_EQ(read.isFXAAEnabled(), written.isFXAAEnabled());
    EXPECT_FLOAT_EQ(read.getFXAAEdgeThresholdEXT(), written.getFXAAEdgeThresholdEXT());
    EXPECT_EQ(read.getRenderQuality(), written.getRenderQuality());

    // And the round trip is stable: writing what was read gives the same text back.
    EXPECT_EQ(read.toStringEXT(), written.toStringEXT());
}

TEST(SettingsSerializationTest, EveryTonemappingModeAndQualitySurvives)
{
    for (const TonemappingMode mode :
         {TonemappingMode::None, TonemappingMode::Reinhard, TonemappingMode::Filmic,
          TonemappingMode::Aces, TonemappingMode::Uncharted2})
    {
        RenderPipelineSettings written;
        written.setTonemappingMode(mode);
        RenderPipelineSettings read;
        read.applyFromStringEXT(written.toStringEXT());
        EXPECT_EQ(read.getTonemappingMode(), mode);
    }

    for (const RenderQuality quality :
         {RenderQuality::Low, RenderQuality::Medium, RenderQuality::High, RenderQuality::Ultra})
    {
        RenderPipelineSettings written;
        written.setRenderQuality(quality);
        RenderPipelineSettings read;
        read.applyFromStringEXT(written.toStringEXT());
        EXPECT_EQ(read.getRenderQuality(), quality);
    }
}

TEST(SettingsSerializationTest, UnknownAndMalformedFieldsAreIgnoredRatherThanRefused)
{
    // What lets an older build read a newer settings string, and what stops one bad field from
    // costing a whole look.
    RenderPipelineSettings settings;
    const float originalGamma = settings.getGamma();

    const int applied = settings.applyFromStringEXT(
        "somethingFromTheFuture=42;bloomIntensity=notANumber;;noEquals;exposure=3.5;gamma=");
    EXPECT_EQ(applied, 1) << "only the one well-formed field should have applied";
    EXPECT_FLOAT_EQ(settings.getExposure(), 3.5f);
    EXPECT_FLOAT_EQ(settings.getGamma(), originalGamma) << "an empty value changed a field";
}

TEST(SettingsSerializationTest, LoadedValuesGoThroughTheSameClamping)
{
    // The path most likely to bypass validation: a settings file with a stale zero gamma in it.
    RenderPipelineSettings settings;
    EXPECT_EQ(settings.applyFromStringEXT("gamma=0;exposure=-4;"), 2);
    EXPECT_FLOAT_EQ(settings.getGamma(), RenderPipelineSettings::kMinimumGamma);
    EXPECT_FLOAT_EQ(settings.getExposure(), 0.0f);
}

TEST(SettingsSerializationTest, AnEmptyStringChangesNothing)
{
    RenderPipelineSettings settings;
    const std::string before = settings.toStringEXT();
    EXPECT_EQ(settings.applyFromStringEXT(""), 0);
    EXPECT_EQ(settings.toStringEXT(), before);
}


// =====================================================================================
// MOD-715: a device reset drops the pipeline's targets
// =====================================================================================

TEST(PipelineDeviceResetTest, ReleasingResourcesDropsTheTargetsAndTheNextFrameRebuildsThem)
{
    GraphicsDevice gd;
    CNA_SKIP_WITHOUT_RENDER_TARGETS(gd);

    RenderPipeline pipeline(gd);
    pipeline.resize(kWidth, kHeight);
    pipeline.getSettings().setBloomEnabled(true);

    pipeline.begin(Color::Black);
    pipeline.end();
    ASSERT_GT(pipeline.getGpuMemoryEstimateBytes(), 0u);

    pipeline.releaseDeviceResourcesEXT();
    EXPECT_EQ(pipeline.getGpuMemoryEstimateBytes(), 0u)
        << "the targets survived a release, so a reset would leave the pipeline holding storage "
           "the driver has already destroyed";
    EXPECT_FALSE(pipeline.isUsingSceneTarget());

    // And the pipeline still works: a reset is not a one-way door.
    EXPECT_NO_THROW({
        pipeline.begin(Color::Black);
        pipeline.end();
    });
    EXPECT_GT(pipeline.getGpuMemoryEstimateBytes(), 0u) << "the next frame did not reallocate";
}

TEST(PipelineDeviceResetTest, ReleasingMidFrameIsRefused)
{
    // Releasing the scene target while it is bound is the exact situation this exists to avoid, so
    // it is refused rather than done quietly.
    GraphicsDevice gd;
    CNA_SKIP_WITHOUT_RENDER_TARGETS(gd);

    RenderPipeline pipeline(gd);
    pipeline.resize(kWidth, kHeight);
    pipeline.getSettings().setBloomEnabled(true);

    pipeline.begin(Color::Black);
    EXPECT_THROW(pipeline.releaseDeviceResourcesEXT(), std::logic_error);
    pipeline.end();
}

TEST(PipelineDeviceResetTest, TheDeviceResetEventReachesThePipeline)
{
    // The wiring, checked by raising the event the device itself raises. Without this the pipeline
    // would have to be told about a reset by the game, which no game would remember to do.
    GraphicsDevice gd;
    CNA_SKIP_WITHOUT_RENDER_TARGETS(gd);

    RenderPipeline pipeline(gd);
    pipeline.resize(kWidth, kHeight);
    pipeline.getSettings().setBloomEnabled(true);
    pipeline.begin(Color::Black);
    pipeline.end();
    ASSERT_GT(pipeline.getGpuMemoryEstimateBytes(), 0u);

    gd.DeviceReset.Raise(&gd, System::EventArgs::Empty);
    EXPECT_EQ(pipeline.getGpuMemoryEstimateBytes(), 0u)
        << "the pipeline is not subscribed to DeviceReset";
}

TEST(PipelineDeviceResetTest, APipelineDestroyedBeforeItsDeviceUnsubscribes)
{
    // The handler captures `this`, and a device outliving a pipeline is the normal case since Game
    // owns the device. Without the Remove in the destructor this raise would call into freed
    // memory -- which ASan would catch and an ordinary run would not.
    GraphicsDevice gd;
    {
        RenderPipeline pipeline(gd);
        pipeline.resize(kWidth, kHeight);
    }
    EXPECT_NO_THROW(gd.DeviceReset.Raise(&gd, System::EventArgs::Empty));
}

} // namespace

#endif // CNA_CNAEXT
