// SPDX-License-Identifier: MS-PL

#include "CNA/Internal/Renderers/Software/SoftwareRenderer.hpp"

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
        return currentRenderTarget_ != nullptr ? currentRenderTarget_->Framebuffer() : backbuffer_;
    }

    const SoftwareFramebuffer& SoftwareRenderer::CurrentFramebuffer() const
    {
        return currentRenderTarget_ != nullptr ? currentRenderTarget_->Framebuffer() : backbuffer_;
    }

    void SoftwareRenderer::Clear(float r, float g, float b, float a)
    {
        CurrentFramebuffer().ClearColor(r, g, b, a);
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
        if (currentRenderTarget_ == nullptr)
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
    bool SoftwareRenderer::SupportsCapability(CNA::GraphicsCapability capability) const
    {
        switch (capability)
        {
            // REMED-CONTENT-004: Texture3D remains an explicit, documented v1 scope boundary for
            // this renderer (see this header's own "Boundaries" comment) -- CreateTexture3D() keeps
            // IGraphicsRenderer's shared default (returns nullptr). Reported here so Texture3D's own
            // constructor can fail cleanly instead of silently discarding every SetData()/GetData()
            // call.
            case CNA::GraphicsCapability::Texture3D:
                return false;
            case CNA::GraphicsCapability::MultiStreamVertexInput:
                // REMED-GFX-201: implemented -- the vertex reader resolves each combined-layout
                // byte offset to the stream that owns it, so every attribute is fetched from its
                // own buffer with that buffer's own stride and binding offset, with no interleaved
                // temporary and no per-vertex allocation.
                return true;
            case CNA::GraphicsCapability::MultipleRenderTargets:
                // SetRenderTargets() throws for count > 1 and ApplyBlendState() notes the same
                // limit: this renderer has ONE active colour buffer. Reported honestly instead of
                // inherited as the blanket true below -- a capability is a promise, and this one
                // was being made and then broken.
                return false;
            case CNA::GraphicsCapability::Instancing:
                // Not implemented: this renderer does not override DrawInstancedPrimitivesEx, so
                // an instanced draw is the shared base-class refusal -- reported honestly instead
                // of inherited as the blanket true below.
                return false;
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
        return std::make_unique<SoftwareRenderTargetRenderer>(w, h, depthFormat, mipMap, multiSampleCount);
    }

    void SoftwareRenderer::SetRenderTarget2D(IRenderTargetRenderer* rt)
    {
        if (currentRenderTarget_ != nullptr)
            currentRenderTarget_->UnbindAsRenderTarget();
        currentRenderTarget_ = static_cast<SoftwareRenderTargetRenderer*>(rt);
        if (currentRenderTarget_ != nullptr)
            currentRenderTarget_->BindAsRenderTarget();
    }

    void SoftwareRenderer::SetRenderTargets(
        const RenderTargetBindingDescriptor* renderTargets, int count)
    {
        if (!renderTargets || count <= 0)
        {
            SetRenderTarget2D(nullptr);
            return;
        }
        if (count > 1)
            throw std::runtime_error(
                "SoftwareRenderer does not support multiple simultaneous render targets.");
        if (renderTargets[0].IsRenderTargetCubeFace())
            throw std::runtime_error(
                "SoftwareRenderer does not support RenderTargetCube face bindings.");
        SetRenderTarget2D(renderTargets[0].GetRenderTarget2D());
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
        // REMED-GFX-077: Software has one active colour buffer (no MRT), so only slot-0's write mask
        // applies; the CPU fragment writers (WriteColoredFragment/WriteShadedFragment) gate each
        // channel by it. Single-sample surfaces use MultiSampleMask bit 0; the optional four-sample
        // colour plane uses bits 0..3 (GDI-073).
        colorWriteMask_  = writeState.colorWriteChannels[0];
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
                                                         int stencilWriteMask, int referenceStencil, bool,
                                                         int, int, int, int)
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
        stencilTestEnabled_ = stencilEnable;
        stencilCompareFunction_ = stencilFunc;
        stencilPassOperation_ = stencilPass;
        stencilFailOperation_ = stencilFail;
        stencilDepthFailOperation_ = stencilDepthFail;
        stencilReadMask_ = stencilMask & 0xFF;
        stencilWriteMask_ = stencilWriteMask & 0xFF;
        referenceStencil_ = referenceStencil & 0xFF;
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
    // never reached a single textured fragment and every draw filtered LinearClamp. maxAnisotropy is
    // still not consumed: this renderer has no anisotropic filter, and TextureFilter::Anisotropic
    // already resolves to Linear through the same min/mag table the other ordinals use.
    void SoftwareRenderer::ApplySamplerState(int slot, int filter, int addressU, int addressV, int)
    {
        if (slot < 0 || slot >= kMaxSamplerSlots)
            throw std::runtime_error("SoftwareRenderer::ApplySamplerState: slot must be 0..15");
        SoftwareSamplerState& s = samplerSlots_[static_cast<std::size_t>(slot)];
        s.filter = filter;
        s.addressU = addressU;
        s.addressV = addressV;
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
        SoftwareFramebuffer& fb = CurrentFramebuffer();
        fb.ClearColor(r, g, b, a);
        fb.ClearDepthValue(depth);
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
        CurrentFramebuffer().ClearColor(r, g, b, a);
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
