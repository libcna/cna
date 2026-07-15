#include "CNA/Internal/Backends/Canvas/CanvasGraphicsBackend.hpp"
#include "CNA/Internal/Backends/Canvas/CanvasTextureBackend.hpp"
#include "CNA/Internal/Backends/Canvas/CanvasRenderTargetBackend.hpp"
#include "CNA/Internal/Backends/Canvas/CanvasSpriteBatchBackend.hpp"

#include <SDL3/SDL.h>

#include <stdexcept>

#if defined(__EMSCRIPTEN__)
#include <emscripten.h>

// plan_canvas.md CANVAS-10/Design decision 2: shared JS-side helper that locates the existing DOM
// <canvas> element (Module['canvas'] || document.querySelector('canvas'), the same lookup
// EasyGLGraphicsBackend.cpp already uses for its own GL context) and caches its '2d' context on
// Module['cnaMainCtx']. Module['cnaCurrentCtx'] is "whichever context Clear()/Draw() currently
// target" -- the main canvas by default, or a bound CanvasRenderTargetBackend's own off-screen
// context (see CanvasRenderTargetBackend.cpp's Bind/UnbindAsRenderTarget, Phase C3), mirroring
// SDL_Renderer's SetRenderTarget2D model of "operate on whatever's currently bound". EM_JS-declared
// functions are ordinary top-level JS functions in the generated glue code, so every Canvas2D EM_JS
// function in this backend (across every .cpp -- textures/render targets included) can call this
// one directly by name.
EM_JS(void, CNA_Canvas2D_EnsureMainContext, (), {
    if (Module['cnaMainCtx']) return;
    const canvas = Module['canvas'] || document.querySelector('canvas');
    if (!canvas) { console.error('[CNA] Canvas2D: no <canvas> element found'); return; }
    const ctx = canvas.getContext('2d');
    if (!ctx) { console.error('[CNA] Canvas2D: getContext(\'2d\') failed'); return; }
    Module['cnaMainCtx'] = ctx;
    if (!Module['cnaCurrentCtx']) Module['cnaCurrentCtx'] = ctx;
});

// plan_canvas.md CANVAS-11: real Clear() against whichever context is currently bound (main canvas
// or a bound render target -- see CANVAS-22). alpha<=0 uses clearRect (transparent, and clearRect
// always ignores globalCompositeOperation by spec, so no explicit reset is needed for that path).
// alpha>0 uses an explicit globalCompositeOperation='copy' fillRect: real XNA/FNA's Clear(color) is
// an unconditional overwrite of every pixel to exactly `color`, not a blend with whatever was
// already there -- plain 'source-over' (or whatever composite mode a previous SpriteBatch draw left
// active, e.g. 'lighter'/'copy' from BlendState.Additive/Opaque) would incorrectly blend instead.
// Wrapped in save()/restore() (not a permanent setTransform/globalCompositeOperation mutation) so a
// Clear() called mid-SpriteBatch-session (legal in XNA/FNA -- Begin()/End() doesn't prevent other
// GraphicsDevice calls in between) doesn't corrupt that session's own active transform/blend state
// for subsequent Draw() calls after Clear() returns.
EM_JS(void, CNA_Canvas2D_Clear, (double r, double g, double b, double a), {
    CNA_Canvas2D_EnsureMainContext();
    const ctx = Module['cnaCurrentCtx'];
    if (!ctx) return;
    ctx.save();
    ctx.setTransform(1, 0, 0, 1, 0, 0);
    const w = ctx.canvas.width, h = ctx.canvas.height;
    if (a <= 0) {
        ctx.clearRect(0, 0, w, h);
    } else {
        ctx.globalCompositeOperation = 'copy';
        ctx.globalAlpha = 1;
        const ri = Math.round(r * 255), gi = Math.round(g * 255), bi = Math.round(b * 255);
        ctx.fillStyle = 'rgba(' + ri + ',' + gi + ',' + bi + ',' + a + ')';
        ctx.fillRect(0, 0, w, h);
    }
    ctx.restore();
});

// plan_canvas.md CANVAS-25: real, synchronous ctx.getImageData() readback against whichever
// context is currently bound -- Canvas2D's getImageData is genuinely synchronous (Design
// decision 3), no faking/async round-trip needed. Writes w*h*4 RGBA8 bytes to outPixels.
EM_JS(void, CNA_Canvas2D_ReadCurrentPixels, (int x, int y, int w, int h, uint8_t* outPixels), {
    CNA_Canvas2D_EnsureMainContext();
    const ctx = Module['cnaCurrentCtx'];
    if (!ctx) return;
    const imageData = ctx.getImageData(x, y, w, h);
    HEAPU8.set(imageData.data, outPixels);
});

// plan_canvas.md CANVAS-40/41/Design decision 5: caches the globalCompositeOperation string
// CanvasSpriteBatchBackend's own DrawSprite EM_JS function applies to its final blit, plus a
// separate needsUnpremultiply flag (Module['cnaNeedsUnpremultiply']) -- the composite-op STRING is
// the same 'source-over' for both AlphaBlend and NonPremultiplied (Canvas2D has only one alpha-blend
// composite operator), but only AlphaBlend's source data needs the per-pixel un-premultiply pass
// DrawSprite applies before compositing (see CanvasCompositeOp's own doc comment). opCode: 0 =
// 'copy' (Opaque), 1 = 'source-over'/no unpremultiply (NonPremultiplied), 2 = 'source-over'/
// unpremultiply (AlphaBlend), 3 = 'lighter' (Additive).
EM_JS(void, CNA_Canvas2D_SetCompositeOp, (int opCode), {
    const ops = ['copy', 'source-over', 'source-over', 'lighter'];
    Module['cnaCompositeOp'] = ops[opCode] || 'source-over';
    Module['cnaNeedsUnpremultiply'] = (opCode === 2);
});

// plan_canvas.md CANVAS-22: restores the main canvas as the current draw/clear/read target --
// the counterpart of CanvasRenderTargetBackend.cpp's CNA_Canvas2D_BindRenderTarget(id).
EM_JS(void, CNA_Canvas2D_UnbindRenderTarget, (), {
    CNA_Canvas2D_EnsureMainContext();
    Module['cnaCurrentCtx'] = Module['cnaMainCtx'];
});
#endif

namespace CNA::Internal::Backends::Canvas
{
    namespace
    {
        // Phase C1's NotYetImplemented() placeholder (Clear/Present/CreateTexture/CreateSpriteBatch)
        // has no remaining callers -- Phases C2-C4 replaced all of them with real implementations.
        // Only the inherently-3D-only surface still throws, via ThrowNo3D() below (permanent, not a
        // placeholder -- Phase C7 just formalizes/audits this same wiring).
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

    std::unique_ptr<ITextureBackend> CanvasGraphicsBackend::CreateTexture(const ImageData& data)
    {
        return std::make_unique<CanvasTextureBackend>(data);
    }

    std::unique_ptr<ISpriteBatchBackend> CanvasGraphicsBackend::CreateSpriteBatch()
    {
        return std::make_unique<CanvasSpriteBatchBackend>();
    }

    std::unique_ptr<IRenderTargetBackend> CanvasGraphicsBackend::CreateRenderTarget2D(
        int w, int h, int /*depthFormat*/, bool /*preserveContents*/, bool /*mipMap*/, int /*multiSampleCount*/)
    {
        // depthFormat/mipMap/multiSampleCount are all ignored, same as SDL_RENDERER's own
        // CreateRenderTarget2D: Canvas2D has no depth buffer (CANVAS-23), no native mip chain
        // (CANVAS-21), and no MSAA control on its 2D blit pipeline.
        return std::make_unique<CanvasRenderTargetBackend>(w, h);
    }

    void CanvasGraphicsBackend::SetRenderTarget2D(IRenderTargetBackend* rt)
    {
        if (rt)
        {
            rt->BindAsRenderTarget();
        }
        else
        {
#if defined(__EMSCRIPTEN__)
            CNA_Canvas2D_UnbindRenderTarget();
#endif
        }
    }

    void CanvasGraphicsBackend::SetRenderTargets(IRenderTargetBackend* const* rts, int count)
    {
        if (count > 1)
            throw std::runtime_error(
                "Canvas (HTML Canvas 2D) does not support multiple simultaneous render targets "
                "(MRT): requested " + std::to_string(count) + ", but a CanvasRenderingContext2D "
                "is inherently single-target.");
        SetRenderTarget2D(count > 0 ? rts[0] : nullptr);
    }

    void CanvasGraphicsBackend::ReadBackbuffer(int x, int y, int w, int h, uint8_t* pixels)
    {
#if defined(__EMSCRIPTEN__)
        CNA_Canvas2D_ReadCurrentPixels(x, y, w, h, pixels);
#else
        (void)x; (void)y; (void)w; (void)h; (void)pixels;
#endif
    }

    CanvasCompositeOp BlendStateToCompositeOp(int colorSrcBlend, int alphaSrcBlend,
                                              int colorDstBlend, int alphaDstBlend,
                                              int colorBlendFunc, int alphaBlendFunc)
    {
        // Raw Blend/BlendFunction enum values, same table SdlGraphicsBackend::ToSdlBlendFactor/
        // ToSdlBlendOperation use: One=0, Zero=1, SourceAlpha=4, InverseSourceAlpha=5; Add=0.
        const bool isAdd = colorBlendFunc == 0 && alphaBlendFunc == 0;
        const bool symmetric = colorSrcBlend == alphaSrcBlend && colorDstBlend == alphaDstBlend;

        if (isAdd && symmetric && colorSrcBlend == 0 && colorDstBlend == 1)
        {
            return CanvasCompositeOp::Copy; // Opaque -> 'copy'
        }
        if (isAdd && symmetric && colorSrcBlend == 0 && colorDstBlend == 5)
        {
            // AlphaBlend: srcBlend=One assumes the source color is already premultiplied by its
            // own alpha (real hardware's blend unit just adds it in as-is). Canvas2D's
            // 'source-over' always treats drawImage/fillStyle input as STRAIGHT alpha and
            // premultiplies it internally before compositing -- so genuinely premultiplied source
            // data (SDL_RENDERER's own Task 697 pixel test constructs exactly this) would get
            // multiplied by its own alpha a SECOND time, darkening semi-transparent regions.
            // CanvasSpriteBatchBackend.cpp's DrawSprite un-premultiplies (divides RGB by alpha)
            // before handing pixels to 'source-over' specifically when this op is active, so the
            // net result matches feeding truly-premultiplied data into a srcBlend=One equation.
            return CanvasCompositeOp::AlphaBlendSourceOver;
        }
        if (isAdd && symmetric && colorSrcBlend == 4 && colorDstBlend == 5)
        {
            // NonPremultiplied: srcBlend=SourceAlpha assumes straight (non-premultiplied) source
            // color -- exactly what Canvas2D's 'source-over' already assumes natively, so no
            // extra per-pixel processing is needed here.
            return CanvasCompositeOp::NonPremultipliedSourceOver;
        }
        if (isAdd && symmetric && colorSrcBlend == 4 && colorDstBlend == 0)
        {
            return CanvasCompositeOp::Lighter; // Additive -> 'lighter'
        }
        throw std::runtime_error(
            "Canvas (HTML Canvas 2D) only supports the 4 standard BlendState presets "
            "(Opaque/AlphaBlend/NonPremultiplied/Additive): globalCompositeOperation has no "
            "generic blend-factor/equation model to express an arbitrary custom BlendState.");
    }

    void CanvasGraphicsBackend::ApplyBlendState(int colorSrcBlend, int alphaSrcBlend,
                                                 int colorDstBlend, int alphaDstBlend,
                                                 int colorBlendFunc, int alphaBlendFunc)
    {
        const CanvasCompositeOp op = BlendStateToCompositeOp(
            colorSrcBlend, alphaSrcBlend, colorDstBlend, alphaDstBlend, colorBlendFunc, alphaBlendFunc);
#if defined(__EMSCRIPTEN__)
        CNA_Canvas2D_SetCompositeOp(static_cast<int>(op));
#else
        (void)op;
#endif
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
