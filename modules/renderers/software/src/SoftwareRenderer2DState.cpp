// SPDX-License-Identifier: MS-PL

#include "CNA/Internal/Renderers/Software/SoftwareRenderer.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <stdexcept>

namespace CNA::Internal::Renderers::Software
{
    SoftwareRenderer::SoftwareRenderer(
        int virtualWidth, int virtualHeight,
        bool allocateDepthBuffer, bool allocateStencilBuffer)
        : backbuffer_(allocateDepthBuffer, allocateStencilBuffer)
        , virtualWidth_(virtualWidth), virtualHeight_(virtualHeight)
    {
        backbuffer_.Resize(virtualWidth > 0 ? virtualWidth : 1024, virtualHeight > 0 ? virtualHeight : 768);
    }

    SoftwareRenderer::~SoftwareRenderer() = default;

    SoftwareFramebuffer& SoftwareRenderer::CurrentFramebuffer()
    {
        if (currentMrtCount_ > 0)
            return *currentMrtFramebuffers_[0];
        if (currentRenderTarget_ != nullptr)
            return currentRenderTarget_->Framebuffer();
#ifndef CNA_SOFTWARE_2D_ONLY
        if (currentCubeRenderTarget_ != nullptr)
            return currentCubeRenderTarget_->Framebuffer();
#endif
        return backbuffer_;
    }

    const SoftwareFramebuffer& SoftwareRenderer::CurrentFramebuffer() const
    {
        if (currentMrtCount_ > 0)
            return *currentMrtFramebuffers_[0];
        if (currentRenderTarget_ != nullptr)
            return currentRenderTarget_->Framebuffer();
#ifndef CNA_SOFTWARE_2D_ONLY
        if (currentCubeRenderTarget_ != nullptr)
            return currentCubeRenderTarget_->Framebuffer();
#endif
        return backbuffer_;
    }

    void SoftwareRenderer::Clear(float r, float g, float b, float a)
    {
        ForEachActiveColorTarget(
            [=](SoftwareFramebuffer& framebuffer) { framebuffer.ClearColor(r, g, b, a); });
    }

    void SoftwareRenderer::Present() {}

    void SoftwareRenderer::GetViewportSize(int& width, int& height)
    {
        const SoftwareFramebuffer& fb = CurrentFramebuffer();
        width = fb.width;
        height = fb.height;
    }

    void SoftwareRenderer::SetVirtualResolution(int width, int height)
    {
        if (currentRenderTarget_ == nullptr && currentCubeRenderTarget_ == nullptr &&
            currentMrtCount_ == 0)
            backbuffer_.Resize(width, height);
        virtualWidth_ = width;
        virtualHeight_ = height;
    }

    void SoftwareRenderer::SetPresentationMode(int) {}

    void SoftwareRenderer::ReadBackbuffer(int x, int y, int w, int h, uint8_t* pixels)
    {
        if (w < 0 || h < 0)
            throw std::runtime_error("SoftwareRenderer::ReadBackbuffer: negative width/height");

        SoftwareFramebuffer& writableFramebuffer = CurrentFramebuffer();
        writableFramebuffer.ResolveColor();
        const SoftwareFramebuffer& fb = writableFramebuffer;
        for (int row = 0; row < h; ++row)
        {
            const int srcY = y + row;
            for (int col = 0; col < w; ++col)
            {
                const int srcX = x + col;
                const std::size_t dstIndex = (static_cast<std::size_t>(row) * static_cast<std::size_t>(w) +
                                              static_cast<std::size_t>(col)) * 4u;
                if (srcX < 0 || srcX >= fb.width || srcY < 0 || srcY >= fb.height)
                {
                    pixels[dstIndex + 0] = 0;
                    pixels[dstIndex + 1] = 0;
                    pixels[dstIndex + 2] = 0;
                    pixels[dstIndex + 3] = 0;
                    continue;
                }
                const std::size_t srcIndex = (static_cast<std::size_t>(srcY) * static_cast<std::size_t>(fb.width) +
                                              static_cast<std::size_t>(srcX)) * 4u;
                pixels[dstIndex + 0] = fb.color[srcIndex + 0];
                pixels[dstIndex + 1] = fb.color[srcIndex + 1];
                pixels[dstIndex + 2] = fb.color[srcIndex + 2];
                pixels[dstIndex + 3] = fb.color[srcIndex + 3];
            }
        }
    }

    std::unique_ptr<ITextureRenderer> SoftwareRenderer::CreateTexture(const ImageData& data)
    {
        return std::make_unique<SoftwareTextureRenderer>(data);
    }

    RendererFormatVerdict SoftwareRenderer::ClassifySurfaceFormatEXT(int surfaceFormat) const
    {
        using Microsoft::Xna::Framework::Graphics::SurfaceFormat;
        switch (static_cast<SurfaceFormat>(surfaceFormat))
        {
            case SurfaceFormat::Color:
            case SurfaceFormat::Bgr565:
            case SurfaceFormat::Bgra5551:
            case SurfaceFormat::Bgra4444:
            case SurfaceFormat::Dxt1:
            case SurfaceFormat::Dxt3:
            case SurfaceFormat::Dxt5:
                return RendererFormatVerdict::Supported;
            default:
                return RendererFormatVerdict::Defer;
        }
    }

    RendererFormatVerdict SoftwareRenderer::ClassifyColorTransferFormatEXT(
        int surfaceFormat) const
    {
        using Microsoft::Xna::Framework::Graphics::SurfaceFormat;
        switch (static_cast<SurfaceFormat>(surfaceFormat))
        {
            case SurfaceFormat::Bgr565:
            case SurfaceFormat::Bgra5551:
            case SurfaceFormat::Bgra4444:
            case SurfaceFormat::Dxt1:
            case SurfaceFormat::Dxt3:
            case SurfaceFormat::Dxt5:
                return RendererFormatVerdict::Unsupported;
            default:
                return RendererFormatVerdict::Defer;
        }
    }

    bool SoftwareRenderer::IsCompressedTransferFormatEXT(int surfaceFormat) const
    {
        using Microsoft::Xna::Framework::Graphics::SurfaceFormat;
        const auto format = static_cast<SurfaceFormat>(surfaceFormat);
        return format == SurfaceFormat::Dxt1 || format == SurfaceFormat::Dxt3 ||
               format == SurfaceFormat::Dxt5;
    }

    bool SoftwareRenderer::SupportsCapability(CNA::GraphicsCapability capability) const
    {
        switch (capability)
        {
            case CNA::GraphicsCapability::Texture3D:
                // SOFTWARE-118: every declared RGBA8 mip owns exact CPU volume storage, including
                // partial box upload/readback. This flag intentionally promises storage only.
                return true;
            case CNA::GraphicsCapability::AnisotropicFiltering:
                // SOFTWARE-117: the CPU sampler resolves the full directional texel footprint,
                // selects LOD from its minor axis and averages up to 16 taps along its major axis.
                return true;
            case CNA::GraphicsCapability::OcclusionQuery:
                // SOFTWARE-122: the CPU query counts the exact samples surviving coverage,
                // MultiSampleMask, alpha, depth and stencil through the shared fragment path.
                return true;
            case CNA::GraphicsCapability::CustomEffects:
                // SoftwareEffectRenderer accepts source for resource compatibility, but the CPU
                // rasterizer never executes that source. Its fixed stock-effect path is not custom
                // shader support.
                return false;
            case CNA::GraphicsCapability::MultiStreamVertexInput:
                // REMED-GFX-201: implemented -- the vertex reader resolves each combined-layout
                // byte offset to the stream that owns it, so every attribute is fetched from its
                // own buffer with that buffer's own stride and binding offset, with no interleaved
                // temporary and no per-vertex allocation.
                return true;
            case CNA::GraphicsCapability::MultipleRenderTargets:
                // SOFTWARE-120: up to four ordered CPU colour attachments bind simultaneously.
                // The first owns depth/stencil and classic stock effects emit COLOR0 only, while
                // Clear and resolve/mip finalization visit every attachment.
                return true;
            case CNA::GraphicsCapability::Instancing:
                // SOFTWARE-129: every instance is rasterized on the CPU from its independently
                // frequency-stepped matrix record, through the same declaration/effect path as an
                // ordinary indexed draw.
                return true;
            default:
                return true;
        }
    }

    std::unique_ptr<ISpriteBatchRenderer> SoftwareRenderer::CreateSpriteBatch()
    {
        return std::make_unique<SoftwareSpriteBatchRenderer>(*this);
    }

    std::unique_ptr<IRenderTargetRenderer> SoftwareRenderer::CreateRenderTarget2D(
        int w, int h, int depthFormat, bool, bool mipMap, int multiSampleCount)
    {
        return std::make_unique<SoftwareRenderTargetRenderer>(
            w, h, depthFormat, mipMap, multiSampleCount, depthFormat != 0, false);
    }

    void SoftwareRenderer::SetRenderTarget2D(IRenderTargetRenderer* rt)
    {
        auto* next = dynamic_cast<SoftwareRenderTargetRenderer*>(rt);
        if (rt != nullptr && next == nullptr)
            throw std::runtime_error(
                "SoftwareRenderer::SetRenderTarget2D: incompatible renderer resource.");
        UnbindCurrentTargets();
        currentRenderTarget_ = next;
        if (currentRenderTarget_ != nullptr)
            currentRenderTarget_->BindAsRenderTarget();
    }

    void SoftwareRenderer::SetRenderTargetCubeFace(IRenderTargetCubeRenderer* rt, int face)
    {
#ifdef CNA_SOFTWARE_2D_ONLY
        (void)rt;
        (void)face;
        throw std::runtime_error(
            "Software's GDI 2D compilation unit does not support RenderTargetCube.");
#else
        auto* next = dynamic_cast<SoftwareRenderTargetCubeRenderer*>(rt);
        if (rt != nullptr && next == nullptr)
            throw std::runtime_error(
                "SoftwareRenderer::SetRenderTargetCubeFace: incompatible renderer resource.");
        UnbindCurrentTargets();
        currentCubeRenderTarget_ = next;
        if (currentCubeRenderTarget_ != nullptr)
            currentCubeRenderTarget_->BindAsRenderTargetFace(face);
#endif
    }

    void SoftwareRenderer::SetRenderTargets(
        const RenderTargetBindingDescriptor* renderTargets, int count)
    {
        if (!renderTargets || count <= 0)
        {
            SetRenderTarget2D(nullptr);
            return;
        }
        if (count == 1)
        {
            if (renderTargets[0].IsRenderTargetCubeFace())
                SetRenderTargetCubeFace(renderTargets[0].GetRenderTargetCube(),
                                        renderTargets[0].GetCubeFace());
            else
                SetRenderTarget2D(renderTargets[0].GetRenderTarget2D());
            return;
        }
        if (count > 4)
            throw std::invalid_argument(
                "SoftwareRenderer::SetRenderTargets supports at most four targets.");

        std::array<SoftwareRenderTargetRenderer*, 4> next2D{};
        std::array<SoftwareRenderTargetCubeRenderer*, 4> nextCube{};
        std::array<int, 4> nextFaces{};
        for (int slot = 0; slot < count; ++slot)
        {
            if (renderTargets[slot].IsRenderTargetCubeFace())
            {
                nextCube[static_cast<std::size_t>(slot)] =
                    dynamic_cast<SoftwareRenderTargetCubeRenderer*>(
                        renderTargets[slot].GetRenderTargetCube());
                nextFaces[static_cast<std::size_t>(slot)] = renderTargets[slot].GetCubeFace();
                if (nextCube[static_cast<std::size_t>(slot)] == nullptr)
                    throw std::runtime_error(
                        "SoftwareRenderer::SetRenderTargets: incompatible cube target.");
            }
            else
            {
                next2D[static_cast<std::size_t>(slot)] =
                    dynamic_cast<SoftwareRenderTargetRenderer*>(
                        renderTargets[slot].GetRenderTarget2D());
                if (next2D[static_cast<std::size_t>(slot)] == nullptr)
                    throw std::runtime_error(
                        "SoftwareRenderer::SetRenderTargets: incompatible 2D target.");
            }
        }

        UnbindCurrentTargets();
        try
        {
            for (int slot = 0; slot < count; ++slot)
            {
                const std::size_t index = static_cast<std::size_t>(slot);
                currentMrt2DTargets_[index] = next2D[index];
                currentMrtCubeTargets_[index] = nextCube[index];
                currentMrtCubeFaces_[index] = nextFaces[index];
                if (next2D[index] != nullptr)
                {
                    next2D[index]->BindAsRenderTarget();
                    currentMrtFramebuffers_[index] = &next2D[index]->Framebuffer();
                }
                else
                {
                    currentMrtFramebuffers_[index] =
                        &nextCube[index]->BindForMrt(nextFaces[index], slot == 0);
                }
                ++currentMrtCount_;
            }
        }
        catch (...)
        {
            UnbindCurrentTargets();
            throw;
        }
    }

    void SoftwareRenderer::UnbindCurrentTargets()
    {
        if (currentMrtCount_ > 0)
        {
            for (int slot = 0; slot < currentMrtCount_; ++slot)
            {
                const std::size_t index = static_cast<std::size_t>(slot);
                if (currentMrt2DTargets_[index] != nullptr)
                    currentMrt2DTargets_[index]->UnbindAsRenderTarget();
#ifndef CNA_SOFTWARE_2D_ONLY
                else if (currentMrtCubeTargets_[index] != nullptr)
                    currentMrtCubeTargets_[index]->UnbindForMrt(
                        currentMrtCubeFaces_[index], slot == 0);
#endif
                currentMrtFramebuffers_[index] = nullptr;
                currentMrt2DTargets_[index] = nullptr;
                currentMrtCubeTargets_[index] = nullptr;
                currentMrtCubeFaces_[index] = 0;
            }
            currentMrtCount_ = 0;
        }
        if (currentRenderTarget_ != nullptr)
            currentRenderTarget_->UnbindAsRenderTarget();
#ifndef CNA_SOFTWARE_2D_ONLY
        if (currentCubeRenderTarget_ != nullptr)
            currentCubeRenderTarget_->UnbindAsRenderTarget();
#endif
        currentRenderTarget_ = nullptr;
        currentCubeRenderTarget_ = nullptr;
    }

    void SoftwareRenderer::ForEachActiveColorTarget(
        const std::function<void(SoftwareFramebuffer&)>& operation)
    {
        if (currentMrtCount_ > 0)
        {
            for (int slot = 0; slot < currentMrtCount_; ++slot)
                operation(*currentMrtFramebuffers_[static_cast<std::size_t>(slot)]);
            return;
        }
        operation(CurrentFramebuffer());
    }
    void SoftwareRenderer::ApplyBlendState(int colorSrcBlend, int alphaSrcBlend,
                                                  int colorDstBlend, int alphaDstBlend,
                                                  int colorBlendFunc, int alphaBlendFunc,
                                                  const BlendWriteState& writeState)
    {
        // REMED-GFX-148: retain the complete state instead of reducing it to Opaque/non-Opaque.
        // Reject unknown ordinals at state application so no fragment can fail halfway through a
        // draw. Public Blend and BlendFunction currently define exactly 0..12 and 0..4.
        const auto validFactor = [](int value) { return value >= 0 && value <= 12; };
        if (!validFactor(colorSrcBlend) || !validFactor(alphaSrcBlend) ||
            !validFactor(colorDstBlend) || !validFactor(alphaDstBlend))
            throw std::runtime_error(
                "SoftwareRenderer::ApplyBlendState: unsupported Blend factor ordinal");
        if (colorBlendFunc < 0 || colorBlendFunc > 4 ||
            alphaBlendFunc < 0 || alphaBlendFunc > 4)
            throw std::runtime_error(
                "SoftwareRenderer::ApplyBlendState: unsupported BlendFunction ordinal");
        blendState_ = SoftwareBlendState{colorSrcBlend, alphaSrcBlend,
                                         colorDstBlend, alphaDstBlend,
                                         colorBlendFunc, alphaBlendFunc};
        // SOFTWARE-120: retain all four slot masks. Classic XNA stock effects emit COLOR0 only,
        // so the current fixed CPU fragment paths consume slot 0 and leave higher attachments at
        // their explicit clear/preserved contents. Single-sample surfaces use MultiSampleMask bit
        // 0; the optional four-sample colour plane uses bits 0..3 (GDI-073).
        std::copy_n(writeState.colorWriteChannels, colorWriteMasks_.size(),
                    colorWriteMasks_.begin());
        multiSampleMask_ = writeState.multiSampleMask;
    }

    void SoftwareRenderer::SetBlendFactor(float r, float g, float b, float a)
    {
        blendFactor_ = {std::clamp(r, 0.0f, 1.0f), std::clamp(g, 0.0f, 1.0f),
                        std::clamp(b, 0.0f, 1.0f), std::clamp(a, 0.0f, 1.0f)};
    }

    void SoftwareRenderer::ApplyDepthStencilState(bool depthEnable, bool depthWriteEnable, int depthFunc,
                                                         bool stencilEnable, int stencilFunc, int stencilPass,
                                                         int stencilFail, int stencilDepthFail, int stencilMask,
                                                         int stencilWriteMask, int referenceStencil,
                                                         bool twoSidedStencilMode,
                                                         int ccwStencilFunc, int ccwStencilPass,
                                                         int ccwStencilFail, int ccwStencilDepthFail)
    {
        // REMED-GFX-030: every public CompareFunction has ordinal 0..7. Reject an invalid value at
        // state application rather than carrying it into the hot fragment path or approximating it.
        if (depthFunc < 0 || depthFunc > 7)
            throw std::runtime_error(
                "SoftwareRenderer::ApplyDepthStencilState: unsupported depth CompareFunction ordinal");
        depthTestEnabled_ = depthEnable;
        depthWriteEnabled_ = depthWriteEnable;
        depthCompareFunction_ = depthFunc;
        if (stencilFunc < 0 || stencilFunc > 7)
            throw std::runtime_error(
                "SoftwareRenderer::ApplyDepthStencilState: unsupported stencil CompareFunction ordinal");
        const auto validateStencilOperation = [](int operation) {
            if (operation < 0 || operation > 7)
                throw std::runtime_error(
                    "SoftwareRenderer::ApplyDepthStencilState: unsupported StencilOperation ordinal");
        };
        validateStencilOperation(stencilPass);
        validateStencilOperation(stencilFail);
        validateStencilOperation(stencilDepthFail);
        if (ccwStencilFunc < 0 || ccwStencilFunc > 7)
            throw std::runtime_error(
                "SoftwareRenderer::ApplyDepthStencilState: unsupported counter-clockwise stencil CompareFunction ordinal");
        validateStencilOperation(ccwStencilPass);
        validateStencilOperation(ccwStencilFail);
        validateStencilOperation(ccwStencilDepthFail);
        stencilTestEnabled_ = stencilEnable;
        stencilCompareFunction_ = stencilFunc;
        stencilPassOperation_ = stencilPass;
        stencilFailOperation_ = stencilFail;
        stencilDepthFailOperation_ = stencilDepthFail;
        stencilReadMask_ = stencilMask & 0xFF;
        stencilWriteMask_ = stencilWriteMask & 0xFF;
        referenceStencil_ = referenceStencil & 0xFF;
        twoSidedStencilMode_ = twoSidedStencilMode;
        counterClockwiseStencilCompareFunction_ = ccwStencilFunc;
        counterClockwiseStencilPassOperation_ = ccwStencilPass;
        counterClockwiseStencilFailOperation_ = ccwStencilFail;
        counterClockwiseStencilDepthFailOperation_ = ccwStencilDepthFail;
    }

    void SoftwareRenderer::SetReferenceStencil(int value)
    {
        referenceStencil_ = value & 0xFF;
    }

    void SoftwareRenderer::ApplyRasterizerState(int cullMode, int fillMode, bool scissorTestEnable,
                                                       float depthBias, float slopeScaleDepthBias)
    {
        cullMode_ = cullMode;
        // REMED-GFX-082: capture FillMode (previously discarded). 0=Solid, 1=WireFrame -- the raster
        // paths render only triangle edges when WireFrame. Independent of CullMode/ScissorTestEnable.
        fillMode_ = fillMode;
        // REMED-GFX-080: capture ScissorTestEnable (previously discarded). Independent of the stored
        // ScissorRectangle -- toggling this on/off enables/disables the same stored rectangle.
        scissorTestEnable_ = scissorTestEnable;
        // REMED-GFX-083: capture DepthBias / SlopeScaleDepthBias (both previously discarded). Folded into
        // the post-viewport per-fragment depth by the rasterizer via ComputeDepthBiasOffset; 0/0 (the
        // default) is a byte-identical no-op. Same unscaled units GraphicsDevice forwards to every renderer.
        depthBias_ = depthBias;
        slopeScaleDepthBias_ = slopeScaleDepthBias;
    }

    // REMED-GFX-150: store the SamplerState so the rasterizer's sampler can honor it. Previously
    // every parameter but `slot` was unnamed and discarded, so TextureFilter and TextureAddressMode
    // never reached a single textured fragment and every draw filtered LinearClamp. SOFTWARE-117
    // additionally retains MaxAnisotropy for the directional CPU footprint sampler.
    void SoftwareRenderer::ApplySamplerState(int slot, int filter, int addressU, int addressV,
                                             int maxAnisotropy)
    {
        if (slot < 0 || slot >= kMaxSamplerSlots)
            throw std::runtime_error("SoftwareRenderer::ApplySamplerState: slot must be 0..15");
        SoftwareSamplerState& s = samplerSlots_[static_cast<std::size_t>(slot)];
        s.filter = filter;
        s.addressU = addressU;
        s.addressV = addressV;
        s.maxAnisotropy = maxAnisotropy;
    }

    // REMED-GFX-080: store the ScissorRectangle so the raster paths can intersect it into their
    // effective clip when scissor testing is enabled (previously a no-op, so ScissorRectangle never
    // clipped anything). GraphicsDevice pushes this on every setScissorRectangleProperty() and
    // resets it to the full target on each RenderTarget transition, so this single field is always
    // relative to the currently active target. The rectangle is stored regardless of the current
    // ScissorTestEnable flag -- it becomes active if a later RasterizerState enables scissor testing.
    void SoftwareRenderer::SetScissorRect(int x, int y, int w, int h)
    {
        scissorSet_ = true;
        scissorX_ = x;
        scissorY_ = y;
        scissorWidth_ = w;
        scissorHeight_ = h;
    }

    // REMED-GFX-073: store the viewport so the SpriteBatch path can place its viewport-local quads
    // at (x,y) and clip them to (x,y,w,h). GraphicsDevice pushes this on every setViewportProperty()
    // and resets it to the full target on each RenderTarget transition, so this single field is
    // always relative to the currently active target. REMED-GFX-079 now consumes the same stored
    // rectangle and depth range in every 3D draw path as well.
    void SoftwareRenderer::SetViewport(int x, int y, int w, int h, float minDepth, float maxDepth)
    {
        viewportSet_ = true;
        viewportX_ = x;
        viewportY_ = y;
        viewportWidth_ = w;
        viewportHeight_ = h;
        viewportMinDepth_ = minDepth;
        viewportMaxDepth_ = maxDepth;
    }

    void SoftwareRenderer::GetActiveViewport(int& x, int& y, int& w, int& h) const
    {
        if (!viewportSet_)
        {
            const SoftwareFramebuffer& fb = CurrentFramebuffer();
            x = 0;
            y = 0;
            w = fb.width;
            h = fb.height;
            return;
        }
        x = viewportX_;
        y = viewportY_;
        w = viewportWidth_;
        h = viewportHeight_;
    }

    // REMED-GFX-080: mirrors GetActiveViewport -- returns the stored ScissorRectangle, or the full
    // current framebuffer when none was set (so enabling scissor testing without an explicit
    // rectangle is an inert clip, matching XNA's default full-target ScissorRectangle).
    void SoftwareRenderer::GetActiveScissor(int& x, int& y, int& w, int& h) const
    {
        if (!scissorSet_)
        {
            const SoftwareFramebuffer& fb = CurrentFramebuffer();
            x = 0;
            y = 0;
            w = fb.width;
            h = fb.height;
            return;
        }
        x = scissorX_;
        y = scissorY_;
        w = scissorWidth_;
        h = scissorHeight_;
    }

    void SoftwareRenderer::GetActiveViewportRaster(int& x, int& y, int& w, int& h,
                                                          float& minDepth, float& maxDepth) const
    {
        GetActiveViewport(x, y, w, h);
        // The depth range defaults to the full [0,1] until a custom viewport is set (matching
        // GetActiveViewport's full-framebuffer x/y/w/h fallback); once SetViewport has run, the
        // stored MinDepth/MaxDepth apply. This is the single point where the 3D path resolves its
        // viewport, so all four draw entry points stay consistent.
        minDepth = viewportSet_ ? viewportMinDepth_ : 0.0f;
        maxDepth = viewportSet_ ? viewportMaxDepth_ : 1.0f;
    }

    void SoftwareRenderer::ClearColorAndDepth(float r, float g, float b, float a, float depth)
    {
        ForEachActiveColorTarget(
            [=](SoftwareFramebuffer& framebuffer) { framebuffer.ClearColor(r, g, b, a); });
        CurrentFramebuffer().ClearDepthValue(depth);
    }

    void SoftwareRenderer::ClearDepth(float depth) { CurrentFramebuffer().ClearDepthValue(depth); }
    void SoftwareRenderer::ClearStencil(int stencil)
    { CurrentFramebuffer().ClearStencilValue(stencil); }
    void SoftwareRenderer::ClearDepthAndStencil(float depth, int stencil)
    {
        CurrentFramebuffer().ClearDepthValue(depth);
        CurrentFramebuffer().ClearStencilValue(stencil);
    }
    void SoftwareRenderer::ClearColorAndStencil(float r, float g, float b, float a, int stencil)
    {
        ForEachActiveColorTarget(
            [=](SoftwareFramebuffer& framebuffer) { framebuffer.ClearColor(r, g, b, a); });
        CurrentFramebuffer().ClearStencilValue(stencil);
    }
    void SoftwareRenderer::ClearColorDepthAndStencil(float r, float g, float b, float a, float depth, int stencil)
    {
        ClearColorAndDepth(r, g, b, a, depth);
        CurrentFramebuffer().ClearStencilValue(stencil);
    }

    void SoftwareRenderer::SetDepthTestEnabled(bool enabled) { depthTestEnabled_ = enabled; }
    void SoftwareRenderer::SetBlendEnabled(bool) {}
    void SoftwareRenderer::SetDepthWriteEnabled(bool enabled) { depthWriteEnabled_ = enabled; }
}
