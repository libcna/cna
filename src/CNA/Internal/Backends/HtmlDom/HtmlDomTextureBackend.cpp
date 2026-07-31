// SPDX-License-Identifier: MS-PL
#include "CNA/Internal/Backends/HtmlDom/HtmlDomTextureBackend.hpp"

#include <stdexcept>
#include <string>

#if defined(__EMSCRIPTEN__)
#include <emscripten.h>

// plan_html_dom.md HTMLDOM-23 / design decision 7: installs the two JS helpers the draw paths use
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
//   mode 0 = straight        -- the pixels exactly as uploaded (NonPremultiplied, Additive)
//   mode 1 = un-premultiplied -- RGB divided by alpha (AlphaBlend's srcBlend=One contract)
//   mode 2 = alpha-stripped   -- every alpha forced to 255 (Opaque ignores source alpha entirely)
//
// and, on top of any of those, RGB multiplied by the tint. The maths runs directly over the pixel
// array -- exact, with alpha read and written back completely untouched in every case -- rather
// than through composite-operation tricks, whose algebra CANVAS's own external review found to be
// silently wrong at semi-transparent, non-white edge pixels.
//
// The untinted straight variant is the base canvas itself, not a copy: it is by far the most common
// case (an ordinary untinted sprite) and copying it would double every texture's memory for nothing.
//
// `cnaDomEnsureUrl(variant)` pays for the PNG encode the DOM path needs, once per variant.
// `toDataURL` is the only SYNCHRONOUS canvas-to-URL route a browser offers -- `toBlob`/
// `convertToBlob` are asynchronous and would let a draw be issued before its own texture URL
// existed. The render-target draw path never calls this: it draws the variant's canvas directly.
EM_JS(void, CNA_HtmlDom_InstallTextureHelpers, (), {
    if (Module['cnaDomGetVariant']) return;

    Module['cnaDomGetVariant'] = function(id, mode, r, g, b) {
        const entry = Module['cnaDomTextures'] && Module['cnaDomTextures'][id];
        if (!entry || !entry.ctx) return null;
        const tinted = (r !== 255 || g !== 255 || b !== 255);
        const key = mode + ':' + r + ',' + g + ',' + b;
        let variant = entry.variants[key];
        if (variant) return variant;

        if (mode === 0 && !tinted) {
            variant = { canvas: entry.canvas, url: null, shared: true };
        } else {
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
            variant = { canvas: canvas, url: null, shared: false };

            // LRU cap: a game animating a sprite's tint every frame generates a new variant per
            // frame, so this bounds the cache at a fixed cost instead of letting it grow without
            // limit. Shared (base) variants own no memory of their own and are never enrolled.
            if (!Module['cnaDomVariantLru']) Module['cnaDomVariantLru'] = [];
            const lru = Module['cnaDomVariantLru'];
            lru.push([id, key]);
            while (lru.length > 256) {
                const old = lru.shift();
                const oldEntry = Module['cnaDomTextures'][old[0]];
                const oldVariant = oldEntry && oldEntry.variants[old[1]];
                if (oldVariant && !oldVariant.shared) delete oldEntry.variants[old[1]];
            }
        }
        entry.variants[key] = variant;
        return variant;
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

// plan_html_dom.md HTMLDOM-20: every texture owns one private off-screen canvas (OffscreenCanvas
// where available, else a detached <canvas>), registered by integer id in Module['cnaDomTextures'].
// The canvas holds the pixels; `variants` caches the derived forms of those pixels and is dropped
// whenever the pixels change.
//
// willReadFrequently: every variant this texture ever produces starts with a getImageData on this
// context, so the browser should keep it CPU-backed instead of repeatedly reading back from the GPU.
EM_JS(void, CNA_HtmlDom_CreateTexture, (int id, int width, int height, const uint8_t* rgba), {
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
    Module['cnaDomTextures'][id] = { canvas: canvas, ctx: ctx, w: width, h: height, variants: {} };
});

// plan_html_dom.md HTMLDOM-21: full level-0 re-upload. Every cached variant is dropped: they are
// all derived from these pixels, so leaving one behind would show pre-update content on the next
// draw that selected it.
EM_JS(void, CNA_HtmlDom_UpdateTexture, (int id, int width, int height, const uint8_t* rgba), {
    const entry = Module['cnaDomTextures'] && Module['cnaDomTextures'][id];
    if (!entry) { console.error('[CNA] HTML_DOM: UpdatePixels on unknown texture id', id); return; }
    if (!entry.ctx) return;
    const bytes = new Uint8ClampedArray(HEAPU8.subarray(rgba, rgba + width * height * 4));
    entry.ctx.putImageData(new ImageData(bytes, width, height), 0, 0);
    entry.variants = {};
});

EM_JS(void, CNA_HtmlDom_DestroyTexture, (int id), {
    if (Module['cnaDomTextures']) delete Module['cnaDomTextures'][id];
});
#endif

namespace CNA::Internal::Backends::HtmlDom
{
    namespace
    {
        int NextTextureId()
        {
            static int next = 1;
            return next++;
        }
    }

    HtmlDomTextureBackend::HtmlDomTextureBackend(const ImageData& data)
        : id_(NextTextureId())
        , width_(data.width)
        , height_(data.height)
    {
#if defined(__EMSCRIPTEN__)
        CNA_HtmlDom_CreateTexture(id_, width_, height_, data.pixels.data());
        CNA_HtmlDom_InstallTextureHelpers();
#endif
    }

    HtmlDomTextureBackend::HtmlDomTextureBackend(int width, int height)
        : id_(NextTextureId())
        , width_(width)
        , height_(height)
    {
#if defined(__EMSCRIPTEN__)
        CNA_HtmlDom_CreateTexture(id_, width_, height_, nullptr);
        CNA_HtmlDom_InstallTextureHelpers();
#endif
    }

    HtmlDomTextureBackend::~HtmlDomTextureBackend()
    {
#if defined(__EMSCRIPTEN__)
        CNA_HtmlDom_DestroyTexture(id_);
#endif
    }

    void HtmlDomTextureBackend::UpdatePixels(const uint8_t* rgba, int /*stride*/)
    {
        if (!rgba) return;
#if defined(__EMSCRIPTEN__)
        CNA_HtmlDom_UpdateTexture(id_, width_, height_, rgba);
#endif
    }

    void HtmlDomTextureBackend::UpdatePixelsLevel(int level, const uint8_t* rgba, int levelW, int levelH)
    {
        if (level != 0)
            throw std::runtime_error(
                "HTML_DOM backend: mip-level texture upload (level " + std::to_string(level) +
                ") is not yet implemented. Neither CSS background painting nor this backend's "
                "texture canvases have a mip chain or per-level LOD selection -- the same boundary "
                "CANVAS and SDL_RENDERER draw. Use Texture2D::SetData(level=0, ...).");
        (void)levelW; (void)levelH;
        UpdatePixels(rgba, width_ * 4);
    }
}
