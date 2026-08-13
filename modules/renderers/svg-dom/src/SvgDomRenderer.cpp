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

// plan_svg_dom.md design decision 1/6 and SVGDOM-5: creates the SVG surface over the <canvas>
// SDL3's Emscripten video driver owns. The canvas remains in layout AND in pointer hit testing
// (opacity:0, not visibility:hidden), because SDL registered its mouse/touch handlers on that exact
// element. The visible SVG surface is pointer-transparent, so events fall through to the canvas.
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
    root.style.cssText = 'position:absolute;left:0;top:0;overflow:hidden;pointer-events:none;';
    root.setAttribute('pointer-events', 'none');
    const defs = document.createElementNS(svgNS, 'defs');
    const bg = document.createElementNS(svgNS, 'rect');
    bg.setAttribute('x', 0); bg.setAttribute('y', 0);
    bg.setAttribute('width', '100%'); bg.setAttribute('height', '100%');
    root.appendChild(defs);
    root.appendChild(bg);
    (canvas.parentNode || document.body).insertBefore(root, canvas.nextSibling);
    Module['cnaSvgDomOriginalCanvasOpacity'] = canvas.style.opacity;
    canvas.style.opacity = '0';
    Module['cnaSvgDomRoot'] = root;
    Module['cnaSvgDomDefs'] = defs;
    Module['cnaSvgDomBackgroundRect'] = bg;
    Module['cnaSvgDomBoundCtx'] = null;
    Module['cnaSvgDomFilterCache'] = {};
    Module['cnaSvgDomBackgroundFill'] = null;
    Module['cnaSvgDomSurfaceGeometry'] = {};

    // SVGDOM-A (was: sibling containers keyed by CLIP-RECT IDENTITY, so cross-region paint order
    // followed each region's own first-CREATION order rather than the actual per-flush draw
    // sequence -- a batch drawn 4th could wrongly paint UNDER a batch drawn 1st merely because the
    // 1st batch's rect happened to get its container created first). Fixed architecture: ONE
    // ordered array of per-FLUSH slots, appended to `root` in flush order and NEVER reordered after
    // creation, so DOM document order is BY CONSTRUCTION identical to actual SpriteBatch flush
    // order -- SVG's own fundamental painting model (later document order paints on top) then does
    // the rest correctly, with no reliance on any browser's CSS z-index-on-SVG support. Consecutive
    // flushes that share the exact same effective clip rect (Viewport ∩ Scissor, SVGDOM-F) are
    // coalesced into the SAME slot (see cnaSvgDomClaimFlushSlot below) so the common case -- many
    // unscissored draws, or Immediate-mode sprites sharing one clip state -- still shares one pooled
    // container the way the old 'full' region did, rather than paying one wrapper <g> per flush.
    Module['cnaSvgDomFlushSlots'] = [];
    Module['cnaSvgDomFlushCursor'] = 0;
    Module['cnaSvgDomFlushHighWater'] = 0;
    Module['cnaSvgDomLastFlushKey'] = null;
    Module['cnaSvgDomFrameToken'] = 0;
    Module['cnaSvgDomNextClipId'] = 0;

    // Resolves (creating or reusing, always in flush-chronological order) the slot that should own
    // sprites drawn under the given effective clip rect (an {x,y,w,h} object in absolute
    // logical/viewBox pixels -- already the Viewport ∩ Scissor intersection, see
    // SvgDomState::ComputeEffectiveClipRectEXT -- or null when nothing clips this flush).
    Module['cnaSvgDomClaimFlushSlot'] = function (rect) {
        const logicalW = Module['cnaSvgDomLogicalW'] || 0;
        const logicalH = Module['cnaSvgDomLogicalH'] || 0;
        const isFull = !rect || (rect.x <= 0 && rect.y <= 0 &&
                                  rect.x + rect.w >= logicalW && rect.y + rect.h >= logicalH);
        const key = isFull ? 'full' : (rect.x + ',' + rect.y + ',' + rect.w + ',' + rect.h);

        const slots = Module['cnaSvgDomFlushSlots'];
        let idx;
        if (Module['cnaSvgDomLastFlushKey'] === key && Module['cnaSvgDomFlushCursor'] > 0) {
            // Coalesce: this flush's clip state is identical to the flush immediately before it in
            // THIS frame, so it shares that flush's own slot (and DOM position) instead of claiming
            // a new one -- this is what keeps many same-state Immediate/Deferred flushes as cheap as
            // the old single 'full' region was, while flushes separated by a DIFFERENT clip state
            // still each get their own, correctly ordered slot.
            idx = Module['cnaSvgDomFlushCursor'] - 1;
        } else {
            idx = Module['cnaSvgDomFlushCursor']++;
        }
        Module['cnaSvgDomLastFlushKey'] = key;

        let slot = slots[idx];
        if (!slot) {
            const svgNS2 = 'http://www.w3.org/2000/svg';
            const container = document.createElementNS(svgNS2, 'g');
            // New slots always append at the end: slot index N is therefore always the (N+1)th
            // sprite-bearing child of root, forever -- the invariant that makes document order
            // equal flush order without ever needing to reorder an existing node.
            root.appendChild(container);
            slot = { container: container, pool: [], used: 0, spriteHighWater: 0,
                     clipId: null, clipPathEl: null, clipRectEl: null, clipKey: undefined,
                     frameToken: -1 };
            slots[idx] = slot;
        }
        if (slot.container.style.display === 'none') slot.container.style.removeProperty('display');

        const frameToken = Module['cnaSvgDomFrameToken'];
        if (slot.frameToken !== frameToken) {
            // First touch on this slot THIS frame (a fresh claim, not a same-frame coalesce): any
            // sprites it held are from an EARLIER frame and must not be treated as already written
            // this flush.
            slot.used = 0;
            slot.frameToken = frameToken;
        }

        if (slot.clipKey !== key) {
            if (isFull) {
                if (slot.container.hasAttribute('clip-path')) slot.container.removeAttribute('clip-path');
            } else {
                if (!slot.clipPathEl) {
                    const svgNS3 = 'http://www.w3.org/2000/svg';
                    const clipPath = document.createElementNS(svgNS3, 'clipPath');
                    const clipId = 'cna-svg-dom-clip-' + (Module['cnaSvgDomNextClipId'] =
                        (Module['cnaSvgDomNextClipId'] || 0) + 1);
                    clipPath.id = clipId;
                    const clipRectEl = document.createElementNS(svgNS3, 'rect');
                    clipPath.appendChild(clipRectEl);
                    defs.appendChild(clipPath);
                    slot.clipPathEl = clipPath;
                    slot.clipId = clipId;
                    slot.clipRectEl = clipRectEl;
                    slot.container.setAttribute('clip-path', 'url(#' + clipId + ')');
                }
                slot.clipRectEl.setAttribute('x', rect.x);
                slot.clipRectEl.setAttribute('y', rect.y);
                slot.clipRectEl.setAttribute('width', Math.max(0, rect.w));
                slot.clipRectEl.setAttribute('height', Math.max(0, rect.h));
            }
            slot.clipKey = key;
        }
        return slot;
    };
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
    if (canvas) canvas.style.opacity = Module['cnaSvgDomOriginalCanvasOpacity'] || "";
    Module['cnaSvgDomOriginalCanvasOpacity'] = null;
    Module['cnaSvgDomRoot'] = null;
    Module['cnaSvgDomDefs'] = null;
    Module['cnaSvgDomBackgroundRect'] = null;
    Module['cnaSvgDomFilterCache'] = null;
    Module['cnaSvgDomBackgroundFill'] = null;
    Module['cnaSvgDomSurfaceGeometry'] = null;
    Module['cnaSvgDomFlushSlots'] = null;
    Module['cnaSvgDomFlushCursor'] = 0;
    Module['cnaSvgDomFlushHighWater'] = 0;
    Module['cnaSvgDomLastFlushKey'] = null;
    Module['cnaSvgDomFrameToken'] = 0;
    Module['cnaSvgDomNextClipId'] = 0;
    Module['cnaSvgDomClaimFlushSlot'] = null;
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
    const fill = 'rgba(' + Math.round(r * 255) + ',' + Math.round(g * 255) + ',' +
                 Math.round(b * 255) + ',' + a + ')';
    if (Module['cnaSvgDomBackgroundFill'] !== fill) {
        bg.setAttribute('fill', fill);
        Module['cnaSvgDomBackgroundFill'] = fill;
    }
    // GraphicsDevice.Clear is a real OPERATION, not merely a "frame start" convention: it must erase
    // everything drawn so far into the active target while leaving anything drawn AFTER it alone
    // (Draw, Clear, Draw, Present must show only the second Draw's content -- both for the
    // backbuffer and mid-frame). Rewinding the flush cursor to 0 (not tearing any pool down) means
    // the very NEXT flush reclaims slot 0 first, overwriting whatever it held, exactly reproducing
    // that "erase everything before, keep everything after" contract; Present()'s own high-water
    // hide (below) then hides any slot nothing after this Clear() reclaimed this frame. Bumping the
    // frame token (rather than eagerly resetting every slot's own `used`) is what lets
    // cnaSvgDomClaimFlushSlot cheaply tell "was this slot already touched since the last Clear()" so
    // a stale slot beyond this frame's flush count keeps its LAST frame's sprite count until Present
    // decides whether to hide it, instead of an O(slot count) sweep on every single Clear() call.
    Module['cnaSvgDomFlushCursor'] = 0;
    Module['cnaSvgDomLastFlushKey'] = null;
    Module['cnaSvgDomFrameToken'] = (Module['cnaSvgDomFrameToken'] || 0) + 1;
});

// Ends the frame: hides whole flush-slot containers this frame's flush count did not reach (a
// frame with fewer distinct clip-state flushes than the previous one), tracked by a high-water
// mark over the SLOT array itself (SVGDOM-A) -- then, within every slot actually used this frame,
// hides only the pooled sprite elements it did not reuse, each tracked by its OWN per-slot
// high-water mark (the same shape HtmlDom's own CNA_HtmlDom_PresentFrame uses for its regions), so
// a steady frame (same flush sequence, same per-flush sprite counts) touches nothing here at all.
// There is nothing to swap -- the browser compositor presents the SVG subtree on its next paint
// tick. Cursors are rewound by CNA_SvgDom_Clear (this renderer's own frame-start marker), not here.
EM_JS(void, CNA_SvgDom_PresentFrame, (), {
    const slots = Module['cnaSvgDomFlushSlots'];
    if (!slots) return;
    const cursor = Module['cnaSvgDomFlushCursor'] || 0;
    const high = Module['cnaSvgDomFlushHighWater'] || 0;
    for (let i = cursor; i < high; ++i) {
        const slot = slots[i];
        if (slot && slot.container.style.display !== 'none') slot.container.style.display = 'none';
    }
    Module['cnaSvgDomFlushHighWater'] = cursor;

    for (let i = 0; i < cursor; ++i) {
        const slot = slots[i];
        const pool = slot.pool;
        const used = slot.used || 0;
        const spriteHigh = slot.spriteHighWater || 0;
        for (let j = used; j < spriteHigh; ++j) {
            const entryEl = pool[j];
            if (entryEl && !entryEl.cna.hidden) {
                entryEl.g.style.display = 'none';
                entryEl.cna.hidden = true;
            }
        }
        slot.spriteHighWater = used;
    }
});

// Records the backbuffer's own logical geometry and physical placement (offset + scale). SVGDOM-5:
// Present still checks the geometry every frame so host-page resize/layout changes are observed,
// but unchanged fields do not get written back. Reassigning width/height/viewBox/styles on a large
// live SVG tree can invalidate layout and painting in Firefox even when the string value is equal.
EM_JS(void, CNA_SvgDom_ApplySurfaceGeometry, (int logicalW, int logicalH,
                                              double offsetX, double offsetY,
                                              double scaleX, double scaleY), {
    const root = Module['cnaSvgDomRoot'];
    if (!root) return;
    const geometry = Module['cnaSvgDomSurfaceGeometry'] ||
                     (Module['cnaSvgDomSurfaceGeometry'] = {});
    if (geometry.logicalW !== logicalW) {
        root.setAttribute('width', logicalW);
        geometry.logicalW = logicalW;
    }
    if (geometry.logicalH !== logicalH) {
        root.setAttribute('height', logicalH);
        geometry.logicalH = logicalH;
    }
    const viewBox = '0 0 ' + logicalW + ' ' + logicalH;
    if (geometry.viewBox !== viewBox) {
        root.setAttribute('viewBox', viewBox);
        geometry.viewBox = viewBox;
    }
    if (geometry.offsetX !== offsetX) {
        root.style.left = offsetX + 'px';
        geometry.offsetX = offsetX;
    }
    if (geometry.offsetY !== offsetY) {
        root.style.top = offsetY + 'px';
        geometry.offsetY = offsetY;
    }
    const physicalW = logicalW * scaleX;
    const physicalH = logicalH * scaleY;
    if (geometry.physicalW !== physicalW) {
        root.style.width = physicalW + 'px';
        geometry.physicalW = physicalW;
    }
    if (geometry.physicalH !== physicalH) {
        root.style.height = physicalH + 'px';
        geometry.physicalH = physicalH;
    }
    // cnaSvgDomClaimFlushSlot's own "does this rect already cover the whole surface"
    // collapse-to-'full' check (SVGDOM-A/F) needs the CURRENT logical size -- stashed here rather
    // than re-queried from the SDL window, mirroring HtmlDom's own cnaDomLogicalW/H.
    Module['cnaSvgDomLogicalW'] = logicalW;
    Module['cnaSvgDomLogicalH'] = logicalH;
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
        SetCurrentViewportRectEXT(0, 0, 0, 0);
#if defined(__EMSCRIPTEN__)
        CNA_SvgDom_EnsureRoot();
#endif
        // SVGDOM-C: publish real logical geometry immediately, not only from the first Present().
        // A game can legally SpriteBatch-draw (with a scissor rect) before its first Present() ever
        // runs; without this, cnaSvgDomLogicalW/H stay 0 until then, so
        // cnaSvgDomClaimFlushSlot's own "does this rect already cover the whole surface" check
        // misclassifies a real sub-rect clip as covering nothing (0x0), collapsing it to the
        // unclipped 'full' slot.
        ApplySurfaceGeometryEXT();
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
        // SVGDOM-C: push the new geometry to JS immediately rather than waiting for the next
        // Present() -- a game can legally SpriteBatch-draw (with a scissor rect) in the SAME frame
        // it changes virtual resolution, before Present() ever runs, and that draw's "does this
        // scissor rect cover the whole surface" classification must see the NEW logical size.
        ApplySurfaceGeometryEXT();
    }

    void SvgDomRenderer::SetPresentationMode(int mode)
    {
        if (mode < static_cast<int>(CnaPresentationMode::Letterbox) ||
            mode > static_cast<int>(CnaPresentationMode::FixedHeightDynamicWidth))
            throw std::out_of_range(
                "SVG_DOM renderer: invalid CnaPresentationMode ordinal " + std::to_string(mode));
        presentationMode_ = static_cast<CnaPresentationMode>(mode);
        ApplySurfaceGeometryEXT(); // SVGDOM-C: same reasoning as SetVirtualResolution above.
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
        // SVGDOM-F: Width/Height are retained (not just X/Y) -- they define an unconditional clip
        // rectangle every draw (SVG backbuffer or render-target-bound Canvas2D) must honour,
        // independent of RasterizerState.ScissorTestEnable; see ComputeEffectiveClipRectEXT and its
        // callers in SvgDomSpriteBatchRenderer::Flush. minDepth/maxDepth stay ignored -- no depth
        // buffer exists on this renderer.
        SetCurrentViewportRectEXT(static_cast<float>(x), static_cast<float>(y),
                                  static_cast<float>(w), static_cast<float>(h));
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
