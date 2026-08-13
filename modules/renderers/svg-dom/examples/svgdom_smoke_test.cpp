// SPDX-License-Identifier: MS-PL
//
// plan_svg_dom.md: end-to-end vertical-slice smoke test for the SVG_DOM graphics renderer, written
// to produce a real PASS/FAIL in a real browser (mirrors HTML_DOM's own
// htmldom_smoke_test.cpp/HTMLDOM-15 precedent). Inspects the actual SVG DOM the renderer produced
// -- the root element and its namespace, the per-sprite nested-<svg>+<image> structure, the tint
// feColorMatrix filter, Additive's mix-blend-mode, and a RenderTarget2D round trip via real
// getImageData readback.
//
// NOTE: this file has NOT been executed in the environment this renderer was authored in -- no
// Emscripten SDK is available there (see docs/svg-dom-renderer.md's own Platform Validation
// section). It is a genuine, from-source vertical-slice test intended to run via
// `emcmake cmake ... -DCNA_GRAPHICS_RENDERER=SVG_DOM` + a headless-Chromium harness, the same way
// htmldom_smoke_test.cpp is validated, but that run is an external gate for this task, not
// something claimed to have happened here.
//
// Exit code 0 = every check passed, 1 = at least one failed; also published on
// window.__cnaSmokeResult for a browser driver to read.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"

#include "CNA/Internal/Renderers/SvgDom/SvgDomRenderer.hpp"

#include <SDL3/SDL.h>

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
    constexpr int kExpectedChecks = 15;

#if defined(__EMSCRIPTEN__)
    EM_JS(int, JsSurfaceExists, (), {
        const el = document.getElementById('cna-svg-dom-root');
        return (el && el.tagName.toLowerCase() === 'svg') ? 1 : 0;
    });

    EM_JS(int, JsInputSurfacePreserved, (), {
        const canvas = Module['canvas'] || document.querySelector('canvas');
        const root = Module['cnaSvgDomRoot'];
        return canvas && root && canvas.style.opacity === '0' &&
               canvas.style.visibility !== 'hidden' && root.style.pointerEvents === 'none' &&
               root.getAttribute('pointer-events') === 'none' ? 1 : 0;
    });

    // SVGDOM-A: sprites now live inside per-FLUSH ordered slots (Module['cnaSvgDomFlushSlots']),
    // not one fixed global sprite group -- consecutive flushes sharing the same (here: absent)
    // clip state coalesce into the SAME slot (slot 0), exactly the scenario every Begin/Draw/End
    // pair below produces (no scissor/viewport set anywhere in this test), so slot 0's own
    // container is where every sprite below actually lands, same as the old fixed group was.
    EM_JS(int, JsFlushSlot0ChildCount, (), {
        const slots = Module['cnaSvgDomFlushSlots'];
        const slot0 = slots && slots[0];
        return slot0 ? slot0.container.children.length : -1;
    });

    /// 1 when sprite `i` (in flush slot 0) is a nested <svg> containing an <image> with a
    /// data:image/png href.
    EM_JS(int, JsSpriteHasImageWithPngHref, (int i), {
        const slots = Module['cnaSvgDomFlushSlots'];
        const slot0 = slots && slots[0];
        if (!slot0 || !slot0.container.children[i]) return 0;
        const image = slot0.container.children[i].querySelector('image');
        if (!image) return 0;
        const href = image.getAttributeNS('http://www.w3.org/1999/xlink', 'href') ||
                    image.getAttribute('href') || "";
        return href.indexOf('data:image/png') === 0 ? 1 : 0;
    });

    /// 1 when sprite `i` (in flush slot 0) carries a filter="url(...)" attribute on its own
    /// wrapping <g> (a non-white tint).
    EM_JS(int, JsSpriteHasFilter, (int i), {
        const slots = Module['cnaSvgDomFlushSlots'];
        const slot0 = slots && slots[0];
        if (!slot0 || !slot0.container.children[i]) return 0;
        return slot0.container.children[i].getAttribute('filter') ? 1 : 0;
    });

    EM_JS(int, JsSpriteHasOpacity, (int i, double expected), {
        const slots = Module['cnaSvgDomFlushSlots'];
        const slot0 = slots && slots[0];
        if (!slot0 || !slot0.container.children[i]) return 0;
        const opacity = Number(slot0.container.children[i].style.opacity || 1);
        return Math.abs(opacity - expected) < 0.000001 ? 1 : 0;
    });

    /// 1 when sprite `i` (in flush slot 0)'s own wrapping <g> has mix-blend-mode: plus-lighter.
    EM_JS(int, JsSpriteIsAdditive, (int i), {
        const slots = Module['cnaSvgDomFlushSlots'];
        const slot0 = slots && slots[0];
        if (!slot0 || !slot0.container.children[i]) return 0;
        return slot0.container.children[i].style.mixBlendMode === 'plus-lighter' ? 1 : 0;
    });

    EM_JS(int, JsSupportsPlusLighter, (), {
        return (typeof CSS !== 'undefined' && CSS.supports &&
                CSS.supports('mix-blend-mode', 'plus-lighter')) ? 1 : 0;
    });

    EM_JS(void, JsPublishResult, (int result, int passed, int expected), {
        window.__cnaSmokeResult = result;
        window.__cnaSmokePassed = passed;
        window.__cnaSmokeExpected = expected;
        window.__cnaSmokeDone = true;
    });
#else
    int JsSurfaceExists() { return 0; }
    int JsInputSurfacePreserved() { return 0; }
    int JsFlushSlot0ChildCount() { return -1; }
    int JsSpriteHasImageWithPngHref(int) { return 0; }
    int JsSpriteHasFilter(int) { return 0; }
    int JsSpriteHasOpacity(int, double) { return 0; }
    int JsSpriteIsAdditive(int) { return 0; }
    int JsSupportsPlusLighter() { return 0; }
    void JsPublishResult(int, int, int) {}
#endif
}

class SvgDomSmokeTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    std::unique_ptr<SpriteBatch> spriteBatch_;
    std::unique_ptr<Texture2D> texture_;
    std::unique_ptr<RenderTarget2D> renderTarget_;
    int frame_ = 0;
    int passCount_ = 0;
    int result_ = 1;

    void check(bool ok, const char* label)
    {
        std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", label);
        std::fflush(stdout);
        if (ok) ++passCount_;
    }

protected:
    void LoadContent() override
    {
        spriteBatch_ = std::make_unique<SpriteBatch>(getGraphicsDeviceProperty());
        texture_ = std::make_unique<Texture2D>(
            Texture2D::CreateFromPixels(getGraphicsDeviceProperty(), 2, 2, std::vector<std::uint8_t>{
                255, 0, 0, 255,   0, 255, 0, 255,
                0, 0, 255, 255,   255, 255, 0, 255,
            }));
        renderTarget_ = std::make_unique<RenderTarget2D>(getGraphicsDeviceProperty(), 4, 4);
    }

    void Draw(const GameTime&) override
    {
        ++frame_;
        auto& dev = getGraphicsDeviceProperty();
        auto& renderer = static_cast<SvgDomRenderer&>(dev.GetRenderer());

        if (frame_ == 1)
        {
            check(renderer.GetWindowInternal() != nullptr,
                  "GraphicsDevice has a real SDL_Window under the SVG_DOM renderer");
            check(renderer.GetRendererInternal() == nullptr,
                  "GetRendererInternal() is null -- no SDL_Renderer exists on this renderer");
            check(JsSurfaceExists() == 1, "a real <svg id=\"cna-svg-dom-root\"> surface was created");
            check(JsInputSurfacePreserved() == 1,
                  "the transparent SDL canvas remains the input target while SVG ignores pointer events");
            check(JsSupportsPlusLighter() == 1,
                  "this test browser genuinely supports mix-blend-mode: plus-lighter -- the "
                  "Additive check below is exercising the real CSS blend");
            check(dev.SupportsCapability(CNA::GraphicsCapability::AdditiveBlending) ==
                  (JsSupportsPlusLighter() == 1),
                  "GraphicsDevice::SupportsCapability(AdditiveBlending) agrees with the raw "
                  "CSS.supports() query it wraps");
        }

        dev.Clear(Color::CornflowerBlue);

        if (frame_ == 1)
        {
            spriteBatch_->Begin();
            spriteBatch_->Draw(*texture_, Vector2(0, 0), Color::White);
            spriteBatch_->End();
            check(JsFlushSlot0ChildCount() == 1, "one sprite element was appended for one Draw()");
            check(JsSpriteHasImageWithPngHref(0) == 1,
                  "the sprite is a nested <svg> containing an <image> with a data:image/png href");
            check(JsSpriteHasFilter(0) == 0,
                  "a Color::White (identity) tint carries no feColorMatrix filter -- zero overhead "
                  "for the overwhelmingly common untinted case");

            // This Begin/End pair shares slot 0's own unscissored clip state with the draw above
            // (SVGDOM-A coalescing), so it becomes slot 0's SECOND child (index 1), not a fresh
            // index 0 -- checking index 0 here would inspect the first (untinted) sprite instead
            // of this one.
            spriteBatch_->Begin();
            spriteBatch_->Draw(*texture_, Vector2(0, 0), Color(128, 64, 32, 255));
            spriteBatch_->End();
            check(JsSpriteHasFilter(1) == 1, "a non-white tint is applied via a real SVG feColorMatrix filter");

            // Same coalescing as above: this is slot 0's THIRD child (index 2).
            spriteBatch_->Begin(SpriteSortMode::Deferred, BlendState::Additive);
            spriteBatch_->Draw(*texture_, Vector2(0, 0), Color::White);
            spriteBatch_->End();
            check(JsSpriteIsAdditive(2) == 1,
                  "BlendState::Additive sets mix-blend-mode: plus-lighter on the sprite element");

            // SVGDOM-5: a pure AlphaBlend fade is the high-frequency Mobile Eggbert loading path.
            // It must not allocate/use an SVG filter; only the pooled sprite's opacity changes.
            spriteBatch_->Begin(SpriteSortMode::Deferred, BlendState::AlphaBlend);
            spriteBatch_->Draw(*texture_, Vector2(0, 0),
                               Color::FromNonPremultiplied(255, 255, 255, 128));
            spriteBatch_->End();
            check(JsSpriteHasFilter(3) == 0,
                  "an alpha-only fade carries no feColorMatrix filter");
            check(JsSpriteHasOpacity(3, 128.0 / 255.0) == 1,
                  "an alpha-only fade is represented by the pooled sprite's opacity");

            // Render target: draw a solid colour into it and read it back for real.
            dev.SetRenderTarget(renderTarget_.get());
            dev.Clear(Color(10, 20, 30, 255));
            dev.SetRenderTarget(nullptr);
            std::vector<Color> pixels(4 * 4, Color(0xCD, 0xCD, 0xCD, 0xCD));
            renderTarget_->GetData(pixels.data(), 0, static_cast<int>(pixels.size()));
            check(pixels[0] == Color(10, 20, 30, 255),
                  "RenderTarget2D::Clear + GetData round-trips through the real private canvas");

            bool threw = false;
            std::vector<Color> backbuffer(4 * 4, Color(0xCD, 0xCD, 0xCD, 0xCD));
            try { dev.GetBackBufferData(backbuffer.data(), 0, static_cast<int>(backbuffer.size())); }
            catch (const std::exception&) { threw = true; }
            check(threw,
                  "reading back the SVG backbuffer throws -- no browser API rasterizes a live SVG "
                  "subtree synchronously; render into a RenderTarget2D instead");

            std::printf("=== %d/%d PASS ===\n", passCount_, kExpectedChecks);
            std::fflush(stdout);
            result_ = (passCount_ == kExpectedChecks) ? 0 : 1;
            JsPublishResult(result_, passCount_, kExpectedChecks);
            Exit();
        }
    }

public:
    SvgDomSmokeTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(64);
        gdm_->setPreferredBackBufferHeightProperty(64);
    }

    int getResult() const { return result_; }
};

int main()
{
    // Heap-allocated, not a local: emscripten_set_main_loop(..., simulateInfiniteLoop=1) unwinds
    // this stack frame via a JS-level throw (see docs/emscripten-mainloop-game-lifetime.md) --
    // a stack-local Game here would have its storage reclaimed while the loop callback still
    // holds a raw pointer to it.
    SvgDomSmokeTest* game = new SvgDomSmokeTest();
    game->Run();
    return game->getResult();
}
