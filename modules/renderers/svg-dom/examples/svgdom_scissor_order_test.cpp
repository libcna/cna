// SPDX-License-Identifier: MS-PL
//
// SVGDOM-A regression test: cross-region scissor/clip paint order must match actual SpriteBatch
// flush order, not each clip-region's own first-DOM-creation order. This can only be proven on the
// real SVG backbuffer (no API rasterizes a live SVG subtree to pixels synchronously -- see
// docs/svg-dom-renderer.md), so this leaves a live, deliberately order-sensitive scene in the DOM and
// a companion Playwright harness (scripts/svgdom-browser-test.mjs) screenshots the actual page and
// samples specific pixels.
//
// Scene (32x32 backbuffer, Clear = gray (50,50,50)):
//   1. Batch A: scissor rect1=(0,0,10,10),   draw RED   (clipped to rect1)
//   2. Batch B: scissor rect2=(22,22,10,10), draw BLUE  (clipped to rect2)
//   3. Batch C: scissor DISABLED,            draw GREEN (covers the whole 32x32 surface)
//   4. Batch D: scissor rect3=(0,22,10,10),  draw YELLOW (clipped to rect3)
//
// Correct (draw-order) result: rect1 and rect2 must show GREEN (batch C, drawn AFTER A/B, must
// paint over both), rect3 must show YELLOW (batch D, drawn after C, on its own later region), and
// any other point must show GREEN (only C ever touches it).
//
// Pre-fix (region-first-creation-order) result: the 'full' region existed before any draw ever ran
// (created eagerly at EnsureRoot), so it was PERMANENTLY document-order-FIRST regardless of when it
// was actually drawn to -- rect1's and rect2's own regions, created later by A/B, painted ON TOP of
// it. rect1 would show RED (batch A, wrongly left on top of batch C's green) and rect2 would show
// BLUE (batch B, wrongly left on top) instead of both showing green.
//
// Exit code 0 = every structural check passed, 1 = at least one failed; the real per-pixel
// draw-order proof is in the Playwright harness, not here (see the comment above). Also published on
// window.__cnaScissorOrderResult for the browser driver to read.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SamplerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteSortMode.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"

#include <cstdio>
#include <memory>
#include <vector>

#if defined(__EMSCRIPTEN__)
#include <emscripten.h>
#endif

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;

namespace
{
#if defined(__EMSCRIPTEN__)
    // Structural control: at least 3 distinct flush slots (clip states) must have been created --
    // rect1, rect2/full (may coalesce differently depending on adjacency), rect3 -- proving the
    // scene actually exercised multiple clip regions, not just one degenerate path.
    EM_JS(int, JsFlushSlotCount, (), {
        const slots = Module['cnaSvgDomFlushSlots'];
        return slots ? slots.length : -1;
    });

    EM_JS(void, JsPublishResult, (int result, int passed, int expected), {
        window.__cnaScissorOrderResult = result;
        window.__cnaScissorOrderPassed = passed;
        window.__cnaScissorOrderExpected = expected;
        window.__cnaScissorOrderDone = true;
    });
#else
    int JsFlushSlotCount() { return -1; }
    void JsPublishResult(int, int, int) {}
#endif
}

class SvgDomScissorOrderTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    Texture2D whiteTex_;
    int passCount_ = 0;
    int totalCount_ = 0;
    int result_ = 1;
    bool done_ = false;

    void check(bool ok, const std::string& label)
    {
        std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", label.c_str());
        std::fflush(stdout);
        ++totalCount_;
        if (ok) ++passCount_;
    }

    void DrawFullCover(GraphicsDevice& dev, const Color& color, bool scissorEnable, const Rectangle& rect)
    {
        RasterizerState rs;
        rs.setScissorTestEnableProperty(scissorEnable);
        if (scissorEnable) dev.setScissorRectangleProperty(rect);
        SpriteBatch sb(dev);
        SamplerState point = SamplerState::PointClamp;
        sb.Begin(SpriteSortMode::Deferred, BlendState::Opaque, &point, nullptr, &rs);
        sb.Draw(whiteTex_, Rectangle(0, 0, 32, 32), Rectangle(0, 0, 1, 1), color);
        sb.End();
    }

protected:
    void LoadContent() override
    {
        auto& dev = getGraphicsDeviceProperty();
        whiteTex_ = Texture2D::CreateFromPixels(dev, 1, 1, std::vector<std::uint8_t>{255, 255, 255, 255});
    }

    void Draw(const GameTime&) override
    {
        if (done_) return;
        done_ = true;
        auto& dev = getGraphicsDeviceProperty();

        dev.Clear(Color(50, 50, 50, 255));
        DrawFullCover(dev, Color(255, 0, 0, 255), true, Rectangle(0, 0, 10, 10));   // A: rect1, red
        DrawFullCover(dev, Color(0, 0, 255, 255), true, Rectangle(22, 22, 10, 10)); // B: rect2, blue
        DrawFullCover(dev, Color(0, 255, 0, 255), false, Rectangle());             // C: full, green
        DrawFullCover(dev, Color(255, 255, 0, 255), true, Rectangle(0, 22, 10, 10)); // D: rect3, yellow

        check(JsFlushSlotCount() >= 3, "at least 3 distinct flush slots were created for 4 differently-clipped batches");

        std::printf("=== %d/%d PASS (structural only -- see the Playwright harness for the pixel proof) ===\n",
                    passCount_, totalCount_);
        std::fflush(stdout);
        result_ = (passCount_ == totalCount_) ? 0 : 1;
        JsPublishResult(result_, passCount_, totalCount_);
        Exit();
    }

public:
    SvgDomScissorOrderTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(32);
        gdm_->setPreferredBackBufferHeightProperty(32);
    }

    int getResult() const { return result_; }
};

int main()
{
    SvgDomScissorOrderTest game;
    game.Run();
    return game.getResult();
}
