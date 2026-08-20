// SPDX-License-Identifier: MS-PL
// plans/plan_modern.md MOD-1713: the engine layer under abuse.
//
// Everything here is a shape a game will eventually produce by accident -- a window dragged to one
// pixel, a settings menu whose every switch is flipped between two frames, a pipeline resized every
// frame for a hundred frames. None of it should crash, and where a value is refused it should be
// refused in one predictable way rather than clamped by one class and thrown at by another.

#ifdef CNA_CNAEXT

#include <gtest/gtest.h>

#include "EngineTestSupport.hpp"

#include "CNA/Graphics/LodGroupEXT.hpp"
#include "CNA/Graphics/RenderPipeline.hpp"
#include "CNA/Graphics/RenderPipelineSettings.hpp"
#include "CNA/Graphics/ShadowMap.hpp"
#include "CNA/GraphicsCapability.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"

#include <stdexcept>
#include <vector>

using Microsoft::Xna::Framework::Color;
using Microsoft::Xna::Framework::Graphics::GraphicsDevice;
using CNA::Graphics::LodGroupEXT;
using CNA::Graphics::RenderPipeline;
using CNA::Graphics::RenderQuality;
using CNA::Graphics::ShadowQuality;
using CNA::Graphics::TonemappingMode;

namespace {

    /// A frame at whatever size, with every enabled pass running.
    void RunFrame(RenderPipeline& pipeline)
    {
        pipeline.begin(Color::CornflowerBlue);
        pipeline.end();
    }

} // namespace

TEST(EngineLayerRobustnessTest, AbsurdSizesAreEitherRenderedOrRefusedByRule)
{
    GraphicsDevice gd;
    CNA_SKIP_WITHOUT_RENDER_TARGETS(gd);
    RenderPipeline pipeline(gd);
    pipeline.getSettings().setHDREnabled(true);
    pipeline.getSettings().setBloomEnabled(true);

    // Zero and negative are refused -- there is no sensible frame of that size, and silently
    // clamping to 1 would hide a caller's arithmetic bug for as long as it took to look wrong.
    EXPECT_THROW(pipeline.resize(0, 0), std::invalid_argument);
    EXPECT_THROW(pipeline.resize(-16, 16), std::invalid_argument);
    EXPECT_THROW(pipeline.resize(16, 0), std::invalid_argument);

    // Everything from one pixel up is a real frame and must survive a whole begin/end.
    for (const auto [width, height] : std::vector<std::pair<int, int>>{
             {1, 1}, {1, 720}, {1280, 1}, {17, 23}, {2048, 2048}})
    {
        pipeline.resize(width, height);
        EXPECT_NO_THROW(RunFrame(pipeline)) << width << "x" << height;
    }
}

TEST(EngineLayerRobustnessTest, ResizingEveryFrameDoesNotAccumulate)
{
    // A window being dragged produces a new size every frame. The pipeline must drop the targets it
    // is no longer using rather than keep one per size it has ever seen.
    GraphicsDevice gd;
    CNA_SKIP_WITHOUT_RENDER_TARGETS(gd);
    RenderPipeline pipeline(gd);
    pipeline.getSettings().setHDREnabled(true);
    pipeline.getSettings().setBloomEnabled(true);
    pipeline.getSettings().setSSAOEnabled(true);

    pipeline.resize(256, 256);
    RunFrame(pipeline);
    const std::size_t baseline = pipeline.getGpuMemoryEstimateBytes();

    for (int frame = 0; frame < 100; ++frame)
    {
        pipeline.resize(200 + (frame % 60), 150 + (frame % 45));
        RunFrame(pipeline);
    }

    pipeline.resize(256, 256);
    RunFrame(pipeline);
    EXPECT_EQ(pipeline.getGpuMemoryEstimateBytes(), baseline)
        << "a hundred resizes left memory behind";
}

TEST(EngineLayerRobustnessTest, EverySettingCanBeFlippedBetweenFrames)
{
    // The settings menu case: nothing here has an order it must be changed in, and no combination
    // may throw. The pipeline reads the settings fresh each frame precisely so this is true.
    GraphicsDevice gd;
    CNA_SKIP_WITHOUT_RENDER_TARGETS(gd);
    RenderPipeline pipeline(gd);
    pipeline.resize(128, 128);

    const std::vector<TonemappingMode> modes{TonemappingMode::None, TonemappingMode::Reinhard,
                                             TonemappingMode::Filmic, TonemappingMode::Aces};
    const std::vector<RenderQuality> qualities{RenderQuality::Low, RenderQuality::Medium,
                                               RenderQuality::High, RenderQuality::Ultra};
    const std::vector<ShadowQuality> shadowQualities{
        ShadowQuality::Disabled, ShadowQuality::Low, ShadowQuality::Medium, ShadowQuality::High,
        ShadowQuality::Ultra};

    for (int frame = 0; frame < 64; ++frame)
    {
        auto& settings = pipeline.getSettings();
        settings.setHDREnabled(frame % 2 == 0);
        settings.setBloomEnabled(frame % 3 == 0);
        settings.setSSAOEnabled(frame % 5 == 0);
        settings.setFXAAEnabled(frame % 7 == 0);
        settings.setShadowsEnabled(frame % 4 == 0);
        settings.setTonemappingMode(modes[static_cast<std::size_t>(frame) % modes.size()]);
        settings.setRenderQuality(qualities[static_cast<std::size_t>(frame) % qualities.size()]);
        settings.setShadowQuality(
            shadowQualities[static_cast<std::size_t>(frame) % shadowQualities.size()]);
        settings.setExposure(0.25f + static_cast<float>(frame % 8));
        settings.setGamma(1.0f + static_cast<float>(frame % 3));
        settings.setBloomIntensity(static_cast<float>(frame % 4));
        EXPECT_NO_THROW(RunFrame(pipeline)) << "frame " << frame;
    }
}

TEST(EngineLayerRobustnessTest, AFrameCanBeOpenedAndClosedRepeatedlyWithoutDrift)
{
    GraphicsDevice gd;
    CNA_SKIP_WITHOUT_RENDER_TARGETS(gd);
    RenderPipeline pipeline(gd);
    pipeline.getSettings().setHDREnabled(true);
    pipeline.getSettings().setBloomEnabled(true);
    pipeline.resize(64, 64);

    RunFrame(pipeline);
    const int firstPassCount = pipeline.getLastFramePassCount();
    const std::size_t firstMemory = pipeline.getGpuMemoryEstimateBytes();
    for (int frame = 0; frame < 200; ++frame) RunFrame(pipeline);

    EXPECT_EQ(pipeline.getLastFramePassCount(), firstPassCount);
    EXPECT_EQ(pipeline.getGpuMemoryEstimateBytes(), firstMemory);

    // The two errors a frame can be in, both refused rather than tolerated.
    pipeline.begin(Color::Black);
    EXPECT_THROW(pipeline.begin(Color::Black), std::logic_error);
    pipeline.end();
    EXPECT_THROW(pipeline.end(), std::logic_error);
}

TEST(EngineLayerRobustnessTest, DegenerateInputsToTheHelpersAreRefusedNotUndefined)
{
    GraphicsDevice gd;

    // A shadow map takes a quality rather than a size, so there is no absurd size to pass -- the
    // enumeration is the validation. Every value of it must construct, including the disabled one.
    for (const ShadowQuality quality : {ShadowQuality::Disabled, ShadowQuality::Low,
                                        ShadowQuality::Medium, ShadowQuality::High,
                                        ShadowQuality::Ultra})
        EXPECT_NO_THROW(CNA::Graphics::ShadowMap(gd, quality));

    // An empty LOD group answers rather than reads past its own end.
    LodGroupEXT empty;
    EXPECT_EQ(empty.selectIndex(0.0f), -1);
    EXPECT_EQ(empty.selectIndex(1e30f), -1);
    EXPECT_EQ(empty.select(-1e30f), nullptr);
}

#endif // CNA_CNAEXT
