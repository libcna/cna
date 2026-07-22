// SPDX-License-Identifier: MS-PL
#pragma once

#include "CNA/Internal/Backends/Common/IGraphicsBackend.hpp"

#include <GLES/gl.h>
#include <GLES/glext.h>

#include <cstdint>
#include <memory>
#include <vector>

struct SDL_Window;

namespace CNA::Internal::Backends::OpenGLES1
{
    class OpenGLES1GraphicsBackend;
    class OpenGLES1TextureBackend;
    class OpenGLES1VertexBufferBackend;
    class OpenGLES1IndexBufferBackend;

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
        OpenGLES1TextureBackend(OpenGLES1GraphicsBackend* owner, int width, int height);
        ~OpenGLES1TextureBackend() override;

        OpenGLES1TextureBackend(const OpenGLES1TextureBackend&) = delete;
        OpenGLES1TextureBackend& operator=(const OpenGLES1TextureBackend&) = delete;

        int GetWidth() const override { return width_; }
        int GetHeight() const override { return height_; }
        SDL_Texture* GetNativeTexture() const override { return nullptr; }

        void UpdatePixels(const uint8_t* rgba, int stride) override;
        void UpdatePixelsLevel(int level, const uint8_t* rgba, int levelW, int levelH) override;
        void BindGL() const override;

        /**
         * @brief Adopts `Texture2D`'s own CPU pixel buffer so the texture can be rebuilt after a
         *        GL context loss.
         *
         * Sharing rather than duplicating matches the interface's stated intent and keeps a single
         * copy of the pixels between `Texture2D` and this backend.
         *
         * @param pixels Shared RGBA8 pixel storage owned by `Texture2D`.
         */
        void ShareCpuPixels(std::shared_ptr<std::vector<uint8_t>> pixels) override;

        /**
         * @brief Recreates this texture's GL object after the context it lived in was destroyed.
         *
         * The old GL name dies with the old context, so it is regenerated, its default sampler
         * state reapplied, and its pixels re-uploaded from the shared CPU copy. Without this a
         * restored context samples every pre-loss texture as plain white.
         */
        void RestoreAfterContextLoss();

        [[nodiscard]] unsigned int GetGLHandle() const { return texture_; }

    private:
        OpenGLES1GraphicsBackend* owner_ = nullptr;
        unsigned int texture_ = 0;
        int width_ = 0;
        int height_ = 0;
        std::shared_ptr<std::vector<uint8_t>> pixels_;
    };

    /**
     * @brief NOXNA. Real GPU-side vertex buffer object for OpenGL ES 1.1.
     *
     * `glGenBuffers`/`glBindBuffer`/`glBufferData` are core OpenGL ES 1.1 entry points (the
     * `GL_OES_vertex_buffer_object` extension was folded into the 1.1 core spec, unlike the
     * separately-optional `GL_OES_framebuffer_object`/`GL_OES_texture_cube_map` this backend also
     * uses elsewhere) -- so a real VBO is always available, no runtime extension check needed.
     * `glVertexPointer`/`glColorPointer`/`glTexCoordPointer`/`glNormalPointer` take byte offsets
     * into the currently-bound `GL_ARRAY_BUFFER` instead of raw client pointers once one is bound.
     */
    class OpenGLES1VertexBufferBackend : public IVertexBufferBackend
    {
    public:
        OpenGLES1VertexBufferBackend(OpenGLES1GraphicsBackend* owner, int vertexCapacity);
        ~OpenGLES1VertexBufferBackend() override;

        OpenGLES1VertexBufferBackend(const OpenGLES1VertexBufferBackend&) = delete;
        OpenGLES1VertexBufferBackend& operator=(const OpenGLES1VertexBufferBackend&) = delete;

        void SetData(const void* data, int vertex_count, std::size_t stride_in_bytes) override;
        int GetVertexCount() const override { return vertexCount_; }

        /**
         * @brief Recreates this buffer's GL object after the context it lived in was destroyed.
         *
         * Re-uploads from the CPU shadow kept by `SetData()`; without it a restored context draws
         * from a dead buffer name and renders nothing.
         */
        void RestoreAfterContextLoss();

        /// NOXNA. Binds this buffer's VBO to GL_ARRAY_BUFFER -- callers then use byte offsets
        /// (not raw pointers) with glVertexPointer/glColorPointer/glTexCoordPointer/glNormalPointer.
        void Bind() const;
        [[nodiscard]] std::size_t Stride() const { return stride_; }

    private:
        OpenGLES1GraphicsBackend* owner_ = nullptr;
        unsigned int buffer_ = 0;
        int vertexCapacity_ = 0;
        int vertexCount_ = 0;
        std::size_t stride_ = 0;
        // Raw vertex bytes, kept solely so the GPU buffer can be rebuilt after a context loss
        // (OPENGLES1-80) -- draws always read from the GPU buffer, never from here.
        std::vector<uint8_t> cpuShadow_;
    };

    /**
     * @brief NOXNA. Real GPU-side 16-bit index buffer object for OpenGL ES 1.1 (see vertex buffer
     * above).
     *
     * Also keeps a small CPU-side shadow copy of the raw index values -- needed only for
     * wireframe emulation (OPENGLES1-76): re-expanding a triangle draw into `GL_LINES` requires
     * reading back which vertices form each triangle, which a GPU-only buffer can't answer
     * without a synchronous readback. Mirrors `EasyGLVertexBufferBackend`/
     * `EasyGLIndexBufferBackend`'s own identical `GetCpuBytes()`-backed
     * `EasyGLGraphicsBackend::DrawWireframe()` technique.
     */
    class OpenGLES1IndexBufferBackend : public IIndexBufferBackend
    {
    public:
        OpenGLES1IndexBufferBackend(OpenGLES1GraphicsBackend* owner, int indexCapacity);
        ~OpenGLES1IndexBufferBackend() override;

        OpenGLES1IndexBufferBackend(const OpenGLES1IndexBufferBackend&) = delete;
        OpenGLES1IndexBufferBackend& operator=(const OpenGLES1IndexBufferBackend&) = delete;

        void SetData16(const void* data, int index_count) override;
        int GetIndexCount() const override { return indexCount_; }

        /**
         * @brief Recreates this buffer's GL object after the context it lived in was destroyed.
         *
         * Re-uploads from the CPU shadow that wireframe emulation already maintains.
         */
        void RestoreAfterContextLoss();

        /// NOXNA. Binds this buffer's VBO to GL_ELEMENT_ARRAY_BUFFER -- callers then pass a byte
        /// offset (not a raw pointer) as glDrawElements' `indices` argument.
        void Bind() const;
        /// NOXNA. CPU-side shadow of the index values, for wireframe emulation only (see above).
        [[nodiscard]] const std::vector<uint16_t>& CpuShadow() const { return cpuShadow_; }

    private:
        OpenGLES1GraphicsBackend* owner_ = nullptr;
        unsigned int buffer_ = 0;
        int indexCapacity_ = 0;
        int indexCount_ = 0;
        std::vector<uint16_t> cpuShadow_;
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
     * @brief NOXNA. Off-screen render target backed by `GL_OES_framebuffer_object` (a common but
     * optional ES 1.1 extension -- `OpenGLES1GraphicsBackend::CreateRenderTarget2D()` returns
     * `nullptr`, matching `IGraphicsBackend`'s own "backend that can't support this" contract, on
     * a driver where it isn't present).
     *
     * Depth/stencil is provided by a renderbuffer (`GL_DEPTH_COMPONENT16_OES`, upgraded to
     * `GL_DEPTH_COMPONENT24_OES`/`GL_DEPTH24_STENCIL8_OES` only when the corresponding
     * `GL_OES_depth24`/`GL_OES_packed_depth_stencil` extension is also present -- otherwise this
     * silently falls back to a 16-bit depth-only buffer, a documented deviation, see
     * docs/opengles1-backend.md). Mip generation and multisampling are not implemented (matches
     * `CreateRenderTarget2D`'s own `mipMap`/`multiSampleCount` parameters being accepted but
     * ignored, same as several other CNA backends' own current gaps).
     */
    class OpenGLES1RenderTargetBackend : public IRenderTargetBackend
    {
    public:
        OpenGLES1RenderTargetBackend(OpenGLES1GraphicsBackend* owner, int width, int height, int depthFormat);
        ~OpenGLES1RenderTargetBackend() override;

        OpenGLES1RenderTargetBackend(const OpenGLES1RenderTargetBackend&) = delete;
        OpenGLES1RenderTargetBackend& operator=(const OpenGLES1RenderTargetBackend&) = delete;

        int GetWidth() const override { return width_; }
        int GetHeight() const override { return height_; }
        SDL_Texture* GetNativeTexture() const override { return nullptr; }
        void BindGL() const override;

        void BindAsRenderTarget() override;
        void UnbindAsRenderTarget() override;

        /**
         * @brief Reads raw RGBA8 pixels back out of the target's colour attachment.
         *
         * `RenderTarget2D::GetData()` has no CPU-side pixel shadow to fall back on -- its content
         * only ever comes from GPU rendering -- so without this the call silently yields zeroes.
         * Rows are flipped to the top-left-origin convention every other CNA backend reports.
         *
         * @param level Mip level; only level 0 exists on this backend.
         * @param x Left edge of the sub-rectangle to read.
         * @param y Top edge of the sub-rectangle, in top-left-origin coordinates.
         * @param w Width of the sub-rectangle.
         * @param h Height of the sub-rectangle.
         * @param data Destination buffer receiving RGBA8 pixels.
         * @param dataLength Size of that buffer in bytes.
         */
        void GetData(int level, int x, int y, int w, int h,
                     void* data, int dataLength) const override;

        [[nodiscard]] unsigned int GetColorGLHandle() const override { return colorTexture_; }
        [[nodiscard]] bool HasRealDepthBuffer(bool depthFormatWasRequested) const override
        {
            return depthFormatWasRequested && hasDepth_;
        }

    private:
        OpenGLES1GraphicsBackend* owner_ = nullptr;
        unsigned int fbo_ = 0;
        unsigned int colorTexture_ = 0;
        unsigned int depthRenderbuffer_ = 0;
        int width_ = 0;
        int height_ = 0;
        bool hasDepth_ = false;
        bool hasStencil_ = false;
    };

    /**
     * @brief NOXNA. Cube-map texture backed by `GL_OES_texture_cube_map` (an optional ES 1.1
     * extension -- `OpenGLES1GraphicsBackend::CreateTextureCube()` returns `nullptr` on a driver
     * where it isn't present, same "unsupported, not just unimplemented" contract as the render
     * target above). Used only for `EnvironmentMapEffect`'s reflection map -- see
     * `DrawPrimitivesEx`'s `envMapping` handling and docs/opengles1-backend.md.
     */
    class OpenGLES1TextureCubeBackend : public ITextureCubeBackend
    {
    public:
        explicit OpenGLES1TextureCubeBackend(int size);
        ~OpenGLES1TextureCubeBackend() override;

        OpenGLES1TextureCubeBackend(const OpenGLES1TextureCubeBackend&) = delete;
        OpenGLES1TextureCubeBackend& operator=(const OpenGLES1TextureCubeBackend&) = delete;

        void SetData(int face, int level, int x, int y, int w, int h,
                     const void* data, int dataLength) override;
        void BindGL() const override;

        [[nodiscard]] unsigned int GetGLHandle() const { return texture_; }

    private:
        unsigned int texture_ = 0;
        int size_ = 0;
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
        std::unique_ptr<IRenderTargetBackend> CreateRenderTarget2D(int w, int h, int depthFormat,
                                                                    bool preserveContents = false,
                                                                    bool mipMap = false,
                                                                    int multiSampleCount = 0) override;
        void SetRenderTarget2D(IRenderTargetBackend* rt) override;
        std::unique_ptr<ITextureCubeBackend> CreateTextureCube(int size, bool mipMap, int surfaceFormat) override;

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

        /// NOXNA. Returns the size of the currently-bound OpenGLES1RenderTargetBackend, if any
        /// (used by ApplyLogicalViewportAndOrtho2D so a SpriteBatch flush while an RT is bound
        /// sizes its projection to the RT, not the window -- mirrors EasyGLGraphicsBackend's
        /// identical GetCurrentRenderTarget2DSize()).
        bool GetCurrentRenderTarget2DSize(int& width, int& height) const;

        /// NOXNA. Whether GL_OES_framebuffer_object was detected at startup (queried once via
        /// glGetString(GL_EXTENSIONS), not re-checked per call).
        [[nodiscard]] bool SupportsFramebufferObject() const { return fboSupported_; }
        /// NOXNA. Whether GL_OES_texture_cube_map was detected at startup.
        [[nodiscard]] bool SupportsTextureCubeMap() const { return cubeMapSupported_; }

        /// NOXNA. True when GL_OES_texture_mirrored_repeat is present, i.e. when
        /// TextureAddressMode::Mirror can be honoured instead of degrading to Wrap.
        [[nodiscard]] bool SupportsMirroredRepeat() const { return mirroredRepeatSupported_; }
        /// NOXNA. Whether a second texture unit is actually available (GL_MAX_TEXTURE_UNITS >= 2),
        /// needed for a real DualTextureEffect dispatch.
        [[nodiscard]] bool SupportsSecondTextureUnit() const { return maxTextureUnits_ >= 2; }

        /**
         * @brief Registers a texture so it can be rebuilt if the GL context is lost.
         *
         * @param texture Texture to track; ignored when null.
         */
        void RegisterTextureEXT(OpenGLES1TextureBackend* texture);

        /**
         * @brief Stops tracking a texture that is being destroyed.
         *
         * @param texture Texture to forget; ignored when null or not tracked.
         */
        void UnregisterTextureEXT(OpenGLES1TextureBackend* texture);

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

        void RegisterVertexBufferEXT(OpenGLES1VertexBufferBackend* buffer);

        /**
         * @brief Stops tracking a vertex buffer that is being destroyed.
         *
         * @param buffer Buffer to forget; ignored when null or not tracked.
         */
        void UnregisterVertexBufferEXT(OpenGLES1VertexBufferBackend* buffer);

        /**
         * @brief Registers an index buffer so it can be rebuilt if the GL context is lost.
         *
         * @param buffer Buffer to track; ignored when null.
         */
        void RegisterIndexBufferEXT(OpenGLES1IndexBufferBackend* buffer);

        /**
         * @brief Stops tracking an index buffer that is being destroyed.
         *
         * @param buffer Buffer to forget; ignored when null or not tracked.
         */
        void UnregisterIndexBufferEXT(OpenGLES1IndexBufferBackend* buffer);

        // GL_OES_framebuffer_object entry points -- resolved once at startup via
        // SDL_GL_GetProcAddress (see LoadExtensionEntryPoints()), used by
        // OpenGLES1RenderTargetBackend. Public so that class can call them without this class
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
        // glBlendFunc/default-Add accordingly (documented deviation, see docs/opengles1-backend.md).
        PFNGLBLENDFUNCSEPARATEOESPROC glBlendFuncSeparateOES_ = nullptr;
        PFNGLBLENDEQUATIONOESPROC glBlendEquationOES_ = nullptr;

        bool fboSupported_ = false;
        // Every live texture, so DebugRestoreContext() can rebuild them all against the new
        // context. Raw pointers are safe here because each texture unregisters in its destructor.
        // Sampler state as last pushed down by GraphicsDevice, remembered per slot so each draw
        // can re-apply it to the texture it actually binds (see ApplySamplerToBoundTextureEXT).
        static constexpr int kMaxSamplerSlots = 4;
        int samplerFilter_[kMaxSamplerSlots] = {};
        int samplerAddressU_[kMaxSamplerSlots] = {};
        int samplerAddressV_[kMaxSamplerSlots] = {};

        std::vector<OpenGLES1TextureBackend*> liveTextures_;
        std::vector<OpenGLES1VertexBufferBackend*> liveVertexBuffers_;
        std::vector<OpenGLES1IndexBufferBackend*> liveIndexBuffers_;
        bool cubeMapSupported_ = false;
        bool mirroredRepeatSupported_ = false;
        int maxTextureUnits_ = 1;

        // Wireframe emulation (OPENGLES1-76): FillMode::WireFrame has no glPolygonMode
        // equivalent in ES 1.1 -- Draw*() re-expands each triangle into GL_LINES when set,
        // mirroring EasyGLGraphicsBackend::DrawWireframe's identical technique.
        bool wireframe_ = false;

        IRenderTargetBackend* currentRenderTarget_ = nullptr;
    };
}
