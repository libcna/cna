// SPDX-License-Identifier: MS-PL
#include "CNA/Internal/Renderers/SvgDom/SvgDomRenderTargetRenderer.hpp"
#include "CNA/Internal/Renderers/SvgDom/SvgDomState.hpp"

#include <cstring>

#if defined(__EMSCRIPTEN__)
#include <emscripten.h>

// plan_svg_dom.md design decision 5: a private off-screen canvas backing one render target.
// Registered under Module['cnaSvgDomTextures'][id] -- the same registry the sprite-batch
// render-target draw path (SvgDomSpriteBatchRenderer.cpp) looks entries up in, keyed the same way
// as an ordinary uploaded texture's own JS-side id (SvgDomTextureRenderer::GetCanvasIdEXT), so a
// render target sampled back as a Draw() source texture resolves through the identical lookup
// (CNA_SvgDom_RegisterTextureVariant additively extends THIS SAME entry with variants/variantDims
// the first time that happens -- SVGDOM-E; see SvgDomTextureRenderer.cpp).
EM_JS(void, CNA_SvgDom_CreateTargetCanvas, (int id, int w, int h), {
    if (typeof document === 'undefined') return;
    if (!Module['cnaSvgDomTextures']) Module['cnaSvgDomTextures'] = {};
    const canvas = (typeof OffscreenCanvas !== 'undefined')
        ? new OffscreenCanvas(w, h)
        : Object.assign(document.createElement('canvas'), { width: w, height: h });
    const ctx = canvas.getContext('2d');
    Module['cnaSvgDomTextures'][id] = { canvas: canvas, ctx: ctx, w: w, h: h, isRenderTarget: true };
});

EM_JS(void, CNA_SvgDom_DestroyTargetCanvas, (int id), {
    const reg = Module['cnaSvgDomTextures'];
    const entry = reg && reg[id];
    if (entry && entry.variantObjectUrls && typeof URL !== 'undefined' && URL.revokeObjectURL) {
        for (const key in entry.variantObjectUrls) {
            const objectUrl = entry.variantObjectUrls[key];
            if (objectUrl) URL.revokeObjectURL(objectUrl);
        }
    }
    if (reg) delete reg[id];
});

// Reads the target's current pixels back into the wasm heap at `ptr` (w*h*4 RGBA8 bytes, straight
// alpha -- getImageData's own contract). Returns 1 on success, 0 if the registry entry is missing.
EM_JS(int, CNA_SvgDom_ReadTargetPixels, (int id, void* ptr, int w, int h), {
    const entry = Module['cnaSvgDomTextures'] && Module['cnaSvgDomTextures'][id];
    if (!entry) return 0;
    const imgData = entry.ctx.getImageData(0, 0, w, h);
    HEAPU8.set(imgData.data, ptr);
    return 1;
});

// SVGDOM-D: pushes freshly uploaded (SetData/UpdatePixels) pixels into the target's OWN canvas, not
// only the C++ CPU buffer -- without this, a canvas-composited draw made after UpdatePixels() (or a
// later GetData readback of a target that was never re-bound) would still see whatever the canvas
// held from BEFORE the update, since the two representations had no way to reconverge except a
// bind-triggered readback going the other direction. `pixels` is copied into a fresh
// Uint8ClampedArray before handing it to ImageData -- same pattern CNA_SvgDom_DrawSpriteToTarget
// already uses -- so nothing here retains a view into the (potentially-growing) wasm heap.
EM_JS(void, CNA_SvgDom_WriteTargetPixels, (int id, const void* pixels, int w, int h), {
    const entry = Module['cnaSvgDomTextures'] && Module['cnaSvgDomTextures'][id];
    if (!entry || !entry.ctx) return;
    const bytes = new Uint8ClampedArray(HEAPU8.buffer, pixels, w * h * 4);
    entry.ctx.putImageData(new ImageData(new Uint8ClampedArray(bytes), w, h), 0, 0);
});
#endif

namespace CNA::Internal::Renderers::SvgDom
{
    SvgDomRenderTargetRenderer::SvgDomRenderTargetRenderer(int w, int h)
        : texture_(w, h)
    {
        // texture_'s own constructor already allocated a unique id from the shared
        // AllocateTextureIdEXT() counter (SvgDomTextureRenderer.cpp) -- reused here as the key into
        // the same Module['cnaSvgDomTextures'] registry an ordinary uploaded texture uses.
#if defined(__EMSCRIPTEN__)
        CNA_SvgDom_CreateTargetCanvas(texture_.GetCanvasIdEXT(), w, h);
#endif
    }

    SvgDomRenderTargetRenderer::~SvgDomRenderTargetRenderer()
    {
#if defined(__EMSCRIPTEN__)
        CNA_SvgDom_DestroyTargetCanvas(texture_.GetCanvasIdEXT());
#endif
    }

    void SvgDomRenderTargetRenderer::UpdatePixels(const uint8_t* rgba, int stride)
    {
        texture_.UpdatePixels(rgba, stride);
#if defined(__EMSCRIPTEN__)
        // SVGDOM-D: keep the canvas coherent with the CPU buffer immediately, regardless of bind
        // state -- a subsequent canvas-composited draw (if this target is bound again) must start
        // from what was just uploaded, not from whatever the canvas held before this call.
        CNA_SvgDom_WriteTargetPixels(texture_.GetCanvasIdEXT(), texture_.GetPixelsEXT().data(),
                                     GetWidth(), GetHeight());
#endif
        // Authoritative-state contract (SVGDOM-D): both representations now agree. EnsureFreshEXT
        // additionally re-reads the canvas on EVERY call for as long as this target stays the
        // CURRENTLY BOUND one (see below) regardless of this flag, so it is safe to simply clear it
        // here rather than trying to predict whether more canvas draws will follow before the next
        // unbind.
        dirty_ = false;
    }

    void SvgDomRenderTargetRenderer::BindAsRenderTarget()
    {
        dirty_ = true;
        SetBoundRenderTargetIdEXT(texture_.GetCanvasIdEXT());
    }

    void SvgDomRenderTargetRenderer::UnbindAsRenderTarget()
    {
        SetBoundRenderTargetIdEXT(0);
    }

    void SvgDomRenderTargetRenderer::EnsureFreshEXT() const
    {
#if defined(__EMSCRIPTEN__)
        // SVGDOM-D/E: refresh whenever the CPU buffer is known-stale (dirty_, set by
        // BindAsRenderTarget) OR this target is the one CURRENTLY bound -- the latter closes a gap
        // a dirty_-only check would miss: a target bound, sampled once (which clears dirty_), then
        // drawn into AGAIN without an intervening unbind/rebind has no other hook back into this
        // object to re-arm dirty_, since draws reach the canvas through the generic JS registry, not
        // through a pointer to this instance. Always re-reading while bound trades one extra
        // getImageData call per read for guaranteed-fresh content -- "same render target modified
        // again after it has already been sampled" (a required regression case) must never serve
        // stale pixels merely because nothing unbound it in between. Meaningful ONLY under
        // Emscripten: a native build has no canvas at all, so nothing could have drawn into this
        // target regardless of its bound state, and dirty_ alone (see UpdatePixels/BindAsRenderTarget)
        // already says everything a native build can honestly know.
        const bool currentlyBound = (GetBoundRenderTargetIdEXT() == texture_.GetCanvasIdEXT());
        if (!dirty_ && !currentlyBound) return;
        std::vector<std::uint8_t> full(static_cast<std::size_t>(GetWidth()) * GetHeight() * 4);
        const int ok = CNA_SvgDom_ReadTargetPixels(
            texture_.GetCanvasIdEXT(), full.data(), GetWidth(), GetHeight());
        if (!ok) return; // no browser canvas reachable; dirty_ (if set) stays set -- honest, not stale
        texture_.UpdatePixels(full.data(), GetWidth() * 4);
        dirty_ = false;
#endif
    }

    bool SvgDomRenderTargetRenderer::GetData(int level, int x, int y, int w, int h,
                                             void* data, int dataLength) const
    {
        if (level != 0 || !data) return false;
        if (x < 0 || y < 0 || w <= 0 || h <= 0) return false;
        // Overflow-safe: x/y/w/h are caller-controlled ints reaching this from the public
        // Texture2D::GetData surface; x+w or y+h computed in plain int can wrap for adversarial
        // inputs near INT_MAX, which would defeat the bounds check below on a 32-bit target
        // (Emscripten/wasm32's int and std::size_t are both 32-bit, so an int64_t stopover is not
        // merely stylistic here the way it would be on a 64-bit native build).
        if (static_cast<std::int64_t>(x) + w > GetWidth() ||
            static_cast<std::int64_t>(y) + h > GetHeight())
            return false;
        if (static_cast<std::int64_t>(dataLength) < static_cast<std::int64_t>(w) * h * 4) return false;

        EnsureFreshEXT();
        if (dirty_) return false; // a needed readback couldn't run (no browser canvas reachable)

        return texture_.GetData(0, x, y, w, h, data, dataLength);
    }
}
