// SPDX-License-Identifier: MS-PL
#include "CNA/Internal/Renderers/HtmlDom/HtmlDomTextureRenderer.hpp"

#include "System/ArgumentOutOfRangeException.hpp"

#include <cstring>
#include <limits>
#include <stdexcept>
#include <string>

#if defined(__EMSCRIPTEN__)
#include <emscripten.h>

// plans/plan_html_dom.md HTMLDOM-23 / design decision 7: installs the two JS helpers the draw paths use
// to turn a texture's pixels into something CSS (or a render target's Canvas2D context) can draw.
//
// They are installed onto `Module` rather than declared as their own EM_JS functions on purpose:
// an EM_JS function that no C++ code ever calls can be dropped by the linker's dead-code
// elimination, which would leave the JS-to-JS calls in the draw path referring to a name that does
// not exist at runtime. This installer IS called from C++ (both constructors below), so it and the
// closures it defines always survive linking.
//
// `cnaDomGetVariant(id, mode, r, g, b)` returns the cached form of a texture's pixels for one blend
// mode and tint, generating it on first use:
//
//   mode 0 = straight        -- the pixels exactly as uploaded (NonPremultiplied, Additive, and
//                               HTMLDOM-100: Opaque on the Canvas2D render-target path, which
//                               reproduces real XNA's alpha-included replace via 'copy' instead)
//   mode 1 = un-premultiplied -- RGB divided by alpha (AlphaBlend's srcBlend=One contract)
//   mode 2 = alpha-stripped   -- every alpha forced to 255; Opaque on the DOM <div> backbuffer path
//                               ONLY, an accepted deviation since CSS has no operator that can
//                               replace a stacked element's destination while still showing a
//                               partial source alpha through it
//
// and, on top of any of those, RGB multiplied by the tint. The maths runs directly over the pixel
// array -- exact, with alpha read and written back completely untouched in every case -- rather
// than through composite-operation tricks, whose algebra CANVAS's own external review found to be
// silently wrong at semi-transparent, non-white edge pixels.
//
// The untinted straight variant is the base canvas itself, not a copy: it is by far the most common
// case (an ordinary untinted sprite) and copying it would double every texture's memory for nothing.
//
// plans/plan_html_dom.md HTMLDOM-106: mode 1's "RGB divided by alpha" is only correct for an UPLOADED
// texture, whose bytes are the game's own and are, by XNA/FNA's AlphaBlend convention, assumed
// already premultiplied. A render target's own backing canvas is never in that state: any Canvas2D
// read or redraw of it (getImageData, or using it as a drawImage source) is guaranteed straight
// (non-premultiplied) alpha regardless of which blend mode drew the content -- a browser contract,
// not something this renderer controls. Requesting mode 1 for a render target would therefore divide
// already-straight bytes by alpha a second time; cnaDomGetVariant silently downgrades that request
// to mode 0 for any entry marked isRenderTarget.
//
// `cnaDomEnsureUrl(variant)` pays for the PNG encode the DOM path needs, once per variant.
// `toDataURL` is the only SYNCHRONOUS canvas-to-URL route a browser offers -- `toBlob`/
// `convertToBlob` are asynchronous and would let a draw be issued before its own texture URL
// existed. The render-target draw path never calls this: it draws the variant's canvas directly.
//
EM_JS(void, CNA_HtmlDom_InstallTextureHelpers, (), {
    if (Module['cnaDomGetVariant']) return;

    // plans/plan_html_dom.md HTMLDOM-109: memory-owning variants (everything except the one free, shared,
    // untinted-straight base variant below) live in ONE global `Module['cnaDomVariantCache']` -- a
    // real `Map`, keyed `id + ':' + key`, whose OWN insertion order is the recency order this cache
    // relies on. A cache HIT deletes and re-inserts its entry (`cnaDomVariantCacheGet`), which is
    // how "was just used" becomes "youngest" in O(1) -- the previous design pushed a new LRU-array
    // record on every CREATION only, so a hot variant that was never evicted still aged out from
    // under sustained use by OTHER variants, a real FIFO wearing an LRU's name. Each Map value also
    // carries its OWNING texture entry, so `cnaDomVariantCacheClearOwner` (called from every place
    // that used to just reset `entry.variants = {}` -- SetData/UpdateTexture, render-target bind
    // invalidation, texture destruction) can remove exactly that texture's own live cache records in
    // one pass, rather than leaving them to rot as stale `(id,key)` pairs that could later delete a
    // freshly regenerated variant sharing the same key. The cache is capped at 256 total entries; a
    // full scan during `cnaDomVariantCacheClearOwner` is bounded by that same cap and only ever runs
    // on these rare, non-per-frame lifecycle events, never per draw.
    Module['cnaDomVariantCacheGet'] = function(id, key) {
        const cache = Module['cnaDomVariantCache'];
        if (!cache) return null;
        const combined = id + ':' + key;
        const hit = cache.get(combined);
        if (!hit) return null;
        // Re-inserting moves this key to the END of the Map's iteration order -- the youngest slot
        // -- without touching any other entry's relative order. O(1) amortized, same as the lookup.
        cache.delete(combined);
        cache.set(combined, hit);
        return hit.variant;
    };

    Module['cnaDomVariantCachePut'] = function(owner, id, key, variant) {
        if (!Module['cnaDomVariantCache']) Module['cnaDomVariantCache'] = new Map();
        const cache = Module['cnaDomVariantCache'];
        cache.set(id + ':' + key, { owner: owner, variant: variant });
        if (cache.size > 256) {
            // Map iteration is insertion order, so the FIRST key is the least-recently-touched one
            // -- exactly the LRU eviction candidate, found in O(1) with no separate age bookkeeping.
            const oldestKey = cache.keys().next().value;
            cache.delete(oldestKey);
        }
    };

    Module['cnaDomVariantCacheClearOwner'] = function(owner) {
        const cache = Module['cnaDomVariantCache'];
        if (!cache) return;
        for (const combined of Array.from(cache.keys())) {
            const entry = cache.get(combined);
            if (entry && entry.owner === owner) cache.delete(combined);
        }
    };

    Module['cnaDomGetVariant'] = function(id, mode, r, g, b) {
        const entry = Module['cnaDomTextures'] && Module['cnaDomTextures'][id];
        if (!entry || !entry.ctx) return null;
        // plans/plan_html_dom.md HTMLDOM-106: a render target's own canvas is always straight (non-
        // premultiplied) alpha on ANY Canvas2D read or redraw -- getImageData/drawImage's own
        // contract, unaffected by whatever blend mode drew the content -- whereas an UPLOADED
        // texture's bytes are the game's own and are, by XNA/FNA's AlphaBlend convention, assumed
        // already premultiplied (mode 1 exists specifically to undo that before handing the pixels
        // to the browser's straight-alpha-expecting compositor). Un-premultiplying a render target's
        // already-straight bytes a second time would corrupt its colour at every non-opaque texel,
        // so AlphaBlend sampling a render target is treated as mode 0 (already straight, as-is).
        if (entry.isRenderTarget && mode === 1) mode = 0;
        const tinted = (r !== 255 || g !== 255 || b !== 255);

        // plans/plan_html_dom.md HTMLDOM-109: the untinted straight variant is the base canvas itself, not
        // a copy -- by far the most common case (an ordinary untinted sprite) -- so it is memoized
        // directly on the entry, OUTSIDE the capped global cache entirely. It owns no memory of its
        // own and must never compete with real (memory-owning) variants for one of the 256 slots.
        if (mode === 0 && !tinted) {
            if (!entry.sharedBaseVariant) {
                entry.sharedBaseVariant = { canvas: entry.canvas, url: null, shared: true };
            }
            return entry.sharedBaseVariant;
        }

        const key = mode + ':' + r + ',' + g + ',' + b;
        const cached = Module['cnaDomVariantCacheGet'](id, key);
        if (cached) return cached;

        const w = entry.w, h = entry.h;
        const img = entry.ctx.getImageData(0, 0, w, h);
        const data = img.data;
        for (let i = 0; i < data.length; i += 4) {
            let rr = data[i], gg = data[i + 1], bb = data[i + 2];
            const aa = data[i + 3];
            if (mode === 1 && aa > 0 && aa < 255) {
                const inv = 255 / aa;
                rr = Math.min(255, rr * inv);
                gg = Math.min(255, gg * inv);
                bb = Math.min(255, bb * inv);
            }
            if (tinted) {
                rr = (rr * r) / 255;
                gg = (gg * g) / 255;
                bb = (bb * b) / 255;
            }
            data[i] = rr; data[i + 1] = gg; data[i + 2] = bb;
            if (mode === 2) data[i + 3] = 255;
        }
        const canvas = Module['cnaDomNewCanvas'](w, h);
        if (!canvas) return null;
        canvas.getContext('2d').putImageData(img, 0, 0);
        const variant = { canvas: canvas, url: null, shared: false };
        Module['cnaDomVariantCachePut'](entry, id, key, variant);
        return variant;
    };

    // plans/plan_html_dom.md HTMLDOM-97: TextureAddressMode::Mirror (symmetric on both axes) has no CSS
    // background-repeat equivalent, so it is emulated the same way CANVAS's own equivalent gap was
    // closed (plans/plan_canvas.md CANVAS-44): a lazily-built, cached 2x2 tile where the source variant's
    // pixels are drawn once per quadrant, alternate quadrants horizontally/vertically flipped.
    // Tiling THAT image with ordinary CSS/CanvasPattern 'repeat' reproduces mirror-repeat exactly,
    // by construction -- repeating a 2x2 mirror tile at period 2*width/2*height IS mirror-repeat.
    // Cached in the SAME global variant cache as plain variants (keyed "mirror:<mode>:<r>,<g>,<b>"),
    // so eviction and lifecycle cleanup are unified rather than duplicated bookkeeping.
    Module['cnaDomGetMirrorVariant'] = function(id, mode, r, g, b) {
        const entry = Module['cnaDomTextures'] && Module['cnaDomTextures'][id];
        if (!entry || !entry.ctx) return null;
        const key = 'mirror:' + mode + ':' + r + ',' + g + ',' + b;
        const cached = Module['cnaDomVariantCacheGet'](id, key);
        if (cached) return cached;

        const base = Module['cnaDomGetVariant'](id, mode, r, g, b);
        if (!base) return null;
        const w = entry.w, h = entry.h;
        const canvas = Module['cnaDomNewCanvas'](w * 2, h * 2);
        if (!canvas) return null;
        const ctx = canvas.getContext('2d');
        ctx.drawImage(base.canvas, 0, 0);                        // top-left: as-is
        ctx.save();                                               // top-right: flipped horizontally
        ctx.translate(w * 2, 0); ctx.scale(-1, 1);
        ctx.drawImage(base.canvas, 0, 0);
        ctx.restore();
        ctx.save();                                               // bottom-left: flipped vertically
        ctx.translate(0, h * 2); ctx.scale(1, -1);
        ctx.drawImage(base.canvas, 0, 0);
        ctx.restore();
        ctx.save();                                               // bottom-right: flipped both ways
        ctx.translate(w * 2, h * 2); ctx.scale(-1, -1);
        ctx.drawImage(base.canvas, 0, 0);
        ctx.restore();

        const variant = { canvas: canvas, url: null, shared: false };
        Module['cnaDomVariantCachePut'](entry, id, key, variant);
        return variant;
    };

    // plans/plan_html_dom.md HTMLDOM-104: TextureAddressMode::Clamp with an out-of-bounds sourceRectangle
    // samples the nearest EDGE TEXEL for the overflow, not a crop -- this builds a cached, edge-
    // extended copy of a texture's own variant so the ordinary single-background-image sprite path
    // (unchanged otherwise) can sample straight through the padding as if it were real texture data.
    // `leftPad`/`topPad`/`rightPad`/`bottomPad` are in TEXTURE pixels, already validated bounded
    // (BuildDrawCommandEXT's own kMaxClampPadding) before this is ever called.
    //
    // Rounded UP to a coarse granularity so draws whose overflow amount differs only slightly (e.g.
    // sub-pixel jitter from an animating sourceRectangle) still hit the SAME cached variant instead
    // of generating a new padded canvas every frame -- the extra rounded-up padding is simply never
    // sampled if the actual overflow is smaller, so this cannot produce wrong pixels, only a
    // (bounded) larger cached canvas than the exact minimum.
    //
    // Cached in the SAME global variant cache as plain/mirror variants (keyed
    // "pad:<mode>:<r>,<g>,<b>:<leftPad>,<topPad>,<rightPad>,<bottomPad>"), so eviction and lifecycle
    // cleanup are unified.
    Module['cnaDomGetPaddedVariant'] = function(id, mode, r, g, b, leftPad, topPad, rightPad, bottomPad) {
        const entry = Module['cnaDomTextures'] && Module['cnaDomTextures'][id];
        if (!entry || !entry.ctx) return null;
        const GRANULARITY = 8;
        const roundUp = function(v) { return v <= 0 ? 0 : Math.ceil(v / GRANULARITY) * GRANULARITY; };
        const lp = roundUp(leftPad), tp = roundUp(topPad), rp = roundUp(rightPad), bp = roundUp(bottomPad);
        const key = 'pad:' + mode + ':' + r + ',' + g + ',' + b + ':' + lp + ',' + tp + ',' + rp + ',' + bp;
        const cached = Module['cnaDomVariantCacheGet'](id, key);
        if (cached) return cached;

        const base = Module['cnaDomGetVariant'](id, mode, r, g, b);
        if (!base) return null;
        const w = entry.w, h = entry.h;
        const canvas = Module['cnaDomNewCanvas'](w + lp + rp, h + tp + bp);
        if (!canvas) return null;
        const ctx = canvas.getContext('2d');
        // Stretching a 1-texel source to an N-pixel destination is exact clamp-to-edge sampling only
        // if the browser's own scaler never reads outside that 1-texel band -- measured, not assumed:
        // with smoothing left on, Chromium's GPU-accelerated drawImage bilinear-sampled slightly
        // PAST a 1px source slice's own edge here, bleeding in the adjacent row/column's colour.
        // imageSmoothingEnabled=false pins every draw below to true nearest-neighbour sampling, which
        // cannot read outside the requested source rect.
        ctx.imageSmoothingEnabled = false;
        ctx.drawImage(base.canvas, 0, 0, w, h, lp, tp, w, h);                 // centre: the texture itself
        // Edges: a single-texel-wide/tall strip from the nearest edge, stretched to fill the padding.
        if (lp > 0) ctx.drawImage(base.canvas, 0, 0, 1, h, 0, tp, lp, h);
        if (rp > 0) ctx.drawImage(base.canvas, w - 1, 0, 1, h, lp + w, tp, rp, h);
        if (tp > 0) ctx.drawImage(base.canvas, 0, 0, w, 1, lp, 0, w, tp);
        if (bp > 0) ctx.drawImage(base.canvas, 0, h - 1, w, 1, lp, tp + h, w, bp);
        // Corners: the single corner texel, stretched to fill the corner padding rectangle.
        if (lp > 0 && tp > 0) ctx.drawImage(base.canvas, 0, 0, 1, 1, 0, 0, lp, tp);
        if (rp > 0 && tp > 0) ctx.drawImage(base.canvas, w - 1, 0, 1, 1, lp + w, 0, rp, tp);
        if (lp > 0 && bp > 0) ctx.drawImage(base.canvas, 0, h - 1, 1, 1, 0, tp + h, lp, bp);
        if (rp > 0 && bp > 0) ctx.drawImage(base.canvas, w - 1, h - 1, 1, 1, lp + w, tp + h, rp, bp);

        const variant = { canvas: canvas, url: null, shared: false, padLeft: lp, padTop: tp };
        Module['cnaDomVariantCachePut'](entry, id, key, variant);
        return variant;
    };

    // plans/plan_html_dom.md HTMLDOM-104: given a draw's raw (possibly out-of-bounds) source rectangle and
    // which axes are tiled (Wrap/Mirror -- never padded), decides whether a plain or edge-extended
    // variant is needed and returns BOTH the variant to sample from and the source coordinates
    // shifted to match it. Shared by both CNA_HtmlDom_FlushSprites paths (DOM and Canvas2D) so the
    // padding decision lives in exactly one place. `sx`/`sy` in the returned object are always what
    // the caller should actually use in place of the command's own raw sx/sy from here on.
    Module['cnaDomResolveClampVariant'] = function(id, mode, r, g, b, sx, sy, sw, sh, tiledU, tiledV) {
        const entry = Module['cnaDomTextures'] && Module['cnaDomTextures'][id];
        if (!entry) return null;
        const w = entry.w, h = entry.h;
        const padLeft = tiledU ? 0 : Math.max(0, -sx);
        const padRight = tiledU ? 0 : Math.max(0, sx + sw - w);
        const padTop = tiledV ? 0 : Math.max(0, -sy);
        const padBottom = tiledV ? 0 : Math.max(0, sy + sh - h);
        if (padLeft === 0 && padRight === 0 && padTop === 0 && padBottom === 0) {
            return { variant: Module['cnaDomGetVariant'](id, mode, r, g, b), sx: sx, sy: sy };
        }
        const variant = Module['cnaDomGetPaddedVariant'](id, mode, r, g, b, padLeft, padTop, padRight, padBottom);
        if (!variant) return null;
        return { variant: variant, sx: sx + (variant.padLeft || 0), sy: sy + (variant.padTop || 0) };
    };

    Module['cnaDomEnsureUrl'] = function(variant) {
        if (!variant || variant.url) return;
        let source = variant.canvas;
        // OffscreenCanvas has no toDataURL, so such a variant is copied into a regular <canvas>
        // first -- still a one-off cost per variant, never a per-draw one.
        if (typeof source.toDataURL !== 'function') {
            const copy = document.createElement('canvas');
            copy.width = source.width; copy.height = source.height;
            copy.getContext('2d').drawImage(source, 0, 0);
            source = copy;
        }
        variant.url = source.toDataURL('image/png');
    };
});

// plans/plan_html_dom.md HTMLDOM-20: every texture owns one private off-screen canvas (OffscreenCanvas
// where available, else a detached <canvas>), registered by integer id in Module['cnaDomTextures'].
// The canvas holds the pixels; every derived form of those pixels (HTMLDOM-109) lives in the global
// `Module['cnaDomVariantCache']`, dropped for this entry specifically whenever its pixels change.
//
// willReadFrequently: every variant this texture ever produces starts with a getImageData on this
// context, so the browser should keep it CPU-backed instead of repeatedly reading back from the GPU.
//
// isRenderTarget: plans/plan_html_dom.md HTMLDOM-106. True only for HtmlDomRenderTargetRenderer's own
// backing canvas (its constructor is this function's only caller with isRenderTarget != 0) -- see
// cnaDomGetVariant's own comment for why AlphaBlend sampling needs to know this.
EM_JS(void, CNA_HtmlDom_CreateTexture, (int id, int width, int height, const uint8_t* rgba, int isRenderTarget), {
    if (!Module['cnaDomTextures']) Module['cnaDomTextures'] = {};
    if (!Module['cnaDomNewCanvas']) {
        // Returns null where neither an OffscreenCanvas nor a document exists -- the repo's own
        // GTest runner is plain `node`, with no DOM at all, and a texture constructed there must
        // degrade to "no pixels" rather than throwing a ReferenceError into wasm.
        Module['cnaDomNewCanvas'] = function(w, h) {
            if (typeof OffscreenCanvas !== 'undefined') return new OffscreenCanvas(w, h);
            if (typeof document === 'undefined') return null;
            const c = document.createElement('canvas');
            c.width = w; c.height = h;
            return c;
        };
    }
    const canvas = Module['cnaDomNewCanvas'](width, height);
    const ctx = canvas ? canvas.getContext('2d', { willReadFrequently: true }) : null;
    if (rgba && ctx) {
        const bytes = new Uint8ClampedArray(HEAPU8.subarray(rgba, rgba + width * height * 4));
        ctx.putImageData(new ImageData(bytes, width, height), 0, 0);
    }
    Module['cnaDomTextures'][id] = { canvas: canvas, ctx: ctx, w: width, h: height,
                                     sharedBaseVariant: null, isRenderTarget: !!isRenderTarget };
});

// plans/plan_html_dom.md HTMLDOM-21/HTMLDOM-109: full level-0 re-upload. Every cached variant derived from
// this entry's OLD pixels is dropped -- both the free shared base variant (its `.canvas` is `entry`'s
// own canvas, so it always reflects the CURRENT pixels live and needs no data of its own re-created,
// but its already-encoded `.url`, if any, is now a stale PNG of the old pixels and must not survive)
// and every real (memory-owning) variant sitting in the capped global cache, or the next draw that
// selects one of those keys would show pre-update content.
EM_JS(void, CNA_HtmlDom_UpdateTexture, (int id, int width, int height, const uint8_t* rgba), {
    const entry = Module['cnaDomTextures'] && Module['cnaDomTextures'][id];
    if (!entry) { console.error('[CNA] HTML_DOM: UpdatePixels on unknown texture id', id); return; }
    if (!entry.ctx) return;
    const bytes = new Uint8ClampedArray(HEAPU8.subarray(rgba, rgba + width * height * 4));
    entry.ctx.putImageData(new ImageData(bytes, width, height), 0, 0);
    entry.sharedBaseVariant = null;
    if (Module['cnaDomVariantCacheClearOwner']) Module['cnaDomVariantCacheClearOwner'](entry);
});

// plans/plan_html_dom.md HTMLDOM-109: drops this texture's own live records from the global variant cache
// BEFORE the registry entry itself is removed -- otherwise those records would become orphaned,
// unreachable-by-owner stale `(id,key)` pairs sitting in the cache until eviction finally reaches
// them, wasting a cache slot a real, still-live variant could have used instead.
EM_JS(void, CNA_HtmlDom_DestroyTexture, (int id), {
    const entry = Module['cnaDomTextures'] && Module['cnaDomTextures'][id];
    if (entry && Module['cnaDomVariantCacheClearOwner']) Module['cnaDomVariantCacheClearOwner'](entry);
    if (Module['cnaDomTextures']) delete Module['cnaDomTextures'][id];
});
#endif

namespace CNA::Internal::Renderers::HtmlDom
{
    std::vector<std::uint8_t> TightenTextureRowsEXT(
        const std::uint8_t* rgba, int width, int height, int stride)
    {
        if (rgba == nullptr) return {};
        System::ArgumentOutOfRangeException::ThrowIfNegativeOrZero(width, "width");
        System::ArgumentOutOfRangeException::ThrowIfNegativeOrZero(height, "height");
        if (width > std::numeric_limits<int>::max() / 4)
            throw System::ArgumentOutOfRangeException(
                "width", std::to_string(width), "width * 4 must fit the row-pitch contract.");
        const int rowBytes = width * 4;
        if (stride < rowBytes)
            throw System::ArgumentOutOfRangeException(
                "stride", std::to_string(stride),
                "stride must contain at least width * 4 RGBA8 bytes per row.");

        const std::uint64_t tightBytes = static_cast<std::uint64_t>(rowBytes)
            * static_cast<std::uint64_t>(height);
        const std::uint64_t sourceBytes = static_cast<std::uint64_t>(height - 1)
            * static_cast<std::uint64_t>(stride) + static_cast<std::uint64_t>(rowBytes);
        if (tightBytes > std::numeric_limits<std::size_t>::max() ||
            sourceBytes > std::numeric_limits<std::size_t>::max())
            throw System::ArgumentOutOfRangeException(
                "dimensions", std::to_string(width) + "x" + std::to_string(height),
                "pitched RGBA8 storage must fit the platform address space.");

        std::vector<std::uint8_t> tight(static_cast<std::size_t>(tightBytes));
        for (int row = 0; row < height; ++row)
        {
            std::memcpy(tight.data() + static_cast<std::size_t>(row) * rowBytes,
                        rgba + static_cast<std::size_t>(row) * static_cast<std::size_t>(stride),
                        static_cast<std::size_t>(rowBytes));
        }
        return tight;
    }

    namespace
    {
        int NextTextureId()
        {
            static int next = 1;
            return next++;
        }
    }

    HtmlDomTextureRenderer::HtmlDomTextureRenderer(const ImageData& data)
        : id_(NextTextureId())
        , width_(data.width)
        , height_(data.height)
    {
        // plans/plan_html_dom.md HTMLDOM-120: neither Texture2D's own constructors nor this one validate
        // width/height are positive anywhere upstream -- a zero/negative size would otherwise reach
        // `new OffscreenCanvas(w,h)`/`canvas.width=w` (CNA_HtmlDom_CreateTexture below), whose
        // behavior for a degenerate size is browser-implementation-defined and untested here.
        // Caught here instead, at this renderer's own JS-crossing boundary, with an actionable error.
        System::ArgumentOutOfRangeException::ThrowIfNegativeOrZero(width_, "width");
        System::ArgumentOutOfRangeException::ThrowIfNegativeOrZero(height_, "height");
        if (width_ > std::numeric_limits<int>::max() / 4)
            throw System::ArgumentOutOfRangeException(
                "width", std::to_string(width_), "width * 4 must fit the row-pitch contract.");
        if (data.mipLevels != 1)
            throw std::runtime_error(
                "HTML_DOM renderer: Texture2D mip chains are unsupported; request mipMap=false.");
        if (data.surfaceFormat != 0)
            throw std::runtime_error(
                "HTML_DOM renderer: Texture2D supports only SurfaceFormat::Color (RGBA8).");
        const std::uint64_t requiredBytes = static_cast<std::uint64_t>(width_)
            * static_cast<std::uint64_t>(height_) * 4u;
        if (requiredBytes > std::numeric_limits<std::size_t>::max() ||
            data.pixels.size() != static_cast<std::size_t>(requiredBytes))
            throw System::ArgumentOutOfRangeException(
                "pixels", std::to_string(data.pixels.size()),
                "pixel storage must contain exactly width * height * 4 RGBA8 bytes.");
#if defined(__EMSCRIPTEN__)
        CNA_HtmlDom_CreateTexture(id_, width_, height_, data.pixels.data(), 0);
        CNA_HtmlDom_InstallTextureHelpers();
#endif
    }

    HtmlDomTextureRenderer::HtmlDomTextureRenderer(int width, int height)
        : id_(NextTextureId())
        , width_(width)
        , height_(height)
    {
        // plans/plan_html_dom.md HTMLDOM-120: this constructor is used exclusively by
        // HtmlDomRenderTargetRenderer (see this class's own header comment) -- RenderTarget2D's own
        // constructor (RenderTarget2D.cpp) validates neither width nor height before calling
        // CreateRenderTarget2D, so this is the only place in the whole chain that can catch it.
        System::ArgumentOutOfRangeException::ThrowIfNegativeOrZero(width_, "width");
        System::ArgumentOutOfRangeException::ThrowIfNegativeOrZero(height_, "height");
        if (width_ > std::numeric_limits<int>::max() / 4)
            throw System::ArgumentOutOfRangeException(
                "width", std::to_string(width_), "width * 4 must fit the row-pitch contract.");
        const std::uint64_t requiredBytes = static_cast<std::uint64_t>(width_)
            * static_cast<std::uint64_t>(height_) * 4u;
        if (requiredBytes > std::numeric_limits<std::size_t>::max())
            throw System::ArgumentOutOfRangeException(
                "dimensions", std::to_string(width_) + "x" + std::to_string(height_),
                "RGBA8 render-target storage must fit the platform address space.");
#if defined(__EMSCRIPTEN__)
        // plans/plan_html_dom.md HTMLDOM-106: isRenderTarget=1 -- this constructor is used exclusively by
        // HtmlDomRenderTargetRenderer (see this class's own header comment), never for a plain
        // Texture2D, so it can be hardcoded here rather than threaded through as its own parameter.
        CNA_HtmlDom_CreateTexture(id_, width_, height_, nullptr, 1);
        CNA_HtmlDom_InstallTextureHelpers();
#endif
    }

    HtmlDomTextureRenderer::~HtmlDomTextureRenderer()
    {
#if defined(__EMSCRIPTEN__)
        CNA_HtmlDom_DestroyTexture(id_);
#endif
    }

    void HtmlDomTextureRenderer::UpdatePixels(const uint8_t* rgba, int stride)
    {
        if (!rgba) return;
        const int rowBytes = width_ * 4;
        const std::uint8_t* upload = rgba;
        std::vector<std::uint8_t> tight;
        if (stride != rowBytes)
        {
            tight = TightenTextureRowsEXT(rgba, width_, height_, stride);
            upload = tight.data();
        }
#if defined(__EMSCRIPTEN__)
        CNA_HtmlDom_UpdateTexture(id_, width_, height_, upload);
#else
        (void)upload;
#endif
    }

    void HtmlDomTextureRenderer::UpdatePixelsLevel(int level, const uint8_t* rgba, int levelW, int levelH)
    {
        if (level != 0)
            throw std::runtime_error(
                "HTML_DOM renderer: mip-level texture upload (level " + std::to_string(level) +
                ") is not yet implemented. Neither CSS background painting nor this renderer's "
                "texture canvases have a mip chain or per-level LOD selection -- the same boundary "
                "CANVAS and the native 2D renderer draw. Use Texture2D::SetData(level=0, ...).");
        if (levelW != width_ || levelH != height_)
            throw System::ArgumentOutOfRangeException(
                "levelSize", std::to_string(levelW) + "x" + std::to_string(levelH),
                "level 0 dimensions must match the texture dimensions.");
        UpdatePixels(rgba, width_ * 4);
    }
}
