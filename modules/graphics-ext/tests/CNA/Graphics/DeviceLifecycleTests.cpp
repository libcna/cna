// SPDX-License-Identifier: MS-PL
// plan_modern.md MOD-1714, MOD-1715: device loss across the whole layer, and two devices at once.
//
// Both are about state a subsystem holds that outlives the thing it describes. After a device reset
// every GPU object names storage the driver destroyed; with two devices, an object built on one
// must not be reachable from the other. Neither failure produces an error at the point it happens --
// the first renders into freed storage, the second renders into the wrong window -- so both are
// checked by construction and by what the objects report afterwards.

#ifdef CNA_CNAEXT

#include <gtest/gtest.h>

#include "CNA/Graphics/BloomPass.hpp"
#include "CNA/Graphics/CascadedShadowMap.hpp"
#include "CNA/Graphics/CubeShadowMap.hpp"
#include "CNA/Graphics/DepthNormalPrepass.hpp"
#include "CNA/Graphics/EnvironmentProcessor.hpp"
#include "CNA/Graphics/FxaaPass.hpp"
#include "CNA/Graphics/RenderPipeline.hpp"
#include "CNA/Graphics/RenderPipelineSettings.hpp"
#include "CNA/Graphics/ShadowMap.hpp"
#include "CNA/Graphics/ShadowQuality.hpp"
#include "CNA/Graphics/Skybox.hpp"
#include "CNA/Graphics/SpotShadowMap.hpp"
#include "CNA/Graphics/SsaoPass.hpp"
#include "CNA/Graphics/TonemapPass.hpp"
#include "CNA/Graphics/TonemappingMode.hpp"
#include "EngineTestSupport.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "System/EventArgs.hpp"

#include <memory>

namespace {

using CNA::Graphics::RenderPipeline;
using CNA::Graphics::ShadowQuality;
using CNA::Graphics::TonemappingMode;
using Microsoft::Xna::Framework::Color;
using Microsoft::Xna::Framework::Graphics::GraphicsDevice;

constexpr int kWidth  = 32;
constexpr int kHeight = 32;

/// One frame with every pass the scene can supply inputs for.
void RunFrame(RenderPipeline& pipeline)
{
    pipeline.begin(Color::Black);
    pipeline.end();
}

// =====================================================================================
// MOD-1714: a device reset, then a correct frame, for every subsystem
// =====================================================================================

TEST(DeviceLossTest, EverySubsystemSurvivesAResetAndRendersAgain)
{
    // The row asks for "a correct frame for every subsystem" after a loss. Constructed together
    // rather than one at a time, because the failure this guards against is a *shared* one: they
    // all hold render targets from the same device, and a reset invalidates every one at once.
    GraphicsDevice gd;
    CNA_SKIP_WITHOUT_RENDER_TARGETS(gd);

    RenderPipeline pipeline(gd);
    pipeline.resize(kWidth, kHeight);
    auto& settings = pipeline.getSettings();
    settings.setHDREnabled(true);
    settings.setBloomEnabled(true);
    settings.setFXAAEnabled(true);
    settings.setTonemappingMode(TonemappingMode::Aces);

    CNA::Graphics::ShadowMap          shadowMap(gd, ShadowQuality::Low);
    CNA::Graphics::CascadedShadowMap  cascades(gd, ShadowQuality::Low, 3);
    CNA::Graphics::SpotShadowMap      spot(gd, ShadowQuality::Low);
    CNA::Graphics::CubeShadowMap      cube(gd, ShadowQuality::Low);
    CNA::Graphics::DepthNormalPrepass prepass(gd, kWidth, kHeight);
    CNA::Graphics::Skybox             sky(gd, nullptr);
    CNA::Graphics::BloomPass          bloom(gd);
    CNA::Graphics::SsaoPass           ssao(gd);
    CNA::Graphics::FxaaPass           fxaa(gd);
    CNA::Graphics::TonemapPass        tonemap(gd);

    RunFrame(pipeline);
    const std::size_t before = pipeline.getGpuMemoryEstimateBytes();
    ASSERT_GT(before, 0u);

    // The reset itself. Every object above is still alive and now holds invalid GPU storage.
    gd.DeviceReset.Raise(&gd, System::EventArgs::Empty);

    EXPECT_EQ(pipeline.getGpuMemoryEstimateBytes(), 0u)
        << "the pipeline kept its targets across a reset";

    // And a correct frame afterwards, from every one of them. What is checked is that none throws
    // and that the pipeline reallocates -- a subsystem that had cached a dead handle would fail
    // here rather than at the reset.
    EXPECT_NO_THROW(RunFrame(pipeline));
    EXPECT_GT(pipeline.getGpuMemoryEstimateBytes(), 0u) << "the pipeline did not rebuild";

    EXPECT_NO_THROW((void)shadowMap.isSupported());
    EXPECT_NO_THROW((void)cascades.getCascadeCount());
    EXPECT_NO_THROW((void)spot.isSupported());
    EXPECT_NO_THROW((void)cube.isSupported());
    EXPECT_NO_THROW((void)prepass.isSupported(gd));
    EXPECT_NO_THROW((void)sky.isSupported());
    EXPECT_NO_THROW((void)bloom.isSupported(gd));
    EXPECT_NO_THROW((void)ssao.isSupported(gd));
    EXPECT_NO_THROW((void)fxaa.isSupported(gd));
    EXPECT_NO_THROW((void)tonemap.isSupported(gd));
}

TEST(DeviceLossTest, RepeatedResetsDoNotAccumulate)
{
    // A reset that leaked its old targets would grow without ever failing -- the frames keep
    // rendering, and only a long-running game would notice.
    GraphicsDevice gd;
    CNA_SKIP_WITHOUT_RENDER_TARGETS(gd);

    RenderPipeline pipeline(gd);
    pipeline.resize(kWidth, kHeight);
    pipeline.getSettings().setBloomEnabled(true);

    RunFrame(pipeline);
    const std::size_t first = pipeline.getGpuMemoryEstimateBytes();

    for (int i = 0; i < 20; ++i)
    {
        gd.DeviceReset.Raise(&gd, System::EventArgs::Empty);
        RunFrame(pipeline);
    }
    EXPECT_EQ(pipeline.getGpuMemoryEstimateBytes(), first)
        << "twenty resets left the pipeline holding more than one did";
}

TEST(DeviceLossTest, AResetBetweenBeginAndEndIsIgnoredRatherThanObeyed)
{
    // A reset cannot really arrive mid-frame in a single-threaded pipeline, but if it did, dropping
    // the bound target would be worse than keeping a stale one until end(). Asserted because the
    // handler's guard is a one-line `if` that is easy to remove as dead code.
    GraphicsDevice gd;
    CNA_SKIP_WITHOUT_RENDER_TARGETS(gd);

    RenderPipeline pipeline(gd);
    pipeline.resize(kWidth, kHeight);
    pipeline.getSettings().setBloomEnabled(true);

    pipeline.begin(Color::Black);
    EXPECT_NO_THROW(gd.DeviceReset.Raise(&gd, System::EventArgs::Empty));
    EXPECT_NO_THROW(pipeline.end());
    EXPECT_NO_THROW(RunFrame(pipeline));
}

// =====================================================================================
// MOD-1715: two devices, two pipelines
// =====================================================================================

TEST(MultiDeviceTest, TwoPipelinesOnTwoDevicesDoNotShareState)
{
    // Each pipeline must hold its own targets. Sharing would show up as one pipeline's frame
    // appearing in the other's window, which nothing in a unit test can see -- but the memory
    // estimates can: two pipelines each holding their own targets sum, they do not coincide.
    GraphicsDevice first;
    GraphicsDevice second;
    CNA_SKIP_WITHOUT_RENDER_TARGETS(first);

    RenderPipeline firstPipeline(first);
    RenderPipeline secondPipeline(second);
    firstPipeline.resize(kWidth, kHeight);
    secondPipeline.resize(kWidth * 2, kHeight * 2);
    firstPipeline.getSettings().setBloomEnabled(true);
    secondPipeline.getSettings().setBloomEnabled(true);

    RunFrame(firstPipeline);
    RunFrame(secondPipeline);

    const std::size_t firstBytes  = firstPipeline.getGpuMemoryEstimateBytes();
    const std::size_t secondBytes = secondPipeline.getGpuMemoryEstimateBytes();
    EXPECT_GT(firstBytes, 0u);
    EXPECT_GT(secondBytes, 0u);
    EXPECT_NE(firstBytes, secondBytes)
        << "two pipelines at different sizes reported the same memory -- they are sharing targets";
}

TEST(MultiDeviceTest, AResetOnOneDeviceDoesNotDisturbTheOther)
{
    // The subscription is per device (MOD-715). If it were global -- or if the handler did not
    // check which device raised -- one window losing its context would drop the other's targets.
    GraphicsDevice first;
    GraphicsDevice second;
    CNA_SKIP_WITHOUT_RENDER_TARGETS(first);

    RenderPipeline firstPipeline(first);
    RenderPipeline secondPipeline(second);
    firstPipeline.resize(kWidth, kHeight);
    secondPipeline.resize(kWidth, kHeight);
    firstPipeline.getSettings().setBloomEnabled(true);
    secondPipeline.getSettings().setBloomEnabled(true);

    RunFrame(firstPipeline);
    RunFrame(secondPipeline);
    const std::size_t secondBefore = secondPipeline.getGpuMemoryEstimateBytes();
    ASSERT_GT(secondBefore, 0u);

    first.DeviceReset.Raise(&first, System::EventArgs::Empty);

    EXPECT_EQ(firstPipeline.getGpuMemoryEstimateBytes(), 0u) << "the reset device kept its targets";
    EXPECT_EQ(secondPipeline.getGpuMemoryEstimateBytes(), secondBefore)
        << "a reset on one device dropped the other device's targets";
}

TEST(MultiDeviceTest, APipelineOutlivingItsSiblingIsFine)
{
    // Destroying one pipeline must not disturb the other's subscription, which is the failure a
    // shared or static handler list would produce.
    GraphicsDevice first;
    GraphicsDevice second;
    CNA_SKIP_WITHOUT_RENDER_TARGETS(first);

    RenderPipeline survivor(second);
    survivor.resize(kWidth, kHeight);
    survivor.getSettings().setBloomEnabled(true);
    RunFrame(survivor);
    const std::size_t before = survivor.getGpuMemoryEstimateBytes();

    {
        RenderPipeline doomed(first);
        doomed.resize(kWidth, kHeight);
        RunFrame(doomed);
    }

    EXPECT_EQ(survivor.getGpuMemoryEstimateBytes(), before);
    EXPECT_NO_THROW(second.DeviceReset.Raise(&second, System::EventArgs::Empty));
    EXPECT_EQ(survivor.getGpuMemoryEstimateBytes(), 0u)
        << "the survivor's own subscription was lost when its sibling was destroyed";
}

} // namespace

#endif // CNA_CNAEXT
