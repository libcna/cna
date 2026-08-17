#pragma once

#include "CNA/Internal/Renderers/EasyGL/GlProfile.hpp"

#include "CNA/Internal/Renderers/Common/IGraphicsRenderer.hpp"
#include "CNA/Internal/Graphics/ImageData.hpp"
#include "CNA/Internal/Graphics/VertexDeclarationFidelity.hpp"
#include "CNA/Platform/IPlatformGlContext.hpp"
#if defined(CNA_EASYGL_COMPILED_EFFECTS)
#include "Microsoft/Xna/Framework/Graphics/VertexElement.hpp"
#include "mojoshader.h"
#endif
#include <easygl/easygl.hpp>
#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace CNA::Internal::Renderers::EasyGL
{
    class EasyGLRenderer;
    class EasyGLRenderTargetRenderer;
    class EasyGLRenderTargetCubeRenderer;
    class EasyGLPlatformContext;

    /**
     * @brief Platform-neutral presentation metrics consumed by the EasyGL family.
     *
     * The platform publishes physical drawable pixels plus a logical-to-physical display scale.
     * This value object derives the client-coordinate dimensions and owns EasyGL's virtual
     * resolution transform, so resize/DPI behavior can be tested without a native window or GL.
     */
    class EasyGLSurfaceState
    {
    public:
        EasyGLSurfaceState(const RendererSurfaceInfo& surface, int virtualWidth,
                           int virtualHeight, CnaPresentationMode presentationMode);

        /** @brief Replaces the platform snapshot after resize or density change. */
        void Update(const RendererSurfaceInfo& surface);
        /** @brief Replaces the virtual game resolution. */
        void SetVirtualResolution(int width, int height);
        /** @brief Replaces the presentation policy. */
        void SetPresentationMode(CnaPresentationMode mode);

        /** @brief Gets the physical drawable extent used by GL framebuffer operations. */
        void GetDrawableSize(int& width, int& height) const;
        /** @brief Gets the renderer's logical game extent. */
        void GetLogicalSize(int& width, int& height) const;
        /** @brief Converts logical client units into renderer game units. */
        bool WindowToLogical(float windowX, float windowY,
                             float& logicalX, float& logicalY) const;
        /** @brief Converts renderer game units into logical client units. */
        bool LogicalToWindow(float logicalX, float logicalY,
                             float& windowX, float& windowY) const;
        /** @brief Gets the stable platform window identity. */
        [[nodiscard]] CNA::Platform::WindowId GetWindowId() const { return surface_.windowId; }

    private:
        void GetClientSize(int& width, int& height) const;

        RendererSurfaceInfo surface_;
        int virtualWidth_ = 0;
        int virtualHeight_ = 0;
        CnaPresentationMode presentationMode_ = CnaPresentationMode::FixedHeightDynamicWidth;
    };

    /**
     * @brief REMED-GFX-168: the one record of which EasyGL render target is currently bound.
     *
     * This used to be six plain members of `EasyGLRenderer`, three of them raw pointers to
     * render-target RENDERER OBJECTS. Switching destination calls `UnbindAsRenderTarget()` on the
     * outgoing one -- a virtual call -- to run its pending MSAA resolve and mip regeneration, and
     * nothing cleared those pointers when the object behind them died. `RenderTarget2D::Dispose()`
     * refuses to dispose a bound target, but the DESTRUCTOR is `= default` and never routes through
     * Dispose, so a scoped target leaving scope while bound released its renderer silently and the
     * next `SetRenderTarget()` dispatched through freed storage.
     *
     * Holding the record in its OWN heap allocation, shared between the renderer and every render
     * target it created, is what makes the detach safe in both destruction orders: a dying target
     * clears its own slots here directly, without calling back into a graphics renderer that may
     * itself already be gone, and a graphics renderer that dies first simply takes the record with
     * it, leaving every surviving target's `weak_ptr` expired and its destructor a no-op.
     *
     * Only IDENTITY, EXTENT, and the non-owning name of the active MRT draw FBO live here -- no
     * ownership of anything. A target's native storage stays owned by the target, because the
     * finalization those handles serve writes into that same target's own `colorTex_`/`cubeTex_`:
     * nothing outside a live wrapper can observe it, so a destroyed target has no finalization left
     * to owe. The MRT slots are the one case where the neighbours' finalization IS observable,
     * which is why a detach clears one SLOT rather than abandoning the set.
     */
    struct EasyGLBoundTargetEXT
    {
        /** @brief Currently bound single `RenderTarget2D` renderer, or nullptr. */
        IRenderTargetRenderer*     rt2D = nullptr;
        /** @brief Currently bound `RenderTargetCube` renderer, or nullptr. */
        IRenderTargetCubeRenderer* cube = nullptr;
        /** @brief Currently bound ordered multi-target set; entries beyond `mrtCount` are unused. */
        std::array<EasyGLRenderTargetRenderer*, 4> mrt = {};
        /** @brief Number of live slots in `mrt`; 0 when no multi-target set is bound. */
        int mrtCount = 0;
        /** @brief Native draw FBO for the active MRT set, used to restore it after direct readback. */
        unsigned int mrtFramebuffer = 0;
        /** @brief Extent of the bound destination in pixels; 0 means the default framebuffer. */
        int width  = 0;
        /** @brief Extent of the bound destination in pixels; 0 means the default framebuffer. */
        int height = 0;
    };

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
    [[nodiscard]] bool SampledRowOrderIsBottomUp(const ITextureRenderer* texture);

    class EasyGLTextureRenderer : public ITextureRenderer, public ::easygl::RecoverableResource
    {
    public:
        ::easygl::Texture texture;
        int width = 0;
        int height = 0;

        EasyGLTextureRenderer(const ImageData& data, ::easygl::ResourceRegistry* registry);
        ~EasyGLTextureRenderer() override;
        int GetWidth() const override { return width; }
        int GetHeight() const override { return height; }

        void BindGL(int unit) const override;
        void UpdatePixels(const uint8_t* rgba, int stride) override;
        void UpdatePixelsLevel(int level, const uint8_t* rgba, int levelW, int levelH) override;

        void release_gl_handle_only() override;
        void recreate_gl_resource() override;
        void ShareCpuPixels(std::shared_ptr<std::vector<uint8_t>> pixels) override;

    private:
        /**
         * @brief Gives GL storage to every DECLARED mip level above 0, with no pixel data.
         *
         * REMED-GFX-175. A texture created with `mipMap=true` declares a chain, and Task 924 widens
         * `GL_TEXTURE_MAX_LEVEL` to match that declaration -- but only level 0 was ever given
         * storage, so until the game happened to write every remaining level the texture was
         * mipmap-incomplete and sampled as opaque black under every ordinal carrying a mipmap term.
         * Allocating the declared levels is not generating them: no level is derived from another,
         * and a texture created without `mipMap` has nothing to allocate.
         */
        void AllocateDeclaredLevels();

        std::shared_ptr<std::vector<uint8_t>> pixels_;
        ::easygl::ResourceRegistry* registry_ = nullptr;
        // Task 924: real mip level count this texture was created with -- GL_TEXTURE_MAX_LEVEL
        // must be clamped to match it (mipLevels_-1), or a mipmap-requiring TextureFilter (e.g.
        // Anisotropic) treats the texture as an incomplete mipmap chain and renders solid black,
        // even when mipLevels_==1 (the common non-mipmapped case).
        int mipLevels_ = 1;
    };

    /// EasyGL render target: off-screen FBO with a color texture and optional depth renderbuffer.
    class EasyGLRenderTargetRenderer : public IRenderTargetRenderer, public ::easygl::RecoverableResource
    {
    public:
        /**
         * @brief Creates the FBO, colour texture and optional depth/multisample storage.
         *
         * @param w                Width in pixels.
         * @param h                Height in pixels.
         * @param depthFormat      Raw `DepthFormat` ordinal; `None` allocates no depth storage.
         * @param registry         Context-loss registry this target registers with, or nullptr.
         * @param binding          REMED-GFX-168: the creating renderer's shared binding record, so
         *                         this target can detach itself from it when destroyed. Held as a
         *                         `weak_ptr`, so a target outliving its graphics renderer detaches
         *                         from nothing instead of touching freed storage.
         * @param mipMap           Whether a full mip chain is allocated and regenerated on unbind.
         * @param multiSampleCount Requested sample count, clamped to `GL_MAX_SAMPLES`.
         * @param surfaceFormat    plan_modern.md MOD-115: raw
         *                         `Microsoft::Xna::Framework::Graphics::SurfaceFormat` ordinal for
         *                         the colour attachment. `Color` (the default) keeps the historical
         *                         8-bit RGBA storage exactly as it was; the float formats allocate
         *                         real R/RG/RGBA 16F/32F storage, which is what lets an HDR scene
         *                         keep values above 1.0 instead of clamping them at draw time.
         */
        EasyGLRenderTargetRenderer(int w, int h, int depthFormat, ::easygl::ResourceRegistry* registry,
                                   std::weak_ptr<EasyGLBoundTargetEXT> binding,
                                   bool mipMap = false, int multiSampleCount = 0,
                                   int surfaceFormat = 0);
        ~EasyGLRenderTargetRenderer() override;

        int GetWidth()  const override { return width_; }
        int GetHeight() const override { return height_; }

        void BindGL(int unit) const override;

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
        /// plan_modern.md MOD-115: the raw SurfaceFormat ordinal this target's colour storage was
        /// actually created with. Equal to what was requested -- an unsupported format is refused at
        /// creation rather than substituted, so this can never disagree with the caller's request.
        [[nodiscard]] int GetSurfaceFormatEXT() const { return surfaceFormat_; }
        [[nodiscard]] bool HasRealDepthBuffer(bool depthFormatWasRequested) const override
        {
            return depthFormatWasRequested && depthFormat_ != 0;
        }

        void release_gl_handle_only() override;
        void recreate_gl_resource()   override;

    private:
        friend class EasyGLRenderer;

        void CreateResources();
        /** @brief Resolves this target's multisample colour storage into its public texture. */
        void ResolveColorEXT(const char* traceEvent) const;
        /// REMED-GFX-168: this target's own native identities, for `CNA_EASYGL_TARGET_TRACE`.
        [[nodiscard]] std::string TraceNativeDetailEXT() const;
        /// REMED-GFX-168: clears every slot of the shared binding record that names this target.
        void DetachFromBindingEXT();
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
        int  surfaceFormat_    = 0;  ///< Raw Microsoft::Xna::Framework::Graphics::SurfaceFormat ordinal.
        bool mipMap_           = false;
        int  levelCount_       = 1;
        int  multiSampleCount_ = 0;
        ::easygl::ResourceRegistry* registry_ = nullptr;
        /// REMED-GFX-168: the creating renderer's binding record, so the destructor can detach.
        std::weak_ptr<EasyGLBoundTargetEXT> binding_;
    };

    /// EasyGL cube-map render target: one FBO per face, shared cube-map texture.
    class EasyGLRenderTargetCubeRenderer : public IRenderTargetCubeRenderer,
                                           public ::easygl::RecoverableResource
    {
    public:
        /**
         * @brief Creates the shared cube texture and its render FBO.
         *
         * @param size             Edge length of each face in pixels.
         * @param depthFormat      Raw `DepthFormat` ordinal; `None` allocates no depth storage.
         * @param registry         Context-loss registry this target registers with, or nullptr.
         * @param binding          REMED-GFX-168: the creating renderer's shared binding record. See
         *                         `EasyGLRenderTargetRenderer`'s identical parameter.
         * @param mipMap           Whether a full mip chain is allocated and regenerated on unbind.
         * @param multiSampleCount Requested sample count, clamped to `GL_MAX_SAMPLES`.
         */
        EasyGLRenderTargetCubeRenderer(int size, int depthFormat, ::easygl::ResourceRegistry* registry,
                                       std::weak_ptr<EasyGLBoundTargetEXT> binding,
                                       bool mipMap = false, int multiSampleCount = 0,
                                       int surfaceFormat = 0);
        ~EasyGLRenderTargetCubeRenderer() override;

        [[nodiscard]] int GetSize() const override { return size_; }
        void BindAsRenderTargetFace(int face) override;
        void UnbindAsRenderTarget() override;
        [[nodiscard]] unsigned int GetGLHandle() const override;
        [[nodiscard]] int GetMultiSampleCount() const override { return multiSampleCount_; }

        // ITextureCubeRenderer — bind and upload to the shared cube texture.
        void BindGL(int unit) const override;
        /**
         * @brief Uploads CPU pixels into a rendered cube face's mip level.
         *
         * REMED-GFX-135: the only render-target cube in CNA that implements this rather than
         * inheriting `IRenderTargetCubeRenderer::SetData`'s refusal -- the FBO's colour attachment
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
         * REMED-GFX-134. `EasyGLTextureCubeRenderer::GetData`'s mechanism -- attach the requested
         * face/level to a temporary FBO and glReadPixels it -- with the one difference a rendered
         * face has: this content came from rasterization, so it is stored bottom-up in the face's
         * texel grid exactly as `EasyGLRenderTargetRenderer::GetData` already documents for a 2D
         * target. The requested rectangle is therefore mapped into bottom-up coordinates and the
         * returned rows are flipped back, so the public result is top-row-first like every other
         * renderer. Applied exactly once, and only here: the plain-cube path stays unflipped
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
        /// REMED-GFX-168: this cube's own native identities, for `CNA_EASYGL_TARGET_TRACE`.
        [[nodiscard]] std::string TraceNativeDetailEXT() const;
        /// REMED-GFX-168: clears the shared binding record if it still names this cube.
        void DetachFromBindingEXT();

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
        int  surfaceFormat_    = 0;  ///< Raw Microsoft::Xna::Framework::Graphics::SurfaceFormat ordinal.
        bool mipMap_           = false;
        int  levelCount_       = 1;
        int  multiSampleCount_ = 0;
        int  lastFace_         = 0;  ///< Most recently bound face, used by UnbindAsRenderTarget's resolve.
        ::easygl::ResourceRegistry* registry_ = nullptr;
        /// REMED-GFX-168: the creating renderer's binding record, so the destructor can detach.
        std::weak_ptr<EasyGLBoundTargetEXT> binding_;
    };

    /// EasyGL 3D (volume) texture renderer.
    class EasyGLTexture3DRenderer : public ITexture3DRenderer
    {
    public:
        EasyGLTexture3DRenderer(int w, int h, int depth, bool mipMap, int surfaceFormat);
        ~EasyGLTexture3DRenderer() override = default;

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

        /// Binds this volume texture to the requested GL texture unit.
        void BindGL(int unit) const override;

    private:
        ::easygl::Texture tex_;
        int width_  = 0;
        int height_ = 0;
        int depth_  = 0;
        /// Mip levels this texture really allocated storage for (REMED-GFX-135).
        int levelCount_ = 1;
    };

    /// EasyGL cube map texture renderer.
    class EasyGLTextureCubeRenderer : public ITextureCubeRenderer
    {
    public:
        EasyGLTextureCubeRenderer(int size, bool mipMap, int surfaceFormat);
        /** @brief Releases the GL cube texture (and, under OPENGLES2, its level-registry entry). */
        ~EasyGLTextureCubeRenderer() override;

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

        /// Binds this cube map to the requested GL texture unit.
        void BindGL(int unit) const override;

    private:
        ::easygl::Texture tex_;
        int size_ = 0;
        /// Mip levels this cube really allocated storage for (REMED-GFX-135).
        int levelCount_ = 1;
    };

    /// EasyGL implementation of IEffectRenderer — wraps an easygl::Program.
    class EasyGLEffectRenderer : public IEffectRenderer
    {
    public:
        explicit EasyGLEffectRenderer() = default;
        ~EasyGLEffectRenderer() override = default;

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
        void SetUniformVec3Array(const char* name, const float* values, int count) override;
        void BindTexture(int unit, ITextureRenderer* texture) override;
        void BindTextureCube(int unit, ITextureCubeRenderer* texture) override;
        void BindTexture3D(int unit, ITexture3DRenderer* texture) override;

        /// Returns the underlying compiled program, so a renderer (e.g. SpriteBatch) can bind
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

    class EasyGLOcclusionQueryRenderer : public IOcclusionQueryRenderer, public ::easygl::RecoverableResource
    {
    public:
        explicit EasyGLOcclusionQueryRenderer(::easygl::ResourceRegistry* registry);
        ~EasyGLOcclusionQueryRenderer() override;

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

    class EasyGLSpriteBatchRenderer : public ISpriteBatchRenderer, public ::easygl::RecoverableResource
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
        EasyGLRenderer* graphicsRenderer_ = nullptr;

        // Batching state: quads are accumulated between Begin()/End() and
        // flushed in one draw call. A flush also occurs when the texture changes.
        std::vector<Vertex>   pending_vertices_;
        std::vector<uint16_t> pending_indices_;
        const ITextureRenderer* current_texture_ = nullptr;
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
        explicit EasyGLSpriteBatchRenderer(::easygl::Device& device, ::easygl::ResourceRegistry* registry,
                                          EasyGLRenderer* renderer = nullptr);
        ~EasyGLSpriteBatchRenderer() override;

        void Begin() override;
        void End() override;
        void SetTransformMatrix(const Matrix& m) override;
        void SetCustomEffect(Effect* effect) override;
        void SetSamplerFilter(int textureFilter) override;
        void SetSamplerAddressMode(int addressU, int addressV) override;
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
    class EasyGLVertexBufferRenderer : public IVertexBufferRenderer, public ::easygl::RecoverableResource
    {
    public:
        ::easygl::Buffer vbo;
        ::easygl::VertexArray vao;
        int vertex_count = 0;
        int capacity = 0;

        explicit EasyGLVertexBufferRenderer(int vertex_capacity, ::easygl::ResourceRegistry* registry);
        ~EasyGLVertexBufferRenderer() override;
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
    class EasyGLIndexBufferRenderer : public IIndexBufferRenderer, public ::easygl::RecoverableResource
    {
    public:
        ::easygl::Buffer ibo;
        int index_count = 0;
        int capacity = 0;

        bool thirtyTwoBit = false;

        explicit EasyGLIndexBufferRenderer(int index_capacity, bool thirtyTwoBit,
                                          ::easygl::ResourceRegistry* registry);
        ~EasyGLIndexBufferRenderer() override;
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

    class EasyGLRenderer : public IGraphicsRenderer
    {
    private:
        // Declared first so it is destroyed last: all GL resources below release while the
        // platform context is still current and alive.
        std::unique_ptr<EasyGLPlatformContext> platformContext_;
#if defined(CNA_EASYGL_COMPILED_EFFECTS)
        // plan_fx.md FX-062: one MojoShader GL context per this renderer's whole lifetime, created
        // lazily on first CreateCompiledEffect() call (see GetMojoShaderContextEXT() in
        // EasyGLCompiledEffect.cpp).
        MOJOSHADER_glContext* mojoShaderContext_ = nullptr;
#endif
        EasyGLSurfaceState surfaceState_;
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

        static constexpr int kMaxSamplerSlots = 16;
        ::easygl::Sampler samplers_[kMaxSamplerSlots];
        bool contextRecoveryEnabled_ = true;
        int swapInterval_ = 1;
        /// plan_runtimerenderer.md P11: which of EasyGL's five GL identities this instance serves.
        GlProfile profile_ = kCompileTimeGlProfile;

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
            int loc_lighting_enabled = -1;  ///< BasicEffect unlit-vs-lit shader branch
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
            int loc_pbr_specularmap = -1; ///< KHR_materials_specular strength map (A channel)
            int loc_pbr_specularcolormap = -1; ///< KHR_materials_specular colour map (RGB)
            int loc_pbr_metallic    = -1;  ///< float metallic factor (PbrEffect only)
            int loc_pbr_roughness   = -1;  ///< float roughness factor (PbrEffect only)
            /// plan_gltf.md GLTF-343/344: xyz = dielectric F0, w = dielectric F90.
            int loc_pbr_dielectric_fresnel = -1;
            /// GLTF-344: xyz = unclamped IOR F0 * specularColorFactor, w = specularFactor.
            int loc_pbr_specular_fresnel_inputs = -1;
            /// plan_gltf.md GLTF-210/GLTF-212: vec3 colour-management gate (PbrEffect only).
            /// x = decode the base-colour sample from sRGB, y = decode the emissive sample,
            /// z = encode the fragment's RGB back to sRGB. Each is 0 or 1 and multiplies a
            /// `mix()` rather than driving a branch, so every fragment costs the same.
            int loc_pbr_srgb        = -1;
            /// plan_gltf.md GLTF-224: float normalTexture.scale (PbrEffect only).
            int loc_pbr_normalscale = -1;
            /// plan_gltf.md GLTF-225: float occlusionTexture.strength (PbrEffect only).
            int loc_pbr_occlstrength = -1;
            /// plan_gltf.md GLTF-182/183: vec4 UV1 selectors for PBR slots 0-3.
            int loc_pbr_texcoordsets = -1;
            /// plan_gltf.md GLTF-182/183: UV1 selector for PBR occlusion slot 4.
            int loc_pbr_occlusiontexcoordset = -1;
            /// GLTF-344: UV1 selectors for specular strength/colour slots 5 and 6.
            int loc_pbr_specular_texcoordsets = -1;
            /// plan_gltf.md GLTF-184: ten vec4 affine rows, two for each PBR texture slot.
            std::array<int, 10> loc_pbr_texture_transform_rows{
                -1, -1, -1, -1, -1, -1, -1, -1, -1, -1};
            /// GLTF-344: two affine rows for each specular texture, kept ABI-separate from slots 0-4.
            std::array<int, 4> loc_pbr_specular_texture_transform_rows{-1, -1, -1, -1};
            int loc_rt_flip_v       = -1;  ///< REMED-GFX-147: vec4 render-target V-flip flags for texture units 0-3
            int loc_rt_flip_v_hi    = -1;  ///< REMED-GFX-147: units 4-6 in xyz (PbrEffect only)
            int loc_instanced       = -1;  ///< REMED-GFX-122: stock-program per-instance matrix gate
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
        Prog3D prog_pbr_;            ///< stride=48: legacy single-UV PBR program
        Prog3D prog_pbr_dual_uv_;    ///< stride=60: PBR program with aUV1 and per-map selection
        Prog3D prog_pbr_skinned_;    ///< stride=68: legacy single-UV skinned PBR program
        Prog3D prog_pbr_skinned_dual_uv_;  ///< stride=76: skinned PBR with aUV1 selection

        ::easygl::Texture default_white_texture_;
        bool default_white_texture_ready_ = false;
        ::easygl::Texture default_flat_normal_texture_;      ///< PbrEffect NormalMap fallback (CNB-58)
        bool default_flat_normal_texture_ready_ = false;

        // One reusable MRT FBO. Every bind replaces all ordered attachments; target identity is
        // deliberately not part of any shader/pipeline cache.
        ::easygl::Framebuffer mrtFbo_;
        int maxMrtTargets_ = 1;
        void FinalizeCurrentMRT();

        // Which single RenderTarget2D / RenderTargetCube face / ordered MRT set is currently bound,
        // and its extent. Nothing else in this renderer invokes UnbindAsRenderTarget(), so switching
        // away from a destination is the only thing that runs its MSAA resolve and mip regeneration
        // (Task 336) — which is why the outgoing target's identity has to be remembered at all.
        //
        // REMED-GFX-168: separately allocated and SHARED with every render target this renderer
        // creates, so a target destroyed while it is still bound clears its own slot here instead of
        // leaving a dangling pointer for the next transition to dispatch through. See
        // EasyGLBoundTargetEXT for why the record is shared rather than owned outright, and why it
        // holds identity and extent but no GL handles.
        std::shared_ptr<EasyGLBoundTargetEXT> bound_ = std::make_shared<EasyGLBoundTargetEXT>();

        /// REMED-GFX-168: the binding record as pointer VALUES only, for `CNA_EASYGL_TARGET_TRACE`.
        /// Never dereferences a recorded target -- one of them may already be destroyed storage,
        /// which is precisely what the trace exists to record.
        [[nodiscard]] std::string TraceBindingDetailEXT() const;

        /// plan_modern.md MOD-117: asks GL, once per kind, whether a colour attachment of the
        /// 32-bit (@p fullFloat) or 16-bit float format is framebuffer-complete on this context.
        /// Restores the previously bound framebuffer and drains the error queue before returning.
        [[nodiscard]] bool ProbeFloatRenderTargetSupportEXT(bool fullFloat) const;

        /// Cached results of that probe. Mutable because the query is const and a caller may make
        /// it per frame; the answer cannot change without a new GL context, and a new context means
        /// a new renderer.
        mutable std::optional<bool> probedFullFloatRenderable_;
        mutable std::optional<bool> probedHalfFloatRenderable_;

        // FillMode::WireFrame emulation (OpenGL ES has no glPolygonMode):
        // when active, triangle draws are re-expanded into GL_LINES.
        bool wireframe_ = false;
        ::easygl::Buffer wireframeIbo_;        ///< scratch element buffer of line indices
        bool wireframeIboCreated_ = false;
        std::vector<std::uint32_t> wireframeScratch_;  ///< CPU build buffer (32-bit line indices)

        // Draw the given triangle geometry as a wireframe (GL_LINES). Returns false when the
        // primitive is not a triangle list/strip (caller should fall back to a normal draw).
        // ib == nullptr means a non-indexed draw (sequential vertices from firstVertex).
        bool DrawWireframe(const EasyGLVertexBufferRenderer& vb,
                           const EasyGLIndexBufferRenderer* ib,
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
        void EnsurePbrProgram(bool dualUv);
        void EnsurePbrSkinnedProgram(bool dualUv);
        void EnsureDefaultWhiteTexture();
        void EnsureDefaultFlatNormalTexture();
        /// REMED-GFX-218: which stock program a draw gets. SelectProgram() and
        /// RequireDeclarationFitsStockProgramEXT() both read this single cascade, so the program a
        /// draw is bound to and the input shape it is checked against cannot drift apart.
        enum class StockProgramShape
        {
            PbrSkinned, Pbr, SkinnedVertexLit, Skinned, EnvMapped,
            DualTexturedColored, DualTextured, Textured, ColoredTextured,
            LitVertexLit, Lit, Colored
        };
        static StockProgramShape SelectStockProgramShape(std::size_t stride,
                                                          const GpuDrawParams& params);
        Prog3D& SelectProgram(std::size_t stride, const GpuDrawParams& params);
        /// REMED-GFX-DECL-GUARD: throws `System::NotSupportedException` when @p declaredElements
        /// would bind an element to a stock attribute location that means something else. Runs
        /// before any program is selected, bound or drawn, and never touches a custom
        /// `ShaderEffect` draw -- those keep their own documented element-index convention.
        static void RequireDeclarationFitsStockProgramEXT(
            const std::vector<VertexElement>& declaredElements, std::size_t stride,
            const GpuDrawParams& params);
        void BindDrawParams(Prog3D& p, const Matrix& world, const Matrix& view,
                            const Matrix& projection, const GpuDrawParams& params);
        /// REMED-GFX-147: resolves uRtFlipV/uRtFlipVHi for a freshly linked stock 3D program.
        static void ResolveRenderTargetOrientationUniforms(Prog3D& p);

    public:
        /**
         * @brief Constructs the renderer for one of EasyGL's five GL profiles.
         *
         * plan_runtimerenderer.md phase P11: the profile is a constructor argument rather than a
         * compile definition, which is what lets two GL identities coexist in one binary. It
         * defaults to the profile this build was configured for, so a single-renderer build is
         * unaffected.
         *
         * @param surface The platform surface to present into.
         * @param glContext The platform GL context service this renderer drives.
         * @param virtualWidth Logical presentation width; 0 means unset.
         * @param virtualHeight Logical presentation height; 0 means unset.
         * @param mode Presentation/scaling policy.
         * @param contextRecoveryEnabled Whether to keep CPU-side copies for context-loss recovery.
         * @param multiSampleCount Requested MSAA sample count.
         * @param swapInterval Swap interval (0 immediate, 1 VSync, 2 half-rate).
         * @param profile Which GL profile to create the context and shaders for.
         */

        /**
         * @brief The GL profile this renderer instance was created for.
         *
         * @return The profile.
         */
        [[nodiscard]] GlProfile Profile() const { return profile_; }
        explicit EasyGLRenderer(
            const RendererSurfaceInfo& surface, CNA::Platform::IPlatformGlContext& glContext,
            int virtualWidth = 0, int virtualHeight = 0,
            CnaPresentationMode mode = CnaPresentationMode::FixedHeightDynamicWidth,
            bool contextRecoveryEnabled = true, int multiSampleCount = 1,
            int swapInterval = 1, GlProfile profile = kCompileTimeGlProfile);
        ~EasyGLRenderer() override;

#if defined(CNA_EASYGL_COMPILED_EFFECTS)
        /**
         * @brief Parses a compiled XNA effect for this device (plan_fx.md FX-062).
         * @param effectCode Compiled effect bytes.
         * @param effectCodeBytes Number of bytes at @p effectCode.
         * @return The runtime, or null if MojoShader has no context for this device.
         */
        std::unique_ptr<ICompiledEffectRuntime> CreateCompiledEffect(
            const std::uint8_t* effectCode, std::size_t effectCodeBytes) override;

        /**
         * @brief True: this renderer executes compiled XNA Effect Framework bytecode
         * (plan_fx.md FX-062). Ordinary 3D draws have a working compiled-effect route, verified by
         * a real golden-pixel test and the FX-060 shared conformance suite. Still refused
         * explicitly rather than silently mishandled: a compiled effect's vertex shader sampling a
         * texture, a 3D/cube (not 2D) sampler binding, sampler state translation (a bound texture's
         * own GL creation-time filter/wrap parameters apply instead of the effect's declared
         * sampler_state block), and instanced/multi-stream compiled-effect draws.
         * @return true.
         */
        [[nodiscard]] bool SupportsCompiledEffects() const override { return true; }

        /**
         * @brief CNAEXT. Returns this device's MojoShader context, creating it on first use.
         *
         * MojoShader allows one context per GL context, so it is owned here rather than by each
         * effect. Unlike SDL_GPU, EasyGL's GL context can be recreated (context-loss recovery);
         * this initial implementation does not yet recreate the MojoShader context to follow --
         * a documented, narrower scope than the ordinary (non-compiled-effect) draw path's own
         * recovery support.
         * @return The context, or null if it could not be created.
         */
        CNAEXT [[nodiscard]] MOJOSHADER_glContext* GetMojoShaderContextEXT();

        /**
         * @brief CNAEXT. Returns the raw GL function-pointer loader this renderer's platform
         * context resolves every native GL call through, so MojoShader's OpenGL adapter can look
         * its own functions up the same way (EasyGLCompiledEffect.cpp, a different translation
         * unit from where EasyGLPlatformContext is defined).
         */
        CNAEXT [[nodiscard]] CNA::Platform::GlProcAddressLoader GetProcAddressLoaderEXT() const;

        /**
         * @brief CNAEXT. Binds a compiled effect's currently-applied-pass shader program, vertex
         * attributes and pixel-stage sampler textures, and pushes its uniforms -- everything a
         * compiled-effect draw needs immediately before issuing the actual GL draw call.
         *
         * plan_fx.md FX-062: EasyGL draws immediately (no `Present()`-deferred queue), so this is
         * called directly from `DrawPrimitivesEx`/`DrawIndexedPrimitivesEx`'s own compiled-effect
         * branch rather than captured for later replay the way SDL_GPU's draw route has to.
         *
         * @param declaredElements The caller's `VertexDeclaration` elements for this draw.
         * @param stride The vertex buffer's byte stride (`glVertexAttribPointer`'s own stride).
         * @param runtime The applied compiled effect.
         * @throws std::runtime_error if the applied pass bound no shader pair, or @p runtime was
         *         not created by this renderer.
         * @throws System::NotSupportedException if @p declaredElements does not supply an input
         *         the vertex shader consumes, the vertex shader itself samples a texture, or a
         *         reflected pixel-stage sampler has no texture bound.
         */
        CNAEXT void BindCompiledEffectForDrawEXT(
            const std::vector<Microsoft::Xna::Framework::Graphics::VertexElement>& declaredElements,
            std::size_t stride, ICompiledEffectRuntime& runtime);
#endif
        // AnisotropicFiltering/MultiSampleAntiAliasing re-query the same live GL state the
        // startup capability dump (EnsureGL()) already prints, since they're cheap, idempotent GL
        // queries -- no need to cache them. WireFrame is implemented through measured triangle
        // edge re-expansion because GLES3 has no polygon-mode wireframe. Everything else
        // CNA::GraphicsCapability currently enumerates is genuinely supported here, so falls
        // through to the shared default (true).
        [[nodiscard]] bool SupportsCapability(CNA::GraphicsCapability capability) const override;
        void Clear(float r, float g, float b, float a) override;
        void Present() override;
        void SetSwapInterval(int interval) override;
        void OnSurfaceChanged(const RendererSurfaceInfo& surface) override;
        void GetViewportSize(int& width, int& height) override;
        void getLogicalSize(int& width, int& height) const;
        void getPhysicalSize(int& width, int& height) const;
        /// Returns the currently-bound single 2D render target's size, if one is bound (Task
        /// 1078). Used by EasyGLSpriteBatchRenderer so a custom-effect draw into a RenderTarget2D
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
        // ApplyMultiSampleCount() uses IGraphicsRenderer's default (echoes back the current,
        // already-applied value, ignoring the request). GetMultiSampleCount() reports that real
        // value honestly instead of falling back to the interface default of 0.
        [[nodiscard]] int GetMultiSampleCount() const override { return sampleCount_ > 1 ? sampleCount_ : 0; }

        std::unique_ptr<ITextureRenderer> CreateTexture(const ImageData& data) override;
        std::unique_ptr<ISpriteBatchRenderer> CreateSpriteBatch() override;
        std::unique_ptr<IOcclusionQueryRenderer> CreateOcclusionQuery() override;
        std::unique_ptr<IRenderTargetRenderer> CreateRenderTarget2D(int w, int h, int depthFormat, bool preserveContents = false, bool mipMap = false, int multiSampleCount = 0) override;
        /// plan_modern.md MOD-115: creates the colour attachment in the requested SurfaceFormat --
        /// real R/RG/RGBA 16F/32F storage for the float formats, unchanged 8-bit RGBA for Color.
        /// Throws for a format this GL context cannot render to, rather than substituting Color the
        /// way the shared default does; ask SupportsRenderTargetFormat() first.
        std::unique_ptr<IRenderTargetRenderer> CreateRenderTarget2DEXT(
            int w, int h, int depthFormat, bool preserveContents, bool mipMap,
            int multiSampleCount, int surfaceFormat) override;
        /// plan_modern.md MOD-104/MOD-117: this renderer's own verdict on a render-target
        /// SurfaceFormat. Color is Supported unconditionally; the float formats are answered by a
        /// cached runtime framebuffer-completeness probe, because their availability is a property
        /// of the driver and its extensions rather than of the compile-time GL profile. Every other
        /// format defers to the framework rule, unchanged.
        [[nodiscard]] RendererFormatVerdict ClassifyRenderTargetFormatEXT(int surfaceFormat) const override;
        /// plan_modern.md MOD-123: half-float texture filtering. Core from OpenGL ES 3.0 and
        /// desktop GL 3.0 onward; on the ES 2.0 API generation it needs an extension that this
        /// renderer does not rely on, so it is reported false there.
        [[nodiscard]] bool SupportsHalfFloatTextureLinearFilteringEXT() const override;
        std::unique_ptr<IRenderTargetCubeRenderer> CreateRenderTargetCube(int size, int depthFormat, bool preserveContents = false, bool mipMap = false, int multiSampleCount = 0) override;
        /// plan_modern.md MOD-107: the cube counterpart of CreateRenderTarget2DEXT -- real float
        /// storage for the formats this context can render to, and a refusal for the rest.
        std::unique_ptr<IRenderTargetCubeRenderer> CreateRenderTargetCubeEXT(
            int size, int depthFormat, bool preserveContents, bool mipMap,
            int multiSampleCount, int surfaceFormat) override;
        std::unique_ptr<ITexture3DRenderer> CreateTexture3D(int w, int h, int depth, bool mipMap, int surfaceFormat) override;
        std::unique_ptr<ITextureCubeRenderer> CreateTextureCube(int size, bool mipMap, int surfaceFormat) override;
        std::unique_ptr<IEffectRenderer> CreateEffectRenderer(const std::string& vertSrc,
                                                             const std::string& fragSrc) override;
        void SetRenderTarget2D(IRenderTargetRenderer* rt) override;
        void SetRenderTargetCubeFace(IRenderTargetCubeRenderer* rt, int face) override;
        void SetRenderTargets(const RenderTargetBindingDescriptor* renderTargets,
                              int count) override;
        std::unique_ptr<IIndexBufferRenderer> CreateIndexBuffer16(int index_capacity) override;
        std::unique_ptr<IIndexBufferRenderer> CreateIndexBuffer32(int index_capacity) override;

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
        std::unique_ptr<IVertexBufferRenderer> CreateVertexBuffer(int vertex_capacity) override;
        void DrawColoredPrimitives(const IVertexBufferRenderer& vb,
                                   const Matrix& world, const Matrix& view, const Matrix& projection,
                                   PrimitiveType primitive, int primitiveCount) override;
        void DrawIndexedColoredPrimitives(const IVertexBufferRenderer& vb,
                                          const IIndexBufferRenderer& ib,
                                          const Matrix& world, const Matrix& view, const Matrix& projection,
                                          PrimitiveType primitive, int primitiveCount) override;
        void DrawPrimitivesEx(const IVertexBufferRenderer& vb,
                              const Matrix& world, const Matrix& view, const Matrix& projection,
                              PrimitiveType primitive, int primitiveCount,
                              const GpuDrawParams& params) override;
        void DrawIndexedPrimitivesEx(const IVertexBufferRenderer& vb,
                                     const IIndexBufferRenderer& ib,
                                     const Matrix& world, const Matrix& view, const Matrix& projection,
                                     PrimitiveType primitive, int primitiveCount,
                                     const GpuDrawParams& params) override;
        void DrawInstancedPrimitivesEx(const IVertexBufferRenderer& vb,
                                       const IIndexBufferRenderer& ib,
                                       const Matrix& world, const Matrix& view, const Matrix& projection,
                                       PrimitiveType primitive, int primitiveCount,
                                       int instanceCount,
                                       const GpuDrawParams& params) override;
    };

    /**
     * @brief Creates an EasyGL renderer for the build's default GL profile.
     *
     * plan_runtimerenderer.md design decision 4: declared in the FAMILY's namespace so several
     * renderer archives can link into one binary. Declared here, alongside the class, for the same
     * reason the GDI family declares its own (GdiRenderer.hpp): this family's device-free suites
     * and contract programs construct a renderer directly, without going through GraphicsDevice,
     * and since RTR-P9-9 they compile whenever the family is PRESENT rather than only when it is
     * the default.
     *
     * @param args Construction arguments.
     * @return The new renderer; never nullptr on success. Throws on failure.
     */
    std::unique_ptr<IGraphicsRenderer> CreateGraphicsRenderer(const GraphicsRendererCreateArgs& args);

    /**
     * @brief Creates an EasyGL renderer for an explicit GL profile.
     *
     * plan_runtimerenderer.md P11: each of the five public GL identities this family serves reaches
     * this with its own profile, which is what lets all five be compiled into one binary.
     *
     * @param args Construction arguments.
     * @param profile The GL profile to create the context and shaders for.
     * @return The new renderer; never nullptr on success. Throws on failure.
     */
    std::unique_ptr<IGraphicsRenderer> CreateGraphicsRendererForProfile(
        const GraphicsRendererCreateArgs& args, GlProfile profile);
}
