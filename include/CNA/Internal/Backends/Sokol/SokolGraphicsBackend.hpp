// SPDX-License-Identifier: MS-PL
#pragma once

#include "CNA/CNAHelper.hpp"
#include "../Common/IGraphicsBackend.hpp"

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

struct SDL_Window;

namespace CNA::Internal::Backends::Sokol
{
    class SokolGraphicsBackend;

    /**
     * @brief Identifies the native API sokol_gfx was compiled to dispatch onto. NOXNA.
     *
     * Mirrors the CNA_SOKOL_API CMake option (cmake/BackendSelection.cmake) so a caller can read
     * back which API this build actually resolved to without including sokol_gfx.h itself.
     */
    enum class SokolApiEXT : std::uint8_t
    {
        /** @brief Desktop OpenGL 4.1 core (SOKOL_GLCORE). */
        GLCore,
        /** @brief OpenGL ES 3 / WebGL 2 (SOKOL_GLES3). */
        GLES3,
        /** @brief Direct3D 11 (SOKOL_D3D11). */
        D3D11,
        /** @brief Metal (SOKOL_METAL). */
        Metal,
        /** @brief WebGPU (SOKOL_WGPU). */
        WebGPU
    };

    /**
     * @brief Backend handle for a 2D texture, backed by a sokol_gfx image plus its texture view.
     *
     * sokol_gfx has no partial-image upload and permits at most one sg_update_image() per image
     * per frame, which a caller writing several mip levels in one frame violates immediately. This
     * class therefore keeps a CPU shadow of every mip level and recreates the whole image as an
     * immutable one whenever any level changes -- creation with initial data carries no per-frame
     * restriction, the same reasoning SokolVertexBufferBackend::SetData uses.
     */
    class SokolTextureBackend : public ITextureBackend
    {
    public:
        /**
         * @brief Creates a sokol_gfx image from decoded RGBA8 pixels and a matching texture view.
         *
         * @param data Source image; data.mipLevels mip levels are allocated, level 0 is uploaded
         *             from data.pixels and every further level starts out transparent black until
         *             UpdatePixelsLevel() supplies it.
         */
        explicit SokolTextureBackend(const ImageData& data);

        /** @brief Destroys the sokol_gfx view and image owned by this texture. */
        ~SokolTextureBackend() override;

        SokolTextureBackend(const SokolTextureBackend&) = delete;
        SokolTextureBackend& operator=(const SokolTextureBackend&) = delete;

        /**
         * @brief Returns the width of mip level 0 in pixels.
         * @return Texture width.
         */
        [[nodiscard]] int GetWidth() const override;

        /**
         * @brief Returns the height of mip level 0 in pixels.
         * @return Texture height.
         */
        [[nodiscard]] int GetHeight() const override;

        /**
         * @brief Returns null; this backend renders through sokol_gfx, not SDL_Renderer.
         * @return Always nullptr.
         */
        [[nodiscard]] SDL_Texture* GetNativeTexture() const override;

        /**
         * @brief Replaces mip level 0 in place.
         *
         * @param rgba   Source pixels, RGBA8.
         * @param stride Row pitch in bytes; rows are repacked when it exceeds width * 4.
         */
        void UpdatePixels(const uint8_t* rgba, int stride) override;

        /**
         * @brief Replaces a single mip level in place and re-uploads the whole image.
         *
         * @param level  Mip level to write.
         * @param rgba   Source pixels, RGBA8, tightly packed.
         * @param levelW Width of that mip level in pixels.
         * @param levelH Height of that mip level in pixels.
         */
        void UpdatePixelsLevel(int level, const uint8_t* rgba, int levelW, int levelH) override;

        /**
         * @brief Reads back raw RGBA8 pixels from the CPU shadow of the given mip level.
         *
         * Satisfies the ITextureBackend::GetData contract from the shadow rather than the GPU:
         * this backend has no render targets yet, so every texture's content came from a
         * SetData() this class already recorded.
         *
         * @param level      Mip level to read.
         * @param x          Left edge of the requested region, in pixels.
         * @param y          Top edge of the requested region, in pixels.
         * @param w          Width of the requested region, in pixels.
         * @param h          Height of the requested region, in pixels.
         * @param data       Destination for tightly packed RGBA8 rows, top row first.
         * @param dataLength Size of @p data in bytes.
         * @return True if the whole region was written; false if the region or level is unknown.
         */
        [[nodiscard]] bool GetData(int level, int x, int y, int w, int h,
                                   void* data, int dataLength) const override;

        /**
         * @brief Returns the raw sokol_gfx image handle id backing this texture. NOXNA.
         * @return sg_image id, or 0 when creation failed.
         */
        NOXNA [[nodiscard]] std::uint32_t GetImageIdEXT() const { return imageId_; }

        /**
         * @brief Returns the raw sokol_gfx texture-view handle id used to bind this texture. NOXNA.
         * @return sg_view id, or 0 when creation failed.
         */
        NOXNA [[nodiscard]] std::uint32_t GetViewIdEXT() const { return viewId_; }

    private:
        void RecreateImage();
        void DestroyImage();

        int width_ = 0;
        int height_ = 0;
        int mipLevels_ = 1;
        std::uint32_t imageId_ = 0;
        std::uint32_t viewId_ = 0;
        /// One tightly packed RGBA8 buffer per mip level, kept because sokol_gfx can only replace
        /// an image in full.
        std::vector<std::vector<std::uint8_t>> levels_;
    };

    /**
     * @brief Backend handle for a vertex buffer.
     *
     * Each SetData() recreates the underlying immutable sokol_gfx buffer. sokol_gfx allows only
     * one sg_update_buffer() per buffer per frame, whereas immutable creation is unrestricted, so
     * recreation is what makes repeated uploads within a frame legal here.
     */
    class SokolVertexBufferBackend : public IVertexBufferBackend
    {
    public:
        /**
         * @brief Creates an empty vertex buffer handle with the given capacity hint.
         * @param vertexCapacity Number of vertices the owning VertexBuffer was created for.
         */
        explicit SokolVertexBufferBackend(int vertexCapacity);

        /** @brief Destroys the sokol_gfx buffer owned by this vertex buffer. */
        ~SokolVertexBufferBackend() override;

        SokolVertexBufferBackend(const SokolVertexBufferBackend&) = delete;
        SokolVertexBufferBackend& operator=(const SokolVertexBufferBackend&) = delete;

        /**
         * @brief Uploads vertex data, replacing the whole buffer.
         *
         * @param data          Packed vertex data.
         * @param vertexCount   Number of vertices.
         * @param strideInBytes Size of one vertex in bytes.
         */
        void SetData(const void* data, int vertexCount, std::size_t strideInBytes) override;

        /**
         * @brief Records the caller's full vertex declaration.
         *
         * The colored-3D draw path keys its pipeline on this declaration's real stride and element
         * offsets, so a genuinely custom layout is bound correctly rather than being matched
         * against a fixed set of recognised byte strides.
         *
         * @param vertexDeclaration Full declaration, including stride and elements.
         */
        void SetVertexDeclaration(const VertexDeclaration& vertexDeclaration) override;

        /**
         * @brief Returns the number of vertices uploaded by the last SetData().
         * @return Vertex count, or 0 if nothing has been uploaded.
         */
        [[nodiscard]] int GetVertexCount() const override;

        /**
         * @brief Returns the byte stride of the last uploaded vertex data. NOXNA.
         * @return Stride in bytes, or 0 if nothing has been uploaded.
         */
        NOXNA [[nodiscard]] std::size_t GetStrideEXT() const { return stride_; }

        /**
         * @brief Returns the vertex declaration recorded by SetVertexDeclaration(). NOXNA.
         * @return The declaration, or null when the owning VertexBuffer supplied none.
         */
        NOXNA [[nodiscard]] const VertexDeclaration* GetDeclarationEXT() const
        {
            return hasDeclaration_ ? &declaration_ : nullptr;
        }

        /**
         * @brief Returns the raw sokol_gfx buffer handle id. NOXNA.
         * @return sg_buffer id, or 0 when no data has been uploaded.
         */
        NOXNA [[nodiscard]] std::uint32_t GetBufferIdEXT() const { return bufferId_; }

    private:
        int capacity_ = 0;
        int vertexCount_ = 0;
        std::size_t stride_ = 0;
        std::uint32_t bufferId_ = 0;
        bool hasDeclaration_ = false;
        VertexDeclaration declaration_;
    };

    /**
     * @brief Backend handle for a 16- or 32-bit index buffer.
     *
     * Recreates its immutable sokol_gfx buffer on every upload, for the same per-frame update
     * restriction described on SokolVertexBufferBackend.
     */
    class SokolIndexBufferBackend : public IIndexBufferBackend
    {
    public:
        /**
         * @brief Creates an empty index buffer handle with the given capacity hint.
         * @param indexCapacity  Number of indices the owning IndexBuffer was created for.
         * @param thirtyTwoBit   True when the owning IndexBuffer uses 32-bit indices.
         */
        SokolIndexBufferBackend(int indexCapacity, bool thirtyTwoBit);

        /** @brief Destroys the sokol_gfx buffer owned by this index buffer. */
        ~SokolIndexBufferBackend() override;

        SokolIndexBufferBackend(const SokolIndexBufferBackend&) = delete;
        SokolIndexBufferBackend& operator=(const SokolIndexBufferBackend&) = delete;

        /**
         * @brief Uploads 16-bit index data, replacing the whole buffer.
         * @param data       Packed 16-bit indices.
         * @param indexCount Number of indices.
         */
        void SetData16(const void* data, int indexCount) override;

        /**
         * @brief Uploads 32-bit index data, replacing the whole buffer.
         * @param data       Packed 32-bit indices.
         * @param indexCount Number of indices.
         */
        void SetData32(const void* data, int indexCount) override;

        /**
         * @brief Returns the number of indices uploaded by the last SetData16()/SetData32().
         * @return Index count, or 0 if nothing has been uploaded.
         */
        [[nodiscard]] int GetIndexCount() const override;

        /**
         * @brief Returns whether this buffer holds 32-bit indices.
         * @return True for 32-bit indices, false for 16-bit.
         */
        [[nodiscard]] bool IsThirtyTwoBit() const override;

        /**
         * @brief Returns the raw sokol_gfx buffer handle id. NOXNA.
         * @return sg_buffer id, or 0 when no data has been uploaded.
         */
        NOXNA [[nodiscard]] std::uint32_t GetBufferIdEXT() const { return bufferId_; }

    private:
        void Upload(const void* data, int indexCount, std::size_t indexSize);

        int capacity_ = 0;
        int indexCount_ = 0;
        bool thirtyTwoBit_ = false;
        std::uint32_t bufferId_ = 0;
    };

    /**
     * @brief SpriteBatch implementation drawing through a single streamed sokol_gfx vertex buffer.
     *
     * Quads are accumulated between Begin() and End() and flushed whenever the source texture
     * changes. Each flush appends its vertices to a per-frame streaming buffer (sg_append_buffer)
     * and draws them against a pre-built, immutable quad index buffer, so an arbitrary number of
     * flushes per frame is legal despite sokol_gfx's one-update-per-buffer-per-frame rule.
     */
    class SokolSpriteBatchBackend : public ISpriteBatchBackend
    {
    public:
        /** @brief One sprite-batch vertex: position, texture coordinate, and premultiplied tint. */
        struct Vertex
        {
            /** @brief X position, in XNA top-left-origin pixel space. */
            float x;
            /** @brief Y position, in XNA top-left-origin pixel space. */
            float y;
            /** @brief U texture coordinate. */
            float u;
            /** @brief V texture coordinate. */
            float v;
            /** @brief Red tint, 0..1. */
            float r;
            /** @brief Green tint, 0..1. */
            float g;
            /** @brief Blue tint, 0..1. */
            float b;
            /** @brief Alpha tint, 0..1. */
            float a;
        };

        /**
         * @brief Creates a sprite batch bound to the given backend.
         * @param backend Owning graphics backend; supplies the shared pipeline/sampler caches.
         */
        explicit SokolSpriteBatchBackend(SokolGraphicsBackend& backend);

        /** @brief Destroys this sprite batch. Shared GPU resources stay owned by the backend. */
        ~SokolSpriteBatchBackend() override;

        SokolSpriteBatchBackend(const SokolSpriteBatchBackend&) = delete;
        SokolSpriteBatchBackend& operator=(const SokolSpriteBatchBackend&) = delete;

        /** @brief Begins a batch; Draw() is only legal between Begin() and End(). */
        void Begin() override;

        /** @brief Ends the batch, flushing any sprites still pending. */
        void End() override;

        /**
         * @brief Sets the transform applied on top of the 2D orthographic projection.
         * @param m Transform matrix, as passed to SpriteBatch::Begin.
         */
        void SetTransformMatrix(const Matrix& m) override;

        /**
         * @brief Sets the texture filter applied to every Draw() in this batch.
         * @param textureFilter Raw Microsoft::Xna::Framework::Graphics::TextureFilter value.
         */
        void SetSamplerFilter(int textureFilter) override;

        /**
         * @brief Sets the texture address mode applied to every Draw() in this batch.
         * @param addressU Raw TextureAddressMode value for U.
         * @param addressV Raw TextureAddressMode value for V.
         */
        void SetSamplerAddressMode(int addressU, int addressV) override;

        /**
         * @brief Draws a whole texture at the given position, untinted.
         * @param texture Source texture.
         * @param x       Destination left edge, in pixels.
         * @param y       Destination top edge, in pixels.
         */
        void Draw(const ITextureBackend& texture, float x, float y) override;

        /**
         * @brief Draws a source region of a texture into a destination rectangle.
         * @param texture              Source texture.
         * @param destinationRectangle Destination rectangle, in pixels.
         * @param sourceRectangle      Source region, in texels.
         * @param color                Tint colour.
         */
        void Draw(const ITextureBackend& texture,
                  const Rectangle& destinationRectangle,
                  const Rectangle& sourceRectangle,
                  const Color& color) override;

        /**
         * @brief Draws a source region with rotation, origin, flipping, and layer depth.
         * @param texture              Source texture.
         * @param destinationRectangle Destination rectangle, in pixels.
         * @param sourceRectangle      Source region, in texels.
         * @param color                Tint colour.
         * @param rotation             Rotation in radians, about @p origin.
         * @param origin               Rotation/scaling origin, in source-texture pixels.
         * @param effects              Horizontal/vertical flip flags.
         * @param layerDepth           Sort depth; ordering is resolved by the shared SpriteBatch.
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
        void FlushBatch();

        SokolGraphicsBackend& backend_;
        bool begun_ = false;
        std::vector<Vertex> pendingVertices_;
        const ITextureBackend* currentTexture_ = nullptr;
        Matrix transform_ = Matrix::getIdentityProperty();
        int pendingFilter_ = 0;   // TextureFilter::Linear
        int pendingAddressU_ = 1; // TextureAddressMode::Clamp
        int pendingAddressV_ = 1; // TextureAddressMode::Clamp
    };

    /**
     * @brief CNA graphics backend implemented on sokol_gfx (https://github.com/floooh/sokol).
     *
     * CNA keeps ownership of the SDL window and the game loop; this class creates only the GPU
     * context (SDL_GL_CreateContext for the GL APIs) and drives sokol_gfx inside it, so sokol_app
     * is deliberately not used.
     *
     * Scope: this is the backend's 2D baseline -- clear/present, Texture2D, vertex/index buffers,
     * and a real SpriteBatch. The 3D draw path, render targets, cube/volume textures, custom
     * effects and occlusion queries are not implemented and fail loudly rather than silently
     * no-opping. See plan_sokol.md and docs/sokol-backend.md for the current capability boundary.
     */
    class SokolGraphicsBackend : public IGraphicsBackend
    {
    public:
        /**
         * @brief Creates the GPU context for @p window and initialises sokol_gfx in it.
         *
         * @param args Backend creation arguments; window, virtual resolution, presentation mode,
         *             multisample count and swap interval are honoured.
         */
        explicit SokolGraphicsBackend(const GraphicsBackendCreateArgs& args);

        /** @brief Shuts sokol_gfx down and destroys the GPU context. */
        ~SokolGraphicsBackend() override;

        SokolGraphicsBackend(const SokolGraphicsBackend&) = delete;
        SokolGraphicsBackend& operator=(const SokolGraphicsBackend&) = delete;

        /**
         * @brief Clears the colour buffer.
         * @param r,g,b,a Clear colour, 0..1.
         */
        void Clear(float r, float g, float b, float a) override;

        /**
         * @brief Clears colour and depth.
         * @param r,g,b,a Clear colour, 0..1.
         * @param depth   Depth value to clear with, 0..1.
         */
        void ClearColorAndDepth(float r, float g, float b, float a, float depth) override;

        /**
         * @brief Clears the depth buffer only.
         * @param depth Depth value to clear with, 0..1.
         */
        void ClearDepth(float depth) override;

        /**
         * @brief Clears the stencil buffer only.
         * @param stencil Stencil value to clear with.
         */
        void ClearStencil(int stencil) override;

        /**
         * @brief Clears depth and stencil.
         * @param depth   Depth value to clear with, 0..1.
         * @param stencil Stencil value to clear with.
         */
        void ClearDepthAndStencil(float depth, int stencil) override;

        /**
         * @brief Clears colour and stencil.
         * @param r,g,b,a Clear colour, 0..1.
         * @param stencil Stencil value to clear with.
         */
        void ClearColorAndStencil(float r, float g, float b, float a, int stencil) override;

        /**
         * @brief Clears colour, depth and stencil.
         * @param r,g,b,a Clear colour, 0..1.
         * @param depth   Depth value to clear with, 0..1.
         * @param stencil Stencil value to clear with.
         */
        void ClearColorDepthAndStencil(float r, float g, float b, float a,
                                       float depth, int stencil) override;

        /** @brief Ends the frame's render pass, commits it, and swaps the window's buffers. */
        void Present() override;

        /**
         * @brief Returns the logical (virtual) viewport size games draw in.
         * @param width  Receives the logical width in pixels.
         * @param height Receives the logical height in pixels.
         */
        void GetViewportSize(int& width, int& height) override;

        /**
         * @brief Updates the logical presentation size at runtime.
         * @param width  New logical width in pixels.
         * @param height New logical height in pixels.
         */
        void SetVirtualResolution(int width, int height) override;

        /**
         * @brief Updates the presentation/scaling policy at runtime.
         * @param mode Raw CnaPresentationMode value.
         */
        void SetPresentationMode(int mode) override;

        /**
         * @brief Updates the swap interval at runtime.
         * @param interval 0 = immediate, 1 = VSync, 2 = half refresh rate.
         */
        void SetSwapInterval(int interval) override;

        /**
         * @brief Converts a point from physical window to logical game coordinates.
         * @param windowX Window-space X.
         * @param windowY Window-space Y.
         * @param logX    Receives the logical X.
         * @param logY    Receives the logical Y.
         * @return True when a virtual resolution is configured, false otherwise.
         */
        bool TransformWindowToLogical(float windowX, float windowY,
                                      float& logX, float& logY) const override;

        /**
         * @brief Converts a point from logical game to physical window coordinates.
         * @param logX    Logical X.
         * @param logY    Logical Y.
         * @param windowX Receives the window-space X.
         * @param windowY Receives the window-space Y.
         * @return True when a virtual resolution is configured, false otherwise.
         */
        bool TransformLogicalToWindow(float logX, float logY,
                                      float& windowX, float& windowY) const override;

        /**
         * @brief Returns the SDL window this backend renders into.
         * @return The window; never null for a successfully constructed backend.
         */
        [[nodiscard]] SDL_Window* GetWindowInternal() const override;

        /**
         * @brief Returns null; this backend does not use SDL_Renderer.
         * @return Always nullptr.
         */
        [[nodiscard]] SDL_Renderer* GetRendererInternal() const override;

        /**
         * @brief Creates a sokol_gfx-backed 2D texture.
         * @param data Decoded RGBA8 source image.
         * @return The new texture backend.
         */
        std::unique_ptr<ITextureBackend> CreateTexture(const ImageData& data) override;

        /**
         * @brief Creates a sokol_gfx-backed SpriteBatch.
         * @return The new sprite batch backend.
         */
        std::unique_ptr<ISpriteBatchBackend> CreateSpriteBatch() override;

        /**
         * @brief Creates a sokol_gfx-backed vertex buffer.
         * @param vertexCapacity Number of vertices to size the buffer for.
         * @return The new vertex buffer backend.
         */
        std::unique_ptr<IVertexBufferBackend> CreateVertexBuffer(int vertexCapacity) override;

        /**
         * @brief Creates a sokol_gfx-backed 16-bit index buffer.
         * @param indexCapacity Number of indices to size the buffer for.
         * @return The new index buffer backend.
         */
        std::unique_ptr<IIndexBufferBackend> CreateIndexBuffer16(int indexCapacity) override;

        /**
         * @brief Creates a sokol_gfx-backed 32-bit index buffer.
         * @param indexCapacity Number of indices to size the buffer for.
         * @return The new index buffer backend.
         */
        std::unique_ptr<IIndexBufferBackend> CreateIndexBuffer32(int indexCapacity) override;

        /**
         * @brief Reads rendered back-buffer pixels back into @p pixels as RGBA8.
         * @param x      Left edge of the region, in game coordinates.
         * @param y      Top edge of the region, in game coordinates.
         * @param w      Region width in pixels.
         * @param h      Region height in pixels.
         * @param pixels Destination for w * h * 4 bytes, top row first.
         */
        void ReadBackbuffer(int x, int y, int w, int h, uint8_t* pixels) override;

        /**
         * @brief Binds a render-target set. Only the back buffer (null / count 0) is supported.
         * @param renderTargets Ordered attachment descriptors, or null for the back buffer.
         * @param count         Number of attachments; 0 restores the back buffer.
         */
        void SetRenderTargets(const RenderTargetBindingDescriptor* renderTargets,
                              int count) override;

        /**
         * @brief Applies a BlendState to every subsequent draw.
         * @param colorSrcBlend  Raw Blend value for the colour source factor.
         * @param alphaSrcBlend  Raw Blend value for the alpha source factor.
         * @param colorDstBlend  Raw Blend value for the colour destination factor.
         * @param alphaDstBlend  Raw Blend value for the alpha destination factor.
         * @param colorBlendFunc Raw BlendFunction value for the colour equation.
         * @param alphaBlendFunc Raw BlendFunction value for the alpha equation.
         * @param writeState     Per-slot colour write masks and the coverage sample mask.
         */
        void ApplyBlendState(int colorSrcBlend, int alphaSrcBlend,
                             int colorDstBlend, int alphaDstBlend,
                             int colorBlendFunc, int alphaBlendFunc,
                             const BlendWriteState& writeState) override;

        /**
         * @brief Applies a DepthStencilState to every subsequent draw.
         * @param depthEnable         Whether depth testing is enabled.
         * @param depthWriteEnable    Whether depth writes are enabled.
         * @param depthFunc           Raw CompareFunction value for the depth test.
         * @param stencilEnable       Whether stencil testing is enabled.
         * @param stencilFunc         Raw CompareFunction value for the front-face stencil test.
         * @param stencilPass         Raw StencilOperation value for a passing front-face test.
         * @param stencilFail         Raw StencilOperation value for a failing front-face test.
         * @param stencilDepthFail    Raw StencilOperation value for a front-face depth failure.
         * @param stencilMask         Stencil read mask.
         * @param stencilWriteMask    Stencil write mask.
         * @param referenceStencil    Stencil reference value.
         * @param twoSidedStencilMode Whether back faces use their own stencil operations.
         * @param ccwStencilFunc      Raw CompareFunction value for the back-face stencil test.
         * @param ccwStencilPass      Raw StencilOperation value for a passing back-face test.
         * @param ccwStencilFail      Raw StencilOperation value for a failing back-face test.
         * @param ccwStencilDepthFail Raw StencilOperation value for a back-face depth failure.
         */
        void ApplyDepthStencilState(bool depthEnable, bool depthWriteEnable,
                                    int depthFunc,
                                    bool stencilEnable, int stencilFunc,
                                    int stencilPass, int stencilFail, int stencilDepthFail,
                                    int stencilMask, int stencilWriteMask, int referenceStencil,
                                    bool twoSidedStencilMode,
                                    int ccwStencilFunc, int ccwStencilPass,
                                    int ccwStencilFail, int ccwStencilDepthFail) override;

        /**
         * @brief Applies a RasterizerState to every subsequent draw.
         * @param cullMode            Raw CullMode value.
         * @param fillMode            Raw FillMode value.
         * @param scissorTestEnable   Whether the scissor test is enabled.
         * @param depthBias           Constant depth bias.
         * @param slopeScaleDepthBias Slope-scaled depth bias.
         */
        void ApplyRasterizerState(int cullMode, int fillMode,
                                  bool scissorTestEnable,
                                  float depthBias,
                                  float slopeScaleDepthBias) override;

        /**
         * @brief Applies a SamplerState to a texture slot.
         * @param slot          Texture unit index.
         * @param filter        Raw TextureFilter value.
         * @param addressU      Raw TextureAddressMode value for U.
         * @param addressV      Raw TextureAddressMode value for V.
         * @param maxAnisotropy Maximum anisotropy, 1..16.
         */
        void ApplySamplerState(int slot, int filter, int addressU, int addressV,
                               int maxAnisotropy) override;

        /**
         * @brief Sets the constant blend colour used by the BlendFactor blend modes.
         * @param r,g,b,a Blend factor colour, 0..1.
         */
        void SetBlendFactor(float r, float g, float b, float a) override;

        /**
         * @brief Sets the stencil reference value used by subsequent draws.
         * @param value Stencil reference value.
         */
        void SetReferenceStencil(int value) override;

        /**
         * @brief Sets the scissor clip rectangle, in logical game coordinates.
         * @param x Left edge.
         * @param y Top edge.
         * @param w Width.
         * @param h Height.
         */
        void SetScissorRect(int x, int y, int w, int h) override;

        /**
         * @brief Sets the viewport rectangle and depth range, in logical game coordinates.
         * @param x        Left edge.
         * @param y        Top edge.
         * @param w        Width.
         * @param h        Height.
         * @param minDepth Near depth-range bound.
         * @param maxDepth Far depth-range bound.
         */
        void SetViewport(int x, int y, int w, int h, float minDepth, float maxDepth) override;

        /**
         * @brief Enables or disables depth testing for subsequent draws.
         * @param enabled Whether the depth test is enabled.
         */
        void SetDepthTestEnabled(bool enabled) override;

        /**
         * @brief Enables or disables blending for subsequent draws.
         * @param enabled Whether blending is enabled.
         */
        void SetBlendEnabled(bool enabled) override;

        /**
         * @brief Enables or disables depth writes for subsequent draws.
         * @param enabled Whether depth writes are enabled.
         */
        void SetDepthWriteEnabled(bool enabled) override;

        /**
         * @brief Draws vertex-coloured primitives with the built-in colored-3D program.
         * @param vb             Vertex buffer to read from.
         * @param world          World matrix.
         * @param view           View matrix.
         * @param projection     Projection matrix.
         * @param primitive      Primitive topology.
         * @param primitiveCount Number of primitives.
         */
        void DrawColoredPrimitives(const IVertexBufferBackend& vb,
                                   const Matrix& world,
                                   const Matrix& view,
                                   const Matrix& projection,
                                   PrimitiveType primitive,
                                   int primitiveCount) override;

        /**
         * @brief Indexed counterpart of DrawColoredPrimitives().
         * @param vb             Vertex buffer to read from.
         * @param ib             Index buffer to read from.
         * @param world          World matrix.
         * @param view           View matrix.
         * @param projection     Projection matrix.
         * @param primitive      Primitive topology.
         * @param primitiveCount Number of primitives.
         */
        void DrawIndexedColoredPrimitives(const IVertexBufferBackend& vb,
                                          const IIndexBufferBackend& ib,
                                          const Matrix& world,
                                          const Matrix& view,
                                          const Matrix& projection,
                                          PrimitiveType primitive,
                                          int primitiveCount) override;

        /**
         * @brief Effect-aware non-indexed draw.
         *
         * Only the untextured, unlit path is implemented; any effect requesting texturing,
         * lighting, dual texturing, environment mapping, skinning or PBR throws rather than
         * quietly rendering an unshaded approximation of it.
         *
         * @param vb             Vertex buffer to read from.
         * @param world          World matrix.
         * @param view           View matrix.
         * @param projection     Projection matrix.
         * @param primitive      Primitive topology.
         * @param primitiveCount Number of primitives.
         * @param params         Per-draw effect parameters.
         */
        void DrawPrimitivesEx(const IVertexBufferBackend& vb,
                              const Matrix& world,
                              const Matrix& view,
                              const Matrix& projection,
                              PrimitiveType primitive,
                              int primitiveCount,
                              const GpuDrawParams& params) override;

        /**
         * @brief Indexed counterpart of DrawPrimitivesEx(); same capability boundary.
         * @param vb             Vertex buffer to read from.
         * @param ib             Index buffer to read from.
         * @param world          World matrix.
         * @param view           View matrix.
         * @param projection     Projection matrix.
         * @param primitive      Primitive topology.
         * @param primitiveCount Number of primitives.
         * @param params         Per-draw effect parameters.
         */
        void DrawIndexedPrimitivesEx(const IVertexBufferBackend& vb,
                                     const IIndexBufferBackend& ib,
                                     const Matrix& world,
                                     const Matrix& view,
                                     const Matrix& projection,
                                     PrimitiveType primitive,
                                     int primitiveCount,
                                     const GpuDrawParams& params) override;

        /**
         * @brief Reports which features this backend's current baseline actually supports.
         * @param capability Feature to query.
         * @return True when the feature is implemented and usable.
         */
        [[nodiscard]] bool SupportsCapability(CNA::GraphicsCapability capability) const override;

        /**
         * @brief Returns the back buffer's device-clamped MSAA sample count.
         * @return Sample count; 1 when multisampling is off.
         */
        [[nodiscard]] int GetMultiSampleCount() const override;

        /**
         * @brief Returns the native API sokol_gfx was compiled to dispatch onto. NOXNA.
         * @return The resolved SokolApiEXT value for this build.
         */
        NOXNA [[nodiscard]] static SokolApiEXT GetApiEXT();

        /**
         * @brief Returns the maximum number of sprite quads one frame may draw. NOXNA.
         *
         * The sprite streaming buffer is allocated once at construction, so a frame that exceeds
         * this cap raises an error rather than silently dropping sprites.
         *
         * @return Per-frame sprite quad capacity.
         */
        NOXNA [[nodiscard]] static int GetMaxSpriteQuadsPerFrameEXT();

        /**
         * @brief Draws one flushed run of sprite quads. NOXNA, called by SokolSpriteBatchBackend.
         *
         * @param texture  Source texture for the run.
         * @param vertices Quad vertices, four per quad in top-left, top-right, bottom-right,
         *                 bottom-left order.
         * @param transform Transform applied on top of the orthographic projection.
         * @param filter   Raw TextureFilter value.
         * @param addressU Raw TextureAddressMode value for U.
         * @param addressV Raw TextureAddressMode value for V.
         */
        NOXNA void DrawSpriteRunEXT(const ITextureBackend& texture,
                                    const std::vector<SokolSpriteBatchBackend::Vertex>& vertices,
                                    const Matrix& transform,
                                    int filter, int addressU, int addressV);

        /**
         * @brief Returns the logical (virtual) presentation size. NOXNA.
         * @param width  Receives the logical width in pixels.
         * @param height Receives the logical height in pixels.
         */
        NOXNA void GetLogicalSizeEXT(int& width, int& height) const;

        /**
         * @brief Returns the physical window size in pixels. NOXNA.
         * @param width  Receives the physical width.
         * @param height Receives the physical height.
         */
        NOXNA void GetPhysicalSizeEXT(int& width, int& height) const;

    private:
        struct PipelineKey
        {
            int colorSrcBlend;
            int alphaSrcBlend;
            int colorDstBlend;
            int alphaDstBlend;
            int colorBlendFunc;
            int alphaBlendFunc;
            int colorWriteChannels;
            bool blendEnabled;
            bool depthTestEnabled;
            bool depthWriteEnabled;
            int depthFunc;

            bool operator==(const PipelineKey& other) const;
        };

        struct PipelineKeyHash
        {
            std::size_t operator()(const PipelineKey& key) const;
        };

        /**
         * @brief Which of the three 3D shader programs a Pipeline3DKey targets.
         *
         * Each kind has its own attribute set (see Pipeline3DKey), so the pipeline object -- and
         * therefore the cache key -- must name which shader it was built against, not just the
         * vertex layout.
         */
        enum class Shader3DKind
        {
            /** @brief colored3d.glsl -- vertex colour only, no texture, no lighting. */
            Colored,
            /** @brief textured3d.glsl -- texture + vertex colour, no lighting. */
            Textured,
            /** @brief lit3d.glsl -- ambient + up to 3 directional lights, always samples a texture
             *  (a real one, or the backend's 1x1 white fallback). */
            Lit
        };

        /**
         * @brief Identity of a 3D pipeline: which shader, the shared render state, and the exact
         * vertex layout and topology it was built for.
         *
         * The layout has to be part of the key because sokol_gfx bakes it into the pipeline
         * object, unlike the sprite path where every draw shares one fixed vertex format.
         */
        struct Pipeline3DKey
        {
            Shader3DKind kind;
            int colorSrcBlend;
            int alphaSrcBlend;
            int colorDstBlend;
            int alphaDstBlend;
            int colorBlendFunc;
            int alphaBlendFunc;
            int colorWriteChannels;
            bool blendEnabled;
            bool depthTestEnabled;
            bool depthWriteEnabled;
            int depthFunc;
            int cullMode;
            int primitiveType;
            /// Raw sg_index_type: sokol_gfx bakes the index type into the pipeline, and rejects
            /// both an indexed pipeline used without an index buffer and a width mismatch, so
            /// non-indexed / 16-bit / 32-bit draws each need their own pipeline object.
            int indexType;
            int stride;
            int positionOffset;
            int positionFormat;
            /// Byte offset of the Color element, or -1 when the declaration has none.
            int colorOffset;
            int colorFormat;
            /// Byte offset of the TextureCoordinate element, or -1 when absent (Colored kind
            /// never reads this; Lit kind only needs it when a real texture is bound).
            int texCoordOffset;
            int texCoordFormat;
            /// Byte offset of the Normal element, or -1 when absent (only Lit kind reads this).
            int normalOffset;
            int normalFormat;

            bool operator==(const Pipeline3DKey& other) const;
        };

        struct Pipeline3DKeyHash
        {
            std::size_t operator()(const Pipeline3DKey& key) const;
        };

        struct SamplerKey
        {
            int filter;
            int addressU;
            int addressV;
            int maxAnisotropy;

            bool operator==(const SamplerKey& other) const;
        };

        struct SamplerKeyHash
        {
            std::size_t operator()(const SamplerKey& key) const;
        };

        void CreateGpuContext(SDL_Window* window, int multiSampleCount);
        void SetupSokol();
        void CreateSpriteResources();
        void BeginPassIfNeeded();
        void EndPassIfActive();
        void QueueClear(bool color, float r, float g, float b, float a,
                        bool depth, float depthValue,
                        bool stencil, int stencilValue);
        void DrawColored3D(const IVertexBufferBackend& vb,
                           const IIndexBufferBackend* ib,
                           const Matrix& world,
                           const Matrix& view,
                           const Matrix& projection,
                           PrimitiveType primitive,
                           int primitiveCount,
                           const GpuDrawParams& params);
        [[nodiscard]] std::uint32_t Get3DPipeline(const Pipeline3DKey& key);
        [[nodiscard]] SokolTextureBackend& GetDefaultWhiteTexture();
        [[nodiscard]] std::uint32_t GetSpritePipeline();
        [[nodiscard]] std::uint32_t GetSampler(int filter, int addressU, int addressV,
                                               int maxAnisotropy);
        void ApplyPendingViewportAndScissor();

        SDL_Window* window_ = nullptr;
        void* glContext_ = nullptr;
        int virtualWidth_ = 0;
        int virtualHeight_ = 0;
        CnaPresentationMode presentationMode_ = CnaPresentationMode::FixedHeightDynamicWidth;
        int sampleCount_ = 1;
        int swapInterval_ = 1;

        bool passActive_ = false;
        /// Pending pass action for the next BeginPassIfNeeded(), reset to "load" after each use so
        /// only an explicit Clear* call ever discards existing content.
        bool pendingClearColor_ = false;
        float pendingClear_[4] = {0.0f, 0.0f, 0.0f, 1.0f};
        bool pendingClearDepth_ = false;
        float pendingDepth_ = 1.0f;
        bool pendingClearStencil_ = false;
        int pendingStencil_ = 0;

        int blendColorSrc_ = 0;   // Blend::One
        int blendAlphaSrc_ = 0;   // Blend::One
        int blendColorDst_ = 1;   // Blend::Zero
        int blendAlphaDst_ = 1;   // Blend::Zero
        int blendColorFunc_ = 0;  // BlendFunction::Add
        int blendAlphaFunc_ = 0;  // BlendFunction::Add
        int colorWriteChannels_ = 15;
        bool blendEnabled_ = false;
        float blendFactor_[4] = {1.0f, 1.0f, 1.0f, 1.0f};

        bool depthTestEnabled_ = false;
        bool depthWriteEnabled_ = true;
        int depthFunc_ = 3;       // CompareFunction::LessEqual
        int referenceStencil_ = 0;

        bool scissorEnabled_ = false;
        int scissorRect_[4] = {0, 0, 0, 0};
        bool viewportSet_ = false;
        int viewportRect_[4] = {0, 0, 0, 0};

        int cullMode_ = 0;         // CullMode::None
        int fillMode_ = 0;         // FillMode::Solid

        /// Per-slot SamplerState, mirroring GraphicsDevice.SamplerStates -- read by the textured/
        /// lit 3D draw paths for texture unit 0. Defaults match XNA's own SamplerState default
        /// (equivalent to LinearWrap, MaxAnisotropy 4). SpriteBatch does not read this array: it
        /// receives its filter/address values directly from SpriteBatch.Begin's own SamplerState.
        struct SamplerSlotState
        {
            int filter = 0;         // TextureFilter::Linear
            int addressU = 0;       // TextureAddressMode::Wrap
            int addressV = 0;       // TextureAddressMode::Wrap
            int maxAnisotropy = 4;
        };
        static constexpr int kMaxSamplerSlots = 16;
        SamplerSlotState samplerSlots_[kMaxSamplerSlots];

        std::uint32_t spriteShaderId_ = 0;
        std::uint32_t spriteVertexBufferId_ = 0;
        std::uint32_t spriteIndexBufferId_ = 0;
        std::uint32_t colored3dShaderId_ = 0;
        std::uint32_t textured3dShaderId_ = 0;
        std::uint32_t lit3dShaderId_ = 0;
        /// Lazily created 1x1 opaque-white texture, bound by the Lit shader whenever
        /// GpuDrawParams::textureEnabled is false -- lets one shader serve both "textured and lit"
        /// and "vertex-coloured and lit" (the multiply is then a no-op), the same convention the
        /// EasyGL backend's own default-white-texture fallback uses.
        std::unique_ptr<SokolTextureBackend> defaultWhiteTexture_;
        std::unordered_map<PipelineKey, std::uint32_t, PipelineKeyHash> pipelineCache_;
        std::unordered_map<Pipeline3DKey, std::uint32_t, Pipeline3DKeyHash> pipeline3dCache_;
        std::unordered_map<SamplerKey, std::uint32_t, SamplerKeyHash> samplerCache_;
    };
}
