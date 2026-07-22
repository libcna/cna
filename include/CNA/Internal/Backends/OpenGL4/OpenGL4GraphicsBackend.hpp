// SPDX-License-Identifier: MS-PL
#pragma once

#include "CNA/CNAHelper.hpp"
#include "../Common/IGraphicsBackend.hpp"
#include "CNA/Internal/Backends/OpenGL4/GL4Loader.hpp"

#include <cstdint>
#include <string>
#include <vector>

struct SDL_Window;

namespace CNA::Internal::Backends::OpenGL4
{
    class OpenGL4GraphicsBackend;

    /**
     * @brief plan_opengl4.md GL4-1: real desktop OpenGL 4.x core-profile graphics backend.
     *
     * Deliberately independent of the EasyGL backend/`easy-gl` sibling repository -- EasyGL
     * requests an OpenGL ES 3.0 context (`SDL_GL_CONTEXT_PROFILE_ES`, see
     * `EasyGLGraphicsBackend`'s constructor), which is a different, narrower feature set than a
     * genuine desktop `SDL_GL_CONTEXT_PROFILE_CORE` context (no geometry/tessellation shaders, no
     * `GL_ARB_*` desktop-only extensions, no compatibility with `glGetString(GL_VERSION)` ever
     * reporting "4.x"). This backend requests a real core profile and never touches `easy-gl`.
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
    class OpenGL4TextureBackend final : public ITextureBackend
    {
    public:
        explicit OpenGL4TextureBackend(const ImageData& data);
        ~OpenGL4TextureBackend() override;

        OpenGL4TextureBackend(const OpenGL4TextureBackend&) = delete;
        OpenGL4TextureBackend& operator=(const OpenGL4TextureBackend&) = delete;

        [[nodiscard]] int GetWidth() const override { return width_; }
        [[nodiscard]] int GetHeight() const override { return height_; }
        [[nodiscard]] SDL_Texture* GetNativeTexture() const override { return nullptr; }
        void UpdatePixels(const uint8_t* rgba, int stride) override;
        /// plan_opengl4.md GL4-18: uploads mip level @p level (allocating its storage via
        /// glTexImage2D, since only level 0 is allocated at construction -- matches
        /// EasyGLTextureBackend::UpdatePixelsLevel's identical "caller supplies every level
        /// explicitly" contract; this backend does not auto-generate mips for plain textures).
        void UpdatePixelsLevel(int level, const uint8_t* rgba, int levelW, int levelH) override;
        void BindGL() const override;

        NOXNA [[nodiscard]] unsigned int GLHandle() const { return texture_; }

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
     * level 0 on unbind -- modeled directly on `EasyGLRenderTargetBackend`'s own resource shape
     * (plan_opengl4.md GL4-14), using raw `GL4Loader` calls instead of the `easygl::` wrapper
     * types this backend deliberately avoids depending on.
     *
     * Doubles as both the render target AND the texture later sampled from it (same object,
     * same GL texture handle, via `ITextureBackend::BindGL()`) -- matches how `RenderTarget2D`'s
     * C++ constructor stores this single backend instance as its own `Texture2D::backend_`.
     */
    class OpenGL4RenderTargetBackend final : public IRenderTargetBackend
    {
    public:
        OpenGL4RenderTargetBackend(int w, int h, int depthFormat, bool mipMap, int multiSampleCount);
        ~OpenGL4RenderTargetBackend() override;

        OpenGL4RenderTargetBackend(const OpenGL4RenderTargetBackend&) = delete;
        OpenGL4RenderTargetBackend& operator=(const OpenGL4RenderTargetBackend&) = delete;

        [[nodiscard]] int GetWidth() const override { return width_; }
        [[nodiscard]] int GetHeight() const override { return height_; }
        [[nodiscard]] SDL_Texture* GetNativeTexture() const override { return nullptr; }
        void BindGL() const override;
        void GetData(int level, int x, int y, int w, int h, void* data, int dataLength) const override;

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
     * directly on `EasyGLRenderTargetCubeBackend`'s own resource shape (plan_opengl4.md
     * `GL4-15`), using raw `GL4Loader` calls instead of the `easygl::` wrapper types.
     */
    class OpenGL4RenderTargetCubeBackend final : public IRenderTargetCubeBackend
    {
    public:
        OpenGL4RenderTargetCubeBackend(int size, int depthFormat, bool mipMap, int multiSampleCount);
        ~OpenGL4RenderTargetCubeBackend() override;

        OpenGL4RenderTargetCubeBackend(const OpenGL4RenderTargetCubeBackend&) = delete;
        OpenGL4RenderTargetCubeBackend& operator=(const OpenGL4RenderTargetCubeBackend&) = delete;

        void SetData(int face, int level, int x, int y, int w, int h,
                    const void* data, int dataLength) override;
        void GetData(int face, int level, int x, int y, int w, int h,
                    void* data, int dataLength) const override;
        void BindGL() const override;

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
     * modeled on `EasyGLTexture3DBackend`'s own resource shape (plan_opengl4.md `GL4-20`), using
     * raw `GL4Loader` calls (`gl4_glTexImage3D`/`gl4_glTexSubImage3D`) instead of the `easygl::`
     * wrapper types this backend deliberately avoids depending on.
     */
    class OpenGL4Texture3DBackend final : public ITexture3DBackend
    {
    public:
        OpenGL4Texture3DBackend(int w, int h, int depth, bool mipMap);
        ~OpenGL4Texture3DBackend() override;

        OpenGL4Texture3DBackend(const OpenGL4Texture3DBackend&) = delete;
        OpenGL4Texture3DBackend& operator=(const OpenGL4Texture3DBackend&) = delete;

        void SetData(int level, int x, int y, int z, int w, int h, int depth,
                    const void* data, int dataLength) override;
        void GetData(int level, int x, int y, int z, int w, int h, int depth,
                    void* data, int dataLength) const override;
        void BindGL() const override;

    private:
        unsigned int texture_ = 0;
        int width_ = 0;
        int height_ = 0;
        int depth_ = 0;
        int levelCount_ = 1;
    };

    /**
     * @brief `OpenGL4`-backed plain (non-render-target) `TextureCube` -- modeled on
     * `EasyGLTextureCubeBackend`'s own resource shape (plan_opengl4.md `GL4-20`). Unlike
     * `OpenGL4RenderTargetCubeBackend::GetData` (which Y-flips because it reads back a
     * framebuffer-origin render target), this plain texture's `GetData` does not flip Y --
     * matches `EasyGLTextureCubeBackend::GetData`'s own non-render-target convention, verified by
     * a real pixel round-trip in `OpenGL4_TextureCube`.
     */
    class OpenGL4TextureCubeBackend final : public ITextureCubeBackend
    {
    public:
        OpenGL4TextureCubeBackend(int size, bool mipMap);
        ~OpenGL4TextureCubeBackend() override;

        OpenGL4TextureCubeBackend(const OpenGL4TextureCubeBackend&) = delete;
        OpenGL4TextureCubeBackend& operator=(const OpenGL4TextureCubeBackend&) = delete;

        void SetData(int face, int level, int x, int y, int w, int h,
                    const void* data, int dataLength) override;
        void GetData(int face, int level, int x, int y, int w, int h,
                    void* data, int dataLength) const override;
        void BindGL() const override;

    private:
        unsigned int texture_ = 0;
        int size_ = 0;
        int levelCount_ = 1;
    };

    /**
     * @brief `OpenGL4`-backed occlusion query -- a real GL 1.5 core `GL_SAMPLES_PASSED` query
     * object (plan_opengl4.md `GL4-24`), unlike `EasyGLOcclusionQueryBackend`'s `GLES3`
     * `GL_ANY_SAMPLES_PASSED` (0/1-only) query -- desktop GL reports an exact passed-sample
     * count, matching real XNA's own desktop `OcclusionQuery.PixelCount()` semantics (see
     * `IOcclusionQueryBackend`'s own doc comment contrasting the two).
     */
    class OpenGL4OcclusionQueryBackend final : public IOcclusionQueryBackend
    {
    public:
        OpenGL4OcclusionQueryBackend();
        ~OpenGL4OcclusionQueryBackend() override;

        OpenGL4OcclusionQueryBackend(const OpenGL4OcclusionQueryBackend&) = delete;
        OpenGL4OcclusionQueryBackend& operator=(const OpenGL4OcclusionQueryBackend&) = delete;

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
    class OpenGL4VertexBufferBackend final : public IVertexBufferBackend
    {
    public:
        explicit OpenGL4VertexBufferBackend(int vertexCapacity);
        ~OpenGL4VertexBufferBackend() override;

        OpenGL4VertexBufferBackend(const OpenGL4VertexBufferBackend&) = delete;
        OpenGL4VertexBufferBackend& operator=(const OpenGL4VertexBufferBackend&) = delete;

        void SetData(const void* data, int vertex_count, std::size_t stride_in_bytes) override;
        void SetDataWithOptions(const void* data, int vertex_count, std::size_t stride_in_bytes,
                                SetDataOptions options) override;
        [[nodiscard]] int GetVertexCount() const override { return vertexCount_; }

        NOXNA [[nodiscard]] unsigned int VaoHandle() const { return vao_; }
        NOXNA [[nodiscard]] unsigned int VboHandle() const { return vbo_; }
        NOXNA [[nodiscard]] std::size_t GetStrideInBytes() const { return strideInBytes_; }

    private:
        void ApplyLayout(std::size_t strideInBytes);

        unsigned int vao_ = 0;
        unsigned int vbo_ = 0;
        int capacity_ = 0;
        int vertexCount_ = 0;
        std::size_t strideInBytes_ = 0;
    };

    /** @brief `OpenGL4`-backed 16-bit index buffer. */
    class OpenGL4IndexBufferBackend final : public IIndexBufferBackend
    {
    public:
        explicit OpenGL4IndexBufferBackend(int indexCapacity);
        ~OpenGL4IndexBufferBackend() override;

        OpenGL4IndexBufferBackend(const OpenGL4IndexBufferBackend&) = delete;
        OpenGL4IndexBufferBackend& operator=(const OpenGL4IndexBufferBackend&) = delete;

        void SetData16(const void* data, int index_count) override;
        void SetData16WithOptions(const void* data, int index_count, SetDataOptions options) override;
        [[nodiscard]] int GetIndexCount() const override { return indexCount_; }
        [[nodiscard]] bool IsThirtyTwoBit() const override { return false; }

        NOXNA [[nodiscard]] unsigned int IboHandle() const { return ibo_; }

    private:
        unsigned int ibo_ = 0;
        int capacity_ = 0;
        int indexCount_ = 0;
    };

    /**
     * @brief `OpenGL4`-backed `SpriteBatch`. CPU-side per-quad vertex generation (position already
     * transformed by rotation/origin/scale, matching `EasyGLSpriteBatchBackend`'s established
     * convention), one dynamic VBO/IBO flushed per texture change -- same non-instanced-batching
     * bar as this project's other backends' first-landed `SpriteBatch` milestone.
     */
    class OpenGL4SpriteBatchBackend final : public ISpriteBatchBackend
    {
    public:
        explicit OpenGL4SpriteBatchBackend(OpenGL4GraphicsBackend& owner);
        ~OpenGL4SpriteBatchBackend() override;

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
        struct SpriteVertex
        {
            float x, y;
            float u, v;
            float r, g, b, a;
        };

        void FlushBatch();

        OpenGL4GraphicsBackend* owner_ = nullptr;
        unsigned int vao_ = 0;
        unsigned int vbo_ = 0;
        unsigned int ibo_ = 0;
        bool begun_ = false;
        Matrix transform_;
        int pendingFilter_ = 0;
        int pendingAddressU_ = 1; // TextureAddressMode::Clamp
        int pendingAddressV_ = 1;
        const ITextureBackend* currentTexture_ = nullptr;
        std::vector<SpriteVertex> pendingVertices_;
        std::vector<uint16_t> pendingIndices_;
    };

    struct GraphicsBackendCreateArgs;

    /** @brief Real desktop OpenGL 4.x core-profile `IGraphicsBackend` implementation. */
    class OpenGL4GraphicsBackend final : public IGraphicsBackend
    {
    public:
        OpenGL4GraphicsBackend(SDL_Window* window, int virtualWidth, int virtualHeight,
                               CnaPresentationMode mode, int multiSampleCount, int swapInterval);
        ~OpenGL4GraphicsBackend() override;

        OpenGL4GraphicsBackend(const OpenGL4GraphicsBackend&) = delete;
        OpenGL4GraphicsBackend& operator=(const OpenGL4GraphicsBackend&) = delete;

        void Clear(float r, float g, float b, float a) override;
        void Present() override;
        void GetViewportSize(int& width, int& height) override;
        void SetVirtualResolution(int width, int height) override;
        void SetPresentationMode(int mode) override;
        void SetSwapInterval(int interval) override;

        /// plan_opengl4.md GL4-17: real window/backbuffer MSAA -- a manually-managed multisample
        /// FBO (mirroring EasyGLGraphicsBackend's own msaaFbo_/CreateMsaaBuffers/ResolveMsaa
        /// approach) rather than an SDL_GL_SetAttribute(SDL_GL_MULTISAMPLESAMPLES, ...) window
        /// pixel format, since the latter can't be resolved through our own controlled
        /// glBlitFramebuffer call and would fight this backend's existing Y-flip/ReadBackbuffer
        /// conventions. Fixed at construction time; ApplyMultiSampleCount is not overridden
        /// (inherited no-op default -- same documented "no way to change after construction"
        /// limitation EasyGLGraphicsBackend already has).
        [[nodiscard]] int GetMultiSampleCount() const override { return msaaSampleCount_; }

        [[nodiscard]] SDL_Window* GetWindowInternal() const override { return window_; }
        [[nodiscard]] SDL_Renderer* GetRendererInternal() const override { return nullptr; }

        std::unique_ptr<ITextureBackend> CreateTexture(const ImageData& data) override;
        std::unique_ptr<ISpriteBatchBackend> CreateSpriteBatch() override;

        /// plan_opengl4.md GL4-20: plain (non-render-target) Texture3D/TextureCube.
        std::unique_ptr<ITexture3DBackend> CreateTexture3D(int w, int h, int depth, bool mipMap,
                                                            int surfaceFormat) override;
        std::unique_ptr<ITextureCubeBackend> CreateTextureCube(int size, bool mipMap,
                                                                int surfaceFormat) override;

        /// plan_opengl4.md GL4-24: real GL_SAMPLES_PASSED occlusion queries.
        std::unique_ptr<IOcclusionQueryBackend> CreateOcclusionQuery() override;

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

        /// plan_opengl4.md GL4-13: dispatches by vertex stride to the colored/textured/
        /// colored-textured/lit-textured shader family, matching VulkanGraphicsBackend's/
        /// SdlGpuGraphicsBackend's own stride-keyed pipeline dispatch pattern.
        void DrawPrimitivesEx(const IVertexBufferBackend& vb,
                              const Matrix& world, const Matrix& view, const Matrix& projection,
                              PrimitiveType primitive, int primitiveCount,
                              const GpuDrawParams& params) override;
        void DrawIndexedPrimitivesEx(const IVertexBufferBackend& vb, const IIndexBufferBackend& ib,
                                     const Matrix& world, const Matrix& view, const Matrix& projection,
                                     PrimitiveType primitive, int primitiveCount,
                                     const GpuDrawParams& params) override;

        void ApplySamplerState(int slot, int filter, int addressU, int addressV, int maxAnisotropy) override;
        void SetViewport(int x, int y, int w, int h, float minDepth, float maxDepth) override;

        /// plan_opengl4.md GL4-16: real dynamic BlendState/DepthStencilState/RasterizerState mapping.
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
        void SetBlendFactor(float r, float g, float b, float a) override;
        void SetScissorRect(int x, int y, int w, int h) override;

        /// plan_opengl4.md GL4-14: real FBO-backed RenderTarget2D.
        std::unique_ptr<IRenderTargetBackend> CreateRenderTarget2D(int w, int h, int depthFormat,
                                                                    bool preserveContents = false,
                                                                    bool mipMap = false,
                                                                    int multiSampleCount = 0) override;
        void SetRenderTarget2D(IRenderTargetBackend* rt) override;

        /// plan_opengl4.md GL4-15: real per-face FBO-backed RenderTargetCube + real MRT.
        std::unique_ptr<IRenderTargetCubeBackend> CreateRenderTargetCube(int size, int depthFormat,
                                                                          bool mipMap = false,
                                                                          int multiSampleCount = 0) override;
        void SetRenderTargetCubeFace(IRenderTargetCubeBackend* rt, int face) override;
        void SetRenderTargets(IRenderTargetBackend* const* rts, int count) override;

        /// NOXNA: physical window size, used by the SpriteBatch backend to size its ortho projection.
        NOXNA void GetPhysicalSize(int& width, int& height) const;
        /// NOXNA: logical (virtual) size, honoring FixedHeightDynamicWidth (mirrors EasyGL).
        NOXNA void GetLogicalSize(int& width, int& height) const;
        /// NOXNA: compiles (once) and returns the built-in textured-quad sprite program.
        NOXNA OpenGL4RawProgram& GetOrCreateSpriteProgram();
        /// NOXNA: reports the currently-bound RenderTarget2D's size (mirrors
        /// EasyGLGraphicsBackend::GetCurrentRenderTarget2DSize). Returns false (leaving width/
        /// height untouched) when no render target is currently bound.
        NOXNA [[nodiscard]] bool GetCurrentRenderTarget2DSize(int& width, int& height) const;

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
        /// plan_opengl4.md GL4-21: EnvironmentMapEffect's own dedicated stride-32 program,
        /// selected instead of litTextured3DProgram_ when GpuDrawParams::envMapping is set.
        void EnsureEnvMap3DProgram();
        /// plan_opengl4.md GL4-22: SkinnedEffect's own dedicated stride-52/56 program.
        void EnsureSkinned3DProgram();
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
        /// EasyGLGraphicsBackend::CreateMsaaBuffers.
        void CreateMsaaBuffers(int w, int h);
        /// Binds the default framebuffer for drawing -- FBO 0 when backbuffer MSAA is off, the
        /// manual MSAA FBO (recreated first if the window was resized) otherwise. Mirrors
        /// EasyGLGraphicsBackend::BindDefaultFramebuffer.
        void BindDefaultFramebufferOrMsaa();
        /// Blits the manual MSAA FBO's colour attachment into FBO 0 (mirrors
        /// EasyGLGraphicsBackend::ResolveMsaa). No-op when backbuffer MSAA is off.
        void ResolveMsaa();

        static constexpr int kMaxSamplerSlots = 16;

        SDL_Window* window_ = nullptr;
        void* glContext_ = nullptr;
        int virtualWidth_ = 0;
        int virtualHeight_ = 0;
        CnaPresentationMode presentationMode_ = CnaPresentationMode::FixedHeightDynamicWidth;
        int swapInterval_ = 1;
        bool depthWriteEnabled_ = true;

        /// plan_opengl4.md GL4-14: currently-bound RenderTarget2D (nullptr = default back
        /// buffer). currentRtHeight_ is the render target's own height when one is bound, 0
        /// otherwise -- SetViewport's bottom-left-to-top-left Y flip is computed from this
        /// instead of the window's physical height whenever a target is bound (mirrors
        /// EasyGLGraphicsBackend::currentRtHeight_'s identical role).
        IRenderTargetBackend* currentRt2D_ = nullptr;
        int currentRtHeight_ = 0;
        /// plan_opengl4.md GL4-15: currently-bound RenderTargetCube face (nullptr = none).
        IRenderTargetCubeBackend* currentRtCube_ = nullptr;
        /// plan_opengl4.md GL4-15: lazily-created, persistent FBO reused across every
        /// SetRenderTargets(count > 1) MRT call (mirrors EasyGLGraphicsBackend::mrtFbo_).
        unsigned int mrtFbo_ = 0;

        /// plan_opengl4.md GL4-17: manual backbuffer MSAA FBO (0 = disabled). Sized to the
        /// window's physical size; recreated on resize by BindDefaultFramebufferOrMsaa.
        unsigned int msaaFbo_ = 0;
        unsigned int msaaColorRbo_ = 0;
        unsigned int msaaDepthRbo_ = 0;
        int msaaSampleCount_ = 0;
        int msaaW_ = 0;
        int msaaH_ = 0;

        OpenGL4RawProgram spriteProgram_;
        OpenGL4RawProgram colored3DProgram_;
        int colored3DWvpLoc_ = -1;
        OpenGL4RawProgram coloredParams3DProgram_;
        OpenGL4RawProgram textured3DProgram_;
        OpenGL4RawProgram coloredTextured3DProgram_;
        OpenGL4RawProgram litTextured3DProgram_;
        OpenGL4RawProgram envMap3DProgram_;
        OpenGL4RawProgram skinned3DProgram_;
        OpenGL4RawProgram pbr3DProgram_;
        OpenGL4RawProgram pbrSkinned3DProgram_;
        unsigned int defaultWhiteTexture_ = 0;
        unsigned int defaultFlatNormalTexture_ = 0;
        unsigned int samplers_[kMaxSamplerSlots] = {};
    };
}
