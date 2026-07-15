// SPDX-License-Identifier: MS-PL
#include "CNA/Internal/Backends/SdlGpu/SdlGpuGraphicsBackend.hpp"

#include "CNA/Logger.hpp"
#include "CNA/LogCategory.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <string>

namespace CNA::Internal::Backends::SdlGpu
{
    SdlGpuGraphicsBackend::SdlGpuGraphicsBackend(SDL_Window* window, int virtualWidth, int virtualHeight,
                                                  CnaPresentationMode presentationMode, int swapInterval)
        : window_(window),
          virtualWidth_(virtualWidth),
          virtualHeight_(virtualHeight),
          presentationMode_(presentationMode)
    {
        if (window_ == nullptr)
            throw std::invalid_argument("CNA SDL_GPU: SDL window cannot be null");

        // plan_sdlgpu.md SDLGPU-6: request SPIR-V first -- the only shader format this device's
        // vendored SDL3 compiles a driver for on Linux (Vulkan). DXBC/DXIL/MSL support (Windows/
        // macOS drivers) is deferred to plan_sdlgpu.md's Phase SDLGPU-13.
        device_ = SDL_CreateGPUDevice(SDL_GPU_SHADERFORMAT_SPIRV, /*debug_mode=*/false, /*name=*/nullptr);
        if (device_ == nullptr)
            throw std::runtime_error(std::string("CNA SDL_GPU: SDL_CreateGPUDevice failed: ") + SDL_GetError());

        if (!SDL_ClaimWindowForGPUDevice(device_, window_))
        {
            const std::string error = SDL_GetError();
            SDL_DestroyGPUDevice(device_);
            device_ = nullptr;
            throw std::runtime_error("CNA SDL_GPU: SDL_ClaimWindowForGPUDevice failed: " + error);
        }

        SetSwapInterval(swapInterval);

        int w = 0;
        int h = 0;
        SDL_GetWindowSizeInPixels(window_, &w, &h);
        physicalWidth_ = w;
        physicalHeight_ = h;

        IGraphicsBackend::RegisterForWindow(window_, this);
    }

    SdlGpuGraphicsBackend::~SdlGpuGraphicsBackend()
    {
        IGraphicsBackend::UnregisterForWindow(window_);
        if (depthStencilTexture_ != nullptr)
            SDL_ReleaseGPUTexture(device_, depthStencilTexture_);
        if (device_ != nullptr)
        {
            SDL_ReleaseWindowFromGPUDevice(device_, window_);
            SDL_DestroyGPUDevice(device_);
        }
    }

    void SdlGpuGraphicsBackend::EnsureDepthStencilTexture(Uint32 width, Uint32 height)
    {
        if (width == 0 || height == 0)
            return;

        if (depthStencilFormat_ == SDL_GPU_TEXTUREFORMAT_INVALID)
        {
            // plan_sdlgpu.md: SDL_gpu guarantees at most one of D24_UNORM_S8_UINT/D32_FLOAT_S8_UINT
            // per device -- must query, never assume either is available.
            if (SDL_GPUTextureSupportsFormat(device_, SDL_GPU_TEXTUREFORMAT_D24_UNORM_S8_UINT,
                                              SDL_GPU_TEXTURETYPE_2D,
                                              SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET))
            {
                depthStencilFormat_ = SDL_GPU_TEXTUREFORMAT_D24_UNORM_S8_UINT;
            }
            else if (SDL_GPUTextureSupportsFormat(device_, SDL_GPU_TEXTUREFORMAT_D32_FLOAT_S8_UINT,
                                                   SDL_GPU_TEXTURETYPE_2D,
                                                   SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET))
            {
                depthStencilFormat_ = SDL_GPU_TEXTUREFORMAT_D32_FLOAT_S8_UINT;
            }
            else
            {
                // Genuine SDL_gpu/device capability gap, not a "not implemented yet" stub -- warn
                // and keep running with no depth/stencil attachment rather than throw.
                CNA::Logger::Warn(
                    "CNA SDL_GPU: no combined depth+stencil texture format is supported by this "
                    "device; depth/stencil clearing and depth-tested draws will have no effect.",
                    CNA::LogCategory::GPU);
                return;
            }
        }

        if (depthStencilTexture_ != nullptr && depthStencilWidth_ == width && depthStencilHeight_ == height)
            return;

        if (depthStencilTexture_ != nullptr)
        {
            SDL_ReleaseGPUTexture(device_, depthStencilTexture_);
            depthStencilTexture_ = nullptr;
        }

        SDL_GPUTextureCreateInfo createInfo{};
        createInfo.type = SDL_GPU_TEXTURETYPE_2D;
        createInfo.format = depthStencilFormat_;
        createInfo.usage = SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET;
        createInfo.width = width;
        createInfo.height = height;
        createInfo.layer_count_or_depth = 1;
        createInfo.num_levels = 1;
        createInfo.sample_count = SDL_GPU_SAMPLECOUNT_1;

        depthStencilTexture_ = SDL_CreateGPUTexture(device_, &createInfo);
        if (depthStencilTexture_ == nullptr)
        {
            CNA::Logger::Warn(
                std::string("CNA SDL_GPU: failed to create depth/stencil texture: ") + SDL_GetError(),
                CNA::LogCategory::GPU);
            depthStencilWidth_ = 0;
            depthStencilHeight_ = 0;
            return;
        }
        depthStencilWidth_ = width;
        depthStencilHeight_ = height;
    }

    bool SdlGpuGraphicsBackend::EnsureFrameRendered()
    {
        if (!framePending_)
            return true;

        SDL_GPUCommandBuffer* cmd = SDL_AcquireGPUCommandBuffer(device_);
        if (cmd == nullptr)
            throw std::runtime_error(std::string("CNA SDL_GPU: SDL_AcquireGPUCommandBuffer failed: ") + SDL_GetError());

        SDL_GPUTexture* swapchainTexture = nullptr;
        Uint32 swapchainWidth = 0;
        Uint32 swapchainHeight = 0;
        const bool acquired = SDL_WaitAndAcquireGPUSwapchainTexture(
            cmd, window_, &swapchainTexture, &swapchainWidth, &swapchainHeight);
        if (!acquired)
        {
            // Per SDL_gpu.h: it is an error to cancel a command buffer once
            // SDL_WaitAndAcquireGPUSwapchainTexture has been called on it -- must always submit.
            const std::string error = SDL_GetError();
            SDL_SubmitGPUCommandBuffer(cmd);
            throw std::runtime_error("CNA SDL_GPU: SDL_WaitAndAcquireGPUSwapchainTexture failed: " + error);
        }

        if (swapchainTexture == nullptr)
        {
            // Documented, non-error case (e.g. a minimized window) -- still must submit.
            SDL_SubmitGPUCommandBuffer(cmd);
            return false;
        }

        physicalWidth_ = static_cast<int>(swapchainWidth);
        physicalHeight_ = static_cast<int>(swapchainHeight);
        EnsureDepthStencilTexture(swapchainWidth, swapchainHeight);

        SDL_GPUColorTargetInfo colorTarget{};
        colorTarget.texture = swapchainTexture;
        colorTarget.clear_color = clearColor_;
        colorTarget.load_op = clearColorPending_ ? SDL_GPU_LOADOP_CLEAR : SDL_GPU_LOADOP_LOAD;
        colorTarget.store_op = SDL_GPU_STOREOP_STORE;

        SDL_GPUDepthStencilTargetInfo depthStencilTarget{};
        if (depthStencilTexture_ != nullptr)
        {
            depthStencilTarget.texture = depthStencilTexture_;
            depthStencilTarget.clear_depth = clearDepth_;
            depthStencilTarget.load_op = clearDepthPending_ ? SDL_GPU_LOADOP_CLEAR : SDL_GPU_LOADOP_LOAD;
            depthStencilTarget.store_op = SDL_GPU_STOREOP_STORE;
            depthStencilTarget.clear_stencil = clearStencil_;
            depthStencilTarget.stencil_load_op = clearStencilPending_ ? SDL_GPU_LOADOP_CLEAR : SDL_GPU_LOADOP_LOAD;
            depthStencilTarget.stencil_store_op = SDL_GPU_STOREOP_STORE;
        }

        SDL_GPURenderPass* pass = SDL_BeginGPURenderPass(
            cmd, &colorTarget, 1, depthStencilTexture_ != nullptr ? &depthStencilTarget : nullptr);
        // No draw calls yet -- 2D/3D draw dispatch lands in later phases (plan_sdlgpu.md Phase
        // SDLGPU-5 onward).
        SDL_EndGPURenderPass(pass);

        if (!SDL_SubmitGPUCommandBuffer(cmd))
            throw std::runtime_error(std::string("CNA SDL_GPU: SDL_SubmitGPUCommandBuffer failed: ") + SDL_GetError());

        clearColorPending_ = false;
        clearDepthPending_ = false;
        clearStencilPending_ = false;
        framePending_ = false;
        return true;
    }

    void SdlGpuGraphicsBackend::Clear(float r, float g, float b, float a)
    {
        clearColor_ = SDL_FColor{r, g, b, a};
        clearColorPending_ = true;
        framePending_ = true;
    }

    void SdlGpuGraphicsBackend::ClearColorAndDepth(float r, float g, float b, float a, float depth)
    {
        Clear(r, g, b, a);
        ClearDepth(depth);
    }

    void SdlGpuGraphicsBackend::ClearDepth(float depth)
    {
        clearDepth_ = depth;
        clearDepthPending_ = true;
        framePending_ = true;
    }

    void SdlGpuGraphicsBackend::ClearStencil(int stencil)
    {
        clearStencil_ = static_cast<Uint8>(stencil);
        clearStencilPending_ = true;
        framePending_ = true;
    }

    void SdlGpuGraphicsBackend::ClearDepthAndStencil(float depth, int stencil)
    {
        ClearDepth(depth);
        ClearStencil(stencil);
    }

    void SdlGpuGraphicsBackend::ClearColorAndStencil(float r, float g, float b, float a, int stencil)
    {
        Clear(r, g, b, a);
        ClearStencil(stencil);
    }

    void SdlGpuGraphicsBackend::ClearColorDepthAndStencil(float r, float g, float b, float a, float depth, int stencil)
    {
        Clear(r, g, b, a);
        ClearDepth(depth);
        ClearStencil(stencil);
    }

    void SdlGpuGraphicsBackend::Present()
    {
        // SDL_gpu automatically presents the acquired swapchain texture once the command buffer
        // that acquired it is submitted -- there is no separate explicit present call.
        EnsureFrameRendered();
    }

    SdlGpuGraphicsBackend::LogicalViewport SdlGpuGraphicsBackend::ComputeLogicalViewport() const
    {
        LogicalViewport viewport{};
        viewport.width = static_cast<float>(std::max(0, physicalWidth_));
        viewport.height = static_cast<float>(std::max(0, physicalHeight_));
        viewport.logicalWidth = viewport.width;
        viewport.logicalHeight = viewport.height;
        if (physicalWidth_ <= 0 || physicalHeight_ <= 0)
            return viewport;
        if (presentationMode_ == CnaPresentationMode::NativeBackBuffer || virtualWidth_ <= 0 || virtualHeight_ <= 0)
            return viewport;

        float logicalWidth = static_cast<float>(virtualWidth_);
        float logicalHeight = static_cast<float>(virtualHeight_);
        if (presentationMode_ == CnaPresentationMode::FixedHeightDynamicWidth)
        {
            logicalHeight = static_cast<float>(virtualHeight_);
            logicalWidth = logicalHeight * static_cast<float>(physicalWidth_) / static_cast<float>(physicalHeight_);
            viewport.logicalWidth = logicalWidth;
            viewport.logicalHeight = logicalHeight;
            return viewport;
        }

        viewport.logicalWidth = logicalWidth;
        viewport.logicalHeight = logicalHeight;
        if (presentationMode_ == CnaPresentationMode::Stretch)
            return viewport;
        const float sx = static_cast<float>(physicalWidth_) / logicalWidth;
        const float sy = static_cast<float>(physicalHeight_) / logicalHeight;
        const float scale = presentationMode_ == CnaPresentationMode::Overscan ? std::max(sx, sy) : std::min(sx, sy);
        viewport.width = logicalWidth * scale;
        viewport.height = logicalHeight * scale;
        viewport.x = (static_cast<float>(physicalWidth_) - viewport.width) * 0.5f;
        viewport.y = (static_cast<float>(physicalHeight_) - viewport.height) * 0.5f;
        return viewport;
    }

    void SdlGpuGraphicsBackend::GetViewportSize(int& width, int& height)
    {
        const LogicalViewport viewport = ComputeLogicalViewport();
        width = static_cast<int>(std::lround(viewport.logicalWidth));
        height = static_cast<int>(std::lround(viewport.logicalHeight));
    }

    void SdlGpuGraphicsBackend::SetVirtualResolution(int width, int height)
    {
        virtualWidth_ = width;
        virtualHeight_ = height;
    }

    void SdlGpuGraphicsBackend::SetPresentationMode(int mode)
    {
        if (mode < static_cast<int>(CnaPresentationMode::Letterbox) ||
            mode > static_cast<int>(CnaPresentationMode::FixedHeightDynamicWidth))
            throw std::out_of_range("CNA SDL_GPU: invalid presentation mode");
        presentationMode_ = static_cast<CnaPresentationMode>(mode);
    }

    void SdlGpuGraphicsBackend::SetSwapInterval(int interval)
    {
        interval = std::max(0, interval);
        swapInterval_ = interval;

        // plan_sdlgpu.md: SDL_gpu has no "half-rate" present mode -- XNA's PresentInterval.Two
        // (swapInterval==2) falls back to plain VSYNC, same as swapInterval==1.
        SDL_GPUPresentMode presentMode = SDL_GPU_PRESENTMODE_VSYNC;
        if (interval == 0)
        {
            if (SDL_WindowSupportsGPUPresentMode(device_, window_, SDL_GPU_PRESENTMODE_IMMEDIATE))
                presentMode = SDL_GPU_PRESENTMODE_IMMEDIATE;
            else if (SDL_WindowSupportsGPUPresentMode(device_, window_, SDL_GPU_PRESENTMODE_MAILBOX))
                presentMode = SDL_GPU_PRESENTMODE_MAILBOX;
        }

        if (!SDL_SetGPUSwapchainParameters(device_, window_, SDL_GPU_SWAPCHAINCOMPOSITION_SDR, presentMode))
        {
            // Genuine per-device/driver capability gap, not a "not implemented yet" stub.
            CNA::Logger::Warn(
                std::string("CNA SDL_GPU: SDL_SetGPUSwapchainParameters failed: ") + SDL_GetError(),
                CNA::LogCategory::GPU);
        }
    }

    bool SdlGpuGraphicsBackend::TransformWindowToLogical(float windowX, float windowY, float& logicalX, float& logicalY) const
    {
        const LogicalViewport viewport = ComputeLogicalViewport();
        if (viewport.width == 0.0f || viewport.height == 0.0f)
            return false;
        logicalX = (windowX - viewport.x) * viewport.logicalWidth / viewport.width;
        logicalY = (windowY - viewport.y) * viewport.logicalHeight / viewport.height;
        return windowX >= viewport.x && windowX < viewport.x + viewport.width &&
               windowY >= viewport.y && windowY < viewport.y + viewport.height;
    }

    bool SdlGpuGraphicsBackend::TransformLogicalToWindow(float logicalX, float logicalY, float& windowX, float& windowY) const
    {
        const LogicalViewport viewport = ComputeLogicalViewport();
        if (viewport.logicalWidth == 0.0f || viewport.logicalHeight == 0.0f)
            return false;
        windowX = viewport.x + logicalX * viewport.width / viewport.logicalWidth;
        windowY = viewport.y + logicalY * viewport.height / viewport.logicalHeight;
        return true;
    }

    void SdlGpuGraphicsBackend::ThrowNotImplemented(const char* method)
    {
        throw std::runtime_error(
            std::string("CNA SDL_GPU: ") + method + " is not implemented yet (see plan_sdlgpu.md)");
    }

    std::unique_ptr<ITextureBackend> SdlGpuGraphicsBackend::CreateTexture(const ImageData& /*data*/)
    {
        ThrowNotImplemented("CreateTexture");
    }

    std::unique_ptr<ISpriteBatchBackend> SdlGpuGraphicsBackend::CreateSpriteBatch()
    {
        ThrowNotImplemented("CreateSpriteBatch");
    }

    std::unique_ptr<IVertexBufferBackend> SdlGpuGraphicsBackend::CreateVertexBuffer(int /*vertex_capacity*/)
    {
        ThrowNotImplemented("CreateVertexBuffer");
    }

    std::unique_ptr<IIndexBufferBackend> SdlGpuGraphicsBackend::CreateIndexBuffer16(int /*index_capacity*/)
    {
        ThrowNotImplemented("CreateIndexBuffer16");
    }

    void SdlGpuGraphicsBackend::DrawColoredPrimitives(const IVertexBufferBackend& /*vb*/,
                                                       const Matrix& /*world*/, const Matrix& /*view*/,
                                                       const Matrix& /*projection*/,
                                                       PrimitiveType /*primitive*/, int /*primitiveCount*/)
    {
        ThrowNotImplemented("DrawColoredPrimitives");
    }

    void SdlGpuGraphicsBackend::DrawIndexedColoredPrimitives(const IVertexBufferBackend& /*vb*/,
                                                              const IIndexBufferBackend& /*ib*/,
                                                              const Matrix& /*world*/, const Matrix& /*view*/,
                                                              const Matrix& /*projection*/,
                                                              PrimitiveType /*primitive*/, int /*primitiveCount*/)
    {
        ThrowNotImplemented("DrawIndexedColoredPrimitives");
    }
}

namespace CNA::Internal::Backends
{
    std::unique_ptr<IGraphicsBackend> CreateGraphicsBackend(const GraphicsBackendCreateArgs& args)
    {
        return std::make_unique<SdlGpu::SdlGpuGraphicsBackend>(
            args.window, args.virtualWidth, args.virtualHeight, args.presentationMode, args.swapInterval);
    }
}
