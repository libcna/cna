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
            maxRenderTargets_ = Bridge::GetMaxRenderTargets();
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
                if (primitivePipeline_)
                {
                    Bridge::DestroyPrimitivePipeline(*primitivePipeline_);
                    primitivePipeline_.reset();
                }
#if defined(CNA_RLGL_COMPILED_EFFECTS)
                if (compiledEffectDrawResources_)
                {
                    Bridge::DestroyCompiledEffectDrawResources(
                        *compiledEffectDrawResources_);
                    compiledEffectDrawResources_.reset();
                }
                DestroyCompiledEffectContext();
#endif
                Bridge::DestroyMrtFramebuffer(mrtFramebuffer_);
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
        case CNA::GraphicsCapability::MultipleRenderTargets:
            return maxRenderTargets_ >= 2;
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

    std::unique_ptr<ITextureCubeRenderer> RlglRenderer::CreateTextureCube(
        const int size, const bool mipMap, const int surfaceFormat)
    {
        if (ClassifyTextureCubeFormatEXT(surfaceFormat) !=
            RendererFormatVerdict::Supported)
        {
            throw System::NotSupportedException(
                "RLGL: requested TextureCube SurfaceFormat is not implemented "
                "(plans/plan_rlgl.md RLGL-045)");
        }
        return CreateTextureCubeRenderer(size, mipMap, surfaceFormat);
    }

    std::unique_ptr<IRenderTargetRenderer> RlglRenderer::CreateRenderTarget2D(
        const int width, const int height, const int depthFormat,
        const bool preserveContents, const bool mipMap,
        const int multiSampleCount)
    {
        return CreateRenderTargetRenderer(
            width, height, depthFormat, preserveContents,
            mipMap, multiSampleCount,
            static_cast<int>(Microsoft::Xna::Framework::Graphics::SurfaceFormat::Color));
    }

    std::unique_ptr<IRenderTargetRenderer> RlglRenderer::CreateRenderTarget2DEXT(
        const int width, const int height, const int depthFormat,
        const bool preserveContents, const bool mipMap,
        const int multiSampleCount, const int surfaceFormat)
    {
        if (ClassifyRenderTargetFormatEXT(surfaceFormat) !=
            RendererFormatVerdict::Supported)
        {
            throw System::NotSupportedException(
                "RLGL: requested RenderTarget2D SurfaceFormat is not renderable on this "
                "OpenGL context (plans/plan_rlgl.md RLGL-042)");
        }
        return CreateRenderTargetRenderer(
            width, height, depthFormat, preserveContents,
            mipMap, multiSampleCount, surfaceFormat);
    }

    std::unique_ptr<IRenderTargetCubeRenderer> RlglRenderer::CreateRenderTargetCube(
        const int size, const int depthFormat, const bool preserveContents,
        const bool mipMap, const int multiSampleCount)
    {
        return CreateRenderTargetCubeRenderer(
            size, depthFormat, preserveContents, mipMap, multiSampleCount,
            static_cast<int>(
                Microsoft::Xna::Framework::Graphics::SurfaceFormat::Color));
    }

    std::unique_ptr<IRenderTargetCubeRenderer> RlglRenderer::CreateRenderTargetCubeEXT(
        const int size, const int depthFormat, const bool preserveContents,
        const bool mipMap, const int multiSampleCount, const int surfaceFormat)
    {
        if (ClassifyRenderTargetCubeFormatEXT(surfaceFormat) !=
            RendererFormatVerdict::Supported)
        {
            throw System::NotSupportedException(
                "RLGL: requested RenderTargetCube SurfaceFormat is not renderable on this "
                "OpenGL context (plans/plan_rlgl.md RLGL-046)");
        }
        return CreateRenderTargetCubeRenderer(
            size, depthFormat, preserveContents,
            mipMap, multiSampleCount, surfaceFormat);
    }

    int RlglRenderer::GetMaxRenderTargetsForProfileEXT(const int graphicsProfile) const
    {
        return graphicsProfile == 1 ? maxRenderTargets_ : 1;
    }

    int RlglRenderer::GetMaxCubeSizeForProfileEXT(const int graphicsProfile) const
    {
        return std::min(maxTextureSize_, graphicsProfile == 1 ? 4096 : 512);
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

    RendererFormatVerdict RlglRenderer::ClassifyTextureCubeFormatEXT(
        const int surfaceFormat) const
    {
        using Microsoft::Xna::Framework::Graphics::SurfaceFormat;
        const auto format = static_cast<SurfaceFormat>(surfaceFormat);
        return format == SurfaceFormat::Color || format == SurfaceFormat::Dxt1 ||
            format == SurfaceFormat::Dxt3 || format == SurfaceFormat::Dxt5
                ? RendererFormatVerdict::Supported
                : RendererFormatVerdict::Unsupported;
    }

    RendererFormatVerdict RlglRenderer::ClassifyRenderTargetFormatEXT(
        const int surfaceFormat) const
    {
        return Bridge::ProbeRenderTargetFormat(surfaceFormat)
            ? RendererFormatVerdict::Supported
            : RendererFormatVerdict::Unsupported;
    }

    RendererFormatVerdict RlglRenderer::ClassifyRenderTargetCubeFormatEXT(
        const int surfaceFormat) const
    {
        return Bridge::ProbeRenderTargetCubeFormat(surfaceFormat)
            ? RendererFormatVerdict::Supported
            : RendererFormatVerdict::Unsupported;
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

    bool RlglRenderer::IsCompressedCubeTransferFormatEXT(const int surfaceFormat) const
    {
        return IsCompressedTransferFormatEXT(surfaceFormat) &&
            ClassifyTextureCubeFormatEXT(surfaceFormat) == RendererFormatVerdict::Supported;
    }

    bool RlglRenderer::LoadsCompressedContentNativelyEXT() const
    {
        return true;
    }

    std::unique_ptr<ISpriteBatchRenderer> RlglRenderer::CreateSpriteBatch()
    {
        return CreateSpriteBatchRenderer(*this);
    }

    void RlglRenderer::GetSpriteBatchViewportSize(int& width, int& height)
    {
        if (currentRenderTargetCount_ > 0)
        {
            width = currentRenderTargetWidth_;
            height = currentRenderTargetHeight_;
            return;
        }
        int logicalWidth = 0;
        int logicalHeight = 0;
        GetLogicalSize(logicalWidth, logicalHeight);
        if (viewportIsDefault_)
        {
            width = logicalWidth;
            height = logicalHeight;
            return;
        }

        int defaultX = 0;
        int defaultY = 0;
        int defaultWidth = 0;
        int defaultHeight = 0;
        GetDefaultViewportRect(
            defaultX, defaultY, defaultWidth, defaultHeight);
        (void)defaultX;
        (void)defaultY;
        if (logicalWidth > 0 && logicalHeight > 0 &&
            defaultWidth > 0 && defaultHeight > 0)
        {
            width = static_cast<int>(std::lround(
                static_cast<double>(currentViewportWidth_) * logicalWidth / defaultWidth));
            height = static_cast<int>(std::lround(
                static_cast<double>(currentViewportHeight_) * logicalHeight / defaultHeight));
        }
        else
        {
            width = currentViewportWidth_;
            height = currentViewportHeight_;
        }
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

    int RlglRenderer::GetCurrentSampleCount() const
    {
        if (currentRenderTargetCount_ <= 0) return multiSampleCount_;
        if (currentRenderTargets_[0] != nullptr)
            return currentRenderTargets_[0]->GetMultiSampleCount();
        if (currentRenderTargetCubes_[0] != nullptr)
            return currentRenderTargetCubes_[0]->GetMultiSampleCount();
        throw std::logic_error("RLGL: active render-target set has no first attachment");
    }

    void RlglRenderer::SetRenderTargets(
        const RenderTargetBindingDescriptor* renderTargets, const int count)
    {
        if (count < 0)
            throw std::invalid_argument("RLGL: render-target count cannot be negative");
        if (count == 0)
        {
            FinalizeCurrentRenderTargets();
            Bridge::DestroyMrtFramebuffer(mrtFramebuffer_);
            Bridge::BindDefaultFramebuffer();
            ApplyCurrentRasterizerState();
            return;
        }
        if (renderTargets == nullptr)
            throw std::invalid_argument(
                "RLGL: nonzero render-target count requires a binding array");

        if (count > maxRenderTargets_)
        {
            throw System::NotSupportedException(
                "RLGL: requested render-target count exceeds the live OpenGL MRT limit");
        }

        std::array<IRenderTargetRenderer*, 4> targets{};
        std::array<IRenderTargetCubeRenderer*, 4> cubeTargets{};
        std::array<int, 4> cubeFaces{{-1, -1, -1, -1}};
        std::array<Bridge::MrtAttachment, 4> attachments{};
        int targetWidth = 0;
        int targetHeight = 0;
        int targetSamples = 0;
        int firstDepthFormat = 0;
        int firstDepthBits = 0;
        for (int slot = 0; slot < count; ++slot)
        {
            if (renderTargets[slot].IsRenderTarget2D())
            {
                targets[slot] = renderTargets[slot].GetRenderTarget2D();
                if (targets[slot] == nullptr)
                    throw std::invalid_argument("RLGL: null RenderTarget2D binding");
                const RenderTargetResourceSnapshot snapshot =
                    GetRenderTargetResourceSnapshotForTesting(*targets[slot]);
                if (snapshot.width != renderTargets[slot].GetWidth() ||
                    snapshot.height != renderTargets[slot].GetHeight() ||
                    snapshot.multiSampleCount !=
                        renderTargets[slot].GetAppliedMultiSampleCount())
                {
                    throw std::invalid_argument(
                        "RLGL: RenderTarget2D descriptor does not match native storage");
                }
                attachments[slot] = {
                    snapshot.colorTexture,
                    snapshot.multisampleColorRenderbuffer,
                    snapshot.depthStencilRenderbuffer,
                    100,
                    snapshot.depthFormat,
                    snapshot.multiSampleCount};
                if (slot == 0)
                {
                    targetWidth = snapshot.width;
                    targetHeight = snapshot.height;
                    targetSamples = snapshot.multiSampleCount;
                    firstDepthFormat = snapshot.depthFormat;
                    firstDepthBits = targets[slot]->DepthBufferBitsEXT();
                }
            }
            else if (renderTargets[slot].IsRenderTargetCubeFace())
            {
                cubeTargets[slot] = renderTargets[slot].GetRenderTargetCube();
                cubeFaces[slot] = renderTargets[slot].GetCubeFace();
                if (cubeTargets[slot] == nullptr ||
                    cubeFaces[slot] < 0 || cubeFaces[slot] >= 6)
                {
                    throw std::invalid_argument("RLGL: invalid RenderTargetCube face binding");
                }
                const RenderTargetCubeResourceSnapshot snapshot =
                    GetRenderTargetCubeResourceSnapshotForTesting(*cubeTargets[slot]);
                if (snapshot.size != renderTargets[slot].GetWidth() ||
                    snapshot.size != renderTargets[slot].GetHeight() ||
                    snapshot.multiSampleCount !=
                        renderTargets[slot].GetAppliedMultiSampleCount())
                {
                    throw std::invalid_argument(
                        "RLGL: RenderTargetCube descriptor does not match native storage");
                }
                attachments[slot] = {
                    snapshot.colorTexture,
                    snapshot.multisampleColorRenderbuffers[
                        static_cast<std::size_t>(cubeFaces[slot])],
                    snapshot.depthStencilRenderbuffer,
                    cubeFaces[slot],
                    snapshot.depthFormat,
                    snapshot.multiSampleCount};
                if (slot == 0)
                {
                    targetWidth = snapshot.size;
                    targetHeight = snapshot.size;
                    targetSamples = snapshot.multiSampleCount;
                    firstDepthFormat = snapshot.depthFormat;
                    firstDepthBits = cubeTargets[slot]->DepthBufferBitsEXT();
                }
            }
            else
            {
                throw std::invalid_argument("RLGL: unknown render-target binding kind");
            }

            if (slot > 0 &&
                (renderTargets[slot].GetWidth() != targetWidth ||
                 renderTargets[slot].GetHeight() != targetHeight ||
                 renderTargets[slot].GetAppliedMultiSampleCount() != targetSamples))
            {
                throw std::invalid_argument(
                    "RLGL: MRT dimensions and applied sample counts must match");
            }
            for (int previous = 0; previous < slot; ++previous)
            {
                const bool duplicate2D = targets[slot] != nullptr &&
                    targets[previous] == targets[slot];
                const bool duplicateCubeFace = cubeTargets[slot] != nullptr &&
                    cubeTargets[previous] == cubeTargets[slot] &&
                    cubeFaces[previous] == cubeFaces[slot];
                if (duplicate2D || duplicateCubeFace)
                    throw std::invalid_argument(
                        "RLGL: one render-target subresource cannot occupy multiple MRT slots");
            }
        }

        if (count == 1)
        {
            const bool sameBinding = currentRenderTargetCount_ == 1 &&
                currentRenderTargets_[0] == targets[0] &&
                currentRenderTargetCubes_[0] == cubeTargets[0] &&
                currentRenderTargetCubeFaces_[0] == cubeFaces[0];
            if (!sameBinding)
            {
                FinalizeCurrentRenderTargets();
                Bridge::DestroyMrtFramebuffer(mrtFramebuffer_);
            }
            if (targets[0] != nullptr) targets[0]->BindAsRenderTarget();
            else cubeTargets[0]->BindAsRenderTargetFace(cubeFaces[0]);
        }
        else
        {
            unsigned int candidate = Bridge::CreateMrtFramebuffer(
                attachments.data(), count);
            try
            {
                FinalizeCurrentRenderTargets();
                Bridge::DestroyMrtFramebuffer(mrtFramebuffer_);
                Bridge::BindFramebuffer(candidate);
                mrtFramebuffer_ = candidate;
                candidate = 0;
            }
            catch (...)
            {
                Bridge::DestroyMrtFramebuffer(candidate);
                throw;
            }
        }

        currentRenderTargets_ = targets;
        currentRenderTargetCubes_ = cubeTargets;
        currentRenderTargetCubeFaces_ = cubeFaces;
        currentRenderTargetCount_ = count;
        currentRenderTargetWidth_ = targetWidth;
        currentRenderTargetHeight_ = targetHeight;
        currentTargetDepthBits_ = firstDepthFormat != 0 ? firstDepthBits : 0;
        ApplyCurrentRasterizerState();
    }

    void RlglRenderer::FinalizeCurrentRenderTargets()
    {
        const auto targets = currentRenderTargets_;
        const auto cubeTargets = currentRenderTargetCubes_;
        const auto cubeFaces = currentRenderTargetCubeFaces_;
        const int count = currentRenderTargetCount_;
        for (int slot = 0; slot < count; ++slot)
        {
            if (targets[slot] != nullptr)
                targets[slot]->UnbindAsRenderTarget();
            else if (cubeTargets[slot] != nullptr)
                FinalizeRenderTargetCubeFace(*cubeTargets[slot], cubeFaces[slot]);
        }
        currentRenderTargets_.fill(nullptr);
        currentRenderTargetCubes_.fill(nullptr);
        currentRenderTargetCubeFaces_.fill(-1);
        currentRenderTargetCount_ = 0;
        currentRenderTargetWidth_ = 0;
        currentRenderTargetHeight_ = 0;
        currentTargetDepthBits_ = depthBits_;
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

    void RlglRenderer::ApplyBlendState(
        const int colorSrcBlend, const int alphaSrcBlend,
        const int colorDstBlend, const int alphaDstBlend,
        const int colorBlendFunc, const int alphaBlendFunc,
        const BlendWriteState& writeState)
    {
        Bridge::ApplyBlendState(
            colorSrcBlend, alphaSrcBlend, colorDstBlend, alphaDstBlend,
            colorBlendFunc, alphaBlendFunc,
            writeState.colorWriteChannels, writeState.multiSampleMask);
    }

    void RlglRenderer::SetBlendFactor(
        const float r, const float g, const float b, const float a)
    {
        Bridge::SetBlendFactor(r, g, b, a);
    }

    void RlglRenderer::ApplyDepthStencilState(
        const bool depthEnable, const bool depthWriteEnable, const int depthFunc,
        const bool stencilEnable, const int stencilFunc,
        const int stencilPass, const int stencilFail, const int stencilDepthFail,
        const int stencilMask, const int stencilWriteMask, const int referenceStencil,
        const bool twoSidedStencilMode, const int ccwStencilFunc,
        const int ccwStencilPass, const int ccwStencilFail,
        const int ccwStencilDepthFail)
    {
        stencil_.enabled = stencilEnable;
        stencil_.twoSided = twoSidedStencilMode;
        stencil_.function = stencilFunc;
        stencil_.counterClockwiseFunction = ccwStencilFunc;
        stencil_.readMask = stencilMask;
        stencil_.reference = referenceStencil;
        Bridge::ApplyDepthStencilState(
            depthEnable, depthWriteEnable, depthFunc,
            stencilEnable, stencilFunc, stencilPass, stencilFail, stencilDepthFail,
            stencilMask, stencilWriteMask, referenceStencil, twoSidedStencilMode,
            ccwStencilFunc, ccwStencilPass, ccwStencilFail, ccwStencilDepthFail);
    }

    void RlglRenderer::SetReferenceStencil(const int value)
    {
        stencil_.reference = value;
        Bridge::SetStencilReference(
            stencil_.enabled, stencil_.twoSided,
            stencil_.function, stencil_.counterClockwiseFunction,
            stencil_.readMask, stencil_.reference);
    }

    void RlglRenderer::ApplyRasterizerState(
        const int cullMode, const int fillMode, const bool scissorTestEnable,
        const float depthBias, const float slopeScaleDepthBias)
    {
        rasterizerCullMode_ = cullMode;
        rasterizerFillMode_ = fillMode;
        rasterizerScissorTestEnabled_ = scissorTestEnable;
        rasterizerDepthBias_ = depthBias;
        rasterizerSlopeScaleDepthBias_ = slopeScaleDepthBias;
        ApplyCurrentRasterizerState();
    }

    void RlglRenderer::ApplyCurrentRasterizerState()
    {
        const int appliedDepthBits = currentRenderTargetCount_ > 0
            ? currentTargetDepthBits_ : depthBits_;
        const float depthScale = appliedDepthBits > 0
            ? std::ldexp(1.0f, appliedDepthBits) - 1.0f
            : 0.0f;
        Bridge::ApplyRasterizerState(
            rasterizerCullMode_, rasterizerFillMode_, rasterizerScissorTestEnabled_,
            rasterizerDepthBias_ * depthScale, rasterizerSlopeScaleDepthBias_);
    }

    std::unique_ptr<IVertexBufferRenderer> RlglRenderer::CreateVertexBuffer(
        const int vertexCapacity)
    {
        return CreateVertexBufferRenderer(vertexCapacity);
    }

    std::unique_ptr<IIndexBufferRenderer> RlglRenderer::CreateIndexBuffer16(
        const int indexCapacity)
    {
        return CreateIndexBufferRenderer(indexCapacity, false);
    }

    std::unique_ptr<IIndexBufferRenderer> RlglRenderer::CreateIndexBuffer32(
        const int indexCapacity)
    {
        return CreateIndexBufferRenderer(indexCapacity, true);
    }

    void RlglRenderer::SetViewport(
        const int x, const int y, const int w, const int h,
        const float minDepth, const float maxDepth)
    {
        int framebufferHeight = currentRenderTargetCount_ > 0
            ? currentRenderTargetHeight_ : 0;
        if (currentRenderTargetCount_ == 0)
        {
            int framebufferWidth = 0;
            GetPhysicalSize(framebufferWidth, framebufferHeight);
            (void)framebufferWidth;
        }
        currentViewportWidth_ = w;
        currentViewportHeight_ = h;
        int defaultX = 0;
        int defaultY = 0;
        int defaultWidth = 0;
        int defaultHeight = 0;
        GetDefaultViewportRect(
            defaultX, defaultY, defaultWidth, defaultHeight);
        viewportIsDefault_ = x == defaultX && y == defaultY &&
            w == defaultWidth && h == defaultHeight;
        Bridge::SetViewport(x, framebufferHeight - y - h, w, h, minDepth, maxDepth);
    }

    void RlglRenderer::SetScissorRect(
        const int x, const int y, const int w, const int h)
    {
        int framebufferHeight = currentRenderTargetCount_ > 0
            ? currentRenderTargetHeight_ : 0;
        if (currentRenderTargetCount_ == 0)
        {
            int framebufferWidth = 0;
            GetPhysicalSize(framebufferWidth, framebufferHeight);
            (void)framebufferWidth;
        }
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
