// SPDX-License-Identifier: MS-PL
// plans/plan_llgl.md LLGL-28: real per-draw-call OcclusionQuery for the LLGL graphics renderer, asserted
// against real pixels AND real pixel counts read back from the GPU.
//
// LLGL-specific counterpart of examples/vulkan_occlusionquery_pixelcount_test.cpp: same three
// scenarios, but the poll-across-multiple-frames loop that test needs is unnecessary here --
// LlglOcclusionQueryRenderer::IsComplete()/PixelCount() force a full submit-and-wait
// (FlushPendingFrameEXT) the first time either is asked, so the result is always available on the
// very next call rather than needing to be polled across several Present()s.
//
// Check A -- a fully visible quad renders red at its centre and reports a positive PixelCount().
// Check B -- a nearer opaque occluder drawn first (blue, no query attached) hides a farther quad
//   (red, wrapped in Begin()/End()) completely: the back buffer shows the occluder's own colour
//   and PixelCount() reads exactly 0 -- the real depth test rejected every one of its fragments.
// Check C -- two non-overlapping half-quads drawn between ONE Begin()/End() pair sum their
//   contributions into a single query result, rather than only the last draw counting.
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

#include <cmath>
#include <cstdio>
#include <memory>

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
    constexpr int kSize = 64;

    const Color kBlack(0, 0, 0, 255);
    const Color kRed(255, 0, 0, 255);
    const Color kBlue(0, 0, 255, 255);

    bool ColourMatch(Color got, Color want, int tol = 40)
    {
        return std::abs(got.getRProperty() - want.getRProperty()) <= tol &&
               std::abs(got.getGProperty() - want.getGProperty()) <= tol &&
               std::abs(got.getBProperty() - want.getBProperty()) <= tol;
    }

    void DrawQuad(GraphicsDevice& device, float z, const Color& color)
    {
        const Vector3 tl(-1.0f, 1.0f, z), bl(-1.0f, -1.0f, z);
        const Vector3 br(1.0f, -1.0f, z), tr(1.0f, 1.0f, z);
        const VertexPositionColor quad[6] = {
            {tl, color}, {bl, color}, {br, color}, {tl, color}, {br, color}, {tr, color},
        };
        device.setRasterizerStateProperty(RasterizerState::CullNone);
        device.DrawUserPrimitives(PrimitiveType::TriangleList, quad, 0, 2);
    }

    // Non-overlapping left/right half-quads, so a query wrapping BOTH draws must sum both
    // contributions to report the full-screen pixel count.
    void DrawHalfQuad(GraphicsDevice& device, float xMin, float xMax, float z, const Color& color)
    {
        const Vector3 tl(xMin, 1.0f, z), bl(xMin, -1.0f, z);
        const Vector3 br(xMax, -1.0f, z), tr(xMax, 1.0f, z);
        const VertexPositionColor quad[6] = {
            {tl, color}, {bl, color}, {br, color}, {tl, color}, {br, color}, {tr, color},
        };
        device.setRasterizerStateProperty(RasterizerState::CullNone);
        device.DrawUserPrimitives(PrimitiveType::TriangleList, quad, 0, 2);
    }

    void PrepareFrame(GraphicsDevice& device, BasicEffect& effect)
    {
        device.Clear(kBlack);
        device.setBlendStateProperty(BlendState::Opaque);
        DepthStencilState depthState;
        depthState.setDepthBufferEnableProperty(true);
        depthState.setDepthBufferWriteEnableProperty(true);
        device.setDepthStencilStateProperty(depthState);
        effect.Apply();
    }
}

class LlglOcclusionQueryTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    bool done_   = false;
    int  result_ = 1;

protected:
    void Draw(const GameTime&) override
    {
        if (done_) return;
        done_ = true;

        auto& device = getGraphicsDeviceProperty();
        int passCount = 0;
        const int totalChecks = 6;

        BasicEffect effect(device);
        effect.VertexColorEnabled = true;

        // The virtual-resolution letterboxing this renderer's presentation layer applies (see
        // docs/llgl-renderer.md) means the GPU actually rasterizes at the real window's PHYSICAL
        // pixel resolution, not at kSize x kSize -- a fully covering quad's own measured count
        // (this check) is the reference the multi-draw check below compares against, rather than
        // a hardcoded kSize * kSize (which would only hold if the window's own physical size
        // happened to equal the requested logical one).
        int fullQuadPixelCount = 0;

        // --- Check A: a fully visible quad ------------------------------------------------------
        {
            PrepareFrame(device, effect);
            OcclusionQuery query(device);
            query.Begin();
            DrawQuad(device, 0.9f, kRed);
            query.End();

            const Rectangle centreRegion(kSize / 2, kSize / 2, 1, 1);
            Color centre(0, 0, 0, 0);
            device.GetBackBufferData(&centreRegion, &centre, 0, 1);

            const bool colourOk = ColourMatch(centre, kRed);
            std::printf("[%s] fully visible quad renders Red at centre: got=(%d,%d,%d)\n",
                       colourOk ? "PASS" : "FAIL", centre.getRProperty(), centre.getGProperty(),
                       centre.getBProperty());
            if (colourOk) ++passCount;

            const bool complete = query.getIsCompleteProperty();
            fullQuadPixelCount = query.getPixelCountProperty();
            const bool countOk = complete && fullQuadPixelCount > 0;
            std::printf("[%s] visible quad: IsComplete=%s PixelCount=%d (expected complete=true, count>0)\n",
                       countOk ? "PASS" : "FAIL", complete ? "true" : "false", fullQuadPixelCount);
            if (countOk) ++passCount;
        }

        // --- Check B: a nearer opaque occluder hides the target quad completely ------------------
        {
            PrepareFrame(device, effect);
            DrawQuad(device, 0.1f, kBlue);   // nearer opaque occluder, no query attached

            OcclusionQuery query(device);
            query.Begin();
            DrawQuad(device, 0.9f, kRed);    // target quad -- farther away, fully occluded
            query.End();

            const Rectangle centreRegion(kSize / 2, kSize / 2, 1, 1);
            Color centre(0, 0, 0, 0);
            device.GetBackBufferData(&centreRegion, &centre, 0, 1);

            const bool colourOk = ColourMatch(centre, kBlue);
            std::printf("[%s] occluder's own colour still shows -- target quad genuinely hidden: got=(%d,%d,%d)\n",
                       colourOk ? "PASS" : "FAIL", centre.getRProperty(), centre.getGProperty(),
                       centre.getBProperty());
            if (colourOk) ++passCount;

            const bool complete = query.getIsCompleteProperty();
            const int pixelCount = query.getPixelCountProperty();
            const bool countOk = complete && pixelCount == 0;
            std::printf("[%s] occluded quad: IsComplete=%s PixelCount=%d (expected complete=true, count==0)\n",
                       countOk ? "PASS" : "FAIL", complete ? "true" : "false", pixelCount);
            if (countOk) ++passCount;
        }

        // --- Check C: multiple draws within one Begin()/End() sum their contributions ------------
        {
            PrepareFrame(device, effect);
            OcclusionQuery query(device);
            query.Begin();
            DrawHalfQuad(device, -1.0f, 0.0f, 0.9f, kRed);   // left half
            DrawHalfQuad(device, 0.0f, 1.0f, 0.9f, kRed);    // right half
            query.End();

            const Rectangle centreRegion(kSize / 2, kSize / 2, 1, 1);
            Color centre(0, 0, 0, 0);
            device.GetBackBufferData(&centreRegion, &centre, 0, 1);

            const bool colourOk = ColourMatch(centre, kRed);
            std::printf("[%s] multi-draw scenario: both half-quads visible at centre: got=(%d,%d,%d)\n",
                       colourOk ? "PASS" : "FAIL", centre.getRProperty(), centre.getGProperty(),
                       centre.getBProperty());
            if (colourOk) ++passCount;

            const bool complete = query.getIsCompleteProperty();
            const int pixelCount = query.getPixelCountProperty();
            // Two non-overlapping halves together cover the same area as one full quad -- a
            // discriminating check: if only the LAST draw in the query's span were counted, this
            // would read roughly half of fullQuadPixelCount instead.
            const bool countOk = complete && pixelCount == fullQuadPixelCount;
            std::printf("[%s] multi-draw scenario: IsComplete=%s PixelCount=%d (expected complete=true, count==%d, matching the full quad's own count)\n",
                       countOk ? "PASS" : "FAIL", complete ? "true" : "false", pixelCount, fullQuadPixelCount);
            if (countOk) ++passCount;
        }

        std::printf("=== %d/%d PASS ===\n", passCount, totalChecks);
        result_ = (passCount == totalChecks) ? 0 : 1;
        Exit();
    }

public:
    LlglOcclusionQueryTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(kSize);
        gdm_->setPreferredBackBufferHeightProperty(kSize);
        gdm_->setSynchronizeWithVerticalRetraceProperty(false);
    }

    [[nodiscard]] int GetResult() const { return result_; }
};

int main()
{
    LlglOcclusionQueryTest game;
    game.Run();
    return game.GetResult();
}
