// SPDX-License-Identifier: MS-PL
#include "CNA/Internal/Renderers/NanoVg/NanoVgRenderer.hpp"
#include "CNA/Internal/Renderers/NanoVg/NanoVgTextureRenderer.hpp"
#include "CNA/Internal/Renderers/NanoVg/NanoVgSpriteBatchRenderer.hpp"
#include "CNA/Internal/Renderers/NanoVg/NanoVgGlLoader.hpp"
#include "CNA/Internal/Renderers/Common/NoOp3DResources.hpp"

// Portable GL header selection -- mirrors OpenVgRenderer.cpp's own (same platform branch shape).
// Must precede nanovg_gl.h: its declarations (even outside NANOVG_GL2_IMPLEMENTATION) use GLuint.
#if defined(__APPLE__)
#include <OpenGL/gl.h>
#elif defined(_WIN32)
#include <windows.h>
#include <GL/gl.h>
#else
#include <GL/gl.h>
#endif

#include "nanovg.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <string>

namespace CNA::Internal::Renderers::NanoVg
{
    namespace
    {
        // Raw XNA Blend/BlendFunction ordinals -- same table OpenVgRenderer.cpp's own
        // BlendStateToVgBlendMode uses: One=0, Zero=1, SourceAlpha=4, InverseSourceAlpha=5;
        // Add=0.
        constexpr int kBlendOne = 0, kBlendZero = 1, kBlendSourceAlpha = 4, kBlendInvSourceAlpha = 5;
        constexpr int kBlendFuncAdd = 0;

        constexpr int kFillModeSolid = 0;

        CNA::Platform::GlContextDescription RequestedContext()
        {
            // NanoVG's GL2 backend compiles GLSL 1.10 shaders and needs real buffer/shader-object
            // entry points (GL 1.5/2.0) -- a GL 2.1 compatibility context, the same shape
            // OPENVG/OPENGL2 already request.
            CNA::Platform::GlContextDescription description;
            description.majorVersion = 2;
            description.minorVersion = 1;
            description.profile = CNA::Platform::GlProfile::Compatibility;
            description.depthBits = 0;
            description.stencilBits = 8; // NanoVG's own NVG_STENCIL_STROKES path needs a real stencil plane.
            description.multisampleBuffers = 0;
            description.multisampleSamples = 0;
            description.doubleBuffer = true;
            return description;
        }
    }

    // See nanovg.c's own nvg__compositeOperationState for the real (srcRGB,dstRGB) factor pairs
    // this maps to. NVG_LIGHTER = (GL_ONE, GL_ONE), a real, working Additive blend -- unlike
    // OpenVG (ShivaVG), which declares but never implements additive blending.
    int BlendStateToNvgCompositeOperation(int colorSrcBlend, int alphaSrcBlend,
                                          int colorDstBlend, int alphaDstBlend,
                                          int colorBlendFunc, int alphaBlendFunc)
    {
        const bool isAdd = colorBlendFunc == kBlendFuncAdd && alphaBlendFunc == kBlendFuncAdd;
        const bool symmetric = colorSrcBlend == alphaSrcBlend && colorDstBlend == alphaDstBlend;

        if (isAdd && symmetric && colorSrcBlend == kBlendOne && colorDstBlend == kBlendZero)
            return NVG_COPY; // Opaque
        if (isAdd && symmetric && colorSrcBlend == kBlendSourceAlpha && colorDstBlend == kBlendInvSourceAlpha)
            return NVG_SOURCE_OVER; // NonPremultiplied
        if (isAdd && symmetric && colorSrcBlend == kBlendOne && colorDstBlend == kBlendInvSourceAlpha)
            return NVG_SOURCE_OVER; // AlphaBlend (straight-source caveat, same as OpenVgRenderer's own)
        if (isAdd && symmetric && colorSrcBlend == kBlendSourceAlpha && colorDstBlend == kBlendOne)
            return NVG_LIGHTER; // Additive (real XNA BlendState.Additive is SourceAlpha/One, not One/One).

        throw std::runtime_error(
            "NANOVG only supports the Opaque/AlphaBlend/NonPremultiplied/Additive BlendState "
            "presets: there is no generic blend-factor/equation model to express an arbitrary "
            "custom BlendState.");
    }

    NanoVgRenderer::NanoVgRenderer(const GraphicsRendererCreateArgs& args)
        : surface_(args.surface)
        , virtualWidth_(args.virtualWidth)
        , virtualHeight_(args.virtualHeight)
        , presentationMode_(args.presentationMode)
    {
        platformContext_ = std::make_unique<PlatformGlContextOwner>(
            RequirePlatformGlContext(args.glContext, "NANOVG"),
            RequirePlatformGlWindow(args.surface, "NANOVG"), RequestedContext());

        try
        {
            LoadNanoVgGlFunctions();
            nvg_ = CreateNanoVgGL2Context();
            if (!nvg_)
                throw std::runtime_error("NANOVG: CreateNanoVgGL2Context (nvgCreateGL2) failed.");

            int physW = 0, physH = 0;
            surface_.GetDrawableSize(physW, physH);
            if (physW <= 0) physW = 1;
            if (physH <= 0) physH = 1;
            lastPhysW_ = physW;
            lastPhysH_ = physH;
            refreshPresentationDerivedStateEXT();
            SetSwapInterval(args.swapInterval);

            IGraphicsRenderer::RegisterForWindow(surface_.GetWindowId(), this);
        }
        catch (...)
        {
            if (nvg_) { DeleteNanoVgGL2Context(nvg_); nvg_ = nullptr; }
            platformContext_.reset();
            throw;
        }
    }

    NanoVgRenderer::~NanoVgRenderer()
    {
        IGraphicsRenderer::UnregisterForWindow(surface_.GetWindowId());
        if (platformContext_)
            platformContext_->MakeCurrent();
        if (nvg_) { DeleteNanoVgGL2Context(nvg_); nvg_ = nullptr; }
        platformContext_.reset();
    }

    // Direct port of OpenVgRenderer::ComputeLogicalViewportEXT -- see that class's own comment.
    NanoVgRenderer::LogicalViewport NanoVgRenderer::ComputeLogicalViewportEXT() const
    {
        int physW = 0, physH = 0;
        surface_.GetDrawableSize(physW, physH);

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

        const float sx = static_cast<float>(physW) / logicalWidth;
        const float sy = static_cast<float>(physH) / logicalHeight;
        const float scale = presentationMode_ == CnaPresentationMode::Overscan ? std::max(sx, sy) : std::min(sx, sy);
        viewport.width = logicalWidth * scale;
        viewport.height = logicalHeight * scale;
        viewport.x = (static_cast<float>(physW) - viewport.width) * 0.5f;
        viewport.y = (static_cast<float>(physH) - viewport.height) * 0.5f;
        return viewport;
    }

    int NanoVgRenderer::GetPhysicalHeightEXT() const
    {
        int physH = 0;
        int ignoredWidth = 0;
        surface_.GetDrawableSize(ignoredWidth, physH);
        return physH > 0 ? physH : 1;
    }

    void NanoVgRenderer::applyViewportGL()
    {
        const int physH = GetPhysicalHeightEXT();
        glViewport(viewportX_, physH - viewportY_ - viewportH_, viewportW_, viewportH_);
        glDepthRange(static_cast<double>(viewportMinDepth_), static_cast<double>(viewportMaxDepth_));
    }

    void NanoVgRenderer::refreshPresentationDerivedStateEXT()
    {
        if (!viewportSet_)
        {
            int x = 0, y = 0, w = 0, h = 0;
            GetDefaultViewportRect(x, y, w, h);
            viewportX_ = x; viewportY_ = y; viewportW_ = w; viewportH_ = h;
        }
        applyViewportGL();
    }

    void NanoVgRenderer::EnsureSurfaceSizeEXT()
    {
        MakeContextCurrentEXT();
        int physW = 0, physH = 0;
        surface_.GetDrawableSize(physW, physH);
        if (physW <= 0) physW = 1;
        if (physH <= 0) physH = 1;
        if (physW == lastPhysW_ && physH == lastPhysH_)
            return;

        lastPhysW_ = physW;
        lastPhysH_ = physH;
        refreshPresentationDerivedStateEXT();
    }

    void NanoVgRenderer::Clear(float r, float g, float b, float a)
    {
        EnsureSurfaceSizeEXT();
        glClearColor(r, g, b, a);
        glClear(GL_COLOR_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
    }

    void NanoVgRenderer::Present()
    {
        MakeContextCurrentEXT();
        platformContext_->SwapBuffers();
    }

    void NanoVgRenderer::GetViewportSize(int& width, int& height)
    {
        const LogicalViewport viewport = ComputeLogicalViewportEXT();
        width = static_cast<int>(std::lround(viewport.logicalWidth));
        height = static_cast<int>(std::lround(viewport.logicalHeight));
    }

    void NanoVgRenderer::OnSurfaceChanged(const RendererSurfaceInfo& surface)
    {
        surface_.Update(surface);
        EnsureSurfaceSizeEXT();
    }

    void NanoVgRenderer::GetDefaultViewportRect(int& x, int& y, int& width, int& height)
    {
        const LogicalViewport viewport = ComputeLogicalViewportEXT();
        x = static_cast<int>(std::lround(viewport.x));
        y = static_cast<int>(std::lround(viewport.y));
        width = static_cast<int>(std::lround(viewport.width));
        height = static_cast<int>(std::lround(viewport.height));
    }

    void NanoVgRenderer::SetVirtualResolution(int width, int height)
    {
        if (width < 0 || height < 0)
            throw std::runtime_error("NANOVG: SetVirtualResolution: width/height must not be negative.");
        virtualWidth_ = width;
        virtualHeight_ = height;
        MakeContextCurrentEXT();
        refreshPresentationDerivedStateEXT();
    }

    void NanoVgRenderer::SetPresentationMode(int mode)
    {
        if (mode < static_cast<int>(CnaPresentationMode::Letterbox) ||
            mode > static_cast<int>(CnaPresentationMode::FixedHeightDynamicWidth))
        {
            throw std::runtime_error(
                "NANOVG: SetPresentationMode: " + std::to_string(mode) +
                " is not a valid CnaPresentationMode ordinal.");
        }
        presentationMode_ = static_cast<CnaPresentationMode>(mode);
        MakeContextCurrentEXT();
        refreshPresentationDerivedStateEXT();
    }

    void NanoVgRenderer::SetSwapInterval(int interval)
    {
        MakeContextCurrentEXT();
        if (platformContext_->SetSwapInterval(interval))
        {
            swapInterval_ = interval;
            return;
        }
        if (interval != 0 && platformContext_->SetSwapInterval(1))
            swapInterval_ = 1;
        else if (platformContext_->SetSwapInterval(0))
            swapInterval_ = 0;
    }

    bool NanoVgRenderer::TransformWindowToLogical(float windowX, float windowY, float& logX, float& logY) const
    {
        const LogicalViewport viewport = ComputeLogicalViewportEXT();
        if (viewport.width <= 0.0f || viewport.height <= 0.0f)
            return false;

        const float physX = surface_.WindowToDrawable(windowX);
        const float physY = surface_.WindowToDrawable(windowY);
        if (physX < viewport.x || physX > viewport.x + viewport.width ||
            physY < viewport.y || physY > viewport.y + viewport.height)
            return false;

        logX = (physX - viewport.x) * viewport.logicalWidth / viewport.width;
        logY = (physY - viewport.y) * viewport.logicalHeight / viewport.height;
        return true;
    }

    bool NanoVgRenderer::TransformLogicalToWindow(float logX, float logY, float& windowX, float& windowY) const
    {
        const LogicalViewport viewport = ComputeLogicalViewportEXT();
        if (viewport.logicalWidth <= 0.0f || viewport.logicalHeight <= 0.0f)
            return false;

        const float physX = viewport.x + logX * viewport.width / viewport.logicalWidth;
        const float physY = viewport.y + logY * viewport.height / viewport.logicalHeight;

        windowX = surface_.DrawableToWindow(physX);
        windowY = surface_.DrawableToWindow(physY);
        return true;
    }

    std::unique_ptr<ITextureRenderer> NanoVgRenderer::CreateTexture(const ImageData& data)
    {
        MakeContextCurrentEXT();
        return std::make_unique<NanoVgTextureRenderer>(*this, data);
    }

    std::unique_ptr<ISpriteBatchRenderer> NanoVgRenderer::CreateSpriteBatch()
    {
        return std::make_unique<NanoVgSpriteBatchRenderer>(*this);
    }

    void NanoVgRenderer::SetRenderTargets(const RenderTargetBindingDescriptor* renderTargets, int count)
    {
        if (count == 1 && renderTargets[0].IsRenderTargetCubeFace() && renderTargets[0].GetRenderTargetCube())
        {
            renderTargets[0].GetRenderTargetCube()->BindAsRenderTargetFace(renderTargets[0].GetCubeFace());
            return;
        }
        if (count == 0)
            return;
        throw std::runtime_error(
            "NANOVG does not support render targets: NanoVG's own off-screen-framebuffer helper "
            "(nanovg_gl_utils.h's NVGLUframebuffer) is deliberately out of this renderer's scope.");
    }

    void NanoVgRenderer::ReadBackbuffer(int x, int y, int w, int h, uint8_t* pixels)
    {
        EnsureSurfaceSizeEXT();
        const int physH = std::max(1, lastPhysH_);
        glPixelStorei(GL_PACK_ALIGNMENT, 1);
        for (int row = 0; row < h; ++row)
        {
            const int glY = physH - (y + row) - 1;
            glReadPixels(x, glY, w, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixels + static_cast<std::size_t>(row) * w * 4);
        }
    }

    void NanoVgRenderer::ApplyBlendState(int colorSrcBlend, int alphaSrcBlend,
                                         int colorDstBlend, int alphaDstBlend,
                                         int colorBlendFunc, int alphaBlendFunc,
                                         const BlendWriteState& writeState)
    {
        lastCompositeOp_ = BlendStateToNvgCompositeOperation(
            colorSrcBlend, alphaSrcBlend, colorDstBlend, alphaDstBlend, colorBlendFunc, alphaBlendFunc);

        // BlendWriteState.ColorWriteChannels cannot be honored on this renderer: NanoVG's own
        // stencil-then-color two-pass fill implementation unconditionally calls
        // glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE) before its color pass (nanovg_gl.h's
        // glnvg__fill/glnvg__convexFill), overwriting any externally-set mask on every single
        // draw -- verified empirically (a wrapped glColorMask around the whole SpriteBatch batch
        // was silently undone). Rejecting a non-default mask is honest; silently ignoring it
        // would be exactly the "capability lie" this project's own docs warn against.
        const int cwc = writeState.colorWriteChannels[0];
        if (cwc != 15) // 15 = R|G|B|A, the default
        {
            throw std::runtime_error(
                "NANOVG cannot honor BlendState.ColorWriteChannels: NanoVG's own stencil-based "
                "fill implementation unconditionally resets glColorMask to all-channels-enabled "
                "before every color pass, so an externally-set write mask cannot survive a draw.");
        }
    }

    void NanoVgRenderer::SetScissorRect(int x, int y, int w, int h)
    {
        // No presentation-mode remapping needed here (unlike OpenVgRenderer::SetScissorRect) --
        // see this class's own doc comment: nvgScissor operates in the same logical space nvgRect
        // does, and NanoVG maps that internally the same way it maps path geometry.
        scissorX_ = x; scissorY_ = y; scissorW_ = w; scissorH_ = h;
    }

    void NanoVgRenderer::SetViewport(int x, int y, int w, int h, float minDepth, float maxDepth)
    {
        EnsureSurfaceSizeEXT();
        viewportX_ = x; viewportY_ = y; viewportW_ = w; viewportH_ = h;
        viewportMinDepth_ = minDepth; viewportMaxDepth_ = maxDepth;
        viewportSet_ = true;
        applyViewportGL();
    }

    void NanoVgRenderer::ApplyRasterizerState(int cullMode, int fillMode, bool scissorTestEnable,
                                              float depthBias, float slopeScaleDepthBias)
    {
        (void)cullMode;
        if (fillMode != kFillModeSolid)
        {
            throw std::runtime_error(
                "NANOVG cannot rasterize FillMode.WireFrame: nvgFill/nvgStroke always rasterize "
                "filled/stroked geometry; no unfilled-polygon draw path exists.");
        }
        if (depthBias != 0.0f || slopeScaleDepthBias != 0.0f)
        {
            throw std::runtime_error(
                "NANOVG has no depth buffer: DepthBias/SlopeScaleDepthBias must be 0.");
        }
        scissorEnabled_ = scissorTestEnable;
    }

    void NanoVgRenderer::ApplyDepthStencilState(bool depthEnable, bool depthWriteEnable, int /*depthFunc*/,
                                                bool stencilEnable, int /*stencilFunc*/, int /*stencilPass*/,
                                                int /*stencilFail*/, int /*stencilDepthFail*/, int /*stencilMask*/,
                                                int /*stencilWriteMask*/, int referenceStencil,
                                                bool /*twoSidedStencilMode*/, int /*ccwStencilFunc*/,
                                                int /*ccwStencilPass*/, int /*ccwStencilFail*/,
                                                int /*ccwStencilDepthFail*/)
    {
        if (depthEnable || depthWriteEnable || stencilEnable || referenceStencil != 0)
        {
            throw std::runtime_error(
                "NANOVG has no caller-addressable depth or stencil buffer: DepthStencilState must "
                "be DepthStencilState.None (DepthBufferEnable=false, StencilEnable=false, "
                "ReferenceStencil=0).");
        }
    }

    void NanoVgRenderer::SetReferenceStencil(int value)
    {
        if (value != 0)
            throw std::runtime_error("NANOVG has no stencil buffer: ReferenceStencil must be 0.");
    }

    void NanoVgRenderer::Ensure3DSupported(const char* operation) const
    {
        HandleUnsupported3DCall("NANOVG", operation ? operation : "<unknown 3D operation>");
    }

    bool NanoVgRenderer::SupportsCapability(CNA::GraphicsCapability capability) const
    {
        // NanoVG is 2D-only (ThreeD, DepthStencilBuffer, MultipleRenderTargets, StencilBuffer,
        // Instancing, MultiStreamVertexInput all false), never creates a multisample-capable GL
        // context (MultiSampleAntiAliasing false), never sets anisotropic filtering
        // (AnisotropicFiltering false), has no unfilled-polygon draw path (WireFrame false), no
        // real GPU query object (OcclusionQuery false), no programmable custom-shader stage
        // reachable from SpriteBatch (CustomEffects/CompiledEffects false), and no volume-texture
        // storage (Texture3D false). AdditiveBlending is the one genuine capability edge over
        // OPENVG -- see BlendStateToNvgCompositeOperation's own comment.
        return capability == CNA::GraphicsCapability::AdditiveBlending;
    }

    // ---- 3D: NanoVG is a 2D vector-graphics API with no 3D pipeline at all. ----
    void NanoVgRenderer::ClearColorAndDepth(float, float, float, float, float) { HandleUnsupported3DCall("NANOVG", "ClearColorAndDepth"); }
    void NanoVgRenderer::ClearDepth(float) { HandleUnsupported3DCall("NANOVG", "ClearDepth"); }
    void NanoVgRenderer::ClearStencil(int) { HandleUnsupported3DCall("NANOVG", "ClearStencil"); }
    void NanoVgRenderer::ClearDepthAndStencil(float, int) { HandleUnsupported3DCall("NANOVG", "ClearDepthAndStencil"); }
    void NanoVgRenderer::ClearColorAndStencil(float, float, float, float, int) { HandleUnsupported3DCall("NANOVG", "ClearColorAndStencil"); }
    void NanoVgRenderer::ClearColorDepthAndStencil(float, float, float, float, float, int) { HandleUnsupported3DCall("NANOVG", "ClearColorDepthAndStencil"); }
    void NanoVgRenderer::SetDepthTestEnabled(bool) { HandleUnsupported3DCall("NANOVG", "SetDepthTestEnabled"); }

    void NanoVgRenderer::SetBlendEnabled(bool enabled)
    {
        blendEnabled_ = enabled;
    }

    void NanoVgRenderer::SetDepthWriteEnabled(bool) { HandleUnsupported3DCall("NANOVG", "SetDepthWriteEnabled"); }

    std::unique_ptr<IVertexBufferRenderer> NanoVgRenderer::CreateVertexBuffer(int vertexCapacity)
    {
        HandleUnsupported3DCall("NANOVG", "CreateVertexBuffer");
        return std::make_unique<NoOpVertexBufferRenderer>(vertexCapacity);
    }

    std::unique_ptr<IIndexBufferRenderer> NanoVgRenderer::CreateIndexBuffer16(int indexCapacity)
    {
        HandleUnsupported3DCall("NANOVG", "CreateIndexBuffer16");
        return std::make_unique<NoOpIndexBufferRenderer>(indexCapacity);
    }

    std::unique_ptr<IOcclusionQueryRenderer> NanoVgRenderer::CreateOcclusionQuery()
    {
        if (!ShouldStubUnsupported3DResource())
            return nullptr;
        HandleUnsupported3DCall("NANOVG", "CreateOcclusionQuery");
        return std::make_unique<NoOpOcclusionQueryRenderer>();
    }

    std::unique_ptr<ITexture3DRenderer> NanoVgRenderer::CreateTexture3D(
        int width, int height, int depth, bool, int)
    {
        if (!ShouldStubUnsupported3DResource())
            return nullptr;
        HandleUnsupported3DCall("NANOVG", "CreateTexture3D");
        return std::make_unique<NoOpTexture3DRenderer>(width, height, depth);
    }

    std::unique_ptr<ITextureCubeRenderer> NanoVgRenderer::CreateTextureCube(int size, bool, int)
    {
        if (!ShouldStubUnsupported3DResource())
            return nullptr;
        HandleUnsupported3DCall("NANOVG", "CreateTextureCube");
        return std::make_unique<NoOpTextureCubeRenderer>(size);
    }

    std::unique_ptr<IRenderTargetCubeRenderer> NanoVgRenderer::CreateRenderTargetCube(
        int size, int /*depthFormat*/, bool /*preserveContents*/, bool /*mipMap*/,
        int /*multiSampleCount*/)
    {
        if (!ShouldStubUnsupported3DResource())
            return nullptr;
        HandleUnsupported3DCall("NANOVG", "CreateRenderTargetCube");
        return std::make_unique<NoOpRenderTargetCubeRenderer>(size);
    }

    void NanoVgRenderer::DrawColoredPrimitives(const IVertexBufferRenderer&, const Matrix&, const Matrix&,
                                               const Matrix&, PrimitiveType, int)
    { HandleUnsupported3DCall("NANOVG", "DrawColoredPrimitives"); }

    void NanoVgRenderer::DrawIndexedColoredPrimitives(const IVertexBufferRenderer&, const IIndexBufferRenderer&,
                                                       const Matrix&, const Matrix&, const Matrix&, PrimitiveType, int)
    { HandleUnsupported3DCall("NANOVG", "DrawIndexedColoredPrimitives"); }
}

namespace CNA::Internal::Renderers
{
    namespace NanoVg { std::unique_ptr<IGraphicsRenderer> CreateGraphicsRenderer(const GraphicsRendererCreateArgs& args); }

    std::unique_ptr<IGraphicsRenderer> NanoVg::CreateGraphicsRenderer(const GraphicsRendererCreateArgs& args)
    {
        return std::make_unique<NanoVg::NanoVgRenderer>(args);
    }
}
