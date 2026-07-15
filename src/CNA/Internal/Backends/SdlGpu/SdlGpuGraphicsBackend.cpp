// SPDX-License-Identifier: MS-PL
#include "CNA/Internal/Backends/SdlGpu/SdlGpuGraphicsBackend.hpp"

#include "CNA/Logger.hpp"
#include "CNA/LogCategory.hpp"
#include "CNA/Internal/Backends/SdlGpu/shaders/spirv_shaders.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <stdexcept>
#include <string>

namespace CNA::Internal::Backends::SdlGpu
{
    namespace
    {
        // Mirrors WebGPUGraphicsBackend::SamplerCacheIndex's exact indexing scheme so both
        // backends' sampler caches read the same way: filterIndex*9 + u*3 + v, 18 entries total.
        [[nodiscard]] int SamplerCacheIndex(int filter, int addressU, int addressV)
        {
            const int filterIndex = filter == 0 ? 0 : 1;
            const int u = std::clamp(addressU, 0, 2);
            const int v = std::clamp(addressV, 0, 2);
            return filterIndex * 9 + u * 3 + v;
        }

        [[nodiscard]] SDL_GPUSamplerAddressMode ToAddressMode(int mode)
        {
            switch (mode)
            {
                case 0: return SDL_GPU_SAMPLERADDRESSMODE_REPEAT;
                case 2: return SDL_GPU_SAMPLERADDRESSMODE_MIRRORED_REPEAT;
                default: return SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
            }
        }
    }

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
        QueryDepthStencilFormat();
        CreateSpriteResources();

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
        DestroySpriteResources();
        if (depthStencilTexture_ != nullptr)
            SDL_ReleaseGPUTexture(device_, depthStencilTexture_);
        if (device_ != nullptr)
        {
            SDL_ReleaseWindowFromGPUDevice(device_, window_);
            SDL_DestroyGPUDevice(device_);
        }
    }

    void SdlGpuGraphicsBackend::QueryDepthStencilFormat()
    {
        // plan_sdlgpu.md: SDL_gpu guarantees at most one of D24_UNORM_S8_UINT/D32_FLOAT_S8_UINT
        // per device -- must query, never assume either is available. Queried once here (not
        // lazily inside EnsureDepthStencilTexture) so pipeline creation has a stable answer for
        // SDL_GPUGraphicsPipelineTargetInfo before any frame has actually rendered.
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
        }
    }

    void SdlGpuGraphicsBackend::EnsureDepthStencilTexture(Uint32 width, Uint32 height)
    {
        if (width == 0 || height == 0 || depthStencilFormat_ == SDL_GPU_TEXTUREFORMAT_INVALID)
            return;

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

        // Sprite vertex data must be uploaded via a copy pass BEFORE BeginGPURenderPass --
        // SDL_gpu forbids a copy pass nested inside a render pass.
        UploadSpriteVertexData(cmd);

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
        RenderSprites(pass, cmd);
        // 3D draw dispatch lands in later phases (plan_sdlgpu.md Phase SDLGPU-6 onward).
        SDL_EndGPURenderPass(pass);

        if (!SDL_SubmitGPUCommandBuffer(cmd))
            throw std::runtime_error(std::string("CNA SDL_GPU: SDL_SubmitGPUCommandBuffer failed: ") + SDL_GetError());

        spriteCommands_.clear();
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

    std::unique_ptr<ITextureBackend> SdlGpuGraphicsBackend::CreateTexture(const ImageData& data)
    {
        return std::make_unique<SdlGpuTextureBackend>(*this, data);
    }

    std::unique_ptr<ISpriteBatchBackend> SdlGpuGraphicsBackend::CreateSpriteBatch()
    {
        return std::make_unique<SdlGpuSpriteBatchBackend>(*this);
    }

    std::unique_ptr<IVertexBufferBackend> SdlGpuGraphicsBackend::CreateVertexBuffer(int vertex_capacity)
    {
        return std::make_unique<SdlGpuVertexBufferBackend>(*this, vertex_capacity);
    }

    std::unique_ptr<IIndexBufferBackend> SdlGpuGraphicsBackend::CreateIndexBuffer16(int index_capacity)
    {
        return std::make_unique<SdlGpuIndexBufferBackend>(*this, index_capacity, false);
    }

    void SdlGpuGraphicsBackend::CreateSpriteResources()
    {
        if (spriteVertexShader_ != nullptr)
            return;

        SDL_GPUShaderCreateInfo vsInfo{};
        vsInfo.code = reinterpret_cast<const Uint8*>(Shaders::kSprite2dVertSpv);
        vsInfo.code_size = Shaders::kSprite2dVertSpv_size;
        vsInfo.entrypoint = "main";
        vsInfo.format = SDL_GPU_SHADERFORMAT_SPIRV;
        vsInfo.stage = SDL_GPU_SHADERSTAGE_VERTEX;
        vsInfo.num_uniform_buffers = 1;
        spriteVertexShader_ = SDL_CreateGPUShader(device_, &vsInfo);
        if (spriteVertexShader_ == nullptr)
            throw std::runtime_error(std::string("CNA SDL_GPU: failed to create sprite vertex shader: ") + SDL_GetError());

        SDL_GPUShaderCreateInfo fsInfo{};
        fsInfo.code = reinterpret_cast<const Uint8*>(Shaders::kSprite2dFragSpv);
        fsInfo.code_size = Shaders::kSprite2dFragSpv_size;
        fsInfo.entrypoint = "main";
        fsInfo.format = SDL_GPU_SHADERFORMAT_SPIRV;
        fsInfo.stage = SDL_GPU_SHADERSTAGE_FRAGMENT;
        fsInfo.num_samplers = 1;
        spriteFragmentShader_ = SDL_CreateGPUShader(device_, &fsInfo);
        if (spriteFragmentShader_ == nullptr)
        {
            SDL_ReleaseGPUShader(device_, spriteVertexShader_);
            spriteVertexShader_ = nullptr;
            throw std::runtime_error(std::string("CNA SDL_GPU: failed to create sprite fragment shader: ") + SDL_GetError());
        }
    }

    void SdlGpuGraphicsBackend::DestroySpriteResources()
    {
        if (spritePipeline_ != nullptr)
        {
            SDL_ReleaseGPUGraphicsPipeline(device_, spritePipeline_);
            spritePipeline_ = nullptr;
        }
        for (SDL_GPUSampler*& sampler : samplerCache_)
        {
            if (sampler != nullptr)
                SDL_ReleaseGPUSampler(device_, sampler);
            sampler = nullptr;
        }
        if (spriteVertexBuffer_ != nullptr)
        {
            SDL_ReleaseGPUBuffer(device_, spriteVertexBuffer_);
            spriteVertexBuffer_ = nullptr;
        }
        if (spriteFragmentShader_ != nullptr)
        {
            SDL_ReleaseGPUShader(device_, spriteFragmentShader_);
            spriteFragmentShader_ = nullptr;
        }
        if (spriteVertexShader_ != nullptr)
        {
            SDL_ReleaseGPUShader(device_, spriteVertexShader_);
            spriteVertexShader_ = nullptr;
        }
    }

    SDL_GPUGraphicsPipeline* SdlGpuGraphicsBackend::GetOrCreateSpritePipeline()
    {
        if (spritePipeline_ != nullptr)
            return spritePipeline_;

        SDL_GPUVertexBufferDescription vbDesc{};
        vbDesc.slot = 0;
        vbDesc.pitch = sizeof(SpriteVertex);
        vbDesc.input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX;

        SDL_GPUVertexAttribute attrs[3]{};
        attrs[0].location = 0;
        attrs[0].buffer_slot = 0;
        attrs[0].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2;
        attrs[0].offset = offsetof(SpriteVertex, x);
        attrs[1].location = 1;
        attrs[1].buffer_slot = 0;
        attrs[1].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2;
        attrs[1].offset = offsetof(SpriteVertex, u);
        attrs[2].location = 2;
        attrs[2].buffer_slot = 0;
        attrs[2].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4;
        attrs[2].offset = offsetof(SpriteVertex, r);

        // Standard (non-premultiplied) alpha blend: src*srcAlpha + dst*(1-srcAlpha).
        SDL_GPUColorTargetDescription colorTarget{};
        colorTarget.format = SDL_GetGPUSwapchainTextureFormat(device_, window_);
        colorTarget.blend_state.enable_blend = true;
        colorTarget.blend_state.src_color_blendfactor = SDL_GPU_BLENDFACTOR_SRC_ALPHA;
        colorTarget.blend_state.dst_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
        colorTarget.blend_state.color_blend_op = SDL_GPU_BLENDOP_ADD;
        colorTarget.blend_state.src_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
        colorTarget.blend_state.dst_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
        colorTarget.blend_state.alpha_blend_op = SDL_GPU_BLENDOP_ADD;

        SDL_GPUGraphicsPipelineCreateInfo pipelineInfo{};
        pipelineInfo.vertex_shader = spriteVertexShader_;
        pipelineInfo.fragment_shader = spriteFragmentShader_;
        pipelineInfo.vertex_input_state.vertex_buffer_descriptions = &vbDesc;
        pipelineInfo.vertex_input_state.num_vertex_buffers = 1;
        pipelineInfo.vertex_input_state.vertex_attributes = attrs;
        pipelineInfo.vertex_input_state.num_vertex_attributes = 3;
        pipelineInfo.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
        pipelineInfo.rasterizer_state.fill_mode = SDL_GPU_FILLMODE_FILL;
        pipelineInfo.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_NONE;
        pipelineInfo.rasterizer_state.front_face = SDL_GPU_FRONTFACE_COUNTER_CLOCKWISE;
        pipelineInfo.multisample_state.sample_count = SDL_GPU_SAMPLECOUNT_1;
        // No depth test/write for 2D sprites, but the pipeline must still declare a compatible
        // depth-stencil target format when one is present in the render passes it's used in
        // (this backend's render pass always includes one once depthStencilFormat_ is known --
        // see EnsureFrameRendered/EnsureDepthStencilTexture).
        pipelineInfo.depth_stencil_state.enable_depth_test = false;
        pipelineInfo.depth_stencil_state.enable_depth_write = false;
        pipelineInfo.depth_stencil_state.enable_stencil_test = false;
        pipelineInfo.target_info.color_target_descriptions = &colorTarget;
        pipelineInfo.target_info.num_color_targets = 1;
        pipelineInfo.target_info.has_depth_stencil_target = (depthStencilFormat_ != SDL_GPU_TEXTUREFORMAT_INVALID);
        pipelineInfo.target_info.depth_stencil_format = depthStencilFormat_;

        spritePipeline_ = SDL_CreateGPUGraphicsPipeline(device_, &pipelineInfo);
        if (spritePipeline_ == nullptr)
            throw std::runtime_error(std::string("CNA SDL_GPU: failed to create sprite pipeline: ") + SDL_GetError());
        return spritePipeline_;
    }

    SDL_GPUSampler* SdlGpuGraphicsBackend::GetOrCreateSampler(int textureFilter, int addressU, int addressV)
    {
        const int index = SamplerCacheIndex(textureFilter, addressU, addressV);
        if (samplerCache_[index] != nullptr)
            return samplerCache_[index];

        const SDL_GPUFilter filter = textureFilter == 0 ? SDL_GPU_FILTER_LINEAR : SDL_GPU_FILTER_NEAREST;
        SDL_GPUSamplerCreateInfo createInfo{};
        createInfo.min_filter = filter;
        createInfo.mag_filter = filter;
        createInfo.mipmap_mode = textureFilter == 0 ? SDL_GPU_SAMPLERMIPMAPMODE_LINEAR : SDL_GPU_SAMPLERMIPMAPMODE_NEAREST;
        createInfo.address_mode_u = ToAddressMode(addressU);
        createInfo.address_mode_v = ToAddressMode(addressV);
        createInfo.address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
        createInfo.max_lod = 32.0f;

        samplerCache_[index] = SDL_CreateGPUSampler(device_, &createInfo);
        if (samplerCache_[index] == nullptr)
            throw std::runtime_error(std::string("CNA SDL_GPU: failed to create sampler: ") + SDL_GetError());
        return samplerCache_[index];
    }

    void SdlGpuGraphicsBackend::UploadSpriteVertexData(SDL_GPUCommandBuffer* cmd)
    {
        if (spriteCommands_.empty())
            return;

        const Uint32 requiredBytes = static_cast<Uint32>(spriteCommands_.size() * sizeof(SpriteVertex) * 6);
        if (spriteVertexBuffer_ == nullptr || spriteVertexCapacityBytes_ < requiredBytes)
        {
            if (spriteVertexBuffer_ != nullptr)
                SDL_ReleaseGPUBuffer(device_, spriteVertexBuffer_);
            SDL_GPUBufferCreateInfo bufferInfo{};
            bufferInfo.usage = SDL_GPU_BUFFERUSAGE_VERTEX;
            bufferInfo.size = requiredBytes;
            spriteVertexBuffer_ = SDL_CreateGPUBuffer(device_, &bufferInfo);
            if (spriteVertexBuffer_ == nullptr)
                throw std::runtime_error(std::string("CNA SDL_GPU: failed to create sprite vertex buffer: ") + SDL_GetError());
            spriteVertexCapacityBytes_ = requiredBytes;
        }

        SDL_GPUTransferBufferCreateInfo transferInfo{};
        transferInfo.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
        transferInfo.size = requiredBytes;
        SDL_GPUTransferBuffer* transferBuffer = SDL_CreateGPUTransferBuffer(device_, &transferInfo);
        if (transferBuffer == nullptr)
            throw std::runtime_error(std::string("CNA SDL_GPU: failed to create sprite transfer buffer: ") + SDL_GetError());

        void* mapped = SDL_MapGPUTransferBuffer(device_, transferBuffer, false);
        if (mapped == nullptr)
        {
            SDL_ReleaseGPUTransferBuffer(device_, transferBuffer);
            throw std::runtime_error(std::string("CNA SDL_GPU: failed to map sprite transfer buffer: ") + SDL_GetError());
        }
        auto* dest = static_cast<SpriteVertex*>(mapped);
        for (std::size_t i = 0; i < spriteCommands_.size(); ++i)
            std::memcpy(dest + i * 6, spriteCommands_[i].vertices.data(), sizeof(SpriteVertex) * 6);
        SDL_UnmapGPUTransferBuffer(device_, transferBuffer);

        SDL_GPUCopyPass* copyPass = SDL_BeginGPUCopyPass(cmd);
        SDL_GPUTransferBufferLocation source{};
        source.transfer_buffer = transferBuffer;
        SDL_GPUBufferRegion destRegion{};
        destRegion.buffer = spriteVertexBuffer_;
        destRegion.size = requiredBytes;
        SDL_UploadToGPUBuffer(copyPass, &source, &destRegion, true);
        SDL_EndGPUCopyPass(copyPass);

        SDL_ReleaseGPUTransferBuffer(device_, transferBuffer);
    }

    void SdlGpuGraphicsBackend::RenderSprites(SDL_GPURenderPass* pass, SDL_GPUCommandBuffer* cmd)
    {
        if (spriteCommands_.empty())
            return;

        SDL_BindGPUGraphicsPipeline(pass, GetOrCreateSpritePipeline());

        const float viewportSize[2] = {static_cast<float>(physicalWidth_), static_cast<float>(physicalHeight_)};
        SDL_PushGPUVertexUniformData(cmd, 0, viewportSize, sizeof(viewportSize));

        for (std::size_t i = 0; i < spriteCommands_.size(); ++i)
        {
            const SpriteCommand& command = spriteCommands_[i];
            SDL_GPUBufferBinding vbBinding{};
            vbBinding.buffer = spriteVertexBuffer_;
            vbBinding.offset = static_cast<Uint32>(i * sizeof(SpriteVertex) * 6);
            SDL_BindGPUVertexBuffers(pass, 0, &vbBinding, 1);

            SDL_GPUTextureSamplerBinding samplerBinding{};
            samplerBinding.texture = command.texture->Texture();
            samplerBinding.sampler = GetOrCreateSampler(command.textureFilter, command.addressU, command.addressV);
            SDL_BindGPUFragmentSamplers(pass, 0, &samplerBinding, 1);

            SDL_DrawGPUPrimitives(pass, 6, 1, 0, 0);
        }
    }

    void SdlGpuGraphicsBackend::QueueSprite(const SdlGpuTextureBackend& texture,
                                             const Rectangle& destination,
                                             const Rectangle& source,
                                             const Color& color,
                                             float rotation,
                                             const Vector2& origin,
                                             SpriteEffects effects,
                                             float /*layerDepth*/,
                                             const Matrix& transform,
                                             int textureFilter,
                                             int addressU,
                                             int addressV)
    {
        if (destination.Width == 0 || destination.Height == 0 || source.Width == 0 || source.Height == 0)
            return;
        const LogicalViewport viewport = ComputeLogicalViewport();
        if (viewport.logicalWidth <= 0.0f || viewport.logicalHeight <= 0.0f || physicalWidth_ <= 0 || physicalHeight_ <= 0)
            return;

        const float scaleX = static_cast<float>(destination.Width) / static_cast<float>(source.Width);
        const float scaleY = static_cast<float>(destination.Height) / static_cast<float>(source.Height);
        const float left = -origin.X * scaleX;
        const float top = -origin.Y * scaleY;
        const float right = left + static_cast<float>(destination.Width);
        const float bottom = top + static_cast<float>(destination.Height);
        std::array<Vector2, 4> points{Vector2{left, top}, Vector2{right, top}, Vector2{left, bottom}, Vector2{right, bottom}};
        const float s = std::sin(rotation);
        const float c = std::cos(rotation);
        for (Vector2& point : points)
        {
            const float rotatedX = point.X * c - point.Y * s + static_cast<float>(destination.X);
            const float rotatedY = point.X * s + point.Y * c + static_cast<float>(destination.Y);
            point.X = rotatedX * transform.M11 + rotatedY * transform.M21 + transform.M41;
            point.Y = rotatedX * transform.M12 + rotatedY * transform.M22 + transform.M42;
        }

        float u0 = static_cast<float>(source.X) / static_cast<float>(texture.GetWidth());
        float v0 = static_cast<float>(source.Y) / static_cast<float>(texture.GetHeight());
        float u1 = static_cast<float>(source.X + source.Width) / static_cast<float>(texture.GetWidth());
        float v1 = static_cast<float>(source.Y + source.Height) / static_cast<float>(texture.GetHeight());
        const int effectBits = static_cast<int>(effects);
        if ((effectBits & static_cast<int>(SpriteEffects::FlipHorizontally)) != 0) std::swap(u0, u1);
        if ((effectBits & static_cast<int>(SpriteEffects::FlipVertically)) != 0) std::swap(v0, v1);
        const std::array<Vector2, 4> uv{Vector2{u0, v0}, Vector2{u1, v0}, Vector2{u0, v1}, Vector2{u1, v1}};
        constexpr int indices[6] = {0, 1, 2, 2, 1, 3};

        SpriteCommand command{};
        command.texture = &texture;
        command.textureFilter = textureFilter;
        command.addressU = addressU;
        command.addressV = addressV;
        const float rgba[4] = {
            static_cast<float>(color.getRProperty()) / 255.0f,
            static_cast<float>(color.getGProperty()) / 255.0f,
            static_cast<float>(color.getBProperty()) / 255.0f,
            static_cast<float>(color.getAProperty()) / 255.0f
        };
        for (int i = 0; i < 6; ++i)
        {
            const int corner = indices[i];
            const float px = viewport.x + points[corner].X * viewport.width / viewport.logicalWidth;
            const float py = viewport.y + points[corner].Y * viewport.height / viewport.logicalHeight;
            SpriteVertex& vertex = command.vertices[static_cast<std::size_t>(i)];
            vertex.x = px;
            vertex.y = py;
            vertex.u = uv[corner].X;
            vertex.v = uv[corner].Y;
            vertex.r = rgba[0];
            vertex.g = rgba[1];
            vertex.b = rgba[2];
            vertex.a = rgba[3];
        }
        spriteCommands_.push_back(command);
        framePending_ = true;
    }

    // ---- SdlGpuTextureBackend ----

    SdlGpuTextureBackend::SdlGpuTextureBackend(SdlGpuGraphicsBackend& owner, const ImageData& data)
        : owner_(&owner), width_(data.width), height_(data.height)
    {
        SDL_GPUTextureCreateInfo createInfo{};
        createInfo.type = SDL_GPU_TEXTURETYPE_2D;
        createInfo.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
        createInfo.usage = SDL_GPU_TEXTUREUSAGE_SAMPLER;
        createInfo.width = static_cast<Uint32>(width_);
        createInfo.height = static_cast<Uint32>(height_);
        createInfo.layer_count_or_depth = 1;
        createInfo.num_levels = 1;
        createInfo.sample_count = SDL_GPU_SAMPLECOUNT_1;

        texture_ = SDL_CreateGPUTexture(owner_->Device(), &createInfo);
        if (texture_ == nullptr)
            throw std::runtime_error(std::string("CNA SDL_GPU: failed to create Texture2D: ") + SDL_GetError());

        UpdatePixels(data.pixels.data(), width_ * 4);
    }

    SdlGpuTextureBackend::~SdlGpuTextureBackend()
    {
        if (texture_ != nullptr)
            SDL_ReleaseGPUTexture(owner_->Device(), texture_);
    }

    void SdlGpuTextureBackend::UpdatePixels(const uint8_t* rgba, int stride)
    {
        SDL_GPUDevice* device = owner_->Device();
        const Uint32 rowBytes = static_cast<Uint32>(width_) * 4;
        const Uint32 sizeBytes = rowBytes * static_cast<Uint32>(height_);

        SDL_GPUTransferBufferCreateInfo transferInfo{};
        transferInfo.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
        transferInfo.size = sizeBytes;
        SDL_GPUTransferBuffer* transferBuffer = SDL_CreateGPUTransferBuffer(device, &transferInfo);
        if (transferBuffer == nullptr)
            throw std::runtime_error(std::string("CNA SDL_GPU: failed to create texture transfer buffer: ") + SDL_GetError());

        void* mapped = SDL_MapGPUTransferBuffer(device, transferBuffer, false);
        if (mapped == nullptr)
        {
            SDL_ReleaseGPUTransferBuffer(device, transferBuffer);
            throw std::runtime_error(std::string("CNA SDL_GPU: failed to map texture transfer buffer: ") + SDL_GetError());
        }
        if (stride == static_cast<int>(rowBytes))
        {
            std::memcpy(mapped, rgba, sizeBytes);
        }
        else
        {
            auto* dst = static_cast<uint8_t*>(mapped);
            for (int y = 0; y < height_; ++y)
                std::memcpy(dst + static_cast<std::size_t>(y) * rowBytes,
                            rgba + static_cast<std::size_t>(y) * static_cast<std::size_t>(stride), rowBytes);
        }
        SDL_UnmapGPUTransferBuffer(device, transferBuffer);

        SDL_GPUCommandBuffer* cmd = SDL_AcquireGPUCommandBuffer(device);
        if (cmd == nullptr)
        {
            SDL_ReleaseGPUTransferBuffer(device, transferBuffer);
            throw std::runtime_error(std::string("CNA SDL_GPU: SDL_AcquireGPUCommandBuffer (texture upload) failed: ") + SDL_GetError());
        }
        SDL_GPUCopyPass* copyPass = SDL_BeginGPUCopyPass(cmd);
        SDL_GPUTextureTransferInfo source{};
        source.transfer_buffer = transferBuffer;
        source.pixels_per_row = static_cast<Uint32>(width_);
        source.rows_per_layer = static_cast<Uint32>(height_);
        SDL_GPUTextureRegion destination{};
        destination.texture = texture_;
        destination.w = static_cast<Uint32>(width_);
        destination.h = static_cast<Uint32>(height_);
        destination.d = 1;
        SDL_UploadToGPUTexture(copyPass, &source, &destination, true);
        SDL_EndGPUCopyPass(copyPass);
        if (!SDL_SubmitGPUCommandBuffer(cmd))
        {
            SDL_ReleaseGPUTransferBuffer(device, transferBuffer);
            throw std::runtime_error(std::string("CNA SDL_GPU: SDL_SubmitGPUCommandBuffer (texture upload) failed: ") + SDL_GetError());
        }
        SDL_ReleaseGPUTransferBuffer(device, transferBuffer);
    }

    // ---- SdlGpuVertexBufferBackend ----

    SdlGpuVertexBufferBackend::SdlGpuVertexBufferBackend(SdlGpuGraphicsBackend& owner, int vertexCapacity)
        : owner_(&owner), vertexCapacity_(vertexCapacity)
    {
    }

    SdlGpuVertexBufferBackend::~SdlGpuVertexBufferBackend()
    {
        if (buffer_ != nullptr)
            SDL_ReleaseGPUBuffer(owner_->Device(), buffer_);
    }

    void SdlGpuVertexBufferBackend::SetData(const void* data, int vertexCount, std::size_t strideInBytes)
    {
        SDL_GPUDevice* device = owner_->Device();
        const Uint32 sizeBytes = static_cast<Uint32>(vertexCount) * static_cast<Uint32>(strideInBytes);
        if (buffer_ == nullptr || capacityBytes_ < sizeBytes)
        {
            if (buffer_ != nullptr)
                SDL_ReleaseGPUBuffer(device, buffer_);
            SDL_GPUBufferCreateInfo createInfo{};
            createInfo.usage = SDL_GPU_BUFFERUSAGE_VERTEX;
            createInfo.size = sizeBytes;
            buffer_ = SDL_CreateGPUBuffer(device, &createInfo);
            if (buffer_ == nullptr)
                throw std::runtime_error(std::string("CNA SDL_GPU: failed to create vertex buffer: ") + SDL_GetError());
            capacityBytes_ = sizeBytes;
        }

        SDL_GPUTransferBufferCreateInfo transferInfo{};
        transferInfo.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
        transferInfo.size = sizeBytes;
        SDL_GPUTransferBuffer* transferBuffer = SDL_CreateGPUTransferBuffer(device, &transferInfo);
        if (transferBuffer == nullptr)
            throw std::runtime_error(std::string("CNA SDL_GPU: failed to create vertex transfer buffer: ") + SDL_GetError());
        void* mapped = SDL_MapGPUTransferBuffer(device, transferBuffer, false);
        if (mapped == nullptr)
        {
            SDL_ReleaseGPUTransferBuffer(device, transferBuffer);
            throw std::runtime_error(std::string("CNA SDL_GPU: failed to map vertex transfer buffer: ") + SDL_GetError());
        }
        std::memcpy(mapped, data, sizeBytes);
        SDL_UnmapGPUTransferBuffer(device, transferBuffer);

        SDL_GPUCommandBuffer* cmd = SDL_AcquireGPUCommandBuffer(device);
        if (cmd == nullptr)
        {
            SDL_ReleaseGPUTransferBuffer(device, transferBuffer);
            throw std::runtime_error(std::string("CNA SDL_GPU: SDL_AcquireGPUCommandBuffer (vertex upload) failed: ") + SDL_GetError());
        }
        SDL_GPUCopyPass* copyPass = SDL_BeginGPUCopyPass(cmd);
        SDL_GPUTransferBufferLocation source{};
        source.transfer_buffer = transferBuffer;
        SDL_GPUBufferRegion destRegion{};
        destRegion.buffer = buffer_;
        destRegion.size = sizeBytes;
        SDL_UploadToGPUBuffer(copyPass, &source, &destRegion, true);
        SDL_EndGPUCopyPass(copyPass);
        if (!SDL_SubmitGPUCommandBuffer(cmd))
        {
            SDL_ReleaseGPUTransferBuffer(device, transferBuffer);
            throw std::runtime_error(std::string("CNA SDL_GPU: SDL_SubmitGPUCommandBuffer (vertex upload) failed: ") + SDL_GetError());
        }
        SDL_ReleaseGPUTransferBuffer(device, transferBuffer);

        vertexCount_ = vertexCount;
    }

    // ---- SdlGpuIndexBufferBackend ----

    SdlGpuIndexBufferBackend::SdlGpuIndexBufferBackend(SdlGpuGraphicsBackend& owner, int indexCapacity, bool thirtyTwoBit)
        : owner_(&owner), indexCapacity_(indexCapacity), thirtyTwoBit_(thirtyTwoBit)
    {
    }

    SdlGpuIndexBufferBackend::~SdlGpuIndexBufferBackend()
    {
        if (buffer_ != nullptr)
            SDL_ReleaseGPUBuffer(owner_->Device(), buffer_);
    }

    void SdlGpuIndexBufferBackend::SetData16(const void* data, int indexCount) { Upload(data, indexCount, false); }
    void SdlGpuIndexBufferBackend::SetData32(const void* data, int indexCount) { Upload(data, indexCount, true); }

    void SdlGpuIndexBufferBackend::Upload(const void* data, int indexCount, bool dataIsThirtyTwoBit)
    {
        SDL_GPUDevice* device = owner_->Device();
        const std::size_t elementSize = dataIsThirtyTwoBit ? sizeof(std::uint32_t) : sizeof(std::uint16_t);
        const Uint32 sizeBytes = static_cast<Uint32>(indexCount) * static_cast<Uint32>(elementSize);
        if (buffer_ == nullptr || capacityBytes_ < sizeBytes)
        {
            if (buffer_ != nullptr)
                SDL_ReleaseGPUBuffer(device, buffer_);
            SDL_GPUBufferCreateInfo createInfo{};
            createInfo.usage = SDL_GPU_BUFFERUSAGE_INDEX;
            createInfo.size = sizeBytes;
            buffer_ = SDL_CreateGPUBuffer(device, &createInfo);
            if (buffer_ == nullptr)
                throw std::runtime_error(std::string("CNA SDL_GPU: failed to create index buffer: ") + SDL_GetError());
            capacityBytes_ = sizeBytes;
        }

        SDL_GPUTransferBufferCreateInfo transferInfo{};
        transferInfo.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
        transferInfo.size = sizeBytes;
        SDL_GPUTransferBuffer* transferBuffer = SDL_CreateGPUTransferBuffer(device, &transferInfo);
        if (transferBuffer == nullptr)
            throw std::runtime_error(std::string("CNA SDL_GPU: failed to create index transfer buffer: ") + SDL_GetError());
        void* mapped = SDL_MapGPUTransferBuffer(device, transferBuffer, false);
        if (mapped == nullptr)
        {
            SDL_ReleaseGPUTransferBuffer(device, transferBuffer);
            throw std::runtime_error(std::string("CNA SDL_GPU: failed to map index transfer buffer: ") + SDL_GetError());
        }
        std::memcpy(mapped, data, sizeBytes);
        SDL_UnmapGPUTransferBuffer(device, transferBuffer);

        SDL_GPUCommandBuffer* cmd = SDL_AcquireGPUCommandBuffer(device);
        if (cmd == nullptr)
        {
            SDL_ReleaseGPUTransferBuffer(device, transferBuffer);
            throw std::runtime_error(std::string("CNA SDL_GPU: SDL_AcquireGPUCommandBuffer (index upload) failed: ") + SDL_GetError());
        }
        SDL_GPUCopyPass* copyPass = SDL_BeginGPUCopyPass(cmd);
        SDL_GPUTransferBufferLocation source{};
        source.transfer_buffer = transferBuffer;
        SDL_GPUBufferRegion destRegion{};
        destRegion.buffer = buffer_;
        destRegion.size = sizeBytes;
        SDL_UploadToGPUBuffer(copyPass, &source, &destRegion, true);
        SDL_EndGPUCopyPass(copyPass);
        if (!SDL_SubmitGPUCommandBuffer(cmd))
        {
            SDL_ReleaseGPUTransferBuffer(device, transferBuffer);
            throw std::runtime_error(std::string("CNA SDL_GPU: SDL_SubmitGPUCommandBuffer (index upload) failed: ") + SDL_GetError());
        }
        SDL_ReleaseGPUTransferBuffer(device, transferBuffer);

        indexCount_ = indexCount;
        thirtyTwoBit_ = dataIsThirtyTwoBit;
    }

    // ---- SdlGpuSpriteBatchBackend ----

    SdlGpuSpriteBatchBackend::SdlGpuSpriteBatchBackend(SdlGpuGraphicsBackend& owner)
        : owner_(&owner)
    {
    }

    void SdlGpuSpriteBatchBackend::Begin()
    {
        if (begun_)
            throw std::logic_error("CNA SDL_GPU SpriteBatch.Begin called twice without End");
        begun_ = true;
    }

    void SdlGpuSpriteBatchBackend::End()
    {
        if (!begun_)
            throw std::logic_error("CNA SDL_GPU SpriteBatch.End called without Begin");
        begun_ = false;
    }

    void SdlGpuSpriteBatchBackend::SetCustomEffect(Effect* effect)
    {
        if (effect != nullptr)
            throw std::runtime_error("CNA SDL_GPU: custom SpriteBatch effects are not implemented yet (see plan_sdlgpu.md)");
    }

    void SdlGpuSpriteBatchBackend::Draw(const ITextureBackend& texture, float x, float y)
    {
        const Rectangle source{0, 0, texture.GetWidth(), texture.GetHeight()};
        const Rectangle destination{static_cast<int>(x), static_cast<int>(y), texture.GetWidth(), texture.GetHeight()};
        Draw(texture, destination, source, Color::White);
    }

    void SdlGpuSpriteBatchBackend::Draw(const ITextureBackend& texture,
                                        const Rectangle& destinationRectangle,
                                        const Rectangle& sourceRectangle,
                                        const Color& color)
    {
        Draw(texture, destinationRectangle, sourceRectangle, color, 0.0f, Vector2::Zero, SpriteEffects::None, 0.0f);
    }

    void SdlGpuSpriteBatchBackend::Draw(const ITextureBackend& texture,
                                        const Rectangle& destinationRectangle,
                                        const Rectangle& sourceRectangle,
                                        const Color& color,
                                        float rotation,
                                        const Vector2& origin,
                                        SpriteEffects effects,
                                        float layerDepth)
    {
        if (!begun_)
            throw std::logic_error("CNA SDL_GPU SpriteBatch.Draw called outside Begin/End");
        const auto* sdlGpuTexture = dynamic_cast<const SdlGpuTextureBackend*>(&texture);
        if (sdlGpuTexture == nullptr)
            throw std::invalid_argument("CNA SDL_GPU: SpriteBatch received a texture from another graphics backend");
        owner_->QueueSprite(*sdlGpuTexture, destinationRectangle, sourceRectangle, color, rotation,
                            origin, effects, layerDepth, transform_, textureFilter_, addressU_, addressV_);
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
