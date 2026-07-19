#pragma once

#include "../Common/IGraphicsBackend.hpp"

namespace CNA::Internal::Backends::Stub
{
    /// Vertex-buffer handle for the Stub backend. Stores only the vertex count it was given --
    /// no backing storage of any kind (plan_stub.md design decision 3).
    class StubVertexBufferBackend : public IVertexBufferBackend
    {
    public:
        explicit StubVertexBufferBackend(int vertexCapacity) : vertexCount_(vertexCapacity) {}

        void SetData(const void* data, int vertex_count, std::size_t stride_in_bytes) override
        {
            vertexCount_ = vertex_count;
        }

        [[nodiscard]] int GetVertexCount() const override { return vertexCount_; }

    private:
        int vertexCount_;
    };

    /// Index-buffer handle for the Stub backend. Stores only the index count it was given -- same
    /// no-backing-storage shape as StubVertexBufferBackend.
    class StubIndexBufferBackend : public IIndexBufferBackend
    {
    public:
        explicit StubIndexBufferBackend(int indexCapacity) : indexCount_(indexCapacity) {}

        void SetData16(const void* data, int index_count) override
        {
            indexCount_ = index_count;
            isThirtyTwoBit_ = false;
        }

        void SetData32(const void* data, int index_count) override
        {
            indexCount_ = index_count;
            isThirtyTwoBit_ = true;
        }

        [[nodiscard]] int GetIndexCount() const override { return indexCount_; }
        [[nodiscard]] bool IsThirtyTwoBit() const override { return isThirtyTwoBit_; }

    private:
        int indexCount_;
        bool isThirtyTwoBit_ = false;
    };

    /// Texture handle for the Stub backend. Stores only width/height -- no pixel data of any kind;
    /// SetData()/GetData() are accepted (inherited no-op defaults) and simply discarded.
    class StubTextureBackend : public ITextureBackend
    {
    public:
        StubTextureBackend(int width, int height) : width_(width), height_(height) {}

        [[nodiscard]] int GetWidth() const override { return width_; }
        [[nodiscard]] int GetHeight() const override { return height_; }
        [[nodiscard]] SDL_Texture* GetNativeTexture() const override { return nullptr; }

    private:
        int width_;
        int height_;
    };

    /// SpriteBatch handle for the Stub backend. Every Draw()/Begin()/End() call is a no-op.
    class StubSpriteBatchBackend : public ISpriteBatchBackend
    {
    public:
        void Begin() override {}
        void End() override {}

        void Draw(const ITextureBackend& texture, float x, float y) override {}

        void Draw(const ITextureBackend& texture,
                  const Rectangle& destinationRectangle,
                  const Rectangle& sourceRectangle,
                  const Color& color) override
        {
        }

        void Draw(const ITextureBackend& texture,
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
     * @brief NOXNA. The Stub graphics backend -- a deliberately minimal `IGraphicsBackend`
     * implementation that renders nothing, touches no window, no GPU library, and no SDL video
     * subsystem, and keeps no bookkeeping of any kind (see `plan_stub.md`'s design decisions).
     *
     * Every method either does nothing or returns a fixed/trivial value. This is not a cut corner:
     * it is the entire point of this backend -- the smallest possible complete `IGraphicsBackend`
     * implementation, useful as a build target with no external dependencies and as a minimal
     * reference for anyone implementing a new backend from scratch.
     */
    class StubGraphicsBackend : public IGraphicsBackend
    {
    public:
        StubGraphicsBackend(int virtualWidth, int virtualHeight);

        void Clear(float r, float g, float b, float a) override {}
        void Present() override {}

        void GetViewportSize(int& width, int& height) override;
        void SetVirtualResolution(int width, int height) override;
        void SetPresentationMode(int mode) override {}

        [[nodiscard]] SDL_Window* GetWindowInternal() const override { return nullptr; }
        [[nodiscard]] SDL_Renderer* GetRendererInternal() const override { return nullptr; }

        std::unique_ptr<ITextureBackend> CreateTexture(const ImageData& data) override;
        std::unique_ptr<ISpriteBatchBackend> CreateSpriteBatch() override;

        void ClearColorAndDepth(float r, float g, float b, float a, float depth) override {}
        void ClearDepth(float depth) override {}
        void ClearStencil(int stencil) override {}
        void ClearDepthAndStencil(float depth, int stencil) override {}
        void ClearColorAndStencil(float r, float g, float b, float a, int stencil) override {}
        void ClearColorDepthAndStencil(float r, float g, float b, float a, float depth, int stencil) override {}

        void SetDepthTestEnabled(bool enabled) override {}
        void SetBlendEnabled(bool enabled) override {}
        void SetDepthWriteEnabled(bool enabled) override {}

        std::unique_ptr<IVertexBufferBackend> CreateVertexBuffer(int vertex_capacity) override;
        std::unique_ptr<IIndexBufferBackend> CreateIndexBuffer16(int index_capacity) override;

        void DrawColoredPrimitives(const IVertexBufferBackend& vb,
                                   const Matrix& world,
                                   const Matrix& view,
                                   const Matrix& projection,
                                   PrimitiveType primitive,
                                   int primitiveCount) override
        {
        }

        void DrawIndexedColoredPrimitives(const IVertexBufferBackend& vb,
                                          const IIndexBufferBackend& ib,
                                          const Matrix& world,
                                          const Matrix& view,
                                          const Matrix& projection,
                                          PrimitiveType primitive,
                                          int primitiveCount) override
        {
        }

    private:
        int virtualWidth_;
        int virtualHeight_;
    };
}
