// SPDX-License-Identifier: MS-PL
//
// Deep correctness remediation pixel-verification suite for the SVG_DOM renderer. Every check here
// renders into a RenderTarget2D and reads it back with a real GetData() -- deterministic, byte-exact,
// and (unlike the SVG backbuffer) actually readable, so these checks need no screenshot at all. The
// SVG backbuffer's own draw-ORDER regression (SVGDOM-A) is covered separately by
// svgdom_scissor_order_test.cpp, which does need a real browser screenshot (no API rasterizes a live
// SVG subtree to pixels synchronously -- see docs/svg-dom-renderer.md).
//
// Exit code 0 = every check passed, 1 = at least one failed; also published on
// window.__cnaPixelResult for a browser driver to read (see scripts/svgdom-browser-test.mjs).

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/SamplerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteSortMode.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/Viewport.hpp"

#include "CNA/Internal/Renderers/SvgDom/SvgDomRenderer.hpp"
#include "CNA/Internal/Renderers/SvgDom/SvgDomRenderTargetRenderer.hpp"

#include <cstdio>
#include <memory>
#include <vector>

#if defined(__EMSCRIPTEN__)
#include <emscripten.h>
#endif

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;
using namespace CNA::Internal::Renderers::SvgDom;

namespace
{
#if defined(__EMSCRIPTEN__)
    // SVGDOM-E: true once Module['cnaSvgDomTextures'][id] carries a real `variants` map -- i.e. this
    // render target was successfully sampled as an ordinary Draw() source texture (the exact
    // operation CNA_SvgDom_RegisterTextureVariant used to throw on, since a render target's own
    // registry entry pre-exists in the {canvas,ctx,isRenderTarget} shape with no `variants` field).
    EM_JS(int, JsTextureHasRegisteredVariant, (int id), {
        const entry = Module['cnaSvgDomTextures'] && Module['cnaSvgDomTextures'][id];
        return (entry && entry.variants && entry.variants[0]) ? 1 : 0;
    });

    EM_JS(void, JsPublishResult, (int result, int passed, int expected), {
        window.__cnaPixelResult = result;
        window.__cnaPixelPassed = passed;
        window.__cnaPixelExpected = expected;
        window.__cnaPixelDone = true;
    });
#else
    int JsTextureHasRegisteredVariant(int) { return 0; }
    void JsPublishResult(int, int, int) {}
#endif

    bool CloseC(std::uint8_t a, int b, int tol) { return std::abs(static_cast<int>(a) - b) <= tol; }
    bool CloseColor(const Color& c, int r, int g, int b, int a, int tol = 6)
    {
        return CloseC(c.getRProperty(), r, tol) && CloseC(c.getGProperty(), g, tol) &&
               CloseC(c.getBProperty(), b, tol) && CloseC(c.getAProperty(), a, tol);
    }
}

class SvgDomPixelVerificationTest : public Game
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

    void DrawFullCover(GraphicsDevice& dev, RenderTarget2D& rt, const Color& color,
                       bool scissorEnable, const Rectangle& scissorRect)
    {
        RasterizerState rs;
        rs.setScissorTestEnableProperty(scissorEnable);
        if (scissorEnable) dev.setScissorRectangleProperty(scissorRect);
        SpriteBatch sb(dev);
        SamplerState point = SamplerState::PointClamp;
        sb.Begin(SpriteSortMode::Deferred, BlendState::Opaque, &point, nullptr, &rs);
        sb.Draw(whiteTex_, Rectangle(0, 0, rt.getWidthProperty(), rt.getHeightProperty()),
                Rectangle(0, 0, 1, 1), color);
        sb.End();
    }

    Color Sample(RenderTarget2D& rt, int x, int y)
    {
        std::vector<Color> px(static_cast<std::size_t>(rt.getWidthProperty()) * rt.getHeightProperty(),
                              Color(0xCD, 0xCD, 0xCD, 0xCD));
        rt.GetData(px.data(), 0, static_cast<int>(px.size()));
        return px[static_cast<std::size_t>(y) * rt.getWidthProperty() + x];
    }

    // ---------------------------------------------------------------------------------------
    // SVGDOM-B: scissor must apply on the RenderTarget2D-bound Canvas2D draw path, with the same
    // per-batch timing the SVG backbuffer path already had, and must not leak between batches.
    // ---------------------------------------------------------------------------------------
    void CheckScissorOnRenderTarget(GraphicsDevice& dev)
    {
        auto rt = std::make_unique<RenderTarget2D>(dev, 8, 8);
        dev.SetRenderTarget(rt.get());
        dev.Clear(Color(0, 0, 0, 255));
        DrawFullCover(dev, *rt, Color(255, 0, 0, 255), true, Rectangle(2, 2, 4, 4));
        dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));

        check(CloseColor(Sample(*rt, 3, 3), 255, 0, 0, 255), "RT scissor: inside the rect is drawn");
        check(CloseColor(Sample(*rt, 0, 0), 0, 0, 0, 255), "RT scissor: (0,0) outside the rect stays Clear");
        check(CloseColor(Sample(*rt, 7, 7), 0, 0, 0, 255), "RT scissor: (7,7) outside the rect stays Clear");
        check(CloseColor(Sample(*rt, 2, 2), 255, 0, 0, 255), "RT scissor: top-left corner of the rect is drawn");
        check(CloseColor(Sample(*rt, 5, 5), 255, 0, 0, 255), "RT scissor: bottom-right-1 corner of the rect is drawn");
        check(CloseColor(Sample(*rt, 6, 6), 0, 0, 0, 255), "RT scissor: just past the rect's far edge stays Clear");

        // Multiple batches, different scissor states, on the SAME target: each batch's own scissor
        // rect must apply only to ITS OWN sprites, not leak onto the next/previous batch.
        auto rt2 = std::make_unique<RenderTarget2D>(dev, 8, 8);
        dev.SetRenderTarget(rt2.get());
        dev.Clear(Color(0, 0, 0, 255));
        DrawFullCover(dev, *rt2, Color(0, 255, 0, 255), true, Rectangle(0, 0, 4, 8));  // left half green
        DrawFullCover(dev, *rt2, Color(0, 0, 255, 255), true, Rectangle(4, 0, 4, 8));  // right half blue
        dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));

        check(CloseColor(Sample(*rt2, 1, 4), 0, 255, 0, 255), "RT scissor: batch 1's own rect (left) is green");
        check(CloseColor(Sample(*rt2, 6, 4), 0, 0, 255, 255), "RT scissor: batch 2's own rect (right) is blue, not leaked-into by batch 1");
        check(CloseColor(Sample(*rt2, 3, 4), 0, 255, 0, 255), "RT scissor: left edge column stays green (batch 2 did not overwrite it)");
    }

    // ---------------------------------------------------------------------------------------
    // SVGDOM-D: UpdatePixels (SetData) and the render target's own canvas must stay coherent
    // regardless of call order -- a canvas-composited draw made after SetData must start from the
    // just-uploaded pixels, not from whatever the canvas held before.
    // ---------------------------------------------------------------------------------------
    void CheckUpdatePixelsCanvasCoherence(GraphicsDevice& dev)
    {
        // RenderTargetUsage::PreserveContents (not the 2-arg ctor's DiscardContents default) --
        // DiscardContents auto-clears to black on every SetRenderTarget() bind (real, documented
        // XNA/FNA behavior -- see GraphicsDevice::SetRenderTarget's own DiscardContents handling),
        // which would make the re-bind below always fail for a reason unrelated to what this check
        // actually tests (canvas/CPU-buffer coherence, not content-preservation policy).
        auto rt = std::make_unique<RenderTarget2D>(dev, 4, 4, false, SurfaceFormat::Color,
                                                    DepthFormat::None, 0,
                                                    RenderTargetUsage::PreserveContents);
        dev.SetRenderTarget(rt.get());
        dev.Clear(Color(0, 255, 0, 255)); // green baseline
        dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));

        // SetData (Texture2D::SetData -> UpdatePixels) while NOT bound: must become the new
        // authoritative content -- both the CPU buffer AND the canvas (SVGDOM-D).
        std::vector<Color> red(16, Color(255, 0, 0, 255));
        rt->SetData(red.data(), static_cast<int>(red.size()));
        check(CloseColor(Sample(*rt, 0, 0), 255, 0, 0, 255),
              "SVGDOM-D: SetData while unbound is immediately authoritative (CPU readback)");

        // Now bind and draw only the LEFT HALF blue. If the canvas were not synced from SetData
        // (the pre-fix bug), this draw would composite onto the STALE green canvas instead of red,
        // and the right (undrawn) half would read back green instead of red.
        dev.SetRenderTarget(rt.get());
        DrawFullCover(dev, *rt, Color(0, 0, 255, 255), true, Rectangle(0, 0, 2, 4));
        dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));

        check(CloseColor(Sample(*rt, 0, 2), 0, 0, 255, 255), "SVGDOM-D: the freshly drawn half is blue");
        check(CloseColor(Sample(*rt, 3, 2), 255, 0, 0, 255),
              "SVGDOM-D: the undrawn half is red (from SetData), not stale green from before it -- "
              "proves the canvas was synced from SetData, not left behind");
    }

    // ---------------------------------------------------------------------------------------
    // SVGDOM-E: a RenderTarget2D sampled back as an ordinary Draw() source texture must reflect its
    // real, current content -- including after being modified again post-sampling -- without
    // crashing (the old static_cast was undefined behaviour; the old JS registry shape mismatch
    // threw the instant this was attempted at all).
    // ---------------------------------------------------------------------------------------
    void CheckRenderTargetAsTexture(GraphicsDevice& dev)
    {
        auto rtA = std::make_unique<RenderTarget2D>(dev, 4, 4);
        dev.SetRenderTarget(rtA.get());
        dev.Clear(Color(255, 0, 255, 255)); // magenta
        dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));

        auto rtB = std::make_unique<RenderTarget2D>(dev, 4, 4);
        dev.SetRenderTarget(rtB.get());
        dev.Clear(Color(0, 0, 0, 255));
        {
            SpriteBatch sb(dev);
            SamplerState point = SamplerState::PointClamp;
            sb.Begin(SpriteSortMode::Deferred, BlendState::Opaque, &point, nullptr, nullptr);
            sb.Draw(*rtA, Rectangle(0, 0, 4, 4), Rectangle(0, 0, 4, 4), Color::White);
            sb.End();
        }
        dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));

        check(CloseColor(Sample(*rtB, 2, 2), 255, 0, 255, 255),
              "SVGDOM-E: RenderTarget2D drawn as a source texture samples its real content, no crash");

        // Modify rtA AGAIN after it has already been sampled, then sample it a second time: the
        // second sample must see the NEW content, not the first sample's stale snapshot.
        dev.SetRenderTarget(rtA.get());
        dev.Clear(Color(255, 255, 0, 255)); // yellow
        dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));

        auto rtC = std::make_unique<RenderTarget2D>(dev, 4, 4);
        dev.SetRenderTarget(rtC.get());
        dev.Clear(Color(0, 0, 0, 255));
        {
            SpriteBatch sb(dev);
            SamplerState point = SamplerState::PointClamp;
            sb.Begin(SpriteSortMode::Deferred, BlendState::Opaque, &point, nullptr, nullptr);
            sb.Draw(*rtA, Rectangle(0, 0, 4, 4), Rectangle(0, 0, 4, 4), Color::White);
            sb.End();
        }
        dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));

        check(CloseColor(Sample(*rtC, 2, 2), 255, 255, 0, 255),
              "SVGDOM-E: the same render target modified again after being sampled once shows the "
              "NEW content on a fresh sample, not the earlier stale snapshot");

        // Also sample rtA onto the real SVG backbuffer path (unbound target): exercises
        // DataUriOfEXT/CNA_SvgDom_RegisterTextureVariant's additive-entry fix -- the JS crash this
        // repairs can ONLY be observed on that path (the render-target-bound Canvas2D path above
        // uses PixelsOfEXT instead, a separate code path this does not exercise).
        {
            SpriteBatch sb(dev);
            sb.Begin();
            sb.Draw(*rtA, Vector2(0, 0), Color::White);
            sb.End();
        }
        auto* rtRenderer = static_cast<SvgDomRenderTargetRenderer*>(rtA->GetRenderTargetRenderer());
        check(JsTextureHasRegisteredVariant(rtRenderer->GetCanvasIdEXT()) == 1,
              "SVGDOM-E: drawing a RenderTarget2D onto the SVG backbuffer registers a real texture "
              "variant instead of throwing (the pre-fix registry-shape crash)");
    }

    // ---------------------------------------------------------------------------------------
    // SVGDOM-F: GraphicsDevice.Viewport.Width/Height must clip rendering to the viewport rectangle,
    // unconditionally (independent of RasterizerState.ScissorTestEnable), on the render-target-bound
    // path too -- while the (X,Y) translation keeps working exactly as before.
    // ---------------------------------------------------------------------------------------
    void CheckViewportClipOnRenderTarget(GraphicsDevice& dev)
    {
        auto rt = std::make_unique<RenderTarget2D>(dev, 8, 8);
        dev.SetRenderTarget(rt.get()); // resets Viewport/Scissor to the RT's own full size first
        dev.Clear(Color(0, 0, 0, 255));
        dev.setViewportProperty(Viewport(2, 2, 4, 4)); // sub-rectangle viewport, no scissor at all
        {
            SpriteBatch sb(dev);
            SamplerState point = SamplerState::PointClamp;
            sb.Begin(SpriteSortMode::Deferred, BlendState::Opaque, &point, nullptr, nullptr);
            // Viewport-local (0,0,8,8): a full-viewport-covering sprite that also geometrically
            // spills far outside the viewport rectangle -- must be clipped to it regardless.
            sb.Draw(whiteTex_, Rectangle(0, 0, 8, 8), Rectangle(0, 0, 1, 1), Color(255, 0, 0, 255));
            sb.End();
        }
        dev.setViewportProperty(Viewport(0, 0, 8, 8));
        dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));

        check(CloseColor(Sample(*rt, 3, 3), 255, 0, 0, 255), "Viewport clip: inside the viewport rect is drawn");
        check(CloseColor(Sample(*rt, 2, 2), 255, 0, 0, 255),
              "Viewport clip: (2,2) -- the viewport's own X,Y origin -- is the sprite's local (0,0), drawn");
        check(CloseColor(Sample(*rt, 1, 1), 0, 0, 0, 255),
              "Viewport clip: just outside the viewport rect's near edge stays Clear (unconditional clip, no scissor set)");
        check(CloseColor(Sample(*rt, 7, 7), 0, 0, 0, 255),
              "Viewport clip: far outside the viewport rect stays Clear even though the sprite geometrically covers it");
        check(CloseColor(Sample(*rt, 6, 6), 0, 0, 0, 255),
              "Viewport clip: just past the viewport rect's far edge (6,6) stays Clear");
    }

    // ---------------------------------------------------------------------------------------
    // SVGDOM-5: AlphaBlend draw colours are premultiplied by XNA. The SVG and Canvas2D paths both
    // consume straight RGB plus independent alpha, so the tint must be un-premultiplied before
    // either path uses it. Before the fix this white 50% fade produced ~64 grey, not ~128 grey.
    // ---------------------------------------------------------------------------------------
    void CheckAlphaBlendTintOnRenderTarget(GraphicsDevice& dev)
    {
        auto rt = std::make_unique<RenderTarget2D>(dev, 1, 1);
        dev.SetRenderTarget(rt.get());
        dev.Clear(Color(0, 0, 0, 255));
        {
            SpriteBatch sb(dev);
            SamplerState point = SamplerState::PointClamp;
            sb.Begin(SpriteSortMode::Deferred, BlendState::AlphaBlend, &point, nullptr, nullptr);
            sb.Draw(whiteTex_, Rectangle(0, 0, 1, 1), Rectangle(0, 0, 1, 1),
                    Color::FromNonPremultiplied(255, 255, 255, 128));
            sb.End();
        }
        dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));

        check(CloseColor(Sample(*rt, 0, 0), 128, 128, 128, 255),
              "SVGDOM-5: AlphaBlend alpha-only tint changes opacity without double-darkening RGB");
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

        CheckScissorOnRenderTarget(dev);
        CheckUpdatePixelsCanvasCoherence(dev);
        CheckRenderTargetAsTexture(dev);
        CheckViewportClipOnRenderTarget(dev);
        CheckAlphaBlendTintOnRenderTarget(dev);

        std::printf("=== %d/%d PASS ===\n", passCount_, totalCount_);
        std::fflush(stdout);
        result_ = (passCount_ == totalCount_) ? 0 : 1;
        JsPublishResult(result_, passCount_, totalCount_);
        Exit();
    }

public:
    SvgDomPixelVerificationTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(32);
        gdm_->setPreferredBackBufferHeightProperty(32);
    }

    int getResult() const { return result_; }
};

int main()
{
    // Heap-allocated, not a local: emscripten_set_main_loop(..., simulateInfiniteLoop=1) unwinds
    // this stack frame via a JS-level throw (see docs/emscripten-mainloop-game-lifetime.md) --
    // a stack-local Game here would have its storage reclaimed while the loop callback still
    // holds a raw pointer to it.
    SvgDomPixelVerificationTest* game = new SvgDomPixelVerificationTest();
    game->Run();
    return game->getResult();
}
