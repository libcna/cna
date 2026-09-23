// SPDX-License-Identifier: MS-PL
//
// plans/plan_sdlgpu_modern_graphics.md SMG-0012. See the header for the ordering rule this
// renderer's deferred draw queue forces on compute, and for what SDL_gpu has no resource for.

#include "CNA/Internal/Renderers/SdlGpu/SdlGpuModern.hpp"

#include "CNA/Internal/Renderers/SdlGpu/SdlGpuRenderer.hpp"

#include <algorithm>
#include <cstring>
#include <stdexcept>

namespace CNA::Internal::Renderers::SdlGpu
{
    namespace
    {
        // CNA::Graphics::StorageBufferUsage, which this layer receives as a plain mask.
        constexpr std::uint32_t kUsageStorage = 1u << 0;
        constexpr std::uint32_t kUsageTransferSource = 1u << 1;
        constexpr std::uint32_t kUsageTransferDestination = 1u << 2;
        constexpr std::uint32_t kUsageIndirectArguments = 1u << 3;
        constexpr std::uint32_t kUsageVertex = 1u << 4;
        constexpr std::uint32_t kUsageIndex = 1u << 5;
        constexpr std::uint32_t kUsageConstant = 1u << 6;

        /// SDL_gpu asserts on a zero-sized buffer, and its Vulkan driver asserts below four bytes
        /// (the same clamp SdlGpuIndexBufferRenderer applies, for the same reason).
        [[nodiscard]] Uint32 ClampBufferSize(const std::size_t byteSize)
        {
            return static_cast<Uint32>(std::max<std::size_t>(byteSize, 4));
        }

        /// Translates CNA's usage mask into SDL's. A `Constant` buffer maps to nothing here on
        /// purpose: SDL_gpu has no uniform-buffer resource, and such a buffer is pushed from its
        /// shadow copy at bind time instead (see the header).
        [[nodiscard]] SDL_GPUBufferUsageFlags TranslateUsage(const std::uint32_t usage)
        {
            SDL_GPUBufferUsageFlags flags = 0;
            if ((usage & kUsageStorage) != 0)
                flags |= SDL_GPU_BUFFERUSAGE_COMPUTE_STORAGE_READ
                       | SDL_GPU_BUFFERUSAGE_COMPUTE_STORAGE_WRITE
                       | SDL_GPU_BUFFERUSAGE_GRAPHICS_STORAGE_READ;
            if ((usage & kUsageIndirectArguments) != 0) flags |= SDL_GPU_BUFFERUSAGE_INDIRECT;
            if ((usage & kUsageVertex) != 0) flags |= SDL_GPU_BUFFERUSAGE_VERTEX;
            if ((usage & kUsageIndex) != 0) flags |= SDL_GPU_BUFFERUSAGE_INDEX;
            // A buffer declaring only transfer roles, or only Constant, still needs some usage for
            // SDL to accept it; graphics storage read is the least privileged one that always works
            // and never changes what the buffer can be asked to do from CNA's side.
            if (flags == 0) flags = SDL_GPU_BUFFERUSAGE_GRAPHICS_STORAGE_READ;
            return flags;
        }

        /// Uploads `byteSize` bytes at `byteOffset`, on its own command buffer, submitted before
        /// returning. Every caller here is a public CNA `setBytes`, which is synchronous.
        [[nodiscard]] bool UploadBufferRange(
            SDL_GPUDevice* device, SDL_GPUBuffer* buffer, const std::size_t byteOffset,
            const void* data, const std::size_t byteSize)
        {
            if (device == nullptr || buffer == nullptr || byteSize == 0) return byteSize == 0;
            SDL_GPUTransferBufferCreateInfo transferInfo{};
            transferInfo.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
            transferInfo.size = static_cast<Uint32>(byteSize);
            SDL_GPUTransferBuffer* transfer = SDL_CreateGPUTransferBuffer(device, &transferInfo);
            if (transfer == nullptr) return false;
            void* mapped = SDL_MapGPUTransferBuffer(device, transfer, false);
            if (mapped == nullptr)
            {
                SDL_ReleaseGPUTransferBuffer(device, transfer);
                return false;
            }
            std::memcpy(mapped, data, byteSize);
            SDL_UnmapGPUTransferBuffer(device, transfer);

            SDL_GPUCommandBuffer* cmd = SDL_AcquireGPUCommandBuffer(device);
            if (cmd == nullptr)
            {
                SDL_ReleaseGPUTransferBuffer(device, transfer);
                return false;
            }
            SDL_GPUCopyPass* copy = SDL_BeginGPUCopyPass(cmd);
            SDL_GPUTransferBufferLocation source{};
            source.transfer_buffer = transfer;
            SDL_GPUBufferRegion region{};
            region.buffer = buffer;
            region.offset = static_cast<Uint32>(byteOffset);
            region.size = static_cast<Uint32>(byteSize);
            SDL_UploadToGPUBuffer(copy, &source, &region, false);
            SDL_EndGPUCopyPass(copy);
            const bool submitted = SDL_SubmitGPUCommandBuffer(cmd);
            SDL_ReleaseGPUTransferBuffer(device, transfer);
            return submitted;
        }

        /// Downloads `byteSize` bytes from `byteOffset`. Waits on a fence rather than on the
        /// device, so an unrelated in-flight frame is not stalled and nothing waits forever on
        /// work that was never submitted.
        [[nodiscard]] bool DownloadBufferRange(
            SDL_GPUDevice* device, SDL_GPUBuffer* buffer, const std::size_t byteOffset,
            void* out, const std::size_t byteSize)
        {
            if (device == nullptr || buffer == nullptr || byteSize == 0) return byteSize == 0;
            SDL_GPUTransferBufferCreateInfo transferInfo{};
            transferInfo.usage = SDL_GPU_TRANSFERBUFFERUSAGE_DOWNLOAD;
            transferInfo.size = static_cast<Uint32>(byteSize);
            SDL_GPUTransferBuffer* transfer = SDL_CreateGPUTransferBuffer(device, &transferInfo);
            if (transfer == nullptr) return false;

            SDL_GPUCommandBuffer* cmd = SDL_AcquireGPUCommandBuffer(device);
            if (cmd == nullptr)
            {
                SDL_ReleaseGPUTransferBuffer(device, transfer);
                return false;
            }
            SDL_GPUCopyPass* copy = SDL_BeginGPUCopyPass(cmd);
            SDL_GPUBufferRegion region{};
            region.buffer = buffer;
            region.offset = static_cast<Uint32>(byteOffset);
            region.size = static_cast<Uint32>(byteSize);
            SDL_GPUTransferBufferLocation destination{};
            destination.transfer_buffer = transfer;
            SDL_DownloadFromGPUBuffer(copy, &region, &destination);
            SDL_EndGPUCopyPass(copy);

            SDL_GPUFence* fence = SDL_SubmitGPUCommandBufferAndAcquireFence(cmd);
            if (fence == nullptr)
            {
                SDL_ReleaseGPUTransferBuffer(device, transfer);
                return false;
            }
            SDL_WaitForGPUFences(device, true, &fence, 1);
            SDL_ReleaseGPUFence(device, fence);

            const void* mapped = SDL_MapGPUTransferBuffer(device, transfer, false);
            if (mapped == nullptr)
            {
                SDL_ReleaseGPUTransferBuffer(device, transfer);
                return false;
            }
            std::memcpy(out, mapped, byteSize);
            SDL_UnmapGPUTransferBuffer(device, transfer);
            SDL_ReleaseGPUTransferBuffer(device, transfer);
            return true;
        }
    }

    // ---- SdlGpuStorageBufferRenderer --------------------------------------------------------

    SdlGpuStorageBufferRenderer::SdlGpuStorageBufferRenderer(
        SdlGpuRenderer& owner, const std::size_t byteSize, const std::uint32_t usage,
        const std::uint32_t cpuAccess)
        : owner_(&owner), byteSize_(byteSize), usage_(usage), cpuAccess_(cpuAccess)
    {
        SDL_GPUBufferCreateInfo info{};
        info.usage = TranslateUsage(usage);
        info.size = ClampBufferSize(byteSize);
        buffer_ = SDL_CreateGPUBuffer(owner_->Device(), &info);
        if (buffer_ == nullptr)
            throw std::runtime_error(
                std::string("CNA SDL_GPU: failed to create storage buffer: ") + SDL_GetError());
        owner_->RegisterStorageBufferEXT(this);
        owner_->NotifyResourceEvent(SdlGpuResourceKindEXT::StorageBuffer,
                                    SdlGpuResourceEventEXT::Acquired);
        shadow_.assign(byteSize, 0);
        // A buffer whose contents are never written before a shader reads it is undefined storage
        // on every backend; zeroing it makes a partial SetDataRangeEXT merge into a known state
        // rather than into whatever the allocator handed over.
        (void) UploadBufferRange(owner_->Device(), buffer_, 0, shadow_.data(), shadow_.size());
    }

    SdlGpuStorageBufferRenderer::~SdlGpuStorageBufferRenderer()
    {
        if (owner_ == nullptr) return;
        owner_->UnregisterStorageBufferEXT(this);
        if (buffer_ != nullptr && owner_->Device() != nullptr)
        {
            SDL_ReleaseGPUBuffer(owner_->Device(), buffer_);
            owner_->NotifyResourceEvent(SdlGpuResourceKindEXT::StorageBuffer,
                                        SdlGpuResourceEventEXT::Released);
        }
    }

    void SdlGpuStorageBufferRenderer::ReleaseForRendererTeardownEXT() noexcept
    {
        if (owner_ != nullptr && buffer_ != nullptr && owner_->Device() != nullptr)
        {
            SDL_ReleaseGPUBuffer(owner_->Device(), buffer_);
            owner_->NotifyResourceEvent(SdlGpuResourceKindEXT::StorageBuffer,
                                        SdlGpuResourceEventEXT::Released);
        }
        buffer_ = nullptr;
        owner_ = nullptr;
    }

    void SdlGpuStorageBufferRenderer::SetData(const void* data, const std::size_t byteSize)
    {
        if (!SetDataRangeEXT(0, data, byteSize))
            throw std::runtime_error("CNA SDL_GPU: storage-buffer upload was refused");
    }

    void SdlGpuStorageBufferRenderer::GetData(void* out, const std::size_t byteSize) const
    {
        if (!GetDataRangeEXT(0, out, byteSize))
            throw std::runtime_error("CNA SDL_GPU: storage-buffer readback was refused");
    }

    bool SdlGpuStorageBufferRenderer::SetDataRangeEXT(
        const std::size_t byteOffset, const void* data, const std::size_t byteSize)
    {
        if (data == nullptr && byteSize != 0) return false;
        if (byteOffset > byteSize_ || byteSize > byteSize_ - byteOffset) return false;
        if (byteSize == 0) return true;
        if (owner_ == nullptr) return false;
        std::memcpy(shadow_.data() + byteOffset, data, byteSize);
        return UploadBufferRange(owner_->Device(), buffer_, byteOffset, data, byteSize);
    }

    bool SdlGpuStorageBufferRenderer::GetDataRangeEXT(
        const std::size_t byteOffset, void* out, const std::size_t byteSize) const
    {
        if (out == nullptr && byteSize != 0) return false;
        if (byteOffset > byteSize_ || byteSize > byteSize_ - byteOffset) return false;
        if (byteSize == 0) return true;
        if (owner_ == nullptr) return false;
        // Anything the GPU has been asked to write may still be queued in this renderer's deferred
        // frame; a readback that skipped it would report the state before the work rather than
        // after it.
        owner_->FlushPendingGpuWorkEXT();
        if (!DownloadBufferRange(owner_->Device(), buffer_, byteOffset, out, byteSize))
            return false;
        std::memcpy(shadow_.data() + byteOffset, out, byteSize);
        return true;
    }

    bool SdlGpuStorageBufferRenderer::CopyToEXT(
        IStorageBufferRenderer& destination, const std::size_t sourceByteOffset,
        const std::size_t destinationByteOffset, const std::size_t byteSize)
    {
        auto* target = dynamic_cast<SdlGpuStorageBufferRenderer*>(&destination);
        if (target == nullptr || owner_ == nullptr || target->owner_ != owner_) return false;
        if (sourceByteOffset > byteSize_ || byteSize > byteSize_ - sourceByteOffset) return false;
        if (destinationByteOffset > target->byteSize_
            || byteSize > target->byteSize_ - destinationByteOffset)
            return false;
        if (byteSize == 0) return true;

        owner_->FlushPendingGpuWorkEXT();
        SDL_GPUDevice* device = owner_->Device();
        SDL_GPUCommandBuffer* cmd = SDL_AcquireGPUCommandBuffer(device);
        if (cmd == nullptr) return false;
        SDL_GPUCopyPass* copy = SDL_BeginGPUCopyPass(cmd);
        SDL_GPUBufferLocation source{};
        source.buffer = buffer_;
        source.offset = static_cast<Uint32>(sourceByteOffset);
        SDL_GPUBufferLocation target2{};
        target2.buffer = target->buffer_;
        target2.offset = static_cast<Uint32>(destinationByteOffset);
        SDL_CopyGPUBufferToBuffer(copy, &source, &target2, static_cast<Uint32>(byteSize), false);
        SDL_EndGPUCopyPass(copy);
        if (!SDL_SubmitGPUCommandBuffer(cmd)) return false;
        std::memcpy(target->shadow_.data() + destinationByteOffset,
                    shadow_.data() + sourceByteOffset, byteSize);
        return true;
    }

    bool SdlGpuStorageBufferRenderer::RefreshShadowFromGpuEXT() const
    {
        if (byteSize_ == 0) return true;
        if (owner_ == nullptr) return false;
        owner_->FlushPendingGpuWorkEXT();
        return DownloadBufferRange(owner_->Device(), buffer_, 0, shadow_.data(), byteSize_);
    }

    // SMG-0040: the renderer is single-threaded, like the compiledEffects_ registry this mirrors,
    // so the list takes no lock.
    void SdlGpuRenderer::RegisterStorageBufferEXT(SdlGpuStorageBufferRenderer* buffer)
    {
        storageBuffersEXT_.push_back(buffer);
    }

    void SdlGpuRenderer::UnregisterStorageBufferEXT(SdlGpuStorageBufferRenderer* buffer)
    {
        storageBuffersEXT_.erase(
            std::remove(storageBuffersEXT_.begin(), storageBuffersEXT_.end(), buffer),
            storageBuffersEXT_.end());
    }

    void SdlGpuRenderer::ReleaseStorageBuffersForRendererTeardownEXT()
    {
        // Moved out first: a detached record no longer unregisters, but nothing here may rely on
        // that while iterating.
        const std::vector<SdlGpuStorageBufferRenderer*> survivors = std::move(storageBuffersEXT_);
        storageBuffersEXT_.clear();
        for (SdlGpuStorageBufferRenderer* buffer : survivors)
            buffer->ReleaseForRendererTeardownEXT();
    }

    // ---- SdlGpuStorageTexture2DRenderer -----------------------------------------------------

    namespace
    {
        // CNA::Graphics::StorageTexture2DUsage.
        constexpr std::uint32_t kTextureStorageRead = 1u << 0;
        constexpr std::uint32_t kTextureStorageWrite = 1u << 1;
        constexpr std::uint32_t kTextureSampled = 1u << 2;
    }

    bool TranslateStorageImageFormatEXT(
        const int surfaceFormat, SDL_GPUTextureFormat& out, int& bytesPerTexel)
    {
        // The narrow set CNA's engine layer asks a storage image for. SDL_gpu's compute storage
        // formats are a subset of its texture formats; anything outside this list is classified
        // as having no equivalent rather than guessed at.
        switch (surfaceFormat)
        {
            case 0:  // SurfaceFormat::Color
                out = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
                bytesPerTexel = 4;
                return true;
            case 15:  // SurfaceFormat::HalfVector4
                out = SDL_GPU_TEXTUREFORMAT_R16G16B16A16_FLOAT;
                bytesPerTexel = 8;
                return true;
            case 16:  // SurfaceFormat::Vector4
                out = SDL_GPU_TEXTUREFORMAT_R32G32B32A32_FLOAT;
                bytesPerTexel = 16;
                return true;
            default: return false;
        }
    }

    SdlGpuStorageTexture2DRenderer::SdlGpuStorageTexture2DRenderer(
        SdlGpuRenderer& owner, const int width, const int height, const int mipLevelCount,
        const int surfaceFormat, const std::uint32_t usage)
        : owner_(&owner), width_(width), height_(height),
          levelCount_(std::max(1, mipLevelCount)), surfaceFormat_(surfaceFormat)
    {
        SDL_GPUTextureFormat format = SDL_GPU_TEXTUREFORMAT_INVALID;
        if (width <= 0 || height <= 0
            || !TranslateStorageImageFormatEXT(surfaceFormat, format, bytesPerTexel_))
            throw std::runtime_error(
                "CNA SDL_GPU: this surface format has no SDL_gpu storage-image equivalent");

        SDL_GPUTextureCreateInfo info{};
        info.type = SDL_GPU_TEXTURETYPE_2D;
        info.format = format;
        info.width = static_cast<Uint32>(width);
        info.height = static_cast<Uint32>(height);
        info.layer_count_or_depth = 1;
        info.num_levels = static_cast<Uint32>(levelCount_);
        info.sample_count = SDL_GPU_SAMPLECOUNT_1;
        info.usage = 0;
        if ((usage & kTextureStorageRead) != 0)
            info.usage |= SDL_GPU_TEXTUREUSAGE_COMPUTE_STORAGE_READ;
        if ((usage & kTextureStorageWrite) != 0)
        {
            info.usage |= SDL_GPU_TEXTUREUSAGE_COMPUTE_STORAGE_WRITE;
            writable_ = true;
        }
        if ((usage & kTextureSampled) != 0) info.usage |= SDL_GPU_TEXTUREUSAGE_SAMPLER;
        // A texture with no usage at all is rejected by SDL; sampling is the least privileged one
        // and is what a read-back-only storage texture still needs to be transferable.
        if (info.usage == 0) info.usage = SDL_GPU_TEXTUREUSAGE_SAMPLER;

        if (!SDL_GPUTextureSupportsFormat(owner_->Device(), format, SDL_GPU_TEXTURETYPE_2D,
                                          info.usage))
            throw std::runtime_error(
                "CNA SDL_GPU: the device refuses this storage-image format and usage combination");

        texture_ = SDL_CreateGPUTexture(owner_->Device(), &info);
        if (texture_ == nullptr)
            throw std::runtime_error(
                std::string("CNA SDL_GPU: failed to create storage texture: ") + SDL_GetError());
    }

    SdlGpuStorageTexture2DRenderer::~SdlGpuStorageTexture2DRenderer()
    {
        if (texture_ != nullptr && owner_ != nullptr && owner_->Device() != nullptr)
            owner_->QueueTextureRelease(texture_);
    }

    bool SdlGpuStorageTexture2DRenderer::SetData(
        const int mipLevel, const int x, const int y, const int width, const int height,
        const void* data, const std::size_t byteCount)
    {
        if (data == nullptr || mipLevel < 0 || mipLevel >= levelCount_) return false;
        if (x < 0 || y < 0 || width <= 0 || height <= 0) return false;
        const int levelWidth = std::max(1, width_ >> mipLevel);
        const int levelHeight = std::max(1, height_ >> mipLevel);
        if (x + width > levelWidth || y + height > levelHeight) return false;
        const std::size_t expected =
            static_cast<std::size_t>(width) * static_cast<std::size_t>(height)
            * static_cast<std::size_t>(bytesPerTexel_);
        if (byteCount != expected) return false;

        SDL_GPUDevice* device = owner_->Device();
        SDL_GPUTransferBufferCreateInfo transferInfo{};
        transferInfo.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
        transferInfo.size = static_cast<Uint32>(byteCount);
        SDL_GPUTransferBuffer* transfer = SDL_CreateGPUTransferBuffer(device, &transferInfo);
        if (transfer == nullptr) return false;
        void* mapped = SDL_MapGPUTransferBuffer(device, transfer, false);
        if (mapped == nullptr)
        {
            SDL_ReleaseGPUTransferBuffer(device, transfer);
            return false;
        }
        std::memcpy(mapped, data, byteCount);
        SDL_UnmapGPUTransferBuffer(device, transfer);

        SDL_GPUCommandBuffer* cmd = SDL_AcquireGPUCommandBuffer(device);
        if (cmd == nullptr)
        {
            SDL_ReleaseGPUTransferBuffer(device, transfer);
            return false;
        }
        SDL_GPUCopyPass* copy = SDL_BeginGPUCopyPass(cmd);
        SDL_GPUTextureTransferInfo source{};
        source.transfer_buffer = transfer;
        source.pixels_per_row = static_cast<Uint32>(width);
        source.rows_per_layer = static_cast<Uint32>(height);
        SDL_GPUTextureRegion region{};
        region.texture = texture_;
        region.mip_level = static_cast<Uint32>(mipLevel);
        region.x = static_cast<Uint32>(x);
        region.y = static_cast<Uint32>(y);
        region.w = static_cast<Uint32>(width);
        region.h = static_cast<Uint32>(height);
        region.d = 1;
        SDL_UploadToGPUTexture(copy, &source, &region, false);
        SDL_EndGPUCopyPass(copy);
        const bool submitted = SDL_SubmitGPUCommandBuffer(cmd);
        SDL_ReleaseGPUTransferBuffer(device, transfer);
        return submitted;
    }

    bool SdlGpuStorageTexture2DRenderer::GetData(
        const int mipLevel, const int x, const int y, const int width, const int height,
        void* data, const std::size_t byteCount) const
    {
        if (data == nullptr || mipLevel < 0 || mipLevel >= levelCount_) return false;
        if (x < 0 || y < 0 || width <= 0 || height <= 0) return false;
        const int levelWidth = std::max(1, width_ >> mipLevel);
        const int levelHeight = std::max(1, height_ >> mipLevel);
        if (x + width > levelWidth || y + height > levelHeight) return false;
        const std::size_t expected =
            static_cast<std::size_t>(width) * static_cast<std::size_t>(height)
            * static_cast<std::size_t>(bytesPerTexel_);
        if (byteCount != expected) return false;

        owner_->FlushPendingGpuWorkEXT();
        SDL_GPUDevice* device = owner_->Device();
        SDL_GPUTransferBufferCreateInfo transferInfo{};
        transferInfo.usage = SDL_GPU_TRANSFERBUFFERUSAGE_DOWNLOAD;
        transferInfo.size = static_cast<Uint32>(byteCount);
        SDL_GPUTransferBuffer* transfer = SDL_CreateGPUTransferBuffer(device, &transferInfo);
        if (transfer == nullptr) return false;

        SDL_GPUCommandBuffer* cmd = SDL_AcquireGPUCommandBuffer(device);
        if (cmd == nullptr)
        {
            SDL_ReleaseGPUTransferBuffer(device, transfer);
            return false;
        }
        SDL_GPUCopyPass* copy = SDL_BeginGPUCopyPass(cmd);
        SDL_GPUTextureRegion region{};
        region.texture = texture_;
        region.mip_level = static_cast<Uint32>(mipLevel);
        region.x = static_cast<Uint32>(x);
        region.y = static_cast<Uint32>(y);
        region.w = static_cast<Uint32>(width);
        region.h = static_cast<Uint32>(height);
        region.d = 1;
        SDL_GPUTextureTransferInfo destination{};
        destination.transfer_buffer = transfer;
        destination.pixels_per_row = static_cast<Uint32>(width);
        destination.rows_per_layer = static_cast<Uint32>(height);
        SDL_DownloadFromGPUTexture(copy, &region, &destination);
        SDL_EndGPUCopyPass(copy);

        SDL_GPUFence* fence = SDL_SubmitGPUCommandBufferAndAcquireFence(cmd);
        if (fence == nullptr)
        {
            SDL_ReleaseGPUTransferBuffer(device, transfer);
            return false;
        }
        SDL_WaitForGPUFences(device, true, &fence, 1);
        SDL_ReleaseGPUFence(device, fence);

        const void* mapped = SDL_MapGPUTransferBuffer(device, transfer, false);
        if (mapped == nullptr)
        {
            SDL_ReleaseGPUTransferBuffer(device, transfer);
            return false;
        }
        std::memcpy(data, mapped, byteCount);
        SDL_UnmapGPUTransferBuffer(device, transfer);
        SDL_ReleaseGPUTransferBuffer(device, transfer);
        return true;
    }

    // ---- SdlGpuComputeShaderRenderer --------------------------------------------------------

    SdlGpuComputeShaderRenderer::SdlGpuComputeShaderRenderer(SdlGpuRenderer& owner)
        : owner_(&owner)
    {
    }

    SdlGpuComputeShaderRenderer::~SdlGpuComputeShaderRenderer()
    {
        if (pipeline_ != nullptr && owner_ != nullptr && owner_->Device() != nullptr)
            SDL_ReleaseGPUComputePipeline(owner_->Device(), pipeline_);
    }

    bool SdlGpuComputeShaderRenderer::CompileProgram(const std::string& computeSrc)
    {
        compileError_.clear();
        if (pipeline_ != nullptr)
        {
            SDL_ReleaseGPUComputePipeline(owner_->Device(), pipeline_);
            pipeline_ = nullptr;
        }
        if (!LooksLikeSpirvEXT(computeSrc.data(), computeSrc.size()))
        {
            compileError_ =
                "the SDL_GPU renderer consumes SPIR-V compute payloads only; this one is not "
                "SPIR-V";
            return false;
        }
        reflection_ = RemapSpirvForSdlGpuEXT(computeSrc.data(), computeSrc.size());
        if (!reflection_.error.empty())
        {
            compileError_ = "compute SPIR-V: " + reflection_.error;
            return false;
        }
        if (reflection_.stage != SpirvStageEXT::Compute)
        {
            compileError_ = "compute payload declares no GLCompute entry point";
            return false;
        }

        // CNA's XNA textures are float-sampled everywhere, so an integer sampler cannot legally
        // read one. Refusing here rather than at bind time is the point: the driver would happily
        // create a pipeline whose descriptor aliases a float image to an integer one, and the
        // wrong texels would arrive with nothing having failed.
        for (const SpirvResourceBindingEXT& resource : reflection_.resources)
        {
            if (resource.kind != SpirvResourceKindEXT::SampledTexture) continue;
            if (!resource.integerSampled) continue;
            compileError_ =
                "SDL_GPU compute shader: binding " + std::to_string(resource.originalBinding)
                + " is not a float32 sampler2D, and CNA's textures are float-sampled, so an "
                  "integer sampler cannot read one";
            return false;
        }

        SDL_GPUComputePipelineCreateInfo info{};
        info.code = reinterpret_cast<const Uint8*>(reflection_.words.data());
        info.code_size = reflection_.words.size() * sizeof(std::uint32_t);
        info.entrypoint = "main";
        info.format = SDL_GPU_SHADERFORMAT_SPIRV;
        info.num_samplers = reflection_.samplerCount;
        info.num_readonly_storage_textures = reflection_.readOnlyStorageTextureCount;
        info.num_readonly_storage_buffers = reflection_.readOnlyStorageBufferCount;
        info.num_readwrite_storage_textures = reflection_.readWriteStorageTextureCount;
        info.num_readwrite_storage_buffers = reflection_.readWriteStorageBufferCount;
        info.num_uniform_buffers = reflection_.uniformBufferCount;
        info.threadcount_x = std::max<Uint32>(1, reflection_.localSizeX);
        info.threadcount_y = std::max<Uint32>(1, reflection_.localSizeY);
        info.threadcount_z = std::max<Uint32>(1, reflection_.localSizeZ);

        pipeline_ = SDL_CreateGPUComputePipeline(owner_->Device(), &info);
        if (pipeline_ == nullptr)
        {
            compileError_ =
                std::string("SDL_CreateGPUComputePipeline failed: ") + SDL_GetError();
            return false;
        }
        uniformBlock_.assign(reflection_.uniformBlockBytes, 0);
        uniformBlockDirty_ = reflection_.uniformBlockBytes != 0;
        return true;
    }

    void SdlGpuComputeShaderRenderer::WriteNamedUniformEXT(
        const char* name, const void* data, const std::size_t byteCount)
    {
        if (name == nullptr) return;
        for (const SpirvUniformMemberEXT& member : reflection_.uniformMembers)
        {
            if (member.name != name) continue;
            if (member.byteOffset + byteCount > uniformBlock_.size()) return;
            std::memcpy(uniformBlock_.data() + member.byteOffset, data, byteCount);
            uniformBlockDirty_ = true;
            return;
        }
        // Not found. Silence here would look like a working uniform, so the refusal is recorded --
        // the shader package that wants named scalars has to keep its member names (see
        // SpirvUniformMemberEXT).
        compileError_ = std::string("compute shader declares no uniform member named '") + name
                      + "'; a package using named scalars must be compiled with "
                        "\"optimization\": \"zero\" so its OpMemberName survives";
    }

    void SdlGpuComputeShaderRenderer::SetUniformInt(const char* name, const int value)
    {
        WriteNamedUniformEXT(name, &value, sizeof(value));
    }

    void SdlGpuComputeShaderRenderer::SetUniformFloat(const char* name, const float value)
    {
        WriteNamedUniformEXT(name, &value, sizeof(value));
    }

    void SdlGpuComputeShaderRenderer::BindStorageBuffer(
        const int binding, IStorageBufferRenderer* buffer)
    {
        auto* native = dynamic_cast<SdlGpuStorageBufferRenderer*>(buffer);
        const auto existing = std::find_if(
            storageBuffers_.begin(), storageBuffers_.end(),
            [binding](const BoundBuffer& bound) { return bound.binding == binding; });
        if (existing != storageBuffers_.end())
        {
            existing->buffer = native;
            return;
        }
        storageBuffers_.push_back(BoundBuffer{binding, native});
    }

    bool SdlGpuComputeShaderRenderer::BindConstantBufferEXT(
        const int binding, IStorageBufferRenderer* buffer)
    {
        auto* native = dynamic_cast<SdlGpuStorageBufferRenderer*>(buffer);
        if (native == nullptr) return false;
        const auto existing = std::find_if(
            constantBuffers_.begin(), constantBuffers_.end(),
            [binding](const BoundBuffer& bound) { return bound.binding == binding; });
        if (existing != constantBuffers_.end())
        {
            existing->buffer = native;
            return true;
        }
        constantBuffers_.push_back(BoundBuffer{binding, native});
        return true;
    }

    namespace
    {
        /// Records one binding at `unit`, replacing any previous one there.
        void RecordBoundTexture(
            std::vector<SdlGpuComputeBoundTextureEXT>& into, const int unit,
            SDL_GPUTexture* texture, std::shared_ptr<const void> keepAlive, const bool writable)
        {
            for (auto& bound : into)
            {
                if (bound.unit != unit) continue;
                bound.texture = texture;
                bound.keepAlive = std::move(keepAlive);
                bound.writable = writable;
                return;
            }
            into.push_back(
                SdlGpuComputeBoundTextureEXT{unit, texture, std::move(keepAlive), writable});
        }
    }

    void SdlGpuComputeShaderRenderer::BindImageTexture(
        const int unit, ITextureRenderer* texture, int)
    {
        const SdlGpuSampledTextureEXT resolved =
            ResolveSampledTextureEXT(texture, "ComputeShader.BindImageTexture");
        RecordBoundTexture(storageTextures_, unit, resolved.texture, resolved.keepAlive, true);
    }

    bool SdlGpuComputeShaderRenderer::BindStorageTexture2DEXT(
        const int unit, std::shared_ptr<IStorageTexture2DRenderer> texture, int)
    {
        auto native = std::dynamic_pointer_cast<SdlGpuStorageTexture2DRenderer>(texture);
        if (native == nullptr) return false;
        RecordBoundTexture(storageTextures_, unit, native->Texture(), native,
                           native->WritableEXT());
        return true;
    }

    void SdlGpuComputeShaderRenderer::BindTexture(const int unit, ITextureRenderer* texture)
    {
        const SdlGpuSampledTextureEXT resolved =
            ResolveSampledTextureEXT(texture, "ComputeShader.BindTexture");
        RecordBoundTexture(sampledTextures_, unit, resolved.texture, resolved.keepAlive, false);
    }

    void SdlGpuComputeShaderRenderer::DispatchEXT(
        const int groupsX, const int groupsY, const int groupsZ)
    {
        if (pipeline_ == nullptr || groupsX <= 0 || groupsY <= 0 || groupsZ <= 0) return;

        // The ordering rule stated in the header: anything this dispatch may read has to have been
        // submitted, and on this renderer a draw is not submitted until the frame is.
        owner_->FlushPendingGpuWorkEXT();

        SDL_GPUDevice* device = owner_->Device();
        SDL_GPUCommandBuffer* cmd = SDL_AcquireGPUCommandBuffer(device);
        if (cmd == nullptr) return;

        // SDL splits the two directions: read-write bindings are declared when the pass opens, so
        // the driver can transition them once, and read-only ones are bound inside it.
        // Indexed BY SLOT, never appended. `SDL_BindGPUComputeStorageBuffers` takes a contiguous
        // array starting at a first-slot index, so a resource the caller left unbound must leave a
        // hole in place rather than shift every later binding down one -- which is what appending
        // did, and is exactly the kind of defect that produces a shader reading the wrong buffer
        // while nothing reports an error.
        std::vector<SDL_GPUStorageBufferReadWriteBinding> readWrite(
            reflection_.readWriteStorageBufferCount);
        std::vector<SDL_GPUBuffer*> readOnly(reflection_.readOnlyStorageBufferCount, nullptr);
        for (const SpirvResourceBindingEXT& resource : reflection_.resources)
        {
            if (resource.kind != SpirvResourceKindEXT::StorageBuffer) continue;
            const auto bound = std::find_if(
                storageBuffers_.begin(), storageBuffers_.end(),
                [&resource](const BoundBuffer& candidate) {
                    return static_cast<std::uint32_t>(candidate.binding) == resource.originalBinding;
                });
            SDL_GPUBuffer* buffer =
                bound != storageBuffers_.end() && bound->buffer != nullptr
                    ? bound->buffer->Buffer()
                    : nullptr;
            if (buffer == nullptr) continue;
            if (resource.readOnly)
            {
                if (resource.slot < readOnly.size()) readOnly[resource.slot] = buffer;
            }
            else if (resource.slot < readWrite.size())
            {
                readWrite[resource.slot].buffer = buffer;
                readWrite[resource.slot].cycle = false;
            }
        }

        // Storage images divide the same way, and by the same reflected read-only flag.
        std::vector<SDL_GPUStorageTextureReadWriteBinding> readWriteTextures(
            reflection_.readWriteStorageTextureCount);
        std::vector<SDL_GPUTexture*> readOnlyTextures(
            reflection_.readOnlyStorageTextureCount, nullptr);
        for (const SpirvResourceBindingEXT& resource : reflection_.resources)
        {
            if (resource.kind != SpirvResourceKindEXT::StorageTexture) continue;
            const auto bound = std::find_if(
                storageTextures_.begin(), storageTextures_.end(),
                [&resource](const SdlGpuComputeBoundTextureEXT& candidate) {
                    return static_cast<std::uint32_t>(candidate.unit) == resource.originalBinding;
                });
            if (bound == storageTextures_.end() || bound->texture == nullptr) continue;
            if (resource.readOnly)
            {
                if (resource.slot < readOnlyTextures.size())
                    readOnlyTextures[resource.slot] = bound->texture;
            }
            else if (resource.slot < readWriteTextures.size())
            {
                readWriteTextures[resource.slot].texture = bound->texture;
            }
        }

        SDL_GPUComputePass* pass = SDL_BeginGPUComputePass(
            cmd, readWriteTextures.data(), static_cast<Uint32>(readWriteTextures.size()),
            readWrite.data(), static_cast<Uint32>(readWrite.size()));
        if (pass == nullptr)
        {
            (void) SDL_SubmitGPUCommandBuffer(cmd);
            return;
        }
        SDL_BindGPUComputePipeline(pass, pipeline_);
        if (!readOnly.empty())
            SDL_BindGPUComputeStorageBuffers(
                pass, 0, readOnly.data(), static_cast<Uint32>(readOnly.size()));
        if (!readOnlyTextures.empty())
            SDL_BindGPUComputeStorageTextures(
                pass, 0, readOnlyTextures.data(),
                static_cast<Uint32>(readOnlyTextures.size()));

        // Every sampler the module declares must resolve to a real image: SDL writes a descriptor
        // for each, so one left unbound reaches the driver as null (SMG-0008 found the same shape
        // on the graphics path, as a crash inside the Vulkan driver rather than a diagnostic).
        if (reflection_.samplerCount > 0)
        {
            std::vector<SDL_GPUTextureSamplerBinding> samplers(reflection_.samplerCount);
            for (auto& binding : samplers)
            {
                binding.texture = owner_->AcquireDefaultWhiteTextureEXT();
                binding.sampler = owner_->AcquireComputeSamplerEXT();
            }
            for (const SpirvResourceBindingEXT& resource : reflection_.resources)
            {
                if (resource.kind != SpirvResourceKindEXT::SampledTexture) continue;
                if (resource.slot >= samplers.size()) continue;
                const auto bound = std::find_if(
                    sampledTextures_.begin(), sampledTextures_.end(),
                    [&resource](const SdlGpuComputeBoundTextureEXT& candidate) {
                        return static_cast<std::uint32_t>(candidate.unit)
                             == resource.originalBinding;
                    });
                if (bound != sampledTextures_.end() && bound->texture != nullptr)
                    samplers[resource.slot].texture = bound->texture;
            }
            if (!samplers.empty())
                SDL_BindGPUComputeSamplers(
                    pass, 0, samplers.data(), static_cast<Uint32>(samplers.size()));
        }

        // Uniform slot: whichever slot the module's own uniform block was assigned. A constant
        // buffer bound through BindConstantBufferEXT is pushed from its shadow copy, which is what
        // a uniform buffer is on SDL_gpu.
        for (const SpirvResourceBindingEXT& resource : reflection_.resources)
        {
            if (resource.kind != SpirvResourceKindEXT::UniformBuffer) continue;
            const auto constant = std::find_if(
                constantBuffers_.begin(), constantBuffers_.end(),
                [&resource](const BoundBuffer& candidate) {
                    return static_cast<std::uint32_t>(candidate.binding) == resource.originalBinding;
                });
            if (constant != constantBuffers_.end() && constant->buffer != nullptr)
            {
                const std::vector<std::uint8_t>& bytes = constant->buffer->ShadowEXT();
                if (!bytes.empty())
                    SDL_PushGPUComputeUniformData(
                        cmd, resource.slot, bytes.data(), static_cast<Uint32>(bytes.size()));
                continue;
            }
            if (!uniformBlock_.empty())
                SDL_PushGPUComputeUniformData(
                    cmd, resource.slot, uniformBlock_.data(),
                    static_cast<Uint32>(uniformBlock_.size()));
        }

        SDL_DispatchGPUCompute(pass, static_cast<Uint32>(groupsX), static_cast<Uint32>(groupsY),
                               static_cast<Uint32>(groupsZ));
        SDL_EndGPUComputePass(pass);
        (void) SDL_SubmitGPUCommandBuffer(cmd);
        uniformBlockDirty_ = false;
    }
}
