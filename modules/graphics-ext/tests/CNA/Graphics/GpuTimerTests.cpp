// SPDX-License-Identifier: MS-PL
// plan_modern.md MOD-2163: GPU timer queries.
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
    GraphicsDevice gd;
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
    GraphicsDevice gd;
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
    GraphicsDevice gd;
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
    GraphicsDevice gd;
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

    // Never blocks, so the result is collected by asking repeatedly rather than by waiting.
    bool collected = false;
    for (int attempt = 0; attempt < 10000 && !collected; ++attempt)
    {
        gd.Clear(Color::Black);
        collected = timer.poll();
    }

    ASSERT_TRUE(collected) << "the GPU never finished a range of fifty clears";
    std::printf("    fifty 256x256 clears took %.4f ms on the GPU\n", timer.getLastMilliseconds());
    EXPECT_GE(timer.getLastMilliseconds(), 0.0);
    EXPECT_EQ(timer.getSampleCount(), 1);
}

TEST(GpuTimerTest, PollingBeforeTheGpuFinishesReturnsFalseRatherThanBlocking)
{
    GraphicsDevice gd;
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
    GraphicsDevice gd;
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

    const double few  = measure(4);
    const double many = measure(40);
    ASSERT_GE(few, 0.0);
    ASSERT_GE(many, 0.0);
    std::printf("    4 full-screen draws %.4f ms, 40 draws %.4f ms\n", few, many);
    EXPECT_GT(many, few * 2.0) << "ten times the fill did not take at least twice as long";
}

} // namespace

#endif // CNA_CNAEXT
