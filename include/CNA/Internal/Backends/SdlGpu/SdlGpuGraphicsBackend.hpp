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
     * @brief `SDL_gpu`-backed `RenderTarget2D` (Phase `SDLGPU-8`, `SDLGPU-35`).
     *
     * Owns a standalone `COLOR_TARGET | SAMPLER` texture rendered into its own render pass, one
     * per frame (see `SdlGpuGraphicsBackend::EnsureFrameRendered`'s per-target grouping) --
     * offscreen passes run before the swapchain pass so a target bound-then-unbound earlier in
     * the frame can safely be sampled by a later swapchain-targeted draw within the same frame.
     * MSAA (`SDLGPU-38`) is not implemented yet -- `CreateRenderTarget2D` throws if
     * `multiSampleCount > 0` is requested rather than silently ignoring it.
     */
    class SdlGpuRenderTargetBackend final : public IRenderTargetBackend
    {
    public:
        SdlGpuRenderTargetBackend(SdlGpuGraphicsBackend& owner, int width, int height,
                                  int depthFormat, bool mipMap);
        ~SdlGpuRenderTargetBackend() override;

        SdlGpuRenderTargetBackend(const SdlGpuRenderTargetBackend&) = delete;
        SdlGpuRenderTargetBackend& operator=(const SdlGpuRenderTargetBackend&) = delete;

        [[nodiscard]] int GetWidth() const override { return width_; }
        [[nodiscard]] int GetHeight() const override { return height_; }
        [[nodiscard]] SDL_Texture* GetNativeTexture() const override { return nullptr; }

        void BindAsRenderTarget() override;
        void UnbindAsRenderTarget() override;
        [[nodiscard]] bool HasRealDepthBuffer(bool depthFormatWasRequested) const override
        {
            return depthFormatWasRequested && depthTexture_ != nullptr;
        }

        /** @brief Returns the sampleable color texture. NOXNA — internal use only. */
        NOXNA [[nodiscard]] SDL_GPUTexture* ColorTexture() const { return colorTexture_; }
        /** @brief Returns the depth/stencil texture, or null when `DepthFormat::None` was requested. NOXNA. */
        NOXNA [[nodiscard]] SDL_GPUTexture* DepthTexture() const { return depthTexture_; }
        /** @brief Whether a mip chain should be regenerated after this target's pass each frame. NOXNA. */
        NOXNA [[nodiscard]] bool WantsMipMap() const { return mipMap_; }

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

    private:
        SdlGpuGraphicsBackend* owner_ = nullptr;
        int width_ = 0;
        int height_ = 0;
        bool mipMap_ = false;
        SDL_GPUTexture* colorTexture_ = nullptr;
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
            const SdlGpuTextureBackend* texture = nullptr;
            int textureFilter = 0;
            int addressU = 1;
            int addressV = 1;
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
            const SdlGpuTextureBackend* texture = nullptr;
            int textureFilter = 0;
            int addressU = 1;
            int addressV = 1;
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
            const SdlGpuTextureBackend* texture = nullptr;
            int textureFilter = 0;
            int addressU = 1;
            int addressV = 1;
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
            const SdlGpuTextureBackend* texture0 = nullptr;
            const SdlGpuTextureBackend* texture1 = nullptr;
            int textureFilter = 0;
            int addressU = 1;
            int addressV = 1;
            bool hasVertexColor = false;  ///< stride 24 vs stride 20
            DrawTarget target;  ///< default = swapchain
            SDL_GPUBuffer* uploadedVertexBuffer = nullptr;
            SDL_GPUBuffer* uploadedIndexBuffer = nullptr;
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
        /** @brief Stores the blend-enabled flag (not yet wired to a pipeline — every 3D pipeline is currently opaque, no blend). */
        void SetBlendEnabled(bool enabled) override { blendEnabled_ = enabled; }
        /** @brief Stores the depth-write enabled flag, read by the 3D draw path's pipeline cache key. */
        void SetDepthWriteEnabled(bool enabled) override { depthWriteEnabled_ = enabled; }

        std::unique_ptr<IVertexBufferBackend> CreateVertexBuffer(int vertex_capacity) override;
        std::unique_ptr<IIndexBufferBackend> CreateIndexBuffer16(int index_capacity) override;

        /**
         * @brief Creates an off-screen `RenderTarget2D` (Phase `SDLGPU-8`, `SDLGPU-35`).
         *
         * @param multiSampleCount Must be 0 — MSAA render targets are `SDLGPU-38`, not yet
         *        implemented; a non-zero value throws rather than silently rendering without MSAA.
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
                                int addressV);

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
        [[nodiscard]] SDL_GPUGraphicsPipeline* GetOrCreateSpritePipeline(SDL_GPUTextureFormat colorFormat);
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
            SDL_GPUTextureFormat colorFormat);
        [[nodiscard]] SDL_GPUGraphicsPipeline* GetOrCreatePipelineTextured3D(
            SDL_GPUPrimitiveType topology, bool depthTest, bool depthWrite, int depthFunc,
            SDL_GPUTextureFormat colorFormat);
        [[nodiscard]] SDL_GPUGraphicsPipeline* GetOrCreatePipelineColoredTextured3D(
            SDL_GPUPrimitiveType topology, bool depthTest, bool depthWrite, int depthFunc,
            SDL_GPUTextureFormat colorFormat);
        [[nodiscard]] SDL_GPUGraphicsPipeline* GetOrCreatePipelineLitTextured3D(
            SDL_GPUPrimitiveType topology, bool depthTest, bool depthWrite, int depthFunc,
            SDL_GPUTextureFormat colorFormat);
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
            SDL_GPUTextureFormat colorFormat);
        [[nodiscard]] SDL_GPUGraphicsPipeline* GetOrCreatePipelineDualTexture3D(
            std::size_t stride, SDL_GPUPrimitiveType topology, bool depthTest, bool depthWrite, int depthFunc,
            SDL_GPUTextureFormat colorFormat);
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

        SDL_Window* window_ = nullptr;
        SDL_GPUDevice* device_ = nullptr;
        SDL_GPUTexture* depthStencilTexture_ = nullptr;
        SDL_GPUTextureFormat depthStencilFormat_ = SDL_GPU_TEXTUREFORMAT_INVALID;

        SDL_GPUShader* spriteVertexShader_ = nullptr;
        SDL_GPUShader* spriteFragmentShader_ = nullptr;
        // Keyed by (int)colorFormat -- Phase SDLGPU-8 needs more than one variant (swapchain
        // format vs. render-target R8G8B8A8_UNORM), unlike Phases 1-7 where sprites only ever
        // targeted the swapchain.
        std::unordered_map<int, SDL_GPUGraphicsPipeline*> spritePipelines_;
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
        std::unordered_map<int, SDL_GPUGraphicsPipeline*> coloredPipelines_;
        std::vector<ColoredDrawCommand> coloredDrawCommands_;

        SDL_GPUShader* texturedVertexShader_ = nullptr;         ///< stride 20 (VertexPositionTexture)
        SDL_GPUShader* coloredTexturedVertexShader_ = nullptr;  ///< stride 24 (VertexPositionColorTexture)
        SDL_GPUShader* texturedFragmentShader_ = nullptr;       ///< shared by both stride-20/24 pipelines
        std::unordered_map<int, SDL_GPUGraphicsPipeline*> texturedPipelines_;
        std::unordered_map<int, SDL_GPUGraphicsPipeline*> coloredTexturedPipelines_;
        std::vector<TexturedDrawCommand> texturedDrawCommands_;

        SDL_GPUShader* litTexturedVertexShader_ = nullptr;
        SDL_GPUShader* litTexturedFragmentShader_ = nullptr;
        std::unordered_map<int, SDL_GPUGraphicsPipeline*> litTexturedPipelines_;
        std::vector<LitTexturedDrawCommand> litTexturedDrawCommands_;

        // Phase SDLGPU-7. alphaTestPipelines_ holds BOTH stride-20 and stride-32 pipelines
        // (shared shader, different vertex_input_state) -- its cache key folds in the stride
        // (see GetOrCreatePipelineAlphaTest3D's own key computation), unlike every other
        // pipeline map here which is already stride-specific by construction.
        SDL_GPUShader* alphaTestVertexShader_ = nullptr;         ///< strides 20/32 (no vertex colour)
        SDL_GPUShader* alphaTestColoredVertexShader_ = nullptr;  ///< stride 24 (vertex colour tint)
        SDL_GPUShader* alphaTestFragmentShader_ = nullptr;
        std::unordered_map<int, SDL_GPUGraphicsPipeline*> alphaTestPipelines_;
        std::unordered_map<int, SDL_GPUGraphicsPipeline*> alphaTestColoredPipelines_;
        std::vector<AlphaTestDrawCommand> alphaTestDrawCommands_;

        SDL_GPUShader* dualTextureVertexShader_ = nullptr;         ///< stride 20
        SDL_GPUShader* dualTextureColoredVertexShader_ = nullptr;  ///< stride 24
        SDL_GPUShader* dualTextureFragmentShader_ = nullptr;
        std::unordered_map<int, SDL_GPUGraphicsPipeline*> dualTexturePipelines_;
        std::unordered_map<int, SDL_GPUGraphicsPipeline*> dualTextureColoredPipelines_;
        std::vector<DualTextureDrawCommand> dualTextureDrawCommands_;

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
        bool blendEnabled_ = true;
    };
}
