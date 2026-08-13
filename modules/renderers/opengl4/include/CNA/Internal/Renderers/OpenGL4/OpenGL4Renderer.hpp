// SPDX-License-Identifier: MS-PL
#pragma once

#include "CNA/CNAHelper.hpp"
#include "CNA/Internal/Renderers/Common/IGraphicsRenderer.hpp"
#include "CNA/Internal/Renderers/Common/PlatformGlRendererState.hpp"
#include "CNA/Internal/Renderers/OpenGL4/GL4Loader.hpp"
#include "CNA/Internal/Graphics/VertexDeclarationFidelity.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace CNA::Internal::Renderers::OpenGL4
{
    class OpenGL4Renderer;

    /**
     * @brief plan_opengl4.md GL4-1: real desktop OpenGL 4.x core-profile graphics renderer.
     *
     * Deliberately independent of the EasyGL renderer/`easy-gl` sibling repository -- EasyGL
     * requests an OpenGL ES 3.0 context (see
     * `EasyGLRenderer`'s constructor), which is a different, narrower feature set than a
     * genuine desktop core-profile context (no geometry/tessellation shaders, no
     * `GL_ARB_*` desktop-only extensions, no compatibility with `glGetString(GL_VERSION)` ever
     * reporting "4.x"). This renderer requests a real core profile and never touches `easy-gl`.
     *
     * Uses a small hand-rolled loader (GL4Loader.hpp) for the GL 1.2+ entry points a core-profile
     * program needs (buffers, VAOs, shaders/programs, `glActiveTexture`, separate blend
     * funcs/equations) -- no third-party GL loader dependency.
     */
    class OpenGL4RawProgram
    {
    public:
        OpenGL4RawProgram() = default;
        ~OpenGL4RawProgram();

        OpenGL4RawProgram(const OpenGL4RawProgram&) = delete;
        OpenGL4RawProgram& operator=(const OpenGL4RawProgram&) = delete;
        OpenGL4RawProgram(OpenGL4RawProgram&&) noexcept;
        OpenGL4RawProgram& operator=(OpenGL4RawProgram&&) noexcept;

        /// Compiles and links @p vertSrc/@p fragSrc. Returns true on success.
        bool Compile(const std::string& vertSrc, const std::string& fragSrc);
        void Use() const;
        [[nodiscard]] bool IsValid() const { return program_ != 0; }
        [[nodiscard]] const std::string& GetError() const { return error_; }
        [[nodiscard]] int UniformLocation(const char* name) const;
        [[nodiscard]] unsigned int Handle() const { return program_; }

    private:
        void Destroy();

        unsigned int program_ = 0;
        std::string error_;
    };

    /** @brief `OpenGL4`-backed `Texture2D`/`RenderTarget2D`-independent plain texture. */
    class OpenGL4TextureRenderer final : public ITextureRenderer
    {
    public:
        explicit OpenGL4TextureRenderer(const ImageData& data);
        ~OpenGL4TextureRenderer() override;

        OpenGL4TextureRenderer(const OpenGL4TextureRenderer&) = delete;
        OpenGL4TextureRenderer& operator=(const OpenGL4TextureRenderer&) = delete;

        [[nodiscard]] int GetWidth() const override { return width_; }
        [[nodiscard]] int GetHeight() const override { return height_; }

        void UpdatePixels(const uint8_t* rgba, int stride) override;
        /// plan_opengl4.md GL4-18: uploads mip level @p level (allocating its storage via
        /// glTexImage2D, since only level 0 is allocated at construction -- matches
        /// EasyGLTextureRenderer::UpdatePixelsLevel's identical "caller supplies every level
        /// explicitly" contract; this renderer does not auto-generate mips for plain textures).
        void UpdatePixelsLevel(int level, const uint8_t* rgba, int levelW, int levelH) override;
        void BindGL(int /*unit*/) const override;

        CNAEXT [[nodiscard]] unsigned int GLHandle() const { return texture_; }

    private:
        unsigned int texture_ = 0;
        int width_ = 0;
        int height_ = 0;
        /// plan_opengl4.md GL4-18: real ImageData::mipLevels count this texture was created
        /// with -- GL_TEXTURE_MAX_LEVEL is clamped to levelCount_-1 so a mipmap-requiring
        /// TextureFilter (e.g. Anisotropic) doesn't treat this as an incomplete mipmap chain
        /// (GL's own default max level is 1000), even for an ordinary single-level texture that
        /// never uploads anything beyond level 0.
        int levelCount_ = 1;
    };

    /**
     * @brief `OpenGL4`-backed `RenderTarget2D`: a real FBO with a colour texture attachment,
     * an optional depth/stencil renderbuffer, an optional multisampled colour renderbuffer
     * resolved into the colour texture on unbind, and an optional mip chain regenerated from
     * level 0 on unbind -- modeled directly on `EasyGLRenderTargetRenderer`'s own resource shape
     * (plan_opengl4.md GL4-14), using raw `GL4Loader` calls instead of the `easygl::` wrapper
     * types this renderer deliberately avoids depending on.
     *
     * Doubles as both the render target AND the texture later sampled from it (same object,
     * same GL texture handle, via `ITextureRenderer::BindGL()`) -- matches how `RenderTarget2D`'s
     * C++ constructor stores this single renderer instance as its own `Texture2D::renderer_`.
     */
    class OpenGL4RenderTargetRenderer final : public IRenderTargetRenderer
    {
    public:
        OpenGL4RenderTargetRenderer(int w, int h, int depthFormat, bool mipMap, int multiSampleCount);
        ~OpenGL4RenderTargetRenderer() override;

        OpenGL4RenderTargetRenderer(const OpenGL4RenderTargetRenderer&) = delete;
        OpenGL4RenderTargetRenderer& operator=(const OpenGL4RenderTargetRenderer&) = delete;

        [[nodiscard]] int GetWidth() const override { return width_; }
        [[nodiscard]] int GetHeight() const override { return height_; }

        void BindGL(int /*unit*/) const override;
        [[nodiscard]] bool GetData(int level, int x, int y, int w, int h, void* data, int dataLength) const override;

        void BindAsRenderTarget() override;
        void UnbindAsRenderTarget() override;
        [[nodiscard]] unsigned int GetColorGLHandle() const override { return colorTexture_; }
        [[nodiscard]] int GetMultiSampleCount() const override { return multiSampleCount_; }

    private:
        void CreateResources();
        void DestroyResources();

        unsigned int fbo_ = 0;
        unsigned int resolveFbo_ = 0;              ///< MSAA only: blit destination (colour = colorTexture_).
        unsigned int colorTexture_ = 0;
        unsigned int depthRenderbuffer_ = 0;
        unsigned int msaaColorRenderbuffer_ = 0;    ///< MSAA only: real draw target (fbo_'s own colour attachment).
        int width_ = 0;
        int height_ = 0;
        int depthFormat_ = 0;                       ///< Raw Microsoft::Xna::Framework::Graphics::DepthFormat ordinal.
        bool mipMap_ = false;
        int levelCount_ = 1;
        int multiSampleCount_ = 0;
    };

    /**
     * @brief `OpenGL4`-backed `RenderTargetCube`: one FBO shared across all 6 faces of a single
     * cube-map texture, re-attaching the requested face on `BindAsRenderTargetFace` -- modeled
     * directly on `EasyGLRenderTargetCubeRenderer`'s own resource shape (plan_opengl4.md
     * `GL4-15`), using raw `GL4Loader` calls instead of the `easygl::` wrapper types.
     */
    class OpenGL4RenderTargetCubeRenderer final : public IRenderTargetCubeRenderer
    {
    public:
        OpenGL4RenderTargetCubeRenderer(int size, int depthFormat, bool mipMap, int multiSampleCount);
        ~OpenGL4RenderTargetCubeRenderer() override;

        OpenGL4RenderTargetCubeRenderer(const OpenGL4RenderTargetCubeRenderer&) = delete;
        OpenGL4RenderTargetCubeRenderer& operator=(const OpenGL4RenderTargetCubeRenderer&) = delete;

        [[nodiscard]] bool SetData(int face, int level, int x, int y, int w, int h,
                                   const void* data, int dataLength) override;
        [[nodiscard]] bool GetData(int face, int level, int x, int y, int w, int h,
                                   void* data, int dataLength) const override;
        void BindGL(int /*unit*/) const override;

        [[nodiscard]] int GetSize() const override { return size_; }
        void BindAsRenderTargetFace(int face) override;
        void UnbindAsRenderTarget() override;
        [[nodiscard]] unsigned int GetGLHandle() const override { return cubeTexture_; }
        [[nodiscard]] int GetMultiSampleCount() const override { return multiSampleCount_; }

    private:
        void CreateResources();
        void DestroyResources();

        unsigned int fbo_ = 0;
        unsigned int resolveFbo_ = 0;              ///< MSAA only: blit destination (re-attached per face).
        unsigned int cubeTexture_ = 0;
        unsigned int depthRenderbuffer_ = 0;
        unsigned int msaaColorRenderbuffer_ = 0;    ///< MSAA only: shared across all 6 faces (one bound at a time).
        int size_ = 0;
        int depthFormat_ = 0;                       ///< Raw Microsoft::Xna::Framework::Graphics::DepthFormat ordinal.
        bool mipMap_ = false;
        int levelCount_ = 1;
        int multiSampleCount_ = 0;
        int lastFace_ = 0;                          ///< Most recently bound face, used by UnbindAsRenderTarget's resolve.
    };

    /**
     * @brief `OpenGL4`-backed plain (non-render-target) `Texture3D` (volume texture) --
     * modeled on `EasyGLTexture3DRenderer`'s own resource shape (plan_opengl4.md `GL4-20`), using
     * raw `GL4Loader` calls (`gl4_glTexImage3D`/`gl4_glTexSubImage3D`) instead of the `easygl::`
     * wrapper types this renderer deliberately avoids depending on.
     */
    class OpenGL4Texture3DRenderer final : public ITexture3DRenderer
    {
    public:
        OpenGL4Texture3DRenderer(int w, int h, int depth, bool mipMap);
        ~OpenGL4Texture3DRenderer() override;

        OpenGL4Texture3DRenderer(const OpenGL4Texture3DRenderer&) = delete;
        OpenGL4Texture3DRenderer& operator=(const OpenGL4Texture3DRenderer&) = delete;

        [[nodiscard]] bool SetData(int level, int x, int y, int z, int w, int h, int depth,
                                   const void* data, int dataLength) override;
        [[nodiscard]] bool GetData(int level, int x, int y, int z, int w, int h, int depth,
                                   void* data, int dataLength) const override;
        void BindGL(int /*unit*/) const override;

    private:
        unsigned int texture_ = 0;
        int width_ = 0;
        int height_ = 0;
        int depth_ = 0;
        int levelCount_ = 1;
    };

    /**
     * @brief `OpenGL4`-backed plain (non-render-target) `TextureCube` -- modeled on
     * `EasyGLTextureCubeRenderer`'s own resource shape (plan_opengl4.md `GL4-20`). Unlike
     * `OpenGL4RenderTargetCubeRenderer::GetData` (which Y-flips because it reads back a
     * framebuffer-origin render target), this plain texture's `GetData` does not flip Y --
     * matches `EasyGLTextureCubeRenderer::GetData`'s own non-render-target convention, verified by
     * a real pixel round-trip in `OpenGL4_TextureCube`.
     */
    class OpenGL4TextureCubeRenderer final : public ITextureCubeRenderer
    {
    public:
        OpenGL4TextureCubeRenderer(int size, bool mipMap);
        ~OpenGL4TextureCubeRenderer() override;

        OpenGL4TextureCubeRenderer(const OpenGL4TextureCubeRenderer&) = delete;
        OpenGL4TextureCubeRenderer& operator=(const OpenGL4TextureCubeRenderer&) = delete;

        [[nodiscard]] bool SetData(int face, int level, int x, int y, int w, int h,
                                   const void* data, int dataLength) override;
        [[nodiscard]] bool GetData(int face, int level, int x, int y, int w, int h,
                                   void* data, int dataLength) const override;
        void BindGL(int /*unit*/) const override;

    private:
        unsigned int texture_ = 0;
        int size_ = 0;
        int levelCount_ = 1;
    };

    /**
     * @brief `OpenGL4`-backed custom `ShaderEffect` program (plan_opengl4.md `GL4-30`) -- wraps a
     * caller-supplied GLSL 410 core vertex+fragment source pair, modeled on
     * `EasyGLEffectRenderer`'s own shape (a thin `IEffectRenderer` wrapper around one compiled
     * program). Uses `OpenGL4RawProgram` internally, the same compiled-program type every
     * built-in stride-dispatched shader on this renderer already uses.
     */
    class OpenGL4EffectRenderer final : public IEffectRenderer
    {
    public:
        OpenGL4EffectRenderer() = default;
        ~OpenGL4EffectRenderer() override = default;

        OpenGL4EffectRenderer(const OpenGL4EffectRenderer&) = delete;
        OpenGL4EffectRenderer& operator=(const OpenGL4EffectRenderer&) = delete;

        bool CompileProgram(const std::string& vertSrc, const std::string& fragSrc) override;
        void Bind() override;
        void Unbind() override;
        [[nodiscard]] bool IsValid() const override;
        [[nodiscard]] std::string GetCompileError() const override;
        void SetUniformFloat(const char* name, float value) override;
        void SetUniformInt(const char* name, int value) override;
        void SetUniformVec2(const char* name, float x, float y) override;
        void SetUniformVec3(const char* name, float x, float y, float z) override;
        void SetUniformVec4(const char* name, float x, float y, float z, float w) override;
        void SetUniformMat4(const char* name, const float* matrix) override;
        void SetUniformFloatArray(const char* name, const float* values, int count) override;
        void SetUniformVec2Array(const char* name, const float* values, int count) override;
        void BindTexture(int unit, ITextureRenderer* texture) override;
        void BindTextureCube(int unit, ITextureCubeRenderer* texture) override;
        void BindTexture3D(int unit, ITexture3DRenderer* texture) override;

        /// Returns the underlying compiled program, so a renderer (e.g. SpriteBatch) can bind
        /// the SAME program this ShaderEffect's SetUniformXxx() calls actually write to.
        [[nodiscard]] OpenGL4RawProgram& GetProgram() { return program_; }

    private:
        OpenGL4RawProgram program_;
    };

    /**
     * @brief `OpenGL4`-backed occlusion query -- a real GL 1.5 core `GL_SAMPLES_PASSED` query
     * object (plan_opengl4.md `GL4-24`), unlike `EasyGLOcclusionQueryRenderer`'s `GLES3`
     * `GL_ANY_SAMPLES_PASSED` (0/1-only) query -- desktop GL reports an exact passed-sample
     * count, matching real XNA's own desktop `OcclusionQuery.PixelCount()` semantics (see
     * `IOcclusionQueryRenderer`'s own doc comment contrasting the two).
     */
    class OpenGL4OcclusionQueryRenderer final : public IOcclusionQueryRenderer
    {
    public:
        OpenGL4OcclusionQueryRenderer();
        ~OpenGL4OcclusionQueryRenderer() override;

        OpenGL4OcclusionQueryRenderer(const OpenGL4OcclusionQueryRenderer&) = delete;
        OpenGL4OcclusionQueryRenderer& operator=(const OpenGL4OcclusionQueryRenderer&) = delete;

        void Begin() override;
        void End() override;
        [[nodiscard]] bool IsComplete() const override;
        [[nodiscard]] int PixelCount() const override;

    private:
        unsigned int query_ = 0;
        mutable bool resultCached_ = false;
        mutable int cachedResult_ = 0;
    };

    /** @brief `OpenGL4`-backed vertex buffer (VBO + its own VAO). */
    class OpenGL4VertexBufferRenderer final : public IVertexBufferRenderer
    {
    public:
        explicit OpenGL4VertexBufferRenderer(int vertexCapacity);
        ~OpenGL4VertexBufferRenderer() override;

        OpenGL4VertexBufferRenderer(const OpenGL4VertexBufferRenderer&) = delete;
        OpenGL4VertexBufferRenderer& operator=(const OpenGL4VertexBufferRenderer&) = delete;

        void SetData(const void* data, int vertex_count, std::size_t stride_in_bytes) override;
        void SetDataWithOptions(const void* data, int vertex_count, std::size_t stride_in_bytes,
                                SetDataOptions options) override;
        [[nodiscard]] int GetVertexCount() const override { return vertexCount_; }
        /// plan_opengl4.md GL4-33: supplies a caller-owned VertexDeclaration so ApplyLayout() can
        /// bind genuinely custom vertex layouts generically (attribute location = the element's
        /// own index within the declaration), instead of only the fixed byte-strides the switch
        /// in ApplyLayout() otherwise recognizes -- needed by hardware instancing's per-instance
        /// attribute buffer, which never matches one of those fixed strides. Mirrors
        /// EasyGLVertexBufferRenderer::SetVertexDeclaration's own shape. Pure in the current
        /// IGraphicsRenderer precisely so a renderer must make this decision explicitly; the stored
        /// declaration also feeds the REMED-GFX-DECL-GUARD fidelity check at draw time.
        void SetVertexDeclaration(const VertexDeclaration& vertexDeclaration) override;

        CNAEXT [[nodiscard]] unsigned int VaoHandle() const { return vao_; }
        CNAEXT [[nodiscard]] unsigned int VboHandle() const { return vbo_; }
        CNAEXT [[nodiscard]] std::size_t GetStrideInBytes() const { return strideInBytes_; }
        /// plan_opengl4.md GL4-33: the declaration set via SetVertexDeclaration(), or empty if
        /// none was ever supplied (this buffer uses the fixed stride-keyed layout instead).
        CNAEXT [[nodiscard]] const std::vector<VertexElement>& GetDeclarationElements() const
        {
            return declaration_.GetElements();
        }
        /// REMED-GFX-DECL-GUARD: the remembered declaration for the draw-time fidelity check.
        CNAEXT [[nodiscard]] const CNA::Internal::Graphics::DeclaredVertexLayout&
        GetDeclarationEXT() const { return declaration_; }

    private:
        void ApplyLayout(std::size_t strideInBytes);

        unsigned int vao_ = 0;
        unsigned int vbo_ = 0;
        int capacity_ = 0;
        int vertexCount_ = 0;
        std::size_t strideInBytes_ = 0;
        CNA::Internal::Graphics::DeclaredVertexLayout declaration_;
    };

    /**
     * @brief Refuses a stock-program draw whose VertexDeclaration this renderer cannot bind
     *        faithfully.
     *
     * REMED-GFX-DECL-GUARD. BindProgramForStride()/ApplyLayout() pick this renderer's native
     * attribute layout from the buffer's byte stride alone, so a declaration that packs different
     * semantics into the same stride would be read from the wrong bytes. The check is asymmetric:
     * only what the caller actually declared is verified, never equality against this renderer's
     * own template. Draws through a custom ShaderEffect are deliberately not guarded here -- their
     * attributes are bound generically from the declaration itself (GL4-33), which is faithful by
     * construction, matching EasyGLRenderer's own custom-effect gating.
     *
     * @param vb The vertex buffer whose declaration is being checked.
     * @param route Name of the draw route, for the diagnostic message.
     * @throws System::NotSupportedException When the declaration cannot be represented.
     */
    inline void RequireFaithfulDeclarationEXT(const IVertexBufferRenderer& vb, const char* route)
    {
        const auto& glVb = static_cast<const OpenGL4VertexBufferRenderer&>(vb);
        CNA::Internal::Graphics::RequireFaithfulVertexDeclaration(
            glVb.GetDeclarationEXT(), static_cast<int>(glVb.GetStrideInBytes()),
            CNA::Internal::Graphics::UnlistedStrideLayout::PositionOnlyFallback,
            "OpenGL4", route);
    }

    /**
     * @brief `OpenGL4`-backed index buffer -- 16-bit by default, real 32-bit support
     * (plan_opengl4.md `GL4-31`) when constructed with `thirtyTwoBit=true` (`GL_UNSIGNED_INT`
     * storage/draw-call index type instead of `GL_UNSIGNED_SHORT`).
     */
    class OpenGL4IndexBufferRenderer final : public IIndexBufferRenderer
    {
    public:
        explicit OpenGL4IndexBufferRenderer(int indexCapacity, bool thirtyTwoBit = false);
        ~OpenGL4IndexBufferRenderer() override;

        OpenGL4IndexBufferRenderer(const OpenGL4IndexBufferRenderer&) = delete;
        OpenGL4IndexBufferRenderer& operator=(const OpenGL4IndexBufferRenderer&) = delete;

        void SetData16(const void* data, int index_count) override;
        void SetData16WithOptions(const void* data, int index_count, SetDataOptions options) override;
        /// plan_opengl4.md GL4-31: real 32-bit index upload (GL_UNSIGNED_INT storage).
        void SetData32(const void* data, int index_count) override;
        void SetData32WithOptions(const void* data, int index_count, SetDataOptions options) override;
        [[nodiscard]] int GetIndexCount() const override { return indexCount_; }
        [[nodiscard]] bool IsThirtyTwoBit() const override { return thirtyTwoBit_; }

        CNAEXT [[nodiscard]] unsigned int IboHandle() const { return ibo_; }

    private:
        unsigned int ibo_ = 0;
        int capacity_ = 0;
        int indexCount_ = 0;
        bool thirtyTwoBit_ = false;
    };

    /**
     * @brief `OpenGL4`-backed `SpriteBatch`. CPU-side per-quad vertex generation (position already
     * transformed by rotation/origin/scale, matching `EasyGLSpriteBatchRenderer`'s established
     * convention), one dynamic VBO/IBO flushed per texture change -- same non-instanced-batching
     * bar as this project's other renderers' first-landed `SpriteBatch` milestone.
     */
    class OpenGL4SpriteBatchRenderer final : public ISpriteBatchRenderer
    {
    public:
        explicit OpenGL4SpriteBatchRenderer(OpenGL4Renderer& owner);
        ~OpenGL4SpriteBatchRenderer() override;

        void Begin() override;
        void End() override;
        void SetTransformMatrix(const Matrix& m) override { transform_ = m; }
        /// plan_opengl4.md GL4-32: lets a custom Microsoft::Xna::Framework::Graphics::ShaderEffect
        /// (CNAEXT) drive 2D SpriteBatch rendering instead of the built-in sprite program, mirroring
        /// EasyGLRenderer::SetCustomEffect's own shape.
        void SetCustomEffect(Effect* effect) override;
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
        struct SpriteVertex
        {
            float x, y;
            float u, v;
            float r, g, b, a;
        };

        void FlushBatch();

        OpenGL4Renderer* owner_ = nullptr;
        unsigned int vao_ = 0;
        unsigned int vbo_ = 0;
        unsigned int ibo_ = 0;
        bool begun_ = false;
        Matrix transform_;
        int pendingFilter_ = 0;
        int pendingAddressU_ = 1; // TextureAddressMode::Clamp
        int pendingAddressV_ = 1;
        /// plan_opengl4.md GL4-32: non-owning; set via SetCustomEffect(), matching
        /// EasyGLRenderer::customEffect_'s own lifetime convention (caller-owned).
        Effect* customEffect_ = nullptr;
        const ITextureRenderer* currentTexture_ = nullptr;
        std::vector<SpriteVertex> pendingVertices_;
        std::vector<uint16_t> pendingIndices_;
    };

    /** @brief Real desktop OpenGL 4.x core-profile `IGraphicsRenderer` implementation. */
    class OpenGL4Renderer final : public IGraphicsRenderer
    {
    public:
        explicit OpenGL4Renderer(const GraphicsRendererCreateArgs& args);
        ~OpenGL4Renderer() override;

        OpenGL4Renderer(const OpenGL4Renderer&) = delete;
        OpenGL4Renderer& operator=(const OpenGL4Renderer&) = delete;

        void Clear(float r, float g, float b, float a) override;
        void Present() override;
        void GetViewportSize(int& width, int& height) override;
        void OnSurfaceChanged(const RendererSurfaceInfo& surface) override;
        void SetVirtualResolution(int width, int height) override;
        void SetPresentationMode(int mode) override;
        void SetSwapInterval(int interval) override;

        /// plan_opengl4.md GL4-28: real physical<->logical coordinate mapping for
        /// Mouse/touch, matching EasyGLRenderer's own pure-uniform-scale (no offset)
        /// formula -- exact for this renderer's own default FixedHeightDynamicWidth presentation,
        /// where the logical viewport always fills the whole physical window (no letterbox bars).
        bool TransformWindowToLogical(float windowX, float windowY,
                                      float& logX, float& logY) const override;
        bool TransformLogicalToWindow(float logX, float logY,
                                      float& windowX, float& windowY) const override;

        /// plan_opengl4.md GL4-17: real window/backbuffer MSAA -- a manually-managed multisample
        /// FBO (mirroring EasyGLRenderer's own msaaFbo_/CreateMsaaBuffers/ResolveMsaa
        /// approach) rather than a multisampled window pixel format
        /// pixel format, since the latter can't be resolved through our own controlled
        /// glBlitFramebuffer call and would fight this renderer's existing Y-flip/ReadBackbuffer
        /// conventions. Fixed at construction time; ApplyMultiSampleCount is not overridden
        /// (inherited no-op default -- same documented "no way to change after construction"
        /// limitation EasyGLRenderer already has).
        [[nodiscard]] int GetMultiSampleCount() const override { return msaaSampleCount_; }


        /**
         * @brief Answers every current GraphicsCapability member explicitly.
         *
         * The base default answers `true` for almost everything, which is how a renderer forked
         * before an enum member existed ends up claiming it (the exact defect the `opengles1`
         * lane found in its own inherited default). The switch below has no default case, so a
         * future member surfaces as a compiler warning here instead of a confident wrong answer.
         */
        [[nodiscard]] bool SupportsCapability(CNA::GraphicsCapability capability) const override;

        std::unique_ptr<ITextureRenderer> CreateTexture(const ImageData& data) override;
        std::unique_ptr<ISpriteBatchRenderer> CreateSpriteBatch() override;

        /// plan_opengl4.md GL4-20: plain (non-render-target) Texture3D/TextureCube.
        std::unique_ptr<ITexture3DRenderer> CreateTexture3D(int w, int h, int depth, bool mipMap,
                                                            int surfaceFormat) override;
        std::unique_ptr<ITextureCubeRenderer> CreateTextureCube(int size, bool mipMap,
                                                                int surfaceFormat) override;

        /// plan_opengl4.md GL4-24: real GL_SAMPLES_PASSED occlusion queries.
        std::unique_ptr<IOcclusionQueryRenderer> CreateOcclusionQuery() override;

        /// plan_opengl4.md GL4-30: compiles a caller-supplied GLSL 410 core vertex+fragment
        /// source pair for a custom Microsoft::Xna::Framework::Graphics::ShaderEffect (CNAEXT).
        std::unique_ptr<IEffectRenderer> CreateEffectRenderer(const std::string& vertSrc,
                                                             const std::string& fragSrc) override;

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
        /// plan_opengl4.md GL4-31: real 32-bit index buffer support (GL_UNSIGNED_INT).
        std::unique_ptr<IIndexBufferRenderer> CreateIndexBuffer32(int index_capacity) override;

        void DrawColoredPrimitives(const IVertexBufferRenderer& vb,
                                   const Matrix& world, const Matrix& view, const Matrix& projection,
                                   PrimitiveType primitive, int primitiveCount) override;
        void DrawIndexedColoredPrimitives(const IVertexBufferRenderer& vb, const IIndexBufferRenderer& ib,
                                          const Matrix& world, const Matrix& view, const Matrix& projection,
                                          PrimitiveType primitive, int primitiveCount) override;

        /// plan_opengl4.md GL4-13: dispatches by vertex stride to the colored/textured/
        /// colored-textured/lit-textured shader family, matching VulkanRenderer's/
        /// SdlGpuRenderer's own stride-keyed pipeline dispatch pattern.
        void DrawPrimitivesEx(const IVertexBufferRenderer& vb,
                              const Matrix& world, const Matrix& view, const Matrix& projection,
                              PrimitiveType primitive, int primitiveCount,
                              const GpuDrawParams& params) override;
        void DrawIndexedPrimitivesEx(const IVertexBufferRenderer& vb, const IIndexBufferRenderer& ib,
                                     const Matrix& world, const Matrix& view, const Matrix& projection,
                                     PrimitiveType primitive, int primitiveCount,
                                     const GpuDrawParams& params) override;
        /// plan_opengl4.md GL4-33: real hardware instancing (glDrawElementsInstanced). With a
        /// custom ShaderEffect (params.customEffectRenderer), also binds params.instanceVb's own
        /// attributes generically (via its VertexDeclaration) at locations continuing right after
        /// the mesh buffer's own, each with glVertexAttribDivisor(location, 1) -- matches
        /// EasyGLRenderer::DrawInstancedPrimitivesEx's own Task 1082 shape.
        void DrawInstancedPrimitivesEx(const IVertexBufferRenderer& vb, const IIndexBufferRenderer& ib,
                                       const Matrix& world, const Matrix& view, const Matrix& projection,
                                       PrimitiveType primitive, int primitiveCount, int instanceCount,
                                       const GpuDrawParams& params) override;

        void ApplySamplerState(int slot, int filter, int addressU, int addressV, int maxAnisotropy) override;
        void SetViewport(int x, int y, int w, int h, float minDepth, float maxDepth) override;

        /// plan_opengl4.md GL4-16: real dynamic BlendState/DepthStencilState/RasterizerState
        /// mapping. REMED-GFX-077: the appended BlendWriteState carries the four per-MRT-slot
        /// ColorWriteChannels masks (applied via the GL 3.0+ core glColorMaski) and the
        /// MultiSampleMask (a documented capability gap on the GL profile, same as EasyGL's).
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
        void SetBlendFactor(float r, float g, float b, float a) override;
        void SetScissorRect(int x, int y, int w, int h) override;

        /// plan_opengl4.md GL4-14: real FBO-backed RenderTarget2D.
        std::unique_ptr<IRenderTargetRenderer> CreateRenderTarget2D(int w, int h, int depthFormat,
                                                                    bool preserveContents = false,
                                                                    bool mipMap = false,
                                                                    int multiSampleCount = 0) override;
        void SetRenderTarget2D(IRenderTargetRenderer* rt) override;

        /// plan_opengl4.md GL4-15: real per-face FBO-backed RenderTargetCube + real MRT.
        /// REMED-GFX-136: preserveContents is consumed by being deliberately unused for the
        /// single-sample case -- a GL FBO's colour attachment IS the cube texture and binding an
        /// FBO never touches its contents, so a face is preserved by construction; the only thing
        /// that clears one is the explicit glClear GraphicsDevice issues for a DiscardContents
        /// target.
        std::unique_ptr<IRenderTargetCubeRenderer> CreateRenderTargetCube(int size, int depthFormat,
                                                                          bool preserveContents = false,
                                                                          bool mipMap = false,
                                                                          int multiSampleCount = 0) override;
        void SetRenderTargetCubeFace(IRenderTargetCubeRenderer* rt, int face) override;
        /// Descriptor-based multi-target binding: routes a single descriptor to the 2D or
        /// cube-face setter (cube-face descriptors are explicitly consumed, never flattened), and
        /// refuses cube faces inside a multi-target set rather than binding a subset.
        void SetRenderTargets(const RenderTargetBindingDescriptor* renderTargets,
                              int count) override;

        /// CNAEXT: physical window size, used by the SpriteBatch renderer to size its ortho projection.
        CNAEXT void GetPhysicalSize(int& width, int& height) const;
        /// CNAEXT: logical (virtual) size, honoring FixedHeightDynamicWidth (mirrors EasyGL).
        CNAEXT void GetLogicalSize(int& width, int& height) const;
        /// CNAEXT: compiles (once) and returns the built-in textured-quad sprite program.
        CNAEXT OpenGL4RawProgram& GetOrCreateSpriteProgram();
        /// CNAEXT: reports the currently-bound RenderTarget2D's size (mirrors
        /// EasyGLRenderer::GetCurrentRenderTarget2DSize). Returns false (leaving width/
        /// height untouched) when no render target is currently bound.
        CNAEXT [[nodiscard]] bool GetCurrentRenderTarget2DSize(int& width, int& height) const;

    private:
        void EnsureColored3DProgram();
        /// plan_opengl4.md GL4-25: GpuDrawParams-aware stride-16 program (DiffuseColor/
        /// VertexColorEnabled/AlphaTest/fog), a real BindProgramForStride case -- separate from
        /// the params-free colored3DProgram_ above, which stays reserved for
        /// DrawColoredPrimitives/DrawIndexedColoredPrimitives's own fast path.
        void EnsureColoredParams3DProgram();
        void EnsureTextured3DProgram();
        void EnsureColoredTextured3DProgram();
        void EnsureLitTextured3DProgram();
        /// plan_opengl4.md GL4-29: real XNA's BasicEffect defaults PreferPerPixelLighting=false --
        /// per-vertex/Gouraud-shaded lighting, the opposite of litTextured3DProgram_ above (which
        /// is the PreferPerPixelLighting=true family). Selected by BindProgramForStride instead of
        /// litTextured3DProgram_ when params.lightingEnabled && !params.preferPerPixelLighting.
        void EnsureLitTextured3DVertexLitProgram();
        /// plan_opengl4.md GL4-21: EnvironmentMapEffect's own dedicated stride-32 program,
        /// selected instead of litTextured3DProgram_ when GpuDrawParams::envMapping is set.
        void EnsureEnvMap3DProgram();
        /// plan_opengl4.md GL4-22: SkinnedEffect's own dedicated stride-52/56 program.
        void EnsureSkinned3DProgram();
        /// plan_opengl4.md GL4-29: SkinnedEffect's own per-vertex-lit sibling of
        /// skinned3DProgram_ above -- real XNA's SkinnedEffect also defaults
        /// PreferPerPixelLighting=false. Selected instead of skinned3DProgram_ when
        /// params.lightingEnabled && !params.preferPerPixelLighting.
        void EnsureSkinned3DVertexLitProgram();
        /// plan_opengl4.md GL4-23: PbrEffect's own dedicated stride-48 program (glTF
        /// metallic-roughness BRDF).
        void EnsurePbr3DProgram();
        /// plan_opengl4.md GL4-23: SkinnedPbrEffect's own dedicated stride-68 program (PBR BRDF +
        /// bone skinning combined).
        void EnsurePbrSkinned3DProgram();
        /// plan_opengl4.md GL4-23: lazily-created 1x1 opaque white fallback, bound to
        /// PbrEffect's MetallicRoughnessMap/EmissiveMap/OcclusionMap texture units whenever the
        /// corresponding GpuDrawParams::pbr*Map pointer is null (the PBR fragment shader samples
        /// all 5 texture units unconditionally, unlike DualTextureEffect/EnvironmentMapEffect's
        /// uniform-gated optional samplers -- an unbound/stale unit would otherwise sample
        /// whatever GL texture a previous, unrelated draw last left bound to that unit).
        void EnsureDefaultWhiteTexture();
        /// plan_opengl4.md GL4-23: lazily-created 1x1 tangent-space "flat" normal (128,128,255 ->
        /// decodes to (0,0,1)) fallback for PbrEffect's NormalMap when unset.
        void EnsureDefaultFlatNormalTexture();

        /// Binds the correct stride-keyed program for @p strideInBytes, uploads its uniforms
        /// from @p world/@p view/@p projection/@p params, and binds texture unit 0 if
        /// params.texture0 is set. Returns false (caller should fall back to
        /// DrawColoredPrimitives/DrawIndexedColoredPrimitives) for an unrecognized stride.
        bool BindProgramForStride(std::size_t strideInBytes, const Matrix& world, const Matrix& view,
                                  const Matrix& projection, const GpuDrawParams& params);

        /// plan_opengl4.md GL4-17: (re)allocates the manual backbuffer MSAA FBO's colour+depth
        /// renderbuffers at the given physical size, mirroring
        /// EasyGLRenderer::CreateMsaaBuffers.
        void CreateMsaaBuffers(int w, int h);
        /// Binds the default framebuffer for drawing -- FBO 0 when backbuffer MSAA is off, the
        /// manual MSAA FBO (recreated first if the window was resized) otherwise. Mirrors
        /// EasyGLRenderer::BindDefaultFramebuffer.
        void BindDefaultFramebufferOrMsaa();
        /// Blits the manual MSAA FBO's colour attachment into FBO 0 (mirrors
        /// EasyGLRenderer::ResolveMsaa). No-op when backbuffer MSAA is off.
        void ResolveMsaa();

        static constexpr int kMaxSamplerSlots = 16;

        // Declared before every GL resource-owning member so it outlives those resources.
        std::unique_ptr<PlatformGlContextOwner> platformContext_;
        PlatformGlSurfaceState surface_;
        int virtualWidth_ = 0;
        int virtualHeight_ = 0;
        CnaPresentationMode presentationMode_ = CnaPresentationMode::FixedHeightDynamicWidth;
        int swapInterval_ = 1;
        bool depthWriteEnabled_ = true;

        /// plan_opengl4.md GL4-14: currently-bound RenderTarget2D (nullptr = default back
        /// buffer). currentRtHeight_ is the render target's own height when one is bound, 0
        /// otherwise -- SetViewport's bottom-left-to-top-left Y flip is computed from this
        /// instead of the window's physical height whenever a target is bound (mirrors
        /// EasyGLRenderer::currentRtHeight_'s identical role).
        IRenderTargetRenderer* currentRt2D_ = nullptr;
        int currentRtHeight_ = 0;
        /// plan_opengl4.md GL4-15: currently-bound RenderTargetCube face (nullptr = none).
        IRenderTargetCubeRenderer* currentRtCube_ = nullptr;
        /// plan_opengl4.md GL4-15: lazily-created, persistent FBO reused across every
        /// SetRenderTargets(count > 1) MRT call (mirrors EasyGLRenderer::mrtFbo_).
        unsigned int mrtFbo_ = 0;

        /// plan_opengl4.md GL4-17: manual backbuffer MSAA FBO (0 = disabled). Sized to the
        /// window's physical size; recreated on resize by BindDefaultFramebufferOrMsaa.
        unsigned int msaaFbo_ = 0;
        unsigned int msaaColorRbo_ = 0;
        unsigned int msaaDepthRbo_ = 0;
        int msaaSampleCount_ = 0;
        int msaaW_ = 0;
        int msaaH_ = 0;
        /// Driver-granted anisotropic-filtering ceiling, queried once at context creation;
        /// stays 1.0 when EXT/ARB_texture_filter_anisotropic is absent (core only in GL 4.6).
        float maxAnisotropy_ = 1.0f;

        OpenGL4RawProgram spriteProgram_;
        OpenGL4RawProgram colored3DProgram_;
        int colored3DWvpLoc_ = -1;
        OpenGL4RawProgram coloredParams3DProgram_;
        OpenGL4RawProgram textured3DProgram_;
        OpenGL4RawProgram coloredTextured3DProgram_;
        OpenGL4RawProgram litTextured3DProgram_;
        /// plan_opengl4.md GL4-29: per-vertex-lit sibling of litTextured3DProgram_ above.
        OpenGL4RawProgram litTextured3DVertexLitProgram_;
        OpenGL4RawProgram envMap3DProgram_;
        OpenGL4RawProgram skinned3DProgram_;
        /// plan_opengl4.md GL4-29: per-vertex-lit sibling of skinned3DProgram_ above.
        OpenGL4RawProgram skinned3DVertexLitProgram_;
        OpenGL4RawProgram pbr3DProgram_;
        OpenGL4RawProgram pbrSkinned3DProgram_;
        unsigned int defaultWhiteTexture_ = 0;
        unsigned int defaultFlatNormalTexture_ = 0;
        unsigned int samplers_[kMaxSamplerSlots] = {};
    };
}
