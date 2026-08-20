// SPDX-License-Identifier: MS-PL
// plans/plan_modern.md MOD-2164: per-pass GPU timings through the pipeline.
//
// A frame that is too slow is not a useful fact; a *pass* that is too slow is. The chain already
// knows what it ran and in what order, so it is the one place that can attribute the time without
// the application counting anything.
//
// The property under test that matters most is that switching timing on does not change the frame
// or make it wait. A measurement that stalls the pipeline is measuring the measurement.

#ifdef CNA_CNAEXT

#include <gtest/gtest.h>

#include "EngineTestSupport.hpp"

#include "CNA/Graphics/GpuTimer.hpp"
#include "CNA/Graphics/FxaaPass.hpp"
#include "CNA/Graphics/PostProcessChain.hpp"
#include "CNA/Graphics/PostProcessContext.hpp"
#include "CNA/Graphics/RenderPipeline.hpp"
#include "CNA/Graphics/RenderPipelineSettings.hpp"
#include "CNA/Graphics/TonemapPass.hpp"
#include "CNA/Graphics/TonemappingMode.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/PresentationParameters.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"

#include <cstdio>
#include <memory>
#include <string>
#include <vector>

namespace {

using CNA::Graphics::GpuTimer;
using CNA::Graphics::PostProcessChain;
using CNA::Graphics::PostProcessContext;
using CNA::Graphics::RenderPipeline;
using CNA::Graphics::RenderPipelineSettings;
using CNA::Graphics::TonemappingMode;
using Microsoft::Xna::Framework::Color;
using Microsoft::Xna::Framework::Graphics::GraphicsDevice;
using Microsoft::Xna::Framework::Graphics::RenderTarget2D;
using Microsoft::Xna::Framework::Graphics::Texture2D;

constexpr int kSize = 128;

std::unique_ptr<Texture2D> MakeScene(GraphicsDevice& gd)
{
    auto texture = std::make_unique<Texture2D>(gd, kSize, kSize);
    std::vector<Color> texels;
    texels.reserve(static_cast<std::size_t>(kSize) * kSize);
    for (int y = 0; y < kSize; ++y)
        for (int x = 0; x < kSize; ++x) texels.emplace_back(x % 256, y % 256, 128, 255);
    texture->SetData(texels.data(), static_cast<int>(texels.size()));
    return texture;
}

PostProcessContext MakeContext(Texture2D* scene, RenderTarget2D* destination)
{
    PostProcessContext context;
    context.source      = scene;
    context.destination = destination;
    context.width       = kSize;
    context.height      = kSize;
    return context;
}

/// Runs the chain enough times for a late-arriving result to land.
///
/// A one-texel read-back per frame, because on a software rasteriser nothing else here forces the
/// batch through: without it a run of eight frames retires one query rather than eight, and a test
/// that waited longer would only queue more work. A real game presents a frame, which does the same
/// job.
void RunFrames(PostProcessChain& chain, const PostProcessContext& context, const int frames)
{
    Color probe = Color::Black;
    const Microsoft::Xna::Framework::Rectangle oneTexel(0, 0, 1, 1);
    for (int i = 0; i < frames; ++i)
    {
        chain.apply(context);
        if (context.destination != nullptr)
            context.destination->GetData(0, &oneTexel, &probe, 0, 1);
    }
}

// ── The switch ──────────────────────────────────────────────────────────────

TEST(PassTimingTest, TimingIsOffByDefaultAndReportsNothing)
{
    GraphicsDevice gd;
    PostProcessChain chain(gd);
    EXPECT_FALSE(chain.isGpuTimingEnabled());
    EXPECT_TRUE(chain.getPassTimings().empty());
}

TEST(PassTimingTest, TurningItOnWhereThereIsNoTimerIsAcceptedAndDoesNothing)
{
    // The distinction the whole design turns on: an empty list, not a list of zeroes. A caller has
    // to be able to tell "not measured here" from "this pass took no time".
    GraphicsDevice gd;
    CNA_SKIP_WITHOUT_RENDER_TARGETS(gd);
    {
        GpuTimer probe(gd);
        if (probe.isSupported()) GTEST_SKIP() << "this renderer has a GPU timer";
    }

    auto scene = MakeScene(gd);
    RenderTarget2D destination(gd, kSize, kSize);
    PostProcessChain chain(gd);
    chain.addOwnedPass(std::make_unique<CNA::Graphics::TonemapPass>(gd));

    EXPECT_NO_THROW(chain.setGpuTimingEnabled(true));
    RunFrames(chain, MakeContext(scene.get(), &destination), 3);

    EXPECT_FALSE(chain.isGpuTimingEnabled());
    EXPECT_TRUE(chain.getPassTimings().empty());
}

// ── The measurement ─────────────────────────────────────────────────────────

TEST(PassTimingTest, EachPassReportsItsOwnNameAndItsOwnTime)
{
    GraphicsDevice gd;
    CNA_SKIP_WITHOUT_RENDER_TARGETS(gd);
    {
        GpuTimer probe(gd);
        if (!probe.isSupported()) GTEST_SKIP() << probe.getUnsupportedReason();
    }

    auto scene = MakeScene(gd);
    RenderTarget2D destination(gd, kSize, kSize);

    PostProcessChain chain(gd);
    chain.addOwnedPass(std::make_unique<CNA::Graphics::TonemapPass>(gd));
    chain.addOwnedPass(std::make_unique<CNA::Graphics::FxaaPass>(gd));
    chain.setGpuTimingEnabled(true);

    // The result arrives late by design, so a single frame is not expected to have one.
    RunFrames(chain, MakeContext(scene.get(), &destination), 8);

    ASSERT_EQ(chain.getPassTimings().size(), 2u);
    EXPECT_EQ(chain.getPassTimings()[0].Name, "Tonemap");
    EXPECT_EQ(chain.getPassTimings()[1].Name, "FXAA");

    for (const auto& timing : chain.getPassTimings())
        std::printf("    %-10s %.4f ms over %d samples\n", timing.Name.c_str(),
                    timing.Milliseconds, timing.SampleCount);

    // Every pass, not just the first. Polling *after* the loop rather than before reports a number
    // for whichever pass the driver happened to have retired and nothing for the rest, which looks
    // like a working measurement of a chain with one pass in it.
    for (const auto& timing : chain.getPassTimings())
    {
        EXPECT_GT(timing.SampleCount, 0) << timing.Name << " reported nothing over eight frames";
        EXPECT_GE(timing.Milliseconds, 0.0);
    }
}

TEST(PassTimingTest, TimingDoesNotChangeTheFrame)
{
    // The claim a caller has to be able to rely on before switching it on in a real build.
    GraphicsDevice gd;
    CNA_SKIP_WITHOUT_RENDER_TARGETS(gd);
    CNA_SKIP_WITHOUT_RENDER_TARGET_READBACK(gd);

    auto scene = MakeScene(gd);
    RenderTarget2D destination(gd, kSize, kSize);

    PostProcessChain chain(gd);
    chain.addOwnedPass(std::make_unique<CNA::Graphics::TonemapPass>(gd));
    const PostProcessContext context = MakeContext(scene.get(), &destination);

    chain.apply(context);
    std::vector<Color> untimed(static_cast<std::size_t>(kSize) * kSize, Color::Black);
    destination.GetData(untimed.data(), static_cast<int>(untimed.size()));

    chain.setGpuTimingEnabled(true);
    RunFrames(chain, context, 4);
    std::vector<Color> timed(static_cast<std::size_t>(kSize) * kSize, Color::Black);
    destination.GetData(timed.data(), static_cast<int>(timed.size()));

    for (std::size_t i = 0; i < untimed.size(); ++i)
        ASSERT_EQ(untimed[i].getRProperty(), timed[i].getRProperty()) << "at pixel " << i;
}

TEST(PassTimingTest, SwitchingItOffForgetsWhatItMeasured)
{
    GraphicsDevice gd;
    CNA_SKIP_WITHOUT_RENDER_TARGETS(gd);

    auto scene = MakeScene(gd);
    RenderTarget2D destination(gd, kSize, kSize);

    PostProcessChain chain(gd);
    chain.addOwnedPass(std::make_unique<CNA::Graphics::TonemapPass>(gd));
    chain.setGpuTimingEnabled(true);
    RunFrames(chain, MakeContext(scene.get(), &destination), 4);

    chain.setGpuTimingEnabled(false);
    EXPECT_FALSE(chain.isGpuTimingEnabled());
    EXPECT_TRUE(chain.getPassTimings().empty())
        << "stale timings survived being switched off, which is how a stopped measurement becomes "
           "a lie";
}

TEST(PassTimingTest, AnEmptyChainReportsNoTimings)
{
    GraphicsDevice gd;
    CNA_SKIP_WITHOUT_RENDER_TARGETS(gd);

    auto scene = MakeScene(gd);
    RenderTarget2D destination(gd, kSize, kSize);

    PostProcessChain chain(gd);
    chain.setGpuTimingEnabled(true);
    RunFrames(chain, MakeContext(scene.get(), &destination), 2);
    EXPECT_TRUE(chain.getPassTimings().empty());
}

// ── Through the pipeline ────────────────────────────────────────────────────

TEST(PassTimingTest, ThePipelineSurfacesTheChainsTimings)
{
    GraphicsDevice gd;
    CNA_SKIP_WITHOUT_RENDER_TARGETS(gd);

    RenderPipeline pipeline(gd);
    pipeline.resize(kSize, kSize);
    EXPECT_FALSE(pipeline.isGpuTimingEnabledEXT());
    EXPECT_TRUE(pipeline.getPassTimingsEXT().empty());

    RenderPipelineSettings& settings = pipeline.getSettings();
    settings.setTonemappingMode(TonemappingMode::Aces);
    settings.setFXAAEnabled(true);

    pipeline.setGpuTimingEnabledEXT(true);
    // The back buffer's own size, not the pipeline's: `resize` sets the pipeline's intermediates
    // and leaves the device's back buffer alone, and a read of the wrong length is refused.
    const auto& presentation = gd.getPresentationParametersProperty();
    std::vector<Color> frameBuffer(
        static_cast<std::size_t>(presentation.getBackBufferWidthProperty())
            * presentation.getBackBufferHeightProperty(), Color::Black);
    bool readable = !frameBuffer.empty();
    for (int frame = 0; frame < 8; ++frame)
    {
        pipeline.begin(Color::CornflowerBlue);
        pipeline.end();
        // A real game presents here, and presenting is what retires the frame's queries. Reading
        // the back buffer does the same job; without it the last pass's range is still in flight
        // when the next frame collects, forever.
        if (readable)
            try { gd.GetBackBufferData(frameBuffer.data(), static_cast<int>(frameBuffer.size())); }
            catch (...) { readable = false; }
    }

    {
        GpuTimer probe(gd);
        if (!probe.isSupported())
        {
            EXPECT_FALSE(pipeline.isGpuTimingEnabledEXT());
            EXPECT_TRUE(pipeline.getPassTimingsEXT().empty());
            GTEST_SKIP() << probe.getUnsupportedReason();
        }
    }

    ASSERT_FALSE(pipeline.getPassTimingsEXT().empty())
        << "the pipeline ran a chain and reported nothing about it";
    for (const auto& timing : pipeline.getPassTimingsEXT())
        std::printf("    pipeline pass %-10s %.4f ms over %d samples\n", timing.Name.c_str(),
                    timing.Milliseconds, timing.SampleCount);
    EXPECT_TRUE(pipeline.isGpuTimingEnabledEXT());
    if (readable)
        for (const auto& timing : pipeline.getPassTimingsEXT())
            EXPECT_GT(timing.SampleCount, 0)
                << timing.Name << " reported nothing over eight presented frames";
}

} // namespace

#endif // CNA_CNAEXT
