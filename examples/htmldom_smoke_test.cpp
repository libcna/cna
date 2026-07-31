// SPDX-License-Identifier: MS-PL
//
// plan_html_dom.md HTMLDOM-15/HTMLDOM-72: end-to-end smoke test for the HTML_DOM graphics backend,
// written to produce a real PASS/FAIL in a real browser.
//
// Unlike the CANVAS backend's own smoke test -- which could only ever prove that it configures and
// links, because its dev loop had no browser -- this one asserts the actual rendered result: it
// inspects the DOM the backend produced, checking the surface element, the sprite pool, the CSS
// transforms and background images, the element recycling across frames, and a RenderTarget2D
// round-trip. It is driven headlessly by scripts/run-htmldom-browser-test.sh (Chromium via
// Playwright), which reads the same PASS/FAIL lines this file prints.
//
// Exit code 0 = every check passed, 1 = at least one failed. In the browser the exit code is not
// observable, so the outcome is also published on window.__cnaSmokeResult for the driver to read.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"

#include "CNA/Internal/Backends/HtmlDom/HtmlDomGraphicsBackend.hpp"

#include <algorithm>
#include <cstdio>
#include <memory>
#include <vector>

#if defined(__EMSCRIPTEN__)
#include <emscripten.h>
#endif

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;
using namespace CNA::Internal::Backends::HtmlDom;

namespace
{
    constexpr int kExpectedChecks = 17;

#if defined(__EMSCRIPTEN__)
    /// Number of sprite elements currently visible in the DOM surface.
    EM_JS(int, JsVisibleSpriteCount, (), {
        const root = document.getElementById('cna-dom-root');
        if (!root) return -1;
        let n = 0;
        for (const el of root.children) if (el.style.display !== 'none') ++n;
        return n;
    });

    /// Total number of pooled sprite elements, visible or not.
    EM_JS(int, JsPooledSpriteCount, (), {
        const root = document.getElementById('cna-dom-root');
        return root ? root.children.length : -1;
    });

    EM_JS(int, JsSurfaceExists, (), {
        return document.getElementById('cna-dom-root') ? 1 : 0;
    });

    /// 1 when the surface's background matches the requested clear colour.
    ///
    /// Parsed by splitting rather than with a regular expression, and the same goes for every other
    /// helper here: a backslash in an EM_JS body does not survive the preprocessor's stringification
    /// of that body, so `/(\d+)/g` silently becomes `/(d+)/g` -- a check that then quietly matches
    /// nothing instead of failing loudly. (Found the hard way: three checks in this file failed
    /// against a backend that was producing exactly the right DOM.)
    EM_JS(int, JsClearColorMatches, (int r, int g, int b), {
        const root = document.getElementById('cna-dom-root');
        if (!root) return 0;
        const bg = getComputedStyle(root).backgroundColor;   // "rgb(100, 149, 237)" / "rgba(...)"
        const open = bg.indexOf('(');
        const close = bg.indexOf(')');
        if (open < 0 || close < 0) return 0;
        const parts = bg.substring(open + 1, close).split(',');
        if (parts.length < 3) return 0;
        return (parseInt(parts[0], 10) === r && parseInt(parts[1], 10) === g &&
                parseInt(parts[2], 10) === b) ? 1 : 0;
    });

    /// 1 when sprite `i`'s inline `transform` contains `needle`.
    ///
    /// Spaces are stripped from both sides first: the CSSOM re-serializes what was assigned, so a
    /// written `translate(8px,8px)` reads back as `translate(8px, 8px)`. Comparing raw strings would
    /// assert the browser's formatting rather than the geometry. Done with split/join instead of a
    /// regular expression for the reason given on JsClearColorMatches above.
    EM_JS(int, JsSpriteTransformContains, (int i, const char* needle), {
        const root = document.getElementById('cna-dom-root');
        if (!root || !root.children[i]) return 0;
        const strip = function(s) { return s.split(' ').join(''); };
        return strip(root.children[i].style.transform).indexOf(strip(UTF8ToString(needle))) >= 0 ? 1 : 0;
    });

    /// 1 when sprite `i` is textured from a generated data URL rather than left unpainted.
    EM_JS(int, JsSpriteHasDataUrlBackground, (int i), {
        const root = document.getElementById('cna-dom-root');
        if (!root || !root.children[i]) return 0;
        return root.children[i].style.backgroundImage.indexOf('url("data:image/png') >= 0 ? 1 : 0;
    });

    /// Sprite `i`'s inline width in CSS pixels, or -1.
    EM_JS(int, JsSpriteWidth, (int i), {
        const root = document.getElementById('cna-dom-root');
        if (!root || !root.children[i]) return -1;
        return parseInt(root.children[i].style.width, 10);
    });

    /// Sprite `i`'s opacity, scaled to 0..255, or -1.
    EM_JS(int, JsSpriteOpacity255, (int i), {
        const root = document.getElementById('cna-dom-root');
        if (!root || !root.children[i]) return -1;
        return Math.round(parseFloat(root.children[i].style.opacity) * 255);
    });

    /// 1 when the SDL canvas is hidden (the DOM surface renders in its place).
    EM_JS(int, JsCanvasHidden, (), {
        const canvas = Module['canvas'] || document.querySelector('canvas');
        return canvas && canvas.style.visibility === 'hidden' ? 1 : 0;
    });

    EM_JS(void, JsPublishResult, (int result, int passed, int expected), {
        window.__cnaSmokeResult = result;
        window.__cnaSmokePassed = passed;
        window.__cnaSmokeExpected = expected;
        window.__cnaSmokeDone = true;
    });
#else
    int JsVisibleSpriteCount() { return -1; }
    int JsPooledSpriteCount() { return -1; }
    int JsSurfaceExists() { return 0; }
    int JsClearColorMatches(int, int, int) { return 0; }
    int JsSpriteTransformContains(int, const char*) { return 0; }
    int JsSpriteHasDataUrlBackground(int) { return 0; }
    int JsSpriteWidth(int) { return -1; }
    int JsSpriteOpacity255(int) { return -1; }
    int JsCanvasHidden() { return 0; }
    void JsPublishResult(int, int, int) {}
#endif
}

class HtmlDomSmokeTest : public Game
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
        // 2x2 RGBA8, one distinct fully-opaque colour per texel.
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
        auto& backend = static_cast<HtmlDomGraphicsBackend&>(dev.GetBackend());

        if (frame_ == 1)
        {
            check(backend.GetWindowInternal() != nullptr,
                  "GraphicsDevice has a real SDL_Window under the HTML_DOM backend");
            check(backend.GetRendererInternal() == nullptr,
                  "GetRendererInternal() is null -- no SDL_Renderer exists on this backend");
            int w = 0, h = 0;
            backend.GetViewportSize(w, h);
            check(w > 0 && h > 0, "GetViewportSize() reports a positive logical size");
            check(JsSurfaceExists() == 1, "the #cna-dom-root surface element was created");
            check(JsCanvasHidden() == 1, "the SDL <canvas> is hidden, so only the DOM surface shows");
        }

        dev.Clear(Color::CornflowerBlue);

        if (frame_ <= 2)
        {
            // Two sprites: an axis-aligned one and a rotated, tinted, flipped one.
            spriteBatch_->Begin();
            spriteBatch_->Draw(*texture_, Vector2(8, 8), Color::White);
            spriteBatch_->Draw(*texture_, Rectangle(20, 8, 16, 16), Rectangle(0, 0, 2, 2),
                               Color(128, 128, 255, 200), 0.5f, Vector2(1, 1),
                               SpriteEffects::FlipHorizontally, 0.0f);
            spriteBatch_->End();
        }
        else
        {
            // One sprite: the pool must recycle, leaving the second element hidden rather than
            // removed -- that recycling is the whole performance premise of this backend.
            spriteBatch_->Begin();
            spriteBatch_->Draw(*texture_, Vector2(8, 8), Color::White);
            spriteBatch_->End();
        }

        if (frame_ == 2)
        {
            check(JsClearColorMatches(100, 149, 237) == 1,
                  "Clear(CornflowerBlue) painted the surface background");
            check(JsVisibleSpriteCount() == 2, "both sprites are present as visible DOM elements");
            check(JsSpriteWidth(0) == 2,
                  "sprite 0's element is sized from its source rectangle (2px)");
            check(JsSpriteHasDataUrlBackground(0) == 1,
                  "sprite 0 is textured from a generated PNG data URL");
            check(JsSpriteTransformContains(0, "translate(8px,8px)") == 1,
                  "sprite 0's CSS transform places it at its destination");
            check(JsSpriteTransformContains(1, "rotate(0.5rad)") == 1,
                  "sprite 1's CSS transform carries its rotation");
            check(JsSpriteTransformContains(1, "scale(-1,1)") == 1,
                  "sprite 1's CSS transform carries its horizontal flip");
            check(JsSpriteOpacity255(1) == 200,
                  "sprite 1's tint alpha became the element's opacity");
        }

        if (frame_ == 3)
        {
            // plan_html_dom.md design decision 10: a render target is a real off-screen canvas, so
            // unlike the DOM backbuffer it can be both drawn into and read back exactly. Clear to a
            // colour no other surface in this test uses, then verify every texel came back.
            dev.SetRenderTarget(renderTarget_.get());
            dev.Clear(Color(10, 200, 90, 255));
            dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));

            std::vector<Color> pixels(16, Color(0xCD, 0xCD, 0xCD, 0xCD));
            bool readbackOk = false;
            try
            {
                renderTarget_->GetData(pixels.data(), 0, static_cast<int>(pixels.size()));
                readbackOk = true;
            }
            catch (const std::exception& e)
            {
                std::printf("       RenderTarget2D::GetData threw: %s\n", e.what());
            }
            check(readbackOk, "RenderTarget2D::GetData completes against the DOM backend");
            const bool allMatch = readbackOk &&
                std::all_of(pixels.begin(), pixels.end(), [](const Color& c) {
                    return c.getRProperty() == 10 && c.getGProperty() == 200 &&
                           c.getBProperty() == 90 && c.getAProperty() == 255;
                });
            check(allMatch, "the render target read back exactly what was cleared into it");

            // And the backbuffer genuinely cannot be read: it is a live DOM subtree.
            bool backbufferRefused = false;
            try
            {
                std::vector<Color> bb(4, Color(0, 0, 0, 0));
                dev.GetBackBufferData(bb.data(), 0, static_cast<int>(bb.size()));
            }
            catch (const std::exception&) { backbufferRefused = true; }
            check(backbufferRefused, "backbuffer readback is refused rather than fabricated");
        }

        if (frame_ == 4)
        {
            check(JsVisibleSpriteCount() == 1 && JsPooledSpriteCount() == 2,
                  "the element pool recycles: 1 visible, 2 retained");

            std::printf("=== %d/%d PASS ===\n", passCount_, kExpectedChecks);
            std::fflush(stdout);
            result_ = (passCount_ == kExpectedChecks) ? 0 : 1;
            JsPublishResult(result_, passCount_, kExpectedChecks);
            Exit();
        }
    }

public:
    HtmlDomSmokeTest()
    {
        gdm_ = std::make_unique<GraphicsDeviceManager>(this);
        gdm_->setPreferredBackBufferWidthProperty(64);
        gdm_->setPreferredBackBufferHeightProperty(64);
    }

    int getResult() const { return result_; }
};

int main()
{
    HtmlDomSmokeTest game;
    game.Run();
    return game.getResult();
}
