#include "CNA/Internal/Renderers/PixiJs/PixiJsRenderer.hpp"
#include "CNA/Internal/Renderers/PixiJs/PixiJsTextureRenderer.hpp"
#include "CNA/Internal/Renderers/PixiJs/PixiJsRenderTargetRenderer.hpp"
#include "CNA/Internal/Renderers/PixiJs/PixiJsSpriteBatchRenderer.hpp"
#include "CNA/Internal/Renderers/Common/NoOp3DResources.hpp"

#include <SDL3/SDL.h>

#include <stdexcept>

#if defined(__EMSCRIPTEN__)
#include <emscripten.h>

// plan_pixijs.md Design decision 2: shared JS-side helper that locates the existing DOM <canvas>
// element (the identical Module['canvas'] || document.querySelector('canvas') lookup
// EasyGLRenderer.cpp/CanvasRenderer.cpp already use) and constructs exactly one PIXI.Application
// against it, cached on Module['cnaPixiApp']. plan_pixijs.md PIXIJS-22: Present() drives rendering
// explicitly (autoStart:false, sharedTicker:false) so CNA's own Game loop stays authoritative over
// frame timing, the same relationship it has with every other renderer -- PixiJS's own ticker is
// never started.
EM_JS(void, CNA_PixiJs_EnsureApp, (), {
    if (Module['cnaPixiApp']) return;
    const canvas = Module['canvas'] || document.querySelector('canvas');
    if (!canvas) { console.error('[CNA] PixiJS: no <canvas> element found'); return; }
    if (typeof PIXI === 'undefined') { console.error('[CNA] PixiJS: PIXI global is not defined -- was the vendored pixi.min.js linked via --pre-js?'); return; }
    const app = new PIXI.Application({
        view: canvas,
        width: canvas.width,
        height: canvas.height,
        autoStart: false,
        sharedTicker: false,
        backgroundAlpha: 1,
    });
    Module['cnaPixiApp'] = app;
    Module['cnaPixiTextures'] = Module['cnaPixiTextures'] || {};
    Module['cnaPixiSpritePool'] = Module['cnaPixiSpritePool'] || [];
    // The active render target: the app's own stage/renderer by default, or a bound
    // PixiJsRenderTargetRenderer's own container/RenderTexture pair (see
    // PixiJsRenderTargetRenderer.cpp's Bind/UnbindAsRenderTarget).
    Module['cnaPixiActiveContainer'] = app.stage;
    Module['cnaPixiActiveRenderTexture'] = null;
});

EM_JS(void, CNA_PixiJs_Clear, (double r, double g, double b, double a), {
    CNA_PixiJs_EnsureApp();
    const app = Module['cnaPixiApp'];
    if (!app) return;
    // plan_pixijs.md Design decision 9's counterpart for Clear(): drop every sprite queued so far
    // against the active container (same "Clear() resets the frame" semantics
    // plan_html_dom.md Design decision 9 established) and set the background colour PixiJS itself
    // clears to on the next render() call.
    const container = Module['cnaPixiActiveContainer'];
    if (container && container.children) container.removeChildren();
    const colorHex = (Math.round(r * 255) << 16) | (Math.round(g * 255) << 8) | Math.round(b * 255);
    if (Module['cnaPixiActiveRenderTexture']) {
        app.renderer.render(container, { renderTexture: Module['cnaPixiActiveRenderTexture'], clear: true });
    } else {
        app.renderer.background.color = colorHex;
        app.renderer.background.alpha = a;
    }
});

// plan_pixijs.md PIXIJS-22: the one explicit render() call per CNA frame -- PixiJS's own ticker is
// never started (autoStart:false/sharedTicker:false above), so nothing renders unless CNA's Game
// loop asks for it here.
EM_JS(void, CNA_PixiJs_Render, (), {
    const app = Module['cnaPixiApp'];
    if (!app) return;
    if (Module['cnaPixiActiveRenderTexture']) {
        app.renderer.render(Module['cnaPixiActiveContainer'], { renderTexture: Module['cnaPixiActiveRenderTexture'] });
    } else {
        app.renderer.render(app.stage);
    }
});

// plan_pixijs.md Design decision 9: app.renderer.extract.pixels() is PixiJS's own supported,
// genuinely synchronous readback API -- no getImageData-style plumbing needed. Writes w*h*4 RGBA8
// bytes to outPixels.
EM_JS(void, CNA_PixiJs_ReadCurrentPixels, (int x, int y, int w, int h, uint8_t* outPixels), {
    const app = Module['cnaPixiApp'];
    if (!app) return;
    const target = Module['cnaPixiActiveRenderTexture'] || undefined;
    const pixels = app.renderer.extract.pixels(target);
    // extract.pixels() returns the FULL target's pixels; slice the requested x/y/w/h rectangle out
    // of it rather than assuming (x,y) is always (0,0).
    const fullWidth = target ? target.width : app.renderer.width;
    const bytesPerRow = w * 4;
    for (let row = 0; row < h; ++row) {
        const srcOffset = ((y + row) * fullWidth + x) * 4;
        HEAPU8.set(pixels.subarray(srcOffset, srcOffset + bytesPerRow), outPixels + row * bytesPerRow);
    }
});

// plan_pixijs.md Design decision 6: caches the active PIXI.BLEND_MODES value (or, for the future
// PIXIJS-52 custom-blend-mode path, a registered custom id) that PixiJsSpriteBatchRenderer's own
// flush applies to each pooled sprite. blendCode: 0=Opaque, 1=AlphaBlend, 2=NonPremultiplied,
// 3=Additive -- mirrors PixiJsBlendMode's own numbering (PixiJsRenderer.hpp).
EM_JS(void, CNA_PixiJs_SetBlendMode, (int blendCode), {
    // PIXI.BLEND_MODES.NORMAL === 0, .ADD === 1 in PixiJS v7. Opaque/AlphaBlend/NonPremultiplied
    // all render via NORMAL in this v1 scope (Design decision 6's 4-preset boundary); only Additive
    // gets a distinct native blend mode. Opaque's real (One,Zero) semantics and
    // AlphaBlend/NonPremultiplied's premultiply distinction are not yet applied here -- tracked as
    // PIXIJS-50/51, not silently assumed equivalent to NORMAL.
    const modes = [0, 0, 0, 1];
    Module['cnaPixiBlendMode'] = modes[blendCode] !== undefined ? modes[blendCode] : 0;
});

EM_JS(void, CNA_PixiJs_UnbindRenderTarget, (), {
    const app = Module['cnaPixiApp'];
    if (!app) return;
    Module['cnaPixiActiveContainer'] = app.stage;
    Module['cnaPixiActiveRenderTexture'] = null;
});
#endif

namespace CNA::Internal::Renderers::PixiJs
{
    PixiJsRenderer::PixiJsRenderer(SDL_Window* window, int virtualWidth, int virtualHeight,
                                   CnaPresentationMode mode)
        : window_(window)
        , virtualWidth_(virtualWidth)
        , virtualHeight_(virtualHeight)
        , presentationMode_(mode)
    {
        if (!window_) throw std::runtime_error("PixiJsRenderer initialized with null window.");
        IGraphicsRenderer::RegisterForWindow(window_, this);
    }

    PixiJsRenderer::~PixiJsRenderer()
    {
        IGraphicsRenderer::UnregisterForWindow(window_);
    }

    void PixiJsRenderer::Clear(float r, float g, float b, float a)
    {
#if defined(__EMSCRIPTEN__)
        CNA_PixiJs_Clear(r, g, b, a);
#else
        (void)r; (void)g; (void)b; (void)a;
#endif
    }

    void PixiJsRenderer::Present()
    {
#if defined(__EMSCRIPTEN__)
        CNA_PixiJs_Render();
#endif
    }

    void PixiJsRenderer::getLogicalSize(int& width, int& height) const
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

    void PixiJsRenderer::GetViewportSize(int& width, int& height)
    {
        getLogicalSize(width, height);
    }

    void PixiJsRenderer::SetVirtualResolution(int width, int height)
    {
        virtualWidth_ = width;
        virtualHeight_ = height;
    }

    void PixiJsRenderer::SetPresentationMode(int mode)
    {
        presentationMode_ = static_cast<CnaPresentationMode>(mode);
    }

    bool PixiJsRenderer::TransformWindowToLogical(float windowX, float windowY,
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

    bool PixiJsRenderer::TransformLogicalToWindow(float logX, float logY,
                                                  float& windowX, float& windowY) const
    {
        if (virtualHeight_ <= 0) return false;
        int physW, physH;
        SDL_GetWindowSize(window_, &physW, &physH);
        if (physH <= 0) return false;
        const float invScale = static_cast<float>(physH) / static_cast<float>(virtualHeight_);
        windowX = logX * invScale;
        windowY = logY * invScale;
        return true;
    }

    std::unique_ptr<ITextureRenderer> PixiJsRenderer::CreateTexture(const ImageData& data)
    {
        return std::make_unique<PixiJsTextureRenderer>(data);
    }

    std::unique_ptr<ISpriteBatchRenderer> PixiJsRenderer::CreateSpriteBatch()
    {
        return std::make_unique<PixiJsSpriteBatchRenderer>(state_);
    }

    std::unique_ptr<IRenderTargetRenderer> PixiJsRenderer::CreateRenderTarget2D(
        int w, int h, int /*depthFormat*/, bool /*preserveContents*/, bool /*mipMap*/, int /*multiSampleCount*/)
    {
        // depthFormat/mipMap/multiSampleCount are ignored in this v1 scope, same boundary
        // CANVAS-23/CANVAS-21 drew for their own render targets (plan_pixijs.md PIXIJS-34).
        return std::make_unique<PixiJsRenderTargetRenderer>(w, h);
    }

    void PixiJsRenderer::SetRenderTarget2D(IRenderTargetRenderer* rt)
    {
        if (rt)
        {
            rt->BindAsRenderTarget();
        }
        else
        {
#if defined(__EMSCRIPTEN__)
            CNA_PixiJs_UnbindRenderTarget();
#endif
        }
    }

    void PixiJsRenderer::SetRenderTargets(
        const RenderTargetBindingDescriptor* renderTargets, int count)
    {
        if (count > 1)
            throw std::runtime_error(
                "PixiJS does not support multiple simultaneous render targets (MRT): requested " +
                std::to_string(count) + ", but this renderer's v1 scope targets one "
                "PIXI.RenderTexture at a time.");
        if (count > 0 && renderTargets[0].IsRenderTargetCubeFace())
            throw std::runtime_error("PixiJS does not support RenderTargetCube face bindings.");
        SetRenderTarget2D(count > 0 ? renderTargets[0].GetRenderTarget2D() : nullptr);
    }

    void PixiJsRenderer::ReadBackbuffer(int x, int y, int w, int h, uint8_t* pixels)
    {
#if defined(__EMSCRIPTEN__)
        CNA_PixiJs_ReadCurrentPixels(x, y, w, h, pixels);
#else
        (void)x; (void)y; (void)w; (void)h; (void)pixels;
#endif
    }

    PixiJsBlendMode BlendStateToPixiJsBlendMode(int colorSrcBlend, int alphaSrcBlend,
                                                int colorDstBlend, int alphaDstBlend,
                                                int colorBlendFunc, int alphaBlendFunc)
    {
        // Raw Blend/BlendFunction enum values, same table CanvasRenderer::BlendStateToCompositeOp
        // uses: One=0, Zero=1, SourceAlpha=4, InverseSourceAlpha=5; Add=0.
        const bool isAdd = colorBlendFunc == 0 && alphaBlendFunc == 0;
        const bool symmetric = colorSrcBlend == alphaSrcBlend && colorDstBlend == alphaDstBlend;

        if (isAdd && symmetric && colorSrcBlend == 0 && colorDstBlend == 1)
            return PixiJsBlendMode::Opaque;
        if (isAdd && symmetric && colorSrcBlend == 0 && colorDstBlend == 5)
            return PixiJsBlendMode::AlphaBlend;
        if (isAdd && symmetric && colorSrcBlend == 4 && colorDstBlend == 5)
            return PixiJsBlendMode::NonPremultiplied;
        if (isAdd && symmetric && colorSrcBlend == 4 && colorDstBlend == 0)
            return PixiJsBlendMode::Additive;
        throw std::runtime_error(
            "PixiJS (v1 scope) only supports the 4 standard BlendState presets "
            "(Opaque/AlphaBlend/NonPremultiplied/Additive) -- plan_pixijs.md PIXIJS-52 tracks a "
            "future fully-generic mapping via custom PixiJS blend-mode registration.");
    }

    void PixiJsRenderer::ApplyBlendState(int colorSrcBlend, int alphaSrcBlend,
                                         int colorDstBlend, int alphaDstBlend,
                                         int colorBlendFunc, int alphaBlendFunc,
                                         const BlendWriteState& /*writeState*/)
    {
        const PixiJsBlendMode mode = BlendStateToPixiJsBlendMode(
            colorSrcBlend, alphaSrcBlend, colorDstBlend, alphaDstBlend, colorBlendFunc, alphaBlendFunc);
        state_->blendMode = mode;
#if defined(__EMSCRIPTEN__)
        CNA_PixiJs_SetBlendMode(static_cast<int>(mode));
#else
        (void)mode;
#endif
    }

    // ---- 3D: PixiJS's v1 scope is intentionally 2D-only (see plan_pixijs.md's own scope note --
    // unlike CANVAS/HTML_DOM this is a deliberate boundary, not a structural one). ----
    void PixiJsRenderer::ClearColorAndDepth(float, float, float, float, float) { HandleUnsupported3DCall("PixiJS", "ClearColorAndDepth"); }
    void PixiJsRenderer::ClearDepth(float) { HandleUnsupported3DCall("PixiJS", "ClearDepth"); }
    void PixiJsRenderer::ClearStencil(int) { HandleUnsupported3DCall("PixiJS", "ClearStencil"); }
    void PixiJsRenderer::ClearDepthAndStencil(float, int) { HandleUnsupported3DCall("PixiJS", "ClearDepthAndStencil"); }
    void PixiJsRenderer::ClearColorAndStencil(float, float, float, float, int) { HandleUnsupported3DCall("PixiJS", "ClearColorAndStencil"); }
    void PixiJsRenderer::ClearColorDepthAndStencil(float, float, float, float, float, int) { HandleUnsupported3DCall("PixiJS", "ClearColorDepthAndStencil"); }
    void PixiJsRenderer::SetDepthTestEnabled(bool) { HandleUnsupported3DCall("PixiJS", "SetDepthTestEnabled"); }
    void PixiJsRenderer::SetBlendEnabled(bool) { HandleUnsupported3DCall("PixiJS", "SetBlendEnabled"); }
    void PixiJsRenderer::SetDepthWriteEnabled(bool) { HandleUnsupported3DCall("PixiJS", "SetDepthWriteEnabled"); }

    std::unique_ptr<IVertexBufferRenderer> PixiJsRenderer::CreateVertexBuffer(int vertexCapacity)
    {
        HandleUnsupported3DCall("PixiJS", "CreateVertexBuffer");
        return std::make_unique<NoOpVertexBufferRenderer>(vertexCapacity);
    }

    std::unique_ptr<IIndexBufferRenderer> PixiJsRenderer::CreateIndexBuffer16(int indexCapacity)
    {
        HandleUnsupported3DCall("PixiJS", "CreateIndexBuffer16");
        return std::make_unique<NoOpIndexBufferRenderer>(indexCapacity);
    }

    std::unique_ptr<IOcclusionQueryRenderer> PixiJsRenderer::CreateOcclusionQuery()
    {
        // plan_pixijs.md Design decision 11: PixiJS exposes no occlusion-query primitive; shared
        // IGraphicsRenderer nullptr default under Throw policy, matching CANVAS-66/HTML_DOM.
        if (!ShouldStubUnsupported3DResource())
            return nullptr;
        HandleUnsupported3DCall("PixiJS", "CreateOcclusionQuery");
        return std::make_unique<NoOpOcclusionQueryRenderer>();
    }

    std::unique_ptr<ITexture3DRenderer> PixiJsRenderer::CreateTexture3D(
        int width, int height, int depth, bool, int)
    {
        if (!ShouldStubUnsupported3DResource())
            return nullptr;
        HandleUnsupported3DCall("PixiJS", "CreateTexture3D");
        return std::make_unique<NoOpTexture3DRenderer>(width, height, depth);
    }

    std::unique_ptr<ITextureCubeRenderer> PixiJsRenderer::CreateTextureCube(int size, bool, int)
    {
        if (!ShouldStubUnsupported3DResource())
            return nullptr;
        HandleUnsupported3DCall("PixiJS", "CreateTextureCube");
        return std::make_unique<NoOpTextureCubeRenderer>(size);
    }

    std::unique_ptr<IRenderTargetCubeRenderer> PixiJsRenderer::CreateRenderTargetCube(
        int size, int, bool, bool, int)
    {
        if (!ShouldStubUnsupported3DResource())
            return nullptr;
        HandleUnsupported3DCall("PixiJS", "CreateRenderTargetCube");
        return std::make_unique<NoOpRenderTargetCubeRenderer>(size);
    }

    void PixiJsRenderer::DrawColoredPrimitives(const IVertexBufferRenderer&,
                                               const Matrix&, const Matrix&, const Matrix&,
                                               PrimitiveType, int) { HandleUnsupported3DCall("PixiJS", "DrawColoredPrimitives"); }

    void PixiJsRenderer::DrawIndexedColoredPrimitives(const IVertexBufferRenderer&, const IIndexBufferRenderer&,
                                                       const Matrix&, const Matrix&, const Matrix&,
                                                       PrimitiveType, int) { HandleUnsupported3DCall("PixiJS", "DrawIndexedColoredPrimitives"); }
}

namespace CNA::Internal::Renderers
{
    std::unique_ptr<IGraphicsRenderer> CreateGraphicsRenderer(const GraphicsRendererCreateArgs& args)
    {
        return std::make_unique<PixiJs::PixiJsRenderer>(
            args.window, args.virtualWidth, args.virtualHeight, args.presentationMode);
    }
}
