// SPDX-License-Identifier: MS-PL
#pragma once

#include "CNA/Internal/Backends/Common/IGraphicsBackend.hpp"

#include <GLES/gl.h>
#include <GLES/glext.h>

#include <cstdint>
#include <vector>

struct SDL_Window;

namespace CNA::Internal::Backends::OpenGLES1
{
    /**
     * @brief NOXNA. Real OpenGL ES 1.1 (fixed-function, no shaders) texture handle.
     *
     * Always stores a level-0 RGBA8 image. There is no SDL_Renderer involved (GetNativeTexture()
     * always returns nullptr, matching every other raw-GL backend), and no programmable shader
     * involvement -- texture combining is done entirely via glTexEnv*.
     */
    class OpenGLES1TextureBackend : public ITextureBackend
    {
    public:
        OpenGLES1TextureBackend(int width, int height);
        ~OpenGLES1TextureBackend() override;

        OpenGLES1TextureBackend(const OpenGLES1TextureBackend&) = delete;
        OpenGLES1TextureBackend& operator=(const OpenGLES1TextureBackend&) = delete;

        int GetWidth() const override { return width_; }
        int GetHeight() const override { return height_; }
        SDL_Texture* GetNativeTexture() const override { return nullptr; }

        void UpdatePixels(const uint8_t* rgba, int stride) override;
        void UpdatePixelsLevel(int level, const uint8_t* rgba, int levelW, int levelH) override;
        void BindGL() const override;

        [[nodiscard]] unsigned int GetGLHandle() const { return texture_; }

    private:
        unsigned int texture_ = 0;
        int width_ = 0;
        int height_ = 0;
    };

    /**
     * @brief NOXNA. Client-side vertex storage for OpenGL ES 1.1.
     *
     * ES 1.1 core does not mandate GPU-side buffer objects (GL_OES_vertex_buffer_object is an
     * optional extension) -- every draw uploads directly from a CPU-side byte array via
     * glVertexPointer/glColorPointer/glTexCoordPointer/glNormalPointer, which the fixed-function
     * pipeline always supports.
     */
    class OpenGLES1VertexBufferBackend : public IVertexBufferBackend
    {
    public:
        explicit OpenGLES1VertexBufferBackend(int vertexCapacity) : vertexCapacity_(vertexCapacity) {}

        void SetData(const void* data, int vertex_count, std::size_t stride_in_bytes) override;
        int GetVertexCount() const override { return vertexCount_; }

        [[nodiscard]] const uint8_t* Data() const { return data_.data(); }
        [[nodiscard]] std::size_t Stride() const { return stride_; }

    private:
        int vertexCapacity_ = 0;
        int vertexCount_ = 0;
        std::size_t stride_ = 0;
        std::vector<uint8_t> data_;
    };

    /** @brief NOXNA. Client-side 16-bit index storage for OpenGL ES 1.1 (see vertex buffer above). */
    class OpenGLES1IndexBufferBackend : public IIndexBufferBackend
    {
    public:
        explicit OpenGLES1IndexBufferBackend(int indexCapacity) : indexCapacity_(indexCapacity) {}

        void SetData16(const void* data, int index_count) override;
        int GetIndexCount() const override { return indexCount_; }

        [[nodiscard]] const uint16_t* Data() const { return data_.data(); }

    private:
        int indexCapacity_ = 0;
        int indexCount_ = 0;
        std::vector<uint16_t> data_;
    };

    class OpenGLES1GraphicsBackend;

    /**
     * @brief NOXNA. 2D quad batcher built on the fixed-function pipeline.
     *
     * Batches quads into CPU-side arrays exactly like the vertex/index buffers above, and flushes
     * them with a single glDrawElements() call per texture change, under an orthographic
     * GL_PROJECTION matrix (glOrthof) matching XNA's SpriteBatch top-left-origin convention.
     */
    class OpenGLES1SpriteBatchBackend : public ISpriteBatchBackend
    {
    public:
        explicit OpenGLES1SpriteBatchBackend(OpenGLES1GraphicsBackend* owner) : owner_(owner) {}

        void Begin() override;
        void End() override;
        void SetTransformMatrix(const Matrix& m) override { transform_ = m; }
        void SetSamplerFilter(int textureFilter) override { pendingFilter_ = textureFilter; }
        void SetSamplerAddressMode(int addressU, int addressV) override
        {
            pendingAddressU_ = addressU;
            pendingAddressV_ = addressV;
        }

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
        struct Vertex
        {
            float x, y;
            float u, v;
            float r, g, b, a;
        };

        void FlushBatch();

        OpenGLES1GraphicsBackend* owner_ = nullptr;
        bool begun_ = false;
        Matrix transform_ = Matrix::getIdentityProperty();
        int pendingFilter_ = 0;
        int pendingAddressU_ = 0;
        int pendingAddressV_ = 0;
        const ITextureBackend* currentTexture_ = nullptr;
        std::vector<Vertex> pendingVertices_;
        std::vector<uint16_t> pendingIndices_;
    };

    /**
     * @brief NOXNA. Graphics backend built on real OpenGL ES 1.1 (the fixed-function "Common"
     * profile), deliberately independent of the EasyGL backend (which targets WebGL2/OpenGL ES
     * 3.0 and cannot create an ES 1.1 context at all).
     *
     * There is no programmable-shader path: `CreateEffectBackend()` keeps the base class's
     * `nullptr` default and every stock XNA effect (`BasicEffect`, `AlphaTestEffect`, ...) is
     * rendered by translating `GpuDrawParams` onto the fixed-function matrix stack, texture
     * environment, and per-vertex lighting/fog state instead of compiling a shader. See
     * `docs/opengles1-backend.md` for exactly which effects map cleanly onto this pipeline and
     * which do not (skinning, PBR, cube-map environment mapping, and custom `ShaderEffect` all
     * have no fixed-function equivalent and are permanently unsupported by this backend, not a
     * "not yet implemented" gap).
     */
    class OpenGLES1GraphicsBackend : public IGraphicsBackend
    {
    public:
        explicit OpenGLES1GraphicsBackend(const GraphicsBackendCreateArgs& args);
        ~OpenGLES1GraphicsBackend() override;

        OpenGLES1GraphicsBackend(const OpenGLES1GraphicsBackend&) = delete;
        OpenGLES1GraphicsBackend& operator=(const OpenGLES1GraphicsBackend&) = delete;

        void Clear(float r, float g, float b, float a) override;
        void Present() override;
        void GetViewportSize(int& width, int& height) override;
        void SetVirtualResolution(int width, int height) override;
        void SetPresentationMode(int mode) override;
        void SetSwapInterval(int interval) override;

        bool TransformWindowToLogical(float windowX, float windowY, float& logX, float& logY) const override;
        bool TransformLogicalToWindow(float logX, float logY, float& windowX, float& windowY) const override;

        SDL_Window* GetWindowInternal() const override { return window_; }
        SDL_Renderer* GetRendererInternal() const override { return nullptr; }

        std::unique_ptr<ITextureBackend> CreateTexture(const ImageData& data) override;
        std::unique_ptr<ISpriteBatchBackend> CreateSpriteBatch() override;

        void ReadBackbuffer(int x, int y, int w, int h, uint8_t* pixels) override;

        void ClearColorAndDepth(float r, float g, float b, float a, float depth) override;
        void ClearDepth(float depth) override;
        void ClearStencil(int stencil) override;
        void ClearDepthAndStencil(float depth, int stencil) override;
        void ClearColorAndStencil(float r, float g, float b, float a, int stencil) override;
        void ClearColorDepthAndStencil(float r, float g, float b, float a, float depth, int stencil) override;

        void SetDepthTestEnabled(bool enabled) override;
        void SetBlendEnabled(bool enabled) override;
        void SetDepthWriteEnabled(bool enabled) override;

        std::unique_ptr<IVertexBufferBackend> CreateVertexBuffer(int vertex_capacity) override;
        std::unique_ptr<IIndexBufferBackend> CreateIndexBuffer16(int index_capacity) override;

        void DrawColoredPrimitives(const IVertexBufferBackend& vb,
                                   const Matrix& world, const Matrix& view, const Matrix& projection,
                                   PrimitiveType primitive, int primitiveCount) override;
        void DrawIndexedColoredPrimitives(const IVertexBufferBackend& vb, const IIndexBufferBackend& ib,
                                          const Matrix& world, const Matrix& view, const Matrix& projection,
                                          PrimitiveType primitive, int primitiveCount) override;
        void DrawPrimitivesEx(const IVertexBufferBackend& vb,
                             const Matrix& world, const Matrix& view, const Matrix& projection,
                             PrimitiveType primitive, int primitiveCount,
                             const GpuDrawParams& params) override;
        void DrawIndexedPrimitivesEx(const IVertexBufferBackend& vb, const IIndexBufferBackend& ib,
                                    const Matrix& world, const Matrix& view, const Matrix& projection,
                                    PrimitiveType primitive, int primitiveCount,
                                    const GpuDrawParams& params) override;

        void ApplyBlendState(int colorSrcBlend, int alphaSrcBlend,
                             int colorDstBlend, int alphaDstBlend,
                             int colorBlendFunc, int alphaBlendFunc) override;
        void ApplyDepthStencilState(bool depthEnable, bool depthWriteEnable,
                                    int depthFunc,
                                    bool stencilEnable, int stencilFunc,
                                    int stencilPass, int stencilFail, int stencilDepthFail,
                                    int stencilMask, int stencilWriteMask, int referenceStencil,
                                    bool twoSidedStencilMode,
                                    int ccwStencilFunc, int ccwStencilPass,
                                    int ccwStencilFail, int ccwStencilDepthFail) override;
        void ApplyRasterizerState(int cullMode, int fillMode,
                                  bool scissorTestEnable,
                                  float depthBias = 0.0f,
                                  float slopeScaleDepthBias = 0.0f) override;
        void ApplySamplerState(int slot, int filter, int addressU, int addressV, int maxAnisotropy) override;
        void SetScissorRect(int x, int y, int w, int h) override;
        void SetViewport(int x, int y, int w, int h, float minDepth, float maxDepth) override;

        [[nodiscard]] bool SupportsDepthStencil() const override { return true; }
        [[nodiscard]] bool SupportsCapability(CNA::GraphicsCapability capability) const override;

        void DebugSimulateContextLoss() override;
        void DebugRestoreContext() override;

        /// NOXNA. Physical (window) pixel size -- used by the sprite-batch backend to size its
        /// GL viewport/orthographic projection.
        void GetPhysicalSize(int& width, int& height) const;
        /// NOXNA. Logical (virtual-resolution) size -- see GetPhysicalSize's own note.
        void GetLogicalSize(int& width, int& height) const;

        /// NOXNA. Sets the GL viewport to the physical window size and an orthographic
        /// GL_PROJECTION matching the logical (virtual-resolution) size. Called by
        /// OpenGLES1SpriteBatchBackend::FlushBatch() before each flush, mirroring every other
        /// backend's SpriteBatch-owns-its-own-2D-projection convention.
        void ApplyLogicalViewportAndOrtho2D();

    private:
        void CreateGLContext();
        void DestroyGLContext();
        void LoadExtensionEntryPoints();

        SDL_Window* window_ = nullptr;
        void* glContext_ = nullptr;

        int virtualWidth_ = 0;
        int virtualHeight_ = 0;
        CnaPresentationMode presentationMode_ = CnaPresentationMode::FixedHeightDynamicWidth;
        int swapInterval_ = 1;

        // GL_OES_blend_subtract / GL_OES_blend_func_separate -- both widely-supported ES1.1
        // extensions, resolved once at startup instead of assumed to exist as directly-linkable
        // symbols (portable across ES1 CM implementations that only expose them via
        // eglGetProcAddress/SDL_GL_GetProcAddress). Null if unavailable; callers fall back to
        // glBlendFunc/default-Add accordingly (documented deviation, see docs/opengles1-backend.md).
        PFNGLBLENDFUNCSEPARATEOESPROC glBlendFuncSeparateOES_ = nullptr;
        PFNGLBLENDEQUATIONOESPROC glBlendEquationOES_ = nullptr;
    };
}
