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

// REMED-PIXIJS-5 (found by the render-target smoke test, 2026-08-17): a bound render texture's
// Clear() used to render with `clear: true` but never actually told the renderer WHICH colour to
// clear to -- `app.renderer.background.color`/`.alpha` is the renderer-wide clear colour PixiJS's
// own render() call reads for ANY clear (main stage or a renderTexture target alike), and it was
// being left at whatever a PREVIOUS Clear() call (or the default black) had set it to, silently
// ignoring this call's own (r,g,b,a). Fixed by setting it unconditionally, before either path.
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
    app.renderer.background.color = colorHex;
    app.renderer.background.alpha = a;
    if (Module['cnaPixiActiveRenderTexture']) {
        // REMED-PIXIJS-5 (part 2, found via direct EM_JS trace logging, 2026-08-17):
        // render(container, {renderTexture, clear:true}) clears a renderTexture target to
        // transparent black UNCONDITIONALLY -- app.renderer.background (set above) only ever
        // affects the main canvas's own background, never an arbitrary render-to-texture call.
        // There is no per-call clear-colour option on PixiJS v7's render() for a renderTexture
        // target. Painted instead: a reusable 1x1 white sprite, tinted/sized to cover the whole
        // target and blended with BLEND_MODES.NONE (the same real "unconditional overwrite"
        // mechanism Opaque already uses, REMED-PIXIJS-3) -- guaranteed to replace every pixel
        // regardless of prior content, then removed so it isn't left queued for the next real
        // Draw() flush (Clear() itself already consumed it).
        if (!Module['cnaPixiClearSprite']) {
            const whiteBuffer = new Uint8Array([255, 255, 255, 255]);
            const whiteBase = new PIXI.BaseTexture(new PIXI.BufferResource(whiteBuffer, { width: 1, height: 1 }), {
                width: 1, height: 1, scaleMode: PIXI.SCALE_MODES.NEAREST, alphaMode: PIXI.ALPHA_MODES.NPM,
            });
            Module['cnaPixiClearSprite'] = new PIXI.Sprite(new PIXI.Texture(whiteBase));
        }
        const clearSprite = Module['cnaPixiClearSprite'];
        const target = Module['cnaPixiActiveRenderTexture'];
        clearSprite.anchor.set(0, 0);
        clearSprite.position.set(0, 0);
        clearSprite.scale.set(target.width, target.height);
        clearSprite.tint = colorHex;
        clearSprite.alpha = a;
        clearSprite.blendMode = 20; // PIXI.BLEND_MODES.NONE
        container.addChild(clearSprite);
        app.renderer.render(container, { renderTexture: target, clear: true });
        container.removeChild(clearSprite);
    }
    // The main-stage path needs no explicit render() here: app.renderer.background is already set
    // above, and Present()/GetBackBufferData's own force-render (REMED-PIXIJS-1) picks it up on the
    // next actual render() call.
});

// plan_pixijs.md PIXIJS-22: the one explicit render() call per CNA frame -- PixiJS's own ticker is
// never started (autoStart:false/sharedTicker:false above), so nothing renders unless CNA's Game
// loop asks for it here.
// REMED-PIXIJS-5 (part 4): `clear: false` on the render-texture branch, same reasoning as
// CNA_PixiJs_ReadTexturePixels's own fix -- render()'s default clear would otherwise wipe out
// whatever Clear() itself already painted into the target on every call, including ones where the
// active container has nothing new queued.
EM_JS(void, CNA_PixiJs_Render, (), {
    const app = Module['cnaPixiApp'];
    if (!app) return;
    if (Module['cnaPixiActiveRenderTexture']) {
        app.renderer.render(Module['cnaPixiActiveContainer'], { renderTexture: Module['cnaPixiActiveRenderTexture'], clear: false });
    } else {
        app.renderer.render(app.stage);
    }
});

// plan_pixijs.md Design decision 9: app.renderer.extract.pixels() is PixiJS's own supported,
// genuinely synchronous readback API -- no getImageData-style plumbing needed. Writes w*h*4 RGBA8
// bytes to outPixels.
//
// REMED-PIXIJS-1 (found by the first real browser run, 2026-08-17): PixiJS is retained-mode --
// SpriteBatch::Draw() only mutates pooled sprite properties (CNA_PixiJs_FlushSprites), it does not
// paint anything. XNA's GraphicsDevice::GetBackBufferData() is legitimately callable mid-frame,
// before the framework's own end-of-frame Present() (which is the only other place that calls
// render()) -- so reading extract.pixels() here without rendering first returned the PREVIOUS
// frame's stale backbuffer content (or a blank one on the first frame), not what was just drawn.
// Force a render immediately before extracting, mirroring every other CNA renderer's "a draw call's
// effect is visible to an immediate readback" contract.
EM_JS(void, CNA_PixiJs_ReadCurrentPixels, (int x, int y, int w, int h, uint8_t* outPixels), {
    const app = Module['cnaPixiApp'];
    if (!app) return;
    // REMED-PIXIJS-5 (part 4): clear:false on the render-texture branch -- see CNA_PixiJs_Render's
    // own identical fix, same reasoning.
    if (Module['cnaPixiActiveRenderTexture']) {
        app.renderer.render(Module['cnaPixiActiveContainer'], { renderTexture: Module['cnaPixiActiveRenderTexture'], clear: false });
    } else {
        app.renderer.render(app.stage);
    }
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

// plan_pixijs.md Design decision 6 / REMED-PIXIJS-3: caches the active PIXI.BLEND_MODES value (or,
// for the future PIXIJS-52 custom-blend-mode path, a registered custom id) that
// PixiJsSpriteBatchRenderer's own flush applies to each pooled sprite. blendCode: 0=Opaque,
// 1=AlphaBlend, 2=NonPremultiplied, 3=Additive -- mirrors PixiJsBlendMode's own numbering
// (PixiJsRenderer.hpp).
EM_JS(void, CNA_PixiJs_SetBlendMode, (int blendCode), {
    // PIXI.BLEND_MODES.NORMAL===0, .ADD===1, .NONE===20 in PixiJS v7 (confirmed live, 2026-08-17:
    // PIXI.BLEND_MODES was enumerated directly in a real browser rather than assumed from memory).
    // Opaque -> NONE: PixiJS's real "disable GL_BLEND entirely" mode, matching XNA's real
    // srcBlend=One/dstBlend=Zero contract (an unconditional overwrite that ignores source alpha) --
    // verified in a real browser (cna_test_pixijs_smoke) that a semi-transparent source pixel drawn
    // with Opaque produces a fully-opaque destination pixel, not a blended one.
    // AlphaBlend/NonPremultiplied still both render via NORMAL in this v1 scope (Design decision
    // 6's 4-preset boundary) -- PixiJS's default NORMAL blend already composites straight-alpha
    // (non-premultiplied) texture data correctly for the common case (verified via the Additive
    // test's own CornflowerBlue-background math and the flip/rotation tests' exact pixel checks,
    // all of which draw through this same NORMAL path with Color::White tint), but the exact
    // premultiplied-vs-straight distinction the two BlendState presets are supposed to carry is
    // still not independently applied -- tracked as PIXIJS-51, not silently assumed equivalent.
    // Module['cnaPixiBlendMode'] is currently write-only: this renderer's 2D-only v1 scope means no
    // draw path ever reads it back (SpriteBatch flushes apply blend mode directly from their own
    // activeBlendMode_/PixiBlendModeToPixiJsCode(), not through this global). PixiJsBlendMode::Custom
    // (index 4, PIXIJS-52) has no entry here and falls back to 0 -- harmless for the same reason, and
    // the real generic-blend GL factors/equation are applied by PixiJsSpriteBatchRenderer's own
    // per-flush custom blend-mode slot instead (PixiJsSpriteBatchRenderer.cpp).
    const modes = [20, 0, 0, 1];
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
        // plan_pixijs.md PIXIJS-52: any other combination gets a real, generic mapping via a custom
        // PixiJS blend-mode table entry (see ApplyBlendState/XnaBlendToGlFactor/
        // XnaBlendFunctionToGlEquation below) rather than a throw.
        return PixiJsBlendMode::Custom;
    }

    // plan_pixijs.md PIXIJS-52: raw XNA Blend enum (One=0, Zero=1, SourceColor=2,
    // InverseSourceColor=3, SourceAlpha=4, InverseSourceAlpha=5, DestinationColor=6,
    // InverseDestinationColor=7, DestinationAlpha=8, InverseDestinationAlpha=9, BlendFactor=10,
    // InverseBlendFactor=11, SourceAlphaSaturation=12) -> real WebGL blend-factor GL enum values,
    // confirmed live against a real WebGL context (2026-08-17: gl.ONE===1, gl.ZERO===0,
    // gl.SRC_COLOR===768, gl.DST_COLOR===774, gl.SRC_ALPHA_SATURATE===776, etc. -- not hand-derived
    // from memory).
    int XnaBlendToGlFactor(int xnaBlend)
    {
        switch (xnaBlend)
        {
            case 0: return 1;     // One -> ONE
            case 1: return 0;     // Zero -> ZERO
            case 2: return 768;   // SourceColor -> SRC_COLOR
            case 3: return 769;   // InverseSourceColor -> ONE_MINUS_SRC_COLOR
            case 4: return 770;   // SourceAlpha -> SRC_ALPHA
            case 5: return 771;   // InverseSourceAlpha -> ONE_MINUS_SRC_ALPHA
            case 6: return 774;   // DestinationColor -> DST_COLOR
            case 7: return 775;   // InverseDestinationColor -> ONE_MINUS_DST_COLOR
            case 8: return 772;   // DestinationAlpha -> DST_ALPHA
            case 9: return 773;   // InverseDestinationAlpha -> ONE_MINUS_DST_ALPHA
            case 10: return 32769; // BlendFactor -> CONSTANT_COLOR (gl.blendColor not wired, PIXIJS-52 note)
            case 11: return 32770; // InverseBlendFactor -> ONE_MINUS_CONSTANT_COLOR
            case 12: return 776;  // SourceAlphaSaturation -> SRC_ALPHA_SATURATE
            default:
                throw std::runtime_error(
                    "PixiJS: unrecognized Blend enum value " + std::to_string(xnaBlend) +
                    " passed to XnaBlendToGlFactor.");
        }
    }

    // plan_pixijs.md PIXIJS-52: raw XNA BlendFunction enum (Add=0, Subtract=1, ReverseSubtract=2,
    // Max=3, Min=4) -> real WebGL blend-equation GL enum values (FUNC_ADD=32774,
    // FUNC_SUBTRACT=32778, FUNC_REVERSE_SUBTRACT=32779, MAX=32776, MIN=32775 -- WebGL2 core, no
    // extension needed).
    int XnaBlendFunctionToGlEquation(int xnaBlendFunction)
    {
        switch (xnaBlendFunction)
        {
            case 0: return 32774; // Add -> FUNC_ADD
            case 1: return 32778; // Subtract -> FUNC_SUBTRACT
            case 2: return 32779; // ReverseSubtract -> FUNC_REVERSE_SUBTRACT
            case 3: return 32776; // Max -> MAX
            case 4: return 32775; // Min -> MIN
            default:
                throw std::runtime_error(
                    "PixiJS: unrecognized BlendFunction enum value " +
                    std::to_string(xnaBlendFunction) + " passed to XnaBlendFunctionToGlEquation.");
        }
    }

    void PixiJsRenderer::ApplyBlendState(int colorSrcBlend, int alphaSrcBlend,
                                         int colorDstBlend, int alphaDstBlend,
                                         int colorBlendFunc, int alphaBlendFunc,
                                         const BlendWriteState& /*writeState*/)
    {
        const PixiJsBlendMode mode = BlendStateToPixiJsBlendMode(
            colorSrcBlend, alphaSrcBlend, colorDstBlend, alphaDstBlend, colorBlendFunc, alphaBlendFunc);
        state_->blendMode = mode;
        if (mode == PixiJsBlendMode::Custom)
        {
            // plan_pixijs.md PIXIJS-52: real generic mapping, computed once here rather than at
            // every SpriteBatch flush -- PixiJsSpriteBatchRenderer::Begin() copies these onto its
            // own members, mirroring how it already copies state_->blendMode itself.
            state_->customBlendSrcRGB = XnaBlendToGlFactor(colorSrcBlend);
            state_->customBlendDstRGB = XnaBlendToGlFactor(colorDstBlend);
            state_->customBlendSrcAlpha = XnaBlendToGlFactor(alphaSrcBlend);
            state_->customBlendDstAlpha = XnaBlendToGlFactor(alphaDstBlend);
            state_->customBlendEquationRGB = XnaBlendFunctionToGlEquation(colorBlendFunc);
            state_->customBlendEquationAlpha = XnaBlendFunctionToGlEquation(alphaBlendFunc);
        }
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
