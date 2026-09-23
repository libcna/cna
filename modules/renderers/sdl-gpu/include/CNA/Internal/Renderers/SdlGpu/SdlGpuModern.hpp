// SPDX-License-Identifier: MS-PL
#pragma once

/**
 * @file SdlGpuModern.hpp
 * @brief plans/plan_sdlgpu_modern_graphics.md SMG-0012: the CNAEXT engine layer's own resources on
 *        `SDL_gpu` -- storage buffers, storage textures, texture arrays and compute.
 *
 * Kept out of `SdlGpuRenderer.hpp` for the reason the WebGPU renderer keeps its modern work in
 * `WebGPUModern.hpp`: none of this exists on the classic XNA path, and interleaving it into an
 * already twelve-thousand-line translation unit makes both harder to read.
 *
 * **The ordering rule this renderer needs and the reference renderers do not.** `SdlGpuRenderer`
 * defers every draw to `Present()` (see `PassSegment`), so at the moment the engine layer asks for
 * a dispatch there may be draws queued and not yet submitted. Compute therefore **flushes the
 * pending frame first** (`EnsureFrameRendered()`) and then records its own command buffer, which
 * is the same thing the existing readback path does before `GetData`. With that, SDL's own
 * submission order gives `render -> compute -> render` and `compute write -> graphics read` for
 * free, and no barrier has to be reasoned about: within one command buffer SDL inserts the
 * barriers its resource states imply, and between two command buffers submission order is
 * execution order.
 *
 * **What `SDL_gpu` cannot be asked for, recorded here so it is not rediscovered.** There is no
 * uniform *buffer* usage at all: `SDL_GPU_BUFFERUSAGE_*` has vertex, index, indirect and the four
 * storage combinations, and uniform data reaches a shader only through
 * `SDL_PushGPU*UniformData`. A `StorageBufferUsage::Constant` buffer is therefore backed here by
 * the shadow copy every buffer already keeps, and pushed as uniform data at bind time -- which is
 * SDL's model for a constant buffer, not an emulation of one.
 */

#include "CNA/CNAHelper.hpp"
#include "CNA/Internal/Renderers/Common/IGraphicsRenderer.hpp"
#include "CNA/Internal/Renderers/SdlGpu/SdlGpuSpirvBindings.hpp"

#include <SDL3/SDL_gpu.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace CNA::Internal::Renderers::SdlGpu
{
    class SdlGpuRenderer;

    /**
     * @brief `SDL_gpu`-backed `CNA::Graphics::StorageBuffer`. CNAEXT.
     *
     * One `SDL_GPUBuffer` plus a CPU shadow copy. The shadow is not a cache: it is what a
     * `Constant`-usage buffer is pushed from (SDL has no uniform-buffer resource), and what a
     * partial `SetDataRangeEXT` merges into before the range is uploaded.
     */
    class SdlGpuStorageBufferRenderer final : public IStorageBufferRenderer
    {
    public:
        /**
         * @brief Allocates the native buffer for one descriptor.
         *
         * @param owner Owning renderer; supplies the device.
         * @param byteSize Size in bytes; must be positive.
         * @param usage `CNA::Graphics::StorageBufferUsage` mask.
         * @param cpuAccess `CNA::Graphics::StorageBufferCpuAccess` mask.
         * @throws std::runtime_error If `SDL_CreateGPUBuffer` fails.
         */
        SdlGpuStorageBufferRenderer(SdlGpuRenderer& owner, std::size_t byteSize,
                                    std::uint32_t usage, std::uint32_t cpuAccess);
        /** @brief Releases the native buffer through the renderer's deferred-release queue. */
        ~SdlGpuStorageBufferRenderer() override;

        SdlGpuStorageBufferRenderer(const SdlGpuStorageBufferRenderer&) = delete;
        SdlGpuStorageBufferRenderer& operator=(const SdlGpuStorageBufferRenderer&) = delete;

        /** @brief Replaces the whole buffer. @param data Source. @param byteSize Bytes. */
        void SetData(const void* data, std::size_t byteSize) override;
        /** @brief Reads the whole buffer back. @param out Destination. @param byteSize Bytes. */
        void GetData(void* out, std::size_t byteSize) const override;
        /** @brief Replaces one range. @param byteOffset Start. @param data Source. @param byteSize Bytes.
         *  @return False when the range leaves the buffer. */
        bool SetDataRangeEXT(std::size_t byteOffset, const void* data,
                             std::size_t byteSize) override;
        /** @brief Reads one range. @param byteOffset Start. @param out Destination. @param byteSize Bytes.
         *  @return False when the range leaves the buffer. */
        bool GetDataRangeEXT(std::size_t byteOffset, void* out,
                             std::size_t byteSize) const override;
        /** @brief GPU-side copy into another buffer. @param destination Target. @param sourceByteOffset Source start.
         *  @param destinationByteOffset Target start. @param byteSize Bytes. @return False when refused. */
        bool CopyToEXT(IStorageBufferRenderer& destination, std::size_t sourceByteOffset,
                       std::size_t destinationByteOffset, std::size_t byteSize) override;
        /** @brief Buffer size in bytes. @return The size supplied at construction. */
        [[nodiscard]] std::size_t GetByteSize() const override { return byteSize_; }
        /** @brief Declared usage mask. @return The mask supplied at construction. */
        [[nodiscard]] std::uint32_t GetUsageEXT() const override { return usage_; }
        /** @brief Declared CPU-access mask. @return The mask supplied at construction. */
        [[nodiscard]] std::uint32_t GetCpuAccessEXT() const override { return cpuAccess_; }

        /** @brief The native buffer. CNAEXT -- internal use only. @return The `SDL_GPUBuffer`. */
        CNAEXT [[nodiscard]] SDL_GPUBuffer* Buffer() const noexcept { return buffer_; }
        /** @brief The CPU shadow copy, used for constant pushes. CNAEXT. @return The bytes. */
        CNAEXT [[nodiscard]] const std::vector<std::uint8_t>& ShadowEXT() const noexcept
        {
            return shadow_;
        }
        /** @brief Refreshes the shadow copy from the GPU. CNAEXT. @return False if readback failed. */
        CNAEXT bool RefreshShadowFromGpuEXT() const;

    private:
        SdlGpuRenderer* owner_ = nullptr;
        SDL_GPUBuffer* buffer_ = nullptr;
        std::size_t byteSize_ = 0;
        std::uint32_t usage_ = 0;
        std::uint32_t cpuAccess_ = 0;
        mutable std::vector<std::uint8_t> shadow_;
    };

    /**
     * @brief `SDL_gpu`-backed `CNA::Graphics::StorageTexture2D`. CNAEXT.
     *
     * One `SDL_GPUTexture` carrying whichever of the compute storage-read, storage-write and
     * sampler usages the descriptor asked for. `SDL_gpu` has no separate storage-image object: a
     * texture is a storage image exactly when it was created with those usage bits.
     */
    class SdlGpuStorageTexture2DRenderer final : public IStorageTexture2DRenderer
    {
    public:
        /**
         * @brief Allocates the native texture for one descriptor.
         *
         * @param owner Owning renderer.
         * @param width Width in texels; positive.
         * @param height Height in texels; positive.
         * @param mipLevelCount Levels to allocate; at least one.
         * @param surfaceFormat `SurfaceFormat` ordinal.
         * @param usage `CNA::Graphics::StorageTexture2DUsage` mask.
         * @throws std::runtime_error If the format is unsupported or creation fails.
         */
        SdlGpuStorageTexture2DRenderer(SdlGpuRenderer& owner, int width, int height,
                                       int mipLevelCount, int surfaceFormat,
                                       std::uint32_t usage);
        /** @brief Releases the native texture. */
        ~SdlGpuStorageTexture2DRenderer() override;

        SdlGpuStorageTexture2DRenderer(const SdlGpuStorageTexture2DRenderer&) = delete;
        SdlGpuStorageTexture2DRenderer& operator=(const SdlGpuStorageTexture2DRenderer&) = delete;

        /** @brief Uploads one region. @param mipLevel Level. @param x Left. @param y Top.
         *  @param width Width. @param height Height. @param data Source. @param byteCount Bytes.
         *  @return False when the region or size is wrong. */
        [[nodiscard]] bool SetData(int mipLevel, int x, int y, int width, int height,
                                   const void* data, std::size_t byteCount) override;
        /** @brief Reads one region back. @param mipLevel Level. @param x Left. @param y Top.
         *  @param width Width. @param height Height. @param data Destination. @param byteCount Bytes.
         *  @return False when the region or size is wrong. */
        [[nodiscard]] bool GetData(int mipLevel, int x, int y, int width, int height,
                                   void* data, std::size_t byteCount) const override;

        /** @brief The native texture. CNAEXT. @return The `SDL_GPUTexture`. */
        CNAEXT [[nodiscard]] SDL_GPUTexture* Texture() const noexcept { return texture_; }
        /** @brief Whether a compute shader may write it. CNAEXT. @return True when write usage was asked for. */
        CNAEXT [[nodiscard]] bool WritableEXT() const noexcept { return writable_; }

    private:
        SdlGpuRenderer* owner_ = nullptr;
        SDL_GPUTexture* texture_ = nullptr;
        int width_ = 0;
        int height_ = 0;
        int levelCount_ = 1;
        int surfaceFormat_ = 0;
        int bytesPerTexel_ = 4;
        bool writable_ = false;
    };

    /**
     * @brief Maps a `SurfaceFormat` ordinal to its `SDL_gpu` storage-image format. CNAEXT.
     *
     * The narrow set CNA's engine layer asks a storage image for. Shared with the renderer's
     * format-usage reporting so "this format is refused" and "this format has no equivalent" are
     * the same decision in both places.
     *
     * @param surfaceFormat `SurfaceFormat` ordinal.
     * @param out Receives the native format on success.
     * @param bytesPerTexel Receives the texel size on success.
     * @return False when the format has no storage-image equivalent here.
     */
    [[nodiscard]] bool TranslateStorageImageFormatEXT(
        int surfaceFormat, SDL_GPUTextureFormat& out, int& bytesPerTexel);

    /**
     * @brief One sampled or storage texture a compute shader has been told to bind. CNAEXT.
     *
     * `keepAlive` is what stops a texture the caller has since destroyed from being sampled by a
     * dispatch that still names it; the same reason `SdlGpuSampledTextureEXT` carries one.
     */
    struct SdlGpuComputeBoundTextureEXT
    {
        /** @brief Unit the shader names. */
        int unit = 0;
        /** @brief Native texture, or null when the unit was cleared. */
        SDL_GPUTexture* texture = nullptr;
        /** @brief Keeps the owning resource alive while a dispatch can reference it. */
        std::shared_ptr<const void> keepAlive;
        /** @brief True when the shader may write through this binding. */
        bool writable = false;
    };

    /**
     * @brief `SDL_gpu`-backed `CNA::Graphics::ComputeShader`. CNAEXT.
     *
     * Takes SPIR-V only, through the same descriptor translation the graphics path uses. The
     * pipeline is created on first successful compile, because `SDL_CreateGPUComputePipeline`
     * wants the workgroup size and the per-category resource counts, all of which the reflection
     * already produced.
     */
    class SdlGpuComputeShaderRenderer final : public IComputeShaderRenderer
    {
    public:
        /** @brief Binds this shader to a renderer. @param owner Owning renderer. */
        explicit SdlGpuComputeShaderRenderer(SdlGpuRenderer& owner);
        /** @brief Releases the compute pipeline. */
        ~SdlGpuComputeShaderRenderer() override;

        SdlGpuComputeShaderRenderer(const SdlGpuComputeShaderRenderer&) = delete;
        SdlGpuComputeShaderRenderer& operator=(const SdlGpuComputeShaderRenderer&) = delete;

        /** @brief Creates the pipeline from a SPIR-V payload. @param computeSrc Module bytes.
         *  @return True on success; false with `GetCompileError()` set. */
        bool CompileProgram(const std::string& computeSrc) override;
        /** @brief No-op: this renderer binds at dispatch, which is where the pass exists. */
        void Bind() override {}
        /** @brief Writes a named `int` scalar. @param name Member name. @param value Value. */
        void SetUniformInt(const char* name, int value) override;
        /** @brief Writes a named `float` scalar. @param name Member name. @param value Value. */
        void SetUniformFloat(const char* name, float value) override;
        /** @brief Binds a storage buffer. @param binding Shader binding. @param buffer Buffer or null. */
        void BindStorageBuffer(int binding, IStorageBufferRenderer* buffer) override;
        /** @brief Binds a buffer as a constant block. @param binding Binding. @param buffer Buffer.
         *  @return True when accepted. */
        [[nodiscard]] bool BindConstantBufferEXT(int binding,
                                                 IStorageBufferRenderer* buffer) override;
        /** @brief Binds a storage image. @param unit Unit. @param texture Texture. @param accessMode Access. */
        void BindImageTexture(int unit, ITextureRenderer* texture, int accessMode) override;
        /** @brief Binds a storage texture. @param unit Unit. @param texture Texture. @param accessMode Access.
         *  @return True when accepted. */
        [[nodiscard]] bool BindStorageTexture2DEXT(
            int unit, std::shared_ptr<IStorageTexture2DRenderer> texture, int accessMode) override;
        /** @brief Binds a sampled texture. @param unit Unit. @param texture Texture. */
        void BindTexture(int unit, ITextureRenderer* texture) override;
        /** @brief True: sampled textures bind at the unit the shader names. @return True. */
        [[nodiscard]] bool UsesDirectSampledTextureBindingsEXT() const override { return true; }
        /** @brief Whether the pipeline exists. @return True after a successful compile. */
        [[nodiscard]] bool IsValid() const override { return pipeline_ != nullptr; }
        /** @brief The last compile diagnostic. @return Empty when valid. */
        [[nodiscard]] std::string GetCompileError() const override { return compileError_; }

        /** @brief Records the dispatch onto its own command buffer. CNAEXT.
         *  @param groupsX Workgroups in X. @param groupsY In Y. @param groupsZ In Z. */
        CNAEXT void DispatchEXT(int groupsX, int groupsY, int groupsZ);

    private:
        /// One resource this shader has been told to bind, resolved at dispatch.
        struct BoundBuffer
        {
            int binding = 0;
            SdlGpuStorageBufferRenderer* buffer = nullptr;
        };

        SdlGpuRenderer* owner_ = nullptr;
        SDL_GPUComputePipeline* pipeline_ = nullptr;
        std::string compileError_;
        SpirvRemapResultEXT reflection_;
        std::vector<BoundBuffer> storageBuffers_;
        std::vector<BoundBuffer> constantBuffers_;
        std::vector<SdlGpuComputeBoundTextureEXT> sampledTextures_;
        std::vector<SdlGpuComputeBoundTextureEXT> storageTextures_;
        std::vector<std::uint8_t> uniformBlock_;
        bool uniformBlockDirty_ = false;

        /// Writes `byteCount` bytes at the offset the module gave `name`, or records a refusal.
        void WriteNamedUniformEXT(const char* name, const void* data, std::size_t byteCount);
    };
}
