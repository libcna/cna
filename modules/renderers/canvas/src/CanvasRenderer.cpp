#include "CNA/Internal/Renderers/Canvas/CanvasRenderer.hpp"
#include "CNA/Internal/Renderers/Canvas/CanvasTextureRenderer.hpp"
#include "CNA/Internal/Renderers/Canvas/CanvasRenderTargetRenderer.hpp"
#include "CNA/Internal/Renderers/Canvas/CanvasSpriteBatchRenderer.hpp"
#include "CNA/Internal/Renderers/Common/NoOp3DResources.hpp"

#include <cmath>
#include <stdexcept>

#if defined(__EMSCRIPTEN__)
#include <emscripten.h>

// plans/plan_canvas.md CANVAS-10/Design decision 2: shared JS-side helper that locates the existing DOM
// <canvas> element (Module['canvas'] || document.querySelector('canvas'), the same lookup
// EasyGLRenderer.cpp already uses for its own GL context) and caches its '2d' context on
// Module['cnaMainCtx']. Module['cnaCurrentCtx'] is "whichever context Clear()/Draw() currently
// target" -- the main canvas by default, or a bound CanvasRenderTargetRenderer's own off-screen
// context (see CanvasRenderTargetRenderer.cpp's Bind/UnbindAsRenderTarget, Phase C3), mirroring
// the retained target model of "operate on whatever is currently bound". EM_JS-declared
// functions are ordinary top-level JS functions in the generated glue code, so every Canvas2D EM_JS
// function in this renderer (across every .cpp -- textures/render targets included) can call this
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

// plans/plan_canvas.md CANVAS-11: real Clear() against whichever context is currently bound (main canvas
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

// plans/plan_canvas.md CANVAS-25: real, synchronous ctx.getImageData() readback against whichever
// context is currently bound -- Canvas2D's getImageData is genuinely synchronous (Design
// decision 3), no faking/async round-trip needed. Writes w*h*4 RGBA8 bytes to outPixels.
EM_JS(void, CNA_Canvas2D_ReadCurrentPixels, (int x, int y, int w, int h, uint8_t* outPixels), {
    CNA_Canvas2D_EnsureMainContext();
    const ctx = Module['cnaCurrentCtx'];
    if (!ctx) return;
    const imageData = ctx.getImageData(x, y, w, h);
    HEAPU8.set(imageData.data, outPixels);
});

// plans/plan_canvas.md CANVAS-40/41/Design decision 5: caches the globalCompositeOperation string
// CanvasSpriteBatchRenderer's own DrawSprite EM_JS function applies to its final blit, plus a
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

// plans/plan_canvas.md CANVAS-22: restores the main canvas as the current draw/clear/read target --
// the counterpart of CanvasRenderTargetRenderer.cpp's CNA_Canvas2D_BindRenderTarget(id).
EM_JS(void, CNA_Canvas2D_UnbindRenderTarget, (), {
    CNA_Canvas2D_EnsureMainContext();
    Module['cnaCurrentCtx'] = Module['cnaMainCtx'];
});
#endif

namespace CNA::Internal::Renderers::Canvas
{
    CanvasRenderer::CanvasRenderer(const GraphicsRendererCreateArgs& args)
        : surface_(args.surface)
        , virtualWidth_(args.virtualWidth)
        , virtualHeight_(args.virtualHeight)
        , presentationMode_(args.presentationMode)
    {
        if (surface_.windowId == 0)
            throw std::runtime_error("CanvasRenderer initialized without a platform window.");
        if (!(surface_.displayScale > 0.0f) || !std::isfinite(surface_.displayScale))
            surface_.displayScale = 1.0f;
        IGraphicsRenderer::RegisterForWindow(surface_.windowId, this);
    }

    CanvasRenderer::~CanvasRenderer()
    {
        IGraphicsRenderer::UnregisterForWindow(surface_.windowId);
    }

    void CanvasRenderer::Clear(float r, float g, float b, float a)
    {
#if defined(__EMSCRIPTEN__)
        CNA_Canvas2D_Clear(r, g, b, a);
#else
        (void)r; (void)g; (void)b; (void)a;
#endif
    }

    void CanvasRenderer::Present()
    {
        // plans/plan_canvas.md CANVAS-12: no-op. The browser compositor presents the canvas
        // automatically on the next paint tick; there is nothing for CNA to flush or swap.
    }

    void CanvasRenderer::getWindowSize(int& width, int& height) const
    {
        width = static_cast<int>(std::lround(surface_.drawableSize.width / surface_.displayScale));
        height = static_cast<int>(std::lround(surface_.drawableSize.height / surface_.displayScale));
    }

    void CanvasRenderer::getLogicalSize(int& width, int& height) const
    {
        if (virtualHeight_ <= 0)
        {
            getWindowSize(width, height);
            return;
        }
        int physW, physH;
        getWindowSize(physW, physH);
        height = virtualHeight_;
        if (presentationMode_ == CnaPresentationMode::FixedHeightDynamicWidth && physH > 0)
            width = static_cast<int>(static_cast<double>(physW) * virtualHeight_ / physH + 0.5);
        else
            width = virtualWidth_ > 0 ? virtualWidth_ : physW;
    }

    void CanvasRenderer::GetViewportSize(int& width, int& height)
    {
        getLogicalSize(width, height);
    }

    void CanvasRenderer::SetVirtualResolution(int width, int height)
    {
        virtualWidth_ = width;
        virtualHeight_ = height;
    }

    void CanvasRenderer::SetPresentationMode(int mode)
    {
        presentationMode_ = static_cast<CnaPresentationMode>(mode);
    }

    void CanvasRenderer::OnSurfaceChanged(const RendererSurfaceInfo& surface)
    {
        if (surface.windowId != surface_.windowId)
            throw std::runtime_error("CanvasRenderer surface window id changed.");
        surface_ = surface;
        if (!(surface_.displayScale > 0.0f) || !std::isfinite(surface_.displayScale))
            surface_.displayScale = 1.0f;
    }

    bool CanvasRenderer::TransformWindowToLogical(float windowX, float windowY,
                                                          float& logX, float& logY) const
    {
        if (virtualHeight_ <= 0) return false;
        int physW, physH;
        getWindowSize(physW, physH);
        if (physH <= 0) return false;
        const float scale = static_cast<float>(virtualHeight_) / static_cast<float>(physH);
        logX = windowX * scale;
        logY = windowY * scale;
        return true;
    }

    bool CanvasRenderer::TransformLogicalToWindow(float logX, float logY,
                                                         float& windowX, float& windowY) const
    {
        // Inverse of TransformWindowToLogical -- see EasyGLRenderer::TransformLogicalToWindow
        // for why this is an exact, offset-free uniform scale under the default
        // FixedHeightDynamicWidth presentation mode (no letterbox bars to account for).
        if (virtualHeight_ <= 0) return false;
        int physW, physH;
        getWindowSize(physW, physH);
        if (physH <= 0) return false;
        const float invScale = static_cast<float>(physH) / static_cast<float>(virtualHeight_);
        windowX = logX * invScale;
        windowY = logY * invScale;
        return true;
    }

    std::unique_ptr<ITextureRenderer> CanvasRenderer::CreateTexture(const ImageData& data)
    {
        return std::make_unique<CanvasTextureRenderer>(data);
    }

    std::unique_ptr<ISpriteBatchRenderer> CanvasRenderer::CreateSpriteBatch()
    {
        return std::make_unique<CanvasSpriteBatchRenderer>(state_);
    }

    std::unique_ptr<IRenderTargetRenderer> CanvasRenderer::CreateRenderTarget2D(
        int w, int h, int /*depthFormat*/, bool /*preserveContents*/, bool /*mipMap*/, int /*multiSampleCount*/)
    {
        // depthFormat/mipMap/multiSampleCount are all ignored, same as the native 2D renderer's
        // CreateRenderTarget2D: Canvas2D has no depth buffer (CANVAS-23), no native mip chain
        // (CANVAS-21), and no MSAA control on its 2D blit pipeline.
        return std::make_unique<CanvasRenderTargetRenderer>(w, h);
    }

    void CanvasRenderer::SetRenderTarget2D(IRenderTargetRenderer* rt)
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

    void CanvasRenderer::SetRenderTargets(
        const RenderTargetBindingDescriptor* renderTargets, int count)
    {
        if (count > 1)
            throw std::runtime_error(
                "Canvas (HTML Canvas 2D) does not support multiple simultaneous render targets "
                "(MRT): requested " + std::to_string(count) + ", but a CanvasRenderingContext2D "
                "is inherently single-target.");
        if (count > 0 && renderTargets[0].IsRenderTargetCubeFace())
            throw std::runtime_error(
                "Canvas (HTML Canvas 2D) does not support RenderTargetCube face bindings.");
        SetRenderTarget2D(
            count > 0 ? renderTargets[0].GetRenderTarget2D() : nullptr);
    }

    void CanvasRenderer::ReadBackbuffer(int x, int y, int w, int h, uint8_t* pixels)
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
        // Raw Blend/BlendFunction enum values, matching the native renderer table:
        // One=0, Zero=1, SourceAlpha=4, InverseSourceAlpha=5; Add=0.
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
            // data (the native renderer's Task 697 pixel test constructs exactly this) would get
            // multiplied by its own alpha a SECOND time, darkening semi-transparent regions.
            // CanvasSpriteBatchRenderer.cpp's DrawSprite un-premultiplies (divides RGB by alpha)
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

    void CanvasRenderer::ApplyBlendState(int colorSrcBlend, int alphaSrcBlend,
                                                 int colorDstBlend, int alphaDstBlend,
                                                 int colorBlendFunc, int alphaBlendFunc,
                                                 const BlendWriteState& /*writeState*/)
    {
        // REMED-GFX-077: HTML5 Canvas 2D exposes only globalCompositeOperation — there is no API
        // to mask individual R/G/B/A channels and no coverage-sample-mask concept. Both
        // BlendState.ColorWriteChannels* and BlendState.MultiSampleMask are inexpressible here
        // (documented capability gap, not a silent drop).
        const CanvasCompositeOp op = BlendStateToCompositeOp(
            colorSrcBlend, alphaSrcBlend, colorDstBlend, alphaDstBlend, colorBlendFunc, alphaBlendFunc);
        state_->compositeOp = op;
#if defined(__EMSCRIPTEN__)
        CNA_Canvas2D_SetCompositeOp(static_cast<int>(op));
#else
        (void)op;
#endif
    }

    // ---- 3D: HTML Canvas 2D is intentionally 2D-only. ----
    // Throw remains the default. WarnAndStub logs each method once and returns safely.
    void CanvasRenderer::ClearColorAndDepth(float, float, float, float, float) { HandleUnsupported3DCall("Canvas (HTML Canvas 2D)", "ClearColorAndDepth"); }
    void CanvasRenderer::ClearDepth(float) { HandleUnsupported3DCall("Canvas (HTML Canvas 2D)", "ClearDepth"); }
    void CanvasRenderer::ClearStencil(int) { HandleUnsupported3DCall("Canvas (HTML Canvas 2D)", "ClearStencil"); }
    void CanvasRenderer::ClearDepthAndStencil(float, int) { HandleUnsupported3DCall("Canvas (HTML Canvas 2D)", "ClearDepthAndStencil"); }
    void CanvasRenderer::ClearColorAndStencil(float, float, float, float, int) { HandleUnsupported3DCall("Canvas (HTML Canvas 2D)", "ClearColorAndStencil"); }
    void CanvasRenderer::ClearColorDepthAndStencil(float, float, float, float, float, int) { HandleUnsupported3DCall("Canvas (HTML Canvas 2D)", "ClearColorDepthAndStencil"); }
    void CanvasRenderer::SetDepthTestEnabled(bool) { HandleUnsupported3DCall("Canvas (HTML Canvas 2D)", "SetDepthTestEnabled"); }
    void CanvasRenderer::SetBlendEnabled(bool) { HandleUnsupported3DCall("Canvas (HTML Canvas 2D)", "SetBlendEnabled"); }
    void CanvasRenderer::SetDepthWriteEnabled(bool) { HandleUnsupported3DCall("Canvas (HTML Canvas 2D)", "SetDepthWriteEnabled"); }

    std::unique_ptr<IVertexBufferRenderer> CanvasRenderer::CreateVertexBuffer(
        int vertexCapacity)
    {
        HandleUnsupported3DCall("Canvas (HTML Canvas 2D)", "CreateVertexBuffer");
        return std::make_unique<NoOpVertexBufferRenderer>(vertexCapacity);
    }

    std::unique_ptr<IIndexBufferRenderer> CanvasRenderer::CreateIndexBuffer16(
        int indexCapacity)
    {
        HandleUnsupported3DCall("Canvas (HTML Canvas 2D)", "CreateIndexBuffer16");
        return std::make_unique<NoOpIndexBufferRenderer>(indexCapacity);
    }

    std::unique_ptr<IOcclusionQueryRenderer> CanvasRenderer::CreateOcclusionQuery()
    {
        if (!ShouldStubUnsupported3DResource())
            return nullptr;
        HandleUnsupported3DCall("Canvas (HTML Canvas 2D)", "CreateOcclusionQuery");
        return std::make_unique<NoOpOcclusionQueryRenderer>();
    }

    std::unique_ptr<ITexture3DRenderer> CanvasRenderer::CreateTexture3D(
        int width, int height, int depth, bool, int)
    {
        if (!ShouldStubUnsupported3DResource())
            return nullptr;
        HandleUnsupported3DCall("Canvas (HTML Canvas 2D)", "CreateTexture3D");
        return std::make_unique<NoOpTexture3DRenderer>(width, height, depth);
    }

    std::unique_ptr<ITextureCubeRenderer> CanvasRenderer::CreateTextureCube(
        int size, bool, int)
    {
        if (!ShouldStubUnsupported3DResource())
            return nullptr;
        HandleUnsupported3DCall("Canvas (HTML Canvas 2D)", "CreateTextureCube");
        return std::make_unique<NoOpTextureCubeRenderer>(size);
    }

    std::unique_ptr<IRenderTargetCubeRenderer> CanvasRenderer::CreateRenderTargetCube(
        int size, int, bool, bool, int)
    {
        if (!ShouldStubUnsupported3DResource())
            return nullptr;
        HandleUnsupported3DCall("Canvas (HTML Canvas 2D)", "CreateRenderTargetCube");
        return std::make_unique<NoOpRenderTargetCubeRenderer>(size);
    }

    void CanvasRenderer::DrawColoredPrimitives(const IVertexBufferRenderer&,
                                                      const Matrix&, const Matrix&, const Matrix&,
                                                      PrimitiveType, int) { HandleUnsupported3DCall("Canvas (HTML Canvas 2D)", "DrawColoredPrimitives"); }

    void CanvasRenderer::DrawIndexedColoredPrimitives(const IVertexBufferRenderer&, const IIndexBufferRenderer&,
                                                             const Matrix&, const Matrix&, const Matrix&,
                                                             PrimitiveType, int) { HandleUnsupported3DCall("Canvas (HTML Canvas 2D)", "DrawIndexedColoredPrimitives"); }
}

namespace CNA::Internal::Renderers
{
    // plans/plan_runtimerenderer.md design decision 4: declared in this family's own
    // namespace so several renderer archives can link into one binary, then defined
    // below with a qualified name -- the body keeps its place unchanged.
    namespace Canvas { std::unique_ptr<IGraphicsRenderer> CreateGraphicsRenderer(const GraphicsRendererCreateArgs& args); }

    std::unique_ptr<IGraphicsRenderer> Canvas::CreateGraphicsRenderer(const GraphicsRendererCreateArgs& args)
    {
        return std::make_unique<Canvas::CanvasRenderer>(args);
    }
}
