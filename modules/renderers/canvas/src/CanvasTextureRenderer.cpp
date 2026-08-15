#include "CNA/Internal/Renderers/Canvas/CanvasTextureRenderer.hpp"

#include <stdexcept>

#if defined(__EMSCRIPTEN__)
#include <emscripten.h>

// plan_canvas.md CANVAS-20/Design decision 3: creates a private off-screen canvas (OffscreenCanvas
// where available, else a detached document.createElement('canvas')), uploads `rgba` via a single
// synchronous putImageData(), and registers { canvas, ctx } under `id` in Module['cnaTextures'] --
// CanvasSpriteBatchRenderer (Phase C4) sources ctx.drawImage() from this registry by id, and
// CanvasRenderTargetRenderer (below) reuses the exact same registry for its own bind/unbind.
EM_JS(void, CNA_Canvas2D_CreateTextureWithPixels, (int id, int width, int height, const uint8_t* rgba), {
    if (!Module['cnaTextures']) Module['cnaTextures'] = {};
    let canvas;
    if (typeof OffscreenCanvas !== 'undefined') {
        canvas = new OffscreenCanvas(width, height);
    } else {
        canvas = document.createElement('canvas');
        canvas.width = width;
        canvas.height = height;
    }
    const ctx = canvas.getContext('2d');
    const bytes = new Uint8ClampedArray(HEAPU8.subarray(rgba, rgba + width * height * 4));
    ctx.putImageData(new ImageData(bytes, width, height), 0, 0);
    Module['cnaTextures'][id] = { canvas: canvas, ctx: ctx, isRenderTarget: false };
});

// Same registration as above but blank (fully transparent) -- used by CanvasRenderTargetRenderer,
// which has no initial pixel data to upload.
EM_JS(void, CNA_Canvas2D_CreateBlankCanvas, (int id, int width, int height), {
    if (!Module['cnaTextures']) Module['cnaTextures'] = {};
    let canvas;
    if (typeof OffscreenCanvas !== 'undefined') {
        canvas = new OffscreenCanvas(width, height);
    } else {
        canvas = document.createElement('canvas');
        canvas.width = width;
        canvas.height = height;
    }
    const ctx = canvas.getContext('2d');
    Module['cnaTextures'][id] = { canvas: canvas, ctx: ctx, isRenderTarget: false };
});

// plan_canvas.md CANVAS-20: full level-0 re-upload, same synchronous putImageData() path as
// creation above. Also invalidates this texture's cached mirror-tile (Module['cnaMirrorTiles'][id],
// built lazily by CanvasSpriteBatchRenderer.cpp's DrawSprite for Mirror addressing) -- without this,
// a Texture2D::SetData() call after a Mirror-addressed draw had already built+cached the tile would
// leave that tile stale, so a subsequent Mirror-addressed draw would show pre-update pixels.
EM_JS(void, CNA_Canvas2D_UpdatePixels, (int id, int width, int height, const uint8_t* rgba), {
    const entry = Module['cnaTextures'] && Module['cnaTextures'][id];
    if (!entry) { console.error('[CNA] Canvas2D: UpdatePixels on unknown texture id', id); return; }
    const bytes = new Uint8ClampedArray(HEAPU8.subarray(rgba, rgba + width * height * 4));
    entry.ctx.putImageData(new ImageData(bytes, width, height), 0, 0);
    entry.unpremultipliedCanvas = null;
    entry.unpremultipliedCtx = null;
    if (Module['cnaMirrorTiles']) delete Module['cnaMirrorTiles'][id];
});

EM_JS(void, CNA_Canvas2D_DestroyTexture, (int id), {
    if (Module['cnaTextures']) delete Module['cnaTextures'][id];
    if (Module['cnaMirrorTiles']) delete Module['cnaMirrorTiles'][id];
});
#endif

namespace CNA::Internal::Renderers::Canvas
{
    namespace
    {
        int NextCanvasId()
        {
            static int next = 1;
            return next++;
        }
    }

    CanvasTextureRenderer::CanvasTextureRenderer(const ImageData& data)
        : id_(NextCanvasId())
        , width_(data.width)
        , height_(data.height)
    {
#if defined(__EMSCRIPTEN__)
        CNA_Canvas2D_CreateTextureWithPixels(id_, width_, height_, data.pixels.data());
#endif
    }

    CanvasTextureRenderer::CanvasTextureRenderer(int width, int height)
        : id_(NextCanvasId())
        , width_(width)
        , height_(height)
    {
#if defined(__EMSCRIPTEN__)
        CNA_Canvas2D_CreateBlankCanvas(id_, width_, height_);
#endif
    }

    CanvasTextureRenderer::~CanvasTextureRenderer()
    {
#if defined(__EMSCRIPTEN__)
        CNA_Canvas2D_DestroyTexture(id_);
#endif
    }

    void CanvasTextureRenderer::UpdatePixels(const uint8_t* rgba, int /*stride*/)
    {
        if (!rgba) return;
#if defined(__EMSCRIPTEN__)
        CNA_Canvas2D_UpdatePixels(id_, width_, height_, rgba);
#endif
    }

    void CanvasTextureRenderer::UpdatePixelsLevel(int level, const uint8_t* rgba, int levelW, int levelH)
    {
        if (level != 0)
            throw std::runtime_error(
                "Canvas (HTML Canvas 2D) does not support mip-level texture uploads (level " +
                std::to_string(level) + "): Canvas2D's drawImage/putImageData API has no native mip "
                "chain or per-level LOD sampling, same conclusion the native renderer reached (Task 681). "
                "Use Texture2D::SetData(level=0, ...) only.");
        (void)levelW; (void)levelH;
        UpdatePixels(rgba, width_ * 4);
    }
}
