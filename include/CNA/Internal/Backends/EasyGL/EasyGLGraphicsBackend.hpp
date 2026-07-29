#pragma once

#include "../Common/IGraphicsBackend.hpp"
#include "CNA/Internal/Graphics/ImageData.hpp"
#include <SDL3/SDL.h>
#include <easygl/easygl.hpp>
#include <array>
#include <cstdint>
#include <vector>

namespace CNA::Internal::Backends::EasyGL
{
    class EasyGLGraphicsBackend;

    /**
     * @brief REMED-GFX-147: whether sampling @p texture needs a vertical coordinate correction.
     *
     * This is the single source of truth for the question "did this texture's content arrive by
     * being RENDERED rather than uploaded?", and every EasyGL sampling path -- SpriteBatch's
     * CPU-generated sprite UVs, the stock 3D effects' mesh UVs, and a custom ShaderEffect that
     * opts in -- asks it here so they cannot diverge.
     *
     * An OpenGL framebuffer's origin is bottom-left, so a render target's colour texture stores the
     * logical image bottom-up: texel row v=0 is the LAST logical row. Ordinary textures are
     * uploaded top-down and are unaffected, which is why this asks about the resource's nature and
     * not about the sampler, the target currently bound, or anything per-draw.
     *
     * @param texture Sampled source, or nullptr.
     * @return True when @p texture is a render target's colour attachment.
     */
    [[nodiscard]] bool SampledRowOrderIsBottomUp(const ITextureBackend* texture);

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
        // Task 924: real mip level count this texture was created with -- GL_TEXTURE_MAX_LEVEL
        // must be clamped to match it (mipLevels_-1), or a mipmap-requiring TextureFilter (e.g.
        // Anisotropic) treats the texture as an incomplete mipmap chain and renders solid black,
        // even when mipLevels_==1 (the common non-mipmapped case).
        int mipLevels_ = 1;
    };

    /// EasyGL render target: off-screen FBO with a color texture and optional depth renderbuffer.
    class EasyGLRenderTargetBackend : public IRenderTargetBackend, public ::easygl::RecoverableResource
    {
    public:
        EasyGLRenderTargetBackend(int w, int h, int depthFormat, ::easygl::ResourceRegistry* registry,
                                   bool mipMap = false, int multiSampleCount = 0);
        ~EasyGLRenderTargetBackend() override;

        int GetWidth()  const override { return width_; }
        int GetHeight() const override { return height_; }
        SDL_Texture* GetNativeTexture() const override { return nullptr; }
        void BindGL() const override;

        void BindAsRenderTarget()   override;
        void UnbindAsRenderTarget() override;
        /// REMED-GFX-127: returns true once glReadPixels has filled the whole requested region.
        /// Every invalid request throws; there is no path that returns false having written part of
        /// @p data, so the shared layer can never convert a partially populated buffer.
        [[nodiscard]] bool GetData(int level, int x, int y, int w, int h,
                                   void* data, int dataLength) const override;
        [[nodiscard]] unsigned int GetColorGLHandle() const override;
        [[nodiscard]] const ::easygl::Texture& GetEasyGLColorTexture() const { return colorTex_; }
        [[nodiscard]] int GetMultiSampleCount() const override { return multiSampleCount_; }
        [[nodiscard]] bool HasRealDepthBuffer(bool depthFormatWasRequested) const override
        {
            return depthFormatWasRequested && depthFormat_ != 0;
        }

        void release_gl_handle_only() override;
        void recreate_gl_resource()   override;

    private:
        friend class EasyGLGraphicsBackend;

        void CreateResources();
        void AttachColorToMRT(
            ::easygl::Framebuffer& framebuffer,
            ::metagl::FramebufferAttachment attachment) const;
        void AttachDepthToMRT(::easygl::Framebuffer& framebuffer) const;

        ::easygl::Framebuffer  fbo_;         ///< Render target FBO (color = colorTex_, or msaaColorRbo_ when MSAA).
        ::easygl::Framebuffer  resolveFbo_;  ///< MSAA only: blit destination (color = colorTex_).
        ::easygl::Texture      colorTex_;
        ::easygl::Renderbuffer depthRbo_;
        ::easygl::Renderbuffer msaaColorRbo_;
        int  width_            = 0;
        int  height_           = 0;
        int  depthFormat_      = 0;  ///< Raw Microsoft::Xna::Framework::Graphics::DepthFormat ordinal.
        bool mipMap_           = false;
        int  levelCount_       = 1;
        int  multiSampleCount_ = 0;
        ::easygl::ResourceRegistry* registry_ = nullptr;
    };

    /// EasyGL cube-map render target: one FBO per face, shared cube-map texture.
    class EasyGLRenderTargetCubeBackend : public IRenderTargetCubeBackend,
                                           public ::easygl::RecoverableResource
    {
    public:
        EasyGLRenderTargetCubeBackend(int size, int depthFormat, ::easygl::ResourceRegistry* registry,
                                       bool mipMap = false, int multiSampleCount = 0);
        ~EasyGLRenderTargetCubeBackend() override;

        [[nodiscard]] int GetSize() const override { return size_; }
        void BindAsRenderTargetFace(int face) override;
        void UnbindAsRenderTarget() override;
        [[nodiscard]] unsigned int GetGLHandle() const override;
        [[nodiscard]] int GetMultiSampleCount() const override { return multiSampleCount_; }

        // ITextureCubeBackend — bind and upload to the shared cube texture.
        void BindGL() const override;
        /**
         * @brief Uploads CPU pixels into a rendered cube face's mip level.
         *
         * REMED-GFX-135: the only render-target cube in CNA that implements this rather than
         * inheriting `IRenderTargetCubeBackend::SetData`'s refusal -- the FBO's colour attachment
         * IS a normal GL cube texture here, so glTexSubImage2D reaches it directly.
         *
         * @return True once the whole region has been uploaded with no GL error; false for an
         *         out-of-range face/level/region or a driver-rejected upload.
         */
        [[nodiscard]] bool SetData(int face, int level, int x, int y, int w, int h,
                                   const void* data, int dataLength) override;
        /**
         * @brief Reads a RENDERED cube face's mip level back to the CPU.
         *
         * REMED-GFX-134. `EasyGLTextureCubeBackend::GetData`'s mechanism -- attach the requested
         * face/level to a temporary FBO and glReadPixels it -- with the one difference a rendered
         * face has: this content came from rasterization, so it is stored bottom-up in the face's
         * texel grid exactly as `EasyGLRenderTargetBackend::GetData` already documents for a 2D
         * target. The requested rectangle is therefore mapped into bottom-up coordinates and the
         * returned rows are flipped back, so the public result is top-row-first like every other
         * backend. Applied exactly once, and only here: the plain-cube path stays unflipped
         * because its content came from `SetData`'s own texel-space upload.
         *
         * Whichever face was most recently bound has already been resolved into `cubeTex_` by
         * `UnbindAsRenderTarget`, so an MSAA target is read through its resolved single-sample
         * cube image rather than the multisample renderbuffer.
         *
         * @param face       Cube face index (0=+X, 1=-X, 2=+Y, 3=-Y, 4=+Z, 5=-Z).
         * @param level      Mip level to read.
         * @param x          Left edge of the requested region, in texels.
         * @param y          Top edge of the requested region, in texels.
         * @param w          Width of the requested region, in texels.
         * @param h          Height of the requested region, in texels.
         * @param data       Destination for tightly packed RGBA8 rows, top row first.
         * @param dataLength Size of @p data in bytes; at least w * h * 4.
         * @return True once the whole region was read; false for an out-of-range face/level/region
         *         or an incomplete framebuffer.
         */
        [[nodiscard]] bool GetData(int face, int level, int x, int y, int w, int h,
                                   void* data, int dataLength) const override;

        void release_gl_handle_only() override;
        void recreate_gl_resource()   override;

    private:
        void CreateResources();

        ::easygl::Texture      cubeTex_;
        ::easygl::Framebuffer  fbo_;         ///< Render FBO (color = cubeTex_ face, or msaaColorRbos_[face] when MSAA).
        ::easygl::Framebuffer  resolveFbo_;  ///< MSAA only: blit destination, re-attached per face.
        ::easygl::Renderbuffer depthRbo_;
        /**
         * REMED-GFX-141: SIX multisample colour renderbuffers, one per cube face, not one shared
         * by all six. A renderbuffer is face-agnostic storage, so the single one this used to
         * allocate held whichever face was rendered last -- and a `PreserveContents` face rebound
         * for a partial update loaded THAT instead of its own samples, while the resolved
         * `cubeTex_` layer still held the right content (which is why a full redraw and an
         * immediate readback both passed). `BindAsRenderTargetFace` now re-attaches this face's
         * own renderbuffer, exactly as the non-MSAA path already re-attaches this face's own
         * cube-texture image. Six is what D3D11/D3D12 have always allocated (a six-slice
         * multisampled array); the cost is `size * size * samples * 4 * 6` bytes per cube target,
         * paid once at construction -- never per bind, and with no full-face copy anywhere.
         */
        std::array<::easygl::Renderbuffer, 6> msaaColorRbos_;
        int  size_             = 0;
        int  depthFormat_      = 0;  ///< Raw Microsoft::Xna::Framework::Graphics::DepthFormat ordinal.
        bool mipMap_           = false;
        int  levelCount_       = 1;
        int  multiSampleCount_ = 0;
        int  lastFace_         = 0;  ///< Most recently bound face, used by UnbindAsRenderTarget's resolve.
        ::easygl::ResourceRegistry* registry_ = nullptr;
    };

    /// EasyGL 3D (volume) texture backend.
    class EasyGLTexture3DBackend : public ITexture3DBackend
    {
    public:
        EasyGLTexture3DBackend(int w, int h, int depth, bool mipMap, int surfaceFormat);
        ~EasyGLTexture3DBackend() override = default;

        /// REMED-GFX-135: true only once the requested box has been uploaded into a level this
        /// texture really allocated and GL reported no error; false for an out-of-range level or
        /// box, a source buffer too small for the box, or a driver-rejected upload. glTexSubImage3D
        /// copies out of the caller's memory before returning, so the source is never retained.
        [[nodiscard]] bool SetData(int level, int x, int y, int z,
                                   int w, int h, int depth,
                                   const void* data, int dataLength) override;

        /// REMED-GFX-130: true only once every requested Z slice has been read back through the
        /// temporary FBO below; false when the request is out of range or an FBO attachment for
        /// some slice is not framebuffer-complete, so the shared layer rejects the read instead of
        /// converting its own zeroed scratch buffer into a fabricated volume.
        [[nodiscard]] bool GetData(int level, int x, int y, int z,
                                   int w, int h, int depth,
                                   void* data, int dataLength) const override;

        /// Binds this volume texture to the currently active GL texture unit.
        void BindGL() const override;

    private:
        ::easygl::Texture tex_;
        int width_  = 0;
        int height_ = 0;
        int depth_  = 0;
        /// Mip levels this texture really allocated storage for (REMED-GFX-135).
        int levelCount_ = 1;
    };

    /// EasyGL cube map texture backend.
    class EasyGLTextureCubeBackend : public ITextureCubeBackend
    {
    public:
        EasyGLTextureCubeBackend(int size, bool mipMap, int surfaceFormat);
        ~EasyGLTextureCubeBackend() override = default;

        /// REMED-GFX-135: true only once the requested face/mip rectangle has been uploaded into a
        /// level this texture really allocated and GL reported no error; false for an out-of-range
        /// face/level/rectangle, a source buffer too small for the region, or a driver-rejected
        /// upload. glTexSubImage2D copies out of the caller's memory before returning, so the
        /// source is never retained.
        [[nodiscard]] bool SetData(int face, int level, int x, int y, int w, int h,
                                   const void* data, int dataLength) override;

        /// REMED-GFX-130: true only once the requested face/mip rectangle has been read back
        /// through the temporary FBO below; false for an out-of-range face or an incomplete
        /// framebuffer, so the shared layer rejects the read instead of fabricating a face.
        [[nodiscard]] bool GetData(int face, int level, int x, int y, int w, int h,
                                   void* data, int dataLength) const override;

        /// Binds this cube map to the currently active GL texture unit.
        void BindGL() const override;

    private:
        ::easygl::Texture tex_;
        int size_ = 0;
        /// Mip levels this cube really allocated storage for (REMED-GFX-135).
        int levelCount_ = 1;
    };

    /// EasyGL implementation of IEffectBackend — wraps an easygl::Program.
    class EasyGLEffectBackend : public IEffectBackend
    {
    public:
        explicit EasyGLEffectBackend() = default;
        ~EasyGLEffectBackend() override = default;

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
        void BindTexture(int unit, ITextureBackend* texture) override;
        void BindTextureCube(int unit, ITextureCubeBackend* texture) override;
        void BindTexture3D(int unit, ITexture3DBackend* texture) override;

        /// Returns the underlying compiled program, so a backend (e.g. SpriteBatch) can bind
        /// the SAME program this ShaderEffect's SetUniformXxx() calls actually write to.
        [[nodiscard]] ::easygl::Program& GetProgram() { return program_; }

    private:
        ::easygl::Program program_;
        std::string compileError_;
        /// REMED-GFX-147: per-texture-unit "the bound source is a render target" flags, mirrored
        /// into this effect's own `uRtFlipV` when its GLSL opts in by declaring that uniform.
        float rtFlipV_[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
        bool  rtFlipVUploaded_ = false;
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
        /// REMED-GFX-147: cached SampledRowOrderIsBottomUp(current_texture_). A batch is one
        /// texture by construction, so the answer is resolved when the source is bound, not once
        /// per sprite. Meaningless while current_texture_ is null.
        bool current_texture_bottom_up_ = false;
        Matrix transform_ = Matrix::getIdentityProperty();
        Effect* customEffect_       = nullptr;

        // Raw TextureFilter/TextureAddressMode values set via SetSamplerFilter/SetSamplerAddressMode
        // (SpriteBatch::Begin), applied to texture unit 0 on the next FlushBatch(). Defaults match
        // the GL texture-object defaults baked in at texture creation (Linear filter, Clamp address),
        // so a SpriteBatch that never receives these calls behaves exactly as before this was added.
        int pendingFilter_    = 0; // TextureFilter::Linear
        int pendingAddressU_  = 1; // TextureAddressMode::Clamp
        int pendingAddressV_  = 1; // TextureAddressMode::Clamp

    public:
        explicit EasyGLSpriteBatchBackend(::easygl::Device& device, ::easygl::ResourceRegistry* registry,
                                          EasyGLGraphicsBackend* backend = nullptr);
        ~EasyGLSpriteBatchBackend() override;

        void Begin() override;
        void End() override;
        void SetTransformMatrix(const Matrix& m) override;
        void SetCustomEffect(Effect* effect) override;
        void SetSamplerFilter(int textureFilter) override;
        void SetSamplerAddressMode(int addressU, int addressV) override;
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
        void SetDataWithOptions(const void* data, int vertex_count, std::size_t stride_in_bytes,
                                SetDataOptions options) override;
        // Stores the caller's declaration elements so ApplyLayout() can bind genuinely custom
        // vertex formats generically instead of only the fixed byte-strides the switch below
        // recognizes. Empty means "keep using that switch".
        void SetVertexDeclaration(const VertexDeclaration& vertexDeclaration) override;
        int GetVertexCount() const override { return vertex_count; }
        [[nodiscard]] std::size_t GetStride() const { return stride_in_bytes_; }
        /// Task 1082: exposes this buffer's own declaration elements so a hardware-instancing
        /// draw can bind a *second* (per-instance) buffer's attributes into the same VAO,
        /// continuing at locations right after this buffer's own (see
        /// DrawInstancedPrimitivesEx's custom-effect branch).
        [[nodiscard]] const std::vector<VertexElement>& GetDeclarationElements() const { return declarationElements_; }

        void release_gl_handle_only() override;
        void recreate_gl_resource() override;

    private:
        void InitializeLayout();
        void ApplyLayout(std::size_t stride);
        void uploadWithOptions(const void* data, std::size_t byte_count, SetDataOptions options);
        ::easygl::ResourceRegistry* registry_ = nullptr;
        std::vector<uint8_t> cpu_data_;
        std::size_t stride_in_bytes_ = 0;
        bool gpu_allocated_ = false;
        std::vector<VertexElement> declarationElements_;
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
        void SetData16WithOptions(const void* data, int index_count, SetDataOptions options) override;
        void SetData32WithOptions(const void* data, int index_count, SetDataOptions options) override;
        int  GetIndexCount()  const override { return index_count; }
        bool IsThirtyTwoBit() const override { return thirtyTwoBit; }

        /// CPU shadow copy of the index data (populated when a registry is present).
        /// Used by the EasyGL wireframe emulation to build line indices.
        [[nodiscard]] const std::vector<uint8_t>& GetCpuBytes() const { return cpu_data_; }

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

        /// Raw XNA ColorWriteChannels values for MRT slots 0..3 (bit0=R..bit3=A).
        /// GLES 3.2 applies these with glColorMaski; Clear temporarily forces every active slot
        /// to All because XNA/FNA Clear ignores BlendState write masks.
        std::array<int, 4> currentColorWriteMasks_ = {15, 15, 15, 15};
        bool supportsIndexedColorMasks_ = false;
        void ApplyCurrentColorWriteMasks();
        void ForceAllColorWriteMasks();
        [[nodiscard]] bool HasRestrictedActiveColorWriteMask() const;

        int virtualWidth_ = 0;
        int virtualHeight_ = 0;
        static constexpr int kMaxSamplerSlots = 16;
        ::easygl::Sampler samplers_[kMaxSamplerSlots];
        CnaPresentationMode presentationMode_ = CnaPresentationMode::FixedHeightDynamicWidth;
        bool contextRecoveryEnabled_ = true;
        int swapInterval_ = 1;

        // MSAA — multisampled render buffer resolved to FBO 0 on Present().
        int sampleCount_ = 1;
        int msaaW_       = 0;
        int msaaH_       = 0;
        ::easygl::Framebuffer  msaaFbo_;
        ::easygl::Renderbuffer msaaColorRbo_;
        ::easygl::Renderbuffer msaaDepthRbo_;

        void CreateMsaaBuffers(int w, int h);
        void BindDefaultFramebuffer();
        void ResolveMsaa();

        /// Returns &registry_ when context recovery is enabled, nullptr otherwise.
        [[nodiscard]] ::easygl::ResourceRegistry* RegistryPtr() noexcept
        { return contextRecoveryEnabled_ ? &registry_ : nullptr; }

        // 3D pipeline state — one program per vertex layout
        struct Prog3D {
            ::easygl::Program prog;
            bool  ready           = false;
            int loc_wvp           = -1;
            int loc_normalmat     = -1;  ///< mat3 upper-left of world (lit/env shader)
            int loc_world         = -1;  ///< mat4 full world matrix (env map shader)
            int loc_diffuse       = -1;
            int loc_ambient       = -1;
            int loc_l0dir         = -1;
            int loc_l0diff        = -1;
            int loc_l1dir         = -1;  ///< BasicEffect.DirectionalLight1 (lit shader only)
            int loc_l1diff        = -1;
            int loc_l2dir         = -1;  ///< BasicEffect.DirectionalLight2 (lit shader only)
            int loc_l2diff        = -1;
            int loc_l0spec        = -1;  ///< BasicEffect.DirectionalLight0/1/2.SpecularColor (lit shader only, Task 886)
            int loc_l1spec        = -1;
            int loc_l2spec        = -1;
            int loc_specularcolor = -1;  ///< BasicEffect.SpecularColor (material, applied once to the light sum)
            int loc_specularpower = -1;  ///< BasicEffect.SpecularPower (Blinn-Phong exponent)
            int loc_texture       = -1;
            int loc_texture2      = -1;  ///< second sampler (DualTextureEffect only)
            int loc_envmap        = -1;  ///< samplerCube (EnvironmentMapEffect only)
            int loc_envmap_amount = -1;  ///< float blend [0,1]
            int loc_envmap_spec   = -1;  ///< vec3 specular tint
            int loc_fresnel_enabled = -1;  ///< float 0=off 1=on (EnvironmentMapEffect only)
            int loc_fresnel_factor  = -1;  ///< float exponent (EnvironmentMapEffect only)
            int loc_emissive      = -1;  ///< vec3 emissive+ambient (env map / skinned)
            int loc_eyepos        = -1;  ///< vec3 camera world pos
            int loc_bones         = -1;  ///< mat4[72] bone palette (SkinnedEffect)
            int loc_weightsPerVertex = -1;  ///< int 1/2/4 (SkinnedEffect only, Task 895)
            int loc_alphatest     = -1;  ///< vec4 (refVal, tolerance, passW, failW)
            int loc_fog_vector    = -1;  ///< REMED-GFX-010: vec4 FNA fog vector (dot with object/skin pos)
            int loc_fog_color     = -1;  ///< vec3 RGB fog colour
            int loc_vertexcolor   = -1;  ///< float 0=ignore vertex color, 1=multiply by it (BasicEffect.VertexColorEnabled)
            int loc_pbr_normalmap  = -1;  ///< sampler2D tangent-space normal map (PbrEffect only)
            int loc_pbr_mr         = -1;  ///< sampler2D metallic-roughness map (PbrEffect only, G=roughness/B=metallic)
            int loc_pbr_emissivemap = -1; ///< sampler2D emissive map (PbrEffect only)
            int loc_pbr_occlusionmap = -1; ///< sampler2D occlusion map (PbrEffect only, R channel)
            int loc_pbr_metallic    = -1;  ///< float metallic factor (PbrEffect only)
            int loc_pbr_roughness   = -1;  ///< float roughness factor (PbrEffect only)
            int loc_rt_flip_v       = -1;  ///< REMED-GFX-147: vec4 render-target V-flip flags for texture units 0-3
            int loc_rt_flip_v_hi    = -1;  ///< REMED-GFX-147: vec4 whose x is texture unit 4's flag (PbrEffect only)
            void reset_no_gl() { prog.reset_handle_no_gl(); ready = false; }
        };

        Prog3D prog_colored_;        ///< stride=16: aPos + aColor
        Prog3D prog_textured_;       ///< stride=20: aPos + aUV
        Prog3D prog_col_textured_;   ///< stride=24: aPos + aColor + aUV
        Prog3D prog_lit_textured_;   ///< stride=32: aPos + aNormal + aUV (PreferPerPixelLighting=true: per-pixel Blinn-Phong)
        Prog3D prog_lit_textured_vertexlit_;  ///< stride=32: aPos + aNormal + aUV (PreferPerPixelLighting=false, XNA's own default: per-vertex/Gouraud-shaded Blinn-Phong, Task 1102)
        Prog3D prog_dual_textured_;  ///< stride=20: aPos + aUV, two samplers (DualTextureEffect)
        Prog3D prog_dual_textured_colored_;  ///< stride=24: aPos + aColor + aUV, two samplers (DualTextureEffect, Task 889)
        Prog3D prog_env_mapped_;     ///< stride=32: aPos + aNormal + aUV, cube map (EnvironmentMapEffect)
        Prog3D prog_skinned_;        ///< stride=52: aPos + aNormal + aUV + weights + indices (SkinnedEffect, PreferPerPixelLighting=true: per-pixel Blinn-Phong)
        Prog3D prog_skinned_vertexlit_;  ///< stride=52: aPos + aNormal + aUV + weights + indices (SkinnedEffect, PreferPerPixelLighting=false, XNA's own default: per-vertex/Gouraud-shaded Blinn-Phong, Task 1102b)
        Prog3D prog_pbr_;            ///< stride=48: aPos + aNormal + aTangent + aUV, real glTF metallic-roughness BRDF (PbrEffect, plan_cnj.md CNB-58)
        Prog3D prog_pbr_skinned_;    ///< stride=68: aPos + aNormal + aTangent + aUV + weights + indices, PBR BRDF + skinning (SkinnedPbrEffect, PBR+skinning combo)

        ::easygl::Texture default_white_texture_;
        bool default_white_texture_ready_ = false;
        ::easygl::Texture default_flat_normal_texture_;      ///< PbrEffect NormalMap fallback (CNB-58)
        bool default_flat_normal_texture_ready_ = false;

        // One reusable MRT FBO. Every bind replaces all ordered attachments; target identity is
        // deliberately not part of any shader/pipeline cache.
        ::easygl::Framebuffer mrtFbo_;
        int maxMrtTargets_ = 1;
        std::array<EasyGLRenderTargetBackend*, 4> currentMrtTargets_ = {};
        int currentMrtCount_ = 0;
        void FinalizeCurrentMRT();

        // Extent of the currently bound single target or MRT set; 0 = default framebuffer.
        int currentRtWidth_ = 0;
        int currentRtHeight_ = 0;

        // Tracks the currently-bound single RenderTarget2D/RenderTargetCube backend (nullptr
        // when unbound, in MRT mode, or targeting the default framebuffer) so that switching
        // away from it can trigger UnbindAsRenderTarget()'s mip regeneration (Task 336) — the
        // interface method is never invoked automatically by anything else.
        IRenderTargetBackend*     currentRt2D_   = nullptr;
        IRenderTargetCubeBackend* currentRtCube_ = nullptr;

        // FillMode::WireFrame emulation (OpenGL ES has no glPolygonMode):
        // when active, triangle draws are re-expanded into GL_LINES.
        bool wireframe_ = false;
        ::easygl::Buffer wireframeIbo_;        ///< scratch element buffer of line indices
        bool wireframeIboCreated_ = false;
        std::vector<std::uint32_t> wireframeScratch_;  ///< CPU build buffer (32-bit line indices)

        // Draw the given triangle geometry as a wireframe (GL_LINES). Returns false when the
        // primitive is not a triangle list/strip (caller should fall back to a normal draw).
        // ib == nullptr means a non-indexed draw (sequential vertices from firstVertex).
        bool DrawWireframe(const EasyGLVertexBufferBackend& vb,
                           const EasyGLIndexBufferBackend* ib,
                           PrimitiveType primitive, int primitiveCount,
                           int startIndex, int baseVertex, int firstVertex);

        void EnsureColored3DProgram();
        void EnsureTextured3DProgram();
        void EnsureColoredTextured3DProgram();
        void EnsureLit3DProgram();
        void EnsureLit3DVertexLitProgram();
        void EnsureDualTextured3DProgram();
        void EnsureDualTexturedColored3DProgram();
        void EnsureEnvMapped3DProgram();
        void EnsureSkinnedProgram();
        void EnsureSkinnedVertexLitProgram();
        void EnsurePbrProgram();
        void EnsurePbrSkinnedProgram();
        void EnsureDefaultWhiteTexture();
        void EnsureDefaultFlatNormalTexture();
        Prog3D& SelectProgram(std::size_t stride, const GpuDrawParams& params);
        void BindDrawParams(Prog3D& p, const Matrix& world, const Matrix& view,
                            const Matrix& projection, const GpuDrawParams& params);
        /// REMED-GFX-147: resolves uRtFlipV/uRtFlipVHi for a freshly linked stock 3D program.
        static void ResolveRenderTargetOrientationUniforms(Prog3D& p);

    public:
        explicit EasyGLGraphicsBackend(SDL_Window* window,
                                       int virtualWidth = 0, int virtualHeight = 0,
                                       CnaPresentationMode mode = CnaPresentationMode::FixedHeightDynamicWidth,
                                       bool contextRecoveryEnabled = true,
                                       int multiSampleCount = 1,
                                       int swapInterval = 1);
        ~EasyGLGraphicsBackend() override;
        // AnisotropicFiltering/MultiSampleAntiAliasing re-query the same live GL state the
        // startup capability dump (EnsureGL()) already prints, since they're cheap, idempotent GL
        // queries -- no need to cache them. WireFrame is always false: GLES3 (EasyGL's underlying
        // API) has no wireframe fill mode at all. Everything else CNA::GraphicsCapability
        // currently enumerates is genuinely supported here, so falls through to the shared
        // default (true).
        [[nodiscard]] bool SupportsCapability(CNA::GraphicsCapability capability) const override;
        void Clear(float r, float g, float b, float a) override;
        void Present() override;
        void SetSwapInterval(int interval) override;
        void GetViewportSize(int& width, int& height) override;
        void getLogicalSize(int& width, int& height) const;
        void getPhysicalSize(int& width, int& height) const;
        /// Returns the currently-bound single 2D render target's size, if one is bound (Task
        /// 1078). Used by EasyGLSpriteBatchBackend so a custom-effect draw into a RenderTarget2D
        /// smaller/larger than the window sizes its viewport/projection to the RT, not the window.
        [[nodiscard]] bool GetCurrentRenderTarget2DSize(int& width, int& height) const;
        bool TransformWindowToLogical(float windowX, float windowY,
                                      float& logX, float& logY) const override;
        bool TransformLogicalToWindow(float logX, float logY,
                                      float& windowX, float& windowY) const override;

        void SetVirtualResolution(int width, int height) override;
        void SetPresentationMode(int mode) override;
        // Task 902: EasyGL applies MultiSampleCount only at construction time (via the
        // multiSampleCount ctor argument, clamped into sampleCount_ below) -- there is no way to
        // resize the MSAA renderbuffers without recreating the whole GL context, so
        // ApplyMultiSampleCount() uses IGraphicsBackend's default (echoes back the current,
        // already-applied value, ignoring the request). GetMultiSampleCount() reports that real
        // value honestly instead of falling back to the interface default of 0.
        [[nodiscard]] int GetMultiSampleCount() const override { return sampleCount_ > 1 ? sampleCount_ : 0; }
        SDL_Window* GetWindowInternal() const override { return window; }
        SDL_Renderer* GetRendererInternal() const override { return nullptr; }

        std::unique_ptr<ITextureBackend> CreateTexture(const ImageData& data) override;
        std::unique_ptr<ISpriteBatchBackend> CreateSpriteBatch() override;
        std::unique_ptr<IOcclusionQueryBackend> CreateOcclusionQuery() override;
        std::unique_ptr<IRenderTargetBackend> CreateRenderTarget2D(int w, int h, int depthFormat, bool preserveContents = false, bool mipMap = false, int multiSampleCount = 0) override;
        std::unique_ptr<IRenderTargetCubeBackend> CreateRenderTargetCube(int size, int depthFormat, bool preserveContents = false, bool mipMap = false, int multiSampleCount = 0) override;
        std::unique_ptr<ITexture3DBackend> CreateTexture3D(int w, int h, int depth, bool mipMap, int surfaceFormat) override;
        std::unique_ptr<ITextureCubeBackend> CreateTextureCube(int size, bool mipMap, int surfaceFormat) override;
        std::unique_ptr<IEffectBackend> CreateEffectBackend(const std::string& vertSrc,
                                                             const std::string& fragSrc) override;
        void SetRenderTarget2D(IRenderTargetBackend* rt) override;
        void SetRenderTargetCubeFace(IRenderTargetCubeBackend* rt, int face) override;
        void SetRenderTargets(const RenderTargetBindingDescriptor* renderTargets,
                              int count) override;
        std::unique_ptr<IIndexBufferBackend> CreateIndexBuffer16(int index_capacity) override;
        std::unique_ptr<IIndexBufferBackend> CreateIndexBuffer32(int index_capacity) override;

        void SetContextRecoveryEnabled(bool enabled) override { contextRecoveryEnabled_ = enabled; }
        void DebugSimulateContextLoss() override;
        void DebugRestoreContext() override;
        void ReadBackbuffer(int x, int y, int w, int h, uint8_t* pixels) override;

        // ---- Graphics state: IMPLEMENTED ----
        void ApplyBlendState(int colorSrcBlend, int alphaSrcBlend,
                             int colorDstBlend, int alphaDstBlend,
                             int colorBlendFunc, int alphaBlendFunc,
                             const BlendWriteState& writeState) override;
        void ApplyDepthStencilState(bool depthEnable, bool depthWriteEnable, int depthFunc,
                                    bool stencilEnable, int stencilFunc,
                                    int stencilPass, int stencilFail, int stencilDepthFail,
                                    int stencilMask, int stencilWriteMask, int referenceStencil,
                                    bool twoSidedStencilMode,
                                    int ccwStencilFunc, int ccwStencilPass,
                                    int ccwStencilFail, int ccwStencilDepthFail) override;
        void ApplyRasterizerState(int cullMode, int fillMode, bool scissorTestEnable,
                                  float depthBias, float slopeScaleDepthBias) override;
        void ApplySamplerState(int slot, int filter, int addressU, int addressV,
                               int maxAnisotropy) override;
        void SetBlendFactor(float r, float g, float b, float a) override;
        void SetScissorRect(int x, int y, int w, int h) override;
        void SetViewport(int x, int y, int w, int h, float minDepth, float maxDepth) override;

        // ---- 3D: IMPLEMENTED ----
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
