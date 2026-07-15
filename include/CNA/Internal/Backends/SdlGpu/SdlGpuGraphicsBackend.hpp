// SPDX-License-Identifier: MS-PL
#pragma once

#include "CNA/CNAHelper.hpp"
#include "../Common/IGraphicsBackend.hpp"

#include <SDL3/SDL_gpu.h>

#include <array>
#include <cstdint>
#include <unordered_map>
#include <utility>
#include <vector>

namespace CNA::Internal::Backends::SdlGpu
{
    class SdlGpuGraphicsBackend;
    class SdlGpuRenderTargetBackend;
    class SdlGpuRenderTargetCubeBackend;

    /** @brief `SDL_gpu`-backed `Texture2D`. Plain 2D, `SAMPLER` usage only (no mip chain yet). */
    class SdlGpuTextureBackend final : public ITextureBackend
    {
    public:
        SdlGpuTextureBackend(SdlGpuGraphicsBackend& owner, const ImageData& data);
        ~SdlGpuTextureBackend() override;

        SdlGpuTextureBackend(const SdlGpuTextureBackend&) = delete;
        SdlGpuTextureBackend& operator=(const SdlGpuTextureBackend&) = delete;

        [[nodiscard]] int GetWidth() const override { return width_; }
        [[nodiscard]] int GetHeight() const override { return height_; }
        [[nodiscard]] SDL_Texture* GetNativeTexture() const override { return nullptr; }
        void UpdatePixels(const uint8_t* rgba, int stride) override;

        /** @brief Returns the underlying `SDL_GPUTexture`. NOXNA — internal use only. */
        NOXNA [[nodiscard]] SDL_GPUTexture* Texture() const { return texture_; }

    private:
        SdlGpuGraphicsBackend* owner_ = nullptr;
        SDL_GPUTexture* texture_ = nullptr;
        int width_ = 0;
        int height_ = 0;
    };

    /**
     * @brief `SDL_gpu`-backed `Texture3D` (Phase `SDLGPU-9`, `SDLGPU-40`/`SDLGPU-41`).
     *
     * A single `SDL_GPU_TEXTURETYPE_3D` texture, `SAMPLER` usage only (never a render target).
     * `SetData`/`GetData` both carry an explicit mip `level` straight through to
     * `SDL_GPUTextureRegion.mip_level`, so authored per-level mip data (SDLGPU-41) needs no special
     * handling beyond the region upload/download itself. When `mipMap` was requested at
     * construction, a full level-0 `SetData` additionally triggers a real
     * `SDL_GenerateMipmapsForGPUTexture` pass immediately afterward (the "generated case" — real
     * XNA/FNA has no explicit "regenerate mips" call for `Texture3D`, so this is the natural
     * trigger point).
     */
    class SdlGpuTexture3DBackend final : public ITexture3DBackend
    {
    public:
        SdlGpuTexture3DBackend(SdlGpuGraphicsBackend& owner, int width, int height, int depth, bool mipMap);
        ~SdlGpuTexture3DBackend() override;

        SdlGpuTexture3DBackend(const SdlGpuTexture3DBackend&) = delete;
        SdlGpuTexture3DBackend& operator=(const SdlGpuTexture3DBackend&) = delete;

        void SetData(int level, int x, int y, int z, int w, int h, int depth,
                    const void* data, int dataLength) override;
        void GetData(int level, int x, int y, int z, int w, int h, int depth,
                    void* data, int dataLength) const override;

    private:
        SdlGpuGraphicsBackend* owner_ = nullptr;
        SDL_GPUTexture* texture_ = nullptr;
        int width_ = 0;
        int height_ = 0;
        int depth_ = 0;
        bool mipMap_ = false;
    };

    /**
     * @brief `SDL_gpu`-backed `RenderTarget2D` (Phase `SDLGPU-8`, `SDLGPU-35`/`SDLGPU-38`).
     *
     * Owns a standalone `COLOR_TARGET | SAMPLER` texture rendered into its own render pass, one
     * per frame (see `SdlGpuGraphicsBackend::EnsureFrameRendered`'s per-target grouping) --
     * offscreen passes run before the swapchain pass so a target bound-then-unbound earlier in
     * the frame can safely be sampled by a later swapchain-targeted draw within the same frame.
     * MSAA (`SDLGPU-38`) resolves automatically via `SDL_GPUColorTargetInfo.resolve_texture` at
     * render-pass end -- no manual resolve step needed, same mechanism as
     * `SdlGpuRenderTargetCubeBackend`'s own MSAA support.
     */
    class SdlGpuRenderTargetBackend final : public IRenderTargetBackend
    {
    public:
        SdlGpuRenderTargetBackend(SdlGpuGraphicsBackend& owner, int width, int height,
                                  int depthFormat, bool mipMap, int multiSampleCount);
        ~SdlGpuRenderTargetBackend() override;

        SdlGpuRenderTargetBackend(const SdlGpuRenderTargetBackend&) = delete;
        SdlGpuRenderTargetBackend& operator=(const SdlGpuRenderTargetBackend&) = delete;

        [[nodiscard]] int GetWidth() const override { return width_; }
        [[nodiscard]] int GetHeight() const override { return height_; }
        [[nodiscard]] SDL_Texture* GetNativeTexture() const override { return nullptr; }

        void BindAsRenderTarget() override;
        void UnbindAsRenderTarget() override;
        [[nodiscard]] int GetMultiSampleCount() const override { return multiSampleCount_; }
        [[nodiscard]] bool HasRealDepthBuffer(bool depthFormatWasRequested) const override
        {
            return depthFormatWasRequested && depthTexture_ != nullptr;
        }

        /** @brief Returns the sampleable (single-sample, resolved-into-if-MSAA) color texture. NOXNA. */
        NOXNA [[nodiscard]] SDL_GPUTexture* ColorTexture() const { return colorTexture_; }
        /** @brief Returns the multisampled render texture, or null when not multisampled. NOXNA. */
        NOXNA [[nodiscard]] SDL_GPUTexture* MsaaTexture() const { return msaaTexture_; }
        /** @brief Returns the depth/stencil texture, or null when `DepthFormat::None` was requested. NOXNA. */
        NOXNA [[nodiscard]] SDL_GPUTexture* DepthTexture() const { return depthTexture_; }
        /** @brief Whether a mip chain should be regenerated after this target's pass each frame. NOXNA. */
        NOXNA [[nodiscard]] bool WantsMipMap() const { return mipMap_; }
        /**
         * @brief Real GPU readback of this target's pixels (`SDLGPU-39`) -- reads from the
         * always-single-sample, sampleable `colorTexture_` (already resolved-into if this target
         * is MSAA), a texture this backend fully owns, unlike the swapchain -- so it does not hit
         * the swapchain-download segfault documented in `plan_sdlgpu.md`'s `SDLGPU-39` row.
         * Flushes any pending frame first so the read reflects this frame's draws.
         */
        void GetData(int level, int x, int y, int w, int h, void* data, int dataLength) const override;

        /** @brief Queues a color-only clear, consumed on this target's next render pass. NOXNA. */
        NOXNA void QueueClear(SDL_FColor color) { clearColor_ = color; clearColorPending_ = true; }
        /** @brief Queues a depth clear, consumed on this target's next render pass. NOXNA. */
        NOXNA void QueueClearDepth(float depth) { clearDepth_ = depth; clearDepthPending_ = true; }
        /** @brief Queues a stencil clear, consumed on this target's next render pass. NOXNA. */
        NOXNA void QueueClearStencil(Uint8 stencil) { clearStencil_ = stencil; clearStencilPending_ = true; }
        NOXNA [[nodiscard]] bool ClearColorPending() const { return clearColorPending_; }
        NOXNA [[nodiscard]] bool ClearDepthPending() const { return clearDepthPending_; }
        NOXNA [[nodiscard]] bool ClearStencilPending() const { return clearStencilPending_; }
        NOXNA [[nodiscard]] SDL_FColor ClearColorValue() const { return clearColor_; }
        NOXNA [[nodiscard]] float ClearDepthValue() const { return clearDepth_; }
        NOXNA [[nodiscard]] Uint8 ClearStencilValue() const { return clearStencil_; }
        /** @brief Resets pending-clear flags after this target's pass has rendered this frame. NOXNA. */
        NOXNA void ResetPendingClears() { clearColorPending_ = false; clearDepthPending_ = false; clearStencilPending_ = false; }
        /**
         * @brief Registers this target for its own pass this frame without making it the current
         * draw target (SDLGPU-37: an "extra" MRT target — bound and clearable, but draws still go
         * only to the primary target). NOXNA — internal use only.
         */
        NOXNA void MarkUsedThisFrame();

    private:
        SdlGpuGraphicsBackend* owner_ = nullptr;
        int width_ = 0;
        int height_ = 0;
        bool mipMap_ = false;
        int multiSampleCount_ = 0;
        SDL_GPUTexture* colorTexture_ = nullptr;
        SDL_GPUTexture* msaaTexture_ = nullptr;
        SDL_GPUTexture* depthTexture_ = nullptr;
        bool clearColorPending_ = true;
        bool clearDepthPending_ = true;
        bool clearStencilPending_ = false;
        SDL_FColor clearColor_{0.0f, 0.0f, 0.0f, 1.0f};
        float clearDepth_ = 1.0f;
        Uint8 clearStencil_ = 0;
    };

    /**
     * @brief `SDL_gpu`-backed `RenderTargetCube` (Phase `SDLGPU-8`, `SDLGPU-36`).
     *
     * Owns a single `SDL_GPU_TEXTURETYPE_CUBE` texture (6 layers). Only one face is ever the
     * active render target at a time (matches `D3D11RenderTargetCubeBackend`'s/
     * `D3D12RenderTargetCubeBackend`'s own shared-depth-buffer convention) -- clear/depth state is
     * tracked per face, but there is one shared depth texture reused across whichever face is
     * currently bound. MSAA resolves automatically via `SDL_GPUColorTargetInfo.resolve_texture`/
     * `resolve_layer` at render-pass end (no manual resolve step needed, unlike D3D11/D3D12) --
     * the actual multisampled render target is a separate `2D_ARRAY` texture, since
     * `SDL_GPU_TEXTURETYPE_CUBE` has no multisampled variant. Mip regeneration
     * (`SDL_GenerateMipmapsForGPUTexture`) has no per-layer control, so it regenerates the whole
     * cube's chain (all 6 faces) whenever any face that requested mips was used in a frame.
     */
    class SdlGpuRenderTargetCubeBackend final : public IRenderTargetCubeBackend
    {
    public:
        SdlGpuRenderTargetCubeBackend(SdlGpuGraphicsBackend& owner, int size, int depthFormat,
                                      bool mipMap, int multiSampleCount);
        ~SdlGpuRenderTargetCubeBackend() override;

        SdlGpuRenderTargetCubeBackend(const SdlGpuRenderTargetCubeBackend&) = delete;
        SdlGpuRenderTargetCubeBackend& operator=(const SdlGpuRenderTargetCubeBackend&) = delete;

        [[nodiscard]] int GetSize() const override { return size_; }
        void BindAsRenderTargetFace(int face) override;
        void UnbindAsRenderTarget() override;
        [[nodiscard]] int GetMultiSampleCount() const override { return multiSampleCount_; }
        /**
         * @brief Real GPU readback of one face's pixels (pulled forward from `SDLGPU-39` -- this
         * targets a texture this backend fully controls, unlike the swapchain-download path that
         * segfaulted; see that row's notes in `plan_sdlgpu.md`). Flushes any pending frame first so
         * the read reflects this frame's draws, not stale/uninitialized GPU memory.
         */
        void GetData(int face, int level, int x, int y, int w, int h,
                    void* data, int dataLength) const override;

        /** @brief Returns the single-sample, sampleable cube texture. NOXNA — internal use only. */
        NOXNA [[nodiscard]] SDL_GPUTexture* CubeTexture() const { return cubeTexture_; }
        /** @brief Returns the multisampled 2D-array render texture, or null when not multisampled. NOXNA. */
        NOXNA [[nodiscard]] SDL_GPUTexture* MsaaTexture() const { return msaaTexture_; }
        /** @brief Returns the shared depth/stencil texture, or null when `DepthFormat::None` was requested. NOXNA. */
        NOXNA [[nodiscard]] SDL_GPUTexture* DepthTexture() const { return depthTexture_; }
        /** @brief Whether the mip chain should be regenerated after this cube's faces render each frame. NOXNA. */
        NOXNA [[nodiscard]] bool WantsMipMap() const { return mipMap_; }

        /** @brief Queues a color-only clear for @p face, consumed on that face's next render pass. NOXNA. */
        NOXNA void QueueClear(int face, SDL_FColor color) { clearColor_[face] = color; clearColorPending_[face] = true; }
        /** @brief Queues a depth clear for @p face, consumed on that face's next render pass. NOXNA. */
        NOXNA void QueueClearDepth(int face, float depth) { clearDepth_[face] = depth; clearDepthPending_[face] = true; }
        /** @brief Queues a stencil clear for @p face, consumed on that face's next render pass. NOXNA. */
        NOXNA void QueueClearStencil(int face, Uint8 stencil) { clearStencil_[face] = stencil; clearStencilPending_[face] = true; }
        NOXNA [[nodiscard]] bool ClearColorPending(int face) const { return clearColorPending_[face]; }
        NOXNA [[nodiscard]] bool ClearDepthPending(int face) const { return clearDepthPending_[face]; }
        NOXNA [[nodiscard]] bool ClearStencilPending(int face) const { return clearStencilPending_[face]; }
        NOXNA [[nodiscard]] SDL_FColor ClearColorValue(int face) const { return clearColor_[face]; }
        NOXNA [[nodiscard]] float ClearDepthValue(int face) const { return clearDepth_[face]; }
        NOXNA [[nodiscard]] Uint8 ClearStencilValue(int face) const { return clearStencil_[face]; }
        /** @brief Resets pending-clear flags for @p face after its pass has rendered this frame. NOXNA. */
        NOXNA void ResetPendingClears(int face)
        {
            clearColorPending_[face] = false;
            clearDepthPending_[face] = false;
            clearStencilPending_[face] = false;
        }

    private:
        SdlGpuGraphicsBackend* owner_ = nullptr;
        int size_ = 0;
        bool mipMap_ = false;
        int multiSampleCount_ = 0;
        SDL_GPUTexture* cubeTexture_ = nullptr;
        SDL_GPUTexture* msaaTexture_ = nullptr;
        SDL_GPUTexture* depthTexture_ = nullptr;
        std::array<bool, 6> clearColorPending_{true, true, true, true, true, true};
        std::array<bool, 6> clearDepthPending_{true, true, true, true, true, true};
        std::array<bool, 6> clearStencilPending_{};
        std::array<SDL_FColor, 6> clearColor_{};
        std::array<float, 6> clearDepth_{1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f};
        std::array<Uint8, 6> clearStencil_{};
    };

    /**
     * @brief `SDL_gpu`-backed plain (non-render-target), uploaded `TextureCube` (Phase `SDLGPU-9`,
     * `SDLGPU-51`).
     *
     * A single `SDL_GPU_TEXTURETYPE_CUBE` texture, `SAMPLER` usage only (never a render target).
     * Each face is one of the cube texture's 6 array layers -- `SetData`/`GetData` both carry the
     * face straight through to `SDL_GPUTextureRegion.layer` (matching
     * `SdlGpuRenderTargetCubeBackend::GetData`'s own convention), and the mip `level` straight
     * through to `mip_level`, same as `SdlGpuTexture3DBackend`. Uploads use `cycle=false` for the
     * same reason `SdlGpuTexture3DBackend::SetData` does: a real cube map is built up via multiple
     * independent per-face (and potentially per-level) `SetData` calls that must all land on the
     * SAME underlying resource -- `cycle=true` would silently orphan earlier faces' writes (the
     * bug `SDLGPU-40` found and fixed for `Texture3D`).
     */
    class SdlGpuTextureCubeBackend final : public ITextureCubeBackend
    {
    public:
        SdlGpuTextureCubeBackend(SdlGpuGraphicsBackend& owner, int size, bool mipMap);
        ~SdlGpuTextureCubeBackend() override;

        SdlGpuTextureCubeBackend(const SdlGpuTextureCubeBackend&) = delete;
        SdlGpuTextureCubeBackend& operator=(const SdlGpuTextureCubeBackend&) = delete;

        void SetData(int face, int level, int x, int y, int w, int h,
                    const void* data, int dataLength) override;
        void GetData(int face, int level, int x, int y, int w, int h,
                    void* data, int dataLength) const override;

        /** @brief Returns the underlying `SDL_GPUTexture`. NOXNA — internal use only. */
        NOXNA [[nodiscard]] SDL_GPUTexture* Texture() const { return texture_; }

    private:
        SdlGpuGraphicsBackend* owner_ = nullptr;
        SDL_GPUTexture* texture_ = nullptr;
        int size_ = 0;
        bool mipMap_ = false;
    };

    /** @brief `SDL_gpu`-backed vertex buffer. */
    class SdlGpuVertexBufferBackend final : public IVertexBufferBackend
    {
    public:
        SdlGpuVertexBufferBackend(SdlGpuGraphicsBackend& owner, int vertexCapacity);
        ~SdlGpuVertexBufferBackend() override;

        void SetData(const void* data, int vertexCount, std::size_t strideInBytes) override;
        [[nodiscard]] int GetVertexCount() const override { return vertexCount_; }

        /** @brief Returns the underlying `SDL_GPUBuffer`. NOXNA — internal use only. */
        NOXNA [[nodiscard]] SDL_GPUBuffer* Buffer() const { return buffer_; }
        /** @brief Returns the vertex stride in bytes from the most recent `SetData()`. NOXNA. */
        NOXNA [[nodiscard]] std::size_t Stride() const { return stride_; }
        // CPU-side copy of the most recent SetData() upload -- needed because DrawColoredPrimitives()/
        // DrawPrimitivesEx() render lazily at Present() time, but a caller like
        // GraphicsDevice::DrawUserPrimitives() typically uses a function-local temporary
        // IVertexBufferBackend already destroyed by then (matches WebGPUVertexBufferBackend's own
        // ShadowData() rationale).
        NOXNA [[nodiscard]] const std::vector<std::uint8_t>& ShadowData() const { return shadowData_; }

    private:
        SdlGpuGraphicsBackend* owner_ = nullptr;
        SDL_GPUBuffer* buffer_ = nullptr;
        Uint32 capacityBytes_ = 0;
        int vertexCapacity_ = 0;
        int vertexCount_ = 0;
        std::size_t stride_ = 0;
        std::vector<std::uint8_t> shadowData_;
    };

    /** @brief `SDL_gpu`-backed 16- or 32-bit index buffer. */
    class SdlGpuIndexBufferBackend final : public IIndexBufferBackend
    {
    public:
        SdlGpuIndexBufferBackend(SdlGpuGraphicsBackend& owner, int indexCapacity, bool thirtyTwoBit);
        ~SdlGpuIndexBufferBackend() override;

        void SetData16(const void* data, int indexCount) override;
        void SetData32(const void* data, int indexCount) override;
        [[nodiscard]] int GetIndexCount() const override { return indexCount_; }
        [[nodiscard]] bool IsThirtyTwoBit() const override { return thirtyTwoBit_; }

        /** @brief Returns the underlying `SDL_GPUBuffer`. NOXNA — internal use only. */
        NOXNA [[nodiscard]] SDL_GPUBuffer* Buffer() const { return buffer_; }
        /** @brief CPU-side copy of the most recent upload. NOXNA — see SdlGpuVertexBufferBackend::ShadowData(). */
        NOXNA [[nodiscard]] const std::vector<std::uint8_t>& ShadowData() const { return shadowData_; }

    private:
        void Upload(const void* data, int indexCount, bool dataIsThirtyTwoBit);

        SdlGpuGraphicsBackend* owner_ = nullptr;
        SDL_GPUBuffer* buffer_ = nullptr;
        Uint32 capacityBytes_ = 0;
        int indexCapacity_ = 0;
        int indexCount_ = 0;
        bool thirtyTwoBit_ = false;
        std::vector<std::uint8_t> shadowData_;
    };

    /**
     * @brief `SDL_gpu`-backed custom `ShaderEffect` (Phase `SDLGPU-10`, `SDLGPU-42`/`SDLGPU-43`).
     *
     * Compiles arbitrary user-authored GLSL vertex+fragment source to SPIR-V at RUNTIME via
     * `libshaderc` (`SDL_gpu` only accepts precompiled bytecode, unlike EasyGL's native GLSL
     * driver compile) -- unlike `SDLGPU-13`'s build-time-only `compile_shaders.py` decision for
     * CNA's own built-in effect shaders, an arbitrary user `ShaderEffect`'s source isn't known
     * until the game runs, so `libshaderc` is linked into the real backend binary here, not just
     * the build-time script.
     *
     * Fixed vertex contract, matching every other backend's own custom-`ShaderEffect` convention
     * (`VulkanEffectBackend`/`D3D11EffectBackend`/`D3D12EffectBackend`): `SpriteVertex`-shaped
     * (pos `vec2` @0, uv `vec2` @8, color `vec4` @16, 32 bytes) -- a `SpriteBatch`-custom-shader
     * facility, not a general arbitrary-vertex-format one (see
     * `ISpriteBatchBackend::SetCustomEffect`'s own doc comment).
     *
     * Fixed 128-byte uniform layout, mirroring `D3D11EffectBackend`'s own byte-for-byte convention
     * (both vertex and fragment stages get their own copy, matching this backend's established
     * "same PC struct in both stages" convention): `[0..15]` = vpSize (`vec4`, xy used, zw pad for
     * std140 alignment; set automatically once per sprite render via `SetViewportSizeEXT` -- the
     * game/effect author never calls it directly), `[16..79]` = `mat4` matrix, `[80..95]` = `vec4`
     * color, `[96..99]` = float/int slot 0 (`name` is deliberately ignored, matching every sibling
     * `EffectBackend`'s own name-discarding convention). Per `SDL_gpu`'s own binding
     * convention (`SDL_CreateGPUShader`'s doc comment), the compiled GLSL must declare its uniform
     * block at vertex `set=1`/`binding=0` and fragment `set=3`/`binding=0`, and its one texture
     * sampler at fragment `set=2`/`binding=0`.
     */
    class SdlGpuEffectBackend final : public IEffectBackend
    {
    public:
        explicit SdlGpuEffectBackend(SdlGpuGraphicsBackend& owner);
        ~SdlGpuEffectBackend() override;

        SdlGpuEffectBackend(const SdlGpuEffectBackend&) = delete;
        SdlGpuEffectBackend& operator=(const SdlGpuEffectBackend&) = delete;

        bool CompileProgram(const std::string& vertSrc, const std::string& fragSrc) override;
        /** @brief No-op -- this backend defers the actual pipeline bind to `RenderSprites()` at
         * `Present()` time, using a per-`SpriteCommand` snapshot of this object's uniform state
         * (see `SdlGpuGraphicsBackend::QueueSprite`'s own custom-effect handling). */
        void Bind() override {}
        /** @brief No-op, same rationale as `Bind()`. */
        void Unbind() override {}
        [[nodiscard]] bool IsValid() const override { return valid_; }
        [[nodiscard]] std::string GetCompileError() const override { return compileError_; }

        void SetUniformMat4(const char* name, const float* matrix) override;
        void SetUniformVec4(const char* name, float x, float y, float z, float w) override;
        void SetUniformVec3(const char* name, float x, float y, float z) override;
        void SetUniformVec2(const char* name, float x, float y) override;
        void SetUniformFloat(const char* name, float value) override;
        void SetUniformInt(const char* name, int value) override;

        /** @brief Writes the `[0..15]`-byte vpSize slot -- called once per sprite render by
         * `SdlGpuGraphicsBackend::QueueSprite`, mirroring every sibling `EffectBackend`'s own
         * "set automatically by the sprite-batch runtime" convention. NOXNA. */
        NOXNA void SetViewportSizeEXT(float width, float height);
        /** @brief Returns the pipeline for @p colorFormat, compiling+caching it on first use. Null
         * if `CompileProgram()` did not succeed. NOXNA — internal use only. */
        NOXNA [[nodiscard]] SDL_GPUGraphicsPipeline* GetOrCreatePipeline(SDL_GPUTextureFormat colorFormat);
        /** @brief Returns a snapshot of the current 128-byte uniform block. NOXNA — internal use
         * only (called once per queued sprite, so later `SetUniform*` calls on the live effect
         * object don't retroactively change an already-queued sprite's rendered result). */
        NOXNA [[nodiscard]] std::array<float, 32> SnapshotUniforms() const { return pushConst_; }

    private:
        SdlGpuGraphicsBackend* owner_ = nullptr;
        SDL_GPUShader* vertexShader_ = nullptr;
        SDL_GPUShader* fragmentShader_ = nullptr;
        std::unordered_map<int, SDL_GPUGraphicsPipeline*> pipelines_;
        std::array<float, 32> pushConst_{};
        std::string compileError_;
        bool valid_ = false;
    };

    /** @brief `SDL_gpu`-backed `SpriteBatch`. Queues quads; actual draws happen at Present() time. */
    class SdlGpuSpriteBatchBackend final : public ISpriteBatchBackend
    {
    public:
        explicit SdlGpuSpriteBatchBackend(SdlGpuGraphicsBackend& owner);
        ~SdlGpuSpriteBatchBackend() override = default;

        void Begin() override;
        void End() override;
        void SetTransformMatrix(const Matrix& matrix) override { transform_ = matrix; }
        void SetCustomEffect(Effect* effect) override;
        void SetSamplerFilter(int textureFilter) override { textureFilter_ = textureFilter; }
        void SetSamplerAddressMode(int addressU, int addressV) override { addressU_ = addressU; addressV_ = addressV; }
        void Draw(const ITextureBackend& texture, float x, float y) override;
        void Draw(const ITextureBackend& texture,
                  const Rectangle& destinationRectangle,
                  const Rectangle& sourceRectangle,
                  const Color& color) override;
        void Draw(const ITextureBackend& texture,
                  const Rectangle& destinationRectangle,
                  const Rectangle& sourceRectangle,
                  const Color& color,
                  float rotation,
                  const Vector2& origin,
                  SpriteEffects effects,
                  float layerDepth) override;

    private:
        SdlGpuGraphicsBackend* owner_ = nullptr;
        bool begun_ = false;
        Matrix transform_ = Matrix::getIdentityProperty();
        int textureFilter_ = 0;
        int addressU_ = 1;
        int addressV_ = 1;
        /// Set at Begin() time (SetCustomEffect), cleared at End() -- SDLGPU-42/43. Resolved to the
        /// concrete SdlGpuEffectBackend* and snapshotted per-sprite at Draw() time (see QueueSprite),
        /// not read again at Present() time, so later SetUniform* calls on the same live effect
        /// object never retroactively change an already-queued sprite's rendered result.
        Effect* customEffect_ = nullptr;
    };

    /**
     * @brief `SDL_gpu`-backed graphics backend (`CNA_GRAPHICS_BACKEND=SDL_GPU`).
     *
     * See `plan_sdlgpu.md` for the phased implementation plan. As of Phase `SDLGPU-6`, device/
     * window/swapchain lifecycle, color+depth+stencil clear/present, `Texture2D`, vertex/index
     * buffers, `SpriteBatch`, and the core 3D vertex formats (`colored3d`/`textured3d`/
     * `colored_textured3d`/`lit_textured3d`, i.e. `BasicEffect`) are real and verified.
     * `AlphaTestEffect`/`DualTextureEffect`/`EnvironmentMapEffect`/`SkinnedEffect`-specific
     * `GpuDrawParams` fields (`alphaTest`, `dualTexture`, `envMapping`, `skinned`) are not yet
     * checked by this backend's `DrawPrimitivesEx`/`DrawIndexedPrimitivesEx` dispatch — such a
     * draw currently renders as a plain `BasicEffect` draw instead (later phases add real
     * per-effect dispatch, matching `WebGPUGraphicsBackend`'s own precedent).
     */
    class SdlGpuGraphicsBackend final : public IGraphicsBackend
    {
        friend class SdlGpuRenderTargetBackend;
        friend class SdlGpuRenderTargetCubeBackend;
        friend class SdlGpuEffectBackend;
    public:
        /** @brief Vertex layout for the `sprite2d` pipeline: position, UV, RGBA color (32 bytes). */
        struct SpriteVertex
        {
            float x, y;
            float u, v;
            float r, g, b, a;
        };

        // Identifies which render pass a queued draw/sprite belongs to: the swapchain (both null,
        // face -1), a RenderTarget2D (rt set), or one face of a RenderTargetCube (cube + face
        // set). At most one of rt/cube is ever set, matching this backend's single-current-target
        // semantics (SetRenderTarget2D/SetRenderTargetCubeFace are mutually exclusive).
        struct DrawTarget
        {
            const SdlGpuRenderTargetBackend* rt = nullptr;
            const SdlGpuRenderTargetCubeBackend* cube = nullptr;
            int face = -1;

            bool operator==(const DrawTarget& other) const
            {
                return rt == other.rt && cube == other.cube && face == other.face;
            }
            bool operator!=(const DrawTarget& other) const { return !(*this == other); }
        };

        // SDLGPU-18: raw XNA Blend/BlendFunction ordinals, captured at Queue*Draw time so the
        // exact requested blend equation (not just enabled/disabled) is baked into the pipeline
        // that command actually renders with. Defaults mirror BlendState.Opaque (One/Zero/Add).
        struct BlendKeyParams
        {
            int colorSrc = 0, colorDst = 1, alphaSrc = 0, alphaDst = 1, colorFunc = 0, alphaFunc = 0;
        };

        // SDLGPU-19: raw XNA StencilOperation/CompareFunction ordinals for real front/back
        // stencil-op pipeline baking. Defaults mirror DepthStencilState.Default (stencil disabled).
        struct StencilKeyParams
        {
            bool enable = false;
            int func = 0, fail = 0, depthFail = 0, pass = 0;
            bool twoSided = false;
            int ccwFunc = 0, ccwFail = 0, ccwDepthFail = 0, ccwPass = 0;
        };

        // SDLGPU-18/19/20: snapshot of BlendState/the stencil half of DepthStencilState/
        // RasterizerState's cull+fill fields, captured at Queue*Draw time -- mirrors DrawTarget's
        // own "one shared struct field per DrawCommand" precedent. depthTest/depthWrite/depthFunc
        // stay as each DrawCommand's own pre-existing, separate fields (unchanged) since those
        // predate this and are already correctly threaded through; this struct only carries the
        // NEW dimensions this task adds. ScissorTestEnable/DepthBias/SlopeScaleDepthBias are
        // deliberately NOT here -- scissor is real render-pass-time state (see SetScissorRect),
        // and SDL_gpu has no per-draw-dynamic depth-bias equivalent to Vulkan's vkCmdSetDepthBias
        // (only a pipeline-baked one), so depth bias is a documented, deliberate deferral (see
        // plan_sdlgpu.md's SDLGPU-20 row).
        struct RenderStateSnapshot
        {
            bool blendEnabled = false;
            BlendKeyParams blend;
            int cullMode = 0;   // XNA CullMode ordinal; 0 = None
            bool wireframe = false;
            StencilKeyParams stencil;
            // SDL_gpu exposes this as a genuine per-draw dynamic value (SDL_SetGPUStencilReference),
            // not a pipeline-baked one -- captured per-command like the rest of this snapshot rather
            // than applied once per pass (unlike scissor, which has no per-draw SDL_gpu equivalent).
            int stencilReference = 0;
        };

        // SDLGPU-21: one XNA GraphicsDevice.SamplerStates[slot] snapshot, stored per-slot by
        // ApplySamplerState() and read directly (not via RenderStateSnapshot) into each
        // DrawCommand's own pre-existing textureFilter/addressU/addressV fields at Queue*Draw
        // time. Defaults mirror SamplerState.LinearWrap, the real XNA GraphicsDevice default
        // (SpriteCommand's own defaults deliberately differ -- SpriteBatch's real XNA default is
        // SamplerState.LinearClamp, a distinct, correct behavioral difference, not an inconsistency).
        struct SamplerSlotState
        {
            int filter = 0;
            int addressU = 0;
            int addressV = 0;
            int maxAnisotropy = 4;
        };

        struct SpriteCommand
        {
            // Raw native handle rather than a concrete backend pointer -- SpriteBatch can draw
            // either a plain Texture2D (SdlGpuTextureBackend) or a RenderTarget2D/RenderTargetCube
            // (SdlGpuRenderTargetBackend/SdlGpuRenderTargetCubeBackend); all expose an
            // SDL_GPUTexture* but are unrelated classes (see QueueSprite/SdlGpuSpriteBatchBackend::Draw).
            SDL_GPUTexture* texture = nullptr;
            std::array<SpriteVertex, 6> vertices{};
            int textureFilter = 0;
            int addressU = 1;
            int addressV = 1;
            DrawTarget target;  ///< default = swapchain
            // SDLGPU-42/43: non-null if this sprite was queued during a SetCustomEffect(effect)
            // Begin/End cycle with a validly-compiled custom shader -- customUniforms is a snapshot
            // of the effect's uniform state AT QUEUE TIME (see QueueSprite), not read again at
            // Present() time, so later SetUniform* calls on the same live effect object never
            // retroactively change this already-queued sprite's rendered result.
            SdlGpuEffectBackend* customEffect = nullptr;
            std::array<float, 32> customUniforms{};
            // SDLGPU-18/19/20: SpriteBatch.Begin() sets GraphicsDevice.BlendState/DepthStencilState/
            // RasterizerState the same way any other draw does (defaulting to
            // BlendState.AlphaBlend/DepthStencilState.None/RasterizerState.CullCounterClockwise
            // per SpriteBatch.Begin()'s own real XNA defaults), so sprites bake the same
            // dynamically-captured state as every 3D shader family, not a hardcoded blend/no-depth.
            bool depthTest = false;
            bool depthWrite = false;
            int depthFunc = 3;
            RenderStateSnapshot renderState;
        };

        // Phase SDLGPU-6: colored3d/textured3d/colored_textured3d/lit_textured3d draw commands.
        // Vertex/index data is shadow-copied at Draw-call time (see
        // SdlGpuVertexBufferBackend::ShadowData()'s own rationale) and re-uploaded into a
        // transient SDL_GPUBuffer during the copy pass that precedes each frame's render pass
        // (see UploadSceneDrawData/ReleaseSceneDrawBuffers). No fog (deliberately deferred, same
        // as this codebase's WebGPU backend's own initial 3D vertical slice).
        struct ColoredDrawCommand
        {
            std::vector<std::uint8_t> vertexData;
            std::vector<std::uint8_t> indexData;  ///< empty for a non-indexed draw
            bool indexed = false;
            bool index32 = false;
            Uint32 vertexCount = 0;
            Uint32 indexCount = 0;
            SDL_GPUPrimitiveType topology = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
            std::array<float, 32> uniforms{};  ///< mirrors VulkanGraphicsBackend::FillExtPushConst()'s 128-byte layout
            bool depthTest = false;
            bool depthWrite = false;
            int depthFunc = 3;  ///< XNA CompareFunction ordinal; 3 = LessEqual
            RenderStateSnapshot renderState;  ///< SDLGPU-18/19/20
            DrawTarget target;  ///< default = swapchain
            SDL_GPUBuffer* uploadedVertexBuffer = nullptr;  ///< transient, set by UploadSceneDrawData
            SDL_GPUBuffer* uploadedIndexBuffer = nullptr;   ///< transient, set by UploadSceneDrawData
        };

        // textured3d (stride 20, VertexPositionTexture) and colored_textured3d (stride 24,
        // VertexPositionColorTexture) share this command shape and the same fragment shader --
        // `hasVertexColor` selects the stride-24 vertex-input-state/pipeline vs stride-20's.
        struct TexturedDrawCommand
        {
            std::vector<std::uint8_t> vertexData;
            std::vector<std::uint8_t> indexData;
            bool indexed = false;
            bool index32 = false;
            Uint32 vertexCount = 0;
            Uint32 indexCount = 0;
            SDL_GPUPrimitiveType topology = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
            std::array<float, 32> uniforms{};
            bool depthTest = false;
            bool depthWrite = false;
            int depthFunc = 3;
            RenderStateSnapshot renderState;  ///< SDLGPU-18/19/20
            const SdlGpuTextureBackend* texture = nullptr;
            int textureFilter = 0;
            int addressU = 0;
            int addressV = 0;
            bool hasVertexColor = false;  ///< stride 24 vs stride 20
            DrawTarget target;  ///< default = swapchain
            SDL_GPUBuffer* uploadedVertexBuffer = nullptr;
            SDL_GPUBuffer* uploadedIndexBuffer = nullptr;
        };

        // lit_textured3d (stride 32, VertexPositionNormalTexture) -- real Blinn-Phong lighting.
        struct LitTexturedDrawCommand
        {
            std::vector<std::uint8_t> vertexData;
            std::vector<std::uint8_t> indexData;
            bool indexed = false;
            bool index32 = false;
            Uint32 vertexCount = 0;
            Uint32 indexCount = 0;
            SDL_GPUPrimitiveType topology = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
            std::array<float, 32> uniforms{};
            std::array<float, 56> lightUniforms{};  ///< LitLightParams: 10 vec4 + 1 mat4 = 224 bytes
            bool depthTest = false;
            bool depthWrite = false;
            int depthFunc = 3;
            RenderStateSnapshot renderState;  ///< SDLGPU-18/19/20
            const SdlGpuTextureBackend* texture = nullptr;
            int textureFilter = 0;
            int addressU = 0;
            int addressV = 0;
            DrawTarget target;  ///< default = swapchain
            SDL_GPUBuffer* uploadedVertexBuffer = nullptr;
            SDL_GPUBuffer* uploadedIndexBuffer = nullptr;
        };

        // AlphaTestEffect (Phase SDLGPU-7) -- strides 20 (VertexPositionTexture)/32
        // (VertexPositionNormalTexture, normal unread) share one shader (alphaTestVertexShader_);
        // stride 24 (VertexPositionColorTexture, vertex-color tint) uses
        // alphaTestColoredVertexShader_. `stride` selects which at render time.
        struct AlphaTestDrawCommand
        {
            std::vector<std::uint8_t> vertexData;
            std::vector<std::uint8_t> indexData;
            bool indexed = false;
            bool index32 = false;
            Uint32 vertexCount = 0;
            Uint32 indexCount = 0;
            SDL_GPUPrimitiveType topology = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
            std::array<float, 32> uniforms{};  ///< [20..23]=alphaTest params, [24]=vertexColorEnabled (no lighting/ambient slots needed)
            bool depthTest = false;
            bool depthWrite = false;
            int depthFunc = 3;
            RenderStateSnapshot renderState;  ///< SDLGPU-18/19/20
            const SdlGpuTextureBackend* texture = nullptr;
            int textureFilter = 0;
            int addressU = 0;
            int addressV = 0;
            std::size_t stride = 20;  ///< 20, 24, or 32 -- selects vertex layout + shader
            DrawTarget target;  ///< default = swapchain
            SDL_GPUBuffer* uploadedVertexBuffer = nullptr;
            SDL_GPUBuffer* uploadedIndexBuffer = nullptr;
        };

        // DualTextureEffect (Phase SDLGPU-7) -- two texture units sampled at the same UV
        // (`tex1.rgb*=2; result=tex1*tex2*tint`). Strides 20/24 use dedicated vertex shaders
        // (dualTextureVertexShader_/dualTextureColoredVertexShader_); the fragment shader is
        // shared and does not need the primary PC block at all (fragTint is already resolved by
        // the vertex stage).
        struct DualTextureDrawCommand
        {
            std::vector<std::uint8_t> vertexData;
            std::vector<std::uint8_t> indexData;
            bool indexed = false;
            bool index32 = false;
            Uint32 vertexCount = 0;
            Uint32 indexCount = 0;
            SDL_GPUPrimitiveType topology = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
            std::array<float, 32> uniforms{};
            bool depthTest = false;
            bool depthWrite = false;
            int depthFunc = 3;
            RenderStateSnapshot renderState;  ///< SDLGPU-18/19/20
            const SdlGpuTextureBackend* texture0 = nullptr;
            const SdlGpuTextureBackend* texture1 = nullptr;
            ///@{ SDLGPU-21: independent per-slot sampler state (GraphicsDevice.SamplerStates[0]/[1]).
            int texture0Filter = 0;
            int texture0AddressU = 0;
            int texture0AddressV = 0;
            int texture1Filter = 0;
            int texture1AddressU = 0;
            int texture1AddressV = 0;
            ///@}
            bool hasVertexColor = false;  ///< stride 24 vs stride 20
            DrawTarget target;  ///< default = swapchain
            SDL_GPUBuffer* uploadedVertexBuffer = nullptr;
            SDL_GPUBuffer* uploadedIndexBuffer = nullptr;
        };

        // EnvironmentMapEffect (Phase SDLGPU-9, SDLGPU-33) -- stride 32 (VertexPositionNormalTexture,
        // same layout lit_textured3d uses). `envMapTexture` is resolved at Queue-time to the raw
        // SDL_GPUTexture* since GpuDrawParams::envMap (an ITextureCubeBackend*) may be either a
        // plain SdlGpuTextureCubeBackend or a SdlGpuRenderTargetCubeBackend -- mirrors
        // SpriteBatch::Draw's own dual-backend resolve for ITextureBackend.
        struct EnvMapDrawCommand
        {
            std::vector<std::uint8_t> vertexData;
            std::vector<std::uint8_t> indexData;
            bool indexed = false;
            bool index32 = false;
            Uint32 vertexCount = 0;
            Uint32 indexCount = 0;
            SDL_GPUPrimitiveType topology = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
            std::array<float, 24> uniforms{};       ///< PC: mvp(16) + diffuseColor(4) + emissiveAmount(4)
            std::array<float, 48> envMapUniforms{}; ///< EnvMapParams: world(16) + 8 vec4 (32) = 48 floats
            bool depthTest = false;
            bool depthWrite = false;
            int depthFunc = 3;
            RenderStateSnapshot renderState;  ///< SDLGPU-18/19/20
            const SdlGpuTextureBackend* texture = nullptr;
            SDL_GPUTexture* envMapTexture = nullptr;
            int textureFilter = 0;
            int addressU = 0;
            int addressV = 0;
            DrawTarget target;  ///< default = swapchain
            SDL_GPUBuffer* uploadedVertexBuffer = nullptr;
            SDL_GPUBuffer* uploadedIndexBuffer = nullptr;
        };

        // SkinnedEffect (Phase SDLGPU-7, SDLGPU-34) -- stride 52 (VertexPositionNormalTextureSkinned).
        // The fragment stage reuses litTexturedFragmentShader_ unchanged (byte-identical varying
        // interface and UBO layout to lit_textured3d's own fragment shader) -- no separate
        // skinned fragment shader exists. The 72-bone palette (4608 bytes) is uploaded as a real
        // SDL_GPUBuffer (GRAPHICS_STORAGE_READ) and bound via SDL_BindGPUVertexStorageBuffers,
        // NOT pushed via SDL_PushGPUVertexUniformData -- empirically found (SdlGpu_Skinned) that
        // this backend's push-uniform-data mechanism has a real ~4096-byte cap per slot on this
        // Vulkan-backed environment, well under the full 4608-byte palette.
        struct SkinnedDrawCommand
        {
            std::vector<std::uint8_t> vertexData;
            std::vector<std::uint8_t> indexData;
            bool indexed = false;
            bool index32 = false;
            Uint32 vertexCount = 0;
            Uint32 indexCount = 0;
            SDL_GPUPrimitiveType topology = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
            std::array<float, 32> uniforms{};        ///< PC: same 32-float layout FillExtUniforms already fills
            std::array<float, 72 * 16> boneUniforms{}; ///< BoneBlock: 72 mat4 = 1152 floats (4608 bytes), uploaded as a storage buffer
            std::array<float, 56> lightUniforms{};   ///< SkinnedLightParams: byte-identical to LitLightParams
            bool depthTest = false;
            bool depthWrite = false;
            int depthFunc = 3;
            RenderStateSnapshot renderState;  ///< SDLGPU-18/19/20
            const SdlGpuTextureBackend* texture = nullptr;
            int textureFilter = 0;
            int addressU = 0;
            int addressV = 0;
            DrawTarget target;  ///< default = swapchain
            SDL_GPUBuffer* uploadedVertexBuffer = nullptr;
            SDL_GPUBuffer* uploadedIndexBuffer = nullptr;
            SDL_GPUBuffer* uploadedBoneBuffer = nullptr;
        };

        /**
         * @brief Constructs the backend against an already-created SDL window.
         *
         * @param window SDL window to claim for `SDL_gpu` rendering. Must not be null.
         * @param virtualWidth Initial virtual (game-logic) resolution width.
         * @param virtualHeight Initial virtual (game-logic) resolution height.
         * @param presentationMode Initial presentation/scaling policy.
         * @param swapInterval Initial swap interval (0=immediate, 1=VSync, 2=half-rate).
         */
        SdlGpuGraphicsBackend(SDL_Window* window, int virtualWidth, int virtualHeight,
                              CnaPresentationMode presentationMode, int swapInterval);
        /** @brief Releases the window from the `SDL_GPUDevice` and destroys the device. */
        ~SdlGpuGraphicsBackend() override;

        SdlGpuGraphicsBackend(const SdlGpuGraphicsBackend&) = delete;
        SdlGpuGraphicsBackend& operator=(const SdlGpuGraphicsBackend&) = delete;

        /** @brief Queues a color-only clear, consumed on the next render pass. */
        void Clear(float r, float g, float b, float a) override;
        /** @brief Renders any pending clear and presents the swapchain texture. */
        void Present() override;
        /** @brief Returns the current logical (virtual) viewport size. */
        void GetViewportSize(int& width, int& height) override;
        /** @brief Updates the virtual (game-logic) resolution used for presentation scaling. */
        void SetVirtualResolution(int width, int height) override;
        /** @brief Updates the presentation/scaling policy. */
        void SetPresentationMode(int mode) override;
        /** @brief Updates the swap interval, reconfiguring the swapchain present mode. */
        void SetSwapInterval(int interval) override;
        /** @brief Converts a physical window point to logical (virtual) game coordinates. */
        bool TransformWindowToLogical(float windowX, float windowY, float& logicalX, float& logicalY) const override;
        /** @brief Converts a logical (virtual) game point to physical window coordinates. */
        bool TransformLogicalToWindow(float logicalX, float logicalY, float& windowX, float& windowY) const override;

        /** @brief Returns the SDL window this backend renders into. */
        [[nodiscard]] SDL_Window* GetWindowInternal() const override { return window_; }
        /** @brief Always null — this backend does not use `SDL_Renderer`. */
        [[nodiscard]] SDL_Renderer* GetRendererInternal() const override { return nullptr; }

        std::unique_ptr<ITextureBackend> CreateTexture(const ImageData& data) override;
        std::unique_ptr<ISpriteBatchBackend> CreateSpriteBatch() override;

        /** @brief Queues a combined color+depth clear, consumed on the next render pass. */
        void ClearColorAndDepth(float r, float g, float b, float a, float depth) override;
        /** @brief Queues a depth-only clear, consumed on the next render pass. */
        void ClearDepth(float depth) override;
        /** @brief Queues a stencil-only clear, consumed on the next render pass. */
        void ClearStencil(int stencil) override;
        /** @brief Queues a combined depth+stencil clear, consumed on the next render pass. */
        void ClearDepthAndStencil(float depth, int stencil) override;
        /** @brief Queues a combined color+stencil clear, consumed on the next render pass. */
        void ClearColorAndStencil(float r, float g, float b, float a, int stencil) override;
        /** @brief Queues a combined color+depth+stencil clear, consumed on the next render pass. */
        void ClearColorDepthAndStencil(float r, float g, float b, float a, float depth, int stencil) override;
        /** @brief Stores the depth-test enabled flag, read by the 3D draw path's pipeline cache key. */
        void SetDepthTestEnabled(bool enabled) override { depthTestEnabled_ = enabled; }
        /** @brief Stores the blend-enabled flag directly -- a test-only shortcut separate from the
         * real `BlendState` object; `ApplyBlendState()` below is the real production path and may
         * overwrite this same field the next time a `BlendState` is assigned. */
        void SetBlendEnabled(bool enabled) override { blendEnabled_ = enabled; }
        /** @brief Stores the depth-write enabled flag, read by the 3D draw path's pipeline cache key. */
        void SetDepthWriteEnabled(bool enabled) override { depthWriteEnabled_ = enabled; }

        /**
         * @brief Real `BlendState` mapping (`SDLGPU-18`) -- stores the full requested blend
         * equation (not just enabled/disabled), baked into every 3D/sprite pipeline's
         * `SDL_GPUColorTargetBlendState` at `Queue*Draw()` time. Blend is considered "enabled"
         * unless the request is exactly `BlendState.Opaque`'s own values (`One`/`Zero`/`Add`),
         * mirroring `VulkanGraphicsBackend::ApplyBlendState`'s own convention.
         */
        void ApplyBlendState(int colorSrcBlend, int alphaSrcBlend,
                             int colorDstBlend, int alphaDstBlend,
                             int colorBlendFunc, int alphaBlendFunc) override;
        /**
         * @brief Real `DepthStencilState` mapping (`SDLGPU-19`) -- `depthEnable`/`depthWriteEnable`/
         * `depthFunc` update the same fields `SetDepthTestEnabled`/`SetDepthWriteEnabled` already
         * drive (whichever was set most recently wins, matching two setters sharing one field);
         * the stencil fields are new, baked into every 3D/sprite pipeline's front/back
         * `SDL_GPUStencilOpState`. `referenceStencil` is applied immediately and separately (real
         * render-pass-time state, not baked into the pipeline) via `SetReferenceStencil()`'s own
         * mechanism, matching `GraphicsDevice.ReferenceStencil`'s real standalone-property
         * semantics.
         */
        void ApplyDepthStencilState(bool depthEnable, bool depthWriteEnable, int depthFunc,
                                    bool stencilEnable, int stencilFunc,
                                    int stencilPass, int stencilFail, int stencilDepthFail,
                                    int stencilMask, int stencilWriteMask, int referenceStencil,
                                    bool twoSidedStencilMode,
                                    int ccwStencilFunc, int ccwStencilPass,
                                    int ccwStencilFail, int ccwStencilDepthFail) override;
        /**
         * @brief Real `RasterizerState` mapping (`SDLGPU-20`) -- `cullMode`/`fillMode` are baked
         * into every 3D/sprite pipeline's `SDL_GPURasterizerState`; `scissorTestEnable` is applied
         * per render pass via `SDL_SetGPUScissor` (see `SetScissorRect()`). `depthBias`/
         * `slopeScaleDepthBias` are stored but deliberately NOT yet applied: unlike Vulkan's
         * `vkCmdSetDepthBias` (a true per-draw dynamic state), `SDL_gpu` only exposes depth bias as
         * pipeline-baked `SDL_GPURasterizerState` fields, which would require folding two floats
         * into the pipeline cache key -- a real, documented deferral (see `plan_sdlgpu.md`'s
         * `SDLGPU-20` row), not a silent drop.
         */
        void ApplyRasterizerState(int cullMode, int fillMode, bool scissorTestEnable,
                                  float depthBias = 0.0f, float slopeScaleDepthBias = 0.0f) override;
        /** @brief Stores `GraphicsDevice.ReferenceStencil`, independent of a full `DepthStencilState`
         * re-application -- matches `IGraphicsBackend::SetReferenceStencil`'s own documented
         * standalone-property contract. Captured into `RenderStateSnapshot::stencilReference` at
         * the next `Queue*Draw()`/`QueueSprite()` call, then applied per draw call via a real
         * `SDL_SetGPUStencilReference` (a genuine SDL_gpu per-draw dynamic value, not baked into
         * the pipeline) in each `Render*Draws`/`RenderSprites` function. */
        void SetReferenceStencil(int value) override;
        /** @brief Real scissor-rect mapping (`SDLGPU-20`) -- stores the rect; actually applied via
         * `SDL_SetGPUScissor` once per render pass in `RenderSprites`/`Render*Draws`, gated on
         * `ApplyRasterizerState`'s own `scissorTestEnable` flag (a disabled scissor test uses the
         * full render-target extents instead of this rect, matching "no clipping"). */
        void SetScissorRect(int x, int y, int w, int h) override;
        /**
         * @brief Real per-slot `SamplerState` mapping (`SDLGPU-21`) for direct 3D draws --
         * `SpriteBatch`'s own per-draw sampler selection (`SetSamplerFilter`/`SetSamplerAddressMode`)
         * already had its own separate path before this. Stores into `samplerSlots_[slot]`, read
         * directly into each `DrawCommand`'s own `textureFilter`/`addressU`/`addressV` fields at
         * the next `Queue*Draw()` call (slot 0 for every single-texture family; slots 0 and 1
         * independently for `DualTextureEffect`'s two texture units). `maxAnisotropy` is stored but
         * not applied -- `GetOrCreateSampler()`'s own cache has no anisotropic-filtering dimension
         * yet, a pre-existing limitation shared with `SpriteBatch`'s own sampler path, not
         * introduced here.
         */
        void ApplySamplerState(int slot, int filter, int addressU, int addressV, int maxAnisotropy) override;

        std::unique_ptr<IVertexBufferBackend> CreateVertexBuffer(int vertex_capacity) override;
        std::unique_ptr<IIndexBufferBackend> CreateIndexBuffer16(int index_capacity) override;

        /**
         * @brief Creates an off-screen `RenderTarget2D` (Phase `SDLGPU-8`, `SDLGPU-35`/`SDLGPU-38`),
         * including real MSAA.
         */
        std::unique_ptr<IRenderTargetBackend> CreateRenderTarget2D(int w, int h, int depthFormat,
                                                                    bool preserveContents = false,
                                                                    bool mipMap = false,
                                                                    int multiSampleCount = 0) override;
        /** @brief Activates the given render target (pass nullptr to restore the swapchain). */
        void SetRenderTarget2D(IRenderTargetBackend* rt) override;

        /**
         * @brief Creates a `RenderTargetCube` (Phase `SDLGPU-8`, `SDLGPU-36`), including real MSAA.
         */
        std::unique_ptr<IRenderTargetCubeBackend> CreateRenderTargetCube(int size, int depthFormat,
                                                                          bool mipMap = false,
                                                                          int multiSampleCount = 0) override;

        /** @brief Creates a `Texture3D` (Phase `SDLGPU-9`, `SDLGPU-40`/`SDLGPU-41`). */
        std::unique_ptr<ITexture3DBackend> CreateTexture3D(int w, int h, int depth, bool mipMap,
                                                            int surfaceFormat) override;

        /** @brief Creates a plain, uploaded `TextureCube` (Phase `SDLGPU-9`, `SDLGPU-51`). */
        std::unique_ptr<ITextureCubeBackend> CreateTextureCube(int size, bool mipMap,
                                                                int surfaceFormat) override;

        /**
         * @brief Creates a custom-`ShaderEffect` backend (Phase `SDLGPU-10`, `SDLGPU-42`/`SDLGPU-43`),
         * compiling @p vertSrc/@p fragSrc (GLSL source) to SPIR-V at runtime via `libshaderc`.
         * Compilation failure is reported via the returned backend's `IsValid()`/`GetCompileError()`
         * (matches `VulkanGraphicsBackend`/`D3D11GraphicsBackend`'s own convention), not an
         * exception.
         */
        std::unique_ptr<IEffectBackend> CreateEffectBackend(const std::string& vertSrc,
                                                            const std::string& fragSrc) override;

        /**
         * @brief Activates multiple render targets for MRT (Phase `SDLGPU-8`, `SDLGPU-37`).
         *
         * `rts[0]` becomes the real draw target (identical to `SetRenderTarget2D(rts[0])`);
         * `rts[1..count-1]` are bound and independently cleared but do not receive draws -- no
         * shader in this codebase declares more than one fragment output, the same honest scope
         * boundary this project's D3D11/D3D12 MRT support already established.
         */
        void SetRenderTargets(IRenderTargetBackend* const* rts, int count) override;

        /** @brief Draws stride-16 (VertexPositionColor) primitives with a hardcoded white/vertex-color-enabled BasicEffect. */
        void DrawColoredPrimitives(const IVertexBufferBackend& vb,
                                   const Matrix& world, const Matrix& view, const Matrix& projection,
                                   PrimitiveType primitive, int primitiveCount) override;
        /** @brief Indexed counterpart of DrawColoredPrimitives(). */
        void DrawIndexedColoredPrimitives(const IVertexBufferBackend& vb,
                                          const IIndexBufferBackend& ib,
                                          const Matrix& world, const Matrix& view, const Matrix& projection,
                                          PrimitiveType primitive, int primitiveCount) override;
        /** @brief Effect-aware draw — dispatches to colored3d/textured3d/colored_textured3d/lit_textured3d by vertex stride. */
        void DrawPrimitivesEx(const IVertexBufferBackend& vb,
                              const Matrix& world, const Matrix& view, const Matrix& projection,
                              PrimitiveType primitive, int primitiveCount,
                              const GpuDrawParams& params) override;
        /** @brief Indexed counterpart of DrawPrimitivesEx(). */
        void DrawIndexedPrimitivesEx(const IVertexBufferBackend& vb, const IIndexBufferBackend& ib,
                                     const Matrix& world, const Matrix& view, const Matrix& projection,
                                     PrimitiveType primitive, int primitiveCount,
                                     const GpuDrawParams& params) override;

        /**
         * @brief Queues a sprite quad for drawing on the next render pass. NOXNA — internal use
         * only. @p texture supplies GetWidth()/GetHeight() for UV math (works for any
         * ITextureBackend); @p nativeTexture is the raw SDL_GPUTexture* actually bound at render
         * time (Texture2D and RenderTarget2D are unrelated concrete backend classes, so
         * SdlGpuSpriteBatchBackend::Draw resolves this once at call time).
         */
        NOXNA void QueueSprite(const ITextureBackend& texture, SDL_GPUTexture* nativeTexture,
                                const Rectangle& destinationRectangle,
                                const Rectangle& sourceRectangle,
                                const Color& color,
                                float rotation,
                                const Vector2& origin,
                                SpriteEffects effects,
                                float layerDepth,
                                const Matrix& transform,
                                int textureFilter,
                                int addressU,
                                int addressV,
                                SdlGpuEffectBackend* customEffect = nullptr);

        /** @brief Returns the underlying `SDL_GPUDevice`. NOXNA — internal use only. */
        NOXNA [[nodiscard]] SDL_GPUDevice* Device() const { return device_; }

    private:
        struct LogicalViewport
        {
            float x = 0.0f;
            float y = 0.0f;
            float width = 0.0f;
            float height = 0.0f;
            float logicalWidth = 0.0f;
            float logicalHeight = 0.0f;
        };

        [[nodiscard]] LogicalViewport ComputeLogicalViewport() const;
        // Renders any pending clear into the acquired swapchain texture and submits the command
        // buffer. Returns false if no swapchain texture could be acquired this frame (minimized
        // window, lost surface, etc) -- mirrors WebGPUGraphicsBackend::EnsureFrameRendered's
        // on-demand-submit semantics.
        bool EnsureFrameRendered();
        // (Re)creates depthStencilTexture_ if it does not already match the requested size.
        // depthStencilFormat_ itself is queried once in the constructor (QueryDepthStencilFormat),
        // not here, since pipeline creation needs a stable answer before any frame has rendered.
        void EnsureDepthStencilTexture(Uint32 width, Uint32 height);
        // Queries the best available combined depth+stencil format once, at construction time.
        void QueryDepthStencilFormat();

        // sprite2d pipeline: shader modules, one pipeline (alpha-blended, no depth test/write --
        // compatible with a render pass that has a depth attachment, just doesn't use it), and a
        // sampler cache keyed by (filter, addressU, addressV), mirroring
        // WebGPUGraphicsBackend::SamplerCacheIndex's exact indexing scheme (filterIndex*9+u*3+v).
        void CreateSpriteResources();
        void DestroySpriteResources();
        [[nodiscard]] SDL_GPUGraphicsPipeline* GetOrCreateSpritePipeline(SDL_GPUTextureFormat colorFormat,
                                                                          bool depthTest, bool depthWrite, int depthFunc,
                                                                          const RenderStateSnapshot& renderState);
        [[nodiscard]] SDL_GPUSampler* GetOrCreateSampler(int textureFilter, int addressU, int addressV);
        // Uploads all queued sprite vertex data (copy pass) -- must run BEFORE
        // BeginGPURenderPass; SDL_gpu forbids a copy pass nested inside a render pass.
        void UploadSpriteVertexData(SDL_GPUCommandBuffer* cmd);
        // Issues the actual bind+draw calls for each queued sprite targeting @p target (nullptr =
        // swapchain) -- must run INSIDE the render pass, after UploadSpriteVertexData's copy pass
        // has already been submitted-queued on the same command buffer.
        void RenderSprites(SDL_GPURenderPass* pass, SDL_GPUCommandBuffer* cmd,
                           const DrawTarget& target, SDL_GPUTextureFormat colorFormat);

        // Phase SDLGPU-6: colored3d/textured3d/colored_textured3d/lit_textured3d.
        void CreateColoredResources();
        void DestroyColoredResources();
        void CreateTexturedResources();   ///< also creates colored_textured3d's vertex shader (shares textured3d's fragment shader)
        void DestroyTexturedResources();
        void CreateLitTexturedResources();
        void DestroyLitTexturedResources();
        [[nodiscard]] SDL_GPUGraphicsPipeline* GetOrCreatePipelineColored3D(
            SDL_GPUPrimitiveType topology, bool depthTest, bool depthWrite, int depthFunc,
            SDL_GPUTextureFormat colorFormat, const RenderStateSnapshot& renderState);
        [[nodiscard]] SDL_GPUGraphicsPipeline* GetOrCreatePipelineTextured3D(
            SDL_GPUPrimitiveType topology, bool depthTest, bool depthWrite, int depthFunc,
            SDL_GPUTextureFormat colorFormat, const RenderStateSnapshot& renderState);
        [[nodiscard]] SDL_GPUGraphicsPipeline* GetOrCreatePipelineColoredTextured3D(
            SDL_GPUPrimitiveType topology, bool depthTest, bool depthWrite, int depthFunc,
            SDL_GPUTextureFormat colorFormat, const RenderStateSnapshot& renderState);
        [[nodiscard]] SDL_GPUGraphicsPipeline* GetOrCreatePipelineLitTextured3D(
            SDL_GPUPrimitiveType topology, bool depthTest, bool depthWrite, int depthFunc,
            SDL_GPUTextureFormat colorFormat, const RenderStateSnapshot& renderState);
        // params == nullptr: the legacy DrawColoredPrimitives path (hardcoded white diffuse,
        // vertexColorEnabled=true). params != nullptr: DrawPrimitivesEx's real GpuDrawParams path.
        void QueueColoredDraw(const IVertexBufferBackend& vb, const IIndexBufferBackend* ib,
                              const Matrix& world, const Matrix& view, const Matrix& projection,
                              PrimitiveType primitive, int primitiveCount,
                              const GpuDrawParams* params = nullptr);
        void QueueTexturedDraw(const IVertexBufferBackend& vb, const IIndexBufferBackend* ib,
                               const Matrix& world, const Matrix& view, const Matrix& projection,
                               PrimitiveType primitive, int primitiveCount, const GpuDrawParams& params);
        void QueueLitTexturedDraw(const IVertexBufferBackend& vb, const IIndexBufferBackend* ib,
                                  const Matrix& world, const Matrix& view, const Matrix& projection,
                                  PrimitiveType primitive, int primitiveCount, const GpuDrawParams& params);

        // Phase SDLGPU-7: AlphaTestEffect / DualTextureEffect.
        void CreateAlphaTestResources();
        void DestroyAlphaTestResources();
        void CreateDualTextureResources();
        void DestroyDualTextureResources();
        [[nodiscard]] SDL_GPUGraphicsPipeline* GetOrCreatePipelineAlphaTest3D(
            std::size_t stride, SDL_GPUPrimitiveType topology, bool depthTest, bool depthWrite, int depthFunc,
            SDL_GPUTextureFormat colorFormat, const RenderStateSnapshot& renderState);
        [[nodiscard]] SDL_GPUGraphicsPipeline* GetOrCreatePipelineDualTexture3D(
            std::size_t stride, SDL_GPUPrimitiveType topology, bool depthTest, bool depthWrite, int depthFunc,
            SDL_GPUTextureFormat colorFormat, const RenderStateSnapshot& renderState);
        void QueueAlphaTestDraw(const IVertexBufferBackend& vb, const IIndexBufferBackend* ib,
                                const Matrix& world, const Matrix& view, const Matrix& projection,
                                PrimitiveType primitive, int primitiveCount, const GpuDrawParams& params);
        void QueueDualTextureDraw(const IVertexBufferBackend& vb, const IIndexBufferBackend* ib,
                                  const Matrix& world, const Matrix& view, const Matrix& projection,
                                  PrimitiveType primitive, int primitiveCount, const GpuDrawParams& params);
        void RenderAlphaTestDraws(SDL_GPURenderPass* pass, SDL_GPUCommandBuffer* cmd,
                                  const DrawTarget& target, SDL_GPUTextureFormat colorFormat);
        void RenderDualTextureDraws(SDL_GPURenderPass* pass, SDL_GPUCommandBuffer* cmd,
                                   const DrawTarget& target, SDL_GPUTextureFormat colorFormat);

        // Phase SDLGPU-9: EnvironmentMapEffect (SDLGPU-33).
        void CreateEnvMapResources();
        void DestroyEnvMapResources();
        [[nodiscard]] SDL_GPUGraphicsPipeline* GetOrCreatePipelineEnvMap3D(
            SDL_GPUPrimitiveType topology, bool depthTest, bool depthWrite, int depthFunc,
            SDL_GPUTextureFormat colorFormat, const RenderStateSnapshot& renderState);
        void QueueEnvMapDraw(const IVertexBufferBackend& vb, const IIndexBufferBackend* ib,
                             const Matrix& world, const Matrix& view, const Matrix& projection,
                             PrimitiveType primitive, int primitiveCount, const GpuDrawParams& params);
        void RenderEnvMapDraws(SDL_GPURenderPass* pass, SDL_GPUCommandBuffer* cmd,
                               const DrawTarget& target, SDL_GPUTextureFormat colorFormat);

        // Phase SDLGPU-7: SkinnedEffect (SDLGPU-34).
        void CreateSkinnedResources();
        void DestroySkinnedResources();
        [[nodiscard]] SDL_GPUGraphicsPipeline* GetOrCreatePipelineSkinned3D(
            SDL_GPUPrimitiveType topology, bool depthTest, bool depthWrite, int depthFunc,
            SDL_GPUTextureFormat colorFormat, const RenderStateSnapshot& renderState);
        void QueueSkinnedDraw(const IVertexBufferBackend& vb, const IIndexBufferBackend* ib,
                              const Matrix& world, const Matrix& view, const Matrix& projection,
                              PrimitiveType primitive, int primitiveCount, const GpuDrawParams& params);
        void RenderSkinnedDraws(SDL_GPURenderPass* pass, SDL_GPUCommandBuffer* cmd,
                                const DrawTarget& target, SDL_GPUTextureFormat colorFormat);

        // Uploads every queued 3D draw command's shadow-copied vertex/index data into a fresh
        // transient SDL_GPUBuffer per command (mirrors WebGPUGraphicsBackend's own per-draw
        // transient-buffer approach) -- must run in the same copy pass as UploadSpriteVertexData,
        // BEFORE BeginGPURenderPass.
        void UploadSceneDrawData(SDL_GPUCommandBuffer* cmd);
        void RenderColoredDraws(SDL_GPURenderPass* pass, SDL_GPUCommandBuffer* cmd,
                               const DrawTarget& target, SDL_GPUTextureFormat colorFormat);
        void RenderTexturedDraws(SDL_GPURenderPass* pass, SDL_GPUCommandBuffer* cmd,
                                 const DrawTarget& target, SDL_GPUTextureFormat colorFormat);
        void RenderLitTexturedDraws(SDL_GPURenderPass* pass, SDL_GPUCommandBuffer* cmd,
                                    const DrawTarget& target, SDL_GPUTextureFormat colorFormat);
        // Releases every transient buffer UploadSceneDrawData created, and clears all 3 queues --
        // safe to call immediately after SDL_SubmitGPUCommandBuffer (SDL_gpu defers the actual
        // free until the GPU is done, per SDL_ReleaseGPUBuffer's own documented contract).
        void ReleaseSceneDrawBuffers();
        [[nodiscard]] SDL_GPUPrimitiveType ToTopology(PrimitiveType primitive) const;
        [[nodiscard]] int PrimitiveVertexCount(PrimitiveType primitive, int primitiveCount) const;
        [[nodiscard]] int PrimitiveIndexCount(PrimitiveType primitive, int primitiveCount) const;

        // Phase SDLGPU-8: renders one off-screen render target's own pass (all draws/sprites
        // queued against it, across every shader family) and regenerates its mip chain afterward
        // if requested -- called once per distinct target used this frame, before the swapchain
        // pass itself (see EnsureFrameRendered's per-target grouping).
        void RenderToTarget(SDL_GPUCommandBuffer* cmd, SdlGpuRenderTargetBackend* target);
        // Phase SDLGPU-8 (SDLGPU-36): renders one RenderTargetCube face's own pass -- called once
        // per distinct (cube, face) pair used this frame, before the swapchain pass.
        void RenderToTargetCubeFace(SDL_GPUCommandBuffer* cmd, SdlGpuRenderTargetCubeBackend* cube, int face);
        // Returns the DrawTarget matching whichever target (swapchain/2D RT/cube face) is
        // currently bound -- 2D and cube binding are mutually exclusive (see
        // SdlGpuRenderTargetBackend::BindAsRenderTarget/SdlGpuRenderTargetCubeBackend::BindAsRenderTargetFace).
        [[nodiscard]] DrawTarget CurrentDrawTarget() const;
        // SDLGPU-18/19/20: snapshots the backend's current blend/cull/fillmode/stencil state --
        // called once per Queue*Draw()/QueueSprite() so later ApplyBlendState/ApplyRasterizerState/
        // ApplyDepthStencilState calls never retroactively change an already-queued draw's baked
        // pipeline (mirrors every other per-command uniform snapshot already established here).
        [[nodiscard]] RenderStateSnapshot CaptureRenderState() const;
        // Applies SDL_SetGPUScissor for the current render pass, using scissorEnabled_'s rect if
        // set, otherwise the full render-target/swapchain extents (SDLGPU-20) -- called once per
        // render pass from RenderSprites/Render*Draws' own pass-setup code, mirroring how
        // viewport/scissor are real render-pass-time state on this backend's Vulkan-driven peers,
        // not baked into any pipeline.
        void ApplyScissorForPass(SDL_GPURenderPass* pass, int targetWidth, int targetHeight) const;

        SDL_Window* window_ = nullptr;
        SDL_GPUDevice* device_ = nullptr;
        SDL_GPUTexture* depthStencilTexture_ = nullptr;
        SDL_GPUTextureFormat depthStencilFormat_ = SDL_GPU_TEXTUREFORMAT_INVALID;

        SDL_GPUShader* spriteVertexShader_ = nullptr;
        SDL_GPUShader* spriteFragmentShader_ = nullptr;
        // Keyed by (int)colorFormat -- Phase SDLGPU-8 needs more than one variant (swapchain
        // format vs. render-target R8G8B8A8_UNORM), unlike Phases 1-7 where sprites only ever
        // targeted the swapchain.
        std::unordered_map<std::size_t, SDL_GPUGraphicsPipeline*> spritePipelines_;
        std::array<SDL_GPUSampler*, 18> samplerCache_{};
        SDL_GPUBuffer* spriteVertexBuffer_ = nullptr;
        Uint32 spriteVertexCapacityBytes_ = 0;
        std::vector<SpriteCommand> spriteCommands_;
        Uint32 depthStencilWidth_ = 0;
        Uint32 depthStencilHeight_ = 0;

        // Phase SDLGPU-6 pipeline caches, keyed by topology*4+depthTest*2+depthWrite (depthFunc
        // is folded in as topology*4*8+depthFunc*4+... -- see the .cpp's PipelineCacheKey() for
        // the exact packing, mirroring WebGPUGraphicsBackend's own int-keyed cache convention).
        SDL_GPUShader* coloredVertexShader_ = nullptr;
        SDL_GPUShader* coloredFragmentShader_ = nullptr;
        std::unordered_map<std::size_t, SDL_GPUGraphicsPipeline*> coloredPipelines_;
        std::vector<ColoredDrawCommand> coloredDrawCommands_;

        SDL_GPUShader* texturedVertexShader_ = nullptr;         ///< stride 20 (VertexPositionTexture)
        SDL_GPUShader* coloredTexturedVertexShader_ = nullptr;  ///< stride 24 (VertexPositionColorTexture)
        SDL_GPUShader* texturedFragmentShader_ = nullptr;       ///< shared by both stride-20/24 pipelines
        std::unordered_map<std::size_t, SDL_GPUGraphicsPipeline*> texturedPipelines_;
        std::unordered_map<std::size_t, SDL_GPUGraphicsPipeline*> coloredTexturedPipelines_;
        std::vector<TexturedDrawCommand> texturedDrawCommands_;

        SDL_GPUShader* litTexturedVertexShader_ = nullptr;
        SDL_GPUShader* litTexturedFragmentShader_ = nullptr;
        std::unordered_map<std::size_t, SDL_GPUGraphicsPipeline*> litTexturedPipelines_;
        std::vector<LitTexturedDrawCommand> litTexturedDrawCommands_;

        // Phase SDLGPU-7. alphaTestPipelines_ holds BOTH stride-20 and stride-32 pipelines
        // (shared shader, different vertex_input_state) -- its cache key folds in the stride
        // (see GetOrCreatePipelineAlphaTest3D's own key computation), unlike every other
        // pipeline map here which is already stride-specific by construction.
        SDL_GPUShader* alphaTestVertexShader_ = nullptr;         ///< strides 20/32 (no vertex colour)
        SDL_GPUShader* alphaTestColoredVertexShader_ = nullptr;  ///< stride 24 (vertex colour tint)
        SDL_GPUShader* alphaTestFragmentShader_ = nullptr;
        std::unordered_map<std::size_t, SDL_GPUGraphicsPipeline*> alphaTestPipelines_;
        std::unordered_map<std::size_t, SDL_GPUGraphicsPipeline*> alphaTestColoredPipelines_;
        std::vector<AlphaTestDrawCommand> alphaTestDrawCommands_;

        SDL_GPUShader* dualTextureVertexShader_ = nullptr;         ///< stride 20
        SDL_GPUShader* dualTextureColoredVertexShader_ = nullptr;  ///< stride 24
        SDL_GPUShader* dualTextureFragmentShader_ = nullptr;
        std::unordered_map<std::size_t, SDL_GPUGraphicsPipeline*> dualTexturePipelines_;
        std::unordered_map<std::size_t, SDL_GPUGraphicsPipeline*> dualTextureColoredPipelines_;
        std::vector<DualTextureDrawCommand> dualTextureDrawCommands_;

        SDL_GPUShader* envMapVertexShader_ = nullptr;
        SDL_GPUShader* envMapFragmentShader_ = nullptr;
        std::unordered_map<std::size_t, SDL_GPUGraphicsPipeline*> envMapPipelines_;
        std::vector<EnvMapDrawCommand> envMapDrawCommands_;

        // No dedicated fragment shader -- reuses litTexturedFragmentShader_ (see SkinnedDrawCommand's
        // own doc comment).
        SDL_GPUShader* skinnedVertexShader_ = nullptr;
        std::unordered_map<std::size_t, SDL_GPUGraphicsPipeline*> skinnedPipelines_;
        std::vector<SkinnedDrawCommand> skinnedDrawCommands_;

        int depthCompareFunction_ = 3;  ///< XNA CompareFunction ordinal; 3 = LessEqual (DepthStencilState.Default)

        // Phase SDLGPU-8: nullptr = the swapchain is the active target. usedRenderTargetsThisFrame_
        // preserves first-bind order so EnsureFrameRendered renders each in that order, all before
        // the swapchain's own pass (see RenderToTarget/EnsureFrameRendered) -- every Clear()/draw
        // call recorded against the same target within one frame is accumulated into that target's
        // single pass, regardless of how many times SetRenderTarget2D re-selected it meanwhile.
        SdlGpuRenderTargetBackend* currentRenderTarget_ = nullptr;
        std::vector<SdlGpuRenderTargetBackend*> usedRenderTargetsThisFrame_;

        // SDLGPU-36: mirrors currentRenderTarget_/usedRenderTargetsThisFrame_ for RenderTargetCube
        // faces -- currentRenderTarget_ and currentRenderTargetCube_ are mutually exclusive (binding
        // one clears the other, matching SetRenderTarget2D/SetRenderTargetCubeFace's real XNA
        // single-current-target semantics).
        SdlGpuRenderTargetCubeBackend* currentRenderTargetCube_ = nullptr;
        int currentActiveCubeFace_ = -1;
        std::vector<std::pair<SdlGpuRenderTargetCubeBackend*, int>> usedRenderTargetCubeFacesThisFrame_;

        // SDLGPU-37: "extra" MRT targets (rts[1..count-1] of the most recent SetRenderTargets()
        // call) -- bound (via MarkUsedThisFrame(), so they get their own pass) and independently
        // cleared (Clear()/ClearXxx() propagate here too), but currentRenderTarget_ always stays
        // rts[0] since draws remain single-target (see SetRenderTargets's own doc comment).
        std::vector<SdlGpuRenderTargetBackend*> currentExtraMrtTargets_;

        int physicalWidth_ = 0;
        int physicalHeight_ = 0;
        int virtualWidth_ = 0;
        int virtualHeight_ = 0;
        CnaPresentationMode presentationMode_ = CnaPresentationMode::FixedHeightDynamicWidth;
        int swapInterval_ = 1;

        bool framePending_ = true;

        bool clearColorPending_ = true;
        bool clearDepthPending_ = true;
        bool clearStencilPending_ = false;
        SDL_FColor clearColor_{0.0f, 0.0f, 0.0f, 1.0f};
        float clearDepth_ = 1.0f;
        Uint8 clearStencil_ = 0;

        bool depthTestEnabled_ = false;
        bool depthWriteEnabled_ = false;
        bool blendEnabled_ = false;  ///< matches BlendState.Opaque (no blend), the real XNA default

        // SDLGPU-18/19/20: "current" render state, mirroring VulkanGraphicsBackend's own
        // blendParams_/dsParams_/cullMode_ pattern for a backend whose pipeline objects are
        // pre-baked/immutable, not a live pipeline-state-object mechanism. Captured into a
        // RenderStateSnapshot at Queue*Draw()/QueueSprite() time (see CaptureRenderState()).
        BlendKeyParams blendParams_;
        int cullMode_ = 2;         ///< XNA CullMode ordinal; 2 = CullCounterClockwiseFace (RasterizerState's real default)
        bool fillModeWireframe_ = false;
        StencilKeyParams stencilParams_;
        int stencilReadMask_ = 0xFF;
        int stencilWriteMask_ = 0xFF;
        int referenceStencil_ = 0;  ///< real render-pass-time state (SDL_SetGPUStencilReference), not baked into any pipeline
        bool scissorEnabled_ = false;
        int scissorX_ = 0;
        int scissorY_ = 0;
        int scissorW_ = 0;
        int scissorH_ = 0;
        // Stored but deliberately not yet applied -- see ApplyRasterizerState's own doc comment.
        float depthBias_ = 0.0f;
        float slopeScaleDepthBias_ = 0.0f;

        // SDLGPU-21: one entry per GraphicsDevice.SamplerStates[slot] (16, matching
        // SamplerStateCollection::MaxSamplers), set by ApplySamplerState() and read directly into
        // each DrawCommand's own textureFilter/addressU/addressV fields at Queue*Draw() time.
        std::array<SamplerSlotState, 16> samplerSlots_;
    };
}
