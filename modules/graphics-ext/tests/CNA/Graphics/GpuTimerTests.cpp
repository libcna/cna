// SPDX-License-Identifier: MS-PL
// plans/plan_modern.md MOD-2163: GPU timer queries.
//
// Every number in docs/cnaext-perf.md was measured with a CPU wall clock wrapped around a one-texel
// read-back. That works, and it measures the wrong thing twice over: the clock starts when the
// driver *accepts* the work rather than when the GPU starts it, and the read-back that forces
// completion is a synchronisation the real frame would never perform.
//
// These tests are written so they say something on a renderer that has no timer query at all, which
// is the case on this machine. Where the query is absent the claim under test is the refusal: it
// must be a refusal, with a reason, and never a CPU number wearing a GPU name.

#ifdef CNA_CNAEXT

#include <gtest/gtest.h>

#include "EngineTestSupport.hpp"

#include "CNA/Graphics/GpuTimer.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColor.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <string>

namespace {

using CNA::Graphics::GpuTimer;
using Microsoft::Xna::Framework::Color;
using Microsoft::Xna::Framework::Rectangle;
using Microsoft::Xna::Framework::Graphics::GraphicsDevice;
using Microsoft::Xna::Framework::Graphics::RenderTarget2D;

// ── The refusal, which is the case on renderers without the extension ────────

TEST(GpuTimerTest, AnUnsupportedTimerSaysSoAndSaysWhy)
{
    // The property that matters most, because it is the one a caller acts on. An unsupported timer
    // that quietly returned zero would read as "this pass is free".
    CnaTest::EngineLayer::HiDefDevice gd;
    GpuTimer timer(gd);

    std::printf("    GPU timer supported: %s%s%s\n", timer.isSupported() ? "yes" : "no",
                timer.isSupported() ? "" : " -- ", timer.getUnsupportedReason().c_str());

    if (timer.isSupported())
    {
        EXPECT_TRUE(timer.getUnsupportedReason().empty());
    }
    else
    {
        EXPECT_FALSE(timer.getUnsupportedReason().empty())
            << "the timer refused without saying why";
        EXPECT_NE(timer.getUnsupportedReason().find("timer"), std::string::npos)
            << timer.getUnsupportedReason();
    }
}

TEST(GpuTimerTest, AnUnsupportedTimerIsInertRatherThanFatal)
{
    // A pipeline that measures itself must run the same on a renderer that cannot be measured.
    CnaTest::EngineLayer::HiDefDevice gd;
    GpuTimer timer(gd);
    if (timer.isSupported()) GTEST_SKIP() << "this renderer has a GPU timer; see the cases below";

    EXPECT_NO_THROW(timer.begin());
    EXPECT_NO_THROW(timer.end());
    EXPECT_FALSE(timer.isOpen());
    EXPECT_FALSE(timer.isResultAvailable());
    EXPECT_FALSE(timer.poll());
    EXPECT_DOUBLE_EQ(timer.getLastMilliseconds(), 0.0);
    EXPECT_EQ(timer.getSampleCount(), 0);
}

TEST(GpuTimerTest, AnUnsupportedTimerNeverInventsANumber)
{
    // The whole reason a CPU fallback is not offered. A timer that fell back to a wall clock would
    // return a plausible number here and it would be the time the driver took to accept the work --
    // exactly the quantity GPU timing exists to see past.
    CnaTest::EngineLayer::HiDefDevice gd;
    GpuTimer timer(gd);
    if (timer.isSupported()) GTEST_SKIP() << "this renderer has a GPU timer";

    CNA_SKIP_WITHOUT_RENDER_TARGETS(gd);
    RenderTarget2D target(gd, 128, 128);
    timer.begin();
    gd.SetRenderTarget(&target);
    for (int i = 0; i < 20; ++i) gd.Clear(Color::Blue);
    gd.SetRenderTarget(nullptr);
    timer.end();

    EXPECT_DOUBLE_EQ(timer.getLastMilliseconds(), 0.0)
        << "an unsupported timer produced a number for work that really did happen";
}

// ── The measurement, where the query exists ─────────────────────────────────

TEST(GpuTimerTest, AClosedRangeEventuallyReportsANonNegativeTime)
{
    CnaTest::EngineLayer::HiDefDevice gd;
    GpuTimer timer(gd);
    if (!timer.isSupported())
        GTEST_SKIP() << timer.getUnsupportedReason();
    CNA_SKIP_WITHOUT_RENDER_TARGETS(gd);

    RenderTarget2D target(gd, 256, 256);
    timer.begin();
    EXPECT_TRUE(timer.isOpen());
    gd.SetRenderTarget(&target);
    for (int i = 0; i < 50; ++i) gd.Clear(Color::Blue);
    gd.SetRenderTarget(nullptr);
    timer.end();
    EXPECT_FALSE(timer.isOpen());

    // Deferred renderers do not submit a public frame from GpuTimer::end(); doing so would turn a
    // measurement helper into a hidden present boundary. Submit exactly as an application does.
    gd.Present();

    // Never blocks, so the result is collected by asking repeatedly rather than by waiting.
    bool collected = false;
    for (int attempt = 0; attempt < 10000 && !collected; ++attempt)
    {
        gd.Clear(Color::Black);
        collected = timer.poll();
        if (!collected && (attempt % 8) == 7) gd.Present();
    }

    ASSERT_TRUE(collected) << "the GPU never finished a range of fifty clears";
    std::printf("    fifty 256x256 clears took %.4f ms on the GPU\n", timer.getLastMilliseconds());
    EXPECT_GE(timer.getLastMilliseconds(), 0.0);
    EXPECT_EQ(timer.getSampleCount(), 1);
}

TEST(GpuTimerTest, PollingBeforeTheGpuFinishesReturnsFalseRatherThanBlocking)
{
    CnaTest::EngineLayer::HiDefDevice gd;
    GpuTimer timer(gd);
    if (!timer.isSupported()) GTEST_SKIP() << timer.getUnsupportedReason();

    // Nothing was ever timed, so there is nothing to collect and the answer must be no.
    EXPECT_FALSE(timer.isResultAvailable());
    EXPECT_FALSE(timer.poll());
}

TEST(GpuTimerTest, MoreWorkTakesMoreGpuTime)
{
    // The claim that makes the number a measurement rather than a reading. Asserted as a ratio
    // between two amounts of the same work, because absolute times on any one machine mean nothing.
    //
    // The workload is **draws, not clears**. The first version of this test timed ten clears
    // against a hundred and the hundred came back *faster*: a driver is free to collapse repeated
    // full-target clears with nothing between them into one, and llvmpipe does. A workload the
    // driver can optimise away measures the optimiser.
    CnaTest::EngineLayer::HiDefDevice gd;
    GpuTimer timer(gd);
    if (!timer.isSupported()) GTEST_SKIP() << timer.getUnsupportedReason();
    CNA_SKIP_WITHOUT_RENDER_TARGETS(gd);

    using Microsoft::Xna::Framework::Matrix;
    using Microsoft::Xna::Framework::Vector3;
    using Microsoft::Xna::Framework::Graphics::BasicEffect;
    using Microsoft::Xna::Framework::Graphics::PrimitiveType;
    using Microsoft::Xna::Framework::Graphics::RasterizerState;
    using Microsoft::Xna::Framework::Graphics::VertexPositionColor;

    const auto at = [](const float x, const float y) {
        return VertexPositionColor(Vector3(x, y, 0.0f), Color(60, 60, 60, 255));
    };
    const VertexPositionColor quad[6] = {
        at(-1.0f, -1.0f), at(-1.0f, 1.0f), at(1.0f, 1.0f),
        at(-1.0f, -1.0f), at(1.0f, 1.0f),  at(1.0f, -1.0f),
    };

    RenderTarget2D target(gd, 512, 512);
    BasicEffect effect(gd);
    effect.VertexColorEnabled = true;
    effect.World      = Matrix::getIdentityProperty();
    effect.View       = Matrix::getIdentityProperty();
    effect.Projection = Matrix::getIdentityProperty();

    const auto measure = [&](const int draws) {
        timer.begin();
        gd.SetRenderTarget(&target);
        gd.setRasterizerStateProperty(RasterizerState::CullNone);
        gd.Clear(Color::Black);
        effect.Apply();
        gd.SetVertexBuffer(nullptr);
        for (int i = 0; i < draws; ++i)
            gd.DrawUserPrimitives(PrimitiveType::TriangleList, quad, 0, 2);
        gd.SetRenderTarget(nullptr);
        timer.end();

        // Make the asynchronous contract explicit for deferred APIs. Immediate GL simply submits
        // an otherwise ordinary frame here; Vulkan records the two timestamps with this work.
        gd.Present();

        // A one-texel read-back to force the batch through. Polling in a spin loop is what a game
        // does and what `PollingBeforeTheGpuFinishesReturnsFalseRatherThanBlocking` covers; here the
        // point is a number, and on a software rasteriser a spin of cheap clears can keep appending
        // to the same batch faster than the driver retires it, so the loop never sees the result.
        Color probe = Color::Black;
        const Rectangle oneTexel(0, 0, 1, 1);
        target.GetData(0, &oneTexel, &probe, 0, 1);

        for (int attempt = 0; attempt < 1000; ++attempt)
            if (timer.poll()) return timer.getLastMilliseconds();
        return -1.0;
    };

    // The first use may compile/link the pipeline lazily inside a software driver. Keep that
    // one-time cost out of a scaling assertion that is meant to compare steady-state fill work.
    const double warmUp = measure(4);
    ASSERT_GE(warmUp, 0.0);
    const double few  = measure(4);
    const double many = measure(40);
    ASSERT_GE(few, 0.0);
    ASSERT_GE(many, 0.0);
    std::printf("    4 full-screen draws %.4f ms, 40 draws %.4f ms\n", few, many);
    EXPECT_GT(many, few * 2.0) << "ten times the fill did not take at least twice as long";
}

// ── The deterministic scaling validation ────────────────────────────────────

TEST(GpuTimerTest, TheNumberTracksTheWorkloadAcrossThreeSizes)
{
    // `MoreWorkTakesMoreGpuTime` compares two workloads, which a timer can satisfy by accident:
    // two samples drawn from noise around one constant pass it roughly half the time. This asks
    // the stronger question -- does the number *follow* the work -- across three sizes an order of
    // magnitude apart, and it asks it about the shape of the whole curve rather than one ratio.
    //
    // It exists because a timer can be wrong in a way that still returns a number. On WebGPU the
    // timestamps were written at the boundaries of two *empty compute passes* that nothing ordered
    // against the graphics work between them, so the pair measured a fixed submission overhead:
    // 4 draws read 0.2365 ms and 40 draws read 0.2006 ms -- ten times the fill coming back as
    // slightly less time (plans/plan_webgpu_modern_graphics.md WMG-0017). Every assertion below is
    // chosen to fail on that behaviour and to pass on a real GPU's noise.
    CnaTest::EngineLayer::HiDefDevice gd;
    GpuTimer timer(gd);
    if (!timer.isSupported()) GTEST_SKIP() << timer.getUnsupportedReason();
    CNA_SKIP_WITHOUT_RENDER_TARGETS(gd);

    using Microsoft::Xna::Framework::Matrix;
    using Microsoft::Xna::Framework::Vector3;
    using Microsoft::Xna::Framework::Graphics::BasicEffect;
    using Microsoft::Xna::Framework::Graphics::PrimitiveType;
    using Microsoft::Xna::Framework::Graphics::RasterizerState;
    using Microsoft::Xna::Framework::Graphics::VertexPositionColor;

    const auto at = [](const float x, const float y) {
        return VertexPositionColor(Vector3(x, y, 0.0f), Color(60, 60, 60, 255));
    };
    const VertexPositionColor quad[6] = {
        at(-1.0f, -1.0f), at(-1.0f, 1.0f), at(1.0f, 1.0f),
        at(-1.0f, -1.0f), at(1.0f, 1.0f),  at(1.0f, -1.0f),
    };

    RenderTarget2D target(gd, 512, 512);
    BasicEffect effect(gd);
    effect.VertexColorEnabled = true;
    effect.World      = Matrix::getIdentityProperty();
    effect.View       = Matrix::getIdentityProperty();
    effect.Projection = Matrix::getIdentityProperty();

    // Returns the GPU milliseconds for `draws` full-screen quads, and reports through `wallMs` the
    // CPU time the whole submission and read-back took. The wall clock is not the measurement --
    // it is the plausibility bound: GPU time inside the range cannot exceed the wall clock that
    // encloses submitting it and waiting for it, so a units error (ticks read as nanoseconds when
    // they are not) shows up here as a number larger than the clock that contains it.
    const auto measure = [&](const int draws, double* wallMs) {
        const auto started = std::chrono::steady_clock::now();
        timer.begin();
        gd.SetRenderTarget(&target);
        gd.setRasterizerStateProperty(RasterizerState::CullNone);
        gd.Clear(Color::Black);
        effect.Apply();
        gd.SetVertexBuffer(nullptr);
        for (int i = 0; i < draws; ++i)
            gd.DrawUserPrimitives(PrimitiveType::TriangleList, quad, 0, 2);
        gd.SetRenderTarget(nullptr);
        timer.end();
        gd.Present();

        Color probe = Color::Black;
        const Rectangle oneTexel(0, 0, 1, 1);
        target.GetData(0, &oneTexel, &probe, 0, 1);

        double result = -1.0;
        for (int attempt = 0; attempt < 1000; ++attempt)
            if (timer.poll()) { result = timer.getLastMilliseconds(); break; }
        const auto finished = std::chrono::steady_clock::now();
        if (wallMs != nullptr)
            *wallMs = std::chrono::duration<double, std::milli>(finished - started).count();
        return result;
    };

    // Lazy pipeline creation is a one-time cost and belongs outside a steady-state comparison.
    ASSERT_GE(measure(4, nullptr), 0.0) << "the GPU never finished the warm-up range";

    constexpr int kDraws[3] = { 4, 40, 160 };
    double gpuMs[3] = {};
    double wallMs[3] = {};
    for (int tier = 0; tier < 3; ++tier)
    {
        gpuMs[tier] = measure(kDraws[tier], &wallMs[tier]);
        ASSERT_GE(gpuMs[tier], 0.0) << "the GPU never finished the range of " << kDraws[tier]
                                    << " draws";
        std::printf("    %3d full-screen draws: GPU %.4f ms (CPU wall %.4f ms)\n", kDraws[tier],
                    gpuMs[tier], wallMs[tier]);
    }

    // Finite and non-negative. A NaN or an infinity compares false against every bound below, so
    // it would otherwise slip through the ordering assertions rather than fail them.
    for (int tier = 0; tier < 3; ++tier)
    {
        EXPECT_TRUE(std::isfinite(gpuMs[tier]))
            << kDraws[tier] << " draws produced a non-finite GPU time";
        EXPECT_GE(gpuMs[tier], 0.0) << kDraws[tier] << " draws produced a negative GPU time";
    }

    // Monotone: the curve rises at every step. The broken timer's 0.2365 -> 0.2006 fails here.
    EXPECT_GT(gpuMs[1], gpuMs[0])
        << "ten times the fill did not take longer at all -- the timer is not measuring the work";
    EXPECT_GT(gpuMs[2], gpuMs[1])
        << "four times the fill again did not take longer at all";

    // And it rises by an amount related to the work. Forty times the fill; a factor of four is
    // well under any real scaling (Radeon 780M measures ~27x) and far above the ~1.0 a timer
    // returns when it is measuring a fixed overhead.
    EXPECT_GT(gpuMs[2], gpuMs[0] * 4.0)
        << "forty times the fill took less than four times as long: " << gpuMs[0] << " ms -> "
        << gpuMs[2] << " ms";

    // Plausible in absolute terms. Not a wall-clock equality -- GPU timing is not that -- but the
    // range cannot have taken longer than the CPU spent submitting it and waiting for it. The 5 ms
    // allowance covers a clock whose own overhead is smaller than its resolution.
    for (int tier = 0; tier < 3; ++tier)
        EXPECT_LT(gpuMs[tier], wallMs[tier] + 5.0)
            << kDraws[tier] << " draws reported " << gpuMs[tier]
            << " ms of GPU time inside a submission that took " << wallMs[tier]
            << " ms of wall clock -- the tick-to-nanosecond conversion is wrong";

    // Repeated runs of one workload are plausible: they land in the same neighbourhood rather than
    // wandering. Deliberately loose -- a shared GPU makes any tight bound flaky -- and it is the
    // spread that is under test, not the value.
    double repeats[3] = {};
    for (double& sample : repeats)
    {
        sample = measure(kDraws[1], nullptr);
        ASSERT_GE(sample, 0.0);
    }
    const double lowest  = std::min({ repeats[0], repeats[1], repeats[2] });
    const double highest = std::max({ repeats[0], repeats[1], repeats[2] });
    std::printf("    %d draws repeated three times: %.4f / %.4f / %.4f ms\n", kDraws[1],
                repeats[0], repeats[1], repeats[2]);
    EXPECT_GT(lowest, 0.0) << "a repeat of a workload that measured above zero came back as zero";
    EXPECT_LT(highest, lowest * 20.0)
        << "three runs of the same workload spread further than a shared GPU explains: " << lowest
        << " ms to " << highest << " ms";
}

} // namespace

#endif // CNA_CNAEXT
