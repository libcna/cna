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
        void BindGL() const override;
        void UpdatePixels(const uint8_t* rgba, int stride) override;
        void UpdatePixelsLevel(int level, const uint8_t* rgba, int levelW, int levelH) override;

        void release_gl_handle_only() override;
        void recreate_gl_resource() override;
        void ShareCpuPixels(std::shared_ptr<std::vector<uint8_t>> pixels) override;

    private:
        std::shared_ptr<std::vector<uint8_t>> pixels_;
        ::easygl::ResourceRegistry* registry_ = nullptr;
    };

    /// EasyGL render target: off-screen FBO with a color texture and optional depth renderbuffer.
    class EasyGLRenderTargetBackend : public IRenderTargetBackend, public ::easygl::RecoverableResource
    {
    public:
        EasyGLRenderTargetBackend(int w, int h, bool hasDepth, ::easygl::ResourceRegistry* registry);
        ~EasyGLRenderTargetBackend() override;

        int GetWidth()  const override { return width_; }
        int GetHeight() const override { return height_; }
        SDL_Texture* GetNativeTexture() const override { return nullptr; }
        void BindGL() const override;

        void BindAsRenderTarget()   override;
        void UnbindAsRenderTarget() override;

        void release_gl_handle_only() override;
        void recreate_gl_resource()   override;

    private:
        void CreateResources();

        ::easygl::Framebuffer  fbo_;
        ::easygl::Texture      colorTex_;
        ::easygl::Renderbuffer depthRbo_;
        int  width_    = 0;
        int  height_   = 0;
        bool hasDepth_ = false;
        ::easygl::ResourceRegistry* registry_ = nullptr;
    };

    class EasyGLOcclusionQueryBackend : public IOcclusionQueryBackend, public ::easygl::RecoverableResource
    {
    public:
        explicit EasyGLOcclusionQueryBackend(::easygl::ResourceRegistry* registry);
        ~EasyGLOcclusionQueryBackend() override;

        void Begin() override;
        void End()   override;
        [[nodiscard]] bool IsComplete() const override;
        [[nodiscard]] int  PixelCount() const override;

        void release_gl_handle_only() override;
        void recreate_gl_resource()   override;

    private:
        ::easygl::Query query_;
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
        const ITextureBackend* current_texture_ = nullptr;
        Matrix transform_ = Matrix::getIdentityProperty();
        Effect* customEffect_       = nullptr;
        ::easygl::Program customProgram_;
        Effect* compiledFor_        = nullptr;

    public:
        explicit EasyGLSpriteBatchBackend(::easygl::Device& device, ::easygl::ResourceRegistry* registry,
                                          EasyGLGraphicsBackend* backend = nullptr);
        ~EasyGLSpriteBatchBackend() override;

        void Begin() override;
        void End() override;
        void SetTransformMatrix(const Matrix& m) override;
        void SetCustomEffect(Effect* effect) override;
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
     * @brief EasyGL-backed vertex buffer.
     *
     * Owns a GL VBO and VAO. The VAO attribute layout is configured
     * dynamically in ApplyLayout() based on the stride passed to SetData(),
     * supporting all four built-in XNA vertex types.
     *
     * @note Status: IMPLEMENTED for stride 16/20/24/32 (VertexPositionColor,
     *       VertexPositionTexture, VertexPositionColorTexture,
     *       VertexPositionNormalTexture).
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
        [[nodiscard]] std::size_t GetStride() const { return stride_in_bytes_; }

        void release_gl_handle_only() override;
        void recreate_gl_resource() override;

    private:
        void InitializeLayout();
        void ApplyLayout(std::size_t stride);
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

        bool thirtyTwoBit = false;

        explicit EasyGLIndexBufferBackend(int index_capacity, bool thirtyTwoBit,
                                          ::easygl::ResourceRegistry* registry);
        ~EasyGLIndexBufferBackend() override;
        void SetData16(const void* data, int index_count) override;
        void SetData32(const void* data, int index_count) override;
        int  GetIndexCount()  const override { return index_count; }
        bool IsThirtyTwoBit() const override { return thirtyTwoBit; }

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

        // 3D pipeline state — one program per vertex layout
        struct Prog3D {
            ::easygl::Program prog;
            bool  ready      = false;
            int loc_wvp      = -1;
            int loc_normalmat= -1;  ///< mat3 upper-left of world (lit shader only)
            int loc_diffuse  = -1;
            int loc_ambient  = -1;
            int loc_l0dir    = -1;
            int loc_l0diff   = -1;
            int loc_texture  = -1;
            void reset_no_gl() { prog.reset_handle_no_gl(); ready = false; }
        };

        Prog3D prog_colored_;       ///< stride=16: aPos + aColor
        Prog3D prog_textured_;      ///< stride=20: aPos + aUV
        Prog3D prog_col_textured_;  ///< stride=24: aPos + aColor + aUV
        Prog3D prog_lit_textured_;  ///< stride=32: aPos + aNormal + aUV

        ::easygl::Texture default_white_texture_;
        bool default_white_texture_ready_ = false;

        void EnsureColored3DProgram();
        void EnsureTextured3DProgram();
        void EnsureColoredTextured3DProgram();
        void EnsureLit3DProgram();
        void EnsureDefaultWhiteTexture();
        Prog3D& SelectProgram(std::size_t stride);
        void BindDrawParams(Prog3D& p, const Matrix& world, const Matrix& view,
                            const Matrix& projection, const GpuDrawParams& params);

    public:
        explicit EasyGLGraphicsBackend(SDL_Window* window,
                                       int virtualWidth = 0, int virtualHeight = 0,
                                       CnaPresentationMode mode = CnaPresentationMode::FixedHeightDynamicWidth);
        ~EasyGLGraphicsBackend() override;
        void Clear(float r, float g, float b, float a) override;
        void Present() override;
        void GetViewportSize(int& width, int& height) override;
        void getLogicalSize(int& width, int& height) const;
        void getPhysicalSize(int& width, int& height) const;
        bool TransformWindowToLogical(float windowX, float windowY,
                                      float& logX, float& logY) const override;

        void SetVirtualResolution(int width, int height) override;
        void SetPresentationMode(int mode) override;
        SDL_Window* GetWindowInternal() const override { return window; }
        SDL_Renderer* GetRendererInternal() const override { return nullptr; }

        std::unique_ptr<ITextureBackend> CreateTexture(const ImageData& data) override;
        std::unique_ptr<ISpriteBatchBackend> CreateSpriteBatch() override;
        std::unique_ptr<IOcclusionQueryBackend> CreateOcclusionQuery() override;
        std::unique_ptr<IRenderTargetBackend> CreateRenderTarget2D(int w, int h, bool hasDepth) override;
        void SetRenderTarget2D(IRenderTargetBackend* rt) override;
        std::unique_ptr<IIndexBufferBackend> CreateIndexBuffer16(int index_capacity) override;
        std::unique_ptr<IIndexBufferBackend> CreateIndexBuffer32(int index_capacity) override;

        void DebugSimulateContextLoss() override;
        void DebugRestoreContext() override;
        void ReadBackbuffer(int x, int y, int w, int h, uint8_t* pixels) override;

        // ---- Graphics state: IMPLEMENTED ----
        void ApplyBlendState(int colorSrcBlend, int alphaSrcBlend,
                             int colorDstBlend, int alphaDstBlend,
                             int colorBlendFunc, int alphaBlendFunc) override;
        void ApplyDepthStencilState(bool depthEnable, bool depthWriteEnable, int depthFunc) override;
        void ApplyRasterizerState(int cullMode, int fillMode, bool scissorTestEnable) override;

        // ---- 3D: IMPLEMENTED ----
        void ClearColorAndDepth(float r, float g, float b, float a, float depth) override;
        void SetDepthTestEnabled(bool enabled) override;
        void SetBlendEnabled(bool enabled) override;
        void SetDepthWriteEnabled(bool enabled) override;
        std::unique_ptr<IVertexBufferBackend> CreateVertexBuffer(int vertex_capacity) override;
        void DrawColoredPrimitives(const IVertexBufferBackend& vb,
                                   const Matrix& world, const Matrix& view, const Matrix& projection,
                                   PrimitiveType primitive, int primitiveCount) override;
        void DrawIndexedColoredPrimitives(const IVertexBufferBackend& vb,
                                          const IIndexBufferBackend& ib,
                                          const Matrix& world, const Matrix& view, const Matrix& projection,
                                          PrimitiveType primitive, int primitiveCount) override;
        void DrawPrimitivesEx(const IVertexBufferBackend& vb,
                              const Matrix& world, const Matrix& view, const Matrix& projection,
                              PrimitiveType primitive, int primitiveCount,
                              const GpuDrawParams& params) override;
        void DrawIndexedPrimitivesEx(const IVertexBufferBackend& vb,
                                     const IIndexBufferBackend& ib,
                                     const Matrix& world, const Matrix& view, const Matrix& projection,
                                     PrimitiveType primitive, int primitiveCount,
                                     const GpuDrawParams& params) override;
        void DrawInstancedPrimitivesEx(const IVertexBufferBackend& vb,
                                       const IIndexBufferBackend& ib,
                                       const Matrix& world, const Matrix& view, const Matrix& projection,
                                       PrimitiveType primitive, int primitiveCount,
                                       int instanceCount,
                                       const GpuDrawParams& params) override;
    };
}
