#include "CNA/Internal/Renderers/Canvas/CanvasRenderTargetRenderer.hpp"

#include <cstdint>
#include <string>

#include "System/ArgumentOutOfRangeException.hpp"
#include "System/NotSupportedException.hpp"

#if defined(__EMSCRIPTEN__)
#include <emscripten.h>

// plan_canvas.md CANVAS-22: switches Module['cnaCurrentCtx'] (see CanvasRenderer.cpp) to
// this render target's own off-screen context, so subsequent Clear()/Draw()/ReadBackbuffer() calls
// target it instead of the main canvas. The counterpart, CNA_Canvas2D_UnbindRenderTarget(), lives
// in CanvasRenderer.cpp (SetRenderTarget2D(nullptr)'s path).
//
// Also invalidates this id's cached mirror-tile (Module['cnaMirrorTiles'][id] -- see
// CanvasSpriteBatchRenderer.cpp's DrawSprite): a render target's pixels can change via direct
// Clear()/Draw() calls made against it while bound, never going through CanvasTextureRenderer's own
// UpdatePixels()/CNA_Canvas2D_UpdatePixels (which already invalidates the same cache for the plain
// Texture2D::SetData path). Without this, binding a render target, drawing new content into it, then
// later sampling it with Mirror addressing would show the stale pre-bind tile. Conservative but
// correct: every bind is treated as "this target's content may be about to change," even if the
// caller only reads it back without drawing anything new.
// REMED-GFX-127: synchronous readback of ONE target's own off-screen context (unlike
// CNA_Canvas2D_ReadCurrentPixels in CanvasRenderer.cpp, which reads whichever context is
// current). Returns 0 when the id is unknown so the caller can report the failure rather than hand
// the shared layer a buffer it never filled.
EM_JS(int, CNA_Canvas2D_ReadTexturePixels, (int id, int x, int y, int w, int h, uint8_t* outPixels), {
    const entry = Module['cnaTextures'] && Module['cnaTextures'][id];
    if (!entry || !entry.ctx) return 0;
    const imageData = entry.ctx.getImageData(x, y, w, h);
    HEAPU8.set(imageData.data, outPixels);
    return 1;
});

EM_JS(void, CNA_Canvas2D_BindRenderTarget, (int id), {
    const entry = Module['cnaTextures'] && Module['cnaTextures'][id];
    if (!entry) { console.error('[CNA] Canvas2D: BindRenderTarget on unknown id', id); return; }
    Module['cnaCurrentCtx'] = entry.ctx;
    entry.unpremultipliedCanvas = null;
    entry.unpremultipliedCtx = null;
    if (Module['cnaMirrorTiles']) delete Module['cnaMirrorTiles'][id];
});

EM_JS(void, CNA_Canvas2D_MarkRenderTarget, (int id), {
    const entry = Module['cnaTextures'] && Module['cnaTextures'][id];
    if (entry) entry.isRenderTarget = true;
});
#endif

namespace CNA::Internal::Renderers::Canvas
{
    CanvasRenderTargetRenderer::CanvasRenderTargetRenderer(int w, int h)
        : texture_(w, h)
    {
#if defined(__EMSCRIPTEN__)
        CNA_Canvas2D_MarkRenderTarget(texture_.GetCanvasId());
#endif
    }

    CanvasRenderTargetRenderer::~CanvasRenderTargetRenderer() = default;

    void CanvasRenderTargetRenderer::BindAsRenderTarget()
    {
#if defined(__EMSCRIPTEN__)
        CNA_Canvas2D_BindRenderTarget(texture_.GetCanvasId());
#endif
    }

    bool CanvasRenderTargetRenderer::GetData(int level, int x, int y, int w, int h,
                                            void* data, int dataLength) const
    {
        if (level < 0)
            throw System::ArgumentOutOfRangeException(
                "level", std::to_string(level), "level must not be negative.");
        // plan_canvas.md CANVAS-21's boundary: Canvas2D has no mip chain, so a level > 0 request is
        // rejected rather than answered from level 0.
        if (level > 0)
            throw System::NotSupportedException(
                "CanvasRenderTargetRenderer::GetData: Canvas2D has no mip chain; level " +
                std::to_string(level) + " was requested.");
        const int targetWidth = texture_.GetWidth();
        const int targetHeight = texture_.GetHeight();
        // 64-bit throughout, so a rectangle near INT_MAX is rejected rather than wrapping.
        const std::int64_t right = static_cast<std::int64_t>(x) + static_cast<std::int64_t>(w);
        const std::int64_t bottom = static_cast<std::int64_t>(y) + static_cast<std::int64_t>(h);
        if (x < 0 || y < 0 || w <= 0 || h <= 0 ||
            right > static_cast<std::int64_t>(targetWidth) ||
            bottom > static_cast<std::int64_t>(targetHeight))
            throw System::ArgumentOutOfRangeException(
                "rect",
                std::to_string(x) + "," + std::to_string(y) + "," + std::to_string(w) + "," +
                    std::to_string(h),
                "The requested rectangle leaves the " + std::to_string(targetWidth) + "x" +
                    std::to_string(targetHeight) + " render target.");
        const std::int64_t requiredBytes =
            static_cast<std::int64_t>(w) * static_cast<std::int64_t>(h) * 4;
        if (static_cast<std::int64_t>(dataLength) < requiredBytes)
            throw System::ArgumentOutOfRangeException(
                "dataLength", std::to_string(dataLength),
                "The destination holds fewer than the " + std::to_string(requiredBytes) +
                    " bytes the requested rectangle needs.");
        if (data == nullptr) return false;

#if defined(__EMSCRIPTEN__)
        return CNA_Canvas2D_ReadTexturePixels(texture_.GetCanvasId(), x, y, w, h,
                                              static_cast<std::uint8_t*>(data)) != 0;
#else
        // No browser, so no canvas and no pixels. REMED-GFX-127: say so instead of letting the
        // shared layer answer with its own zeroed scratch buffer.
        return false;
#endif
    }

    void CanvasRenderTargetRenderer::UnbindAsRenderTarget()
    {
        // A genuine no-op, unlike e.g. EasyGL's UnbindAsRenderTarget() (which does real MSAA-resolve/
        // mip-regeneration work): BindAsRenderTarget() above is idempotent/absolute -- it always
        // sets Module['cnaCurrentCtx'] to this target's own context outright, so switching directly
        // from this target to another (or back to the main canvas via
        // CanvasRenderer::SetRenderTarget2D(nullptr)'s CNA_Canvas2D_UnbindRenderTarget())
        // never needs this target to do any cleanup of its own first.
    }
}
