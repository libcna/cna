// SPDX-License-Identifier: MS-PL
// Tasks 447/854: real per-draw-call OcclusionQuery correlation on Vulkan -- a fully visible quad
// should report a POSITIVE OcclusionQuery.PixelCount(), a fully occluded quad should report
// zero/lower.
//
// Vulkan-specific counterpart of examples/easygl_occlusion_query_visible_quad_test.cpp (Task 445),
// examples/easygl_occlusion_query_occluded_quad_test.cpp (Task 446), and
// examples/bgfx_occlusionquery_pixelcount_test.cpp (Tasks 814/815), combined into one file (same
// shape as the Bgfx counterpart). Unlike those, this file's whole point is verifying the previously
// entirely-stubbed Vulkan OcclusionQueryRenderer::Begin()/End() (Task 447/854's real fix: draws are
// tagged with the currently-active query via VulkanRenderer::PushPending3DDraw(), and
// RecordCommandBuffer() wraps the tagged run in a real vkCmdBeginQuery/vkCmdEndQuery pair, with a
// real per-frame vkCmdResetQueryPool before it) -- so PixelCount()/IsComplete() ARE this test's
// primary subject, not an informational aside.
//
// Method: Scenario A draws one quad with nothing hiding it -- PixelCount() should be positive
// once IsComplete() is true. Scenario B draws a nearer opaque occluder quad first (no query
// attached), then the target quad wrapped in Begin()/End() -- the real depth test should reject
// every one of the target's fragments, so PixelCount() should read 0 (or at least far lower than
// Scenario A's own count).
//
// Exit code 0 = all checks PASS, 1 = any FAIL.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/DepthStencilState.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/OcclusionQuery.hpp"
#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColor.hpp"

#include <cstdio>
#include <memory>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

static constexpr int kSize = 64;
static constexpr int kMaxPollFrames = 30;

namespace
{
    const Color kBlack(0, 0, 0, 255);
    const Color kRed(255, 0, 0, 255);
    const Color kBlue(0, 0, 255, 255);

    bool colourMatch(Color got, Color want, int tol = 40)
    {
        return std::abs((int)got.getRProperty() - (int)want.getRProperty()) <= tol
            && std::abs((int)got.getGProperty() - (int)want.getGProperty()) <= tol
            && std::abs((int)got.getBProperty() - (int)want.getBProperty()) <= tol;
    }

    void DrawQuad(GraphicsDevice& dev, float z, const Color& colour)
    {
        const Vector3 tl(-1.0f,  1.0f, z), bl(-1.0f, -1.0f, z);
        const Vector3 br( 1.0f, -1.0f, z), tr( 1.0f,  1.0f, z);
        const VertexPositionColor q[6] = {
            { tl, colour }, { bl, colour }, { br, colour },
            { tl, colour }, { br, colour }, { tr, colour },
        };
        dev.setRasterizerStateProperty(RasterizerState::CullNone);
        dev.DrawUserPrimitives(PrimitiveType::TriangleList, q, 0, 2);
    }

    // Task 447/854's own "multiple draws within one render pass" policy: left/right non-
    // overlapping half-quads, so a query wrapping BOTH draws must sum both contributions (not
    // just the last one) to report the full-screen pixel count.
    void DrawHalfQuad(GraphicsDevice& dev, float xMin, float xMax, float z, const Color& colour)
    {
        const Vector3 tl(xMin,  1.0f, z), bl(xMin, -1.0f, z);
        const Vector3 br(xMax, -1.0f, z), tr(xMax,  1.0f, z);
        const VertexPositionColor q[6] = {
            { tl, colour }, { bl, colour }, { br, colour },
            { tl, colour }, { br, colour }, { tr, colour },
        };
        dev.setRasterizerStateProperty(RasterizerState::CullNone);
        dev.DrawUserPrimitives(PrimitiveType::TriangleList, q, 0, 2);
    }
}

class VulkanOcclusionQueryPixelCountTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    bool done_   = false;
    int  result_ = 1;

    struct ScenarioResult { Color centre; bool complete; int pixelCount; };

    // occluded=false -> Scenario A: single quad, nothing hides it.
    // occluded=true  -> Scenario B: opaque nearer occluder drawn first, real depth test.
    ScenarioResult RunScenario(GraphicsDevice& dev, bool occluded)
    {
        std::unique_ptr<OcclusionQuery> query;
        int frame = 0;
        bool complete = false;
        int pixelCount = -12345;
        Color centre(0, 0, 0, 0);

        while (true)
        {
            BasicEffect fx(dev);
            fx.VertexColorEnabled = true;
            dev.Clear(kBlack);
            dev.setBlendStateProperty(BlendState::Opaque);
            DepthStencilState ds;
            ds.setDepthBufferEnableProperty(true);
            ds.setDepthBufferWriteEnableProperty(true);
            dev.setDepthStencilStateProperty(ds);
            fx.Apply();

            if (frame == 0)
            {
                query = std::make_unique<OcclusionQuery>(dev);
                if (occluded)
                    DrawQuad(dev, 0.1f, kBlue); // nearer opaque occluder, no query attached
                query->Begin();
                DrawQuad(dev, 0.9f, kRed); // target quad -- farther away
                query->End();
                ++frame;
                continue;
            }

            if (occluded) DrawQuad(dev, 0.1f, kBlue);
            DrawQuad(dev, 0.9f, kRed);

            const Rectangle reg(kSize / 2, kSize / 2, 1, 1);
            dev.GetBackBufferData(&reg, &centre, 0, 1);

            complete   = query->getIsCompleteProperty();
            pixelCount = query->getPixelCountProperty();

            if (complete || frame >= kMaxPollFrames)
                break;
            ++frame;
        }

        return { centre, complete, pixelCount };
    }

    // Task 447/854's "multiple draws within one render pass" multi-draw-span policy: both
    // non-overlapping half-quads are drawn between a SINGLE Begin()/End() pair, so a correct
    // implementation must sum both draws' contributions into one query result.
    ScenarioResult RunMultiDrawScenario(GraphicsDevice& dev)
    {
        std::unique_ptr<OcclusionQuery> query;
        int frame = 0;
        bool complete = false;
        int pixelCount = -12345;
        Color centre(0, 0, 0, 0);

        while (true)
        {
            BasicEffect fx(dev);
            fx.VertexColorEnabled = true;
            dev.Clear(kBlack);
            dev.setBlendStateProperty(BlendState::Opaque);
            DepthStencilState ds;
            ds.setDepthBufferEnableProperty(true);
            ds.setDepthBufferWriteEnableProperty(true);
            dev.setDepthStencilStateProperty(ds);
            fx.Apply();

            if (frame == 0)
            {
                query = std::make_unique<OcclusionQuery>(dev);
                query->Begin();
                DrawHalfQuad(dev, -1.0f, 0.0f, 0.9f, kRed);  // left half
                DrawHalfQuad(dev,  0.0f, 1.0f, 0.9f, kRed);  // right half
                query->End();
                ++frame;
                continue;
            }

            DrawHalfQuad(dev, -1.0f, 0.0f, 0.9f, kRed);
            DrawHalfQuad(dev,  0.0f, 1.0f, 0.9f, kRed);

            const Rectangle reg(kSize / 2, kSize / 2, 1, 1);
            dev.GetBackBufferData(&reg, &centre, 0, 1);

            complete   = query->getIsCompleteProperty();
            pixelCount = query->getPixelCountProperty();

            if (complete || frame >= kMaxPollFrames)
                break;
            ++frame;
        }

        return { centre, complete, pixelCount };
    }

protected:
    void Draw(const GameTime&) override
    {
        if (done_) return;
        done_ = true;

        auto& dev = getGraphicsDeviceProperty();

        int passCount = 0;
        const int totalChecks = 6;

        const ScenarioResult visible = RunScenario(dev, /*occluded=*/false);
        const bool visibleOk = colourMatch(visible.centre, kRed);
        std::printf("[%s] fully visible quad actually renders Red at centre: got=(%d,%d,%d)\n",
                    visibleOk ? "PASS" : "FAIL",
                    visible.centre.getRProperty(), visible.centre.getGProperty(), visible.centre.getBProperty());
        if (visibleOk) ++passCount;
        const bool visibleCountOk = visible.complete && visible.pixelCount > 0;
        std::printf("[%s] visible quad: IsComplete=%s PixelCount=%d (expected complete=true, count>0)\n",
                    visibleCountOk ? "PASS" : "FAIL",
                    visible.complete ? "true" : "false", visible.pixelCount);
        if (visibleCountOk) ++passCount;

        const ScenarioResult occluded = RunScenario(dev, /*occluded=*/true);
        const bool occludedOk = colourMatch(occluded.centre, kBlue);
        std::printf("[%s] occluder's own colour (Blue) still shows -- target quad genuinely hidden behind it: got=(%d,%d,%d)\n",
                    occludedOk ? "PASS" : "FAIL",
                    occluded.centre.getRProperty(), occluded.centre.getGProperty(), occluded.centre.getBProperty());
        if (occludedOk) ++passCount;
        const bool occludedCountOk = occluded.complete && occluded.pixelCount == 0;
        std::printf("[%s] occluded quad: IsComplete=%s PixelCount=%d (expected complete=true, count==0)\n",
                    occludedCountOk ? "PASS" : "FAIL",
                    occluded.complete ? "true" : "false", occluded.pixelCount);
        if (occludedCountOk) ++passCount;

        const ScenarioResult multi = RunMultiDrawScenario(dev);
        const bool multiOk = colourMatch(multi.centre, kRed);
        std::printf("[%s] multi-draw scenario: both half-quads visible at centre: got=(%d,%d,%d)\n",
                    multiOk ? "PASS" : "FAIL",
                    multi.centre.getRProperty(), multi.centre.getGProperty(), multi.centre.getBProperty());
        if (multiOk) ++passCount;
        // 2 non-overlapping 32x64 halves = 4096 total, same as one full 64x64 quad -- a
        // discriminating check: if only the LAST draw in the query's span were counted (a
        // multi-draw-span bug), this would read ~2048 instead.
        const bool multiCountOk = multi.complete && multi.pixelCount == kSize * kSize;
        std::printf("[%s] multi-draw scenario: IsComplete=%s PixelCount=%d (expected complete=true, count==%d, summed across both draws)\n",
                    multiCountOk ? "PASS" : "FAIL",
                    multi.complete ? "true" : "false", multi.pixelCount, kSize * kSize);
        if (multiCountOk) ++passCount;

        std::printf("=== %d/%d PASS ===\n", passCount, totalChecks);
        result_ = (passCount == totalChecks) ? 0 : 1;
        Exit();
    }

public:
    VulkanOcclusionQueryPixelCountTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(kSize);
        gdm_->setPreferredBackBufferHeightProperty(kSize);
    }

    int getResult() const { return result_; }
};

int main()
{
    VulkanOcclusionQueryPixelCountTest game;
    game.Run();
    return game.getResult();
}
