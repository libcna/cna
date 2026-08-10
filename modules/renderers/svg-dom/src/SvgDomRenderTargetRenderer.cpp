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
// render target sampled back as a Draw() source texture resolves through the identical lookup.
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

    bool SvgDomRenderTargetRenderer::GetData(int level, int x, int y, int w, int h,
                                             void* data, int dataLength) const
    {
        if (level != 0 || !data) return false;
        if (x < 0 || y < 0 || w <= 0 || h <= 0) return false;
        if (x + w > GetWidth() || y + h > GetHeight()) return false;
        if (dataLength < w * h * 4) return false;

        if (dirty_)
        {
#if defined(__EMSCRIPTEN__)
            std::vector<std::uint8_t> full(static_cast<std::size_t>(GetWidth()) * GetHeight() * 4);
            const int ok = CNA_SvgDom_ReadTargetPixels(
                texture_.GetCanvasIdEXT(), full.data(), GetWidth(), GetHeight());
            if (!ok) return false;
            texture_.UpdatePixels(full.data(), GetWidth() * 4);
            dirty_ = false;
#else
            return false;
#endif
        }

        return texture_.GetData(0, x, y, w, h, data, dataLength);
    }
}
