// SPDX-License-Identifier: MS-PL
#include "CNA/Internal/Renderers/SvgDom/SvgDomRenderer.hpp"
#include "CNA/Internal/Renderers/SvgDom/SvgDomTextureRenderer.hpp"
#include "CNA/Internal/Renderers/SvgDom/SvgDomRenderTargetRenderer.hpp"
#include "CNA/Internal/Renderers/SvgDom/SvgDomSpriteBatchRenderer.hpp"
#include "CNA/Internal/Renderers/Common/NotYetImplemented.hpp"

#include "System/ArgumentNullException.hpp"
#include "System/ArgumentOutOfRangeException.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <string>

#if defined(__EMSCRIPTEN__)
#include <emscripten.h>

// plan_svg_dom.md design decision 1/6: creates the SVG surface over the <canvas> SDL3's
// Emscripten video driver owns -- same host-integration shape as HtmlDom's own root (canvas stays
// in the layout, hidden, so SDL keeps sizing it and delivering input through it), but the surface
// itself is a real <svg> element (SVG namespace), not a CSS-styled <div>.
EM_JS(void, CNA_SvgDom_EnsureRoot, (), {
    if (typeof document === 'undefined') return;
    Module['cnaSvgDomRefCount'] = (Module['cnaSvgDomRefCount'] || 0) + 1;
    if (Module['cnaSvgDomRoot']) return;
    const canvas = Module['canvas'] || document.querySelector('canvas');
    if (!canvas) { console.error('[CNA] SVG_DOM: no <canvas> element to anchor the SVG surface to'); return; }
    if (document.getElementById('cna-svg-dom-root')) {
        console.error('[CNA] SVG_DOM: the page already has an element with id "cna-svg-dom-root" ' +
                      'that this renderer did not create -- refusing to proceed.');
        return;
    }
    const svgNS = 'http://www.w3.org/2000/svg';
    const root = document.createElementNS(svgNS, 'svg');
    root.id = 'cna-svg-dom-root';
    root.style.cssText = 'position:absolute;left:0;top:0;overflow:hidden;';
    const defs = document.createElementNS(svgNS, 'defs');
    const bg = document.createElementNS(svgNS, 'rect');
    bg.setAttribute('x', 0); bg.setAttribute('y', 0);
    bg.setAttribute('width', '100%'); bg.setAttribute('height', '100%');
    const spriteGroup = document.createElementNS(svgNS, 'g');
    root.appendChild(defs);
    root.appendChild(bg);
    root.appendChild(spriteGroup);
    (canvas.parentNode || document.body).insertBefore(root, canvas.nextSibling);
    Module['cnaSvgDomOriginalCanvasVisibility'] = canvas.style.visibility;
    canvas.style.visibility = 'hidden';
    Module['cnaSvgDomRoot'] = root;
    Module['cnaSvgDomDefs'] = defs;
    Module['cnaSvgDomBackgroundRect'] = bg;
    Module['cnaSvgDomSpriteGroup'] = spriteGroup;
    Module['cnaSvgDomBoundCtx'] = null;
    Module['cnaSvgDomFilterCache'] = {};
    Module['cnaSvgDomClipRect'] = null;
});

EM_JS(void, CNA_SvgDom_DestroyRoot, (), {
    if (typeof document !== 'undefined') {
        Module['cnaSvgDomRefCount'] = Math.max(0, (Module['cnaSvgDomRefCount'] || 1) - 1);
        if (Module['cnaSvgDomRefCount'] > 0) return;
    }
    const root = Module['cnaSvgDomRoot'];
    if (root && root.parentNode) root.parentNode.removeChild(root);
    const canvas = Module['canvas'] ||
                   (typeof document === 'undefined' ? null : document.querySelector('canvas'));
    if (canvas) canvas.style.visibility = Module['cnaSvgDomOriginalCanvasVisibility'] || "";
    Module['cnaSvgDomOriginalCanvasVisibility'] = null;
    Module['cnaSvgDomRoot'] = null;
    Module['cnaSvgDomDefs'] = null;
    Module['cnaSvgDomBackgroundRect'] = null;
    Module['cnaSvgDomSpriteGroup'] = null;
    Module['cnaSvgDomFilterCache'] = null;
    Module['cnaSvgDomClipRect'] = null;
});

// XNA's Clear overwrites everything drawn so far: repaints the background and drops every sprite
// queued this frame. With a render target bound, clears that canvas instead (real overwrite, not a
// blend -- 'copy' compositing, matching CNA_HtmlDom_Clear's own reasoning).
EM_JS(void, CNA_SvgDom_Clear, (double r, double g, double b, double a), {
    const ctx = Module['cnaSvgDomBoundCtx'];
    if (ctx) {
        ctx.save();
        ctx.setTransform(1, 0, 0, 1, 0, 0);
        if (a <= 0) {
            ctx.clearRect(0, 0, ctx.canvas.width, ctx.canvas.height);
        } else {
            ctx.globalCompositeOperation = 'copy';
            ctx.globalAlpha = 1;
            ctx.fillStyle = 'rgba(' + Math.round(r * 255) + ',' + Math.round(g * 255) + ',' +
                            Math.round(b * 255) + ',' + a + ')';
            ctx.fillRect(0, 0, ctx.canvas.width, ctx.canvas.height);
        }
        ctx.restore();
        return;
    }
    const bg = Module['cnaSvgDomBackgroundRect'];
    if (!bg) return;
    bg.setAttribute('fill', 'rgba(' + Math.round(r * 255) + ',' + Math.round(g * 255) + ',' +
                    Math.round(b * 255) + ',' + a + ')');
    // plan_svg_dom.md: Clear() is this renderer's own "frame start" marker (XNA games call it once
    // per frame, before any Draw()) -- rewinds the pooled-sprite cursor to 0 rather than tearing
    // the pool down, so a steady frame reuses the SAME elements CNA_SvgDom_FlushSpritesToSvg
    // dirty-diffs against instead of paying create/destroy churn every frame. The pool elements
    // themselves are hidden/shown by CNA_SvgDom_PresentFrame's own high-water-mark bookkeeping.
    Module['cnaSvgDomSpriteUsed'] = 0;
});

// Ends the frame: hides only the pooled sprite elements this frame did not use, tracked by a
// high-water mark (the same shape HtmlDom's own CNA_HtmlDom_PresentFrame uses), so a steady sprite
// count touches nothing here at all. There is nothing to swap -- the browser compositor presents
// the SVG subtree on its next paint tick. Module['cnaSvgDomSpriteUsed'] itself is rewound to 0 by
// CNA_SvgDom_Clear (this renderer's own frame-start marker), not here.
EM_JS(void, CNA_SvgDom_PresentFrame, (), {
    const pool = Module['cnaSvgDomSpritePool'];
    if (!pool) return;
    const used = Module['cnaSvgDomSpriteUsed'] || 0;
    const high = Module['cnaSvgDomSpriteHighWater'] || 0;
    for (let i = used; i < high; ++i) {
        const entryEl = pool[i];
        if (entryEl && !entryEl.cna.hidden) {
            entryEl.viewport.style.display = 'none';
            entryEl.cna.hidden = true;
        }
    }
    Module['cnaSvgDomSpriteHighWater'] = used;
});

// Records the backbuffer's own logical geometry and physical placement (offset + scale). V1:
// applied unconditionally every Present() (see SvgDomRenderer::Present's own comment) rather than
// diffed against the previous frame -- a real, smaller-scope-than-HtmlDom simplification.
EM_JS(void, CNA_SvgDom_ApplySurfaceGeometry, (int logicalW, int logicalH,
                                              double offsetX, double offsetY,
                                              double scaleX, double scaleY), {
    const root = Module['cnaSvgDomRoot'];
    if (!root) return;
    root.setAttribute('width', logicalW);
    root.setAttribute('height', logicalH);
    root.setAttribute('viewBox', '0 0 ' + logicalW + ' ' + logicalH);
    root.style.left = offsetX + 'px';
    root.style.top = offsetY + 'px';
    root.style.width = (logicalW * scaleX) + 'px';
    root.style.height = (logicalH * scaleY) + 'px';
});

EM_JS(int, CNA_SvgDom_ReadBound, (int x, int y, int w, int h, uint8_t* outPixels), {
    const ctx = Module['cnaSvgDomBoundCtx'];
    if (!ctx) return 0;
    const imageData = ctx.getImageData(x, y, w, h);
    HEAPU8.set(imageData.data, outPixels);
    return 1;
});

EM_JS(int, CNA_SvgDom_SupportsAdditiveBlending, (), {
    if (Module['cnaSvgDomSupportsAdditiveBlending'] === undefined) {
        Module['cnaSvgDomSupportsAdditiveBlending'] =
            (typeof CSS !== 'undefined' && CSS.supports &&
             CSS.supports('mix-blend-mode', 'plus-lighter')) ? 1 : 0;
    }
    return Module['cnaSvgDomSupportsAdditiveBlending'];
});
#endif

namespace CNA::Internal::Renderers::SvgDom
{
    namespace
    {
        [[noreturn]] void ThrowNo3D(const char* what)
        {
            NotYetImplemented("SVG_DOM", what);
        }
    }

    SvgDomRenderer::SvgDomRenderer(SDL_Window* window, int virtualWidth, int virtualHeight,
                                  CnaPresentationMode mode)
        : window_(window)
        , virtualWidth_(virtualWidth)
        , virtualHeight_(virtualHeight)
        , presentationMode_(mode)
    {
        if (!window_) throw std::runtime_error("SvgDomRenderer initialized with null window.");
        IGraphicsRenderer::RegisterForWindow(window_, this);
        SetBoundRenderTargetIdEXT(0);
        SetCurrentCompositeOpEXT(DomCompositeOp::NonPremultiplied);
        SetCurrentScissorEnableEXT(false);
        SetCurrentScissorRectEXT(0, 0, 0, 0);
        SetCurrentViewportOffsetEXT(0, 0);
#if defined(__EMSCRIPTEN__)
        CNA_SvgDom_EnsureRoot();
#endif
    }

    SvgDomRenderer::~SvgDomRenderer()
    {
        IGraphicsRenderer::UnregisterForWindow(window_);
#if defined(__EMSCRIPTEN__)
        CNA_SvgDom_DestroyRoot();
#endif
    }

    void SvgDomRenderer::Clear(float r, float g, float b, float a)
    {
#if defined(__EMSCRIPTEN__)
        CNA_SvgDom_Clear(r, g, b, a);
#else
        (void)r; (void)g; (void)b; (void)a;
#endif
    }

    void SvgDomRenderer::ApplySurfaceGeometryEXT()
    {
        const LogicalViewport viewport = ComputeLogicalViewport();
        const int logicalW = static_cast<int>(std::lround(viewport.logicalWidth));
        const int logicalH = static_cast<int>(std::lround(viewport.logicalHeight));
        const float scaleX = viewport.logicalWidth > 0.0f ? viewport.width / viewport.logicalWidth : 1.0f;
        const float scaleY = viewport.logicalHeight > 0.0f ? viewport.height / viewport.logicalHeight : 1.0f;
#if defined(__EMSCRIPTEN__)
        CNA_SvgDom_ApplySurfaceGeometry(logicalW, logicalH, viewport.x, viewport.y, scaleX, scaleY);
#else
        (void)logicalW; (void)logicalH; (void)scaleX; (void)scaleY;
#endif
    }

    void SvgDomRenderer::Present()
    {
        // V1: geometry is applied every frame rather than diffed against the previous one (see the
        // class doc's own note on the smaller V1 scope) -- a real, documented cost relative to
        // HtmlDom's own "only touch the DOM when geometry actually changed" optimization. The
        // sprite POOL itself, however, is genuinely reused/dirty-diffed frame to frame (see
        // SvgDomSpriteBatchRenderer.cpp's CNA_SvgDom_FlushSpritesToSvg) -- this only hides the
        // unused tail.
        ApplySurfaceGeometryEXT();
#if defined(__EMSCRIPTEN__)
        CNA_SvgDom_PresentFrame();
#endif
    }

    SvgDomRenderer::LogicalViewport SvgDomRenderer::ComputeLogicalViewport() const
    {
        // Same per-CnaPresentationMode geometry every CNA 2D-DOM/CSS renderer implements
        // (SdlGpuRenderer's own reference shape) -- independently computed here.
        LogicalViewport viewport{};
        int physW = 0, physH = 0;
        SDL_GetWindowSize(window_, &physW, &physH);
        viewport.width = viewport.logicalWidth = static_cast<float>(std::max(0, physW));
        viewport.height = viewport.logicalHeight = static_cast<float>(std::max(0, physH));
        if (physW <= 0 || physH <= 0) return viewport;
        if (presentationMode_ == CnaPresentationMode::NativeBackBuffer ||
            virtualWidth_ <= 0 || virtualHeight_ <= 0)
            return viewport;

        float logicalWidth = static_cast<float>(virtualWidth_);
        float logicalHeight = static_cast<float>(virtualHeight_);
        if (presentationMode_ == CnaPresentationMode::FixedHeightDynamicWidth)
        {
            logicalWidth = logicalHeight * static_cast<float>(physW) / static_cast<float>(physH);
            viewport.logicalWidth = logicalWidth;
            viewport.logicalHeight = logicalHeight;
            return viewport;
        }

        viewport.logicalWidth = logicalWidth;
        viewport.logicalHeight = logicalHeight;
        if (presentationMode_ == CnaPresentationMode::Stretch)
            return viewport;

        const float sx = static_cast<float>(physW) / logicalWidth;
        const float sy = static_cast<float>(physH) / logicalHeight;
        const float scale = presentationMode_ == CnaPresentationMode::Overscan
                                 ? std::max(sx, sy)
                                 : std::min(sx, sy);
        viewport.width = logicalWidth * scale;
        viewport.height = logicalHeight * scale;
        viewport.x = (static_cast<float>(physW) - viewport.width) * 0.5f;
        viewport.y = (static_cast<float>(physH) - viewport.height) * 0.5f;
        return viewport;
    }

    void SvgDomRenderer::getLogicalSize(int& width, int& height) const
    {
        const LogicalViewport viewport = ComputeLogicalViewport();
        width = static_cast<int>(std::lround(viewport.logicalWidth));
        height = static_cast<int>(std::lround(viewport.logicalHeight));
    }

    void SvgDomRenderer::GetViewportSize(int& width, int& height)
    {
        getLogicalSize(width, height);
    }

    void SvgDomRenderer::SetVirtualResolution(int width, int height)
    {
        virtualWidth_ = width;
        virtualHeight_ = height;
    }

    void SvgDomRenderer::SetPresentationMode(int mode)
    {
        if (mode < static_cast<int>(CnaPresentationMode::Letterbox) ||
            mode > static_cast<int>(CnaPresentationMode::FixedHeightDynamicWidth))
            throw std::out_of_range(
                "SVG_DOM renderer: invalid CnaPresentationMode ordinal " + std::to_string(mode));
        presentationMode_ = static_cast<CnaPresentationMode>(mode);
    }

    bool SvgDomRenderer::TransformWindowToLogical(float windowX, float windowY,
                                                  float& logX, float& logY) const
    {
        if (virtualWidth_ <= 0 || virtualHeight_ <= 0) return false;
        const LogicalViewport viewport = ComputeLogicalViewport();
        if (viewport.width <= 0.0f || viewport.height <= 0.0f) return false;
        logX = (windowX - viewport.x) * viewport.logicalWidth / viewport.width;
        logY = (windowY - viewport.y) * viewport.logicalHeight / viewport.height;
        return windowX >= viewport.x && windowX < viewport.x + viewport.width &&
               windowY >= viewport.y && windowY < viewport.y + viewport.height;
    }

    bool SvgDomRenderer::TransformLogicalToWindow(float logX, float logY,
                                                  float& windowX, float& windowY) const
    {
        if (virtualWidth_ <= 0 || virtualHeight_ <= 0) return false;
        const LogicalViewport viewport = ComputeLogicalViewport();
        if (viewport.logicalWidth <= 0.0f || viewport.logicalHeight <= 0.0f) return false;
        windowX = viewport.x + logX * viewport.width / viewport.logicalWidth;
        windowY = viewport.y + logY * viewport.height / viewport.logicalHeight;
        return true;
    }

    std::unique_ptr<ITextureRenderer> SvgDomRenderer::CreateTexture(const ImageData& data)
    {
        return std::make_unique<SvgDomTextureRenderer>(data);
    }

    std::unique_ptr<ISpriteBatchRenderer> SvgDomRenderer::CreateSpriteBatch()
    {
        return std::make_unique<SvgDomSpriteBatchRenderer>();
    }

    std::unique_ptr<IOcclusionQueryRenderer> SvgDomRenderer::CreateOcclusionQuery()
    {
        ThrowNo3D("CreateOcclusionQuery");
    }

    std::unique_ptr<ITexture3DRenderer> SvgDomRenderer::CreateTexture3D(int, int, int, bool, int)
    {
        ThrowNo3D("CreateTexture3D");
    }

    std::unique_ptr<ITextureCubeRenderer> SvgDomRenderer::CreateTextureCube(int, bool, int)
    {
        ThrowNo3D("CreateTextureCube");
    }

    std::unique_ptr<IRenderTargetCubeRenderer> SvgDomRenderer::CreateRenderTargetCube(
        int, int, bool, bool, int)
    {
        ThrowNo3D("CreateRenderTargetCube");
    }

    std::unique_ptr<IEffectRenderer> SvgDomRenderer::CreateEffectRenderer(
        const std::string&, const std::string&)
    {
        ThrowNo3D("CreateEffectRenderer (custom programmable effects)");
    }

    std::unique_ptr<IRenderTargetRenderer> SvgDomRenderer::CreateRenderTarget2D(
        int w, int h, int depthFormat, bool /*preserveContents*/, bool mipMap, int multiSampleCount)
    {
        if (depthFormat != 0)
            throw std::runtime_error(
                "SVG_DOM renderer: RenderTarget2D depth/stencil attachments are unsupported; "
                "request DepthFormat::None.");
        if (mipMap)
            throw std::runtime_error(
                "SVG_DOM renderer: mipmapped RenderTarget2D resources are unsupported.");
        if (multiSampleCount != 0)
            throw std::runtime_error(
                "SVG_DOM renderer: multisampled RenderTarget2D resources are unsupported; request a "
                "zero sample count.");
        return std::make_unique<SvgDomRenderTargetRenderer>(w, h);
    }

    std::unique_ptr<IRenderTargetRenderer> SvgDomRenderer::CreateRenderTarget2DEXT(
        int w, int h, int depthFormat, bool preserveContents, bool mipMap,
        int multiSampleCount, int surfaceFormat)
    {
        if (surfaceFormat != 0)
            throw std::runtime_error("SVG_DOM renderer: RenderTarget2D supports only SurfaceFormat::Color.");
        return CreateRenderTarget2D(w, h, depthFormat, preserveContents, mipMap, multiSampleCount);
    }

    void SvgDomRenderer::SetRenderTarget2D(IRenderTargetRenderer* rt)
    {
        if (rt)
        {
            if (dynamic_cast<SvgDomRenderTargetRenderer*>(rt) == nullptr)
                throw std::runtime_error(
                    "SVG_DOM renderer: refusing to bind a render target owned by another renderer.");
            rt->BindAsRenderTarget();
        }
        else
        {
            SetBoundRenderTargetIdEXT(0);
        }
    }

    void SvgDomRenderer::SetRenderTargets(const RenderTargetBindingDescriptor* renderTargets, int count)
    {
        if (count < 0)
            throw System::ArgumentOutOfRangeException(
                "count", std::to_string(count), "count must not be negative.");
        if (count > 0 && renderTargets == nullptr)
            throw System::ArgumentNullException(
                "renderTargets", "count was " + std::to_string(count) + " but renderTargets was null.");
        if (count > 1)
            throw std::runtime_error(
                "SVG_DOM renderer: multiple simultaneous render targets (MRT) are not yet "
                "implemented -- requested " + std::to_string(count) + ".");
        if (count > 0 && renderTargets[0].IsRenderTargetCubeFace())
            throw std::runtime_error("SVG_DOM renderer: RenderTargetCube face bindings are not yet implemented.");
        SetRenderTarget2D(count > 0 ? renderTargets[0].GetRenderTarget2D() : nullptr);
    }

    void SvgDomRenderer::ReadBackbuffer(int x, int y, int w, int h, uint8_t* pixels)
    {
        if (GetBoundRenderTargetIdEXT() == 0)
            throw std::runtime_error(
                "SVG_DOM renderer: reading back the backbuffer is not yet implemented, and cannot "
                "be: the backbuffer here is a live SVG subtree composited by the browser, and no "
                "browser API rasterizes one to pixels synchronously. Render into a RenderTarget2D "
                "and read that instead, or use the CANVAS renderer when backbuffer readback is "
                "required.");
        System::ArgumentNullException::ThrowIfNull(pixels, "pixels");
        System::ArgumentOutOfRangeException::ThrowIfNegative(x, "x");
        System::ArgumentOutOfRangeException::ThrowIfNegative(y, "y");
        System::ArgumentOutOfRangeException::ThrowIfNegativeOrZero(w, "w");
        System::ArgumentOutOfRangeException::ThrowIfNegativeOrZero(h, "h");
#if defined(__EMSCRIPTEN__)
        if (CNA_SvgDom_ReadBound(x, y, w, h, pixels) == 0)
            throw std::runtime_error("SVG_DOM renderer: the bound render target could not be read back.");
#else
        (void)x; (void)y; (void)w; (void)h; (void)pixels;
        throw std::runtime_error("SVG_DOM renderer: no browser, so no pixels to read back.");
#endif
    }

    void SvgDomRenderer::SetScissorRect(int x, int y, int w, int h)
    {
        SetCurrentScissorRectEXT(static_cast<float>(x), static_cast<float>(y),
                                 static_cast<float>(w), static_cast<float>(h));
    }

    void SvgDomRenderer::SetViewport(int x, int y, int w, int h, float, float)
    {
        (void)w; (void)h;
        SetCurrentViewportOffsetEXT(static_cast<float>(x), static_cast<float>(y));
    }

    void SvgDomRenderer::ApplyBlendState(int colorSrcBlend, int alphaSrcBlend,
                                         int colorDstBlend, int alphaDstBlend,
                                         int colorBlendFunc, int alphaBlendFunc,
                                         const BlendWriteState& writeState)
    {
        for (int target = 0; target < 4; ++target)
        {
            if (writeState.colorWriteChannels[target] != 15)
                throw std::runtime_error(
                    "SVG_DOM renderer: non-default ColorWriteChannels on render target " +
                    std::to_string(target) + " are unsupported.");
        }
        if (writeState.multiSampleMask != 0xFFFFFFFFu)
            throw std::runtime_error(
                "SVG_DOM renderer: BlendState.MultiSampleMask is unsupported (this renderer has no "
                "multisample storage).");
        SetCurrentCompositeOpEXT(BlendStateToDomCompositeOp(
            colorSrcBlend, alphaSrcBlend, colorDstBlend, alphaDstBlend, colorBlendFunc, alphaBlendFunc));
    }

    void SvgDomRenderer::ApplyDepthStencilState(
        bool depthEnable, bool depthWriteEnable, int,
        bool stencilEnable, int, int, int, int, int, int, int,
        bool, int, int, int, int)
    {
        if (depthEnable || depthWriteEnable || stencilEnable)
            ThrowNo3D("ApplyDepthStencilState");
    }

    void SvgDomRenderer::SetReferenceStencil(int value)
    {
        if (value != 0)
            ThrowNo3D("SetReferenceStencil");
    }

    void SvgDomRenderer::ApplyRasterizerState(int cullMode, int fillMode, bool scissorTestEnable,
                                              float depthBias, float slopeScaleDepthBias)
    {
        if (cullMode != 0 && cullMode != 2)
            ThrowNo3D("RasterizerState.CullMode::CullClockwiseFace");
        if (fillMode != 0)
            ThrowNo3D("RasterizerState.FillMode::WireFrame");
        if (depthBias != 0.0f || slopeScaleDepthBias != 0.0f)
            ThrowNo3D("RasterizerState depth bias");
        SetCurrentScissorEnableEXT(scissorTestEnable);
    }

    bool SvgDomRenderer::SupportsCapability(CNA::GraphicsCapability capability) const
    {
        switch (capability)
        {
            case CNA::GraphicsCapability::AdditiveBlending:
#if defined(__EMSCRIPTEN__)
                return CNA_SvgDom_SupportsAdditiveBlending() != 0;
#else
                return false;
#endif
            default:
                return false;
        }
    }

    void SvgDomRenderer::Ensure3DSupported(const char* operation) const
    {
        ThrowNo3D(operation ? operation : "3D operation");
    }

    void SvgDomRenderer::ClearColorAndDepth(float, float, float, float, float) { ThrowNo3D("ClearColorAndDepth (3D)"); }
    void SvgDomRenderer::ClearDepth(float) { ThrowNo3D("ClearDepth (3D)"); }
    void SvgDomRenderer::ClearStencil(int) { ThrowNo3D("ClearStencil (3D)"); }
    void SvgDomRenderer::ClearDepthAndStencil(float, int) { ThrowNo3D("ClearDepthAndStencil (3D)"); }
    void SvgDomRenderer::ClearColorAndStencil(float, float, float, float, int) { ThrowNo3D("ClearColorAndStencil (3D)"); }
    void SvgDomRenderer::ClearColorDepthAndStencil(float, float, float, float, float, int) { ThrowNo3D("ClearColorDepthAndStencil (3D)"); }
    void SvgDomRenderer::SetDepthTestEnabled(bool) { ThrowNo3D("SetDepthTestEnabled (3D)"); }
    void SvgDomRenderer::SetBlendEnabled(bool) { ThrowNo3D("SetBlendEnabled (3D)"); }
    void SvgDomRenderer::SetDepthWriteEnabled(bool) { ThrowNo3D("SetDepthWriteEnabled (3D)"); }

    std::unique_ptr<IVertexBufferRenderer> SvgDomRenderer::CreateVertexBuffer(int)
    {
        ThrowNo3D("CreateVertexBuffer (3D)");
    }

    std::unique_ptr<IIndexBufferRenderer> SvgDomRenderer::CreateIndexBuffer16(int)
    {
        ThrowNo3D("CreateIndexBuffer16 (3D)");
    }

    void SvgDomRenderer::DrawColoredPrimitives(const IVertexBufferRenderer&,
                                               const Matrix&, const Matrix&, const Matrix&,
                                               PrimitiveType, int) { ThrowNo3D("DrawColoredPrimitives (3D)"); }

    void SvgDomRenderer::DrawIndexedColoredPrimitives(const IVertexBufferRenderer&, const IIndexBufferRenderer&,
                                                      const Matrix&, const Matrix&, const Matrix&,
                                                      PrimitiveType, int) { ThrowNo3D("DrawIndexedColoredPrimitives (3D)"); }
}

namespace CNA::Internal::Renderers
{
    std::unique_ptr<IGraphicsRenderer> CreateGraphicsRenderer(const GraphicsRendererCreateArgs& args)
    {
        return std::make_unique<SvgDom::SvgDomRenderer>(
            args.window, args.virtualWidth, args.virtualHeight, args.presentationMode);
    }
}
