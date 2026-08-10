// SPDX-License-Identifier: MS-PL
#include "CNA/Internal/Renderers/OpenVg/OpenVgRenderer.hpp"
#include "CNA/Internal/Renderers/OpenVg/OpenVgTextureRenderer.hpp"
#include "CNA/Internal/Renderers/OpenVg/OpenVgSpriteBatchRenderer.hpp"
#include "CNA/Internal/Renderers/Common/NoOp3DResources.hpp"

#include "openvg.h"

#include <SDL3/SDL.h>
#include <GL/gl.h>

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
    }

    // ShivaVG's own updateBlendingStateGL (src/shPipeline.c) implements VG_BLEND_SRC (opaque,
    // blending disabled) and VG_BLEND_SRC_OVER (glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA),
    // i.e. straight/non-premultiplied-alpha-over) for real. VG_BLEND_ADDITIVE/MULTIPLY/SCREEN/
    // DARKEN/LIGHTEN are declared in openvg.h's VGBlendMode enum but have NO case in that switch --
    // they silently fall through to the `default: VG_BLEND_SRC_OVER` arm, i.e. asking ShivaVG for
    // additive blending silently renders ordinary alpha blending instead. Accepting that here would
    // be exactly the kind of unfaithful "capability lie" this project's own GraphicsCapability docs
    // warn against, so BlendState.Additive is rejected rather than silently mis-rendered.
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

    namespace
    {
        void ApplyDeviceFlip(int physicalHeight)
        {
            // OpenVG's image space is Y-up (row 0 of a VGImage is its BOTTOM row); XNA/SpriteBatch
            // coordinates are Y-down, origin top-left. This single outer transform -- applied once,
            // before any per-sprite translate/rotate/scale -- reconciles the two: everything a
            // caller composes afterward (SetTransformMatrix, per-Draw translate/rotate/scale) can
            // keep assuming ordinary Y-down screen coordinates.
            vgLoadIdentity();
            vgTranslate(0.0f, static_cast<VGfloat>(physicalHeight));
            vgScale(1.0f, -1.0f);
        }
    }

    OpenVgRenderer::OpenVgRenderer(SDL_Window* window, int virtualWidth, int virtualHeight,
                                   CnaPresentationMode mode)
        : window_(window)
        , virtualWidth_(virtualWidth)
        , virtualHeight_(virtualHeight)
        , presentationMode_(mode)
    {
        if (!window_) throw std::runtime_error("OpenVgRenderer initialized with null window.");

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
        {
            SDL_GL_DestroyContext(glContext_);
            glContext_ = nullptr;
            throw std::runtime_error("OPENVG: vgCreateContextSH failed.");
        }

        IGraphicsRenderer::RegisterForWindow(window_, this);
    }

    OpenVgRenderer::~OpenVgRenderer()
    {
        IGraphicsRenderer::UnregisterForWindow(window_);
        vgDestroyContextSH();
        if (glContext_)
        {
            SDL_GL_MakeCurrent(window_, nullptr);
            SDL_GL_DestroyContext(glContext_);
        }
    }

    void OpenVgRenderer::applyDefaultViewportAndSurfaceSize()
    {
        int physW = 0, physH = 0;
        SDL_GetWindowSizeInPixels(window_, &physW, &physH);
        if (physW <= 0) physW = 1;
        if (physH <= 0) physH = 1;
        vgResizeSurfaceSH(physW, physH);
        glViewport(0, 0, physW, physH);
    }

    void OpenVgRenderer::Clear(float r, float g, float b, float a)
    {
        applyDefaultViewportAndSurfaceSize();
        int physW = 0, physH = 0;
        SDL_GetWindowSizeInPixels(window_, &physW, &physH);
        const VGfloat clearColor[4] = { r, g, b, a };
        vgSetfv(VG_CLEAR_COLOR, 4, clearColor);
        vgClear(0, 0, physW > 0 ? physW : 1, physH > 0 ? physH : 1);
    }

    void OpenVgRenderer::Present()
    {
        vgFlush();
        SDL_GL_SwapWindow(window_);
    }

    void OpenVgRenderer::getLogicalSize(int& width, int& height) const
    {
        // Same FixedHeightDynamicWidth math CanvasRenderer::getLogicalSize/EasyGLRenderer::
        // getLogicalSize use.
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

    void OpenVgRenderer::GetViewportSize(int& width, int& height)
    {
        getLogicalSize(width, height);
    }

    void OpenVgRenderer::SetVirtualResolution(int width, int height)
    {
        virtualWidth_ = width;
        virtualHeight_ = height;
    }

    void OpenVgRenderer::SetPresentationMode(int mode)
    {
        presentationMode_ = static_cast<CnaPresentationMode>(mode);
    }

    bool OpenVgRenderer::TransformWindowToLogical(float windowX, float windowY, float& logX, float& logY) const
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

    bool OpenVgRenderer::TransformLogicalToWindow(float logX, float logY, float& windowX, float& windowY) const
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
        // Real readback from the same GL framebuffer ShivaVG rendered into -- genuine pixels, not
        // fabricated. Game/logical Y is top-down; glReadPixels is bottom-up, so each row is read
        // from its flipped physical Y and written to its logical row directly (no separate
        // whole-buffer flip pass needed).
        int physH = 0;
        SDL_GetWindowSizeInPixels(window_, nullptr, &physH);
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
                                         const BlendWriteState& /*writeState*/)
    {
        // REMED-GFX-077: OpenVG exposes no per-channel color-write mask and no coverage sample
        // mask -- BlendWriteState is inexpressible here (documented gap, not a silent drop), same
        // shape as Canvas's own ApplyBlendState.
        lastBlendMode_ = BlendStateToVgBlendMode(
            colorSrcBlend, alphaSrcBlend, colorDstBlend, alphaDstBlend, colorBlendFunc, alphaBlendFunc);
        if (blendEnabled_)
            vgSeti(VG_BLEND_MODE, lastBlendMode_);
    }

    void OpenVgRenderer::SetScissorRect(int x, int y, int w, int h)
    {
        int physH = 0;
        SDL_GetWindowSizeInPixels(window_, nullptr, &physH);
        const VGint rect[4] = { x, physH - y - h, w, h }; // OpenVG scissor rects are Y-up too.
        vgSeti(VG_SCISSORING, VG_TRUE);
        vgSetiv(VG_SCISSOR_RECTS, 4, rect);
    }

    void OpenVgRenderer::SetViewport(int x, int y, int w, int h, float /*minDepth*/, float /*maxDepth*/)
    {
        // OpenVG has no separate viewport concept from its GL-backed surface -- the physical
        // GL viewport is what actually clips/positions rendering.
        int physH = 0;
        SDL_GetWindowSizeInPixels(window_, nullptr, &physH);
        glViewport(x, physH - y - h, w, h);
    }

    int OpenVgRenderer::GetPhysicalHeightEXT() const
    {
        int physH = 0;
        SDL_GetWindowSizeInPixels(window_, nullptr, &physH);
        return physH > 0 ? physH : 1;
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
    std::unique_ptr<IGraphicsRenderer> CreateGraphicsRenderer(const GraphicsRendererCreateArgs& args)
    {
        return std::make_unique<OpenVg::OpenVgRenderer>(
            args.window, args.virtualWidth, args.virtualHeight, args.presentationMode);
    }
}
