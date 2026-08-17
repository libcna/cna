#include "CNA/Internal/Renderers/PixiJs/PixiJsRenderer.hpp"
#include "CNA/Internal/Renderers/PixiJs/PixiJsTextureRenderer.hpp"
#include "CNA/Internal/Renderers/PixiJs/PixiJsRenderTargetRenderer.hpp"
#include "CNA/Internal/Renderers/PixiJs/PixiJsSpriteBatchRenderer.hpp"
#include "CNA/Internal/Renderers/Common/NoOp3DResources.hpp"

#include "System/NotSupportedException.hpp"

#include <cmath>
#include <stdexcept>

#if defined(__EMSCRIPTEN__)
#include <emscripten.h>

// =====================================================================================
// PIXIJS-87 -- the submission model
// =====================================================================================
//
// PixiJS is a retained scene graph, but XNA's SpriteBatch is not: after `End()` returns, the
// sprites of that batch are logically IN the target, in the order they were submitted, and the
// C++ Texture2D objects they sampled may legally be destroyed. The first version of this renderer
// tried to mirror SpriteBatch onto the retained graph directly -- pooled sprites were parented to
// the active container and only painted later, by `Present()`. That is unsound, and every one of
// these followed from it: a second `Begin/End` in one frame reused the pool from index 0 and
// overwrote the first batch; `SpriteSortMode::Immediate` kept only the last sprite; binding
// another render target moved pooled nodes out of the previous target's container (PixiJS
// re-parents on `addChild`); a `BlendState` or `SamplerState` set by a later batch applied to
// earlier, not-yet-painted sprites; and a texture destroyed after `End()` took its GPU resource
// with it before anything had sampled it.
//
// The model here instead commits at every submission point. One `PIXI.Container` (the scratch
// container) is filled from the pooled sprites of ONE flush, rendered immediately into the active
// target with `clear: false`, and emptied again. `render()` with `clear:false` genuinely
// accumulates -- verified in a real browser: two separate flushes to the canvas both survive, and
// neither disturbs the other's pixels. Consequences, all of them the XNA-correct behaviour:
//
//   * `End()` (or each `Draw()` in Immediate mode) rasterizes. Ordering is submission order.
//   * The pool only ever has to be as large as the biggest single flush, and no sprite is ever
//     parented to two containers, so Design decision 7's object pooling survives intact.
//   * Blend, sampler, colour-mask and blend-factor state is applied to the GL context immediately
//     before the render that consumes it, so it cannot be retroactively changed.
//   * A texture may be destroyed as soon as `End()` returns; nothing still references it.
//   * Each render target owns its own `PIXI.RenderTexture` and accumulates independently; the
//     back buffer is the canvas's own drawing buffer, kept across compositing by
//     `preserveDrawingBuffer` so accumulation is defined between frames too.
//   * `GetBackBufferData`/`GetData` read what is already there -- no "force a render before
//     reading" correction (REMED-PIXIJS-1) is needed, because nothing is ever left unpainted.
//
// Every EM_JS entry point returns an int status instead of logging and returning silently: a
// failed CNA API call must surface as a real C++ exception, not a lost draw (PIXIJS-91).

// PIXIJS-91: creates the one PIXI.Application on the platform's existing <canvas> (Design
// decision 2), plus every renderer-owned JS object, and adopts a new drawable size on later calls.
// Returns 1 on success, 0 if PixiJS or the canvas is missing -- the caller turns 0 into a throw.
// PIXIJS-92 -- the ownership rule, which a real destroy/recreate test forced into the open.
//
// The <canvas> belongs to the platform, and so, transitively, does its WebGL context: a canvas
// hands out exactly one, and PixiJS's own Renderer.destroy() deliberately calls
// WEBGL_lose_context.loseContext(). Destroying the PIXI.Application therefore does not "release the
// renderer's application" -- it permanently poisons the platform's canvas, and constructing a
// second PIXI.Application on it fails inside PixiJS's own batch setup ("Invalid value of `0` passed
// to checkMaxIfStatementsInShader", because the lost context reports zero texture units). Found by
// running exactly that sequence in a real browser.
//
// So the PIXI.Application is scoped to the CANVAS and lives on Module['cnaPixiApp']; everything the
// RENDERER creates -- textures, render textures, the sprite pool, the scratch container, the clear
// sprite, the blend-mode slots, the active-target selection -- is scoped to the renderer and lives
// on Module['cnaPixi']. Teardown releases the second and leaves the first, which is what makes
// destroying a GraphicsDevice and creating another one on the same page work with no stale renderer
// state carried across.
EM_JS(int, CNA_PixiJs_EnsureApp, (int width, int height), {
    try {
        let state = Module['cnaPixi'];
        if (state && state.app) {
            if (width > 0 && height > 0 &&
                (state.app.renderer.width !== width || state.app.renderer.height !== height)) {
                // PIXIJS-93: adopt a resize. renderer.resize() re-establishes the projection and
                // the GL viewport; without it every later draw keeps using the original extent and
                // a resized canvas silently renders into the wrong rectangle.
                state.app.renderer.resize(width, height);
            }
            return 1;
        }
        if (typeof PIXI === 'undefined') {
            console.error('[CNA] PixiJS: the PIXI global is not defined -- was the vendored pixi.min.js linked via --extern-pre-js?');
            return 0;
        }
        let app = Module['cnaPixiApp'];
        if (!app) {
            const canvas = Module['canvas'] || document.querySelector('canvas');
            if (!canvas) {
                console.error('[CNA] PixiJS: no <canvas> element found');
                return 0;
            }
            app = new PIXI.Application({
                view: canvas,
                width: width > 0 ? width : (canvas.width || 1),
                height: height > 0 ? height : (canvas.height || 1),
                // PIXIJS-22: CNA's Game loop owns frame timing; PixiJS's own ticker never starts.
                autoStart: false,
                sharedTicker: false,
                backgroundAlpha: 1,
                // PIXIJS-87: the canvas drawing buffer IS this renderer's back buffer, and several
                // flushes accumulate into it across a frame. Without this the browser is free to
                // discard it at every compositing step, which would make "draw, then read back"
                // depend on where a compositor boundary happened to fall.
                preserveDrawingBuffer: true,
            });
            Module['cnaPixiApp'] = app;
        } else if (width > 0 && height > 0 &&
                   (app.renderer.width !== width || app.renderer.height !== height)) {
            app.renderer.resize(width, height);
        }
        const whiteBase = new PIXI.BaseTexture(
            new PIXI.BufferResource(new Uint8Array([255, 255, 255, 255]), { width: 1, height: 1 }),
            { width: 1, height: 1, scaleMode: PIXI.SCALE_MODES.NEAREST, alphaMode: PIXI.ALPHA_MODES.NPM });
        const whiteTexture = new PIXI.Texture(whiteBase);
        // PIXIJS-87: "srcRGB,dstRGB,srcAlpha,dstAlpha,eqRGB,eqAlpha" -> a slot index in
        // renderer.state.blendModes. A DISTINCT slot per distinct tuple, never one slot mutated in
        // place: PixiJS's StateSystem short-circuits setBlendMode() when the id is unchanged, so
        // rewriting a slot's factors has no effect on a later draw that reuses the same id.
        // Confirmed in a real browser before this code was written -- two renders through one
        // mutated slot produced identical pixels for two different factor tuples. The map hangs off
        // the APPLICATION, because that is what owns the blendModes table it indexes into: a new
        // renderer on the same canvas reuses the slots already registered rather than appending
        // duplicates of them.
        if (!app.__cnaBlendSlots) app.__cnaBlendSlots = {};
        Module['cnaPixi'] = {
            app: app,
            gl: app.renderer.gl,
            textures: {},
            pool: [],
            scratch: new PIXI.Container(),
            whiteTexture: whiteTexture,
            clearSprite: new PIXI.Sprite(whiteTexture),
            // null = the canvas back buffer; otherwise the bound target's PIXI.RenderTexture.
            activeTarget: null,
            activeTargetId: 0,
            blendSlots: app.__cnaBlendSlots,
        };
        return 1;
    } catch (e) {
        console.error('[CNA] PixiJS: application creation failed:', e);
        return 0;
    }
});

// PIXIJS-92: releases every RENDERER-owned JS object, leaving the canvas-owned PIXI.Application in
// place (see the block comment above for why destroying it would be a one-way trip).
EM_JS(void, CNA_PixiJs_ReleaseRendererResources, (), {
    const state = Module['cnaPixi'];
    if (!state) return;
    try {
        if (state.scratch) {
            state.scratch.removeChildren();
            state.scratch.destroy({ children: false });
        }
        for (const sprite of state.pool) {
            // A pooled sprite never owns the texture it was last handed; those belong to the
            // texture registry destroyed below.
            if (sprite) sprite.destroy({ children: false, texture: false, baseTexture: false });
        }
        if (state.clearSprite) state.clearSprite.destroy({ children: false, texture: false, baseTexture: false });
        if (state.whiteTexture) state.whiteTexture.destroy(true);
        for (const id of Object.keys(state.textures)) {
            const entry = state.textures[id];
            if (!entry) continue;
            for (const key of Object.keys(entry.views)) entry.views[key].destroy(false);
            entry.texture.destroy(true);
        }
        // The application keeps no reference to any of the above -- nothing is ever parented to
        // app.stage -- so there is nothing on it left to unwire.
    } catch (e) {
        console.error('[CNA] PixiJS: teardown reported', e);
    }
    delete Module['cnaPixi'];
});

// PIXIJS-87: Clear paints an unconditional overwrite over the WHOLE active target and nothing
// else. It is the same operation for the back buffer and for a bound render target, which is what
// makes it a real ordering boundary: everything submitted before it is already rasterized, so the
// overwrite genuinely erases it, and nothing submitted after it is affected.
//
// This replaces the old asymmetric implementation, which set app.renderer.background for the main
// canvas (only observed by a LATER render's clear) and dropped the active container's children --
// a "reset the frame" step that silently discarded sprites belonging to another batch.
EM_JS(int, CNA_PixiJs_Clear, (double r, double g, double b, double a), {
    const state = Module['cnaPixi'];
    if (!state || !state.app) return 0;
    try {
        const target = state.activeTarget;
        const width = target ? target.width : state.app.renderer.width;
        const height = target ? target.height : state.app.renderer.height;
        const sprite = state.clearSprite;
        sprite.anchor.set(0, 0);
        sprite.position.set(0, 0);
        sprite.rotation = 0;
        sprite.scale.set(width, height);
        sprite.tint = (Math.round(r * 255) << 16) | (Math.round(g * 255) << 8) | Math.round(b * 255);
        sprite.alpha = a;
        // BLEND_MODES.NONE is PixiJS's real "no GL blending" mode: the fragment replaces the
        // destination outright, which is exactly Clear.
        sprite.blendMode = PIXI.BLEND_MODES.NONE;
        state.scratch.removeChildren();
        state.scratch.addChild(sprite);
        // A colour-write mask left over from an earlier batch must not mask a Clear.
        state.gl.colorMask(true, true, true, true);
        if (target) state.app.renderer.render(state.scratch, { renderTexture: target, clear: false });
        else state.app.renderer.render(state.scratch, { clear: false });
        state.scratch.removeChildren();
        return 1;
    } catch (e) {
        console.error('[CNA] PixiJS: Clear failed:', e);
        return 0;
    }
});

// PIXIJS-87: every draw is already in the back buffer by the time Present runs (that is the whole
// point of the commit-on-submit model), so Present only has to make sure the GL command stream is
// handed to the driver. The browser composites the preserved drawing buffer itself.
EM_JS(int, CNA_PixiJs_Present, (), {
    const state = Module['cnaPixi'];
    if (!state || !state.app) return 0;
    state.gl.flush();
    return 1;
});

// PIXIJS-87: a plain read. extract.pixels() with no argument reads the canvas back buffer and
// applies PixiJS's own Y flip; nothing needs re-rendering first, because a submitted draw is
// already painted.
EM_JS(int, CNA_PixiJs_ReadBackbuffer, (int x, int y, int w, int h, uint8_t* outPixels), {
    const state = Module['cnaPixi'];
    if (!state || !state.app) return 0;
    try {
        const pixels = state.app.renderer.extract.pixels();
        const fullWidth = state.app.renderer.width;
        const bytesPerRow = w * 4;
        for (let row = 0; row < h; ++row) {
            const srcOffset = ((y + row) * fullWidth + x) * 4;
            HEAPU8.set(pixels.subarray(srcOffset, srcOffset + bytesPerRow), outPixels + row * bytesPerRow);
        }
        return 1;
    } catch (e) {
        console.error('[CNA] PixiJS: back-buffer readback failed:', e);
        return 0;
    }
});

EM_JS(int, CNA_PixiJs_UnbindRenderTarget, (), {
    const state = Module['cnaPixi'];
    if (!state || !state.app) return 0;
    state.activeTarget = null;
    state.activeTargetId = 0;
    return 1;
});
#endif

namespace CNA::Internal::Renderers::PixiJs
{
    namespace
    {
        // PIXIJS-91: one place that turns "PixiJS is not usable" into a real, propagated error.
        // The previous code called console.error and returned, which made a legal CNA call
        // (a Draw before the first Clear, a SetRenderTarget as the first operation) silently do
        // nothing at all.
        void EnsureApp([[maybe_unused]] int width, [[maybe_unused]] int height)
        {
#if defined(__EMSCRIPTEN__)
            if (CNA_PixiJs_EnsureApp(width, height) == 0)
                throw std::runtime_error(
                    "PixiJS: could not create the PIXI.Application. Either the vendored pixi.min.js "
                    "was not linked into this build (cmake/ThirdPartyPixiJS.cmake / --extern-pre-js) "
                    "or the platform has not created a <canvas> element yet.");
#endif
        }
    }

    PixiJsRenderer::PixiJsRenderer(const GraphicsRendererCreateArgs& args)
        : surface_(args.surface)
        , virtualWidth_(args.virtualWidth)
        , virtualHeight_(args.virtualHeight)
        , presentationMode_(args.presentationMode)
    {
        if (surface_.windowId == 0)
            throw std::runtime_error("PixiJsRenderer initialized without a platform window.");
        if (!(surface_.displayScale > 0.0f) || !std::isfinite(surface_.displayScale))
            surface_.displayScale = 1.0f;
        // PIXIJS-91: initialization is explicit and happens here, not lazily inside Clear(). A
        // renderer that cannot reach PixiJS fails construction loudly instead of accepting draw
        // calls it will discard.
        EnsureApp(surface_.drawableSize.width, surface_.drawableSize.height);
        IGraphicsRenderer::RegisterForWindow(surface_.windowId, this);
    }

    PixiJsRenderer::~PixiJsRenderer()
    {
        IGraphicsRenderer::UnregisterForWindow(surface_.windowId);
#if defined(__EMSCRIPTEN__)
        CNA_PixiJs_ReleaseRendererResources();
#endif
    }

    void PixiJsRenderer::Clear(float r, float g, float b, float a)
    {
        EnsureApp(surface_.drawableSize.width, surface_.drawableSize.height);
#if defined(__EMSCRIPTEN__)
        if (CNA_PixiJs_Clear(r, g, b, a) == 0)
            throw std::runtime_error("PixiJS: Clear failed; see the browser console for the underlying error.");
#else
        (void)r; (void)g; (void)b; (void)a;
#endif
    }

    void PixiJsRenderer::Present()
    {
        EnsureApp(surface_.drawableSize.width, surface_.drawableSize.height);
#if defined(__EMSCRIPTEN__)
        if (CNA_PixiJs_Present() == 0)
            throw std::runtime_error("PixiJS: Present failed; the PIXI.Application is not available.");
#endif
    }

    void PixiJsRenderer::getWindowSize(int& width, int& height) const
    {
        width = static_cast<int>(std::lround(surface_.drawableSize.width / surface_.displayScale));
        height = static_cast<int>(std::lround(surface_.drawableSize.height / surface_.displayScale));
    }

    void PixiJsRenderer::getLogicalSize(int& width, int& height) const
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

    void PixiJsRenderer::OnSurfaceChanged(const RendererSurfaceInfo& surface)
    {
        if (surface.windowId != surface_.windowId)
            throw std::runtime_error("PixiJsRenderer surface window id changed.");
        surface_ = surface;
        if (!(surface_.displayScale > 0.0f) || !std::isfinite(surface_.displayScale))
            surface_.displayScale = 1.0f;
        // PIXIJS-93: a resize the platform reports has to reach PixiJS's own renderer, or the
        // projection and GL viewport stay pinned to the size the application was created with.
        EnsureApp(surface_.drawableSize.width, surface_.drawableSize.height);
    }

    bool PixiJsRenderer::TransformWindowToLogical(float windowX, float windowY,
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

    bool PixiJsRenderer::TransformLogicalToWindow(float logX, float logY,
                                                  float& windowX, float& windowY) const
    {
        if (virtualHeight_ <= 0) return false;
        int physW, physH;
        getWindowSize(physW, physH);
        if (physH <= 0) return false;
        const float invScale = static_cast<float>(physH) / static_cast<float>(virtualHeight_);
        windowX = logX * invScale;
        windowY = logY * invScale;
        return true;
    }

    std::unique_ptr<ITextureRenderer> PixiJsRenderer::CreateTexture(const ImageData& data)
    {
        EnsureApp(surface_.drawableSize.width, surface_.drawableSize.height);
        return std::make_unique<PixiJsTextureRenderer>(data);
    }

    std::unique_ptr<ISpriteBatchRenderer> PixiJsRenderer::CreateSpriteBatch()
    {
        EnsureApp(surface_.drawableSize.width, surface_.drawableSize.height);
        return std::make_unique<PixiJsSpriteBatchRenderer>(state_);
    }

    std::unique_ptr<IRenderTargetRenderer> PixiJsRenderer::CreateRenderTarget2D(
        int w, int h, int /*depthFormat*/, bool /*preserveContents*/, bool /*mipMap*/, int /*multiSampleCount*/)
    {
        // depthFormat/mipMap/multiSampleCount are ignored in this v1 scope, same boundary
        // CANVAS-23/CANVAS-21 drew for their own render targets (plan_pixijs.md PIXIJS-34).
        EnsureApp(surface_.drawableSize.width, surface_.drawableSize.height);
        return std::make_unique<PixiJsRenderTargetRenderer>(w, h);
    }

    void PixiJsRenderer::SetRenderTarget2D(IRenderTargetRenderer* rt)
    {
        // PIXIJS-91: binding a render target may legally be the very first graphics operation of a
        // frame, before any Clear.
        EnsureApp(surface_.drawableSize.width, surface_.drawableSize.height);
        if (rt)
        {
            rt->BindAsRenderTarget();
        }
        else
        {
#if defined(__EMSCRIPTEN__)
            if (CNA_PixiJs_UnbindRenderTarget() == 0)
                throw std::runtime_error("PixiJS: could not restore the back buffer as the render target.");
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
        EnsureApp(surface_.drawableSize.width, surface_.drawableSize.height);
#if defined(__EMSCRIPTEN__)
        if (CNA_PixiJs_ReadBackbuffer(x, y, w, h, pixels) == 0)
            throw std::runtime_error("PixiJS: back-buffer readback failed.");
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
        return PixiJsBlendMode::Custom;
    }

    // Raw XNA Blend enum (One=0, Zero=1, SourceColor=2, InverseSourceColor=3, SourceAlpha=4,
    // InverseSourceAlpha=5, DestinationColor=6, InverseDestinationColor=7, DestinationAlpha=8,
    // InverseDestinationAlpha=9, BlendFactor=10, InverseBlendFactor=11, SourceAlphaSaturation=12)
    // -> real WebGL blend-factor GL enum values, confirmed live against a real WebGL context
    // (gl.ONE===1, gl.ZERO===0, gl.SRC_COLOR===768, gl.DST_COLOR===774, gl.SRC_ALPHA_SATURATE===776,
    // gl.CONSTANT_COLOR===32769, gl.ONE_MINUS_CONSTANT_COLOR===32770).
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
            // PIXIJS-88: the constant itself is supplied by SetBlendFactor -> gl.blendColor.
            case 10: return 32769; // BlendFactor -> CONSTANT_COLOR
            case 11: return 32770; // InverseBlendFactor -> ONE_MINUS_CONSTANT_COLOR
            case 12: return 776;  // SourceAlphaSaturation -> SRC_ALPHA_SATURATE
            default:
                throw std::runtime_error(
                    "PixiJS: unrecognized Blend enum value " + std::to_string(xnaBlend) +
                    " passed to XnaBlendToGlFactor.");
        }
    }

    // Raw XNA BlendFunction enum (Add=0, Subtract=1, ReverseSubtract=2, Max=3, Min=4) -> real WebGL
    // blend-equation GL enum values (FUNC_ADD=32774, FUNC_SUBTRACT=32778,
    // FUNC_REVERSE_SUBTRACT=32779, MAX=32776, MIN=32775 -- WebGL2 core, no extension needed).
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

    int TextureAddressModeToPixiWrapMode(int addressMode)
    {
        switch (addressMode)
        {
            case 0: return 10497; // Wrap -> PIXI.WRAP_MODES.REPEAT (gl.REPEAT)
            case 1: return 33071; // Clamp -> PIXI.WRAP_MODES.CLAMP (gl.CLAMP_TO_EDGE)
            case 2: return 33648; // Mirror -> PIXI.WRAP_MODES.MIRRORED_REPEAT
            default:
                // PIXIJS-90: this used to fall through to Clamp, which silently rendered a sampler
                // state the caller never asked for.
                throw std::runtime_error(
                    "PixiJS: unrecognized TextureAddressMode enum value " +
                    std::to_string(addressMode) + " passed to TextureAddressModeToPixiWrapMode.");
        }
    }

    bool TextureFilterIsLinear(int textureFilter)
    {
        // The same magnification-dominant TextureFilter grouping CANVAS-42 established: a
        // SpriteBatch draw is near-universally magnification-dominant, so the "expand" component is
        // what visibly matters. Linear=0, Point=1, Anisotropic=2, LinearMipPoint=3,
        // PointMipLinear=4, MinLinearMagPointMipLinear=5, MinLinearMagPointMipPoint=6,
        // MinPointMagLinearMipLinear=7, MinPointMagLinearMipPoint=8.
        switch (textureFilter)
        {
            case 0: case 2: case 3: case 7: case 8:
                return true;
            case 1: case 4: case 5: case 6:
                return false;
            default:
                throw std::runtime_error(
                    "PixiJS: unrecognized TextureFilter enum value " +
                    std::to_string(textureFilter) + " passed to TextureFilterIsLinear.");
        }
    }

    void PixiJsRenderer::ApplyBlendState(int colorSrcBlend, int alphaSrcBlend,
                                         int colorDstBlend, int alphaDstBlend,
                                         int colorBlendFunc, int alphaBlendFunc,
                                         const BlendWriteState& writeState)
    {
        // PIXIJS-89: MultiSampleMask. This renderer's targets are single-sample (the canvas is
        // created without antialiasing, and PIXI.RenderTexture.create() is used without a
        // multisample setting), so every mask that enables coverage sample 0 behaves exactly like
        // the all-ones default -- that is an equivalence, not an approximation. A mask that
        // disables sample 0 asks for "write nothing", which has no single-sample equivalent and is
        // rejected rather than silently ignored.
        if ((writeState.multiSampleMask & 1u) == 0u)
            throw System::NotSupportedException(
                "PixiJS: BlendState.MultiSampleMask disables coverage sample 0. This renderer's "
                "targets are single-sample, so that mask has no representable meaning here. Every "
                "mask that leaves sample 0 enabled is supported and behaves as the all-ones default.");
        // colorWriteChannels[1..3] describe MRT slots 1..3. This renderer never binds more than
        // one render target -- SetRenderTargets rejects any count above one -- so those slots are
        // inapplicable rather than dropped, the same position EasyGLRenderer takes for a GL profile
        // without indexed colour masks (it only rejects them once an MRT set is actually bound).

        const PixiJsBlendMode mode = BlendStateToPixiJsBlendMode(
            colorSrcBlend, alphaSrcBlend, colorDstBlend, alphaDstBlend, colorBlendFunc, alphaBlendFunc);
        // PIXIJS-87: resolve the literal factors for EVERY blend state, preset or not. Mapping the
        // presets onto PIXI.BLEND_MODES instead is what made AlphaBlend indistinguishable from
        // NonPremultiplied -- see PixiJsBlendMode's own doc comment.
        state_->blendMode = mode;
        state_->blendSrcRGB = XnaBlendToGlFactor(colorSrcBlend);
        state_->blendDstRGB = XnaBlendToGlFactor(colorDstBlend);
        state_->blendSrcAlpha = XnaBlendToGlFactor(alphaSrcBlend);
        state_->blendDstAlpha = XnaBlendToGlFactor(alphaDstBlend);
        state_->blendEquationRGB = XnaBlendFunctionToGlEquation(colorBlendFunc);
        state_->blendEquationAlpha = XnaBlendFunctionToGlEquation(alphaBlendFunc);
        state_->colorWriteChannels = writeState.colorWriteChannels[0];
    }

    void PixiJsRenderer::SetBlendFactor(float r, float g, float b, float a)
    {
        state_->blendFactorR = r;
        state_->blendFactorG = g;
        state_->blendFactorB = b;
        state_->blendFactorA = a;
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

    // plan_runtimerenderer.md design decision 4: the factory is family-scoped, so several renderer
    // archives can link into one binary. PixiJsRendererDescriptor.cpp takes its address.
    std::unique_ptr<IGraphicsRenderer> CreateGraphicsRenderer(const GraphicsRendererCreateArgs& args)
    {
        return std::make_unique<PixiJsRenderer>(args);
    }
}
