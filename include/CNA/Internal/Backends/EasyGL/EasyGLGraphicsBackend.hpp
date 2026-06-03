#pragma once

#include "../Common/IGraphicsBackend.hpp"
#include "CNA/Internal/Graphics/ImageData.hpp"
#include <SDL3/SDL.h>
#include <easygl/easygl.hpp>
#include <cstdint>
#include <vector>

namespace CNA::Internal::Backends::EasyGL
{
    class EasyGLGraphicsBackend;

    class EasyGLTextureBackend : public ITextureBackend, public ::easygl::RecoverableResource
    {
    public:
        ::easygl::Texture texture;
        int width = 0;
        int height = 0;

        EasyGLTextureBackend(const ImageData& data, ::easygl::ResourceRegistry* registry);
        ~EasyGLTextureBackend() override;
        int GetWidth() const override { return width; }
        int GetHeight() const override { return height; }
        SDL_Texture* GetNativeTexture() const override { return nullptr; }

        void release_gl_handle_only() override;
        void recreate_gl_resource() override;

    private:
        ImageData image_data_;
        ::easygl::ResourceRegistry* registry_ = nullptr;
    };

    class EasyGLSpriteBatchBackend : public ISpriteBatchBackend, public ::easygl::RecoverableResource
    {
    public:
        struct Vertex { float x, y, u, v, r, g, b, a; };

    private:
        ::easygl::Device& device_;
        ::easygl::Program program_;
        ::easygl::VertexArray vao_;
        ::easygl::Buffer vbo_;
        ::easygl::Buffer ibo_;
        bool begun = false;
        ::easygl::ResourceRegistry* registry_ = nullptr;
        EasyGLGraphicsBackend* graphicsBackend_ = nullptr;

        // Batching state: quads are accumulated between Begin()/End() and
        // flushed in one draw call. A flush also occurs when the texture changes.
        std::vector<Vertex>   pending_vertices_;
        std::vector<uint16_t> pending_indices_;
        const EasyGLTextureBackend* current_texture_ = nullptr;

    public:
        explicit EasyGLSpriteBatchBackend(::easygl::Device& device, ::easygl::ResourceRegistry* registry,
                                          EasyGLGraphicsBackend* backend = nullptr);
        ~EasyGLSpriteBatchBackend() override;

        void Begin() override;
        void End() override;
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

        void release_gl_handle_only() override;
        void recreate_gl_resource() override;

    private:
        void InitializeResources();
        void FlushBatch();
    };

    /**
     * @brief EasyGL-backed `VertexPositionColor` vertex buffer.
     *
     * Owns a `::easygl::Buffer` (GL VBO) and a `::easygl::VertexArray`
     * pre-configured for the position+color layout.
     *
     * @note Status: IMPLEMENTED for `VertexPositionColor`.
     */
    class EasyGLVertexBufferBackend : public IVertexBufferBackend, public ::easygl::RecoverableResource
    {
    public:
        ::easygl::Buffer vbo;
        ::easygl::VertexArray vao;
        int vertex_count = 0;
        int capacity = 0;

        explicit EasyGLVertexBufferBackend(int vertex_capacity, ::easygl::ResourceRegistry* registry);
        ~EasyGLVertexBufferBackend() override;
        void SetData(const void* data, int vertex_count, std::size_t stride_in_bytes) override;
        int GetVertexCount() const override { return vertex_count; }

        void release_gl_handle_only() override;
        void recreate_gl_resource() override;

    private:
        void InitializeLayout();
        ::easygl::ResourceRegistry* registry_ = nullptr;
        std::vector<uint8_t> cpu_data_;
        std::size_t stride_in_bytes_ = 0;
    };

    /**
     * @brief EasyGL-backed 16-bit index buffer (GL IBO).
     *
     * @note Status: IMPLEMENTED.
     */
    class EasyGLIndexBufferBackend : public IIndexBufferBackend, public ::easygl::RecoverableResource
    {
    public:
        ::easygl::Buffer ibo;
        int index_count = 0;
        int capacity = 0;

        explicit EasyGLIndexBufferBackend(int index_capacity, ::easygl::ResourceRegistry* registry);
        ~EasyGLIndexBufferBackend() override;
        void SetData16(const void* data, int index_count) override;
        int GetIndexCount() const override { return index_count; }

        void release_gl_handle_only() override;
        void recreate_gl_resource() override;

    private:
        ::easygl::ResourceRegistry* registry_ = nullptr;
        std::vector<uint8_t> cpu_data_;
    };

    class EasyGLGraphicsBackend : public IGraphicsBackend
    {
    private:
        SDL_Window* window = nullptr;
        SDL_GLContext gl_context = nullptr;
        ::easygl::Device device;
        ::easygl::ResourceRegistry registry_;

        int virtualWidth_ = 0;
        int virtualHeight_ = 0;
        CnaPresentationMode presentationMode_ = CnaPresentationMode::FixedHeightDynamicWidth;

        // 3D pipeline state
        ::easygl::Program program3d_;
        int loc_world_view_projection_ = -1;
        bool program3d_ready_ = false;

        void EnsureColored3DProgram();

    public:
        explicit EasyGLGraphicsBackend(SDL_Window* window,
                                       int virtualWidth = 0, int virtualHeight = 0,
                                       CnaPresentationMode mode = CnaPresentationMode::FixedHeightDynamicWidth);
        ~EasyGLGraphicsBackend() override;
        void Clear(float r, float g, float b, float a) override;
        void Present() override;
        void GetViewportSize(int& width, int& height) override;
        void getLogicalSize(int& width, int& height) const;

        void SetVirtualResolution(int width, int height) override;
        void SetPresentationMode(int mode) override;
        SDL_Window* GetWindowInternal() const override { return window; }
        SDL_Renderer* GetRendererInternal() const override { return nullptr; }

        std::unique_ptr<ITextureBackend> CreateTexture(const ImageData& data) override;
        std::unique_ptr<ISpriteBatchBackend> CreateSpriteBatch() override;

        void DebugSimulateContextLoss() override;
        void DebugRestoreContext() override;

        // ---- 3D: IMPLEMENTED ----
        void ClearColorAndDepth(float r, float g, float b, float a, float depth) override;
        void SetDepthTestEnabled(bool enabled) override;
        void SetBlendEnabled(bool enabled) override;
        void SetDepthWriteEnabled(bool enabled) override;
        std::unique_ptr<IVertexBufferBackend> CreateVertexBuffer(int vertex_capacity) override;
        std::unique_ptr<IIndexBufferBackend> CreateIndexBuffer16(int index_capacity) override;
        void DrawColoredPrimitives(const IVertexBufferBackend& vb,
                                   const Matrix& world, const Matrix& view, const Matrix& projection,
                                   PrimitiveType primitive, int primitiveCount) override;
        void DrawIndexedColoredPrimitives(const IVertexBufferBackend& vb,
                                          const IIndexBufferBackend& ib,
                                          const Matrix& world, const Matrix& view, const Matrix& projection,
                                          PrimitiveType primitive, int primitiveCount) override;
    };
}
