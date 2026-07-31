// SPDX-License-Identifier: MS-PL
#pragma once

#include "CNA/CNAHelper.hpp"
#include "CNA/Internal/Backends/Llgl/LlglRendererSelection.hpp"
#include "CNA/Internal/Backends/Llgl/LlglSdlSurface.hpp"
#include "../Common/IGraphicsBackend.hpp"

#include <LLGL/LLGL.h>

#include <cstdint>
#include <map>
#include <memory>
#include <vector>

namespace CNA::Internal::Backends::Llgl
{
    class LlglGraphicsBackend;

    /**
     * @brief A 2D texture living in LLGL. NOXNA.
     *
     * Backs `Microsoft::Xna::Framework::Graphics::Texture2D`. Pixels are always stored as RGBA8,
     * the format the shared texture layer hands every backend.
     */
    class LlglTextureBackend final : public ITextureBackend
    {
    public:
        /**
         * @brief Takes ownership of an already-created LLGL texture.
         *
         * @param renderSystem Render system that created @p texture and will release it.
         * @param texture      The texture resource; must not be null.
         * @param width        Width in pixels of mip level 0.
         * @param height       Height in pixels of mip level 0.
         * @param mipLevels    Number of mip levels the texture was created with.
         */
        LlglTextureBackend(LLGL::RenderSystem* renderSystem, LLGL::Texture* texture,
                           int width, int height, int mipLevels);

        /** @brief Releases the LLGL texture. */
        ~LlglTextureBackend() override;

        LlglTextureBackend(const LlglTextureBackend&) = delete;
        LlglTextureBackend& operator=(const LlglTextureBackend&) = delete;

        /** @brief Returns the width in pixels of mip level 0. */
        [[nodiscard]] int GetWidth() const override { return width_; }

        /** @brief Returns the height in pixels of mip level 0. */
        [[nodiscard]] int GetHeight() const override { return height_; }

        /**
         * @brief Returns null: this backend owns no SDL texture.
         *
         * `ITextureBackend::GetNativeTexture` exists for the SDL_Renderer backend's benefit; every
         * GPU-API backend in this project answers null.
         *
         * @return Always null.
         */
        [[nodiscard]] SDL_Texture* GetNativeTexture() const override { return nullptr; }

        /**
         * @brief Replaces the whole of mip level 0.
         *
         * @param rgba   Source pixels, RGBA8.
         * @param stride Row pitch in bytes of @p rgba.
         */
        void UpdatePixels(const std::uint8_t* rgba, int stride) override;

        /**
         * @brief Replaces the whole of one mip level.
         *
         * @param rgba   Source pixels, RGBA8, tightly packed.
         * @param level  Mip level to write.
         * @param levelW Width of that mip level.
         * @param levelH Height of that mip level.
         */
        void UpdatePixelsLevel(int level, const std::uint8_t* rgba, int levelW, int levelH) override;

        /**
         * @brief Reads pixels back from the GPU.
         *
         * @param level      Mip level to read.
         * @param x          Left edge of the region in pixels.
         * @param y          Top edge of the region in pixels.
         * @param w          Width of the region in pixels.
         * @param h          Height of the region in pixels.
         * @param data       Destination for tightly packed RGBA8 rows, top row first.
         * @param dataLength Size of @p data in bytes; must be at least w * h * 4.
         * @return True if the whole region was read back; false if it could not be.
         */
        [[nodiscard]] bool GetData(int level, int x, int y, int w, int h,
                                   void* data, int dataLength) const override;

        /** @brief Returns the underlying LLGL texture. */
        [[nodiscard]] LLGL::Texture* GetLlglTexture() const { return texture_; }

    private:
        LLGL::RenderSystem* renderSystem_ = nullptr;
        LLGL::Texture*      texture_      = nullptr;
        int                 width_        = 0;
        int                 height_       = 0;
        int                 mipLevels_    = 1;
    };

    /**
     * @brief A vertex buffer living in LLGL. NOXNA.
     *
     * Uploads are real: the data reaches GPU memory and the vertex count round-trips. Drawing from
     * one is a 3D-pipeline operation, which this backend does not implement yet -- see
     * `LlglGraphicsBackend::DrawColoredPrimitives`.
     */
    class LlglVertexBufferBackend final : public IVertexBufferBackend
    {
    public:
        /**
         * @brief Creates a buffer sized for @p vertexCapacity vertices of unknown stride.
         *
         * The final byte size is only known once `SetData` supplies a stride, so the LLGL buffer is
         * created lazily on the first upload.
         *
         * @param renderSystem   Render system that will own the buffer.
         * @param vertexCapacity Number of vertices the caller intends to store.
         */
        LlglVertexBufferBackend(LLGL::RenderSystem* renderSystem, int vertexCapacity);

        /** @brief Releases the LLGL buffer. */
        ~LlglVertexBufferBackend() override;

        LlglVertexBufferBackend(const LlglVertexBufferBackend&) = delete;
        LlglVertexBufferBackend& operator=(const LlglVertexBufferBackend&) = delete;

        /**
         * @brief Uploads vertex data.
         *
         * @param data           Packed vertex data.
         * @param vertexCount    Number of vertices in @p data.
         * @param strideInBytes  Size of one vertex in bytes.
         */
        void SetData(const void* data, int vertexCount, std::size_t strideInBytes) override;

        /**
         * @brief Records the caller's vertex declaration.
         *
         * Kept so a later 3D draw path can build the matching LLGL vertex attribute list; the 2D
         * path has its own fixed sprite layout and never consults it.
         *
         * @param vertexDeclaration Declaration in declaration order, including its stride.
         */
        void SetVertexDeclaration(const VertexDeclaration& vertexDeclaration) override;

        /** @brief Returns the number of vertices most recently uploaded. */
        [[nodiscard]] int GetVertexCount() const override { return vertexCount_; }

        /** @brief Returns the underlying LLGL buffer, or null before the first upload. */
        [[nodiscard]] LLGL::Buffer* GetLlglBuffer() const { return buffer_; }

        /** @brief Returns the stride in bytes of the most recent upload, or 0 before it. */
        [[nodiscard]] std::size_t GetStride() const { return stride_; }

    private:
        LLGL::RenderSystem* renderSystem_   = nullptr;
        LLGL::Buffer*       buffer_         = nullptr;
        int                 vertexCapacity_ = 0;
        int                 vertexCount_    = 0;
        std::size_t         stride_         = 0;
        std::size_t         byteCapacity_   = 0;
        bool                hasDeclaration_ = false;
    };

    /**
     * @brief An index buffer living in LLGL. NOXNA.
     *
     * Like `LlglVertexBufferBackend`, uploads are real; consuming one in a draw call belongs to the
     * not-yet-implemented 3D pipeline.
     */
    class LlglIndexBufferBackend final : public IIndexBufferBackend
    {
    public:
        /**
         * @brief Creates an index buffer.
         *
         * @param renderSystem  Render system that will own the buffer.
         * @param indexCapacity Number of indices the caller intends to store.
         * @param thirtyTwoBit  True for 32-bit indices, false for 16-bit.
         */
        LlglIndexBufferBackend(LLGL::RenderSystem* renderSystem, int indexCapacity, bool thirtyTwoBit);

        /** @brief Releases the LLGL buffer. */
        ~LlglIndexBufferBackend() override;

        LlglIndexBufferBackend(const LlglIndexBufferBackend&) = delete;
        LlglIndexBufferBackend& operator=(const LlglIndexBufferBackend&) = delete;

        /**
         * @brief Uploads 16-bit index data.
         *
         * @param data       Packed 16-bit indices.
         * @param indexCount Number of indices.
         */
        void SetData16(const void* data, int indexCount) override;

        /**
         * @brief Uploads 32-bit index data.
         *
         * @param data       Packed 32-bit indices.
         * @param indexCount Number of indices.
         */
        void SetData32(const void* data, int indexCount) override;

        /** @brief Returns the number of indices most recently uploaded. */
        [[nodiscard]] int GetIndexCount() const override { return indexCount_; }

        /** @brief Returns whether this buffer stores 32-bit indices. */
        [[nodiscard]] bool IsThirtyTwoBit() const override { return thirtyTwoBit_; }

        /** @brief Returns the underlying LLGL buffer, or null before the first upload. */
        [[nodiscard]] LLGL::Buffer* GetLlglBuffer() const { return buffer_; }

    private:
        void Upload(const void* data, int indexCount, std::size_t indexSize);

        LLGL::RenderSystem* renderSystem_  = nullptr;
        LLGL::Buffer*       buffer_        = nullptr;
        int                 indexCapacity_ = 0;
        int                 indexCount_    = 0;
        bool                thirtyTwoBit_  = false;
        std::size_t         byteCapacity_  = 0;
    };

    /**
     * @brief Sprite rendering for `Microsoft::Xna::Framework::Graphics::SpriteBatch`. NOXNA.
     *
     * Each `Draw` turns into six vertices appended to the owning backend's frame, so sprites from
     * one Begin/End block reach the GPU in submission order together with the frame's clears.
     */
    class LlglSpriteBatchBackend final : public ISpriteBatchBackend
    {
    public:
        /**
         * @brief Binds this sprite batch to the backend that will draw its sprites.
         *
         * @param owner Backend that owns the swap chain and the frame's command list.
         */
        explicit LlglSpriteBatchBackend(LlglGraphicsBackend& owner);

        /** @brief Resets the per-block sampler and transform state. */
        void Begin() override;

        /** @brief Ends the block; the queued sprites are drawn when the frame is presented. */
        void End() override;

        /**
         * @brief Sets the transform applied on top of the 2D projection.
         *
         * @param m Transform matrix, in XNA row-vector convention.
         */
        void SetTransformMatrix(const Matrix& m) override;

        /**
         * @brief Sets the texture filter used by subsequent draws in this block.
         *
         * @param textureFilter Raw `TextureFilter` ordinal.
         */
        void SetSamplerFilter(int textureFilter) override;

        /**
         * @brief Sets the texture addressing modes used by subsequent draws in this block.
         *
         * @param addressU Raw `TextureAddressMode` ordinal for U.
         * @param addressV Raw `TextureAddressMode` ordinal for V.
         */
        void SetSamplerAddressMode(int addressU, int addressV) override;

        /**
         * @brief Draws a whole texture at a position, unscaled and untinted.
         *
         * @param texture Texture to draw.
         * @param x       Left edge in logical pixels.
         * @param y       Top edge in logical pixels.
         */
        void Draw(const ITextureBackend& texture, float x, float y) override;

        /**
         * @brief Draws a source region of a texture into a destination rectangle with a tint.
         *
         * @param texture              Texture to draw.
         * @param destinationRectangle Destination in logical pixels.
         * @param sourceRectangle      Source region in texels.
         * @param color                Tint multiplied with the sampled texel.
         */
        void Draw(const ITextureBackend& texture,
                  const Rectangle& destinationRectangle,
                  const Rectangle& sourceRectangle,
                  const Color& color) override;

        /**
         * @brief Draws a sprite with rotation, origin and flipping.
         *
         * @param texture              Texture to draw.
         * @param destinationRectangle Destination in logical pixels.
         * @param sourceRectangle      Source region in texels.
         * @param color                Tint multiplied with the sampled texel.
         * @param rotation             Rotation in radians about @p origin.
         * @param origin               Rotation origin in source texels.
         * @param effects              Horizontal/vertical flip flags.
         * @param layerDepth           Layer depth; ignored, this backend draws in submission order.
         */
        void Draw(const ITextureBackend& texture,
                  const Rectangle& destinationRectangle,
                  const Rectangle& sourceRectangle,
                  const Color& color,
                  float rotation,
                  const Vector2& origin,
                  SpriteEffects effects,
                  float layerDepth) override;

    private:
        LlglGraphicsBackend& owner_;
        Matrix               transform_;
        int                  textureFilter_ = 0;
        int                  addressU_      = 1;
        int                  addressV_      = 1;
    };

    /**
     * @brief CNA graphics backend built on LLGL. NOXNA.
     *
     * Unlike the other backends in this project, this one names no native graphics API: LLGL is
     * itself an abstraction layer, and the module it drives (OpenGL or Vulkan) is chosen when the
     * process starts -- see `Detail::ResolveRendererModule`.
     *
     * Scope of the current implementation is the 2D pipeline: swap chain, clears, presentation
     * (including virtual-resolution letterboxing), `Texture2D` upload and readback, and
     * `SpriteBatch` with real blend and sampler state. The 3D pipeline, render targets, cube and
     * volume textures and custom effects are not implemented; each either reports itself
     * unsupported through the shared interface's own "no backend" convention or fails loudly.
     */
    class LlglGraphicsBackend final : public IGraphicsBackend
    {
    public:
        /**
         * @brief Creates the render system, swap chain and sprite pipeline.
         *
         * @param args Window, virtual resolution, presentation mode, sample count and swap
         *             interval requested by `GraphicsDevice`.
         * @throws std::runtime_error If no renderer module can be loaded, the window cannot be
         *         expressed as an LLGL surface, or any GPU resource fails to be created.
         */
        explicit LlglGraphicsBackend(const GraphicsBackendCreateArgs& args);

        /** @brief Releases every GPU resource and unloads the render system. */
        ~LlglGraphicsBackend() override;

        LlglGraphicsBackend(const LlglGraphicsBackend&) = delete;
        LlglGraphicsBackend& operator=(const LlglGraphicsBackend&) = delete;

        /**
         * @brief Queues a colour clear of the whole back buffer.
         *
         * @param r,g,b,a Clear colour components in the range 0..1.
         */
        void Clear(float r, float g, float b, float a) override;

        /**
         * @brief Queues a colour and depth clear.
         *
         * @param r,g,b,a Clear colour components in the range 0..1.
         * @param depth   Depth value to clear with.
         */
        void ClearColorAndDepth(float r, float g, float b, float a, float depth) override;

        /**
         * @brief Queues a depth-only clear.
         *
         * @param depth Depth value to clear with.
         */
        void ClearDepth(float depth) override;

        /**
         * @brief Queues a stencil-only clear.
         *
         * @param stencil Stencil value to clear with.
         */
        void ClearStencil(int stencil) override;

        /**
         * @brief Queues a depth and stencil clear.
         *
         * @param depth   Depth value to clear with.
         * @param stencil Stencil value to clear with.
         */
        void ClearDepthAndStencil(float depth, int stencil) override;

        /**
         * @brief Queues a colour and stencil clear.
         *
         * @param r,g,b,a Clear colour components in the range 0..1.
         * @param stencil Stencil value to clear with.
         */
        void ClearColorAndStencil(float r, float g, float b, float a, int stencil) override;

        /**
         * @brief Queues a colour, depth and stencil clear.
         *
         * @param r,g,b,a Clear colour components in the range 0..1.
         * @param depth   Depth value to clear with.
         * @param stencil Stencil value to clear with.
         */
        void ClearColorDepthAndStencil(float r, float g, float b, float a, float depth, int stencil) override;

        /** @brief Records the frame's commands, submits them and presents the back buffer. */
        void Present() override;

        /**
         * @brief Reports the logical (virtual-resolution) size games draw into.
         *
         * @param width  Receives the logical width in pixels.
         * @param height Receives the logical height in pixels.
         */
        void GetViewportSize(int& width, int& height) override;

        /**
         * @brief Changes the logical resolution at runtime.
         *
         * @param width  New logical width in pixels.
         * @param height New logical height in pixels.
         */
        void SetVirtualResolution(int width, int height) override;

        /**
         * @brief Changes how the logical resolution is fitted onto the window.
         *
         * @param mode Raw `CnaPresentationMode` ordinal.
         */
        void SetPresentationMode(int mode) override;

        /**
         * @brief Changes the swap interval.
         *
         * @param interval 0 for immediate, 1 for vsync, 2 for half refresh rate.
         */
        void SetSwapInterval(int interval) override;

        /** @brief Returns the back buffer's actual sample count, or 0 when not multisampled. */
        [[nodiscard]] int GetMultiSampleCount() const override;

        /**
         * @brief Converts a point from window pixels to logical game coordinates.
         *
         * @param windowX Window-space X in pixels.
         * @param windowY Window-space Y in pixels.
         * @param logX    Receives the logical X.
         * @param logY    Receives the logical Y.
         * @return True when the conversion was performed.
         */
        bool TransformWindowToLogical(float windowX, float windowY,
                                      float& logX, float& logY) const override;

        /**
         * @brief Converts a point from logical game coordinates to window pixels.
         *
         * @param logX    Logical X.
         * @param logY    Logical Y.
         * @param windowX Receives the window-space X in pixels.
         * @param windowY Receives the window-space Y in pixels.
         * @return True when the conversion was performed.
         */
        bool TransformLogicalToWindow(float logX, float logY,
                                      float& windowX, float& windowY) const override;

        /** @brief Returns the SDL window this backend presents to. */
        [[nodiscard]] SDL_Window* GetWindowInternal() const override { return window_; }

        /** @brief Returns null: this backend does not use SDL_Renderer. */
        [[nodiscard]] SDL_Renderer* GetRendererInternal() const override { return nullptr; }

        /**
         * @brief Creates a 2D texture from RGBA8 pixels.
         *
         * @param data Image dimensions, mip level count and pixel data.
         * @return The new texture backend.
         */
        std::unique_ptr<ITextureBackend> CreateTexture(const ImageData& data) override;

        /** @brief Creates a sprite batch bound to this backend. */
        std::unique_ptr<ISpriteBatchBackend> CreateSpriteBatch() override;

        /**
         * @brief Creates a vertex buffer.
         *
         * @param vertex_capacity Number of vertices the caller intends to store.
         * @return The new vertex buffer backend.
         */
        std::unique_ptr<IVertexBufferBackend> CreateVertexBuffer(int vertex_capacity) override;

        /**
         * @brief Creates a 16-bit index buffer.
         *
         * @param index_capacity Number of indices the caller intends to store.
         * @return The new index buffer backend.
         */
        std::unique_ptr<IIndexBufferBackend> CreateIndexBuffer16(int index_capacity) override;

        /**
         * @brief Creates a 32-bit index buffer.
         *
         * @param index_capacity Number of indices the caller intends to store.
         * @return The new index buffer backend.
         */
        std::unique_ptr<IIndexBufferBackend> CreateIndexBuffer32(int index_capacity) override;

        /**
         * @brief Reads rendered back-buffer pixels.
         *
         * @param x      Left edge of the region in window pixels.
         * @param y      Top edge of the region in window pixels.
         * @param w      Width of the region in pixels.
         * @param h      Height of the region in pixels.
         * @param pixels Destination for w * h * 4 bytes of RGBA8, top row first.
         * @throws std::runtime_error If the copy could not be performed.
         */
        void ReadBackbuffer(int x, int y, int w, int h, std::uint8_t* pixels) override;

        /**
         * @brief Binds a set of render targets.
         *
         * This backend creates no render targets yet, so the only accepted request is the one that
         * restores the back buffer; anything else fails loudly rather than silently drawing to the
         * window.
         *
         * @param renderTargets Ordered target descriptors, or null.
         * @param count         Number of descriptors; 0 restores the back buffer.
         * @throws std::runtime_error If any target is supplied.
         */
        void SetRenderTargets(const RenderTargetBindingDescriptor* renderTargets, int count) override;

        /**
         * @brief Applies a `BlendState`.
         *
         * @param colorSrcBlend  Raw `Blend` ordinal for the colour source factor.
         * @param alphaSrcBlend  Raw `Blend` ordinal for the alpha source factor.
         * @param colorDstBlend  Raw `Blend` ordinal for the colour destination factor.
         * @param alphaDstBlend  Raw `Blend` ordinal for the alpha destination factor.
         * @param colorBlendFunc Raw `BlendFunction` ordinal for colour.
         * @param alphaBlendFunc Raw `BlendFunction` ordinal for alpha.
         * @param writeState     Colour write masks and the coverage sample mask.
         */
        void ApplyBlendState(int colorSrcBlend, int alphaSrcBlend,
                             int colorDstBlend, int alphaDstBlend,
                             int colorBlendFunc, int alphaBlendFunc,
                             const BlendWriteState& writeState) override;

        /**
         * @brief Sets the constant blend colour used by the BlendFactor blend modes.
         *
         * @param r,g,b,a Blend factor components in the range 0..1.
         */
        void SetBlendFactor(float r, float g, float b, float a) override;

        /**
         * @brief Applies a `SamplerState` to a texture slot.
         *
         * @param slot          Texture slot index; only slot 0 is consumed by the 2D pipeline.
         * @param filter        Raw `TextureFilter` ordinal.
         * @param addressU      Raw `TextureAddressMode` ordinal for U.
         * @param addressV      Raw `TextureAddressMode` ordinal for V.
         * @param maxAnisotropy Maximum anisotropy, 1..16.
         */
        void ApplySamplerState(int slot, int filter, int addressU, int addressV,
                               int maxAnisotropy) override;

        /**
         * @brief Applies a `RasterizerState`.
         *
         * Only the scissor-enable bit affects the 2D pipeline; cull mode and fill mode belong to
         * the 3D pipeline and are recorded for it.
         *
         * @param cullMode            Raw `CullMode` ordinal.
         * @param fillMode            Raw `FillMode` ordinal.
         * @param scissorTestEnable   Whether the scissor test is enabled.
         * @param depthBias           Constant depth bias.
         * @param slopeScaleDepthBias Slope-scaled depth bias.
         */
        void ApplyRasterizerState(int cullMode, int fillMode, bool scissorTestEnable,
                                  float depthBias, float slopeScaleDepthBias) override;

        /**
         * @brief Sets the scissor rectangle in logical coordinates.
         *
         * @param x Left edge.
         * @param y Top edge.
         * @param w Width.
         * @param h Height.
         */
        void SetScissorRect(int x, int y, int w, int h) override;

        /**
         * @brief Sets the viewport rectangle in logical coordinates.
         *
         * @param x        Left edge.
         * @param y        Top edge.
         * @param w        Width.
         * @param h        Height.
         * @param minDepth Minimum depth.
         * @param maxDepth Maximum depth.
         */
        void SetViewport(int x, int y, int w, int h, float minDepth, float maxDepth) override;

        /**
         * @brief Records whether depth testing is enabled.
         *
         * @param enabled Requested depth test state.
         */
        void SetDepthTestEnabled(bool enabled) override;

        /**
         * @brief Records whether blending is enabled.
         *
         * @param enabled Requested blend state.
         */
        void SetBlendEnabled(bool enabled) override;

        /**
         * @brief Records whether depth writes are enabled.
         *
         * @param enabled Requested depth write state.
         */
        void SetDepthWriteEnabled(bool enabled) override;

        /**
         * @brief Draws colour-only primitives; part of the not-yet-implemented 3D pipeline.
         *
         * @param vb             Vertex buffer to draw from.
         * @param world          World matrix.
         * @param view           View matrix.
         * @param projection     Projection matrix.
         * @param primitive      Primitive topology.
         * @param primitiveCount Number of primitives.
         * @throws std::runtime_error Always.
         */
        void DrawColoredPrimitives(const IVertexBufferBackend& vb,
                                   const Matrix& world, const Matrix& view, const Matrix& projection,
                                   PrimitiveType primitive, int primitiveCount) override;

        /**
         * @brief Draws indexed colour-only primitives; part of the not-yet-implemented 3D pipeline.
         *
         * @param vb             Vertex buffer to draw from.
         * @param ib             Index buffer to draw with.
         * @param world          World matrix.
         * @param view           View matrix.
         * @param projection     Projection matrix.
         * @param primitive      Primitive topology.
         * @param primitiveCount Number of primitives.
         * @throws std::runtime_error Always.
         */
        void DrawIndexedColoredPrimitives(const IVertexBufferBackend& vb, const IIndexBufferBackend& ib,
                                          const Matrix& world, const Matrix& view, const Matrix& projection,
                                          PrimitiveType primitive, int primitiveCount) override;

        /**
         * @brief Reports whether a graphics capability is supported by this backend today.
         *
         * @param capability Capability to query.
         * @return True only for capabilities the current 2D implementation genuinely provides.
         */
        [[nodiscard]] bool SupportsCapability(CNA::GraphicsCapability capability) const override;

        /** @brief Returns the largest single-axis texture dimension the device reports. */
        [[nodiscard]] int GetMaxTextureDimension() const override;

        /** @brief Returns the renderer module this instance actually loaded. */
        [[nodiscard]] Detail::RendererModule GetRendererModule() const { return module_; }

        /** @brief Returns the LLGL renderer name reported by the loaded module. */
        [[nodiscard]] const char* GetRendererNameEXT() const;

        /**
         * @brief Appends one sprite's geometry to the current frame.
         *
         * Called by `LlglSpriteBatchBackend`; not part of the shared backend interface.
         *
         * @param texture     Texture to sample.
         * @param destination Destination rectangle in logical pixels.
         * @param source      Source region in texels.
         * @param color       Tint multiplied with the sampled texel.
         * @param rotation    Rotation in radians about @p origin.
         * @param origin      Rotation origin in source texels.
         * @param effects     Horizontal/vertical flip flags.
         * @param transform   SpriteBatch transform matrix.
         * @param filter      Raw `TextureFilter` ordinal for this draw.
         * @param addressU    Raw `TextureAddressMode` ordinal for U.
         * @param addressV    Raw `TextureAddressMode` ordinal for V.
         */
        void QueueSpriteEXT(const LlglTextureBackend& texture,
                            const Rectangle& destination,
                            const Rectangle& source,
                            const Color& color,
                            float rotation,
                            const Vector2& origin,
                            SpriteEffects effects,
                            const Matrix& transform,
                            int filter, int addressU, int addressV);

    private:
        /** @brief One recorded frame operation, replayed in submission order at Present(). */
        struct FrameCommand
        {
            enum class Kind { Clear, Sprite };

            /** @brief Which operation this entry replays. */
            Kind             kind         = Kind::Clear;
            long             clearFlags   = 0;
            float            clearColor[4] = {0.0f, 0.0f, 0.0f, 0.0f};
            float            clearDepth   = 1.0f;
            std::uint32_t    clearStencil = 0;
            LLGL::Texture*   texture      = nullptr;
            LLGL::Sampler*   sampler      = nullptr;
            LLGL::PipelineState* pipeline = nullptr;
            std::uint32_t    firstVertex  = 0;
            std::uint32_t    vertexCount  = 0;
            float            blendFactor[4] = {1.0f, 1.0f, 1.0f, 1.0f};
            std::int32_t     scissor[4]   = {0, 0, 0, 0};
            bool             scissorEnabled = false;
        };

        /** @brief The letterboxed destination rectangle of the logical canvas, in window pixels. */
        struct PresentationRect
        {
            float x      = 0.0f;
            float y      = 0.0f;
            float width  = 0.0f;
            float height = 0.0f;
            float logicalWidth  = 0.0f;
            float logicalHeight = 0.0f;
        };

        void CreateSpritePipelineResources();
        void CaptureBackbuffer();
        void ReplayFrameCommands();
        void UpdateSwapChainResolution();
        [[nodiscard]] PresentationRect ComputePresentationRect() const;
        [[nodiscard]] std::uint64_t MakeBlendPipelineKey(bool scissorEnabled) const;
        LLGL::PipelineState* AcquireSpritePipeline(bool scissorEnabled);
        LLGL::Sampler* AcquireSampler(int filter, int addressU, int addressV, int maxAnisotropy);
        void QueueClear(long flags, const float color[4], float depth, std::uint32_t stencil);
        void UploadFrameResources();
        void RecordAndSubmitFrame();
        [[nodiscard]] bool ComputeEffectiveScissor(std::int32_t outRect[4]) const;

        SDL_Window*                 window_        = nullptr;
        Detail::RendererModule      module_        = Detail::RendererModule::OpenGL;
        LLGL::RenderSystemPtr       renderer_;
        std::shared_ptr<LlglSdlSurface> surface_;
        LLGL::SwapChain*            swapChain_     = nullptr;
        LLGL::CommandBuffer*        commands_      = nullptr;
        LLGL::CommandQueue*         queue_         = nullptr;

        LLGL::PipelineLayout*       spriteLayout_  = nullptr;
        LLGL::Shader*               spriteVertexShader_   = nullptr;
        LLGL::Shader*               spriteFragmentShader_ = nullptr;
        LLGL::Buffer*               spriteProjectionBuffer_ = nullptr;
        LLGL::Buffer*               spriteVertexBuffer_     = nullptr;
        std::size_t                 spriteVertexCapacity_   = 0;

        std::map<std::uint64_t, LLGL::PipelineState*> pipelineCache_;
        std::map<std::uint64_t, LLGL::Sampler*>       samplerCache_;

        std::vector<float>          spriteVertexData_;
        std::vector<FrameCommand>   frameCommands_;
        /// True when ReadBackbuffer() already recorded and submitted the current frame, so
        /// Present() must not record it a second time.
        bool                        frameSubmitted_ = false;

        /// Whole-back-buffer capture serving every ReadBackbuffer() of the same frame; the swap
        /// chain's render pass discards its colour attachment on entry, so it can only be read once.
        std::vector<std::uint8_t>   backbufferCache_;
        int                         backbufferCacheWidth_  = 0;
        int                         backbufferCacheHeight_ = 0;
        bool                        backbufferCacheValid_  = false;

        int   virtualWidth_   = 0;
        int   virtualHeight_  = 0;
        int   presentationMode_ = 0;
        int   swapInterval_   = 1;
        int   requestedSampleCount_ = 1;

        int   colorSrcBlend_  = 0;
        int   alphaSrcBlend_  = 0;
        int   colorDstBlend_  = 1;
        int   alphaDstBlend_  = 1;
        int   colorBlendFunc_ = 0;
        int   alphaBlendFunc_ = 0;
        int   colorWriteChannels_ = 15;
        bool  blendEnabled_   = false;
        float blendFactor_[4] = {1.0f, 1.0f, 1.0f, 1.0f};

        int   samplerFilter_  = 0;
        int   samplerAddressU_ = 1;
        int   samplerAddressV_ = 1;
        int   samplerMaxAnisotropy_ = 1;

        bool  scissorTestEnabled_ = false;
        bool  scissorRectSet_     = false;
        std::int32_t scissorRect_[4] = {0, 0, 0, 0};
        bool  viewportSet_        = false;
        std::int32_t viewportRect_[4] = {0, 0, 0, 0};

        bool  depthTestEnabled_  = false;
        bool  depthWriteEnabled_ = true;
        int   cullMode_ = 2;
        int   fillMode_ = 0;
    };
}
