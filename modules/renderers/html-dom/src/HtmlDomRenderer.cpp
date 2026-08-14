// SPDX-License-Identifier: MS-PL
#include "CNA/Internal/Renderers/HtmlDom/HtmlDomRenderer.hpp"
#include "CNA/Internal/Renderers/HtmlDom/HtmlDomTextureRenderer.hpp"
#include "CNA/Internal/Renderers/HtmlDom/HtmlDomRenderTargetRenderer.hpp"
#include "CNA/Internal/Renderers/HtmlDom/HtmlDomSpriteBatchRenderer.hpp"
#include "CNA/Internal/Renderers/Common/NotYetImplemented.hpp"

#include "System/ArgumentNullException.hpp"
#include "System/ArgumentOutOfRangeException.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <string>

#if defined(__EMSCRIPTEN__)
#include <emscripten.h>

// plan_html_dom.md HTMLDOM-10 / design decision 2: creates the <div> every sprite element lives in,
// positioned over the <canvas> SDL3's Emscripten video driver owns.
//
// The canvas itself is made transparent but deliberately NOT hidden, removed or display:none'd --
// SDL keeps sizing it and delivering input through it, so it must remain in both layout and pointer
// hit testing. The DOM surface ignores pointer events, allowing them to reach the transparent
// canvas underneath. `contain:strict` keeps the sprite subtree's layout and painting isolated from
// the rest of the page, and clips sprites that leave the viewport.
//
// plan_html_dom.md HTMLDOM-108: #cna-dom-root now lives inside a second wrapper element,
// #cna-dom-viewport, sized/positioned to the CANVAS's own physical bounds with its own
// `overflow:hidden`. Root's own CSS box is always exactly the logical (game-coordinate) size and
// gets a per-CnaPresentationMode `transform: scale()` plus a `left`/`top` offset (both computed by
// cnaDomApplySurfaceGeometry, from HtmlDomRenderer::ComputeLogicalViewport) -- for Overscan
// specifically, that scaled root is LARGER than the canvas and deliberately extends past its edges
// on purpose (the whole point of an overscan crop). Root's own `overflow:hidden`/`contain:strict`
// only ever clipped root's CHILDREN to root's OWN (pre-transform, logical-sized) box, which was
// never the box that needed clipping here -- nothing previously bounded the SCALED, POSITIONED root
// element itself to the physical canvas area, so an oversized/offset root could bleed into (or
// trigger a scrollbar on) the surrounding host page. The wrapper is a real element specifically so
// this clipping is self-contained within this renderer's own DOM structure, independent of whatever
// the host page's own layout happens to be.
//
// plan_html_dom.md HTMLDOM-115: the REST of the host-page integration surface -- an existing
// element ID-colliding with this renderer's own, the canvas's own pre-existing `visibility` value
// (preserved and restored exactly, not assumed to have been unset), and a `window.resize` listener
// so a host-page reflow that MOVES the canvas without resizing it (Present()'s own dirty check only
// fires on a SIZE change) still re-syncs the wrapper's own position. Z-order relative to page
// siblings is deliberately left as plain DOM order (no explicit `z-index`) -- the wrapper is
// inserted immediately after the canvas, so it paints on top of whatever came before the canvas in
// the page and below whatever comes after; a host page needing a specific stacking order relative
// to its OWN unrelated content should give ITS OWN elements an explicit `z-index`, the same as it
// would need to for any two ordinary sibling elements. A canvas positioned via a CSS `transform`
// (scale/rotate/skew) is a documented, accepted gap -- `offsetLeft`/`offsetTop` (what
// cnaDomApplySurfaceGeometry reads) do not account for CSS transforms at all, only layout position;
// see docs/html-dom-renderer.md's own Known Limitations.
EM_JS(void, CNA_HtmlDom_EnsureRoot, (), {
    // Guarded: the repo's own GTest runner is plain `node`, where there is no document at all, so
    // an unguarded reference would throw a ReferenceError into wasm instead of degrading to
    // "no surface, nothing renders". No document means no root and no ref count either, matching
    // CNA_HtmlDom_DestroyRoot's own no-op there.
    if (typeof document === 'undefined') return;
    // plan_html_dom.md HTMLDOM-114: reference-counted, not a plain "already initialized" guard.
    // A SECOND HtmlDomRenderer constructed while a FIRST one is still alive (an unusual but
    // real-to-guard-against scenario -- nothing in IGraphicsRenderer prevents constructing more than
    // one) must not silently ADOPT this shared root and then rip it out from under the first
    // renderer when the second one alone is later destroyed. CNA_HtmlDom_DestroyRoot only actually
    // tears the DOM surface down once every renderer that ever called this has also called that --
    // the ordinary single-renderer case still goes 0->1->0 exactly as before.
    Module['cnaDomRendererRefCount'] = (Module['cnaDomRendererRefCount'] || 0) + 1;
    if (Module['cnaDomRoot']) return;
    const canvas = Module['canvas'] || document.querySelector('canvas');
    if (!canvas) { console.error('[CNA] HTML_DOM: no <canvas> element to anchor the DOM surface to'); return; }
    // plan_html_dom.md HTMLDOM-115: a page that already has its OWN unrelated element using either
    // of these exact ids is a real, if unlikely, host-page integration hazard -- silently adopting/
    // conflating it with this renderer's own root would let some other script's DOM manipulation
    // corrupt this renderer's rendering in confusing ways. Fails loudly, the same way the missing-
    // canvas case above does, rather than proceeding into an ID collision.
    if (document.getElementById('cna-dom-root') || document.getElementById('cna-dom-viewport')) {
        console.error('[CNA] HTML_DOM: the page already has an element with id "cna-dom-root" or ' +
                      '"cna-dom-viewport" that this renderer did not create -- refusing to proceed ' +
                      'to avoid a silent ID collision. Remove or rename the conflicting element.');
        return;
    }
    const viewportEl = document.createElement('div');
    viewportEl.id = 'cna-dom-viewport';
    // The static black matches SDL3's own SDL_LOGICAL_PRESENTATION_LETTERBOX behaviour (its bars
    // are always black, not the app's current clear colour) -- this element is exactly what shows
    // through in a Letterbox bar or outside an undersized NativeBackBuffer surface.
    viewportEl.style.cssText = 'position:absolute;left:0;top:0;overflow:hidden;contain:strict;' +
                               'pointer-events:none;' +
                               'background-color:#000;';
    const root = document.createElement('div');
    root.id = 'cna-dom-root';
    root.style.cssText = 'position:absolute;left:0;top:0;overflow:hidden;transform-origin:0 0;' +
                         'contain:strict;background-color:#000;';
    (canvas.parentNode || document.body).insertBefore(viewportEl, canvas.nextSibling);
    viewportEl.appendChild(root);
    // Keep the canvas in pointer hit testing: SDL's Emscripten driver registered its mouse/touch
    // handlers on this exact element. visibility:hidden would remove it from hit testing, while
    // opacity:0 keeps it as the input surface without exposing its pixels below the DOM renderer.
    // Preserve the host page's exact inline opacity for symmetric teardown.
    Module['cnaDomOriginalCanvasOpacity'] = canvas.style.opacity;
    canvas.style.opacity = '0';
    // plan_html_dom.md HTMLDOM-115: Present()'s own dirty check only re-derives geometry when the
    // LOGICAL or PHYSICAL SIZE changes -- a host-page layout reflow that moves the canvas WITHOUT
    // resizing it (a responsive layout shift, an animated margin/position change elsewhere on the
    // page, ...) would never trip that check, leaving the wrapper's own left/top visibly stale
    // until the next real resize. `resize` does not cover every possible reposition (a pure CSS
    // animation with no window resize at all is a documented, accepted gap -- see docs/html-dom-
    // renderer.md's own Known Limitations), but it is the single highest-value, lowest-cost signal:
    // window resizes and orientation changes are what most real responsive layouts key off of.
    const resizeListener = function () {
        if (Module['cnaDomApplySurfaceGeometry']) Module['cnaDomApplySurfaceGeometry']();
    };
    window.addEventListener('resize', resizeListener);
    Module['cnaDomResizeListener'] = resizeListener;
    Module['cnaDomViewportEl'] = viewportEl;
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
    // HtmlDomSpriteBatchRenderer.cpp's CNA_HtmlDom_FlushSprites, the only place a region is chosen,
    // for the batch-granularity rationale. 'full' is the default, zero-clip region every sprite
    // uses until a game actually narrows the scissor rect below the current surface size; its
    // container IS #cna-dom-root itself and its pool IS the same flat pool this renderer always
    // had, so the overwhelmingly common no-scissor case costs nothing beyond what it always did.
    // plan_html_dom.md HTMLDOM-116: idleStreak/idleUsed drive CNA_HtmlDom_PresentFrame's own pool
    // age-out below -- see that function's comment for what they track.
    Module['cnaDomRegions'] = {
        'full': { container: root, pool: [], used: 0, highWater: 0, rect: null, idleStreak: 0, idleUsed: -1 }
    };
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
    // plan_html_dom.md HTMLDOM-110: whether a named (non-'full') region has EVER been created this
    // session -- set once, in cnaDomGetRegion's own region-creation path, never reset. Lets
    // CNA_HtmlDom_FlushSprites skip 'full'-region sprites' otherwise-unconditional per-frame
    // z-index write when there is provably nothing to interleave it against yet -- see that
    // write's own comment for the full rationale and its one narrow, accepted caveat.
    Module['cnaDomAnyNamedRegionEverCreated'] = false;
    // plan_html_dom.md HTMLDOM-110: total CSS property writes / total flush calls
    // CNA_HtmlDom_FlushSprites has issued this session -- CNAEXT instrumentation only, read (and
    // optionally reset) via Module['cnaDomStyleWriteCount']/['cnaDomFlushCallCount'] directly by
    // test code. Never read by the renderer itself.
    Module['cnaDomStyleWriteCount'] = 0;
    Module['cnaDomFlushCallCount'] = 0;

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
    // space concepts on a real GPU (confirmed by reading EasyGLRenderer: its scissor forwards
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

    // plan_html_dom.md HTMLDOM-107: positions and sizes #cna-dom-root to the full LOGICAL surface,
    // always -- HTMLDOM-98 previously shrank/moved root to track a sub-rectangle Viewport directly,
    // which meant a SECOND viewport set later in the SAME frame retroactively moved/clipped every
    // sprite already drawn under an EARLIER viewport (the split-screen/sub-panel use case the
    // capability matrix claimed was never actually implemented), and Clear() -- which real XNA
    // clears the WHOLE render target with, independent of the current Viewport, exactly like a real
    // glClear/ClearRenderTargetView call ignores glViewport/RSSetViewports -- left the backbuffer
    // area OUTSIDE a shrunk root showing the host page's own background instead of the clear colour.
    // The GraphicsDevice.Viewport's own offset is applied per BATCH instead, in
    // CNA_HtmlDom_FlushSprites (both the DOM and Canvas2D paths) -- see the comment there for why
    // sprite position, not root's own box or a region's clip, is the correct place for it. This is
    // an entirely different concept from the CnaPresentationMode offset/scale below.
    //
    // plan_html_dom.md HTMLDOM-108: root's PHYSICAL placement (offset + scale) now follows
    // HtmlDomRenderer::ComputeLogicalViewport's per-CnaPresentationMode geometry -- an
    // earlier version applied a single height-derived uniform scale() unconditionally, correct only
    // for FixedHeightDynamicWidth (which happens to always fill the physical surface exactly) and
    // silently wrong for Letterbox/Overscan/Stretch/NativeBackBuffer. #cna-dom-viewport (the
    // physical-canvas-sized wrapper CNA_HtmlDom_EnsureRoot creates root inside) clips the scaled,
    // offset root to the canvas's own bounds -- needed for Overscan, whose scaled root is
    // deliberately larger than the canvas and extends past its edges on purpose.
    Module['cnaDomApplySurfaceGeometry'] = function () {
        const root = Module['cnaDomRoot'];
        const viewportEl = Module['cnaDomViewportEl'];
        if (!root || !viewportEl) return;
        const w = Module['cnaDomBackbufferW'] || 0;
        const h = Module['cnaDomBackbufferH'] || 0;
        root.style.width = w + 'px';
        root.style.height = h + 'px';
        Module['cnaDomLogicalW'] = w;
        Module['cnaDomLogicalH'] = h;
        const canvas = Module['canvas'] ||
                       (typeof document === 'undefined' ? null : document.querySelector('canvas'));
        viewportEl.style.left = (canvas ? canvas.offsetLeft : 0) + 'px';
        viewportEl.style.top = (canvas ? canvas.offsetTop : 0) + 'px';
        viewportEl.style.width = (canvas ? canvas.offsetWidth : w) + 'px';
        viewportEl.style.height = (canvas ? canvas.offsetHeight : h) + 'px';
        const offsetX = Module['cnaDomViewportOffsetX'] || 0;
        const offsetY = Module['cnaDomViewportOffsetY'] || 0;
        const scaleX = Module['cnaDomViewportScaleX'] || 1;
        const scaleY = Module['cnaDomViewportScaleY'] || 1;
        root.style.left = offsetX + 'px';
        root.style.top = offsetY + 'px';
        root.style.transform = (scaleX !== 1 || scaleY !== 1)
            ? 'scale(' + scaleX + ',' + scaleY + ')' : "";
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
        region = { container: container, pool: [], used: 0, highWater: 0, rect: rect,
                   idleStreak: 0, idleUsed: -1 };
        regions[key] = region;
        // plan_html_dom.md HTMLDOM-110: this is the first-ever NAMED region -- see
        // CNA_HtmlDom_FlushSprites' own needsFullRegionZIndex comment for why this flag exists.
        Module['cnaDomAnyNamedRegionEverCreated'] = true;
        Module['cnaDomTouchRegion'](key);
        Module['cnaDomApplyRegionClip'](region);
        return region;
    };
});

EM_JS(void, CNA_HtmlDom_DestroyRoot, (), {
    // plan_html_dom.md HTMLDOM-114: mirrors CNA_HtmlDom_EnsureRoot's own reference count -- only
    // the LAST renderer to call this actually tears the shared DOM surface down. A second (or
    // third, ...) live renderer, constructed while this one was already alive, keeps the surface
    // alive for whichever renderer(s) are still using it; only its own count decrements here.
    // typeof document check mirrors EnsureRoot's own early-out: no document means no ref count was
    // ever incremented for this call's own construction, so there is nothing to decrement either.
    if (typeof document !== 'undefined') {
        Module['cnaDomRendererRefCount'] = Math.max(0, (Module['cnaDomRendererRefCount'] || 1) - 1);
        if (Module['cnaDomRendererRefCount'] > 0) return;
    }
    // plan_html_dom.md HTMLDOM-108: removes #cna-dom-viewport (which contains root), not root
    // directly -- root's own parent is now the wrapper, not the canvas's own parent, so removing
    // just root would leave the (now-empty) wrapper behind.
    const viewportEl = Module['cnaDomViewportEl'];
    if (viewportEl && viewportEl.parentNode) viewportEl.parentNode.removeChild(viewportEl);
    const canvas = Module['canvas'] ||
                   (typeof document === 'undefined' ? null : document.querySelector('canvas'));
    // plan_html_dom.md HTMLDOM-115: restores the EXACT pre-existing value CNA_HtmlDom_EnsureRoot
    // captured, not a hardcoded "" -- see that capture's own comment.
    if (canvas) canvas.style.opacity = Module['cnaDomOriginalCanvasOpacity'] || "";
    Module['cnaDomOriginalCanvasOpacity'] = null;
    if (Module['cnaDomResizeListener']) {
        window.removeEventListener('resize', Module['cnaDomResizeListener']);
        Module['cnaDomResizeListener'] = null;
    }
    Module['cnaDomViewportEl'] = null;
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
    // plan_html_dom.md HTMLDOM-103: Clear() is this renderer's own "frame start" marker (design
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

        // plan_html_dom.md HTMLDOM-116: every sprite element created in HtmlDomSpriteBatchRenderer.cpp
        // gets 'will-change:transform' (that file's own comment on why), which forces its own
        // permanent compositor layer -- and until this fix, the pool never released one once
        // created: elements past `used` are only ever hidden via display:none above, never removed.
        // So a single transient burst (e.g. a one-time explosion effect briefly drawing 10,000
        // sprites) permanently retained 10,000 DOM nodes and compositor layers for the rest of the
        // session, even after usage settled back down to a handful -- measured directly in
        // modules/renderers/html-dom/examples/htmldom_memory_test.cpp, which is also where kIdleShrinkFrames below is chosen
        // and justified against real numbers, not just asserted.
        //
        // Age-out: once a region's used count has been EXACTLY unchanged for kIdleShrinkFrames
        // consecutive Present() calls -- a genuinely settled scene, not merely "below peak", so a
        // fluctuating sprite count never accumulates a streak and normal gameplay oscillation
        // (including the 5-50/frame swings htmldom_stress_test.cpp's own HTMLDOM-90 run exercises)
        // never triggers this -- the pool entries above that settled count are actually removed from
        // the DOM (not just hidden), releasing their compositor layer and node, and the pool array
        // itself shrinks to match. 180 frames (~3s at 60fps) is deliberately longer than any
        // realistic action-to-idle gap a real game's own pacing would produce between bursts, so this
        // never thrashes create/destroy churn during ordinary gameplay -- it only reclaims a pool
        // that has genuinely stopped needing its past peak.
        const kIdleShrinkFrames = 180;
        if (used === region.idleUsed) {
            if (++region.idleStreak >= kIdleShrinkFrames && pool.length > used) {
                for (let i = pool.length - 1; i >= used; --i) {
                    const el = pool[i];
                    if (el && el.parentNode) el.parentNode.removeChild(el);
                }
                pool.length = used;
                region.idleStreak = 0;
            }
        } else {
            region.idleUsed = used;
            region.idleStreak = 0;
        }

        region.used = 0;
    }
});

// Records the backbuffer's own logical geometry and the physical placement (offset + scale) it
// should render at. Called only when the geometry actually changed (the renderer caches the last
// values), because these are the renderer's only layout-affecting style writes -- doing them every
// frame would defeat the whole design.
//
// plan_html_dom.md HTMLDOM-107: sizes/positions #cna-dom-root directly, to the full backbuffer --
// root no longer tracks a sub-rectangle Viewport at all (see cnaDomApplySurfaceGeometry's own
// comment, CNA_HtmlDom_EnsureRoot, for why).
//
// plan_html_dom.md HTMLDOM-108: offsetX/offsetY/scaleX/scaleY come from
// HtmlDomRenderer::ComputeLogicalViewport, computed on the C++ side once per Present() --
// the same per-CnaPresentationMode geometry SdlGpuRenderer's own reference implementation
// uses, replacing a single height-derived uniform scale that was only ever correct for
// FixedHeightDynamicWidth.
EM_JS(void, CNA_HtmlDom_UpdateSurface, (int logicalW, int logicalH,
                                        double offsetX, double offsetY,
                                        double scaleX, double scaleY), {
    const root = Module['cnaDomRoot'];
    if (!root) return;
    Module['cnaDomBackbufferW'] = logicalW;
    Module['cnaDomBackbufferH'] = logicalH;
    Module['cnaDomViewportOffsetX'] = offsetX;
    Module['cnaDomViewportOffsetY'] = offsetY;
    Module['cnaDomViewportScaleX'] = scaleX;
    Module['cnaDomViewportScaleY'] = scaleY;
    // HTMLDOM-93/94/107/108: the backbuffer's own geometry just changed (that is the only reason
    // this function runs at all -- Present() only calls it when geometry differs from last frame).
    // cnaDomApplySurfaceGeometry re-sizes/positions root and the viewport wrapper against the new
    // geometry and re-derives every region's clip-path, the same reasoning HTMLDOM-93's own resize
    // handling already used.
    Module['cnaDomApplySurfaceGeometry']();
});

// plan_html_dom.md HTMLDOM-80 / design decision 13: real scissor clipping via `clip-path: inset()`.
// Exact, with no transform-inverse maths needed: region containers carry no rotation (only the
// scale()/translate() CNA_HtmlDom_UpdateSurface applies to #cna-dom-root, which every region
// container inherits -- HTMLDOM-108: per-axis scale under Stretch, uniform under every other mode,
// but `inset()` distances are always expressed in the element's own PRE-transform local units
// regardless, so neither case needs different handling), so clip-path's own pre-transform local
// coordinate space already coincides with the same logical-pixel space every sprite's destX/destY
// is expressed in -- the scissor rect can be turned into inset() distances directly, with no
// per-sprite transform to invert.
//
// HTMLDOM-94: this function ONLY records the current rect now -- it does not touch the DOM at all.
// The rect it records is consumed once per SpriteBatch Begin/End batch, at CNA_HtmlDom_FlushSprites
// (HtmlDomSpriteBatchRenderer.cpp), which is what actually resolves it to a region and therefore a
// clip. That is a real behaviour change from the single whole-surface clip-path this function used
// to apply directly to #cna-dom-root: sprites already flushed into a region under an EARLIER rect
// keep that region's own clip-path even after this function is called again with a DIFFERENT rect,
// instead of every sprite currently on screen being retroactively reclipped to whatever rect is
// "current" by the time the frame is inspected. Scoped honestly at BATCH granularity, matching this
// renderer's one-flush-per-Begin/End architecture -- not literally per individual Draw() call within
// the same still-open batch (SpriteSortMode::Immediate games that change ScissorRectangle between
// individual Draw() calls inside one Begin/End won't see finer-grained clipping than that).
//
// Applied regardless of RasterizerState.ScissorTestEnable, the same way SdlRenderer::
// SetScissorRect behaves: SDL_Renderer never overrides ApplyRasterizerState at all, so its own
// SDL_SetRenderClipRect call is never gated on ScissorTestEnable either -- confirmed by reading
// SdlRenderer.cpp before matching its behaviour here, not assumed.
EM_JS(void, CNA_HtmlDom_SetScissorRect, (int x, int y, int w, int h), {
    if (!Module['cnaDomRoot']) return;
    Module['cnaDomScissorRect'] = { x: x, y: y, w: w, h: h };
});

// plan_html_dom.md HTMLDOM-107: ONLY records the new viewport now -- it does not touch the DOM at
// all, the same shape CNA_HtmlDom_SetScissorRect already uses. The viewport recorded here is read
// once per SpriteBatch Begin/End batch, at CNA_HtmlDom_FlushSprites (HtmlDomSpriteBatchRenderer.cpp),
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

// plan_html_dom.md HTMLDOM-117: GraphicsCapability::AdditiveBlending's real, queryable answer --
// memoized, since CSS.supports' own answer can never change mid-session, the same "compute once,
// cache on Module" shape this renderer already uses elsewhere rather than re-querying the CSSOM on
// every SupportsCapability() call.
EM_JS(int, CNA_HtmlDom_SupportsAdditiveBlending, (), {
    if (Module['cnaDomSupportsAdditiveBlending'] === undefined) {
        Module['cnaDomSupportsAdditiveBlending'] =
            (typeof CSS !== 'undefined' && CSS.supports &&
             CSS.supports('mix-blend-mode', 'plus-lighter')) ? 1 : 0;
    }
    return Module['cnaDomSupportsAdditiveBlending'];
});
#endif

namespace CNA::Internal::Renderers::HtmlDom
{
    namespace
    {
        // The owner's instruction for this renderer's 3D surface: throw "not yet implemented" rather
        // than silently doing nothing. Callers can check GraphicsDevice::SupportsCapability()
        // ahead of time instead of relying on the throw -- see SupportsCapability() in the header.
        [[noreturn]] void ThrowNo3D(const char* what)
        {
            NotYetImplemented("HTML_DOM", what);
        }
    }

    HtmlDomRenderer::HtmlDomRenderer(SDL_Window* window, int virtualWidth, int virtualHeight,
                                                   CnaPresentationMode mode)
        : window_(window)
        , virtualWidth_(virtualWidth)
        , virtualHeight_(virtualHeight)
        , presentationMode_(mode)
    {
        if (!window_) throw std::runtime_error("HtmlDomRenderer initialized with null window.");
        IGraphicsRenderer::RegisterForWindow(window_, this);
        // A renderer can be constructed after a previous one was destroyed (GraphicsDevice::Reset),
        // so the shared draw-path state is rewound rather than assumed pristine.
        SetBoundRenderTargetIdEXT(0);
        SetCurrentCompositeOpEXT(DomCompositeOp::NonPremultiplied);
        SetCurrentScissorEnableEXT(false);
#if defined(__EMSCRIPTEN__)
        CNA_HtmlDom_EnsureRoot();
#endif
    }

    HtmlDomRenderer::~HtmlDomRenderer()
    {
        IGraphicsRenderer::UnregisterForWindow(window_);
#if defined(__EMSCRIPTEN__)
        CNA_HtmlDom_DestroyRoot();
#endif
    }

    void HtmlDomRenderer::Clear(float r, float g, float b, float a)
    {
#if defined(__EMSCRIPTEN__)
        CNA_HtmlDom_Clear(r, g, b, a);
#else
        (void)r; (void)g; (void)b; (void)a;
#endif
    }

    void HtmlDomRenderer::Present()
    {
        const LogicalViewport viewport = ComputeLogicalViewport();
        const int logicalW = static_cast<int>(std::lround(viewport.logicalWidth));
        const int logicalH = static_cast<int>(std::lround(viewport.logicalHeight));
        int physW = 0, physH = 0;
        SDL_GetWindowSize(window_, &physW, &physH);
        const float scaleX = viewport.logicalWidth > 0.0f ? viewport.width / viewport.logicalWidth : 1.0f;
        const float scaleY = viewport.logicalHeight > 0.0f ? viewport.height / viewport.logicalHeight : 1.0f;
        // plan_html_dom.md HTMLDOM-108: tracks the full viewport (offset + scale), not just logical/
        // physical W/H as before -- a SetPresentationMode call alone (same virtual and physical
        // size, different mode, therefore a different scale/offset) must also re-apply geometry, and
        // the old W/H-only comparison could not detect that.
        if (logicalW != lastLogicalWidth_ || logicalH != lastLogicalHeight_ ||
            physW != lastPhysicalWidth_ || physH != lastPhysicalHeight_ ||
            viewport.x != lastViewportOffsetX_ || viewport.y != lastViewportOffsetY_ ||
            scaleX != lastViewportScaleX_ || scaleY != lastViewportScaleY_)
        {
            lastLogicalWidth_ = logicalW;
            lastLogicalHeight_ = logicalH;
            lastPhysicalWidth_ = physW;
            lastPhysicalHeight_ = physH;
            lastViewportOffsetX_ = viewport.x;
            lastViewportOffsetY_ = viewport.y;
            lastViewportScaleX_ = scaleX;
            lastViewportScaleY_ = scaleY;
#if defined(__EMSCRIPTEN__)
            CNA_HtmlDom_UpdateSurface(logicalW, logicalH, viewport.x, viewport.y, scaleX, scaleY);
#endif
        }
#if defined(__EMSCRIPTEN__)
        CNA_HtmlDom_PresentFrame();
#endif
    }

    HtmlDomRenderer::LogicalViewport HtmlDomRenderer::ComputeLogicalViewport() const
    {
        // plan_html_dom.md HTMLDOM-108: ported from SdlGpuRenderer::ComputeLogicalViewport
        // (the "complete renderer" this task's own plan row names) rather than re-derived, so every
        // CnaPresentationMode's geometry matches an already-tested reference exactly. See
        // LogicalViewport's own doc comment (HtmlDomRenderer.hpp) for what x/y/width/height
        // and logicalWidth/logicalHeight mean per mode.
        LogicalViewport viewport{};
        int physW = 0, physH = 0;
        SDL_GetWindowSize(window_, &physW, &physH);
        viewport.width = viewport.logicalWidth = static_cast<float>(std::max(0, physW));
        viewport.height = viewport.logicalHeight = static_cast<float>(std::max(0, physH));
        if (physW <= 0 || physH <= 0) return viewport;
        // NativeBackBuffer, and "no virtual resolution configured at all", both mean the same thing
        // for rendering purposes: no scaling, the logical surface just IS the physical one.
        if (presentationMode_ == CnaPresentationMode::NativeBackBuffer ||
            virtualWidth_ <= 0 || virtualHeight_ <= 0)
            return viewport;

        float logicalWidth = static_cast<float>(virtualWidth_);
        float logicalHeight = static_cast<float>(virtualHeight_);
        if (presentationMode_ == CnaPresentationMode::FixedHeightDynamicWidth)
        {
            // Height fixed, width derived from the surface's own aspect ratio -- the viewport
            // always ends up exactly the full physical rect (viewport.width/height keep their
            // physical-size defaults above), by construction: this is the one mode where logical
            // width is CHOSEN to make that true, not a coincidence needing bars or cropping.
            logicalWidth = logicalHeight * static_cast<float>(physW) / static_cast<float>(physH);
            viewport.logicalWidth = logicalWidth;
            viewport.logicalHeight = logicalHeight;
            return viewport;
        }

        viewport.logicalWidth = logicalWidth;
        viewport.logicalHeight = logicalHeight;
        if (presentationMode_ == CnaPresentationMode::Stretch)
            // Fixed logical size stretched to fill the full physical rect on both axes
            // independently (viewport.width/height keep their physical-size defaults) -- the only
            // mode that does not preserve aspect ratio.
            return viewport;

        // Letterbox/Overscan: fixed logical size, UNIFORM scale (same factor both axes, so aspect
        // ratio is preserved), centred in the physical rect. Letterbox picks the smaller of the two
        // per-axis scales so the whole logical rect fits inside the physical one (bars on the
        // non-fitting axis); Overscan picks the larger so the physical rect is fully covered
        // (cropped on the non-fitting axis, content extends past the physical edges).
        const float sx = static_cast<float>(physW) / logicalWidth;
        const float sy = static_cast<float>(physH) / logicalHeight;
        const float scale = presentationMode_ == CnaPresentationMode::Overscan
                                 ? std::max(sx, sy)
                                 : std::min(sx, sy);
        viewport.width = logicalWidth * scale;
        viewport.height = logicalHeight * scale;
        viewport.x = (static_cast<float>(physW) - viewport.width) * 0.5f;
        viewport.y = (static_cast<float>(physH) - viewport.height) * 0.5f;
        return viewport;
    }

    bool HtmlDomRenderer::SupportsCapability(CNA::GraphicsCapability capability) const
    {
        switch (capability)
        {
            case CNA::GraphicsCapability::AdditiveBlending:
#if defined(__EMSCRIPTEN__)
                return CNA_HtmlDom_SupportsAdditiveBlending() != 0;
#else
                return false;
#endif
            default:
                return false;
        }
    }

    void HtmlDomRenderer::getLogicalSize(int& width, int& height) const
    {
        const LogicalViewport viewport = ComputeLogicalViewport();
        width = static_cast<int>(std::lround(viewport.logicalWidth));
        height = static_cast<int>(std::lround(viewport.logicalHeight));
    }

    void HtmlDomRenderer::GetViewportSize(int& width, int& height)
    {
        getLogicalSize(width, height);
    }

    void HtmlDomRenderer::SetVirtualResolution(int width, int height)
    {
        virtualWidth_ = width;
        virtualHeight_ = height;
    }

    void HtmlDomRenderer::SetPresentationMode(int mode)
    {
        // plan_html_dom.md HTMLDOM-108: matches SdlGpuRenderer::SetPresentationMode's own
        // validation -- an invalid ordinal used to be stored unchecked, silently corrupting every
        // later ComputeLogicalViewport() call instead of failing at the actual bad input.
        if (mode < static_cast<int>(CnaPresentationMode::Letterbox) ||
            mode > static_cast<int>(CnaPresentationMode::FixedHeightDynamicWidth))
            throw std::out_of_range(
                "HTML_DOM renderer: invalid CnaPresentationMode ordinal " + std::to_string(mode));
        presentationMode_ = static_cast<CnaPresentationMode>(mode);
    }

    bool HtmlDomRenderer::TransformWindowToLogical(float windowX, float windowY,
                                                          float& logX, float& logY) const
    {
        // Preserves this renderer's own existing contract for "no virtual resolution configured at
        // all": there is no distinct logical space to report a mapping into, regardless of the
        // (perfectly valid, 1:1) rendering fallback ComputeLogicalViewport() uses for that same
        // case when actually drawing.
        if (virtualWidth_ <= 0 || virtualHeight_ <= 0) return false;
        const LogicalViewport viewport = ComputeLogicalViewport();
        if (viewport.width <= 0.0f || viewport.height <= 0.0f) return false;
        logX = (windowX - viewport.x) * viewport.logicalWidth / viewport.width;
        logY = (windowY - viewport.y) * viewport.logicalHeight / viewport.height;
        // Letterbox: a point in the bars (outside the scaled content rectangle) has no logical
        // counterpart at all -- matches SdlGpuRenderer::TransformWindowToLogical's own
        // contract. Overscan's viewport always extends at least to the physical edges on every
        // axis, so every on-screen point is inside it; Stretch/FixedHeightDynamicWidth/
        // NativeBackBuffer's viewport IS the physical rect, so this is never false for them either.
        return windowX >= viewport.x && windowX < viewport.x + viewport.width &&
               windowY >= viewport.y && windowY < viewport.y + viewport.height;
    }

    bool HtmlDomRenderer::TransformLogicalToWindow(float logX, float logY,
                                                          float& windowX, float& windowY) const
    {
        // Exact inverse of TransformWindowToLogical, using the same per-mode LogicalViewport
        // geometry; see that method's own comment for the "no virtual resolution" early-out.
        if (virtualWidth_ <= 0 || virtualHeight_ <= 0) return false;
        const LogicalViewport viewport = ComputeLogicalViewport();
        if (viewport.logicalWidth <= 0.0f || viewport.logicalHeight <= 0.0f) return false;
        windowX = viewport.x + logX * viewport.width / viewport.logicalWidth;
        windowY = viewport.y + logY * viewport.height / viewport.logicalHeight;
        return true;
    }

    std::unique_ptr<ITextureRenderer> HtmlDomRenderer::CreateTexture(const ImageData& data)
    {
        return std::make_unique<HtmlDomTextureRenderer>(data);
    }

    std::unique_ptr<ISpriteBatchRenderer> HtmlDomRenderer::CreateSpriteBatch()
    {
        return std::make_unique<HtmlDomSpriteBatchRenderer>();
    }

    std::unique_ptr<IOcclusionQueryRenderer> HtmlDomRenderer::CreateOcclusionQuery()
    {
        ThrowNo3D("CreateOcclusionQuery");
    }

    std::unique_ptr<ITexture3DRenderer> HtmlDomRenderer::CreateTexture3D(
        int, int, int, bool, int)
    {
        ThrowNo3D("CreateTexture3D");
    }

    std::unique_ptr<ITextureCubeRenderer> HtmlDomRenderer::CreateTextureCube(int, bool, int)
    {
        ThrowNo3D("CreateTextureCube");
    }

    std::unique_ptr<IRenderTargetCubeRenderer> HtmlDomRenderer::CreateRenderTargetCube(
        int, int, bool, bool, int)
    {
        ThrowNo3D("CreateRenderTargetCube");
    }

    std::unique_ptr<IEffectRenderer> HtmlDomRenderer::CreateEffectRenderer(
        const std::string&, const std::string&)
    {
        ThrowNo3D("CreateEffectRenderer (custom programmable effects)");
    }

    std::unique_ptr<IRenderTargetRenderer> HtmlDomRenderer::CreateRenderTarget2D(
        int w, int h, int depthFormat, bool /*preserveContents*/, bool mipMap, int multiSampleCount)
    {
        if (depthFormat != 0)
            throw std::runtime_error(
                "HTML_DOM renderer: RenderTarget2D depth/stencil attachments are unsupported; "
                "request DepthFormat::None.");
        if (mipMap)
            throw std::runtime_error(
                "HTML_DOM renderer: mipmapped RenderTarget2D resources are unsupported.");
        if (multiSampleCount != 0)
            throw std::runtime_error(
                "HTML_DOM renderer: multisampled RenderTarget2D resources are unsupported; "
                "request a zero sample count.");
        return std::make_unique<HtmlDomRenderTargetRenderer>(w, h);
    }

    std::unique_ptr<IRenderTargetRenderer> HtmlDomRenderer::CreateRenderTarget2DEXT(
        int w, int h, int depthFormat, bool preserveContents, bool mipMap,
        int multiSampleCount, int surfaceFormat)
    {
        if (surfaceFormat != 0)
            throw std::runtime_error(
                "HTML_DOM renderer: RenderTarget2D supports only SurfaceFormat::Color.");
        return CreateRenderTarget2D(
            w, h, depthFormat, preserveContents, mipMap, multiSampleCount);
    }

    void HtmlDomRenderer::SetRenderTarget2D(IRenderTargetRenderer* rt)
    {
        if (rt)
        {
            if (dynamic_cast<HtmlDomRenderTargetRenderer*>(rt) == nullptr)
                throw std::runtime_error(
                    "HTML_DOM renderer: refusing to bind a render target owned by another renderer.");
            rt->BindAsRenderTarget();
        }
        else
        {
            SetBoundRenderTargetIdEXT(0);
        }
    }

    void HtmlDomRenderer::SetRenderTargets(
        const RenderTargetBindingDescriptor* renderTargets, int count)
    {
        if (count < 0)
            throw System::ArgumentOutOfRangeException(
                "count", std::to_string(count), "count must not be negative.");
        // plan_html_dom.md HTMLDOM-120: GraphicsDevice::SetRenderTargets (the shared layer, the
        // only caller a real game ever goes through) always passes count=0 for a null pointer
        // (GraphicsDevice.cpp), so this can only fire via a direct call to this renderer bypassing
        // the shared layer entirely -- exactly the case this ticket audits. Without this check,
        // `renderTargets[0]` below dereferences a null pointer.
        if (count > 0 && renderTargets == nullptr)
            throw System::ArgumentNullException(
                "renderTargets", "count was " + std::to_string(count) + " but renderTargets was null.");
        if (count > 1)
            throw std::runtime_error(
                "HTML_DOM renderer: multiple simultaneous render targets (MRT) are not yet "
                "implemented -- requested " + std::to_string(count) + ". A single Canvas2D context "
                "backs each target here, and it is inherently single-output.");
        if (count > 0 && renderTargets[0].IsRenderTargetCubeFace())
            throw std::runtime_error(
                "HTML_DOM renderer: RenderTargetCube face bindings are not yet implemented.");
        SetRenderTarget2D(count > 0 ? renderTargets[0].GetRenderTarget2D() : nullptr);
    }

    void HtmlDomRenderer::ReadBackbuffer(int x, int y, int w, int h, uint8_t* pixels)
    {
        if (GetBoundRenderTargetIdEXT() == 0)
            throw std::runtime_error(
                "HTML_DOM renderer: reading back the backbuffer is not yet implemented, and cannot "
                "be: the backbuffer here is a live DOM subtree composited by the browser, and no "
                "browser API rasterizes one to pixels. Render into a RenderTarget2D and read that "
                "instead, or use the CANVAS renderer when backbuffer readback is required.");
        // plan_html_dom.md HTMLDOM-120: GraphicsDevice::GetBackBufferData (the shared layer) already
        // validates a null buffer and bounds-checks the rectangle against PresentationParameters --
        // but NOT against the actually-bound render target's own real size, which can legitimately
        // be smaller (a too-large region there silently reads back transparent padding rather than
        // throwing, since getImageData does not reject it -- a narrower, accepted gap this specific
        // check does not attempt to close). This guards the direct-renderer case the shared layer's
        // own checks cannot reach at all, and the universally-safe invariants regardless of target
        // size, mirroring HtmlDomRenderTargetRenderer::GetData's own pattern: a null `pixels` here
        // would otherwise write into the start of the wasm heap via HEAPU8.set (CNA_HtmlDom_ReadBound
        // below), real memory corruption, not a clean crash.
        System::ArgumentNullException::ThrowIfNull(pixels, "pixels");
        System::ArgumentOutOfRangeException::ThrowIfNegative(x, "x");
        System::ArgumentOutOfRangeException::ThrowIfNegative(y, "y");
        System::ArgumentOutOfRangeException::ThrowIfNegativeOrZero(w, "w");
        System::ArgumentOutOfRangeException::ThrowIfNegativeOrZero(h, "h");
#if defined(__EMSCRIPTEN__)
        if (CNA_HtmlDom_ReadBound(x, y, w, h, pixels) == 0)
            throw std::runtime_error("HTML_DOM renderer: the bound render target could not be read back.");
#else
        (void)x; (void)y; (void)w; (void)h; (void)pixels;
        throw std::runtime_error("HTML_DOM renderer: no browser, so no pixels to read back.");
#endif
    }

    void HtmlDomRenderer::SetScissorRect(int x, int y, int w, int h)
    {
#if defined(__EMSCRIPTEN__)
        CNA_HtmlDom_SetScissorRect(x, y, w, h);
#else
        (void)x; (void)y; (void)w; (void)h;
#endif
    }

    void HtmlDomRenderer::SetViewport(int x, int y, int w, int h, float /*minDepth*/, float /*maxDepth*/)
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

    void HtmlDomRenderer::ApplyBlendState(int colorSrcBlend, int alphaSrcBlend,
                                                 int colorDstBlend, int alphaDstBlend,
                                                 int colorBlendFunc, int alphaBlendFunc,
                                                 const BlendWriteState& writeState)
    {
        for (int target = 0; target < 4; ++target)
        {
            if (writeState.colorWriteChannels[target] != 15)
                throw std::runtime_error(
                    "HTML_DOM renderer: non-default ColorWriteChannels on render target " +
                    std::to_string(target) + " are unsupported by CSS compositing.");
        }
        if (writeState.multiSampleMask != 0xFFFFFFFFu)
            throw std::runtime_error(
                "HTML_DOM renderer: BlendState.MultiSampleMask is unsupported (this renderer has "
                "no multisample storage).");
        SetCurrentCompositeOpEXT(BlendStateToDomCompositeOp(
            colorSrcBlend, alphaSrcBlend, colorDstBlend, alphaDstBlend, colorBlendFunc, alphaBlendFunc));
    }

    void HtmlDomRenderer::ApplyDepthStencilState(
        bool depthEnable, bool depthWriteEnable, int,
        bool stencilEnable, int, int, int, int, int, int, int,
        bool, int, int, int, int)
    {
        if (depthEnable || depthWriteEnable || stencilEnable)
            ThrowNo3D("ApplyDepthStencilState");
    }

    void HtmlDomRenderer::SetReferenceStencil(int value)
    {
        if (value != 0)
            ThrowNo3D("SetReferenceStencil");
    }

    void HtmlDomRenderer::ApplyRasterizerState(int cullMode, int fillMode,
                                                      bool scissorTestEnable,
                                                      float depthBias, float slopeScaleDepthBias)
    {
        if (cullMode != 0 && cullMode != 2)
            ThrowNo3D("RasterizerState.CullMode::CullClockwiseFace");
        if (fillMode != 0)
            ThrowNo3D("RasterizerState.FillMode::WireFrame");
        if (depthBias != 0.0f || slopeScaleDepthBias != 0.0f)
            ThrowNo3D("RasterizerState depth bias");
        SetCurrentScissorEnableEXT(scissorTestEnable);
    }

    void HtmlDomRenderer::Ensure3DSupported(const char* operation) const
    {
        ThrowNo3D(operation ? operation : "3D operation");
    }

    void HtmlDomRenderer::ClearColorAndDepth(float, float, float, float, float) { ThrowNo3D("ClearColorAndDepth (3D)"); }
    void HtmlDomRenderer::ClearDepth(float) { ThrowNo3D("ClearDepth (3D)"); }
    void HtmlDomRenderer::ClearStencil(int) { ThrowNo3D("ClearStencil (3D)"); }
    void HtmlDomRenderer::ClearDepthAndStencil(float, int) { ThrowNo3D("ClearDepthAndStencil (3D)"); }
    void HtmlDomRenderer::ClearColorAndStencil(float, float, float, float, int) { ThrowNo3D("ClearColorAndStencil (3D)"); }
    void HtmlDomRenderer::ClearColorDepthAndStencil(float, float, float, float, float, int) { ThrowNo3D("ClearColorDepthAndStencil (3D)"); }
    void HtmlDomRenderer::SetDepthTestEnabled(bool) { ThrowNo3D("SetDepthTestEnabled (3D)"); }
    void HtmlDomRenderer::SetBlendEnabled(bool) { ThrowNo3D("SetBlendEnabled (3D)"); }
    void HtmlDomRenderer::SetDepthWriteEnabled(bool) { ThrowNo3D("SetDepthWriteEnabled (3D)"); }

    std::unique_ptr<IVertexBufferRenderer> HtmlDomRenderer::CreateVertexBuffer(int)
    {
        ThrowNo3D("CreateVertexBuffer (3D)");
    }

    std::unique_ptr<IIndexBufferRenderer> HtmlDomRenderer::CreateIndexBuffer16(int)
    {
        ThrowNo3D("CreateIndexBuffer16 (3D)");
    }

    void HtmlDomRenderer::DrawColoredPrimitives(const IVertexBufferRenderer&,
                                                       const Matrix&, const Matrix&, const Matrix&,
                                                       PrimitiveType, int) { ThrowNo3D("DrawColoredPrimitives (3D)"); }

    void HtmlDomRenderer::DrawIndexedColoredPrimitives(const IVertexBufferRenderer&, const IIndexBufferRenderer&,
                                                              const Matrix&, const Matrix&, const Matrix&,
                                                              PrimitiveType, int) { ThrowNo3D("DrawIndexedColoredPrimitives (3D)"); }
}

namespace CNA::Internal::Renderers
{
    // plan_runtimerenderer.md design decision 4: declared in this family's own
    // namespace so several renderer archives can link into one binary, then defined
    // below with a qualified name -- the body keeps its place unchanged.
    namespace HtmlDom { std::unique_ptr<IGraphicsRenderer> CreateGraphicsRenderer(const GraphicsRendererCreateArgs& args); }

    std::unique_ptr<IGraphicsRenderer> HtmlDom::CreateGraphicsRenderer(const GraphicsRendererCreateArgs& args)
    {
        return std::make_unique<HtmlDom::HtmlDomRenderer>(
            args.window, args.virtualWidth, args.virtualHeight, args.presentationMode);
    }
}
