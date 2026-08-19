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
        // Raw XNA BlendFunction ordinal for Add, the only equation NanoVG can express -- its GL2
        // backend never calls glBlendEquation, so the pipeline stays at GL_FUNC_ADD.
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

    namespace
    {
        const char* BlendName(int blendOrdinal)
        {
            switch (blendOrdinal)
            {
            case 0:  return "One";
            case 1:  return "Zero";
            case 2:  return "SourceColor";
            case 3:  return "InverseSourceColor";
            case 4:  return "SourceAlpha";
            case 5:  return "InverseSourceAlpha";
            case 6:  return "DestinationColor";
            case 7:  return "InverseDestinationColor";
            case 8:  return "DestinationAlpha";
            case 9:  return "InverseDestinationAlpha";
            case 10: return "BlendFactor";
            case 11: return "InverseBlendFactor";
            case 12: return "SourceAlphaSaturation";
            default: return "<out of range>";
            }
        }

        /// One XNA `Blend` ordinal as an `NVGblendFactor`. `isSourceFactor` exists only for
        /// SourceAlphaSaturation, which GL accepts as a source factor but not as a destination one
        /// on the 2.1 context this renderer requests.
        int ToNvgBlendFactor(int blendOrdinal, bool isSourceFactor, const char* role)
        {
            switch (blendOrdinal)
            {
            case 0:  return NVG_ONE;
            case 1:  return NVG_ZERO;
            case 2:  return NVG_SRC_COLOR;
            case 3:  return NVG_ONE_MINUS_SRC_COLOR;
            case 4:  return NVG_SRC_ALPHA;
            case 5:  return NVG_ONE_MINUS_SRC_ALPHA;
            case 6:  return NVG_DST_COLOR;
            case 7:  return NVG_ONE_MINUS_DST_COLOR;
            case 8:  return NVG_DST_ALPHA;
            case 9:  return NVG_ONE_MINUS_DST_ALPHA;
            case 10:
            case 11:
                throw std::runtime_error(
                    std::string("NANOVG cannot express BlendState.") + role + " = Blend." +
                    BlendName(blendOrdinal) +
                    ": NanoVG's own NVGblendFactor enum has no constant-colour factor, so "
                    "GraphicsDevice.BlendFactor cannot reach the blend stage.");
            case 12:
                if (isSourceFactor) return NVG_SRC_ALPHA_SATURATE;
                throw std::runtime_error(
                    std::string("NANOVG cannot express BlendState.") + role +
                    " = Blend.SourceAlphaSaturation: GL accepts SRC_ALPHA_SATURATE as a source "
                    "factor only on the OpenGL 2.1 context this renderer requests.");
            default:
                throw std::runtime_error(
                    std::string("NANOVG: BlendState.") + role + " has no valid Blend ordinal (" +
                    std::to_string(blendOrdinal) + ").");
            }
        }
    }

    // A direct per-factor translation, not a preset match. NanoVG's own NVGcompositeOperation
    // presets are a lossy vocabulary -- NVG_SOURCE_OVER stands for (ONE, ONE_MINUS_SRC_ALPHA) on
    // both channels, which is BlendState.AlphaBlend and NOT BlendState.NonPremultiplied -- so
    // routing through them would silently collapse states whose semantics genuinely differ.
    // nvgGlobalCompositeBlendFuncSeparate takes the four factors directly (nanovg.c), and
    // glnvg_convertBlendFuncFactor maps every one of them onto its real GL enum, so the colour and
    // alpha channels stay independent all the way to glBlendFuncSeparate.
    NanoVgBlendFunc BlendStateToNvgBlendFunc(int colorSrcBlend, int alphaSrcBlend,
                                             int colorDstBlend, int alphaDstBlend,
                                             int colorBlendFunc, int alphaBlendFunc)
    {
        if (colorBlendFunc != kBlendFuncAdd || alphaBlendFunc != kBlendFuncAdd)
        {
            throw std::runtime_error(
                "NANOVG only supports BlendFunction.Add: NanoVG's GL2 backend never calls "
                "glBlendEquation/glBlendEquationSeparate, so the blend equation is permanently "
                "GL_FUNC_ADD and Subtract/ReverseSubtract/Min/Max cannot be honoured.");
        }

        NanoVgBlendFunc blend;
        blend.srcRGB   = ToNvgBlendFactor(colorSrcBlend, /*isSourceFactor=*/true,  "ColorSourceBlend");
        blend.dstRGB   = ToNvgBlendFactor(colorDstBlend, /*isSourceFactor=*/false, "ColorDestinationBlend");
        blend.srcAlpha = ToNvgBlendFactor(alphaSrcBlend, /*isSourceFactor=*/true,  "AlphaSourceBlend");
        blend.dstAlpha = ToNvgBlendFactor(alphaDstBlend, /*isSourceFactor=*/false, "AlphaDestinationBlend");
        return blend;
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

            // Queried once, with this renderer's context current. NanoVG never asks GL what it
            // can allocate, and nvgCreateImageRGBA does not check glGetError, so without this an
            // oversized texture becomes a silently empty GL texture object rather than an error.
            GLint maxTextureSize = 0;
            glGetIntegerv(GL_MAX_TEXTURE_SIZE, &maxTextureSize);
            maxGlTextureSize_ = maxTextureSize > 0 ? static_cast<int>(maxTextureSize) : 0;

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

    NanoVgRenderer::SpriteProjection NanoVgRenderer::GetSpriteProjectionEXT() const
    {
        const LogicalViewport viewport = ComputeLogicalViewportEXT();
        const float logicalW = viewport.logicalWidth > 0.0f ? viewport.logicalWidth : 1.0f;
        const float logicalH = viewport.logicalHeight > 0.0f ? viewport.logicalHeight : 1.0f;

        SpriteProjection projection;
        projection.width = logicalW;
        projection.height = logicalH;
        projection.devicePixelRatio = viewport.width > 0.0f ? viewport.width / logicalW : 1.0f;

        int defaultX = 0, defaultY = 0, defaultW = 0, defaultH = 0;
        defaultX = static_cast<int>(std::lround(viewport.x));
        defaultY = static_cast<int>(std::lround(viewport.y));
        defaultW = static_cast<int>(std::lround(viewport.width));
        defaultH = static_cast<int>(std::lround(viewport.height));

        const bool custom = viewportW_ > 0 && viewportH_ > 0 &&
                            (viewportX_ != defaultX || viewportY_ != defaultY ||
                             viewportW_ != defaultW || viewportH_ != defaultH);
        if (!custom)
            return projection;

        // Sprite coordinates are viewport-local physical pixels now, so the scissor rectangle --
        // which stays expressed in the render target's own logical space, exactly as XNA's
        // GraphicsDevice.ScissorRectangle does -- has to be carried across: logical -> physical
        // through the presentation mapping, then minus the viewport's own origin.
        projection.customViewport = true;
        projection.width = static_cast<float>(viewportW_);
        projection.height = static_cast<float>(viewportH_);
        projection.devicePixelRatio = 1.0f;
        projection.scissorScaleX = viewport.width > 0.0f ? viewport.width / logicalW : 1.0f;
        projection.scissorScaleY = viewport.height > 0.0f ? viewport.height / logicalH : 1.0f;
        projection.scissorOffsetX = viewport.x - static_cast<float>(viewportX_);
        projection.scissorOffsetY = viewport.y - static_cast<float>(viewportY_);
        return projection;
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
        // BlendWriteState.ColorWriteChannels cannot be honored on this renderer: nanovg_gl.h's own
        // glnvg__renderFlush calls glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE) at the top of
        // EVERY flush, before the first draw call it submits, overwriting any externally-set mask
        // -- verified empirically (a glColorMask wrapped around a whole SpriteBatch batch was
        // silently undone). Rejecting a non-default mask is honest; silently ignoring it would be
        // exactly the "capability lie" this project's own docs warn against. Validated BEFORE the
        // factors are stored, so a rejected state leaves the last accepted one untouched.
        //
        // All FOUR per-render-target slots are checked, not just slot 0. Slots 1-3 address MRT
        // outputs this renderer has no storage for, so setting one changes no pixel it can produce
        // -- but "changes no pixel" is the same argument that would have excused ignoring
        // MultiSampleMask, and the point of this boundary is that a state is either honoured or
        // named. BLEND2D and SKIA refuse them for the same reason.
        for (int target = 0; target < 4; ++target)
        {
            if (writeState.colorWriteChannels[target] == 15) // 15 = R|G|B|A, the default
                continue;
            throw std::runtime_error(
                "NANOVG cannot honor BlendState.ColorWriteChannels" +
                (target == 0 ? std::string() : std::to_string(target)) +
                ": NanoVG's own glnvg__renderFlush unconditionally resets glColorMask to "
                "all-channels-enabled before the first draw of every flush, so an externally-set "
                "write mask cannot survive a draw" +
                (target == 0
                     ? std::string(".")
                     : std::string(" -- and this renderer has no multiple-render-target output for "
                                   "slot ") + std::to_string(target) + " to write to in any case."));
        }

        // BlendState.MultiSampleMask likewise has no implementation here. It could be argued away
        // -- this renderer never creates a multisample-capable GL context, so a coverage mask has
        // no sample to disable and no observable effect -- but "no observable effect" is an
        // argument for silence, not for acceptance, and silence is what this renderer's whole
        // capability boundary is built to avoid. Refused, like every other state it cannot honour.
        if (writeState.multiSampleMask != 0xFFFFFFFFu)
        {
            throw std::runtime_error(
                "NANOVG cannot honor BlendState.MultiSampleMask: this renderer never creates a "
                "multisample-capable GL context (GraphicsCapability.MultiSampleAntiAliasing is "
                "false), so no sample-coverage mask can be applied. Leave it at the default "
                "0xFFFFFFFF.");
        }

        lastBlendFunc_ = BlendStateToNvgBlendFunc(
            colorSrcBlend, alphaSrcBlend, colorDstBlend, alphaDstBlend, colorBlendFunc, alphaBlendFunc);
    }

    void NanoVgRenderer::SetScissorRect(int x, int y, int w, int h)
    {
        // Stored verbatim, in the render target's own logical space -- exactly what
        // GraphicsDevice.ScissorRectangle means. Whatever remapping the current sprite coordinate
        // space needs happens per draw, in NanoVgSpriteBatchRenderer, because that space depends on
        // the active Viewport (see GetSpriteProjectionEXT) and can change between two draws of one
        // Immediate batch.
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
