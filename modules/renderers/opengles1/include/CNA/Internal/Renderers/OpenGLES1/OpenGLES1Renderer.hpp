// SPDX-License-Identifier: MS-PL
#pragma once

#include "CNA/Internal/Renderers/Common/IGraphicsRenderer.hpp"
#include "CNA/Internal/Graphics/VertexDeclarationFidelity.hpp"

#include <GLES/gl.h>
#include <GLES/glext.h>

#include <cstdint>
#include <memory>
#include <vector>

struct SDL_Window;

namespace CNA::Internal::Renderers::OpenGLES1
{
    class OpenGLES1Renderer;
    class OpenGLES1TextureRenderer;
    class OpenGLES1VertexBufferRenderer;
    class OpenGLES1IndexBufferRenderer;

    /**
     * @brief CNAEXT. Real OpenGL ES 1.1 (fixed-function, no shaders) texture handle.
     *
     * Always stores a level-0 RGBA8 image. There is no SDL_Renderer involved and no programmable
     * shader involvement -- texture combining is done entirely via glTexEnv*.
     */
    class OpenGLES1TextureRenderer : public ITextureRenderer
    {
    public:
        OpenGLES1TextureRenderer(OpenGLES1Renderer* owner, int width, int height);
        ~OpenGLES1TextureRenderer() override;

        OpenGLES1TextureRenderer(const OpenGLES1TextureRenderer&) = delete;
        OpenGLES1TextureRenderer& operator=(const OpenGLES1TextureRenderer&) = delete;

        int GetWidth() const override { return width_; }
        int GetHeight() const override { return height_; }

        void UpdatePixels(const uint8_t* rgba, int stride) override;
        void UpdatePixelsLevel(int level, const uint8_t* rgba, int levelW, int levelH) override;
        void BindGL(int /*unit*/) const override;

        /**
         * @brief Adopts `Texture2D`'s own CPU pixel buffer so the texture can be rebuilt after a
         *        GL context loss.
         *
         * Sharing rather than duplicating matches the interface's stated intent and keeps a single
         * copy of the pixels between `Texture2D` and this renderer.
         *
         * @param pixels Shared RGBA8 pixel storage owned by `Texture2D`.
         */
        void ShareCpuPixels(std::shared_ptr<std::vector<uint8_t>> pixels) override;

        /**
         * @brief Reads raw RGBA8 pixels back out of this texture.
         *
         * ES 1.1 has no `glGetTexImage`, so the texture is read through a temporary framebuffer
         * with it attached as the colour target -- the same route the render target uses. Falls
         * back to the shared CPU copy when framebuffer objects are unavailable, and leaves `data`
         * untouched when neither route works (the interface's own convention).
         *
         * @param level Mip level; only level 0 is stored by this renderer.
         * @param x Left edge of the sub-rectangle to read.
         * @param y Top edge of the sub-rectangle, in top-left-origin coordinates.
         * @param w Width of the sub-rectangle.
         * @param h Height of the sub-rectangle.
         * @param data Destination buffer receiving RGBA8 pixels.
         * @param dataLength Size of that buffer in bytes.
         * @return True when the pixels were read back, false when they could not be.
         */
        [[nodiscard]] bool GetData(int level, int x, int y, int w, int h,
                                   void* data, int dataLength) const override;

        /**
         * @brief Recreates this texture's GL object after the context it lived in was destroyed.
         *
         * The old GL name dies with the old context, so it is regenerated, its default sampler
         * state reapplied, and its pixels re-uploaded from the shared CPU copy. Without this a
         * restored context samples every pre-loss texture as plain white.
         */
        void RestoreAfterContextLoss();

        /// CNAEXT. Releases the shared pixel copy; the texture can no longer survive a context loss.
        void DropCpuShadowEXT() { pixels_.reset(); }

        [[nodiscard]] unsigned int GetGLHandle() const { return texture_; }

    private:
        OpenGLES1Renderer* owner_ = nullptr;
        unsigned int texture_ = 0;
        int width_ = 0;
        int height_ = 0;
        std::shared_ptr<std::vector<uint8_t>> pixels_;
    };

    /**
     * @brief CNAEXT. Real GPU-side vertex buffer object for OpenGL ES 1.1.
     *
     * `glGenBuffers`/`glBindBuffer`/`glBufferData` are core OpenGL ES 1.1 entry points (the
     * `GL_OES_vertex_buffer_object` extension was folded into the 1.1 core spec, unlike the
     * separately-optional `GL_OES_framebuffer_object`/`GL_OES_texture_cube_map` this renderer also
     * uses elsewhere) -- so a real VBO is always available, no runtime extension check needed.
     * `glVertexPointer`/`glColorPointer`/`glTexCoordPointer`/`glNormalPointer` take byte offsets
     * into the currently-bound `GL_ARRAY_BUFFER` instead of raw client pointers once one is bound.
     */
    class OpenGLES1VertexBufferRenderer : public IVertexBufferRenderer
    {
    public:
        OpenGLES1VertexBufferRenderer(OpenGLES1Renderer* owner, int vertexCapacity);
        ~OpenGLES1VertexBufferRenderer() override;

        OpenGLES1VertexBufferRenderer(const OpenGLES1VertexBufferRenderer&) = delete;
        OpenGLES1VertexBufferRenderer& operator=(const OpenGLES1VertexBufferRenderer&) = delete;

        void SetData(const void* data, int vertex_count, std::size_t stride_in_bytes) override;

        // REMED-GFX-DECL-GUARD: this renderer still selects its fixed-function pointer layout from
        // the vertex stride (16/20/24/32), but the declaration is remembered rather than discarded
        // so a draw can refuse one that the stride table would silently reinterpret.
        void SetVertexDeclaration(const VertexDeclaration& vertexDeclaration) override
        {
            declaration_.Remember(vertexDeclaration);
        }

        int GetVertexCount() const override { return vertexCount_; }

        /// CNAEXT. The declaration this buffer carries, for REMED-GFX-DECL-GUARD's fidelity check.
        [[nodiscard]] const CNA::Internal::Graphics::DeclaredVertexLayout& GetDeclarationEXT() const
        {
            return declaration_;
        }

        /// CNAEXT. Releases the raw vertex shadow; the buffer can no longer survive a context loss.
        void DropCpuShadowEXT();

        /**
         * @brief Recreates this buffer's GL object after the context it lived in was destroyed.
         *
         * Re-uploads from the CPU shadow kept by `SetData()`; without it a restored context draws
         * from a dead buffer name and renders nothing.
         */
        void RestoreAfterContextLoss();

        /// CNAEXT. Binds this buffer's VBO to GL_ARRAY_BUFFER -- callers then use byte offsets
        /// (not raw pointers) with glVertexPointer/glColorPointer/glTexCoordPointer/glNormalPointer.
        void Bind() const;
        [[nodiscard]] std::size_t Stride() const { return stride_; }

    private:
        OpenGLES1Renderer* owner_ = nullptr;
        unsigned int buffer_ = 0;
        int vertexCapacity_ = 0;
        int vertexCount_ = 0;
        std::size_t stride_ = 0;
        // Raw vertex bytes, kept solely so the GPU buffer can be rebuilt after a context loss
        // (OPENGLES1-80) -- draws always read from the GPU buffer, never from here.
        std::vector<uint8_t> cpuShadow_;
        CNA::Internal::Graphics::DeclaredVertexLayout declaration_;
    };

    /**
     * @brief Refuses a draw whose VertexDeclaration this renderer cannot bind faithfully.
     *
     * REMED-GFX-DECL-GUARD. The fixed-function pointer setup below picks its layout from the
     * buffer stride alone, so a declaration that packs different semantics into the same stride
     * would be read from the wrong bytes. The check is asymmetric: only what the caller actually
     * declared is verified, never equality against this renderer's own template.
     *
     * @param vb The vertex buffer whose declaration is being checked.
     * @param route Name of the draw route, for the diagnostic message.
     * @throws System::NotSupportedException When the declaration cannot be represented.
     */
    inline void RequireFaithfulDeclarationEXT(const IVertexBufferRenderer& vb, const char* route)
    {
        const auto& esVb = static_cast<const OpenGLES1VertexBufferRenderer&>(vb);
        CNA::Internal::Graphics::RequireFaithfulVertexDeclaration(
            esVb.GetDeclarationEXT(), static_cast<int>(esVb.Stride()),
            CNA::Internal::Graphics::UnlistedStrideLayout::RendererRefusesIt, "OpenGLES1", route);
    }

    /**
     * @brief CNAEXT. Real GPU-side 16-bit index buffer object for OpenGL ES 1.1 (see vertex buffer
     * above).
     *
     * Also keeps a small CPU-side shadow copy of the raw index values -- needed only for
     * wireframe emulation (OPENGLES1-76): re-expanding a triangle draw into `GL_LINES` requires
     * reading back which vertices form each triangle, which a GPU-only buffer can't answer
     * without a synchronous readback. Mirrors `EasyGLVertexBufferRenderer`/
     * `EasyGLIndexBufferRenderer`'s own identical `GetCpuBytes()`-backed
     * `EasyGLRenderer::DrawWireframe()` technique.
     */
    class OpenGLES1IndexBufferRenderer : public IIndexBufferRenderer
    {
    public:
        /**
         * @brief Creates a 16- or 32-bit index buffer.
         *
         * @param owner Renderer used for context-loss restoration; may be null.
         * @param indexCapacity Number of indices the buffer must hold.
         * @param thirtyTwoBit True for 32-bit indices, which require `GL_OES_element_index_uint`.
         */
        OpenGLES1IndexBufferRenderer(OpenGLES1Renderer* owner, int indexCapacity,
                                    bool thirtyTwoBit = false);
        ~OpenGLES1IndexBufferRenderer() override;

        OpenGLES1IndexBufferRenderer(const OpenGLES1IndexBufferRenderer&) = delete;
        OpenGLES1IndexBufferRenderer& operator=(const OpenGLES1IndexBufferRenderer&) = delete;

        void SetData16(const void* data, int index_count) override;

        /**
         * @brief Uploads 32-bit indices; only valid on a buffer created as 32-bit.
         *
         * @param data Source indices.
         * @param index_count Number of indices to upload.
         */
        void SetData32(const void* data, int index_count) override;

        [[nodiscard]] bool IsThirtyTwoBit() const override { return thirtyTwoBit_; }
        int GetIndexCount() const override { return indexCount_; }

        /**
         * @brief Recreates this buffer's GL object after the context it lived in was destroyed.
         *
         * Re-uploads from the CPU shadow that wireframe emulation already maintains.
         */
        void RestoreAfterContextLoss();

        /// CNAEXT. Binds this buffer's VBO to GL_ELEMENT_ARRAY_BUFFER -- callers then pass a byte
        /// offset (not a raw pointer) as glDrawElements' `indices` argument.
        void Bind() const;
        /// CNAEXT. Releases the raw vertex shadow; the buffer can no longer survive a context loss.
        void DropCpuShadowEXT();

        /// CNAEXT. CPU-side shadow of the index values, for wireframe emulation and context-loss
        /// restore only (see above). Always widened to 32 bits so one accessor serves both index
        /// sizes; the GPU buffer still holds the narrow form for 16-bit buffers.
        [[nodiscard]] const std::vector<uint32_t>& CpuShadow() const { return cpuShadow_; }

    private:
        OpenGLES1Renderer* owner_ = nullptr;
        unsigned int buffer_ = 0;
        int indexCapacity_ = 0;
        int indexCount_ = 0;
        bool thirtyTwoBit_ = false;
        std::vector<uint32_t> cpuShadow_;
    };

    class OpenGLES1Renderer;

    /**
     * @brief CNAEXT. 2D quad batcher built on the fixed-function pipeline.
     *
     * Batches quads into CPU-side arrays exactly like the vertex/index buffers above, and flushes
     * them with a single glDrawElements() call per texture change, under an orthographic
     * GL_PROJECTION matrix (glOrthof) matching XNA's SpriteBatch top-left-origin convention.
     */
    class OpenGLES1SpriteBatchRenderer : public ISpriteBatchRenderer
    {
    public:
        explicit OpenGLES1SpriteBatchRenderer(OpenGLES1Renderer* owner) : owner_(owner) {}

        void Begin() override;
        void End() override;
        void SetTransformMatrix(const Matrix& m) override { transform_ = m; }
        void SetSamplerFilter(int textureFilter) override { pendingFilter_ = textureFilter; }
        void SetSamplerAddressMode(int addressU, int addressV) override
        {
            pendingAddressU_ = addressU;
            pendingAddressV_ = addressV;
        }

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

    private:
        struct Vertex
        {
            float x, y;
            float u, v;
            float r, g, b, a;
        };

        void FlushBatch();

        OpenGLES1Renderer* owner_ = nullptr;
        bool begun_ = false;
        Matrix transform_ = Matrix::getIdentityProperty();
        int pendingFilter_ = 0;
        int pendingAddressU_ = 0;
        int pendingAddressV_ = 0;
        const ITextureRenderer* currentTexture_ = nullptr;
        std::vector<Vertex> pendingVertices_;
        std::vector<uint16_t> pendingIndices_;
    };

    /**
     * @brief CNAEXT. Off-screen render target backed by `GL_OES_framebuffer_object` (a common but
     * optional ES 1.1 extension -- `OpenGLES1Renderer::CreateRenderTarget2D()` returns
     * `nullptr`, matching `IGraphicsRenderer`'s own "renderer that can't support this" contract, on
     * a driver where it isn't present).
     *
     * Depth/stencil is provided by a renderbuffer (`GL_DEPTH_COMPONENT16_OES`, upgraded to
     * `GL_DEPTH_COMPONENT24_OES`/`GL_DEPTH24_STENCIL8_OES` only when the corresponding
     * `GL_OES_depth24`/`GL_OES_packed_depth_stencil` extension is also present -- otherwise this
     * silently falls back to a 16-bit depth-only buffer, a documented deviation, see
     * docs/opengles1-renderer.md). Mip generation and multisampling are not implemented (matches
     * `CreateRenderTarget2D`'s own `mipMap`/`multiSampleCount` parameters being accepted but
     * ignored, same as several other CNA renderers' own current gaps).
     */
    class OpenGLES1RenderTargetRenderer : public IRenderTargetRenderer
    {
    public:
        /**
         * @brief Creates the off-screen colour target and its optional depth/stencil renderbuffer.
         *
         * @param owner Owning renderer, used for the FBO entry points.
         * @param width Target width in pixels.
         * @param height Target height in pixels.
         * @param depthFormat XNA `DepthFormat` ordinal; 0 means no depth buffer.
         * @param mipMap True to allocate a full mip chain and regenerate it whenever the target is
         *        unbound.
         */
        OpenGLES1RenderTargetRenderer(OpenGLES1Renderer* owner, int width, int height,
                                     int depthFormat, bool mipMap = false);
        ~OpenGLES1RenderTargetRenderer() override;

        OpenGLES1RenderTargetRenderer(const OpenGLES1RenderTargetRenderer&) = delete;
        OpenGLES1RenderTargetRenderer& operator=(const OpenGLES1RenderTargetRenderer&) = delete;

        int GetWidth() const override { return width_; }
        int GetHeight() const override { return height_; }

        void BindGL(int /*unit*/) const override;

        void BindAsRenderTarget() override;
        void UnbindAsRenderTarget() override;

        /**
         * @brief Reads raw RGBA8 pixels back out of the target's colour attachment.
         *
         * `RenderTarget2D::GetData()` has no CPU-side pixel shadow to fall back on -- its content
         * only ever comes from GPU rendering -- so without this the call silently yields zeroes.
         * Rows are flipped to the top-left-origin convention every other CNA renderer reports.
         *
         * @param level Mip level; only level 0 exists on this renderer.
         * @param x Left edge of the sub-rectangle to read.
         * @param y Top edge of the sub-rectangle, in top-left-origin coordinates.
         * @param w Width of the sub-rectangle.
         * @param h Height of the sub-rectangle.
         * @param data Destination buffer receiving RGBA8 pixels.
         * @param dataLength Size of that buffer in bytes.
         * @return True when the pixels were read back, false when they could not be.
         */
        [[nodiscard]] bool GetData(int level, int x, int y, int w, int h,
                                   void* data, int dataLength) const override;

        [[nodiscard]] unsigned int GetColorGLHandle() const override { return colorTexture_; }
        [[nodiscard]] bool HasRealDepthBuffer(bool depthFormatWasRequested) const override
        {
            return depthFormatWasRequested && hasDepth_;
        }

    private:
        OpenGLES1Renderer* owner_ = nullptr;
        unsigned int fbo_ = 0;
        unsigned int colorTexture_ = 0;
        unsigned int depthRenderbuffer_ = 0;
        int width_ = 0;
        int height_ = 0;
        bool hasDepth_ = false;
        bool hasStencil_ = false;
        bool mipMap_ = false;
        int levelCount_ = 1;
    };

    /**
     * @brief CNAEXT. Cube-map texture backed by `GL_OES_texture_cube_map` (an optional ES 1.1
     * extension -- `OpenGLES1Renderer::CreateTextureCube()` returns `nullptr` on a driver
     * where it isn't present, same "unsupported, not just unimplemented" contract as the render
     * target above). Used only for `EnvironmentMapEffect`'s reflection map -- see
     * `DrawPrimitivesEx`'s `envMapping` handling and docs/opengles1-renderer.md.
     */
    class OpenGLES1TextureCubeRenderer : public ITextureCubeRenderer
    {
    public:
        OpenGLES1TextureCubeRenderer(OpenGLES1Renderer* owner, int size);
        ~OpenGLES1TextureCubeRenderer() override;

        OpenGLES1TextureCubeRenderer(const OpenGLES1TextureCubeRenderer&) = delete;
        OpenGLES1TextureCubeRenderer& operator=(const OpenGLES1TextureCubeRenderer&) = delete;

        [[nodiscard]] bool SetData(int face, int level, int x, int y, int w, int h,
                                   const void* data, int dataLength) override;

        /**
         * @brief Reads back RGBA8 pixels from a sub-rectangle of one cube face.
         *
         * ES 1.1 has no glGetTexImage, so the face is attached to a scratch framebuffer and read
         * through it -- the same route Texture2D::GetData and the render-target cube already take.
         *
         * @param face Cube face index (0=+X, 1=-X, 2=+Y, 3=-Y, 4=+Z, 5=-Z).
         * @param level Mip level; only level 0 is stored by this renderer.
         * @param x Left edge of the sub-rectangle.
         * @param y Top edge of the sub-rectangle, top-left origin.
         * @param w Sub-rectangle width.
         * @param h Sub-rectangle height.
         * @param data Destination RGBA8 buffer.
         * @param dataLength Size of that buffer in bytes.
         * @return True when the face was read back, false when it could not be.
         */
        [[nodiscard]] bool GetData(int face, int level, int x, int y, int w, int h,
                                   void* data, int dataLength) const override;

        void BindGL(int /*unit*/) const override;

        [[nodiscard]] unsigned int GetGLHandle() const { return texture_; }

    private:
        OpenGLES1Renderer* owner_ = nullptr;
        unsigned int texture_ = 0;
        int size_ = 0;
    };

    /**
     * @brief CNAEXT. Cube-map render target: one GPU cube image whose faces are attached to a
     *        framebuffer one at a time (OPENGLES1-84).
     *
     * Needs both `GL_OES_framebuffer_object` and `GL_OES_texture_cube_map`; `CreateRenderTargetCube`
     * returns `nullptr` when either is missing, the same "unsupported, not merely unimplemented"
     * contract `CreateRenderTarget2D` already follows. Mip generation and multisampling are not
     * implemented for cube targets.
     */
    class OpenGLES1RenderTargetCubeRenderer : public IRenderTargetCubeRenderer
    {
    public:
        /**
         * @brief Creates the cube colour image and the framebuffer used to render into its faces.
         *
         * @param owner Owning renderer, used for the FBO entry points.
         * @param size Edge length of each square face, in pixels.
         * @param depthFormat XNA `DepthFormat` ordinal; 0 means no depth buffer.
         */
        OpenGLES1RenderTargetCubeRenderer(OpenGLES1Renderer* owner, int size, int depthFormat);
        ~OpenGLES1RenderTargetCubeRenderer() override;

        OpenGLES1RenderTargetCubeRenderer(const OpenGLES1RenderTargetCubeRenderer&) = delete;
        OpenGLES1RenderTargetCubeRenderer& operator=(const OpenGLES1RenderTargetCubeRenderer&) = delete;

        [[nodiscard]] int GetSize() const override { return size_; }
        void BindAsRenderTargetFace(int face) override;
        void UnbindAsRenderTarget() override;
        [[nodiscard]] unsigned int GetGLHandle() const override { return cubeTexture_; }
        void BindGL(int /*unit*/) const override;

        /**
         * @brief Reads raw RGBA8 pixels back from one cube face.
         *
         * @param face Cube face index (0=+X, 1=-X, 2=+Y, 3=-Y, 4=+Z, 5=-Z).
         * @param level Mip level; only level 0 exists here.
         * @param x Left edge of the sub-rectangle.
         * @param y Top edge of the sub-rectangle, top-left origin.
         * @param w Sub-rectangle width.
         * @param h Sub-rectangle height.
         * @param data Destination RGBA8 buffer.
         * @param dataLength Size of that buffer in bytes.
         * @return True when the face was read back, false when it could not be.
         */
        [[nodiscard]] bool GetData(int face, int level, int x, int y, int w, int h,
                                   void* data, int dataLength) const override;

    private:
        OpenGLES1Renderer* owner_ = nullptr;
        unsigned int fbo_ = 0;
        unsigned int cubeTexture_ = 0;
        unsigned int depthRenderbuffer_ = 0;
        int size_ = 0;
        bool hasDepth_ = false;
    };

    /**
     * @brief CNAEXT. Graphics renderer built on real OpenGL ES 1.1 (the fixed-function "Common"
     * profile), deliberately independent of the EasyGL renderer (which targets WebGL2/OpenGL ES
     * 3.0 and cannot create an ES 1.1 context at all).
     *
     * There is no programmable-shader path: `CreateEffectRenderer()` keeps the base class's
     * `nullptr` default and every stock XNA effect (`BasicEffect`, `AlphaTestEffect`, ...) is
     * rendered by translating `GpuDrawParams` onto the fixed-function matrix stack, texture
     * environment, and per-vertex lighting/fog state instead of compiling a shader. See
     * `docs/opengles1-renderer.md` for exactly which effects map cleanly onto this pipeline and
     * which do not (skinning, PBR, cube-map environment mapping, and custom `ShaderEffect` all
     * have no fixed-function equivalent and are permanently unsupported by this renderer, not a
     * "not yet implemented" gap).
     */
    class OpenGLES1Renderer : public IGraphicsRenderer
    {
    public:
        explicit OpenGLES1Renderer(const GraphicsRendererCreateArgs& args);
        ~OpenGLES1Renderer() override;

        OpenGLES1Renderer(const OpenGLES1Renderer&) = delete;
        OpenGLES1Renderer& operator=(const OpenGLES1Renderer&) = delete;

        void Clear(float r, float g, float b, float a) override;
        void Present() override;
        void GetViewportSize(int& width, int& height) override;
        void SetVirtualResolution(int width, int height) override;
        void SetPresentationMode(int mode) override;
        void SetSwapInterval(int interval) override;

        bool TransformWindowToLogical(float windowX, float windowY, float& logX, float& logY) const override;
        bool TransformLogicalToWindow(float logX, float logY, float& windowX, float& windowY) const override;


        std::unique_ptr<ITextureRenderer> CreateTexture(const ImageData& data) override;
        std::unique_ptr<ISpriteBatchRenderer> CreateSpriteBatch() override;
        std::unique_ptr<IRenderTargetRenderer> CreateRenderTarget2D(int w, int h, int depthFormat,
                                                                    bool preserveContents = false,
                                                                    bool mipMap = false,
                                                                    int multiSampleCount = 0) override;
        void SetRenderTarget2D(IRenderTargetRenderer* rt) override;

        /**
         * @brief Binds a single render target, or restores the back buffer.
         *
         * ES 1.1 has no multiple-render-target mechanism, so a request for more than one target is
         * refused rather than silently reduced to the first -- `SupportsCapability`
         * (`MultipleRenderTargets`) reports `false` for the same reason.
         *
         * @param renderTargets The bindings to apply; nullptr restores the back buffer.
         * @param count How many bindings @p renderTargets holds; 0 restores the back buffer.
         * @throws System::NotSupportedException When more than one target is requested.
         */
        void SetRenderTargets(const RenderTargetBindingDescriptor* renderTargets,
                              int count) override;
        std::unique_ptr<ITextureCubeRenderer> CreateTextureCube(int size, bool mipMap, int surfaceFormat) override;

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

        std::unique_ptr<IVertexBufferRenderer> CreateVertexBuffer(int vertex_capacity) override;
        std::unique_ptr<IIndexBufferRenderer> CreateIndexBuffer16(int index_capacity) override;

        /**
         * @brief Creates a 32-bit index buffer when `GL_OES_element_index_uint` is available.
         *
         * @param index_capacity Number of indices the buffer must hold.
         * @return A 32-bit buffer, or the base class's 16-bit fallback when the extension is absent.
         */
        std::unique_ptr<IIndexBufferRenderer> CreateIndexBuffer32(int index_capacity) override;

        /**
         * @brief Creates a cube-map render target (OPENGLES1-84).
         *
         * @param size Edge length of each face in pixels.
         * @param depthFormat XNA `DepthFormat` ordinal; 0 means no depth buffer.
         * @param mipMap Ignored -- cube mip generation is not implemented.
         * @param multiSampleCount Ignored -- no framebuffer-multisample extension is available.
         * @return The target, or `nullptr` when the required extensions are missing.
         */
        std::unique_ptr<IRenderTargetCubeRenderer> CreateRenderTargetCube(
            int size, int depthFormat, bool preserveContents = false, bool mipMap = false,
            int multiSampleCount = 0) override;

        void DrawColoredPrimitives(const IVertexBufferRenderer& vb,
                                   const Matrix& world, const Matrix& view, const Matrix& projection,
                                   PrimitiveType primitive, int primitiveCount) override;
        void DrawIndexedColoredPrimitives(const IVertexBufferRenderer& vb, const IIndexBufferRenderer& ib,
                                          const Matrix& world, const Matrix& view, const Matrix& projection,
                                          PrimitiveType primitive, int primitiveCount) override;
        void DrawPrimitivesEx(const IVertexBufferRenderer& vb,
                             const Matrix& world, const Matrix& view, const Matrix& projection,
                             PrimitiveType primitive, int primitiveCount,
                             const GpuDrawParams& params) override;
        void DrawIndexedPrimitivesEx(const IVertexBufferRenderer& vb, const IIndexBufferRenderer& ib,
                                    const Matrix& world, const Matrix& view, const Matrix& projection,
                                    PrimitiveType primitive, int primitiveCount,
                                    const GpuDrawParams& params) override;

        void ApplyBlendState(int colorSrcBlend, int alphaSrcBlend,
                             int colorDstBlend, int alphaDstBlend,
                             int colorBlendFunc, int alphaBlendFunc,
                             const BlendWriteState& writeState) override;
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

        /// CNAEXT. Physical (window) pixel size -- used by the sprite-batch renderer to size its
        /// GL viewport/orthographic projection.
        void GetPhysicalSize(int& width, int& height) const;
        /// CNAEXT. Logical (virtual-resolution) size -- see GetPhysicalSize's own note.
        void GetLogicalSize(int& width, int& height) const;

        /// CNAEXT. Sets the GL viewport to the physical window size and an orthographic
        /// GL_PROJECTION matching the logical (virtual-resolution) size. Called by
        /// OpenGLES1SpriteBatchRenderer::FlushBatch() before each flush, mirroring every other
        /// renderer's SpriteBatch-owns-its-own-2D-projection convention.
        void ApplyLogicalViewportAndOrtho2D();

        /// CNAEXT. Returns the size of the currently-bound OpenGLES1RenderTargetRenderer, if any
        /// (used by ApplyLogicalViewportAndOrtho2D so a SpriteBatch flush while an RT is bound
        /// sizes its projection to the RT, not the window -- mirrors EasyGLRenderer's
        /// identical GetCurrentRenderTarget2DSize()).
        bool GetCurrentRenderTarget2DSize(int& width, int& height) const;

        /// CNAEXT. Whether GL_OES_framebuffer_object was detected at startup (queried once via
        /// glGetString(GL_EXTENSIONS), not re-checked per call).
        [[nodiscard]] bool SupportsFramebufferObject() const { return fboSupported_; }
        /// CNAEXT. Whether GL_OES_texture_cube_map was detected at startup.
        [[nodiscard]] bool SupportsTextureCubeMap() const { return cubeMapSupported_; }

        /// CNAEXT. True when GL_OES_texture_mirrored_repeat is present, i.e. when
        /// TextureAddressMode::Mirror can be honoured instead of degrading to Wrap.
        [[nodiscard]] bool SupportsMirroredRepeat() const { return mirroredRepeatSupported_; }

        /// CNAEXT. True when GL_OES_element_index_uint is present, i.e. when 32-bit index buffers
        /// are real rather than silently narrowed to 16 bits.
        [[nodiscard]] bool SupportsThirtyTwoBitIndicesEXT() const { return elementIndexUintSupported_; }

        /// CNAEXT. True when glGenerateMipmapOES resolved, i.e. when render-target mip chains can be
        /// regenerated.
        [[nodiscard]] bool HasGenerateMipmapEXT() const { return glGenerateMipmapOES_ != nullptr; }

        /// CNAEXT. True when GL_OES_framebuffer_object is usable (see fboSupported_).
        [[nodiscard]] bool HasFramebufferObjectsEXT() const { return fboSupported_; }

        /**
         * @brief Returns the backbuffer's actual multisample count.
         *
         * @return The granted sample count, or 0 when the backbuffer is not multisampled.
         */
        [[nodiscard]] int GetMultiSampleCount() const override { return actualMultiSampleCount_; }

        /** @brief Creates a framebuffer object. @param out Receives the new name. */
        void GenFramebufferEXT(unsigned int* out) const { if (glGenFramebuffersOES_) glGenFramebuffersOES_(1, out); }
        /** @brief Deletes a framebuffer object. @param name Name to delete, then zeroed. */
        void DeleteFramebufferEXT(unsigned int* name) const { if (glDeleteFramebuffersOES_) glDeleteFramebuffersOES_(1, name); }
        /** @brief Binds a framebuffer object. @param name Name to bind; 0 for the backbuffer. */
        void BindFramebufferEXT(unsigned int name) const { if (glBindFramebufferOES_) glBindFramebufferOES_(GL_FRAMEBUFFER_OES, name); }
        /** @brief Attaches a 2D texture as colour attachment 0. @param texture Texture name. */
        void AttachTexture2DEXT(unsigned int texture) const
        {
            if (glFramebufferTexture2DOES_)
                glFramebufferTexture2DOES_(GL_FRAMEBUFFER_OES, GL_COLOR_ATTACHMENT0_OES, GL_TEXTURE_2D, texture, 0);
        }
        /** @brief Creates a renderbuffer. @param out Receives the new name. */
        void GenRenderbufferEXT(unsigned int* out) const { if (glGenRenderbuffersOES_) glGenRenderbuffersOES_(1, out); }
        /** @brief Deletes a renderbuffer. @param name Name to delete, then zeroed. */
        void DeleteRenderbufferEXT(unsigned int* name) const { if (glDeleteRenderbuffersOES_) glDeleteRenderbuffersOES_(1, name); }
        /** @brief Binds a renderbuffer. @param name Name to bind. */
        void BindRenderbufferEXT(unsigned int name) const { if (glBindRenderbufferOES_) glBindRenderbufferOES_(GL_RENDERBUFFER_OES, name); }
        /**
         * @brief Allocates renderbuffer storage.
         * @param internalFormat GL internal format.
         * @param w Width in pixels.
         * @param h Height in pixels.
         */
        void RenderbufferStorageEXT(unsigned int internalFormat, int w, int h) const
        {
            if (glRenderbufferStorageOES_)
                glRenderbufferStorageOES_(GL_RENDERBUFFER_OES, static_cast<GLenum>(internalFormat), w, h);
        }
        /** @brief Attaches a renderbuffer as the depth attachment. @param name Renderbuffer name. */
        void AttachDepthRenderbufferEXT(unsigned int name) const
        {
            if (glFramebufferRenderbufferOES_)
                glFramebufferRenderbufferOES_(GL_FRAMEBUFFER_OES, GL_DEPTH_ATTACHMENT_OES, GL_RENDERBUFFER_OES, name);
        }
        /**
         * @brief Attaches one cube face as colour attachment 0.
         * @param faceTarget GL cube-face target enum.
         * @param texture Cube texture name.
         */
        void AttachCubeFaceEXT(unsigned int faceTarget, unsigned int texture) const
        {
            if (glFramebufferTexture2DOES_)
                glFramebufferTexture2DOES_(GL_FRAMEBUFFER_OES, GL_COLOR_ATTACHMENT0_OES,
                                           static_cast<GLenum>(faceTarget), texture, 0);
        }
        /** @brief Reports whether the bound framebuffer is complete. @return True when complete. */
        [[nodiscard]] bool IsFramebufferCompleteEXT() const
        {
            return glCheckFramebufferStatusOES_
                       ? glCheckFramebufferStatusOES_(GL_FRAMEBUFFER_OES) == GL_FRAMEBUFFER_COMPLETE_OES
                       : false;
        }

        /**
         * @brief Regenerates the bound texture's mip chain from level 0.
         *
         * @param target GL texture target of the bound object.
         */
        void GenerateMipmapEXT(unsigned int target) const
        {
            if (glGenerateMipmapOES_) glGenerateMipmapOES_(static_cast<GLenum>(target));
        }
        /// CNAEXT. Whether a second texture unit is actually available (GL_MAX_TEXTURE_UNITS >= 2),
        /// needed for a real DualTextureEffect dispatch.
        [[nodiscard]] bool SupportsSecondTextureUnit() const { return maxTextureUnits_ >= 2; }

        /**
         * @brief Registers a texture so it can be rebuilt if the GL context is lost.
         *
         * @param texture Texture to track; ignored when null.
         */
        void RegisterTextureEXT(OpenGLES1TextureRenderer* texture);

        /**
         * @brief Stops tracking a texture that is being destroyed.
         *
         * @param texture Texture to forget; ignored when null or not tracked.
         */
        void UnregisterTextureEXT(OpenGLES1TextureRenderer* texture);

        /**
         * @brief Registers a vertex buffer so it can be rebuilt if the GL context is lost.
         *
         * @param buffer Buffer to track; ignored when null.
         */
        /**
         * @brief Re-applies the sampler state remembered for a slot to the currently bound texture.
         *
         * GL keeps filter and wrap mode on the *texture object*, while `GraphicsDevice` pushes
         * sampler state down before a draw binds its textures. Applying it at push time would
         * therefore land it on whatever texture happened to be bound; the draw paths call this
         * immediately after each bind instead.
         *
         * @param slot Sampler slot whose remembered state should be applied.
         * @param target GL texture target of the bound object (`GL_TEXTURE_2D` or
         *        `GL_TEXTURE_CUBE_MAP_OES`).
         */
        void ApplySamplerToBoundTextureEXT(int slot, unsigned int target) const;

        /**
         * @brief Applies anisotropic filtering to the bound texture, clamped to the driver's cap.
         *
         * No-op when `GL_EXT_texture_filter_anisotropic` is absent.
         *
         * @param target GL texture target of the bound object.
         * @param requested Anisotropy level asked for by the sampler state.
         */
        void ApplyAnisotropy(unsigned int target, int requested) const;

        /**
         * @brief Enables or disables retaining CPU copies needed for GL context-loss recovery.
         *
         * Disabling drops the copies this renderer holds for textures and vertex buffers, which is
         * the memory saving the flag exists for -- `Texture2D` releasing its own copy alone would
         * not free anything, since the renderer shares ownership of the same buffer. Index-buffer
         * shadows are kept regardless: wireframe emulation reads them (`OPENGLES1-76`).
         *
         * Re-enabling does not resurrect what was already dropped; only resources created or
         * uploaded afterwards are recoverable again.
         *
         * @param enabled False to release the retained copies and give up context-loss recovery.
         */
        void SetContextRecoveryEnabled(bool enabled) override;

        /// CNAEXT. Whether CPU copies for context-loss recovery are currently retained.
        [[nodiscard]] bool IsContextRecoveryEnabledEXT() const { return contextRecoveryEnabled_; }

        void RegisterVertexBufferEXT(OpenGLES1VertexBufferRenderer* buffer);

        /**
         * @brief Stops tracking a vertex buffer that is being destroyed.
         *
         * @param buffer Buffer to forget; ignored when null or not tracked.
         */
        void UnregisterVertexBufferEXT(OpenGLES1VertexBufferRenderer* buffer);

        /**
         * @brief Registers an index buffer so it can be rebuilt if the GL context is lost.
         *
         * @param buffer Buffer to track; ignored when null.
         */
        void RegisterIndexBufferEXT(OpenGLES1IndexBufferRenderer* buffer);

        /**
         * @brief Stops tracking an index buffer that is being destroyed.
         *
         * @param buffer Buffer to forget; ignored when null or not tracked.
         */
        void UnregisterIndexBufferEXT(OpenGLES1IndexBufferRenderer* buffer);

        // GL_OES_framebuffer_object entry points -- resolved once at startup via
        // SDL_GL_GetProcAddress (see LoadExtensionEntryPoints()), used by
        // OpenGLES1RenderTargetRenderer. Public so that class can call them without this class
        // needing to expose every FBO operation as its own wrapper method.
        PFNGLGENFRAMEBUFFERSOESPROC glGenFramebuffersOES_ = nullptr;
        PFNGLBINDFRAMEBUFFEROESPROC glBindFramebufferOES_ = nullptr;
        PFNGLDELETEFRAMEBUFFERSOESPROC glDeleteFramebuffersOES_ = nullptr;
        PFNGLFRAMEBUFFERTEXTURE2DOESPROC glFramebufferTexture2DOES_ = nullptr;
        PFNGLFRAMEBUFFERRENDERBUFFEROESPROC glFramebufferRenderbufferOES_ = nullptr;
        PFNGLCHECKFRAMEBUFFERSTATUSOESPROC glCheckFramebufferStatusOES_ = nullptr;
        PFNGLGENRENDERBUFFERSOESPROC glGenRenderbuffersOES_ = nullptr;
        PFNGLBINDRENDERBUFFEROESPROC glBindRenderbufferOES_ = nullptr;
        PFNGLDELETERENDERBUFFERSOESPROC glDeleteRenderbuffersOES_ = nullptr;
        PFNGLRENDERBUFFERSTORAGEOESPROC glRenderbufferStorageOES_ = nullptr;

        // GL_OES_texture_cube_map's glTexGeniOES -- drives real GL_REFLECTION_MAP_OES automatic
        // reflection-vector texture-coordinate generation for EnvironmentMapEffect (see
        // DrawPrimitivesEx's envMapping handling).
        PFNGLTEXGENIOESPROC glTexGeniOES_ = nullptr;

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
        // glBlendFunc/default-Add accordingly (documented deviation, see docs/opengles1-renderer.md).
        PFNGLBLENDFUNCSEPARATEOESPROC glBlendFuncSeparateOES_ = nullptr;
        PFNGLBLENDEQUATIONOESPROC glBlendEquationOES_ = nullptr;
        PFNGLBLENDEQUATIONSEPARATEOESPROC glBlendEquationSeparateOES_ = nullptr;
        PFNGLGENERATEMIPMAPOESPROC glGenerateMipmapOES_ = nullptr;

        bool fboSupported_ = false;
        // Every live texture, so DebugRestoreContext() can rebuild them all against the new
        // context. Raw pointers are safe here because each texture unregisters in its destructor.
        // Sampler state as last pushed down by GraphicsDevice, remembered per slot so each draw
        // can re-apply it to the texture it actually binds (see ApplySamplerToBoundTextureEXT).
        static constexpr int kMaxSamplerSlots = 4;
        int samplerFilter_[kMaxSamplerSlots] = {};
        int samplerAddressU_[kMaxSamplerSlots] = {};
        int samplerAddressV_[kMaxSamplerSlots] = {};
        int samplerAnisotropy_[kMaxSamplerSlots] = {};

        std::vector<OpenGLES1TextureRenderer*> liveTextures_;
        std::vector<OpenGLES1VertexBufferRenderer*> liveVertexBuffers_;
        std::vector<OpenGLES1IndexBufferRenderer*> liveIndexBuffers_;
        bool cubeMapSupported_ = false;
        bool mirroredRepeatSupported_ = false;
        bool elementIndexUintSupported_ = false;
        bool blendMinMaxSupported_ = false;
        // Requested vs. granted backbuffer MSAA (OPENGLES1-87). Only the granted count is ever
        // reported outwards; a driver that silently ignores the request must not be believed.
        int requestedMultiSampleCount_ = 1;
        int actualMultiSampleCount_ = 0;
        // 1x1 white texture used only as a carrier for the GL_COMBINE stage that multiplies vertex
        // colour by DiffuseColor (OPENGLES1-92) -- the texture itself contributes nothing.
        unsigned int whiteTexture_ = 0;
        bool contextRecoveryEnabled_ = true;
        float maxAnisotropy_ = 1.0f;   // 1.0 means GL_EXT_texture_filter_anisotropic is absent
        int maxTextureUnits_ = 1;

        // Wireframe emulation (OPENGLES1-76): FillMode::WireFrame has no glPolygonMode
        // equivalent in ES 1.1 -- Draw*() re-expands each triangle into GL_LINES when set,
        // mirroring EasyGLRenderer::DrawWireframe's identical technique.
        bool wireframe_ = false;

        IRenderTargetRenderer* currentRenderTarget_ = nullptr;
        IRenderTargetCubeRenderer* currentRenderTargetCube_ = nullptr;
    };
}
