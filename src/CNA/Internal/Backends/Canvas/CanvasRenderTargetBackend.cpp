#include "CNA/Internal/Backends/Canvas/CanvasRenderTargetBackend.hpp"

#if defined(__EMSCRIPTEN__)
#include <emscripten.h>

// plan_canvas.md CANVAS-22: switches Module['cnaCurrentCtx'] (see CanvasGraphicsBackend.cpp) to
// this render target's own off-screen context, so subsequent Clear()/Draw()/ReadBackbuffer() calls
// target it instead of the main canvas. The counterpart, CNA_Canvas2D_UnbindRenderTarget(), lives
// in CanvasGraphicsBackend.cpp (SetRenderTarget2D(nullptr)'s path).
EM_JS(void, CNA_Canvas2D_BindRenderTarget, (int id), {
    const entry = Module['cnaTextures'] && Module['cnaTextures'][id];
    if (!entry) { console.error('[CNA] Canvas2D: BindRenderTarget on unknown id', id); return; }
    Module['cnaCurrentCtx'] = entry.ctx;
});
#endif

namespace CNA::Internal::Backends::Canvas
{
    CanvasRenderTargetBackend::CanvasRenderTargetBackend(int w, int h)
        : texture_(w, h)
    {
    }

    CanvasRenderTargetBackend::~CanvasRenderTargetBackend() = default;

    void CanvasRenderTargetBackend::BindAsRenderTarget()
    {
#if defined(__EMSCRIPTEN__)
        CNA_Canvas2D_BindRenderTarget(texture_.GetCanvasId());
#endif
    }

    void CanvasRenderTargetBackend::UnbindAsRenderTarget()
    {
        // A genuine no-op, unlike e.g. EasyGL's UnbindAsRenderTarget() (which does real MSAA-resolve/
        // mip-regeneration work): BindAsRenderTarget() above is idempotent/absolute -- it always
        // sets Module['cnaCurrentCtx'] to this target's own context outright, so switching directly
        // from this target to another (or back to the main canvas via
        // CanvasGraphicsBackend::SetRenderTarget2D(nullptr)'s CNA_Canvas2D_UnbindRenderTarget())
        // never needs this target to do any cleanup of its own first.
    }
}
