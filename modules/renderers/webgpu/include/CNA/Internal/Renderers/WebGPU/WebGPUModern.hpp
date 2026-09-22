// SPDX-License-Identifier: MS-PL
//
// plans/plan_webgpu_modern_graphics.md: the WebGPU half of the modern CNA Graphics API -- storage
// buffers, compute programs, sampled texture arrays, storage textures, GPU timers and the layout
// builder they share with the descriptor-contract `ShaderEffect` route.
//
// ORDERING (docs/adr/0001-modern-gpu-ordering-lifetime.md)
//
// ADR 0001 promises one device order for every public call. This renderer defers draws: a bind
// cycle's draws are recorded into render passes only when the cycle is flushed. Every modern
// operation here therefore first flushes the draws already queued (keeping the cycle's contents --
// a continuation loads, it never re-applies the cycle's discard or clear), then encodes its own work
// and SUBMITS it. Submission order is queue order, and `wgpuQueueWriteBuffer`/`WriteTexture` are
// queue operations ordered with submissions, so a CPU upload is seen by exactly the dispatches and
// draws issued after it. Nothing waits except a CPU readback, which waits for its own submission.
//
// LIFETIME
//
// Every record here is owned through shared lifetime by its public object, and every queued draw or
// dispatch that uses one retains it, so disposing the public object never invalidates accepted work.
// A record outliving its renderer is detached (`DetachOwnerEXT`) and refuses further work.

#pragma once

#include "CNA/Internal/Renderers/Common/IGraphicsRenderer.hpp"
#include "CNA/Internal/Renderers/WebGPU/WebGPUSampledTexture.hpp"
#include "CNA/Internal/Renderers/WebGPU/WebGPUWgslReflection.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace CNA::Internal::Renderers::WebGPU
{
    class WebGPURenderer;

    /**
     * @brief A modern resource that must stop using its renderer when the renderer goes away.
     *
     * The renderer keeps a registry of these and detaches every one from its destructor, so a
     * public object disposed after its `GraphicsDevice` never reaches a destroyed renderer.
     */
    class IWebGPUModernResourceEXT
    {
    public:
        /** @brief Virtual destructor. */
        virtual ~IWebGPUModernResourceEXT() = default;
        /** @brief Forgets the owning renderer; every later operation refuses. */
        virtual void DetachOwnerEXT() noexcept = 0;
    };

    /** @brief One binding of a program layout, merged across the stages that declare it. */
    struct WebGPUBindingSlotEXT
    {
        /** @brief Bind-group index. */
        std::uint32_t group = 0;
        /** @brief Binding index. */
        std::uint32_t binding = 0;
        /** @brief What the shader declared. */
        WgslResourceKind kind = WgslResourceKind::UniformBuffer;
        /** @brief Every stage that declares it. */
        WGPUShaderStage visibility = WGPUShaderStage_None;
        /** @brief Texture view dimension (sampled and storage textures). */
        WGPUTextureViewDimension viewDimension = WGPUTextureViewDimension_Undefined;
        /** @brief Sampled-texture sample type. */
        WGPUTextureSampleType sampleType = WGPUTextureSampleType_Undefined;
        /** @brief Sampled texture is multisampled. */
        bool multisampled = false;
        /** @brief Storage-texture format. */
        WGPUTextureFormat storageFormat = WGPUTextureFormat_Undefined;
        /** @brief Storage-texture access. */
        WGPUStorageTextureAccess storageAccess = WGPUStorageTextureAccess_Undefined;
        /** @brief Buffer minimum binding size. */
        std::uint64_t minBindingSize = 0;
        /** @brief Variable name, for diagnostics. */
        std::string name;
        /** @brief Declared type, whitespace-normalised. */
        std::string type;
    };

    /**
     * @brief Explicit bind-group and pipeline layouts derived from a program's WGSL.
     *
     * Shared (through `shared_ptr`) by every pipeline a program builds and by every queued command
     * that binds resources for it, so the native layouts outlive both the program object and any
     * accepted work.
     */
    class WebGPUProgramLayoutEXT
    {
    public:
        /** @brief The maximum bind groups a program may use; WebGPU's guaranteed `maxBindGroups`. */
        static constexpr std::uint32_t kMaxGroups = 4;

        /**
         * @brief Builds the layouts for a program from its stages' reflections.
         *
         * A binding declared by several stages must be declared identically; its visibility is the
         * union of those stages.
         *
         * @param device The device to create the layouts on.
         * @param stages Each stage's reflection and its stage flag.
         * @param error Receives the reason on failure.
         * @return The layout, or null with @p error set.
         */
        [[nodiscard]] static std::shared_ptr<WebGPUProgramLayoutEXT> Create(
            WGPUDevice device,
            const std::vector<std::pair<const WgslModuleReflection*, WGPUShaderStage>>& stages,
            std::string& error);

        /** @brief Releases the native layouts. */
        ~WebGPUProgramLayoutEXT();

        WebGPUProgramLayoutEXT(const WebGPUProgramLayoutEXT&) = delete;
        WebGPUProgramLayoutEXT& operator=(const WebGPUProgramLayoutEXT&) = delete;

        /** @brief The pipeline layout. @return Never null for a created layout. */
        [[nodiscard]] WGPUPipelineLayout PipelineLayout() const noexcept { return pipelineLayout_; }
        /** @brief The number of bind groups the pipeline layout declares (highest used + 1). */
        [[nodiscard]] std::uint32_t GroupCount() const noexcept { return groupCount_; }
        /**
         * @brief One group's bind-group layout (an empty layout for a gap).
         * @param group Group index below @ref GroupCount.
         * @return The layout.
         */
        [[nodiscard]] WGPUBindGroupLayout GroupLayout(std::uint32_t group) const noexcept
        {
            return layouts_[group];
        }
        /**
         * @brief One group's bindings, ordered by binding index.
         * @param group Group index below @ref kMaxGroups.
         * @return The bindings.
         */
        [[nodiscard]] const std::vector<WebGPUBindingSlotEXT>& Group(std::uint32_t group) const
        {
            return groups_[group];
        }
        /**
         * @brief Finds one binding.
         * @param group Group index.
         * @param binding Binding index.
         * @return The binding, or null when the program declares none there.
         */
        [[nodiscard]] const WebGPUBindingSlotEXT* Find(std::uint32_t group,
                                                        std::uint32_t binding) const noexcept;

    private:
        WebGPUProgramLayoutEXT() = default;

        std::array<std::vector<WebGPUBindingSlotEXT>, kMaxGroups> groups_{};
        std::array<WGPUBindGroupLayout, kMaxGroups> layouts_{};
        WGPUPipelineLayout pipelineLayout_ = nullptr;
        std::uint32_t groupCount_ = 0;
    };

    /**
     * @brief A WebGPU buffer implementing `CNA::Graphics::StorageBuffer`.
     *
     * The native usage is the declared portable usage plus Storage, CopySrc and CopyDst: every
     * record can be read back, written and byte-copied (see `CopyBytesEXT`), whatever roles the
     * caller declared, because those are this renderer's own transfer mechanisms. The portable
     * usage and CPU-access masks are reported exactly as declared and validated by the public layer.
     */
    class WebGPUStorageBufferRenderer final : public IStorageBufferRenderer,
                                              public IWebGPUModernResourceEXT
    {
    public:
        /**
         * @brief Creates the native buffer.
         * @param owner The renderer.
         * @param byteSize Positive logical size.
         * @param usage Raw `StorageBufferUsage` bits.
         * @param cpuAccess Raw `StorageBufferCpuAccess` bits.
         */
        WebGPUStorageBufferRenderer(WebGPURenderer* owner, std::size_t byteSize,
                                    std::uint32_t usage, std::uint32_t cpuAccess);
        /** @brief Releases the native buffer; accepted work keeps its own reference. */
        ~WebGPUStorageBufferRenderer() override;

        /** @copydoc IStorageBufferRenderer::SetData */
        void SetData(const void* data, std::size_t byteSize) override;
        /** @copydoc IStorageBufferRenderer::GetData */
        void GetData(void* out, std::size_t byteSize) const override;
        /** @copydoc IStorageBufferRenderer::SetDataRangeEXT */
        bool SetDataRangeEXT(std::size_t byteOffset, const void* data,
                             std::size_t byteSize) override;
        /** @copydoc IStorageBufferRenderer::GetDataRangeEXT */
        bool GetDataRangeEXT(std::size_t byteOffset, void* out,
                             std::size_t byteSize) const override;
        /** @copydoc IStorageBufferRenderer::CopyToEXT */
        bool CopyToEXT(IStorageBufferRenderer& destination, std::size_t sourceByteOffset,
                       std::size_t destinationByteOffset, std::size_t byteSize) override;
        /** @brief The logical size in bytes. @return The size. */
        [[nodiscard]] std::size_t GetByteSize() const override { return byteSize_; }
        /** @brief The declared portable usage. @return Raw `StorageBufferUsage` bits. */
        [[nodiscard]] std::uint32_t GetUsageEXT() const override { return usage_; }
        /** @brief The declared CPU access. @return Raw `StorageBufferCpuAccess` bits. */
        [[nodiscard]] std::uint32_t GetCpuAccessEXT() const override { return cpuAccess_; }
        /** @copydoc IWebGPUModernResourceEXT::DetachOwnerEXT */
        void DetachOwnerEXT() noexcept override { owner_ = nullptr; }

        /** @brief The native buffer. @return The handle. */
        [[nodiscard]] WGPUBuffer Buffer() const noexcept { return buffer_; }
        /** @brief The native size: the logical size rounded up to four bytes. @return Bytes. */
        [[nodiscard]] std::uint64_t NativeSize() const noexcept { return nativeSize_; }
        /** @brief Whether this record belongs to @p owner. @param owner The renderer. @return True if so. */
        [[nodiscard]] bool IsOwnedByEXT(const WebGPURenderer* owner) const noexcept
        {
            return owner_ != nullptr && owner_ == owner;
        }

    private:
        void RequireOwner(const char* operation) const;

        WebGPURenderer* owner_ = nullptr;
        WGPUBuffer buffer_ = nullptr;
        std::size_t byteSize_ = 0;
        std::uint64_t nativeSize_ = 0;
        std::uint32_t usage_ = 0;
        std::uint32_t cpuAccess_ = 0;
    };

    /**
     * @brief A WebGPU sampled two-dimensional texture array (`CNA::Graphics::Texture2DArray`).
     */
    class WebGPUTexture2DArrayRenderer final : public ITexture2DArrayRenderer,
                                               public IWebGPUModernResourceEXT
    {
    public:
        /**
         * @brief Creates the native texture and its 2D-array view.
         * @param owner The renderer.
         * @param width Level-zero width.
         * @param height Level-zero height.
         * @param layers Layer count.
         * @param mipLevels Mip-level count.
         * @param format Native texel format.
         * @param bytesPerTexel Texel size of @p format.
         * @param usage Raw `Texture2DArrayUsage` bits.
         */
        WebGPUTexture2DArrayRenderer(WebGPURenderer* owner, int width, int height, int layers,
                                     int mipLevels, WGPUTextureFormat format, int bytesPerTexel,
                                     std::uint32_t usage);
        /** @brief Releases the native texture and view. */
        ~WebGPUTexture2DArrayRenderer() override;

        /** @copydoc ITexture2DArrayRenderer::SetData */
        [[nodiscard]] bool SetData(int layer, int mipLevel, int x, int y, int width, int height,
                                   const void* data, std::size_t byteCount) override;
        /** @copydoc ITexture2DArrayRenderer::GetData */
        [[nodiscard]] bool GetData(int layer, int mipLevel, int x, int y, int width, int height,
                                   void* data, std::size_t byteCount) const override;
        /** @copydoc IWebGPUModernResourceEXT::DetachOwnerEXT */
        void DetachOwnerEXT() noexcept override { owner_ = nullptr; }

        /** @brief The sampleable 2D-array view. @return The view. */
        [[nodiscard]] WGPUTextureView View() const noexcept { return view_; }
        /** @brief The view plus the reference that keeps it alive past disposal. @return The binding. */
        [[nodiscard]] WebGPUSampledTextureEXT SampledEXT() const;
        /** @brief The native format. @return The format. */
        [[nodiscard]] WGPUTextureFormat Format() const noexcept { return format_; }
        /** @brief Whether this record belongs to @p owner. @param owner The renderer. @return True if so. */
        [[nodiscard]] bool IsOwnedByEXT(const WebGPURenderer* owner) const noexcept
        {
            return owner_ != nullptr && owner_ == owner;
        }

    private:
        WebGPURenderer* owner_ = nullptr;
        WGPUTexture texture_ = nullptr;
        WGPUTextureView view_ = nullptr;
        std::shared_ptr<const WebGPUSampledResourceEXT> keepAlive_;
        WGPUTextureFormat format_ = WGPUTextureFormat_Undefined;
        int width_ = 0;
        int height_ = 0;
        int layers_ = 0;
        int mipLevels_ = 0;
        int bytesPerTexel_ = 0;
    };

    /**
     * @brief A WebGPU storage texture (`CNA::Graphics::StorageTexture2D`).
     */
    class WebGPUStorageTexture2DRenderer final : public IStorageTexture2DRenderer,
                                                 public IWebGPUModernResourceEXT
    {
    public:
        /**
         * @brief Creates the native texture, a full sampled view and one storage view per level.
         * @param owner The renderer.
         * @param width Level-zero width.
         * @param height Level-zero height.
         * @param mipLevels Mip-level count.
         * @param format Native texel format.
         * @param bytesPerTexel Texel size of @p format.
         * @param usage Raw `StorageTexture2DUsage` bits.
         */
        WebGPUStorageTexture2DRenderer(WebGPURenderer* owner, int width, int height, int mipLevels,
                                       WGPUTextureFormat format, int bytesPerTexel,
                                       std::uint32_t usage);
        /** @brief Releases the native texture and views. */
        ~WebGPUStorageTexture2DRenderer() override;

        /** @copydoc IStorageTexture2DRenderer::SetData */
        [[nodiscard]] bool SetData(int mipLevel, int x, int y, int width, int height,
                                   const void* data, std::size_t byteCount) override;
        /** @copydoc IStorageTexture2DRenderer::GetData */
        [[nodiscard]] bool GetData(int mipLevel, int x, int y, int width, int height,
                                   void* data, std::size_t byteCount) const override;
        /** @copydoc IWebGPUModernResourceEXT::DetachOwnerEXT */
        void DetachOwnerEXT() noexcept override { owner_ = nullptr; }

        /** @brief The sampleable view over every level. @return The view. */
        [[nodiscard]] WGPUTextureView SampledView() const noexcept { return sampledView_; }
        /** @brief That view plus its keep-alive, or an empty binding when not sampleable. @return The binding. */
        [[nodiscard]] WebGPUSampledTextureEXT SampledEXT() const;
        /** @brief The level-zero storage view a compute image binding uses. @return The view. */
        [[nodiscard]] WGPUTextureView StorageView() const noexcept { return storageView_; }
        /** @brief The native format. @return The format. */
        [[nodiscard]] WGPUTextureFormat Format() const noexcept { return format_; }
        /** @brief The portable usage. @return Raw `StorageTexture2DUsage` bits. */
        [[nodiscard]] std::uint32_t Usage() const noexcept { return usage_; }
        /** @brief Whether this record belongs to @p owner. @param owner The renderer. @return True if so. */
        [[nodiscard]] bool IsOwnedByEXT(const WebGPURenderer* owner) const noexcept
        {
            return owner_ != nullptr && owner_ == owner;
        }

    private:
        WebGPURenderer* owner_ = nullptr;
        WGPUTexture texture_ = nullptr;
        WGPUTextureView sampledView_ = nullptr;
        WGPUTextureView storageView_ = nullptr;
        std::shared_ptr<const WebGPUSampledResourceEXT> keepAlive_;
        WGPUTextureFormat format_ = WGPUTextureFormat_Undefined;
        int width_ = 0;
        int height_ = 0;
        int mipLevels_ = 0;
        int bytesPerTexel_ = 0;
        std::uint32_t usage_ = 0;
    };

    /** @brief One texture unit's `SamplerState`, captured by value at a public draw call. */
    struct WebGPUSamplerStateEXT
    {
        /** @brief XNA `TextureFilter` ordinal. */
        int filter = 0;
        /** @brief XNA `TextureAddressMode` for U. */
        int addressU = 1;
        /** @brief ... for V. */
        int addressV = 1;
        /** @brief ... for W. */
        int addressW = 1;
        /** @brief `SamplerState.MaxMipLevel`. */
        int maxMipLevel = 0;
        /** @brief `SamplerState.MaxAnisotropy`. */
        int maxAnisotropy = 4;
    };

    /** @brief The four uniform arrays and the engine matrices a descriptor-contract effect carries. */
    struct WebGPUEffectArraysEXT
    {
        /** @brief `SetUniformFloatArray` values, one float each. */
        std::vector<float> floats;
        /** @brief `SetUniformVec2Array` values, two floats each. */
        std::vector<float> vec2s;
        /** @brief `SetUniformVec3Array` values, three floats each. */
        std::vector<float> vec3s;
        /** @brief `SetUniformMat4Array` values, sixteen floats each. */
        std::vector<float> mat4s;
        /** @brief The six engine-owned named matrices, column-major. */
        std::array<float, 6 * 16> engineMatrices{};
    };

    /**
     * @brief Everything a descriptor-contract `ShaderEffect` draw reads, captured at the draw call.
     *
     * plans/plan_webgpu_modern_graphics.md WMG-0006. This renderer records a draw and issues it
     * later, so nothing here may be a pointer into effect state a later call can change: the scalar
     * block is a copy, the arrays are a shared immutable snapshot (copy-on-write in the effect), and
     * every texture is an already-resolved view with its own keep-alive.
     */
    struct WebGPUDescriptorEffectSnapshotEXT
    {
        /** @brief The 128-byte scalar block: viewport size, matrix, vector and scalar slots. */
        std::array<float, 32> scalars{};
        /** @brief The uniform arrays, or null when the effect never set one. */
        std::shared_ptr<const WebGPUEffectArraysEXT> arrays;
        /** @brief Sampled 2D textures bound to units 0..3 (a storage texture binds here too). */
        std::array<WebGPUSampledTextureEXT, 4> textures2D{};
        /** @brief Cube textures bound to units 0..3. */
        std::array<WebGPUSampledTextureEXT, 4> cubes{};
        /** @brief Volume textures bound to units 0..3. */
        std::array<WebGPUSampledTextureEXT, 4> volumes{};
        /** @brief Texture arrays bound to units 0..2. */
        std::array<WebGPUSampledTextureEXT, 3> textureArrays{};
        /** @brief The device's sampler state per unit at the draw. */
        std::array<WebGPUSamplerStateEXT, 16> samplers{};
        /** @brief The draw's own texture: `GpuDrawParams::texture0`, or the sprite's. */
        WebGPUSampledTextureEXT primaryTexture;
        /** @brief Storage buffers bound for the draw, by `@group(2)` binding. */
        std::vector<std::pair<std::uint32_t, std::shared_ptr<WebGPUStorageBufferRenderer>>> storageBuffers;
        /** @brief Sprite route only: six vertices as position (pixels), uv and colour. */
        std::array<float, 6 * 8> spriteVertices{};
        /** @brief Sprite route only: whether @ref spriteVertices was filled. */
        bool hasSpriteVertices = false;
    };

    /**
     * @brief A WGSL compute program (`CNA::Graphics::ComputeShader`).
     *
     * The binding contract (docs/webgpu-renderer.md, "Modern shader payloads"): every resource in
     * `@group(0)`, addressed by its binding number -- storage and uniform buffers, sampled textures
     * (their sampler at binding + 32) and storage textures -- and the named scalars
     * `setUniform` writes in one `var<uniform>` struct at `@group(3) @binding(0)`, addressed by
     * member name. This is exactly what the package generator produces from a Vulkan compute
     * program, whose set-0 bindings and push-constant block it maps there.
     */
    class WebGPUComputeShaderRenderer final : public IComputeShaderRenderer,
                                              public IWebGPUModernResourceEXT
    {
    public:
        /** @brief The group holding the named-scalar block. */
        static constexpr std::uint32_t kScalarGroup = 3;
        /** @brief Binding offset of a sampled texture's sampler. */
        static constexpr std::uint32_t kSamplerBindingOffset = 32;

        /**
         * @brief Creates and compiles the program.
         * @param owner The renderer.
         * @param source WGSL source.
         */
        WebGPUComputeShaderRenderer(WebGPURenderer* owner, const std::string& source);
        /** @brief Releases the program. */
        ~WebGPUComputeShaderRenderer() override;

        /** @copydoc IComputeShaderRenderer::CompileProgram */
        bool CompileProgram(const std::string& computeSrc) override;
        /** @brief Nothing to do: a dispatch names its program. */
        void Bind() override {}
        /** @copydoc IComputeShaderRenderer::SetUniformInt */
        void SetUniformInt(const char* name, int value) override;
        /** @copydoc IComputeShaderRenderer::SetUniformFloat */
        void SetUniformFloat(const char* name, float value) override;
        /** @copydoc IComputeShaderRenderer::BindStorageBuffer */
        void BindStorageBuffer(int binding, IStorageBufferRenderer* buffer) override;
        /** @copydoc IComputeShaderRenderer::BindConstantBufferEXT */
        [[nodiscard]] bool BindConstantBufferEXT(int binding, IStorageBufferRenderer* buffer) override;
        /** @copydoc IComputeShaderRenderer::BindImageTexture */
        void BindImageTexture(int unit, ITextureRenderer* texture, int accessMode) override;
        /** @copydoc IComputeShaderRenderer::BindStorageTexture2DEXT */
        [[nodiscard]] bool BindStorageTexture2DEXT(
            int unit, std::shared_ptr<IStorageTexture2DRenderer> texture, int accessMode) override;
        /** @copydoc IComputeShaderRenderer::BindTexture */
        void BindTexture(int unit, ITextureRenderer* texture) override;
        /** @brief True: the unit is the WGSL binding number. @return True. */
        [[nodiscard]] bool UsesDirectSampledTextureBindingsEXT() const override { return true; }
        /** @copydoc IComputeShaderRenderer::IsValid */
        [[nodiscard]] bool IsValid() const override { return pipeline_ != nullptr; }
        /** @copydoc IComputeShaderRenderer::GetCompileError */
        [[nodiscard]] std::string GetCompileError() const override { return compileError_; }
        /** @copydoc IWebGPUModernResourceEXT::DetachOwnerEXT */
        void DetachOwnerEXT() noexcept override { owner_ = nullptr; }

        /**
         * @brief Validates the bindings and hands one immutable dispatch to the renderer.
         * @param groupsX Work groups on x.
         * @param groupsY Work groups on y.
         * @param groupsZ Work groups on z.
         */
        void DispatchEXT(int groupsX, int groupsY, int groupsZ);

    private:
        struct ScalarMember
        {
            std::uint32_t offset = 0;
            bool isFloat = false;
        };

        void ReleaseProgramEXT() noexcept;
        void WriteScalar(const char* name, const void* value, bool isFloat);

        WebGPURenderer* owner_ = nullptr;
        WGPUShaderModule module_ = nullptr;
        WGPUComputePipeline pipeline_ = nullptr;
        std::shared_ptr<WebGPUProgramLayoutEXT> layout_;
        std::string entryPoint_;
        std::string compileError_;
        std::unordered_map<std::string, ScalarMember> scalars_;
        std::vector<std::uint8_t> scalarBytes_;
        std::unordered_map<std::uint32_t, std::shared_ptr<WebGPUStorageBufferRenderer>> buffers_;
        std::unordered_map<std::uint32_t, std::shared_ptr<WebGPUStorageTexture2DRenderer>> images_;
        /// WMG-0008: a Texture2D bound as a compute image, by its own storage view.
        std::unordered_map<std::uint32_t, WebGPUSampledTextureEXT> imageTextures_;
        std::unordered_map<std::uint32_t, WebGPUSampledTextureEXT> textures_;
    };

    /**
     * @brief A GPU timer over timestamp queries (`CNA::Graphics::GpuTimer`).
     *
     * `Begin()` and `End()` each become a one-entry timestamp write, submitted in public order (the
     * pending draws before each are flushed first), and resolved into a mappable buffer. The result
     * becomes available once that buffer maps; `IsResultAvailable()` polls without blocking.
     */
    class WebGPUGpuTimerRenderer final : public IGpuTimerRenderer, public IWebGPUModernResourceEXT
    {
    public:
        /**
         * @brief Creates the two-entry query set and its resolve/readback buffers.
         * @param owner The renderer.
         */
        explicit WebGPUGpuTimerRenderer(WebGPURenderer* owner);
        /** @brief Releases the query set and buffers. */
        ~WebGPUGpuTimerRenderer() override;

        /** @copydoc IGpuTimerRenderer::Begin */
        void Begin() override;
        /** @copydoc IGpuTimerRenderer::End */
        void End() override;
        /** @copydoc IGpuTimerRenderer::IsResultAvailable */
        [[nodiscard]] bool IsResultAvailable() const override;
        /** @copydoc IGpuTimerRenderer::ElapsedNanoseconds */
        [[nodiscard]] std::uint64_t ElapsedNanoseconds() const override;
        /** @copydoc IWebGPUModernResourceEXT::DetachOwnerEXT */
        void DetachOwnerEXT() noexcept override { owner_ = nullptr; }

    private:
        struct MapState;
        void Poll() const;

        WebGPURenderer* owner_ = nullptr;
        WGPUQuerySet querySet_ = nullptr;
        WGPUBuffer resolveBuffer_ = nullptr;
        WGPUBuffer readbackBuffer_ = nullptr;
        bool began_ = false;
        mutable bool mapping_ = false;
        mutable bool available_ = false;
        mutable std::uint64_t elapsed_ = 0;
        std::shared_ptr<MapState> mapState_;
    };
}
