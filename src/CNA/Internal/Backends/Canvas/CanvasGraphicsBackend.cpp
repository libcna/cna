#include "CNA/Internal/Backends/Canvas/CanvasGraphicsBackend.hpp"

#include <SDL3/SDL.h>

#include <stdexcept>

#if defined(__EMSCRIPTEN__)
#include <emscripten.h>

// plan_canvas.md CANVAS-10/Design decision 2: shared JS-side helper that locates the existing DOM
// <canvas> element (Module['canvas'] || document.querySelector('canvas'), the same lookup
// EasyGLGraphicsBackend.cpp already uses for its own GL context) and caches its '2d' context on
// Module['cnaCtx2d']. Every other Canvas2D EM_JS function below calls this first, then reads
// Module['cnaCtx2d'] -- EM_JS-declared functions are ordinary top-level JS functions in the
// generated glue code, so they can call each other directly by name.
EM_JS(void, CNA_Canvas2D_EnsureContext, (), {
    if (Module['cnaCtx2d']) return;
    const canvas = Module['canvas'] || document.querySelector('canvas');
    if (!canvas) { console.error('[CNA] Canvas2D: no <canvas> element found'); return; }
    const ctx = canvas.getContext('2d');
    if (!ctx) { console.error('[CNA] Canvas2D: getContext(\'2d\') failed'); return; }
    Module['cnaCtx2d'] = ctx;
});

// plan_canvas.md CANVAS-11: real Clear() against the main canvas's cached 2D context. alpha<=0 uses
// clearRect (transparent), matching FNA's Clear() semantics for a fully-transparent clear color;
// otherwise an opaque fillRect (Canvas2D has no separate "clear to this exact RGBA" primitive when
// alpha>0 -- a solid fillStyle + fillRect is the correct equivalent).
EM_JS(void, CNA_Canvas2D_Clear, (double r, double g, double b, double a), {
    CNA_Canvas2D_EnsureContext();
    const ctx = Module['cnaCtx2d'];
    if (!ctx) return;
    const w = ctx.canvas.width, h = ctx.canvas.height;
    if (a <= 0) { ctx.clearRect(0, 0, w, h); return; }
    const ri = Math.round(r * 255), gi = Math.round(g * 255), bi = Math.round(b * 255);
    ctx.fillStyle = 'rgba(' + ri + ',' + gi + ',' + bi + ',' + a + ')';
    ctx.fillRect(0, 0, w, h);
});
#endif

namespace CNA::Internal::Backends::Canvas
{
    namespace
    {
        // Phase C1 placeholder for methods a later phase makes real (Clear/Present in C2,
        // textures/render targets in C3, SpriteBatch in C4). Distinct from ThrowNo3D, which
        // Phase C7 wires up permanently for the inherently-3D-only surface.
        [[noreturn]] void NotYetImplemented(const char* methodName)
        {
            throw std::runtime_error(
                std::string("CanvasGraphicsBackend::") + methodName + " not yet implemented (see plan_canvas.md)");
        }

        [[noreturn]] void ThrowNo3D(const char* methodName)
        {
            throw std::runtime_error(
                std::string("Canvas (HTML Canvas 2D) does not support 3D: ") + methodName);
        }
    }

    CanvasGraphicsBackend::CanvasGraphicsBackend(SDL_Window* window, int virtualWidth, int virtualHeight,
                                                  CnaPresentationMode mode)
        : window_(window)
        , virtualWidth_(virtualWidth)
        , virtualHeight_(virtualHeight)
        , presentationMode_(mode)
    {
        if (!window_) throw std::runtime_error("CanvasGraphicsBackend initialized with null window.");
        IGraphicsBackend::RegisterForWindow(window_, this);
    }

    CanvasGraphicsBackend::~CanvasGraphicsBackend()
    {
        IGraphicsBackend::UnregisterForWindow(window_);
    }

    void CanvasGraphicsBackend::Clear(float r, float g, float b, float a)
    {
#if defined(__EMSCRIPTEN__)
        CNA_Canvas2D_Clear(r, g, b, a);
#else
        (void)r; (void)g; (void)b; (void)a;
#endif
    }

    void CanvasGraphicsBackend::Present()
    {
        // plan_canvas.md CANVAS-12: no-op. The browser compositor presents the canvas
        // automatically on the next paint tick; there is nothing for CNA to flush or swap.
    }

    void CanvasGraphicsBackend::getLogicalSize(int& width, int& height) const
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

    void CanvasGraphicsBackend::GetViewportSize(int& width, int& height)
    {
        getLogicalSize(width, height);
    }

    void CanvasGraphicsBackend::SetVirtualResolution(int width, int height)
    {
        virtualWidth_ = width;
        virtualHeight_ = height;
    }

    void CanvasGraphicsBackend::SetPresentationMode(int mode)
    {
        presentationMode_ = static_cast<CnaPresentationMode>(mode);
    }

    bool CanvasGraphicsBackend::TransformWindowToLogical(float windowX, float windowY,
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

    bool CanvasGraphicsBackend::TransformLogicalToWindow(float logX, float logY,
                                                         float& windowX, float& windowY) const
    {
        // Inverse of TransformWindowToLogical -- see EasyGLGraphicsBackend::TransformLogicalToWindow
        // for why this is an exact, offset-free uniform scale under the default
        // FixedHeightDynamicWidth presentation mode (no letterbox bars to account for).
        if (virtualHeight_ <= 0) return false;
        int physW, physH;
        SDL_GetWindowSize(window_, &physW, &physH);
        if (physH <= 0) return false;
        const float invScale = static_cast<float>(physH) / static_cast<float>(virtualHeight_);
        windowX = logX * invScale;
        windowY = logY * invScale;
        return true;
    }

    std::unique_ptr<ITextureBackend> CanvasGraphicsBackend::CreateTexture(const ImageData&)
    {
        NotYetImplemented("CreateTexture");
    }

    std::unique_ptr<ISpriteBatchBackend> CanvasGraphicsBackend::CreateSpriteBatch()
    {
        NotYetImplemented("CreateSpriteBatch");
    }

    void CanvasGraphicsBackend::ClearColorAndDepth(float, float, float, float, float) { ThrowNo3D("ClearColorAndDepth"); }
    void CanvasGraphicsBackend::ClearDepth(float) { ThrowNo3D("ClearDepth"); }
    void CanvasGraphicsBackend::ClearStencil(int) { ThrowNo3D("ClearStencil"); }
    void CanvasGraphicsBackend::ClearDepthAndStencil(float, int) { ThrowNo3D("ClearDepthAndStencil"); }
    void CanvasGraphicsBackend::ClearColorAndStencil(float, float, float, float, int) { ThrowNo3D("ClearColorAndStencil"); }
    void CanvasGraphicsBackend::ClearColorDepthAndStencil(float, float, float, float, float, int) { ThrowNo3D("ClearColorDepthAndStencil"); }
    void CanvasGraphicsBackend::SetDepthTestEnabled(bool) { ThrowNo3D("SetDepthTestEnabled"); }
    void CanvasGraphicsBackend::SetBlendEnabled(bool) { ThrowNo3D("SetBlendEnabled"); }
    void CanvasGraphicsBackend::SetDepthWriteEnabled(bool) { ThrowNo3D("SetDepthWriteEnabled"); }

    std::unique_ptr<IVertexBufferBackend> CanvasGraphicsBackend::CreateVertexBuffer(int)
    {
        ThrowNo3D("CreateVertexBuffer");
    }

    std::unique_ptr<IIndexBufferBackend> CanvasGraphicsBackend::CreateIndexBuffer16(int)
    {
        ThrowNo3D("CreateIndexBuffer16");
    }

    void CanvasGraphicsBackend::DrawColoredPrimitives(const IVertexBufferBackend&,
                                                      const Matrix&, const Matrix&, const Matrix&,
                                                      PrimitiveType, int) { ThrowNo3D("DrawColoredPrimitives"); }

    void CanvasGraphicsBackend::DrawIndexedColoredPrimitives(const IVertexBufferBackend&, const IIndexBufferBackend&,
                                                             const Matrix&, const Matrix&, const Matrix&,
                                                             PrimitiveType, int) { ThrowNo3D("DrawIndexedColoredPrimitives"); }
}

namespace CNA::Internal::Backends
{
    std::unique_ptr<IGraphicsBackend> CreateGraphicsBackend(const GraphicsBackendCreateArgs& args)
    {
        return std::make_unique<Canvas::CanvasGraphicsBackend>(
            args.window, args.virtualWidth, args.virtualHeight, args.presentationMode);
    }
}
