// SPDX-License-Identifier: MS-PL
//
// Apple M4 AM4-108 -- what an OcclusionQuery run counts and when it completes.
//
//   * XNA lets a query be begun again once IsComplete has been asked, whether or not the result
//     had arrived (OcclusionQueryPixelCountPrecisionTest pins that lifecycle); the earlier run is
//     abandoned, and the new run counts only its own draws.
//   * XNA's IsComplete flushes (Direct3D 9 GetData with D3DGETDATA_FLUSH), so a game may poll it
//     until it turns true without presenting in between.
//   * A run whose draws are separated by a Clear counts both sides of it -- the same tally the
//     same coverage gives without the Clear.
//
// TQ-076: use a literal 64x64 viewport and no MSAA, so exact counts have a
// hand-derived area oracle independent of CNA's other query results and window DPI.

#include <chrono>
#include <typeinfo>
#include <optional>
#include <thread>
#include <vector>
#include <gtest/gtest.h>

#include "CNA/GraphicsCapability.hpp"
#include "CNA/RendererTestGate.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/EffectPass.hpp"
#include "Microsoft/Xna/Framework/Graphics/EffectPassCollection.hpp"
#include "Microsoft/Xna/Framework/Graphics/EffectTechnique.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsAdapter.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsProfile.hpp"
#include "Microsoft/Xna/Framework/Graphics/OcclusionQuery.hpp"
#include "Microsoft/Xna/Framework/Graphics/PresentationParameters.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColor.hpp"
#include "Microsoft/Xna/Framework/Graphics/Viewport.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "System/InvalidOperationException.hpp"

using CNA::GraphicsCapability;
using Microsoft::Xna::Framework::Color;
using Microsoft::Xna::Framework::Rectangle;
using Microsoft::Xna::Framework::Vector3;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    class OcclusionQueryLifecycleTest : public ::testing::Test
    {
    protected:
        GraphicsDevice device{GraphicsAdapter::getDefaultAdapterProperty(), GraphicsProfile::HiDef,
                              PresentationParameters::CreateWindowedEXT(800, 480)};

        void SetUp() override
        {
            if (!device.SupportsCapability(GraphicsCapability::OcclusionQuery))
                GTEST_SKIP() << "Renderer explicitly does not support occlusion queries";
            if (CNA::Testing::ActiveRendererIs(CNA::GraphicsRendererType::Headless))
                GTEST_SKIP() << "HEADLESS validates query lifetime but has no raster samples to count";
            device.setViewportProperty(Viewport(0, 0, 64, 64));
            device.Clear(Color(0, 0, 0, 255));
            device.SetDepthTestEnabled(false);
            device.setBlendStateProperty(BlendState::Opaque);
            // NDC quads wind CCW under CNA's default RasterizerState, so they need CullNone.
            device.setRasterizerStateProperty(RasterizerState::CullNone);
            effect_.emplace(device);
            effect_->VertexColorEnabled = true;
        }

        /// Draws the NDC rectangle [x0,x1] x [-1,1].
        void DrawColumns(float x0, float x1)
        {
            const Color red(255, 0, 0, 255);
            const std::vector<VertexPositionColor> quad{
                {Vector3(x0, -1, 0), red}, {Vector3(x1, -1, 0), red},
                {Vector3(x0, 1, 0), red},  {Vector3(x1, 1, 0), red},
            };
            for (EffectPass& pass : effect_->getCurrentTechniqueProperty()->getPassesProperty())
            {
                pass.Apply();
                device.DrawUserPrimitives(PrimitiveType::TriangleStrip, quad.data(), 0, 2,
                                          VertexPositionColor::getVertexDeclarationStatic());
            }
        }

        /// Polls IsComplete alone -- no readback, no Present -- for up to two seconds.
        static bool PollUntilComplete(OcclusionQuery& query)
        {
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
            while (!query.getIsCompleteProperty())
            {
                if (std::chrono::steady_clock::now() > deadline) return false;
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
            return true;
        }

        // Precise queries count samples; visibility-only queries OR them into one bit.
        static int ExpectedCount(const OcclusionQuery& query, int samples)
        {
            return query.isPixelCountPreciseEXT() ? samples : int(samples != 0);
        }

        std::optional<BasicEffect> effect_;
    };
}

TEST_F(OcclusionQueryLifecycleTest, PollingIsCompleteAloneReachesTheResult)
{
    OcclusionQuery query(device);
    query.Begin();
    DrawColumns(-1, 1);
    query.End();
    ASSERT_TRUE(PollUntilComplete(query))
        << "IsComplete must flush, as Direct3D 9's GetData(D3DGETDATA_FLUSH) does, or polling it "
           "between draws and Present never finishes";
    EXPECT_GT(query.getPixelCountProperty(), 0);
    EXPECT_EQ(query.getPixelCountProperty(), ExpectedCount(query, 4096)); // 64 * 64.
}

TEST_F(OcclusionQueryLifecycleTest, ARunBegunAgainBeforeItsResultCountsOnlyItsOwnDraws)
{
    OcclusionQuery query(device);
    query.Begin();
    DrawColumns(-1, 1);
    query.End();
    // XNA's lifecycle: asking IsComplete -- answered or not -- permits the next Begin.
    (void) query.getIsCompleteProperty();
    query.Begin();
    query.End();
    ASSERT_TRUE(PollUntilComplete(query));
    EXPECT_EQ(query.getPixelCountProperty(), 0)
        << "the second run drew nothing; the abandoned first run's samples must not reach it";
}

TEST_F(OcclusionQueryLifecycleTest, AQueryDisposedMidFrameLeavesNothingInTheNextQuerysCount)
{
    {
        OcclusionQuery abandoned(device);
        abandoned.Begin();
        DrawColumns(-1, 1);
        abandoned.End();
    }   // disposed while its draws are still being recorded
    OcclusionQuery next(device);
    next.Begin();
    next.End();
    ASSERT_TRUE(PollUntilComplete(next));
    EXPECT_EQ(next.getPixelCountProperty(), 0)
        << "a disposed query's samples must not land in the next query's count";
}

TEST_F(OcclusionQueryLifecycleTest, ACountSpansAClearBetweenItsDraws)
{
    OcclusionQuery whole(device);
    whole.Begin();
    DrawColumns(-1, 1);
    whole.End();
    ASSERT_TRUE(PollUntilComplete(whole));
    const int wholeCount = whole.getPixelCountProperty();
    ASSERT_GT(wholeCount, 0);
    EXPECT_EQ(wholeCount, ExpectedCount(whole, 4096)); // 64 * 64.

    OcclusionQuery split(device);
    split.Begin();
    DrawColumns(-1, 0);
    device.Clear(Color(0, 0, 0, 255));
    DrawColumns(0, 1);
    split.End();
    ASSERT_TRUE(PollUntilComplete(split));
    const int splitCount = split.getPixelCountProperty();
    EXPECT_EQ(splitCount, ExpectedCount(split, 4096)); // 32 * 64 + 32 * 64.
    if (whole.isPixelCountPreciseEXT())
        EXPECT_EQ(splitCount, wholeCount) << "both halves of the viewport, across the Clear";
    else
        EXPECT_GT(splitCount, 0);
}

// Refresh Begin IL_0046-005d resets the run; get_IsComplete IL_001d-0044
// returns the current native tally, including a legitimate zero after an empty run.
TEST_F(OcclusionQueryLifecycleTest, RepeatedCyclesReplaceCompletedResults)
{
    OcclusionQuery query(device);
    const int samples[] = {4096, 0, 2048, 4096};
    for (int expected : samples)
    {
        SCOPED_TRACE(expected);
        query.Begin();
        EXPECT_FALSE(query.getIsCompleteProperty()) << "a previous result must not survive Begin";
        if (expected != 0) DrawColumns(-1, expected == 2048 ? 0 : 1);
        query.End();
        ASSERT_TRUE(PollUntilComplete(query));
        EXPECT_EQ(query.getPixelCountProperty(), ExpectedCount(query, expected));
        EXPECT_TRUE(query.getIsCompleteProperty());
        EXPECT_EQ(query.getPixelCountProperty(), ExpectedCount(query, expected));
    }
}

// Refresh SetRenderTargets core IL_0000-0557 contains no query-state guard or
// query Begin/End; native query intervals continue across target transitions.
TEST_F(OcclusionQueryLifecycleTest, CountSpansRenderTargetSwitches)
{
    RenderTarget2D target(device, 32, 16, false, SurfaceFormat::Color, DepthFormat::None, 0);
    OcclusionQuery query(device);
    query.Begin();
    DrawColumns(-1, 0); // 32 * 64 = 2048 on the backbuffer.
    device.SetRenderTarget(&target);
    device.Clear(Color::Black);
    DrawColumns(-1, 1); // 32 * 16 = 512 on the target.
    device.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
    device.setViewportProperty(Viewport(0, 0, 64, 64));
    DrawColumns(0, 1); // 32 * 64 = 2048 back on the backbuffer.
    query.End();
    ASSERT_TRUE(PollUntilComplete(query));
    EXPECT_EQ(query.getPixelCountProperty(), ExpectedCount(query, 4608)); // 2048 + 512 + 2048.
}

// Refresh Present core IL_0013-0026 refuses active render targets, not active
// queries; IL_0027-00ce presents without changing OcclusionQuery's pair state.
TEST_F(OcclusionQueryLifecycleTest, PresentInsideQueryPreservesBothSidesOfTheInterval)
{
    OcclusionQuery query(device);
    query.Begin();
    DrawColumns(-1, 0); // 2048 samples.
    device.Present();
    device.setViewportProperty(Viewport(0, 0, 64, 64));
    EXPECT_FALSE(query.getIsCompleteProperty());
    DrawColumns(0, 1); // 2048 samples after Present.
    query.End();
    ASSERT_TRUE(PollUntilComplete(query));
    EXPECT_EQ(query.getPixelCountProperty(), ExpectedCount(query, 4096));
}

TEST_F(OcclusionQueryLifecycleTest, RefusedPresentOnTargetLeavesTheQueryOpen)
{
    RenderTarget2D target(device, 32, 16, false, SurfaceFormat::Color, DepthFormat::None, 0);
    device.SetRenderTarget(&target);
    OcclusionQuery query(device);
    query.Begin();
    DrawColumns(-1, 1);
    try
    {
        device.Present();
        ADD_FAILURE() << "Present with an active target must throw";
    }
    catch (const System::InvalidOperationException& error)
    {
        EXPECT_EQ(typeid(error), typeid(System::InvalidOperationException));
        // Refresh Present IL_001c-0026, CannotPresentActiveRenderTargets resource.
        EXPECT_STREQ(error.what(), "Cannot call Present when a render target is active.");
    }
    EXPECT_FALSE(query.getIsCompleteProperty());
    DrawColumns(-1, 1);
    query.End();
    device.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));
    ASSERT_TRUE(PollUntilComplete(query));
    EXPECT_EQ(query.getPixelCountProperty(), ExpectedCount(query, 1024)); // 2 * 32 * 16.
}

TEST_F(OcclusionQueryLifecycleTest, DisposingTheOnlyActiveQueryDoesNotCaptureLaterDraws)
{
    OcclusionQuery abandoned(device);
    abandoned.Begin();
    DrawColumns(-1, 1);
    abandoned.Dispose(); // no End: remove the sole logical member from the device's active set.
    EXPECT_FALSE(abandoned.HasRenderer());
    DrawColumns(-1, 1); // outside any query, must not enter the following empty interval.
    OcclusionQuery next(device);
    next.Begin();
    next.End();
    ASSERT_TRUE(PollUntilComplete(next));
    EXPECT_EQ(next.getPixelCountProperty(), 0);
    // Follow the empty interval by visible work, so a permanently disabled query also fails.
    next.Begin();
    DrawColumns(-1, 0);
    next.End();
    ASSERT_TRUE(PollUntilComplete(next));
    EXPECT_EQ(next.getPixelCountProperty(), ExpectedCount(next, 2048));
}
