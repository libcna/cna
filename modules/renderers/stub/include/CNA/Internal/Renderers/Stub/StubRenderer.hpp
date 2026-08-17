#pragma once

#include "CNA/Internal/Renderers/Common/IGraphicsRenderer.hpp"

namespace CNA::Internal::Renderers::Stub
{
    /// Vertex-buffer handle for the Stub renderer. Stores only the vertex count it was given --
    /// no backing storage of any kind (plan_stub.md design decision 3).
    class StubVertexBufferRenderer : public IVertexBufferRenderer
    {
    public:
        explicit StubVertexBufferRenderer(int vertexCapacity) : vertexCount_(vertexCapacity) {}

        void SetData(const void* data, int vertex_count, std::size_t stride_in_bytes) override
        {
            vertexCount_ = vertex_count;
        }

        /// The declaration is deliberately discarded, which is the explicit decision
        /// IVertexBufferRenderer requires rather than a default it could inherit silently. Stub
        /// keeps no vertex storage and binds no native layout, so it holds nothing a declaration
        /// could describe unfaithfully -- the draw-time declaration-fidelity guard that renderers
        /// with a real layout must apply has no subject here. Same shape as Headless, which
        /// rasterizes nothing either.
        void SetVertexDeclaration(const VertexDeclaration& vertexDeclaration) override {}

        [[nodiscard]] int GetVertexCount() const override { return vertexCount_; }

    private:
        int vertexCount_;
    };

    /// Index-buffer handle for the Stub renderer. Stores only the index count it was given -- same
    /// no-backing-storage shape as StubVertexBufferRenderer.
    class StubIndexBufferRenderer : public IIndexBufferRenderer
    {
    public:
        StubIndexBufferRenderer(int indexCapacity, bool isThirtyTwoBit)
            : indexCount_(indexCapacity), isThirtyTwoBit_(isThirtyTwoBit) {}

        void SetData16(const void* data, int index_count) override
        {
            if (isThirtyTwoBit_)
                throw std::runtime_error("StubIndexBufferRenderer: SetData16 on a 32-bit buffer");
            indexCount_ = index_count;
        }

        void SetData32(const void* data, int index_count) override
        {
            if (!isThirtyTwoBit_)
                throw std::runtime_error("StubIndexBufferRenderer: SetData32 on a 16-bit buffer");
            indexCount_ = index_count;
        }

        [[nodiscard]] int GetIndexCount() const override { return indexCount_; }
        [[nodiscard]] bool IsThirtyTwoBit() const override { return isThirtyTwoBit_; }

    private:
        int indexCount_;
        bool isThirtyTwoBit_ = false;
    };

    /// Texture handle for the Stub renderer. Stores only width/height -- no pixel data of any kind;
    /// SetData()/GetData() are accepted (inherited no-op defaults) and simply discarded.
    class StubTextureRenderer : public ITextureRenderer
    {
    public:
        StubTextureRenderer(int width, int height) : width_(width), height_(height) {}

        [[nodiscard]] int GetWidth() const override { return width_; }
        [[nodiscard]] int GetHeight() const override { return height_; }

    private:
        int width_;
        int height_;
    };

    /// SpriteBatch handle for the Stub renderer. Every Draw()/Begin()/End() call is a no-op.
    class StubSpriteBatchRenderer : public ISpriteBatchRenderer
    {
    public:
        void Begin() override {}
        void End() override {}

        void Draw(const ITextureRenderer& texture, float x, float y) override {}

        void Draw(const ITextureRenderer& texture,
                  const Rectangle& destinationRectangle,
                  const Rectangle& sourceRectangle,
                  const Color& color) override
        {
        }

        void Draw(const ITextureRenderer& texture,
                  const Rectangle& destinationRectangle,
                  const Rectangle& sourceRectangle,
                  const Color& color,
                  float rotation,
                  const Vector2& origin,
                  SpriteEffects effects,
                  float layerDepth) override
        {
        }
    };

    /**
     * @brief CNAEXT. The Stub graphics renderer -- a deliberately minimal `IGraphicsRenderer`
     * implementation that renders nothing, touches no window, no GPU library, and no SDL video
     * subsystem, and keeps no bookkeeping of any kind (see `plan_stub.md`'s design decisions).
     *
     * Every method either does nothing or returns a fixed/trivial value. This is not a cut corner:
     * it is the entire point of this renderer -- the smallest possible complete `IGraphicsRenderer`
     * implementation, useful as a build target with no external dependencies and as a minimal
     * reference for anyone implementing a new renderer from scratch.
     */
    class StubRenderer : public IGraphicsRenderer
    {
    public:
        StubRenderer(int virtualWidth, int virtualHeight);

        void Clear(float r, float g, float b, float a) override {}
        void Present() override {}

        void GetViewportSize(int& width, int& height) override;
        void SetVirtualResolution(int width, int height) override;
        void SetPresentationMode(int mode) override {}


        std::unique_ptr<ITextureRenderer> CreateTexture(const ImageData& data) override;
        std::unique_ptr<ISpriteBatchRenderer> CreateSpriteBatch() override;

        /// The binding set is deliberately discarded, which is the explicit decision
        /// IGraphicsRenderer requires of every renderer rather than a default that could quietly
        /// flatten a cube face to RenderTarget2D or to face +X. Stub creates no render targets at
        /// all -- CreateRenderTarget2D()/CreateRenderTargetCube() keep the shared nullptr defaults
        /// -- and rasterizes nothing, so there is no attachment to bind and no surface a
        /// misinterpreted descriptor could corrupt.
        void SetRenderTargets(const RenderTargetBindingDescriptor* renderTargets,
                              int count) override {}

        void ClearColorAndDepth(float r, float g, float b, float a, float depth) override {}
        void ClearDepth(float depth) override {}
        void ClearStencil(int stencil) override {}
        void ClearDepthAndStencil(float depth, int stencil) override {}
        void ClearColorAndStencil(float r, float g, float b, float a, int stencil) override {}
        void ClearColorDepthAndStencil(float r, float g, float b, float a, float depth, int stencil) override {}

        void SetDepthTestEnabled(bool enabled) override {}
        void SetBlendEnabled(bool enabled) override {}
        void SetDepthWriteEnabled(bool enabled) override {}

        std::unique_ptr<IVertexBufferRenderer> CreateVertexBuffer(int vertex_capacity) override;
        std::unique_ptr<IIndexBufferRenderer> CreateIndexBuffer16(int index_capacity) override;
        std::unique_ptr<IIndexBufferRenderer> CreateIndexBuffer32(int index_capacity) override;

        void DrawColoredPrimitives(const IVertexBufferRenderer& vb,
                                   const Matrix& world,
                                   const Matrix& view,
                                   const Matrix& projection,
                                   PrimitiveType primitive,
                                   int primitiveCount) override
        {
        }

        void DrawIndexedColoredPrimitives(const IVertexBufferRenderer& vb,
                                          const IIndexBufferRenderer& ib,
                                          const Matrix& world,
                                          const Matrix& view,
                                          const Matrix& projection,
                                          PrimitiveType primitive,
                                          int primitiveCount) override
        {
        }

        [[nodiscard]] bool SupportsCapability(CNA::GraphicsCapability /*capability*/) const override
        {
            return false;
        }

    private:
        int virtualWidth_;
        int virtualHeight_;
    };
}
