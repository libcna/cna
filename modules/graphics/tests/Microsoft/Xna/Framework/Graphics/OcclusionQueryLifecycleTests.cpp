// SPDX-License-Identifier: MS-PL
//
// plans/plan_apple_m4.md AM4-108 -- what an OcclusionQuery run counts and when it completes.
//
//   * XNA lets a query be begun again once IsComplete has been asked, whether or not the result
//     had arrived (OcclusionQueryPixelCountPrecisionTest pins that lifecycle); the earlier run is
//     abandoned, and the new run counts only its own draws.
//   * XNA's IsComplete flushes (Direct3D 9 GetData with D3DGETDATA_FLUSH), so a game may poll it
//     until it turns true without presenting in between.
//   * A run whose draws are separated by a Clear counts both sides of it -- the same tally the
//     same coverage gives without the Clear.
//
// Counts are compared with each other rather than with a pixel area, so a renderer that counts
// physical (for example Retina) samples is measured on its own scale.

#include <chrono>
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
                              PresentationParameters()};

        void SetUp() override
        {
            if (!device.SupportsCapability(GraphicsCapability::OcclusionQuery))
                GTEST_SKIP() << "Renderer explicitly does not support occlusion queries";
            if (CNA::Testing::ActiveRendererIs(CNA::GraphicsRendererType::Headless))
                GTEST_SKIP() << "HEADLESS validates query lifetime but has no raster samples to count";
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

    OcclusionQuery split(device);
    split.Begin();
    DrawColumns(-1, 0);
    device.Clear(Color(0, 0, 0, 255));
    DrawColumns(0, 1);
    split.End();
    ASSERT_TRUE(PollUntilComplete(split));
    const int splitCount = split.getPixelCountProperty();
    if (whole.isPixelCountPreciseEXT())
        EXPECT_EQ(splitCount, wholeCount) << "both halves of the viewport, across the Clear";
    else
        EXPECT_GT(splitCount, 0);
}
