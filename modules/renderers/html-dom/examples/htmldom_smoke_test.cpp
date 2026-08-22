// SPDX-License-Identifier: MS-PL
//
// plans/plan_html_dom.md HTMLDOM-15/HTMLDOM-72: end-to-end smoke test for the HTML_DOM graphics renderer,
// written to produce a real PASS/FAIL in a real browser.
//
// Unlike the CANVAS renderer's own smoke test -- which could only ever prove that it configures and
// links, because its dev loop had no browser -- this one asserts the actual rendered result: it
// inspects the DOM the renderer produced, checking the surface element, the sprite pool, the CSS
// transforms and background images, the element recycling across frames, and a RenderTarget2D
// round-trip. It is driven headlessly by scripts/run-htmldom-browser-test.sh (Chromium via
// Playwright), which reads the same PASS/FAIL lines this file prints.
//
// Exit code 0 = every check passed, 1 = at least one failed. In the browser the exit code is not
// observable, so the outcome is also published on window.__cnaSmokeResult for the driver to read.

#include "Microsoft/Xna/Framework/Game.hpp"
#include "Microsoft/Xna/Framework/GraphicsDeviceManager.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/SamplerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteFont.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/Viewport.hpp"

#include "CNA/Internal/Renderers/HtmlDom/HtmlDomRenderer.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cstdio>
#include <memory>
#include <vector>

#if defined(__EMSCRIPTEN__)
#include <emscripten.h>
#endif

using namespace Microsoft::Xna::Framework;
using namespace Microsoft::Xna::Framework::Graphics;
using namespace CNA::Internal::Renderers::HtmlDom;

namespace
{
    constexpr int kExpectedChecks = 69;

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
    /// against a renderer that was producing exactly the right DOM.)
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
        const strip = function(s) { return s.split(" ").join(""); };
        return strip(root.children[i].style.transform).indexOf(strip(UTF8ToString(needle))) >= 0 ? 1 : 0;
    });

    /// plans/plan_html_dom.md HTMLDOM-107: 1 when the DEFAULT ('full') region's own pool element at
    /// `poolIndex`'s inline `transform` contains `needle`. Reads the pool array directly (not
    /// `root.children[i]`) for the same reason JsFullRegionSpriteZIndexAt (HTMLDOM-103) does:
    /// `root.children[i]` for a small `i` is NOT reliably the i-th 'full' sprite once named scissor
    /// regions exist, since their own container `<div>`s are ALSO direct children of root,
    /// interspersed among 'full' sprites by creation order rather than pool index.
    EM_JS(int, JsFullRegionSpriteTransformContains, (int poolIndex, const char* needle), {
        const regions = Module['cnaDomRegions'];
        const full = regions ? regions['full'] : null;
        const el = full ? full.pool[poolIndex] : null;
        if (!el) return 0;
        const strip = function(s) { return s.split(" ").join(""); };
        return strip(el.style.transform).indexOf(strip(UTF8ToString(needle))) >= 0 ? 1 : 0;
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

    /// 1 when the SDL canvas is transparent but remains available for pointer input.
    EM_JS(int, JsCanvasTransparent, (), {
        const canvas = Module['canvas'] || document.querySelector('canvas');
        return canvas && canvas.style.opacity === '0' ? 1 : 0;
    });

    /// plans/plan_html_dom.md HTMLDOM-113: 1 when THIS browser actually recognises
    /// `mix-blend-mode: plus-lighter` (docs/html-dom-renderer.md's own documented "requires
    /// Chromium 108+/Safari 16.4+/Firefox 122+" boundary -- older engines silently ignore the
    /// value and Additive degrades to normal alpha blending instead of failing, the one place this
    /// renderer degrades silently rather than throwing). An automated CI browser cannot itself be
    /// downgraded to exercise that fallback path, so this check instead confirms the assumption
    /// every OTHER Additive test in this suite depends on: that plus-lighter is genuinely
    /// supported here, and those tests are therefore exercising the real blend, not a silently
    /// degraded one none of them would be able to tell apart from the real thing.
    EM_JS(int, JsSupportsPlusLighter, (), {
        return (typeof CSS !== 'undefined' && CSS.supports &&
                CSS.supports('mix-blend-mode', 'plus-lighter')) ? 1 : 0;
    });

    /// plans/plan_html_dom.md HTMLDOM-94: 1 when a region exists for the exact rectangle `(x,y,w,h)` (a
    /// rectangle that currently covers the whole surface never gets a real region -- it collapses
    /// to the default, unclipped one -- so this always returns 0 for one of those).
    EM_JS(int, JsRegionExists, (int x, int y, int w, int h), {
        const regions = Module['cnaDomRegions'];
        if (!regions) return 0;
        const key = x + ',' + y + ',' + w + ',' + h;
        return regions[key] ? 1 : 0;
    });

    /// 1 when the region for rectangle `(x,y,w,h)` exists and its container's clip-path is
    /// `inset(top right bottom left)` with those four values (in px). Compares parsed numbers
    /// rather than the raw string: the CSSOM normalizes `inset(0px 0px 0px 0px)` down to the
    /// shorthand `inset(0px)` when all four values are equal, so an exact-string comparison would
    /// be asserting the serializer's own formatting choice rather than the geometry.
    // Deliberately no regular expression: a backslash inside an EM_JS body does not survive the
    // preprocessor's stringification of that body (see JsClearColorMatches's own comment, above,
    // for the bug this caused once already in this same file) -- parsed with plain
    // indexOf/substring/split instead.
    EM_JS(int, JsRegionClipPathInsetIs, (int x, int y, int w, int h,
                                         int top, int right, int bottom, int left), {
        const regions = Module['cnaDomRegions'];
        const region = regions ? regions[x + ',' + y + ',' + w + ',' + h] : null;
        if (!region) return 0;
        const cp = region.container.style.clipPath;
        const open = cp.indexOf('inset(');
        const close = cp.indexOf(')');
        if (open !== 0 || close < 0) return 0;
        const inner = cp.substring(open + 6, close).trim();
        const nums = inner.split(' ').filter(function(s) { return s.length > 0; })
                          .map(function(s) { return parseFloat(s); });
        // The shorthand normalizes like margin/padding: 1 value = all four, 2 = top/bottom,
        // left/right, 4 = top,right,bottom,left explicitly.
        let t, r, b, l;
        if (nums.length === 1) { t = r = b = l = nums[0]; }
        else if (nums.length === 2) { t = b = nums[0]; r = l = nums[1]; }
        else if (nums.length === 4) { t = nums[0]; r = nums[1]; b = nums[2]; l = nums[3]; }
        else { return 0; }
        return (t === top && r === right && b === bottom && l === left) ? 1 : 0;
    });

    /// Number of currently-visible sprites inside the region for rectangle `(x,y,w,h)`, or -1 if
    /// that region does not exist.
    EM_JS(int, JsRegionVisibleSpriteCount, (int x, int y, int w, int h), {
        const regions = Module['cnaDomRegions'];
        const region = regions ? regions[x + ',' + y + ',' + w + ',' + h] : null;
        if (!region) return -1;
        let n = 0;
        for (const el of region.container.children) if (el.style.display !== 'none') ++n;
        return n;
    });

    /// plans/plan_html_dom.md HTMLDOM-101: the region container's own real, COMPUTED layout box size
    /// (offsetWidth/offsetHeight -- requires actual browser layout to have run), or -1 if the region
    /// does not exist. Unlike JsRegionClipPathInsetIs (which only reads back the inline style STRING
    /// CNA wrote), this proves the container genuinely has a non-zero box for that clip-path to clip
    /// against -- the HTMLDOM-101 defect was a region whose clip-path values were exactly right but
    /// whose own box was still 0x0, which no clip-path-string check could ever catch.
    EM_JS(int, JsRegionOffsetWidth, (int x, int y, int w, int h), {
        const regions = Module['cnaDomRegions'];
        const region = regions ? regions[x + ',' + y + ',' + w + ',' + h] : null;
        return region ? region.container.offsetWidth : -1;
    });

    /// plans/plan_html_dom.md HTMLDOM-101: see JsRegionOffsetWidth -- the same, for offsetHeight.
    EM_JS(int, JsRegionOffsetHeight, (int x, int y, int w, int h), {
        const regions = Module['cnaDomRegions'];
        const region = regions ? regions[x + ',' + y + ',' + w + ',' + h] : null;
        return region ? region.container.offsetHeight : -1;
    });

    /// plans/plan_html_dom.md HTMLDOM-103: the region for rectangle `(x,y,w,h)`'s own container's current
    /// CSS `z-index` (the paint-order value its most recent flush stamped on it), or -1 if the
    /// region does not exist or carries no z-index yet.
    EM_JS(int, JsRegionZIndex, (int x, int y, int w, int h), {
        const regions = Module['cnaDomRegions'];
        const region = regions ? regions[x + ',' + y + ',' + w + ',' + h] : null;
        if (!region) return -1;
        const z = region.container.style.zIndex;
        return z ? parseInt(z, 10) : -1;
    });

    /// plans/plan_html_dom.md HTMLDOM-103: the DEFAULT ('full') region's own pool element at
    /// `poolIndex`'s current CSS `z-index`, or -1 if that pool slot does not exist. Reads the pool
    /// array directly (not `root.children[i]`) so the index is exactly the same one the renderer
    /// itself uses, with no dependency on where that element happens to sit in root's child list.
    EM_JS(int, JsFullRegionSpriteZIndexAt, (int poolIndex), {
        const regions = Module['cnaDomRegions'];
        const full = regions ? regions['full'] : null;
        const el = full ? full.pool[poolIndex] : null;
        if (!el) return -1;
        const z = el.style.zIndex;
        return z ? parseInt(z, 10) : -1;
    });

    /// 1 when sprite `i`'s `background-repeat` is `'repeat'` (TextureAddressMode::Wrap tiling).
    EM_JS(int, JsSpriteBackgroundRepeat, (int i), {
        const root = document.getElementById('cna-dom-root');
        if (!root || !root.children[i]) return 0;
        return root.children[i].style.backgroundRepeat === 'repeat' ? 1 : 0;
    });

    /// plans/plan_html_dom.md HTMLDOM-97: 1 when sprite `i`'s `background-repeat` equals `expected`
    /// exactly (e.g. `"repeat no-repeat"` for a mixed-axis draw) -- the general form of
    /// JsSpriteBackgroundRepeat above, needed once the two axes can carry different values.
    EM_JS(int, JsSpriteBackgroundRepeatIs, (int i, const char* expected), {
        const root = document.getElementById('cna-dom-root');
        if (!root || !root.children[i]) return 0;
        return root.children[i].style.backgroundRepeat === UTF8ToString(expected) ? 1 : 0;
    });

    /// plans/plan_html_dom.md HTMLDOM-98: #cna-dom-root's own inline `width`, in CSS px, or -1.
    EM_JS(int, JsRootWidth, (), {
        const root = document.getElementById('cna-dom-root');
        return root ? parseInt(root.style.width, 10) : -1;
    });

    /// plans/plan_html_dom.md HTMLDOM-98: #cna-dom-root's own inline `height`, in CSS px, or -1.
    EM_JS(int, JsRootHeight, (), {
        const root = document.getElementById('cna-dom-root');
        return root ? parseInt(root.style.height, 10) : -1;
    });

    /// plans/plan_html_dom.md HTMLDOM-98: #cna-dom-root's own inline `left`, in CSS px (fractional), or -1.
    EM_JS(double, JsRootLeft, (), {
        const root = document.getElementById('cna-dom-root');
        return root ? parseFloat(root.style.left) : -1;
    });

    /// plans/plan_html_dom.md HTMLDOM-98: #cna-dom-root's own inline `top`, in CSS px (fractional), or -1.
    EM_JS(double, JsRootTop, (), {
        const root = document.getElementById('cna-dom-root');
        return root ? parseFloat(root.style.top) : -1;
    });

    /// plans/plan_html_dom.md HTMLDOM-108: the per-CnaPresentationMode scale factor
    /// HtmlDomRenderer::ComputeLogicalViewport last computed and CNA_HtmlDom_UpdateSurface
    /// applied to #cna-dom-root, read directly from the Module state it is stored in (not parsed
    /// back out of the CSS transform string, which is empty whenever the scale is exactly 1).
    EM_JS(double, JsViewportScaleX, (), {
        const v = Module['cnaDomViewportScaleX'];
        return v === undefined ? -1 : v;
    });
    EM_JS(double, JsViewportScaleY, (), {
        const v = Module['cnaDomViewportScaleY'];
        return v === undefined ? -1 : v;
    });

    /// plans/plan_html_dom.md HTMLDOM-108: #cna-dom-viewport's own inline geometry, in CSS px -- the
    /// physical-canvas-sized wrapper that clips the scaled/offset #cna-dom-root (needed for
    /// Overscan, whose root deliberately extends past the canvas's own bounds).
    EM_JS(int, JsViewportWrapperWidth, (), {
        const el = Module['cnaDomViewportEl'];
        return el ? parseInt(el.style.width, 10) : -1;
    });
    EM_JS(int, JsViewportWrapperHeight, (), {
        const el = Module['cnaDomViewportEl'];
        return el ? parseInt(el.style.height, 10) : -1;
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
    int JsFullRegionSpriteTransformContains(int, const char*) { return 0; }
    int JsSpriteHasDataUrlBackground(int) { return 0; }
    int JsSpriteWidth(int) { return -1; }
    int JsSpriteOpacity255(int) { return -1; }
    int JsCanvasTransparent() { return 0; }
    int JsSupportsPlusLighter() { return 0; }
    int JsSpriteBackgroundRepeat(int) { return 0; }
    int JsSpriteBackgroundRepeatIs(int, const char*) { return 0; }
    int JsRootWidth() { return -1; }
    int JsRootHeight() { return -1; }
    double JsRootLeft() { return -1; }
    double JsRootTop() { return -1; }
    double JsViewportScaleX() { return -1; }
    double JsViewportScaleY() { return -1; }
    int JsViewportWrapperWidth() { return -1; }
    int JsViewportWrapperHeight() { return -1; }
    int JsRegionExists(int, int, int, int) { return 0; }
    int JsRegionClipPathInsetIs(int, int, int, int, int, int, int, int) { return 0; }
    int JsRegionVisibleSpriteCount(int, int, int, int) { return -1; }
    int JsRegionOffsetWidth(int, int, int, int) { return -1; }
    int JsRegionOffsetHeight(int, int, int, int) { return -1; }
    int JsRegionZIndex(int, int, int, int) { return -1; }
    int JsFullRegionSpriteZIndexAt(int) { return -1; }
    void JsPublishResult(int, int, int) {}
#endif
}

class HtmlDomSmokeTest : public Game
{
    std::unique_ptr<GraphicsDeviceManager> gdm_;
    std::unique_ptr<SpriteBatch> spriteBatch_;
    std::unique_ptr<Texture2D> texture_;
    std::unique_ptr<RenderTarget2D> renderTarget_;
    std::unique_ptr<SpriteFont> font_;
    // plans/plan_html_dom.md HTMLDOM-102: SpriteBatch::Begin() always applies EITHER the RasterizerState
    // passed to it OR RasterizerState::CullCounterClockwise as its own default (SpriteBatch.cpp,
    // matching FNA's PrepRenderState) -- so GraphicsDevice.RasterizerState set directly, separately
    // from Begin(), is overwritten by the very next Begin() call regardless. Every scissor-rect
    // check below that needs clipping to actually apply must pass this explicitly.
    RasterizerState scissorEnabledState_;
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

        // plans/plan_html_dom.md HTMLDOM-38: a one-glyph SpriteFont ('A', a 4x4 fully-opaque atlas cell,
        // no cropping offset, no bearing) -- enough to exercise DrawString() through the shared
        // SpriteFont/SpriteBatch layer and confirm it reaches the DOM path with zero renderer-
        // specific code, per this renderer's own claim.
        std::vector<std::uint8_t> glyphPixels(4 * 4 * 4, 0);
        for (std::size_t i = 0; i < glyphPixels.size(); i += 4)
        {
            glyphPixels[i] = 255; glyphPixels[i + 1] = 200; glyphPixels[i + 2] = 0; glyphPixels[i + 3] = 255;
        }
        Texture2D glyphAtlas = Texture2D::CreateFromPixels(getGraphicsDeviceProperty(), 4, 4, glyphPixels);
        font_ = std::make_unique<SpriteFont>(
            glyphAtlas,
            std::vector<Rectangle>{Rectangle(0, 0, 4, 4)},
            std::vector<Rectangle>{Rectangle(0, 0, 4, 4)},
            std::vector<charcs>{u'A'},
            /*lineSpacing=*/4, /*spacing=*/0.0f,
            std::vector<Vector3>{Vector3(0.0f, 4.0f, 0.0f)},
            std::nullopt);
    }

    void Draw(const GameTime&) override
    {
        ++frame_;
        auto& dev = getGraphicsDeviceProperty();
        auto& renderer = static_cast<HtmlDomRenderer&>(dev.GetRenderer());

        if (frame_ == 1)
        {
            check(reinterpret_cast<SDL_Window*>(getWindowProperty().getHandleProperty()) != nullptr,
                  "GraphicsDevice has a real SDL_Window under the HTML_DOM renderer");
            check(SDL_GetRenderer(reinterpret_cast<SDL_Window*>(getWindowProperty().getHandleProperty())) == nullptr,
                  "SDL_GetRenderer(window) is null -- no SDL_Renderer exists on this renderer");
            int w = 0, h = 0;
            renderer.GetViewportSize(w, h);
            check(w > 0 && h > 0, "GetViewportSize() reports a positive logical size");
            check(JsSurfaceExists() == 1, "the #cna-dom-root surface element was created");
            check(JsCanvasTransparent() == 1,
                  "the SDL <canvas> is transparent but remains active for pointer input");
            check(JsSupportsPlusLighter() == 1,
                  "HTMLDOM-113: this test browser genuinely supports mix-blend-mode: "
                  "plus-lighter -- every other Additive pixel check in this suite (and "
                  "htmldom_pixel_verification_test.cpp's own) is therefore exercising the real "
                  "CSS blend, not this renderer's documented old-browser silent-degradation "
                  "fallback, which an automated CI browser cannot itself be downgraded to exercise");
            // plans/plan_html_dom.md HTMLDOM-117: the PUBLIC, game-facing query -- not just the raw CSS
            // check the line above uses directly -- agrees with it. A real game can now check
            // GraphicsDevice::SupportsCapability(GraphicsCapability::AdditiveBlending) BEFORE
            // relying on genuine additive compositing, instead of only being able to infer the
            // silent degradation by comparing rendered pixels after the fact.
            check(dev.SupportsCapability(CNA::GraphicsCapability::AdditiveBlending) ==
                  (JsSupportsPlusLighter() == 1),
                  "HTMLDOM-117: GraphicsDevice::SupportsCapability(AdditiveBlending) reports the "
                  "SAME real answer as the raw CSS.supports() query it wraps -- a real, queryable "
                  "capability, not a blanket false this renderer gives every other 2D-only-boundary "
                  "capability");
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
        else if (frame_ <= 4)
        {
            // One sprite: the pool must recycle, leaving the second element hidden rather than
            // removed -- that recycling is the whole performance premise of this renderer.
            spriteBatch_->Begin();
            spriteBatch_->Draw(*texture_, Vector2(8, 8), Color::White);
            spriteBatch_->End();
        }
        // frame_ == 5 draws nothing here -- its own block below owns this frame's drawing
        // entirely, so its sprite indices are known exactly (0 = glyph, 1 = Wrap sprite).

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
            // plans/plan_html_dom.md design decision 10: a render target is a real off-screen canvas, so
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
            check(readbackOk, "RenderTarget2D::GetData completes against the DOM renderer");
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
        }

        if (frame_ == 5)
        {
            // plans/plan_html_dom.md HTMLDOM-38: DrawString needs no renderer-specific code -- every
            // glyph funnels through the same Draw() overload as an ordinary sprite.
            spriteBatch_->Begin();
            spriteBatch_->DrawString(*font_, "A", Vector2(2, 2), Color::White);
            spriteBatch_->End();

            // plans/plan_html_dom.md HTMLDOM-45: TextureAddressMode::Wrap, only distinguishable from
            // Clamp once the requested sourceRectangle exceeds the texture's own bounds -- draw the
            // 2x2 texture with a 4x4 source rectangle (double its size) under PointWrap.
            spriteBatch_->Begin(SpriteSortMode::Deferred, BlendState::AlphaBlend,
                                const_cast<SamplerState*>(&SamplerState::PointWrap), nullptr, nullptr);
            spriteBatch_->Draw(*texture_, Rectangle(40, 40, 8, 8), Rectangle(0, 0, 4, 4), Color::White);
            spriteBatch_->End();

            check(JsVisibleSpriteCount() == 2,
                  "both the glyph and the Wrap-addressed sprite are present as DOM elements");
            check(JsSpriteHasDataUrlBackground(0) == 1,
                  "DrawString's glyph is textured from a generated PNG data URL");
            check(JsSpriteWidth(0) == 4,
                  "the glyph element is sized from the font atlas's own glyph bounds (4px)");
            check(JsSpriteWidth(1) == 4,
                  "Wrap keeps the full out-of-bounds sourceRectangle width (4px), unlike Clamp");
            check(JsSpriteBackgroundRepeat(1) == 1,
                  "TextureAddressMode::Wrap maps to CSS background-repeat: repeat");
        }

        if (frame_ == 6)
        {
            // plans/plan_html_dom.md HTMLDOM-45's other half: while a render target is bound, Wrap is
            // implemented by a completely SEPARATE code path -- a Canvas2D repeating pattern
            // (CNA_HtmlDom_FlushSprites' `targetCtx` branch), not the CSS background-repeat frame 5
            // just checked. That path has never been exercised at all until now, structurally or
            // otherwise, so this checks it pixel-exact rather than just structurally: draw the 2x2
            // texture into the 4x4 render target with a matching 4x4 sourceRectangle under Wrap,
            // which must tile the source twice across each axis.
            dev.SetRenderTarget(renderTarget_.get());
            spriteBatch_->Begin(SpriteSortMode::Deferred, BlendState::Opaque,
                                const_cast<SamplerState*>(&SamplerState::PointWrap), nullptr, nullptr);
            spriteBatch_->Draw(*texture_, Rectangle(0, 0, 4, 4), Rectangle(0, 0, 4, 4), Color::White);
            spriteBatch_->End();
            dev.SetRenderTarget(static_cast<RenderTarget2D*>(nullptr));

            std::vector<Color> pixels(16, Color(0xCD, 0xCD, 0xCD, 0xCD));
            bool readOk = false;
            try
            {
                renderTarget_->GetData(pixels.data(), 0, static_cast<int>(pixels.size()));
                readOk = true;
            }
            catch (const std::exception& e)
            {
                std::printf("       RenderTarget2D::GetData (Wrap tile) threw: %s\n", e.what());
            }
            check(readOk, "the Wrap-tiled render target can be read back");

            // Source layout (LoadContent's comment): (0,0)=red (1,0)=green (0,1)=blue (1,1)=yellow.
            // Tiled 2x2 across the 4x4 target, texel (x,y) must equal source texel (x%2, y%2).
            bool tileMatches = readOk;
            for (int y = 0; tileMatches && y < 4; ++y)
            {
                for (int x = 0; x < 4; ++x)
                {
                    const Color& actual = pixels[static_cast<std::size_t>(y) * 4 + x];
                    const bool wantRed = (x % 2 == 0 && y % 2 == 0);
                    const bool wantGreen = (x % 2 == 1 && y % 2 == 0);
                    const bool wantBlue = (x % 2 == 0 && y % 2 == 1);
                    const Color expected = wantRed   ? Color(255, 0, 0, 255)
                                          : wantGreen ? Color(0, 255, 0, 255)
                                          : wantBlue  ? Color(0, 0, 255, 255)
                                                      : Color(255, 255, 0, 255);
                    if (actual.getRProperty() != expected.getRProperty() ||
                        actual.getGProperty() != expected.getGProperty() ||
                        actual.getBProperty() != expected.getBProperty())
                    {
                        std::printf("       tile mismatch at (%d,%d): got (%d,%d,%d), want (%d,%d,%d)\n",
                                    x, y, actual.getRProperty(), actual.getGProperty(), actual.getBProperty(),
                                    expected.getRProperty(), expected.getGProperty(), expected.getBProperty());
                        tileMatches = false;
                        break;
                    }
                }
            }
            check(tileMatches,
                  "the render-target Wrap path tiles the source texture exactly, pixel-for-pixel");
        }

        // plans/plan_html_dom.md HTMLDOM-80/94: SetScissorRect, implemented as a real clip-path on a
        // dedicated DOM region per distinct scissor rect (design decision 13, revised by HTMLDOM-94)
        // -- verified the same way every other CSS-property mapping in this file is (checking the
        // computed value against a hand-derived expected string), consistent with how
        // transform/backgroundRepeat/mixBlendMode are already checked above, rather than a pixel
        // screenshot: the browser's own clip-path rendering is standard engine behaviour this
        // project doesn't re-verify anywhere else either.
        //
        // Each scissor rect below is set, then immediately consumed by drawing one sprite in its own
        // Begin/End batch -- a region is only created when a batch actually flushes under that rect
        // (HTMLDOM-94), so checking the clip-path right after SetScissorRect with nothing drawn in
        // between (the OLD, pre-HTMLDOM-94 test shape) would find nothing.
        if (frame_ == 7)
        {
            // plans/plan_html_dom.md HTMLDOM-102: RasterizerState.ScissorTestEnable, gating whether
            // SetScissorRect's recorded rect clips anything at all -- an earlier version applied it
            // unconditionally, matching SDL_RENDERER's own omission but not real XNA fidelity (in
            // real XNA a game must explicitly enable scissor testing via RasterizerState; setting
            // ScissorRectangle alone does nothing). Verified FIRST, before the HTMLDOM-80/94 checks
            // below, all of which assume scissor testing is enabled: disabled means a narrow rect
            // creates NO region; re-enabling on the very next batch, same rect, DOES.
            //
            // A real finding while writing this: GraphicsDevice.RasterizerState set directly (e.g.
            // dev.setRasterizerStateProperty(...)) is NOT what SpriteBatch draws under -- Begin()
            // itself always (re-)applies EITHER the RasterizerState passed to it OR
            // RasterizerState::CullCounterClockwise (ScissorTestEnable=false) as its own default,
            // exactly like FNA's PrepRenderState (SpriteBatch.cpp, REMED-GFX-081) -- so a plain,
            // no-argument Begin() call always disables scissor testing regardless of anything set
            // beforehand, and the ENABLED case has to pass scissorEnabledState_ to Begin() directly.
            dev.setScissorRectangleProperty(Rectangle(2, 2, 4, 4));
            spriteBatch_->Begin();
            spriteBatch_->Draw(*texture_, Vector2(2, 2), Color::White);
            spriteBatch_->End();
            check(JsRegionExists(2, 2, 4, 4) == 0,
                  "HTMLDOM-102a: a plain Begin() (RasterizerState::CullCounterClockwise's own "
                  "ScissorTestEnable=false default) means SetScissorRect's recorded rect does NOT "
                  "clip -- no region is created for it at all");

            spriteBatch_->Begin(SpriteSortMode::Deferred, BlendState::AlphaBlend, nullptr, nullptr,
                                &scissorEnabledState_);
            spriteBatch_->Draw(*texture_, Vector2(2, 2), Color::White);
            spriteBatch_->End();
            check(JsRegionExists(2, 2, 4, 4) == 1,
                  "HTMLDOM-102b: re-enabling ScissorTestEnable on the VERY NEXT batch, same "
                  "recorded rect, now creates a real clipped region -- the enable bit is captured "
                  "at the same per-batch granularity as the rect itself, not stale from an earlier "
                  "flush's state");

            // Every other scissor test in this file (below, and in frames 8/10/11) needs scissor
            // testing enabled too, so each of their own Begin() calls passes scissorEnabledState_
            // explicitly -- Begin() applies its OWN RasterizerState every time, never inherits one
            // set separately (see the finding above).

            // Backbuffer is 64x64 (see the constructor below). A rect fully inside it:
            // top=10, left=10, right=64-(10+20)=34, bottom=64-(10+30)=24.
            dev.setScissorRectangleProperty(Rectangle(10, 10, 20, 30));
            spriteBatch_->Begin(SpriteSortMode::Deferred, BlendState::AlphaBlend, nullptr, nullptr,
                                &scissorEnabledState_);
            spriteBatch_->Draw(*texture_, Vector2(0, 0), Color::White);
            spriteBatch_->End();
            check(JsRegionClipPathInsetIs(10, 10, 20, 30, 10, 34, 24, 10) == 1,
                  "HTMLDOM-80a: a scissor rect inside the surface creates a region with the exact "
                  "inset() values");
            check(JsRegionVisibleSpriteCount(10, 10, 20, 30) == 1,
                  "HTMLDOM-80a: the sprite drawn under that scissor rect lives inside its own region");

            // Resetting to the full backbuffer must collapse to the default, unclipped region
            // (#cna-dom-root itself) rather than allocating a real (no-op) clipped container.
            dev.setScissorRectangleProperty(Rectangle(0, 0, 64, 64));
            spriteBatch_->Begin(SpriteSortMode::Deferred, BlendState::AlphaBlend, nullptr, nullptr,
                                &scissorEnabledState_);
            spriteBatch_->Draw(*texture_, Vector2(40, 0), Color::White);
            spriteBatch_->End();
            check(JsRegionExists(0, 0, 64, 64) == 0,
                  "HTMLDOM-80b: a scissor rect matching the full surface collapses to the default "
                  "region instead of allocating a real clipped container");

            // The money check for HTMLDOM-94: the FIRST region (10,10,20,30) must be completely
            // unaffected by the SECOND batch's later, different scissor rect -- exactly the defect a
            // single shared #cna-dom-root clip-path had before this task (the LATEST scissor rect
            // would retroactively reclip every sprite already on screen, no matter which rect was
            // active when each one was actually drawn).
            check(JsRegionClipPathInsetIs(10, 10, 20, 30, 10, 34, 24, 10) == 1,
                  "HTMLDOM-94: an earlier batch's region keeps its own clip after a LATER batch "
                  "changes the scissor rect to something else -- no retroactive reclipping across "
                  "batches");
            check(JsRegionVisibleSpriteCount(10, 10, 20, 30) == 1,
                  "HTMLDOM-94: the earlier batch's sprite is still exactly where it was, still "
                  "clipped to its own rect, unaffected by the later batch");

            // A rect extending past the surface's own bounds: right/bottom insets would go
            // negative from the raw formula and must clamp to 0 rather than expand outward.
            dev.setScissorRectangleProperty(Rectangle(50, 50, 30, 30));
            spriteBatch_->Begin(SpriteSortMode::Deferred, BlendState::AlphaBlend, nullptr, nullptr,
                                &scissorEnabledState_);
            spriteBatch_->Draw(*texture_, Vector2(50, 50), Color::White);
            spriteBatch_->End();
            check(JsRegionClipPathInsetIs(50, 50, 30, 30, 50, 0, 0, 50) == 1,
                  "HTMLDOM-80c: a scissor rect extending past the surface clamps its far insets "
                  "to 0 instead of producing a negative (expanding) inset()");

            dev.setScissorRectangleProperty(Rectangle(0, 0, 64, 64));
        }

        // plans/plan_html_dom.md HTMLDOM-93: window resize + active scissor rect interaction, with no
        // GraphicsDevice.Reset() in between -- checks whether the clip-path's insets (computed once
        // against the surface size at SetScissorRect()-call time) go stale relative to the surface's
        // new size once it actually resizes, or get automatically re-derived.
        //
        // Deliberately calls HtmlDomRenderer::SetScissorRect/Present() directly rather than
        // going through GraphicsDevice::setScissorRectangleProperty()/Present(): a real, separate
        // finding while building this test was that GraphicsDevice::UpdateViewportFromWindow()
        // (called from every GraphicsDevice::Present(), for every renderer, not an HTML_DOM
        // peculiarity) already resets BOTH Viewport and ScissorRectangle to the complete new
        // backbuffer size the instant it detects any viewport-size change -- matching FNA's own
        // real-resize behaviour. That means this staleness can never actually manifest through the
        // public XNA API: by the time a game could observe it, GraphicsDevice has already reset the
        // scissor to the full surface itself. Confirmed non-gap at the framework level (the same
        // shape of finding as HTMLDOM-81's SetViewport one), verified by first reproducing this
        // exact scenario through dev.setScissorRectangleProperty()/dev.Present() and observing
        // GraphicsDevice's own reset win, not assumed. What this test verifies instead is
        // HtmlDomRenderer's OWN contract in isolation -- it must not depend on
        // GraphicsDevice's reset to stay correct, since nothing stops a future caller from reaching
        // the renderer directly the way this test now deliberately does.
        if (frame_ == 8)
        {
            // The constructor's PreferredBackBufferWidth/Height (64x64) already pinned a FIXED
            // virtual resolution equal to the initial backbuffer size (GraphicsDevice's own
            // constructor takes virtualWidth_/virtualHeight_ straight from
            // PresentationParameters.BackBufferWidth/Height and pushes them into the renderer via
            // SetVirtualResolution()). Force 1:1 mode (no virtual resolution) directly on the
            // renderer instead -- a real, supported configuration (getLogicalSize()'s own
            // `virtualHeight_ <= 0` branch) where logical size tracks physical size exactly, so a
            // physical resize genuinely moves the ground a previously-set scissor rect was measured
            // against.
            renderer.SetVirtualResolution(0, 0);

            // Surface is currently still 64x64 (SetVirtualResolution alone doesn't resize anything;
            // it only changes how future geometry is computed). Draw one sprite under this rect so a
            // region for it actually gets created (HTMLDOM-94: SetScissorRect alone only records
            // the rect -- a region only exists once a batch flushes under it).
            renderer.SetScissorRect(5, 5, 10, 10);
            spriteBatch_->Begin(SpriteSortMode::Deferred, BlendState::AlphaBlend, nullptr, nullptr,
                                &scissorEnabledState_);
            spriteBatch_->Draw(*texture_, Vector2(5, 5), Color::White);
            spriteBatch_->End();
            check(JsRegionClipPathInsetIs(5, 5, 10, 10, 5, 49, 49, 5) == 1,
                  "HTMLDOM-93a: a region created against the current (pre-resize) 64x64 surface "
                  "gets the expected insets");

            // Resize the real SDL window, then forward the corresponding platform-neutral surface
            // snapshot before calling the renderer's own Present(). Production does this through
            // GraphicsDevice's platform-event path; this low-level test deliberately bypasses that
            // path, so it must reproduce both halves rather than expecting the renderer to query a
            // native window that is no longer part of its interface.
            auto* window =
                reinterpret_cast<SDL_Window*>(getWindowProperty().getHandleProperty());
            SDL_SetWindowSize(window, 128, 128);
            CNA::Internal::Renderers::RendererSurfaceInfo resizedSurface;
            resizedSurface.windowId = SDL_GetWindowID(window);
            resizedSurface.nativeHandle = getWindowProperty().GetNativeWindowHandleEXT();
            resizedSurface.drawableSize = {128, 128};
            renderer.OnSurfaceChanged(resizedSurface);
            renderer.Present();

            // HTMLDOM-98: real XNA/FNA Viewport does NOT auto-track a resized backbuffer on its own
            // -- GraphicsDevice::UpdateViewportFromWindow() is the thing that resets it, and this
            // test deliberately calls the renderer directly instead of dev.Present() (see above), so
            // it must reproduce that reset by hand, the same way a complete real resize flow would
            // do both together. Without this call the renderer correctly (not a bug) keeps treating
            // the OLD 64x64 viewport as still active, which is real XNA/FNA-consistent behaviour for
            // an unreset Viewport, not something this test is checking.
            renderer.SetViewport(0, 0, 128, 128, 0.0f, 1.0f);

            // Without CNA_HtmlDom_UpdateSurface re-deriving every region's clip on resize, this
            // region would still carry the OLD (5,49,49,5) insets computed against the pre-resize
            // 64x64 surface, even though the surface itself is now 128x128 -- silently clipping the
            // wrong fraction of the resized surface instead of the same logical (5,5,10,10)
            // rectangle.
            check(JsRegionClipPathInsetIs(5, 5, 10, 10, 5, 113, 113, 5) == 1,
                  "HTMLDOM-93b: an existing region's clip is automatically re-derived against the "
                  "new 128x128 surface after a resize, with no GraphicsDevice.Reset() and no "
                  "scissor reapplication by the caller");

            renderer.SetScissorRect(0, 0, 128, 128);
        }

        // plans/plan_html_dom.md HTMLDOM-97: the DOM-path (no render target bound) side of Mirror/mixed-
        // axis addressing -- htmldom_pixel_verification_test.cpp already checked the Canvas2D
        // render-target branch pixel-exact; this is the OTHER of the two separate code paths
        // CNA_HtmlDom_FlushSprites has (design decision 10's own split), checked structurally the
        // same way frame 5's Wrap check above is (CSS property values, not pixels -- consistent
        // with how every other DOM-path property in this file is verified). Draws with no scissor
        // rect active (frame 8 left it at the full 128x128 surface), so both sprites land in the
        // default 'full' region at predictable indices 0 and 1, the same as every other
        // non-scissored frame in this file.
        if (frame_ == 9)
        {
            SamplerState mirrorSampler;
            mirrorSampler.setAddressUProperty(TextureAddressMode::Mirror);
            mirrorSampler.setAddressVProperty(TextureAddressMode::Mirror);
            spriteBatch_->Begin(SpriteSortMode::Deferred, BlendState::AlphaBlend, &mirrorSampler,
                                nullptr, nullptr);
            spriteBatch_->Draw(*texture_, Rectangle(2, 2, 8, 8), Rectangle(0, 0, 4, 4), Color::White);
            spriteBatch_->End();
            check(JsSpriteHasDataUrlBackground(0) == 1,
                  "HTMLDOM-97c: a symmetric-Mirror DOM sprite is textured from a generated PNG "
                  "data URL (the pre-tiled mirrored variant, not a throw)");
            check(JsSpriteBackgroundRepeat(0) == 1,
                  "HTMLDOM-97c: symmetric Mirror maps to CSS background-repeat: repeat -- tiling "
                  "the pre-built mirrored image reproduces mirror-repeat");

            SamplerState mixedSampler;
            mixedSampler.setAddressUProperty(TextureAddressMode::Wrap);
            mixedSampler.setAddressVProperty(TextureAddressMode::Clamp);
            spriteBatch_->Begin(SpriteSortMode::Deferred, BlendState::AlphaBlend, &mixedSampler,
                                nullptr, nullptr);
            spriteBatch_->Draw(*texture_, Rectangle(20, 20, 8, 8), Rectangle(0, 0, 4, 4), Color::White);
            spriteBatch_->End();
            // The CSSOM serializes the two-value 'repeat no-repeat' back as the single-keyword
            // shorthand 'repeat-x' (repeat-x is literally defined as shorthand for that exact
            // pair) -- confirmed by reading back the actual value rather than assumed, the same
            // way JsRootClipPathInsetIs's own normalization caution (this file's "What the browser
            // run caught" note) was found.
            check(JsSpriteBackgroundRepeatIs(1, "repeat-x") == 1,
                  "HTMLDOM-97d: mixed U=Wrap/V=Clamp maps to background-repeat's independent "
                  "per-axis values (U=repeat, V=no-repeat), read back via the CSSOM's own "
                  "'repeat-x' shorthand for that exact pair, not a single value derived from one "
                  "axis's mode");
        }

        // plans/plan_html_dom.md HTMLDOM-98: GraphicsDevice.Viewport (SetViewport), confirmed non-gap
        // against every other 2D-only sibling but a real one against EASYGL. Verified structurally,
        // the same way scissor/regions are (JsRootWidth/Height/Left/Top rather than pixels): the
        // DOM backbuffer itself cannot be read back at all (design decision 11), so there is no
        // pixel-exact route available here the way htmldom_pixel_verification_test.cpp uses for the
        // render-target-bound path.
        //
        // Surface is 128x128 at this point (frame 8's resize), 1:1 mode (frame 8's own
        // SetVirtualResolution(0,0)), so the logical->physical scale is exactly 1 -- the expected
        // "+4" offset below is a real, not-incidentally-1 multiplier check, not a coincidence of
        // skipping the scale math.
        // plans/plan_html_dom.md HTMLDOM-103: true per-flush paint order across regions (a region's own
        // DOM position previously reflected only its FIRST-creation order, not the actual sequence
        // its flushes happened in this frame) and non-destructive eviction (a region still active
        // this frame must never be LRU-evicted out from under the frame being built).
        if (frame_ == 10)
        {
            // A/B/A: region A flushed, then region B, then region A again. True per-flush ordering
            // means A's SECOND paint order must be HIGHER than B's -- DOM-position-only ordering
            // could never show this, since A's own container was already placed in the DOM the
            // first time it was created and never moved after that.
            dev.setScissorRectangleProperty(Rectangle(1, 1, 5, 5));
            spriteBatch_->Begin(SpriteSortMode::Deferred, BlendState::AlphaBlend, nullptr, nullptr,
                                &scissorEnabledState_);
            spriteBatch_->Draw(*texture_, Vector2(1, 1), Color::White);
            spriteBatch_->End();
            const int zA1 = JsRegionZIndex(1, 1, 5, 5);

            dev.setScissorRectangleProperty(Rectangle(10, 1, 5, 5));
            spriteBatch_->Begin(SpriteSortMode::Deferred, BlendState::AlphaBlend, nullptr, nullptr,
                                &scissorEnabledState_);
            spriteBatch_->Draw(*texture_, Vector2(10, 1), Color::White);
            spriteBatch_->End();
            const int zB = JsRegionZIndex(10, 1, 5, 5);

            dev.setScissorRectangleProperty(Rectangle(1, 1, 5, 5));
            spriteBatch_->Begin(SpriteSortMode::Deferred, BlendState::AlphaBlend, nullptr, nullptr,
                                &scissorEnabledState_);
            spriteBatch_->Draw(*texture_, Vector2(1, 1), Color::White);
            spriteBatch_->End();
            const int zA2 = JsRegionZIndex(1, 1, 5, 5);

            check(zA1 > 0 && zB > zA1 && zA2 > zB,
                  "HTMLDOM-103: true per-flush paint order across an A/B/A sequence of scissor "
                  "rects -- region A's SECOND flush (after B's) gets a HIGHER paint order than B, "
                  "reflecting the actual draw sequence rather than each region's own "
                  "first-creation order (which would have left A permanently below B)");

            // The DEFAULT ('full') region interleaved between two named-region flushes: 'full's
            // sprites are direct children of #cna-dom-root, siblings of every named region's own
            // container, so this is the one case DOM position could never reproduce at all. Plain
            // Begin() (scissor testing disabled) is fine here on purpose: (0,0,128,128) covers the
            // whole backbuffer either way, so it resolves to 'full' regardless of the enable bit.
            dev.setScissorRectangleProperty(Rectangle(0, 0, 128, 128));
            spriteBatch_->Begin();
            spriteBatch_->Draw(*texture_, Vector2(20, 1), Color::White);
            spriteBatch_->End();
            const int zFull = JsFullRegionSpriteZIndexAt(0);
            check(zFull > zA2,
                  "HTMLDOM-103: a 'full'-region flush interleaved after region A's second flush "
                  "gets a HIGHER paint order than A -- not merely whatever DOM position 'full' "
                  "sprites already occupied from earlier frames");

            // Non-destructive eviction: touch 15 MORE distinct regions in this SAME frame (17 total
            // with A and B above -- one past the 16-region cap). All 17 are active THIS frame, so
            // none may be evicted; the eviction loop must allow a temporary over-cap instead of
            // silently dropping A or B out from under the frame still being built.
            for (int i = 0; i < 15; ++i)
            {
                dev.setScissorRectangleProperty(Rectangle(30 + i, 1, 2, 2));
                spriteBatch_->Begin(SpriteSortMode::Deferred, BlendState::AlphaBlend, nullptr, nullptr,
                                    &scissorEnabledState_);
                spriteBatch_->Draw(*texture_, Vector2(30 + i, 1), Color::White);
                spriteBatch_->End();
            }
            check(JsRegionExists(1, 1, 5, 5) == 1,
                  "HTMLDOM-103: region A survives after 17 distinct regions were touched within "
                  "this SAME frame (one past the 16-region cap) -- a region still active this "
                  "frame is never evicted, even as the oldest LRU candidate");
            check(JsRegionExists(10, 1, 5, 5) == 1,
                  "HTMLDOM-103: region B likewise survives -- eviction skipped every "
                  "touched-this-frame candidate rather than removing the oldest one "
                  "unconditionally");

            dev.setScissorRectangleProperty(Rectangle(0, 0, 128, 128));
        }

        // plans/plan_html_dom.md HTMLDOM-108: every CnaPresentationMode, implemented for real via
        // HtmlDomRenderer::ComputeLogicalViewport (ported from SdlGpuRenderer's own
        // reference implementation) -- an earlier version only had correct geometry for
        // FixedHeightDynamicWidth; Letterbox/Overscan/Stretch/NativeBackBuffer all fell through to
        // the SAME height-derived uniform scale, silently wrong for every one of them whenever the
        // virtual and physical aspect ratios did not already match by coincidence.
        //
        // Surface is 128x128 physical at this point (frame 8's resize persists). Every scenario
        // below uses virtual resolution 256x128 (a deliberately 2:1 mismatched aspect against the
        // square 128x128 physical surface) so Letterbox/Overscan/Stretch each produce a REAL,
        // different, hand-derivable geometry -- not the "happens to already fit" case that let
        // FixedHeightDynamicWidth alone pass before this task.
        if (frame_ == 11)
        {
            using CNA::Internal::Renderers::CnaPresentationMode;

            // Letterbox: scale = min(physW/virtW, physH/virtH) = min(128/256, 128/128) = min(0.5,1.0)
            // = 0.5 (uniform, both axes). Scaled content is 128x64, centred vertically in the
            // 128-tall physical surface -> bars top and bottom, offsetX=0, offsetY=(128-64)/2=32.
            renderer.SetVirtualResolution(256, 128);
            renderer.SetPresentationMode(static_cast<int>(CnaPresentationMode::Letterbox));
            renderer.Present();
            check(JsViewportScaleX() == 0.5 && JsViewportScaleY() == 0.5,
                  "HTMLDOM-108a: Letterbox scales both axes by the SAME factor -- the smaller of the "
                  "two per-axis ratios, so the whole logical rect fits inside the physical one");
            check(JsRootLeft() == 0.0 && JsRootTop() == 32.0,
                  "HTMLDOM-108a: Letterbox centres the scaled content in the physical surface -- "
                  "here that means a real, non-zero 32px bar top and bottom, none left or right");

            // Overscan: scale = max(0.5, 1.0) = 1.0. Scaled content is 256x128, wider than the
            // 128-wide physical surface -> cropped left/right, offsetX=(128-256)/2=-64, offsetY=0.
            renderer.SetPresentationMode(static_cast<int>(CnaPresentationMode::Overscan));
            renderer.Present();
            check(JsViewportScaleX() == 1.0 && JsViewportScaleY() == 1.0,
                  "HTMLDOM-108b: Overscan scales both axes by the LARGER of the two per-axis ratios "
                  "-- here that is exactly 1.0, unlike Letterbox's 0.5 above for the identical "
                  "virtual/physical pair, a real measurable divergence between the two modes");
            check(JsRootLeft() == -64.0 && JsRootTop() == 0.0,
                  "HTMLDOM-108b: Overscan centres the scaled (now OVERSIZED) content, cropping "
                  "128px total off the left+right edges (-64 offset) rather than adding bars");
            check(JsViewportWrapperWidth() == 128 && JsViewportWrapperHeight() == 128,
                  "HTMLDOM-108b: the physical-canvas-sized wrapper around #cna-dom-root stays "
                  "exactly 128x128 regardless of root's own oversized/offset box -- this is what "
                  "actually crops Overscan's overflow at the canvas's own edges");

            // Stretch: independent per-axis scale, NO aspect-ratio preservation -- scaleX =
            // physW/virtW = 128/256 = 0.5, scaleY = physH/virtH = 128/128 = 1.0. Deliberately
            // DIFFERENT from each other, unlike every other mode's uniform scale.
            renderer.SetPresentationMode(static_cast<int>(CnaPresentationMode::Stretch));
            renderer.Present();
            check(JsViewportScaleX() == 0.5 && JsViewportScaleY() == 1.0,
                  "HTMLDOM-108c: Stretch scales each axis independently to exactly fill the "
                  "physical surface -- the only mode whose two scale factors legitimately differ");
            check(JsRootLeft() == 0.0 && JsRootTop() == 0.0,
                  "HTMLDOM-108c: Stretch always fills the full physical rect, so there is never a "
                  "centring offset to apply, on either axis");

            // NativeBackBuffer: ignores virtualWidth_/virtualHeight_ entirely (still 256x128 from
            // the scenarios above) -- no scaling, GetViewportSize reports the PHYSICAL 128x128, not
            // the configured virtual resolution, matching SdlGpuRenderer::
            // ComputeLogicalViewport's own treatment of this mode.
            renderer.SetPresentationMode(static_cast<int>(CnaPresentationMode::NativeBackBuffer));
            renderer.Present();
            check(JsViewportScaleX() == 1.0 && JsViewportScaleY() == 1.0 &&
                  JsRootLeft() == 0.0 && JsRootTop() == 0.0,
                  "HTMLDOM-108d: NativeBackBuffer never scales or offsets -- the logical surface IS "
                  "the physical one, regardless of any virtual resolution configured");
            {
                int vw = 0, vh = 0;
                renderer.GetViewportSize(vw, vh);
                check(vw == 128 && vh == 128,
                      "HTMLDOM-108d: NativeBackBuffer's own logical size is the PHYSICAL 128x128, "
                      "not the 256x128 virtual resolution still configured from the earlier "
                      "scenarios -- this mode opts out of the virtual-resolution concept entirely");
            }

            // FixedHeightDynamicWidth regression check: height fixed, width derived from the
            // physical surface's own aspect ratio (here 1:1, so 128), which by construction always
            // exactly fills the physical surface -- scale 1,1, no offset. Confirms switching back to
            // this mode after the other four re-derives geometry correctly, not stale state.
            renderer.SetVirtualResolution(1, 128);
            renderer.SetPresentationMode(static_cast<int>(CnaPresentationMode::FixedHeightDynamicWidth));
            renderer.Present();
            check(JsViewportScaleX() == 1.0 && JsViewportScaleY() == 1.0 &&
                  JsRootLeft() == 0.0 && JsRootTop() == 0.0,
                  "HTMLDOM-108e: FixedHeightDynamicWidth still fills the physical surface exactly "
                  "after switching through every other mode first, not left showing stale geometry");

            // plans/plan_html_dom.md HTMLDOM-108: SetPresentationMode must validate its ordinal -- an
            // earlier version accepted anything and stored it unchecked. A rejected call must not
            // have changed the mode in effect, the same "rejected state leaves the prior one alone"
            // contract ApplyBlendStateAcceptsPresetsAndRejectsCustomOnes already established for
            // BlendState (HtmlDomRendererTests.cpp).
            bool threw = false;
            try { renderer.SetPresentationMode(99); }
            catch (const std::out_of_range&) { threw = true; }
            check(threw, "HTMLDOM-108f: SetPresentationMode throws std::out_of_range for an "
                         "invalid CnaPresentationMode ordinal");
            renderer.Present();
            check(JsViewportScaleX() == 1.0 && JsViewportScaleY() == 1.0 &&
                  JsRootLeft() == 0.0 && JsRootTop() == 0.0,
                  "HTMLDOM-108f: the rejected SetPresentationMode(99) call left the previously-set "
                  "FixedHeightDynamicWidth mode's own geometry completely unchanged");

            // plans/plan_html_dom.md HTMLDOM-108: TransformWindowToLogical must return the correct
            // outside-letterbox result -- re-establish the exact Letterbox scenario from above
            // (bars top/bottom, content in physical y in [32,96)). A window point in the TOP BAR
            // (y=10 < 32) has no logical counterpart at all; a point inside the content band
            // (y=64, dead centre of [32,96)) does, at the hand-derived logical coordinates.
            renderer.SetVirtualResolution(256, 128);
            renderer.SetPresentationMode(static_cast<int>(CnaPresentationMode::Letterbox));
            renderer.Present();
            float logX = -1.0f, logY = -1.0f;
            const bool inBar = renderer.TransformWindowToLogical(64.0f, 10.0f, logX, logY);
            check(!inBar, "HTMLDOM-108g: a window coordinate inside a Letterbox bar (above the "
                          "scaled content, not inside it) reports no logical mapping at all");
            const bool inContent = renderer.TransformWindowToLogical(64.0f, 64.0f, logX, logY);
            check(inContent && logX == 128.0f && logY == 64.0f,
                  "HTMLDOM-108g: a window coordinate inside the scaled content band DOES map, at "
                  "the hand-derived logical position ((64,64) physical -> (128,64) logical for "
                  "this exact 256x128-virtual/128x128-physical Letterbox scenario)");

            // plans/plan_html_dom.md HTMLDOM-108: TransformLogicalToWindow/TransformWindowToLogical round
            // trip under Stretch (independent per-axis scale, the mode most likely to expose a
            // swapped X/Y factor). Logical (100,50) -> window (100*0.5, 50*1.0) = (50,50) -> back to
            // logical must reproduce (100,50) exactly (every factor here divides evenly, so this is
            // an exact-equality check, not a tolerance one).
            renderer.SetPresentationMode(static_cast<int>(CnaPresentationMode::Stretch));
            renderer.Present();
            float winX = -1.0f, winY = -1.0f;
            const bool toWindowOk = renderer.TransformLogicalToWindow(100.0f, 50.0f, winX, winY);
            const bool roundTripOk = toWindowOk &&
                renderer.TransformWindowToLogical(winX, winY, logX, logY);
            check(roundTripOk && winX == 50.0f && winY == 50.0f && logX == 100.0f && logY == 50.0f,
                  "HTMLDOM-108h: TransformLogicalToWindow/TransformWindowToLogical round-trip "
                  "exactly under Stretch's independent per-axis scale -- (100,50) logical -> "
                  "(50,50) window -> (100,50) logical again");

            // Restores the exact "128x128 physical, 1:1 (unset virtual resolution) mode, scale
            // exactly 1" state frame 12 (below) and its own comments depend on.
            renderer.SetVirtualResolution(0, 0);
            renderer.Present();
        }

        if (frame_ == 12)
        {
            // plans/plan_html_dom.md HTMLDOM-107: Viewport no longer resizes/repositions #cna-dom-root at
            // all (that was HTMLDOM-98's approach, and the actual defect this task fixes: a SECOND
            // viewport set later in the same frame retroactively moved/clipped every sprite an
            // EARLIER batch already flushed, since they all shared root's one single position). Root
            // now stays fixed at the full backbuffer; the viewport's own (X,Y) offset is applied
            // directly to each BATCH's own sprites instead.
            const int baseW = JsRootWidth(), baseH = JsRootHeight();
            const double baseLeft = JsRootLeft(), baseTop = JsRootTop();

            dev.setViewportProperty(Viewport(4, 4, 16, 16));
            check(JsRootWidth() == baseW && JsRootHeight() == baseH,
                  "HTMLDOM-107a: a sub-rectangle Viewport does NOT resize #cna-dom-root -- it stays "
                  "at the full backbuffer's own size regardless of which viewport is active");
            check(JsRootLeft() == baseLeft && JsRootTop() == baseTop,
                  "HTMLDOM-107b: #cna-dom-root is NOT repositioned by the viewport's X/Y offset "
                  "either -- the offset is realized per-sprite instead (HTMLDOM-107c)");

            spriteBatch_->Begin();
            spriteBatch_->Draw(*texture_, Vector2(0, 0), Color::White);
            spriteBatch_->End();
            check(JsFullRegionSpriteTransformContains(0, "translate(4px,4px)") == 1,
                  "HTMLDOM-107c: the viewport's own (X,Y) offset is applied directly to the "
                  "sprite's own CSS transform as the OUTERMOST translate, matching real XNA/FNA "
                  "where Viewport.X/Y is applied by the rasterizer stage to every sprite's final "
                  "position -- not coming from #cna-dom-root's own repositioning any more");
            check(JsFullRegionSpriteTransformContains(0, "translate(0px,0px)") == 1,
                  "HTMLDOM-107d: the sprite's own destX/destY translate is still present, "
                  "unchanged, after the viewport's translate -- the offset is ADDED, not "
                  "substituted for the sprite's own position");

            // The actual regression this task exists to fix: a SECOND viewport set later in the
            // SAME frame must never retroactively move/clip sprites an EARLIER batch already
            // flushed under a DIFFERENT viewport -- the real "split-screen/sub-panel" claim.
            dev.setViewportProperty(Viewport(4, 4, 16, 16));
            spriteBatch_->Begin();
            spriteBatch_->Draw(*texture_, Vector2(0, 0), Color::White);
            spriteBatch_->End();

            dev.setViewportProperty(Viewport(50, 50, 16, 16));
            spriteBatch_->Begin();
            spriteBatch_->Draw(*texture_, Vector2(0, 0), Color::White);
            spriteBatch_->End();

            check(JsFullRegionSpriteTransformContains(1, "translate(4px,4px)") == 1,
                  "HTMLDOM-107e: an EARLIER batch's sprite keeps ITS OWN viewport's offset (4,4) "
                  "even after a LATER batch sets a DIFFERENT viewport (50,50) -- no retroactive "
                  "re-offsetting of already-flushed sprites");
            check(JsFullRegionSpriteTransformContains(2, "translate(50px,50px)") == 1,
                  "HTMLDOM-107f: the LATER batch's own sprite correctly carries ITS OWN "
                  "viewport's offset");

            // Restore the full-backbuffer viewport for hygiene before the final publish below.
            dev.setViewportProperty(Viewport(0, 0, 128, 128));

            // plans/plan_html_dom.md HTMLDOM-118: SpriteSortMode::Immediate must genuinely apply
            // per-draw -- a scissor rect CHANGE between two Draw() calls inside ONE Begin(
            // Immediate)/End() block must send each sprite to whichever region was actually
            // active AT THE TIME that sprite's own Draw() was called, not both ending up wherever
            // the rect happened to be by End(). Four tiny, non-overlapping regions, all corners
            // far from the scissor-straddle sprite this frame ends on (60,60,20,20) and the exact
            // pixels the harness samples there ((70,70)/(90,90)), so none of them interfere with
            // the final screenshot check below.
            dev.setScissorRectangleProperty(Rectangle(0, 0, 12, 12));
            spriteBatch_->Begin(SpriteSortMode::Immediate, BlendState::AlphaBlend, nullptr, nullptr,
                                &scissorEnabledState_);
            spriteBatch_->Draw(*texture_, Vector2(0, 0), Color::White);
            dev.setScissorRectangleProperty(Rectangle(100, 0, 12, 12));
            spriteBatch_->Draw(*texture_, Vector2(0, 0), Color::White);
            spriteBatch_->End();
            check(JsRegionVisibleSpriteCount(0, 0, 12, 12) == 1,
                  "HTMLDOM-118a: SpriteSortMode::Immediate -- the FIRST draw lands in the region "
                  "that was active AT THE TIME IT WAS CALLED (before the scissor rect changed "
                  "again), not wherever the rect ended up by End()");
            check(JsRegionVisibleSpriteCount(100, 0, 12, 12) == 1,
                  "HTMLDOM-118b: ...and the SECOND draw, issued after the rect changed, "
                  "independently lands in its own DIFFERENT region -- genuine per-draw state "
                  "capture, not a single end-of-batch snapshot applied to every sprite in it");

            // Control: the identical interleaved sequence under Deferred must NOT show this
            // per-draw behaviour -- proving the Immediate result above is real, not an artifact of
            // how JsRegionVisibleSpriteCount counts or of the regions already existing.
            dev.setScissorRectangleProperty(Rectangle(0, 12, 12, 12));
            spriteBatch_->Begin(SpriteSortMode::Deferred, BlendState::AlphaBlend, nullptr, nullptr,
                                &scissorEnabledState_);
            spriteBatch_->Draw(*texture_, Vector2(0, 0), Color::White);
            dev.setScissorRectangleProperty(Rectangle(100, 12, 12, 12));
            spriteBatch_->Draw(*texture_, Vector2(0, 0), Color::White);
            spriteBatch_->End();
            check(JsRegionVisibleSpriteCount(0, 12, 12, 12) == -1,
                  "HTMLDOM-118c: control -- under Deferred, the FIRST draw does NOT land in the "
                  "region that was active when it was CALLED (that per-draw behaviour is "
                  "Immediate-only); a region for that rect isn't even created (-1: does not "
                  "exist) since it was never current at End()");
            check(JsRegionVisibleSpriteCount(100, 12, 12, 12) == 2,
                  "HTMLDOM-118d: ...instead BOTH draws land in whichever region was active AT "
                  "End() -- Deferred's real, unaffected, single end-of-batch-snapshot contract");

            dev.setScissorRectangleProperty(Rectangle(0, 0, 128, 128));   // hygiene.

            // plans/plan_html_dom.md HTMLDOM-101: prove the scissor region has a real, non-zero layout
            // box, AND that clip-path genuinely removes out-of-bounds content from the page's real
            // composited pixels -- not merely that a clip-path string with the right numbers exists
            // on a container that might still be 0x0 (the actual HTMLDOM-101 defect: the earlier
            // HTMLDOM-80/93/94 checks above only read clip-path's inline style string and counted
            // non-'none' elements, both of which stayed green even with a 0x0 box). Left as the very
            // LAST draw of the whole test, deliberately: Clear() (top of every Draw()) hides every
            // region's sprites every frame, so anything drawn in an earlier frame would already be
            // gone by the time scripts/htmldom-browser-test.mjs inspects the finished page -- this
            // survives because nothing runs after it.
            //
            // Surface is 128x128 at this point, 1:1 mode, scale exactly 1 (frame 8/10's own
            // comments already establish this). Source rectangle (0,0,1,1) samples a single solid
            // texel (pure red, LoadContent's own texture layout) so the sprite's whole footprint is
            // one unambiguous colour -- no tint maths, no multi-texel quadrants to reason about.
            dev.setScissorRectangleProperty(Rectangle(60, 60, 20, 20));
            spriteBatch_->Begin(SpriteSortMode::Deferred, BlendState::AlphaBlend, nullptr, nullptr,
                                &scissorEnabledState_);
            // Straddles the rect's own right/bottom edge: the rect spans logical x/y in [60,80),
            // this sprite spans [55,95) on both axes -- part of its footprint must actually paint,
            // part must not.
            spriteBatch_->Draw(*texture_, Rectangle(55, 55, 40, 40), Rectangle(0, 0, 1, 1), Color::White);
            spriteBatch_->End();
            check(JsRegionOffsetWidth(60, 60, 20, 20) > 0 && JsRegionOffsetHeight(60, 60, 20, 20) > 0,
                  "HTMLDOM-101: the region's own container has a real, non-zero computed layout box "
                  "(offsetWidth/offsetHeight), not the 0x0 an unsized container would have");
            dev.setScissorRectangleProperty(Rectangle(0, 0, 128, 128));

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
        scissorEnabledState_.setScissorTestEnableProperty(true);
    }

    int getResult() const { return result_; }
};

int main()
{
    HtmlDomSmokeTest game;
    game.Run();
    return game.getResult();
}
