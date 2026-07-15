// SPDX-License-Identifier: MS-PL
#pragma once

#include "CNA/CNAHelper.hpp"
#include "../Common/IGraphicsBackend.hpp"

#include <SDL3/SDL_gpu.h>

#include <array>
#include <cstdint>
#include <unordered_map>
#include <vector>

namespace CNA::Internal::Backends::SdlGpu
{
    class SdlGpuGraphicsBackend;

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
    public:
        /** @brief Vertex layout for the `sprite2d` pipeline: position, UV, RGBA color (32 bytes). */
        struct SpriteVertex
        {
            float x, y;
            float u, v;
            float r, g, b, a;
        };

        struct SpriteCommand
        {
            const SdlGpuTextureBackend* texture = nullptr;
            std::array<SpriteVertex, 6> vertices{};
            int textureFilter = 0;
            int addressU = 1;
            int addressV = 1;
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

        /** @brief Queues a sprite quad for drawing on the next render pass. NOXNA — internal use only. */
        NOXNA void QueueSprite(const SdlGpuTextureBackend& texture,
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
        [[nodiscard]] SDL_GPUGraphicsPipeline* GetOrCreateSpritePipeline();
        [[nodiscard]] SDL_GPUSampler* GetOrCreateSampler(int textureFilter, int addressU, int addressV);
        // Uploads all queued sprite vertex data (copy pass) -- must run BEFORE
        // BeginGPURenderPass; SDL_gpu forbids a copy pass nested inside a render pass.
        void UploadSpriteVertexData(SDL_GPUCommandBuffer* cmd);
        // Issues the actual bind+draw calls for each queued sprite -- must run INSIDE the
        // render pass, after UploadSpriteVertexData's copy pass has already been submitted-queued
        // on the same command buffer.
        void RenderSprites(SDL_GPURenderPass* pass, SDL_GPUCommandBuffer* cmd);

        // Phase SDLGPU-6: colored3d/textured3d/colored_textured3d/lit_textured3d.
        void CreateColoredResources();
        void DestroyColoredResources();
        void CreateTexturedResources();   ///< also creates colored_textured3d's vertex shader (shares textured3d's fragment shader)
        void DestroyTexturedResources();
        void CreateLitTexturedResources();
        void DestroyLitTexturedResources();
        [[nodiscard]] SDL_GPUGraphicsPipeline* GetOrCreatePipelineColored3D(
            SDL_GPUPrimitiveType topology, bool depthTest, bool depthWrite, int depthFunc);
        [[nodiscard]] SDL_GPUGraphicsPipeline* GetOrCreatePipelineTextured3D(
            SDL_GPUPrimitiveType topology, bool depthTest, bool depthWrite, int depthFunc);
        [[nodiscard]] SDL_GPUGraphicsPipeline* GetOrCreatePipelineColoredTextured3D(
            SDL_GPUPrimitiveType topology, bool depthTest, bool depthWrite, int depthFunc);
        [[nodiscard]] SDL_GPUGraphicsPipeline* GetOrCreatePipelineLitTextured3D(
            SDL_GPUPrimitiveType topology, bool depthTest, bool depthWrite, int depthFunc);
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
        // Uploads every queued 3D draw command's shadow-copied vertex/index data into a fresh
        // transient SDL_GPUBuffer per command (mirrors WebGPUGraphicsBackend's own per-draw
        // transient-buffer approach) -- must run in the same copy pass as UploadSpriteVertexData,
        // BEFORE BeginGPURenderPass.
        void UploadSceneDrawData(SDL_GPUCommandBuffer* cmd);
        void RenderColoredDraws(SDL_GPURenderPass* pass, SDL_GPUCommandBuffer* cmd);
        void RenderTexturedDraws(SDL_GPURenderPass* pass, SDL_GPUCommandBuffer* cmd);
        void RenderLitTexturedDraws(SDL_GPURenderPass* pass, SDL_GPUCommandBuffer* cmd);
        // Releases every transient buffer UploadSceneDrawData created, and clears all 3 queues --
        // safe to call immediately after SDL_SubmitGPUCommandBuffer (SDL_gpu defers the actual
        // free until the GPU is done, per SDL_ReleaseGPUBuffer's own documented contract).
        void ReleaseSceneDrawBuffers();
        [[nodiscard]] SDL_GPUPrimitiveType ToTopology(PrimitiveType primitive) const;
        [[nodiscard]] int PrimitiveVertexCount(PrimitiveType primitive, int primitiveCount) const;
        [[nodiscard]] int PrimitiveIndexCount(PrimitiveType primitive, int primitiveCount) const;

        SDL_Window* window_ = nullptr;
        SDL_GPUDevice* device_ = nullptr;
        SDL_GPUTexture* depthStencilTexture_ = nullptr;
        SDL_GPUTextureFormat depthStencilFormat_ = SDL_GPU_TEXTUREFORMAT_INVALID;

        SDL_GPUShader* spriteVertexShader_ = nullptr;
        SDL_GPUShader* spriteFragmentShader_ = nullptr;
        SDL_GPUGraphicsPipeline* spritePipeline_ = nullptr;
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

        int depthCompareFunction_ = 3;  ///< XNA CompareFunction ordinal; 3 = LessEqual (DepthStencilState.Default)

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
