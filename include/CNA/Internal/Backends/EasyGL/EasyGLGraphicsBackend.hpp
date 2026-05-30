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

    // TODO: EasyGLSpriteBatchBackend does not implement batching. Every Draw()
    // call uploads a fresh VBO/IBO and issues its own draw_elements(), resulting
    // in ~9 GL calls per sprite. A proper implementation would accumulate all
    // quads from Begin() to End() into a single CPU-side vertex buffer and flush
    // it with one draw_elements() call in End(). This can reduce GL call count
    // by 50-100x and is important for performance on mobile/web targets.
    //
    // Implementation estimate: medium difficulty, ~2-4 hours, ~100 lines changed
    // in this file only. What needs to change:
    //   1. Add CPU-side buffers: std::vector<Vertex> pending_vertices_ and
    //      std::vector<uint16_t> pending_indices_ as members.
    //   2. Draw() appends the quad to those vectors instead of uploading immediately.
    //      The vertex/index math is already correct, just move it to a push_back.
    //   3. End() flushes: one set_data() + one draw_elements(), then clears vectors.
    //   4. Texture changes require a flush: track current_texture_ and flush the
    //      pending batch whenever the texture changes between Draw() calls.
    //      Without a texture atlas this is the only real complication.
    //      A texture atlas would eliminate mid-batch flushes but is significantly
    //      more complex to implement.
    class EasyGLSpriteBatchBackend : public ISpriteBatchBackend, public ::easygl::RecoverableResource
    {
    private:
        ::easygl::Device& device_;
        ::easygl::Program program_;
        ::easygl::VertexArray vao_;
        ::easygl::Buffer vbo_;
        ::easygl::Buffer ibo_;
        bool begun = false;
        ::easygl::ResourceRegistry* registry_ = nullptr;

    public:
        explicit EasyGLSpriteBatchBackend(::easygl::Device& device, ::easygl::ResourceRegistry* registry);
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

        // 3D pipeline state
        ::easygl::Program program3d_;
        int loc_world_view_projection_ = -1;
        bool program3d_ready_ = false;

        void EnsureColored3DProgram();

    public:
        explicit EasyGLGraphicsBackend(SDL_Window* window);
        ~EasyGLGraphicsBackend() override;
        void Clear(float r, float g, float b, float a) override;
        void Present() override;
        void GetViewportSize(int& width, int& height) override;

        void SetVirtualResolution(int width, int height) override
        {
        } // no-op: EasyGL uses physical viewport
        void SetPresentationMode(int /*mode*/) override
        {
        } // no-op: EasyGL has no logical presentation
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
