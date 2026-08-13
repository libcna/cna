// SPDX-License-Identifier: MS-PL
#include "CNA/Internal/Renderers/HtmlDom/HtmlDomSpriteBatchRenderer.hpp"
#include "CNA/Internal/Renderers/HtmlDom/HtmlDomTextureRenderer.hpp"
#include "CNA/Internal/Renderers/HtmlDom/HtmlDomRenderTargetRenderer.hpp"

#include "Microsoft/Xna/Framework/Graphics/Effect.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>

#if defined(__EMSCRIPTEN__)
#include <emscripten.h>

// plan_html_dom.md design decision 5 (HTMLDOM-30/31/32): the whole batch crosses the wasm/JS
// boundary exactly once. Everything geometric has already been resolved on the C++ side
// (BuildDrawCommandEXT), so this walks a flat array of 20-field commands and does nothing but
// select each sprite's pooled element and write the CSS properties whose values actually changed.
//
// Why the diffing matters: `transform` and `opacity` are compositor-only properties. A frame that
// merely moves sprites writes those two and nothing else, so the browser neither lays out nor
// repaints -- it re-composites existing layers. Writing every property unconditionally, or
// rebuilding the elements, would give up exactly that. Nothing here ever READS layout (no
// getBoundingClientRect, no offsetWidth), so no forced synchronous reflow is possible either.
//
// plan_html_dom.md HTMLDOM-110: this diffing is what makes "an unchanged sprite costs zero CSS
// writes" TRUE -- it does NOT make an unchanged FRAME free of JS work. A real XNA game resubmits
// its static sprites every frame regardless, and this function still runs (cnaDomFlushCallCount)
// and still walks every command in the batch to reach that diffing in the first place; only the
// actual property WRITES (cnaDomStyleWriteCount) are what genuinely reach zero for byte-identical
// content. See docs/html-dom-renderer.md's Performance section for the corrected claim and the real
// measured numbers.
//
// Sprite n of THIS BATCH always lands on pool element n of the region THIS BATCH resolves to (see
// HTMLDOM-94 below), and elements are appended to that region's container in pool order, so
// document order within one region equals draw order within the batches that landed there.
// Painter's-algorithm ordering ACROSS different regions (including the default 'full' one) is
// realized via a per-flush `z-index` (HTMLDOM-103), not DOM position -- DOM position alone only
// ever reflected each region's own FIRST-creation order, not the actual sequence its flushes
// happened in a given frame. HTMLDOM-110: that z-index write is itself skipped for the default
// 'full' region's own sprites whenever no named region has ever existed this session (see
// needsFullRegionZIndex below) -- with nothing to interleave against, an earlier version wrote it
// on every single flush regardless, a real per-frame cost for the overwhelmingly common
// no-scissor-rect case that had nothing to do with anything actually changing.
//
// With a render target bound there is no DOM to write to (a <div> cannot render into an off-screen
// surface), so the very same command array is replayed into that target's Canvas2D context instead
// (design decision 10). The transform stack below is the exact CSS transform list, in the same
// order, so both paths place a sprite identically. HTMLDOM-102: the scissor rect IS now consulted
// on the render-target path too (a real `ctx.save()/rect()/clip()`, gated on
// RasterizerState.ScissorTestEnable) -- an earlier version left it DOM-backbuffer-only. HTMLDOM-107:
// the ACTIVE VIEWPORT's own (X,Y) offset is likewise consulted on BOTH paths -- a bound render
// target can itself have a sub-rectangle Viewport active (e.g. rendering split-screen INTO an
// intermediate texture), and nothing about either concept is DOM-backbuffer-specific.
EM_JS(void, CNA_HtmlDom_FlushSprites, (const void* cmds, int count, int stride,
                                       double m0, double m1, double m2, double m3, double m4, double m5,
                                       int hasMatrix, int scissorEnabled), {
    if (count <= 0) return;
    // plan_html_dom.md HTMLDOM-110: CNAEXT instrumentation -- every real (non-empty) flush crosses
    // the wasm/JS boundary and runs this function's own body once per SpriteBatch Begin/End batch,
    // REGARDLESS of whether any of it turns into an actual CSS write below (cnaDomStyleWriteCount
    // is the separate counter for that). A real XNA game resubmits its static sprites every frame,
    // so this count is never zero for a genuinely static scene -- direct, measured counter-evidence
    // against any claim that "no JS runs" for an unchanged frame.
    ++Module['cnaDomFlushCallCount'];
    const base = cmds >> 2;
    const targetCtx = Module['cnaDomBoundCtx'];

    // plan_html_dom.md HTMLDOM-107: this flush's viewport offset, captured ONCE per batch (the same
    // per-batch granularity the scissor rect already uses). Real XNA/FNA applies Viewport.X/Y at the
    // RASTERIZER stage, strictly AFTER the projection matrix (built from Viewport.Width/Height
    // alone) and therefore after SpriteBatch's own Begin(transformMatrix) too -- so it is the
    // OUTERMOST translation applied below, not composed into transformMatrix. #cna-dom-root itself
    // no longer moves to track viewport (HTMLDOM-107 -- it always covers the full backbuffer), so
    // this per-flush offset is the only place that translation is realized now.
    const vp = Module['cnaDomViewport'];
    const vpOffX = vp ? vp.x : 0, vpOffY = vp ? vp.y : 0;
    // plan_html_dom.md HTMLDOM-102: this flush's effective scissor rect -- null (no clip) unless
    // RasterizerState.ScissorTestEnable was actually true when this batch's End() ran, captured at
    // the same per-batch granularity as everything else scissor-related. An earlier version applied
    // SetScissorRect's recorded rect unconditionally, ignoring the enable bit entirely.
    const effectiveScissorRect = scissorEnabled ? Module['cnaDomScissorRect'] : null;

    if (targetCtx) {
        // plan_html_dom.md HTMLDOM-102: real ctx.save()/rect()/clip() scissoring for the Canvas2D
        // render-target path -- previously this path did not consult the scissor rect at all.
        // Established ONCE for the whole batch (not per sprite) in absolute target-pixel space
        // (setTransform reset first, since targetCtx's transform otherwise still holds whatever the
        // PREVIOUS sprite/flush last left it at), then every sprite's own save()/restore() below
        // nests inside it.
        targetCtx.save();
        if (effectiveScissorRect) {
            targetCtx.setTransform(1, 0, 0, 1, 0, 0);
            targetCtx.beginPath();
            targetCtx.rect(effectiveScissorRect.x, effectiveScissorRect.y,
                           effectiveScissorRect.w, effectiveScissorRect.h);
            targetCtx.clip();
        }
        for (let i = 0; i < count; ++i) {
            const o = base + i * stride;
            const flags = HEAP32[o + 14];
            const rawMode = HEAP32[o + 15];
            // HTMLDOM-100: rawMode 2 (Opaque) selects the alpha-STRIPPED variant only for the DOM
            // <div> backbuffer path below, where CSS has no operator that can erase whatever is
            // beneath a sprite while still showing the sprite's own (possibly non-255) alpha through
            // it -- full opacity is the closest a stacked, blended <div> can get, and that deviation
            // is accepted there. This Canvas2D path has a real Porter-Duff 'copy' operator, so it
            // fetches the STRAIGHT variant instead and lets 'copy' reproduce Opaque's real XNA
            // semantics exactly: BlendState.cpp confirms Opaque uses symmetric One/Zero factors for
            // BOTH colour and alpha, meaning the destination is replaced by the source pixel
            // INCLUDING its own alpha, never forced to 255 (matches plan_canvas.md CANVAS-44's own
            // 'copy' mapping, already pixel-verified).
            const isOpaque = rawMode === 2;
            const fetchMode = isOpaque ? 0 : rawMode;
            const packed = HEAPU32[o + 16];
            const isMirror = (flags & 64) !== 0;
            const rSrc = packed & 255, gSrc = (packed >>> 8) & 255, bSrc = (packed >>> 16) & 255;
            const sxRaw = HEAPF32[o + 1], syRaw = HEAPF32[o + 2];
            const sw = HEAPF32[o + 3], sh = HEAPF32[o + 4];
            // plan_html_dom.md HTMLDOM-104: a padded (edge-extended) variant when Clamp overflows the
            // texture on a non-tiled axis -- see cnaDomResolveClampVariant's own comment.
            let variant, sx = sxRaw, sy = syRaw;
            if (isMirror) {
                variant = Module['cnaDomGetMirrorVariant'](HEAP32[o], fetchMode, rSrc, gSrc, bSrc);
            } else {
                const resolved = Module['cnaDomResolveClampVariant'](HEAP32[o], fetchMode, rSrc, gSrc, bSrc,
                    sxRaw, syRaw, sw, sh, (flags & 8) !== 0, (flags & 32) !== 0);
                if (!resolved) continue;
                variant = resolved.variant;
                sx = resolved.sx; sy = resolved.sy;
            }
            if (!variant) continue;
            targetCtx.save();
            targetCtx.setTransform(1, 0, 0, 1, 0, 0);
            if (vpOffX !== 0 || vpOffY !== 0) targetCtx.translate(vpOffX, vpOffY);
            if (hasMatrix) targetCtx.transform(m0, m1, m2, m3, m4, m5);
            targetCtx.imageSmoothingEnabled = (flags & 4) !== 0;
            targetCtx.globalAlpha = ((packed >>> 24) & 255) / 255;
            targetCtx.translate(HEAPF32[o + 5], HEAPF32[o + 6]);
            const rot = HEAPF32[o + 9];
            if (rot !== 0) targetCtx.rotate(rot);
            targetCtx.scale(HEAPF32[o + 7], HEAPF32[o + 8]);
            if (flags & 1) {
                const cx = HEAPF32[o + 12];
                targetCtx.translate(cx, 0); targetCtx.scale(-1, 1); targetCtx.translate(-cx, 0);
            }
            if (flags & 2) {
                const cy = HEAPF32[o + 13];
                targetCtx.translate(0, cy); targetCtx.scale(1, -1); targetCtx.translate(0, -cy);
            }
            const lx = HEAPF32[o + 10], ly = HEAPF32[o + 11];
            if (isOpaque) {
                // plan_canvas.md CANVAS-44: Porter-Duff 'copy' is evaluated over the WHOLE
                // compositing area, not just the drawn shape -- clip to exactly the sprite's own
                // footprint (the same already-transformed local space drawImage/fillRect use below)
                // first, or it wipes every other sprite already drawn into this target to
                // transparent.
                targetCtx.beginPath();
                targetCtx.rect(lx, ly, sw, sh);
                targetCtx.clip();
                targetCtx.globalCompositeOperation = 'copy';
            } else {
                targetCtx.globalCompositeOperation = (flags & 16) !== 0 ? 'lighter' : 'source-over';
            }
            const repU = (flags & 8) !== 0, repV = (flags & 32) !== 0;
            if (repU || repV) {
                // Wrap/Mirror: CSS background tiling has no Canvas2D equivalent other than a
                // repeating pattern, offset so the tile phase matches the requested source
                // rectangle. CanvasPattern's own repetition string natively supports independent
                // per-axis repeat (HTMLDOM-97), same as background-repeat's two-value shorthand
                // below -- the mirrored variant is already a pre-tiled image, so 'repeat' on it
                // reproduces mirror-repeat by construction, no different from plain Wrap here.
                const repetition = repU && repV ? 'repeat' : repU ? 'repeat-x' : 'repeat-y';
                const pattern = targetCtx.createPattern(variant.canvas, repetition);
                if (pattern && pattern.setTransform) {
                    const dm = new DOMMatrix();
                    dm.e = lx - sx; dm.f = ly - sy;
                    pattern.setTransform(dm);
                }
                targetCtx.fillStyle = pattern;
                targetCtx.fillRect(lx, ly, sw, sh);
            } else {
                targetCtx.drawImage(variant.canvas, sx, sy, sw, sh, lx, ly, sw, sh);
            }
            targetCtx.restore();
        }
        targetCtx.restore();
        return;
    }

    const root = Module['cnaDomRoot'];
    if (!root) { console.error('[CNA] HTML_DOM: flush before the DOM surface existed'); return; }
    // plan_html_dom.md HTMLDOM-94: this flush is one whole SpriteBatch Begin/End batch, so the
    // scissor rect current RIGHT NOW (whatever the game's last SetScissorRect call recorded) is the
    // one that applies to every sprite in it -- matching real XNA/FNA SpriteBatch Deferred-mode
    // semantics, where ScissorRectangle is read by the GPU once the batch's draw calls are actually
    // issued at End(), not captured per original Draw() call. cnaDomGetRegion resolves that rect to
    // the DOM container (and its own independent sprite pool) this whole batch's sprites belong in,
    // so a LATER batch's different scissor rect can never reach back and reclip these sprites.
    // plan_html_dom.md HTMLDOM-102: passes effectiveScissorRect (null when ScissorTestEnable was
    // false at this batch's End()), not the raw recorded rect -- a disabled scissor test now
    // genuinely collapses to the same zero-cost 'full' region as no rect at all, matching real
    // XNA/FNA. An earlier version resolved the recorded rect unconditionally.
    const region = Module['cnaDomGetRegion'](effectiveScissorRect);
    const container = region.container;
    const pool = region.pool;
    let used = region.used;

    // plan_html_dom.md HTMLDOM-103: true per-flush paint order across regions, via z-index instead
    // of relying on DOM position -- DOM position only ever reflected each region's own FIRST-
    // creation order, not the sequence its flushes actually happened in this frame (interleaving a
    // 'full'-region draw between two different-region draws could never be reproduced by document
    // order alone, since 'full's sprites are direct children of #cna-dom-root, siblings of every
    // other region's own container). Every sprite already forms its own stacking context
    // (will-change:transform below), so a z-index only changes ordering AMONG those already-
    // separate contexts -- it does not change how mix-blend-mode resolves within one.
    const paintOrder = ++Module['cnaDomPaintOrderCounter'];
    const isFullRegion = container === root;
    if (!isFullRegion) {
        const z = String(paintOrder);
        if (container.style.zIndex !== z) { container.style.zIndex = z; ++Module['cnaDomStyleWriteCount']; }
    }
    // plan_html_dom.md HTMLDOM-110: per-sprite z-index on 'full'-region sprites exists ONLY to
    // interleave their paint order against sprites in OTHER regions within the same frame -- with
    // no named region ever created this session, there is nothing to interleave against (every
    // 'full' sprite already paints in correct relative order via DOM/pool position alone), so the
    // write is skipped entirely. An earlier version wrote it on EVERY flush unconditionally -- a
    // real, measured per-frame cost (instrumented via cnaDomStyleWriteCount) that directly
    // contradicted this renderer's own "nothing changes -> nothing costs anything" claim: even a
    // frame resubmitting byte-identical content wrote N CSS properties (N = sprite count) for no
    // visible effect, since equal z-index values never change relative stacking order among
    // themselves. Narrow, accepted caveat: on the exact frame a named region is FIRST created this
    // session, a 'full' flush that already ran EARLIER in that SAME frame keeps its default (no
    // z-index) stacking value, which could show through one frame off from a brand new named
    // region flushed LATER in that same frame -- a one-time transition, never recurring once any
    // named region exists, not worth a retroactive same-frame re-stamp to close completely.
    const needsFullRegionZIndex = Module['cnaDomAnyNamedRegionEverCreated'];

    for (let i = 0; i < count; ++i) {
        const o = base + i * stride;
        const packed = HEAPU32[o + 16];
        const flags = HEAP32[o + 14];
        const isMirror = (flags & 64) !== 0;
        const rSrc = packed & 255, gSrc = (packed >>> 8) & 255, bSrc = (packed >>> 16) & 255;
        const sxRaw = HEAPF32[o + 1], syRaw = HEAPF32[o + 2];
        const sw0 = HEAPF32[o + 3], sh0 = HEAPF32[o + 4];
        // plan_html_dom.md HTMLDOM-104: a padded (edge-extended) variant when Clamp overflows the
        // texture on a non-tiled axis -- see cnaDomResolveClampVariant's own comment. background-
        // position below uses the RESOLVED (possibly shifted) sx/sy, not the raw command values, so
        // it aligns against the padded image's own coordinate space when one was used.
        let variant, sx = sxRaw, sy = syRaw;
        if (isMirror) {
            variant = Module['cnaDomGetMirrorVariant'](HEAP32[o], HEAP32[o + 15], rSrc, gSrc, bSrc);
        } else {
            const resolved = Module['cnaDomResolveClampVariant'](HEAP32[o], HEAP32[o + 15], rSrc, gSrc, bSrc,
                sxRaw, syRaw, sw0, sh0, (flags & 8) !== 0, (flags & 32) !== 0);
            if (!resolved) continue;
            variant = resolved.variant;
            sx = resolved.sx; sy = resolved.sy;
        }
        if (!variant) continue;
        Module['cnaDomEnsureUrl'](variant);

        let el = pool[used];
        if (el === undefined) {
            el = document.createElement('div');
            el.className = 'cna-sprite';
            // will-change keeps each sprite on its own compositor layer, so moving it never
            // repaints. pointer-events:none keeps the sprite layer out of hit testing entirely --
            // input belongs to the platform canvas, which is still the element receiving events.
            el.style.cssText = 'position:absolute;left:0;top:0;transform-origin:0 0;' +
                               'background-repeat:no-repeat;will-change:transform;pointer-events:none;';
            el.__cnaState = { w: -1, h: -1, url: null, bp: null, rep: null, ir: null,
                              mb: null, tf: null, op: -1, hidden: false, zi: -1 };
            container.appendChild(el);
            pool[used] = el;
        }
        const st = el.__cnaState;
        const style = el.style;

        if (st.w !== sw0) { style.width = sw0 + 'px'; st.w = sw0; ++Module['cnaDomStyleWriteCount']; }
        if (st.h !== sh0) { style.height = sh0 + 'px'; st.h = sh0; ++Module['cnaDomStyleWriteCount']; }
        if (st.url !== variant.url) {
            style.backgroundImage = 'url(' + variant.url + ')'; st.url = variant.url;
            ++Module['cnaDomStyleWriteCount'];
        }

        // plan_html_dom.md HTMLDOM-104: (-sx,-sy) uses the RESOLVED source coordinates -- shifted
        // into the padded image's own coordinate space when Clamp overflow required one -- not the
        // raw command values, so the requested source rect still aligns correctly.
        const bp = (-sx) + 'px ' + (-sy) + 'px';
        if (st.bp !== bp) { style.backgroundPosition = bp; st.bp = bp; ++Module['cnaDomStyleWriteCount']; }

        // Two-value background-repeat shorthand: one repetition style per axis, independent of
        // each other (HTMLDOM-97) -- 'no-repeat no-repeat' reads identically to plain 'no-repeat'.
        const rep = ((flags & 8) !== 0 ? 'repeat' : 'no-repeat') + ' ' +
                    ((flags & 32) !== 0 ? 'repeat' : 'no-repeat');
        if (st.rep !== rep) { style.backgroundRepeat = rep; st.rep = rep; ++Module['cnaDomStyleWriteCount']; }
        const ir = (flags & 4) !== 0 ? 'auto' : 'pixelated';
        if (st.ir !== ir) { style.imageRendering = ir; st.ir = ir; ++Module['cnaDomStyleWriteCount']; }
        const mb = (flags & 16) !== 0 ? 'plus-lighter' : 'normal';
        if (st.mb !== mb) { style.mixBlendMode = mb; st.mb = mb; ++Module['cnaDomStyleWriteCount']; }

        // HTMLDOM-107: the viewport's own offset is the OUTERMOST transform -- see this function's
        // own top-of-function comment for why it must come before (not be composed into)
        // transformMatrix.
        let tf = (vpOffX !== 0 || vpOffY !== 0) ? 'translate(' + vpOffX + 'px,' + vpOffY + 'px) ' : "";
        tf += hasMatrix
            ? 'matrix(' + m0 + ',' + m1 + ',' + m2 + ',' + m3 + ',' + m4 + ',' + m5 + ') '
            : "";
        tf += 'translate(' + HEAPF32[o + 5] + 'px,' + HEAPF32[o + 6] + 'px)';
        const rot = HEAPF32[o + 9];
        if (rot !== 0) tf += ' rotate(' + rot + 'rad)';
        const scaleX = HEAPF32[o + 7], scaleY = HEAPF32[o + 8];
        if (scaleX !== 1 || scaleY !== 1) tf += ' scale(' + scaleX + ',' + scaleY + ')';
        if (flags & 1) {
            const cx = HEAPF32[o + 12];
            tf += ' translate(' + cx + 'px,0) scale(-1,1) translate(' + (-cx) + 'px,0)';
        }
        if (flags & 2) {
            const cy = HEAPF32[o + 13];
            tf += ' translate(0,' + cy + 'px) scale(1,-1) translate(0,' + (-cy) + 'px)';
        }
        const lx = HEAPF32[o + 10], ly = HEAPF32[o + 11];
        if (lx !== 0 || ly !== 0) tf += ' translate(' + lx + 'px,' + ly + 'px)';
        if (st.tf !== tf) { style.transform = tf; st.tf = tf; ++Module['cnaDomStyleWriteCount']; }

        const opacity = ((packed >>> 24) & 255) / 255;
        if (st.op !== opacity) { style.opacity = opacity; st.op = opacity; ++Module['cnaDomStyleWriteCount']; }
        // HTMLDOM-103: the 'full' region has no container of its own to stamp a single z-index on
        // (its sprites are direct children of #cna-dom-root, siblings of every named region's own
        // container) -- so each of ITS sprites carries the current flush's paint order individually.
        // HTMLDOM-110: skipped entirely when no named region has ever existed this session -- see
        // needsFullRegionZIndex's own comment above.
        if (isFullRegion && needsFullRegionZIndex && st.zi !== paintOrder) {
            style.zIndex = paintOrder; st.zi = paintOrder; ++Module['cnaDomStyleWriteCount'];
        }
        if (st.hidden) { style.display = ""; st.hidden = false; ++Module['cnaDomStyleWriteCount']; }
        ++used;
    }
    region.used = used;
});
#endif

namespace CNA::Internal::Renderers::HtmlDom
{
    using namespace Microsoft::Xna::Framework;
    using namespace Microsoft::Xna::Framework::Graphics;

    namespace
    {
        // Sibling concrete classes rather than a subclass relationship (IRenderTargetRenderer and
        // HtmlDomTextureRenderer descend from ITextureRenderer along two separate branches), and this
        // renderer's "native handle" is a JS-side integer with no shared accessor on ITextureRenderer
        // to expose it -- so a contained dynamic_cast against the two known concrete types is the
        // safe equivalent here, the same conclusion CANVAS and the native 2D renderer reached.
        int TextureIdOf(const ITextureRenderer& texture)
        {
            if (const auto* t = dynamic_cast<const HtmlDomTextureRenderer*>(&texture)) return t->GetCanvasId();
            if (const auto* rt = dynamic_cast<const HtmlDomRenderTargetRenderer*>(&texture)) return rt->GetCanvasId();
            return 0;
        }
    }

    HtmlDomDrawCommand BuildDrawCommandEXT(int textureId,
                                           int textureWidth, int textureHeight,
                                           const Rectangle& dest,
                                           const Rectangle& source,
                                           const Color& color,
                                           float rotation,
                                           const Vector2& origin,
                                           SpriteEffects effects,
                                           bool smoothing,
                                           int addressU, int addressV,
                                           DomCompositeOp op)
    {
        const float sourceX = static_cast<float>(source.X);
        const float sourceY = static_cast<float>(source.Y);
        const float sourceW = static_cast<float>(source.Width);
        const float sourceH = static_cast<float>(source.Height);

        const bool exceedsBounds = source.X < 0 || source.Y < 0 ||
                                   source.X + source.Width > textureWidth ||
                                   source.Y + source.Height > textureHeight;
        ValidateAddressModes(addressU, addressV, exceedsBounds);
        // HTMLDOM-97: tiling is now per-axis (Wrap or Mirror on THAT axis specifically), not a
        // single whole-rect decision keyed off addressU alone -- a mixed U=Wrap/V=Clamp draw tiles
        // its U extent and clamps its V extent independently. ValidateAddressModes above already
        // guarantees addressU==addressV whenever either is Mirror, so `mirror` below is exact.
        const bool tiledU = exceedsBounds && (addressU == 0 || addressU == 2);
        const bool tiledV = exceedsBounds && (addressV == 0 || addressV == 2);
        const bool mirror = exceedsBounds && addressU == 2 && addressV == 2;

        // plan_html_dom.md HTMLDOM-104: Clamp (a non-tiled axis whose source rect still exceeds the
        // texture) samples the nearest EDGE TEXEL for the out-of-bounds portion -- it does not crop
        // destination geometry. An earlier version narrowed the source rect into the texture and
        // shifted the destination box to match, which cropped the sprite's own footprint (leaving
        // the removed area transparent) instead of showing the edge-replicated colour real XNA/D3D
        // produce there -- confirmed wrong by the audit that reopened this task, and by the existing
        // HTMLDOM-97b pixel test itself explicitly asserting the (wrong) transparent-crop result as
        // correct. The texture-pixel padding needed on each non-tiled, out-of-bounds edge is
        // computed here so the JS flush can fetch a cached, edge-extended texture variant
        // (Module['cnaDomGetPaddedVariant'], resolved via cnaDomResolveClampVariant) sized to it --
        // sx/sy/sw/sh below stay the RAW, unclamped source rect; only the JS-side variant image
        // itself grows to cover the overflow, sourceRectangle. destRect/origin math is therefore
        // completely unaffected by Clamp -- exactly as real XNA behaves.
        const float texW = static_cast<float>(textureWidth);
        const float texH = static_cast<float>(textureHeight);
        const float padLeft = (exceedsBounds && !tiledU) ? std::max(0.0f, -sourceX) : 0.0f;
        const float padRight = (exceedsBounds && !tiledU) ? std::max(0.0f, sourceX + sourceW - texW) : 0.0f;
        const float padTop = (exceedsBounds && !tiledV) ? std::max(0.0f, -sourceY) : 0.0f;
        const float padBottom = (exceedsBounds && !tiledV) ? std::max(0.0f, sourceY + sourceH - texH) : 0.0f;
        // Honest boundary, not silent cropping: beyond this many texture pixels of edge-extension on
        // any one side, reject the draw outright rather than approximate it. Generous for the real
        // use case (a few pixels of atlas-seam bleed margin, or a modest out-of-bounds sample), while
        // keeping the cached padded-canvas variants this generates bounded in size.
        constexpr float kMaxClampPadding = 256.0f;
        if (padLeft > kMaxClampPadding || padRight > kMaxClampPadding ||
            padTop > kMaxClampPadding || padBottom > kMaxClampPadding)
        {
            throw std::runtime_error(
                "HTML_DOM renderer: TextureAddressMode::Clamp sourceRectangle overflow exceeds " +
                std::to_string(static_cast<int>(kMaxClampPadding)) + " texture pixels on at least "
                "one edge (left=" + std::to_string(padLeft) + ", top=" + std::to_string(padTop) +
                ", right=" + std::to_string(padRight) + ", bottom=" + std::to_string(padBottom) +
                "). Rejected rather than silently cropping the sprite's own destination geometry -- "
                "keep sourceRectangle closer to the texture's own bounds.");
        }

        HtmlDomDrawCommand cmd{};
        cmd.textureId = textureId;
        cmd.sx = sourceX;
        cmd.sy = sourceY;
        cmd.sw = sourceW;
        cmd.sh = sourceH;
        cmd.destX = static_cast<float>(dest.X);
        cmd.destY = static_cast<float>(dest.Y);
        cmd.scaleX = sourceW != 0.0f ? static_cast<float>(dest.Width) / sourceW : 0.0f;
        cmd.scaleY = sourceH != 0.0f ? static_cast<float>(dest.Height) / sourceH : 0.0f;
        cmd.rotation = rotation;
        cmd.localX = -origin.X;
        cmd.localY = -origin.Y;
        // Flip mirrors about the sprite's OWN centre -- not the pivot, and not the coordinate
        // system -- so the destination footprint is unchanged by flipping, matching real XNA/FNA
        // semantics where SpriteEffects only changes which source corner maps to which (unchanged)
        // destination corner. The unclamped source size defines that centre.
        cmd.flipCenterX = -origin.X + sourceW * 0.5f;
        cmd.flipCenterY = -origin.Y + sourceH * 0.5f;

        int flags = 0;
        if ((static_cast<int>(effects) & static_cast<int>(SpriteEffects::FlipHorizontally)) != 0)
            flags |= FlagFlipHorizontally;
        if ((static_cast<int>(effects) & static_cast<int>(SpriteEffects::FlipVertically)) != 0)
            flags |= FlagFlipVertically;
        if (smoothing) flags |= FlagSmoothing;
        if (tiledU) flags |= FlagWrap;
        if (tiledV) flags |= FlagWrapV;
        if (mirror) flags |= FlagMirror;
        if (op == DomCompositeOp::Additive) flags |= FlagAdditive;
        cmd.flags = flags;

        cmd.variantMode = VariantModeFor(op);
        cmd.packedColor = static_cast<std::uint32_t>(color.getRProperty())
                        | (static_cast<std::uint32_t>(color.getGProperty()) << 8)
                        | (static_cast<std::uint32_t>(color.getBProperty()) << 16)
                        | (static_cast<std::uint32_t>(color.getAProperty()) << 24);
        return cmd;
    }

    void HtmlDomSpriteBatchRenderer::Begin()
    {
        begun_ = true;
        commands_.clear();
    }

    void HtmlDomSpriteBatchRenderer::End()
    {
        begun_ = false;
#if defined(__EMSCRIPTEN__)
        CNA_HtmlDom_FlushSprites(commands_.data(), static_cast<int>(commands_.size()),
                                 HtmlDomDrawCommandFields,
                                 matrix_[0], matrix_[1], matrix_[2], matrix_[3], matrix_[4], matrix_[5],
                                 hasMatrix_ ? 1 : 0, GetCurrentScissorEnableEXT() ? 1 : 0);
#endif
        commands_.clear();
        hasMatrix_ = false;
    }

    void HtmlDomSpriteBatchRenderer::SetTransformMatrix(const Matrix& m)
    {
        // XNA/FNA Matrix is row-major (row-vector convention: v' = v * M); CSS matrix(a,b,c,d,e,f)
        // defines x' = a*x + c*y + e, y' = b*x + d*y + f. Matching terms gives a=M11, b=M12, c=M21,
        // d=M22, e=M41, f=M42 -- the same mapping Canvas2D's setTransform needs.
        matrix_[0] = m.M11; matrix_[1] = m.M12;
        matrix_[2] = m.M21; matrix_[3] = m.M22;
        matrix_[4] = m.M41; matrix_[5] = m.M42;
        hasMatrix_ = !(m.M11 == 1.0f && m.M12 == 0.0f && m.M21 == 0.0f &&
                       m.M22 == 1.0f && m.M41 == 0.0f && m.M42 == 0.0f);
    }

    void HtmlDomSpriteBatchRenderer::SetCustomEffect(Effect* effect)
    {
        if (effect != nullptr)
            throw std::runtime_error(
                "HTML_DOM renderer: custom SpriteBatch Effects are not yet implemented. CSS "
                "compositing has no programmable shader stage for one to run in.");
    }

    void HtmlDomSpriteBatchRenderer::SetSamplerFilter(int textureFilter)
    {
        switch (textureFilter)
        {
            case 0: smoothingEnabled_ = true; break;  // Linear
            case 1: smoothingEnabled_ = false; break; // Point
            default:
                throw std::runtime_error(
                    "HTML_DOM renderer: only TextureFilter::Linear and TextureFilter::Point are "
                    "supported. Anisotropic and mip/min-mag-split filters cannot be represented "
                    "by one CSS/Canvas2D smoothing toggle.");
        }
    }

    void HtmlDomSpriteBatchRenderer::SetSamplerAddressMode(int addressU, int addressV)
    {
        if (addressU < 0 || addressU > 2 || addressV < 0 || addressV > 2)
            throw std::runtime_error(
                "HTML_DOM renderer: TextureAddressMode must be Wrap, Clamp, or Mirror.");
        addressU_ = addressU;
        addressV_ = addressV;
    }

    void HtmlDomSpriteBatchRenderer::QueueDraw(const ITextureRenderer& texture,
                                              const Rectangle& destinationRectangle,
                                              const Rectangle& sourceRectangle,
                                              const Color& color,
                                              float rotation,
                                              const Vector2& origin,
                                              SpriteEffects effects)
    {
        if (!begun_)
            throw std::runtime_error("HtmlDomSpriteBatchRenderer::Draw called before Begin().");
        const int id = TextureIdOf(texture);
        if (id == 0) return;
        if (sourceRectangle.Width <= 0 || sourceRectangle.Height <= 0 ||
            destinationRectangle.Width == 0 || destinationRectangle.Height == 0)
            return;

        const HtmlDomDrawCommand cmd = BuildDrawCommandEXT(
            id, texture.GetWidth(), texture.GetHeight(), destinationRectangle, sourceRectangle,
            color, rotation, origin, effects, smoothingEnabled_, addressU_, addressV_,
            GetCurrentCompositeOpEXT());

        // plan_html_dom.md HTMLDOM-118: SpriteSortMode::Immediate means THIS sprite's own draw call
        // reaches the DOM/Canvas2D right now, under whatever device state is current AT THIS
        // INSTANT -- not deferred until End(), which could see a LATER state some subsequent
        // SetScissorRect()/SetViewport() call already overwrote. Flushing this one command as its
        // own one-sprite "batch", synchronously, is what makes that true with no extra per-draw
        // state capture needed here: every piece of global JS state CNA_HtmlDom_FlushSprites itself
        // reads (Module['cnaDomScissorRect']/['cnaDomViewport'], GetCurrentCompositeOpEXT() above,
        // GetCurrentScissorEnableEXT() below) is always whatever the game's last SetScissorRect()/
        // SetViewport()/ApplyBlendState() call recorded, updated eagerly and independently of any
        // SpriteBatch batching -- so reading it right now, synchronously, already gives the correct
        // per-draw answer. Never queued into commands_ in this mode: an Immediate command has
        // already reached its destination the instant this returns, so leaving it in commands_ too
        // would flush it a second time at End().
        if (immediateMode_)
        {
#if defined(__EMSCRIPTEN__)
            CNA_HtmlDom_FlushSprites(&cmd, 1, HtmlDomDrawCommandFields,
                                     matrix_[0], matrix_[1], matrix_[2], matrix_[3], matrix_[4], matrix_[5],
                                     hasMatrix_ ? 1 : 0, GetCurrentScissorEnableEXT() ? 1 : 0);
#endif
            return;
        }
        commands_.push_back(cmd);
    }

    void HtmlDomSpriteBatchRenderer::Draw(const ITextureRenderer& texture, float x, float y)
    {
        const Rectangle destRect(static_cast<int>(x), static_cast<int>(y),
                                 texture.GetWidth(), texture.GetHeight());
        const Rectangle srcRect(0, 0, texture.GetWidth(), texture.GetHeight());
        QueueDraw(texture, destRect, srcRect, Color(255, 255, 255, 255), 0.0f, Vector2(0, 0),
                  SpriteEffects::None);
    }

    void HtmlDomSpriteBatchRenderer::Draw(const ITextureRenderer& texture,
                                         const Rectangle& destinationRectangle,
                                         const Rectangle& sourceRectangle,
                                         const Color& color)
    {
        QueueDraw(texture, destinationRectangle, sourceRectangle, color, 0.0f, Vector2(0, 0),
                  SpriteEffects::None);
    }

    void HtmlDomSpriteBatchRenderer::Draw(const ITextureRenderer& texture,
                                         const Rectangle& destinationRectangle,
                                         const Rectangle& sourceRectangle,
                                         const Color& color,
                                         float rotation,
                                         const Vector2& origin,
                                         SpriteEffects effects,
                                         float layerDepth)
    {
        // layerDepth is deliberately unused: SpriteSortMode sorting happens entirely in the shared
        // SpriteBatch layer, and this renderer realizes the resulting order as DOM document order.
        (void)layerDepth;
        QueueDraw(texture, destinationRectangle, sourceRectangle, color, rotation, origin, effects);
    }
}
