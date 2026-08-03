// SPDX-License-Identifier: MS-PL
#include "CNA/Internal/Backends/HtmlDom/HtmlDomGraphicsBackend.hpp"
#include "CNA/Internal/Backends/HtmlDom/HtmlDomTextureBackend.hpp"
#include "CNA/Internal/Backends/HtmlDom/HtmlDomRenderTargetBackend.hpp"
#include "CNA/Internal/Backends/HtmlDom/HtmlDomSpriteBatchBackend.hpp"
#include "CNA/Internal/Backends/Common/NotYetImplemented.hpp"

#include <SDL3/SDL.h>

#include <stdexcept>
#include <string>

#if defined(__EMSCRIPTEN__)
#include <emscripten.h>

// plan_html_dom.md HTMLDOM-10 / design decision 2: creates the <div> every sprite element lives in,
// positioned over the <canvas> SDL3's Emscripten video driver owns.
//
// The canvas itself is hidden but deliberately NOT removed or display:none'd -- SDL keeps sizing it
// and delivering input through it, so it has to stay in the layout. `contain:strict` keeps the
// sprite subtree's layout and painting isolated from the rest of the page, and clips sprites that
// leave the viewport.
EM_JS(void, CNA_HtmlDom_EnsureRoot, (), {
    if (Module['cnaDomRoot']) return;
    // Guarded: the repo's own GTest runner is plain `node`, where there is no document at all, so
    // an unguarded reference would throw a ReferenceError into wasm instead of degrading to
    // "no surface, nothing renders".
    if (typeof document === 'undefined') return;
    const canvas = Module['canvas'] || document.querySelector('canvas');
    if (!canvas) { console.error('[CNA] HTML_DOM: no <canvas> element to anchor the DOM surface to'); return; }
    const root = document.createElement('div');
    root.id = 'cna-dom-root';
    root.style.cssText = 'position:absolute;left:0;top:0;overflow:hidden;transform-origin:0 0;' +
                         'contain:strict;background-color:#000;';
    (canvas.parentNode || document.body).insertBefore(root, canvas.nextSibling);
    canvas.style.visibility = 'hidden';
    Module['cnaDomRoot'] = root;
    Module['cnaDomBoundCtx'] = null;
    Module['cnaDomClearColor'] = null;
    Module['cnaDomScissorRect'] = null;
    Module['cnaDomViewport'] = null;
    Module['cnaDomMaxRegions'] = 16;
    // plan_html_dom.md HTMLDOM-94: sprites are grouped into per-scissor-rect DOM containers
    // ("regions") instead of one flat pool directly under #cna-dom-root, so a scissor rect active
    // for one SpriteBatch Begin/End batch cannot retroactively clip sprites a DIFFERENT batch drew
    // earlier in the same frame under a different rect (or vice versa) -- see
    // HtmlDomSpriteBatchBackend.cpp's CNA_HtmlDom_FlushSprites, the only place a region is chosen,
    // for the batch-granularity rationale. 'full' is the default, zero-clip region every sprite
    // uses until a game actually narrows the scissor rect below the current surface size; its
    // container IS #cna-dom-root itself and its pool IS the same flat pool this backend always
    // had, so the overwhelmingly common no-scissor case costs nothing beyond what it always did.
    Module['cnaDomRegions'] = { 'full': { container: root, pool: [], used: 0, highWater: 0, rect: null } };
    Module['cnaDomRegionOrder'] = [];
    // plan_html_dom.md HTMLDOM-103: which non-default regions a batch has actually flushed into
    // THIS FRAME -- reset in CNA_HtmlDom_Clear (frame start), populated by cnaDomTouchRegion. A
    // region in this set must never be evicted, even if it is the least-recently-used candidate:
    // evicting it would remove a container whose sprites are still part of the frame being built,
    // making already-submitted draws silently disappear.
    Module['cnaDomRegionsTouchedThisFrame'] = new Set();
    // plan_html_dom.md HTMLDOM-103: a monotonically increasing per-flush counter, used as each
    // region's/full-region-sprite's CSS z-index so paint order reflects the ACTUAL sequence of
    // SpriteBatch flushes this frame, not merely each region's own first-creation order (DOM
    // position, which HTMLDOM-94 originally relied on, only ever captured the latter).
    Module['cnaDomPaintOrderCounter'] = 0;

    // Sets a region's own clip-path from its stored rect against the CURRENT backbuffer size.
    // Called once when a region is created and again for every region whenever the surface's
    // logical size changes (CNA_HtmlDom_UpdateSurface, below), so a resize re-derives every active
    // clip instead of leaving stale insets applied to boxes that have since changed size
    // (HTMLDOM-93).
    //
    // plan_html_dom.md HTMLDOM-107: rect.x/rect.y are in ABSOLUTE render-target pixel space -- the
    // SAME space #cna-dom-root's own local coordinate space now always occupies, since root is kept
    // at the full backbuffer at all times (see CNA_HtmlDom_UpdateSurface below) and is never itself
    // moved/resized to track a sub-viewport any more. Scissor and viewport are independent absolute-
    // space concepts on a real GPU (confirmed by reading EasyGLGraphicsBackend: its scissor forwards
    // straight to glScissor, unaffected by whatever glViewport region is active), so no viewport
    // offset needs to be (or should be) subtracted here at all -- HTMLDOM-98 introduced that
    // subtraction only because IT moved root to track viewport, which HTMLDOM-107 undoes.
    Module['cnaDomApplyRegionClip'] = function (region) {
        if (!region.rect) { region.container.style.clipPath = ""; return; }
        const logicalW = Module['cnaDomLogicalW'] || 0;
        const logicalH = Module['cnaDomLogicalH'] || 0;
        const top = Math.max(0, region.rect.y);
        const left = Math.max(0, region.rect.x);
        const right = Math.max(0, logicalW - (region.rect.x + region.rect.w));
        const bottom = Math.max(0, logicalH - (region.rect.y + region.rect.h));
        region.container.style.clipPath =
            'inset(' + top + 'px ' + right + 'px ' + bottom + 'px ' + left + 'px)';
    };

    // plan_html_dom.md HTMLDOM-107: positions and sizes #cna-dom-root to the FULL backbuffer,
    // always -- HTMLDOM-98 previously shrank/moved root to track a sub-rectangle Viewport directly,
    // which meant a SECOND viewport set later in the SAME frame retroactively moved/clipped every
    // sprite already drawn under an EARLIER viewport (the split-screen/sub-panel use case the
    // capability matrix claimed was never actually implemented), and Clear() -- which real XNA
    // clears the WHOLE render target with, independent of the current Viewport, exactly like a real
    // glClear/ClearRenderTargetView call ignores glViewport/RSSetViewports -- left the backbuffer
    // area OUTSIDE a shrunk root showing the host page's own background instead of the clear colour.
    // The viewport's own offset is applied per BATCH instead, in CNA_HtmlDom_FlushSprites (both the
    // DOM and Canvas2D paths) -- see the comment there for why sprite position, not root's own box
    // or a region's clip, is the correct place for it.
    Module['cnaDomApplySurfaceGeometry'] = function () {
        const root = Module['cnaDomRoot'];
        if (!root) return;
        const w = Module['cnaDomBackbufferW'] || 0;
        const h = Module['cnaDomBackbufferH'] || 0;
        root.style.width = w + 'px';
        root.style.height = h + 'px';
        Module['cnaDomLogicalW'] = w;
        Module['cnaDomLogicalH'] = h;
        const canvas = Module['canvas'] ||
                       (typeof document === 'undefined' ? null : document.querySelector('canvas'));
        root.style.left = (canvas ? canvas.offsetLeft : 0) + 'px';
        root.style.top = (canvas ? canvas.offsetTop : 0) + 'px';
        // The backbuffer's own geometry just changed -- re-derive every region's clip-path against
        // it, the same reasoning HTMLDOM-93's own resize handling already used.
        const regions = Module['cnaDomRegions'];
        if (regions) for (const key in regions) Module['cnaDomApplyRegionClip'](regions[key]);
    };

    // Moves `key` to the most-recently-used end of the eviction order (appending it if it is not
    // there yet -- a brand new region's own first touch), and marks it active for the current
    // frame so the eviction loop below can never remove it out from under an in-progress frame
    // (HTMLDOM-103). The default 'full' region is never tracked here -- it is never evicted.
    Module['cnaDomTouchRegion'] = function (key) {
        if (key === 'full') return;
        Module['cnaDomRegionsTouchedThisFrame'].add(key);
        const order = Module['cnaDomRegionOrder'];
        const idx = order.indexOf(key);
        if (idx >= 0) order.splice(idx, 1);
        order.push(key);
    };

    // Returns the region that should own sprites drawn under the given scissor rect (an {x,y,w,h}
    // object in absolute backbuffer pixels, or null/undefined for "no scissor rect has been set").
    // plan_html_dom.md HTMLDOM-107: scissor and viewport are independent absolute-space concepts
    // (see cnaDomApplyRegionClip's own comment) -- whether a rect is "full" depends only on the
    // BACKBUFFER's own size, never on whatever viewport happens to be active, so this checks
    // Module['cnaDomLogicalW'/'H'] alone. A rect that currently covers the whole backbuffer
    // collapses to the SAME default 'full' region as no rect at all, so resetting
    // ScissorRectangle to the full backbuffer (what GraphicsDevice.UpdateViewportFromWindow does on
    // every resize, and what most games do explicitly) always lands back on the zero-cost path
    // rather than accumulating a pointless no-op-clip container.
    Module['cnaDomGetRegion'] = function (rect) {
        const regions = Module['cnaDomRegions'];
        const logicalW = Module['cnaDomLogicalW'] || 0;
        const logicalH = Module['cnaDomLogicalH'] || 0;
        const isFull = !rect || (rect.x <= 0 && rect.y <= 0 &&
                                  rect.x + rect.w >= logicalW && rect.y + rect.h >= logicalH);
        if (isFull) return regions['full'];

        const key = rect.x + ',' + rect.y + ',' + rect.w + ',' + rect.h;
        let region = regions[key];
        if (region) { Module['cnaDomTouchRegion'](key); return region; }

        // LRU-evict the least-recently-used non-default region at capacity -- bounds a
        // pathological game that cycles through many distinct scissor rects, mirroring the texture
        // variant cache's own cap. plan_html_dom.md HTMLDOM-103: skips any region already touched
        // THIS FRAME (cnaDomRegionsTouchedThisFrame) -- the old unconditional order.shift() could
        // remove the oldest region even while its sprites were still part of the frame being built
        // right now, making an already-submitted draw silently disappear. Scans oldest-first (the
        // order array's own invariant) for the first evictable candidate rather than just the head.
        const order = Module['cnaDomRegionOrder'];
        const touchedThisFrame = Module['cnaDomRegionsTouchedThisFrame'];
        while (order.length >= Module['cnaDomMaxRegions']) {
            let evictIdx = -1;
            for (let i = 0; i < order.length; ++i) {
                if (!touchedThisFrame.has(order[i])) { evictIdx = i; break; }
            }
            if (evictIdx < 0) {
                // Every existing region is active this frame -- nothing is safe to remove. Allow a
                // temporary over-cap rather than dropping a region a draw call this same frame still
                // depends on; the cap is restored on a later frame once some of these go cold again.
                console.warn('[CNA] HTML_DOM: scissor-region cap (' + Module['cnaDomMaxRegions'] +
                             ') exceeded within a single frame -- every existing region is still ' +
                             'active this frame, so none were evicted.');
                break;
            }
            const evictKey = order[evictIdx];
            order.splice(evictIdx, 1);
            const evictRegion = regions[evictKey];
            if (evictRegion && evictRegion.container.parentNode) {
                evictRegion.container.parentNode.removeChild(evictRegion.container);
            }
            delete regions[evictKey];
        }

        // plan_html_dom.md HTMLDOM-101: width/height:100% (of #cna-dom-root, which HTMLDOM-107 keeps
        // sized to the full backbuffer at all times) gives this container a REAL, non-zero layout
        // box. Without an explicit size an absolutely-positioned element's own absolutely-positioned
        // children (the sprite <div>s -- also position:absolute) contribute no intrinsic size at
        // all, so the box stayed exactly 0x0 -- clip-path had nothing to clip, and sprites inside it
        // were genuinely unpainted/un-hit-testable despite the clip-path's own inset() values being
        // computed correctly (confirmed with a focused headless-Chromium probe: offsetWidth/
        // offsetHeight measured 0 before this fix). 100% tracks a resize automatically, with no
        // extra JS bookkeeping needed on top of what cnaDomApplySurfaceGeometry already does to root
        // itself.
        const container = document.createElement('div');
        container.style.cssText = 'position:absolute;left:0;top:0;width:100%;height:100%;';
        Module['cnaDomRoot'].appendChild(container);
        region = { container: container, pool: [], used: 0, highWater: 0, rect: rect };
        regions[key] = region;
        Module['cnaDomTouchRegion'](key);
        Module['cnaDomApplyRegionClip'](region);
        return region;
    };
});

EM_JS(void, CNA_HtmlDom_DestroyRoot, (), {
    const root = Module['cnaDomRoot'];
    if (root && root.parentNode) root.parentNode.removeChild(root);
    const canvas = Module['canvas'] ||
                   (typeof document === 'undefined' ? null : document.querySelector('canvas'));
    if (canvas) canvas.style.visibility = "";
    Module['cnaDomRoot'] = null;
    Module['cnaDomRegions'] = null;
    Module['cnaDomRegionOrder'] = null;
    Module['cnaDomScissorRect'] = null;
    Module['cnaDomViewport'] = null;
    Module['cnaDomRegionsTouchedThisFrame'] = null;
    Module['cnaDomPaintOrderCounter'] = null;
});

// plan_html_dom.md HTMLDOM-11 / design decision 9: XNA's Clear overwrites everything drawn so far,
// which in a retained-mode surface means two things -- repaint the background, and drop every
// sprite queued this frame (hiding any that are still visible from an earlier point in the same
// frame, then rewinding the pool cursor so the next draw starts from element 0 again).
//
// In the ordinary Clear-at-frame-start case the cursor is already 0, so the loop does nothing at
// all and the whole call costs one background-colour comparison.
//
// With a render target bound this clears that canvas instead. `copy` rather than plain fills:
// XNA's Clear is an unconditional overwrite of every pixel, not a blend with what was there, so a
// leftover composite mode from a previous additive draw must not affect it. clearRect is used for a
// fully transparent clear, where it is both exact and cheaper.
EM_JS(void, CNA_HtmlDom_Clear, (double r, double g, double b, double a), {
    const ctx = Module['cnaDomBoundCtx'];
    if (ctx) {
        ctx.save();
        ctx.setTransform(1, 0, 0, 1, 0, 0);
        if (a <= 0) {
            ctx.clearRect(0, 0, ctx.canvas.width, ctx.canvas.height);
        } else {
            ctx.globalCompositeOperation = 'copy';
            ctx.globalAlpha = 1;
            ctx.fillStyle = 'rgba(' + Math.round(r * 255) + ',' + Math.round(g * 255) + ',' +
                            Math.round(b * 255) + ',' + a + ')';
            ctx.fillRect(0, 0, ctx.canvas.width, ctx.canvas.height);
        }
        ctx.restore();
        return;
    }

    const root = Module['cnaDomRoot'];
    if (!root) return;
    const css = 'rgba(' + Math.round(r * 255) + ',' + Math.round(g * 255) + ',' +
                Math.round(b * 255) + ',' + a + ')';
    if (Module['cnaDomClearColor'] !== css) {
        root.style.backgroundColor = css;
        Module['cnaDomClearColor'] = css;
    }
    // plan_html_dom.md HTMLDOM-103: Clear() is this backend's own "frame start" marker (design
    // decision 9's own reasoning -- XNA games call it once per frame, before any Draw()), so it is
    // where "which regions are active THIS frame" resets, ready for cnaDomTouchRegion to repopulate
    // as this frame's batches actually flush.
    Module['cnaDomRegionsTouchedThisFrame'].clear();
    // Every region's pool is cleared, not just the default one -- a sprite drawn under a narrower
    // scissor rect earlier this frame must disappear on Clear() exactly like one drawn unclipped.
    const regions = Module['cnaDomRegions'];
    for (const key in regions) {
        const region = regions[key];
        const pool = region.pool;
        const used = region.used;
        for (let i = 0; i < used; ++i) {
            const el = pool[i];
            if (el && !el.__cnaState.hidden) { el.style.display = 'none'; el.__cnaState.hidden = true; }
        }
        region.used = 0;
    }
});

// plan_html_dom.md HTMLDOM-12: ends the frame by hiding the pool elements this frame did not use --
// and only those, tracked by a high-water mark, so a steady frame count touches nothing. There is
// no buffer to swap: the browser compositor presents the DOM on its next paint tick.
//
// HTMLDOM-94: done per region, the same way Clear() above is -- each region tracks its own
// used/highWater independently, since a region's sprite count varies frame to frame just like the
// default pool's always could.
EM_JS(void, CNA_HtmlDom_PresentFrame, (), {
    const regions = Module['cnaDomRegions'];
    if (!regions) return;
    for (const key in regions) {
        const region = regions[key];
        const pool = region.pool;
        const used = region.used;
        const high = region.highWater;
        for (let i = used; i < high; ++i) {
            const el = pool[i];
            if (el && !el.__cnaState.hidden) { el.style.display = 'none'; el.__cnaState.hidden = true; }
        }
        region.highWater = used;
        region.used = 0;
    }
});

// Records the backbuffer's own logical/physical geometry and applies the logical→physical scale.
// Called only when the geometry actually changed (the backend caches the last values), because
// these are the backend's only layout-affecting style writes -- doing them every frame would defeat
// the whole design.
//
// plan_html_dom.md HTMLDOM-107: sizes/positions #cna-dom-root directly, to the full backbuffer --
// root no longer tracks a sub-rectangle Viewport at all (see cnaDomApplySurfaceGeometry's own
// comment, CNA_HtmlDom_EnsureRoot, for why).
EM_JS(void, CNA_HtmlDom_UpdateSurface, (int logicalW, int logicalH, int physW, int physH), {
    const root = Module['cnaDomRoot'];
    if (!root) return;
    Module['cnaDomBackbufferW'] = logicalW;
    Module['cnaDomBackbufferH'] = logicalH;
    Module['cnaDomScale'] = logicalH > 0 ? physH / logicalH : 1;
    root.style.transform = Module['cnaDomScale'] !== 1 ? 'scale(' + Module['cnaDomScale'] + ')' : "";
    // HTMLDOM-93/94/107: the backbuffer's own geometry just changed (that is the only reason this
    // function runs at all -- Present() only calls it when geometry differs from last frame).
    // cnaDomApplySurfaceGeometry re-sizes/positions root against the new geometry and re-derives
    // every region's clip-path, the same reasoning HTMLDOM-93's own resize handling already used.
    Module['cnaDomApplySurfaceGeometry']();
});

// plan_html_dom.md HTMLDOM-80 / design decision 13: real scissor clipping via `clip-path: inset()`.
// Exact, with no transform-inverse maths needed: region containers carry no rotation (only the
// uniform logical->physical scale() CNA_HtmlDom_UpdateSurface applies to #cna-dom-root, which every
// region container inherits), so clip-path's own pre-transform local coordinate space already
// coincides with the same logical-pixel space every sprite's destX/destY is expressed in -- the
// scissor rect can be turned into inset() distances directly, with no per-sprite transform to
// invert.
//
// HTMLDOM-94: this function ONLY records the current rect now -- it does not touch the DOM at all.
// The rect it records is consumed once per SpriteBatch Begin/End batch, at CNA_HtmlDom_FlushSprites
// (HtmlDomSpriteBatchBackend.cpp), which is what actually resolves it to a region and therefore a
// clip. That is a real behaviour change from the single whole-surface clip-path this function used
// to apply directly to #cna-dom-root: sprites already flushed into a region under an EARLIER rect
// keep that region's own clip-path even after this function is called again with a DIFFERENT rect,
// instead of every sprite currently on screen being retroactively reclipped to whatever rect is
// "current" by the time the frame is inspected. Scoped honestly at BATCH granularity, matching this
// backend's one-flush-per-Begin/End architecture -- not literally per individual Draw() call within
// the same still-open batch (SpriteSortMode::Immediate games that change ScissorRectangle between
// individual Draw() calls inside one Begin/End won't see finer-grained clipping than that).
//
// Applied regardless of RasterizerState.ScissorTestEnable, the same way SdlGraphicsBackend::
// SetScissorRect behaves: SDL_Renderer never overrides ApplyRasterizerState at all, so its own
// SDL_SetRenderClipRect call is never gated on ScissorTestEnable either -- confirmed by reading
// SdlGraphicsBackend.cpp before matching its behaviour here, not assumed.
EM_JS(void, CNA_HtmlDom_SetScissorRect, (int x, int y, int w, int h), {
    if (!Module['cnaDomRoot']) return;
    Module['cnaDomScissorRect'] = { x: x, y: y, w: w, h: h };
});

// plan_html_dom.md HTMLDOM-107: ONLY records the new viewport now -- it does not touch the DOM at
// all, the same shape CNA_HtmlDom_SetScissorRect already uses. The viewport recorded here is read
// once per SpriteBatch Begin/End batch, at CNA_HtmlDom_FlushSprites (HtmlDomSpriteBatchBackend.cpp),
// which applies its (X,Y) offset directly to that batch's own sprites -- so a viewport set later in
// the same frame can never retroactively move/clip sprites an EARLIER batch already flushed under a
// DIFFERENT viewport (the real defect HTMLDOM-98's "resize root to track viewport" approach had:
// see cnaDomApplySurfaceGeometry's own comment for the full before/after).
EM_JS(void, CNA_HtmlDom_SetViewport, (int x, int y, int w, int h), {
    if (!Module['cnaDomRoot']) return;
    Module['cnaDomViewport'] = { x: x, y: y, w: w, h: h };
});

// Reads back the currently bound render target. Returns 0 when nothing is bound -- the DOM
// backbuffer genuinely cannot be read (design decision 11) and the caller turns that into an
// explicit exception rather than a fabricated frame.
EM_JS(int, CNA_HtmlDom_ReadBound, (int x, int y, int w, int h, uint8_t* outPixels), {
    const ctx = Module['cnaDomBoundCtx'];
    if (!ctx) return 0;
    const imageData = ctx.getImageData(x, y, w, h);
    HEAPU8.set(imageData.data, outPixels);
    return 1;
});
#endif

namespace CNA::Internal::Backends::HtmlDom
{
    namespace
    {
        // The owner's instruction for this backend's 3D surface: throw "not yet implemented" rather
        // than silently doing nothing. Callers can check GraphicsDevice::SupportsCapability()
        // ahead of time instead of relying on the throw -- see SupportsCapability() in the header.
        [[noreturn]] void ThrowNo3D(const char* what)
        {
            NotYetImplemented("HTML_DOM", what);
        }
    }

    HtmlDomGraphicsBackend::HtmlDomGraphicsBackend(SDL_Window* window, int virtualWidth, int virtualHeight,
                                                   CnaPresentationMode mode)
        : window_(window)
        , virtualWidth_(virtualWidth)
        , virtualHeight_(virtualHeight)
        , presentationMode_(mode)
    {
        if (!window_) throw std::runtime_error("HtmlDomGraphicsBackend initialized with null window.");
        IGraphicsBackend::RegisterForWindow(window_, this);
        // A backend can be constructed after a previous one was destroyed (GraphicsDevice::Reset),
        // so the shared draw-path state is rewound rather than assumed pristine.
        SetBoundRenderTargetIdEXT(0);
        SetCurrentCompositeOpEXT(DomCompositeOp::NonPremultiplied);
#if defined(__EMSCRIPTEN__)
        CNA_HtmlDom_EnsureRoot();
#endif
    }

    HtmlDomGraphicsBackend::~HtmlDomGraphicsBackend()
    {
        IGraphicsBackend::UnregisterForWindow(window_);
#if defined(__EMSCRIPTEN__)
        CNA_HtmlDom_DestroyRoot();
#endif
    }

    void HtmlDomGraphicsBackend::Clear(float r, float g, float b, float a)
    {
#if defined(__EMSCRIPTEN__)
        CNA_HtmlDom_Clear(r, g, b, a);
#else
        (void)r; (void)g; (void)b; (void)a;
#endif
    }

    void HtmlDomGraphicsBackend::Present()
    {
        int logicalW = 0, logicalH = 0;
        getLogicalSize(logicalW, logicalH);
        int physW = 0, physH = 0;
        SDL_GetWindowSize(window_, &physW, &physH);
        if (logicalW != lastLogicalWidth_ || logicalH != lastLogicalHeight_ ||
            physW != lastPhysicalWidth_ || physH != lastPhysicalHeight_)
        {
            lastLogicalWidth_ = logicalW;
            lastLogicalHeight_ = logicalH;
            lastPhysicalWidth_ = physW;
            lastPhysicalHeight_ = physH;
#if defined(__EMSCRIPTEN__)
            CNA_HtmlDom_UpdateSurface(logicalW, logicalH, physW, physH);
#endif
        }
#if defined(__EMSCRIPTEN__)
        CNA_HtmlDom_PresentFrame();
#endif
    }

    void HtmlDomGraphicsBackend::getLogicalSize(int& width, int& height) const
    {
        if (virtualHeight_ <= 0)
        {
            SDL_GetWindowSize(window_, &width, &height);
            return;
        }
        int physW, physH;
        SDL_GetWindowSize(window_, &physW, &physH);
        height = virtualHeight_;
        if (presentationMode_ == CnaPresentationMode::FixedHeightDynamicWidth && physH > 0)
            width = static_cast<int>(static_cast<double>(physW) * virtualHeight_ / physH + 0.5);
        else
            width = virtualWidth_ > 0 ? virtualWidth_ : physW;
    }

    void HtmlDomGraphicsBackend::GetViewportSize(int& width, int& height)
    {
        getLogicalSize(width, height);
    }

    void HtmlDomGraphicsBackend::SetVirtualResolution(int width, int height)
    {
        virtualWidth_ = width;
        virtualHeight_ = height;
    }

    void HtmlDomGraphicsBackend::SetPresentationMode(int mode)
    {
        presentationMode_ = static_cast<CnaPresentationMode>(mode);
    }

    bool HtmlDomGraphicsBackend::TransformWindowToLogical(float windowX, float windowY,
                                                          float& logX, float& logY) const
    {
        if (virtualHeight_ <= 0) return false;
        int physW, physH;
        SDL_GetWindowSize(window_, &physW, &physH);
        if (physH <= 0) return false;
        const float scale = static_cast<float>(virtualHeight_) / static_cast<float>(physH);
        logX = windowX * scale;
        logY = windowY * scale;
        return true;
    }

    bool HtmlDomGraphicsBackend::TransformLogicalToWindow(float logX, float logY,
                                                          float& windowX, float& windowY) const
    {
        // Exact inverse of TransformWindowToLogical: under FixedHeightDynamicWidth the logical
        // width already follows the surface's aspect ratio, so the mapping is a uniform scale with
        // no letterbox offset to account for.
        if (virtualHeight_ <= 0) return false;
        int physW, physH;
        SDL_GetWindowSize(window_, &physW, &physH);
        if (physH <= 0) return false;
        const float invScale = static_cast<float>(physH) / static_cast<float>(virtualHeight_);
        windowX = logX * invScale;
        windowY = logY * invScale;
        return true;
    }

    std::unique_ptr<ITextureBackend> HtmlDomGraphicsBackend::CreateTexture(const ImageData& data)
    {
        return std::make_unique<HtmlDomTextureBackend>(data);
    }

    std::unique_ptr<ISpriteBatchBackend> HtmlDomGraphicsBackend::CreateSpriteBatch()
    {
        return std::make_unique<HtmlDomSpriteBatchBackend>();
    }

    std::unique_ptr<IRenderTargetBackend> HtmlDomGraphicsBackend::CreateRenderTarget2D(
        int w, int h, int /*depthFormat*/, bool /*preserveContents*/, bool /*mipMap*/, int /*multiSampleCount*/)
    {
        // depthFormat/mipMap/multiSampleCount are all ignored, the same as SDL_RENDERER and CANVAS:
        // this backend has no depth storage, no mip chain and no MSAA to configure.
        return std::make_unique<HtmlDomRenderTargetBackend>(w, h);
    }

    void HtmlDomGraphicsBackend::SetRenderTarget2D(IRenderTargetBackend* rt)
    {
        if (rt)
        {
            rt->BindAsRenderTarget();
        }
        else
        {
            SetBoundRenderTargetIdEXT(0);
        }
    }

    void HtmlDomGraphicsBackend::SetRenderTargets(
        const RenderTargetBindingDescriptor* renderTargets, int count)
    {
        if (count > 1)
            throw std::runtime_error(
                "HTML_DOM backend: multiple simultaneous render targets (MRT) are not yet "
                "implemented -- requested " + std::to_string(count) + ". A single Canvas2D context "
                "backs each target here, and it is inherently single-output.");
        if (count > 0 && renderTargets[0].IsRenderTargetCubeFace())
            throw std::runtime_error(
                "HTML_DOM backend: RenderTargetCube face bindings are not yet implemented.");
        SetRenderTarget2D(count > 0 ? renderTargets[0].GetRenderTarget2D() : nullptr);
    }

    void HtmlDomGraphicsBackend::ReadBackbuffer(int x, int y, int w, int h, uint8_t* pixels)
    {
        if (GetBoundRenderTargetIdEXT() == 0)
            throw std::runtime_error(
                "HTML_DOM backend: reading back the backbuffer is not yet implemented, and cannot "
                "be: the backbuffer here is a live DOM subtree composited by the browser, and no "
                "browser API rasterizes one to pixels. Render into a RenderTarget2D and read that "
                "instead, or use the CANVAS backend when backbuffer readback is required.");
#if defined(__EMSCRIPTEN__)
        if (CNA_HtmlDom_ReadBound(x, y, w, h, pixels) == 0)
            throw std::runtime_error("HTML_DOM backend: the bound render target could not be read back.");
#else
        (void)x; (void)y; (void)w; (void)h; (void)pixels;
        throw std::runtime_error("HTML_DOM backend: no browser, so no pixels to read back.");
#endif
    }

    void HtmlDomGraphicsBackend::SetScissorRect(int x, int y, int w, int h)
    {
#if defined(__EMSCRIPTEN__)
        CNA_HtmlDom_SetScissorRect(x, y, w, h);
#else
        (void)x; (void)y; (void)w; (void)h;
#endif
    }

    void HtmlDomGraphicsBackend::SetViewport(int x, int y, int w, int h, float /*minDepth*/, float /*maxDepth*/)
    {
        // Idempotent: a game that binds/unbinds render targets every frame would otherwise record
        // (and cross the wasm/JS boundary for) the same unchanged viewport twice a frame for no
        // observable effect -- CNA_HtmlDom_SetViewport no longer touches the DOM at all (HTMLDOM-107),
        // but this guard is still cheap insurance against redundant Module state writes.
        if (x == lastViewportX_ && y == lastViewportY_ &&
            w == lastViewportWidth_ && h == lastViewportHeight_)
            return;
        lastViewportX_ = x;
        lastViewportY_ = y;
        lastViewportWidth_ = w;
        lastViewportHeight_ = h;
#if defined(__EMSCRIPTEN__)
        CNA_HtmlDom_SetViewport(x, y, w, h);
#else
        (void)x; (void)y; (void)w; (void)h;
#endif
    }

    void HtmlDomGraphicsBackend::ApplyBlendState(int colorSrcBlend, int alphaSrcBlend,
                                                 int colorDstBlend, int alphaDstBlend,
                                                 int colorBlendFunc, int alphaBlendFunc,
                                                 const BlendWriteState& /*writeState*/)
    {
        // REMED-GFX-077: CSS compositing has neither per-channel colour write masks nor any
        // coverage-sample-mask concept, so BlendState.ColorWriteChannels* and
        // BlendState.MultiSampleMask are inexpressible here -- a documented capability gap
        // (docs/html-dom-backend.md), not a silent drop.
        SetCurrentCompositeOpEXT(BlendStateToDomCompositeOp(
            colorSrcBlend, alphaSrcBlend, colorDstBlend, alphaDstBlend, colorBlendFunc, alphaBlendFunc));
    }

    void HtmlDomGraphicsBackend::ClearColorAndDepth(float, float, float, float, float) { ThrowNo3D("ClearColorAndDepth (3D)"); }
    void HtmlDomGraphicsBackend::ClearDepth(float) { ThrowNo3D("ClearDepth (3D)"); }
    void HtmlDomGraphicsBackend::ClearStencil(int) { ThrowNo3D("ClearStencil (3D)"); }
    void HtmlDomGraphicsBackend::ClearDepthAndStencil(float, int) { ThrowNo3D("ClearDepthAndStencil (3D)"); }
    void HtmlDomGraphicsBackend::ClearColorAndStencil(float, float, float, float, int) { ThrowNo3D("ClearColorAndStencil (3D)"); }
    void HtmlDomGraphicsBackend::ClearColorDepthAndStencil(float, float, float, float, float, int) { ThrowNo3D("ClearColorDepthAndStencil (3D)"); }
    void HtmlDomGraphicsBackend::SetDepthTestEnabled(bool) { ThrowNo3D("SetDepthTestEnabled (3D)"); }
    void HtmlDomGraphicsBackend::SetBlendEnabled(bool) { ThrowNo3D("SetBlendEnabled (3D)"); }
    void HtmlDomGraphicsBackend::SetDepthWriteEnabled(bool) { ThrowNo3D("SetDepthWriteEnabled (3D)"); }

    std::unique_ptr<IVertexBufferBackend> HtmlDomGraphicsBackend::CreateVertexBuffer(int)
    {
        ThrowNo3D("CreateVertexBuffer (3D)");
    }

    std::unique_ptr<IIndexBufferBackend> HtmlDomGraphicsBackend::CreateIndexBuffer16(int)
    {
        ThrowNo3D("CreateIndexBuffer16 (3D)");
    }

    void HtmlDomGraphicsBackend::DrawColoredPrimitives(const IVertexBufferBackend&,
                                                       const Matrix&, const Matrix&, const Matrix&,
                                                       PrimitiveType, int) { ThrowNo3D("DrawColoredPrimitives (3D)"); }

    void HtmlDomGraphicsBackend::DrawIndexedColoredPrimitives(const IVertexBufferBackend&, const IIndexBufferBackend&,
                                                              const Matrix&, const Matrix&, const Matrix&,
                                                              PrimitiveType, int) { ThrowNo3D("DrawIndexedColoredPrimitives (3D)"); }
}

namespace CNA::Internal::Backends
{
    std::unique_ptr<IGraphicsBackend> CreateGraphicsBackend(const GraphicsBackendCreateArgs& args)
    {
        return std::make_unique<HtmlDom::HtmlDomGraphicsBackend>(
            args.window, args.virtualWidth, args.virtualHeight, args.presentationMode);
    }
}
