// SPDX-License-Identifier: MS-PL

#include "CNA/Internal/Renderers/Rlgl/RlglRenderer.hpp"

#include "CNA/Logger.hpp"
#include "Microsoft/Xna/Framework/Graphics/SurfaceFormat.hpp"
#include "System/NotSupportedException.hpp"

#include "RlglBridge.hpp"
#include "RlglResources.hpp"

#include <algorithm>
#include <cmath>
#include <mutex>
#include <stdexcept>
#include <string>

namespace CNA::Internal::Renderers::Rlgl
{
    namespace
    {
        constexpr const char* kRendererName = "RLGL";

        std::mutex lifecycleMutex;
        bool lifecycleActive = false;

        CNA::Platform::GlContextDescription RequestedContext(
            const int multiSampleCount, const bool withMultisampling)
        {
            CNA::Platform::GlContextDescription description;
            description.majorVersion = 3;
            description.minorVersion = 3;
            description.profile = CNA::Platform::GlProfile::Core;
            description.depthBits = 24;
            description.stencilBits = 8;
            description.doubleBuffer = true;
            if (withMultisampling && multiSampleCount > 1)
            {
                description.multisampleBuffers = 1;
                description.multisampleSamples = multiSampleCount;
            }
            return description;
        }

        void ClaimLifecycle()
        {
            const std::scoped_lock lock(lifecycleMutex);
            if (lifecycleActive)
            {
                throw std::runtime_error(
                    "RLGL: rlgl 6.0 keeps process-global renderer state; only one live RLGL "
                    "GraphicsDevice is supported");
            }
            lifecycleActive = true;
        }

        void ReleaseLifecycle()
        {
            const std::scoped_lock lock(lifecycleMutex);
            lifecycleActive = false;
        }

        [[noreturn]] void Unsupported(const char* operation, const char* task)
        {
            throw System::NotSupportedException(
                std::string(kRendererName) + ": " + operation +
                " is not implemented yet (plans/plan_rlgl.md " + task + ")");
        }
    }

    RlglRenderer::RlglRenderer(const GraphicsRendererCreateArgs& args)
        : surface_(args.surface)
        , platformGlService_(&RequirePlatformGlContext(args.glContext, kRendererName))
        , virtualWidth_(args.virtualWidth)
        , virtualHeight_(args.virtualHeight)
        , presentationMode_(args.presentationMode)
        , swapInterval_(args.swapInterval)
    {
        RequirePlatformGlWindow(args.surface, kRendererName);
        ClaimLifecycle();
        lifecycleClaimed_ = true;

        try
        {
            CreateContext(args.multiSampleCount);

            int width = 0;
            int height = 0;
            GetPhysicalSize(width, height);
            const std::string version = Bridge::Initialize(
                platformContext_->GetLoader(), width, height);
            rlglInitialized_ = true;
            maxTextureSize_ = Bridge::GetMaxTextureSize();
            maxSamplerSlots_ = Bridge::GetMaxSamplerSlots();
            maxSamplerAnisotropy_ = Bridge::GetMaxSamplerAnisotropy();
            if (maxSamplerSlots_ < static_cast<int>(samplers_.size()))
            {
                throw std::runtime_error(
                    "RLGL: the OpenGL context exposes fewer than 16 fragment texture units");
            }

            platformContext_->SetSwapInterval(swapInterval_);
            IGraphicsRenderer::RegisterForWindow(surface_.GetWindowId(), this);
            registered_ = true;

            CNA::Logger::Info(
                "RLGL renderer initialized standalone rlgl 6.0 with OpenGL " + version,
                CNA::LogCategory::RENDER);
        }
        catch (...)
        {
            if (rlglInitialized_)
            {
                Bridge::Shutdown();
                rlglInitialized_ = false;
            }
            platformContext_.reset();
            ReleaseLifecycle();
            lifecycleClaimed_ = false;
            throw;
        }
    }

    RlglRenderer::~RlglRenderer()
    {
        if (registered_)
        {
            IGraphicsRenderer::UnregisterForWindow(surface_.GetWindowId());
            registered_ = false;
        }

        if (rlglInitialized_ && platformContext_)
        {
            try
            {
                platformContext_->MakeCurrent();
                std::array<unsigned int, 16> samplerIds{};
                for (std::size_t index = 0; index < samplers_.size(); ++index)
                    samplerIds[index] = samplers_[index].id;
                Bridge::DestroySamplers(samplerIds.data(), samplerIds.size());
                Bridge::Shutdown();
            }
            catch (const std::exception& error)
            {
                CNA::Logger::Error(
                    std::string("RLGL: failed to make the context current during shutdown: ") +
                        error.what(),
                    CNA::LogCategory::RENDER);
            }
            rlglInitialized_ = false;
        }

        platformContext_.reset();
        if (lifecycleClaimed_)
        {
            ReleaseLifecycle();
            lifecycleClaimed_ = false;
        }
    }

    void RlglRenderer::CreateContext(const int requestedMultiSampleCount)
    {
        const bool wantMultisampling = requestedMultiSampleCount > 1;
        try
        {
            platformContext_ = std::make_unique<PlatformGlContextOwner>(
                *platformGlService_, surface_.GetWindowId(),
                RequestedContext(requestedMultiSampleCount, wantMultisampling));
        }
        catch (const CNA::Platform::PlatformException&)
        {
            if (!wantMultisampling) throw;
            platformContext_ = std::make_unique<PlatformGlContextOwner>(
                *platformGlService_, surface_.GetWindowId(),
                RequestedContext(requestedMultiSampleCount, false));
        }

        const auto granted = platformContext_->GetAttributes();
        if (granted.profile != CNA::Platform::GlProfile::Core ||
            granted.majorVersion < 3 ||
            (granted.majorVersion == 3 && granted.minorVersion < 3))
        {
            throw std::runtime_error(
                "RLGL: platform did not grant the required OpenGL 3.3 core context");
        }

        depthBits_ = granted.depthBits;
        stencilBits_ = granted.stencilBits;
        multiSampleCount_ =
            granted.multisampleBuffers > 0 && granted.multisampleSamples > 1
                ? granted.multisampleSamples
                : 0;
    }

    void RlglRenderer::Clear(const float r, const float g, const float b, const float a)
    {
        Bridge::Clear(Bridge::ColorPlane, r, g, b, a, 1.0f, 0);
    }

    void RlglRenderer::Present()
    {
        platformContext_->SwapBuffers();
    }

    void RlglRenderer::GetPhysicalSize(int& width, int& height) const
    {
        surface_.GetDrawableSize(width, height);
    }

    void RlglRenderer::GetLogicalSize(int& width, int& height) const
    {
        if (virtualHeight_ <= 0)
        {
            GetPhysicalSize(width, height);
            return;
        }

        int physicalWidth = 0;
        int physicalHeight = 0;
        GetPhysicalSize(physicalWidth, physicalHeight);
        height = virtualHeight_;
        if (presentationMode_ == CnaPresentationMode::FixedHeightDynamicWidth &&
            physicalHeight > 0)
        {
            width = static_cast<int>(
                std::lround(static_cast<double>(physicalWidth) * virtualHeight_ / physicalHeight));
        }
        else
        {
            width = virtualWidth_ > 0 ? virtualWidth_ : physicalWidth;
        }
    }

    void RlglRenderer::GetViewportSize(int& width, int& height)
    {
        GetLogicalSize(width, height);
    }

    void RlglRenderer::GetDefaultViewportRect(
        int& x, int& y, int& width, int& height)
    {
        int physicalWidth = 0;
        int physicalHeight = 0;
        GetPhysicalSize(physicalWidth, physicalHeight);

        x = 0;
        y = 0;
        width = std::max(0, physicalWidth);
        height = std::max(0, physicalHeight);

        if (physicalWidth <= 0 || physicalHeight <= 0 ||
            presentationMode_ == CnaPresentationMode::NativeBackBuffer ||
            presentationMode_ == CnaPresentationMode::FixedHeightDynamicWidth ||
            presentationMode_ == CnaPresentationMode::Stretch ||
            virtualWidth_ <= 0 || virtualHeight_ <= 0)
        {
            return;
        }

        const double scaleX = static_cast<double>(physicalWidth) / virtualWidth_;
        const double scaleY = static_cast<double>(physicalHeight) / virtualHeight_;
        const double scale = presentationMode_ == CnaPresentationMode::Overscan
                                 ? std::max(scaleX, scaleY)
                                 : std::min(scaleX, scaleY);
        width = static_cast<int>(std::lround(virtualWidth_ * scale));
        height = static_cast<int>(std::lround(virtualHeight_ * scale));
        x = static_cast<int>(std::lround((physicalWidth - width) * 0.5));
        y = static_cast<int>(std::lround((physicalHeight - height) * 0.5));
    }

    void RlglRenderer::OnSurfaceChanged(const RendererSurfaceInfo& surface)
    {
        surface_.Update(surface);
        int width = 0;
        int height = 0;
        GetPhysicalSize(width, height);
        Bridge::SetFramebufferSize(width, height);
    }

    void RlglRenderer::SetVirtualResolution(const int width, const int height)
    {
        virtualWidth_ = width;
        virtualHeight_ = height;
    }

    void RlglRenderer::SetPresentationMode(const int mode)
    {
        presentationMode_ = static_cast<CnaPresentationMode>(mode);
    }

    void RlglRenderer::SetSwapInterval(const int interval)
    {
        swapInterval_ = interval;
        platformContext_->SetSwapInterval(interval);
    }

    int RlglRenderer::GetAppliedMultiSampleCountEXT(
        const int requestedMultiSampleCount) const
    {
        (void)requestedMultiSampleCount;
        return multiSampleCount_;
    }

    bool RlglRenderer::SupportsDepthStencil() const
    {
        return SupportsDepthBuffer() && SupportsStencilBuffer();
    }

    bool RlglRenderer::SupportsCapability(const CNA::GraphicsCapability capability) const
    {
        // Native GL availability is not a CNA implementation promise. Each row opts in only after
        // its resource/state/draw path and observable behavior have dedicated validation.
        switch (capability)
        {
        case CNA::GraphicsCapability::AnisotropicFiltering:
            return maxSamplerAnisotropy_ > 1.0f;
        default:
            return false;
        }
    }

    void RlglRenderer::ReadBackbuffer(
        const int x, const int y, const int w, const int h, uint8_t* pixels)
    {
        int framebufferWidth = 0;
        int framebufferHeight = 0;
        GetPhysicalSize(framebufferWidth, framebufferHeight);
        (void)framebufferWidth;
        Bridge::ReadBackbuffer(x, y, w, h, framebufferHeight, pixels);
    }

    std::unique_ptr<ITextureRenderer> RlglRenderer::CreateTexture(const ImageData& data)
    {
        return CreateTextureRenderer(data);
    }

    RendererFormatVerdict RlglRenderer::ClassifySurfaceFormatEXT(
        const int surfaceFormat) const
    {
        using Microsoft::Xna::Framework::Graphics::SurfaceFormat;
        switch (static_cast<SurfaceFormat>(surfaceFormat))
        {
        case SurfaceFormat::Color:
        case SurfaceFormat::Bgr565:
        case SurfaceFormat::Bgra5551:
        case SurfaceFormat::Bgra4444:
        case SurfaceFormat::NormalizedByte2:
        case SurfaceFormat::NormalizedByte4:
        case SurfaceFormat::Rgba1010102:
        case SurfaceFormat::Rg32:
        case SurfaceFormat::Rgba64:
        case SurfaceFormat::Alpha8:
        case SurfaceFormat::Single:
        case SurfaceFormat::Vector2:
        case SurfaceFormat::Vector4:
        case SurfaceFormat::HalfSingle:
        case SurfaceFormat::HalfVector2:
        case SurfaceFormat::HalfVector4:
        case SurfaceFormat::HdrBlendable:
            return RendererFormatVerdict::Supported;
        case SurfaceFormat::Dxt1:
        case SurfaceFormat::Dxt3:
        case SurfaceFormat::Dxt5:
            return RendererFormatVerdict::Supported;
        default:
            return RendererFormatVerdict::Unsupported;
        }
    }

    RendererFormatVerdict RlglRenderer::ClassifyColorTransferFormatEXT(
        const int surfaceFormat) const
    {
        using Microsoft::Xna::Framework::Graphics::SurfaceFormat;
        const auto format = static_cast<SurfaceFormat>(surfaceFormat);
        if (format == SurfaceFormat::NormalizedByte2 ||
            format == SurfaceFormat::NormalizedByte4)
        {
            return RendererFormatVerdict::Unsupported;
        }
        return RendererFormatVerdict::Defer;
    }

    bool RlglRenderer::IsCompressedTransferFormatEXT(const int surfaceFormat) const
    {
        using Microsoft::Xna::Framework::Graphics::SurfaceFormat;
        const auto format = static_cast<SurfaceFormat>(surfaceFormat);
        return format == SurfaceFormat::Dxt1 || format == SurfaceFormat::Dxt3 ||
            format == SurfaceFormat::Dxt5;
    }

    bool RlglRenderer::LoadsCompressedContentNativelyEXT() const
    {
        return true;
    }

    std::unique_ptr<ISpriteBatchRenderer> RlglRenderer::CreateSpriteBatch()
    {
        Unsupported("SpriteBatch", "RLGL-010");
    }

    RlglRenderer::SamplerRecord& RlglRenderer::GetSamplerRecord(const int slot)
    {
        if (slot < 0 || slot >= static_cast<int>(samplers_.size()) || slot >= maxSamplerSlots_)
            throw std::out_of_range("RLGL: sampler slot is outside the XNA range");
        SamplerRecord& sampler = samplers_[static_cast<std::size_t>(slot)];
        if (sampler.id == 0) sampler.id = Bridge::CreateSampler();
        return sampler;
    }

    void RlglRenderer::ApplySamplerRecord(const int slot, SamplerRecord& sampler)
    {
        Bridge::ApplySampler(
            sampler.id, slot, sampler.filter,
            sampler.addressU, sampler.addressV, sampler.addressW,
            sampler.maxAnisotropy, sampler.maxMipLevel, sampler.lodBias);
    }

    void RlglRenderer::ApplySamplerState(
        const int slot, const int filter, const int addressU, const int addressV,
        const int maxAnisotropy)
    {
        SamplerRecord& sampler = GetSamplerRecord(slot);
        sampler.filter = filter;
        sampler.addressU = addressU;
        sampler.addressV = addressV;
        sampler.addressW = addressU;
        sampler.maxAnisotropy = maxAnisotropy;
        sampler.maxMipLevel = 0;
        sampler.lodBias = 0.0f;
        ApplySamplerRecord(slot, sampler);
    }

    void RlglRenderer::ApplySamplerMipState(
        const int slot, const int maxMipLevel, const float lodBias)
    {
        SamplerRecord& sampler = GetSamplerRecord(slot);
        sampler.maxMipLevel = maxMipLevel;
        sampler.lodBias = lodBias;
        ApplySamplerRecord(slot, sampler);
    }

    void RlglRenderer::ApplySamplerAddressW(const int slot, const int addressW)
    {
        SamplerRecord& sampler = GetSamplerRecord(slot);
        sampler.addressW = addressW;
        ApplySamplerRecord(slot, sampler);
    }

    void RlglRenderer::SetRenderTargets(
        const RenderTargetBindingDescriptor* renderTargets, const int count)
    {
        (void)renderTargets;
        if (count != 0) Unsupported("render targets", "RLGL-014");
        Bridge::BindDefaultFramebuffer();
    }

    void RlglRenderer::ClearColorAndDepth(
        const float r, const float g, const float b, const float a, const float depth)
    {
        Bridge::Clear(Bridge::ColorPlane | Bridge::DepthPlane, r, g, b, a, depth, 0);
    }

    void RlglRenderer::ClearDepth(const float depth)
    {
        Bridge::Clear(Bridge::DepthPlane, 0.0f, 0.0f, 0.0f, 0.0f, depth, 0);
    }

    void RlglRenderer::ClearStencil(const int stencil)
    {
        Bridge::Clear(Bridge::StencilPlane, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, stencil);
    }

    void RlglRenderer::ClearDepthAndStencil(const float depth, const int stencil)
    {
        Bridge::Clear(
            Bridge::DepthPlane | Bridge::StencilPlane,
            0.0f, 0.0f, 0.0f, 0.0f, depth, stencil);
    }

    void RlglRenderer::ClearColorAndStencil(
        const float r, const float g, const float b, const float a, const int stencil)
    {
        Bridge::Clear(
            Bridge::ColorPlane | Bridge::StencilPlane, r, g, b, a, 1.0f, stencil);
    }

    void RlglRenderer::ClearColorDepthAndStencil(
        const float r, const float g, const float b, const float a,
        const float depth, const int stencil)
    {
        Bridge::Clear(
            Bridge::ColorPlane | Bridge::DepthPlane | Bridge::StencilPlane,
            r, g, b, a, depth, stencil);
    }

    void RlglRenderer::SetDepthTestEnabled(const bool enabled)
    {
        Bridge::SetDepthTestEnabled(enabled);
    }

    void RlglRenderer::SetBlendEnabled(const bool enabled)
    {
        Bridge::SetBlendEnabled(enabled);
    }

    void RlglRenderer::SetDepthWriteEnabled(const bool enabled)
    {
        Bridge::SetDepthWriteEnabled(enabled);
    }

    std::unique_ptr<IVertexBufferRenderer> RlglRenderer::CreateVertexBuffer(
        const int vertexCapacity)
    {
        (void)vertexCapacity;
        Unsupported("vertex buffers", "RLGL-011");
    }

    std::unique_ptr<IIndexBufferRenderer> RlglRenderer::CreateIndexBuffer16(
        const int indexCapacity)
    {
        (void)indexCapacity;
        Unsupported("16-bit index buffers", "RLGL-011");
    }

    std::unique_ptr<IIndexBufferRenderer> RlglRenderer::CreateIndexBuffer32(
        const int indexCapacity)
    {
        (void)indexCapacity;
        Unsupported("32-bit index buffers", "RLGL-011");
    }

    void RlglRenderer::DrawColoredPrimitives(
        const IVertexBufferRenderer& vb, const Matrix& world, const Matrix& view,
        const Matrix& projection, const PrimitiveType primitive, const int primitiveCount)
    {
        (void)vb;
        (void)world;
        (void)view;
        (void)projection;
        (void)primitive;
        (void)primitiveCount;
        Unsupported("non-indexed primitive draws", "RLGL-011");
    }

    void RlglRenderer::DrawIndexedColoredPrimitives(
        const IVertexBufferRenderer& vb, const IIndexBufferRenderer& ib,
        const Matrix& world, const Matrix& view, const Matrix& projection,
        const PrimitiveType primitive, const int primitiveCount)
    {
        (void)vb;
        (void)ib;
        (void)world;
        (void)view;
        (void)projection;
        (void)primitive;
        (void)primitiveCount;
        Unsupported("indexed primitive draws", "RLGL-011");
    }

    void RlglRenderer::SetViewport(
        const int x, const int y, const int w, const int h,
        const float minDepth, const float maxDepth)
    {
        int framebufferWidth = 0;
        int framebufferHeight = 0;
        GetPhysicalSize(framebufferWidth, framebufferHeight);
        (void)framebufferWidth;
        Bridge::SetViewport(x, framebufferHeight - y - h, w, h, minDepth, maxDepth);
    }

    void RlglRenderer::SetScissorRect(
        const int x, const int y, const int w, const int h)
    {
        int framebufferWidth = 0;
        int framebufferHeight = 0;
        GetPhysicalSize(framebufferWidth, framebufferHeight);
        (void)framebufferWidth;
        Bridge::SetScissor(x, framebufferHeight - y - h, w, h);
    }
}

namespace CNA::Internal::Renderers::Rlgl
{
    std::unique_ptr<IGraphicsRenderer> CreateGraphicsRenderer(
        const GraphicsRendererCreateArgs& args)
    {
        return std::make_unique<RlglRenderer>(args);
    }
}
