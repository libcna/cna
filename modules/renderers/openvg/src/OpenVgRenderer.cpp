// SPDX-License-Identifier: MS-PL
#include "CNA/Internal/Renderers/OpenVg/OpenVgRenderer.hpp"
#include "CNA/Internal/Renderers/OpenVg/OpenVgTextureRenderer.hpp"
#include "CNA/Internal/Renderers/OpenVg/OpenVgSpriteBatchRenderer.hpp"
#include "CNA/Internal/Renderers/Common/NoOp3DResources.hpp"

#include "openvg.h"

#include <SDL3/SDL.h>

// P1-8: portable GL/GLU header selection -- mirrors the pinned ShivaVG source's own shDefs.h
// platform branch exactly (Apple's headers live under a different framework path, and MSVC's
// <GL/gl.h> requires <windows.h> to already be included), rather than assuming the Linux/X11
// header layout unconditionally.
#if defined(__APPLE__)
#include <OpenGL/gl.h>
#include <OpenGL/glu.h>
#elif defined(_WIN32)
#include <windows.h>
#include <GL/gl.h>
#include <GL/glu.h>
#else
#include <GL/gl.h>
#include <GL/glu.h>
#endif

#include <algorithm>
#include <atomic>
#include <cmath>
#include <stdexcept>
#include <string>

namespace CNA::Internal::Renderers::OpenVg
{
    namespace
    {
        // Raw XNA Blend/BlendFunction ordinals, same table every other renderer's own
        // ApplyBlendState mapping (e.g. CanvasRenderer::BlendStateToCompositeOp) uses:
        // One=0, Zero=1, SourceAlpha=4, InverseSourceAlpha=5; Add=0.
        constexpr int kBlendOne = 0, kBlendZero = 1, kBlendSourceAlpha = 4, kBlendInvSourceAlpha = 5;
        constexpr int kBlendFuncAdd = 0;

        // FillMode::Solid = 0 (XNA/FNA ordinal) -- the only value ShivaVG can honor (vgDrawImage/
        // vgDrawPath always rasterize filled; no unfilled-polygon draw path exists).
        constexpr int kFillModeSolid = 0;

        // ShivaVG (see shContext.c's own `static VGContext *g_context`) is a genuine process-wide
        // singleton: vgCreateContextSH() silently returns VG_TRUE and reuses the existing context
        // if one is already live, and vgDestroyContextSH() unconditionally frees it. Two
        // concurrently-live CNA OpenVgRenderer instances would therefore silently alias one
        // ShivaVG context, and the first renderer destroyed would free it out from under the
        // second (a use-after-free on its very next OpenVG call). This process-wide flag is the
        // smallest robust fix: only one OpenVgRenderer may be live at a time; sequential
        // construct/destroy/construct cycles remain fully supported.
        std::atomic<bool> g_shivaVgContextOwned{false};
    }

    // ShivaVG's own updateBlendingStateGL (src/shPipeline.c) implements VG_BLEND_SRC (opaque,
    // blending disabled) and VG_BLEND_SRC_OVER (glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA),
    // i.e. straight/non-premultiplied-alpha-over) for real. VG_BLEND_ADDITIVE/MULTIPLY/SCREEN/
    // DARKEN/LIGHTEN are declared in openvg.h's VGBlendMode enum but have NO case in that switch --
    // they silently fall through to the `case VG_BLEND_SRC_OVER: default:` arm, i.e. asking
    // ShivaVG for additive blending silently renders ordinary alpha blending instead (verified
    // directly against the pinned shPipeline.c source). Accepting that here would be exactly the
    // kind of unfaithful "capability lie" this project's own GraphicsCapability docs warn against,
    // so BlendState.Additive is rejected rather than silently mis-rendered.
    //
    // ImageData/OpenVgTextureRenderer always store STRAIGHT (non-premultiplied) alpha (see
    // OpenVgTextureRenderer.hpp's own doc comment) -- CNA's content pipeline does not premultiply
    // Texture2D pixels at upload time. For straight source data, VG_BLEND_SRC_OVER's real GL
    // blend func (GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA) computes the exact correct "straight-over"
    // composite, which is what BOTH BlendState.NonPremultiplied and BlendState.AlphaBlend visually
    // need for a straight-alpha source texture (XNA's AlphaBlend preset's own SourceBlend=One
    // hardware contract technically assumes an already-premultiplied source; since this renderer's
    // textures are never premultiplied, mapping AlphaBlend to VG_BLEND_SRC_OVER too gives the
    // correct pixel result for every texture this renderer can actually produce -- SpriteBatch's
    // overwhelmingly common default blend state -- but would diverge from real XNA if a caller
    // manually uploaded already-premultiplied bytes via Texture2D::SetData, a narrow and documented
    // gap, not a silent general-purpose one).
    int BlendStateToVgBlendMode(int colorSrcBlend, int alphaSrcBlend,
                                int colorDstBlend, int alphaDstBlend,
                                int colorBlendFunc, int alphaBlendFunc)
    {
        const bool isAdd = colorBlendFunc == kBlendFuncAdd && alphaBlendFunc == kBlendFuncAdd;
        const bool symmetric = colorSrcBlend == alphaSrcBlend && colorDstBlend == alphaDstBlend;

        if (isAdd && symmetric && colorSrcBlend == kBlendOne && colorDstBlend == kBlendZero)
            return VG_BLEND_SRC; // Opaque
        if (isAdd && symmetric && colorSrcBlend == kBlendSourceAlpha && colorDstBlend == kBlendInvSourceAlpha)
            return VG_BLEND_SRC_OVER; // NonPremultiplied
        if (isAdd && symmetric && colorSrcBlend == kBlendOne && colorDstBlend == kBlendInvSourceAlpha)
            return VG_BLEND_SRC_OVER; // AlphaBlend (documented straight-source caveat above)

        throw std::runtime_error(
            "OpenVG (ShivaVG) only supports the Opaque/AlphaBlend/NonPremultiplied BlendState "
            "presets: VG_BLEND_ADDITIVE is declared by OpenVG but not actually implemented by "
            "ShivaVG (silently falls back to normal alpha blending), and there is no generic "
            "blend-factor/equation model to express an arbitrary custom BlendState.");
    }

    OpenVgRenderer::OpenVgRenderer(SDL_Window* window, int virtualWidth, int virtualHeight,
                                   CnaPresentationMode mode, int swapInterval)
        : window_(window)
        , virtualWidth_(virtualWidth)
        , virtualHeight_(virtualHeight)
        , presentationMode_(mode)
    {
        if (!window_) throw std::runtime_error("OpenVgRenderer initialized with null window.");

        {
            bool expected = false;
            if (!g_shivaVgContextOwned.compare_exchange_strong(expected, true))
            {
                throw std::runtime_error(
                    "OPENVG: another OpenVgRenderer is already live in this process. ShivaVG's "
                    "vgCreateContextSH()/vgDestroyContextSH() (shContext.c) operate on a single "
                    "process-global VGContext, so two CNA OpenVG renderers cannot safely coexist "
                    "-- destroy the existing one before constructing another.");
            }
        }
        ownsGlobalContext_ = true;

        // Exception-safe from here on: every acquired resource (the global-context ownership
        // flag above, and the SDL GL context below) is released on any throw before this
        // constructor commits, since this object's own destructor never runs for a
        // not-yet-fully-constructed instance.
        try
        {
            // ShivaVG is fixed-function/immediate-mode desktop GL (glVertexPointer/glDrawArrays-era
            // client vertex arrays, no VAOs/shaders) -- it needs a genuine compatibility-profile
            // context. The profile mask ALONE is not enough to reliably get one (found empirically:
            // vgClear worked but vgDrawPath/vgDrawImage silently rasterized nothing on this
            // environment's Mesa software GL, consistent with a driver-chosen core profile silently
            // ignoring client-array draw calls) -- pinning an explicit legacy version alongside the
            // mask, the same combination OpenGL2Renderer's own constructor uses for the identical
            // reason, is what actually forces a compatibility context.
            SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_COMPATIBILITY);
            SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
            SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 1);

            glContext_ = SDL_GL_CreateContext(window_);
            if (!glContext_)
                throw std::runtime_error(std::string("OPENVG: SDL_GL_CreateContext failed: ") + SDL_GetError());
            if (!SDL_GL_MakeCurrent(window_, glContext_))
                throw std::runtime_error(std::string("OPENVG: SDL_GL_MakeCurrent failed: ") + SDL_GetError());

            int physW = 0, physH = 0;
            SDL_GetWindowSizeInPixels(window_, &physW, &physH);
            if (physW <= 0) physW = 1;
            if (physH <= 0) physH = 1;

            if (!vgCreateContextSH(physW, physH))
                throw std::runtime_error("OPENVG: vgCreateContextSH failed.");

            lastPhysW_ = physW;
            lastPhysH_ = physH;
            refreshPresentationDerivedStateEXT();
            SetSwapInterval(swapInterval);

            IGraphicsRenderer::RegisterForWindow(window_, this);
        }
        catch (...)
        {
            if (glContext_)
            {
                SDL_GL_MakeCurrent(window_, nullptr);
                SDL_GL_DestroyContext(glContext_);
                glContext_ = nullptr;
            }
            if (ownsGlobalContext_)
            {
                g_shivaVgContextOwned.store(false);
                ownsGlobalContext_ = false;
            }
            throw;
        }
    }

    OpenVgRenderer::~OpenVgRenderer()
    {
        IGraphicsRenderer::UnregisterForWindow(window_);
        // P2-2: re-establish currentness before touching ShivaVG's context -- some other GL
        // consumer in this process may have changed the current context since this renderer's
        // last call (the single-live-OpenVG-renderer rule only guarantees no OTHER OpenVgRenderer
        // shares ShivaVG's global context, not that nothing else in the process ever calls
        // SDL_GL_MakeCurrent).
        if (glContext_)
            SDL_GL_MakeCurrent(window_, glContext_);
        vgDestroyContextSH();
        if (glContext_)
        {
            SDL_GL_MakeCurrent(window_, nullptr);
            SDL_GL_DestroyContext(glContext_);
        }
        if (ownsGlobalContext_)
        {
            g_shivaVgContextOwned.store(false);
            ownsGlobalContext_ = false;
        }
    }

    // Mirrors OpenGL2Renderer::ComputeLogicalViewport's algorithm exactly (see that renderer's own
    // "reference implementation" doc comment on IGraphicsRenderer::GetDefaultViewportRect), with
    // one deliberate change: physical-pixel-based throughout (SDL_GetWindowSizeInPixels), not
    // window-point-based (SDL_GetWindowSize) -- glViewport/gluOrtho2D/glReadPixels all require real
    // physical pixels, and conflating the two would be wrong under a high-DPI window scale factor
    // (P0-1's own "account correctly for high-DPI" requirement). NativeBackBuffer/no-virtual-
    // resolution/zero-size-window all degenerate to the full physical window with no scaling.
    OpenVgRenderer::LogicalViewport OpenVgRenderer::ComputeLogicalViewportEXT() const
    {
        int physW = 0, physH = 0;
        SDL_GetWindowSizeInPixels(window_, &physW, &physH);

        LogicalViewport viewport{};
        viewport.width = static_cast<float>(std::max(0, physW));
        viewport.height = static_cast<float>(std::max(0, physH));
        viewport.logicalWidth = viewport.width;
        viewport.logicalHeight = viewport.height;
        if (physW <= 0 || physH <= 0)
            return viewport;
        if (presentationMode_ == CnaPresentationMode::NativeBackBuffer ||
            virtualWidth_ <= 0 || virtualHeight_ <= 0)
            return viewport;

        float logicalWidth = static_cast<float>(virtualWidth_);
        float logicalHeight = static_cast<float>(virtualHeight_);
        if (presentationMode_ == CnaPresentationMode::FixedHeightDynamicWidth)
        {
            logicalHeight = static_cast<float>(virtualHeight_);
            logicalWidth = logicalHeight * static_cast<float>(physW) / static_cast<float>(physH);
            viewport.logicalWidth = logicalWidth;
            viewport.logicalHeight = logicalHeight;
            return viewport;
        }

        viewport.logicalWidth = logicalWidth;
        viewport.logicalHeight = logicalHeight;
        if (presentationMode_ == CnaPresentationMode::Stretch)
            return viewport;

        // Letterbox/Overscan: scale uniformly (min for Letterbox -- shrink to fit inside the
        // window, adding bars; max for Overscan -- grow to cover the window, cropping the excess)
        // and centre the result.
        const float sx = static_cast<float>(physW) / logicalWidth;
        const float sy = static_cast<float>(physH) / logicalHeight;
        const float scale = presentationMode_ == CnaPresentationMode::Overscan ? std::max(sx, sy) : std::min(sx, sy);
        viewport.width = logicalWidth * scale;
        viewport.height = logicalHeight * scale;
        viewport.x = (static_cast<float>(physW) - viewport.width) * 0.5f;
        viewport.y = (static_cast<float>(physH) - viewport.height) * 0.5f;
        return viewport;
    }

    int OpenVgRenderer::GetPhysicalHeightEXT() const
    {
        int physH = 0;
        SDL_GetWindowSizeInPixels(window_, nullptr, &physH);
        return physH > 0 ? physH : 1;
    }

    int OpenVgRenderer::GetLogicalHeightEXT() const
    {
        const LogicalViewport viewport = ComputeLogicalViewportEXT();
        const int h = static_cast<int>(std::lround(viewport.logicalHeight));
        return h > 0 ? h : 1;
    }

    void OpenVgRenderer::applyOrtho()
    {
        const LogicalViewport viewport = ComputeLogicalViewportEXT();
        const double logicalW = viewport.logicalWidth > 0.0f ? viewport.logicalWidth : 1.0;
        const double logicalH = viewport.logicalHeight > 0.0f ? viewport.logicalHeight : 1.0;
        glMatrixMode(GL_PROJECTION);
        glLoadIdentity();
        gluOrtho2D(0.0, logicalW, 0.0, logicalH);
        glMatrixMode(GL_MODELVIEW);
        glLoadIdentity();
    }

    void OpenVgRenderer::applyViewportGL()
    {
        const int physH = GetPhysicalHeightEXT();
        glViewport(viewportX_, physH - viewportY_ - viewportH_, viewportW_, viewportH_);
        glDepthRange(static_cast<double>(viewportMinDepth_), static_cast<double>(viewportMaxDepth_));
    }

    void OpenVgRenderer::applyScissorRectGL()
    {
        // Unlike SetViewport (a raw pass-through, matching every sibling renderer's own
        // convention -- see the class doc comment), the scissor rectangle IS mapped through the
        // current presentation transform: GraphicsDevice's own auto-reset path
        // (GraphicsDevice::UpdateViewportFromWindow) always resets ScissorRectangle to
        // (0,0,logicalWidth,logicalHeight) in LOGICAL units, in the SAME absolute logical-canvas
        // space SpriteBatch draws use -- so mapping it through ComputeLogicalViewportEXT() the same
        // way SpriteBatch's own device-flip transform does is what makes "scissor + presentation
        // scaling" (Letterbox/Overscan/Stretch bars/crop) actually clip the correct physical
        // pixels, not merely the correct LOGICAL numbers reinterpreted as physical ones.
        const LogicalViewport viewport = ComputeLogicalViewportEXT();
        const float sx = viewport.logicalWidth > 0.0f ? viewport.width / viewport.logicalWidth : 1.0f;
        const float sy = viewport.logicalHeight > 0.0f ? viewport.height / viewport.logicalHeight : 1.0f;
        const float physX = viewport.x + static_cast<float>(scissorX_) * sx;
        const float physYTop = viewport.y + static_cast<float>(scissorY_) * sy;
        const float physW = static_cast<float>(scissorW_) * sx;
        const float physH = static_cast<float>(scissorH_) * sy;
        const int surfaceH = GetPhysicalHeightEXT();

        const VGint rect[4] = {
            static_cast<VGint>(std::lround(physX)),
            static_cast<VGint>(std::lround(static_cast<float>(surfaceH) - physYTop - physH)),
            static_cast<VGint>(std::max(0.0f, std::round(physW))),
            static_cast<VGint>(std::max(0.0f, std::round(physH)))
        };
        vgSetiv(VG_SCISSOR_RECTS, 4, rect);
    }

    void OpenVgRenderer::applyScissorEnableGL()
    {
        vgSeti(VG_SCISSORING, scissorEnabled_ ? VG_TRUE : VG_FALSE);
    }

    void OpenVgRenderer::refreshPresentationDerivedStateEXT()
    {
        applyOrtho();
        if (!viewportSet_)
        {
            int x = 0, y = 0, w = 0, h = 0;
            GetDefaultViewportRect(x, y, w, h);
            viewportX_ = x; viewportY_ = y; viewportW_ = w; viewportH_ = h;
        }
        applyViewportGL();
        applyScissorRectGL();
        applyScissorEnableGL();
    }

    void OpenVgRenderer::EnsureSurfaceSizeEXT()
    {
        int physW = 0, physH = 0;
        SDL_GetWindowSizeInPixels(window_, &physW, &physH);
        if (physW <= 0) physW = 1;
        if (physH <= 0) physH = 1;
        if (physW == lastPhysW_ && physH == lastPhysH_)
            return;

        lastPhysW_ = physW;
        lastPhysH_ = physH;
        // vgResizeSurfaceSH keeps ShivaVG's own context->surfaceWidth/surfaceHeight in sync with
        // the real physical framebuffer -- vgClear's own clip-to-window clamp (shContext.c) uses
        // those fields directly, so letting them go stale after a resize would silently shrink a
        // later full-surface Clear() back to the OLD (smaller) size on window growth. This call
        // also resets ShivaVG's OWN glViewport/GL_PROJECTION to the full new physical surface as a
        // side effect (shContext.c); refreshPresentationDerivedStateEXT() below immediately
        // reapplies this renderer's own authoritative ortho/viewport/scissor state on top, so a
        // caller-visible custom Viewport/ScissorRectangle survives the resize unchanged (P0-4).
        vgResizeSurfaceSH(physW, physH);
        refreshPresentationDerivedStateEXT();
    }

    void OpenVgRenderer::Clear(float r, float g, float b, float a)
    {
        EnsureSurfaceSizeEXT();
        const int physW = std::max(1, lastPhysW_);
        const int physH = std::max(1, lastPhysH_);
        const VGfloat clearColor[4] = { r, g, b, a };
        vgSetfv(VG_CLEAR_COLOR, 4, clearColor);
        // Always the full CURRENT physical surface -- deliberately independent of whatever custom
        // Viewport/ScissorRectangle is active (P0-3: Clear must not consult, let alone mutate,
        // viewport/scissor state). vgClear's own internal clip-to-window branch (shContext.c) is
        // never taken for a full-surface request, so it does not touch GL_SCISSOR_TEST either.
        vgClear(0, 0, physW, physH);
    }

    void OpenVgRenderer::Present()
    {
        vgFlush();
        SDL_GL_SwapWindow(window_);
    }

    void OpenVgRenderer::GetViewportSize(int& width, int& height)
    {
        const LogicalViewport viewport = ComputeLogicalViewportEXT();
        width = static_cast<int>(std::lround(viewport.logicalWidth));
        height = static_cast<int>(std::lround(viewport.logicalHeight));
    }

    // The reference "real Letterbox/Overscan/Stretch" GetDefaultViewportRect() override in this
    // codebase (see IGraphicsRenderer's own base doc comment) is OpenGL2Renderer's -- this mirrors
    // it exactly. GraphicsDevice::UpdateViewportFromWindow() applies this PHYSICAL rectangle as the
    // actual glViewport, separately from GetViewportSize()'s LOGICAL result above.
    void OpenVgRenderer::GetDefaultViewportRect(int& x, int& y, int& width, int& height)
    {
        const LogicalViewport viewport = ComputeLogicalViewportEXT();
        x = static_cast<int>(std::lround(viewport.x));
        y = static_cast<int>(std::lround(viewport.y));
        width = static_cast<int>(std::lround(viewport.width));
        height = static_cast<int>(std::lround(viewport.height));
    }

    void OpenVgRenderer::SetVirtualResolution(int width, int height)
    {
        if (width < 0 || height < 0)
            throw std::runtime_error("OpenVG: SetVirtualResolution: width/height must not be negative.");
        virtualWidth_ = width;
        virtualHeight_ = height;
        refreshPresentationDerivedStateEXT();
    }

    void OpenVgRenderer::SetPresentationMode(int mode)
    {
        // P2-1: reject a raw ordinal that does not name a real CnaPresentationMode instead of
        // silently continuing in an undefined presentation state.
        if (mode < static_cast<int>(CnaPresentationMode::Letterbox) ||
            mode > static_cast<int>(CnaPresentationMode::FixedHeightDynamicWidth))
        {
            throw std::runtime_error(
                "OpenVG: SetPresentationMode: " + std::to_string(mode) +
                " is not a valid CnaPresentationMode ordinal.");
        }
        presentationMode_ = static_cast<CnaPresentationMode>(mode);
        refreshPresentationDerivedStateEXT();
    }

    void OpenVgRenderer::SetSwapInterval(int interval)
    {
        if (SDL_GL_SetSwapInterval(interval) != 0 && interval != 0)
        {
            // Some drivers reject adaptive vsync (-1) or a specific positive interval; fall back
            // to the standard single-buffer vsync every desktop GL driver supports rather than
            // silently keeping whatever interval happened to be active before.
            if (SDL_GL_SetSwapInterval(1) != 0)
                SDL_GL_SetSwapInterval(0);
        }
        // Never claim a mode SDL rejected -- record what actually ended up active.
        int actualInterval = interval;
        if (SDL_GL_GetSwapInterval(&actualInterval))
            swapInterval_ = actualInterval;
    }

    bool OpenVgRenderer::TransformWindowToLogical(float windowX, float windowY, float& logX, float& logY) const
    {
        // `windowX`/`windowY` are SDL window-point coordinates (what SDL's own mouse/touch events
        // report -- SDL_GetWindowSize's units, NOT necessarily physical pixels on a high-DPI
        // display), while ComputeLogicalViewportEXT()'s rectangle is physical-pixel-based (P0-1) --
        // scaled by the window's own DPI factor before the presentation-rect math so the two
        // spaces agree.
        int winW = 0, winH = 0;
        SDL_GetWindowSize(window_, &winW, &winH);
        if (winW <= 0 || winH <= 0)
            return false;

        const LogicalViewport viewport = ComputeLogicalViewportEXT();
        if (viewport.width <= 0.0f || viewport.height <= 0.0f)
            return false;

        int physW = 0, physH = 0;
        SDL_GetWindowSizeInPixels(window_, &physW, &physH);
        const float scaleX = physW > 0 ? static_cast<float>(physW) / static_cast<float>(winW) : 1.0f;
        const float scaleY = physH > 0 ? static_cast<float>(physH) / static_cast<float>(winH) : 1.0f;

        const float physX = windowX * scaleX;
        const float physY = windowY * scaleY;
        // Outside the current presentation rectangle -- a Letterbox bar, or an Overscan/Stretch
        // window edge -- has no corresponding logical position.
        if (physX < viewport.x || physX > viewport.x + viewport.width ||
            physY < viewport.y || physY > viewport.y + viewport.height)
            return false;

        logX = (physX - viewport.x) * viewport.logicalWidth / viewport.width;
        logY = (physY - viewport.y) * viewport.logicalHeight / viewport.height;
        return true;
    }

    bool OpenVgRenderer::TransformLogicalToWindow(float logX, float logY, float& windowX, float& windowY) const
    {
        const LogicalViewport viewport = ComputeLogicalViewportEXT();
        if (viewport.logicalWidth <= 0.0f || viewport.logicalHeight <= 0.0f)
            return false;

        const float physX = viewport.x + logX * viewport.width / viewport.logicalWidth;
        const float physY = viewport.y + logY * viewport.height / viewport.logicalHeight;

        int winW = 0, winH = 0;
        SDL_GetWindowSize(window_, &winW, &winH);
        int physW = 0, physH = 0;
        SDL_GetWindowSizeInPixels(window_, &physW, &physH);
        if (winW <= 0 || winH <= 0 || physW <= 0 || physH <= 0)
            return false;
        const float invScaleX = static_cast<float>(winW) / static_cast<float>(physW);
        const float invScaleY = static_cast<float>(winH) / static_cast<float>(physH);

        windowX = physX * invScaleX;
        windowY = physY * invScaleY;
        return true;
    }

    std::unique_ptr<ITextureRenderer> OpenVgRenderer::CreateTexture(const ImageData& data)
    {
        return std::make_unique<OpenVgTextureRenderer>(data);
    }

    std::unique_ptr<ISpriteBatchRenderer> OpenVgRenderer::CreateSpriteBatch()
    {
        return std::make_unique<OpenVgSpriteBatchRenderer>(*this);
    }

    void OpenVgRenderer::SetRenderTargets(const RenderTargetBindingDescriptor* renderTargets, int count)
    {
        // ShivaVG has no EGL-VGImage-surface/FBO equivalent to bind an off-screen VGImage as a draw
        // target -- CreateRenderTarget2D keeps the shared IGraphicsRenderer default (returns
        // nullptr), so GraphicsDevice never has a real IRenderTargetRenderer* for a RenderTarget2D
        // binding to hand back here, and that case still throws.
        //
        // A RenderTargetCube FACE binding is the one narrow exception: CreateRenderTargetCube()
        // (below) honors Unsupported3DGraphicsCallBehavior::WarnAndStub with a real
        // NoOpRenderTargetCubeRenderer, exactly like CreateTexture3D's own null-object path -- if a
        // caller legitimately holds one of those (only possible under WarnAndStub), binding it must
        // not throw either, matching IGraphicsRenderer::SetRenderTargetCubeFace's own default
        // behavior (BindAsRenderTargetFace/UnbindAsRenderTarget on the object handed to it).
        if (count == 1 && renderTargets[0].IsRenderTargetCubeFace() && renderTargets[0].GetRenderTargetCube())
        {
            renderTargets[0].GetRenderTargetCube()->BindAsRenderTargetFace(renderTargets[0].GetCubeFace());
            return;
        }
        if (count == 0)
        {
            // Nothing to unbind -- OpenVgRenderer never has a genuine bound target (RenderTarget2D
            // is always refused below; a bound RenderTargetCube face is a no-op object with nothing
            // to restore either).
            return;
        }
        throw std::runtime_error(
            "OpenVG (ShivaVG) does not support render targets: ShivaVG has no off-screen "
            "VGImage-surface (EGL pbuffer/FBO) equivalent to draw into.");
    }

    void OpenVgRenderer::ReadBackbuffer(int x, int y, int w, int h, uint8_t* pixels)
    {
        EnsureSurfaceSizeEXT();
        // Real readback from the same GL framebuffer ShivaVG rendered into -- genuine pixels, not
        // fabricated. Deliberately PHYSICAL pixel coordinates (matching every other GL-backed CNA
        // renderer's own ReadBackbuffer contract): game/logical Y is top-down; glReadPixels is
        // bottom-up, so each row is read from its flipped physical Y and written to its logical
        // row directly (no separate whole-buffer flip pass needed).
        const int physH = std::max(1, lastPhysH_);
        glPixelStorei(GL_PACK_ALIGNMENT, 1);
        for (int row = 0; row < h; ++row)
        {
            const int glY = physH - (y + row) - 1;
            glReadPixels(x, glY, w, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixels + static_cast<std::size_t>(row) * w * 4);
        }
    }

    void OpenVgRenderer::ApplyBlendState(int colorSrcBlend, int alphaSrcBlend,
                                         int colorDstBlend, int alphaDstBlend,
                                         int colorBlendFunc, int alphaBlendFunc,
                                         const BlendWriteState& writeState)
    {
        lastBlendMode_ = BlendStateToVgBlendMode(
            colorSrcBlend, alphaSrcBlend, colorDstBlend, alphaDstBlend, colorBlendFunc, alphaBlendFunc);
        if (blendEnabled_)
            vgSeti(VG_BLEND_MODE, lastBlendMode_);

        // ColorWriteChannels[0] is honored for real via glColorMask, applied by
        // OpenVgSpriteBatchRenderer around each vgDrawImage call (see GetColorWriteMaskEXT's own
        // doc comment for why that is safe against ShivaVG's own internal glColorMask usage).
        // Slots 1-3 are meaningless (OpenVG has no MRT -- GraphicsCapability::MultipleRenderTargets
        // is false) and are intentionally ignored, same as every other single-target 2D renderer.
        // MultiSampleMask is intentionally ignored too: OpenVG never creates a multisample-capable
        // GL context (GraphicsCapability::MultiSampleAntiAliasing is false), so a coverage mask can
        // never have ANY observable effect here -- unlike ColorWriteChannels, there is no "honor
        // it" option, and unlike DepthBias there is no user-visible corner it could silently get
        // wrong either, so this is inert bookkeeping rather than a dropped feature.
        colorWriteMask_[0] = ColorWriteHasRed(writeState.colorWriteChannels[0]);
        colorWriteMask_[1] = ColorWriteHasGreen(writeState.colorWriteChannels[0]);
        colorWriteMask_[2] = ColorWriteHasBlue(writeState.colorWriteChannels[0]);
        colorWriteMask_[3] = ColorWriteHasAlpha(writeState.colorWriteChannels[0]);
    }

    void OpenVgRenderer::SetScissorRect(int x, int y, int w, int h)
    {
        EnsureSurfaceSizeEXT();
        scissorX_ = x; scissorY_ = y; scissorW_ = w; scissorH_ = h;
        applyScissorRectGL();
        // Enable state is intentionally NOT touched here -- see ApplyRasterizerState (P0-5):
        // SetScissorRect only ever updates the rectangle.
    }

    void OpenVgRenderer::SetViewport(int x, int y, int w, int h, float minDepth, float maxDepth)
    {
        EnsureSurfaceSizeEXT();
        viewportX_ = x; viewportY_ = y; viewportW_ = w; viewportH_ = h;
        viewportMinDepth_ = minDepth; viewportMaxDepth_ = maxDepth;
        viewportSet_ = true;
        applyViewportGL();
        // The GL_PROJECTION ortho is scoped to the current LOGICAL size (ComputeLogicalViewportEXT
        // ()), which does not depend on this custom rectangle's own w/h -- matching
        // OpenGL2Renderer's own model, where a smaller custom viewport scales the SAME logical
        // canvas into that smaller physical rectangle rather than reinterpreting it as a fresh,
        // undistorted local coordinate space. No ortho update is needed here.
    }

    void OpenVgRenderer::ApplyRasterizerState(int cullMode, int fillMode, bool scissorTestEnable,
                                              float depthBias, float slopeScaleDepthBias)
    {
        // CullMode has no meaning for 2D quads: SpriteBatch's own vgDrawImage path never performs
        // back-face culling on ANY CNA renderer regardless of value, so every CullMode (including
        // GraphicsDevice's own device-default RasterizerState.CullCounterClockwise) is accepted as
        // benign -- exactly the "conventional default necessary for ordinary construction" carve-
        // out this project's own audit guidance describes.
        (void)cullMode;
        if (fillMode != kFillModeSolid)
        {
            throw std::runtime_error(
                "OpenVG (ShivaVG) cannot rasterize FillMode.WireFrame: vgDrawImage/vgDrawPath "
                "always rasterize filled geometry; no unfilled-polygon draw path exists.");
        }
        if (depthBias != 0.0f || slopeScaleDepthBias != 0.0f)
        {
            throw std::runtime_error(
                "OpenVG (ShivaVG) has no depth buffer: DepthBias/SlopeScaleDepthBias must be 0.");
        }
        scissorEnabled_ = scissorTestEnable;
        applyScissorEnableGL();
    }

    void OpenVgRenderer::ApplyDepthStencilState(bool depthEnable, bool depthWriteEnable, int /*depthFunc*/,
                                                bool stencilEnable, int /*stencilFunc*/, int /*stencilPass*/,
                                                int /*stencilFail*/, int /*stencilDepthFail*/, int /*stencilMask*/,
                                                int /*stencilWriteMask*/, int referenceStencil,
                                                bool /*twoSidedStencilMode*/, int /*ccwStencilFunc*/,
                                                int /*ccwStencilPass*/, int /*ccwStencilFail*/,
                                                int /*ccwStencilDepthFail*/)
    {
        // DepthStencilState.None (both enables false, the reference stencil left at its default 0)
        // is what SpriteBatch.Begin() applies by default and is silently accepted -- OpenVG
        // genuinely has no depth or stencil buffer on any target (SupportsDepthStencil() is
        // false), so anything that would actually rely on depth or stencil testing is rejected
        // rather than silently no-op'd.
        if (depthEnable || depthWriteEnable || stencilEnable || referenceStencil != 0)
        {
            throw std::runtime_error(
                "OpenVG (ShivaVG) has no depth or stencil buffer: DepthStencilState must be "
                "DepthStencilState.None (DepthBufferEnable=false, StencilEnable=false, "
                "ReferenceStencil=0).");
        }
    }

    void OpenVgRenderer::SetReferenceStencil(int value)
    {
        if (value != 0)
        {
            throw std::runtime_error(
                "OpenVG (ShivaVG) has no stencil buffer: ReferenceStencil must be 0.");
        }
    }

    void OpenVgRenderer::Ensure3DSupported(const char* operation) const
    {
        // Earliest possible guard (P0-6): runs before GraphicsDevice::DrawPrimitives/
        // DrawIndexedPrimitives/DrawInstancedPrimitives/DrawUserPrimitives/
        // DrawUserIndexedPrimitives touch currentVertexBuffer_/currentIndexBuffer_, validate
        // stream ranges, or apply sampler state -- so an unsupported 3D call is rejected (Throw
        // policy) or safely no-op'd (WarnAndStub) before any of that runs, not merely once native
        // submission is reached.
        HandleUnsupported3DCall("OpenVG (ShivaVG)", operation ? operation : "<unknown 3D operation>");
    }

    // ---- 3D: OpenVG is a 2D vector-graphics API with no 3D pipeline at all. ----
    void OpenVgRenderer::ClearColorAndDepth(float, float, float, float, float) { HandleUnsupported3DCall("OpenVG (ShivaVG)", "ClearColorAndDepth"); }
    void OpenVgRenderer::ClearDepth(float) { HandleUnsupported3DCall("OpenVG (ShivaVG)", "ClearDepth"); }
    void OpenVgRenderer::ClearStencil(int) { HandleUnsupported3DCall("OpenVG (ShivaVG)", "ClearStencil"); }
    void OpenVgRenderer::ClearDepthAndStencil(float, int) { HandleUnsupported3DCall("OpenVG (ShivaVG)", "ClearDepthAndStencil"); }
    void OpenVgRenderer::ClearColorAndStencil(float, float, float, float, int) { HandleUnsupported3DCall("OpenVG (ShivaVG)", "ClearColorAndStencil"); }
    void OpenVgRenderer::ClearColorDepthAndStencil(float, float, float, float, float, int) { HandleUnsupported3DCall("OpenVG (ShivaVG)", "ClearColorDepthAndStencil"); }
    void OpenVgRenderer::SetDepthTestEnabled(bool) { HandleUnsupported3DCall("OpenVG (ShivaVG)", "SetDepthTestEnabled"); }

    void OpenVgRenderer::SetBlendEnabled(bool enabled)
    {
        blendEnabled_ = enabled;
        vgSeti(VG_BLEND_MODE, enabled ? lastBlendMode_ : VG_BLEND_SRC);
    }

    void OpenVgRenderer::SetDepthWriteEnabled(bool) { HandleUnsupported3DCall("OpenVG (ShivaVG)", "SetDepthWriteEnabled"); }

    std::unique_ptr<IVertexBufferRenderer> OpenVgRenderer::CreateVertexBuffer(int vertexCapacity)
    {
        HandleUnsupported3DCall("OpenVG (ShivaVG)", "CreateVertexBuffer");
        return std::make_unique<NoOpVertexBufferRenderer>(vertexCapacity);
    }

    std::unique_ptr<IIndexBufferRenderer> OpenVgRenderer::CreateIndexBuffer16(int indexCapacity)
    {
        HandleUnsupported3DCall("OpenVG (ShivaVG)", "CreateIndexBuffer16");
        return std::make_unique<NoOpIndexBufferRenderer>(indexCapacity);
    }

    std::unique_ptr<IOcclusionQueryRenderer> OpenVgRenderer::CreateOcclusionQuery()
    {
        if (!ShouldStubUnsupported3DResource())
            return nullptr;
        HandleUnsupported3DCall("OpenVG (ShivaVG)", "CreateOcclusionQuery");
        return std::make_unique<NoOpOcclusionQueryRenderer>();
    }

    std::unique_ptr<ITexture3DRenderer> OpenVgRenderer::CreateTexture3D(
        int width, int height, int depth, bool, int)
    {
        if (!ShouldStubUnsupported3DResource())
            return nullptr;
        HandleUnsupported3DCall("OpenVG (ShivaVG)", "CreateTexture3D");
        return std::make_unique<NoOpTexture3DRenderer>(width, height, depth);
    }

    std::unique_ptr<ITextureCubeRenderer> OpenVgRenderer::CreateTextureCube(int size, bool, int)
    {
        if (!ShouldStubUnsupported3DResource())
            return nullptr;
        HandleUnsupported3DCall("OpenVG (ShivaVG)", "CreateTextureCube");
        return std::make_unique<NoOpTextureCubeRenderer>(size);
    }

    std::unique_ptr<IRenderTargetCubeRenderer> OpenVgRenderer::CreateRenderTargetCube(
        int size, int /*depthFormat*/, bool /*preserveContents*/, bool /*mipMap*/,
        int /*multiSampleCount*/)
    {
        if (!ShouldStubUnsupported3DResource())
            return nullptr;
        HandleUnsupported3DCall("OpenVG (ShivaVG)", "CreateRenderTargetCube");
        return std::make_unique<NoOpRenderTargetCubeRenderer>(size);
    }

    void OpenVgRenderer::DrawColoredPrimitives(const IVertexBufferRenderer&, const Matrix&, const Matrix&,
                                               const Matrix&, PrimitiveType, int)
    { HandleUnsupported3DCall("OpenVG (ShivaVG)", "DrawColoredPrimitives"); }

    void OpenVgRenderer::DrawIndexedColoredPrimitives(const IVertexBufferRenderer&, const IIndexBufferRenderer&,
                                                       const Matrix&, const Matrix&, const Matrix&, PrimitiveType, int)
    { HandleUnsupported3DCall("OpenVG (ShivaVG)", "DrawIndexedColoredPrimitives"); }
}

namespace CNA::Internal::Renderers
{
    // plan_runtimerenderer.md design decision 4: declared in this family's own
    // namespace so several renderer archives can link into one binary, then defined
    // below with a qualified name -- the body keeps its place unchanged.
    namespace OpenVg { std::unique_ptr<IGraphicsRenderer> CreateGraphicsRenderer(const GraphicsRendererCreateArgs& args); }

    std::unique_ptr<IGraphicsRenderer> OpenVg::CreateGraphicsRenderer(const GraphicsRendererCreateArgs& args)
    {
        return std::make_unique<OpenVg::OpenVgRenderer>(
            args.window, args.virtualWidth, args.virtualHeight, args.presentationMode,
            args.swapInterval);
    }
}
