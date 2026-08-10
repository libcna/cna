// SPDX-License-Identifier: MS-PL
#pragma once

#include "CNA/CNAHelper.hpp"
#include "CNA/Internal/Renderers/Common/IGraphicsRenderer.hpp"

#include <cstdint>
#include <memory>

namespace CNA::Internal::Renderers::PortableGL
{
    /**
     * Renderer handle for a vertex buffer, backed by a real PortableGL (`rswinkle/PortableGL`)
     * GPU-shaped buffer object created through `glGenBuffers`/`glBufferData`. Only the 16-byte
     * `VertexPositionColor` layout (float3 position + 4-byte packed color) is understood by the
     * colored 3D draw path this v1 renderer implements; the declaration is otherwise accepted and
     * the raw stride is kept so `PortableGLRenderer::DrawColoredPrimitives` can bind it.
     */
    class PortableGLVertexBufferRenderer final : public IVertexBufferRenderer
    {
    public:
        /**
         * @param pglContext Opaque pointer to the owning renderer's real PortableGL `glContext`
         *                   (kept `void*` here so this public header never needs to include
         *                   `portablegl.h`; cast back to `glContext*` only inside the .cpp).
         * @param vertexCapacity Initial vertex capacity, matching `IGraphicsRenderer::CreateVertexBuffer`.
         */
        PortableGLVertexBufferRenderer(void* pglContext, int vertexCapacity);
        ~PortableGLVertexBufferRenderer() override;

        void SetData(const void* data, int vertex_count, std::size_t stride_in_bytes) override;
        void SetVertexDeclaration(const VertexDeclaration& /*vertexDeclaration*/) override {}
        [[nodiscard]] int GetVertexCount() const override { return vertexCount_; }

        /** @brief Real PortableGL buffer object handle (`glGenBuffers` id), 0 if never uploaded. */
        [[nodiscard]] unsigned int GLBufferHandle() const { return glBuffer_; }
        /** @brief Stride in bytes of the most recent `SetData()` upload. */
        [[nodiscard]] std::size_t StrideInBytes() const { return stride_; }

    private:
        void* pglContext_;
        unsigned int glBuffer_ = 0;
        int vertexCount_ = 0;
        std::size_t stride_ = 0;
    };

    /** @brief Renderer handle for a 16- or 32-bit index buffer, backed by a real PortableGL buffer object. */
    class PortableGLIndexBufferRenderer final : public IIndexBufferRenderer
    {
    public:
        /** @param pglContext See `PortableGLVertexBufferRenderer`'s identical parameter. */
        PortableGLIndexBufferRenderer(void* pglContext, int indexCapacity);
        ~PortableGLIndexBufferRenderer() override;

        void SetData16(const void* data, int index_count) override;
        void SetData32(const void* data, int index_count) override;
        [[nodiscard]] int GetIndexCount() const override { return indexCount_; }
        [[nodiscard]] bool IsThirtyTwoBit() const override { return thirtyTwoBit_; }

        /** @brief Real PortableGL buffer object handle (`glGenBuffers` id), 0 if never uploaded. */
        [[nodiscard]] unsigned int GLBufferHandle() const { return glBuffer_; }

    private:
        void* pglContext_;
        unsigned int glBuffer_ = 0;
        int indexCount_ = 0;
        bool thirtyTwoBit_ = false;
    };

    /**
     * Renderer handle for a texture, backed by a real PortableGL texture object created through
     * `glGenTextures`/`glTexImage2D`. There is no native SDL texture behind this handle -- this
     * renderer is CPU-only, so `GetNativeTexture()` always returns null, matching the Software
     * renderer's own texture handle shape.
     */
    class PortableGLTextureRenderer final : public ITextureRenderer
    {
    public:
        /** @param pglContext See `PortableGLVertexBufferRenderer`'s identical parameter. */
        PortableGLTextureRenderer(void* pglContext, const ImageData& data);
        ~PortableGLTextureRenderer() override;

        [[nodiscard]] int GetWidth() const override { return width_; }
        [[nodiscard]] int GetHeight() const override { return height_; }
        [[nodiscard]] SDL_Texture* GetNativeTexture() const override { return nullptr; }

        /** @brief Real PortableGL texture object handle (`glGenTextures` id). */
        [[nodiscard]] unsigned int GLTextureHandle() const { return glTexture_; }

    private:
        void* pglContext_;
        unsigned int glTexture_ = 0;
        int width_ = 0;
        int height_ = 0;
    };

    class PortableGLRenderer;

    /**
     * SpriteBatch handle for the PortableGL renderer. Every `Draw()` call is submitted immediately
     * as a real textured-quad draw through PortableGL's own program/draw-call machinery (a
     * dedicated textured vertex/fragment shader pair sampling the bound texture via PortableGL's
     * `texture2D()`), not a CPU blit.
     */
    class PortableGLSpriteBatchRenderer final : public ISpriteBatchRenderer
    {
    public:
        explicit PortableGLSpriteBatchRenderer(PortableGLRenderer& owner);

        void Begin() override { begun_ = true; }
        void End() override { begun_ = false; }
        void SetTransformMatrix(const Matrix& m) override { transform_ = m; }

        void Draw(const ITextureRenderer& texture, float x, float y) override;
        void Draw(const ITextureRenderer& texture,
                  const Rectangle& destinationRectangle,
                  const Rectangle& sourceRectangle,
                  const Color& color) override;
        void Draw(const ITextureRenderer& texture,
                  const Rectangle& destinationRectangle,
                  const Rectangle& sourceRectangle,
                  const Color& color,
                  float rotation,
                  const Vector2& origin,
                  SpriteEffects effects,
                  float layerDepth) override;

        /** @brief CNAEXT. Whether a `Begin()` is currently active without a matching `End()`. */
        CNAEXT [[nodiscard]] bool IsBegunEXT() const { return begun_; }

    private:
        PortableGLRenderer& owner_;
        Matrix transform_ = Matrix::getIdentityProperty();
        bool begun_ = false;
    };

    /**
     * PortableGL graphics renderer -- a genuine CPU software OpenGL 3.x-ish renderer built on
     * `rswinkle/PortableGL` (https://github.com/rswinkle/PortableGL, MIT licensed, single C99
     * header). Same architectural shape as the Headless/Software renderers -- no window, no GPU
     * library, `Present()` is a no-op, `GetWindowInternal()`/`GetRendererInternal()` return null,
     * and pixel truth is exposed via `ReadBackbuffer()` -- but unlike Software's hand-rolled
     * rasterizer, the actual rasterization/shading pipeline is delegated to real PortableGL API
     * calls (`glGenBuffers`/`glBufferData`/`glVertexAttribPointer`, `pglCreateProgram` with real C
     * vertex+fragment shader callbacks, `glDrawArrays`/`glDrawElements`, `glClear`, `glViewport`,
     * `glTexImage2D`).
     *
     * v1 scope: vertex-colored 3D primitives (`VertexPositionColor`, stride 16) and a textured
     * SpriteBatch path. Render targets, cube/3D textures, occlusion queries and custom Effects are
     * not implemented -- `SupportsCapability()` answers each of those truthfully as unsupported
     * rather than silently no-opping.
     */
    class PortableGLRenderer final : public IGraphicsRenderer
    {
    public:
        PortableGLRenderer(int virtualWidth, int virtualHeight);
        ~PortableGLRenderer() override;

        void Clear(float r, float g, float b, float a) override;
        void Present() override {}
        void GetViewportSize(int& width, int& height) override;
        void SetVirtualResolution(int width, int height) override;
        void SetPresentationMode(int /*mode*/) override {}

        void ReadBackbuffer(int x, int y, int w, int h, uint8_t* pixels) override;

        [[nodiscard]] SDL_Window* GetWindowInternal() const override { return nullptr; }
        [[nodiscard]] SDL_Renderer* GetRendererInternal() const override { return nullptr; }

        std::unique_ptr<ITextureRenderer> CreateTexture(const ImageData& data) override;
        std::unique_ptr<ISpriteBatchRenderer> CreateSpriteBatch() override;

        /// PortableGL creates no render targets in v1 (see class doc); the binding set is
        /// deliberately discarded rather than silently flattening a cube face, matching Stub's own
        /// documented rationale for this same required override.
        void SetRenderTargets(const RenderTargetBindingDescriptor* /*renderTargets*/,
                              int /*count*/) override {}

        void ClearColorAndDepth(float r, float g, float b, float a, float depth) override;
        void ClearDepth(float depth) override;
        void ClearStencil(int stencil) override;
        void ClearDepthAndStencil(float depth, int stencil) override;
        void ClearColorAndStencil(float r, float g, float b, float a, int stencil) override;
        void ClearColorDepthAndStencil(float r, float g, float b, float a, float depth, int stencil) override;

        void SetDepthTestEnabled(bool enabled) override;
        void SetBlendEnabled(bool enabled) override;
        void SetDepthWriteEnabled(bool enabled) override;

        std::unique_ptr<IVertexBufferRenderer> CreateVertexBuffer(int vertex_capacity) override;
        std::unique_ptr<IIndexBufferRenderer> CreateIndexBuffer16(int index_capacity) override;

        void DrawColoredPrimitives(const IVertexBufferRenderer& vb,
                                   const Matrix& world,
                                   const Matrix& view,
                                   const Matrix& projection,
                                   PrimitiveType primitive,
                                   int primitiveCount) override;

        void DrawIndexedColoredPrimitives(const IVertexBufferRenderer& vb,
                                          const IIndexBufferRenderer& ib,
                                          const Matrix& world,
                                          const Matrix& view,
                                          const Matrix& projection,
                                          PrimitiveType primitive,
                                          int primitiveCount) override;

        [[nodiscard]] bool SupportsCapability(CNA::GraphicsCapability capability) const override;

        /**
         * CNAEXT. Internal helper used by `PortableGLSpriteBatchRenderer` to issue one real
         * textured-quad draw through the same PGL program/draw-call machinery
         * `DrawColoredPrimitives` uses -- not part of `IGraphicsRenderer`.
         *
         * @param glTexture       PortableGL texture handle to sample.
         * @param positionsPixels Four corner positions (TL, TR, BL, BR), in destination pixel space.
         * @param uvs             Four corresponding normalized texture coordinates.
         * @param colorsRgba01    Four corresponding per-vertex tint colors, components in [0,1].
         * @param spriteTransform `ISpriteBatchRenderer::SetTransformMatrix()`'s current value,
         *                        composed with the internal ortho projection exactly as
         *                        `EasyGLRenderer`'s own SpriteBatch does (`transform * ortho`).
         */
        CNAEXT void DrawTexturedQuadEXT(unsigned int glTexture,
                                        const float positionsPixels[4][2],
                                        const float uvs[4][2],
                                        const float colorsRgba01[4][4],
                                        const Matrix& spriteTransform);

    private:
        struct Impl;
        std::unique_ptr<Impl> impl_;
        int virtualWidth_;
        int virtualHeight_;
    };
}
