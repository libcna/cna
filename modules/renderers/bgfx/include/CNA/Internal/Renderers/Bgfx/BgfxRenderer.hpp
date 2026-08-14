#pragma once

#include "CNA/Internal/Renderers/Common/IGraphicsRenderer.hpp"
#include <bgfx/bgfx.h>
#include <SDL3/SDL.h>
#include <array>
#include <cstdarg>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace CNA::Internal::Renderers::Bgfx
{
    namespace Detail
    {
        bgfx::RendererType::Enum GetDefaultRendererType();
        bgfx::RendererType::Enum ParseRendererTypeOverride(const char* value);
        bgfx::RendererType::Enum ResolveRendererType(const char* value);

        // Task 951: the single highest bgfx view id (255) is permanently reserved as a dedicated
        // "backbuffer flush" view -- never handed out for public rendering. bgfx processes every
        // configured view in ascending id order each frame, so whichever view was configured last
        // (highest id) is left as the bound GL framebuffer once that processing finishes; a real,
        // concurrently-bound render target's view (ids [1,254]) would otherwise be that last view
        // whenever one is active in the same frame, corrupting/crashing BgfxRenderer::
        // ReadBackbuffer()'s bgfx::requestScreenShot()-based glReadPixels. Explicitly binding this
        // reserved view to the backbuffer and bgfx::touch()-ing it right before that screenshot
        // request guarantees the real backbuffer is what ends up GL-bound, without ever touching
        // (and so without ever discarding the still-pending, unflushed draws queued against) any
        // render target's own view.
        inline constexpr bgfx::ViewId kBackbufferFlushViewId = 255;

        // REMED-GFX-065: bgfx view state (setViewRect / setViewTransform) is PER-VIEW (resolved once
        // at bgfx::frame(), last-write-wins), so two DIFFERENT GraphicsDevice.Viewport states drawn to
        // one target within a frame would collapse onto its single base view id -- the first draw's
        // viewport is lost. The fix allocates an ordered per-FRAME view id whenever the viewport
        // changes on a target, keeping each draw under its own rect/ortho while bgfx's ascending-id
        // execution preserves submission order. REMED-GFX-179 removes the later fixed split between
        // persistent target-base IDs and ephemeral segment IDs. A view is routing state for ONE
        // frame, not a descriptor owned by a render target: never-bound targets need no view at
        // all, and a target used in a later frame may reuse the same numeric id safely. View 0 is
        // retained only for the first backbuffer operation of a frame. Every render-target bind
        // cycle, later backbuffer cycle, viewport/transform transition, and ordered Clear draws
        // monotonically from the unified per-frame range [1,255). Thus simultaneous LIVE target
        // count is limited only by native resource pools, while the actual view limit applies to
        // distinct ordered view states USED during one frame. The reserved readback/flush id 255
        // remains excluded, IDs cannot collide within a frame, and the cursor is reclaimed after
        // every bgfx::frame().
        inline constexpr bgfx::ViewId kFirstPublicFrameViewId = 1;

        // REMED-GFX-155: bgfx's compile-time view-id count (BGFX_CONFIG_MAX_VIEWS). Every id in
        // [0, kMaxViews) is a legal view. Named rather than a literal so the per-frame bookkeeping
        // and the id space it covers cannot drift apart.
        inline constexpr int kMaxViews = 256;
        static_assert(kBackbufferFlushViewId == kMaxViews - 1,
                      "the reserved backbuffer-flush view must be the HIGHEST view id, so bgfx's "
                      "ascending-id execution always leaves it last (Task 951)");
        static_assert(kFirstPublicFrameViewId < kBackbufferFlushViewId,
                      "public frame views must sit below the reserved flush/readback view");

        // REMED-GFX-158: `bgfx::reset()` ends by discarding EVERY view's framebuffer binding --
        //
        //     for (uint32_t ii = 0; ii < BGFX_CONFIG_MAX_VIEWS; ++ii)
        //         m_view[ii].setFrameBuffer(BGFX_INVALID_HANDLE);   // bgfx_p.h, Context::reset
        //
        // including the binding just programmed for the render target that is bound RIGHT NOW.
        // bgfx resolves view state once, at bgfx::frame(), so the operations already submitted
        // against that view do not follow the target they were aimed at: they resolve against the
        // backbuffer and the target is never written. That is why a RenderTarget2D constructed and
        // rendered into in the same public frame could lose that frame -- this renderer calls
        // bgfx::reset() the moment it notices the SDL window's size differs from the size bgfx was
        // initialised with, which for a brand-new target's first bind cycle happens between the
        // bind and the draw. It is not deferred resource creation and not a bgfx frame latency:
        // the texture and framebuffer are both complete before the frame's draws are submitted
        // (bgfx.cpp, Context::renderFrame executes m_cmdPre first).
        //
        // The correction is to make the loss recoverable rather than to avoid the reset: every
        // view->framebuffer binding this renderer programs goes through SetViewFrameBufferEXT,
        // which mirrors it, and ResetBackbufferEXT replays the mirror straight after the reset,
        // restoring exactly what the reset discarded. Nothing is deferred, no frame is advanced.
        // A framebuffer must be handed to ForgetFrameBufferEXT before it is destroyed, so a
        // recycled bgfx handle index can never be replayed as a resurrected binding.

        /**
         * @brief Programs a view's framebuffer and records it, so a later reset can restore it.
         *
         * @param id View id to program.
         * @param fb Framebuffer to bind, or BGFX_INVALID_HANDLE for the backbuffer.
         */
        void SetViewFrameBufferEXT(bgfx::ViewId id, bgfx::FrameBufferHandle fb);

        /**
         * @brief Drops @p fb from the mirror. Must be called before destroying a framebuffer.
         *
         * @param fb The framebuffer about to be destroyed. An invalid handle is ignored.
         */
        void ForgetFrameBufferEXT(bgfx::FrameBufferHandle fb);

        /**
         * @brief Applies a real renderer backbuffer reset, or records the no-backbuffer Noop reset.
         *
         * REMED-GFX-196: Noop deliberately advertises no texture format with
         * `BGFX_CAPS_FORMAT_TEXTURE_BACKBUFFER`; it has no native backbuffer to resize. Issuing
         * `bgfx::reset` anyway reaches bgfx's fatal format assertion before its unchanged-reset
         * early return. The Noop path therefore performs no native reset, while every renderer
         * with a real backbuffer is capability-checked before the same reset-and-restore sequence.
         *
         * @param width  New backbuffer width.
         * @param height New backbuffer height.
         * @param flags  bgfx reset flags.
         */
        void ResetBackbufferEXT(uint16_t width, uint16_t height, uint32_t flags);

        /**
         * @brief Normalizes a bgfx native render-target MSAA limit to CNA's supported counts.
         *
         * @param nativeLimit Exact maximum selected by the active bgfx renderer.
         * @param colorSupported Whether the colour format advertises framebuffer MSAA.
         * @param depthSupported Whether the requested depth format advertises framebuffer MSAA.
         * @return One of 0, 2, 4, 8 or 16; zero when either attachment lacks support.
         */
        int NormalizeRenderTargetMsaaLimitEXT(uint32_t nativeLimit, bool colorSupported,
                                              bool depthSupported);
    }

    class BgfxRenderer;

    /// bgfx callback implementation — captures screenshot data for ReadBackbuffer.
    /// All methods other than fatal() and screenShot() are no-ops.
    struct BgfxCnaCallback : public bgfx::CallbackI
    {
        std::vector<uint8_t> screenshotBytes;
        uint32_t screenshotWidth  = 0;
        uint32_t screenshotHeight = 0;
        uint32_t screenshotPitch  = 0;
        bool     screenshotYFlip  = false;
        bool     screenshotReady  = false;

        void fatal(const char* _file, uint16_t _line,
                   bgfx::Fatal::Enum _code, const char* _str) override;
        /**
         * @brief Forwards bgfx's own trace and warning stream to stderr when asked to.
         *
         * REMED-GFX-154: this was an unconditional no-op, so every `BX_TRACE`/`BX_WARN` bgfx emits
         * -- invalid blit, invalid readback usage, destroyed-handle use, framebuffer and resolve
         * complaints, renderer fallback -- was discarded before anyone could read it. A readback
         * defect is exactly the kind bgfx would have warned about, so a diagnostics run needs the
         * stream. Off unless `CNA_BGFX_TRACE_DIAGNOSTICS` is set, so ordinary runs are byte-identical.
         */
        void traceVargs(const char* _filePath, uint16_t _line, const char* _format,
                        va_list _argList) override;
        /// Set once from `CNA_BGFX_TRACE_DIAGNOSTICS`; see traceVargs.
        bool traceDiagnostics = false;
        void profilerBegin(const char*, uint32_t, const char*, uint16_t) override {}
        void profilerBeginLiteral(const char*, uint32_t, const char*, uint16_t) override {}
        void profilerEnd() override {}
        uint32_t cacheReadSize(uint64_t) override { return 0; }
        bool     cacheRead(uint64_t, void*, uint32_t) override { return false; }
        void     cacheWrite(uint64_t, const void*, uint32_t) override {}
        bgfx::TextureFormat::Enum screenshotFormat = bgfx::TextureFormat::BGRA8;

        void screenShot(const char* _filePath, uint32_t _w, uint32_t _h, uint32_t _pitch,
                        bgfx::TextureFormat::Enum _format,
                        const void* _data, uint32_t _size, bool _yflip) override;
        void captureBegin(uint32_t, uint32_t, uint32_t,
                          bgfx::TextureFormat::Enum, bool) override {}
        void captureEnd() override {}
        void captureFrame(const void*, uint32_t) override {}
    };

    /// bgfx-backed effect renderer.
    /// bgfx uses pre-compiled binary shaders; CompileProgram always returns false.
    /// Load binary shaders directly via the bgfx::createProgram path.
    class BgfxEffectRenderer : public IEffectRenderer
    {
    public:
        bgfx::ProgramHandle program = BGFX_INVALID_HANDLE;

        BgfxEffectRenderer() = default;
        ~BgfxEffectRenderer() override;

        bool CompileProgram(const std::string& /*vertSrc*/, const std::string& /*fragSrc*/) override { return false; }
        void Bind() override;
        void Unbind() override {}
        [[nodiscard]] bool IsValid() const override { return bgfx::isValid(program); }
        [[nodiscard]] std::string GetCompileError() const override
        {
            return "bgfx renderer requires pre-compiled binary shaders (not GLSL source)";
        }
    };

    /// bgfx-backed occlusion query.
    class BgfxOcclusionQueryRenderer : public IOcclusionQueryRenderer
    {
    public:
        bgfx::OcclusionQueryHandle handle = BGFX_INVALID_HANDLE;

        explicit BgfxOcclusionQueryRenderer(BgfxRenderer* owner);
        ~BgfxOcclusionQueryRenderer() override;

        // Task 448: marks/unmarks this query as the renderer's "active" occlusion query, so every
        // 3D submit() call made in between is routed through bgfx's own occlusion-query submit()
        // overload (see BgfxRenderer::SubmitViewProgram). bgfx submits synchronously
        // (unlike Vulkan's deferred command recording -- see Task 447's own BLOCKED write-up), so
        // no further correlation machinery is needed.
        void Begin() override;
        void End()   override;
        [[nodiscard]] bool IsComplete() const override;
        [[nodiscard]] int  PixelCount() const override;

    private:
        BgfxRenderer* owner_ = nullptr;
    };

    // -------------------------------------------------------------------------
    // IBgfxSamplable — common interface for any Bgfx object that can be bound as
    // a sampled texture (regular Texture2D and RenderTarget2D). Task 878/879 fix
    // (closes Task 873): BgfxSpriteBatchRenderer::Draw previously did an unsafe
    // static_cast<const BgfxTextureRenderer&> on whatever ITextureRenderer it was
    // handed, which silently read BgfxRenderTargetRenderer::fbo (a framebuffer-pool
    // handle) where BgfxTextureRenderer::textureHandle (a texture-pool handle) was
    // expected whenever the argument was actually a render target. This accessor
    // lets each concrete renderer report its own real sampleable texture handle.
    // -------------------------------------------------------------------------

    struct IBgfxSamplable
    {
        virtual ~IBgfxSamplable() = default;
        virtual bgfx::TextureHandle GetBgfxTextureHandle() const = 0;
        // REMED-GFX-067: true when the sampleable handle is a render target's color attachment
        // (an FBO-backed texture). On originBottomLeft renderers (OpenGL/GLES/WebGL) a framebuffer
        // attachment's texel memory is stored bottom-up, so sampling it with the ordinary top-down
        // V convention yields a vertically-mirrored image — unlike an ordinary (SetData-uploaded)
        // Texture2D, whose memory is top-down. The SpriteBatch sample path flips V for these
        // sources when caps->originBottomLeft is set. Default false; only render targets override.
        virtual bool IsRenderTargetColorSource() const { return false; }
        // REMED-GFX-181: the exact `_flags` word this resource was CREATED with. bgfx keeps a
        // per-texture sampler state (`TextureGL::m_flags` and its per-renderer equivalents) and
        // falls back to it for any binding that does not supply its own, so a trace that means to
        // say WHICH state a draw really sampled with has to be able to name both candidates.
        // Reported for diagnostics only -- nothing in the draw path branches on it.
        virtual uint64_t GetBgfxCreationFlagsEXT() const = 0;
    };

    // -------------------------------------------------------------------------
    // IBgfxCubeSamplable — the cube-map sibling of IBgfxSamplable (Task 907, closes Task 874):
    // BgfxRenderer::DrawPrimitivesEx's EnvironmentMapEffect dispatch previously did an
    // unsafe static_cast<const BgfxTextureCubeRenderer&> on whatever ITextureCubeRenderer it was
    // handed, reading BgfxRenderTargetCubeRenderer::fbo (a framebuffer-pool handle) where
    // BgfxTextureCubeRenderer::handle (a texture-pool handle) was expected whenever the argument
    // was actually a RenderTargetCube -- the identical bug shape Task 873 already fixed for
    // RenderTarget2D via IBgfxSamplable.
    // -------------------------------------------------------------------------

    struct IBgfxCubeSamplable
    {
        virtual ~IBgfxCubeSamplable() = default;
        virtual bgfx::TextureHandle GetBgfxCubeTextureHandle() const = 0;
        /// REMED-GFX-181: see IBgfxSamplable::GetBgfxCreationFlagsEXT. Diagnostics only.
        virtual uint64_t GetBgfxCubeCreationFlagsEXT() const = 0;
    };

    class BgfxTextureRenderer : public ITextureRenderer, public IBgfxSamplable
    {
    public:
        bgfx::TextureHandle textureHandle = BGFX_INVALID_HANDLE;
        int width = 0;
        int height = 0;
        /// REMED-GFX-181: the `_flags` word passed to bgfx::createTexture2D. Diagnostics only.
        uint64_t creationFlags_ = 0;

        explicit BgfxTextureRenderer(const ImageData& data);
        ~BgfxTextureRenderer() override;
        int GetWidth() const override { return width; }
        int GetHeight() const override { return height; }
        SDL_Texture* GetNativeTexture() const override { return nullptr; }
        bgfx::TextureHandle GetBgfxTextureHandle() const override { return textureHandle; }
        uint64_t GetBgfxCreationFlagsEXT() const override { return creationFlags_; }
        // Task 926 (split from Task 867): real GPU upload for level 0 and level>0, mirroring
        // BgfxTextureCubeRenderer::SetData's established bgfx::updateTextureCube pattern.
        void UpdatePixels(const uint8_t* rgba, int stride) override;
        void UpdatePixelsLevel(int level, const uint8_t* rgba, int levelW, int levelH) override;
    };

    /// bgfx-backed cube map texture.
    class BgfxTextureCubeRenderer : public ITextureCubeRenderer, public IBgfxCubeSamplable
    {
    public:
        bgfx::TextureHandle handle = BGFX_INVALID_HANDLE;
        int size_ = 0;
        /// Mip levels bgfx really allocated for this cube (REMED-GFX-135).
        int levelCount_ = 1;
        /// REMED-GFX-181: the `_flags` word passed to bgfx::createTextureCube. Diagnostics only.
        uint64_t creationFlags_ = 0;

        BgfxTextureCubeRenderer(int size, bool mipMap, int surfaceFormat);
        bgfx::TextureHandle GetBgfxCubeTextureHandle() const override { return handle; }
        uint64_t GetBgfxCubeCreationFlagsEXT() const override { return creationFlags_; }
        ~BgfxTextureCubeRenderer() override;

        /// REMED-GFX-135: true only once the whole region has been handed to bgfx as a
        /// `bgfx::copy()`-owned memory block sized exactly to it; false for an invalid handle, an
        /// out-of-range face/level/rectangle or a source buffer too small for the region. The copy
        /// happens inside this call, so the caller's memory is never retained past it.
        [[nodiscard]] bool SetData(int face, int level, int x, int y, int w, int h,
                                   const void* data, int dataLength) override;
        /// Task 914: real GPU readback via a temporary BGFX_TEXTURE_BLIT_DST|BGFX_TEXTURE_READ_BACK
        /// 2D texture (blit the requested face/mip/region into it, then bgfx::readTexture()).
        /// REMED-GFX-130: true only once bgfx has advanced to the frame that completes the
        /// readback; false when BGFX_CAPS_TEXTURE_BLIT/READ_BACK is unavailable on the selected
        /// renderer or the frame never arrives, so the shared layer rejects the read instead of
        /// converting its own zeroed scratch buffer into a fabricated face.
        [[nodiscard]] bool GetData(int face, int level, int x, int y, int w, int h,
                                   void* data, int dataLength) const override;
    };

    /// bgfx-backed 3D (volume) texture.
    class BgfxTexture3DRenderer : public ITexture3DRenderer
    {
    public:
        bgfx::TextureHandle handle = BGFX_INVALID_HANDLE;
        int width_ = 0, height_ = 0, depth_ = 0;
        /// Mip levels bgfx really allocated for this volume (REMED-GFX-135).
        int levelCount_ = 1;

        BgfxTexture3DRenderer(int w, int h, int depth, bool mipMap, int surfaceFormat);
        ~BgfxTexture3DRenderer() override;

        /// REMED-GFX-135: same completion contract as BgfxTextureCubeRenderer::SetData above.
        [[nodiscard]] bool SetData(int level, int x, int y, int z,
                                   int w, int h, int depth,
                                   const void* data, int dataLength) override;
        /// Task 914: real GPU readback via a temporary BGFX_TEXTURE_BLIT_DST|BGFX_TEXTURE_READ_BACK
        /// 3D texture sized to the requested region (blit from the requested mip/offset, then
        /// bgfx::readTexture()). REMED-GFX-130: same explicit completion contract as
        /// BgfxTextureCubeRenderer::GetData above.
        [[nodiscard]] bool GetData(int level, int x, int y, int z,
                                   int w, int h, int depth,
                                   void* data, int dataLength) const override;
    };

    /// bgfx-backed 2D render target (bgfx framebuffer with color + depth textures).
    class BgfxRenderTargetRenderer : public IRenderTargetRenderer, public IBgfxSamplable
    {
    public:
        bgfx::FrameBufferHandle fbo = BGFX_INVALID_HANDLE;
        bgfx::TextureHandle colorTex = BGFX_INVALID_HANDLE;
        int  width            = 0;
        int  height           = 0;
        bool preserveContents = false;
        // Task 878/879: real, renderer-clamped applied MultiSampleCount (0 = no MSAA). bgfx
        // resolves an RT_MSAA_Xn color attachment into a sampleable single-sample image
        // internally -- no explicit resolve step is needed on this renderer.
        int  multiSampleCount = 0;
        /// REMED-GFX-181: the `_flags` word passed to bgfx::createTexture2D. Diagnostics only.
        uint64_t creationFlags_ = 0;
        /// REMED-GFX-185: the depth attachment's creation flags, or zero when absent. Diagnostics only.
        uint64_t depthCreationFlags_ = 0;

        BgfxRenderTargetRenderer(BgfxRenderer* owner, int w, int h, int depthFormat,
                                 bool preserveContents = false,
                                 int requestedMultiSampleCount = 0, bool mipMap = false);
        ~BgfxRenderTargetRenderer() override;

        int GetWidth()  const override { return width; }
        int GetHeight() const override { return height; }
        SDL_Texture* GetNativeTexture() const override { return nullptr; }
        void UpdatePixels(const uint8_t* rgba, int stride) override {}
        void BindGL(int /*unit*/) const override {}
        int GetMultiSampleCount() const override { return multiSampleCount; }
        bgfx::TextureHandle GetBgfxTextureHandle() const override { return colorTex; }
        uint64_t GetBgfxCreationFlagsEXT() const override { return creationFlags_; }
        // REMED-GFX-067: this IS a render-target color attachment — see IBgfxSamplable.
        bool IsRenderTargetColorSource() const override { return true; }

        /**
         * @brief Reads this target's rendered colour attachment back as tightly packed RGBA8 rows.
         *
         * REMED-GFX-127. Uses the SAME mechanism Task 914 already established for
         * BgfxTextureCubeRenderer/BgfxTexture3DRenderer readback -- blit the requested mip/rectangle
         * into a temporary `BGFX_TEXTURE_BLIT_DST | BGFX_TEXTURE_READ_BACK` texture, then
         * `bgfx::readTexture()` and advance frames until the returned frame number is reached. The
         * blit is issued on the reserved highest view id, so bgfx's ascending-view-id execution
         * order guarantees it runs after every render-target and viewport-segment view queued this
         * frame and therefore sees this frame's draws rather than the previous frame's content.
         *
         * bgfx's texture readback is inherently deferred: the data only exists once the frame that
         * carries the blit has been submitted, so this call advances bgfx frames, exactly as
         * BgfxRenderer::ReadBackbuffer already does. That frame advance is confined to this
         * explicitly synchronous call -- ordinary rendering never pays for it -- and the temporary
         * readback texture is destroyed before returning, so repeated readbacks hold no extra GPU
         * memory. Before this override existed the call reached ITextureRenderer::GetData's default
         * and the shared layer converted its own zeroed scratch buffer for the caller.
         *
         * @param level      Mip level to read; this target has a full chain only when it was
         *                   created with mipMap.
         * @param x          Left edge of the requested rectangle, in level pixels.
         * @param y          Top edge of the requested rectangle, in level pixels.
         * @param w          Width of the requested rectangle, in pixels.
         * @param h          Height of the requested rectangle, in pixels.
         * @param data       Destination for @p w * @p h tightly packed RGBA8 pixels.
         * @param dataLength Capacity of @p data in bytes.
         * @return True once the whole rectangle has been written; false when this renderer cannot
         *         create a readback texture (BGFX_CAPS_TEXTURE_BLIT/READ_BACK absent) or the
         *         deferred read never completed, leaving @p data untouched.
         * @throws System::NotSupportedException if this target has no such mip level.
         * @throws System::ArgumentOutOfRangeException if @p level is negative, the rectangle leaves
         *         the level, or @p dataLength is too small for the rectangle.
         */
        [[nodiscard]] bool GetData(int level, int x, int y, int w, int h,
                                   void* data, int dataLength) const override;

        void BindAsRenderTarget()   override;
        void UnbindAsRenderTarget() override;

    private:
        BgfxRenderer* owner_ = nullptr;
        int mipLevels_ = 1;   ///< 1 unless this target was created with a full mip chain.
    };

    /// bgfx-backed cube map render target.
    class BgfxRenderTargetCubeRenderer : public IRenderTargetCubeRenderer, public IBgfxCubeSamplable
    {
    public:
        BgfxRenderer* owner_ = nullptr;
        /// The currently bound face's framebuffer -- an alias into `faceFbos_`, never separately
        /// owned (REMED-GFX-134).
        bgfx::FrameBufferHandle fbo = BGFX_INVALID_HANDLE;
        /**
         * @brief One lazily created framebuffer per cube face, all kept alive together.
         *
         * REMED-GFX-134: this used to be a single handle that `BindAsRenderTargetFace` destroyed
         * and rebuilt on every face switch. Within one un-advanced bgfx frame that left the
         * FIRST-bound face's already-recorded draws pointing at a destroyed framebuffer while the
         * base view had been re-pointed at the newest face, so binding six faces in one frame
         * rendered five and silently dropped the first.
         */
        std::array<bgfx::FrameBufferHandle, 6> faceFbos_ = {{
            BGFX_INVALID_HANDLE, BGFX_INVALID_HANDLE, BGFX_INVALID_HANDLE,
            BGFX_INVALID_HANDLE, BGFX_INVALID_HANDLE, BGFX_INVALID_HANDLE,
        }};
        /**
         * @brief One independently preserving multisample colour producer per cube face.
         *
         * REMED-GFX-195: bgfx's OpenGL texture object owns exactly one hidden multisample
         * renderbuffer, even when its resolved image is a six-faced cube. Attaching different
         * layers of one multisampled cube therefore re-attached that same renderbuffer. These six
         * 2D targets keep the native multisample storage independent; ordered blits publish their
         * resolved mip chains into `cubeTex` for sampling and GetData. Invalid on single-sample
         * targets, whose established direct-to-cube path is unchanged.
         */
        std::array<bgfx::TextureHandle, 6> msaaFaceColorTex_ = {{
            BGFX_INVALID_HANDLE, BGFX_INVALID_HANDLE, BGFX_INVALID_HANDLE,
            BGFX_INVALID_HANDLE, BGFX_INVALID_HANDLE, BGFX_INVALID_HANDLE,
        }};
        bgfx::TextureHandle cubeTex = BGFX_INVALID_HANDLE;
        // Task 877: single 2D depth/stencil texture shared across all 6 faces (mirrors
        // VulkanRenderTargetCubeRenderer's shared depthImage_) -- BGFX_INVALID_HANDLE when the
        // requested DepthFormat is None. Re-attached alongside whichever face's color view is
        // bound in BindAsRenderTargetFace, same as the color attachment's per-face rebuild.
        bgfx::TextureHandle depthTex = BGFX_INVALID_HANDLE;
        int size_ = 0;
        // Task 903: real, renderer-clamped applied MultiSampleCount (0 = no MSAA), mirroring
        // BgfxRenderTargetRenderer's identical Task 878/879 field.
        int  multiSampleCount = 0;
        /// Mip levels bgfx really allocated for this cube target (REMED-GFX-134).
        int levelCount_ = 1;
        /// REMED-GFX-181: the `_flags` word passed to bgfx::createTextureCube. Diagnostics only.
        uint64_t creationFlags_ = 0;
        /// REMED-GFX-185: the per-face colour producer's creation flags. Diagnostics only.
        uint64_t msaaProducerCreationFlags_ = 0;
        /// REMED-GFX-185: the shared depth attachment's creation flags, or zero. Diagnostics only.
        uint64_t depthCreationFlags_ = 0;

        /// REMED-GFX-195: face whose per-face producer is currently bound, or -1.
        int boundFace_ = -1;

        BgfxRenderTargetCubeRenderer(BgfxRenderer* owner, int size, int depthFormat,
                                     bool mipMap = false, int requestedMultiSampleCount = 0);
        ~BgfxRenderTargetCubeRenderer() override;

        [[nodiscard]] int GetSize() const override { return size_; }
        void BindAsRenderTargetFace(int face) override;
        void UnbindAsRenderTarget() override;
        int GetMultiSampleCount() const override { return multiSampleCount; }
        [[nodiscard]] unsigned int GetGLHandle() const override { return 0; }
        bgfx::TextureHandle GetBgfxCubeTextureHandle() const override { return cubeTex; }
        uint64_t GetBgfxCubeCreationFlagsEXT() const override { return creationFlags_; }

        /**
         * @brief Reads a RENDERED cube face's mip level back to the CPU.
         *
         * REMED-GFX-134. Reuses `BgfxRenderer::ReadTextureRegionEXT` -- the same
         * blit-into-a-BLIT_DST|READ_BACK-texture then `bgfx::readTexture()` mechanism
         * `BgfxRenderTargetRenderer::GetData` uses -- with the cube face selected as the source
         * array layer. Going through that helper rather than
         * `BgfxTextureCubeRenderer::GetData`'s own copy is what makes this correct for a RENDER
         * TARGET: it issues the blit on the reserved highest view id, so the copy runs AFTER every
         * render-target and viewport-segment view queued this frame instead of reading the previous
         * frame's content, and it recycles this renderer's per-frame state across the forced
         * `bgfx::frame()` advance.
         *
         * REMED-GFX-067's `originBottomLeft` correction applies here for the same reason it applies
         * to a 2D target: on OpenGL/GLES/WebGL a rendered attachment stores its texel memory
         * bottom-up, so the requested rectangle is mapped into bottom-up coordinates and the
         * returned rows flipped back. A no-op on Vulkan/D3D/Metal.
         *
         * @param face       Cube face index (0=+X, 1=-X, 2=+Y, 3=-Y, 4=+Z, 5=-Z).
         * @param level      Mip level to read.
         * @param x          Left edge of the requested region, in texels.
         * @param y          Top edge of the requested region, in texels.
         * @param w          Width of the requested region, in texels.
         * @param h          Height of the requested region, in texels.
         * @param data       Destination for tightly packed RGBA8 rows, top row first.
         * @param dataLength Size of @p data in bytes; at least w * h * 4.
         * @return True once the whole region was written; false for an out-of-range
         *         face/level/region, a missing BGFX_CAPS_TEXTURE_BLIT/READ_BACK, or a frame that
         *         never arrives.
         */
        [[nodiscard]] bool GetData(int face, int level, int x, int y, int w, int h,
                                   void* data, int dataLength) const override;
    };

    /// bgfx dynamic vertex buffer.
    class BgfxVertexBufferRenderer : public IVertexBufferRenderer
    {
    public:
        bgfx::DynamicVertexBufferHandle handle = BGFX_INVALID_HANDLE;
        bgfx::VertexLayout layout;
        int vertexCount = 0;
        std::size_t stride = 0;
        std::vector<uint8_t> cpuData; ///< CPU copy kept for per-instance reads in instanced draws.

        explicit BgfxVertexBufferRenderer(int capacity);
        ~BgfxVertexBufferRenderer() override;

        void SetData(const void* data, int vertex_count, std::size_t stride_in_bytes) override;
        /// REMED-GFX-216: the caller's own declaration is what the native layout is built from.
        /// This used to be an empty override, so the authoritative description was delivered and
        /// discarded and `SetData` guessed a layout from the byte stride instead -- which cannot
        /// distinguish `Position@0 + Color@12` from `Color@0 + Position@4`, both stride 16.
        void SetVertexDeclaration(const VertexDeclaration& vertexDeclaration) override;
        [[nodiscard]] int GetVertexCount() const override { return vertexCount; }
        /// The declaration this buffer's current native layout was derived from. Empty until the
        /// first SetVertexDeclaration call. Diagnostics and tests only.
        CNAEXT [[nodiscard]] const VertexDeclaration& GetVertexDeclarationEXT() const noexcept
        {
            return declaration_;
        }
        /// REMED-GFX-109: records that bgfx draw state now references this native version.
        /// The next real SetData must rotate to a different native allocation because bgfx
        /// executes every dynamic-buffer update before any draws in the submitted frame.
        void MarkSubmitted() const noexcept { submittedSinceUpdate_ = true; }

    private:
        int capacity_ = 0;
        mutable bool submittedSinceUpdate_ = false;
        /// REMED-GFX-216: the declaration the current `layout` was derived from, kept so an upload
        /// can tell "the same declaration again" from "a different declaration that happens to
        /// share a stride" -- the second is exactly the case a stride comparison cannot see.
        VertexDeclaration declaration_;
        bool hasDeclaration_ = false;
        /// Set when `declaration_` was replaced and the native layout has not been rebuilt yet.
        bool declarationChanged_ = false;
    };

    /// bgfx dynamic index buffer with a construction-time-fixed 16- or 32-bit element width.
    class BgfxIndexBufferRenderer : public IIndexBufferRenderer
    {
    public:
        bgfx::DynamicIndexBufferHandle handle = BGFX_INVALID_HANDLE;
        int indexCount = 0;
        bool is32bit = false;
        /// Task 766: CPU copy of the raw index bytes, needed to re-expand triangle indices into a
        /// line-list for FillMode::WireFrame emulation (mirrors EasyGLIndexBufferRenderer's own
        /// GetCpuBytes(), bgfx has no equivalent read-back API for a DynamicIndexBufferHandle).
        std::vector<uint8_t> cpuData;

        explicit BgfxIndexBufferRenderer(int capacity, bool thirtyTwoBit = false);
        ~BgfxIndexBufferRenderer() override;

        void SetData16(const void* data, int index_count) override;
        void SetData32(const void* data, int index_count) override;
        [[nodiscard]] int  GetIndexCount()  const override { return indexCount; }
        [[nodiscard]] bool IsThirtyTwoBit() const override { return is32bit; }
        /// Returns the exact flags supplied for this handle and every immutable replacement.
        [[nodiscard]] std::uint16_t GetNativeCreationFlagsEXT() const noexcept
        {
            return nativeCreationFlags_;
        }
        /// REMED-GFX-109: see BgfxVertexBufferRenderer::MarkSubmitted().
        void MarkSubmitted() const noexcept { submittedSinceUpdate_ = true; }

    private:
        int capacity_ = 0;
        std::uint16_t nativeCreationFlags_ = BGFX_BUFFER_ALLOW_RESIZE;
        mutable bool submittedSinceUpdate_ = false;
    };

    class BgfxSpriteBatchRenderer : public ISpriteBatchRenderer
    {
    public:
        BgfxRenderer& graphicsRenderer;
        bool begun = false;

        explicit BgfxSpriteBatchRenderer(BgfxRenderer& graphicsRenderer);
        ~BgfxSpriteBatchRenderer() override = default;
        void Begin() override;
        void End() override;
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

        // Task 750: SpriteBatch::Begin()'s SamplerState (Filter/AddressU/AddressV) previously had
        // no effect at all on this renderer (silent no-op via ISpriteBatchRenderer's default empty
        // bodies) -- same bug shape already fixed on EasyGL (Task 269) and Vulkan (Task 665).
        void SetSamplerFilter(int textureFilter) override;
        void SetSamplerAddressMode(int addressU, int addressV) override;

        // Task 808: SpriteBatch::Begin()'s transformMatrix parameter previously had no effect at
        // all on this renderer (same "silent no-op via ISpriteBatchRenderer's default empty body"
        // shape as Task 750's SamplerState fix). Stored on the owning BgfxRenderer (not
        // here) since the combined transform is applied in EnsureViewState(), which every 2D/3D
        // draw path calls.
        void SetTransformMatrix(const Matrix& m) override;

    private:
        int pendingFilter_   = 0; // TextureFilter::Linear
        int pendingAddressU_ = 1; // TextureAddressMode::Clamp
        int pendingAddressV_ = 1; // TextureAddressMode::Clamp
    };

    class BgfxRenderer : public IGraphicsRenderer
    {
    public:
        SDL_Window* window = nullptr;
        bgfx::ProgramHandle spriteProgram = BGFX_INVALID_HANDLE;
        bgfx::UniformHandle textureSampler = BGFX_INVALID_HANDLE;
        bgfx::ViewId spriteViewId = 0;
        bgfx::ViewId currentViewId_ = 0;  ///< Active view (0=backbuffer, 1=RT0, etc.)
        uint16_t cachedWidth = 0;
        uint16_t cachedHeight = 0;
        // Task 878/879 fix: the currently-bound RT's own size (0 when targeting the backbuffer),
        // so EnsureViewState() can preserve BindAsRenderTarget()'s RT-sized viewport/ortho instead
        // of always stomping it back to the full window size. See EnsureViewState()'s comment.
        uint16_t currentRtWidth_ = 0;
        uint16_t currentRtHeight_ = 0;
        // REMED-GFX-195: the active cube face is retained across Present/GetData frame advances so
        // a later draw while it remains publicly bound can publish another resolve. Target switches
        // replace/clear it after queueing the outgoing face's ordered copy.
        BgfxRenderTargetCubeRenderer* currentCubeTarget_ = nullptr;
        uint32_t clearRgba = 0x000000ff;
        // REMED-GFX-018: exact clear mask owned by the currently-active ordered view. bgfx clear
        // state is per-view and last-wins until frame(), so this cannot be inferred from the view
        // id (clear operations may themselves own segment views). Draw-only views use CLEAR_NONE.
        uint16_t currentViewClearFlags_ = BGFX_CLEAR_NONE;
        // Task 871: threaded into EnsureViewState()'s bgfx::setViewClear() call, replacing a
        // previously-hardcoded stencil clear value of 0.
        uint8_t clearStencilValue_ = 0;
        // Task 950: threaded into EnsureViewState()'s bgfx::setViewClear() call, replacing a
        // previously-hardcoded depth clear value of 1.0f.
        float clearDepthValue_ = 1.0f;
        // Task 808: SpriteBatch::Begin()'s transformMatrix, set by BgfxSpriteBatchRenderer::
        // SetTransformMatrix(). Combined with the sprite view's own ortho projection in
        // EnsureViewState() (`orthoWithTransform = ortho * spriteTransform_`, bx::mtxMul order --
        // matches EasyGL's `transform_ * orthoM` combined-matrix semantics, just computed in raw
        // column-major float space since bgfx's setViewTransform takes raw arrays, not a Matrix).
        // Defaults to identity, matching every SpriteBatch::Begin() overload's own null default.
        Matrix spriteTransform_ = Matrix::getIdentityProperty();
        bool initialized = false;
        uint32_t resetFlags_ = BGFX_RESET_VSYNC;

        // Stored graphics state applied per-draw in bgfx
        uint64_t blendFlags_  = BGFX_STATE_BLEND_ALPHA;
        uint64_t depthFlags_  = BGFX_STATE_DEPTH_TEST_LESS | BGFX_STATE_WRITE_Z;
        uint64_t cullFlags_   = BGFX_STATE_CULL_CCW;
        /// REMED-GFX-077: per-draw colour write mask (BlendState.ColorWriteChannels slot 0), OR'd
        /// into every bgfx::setState word. bgfx colour write is a single global-per-draw mask, so
        /// this replaces the former hardcoded BGFX_STATE_WRITE_RGB|BGFX_STATE_WRITE_A. Defaults to
        /// all four channels (= XNA default ColorWriteChannels.All).
        uint64_t colorWriteFlags_ = BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A;
        // Task 766: FillMode::WireFrame (fillMode==1), set by ApplyRasterizerState. bgfx has no
        // native polygon-fill-mode toggle (unlike D3D9/Vulkan) -- emulated by re-expanding
        // triangle indices into a line list at draw time, mirroring EasyGL's DrawWireframe.
        bool wireframe_ = false;
        // Task 767: RasterizerState.DepthBias, set by ApplyRasterizerState. bgfx has no native
        // polygon-offset mechanism (unlike D3D9/Vulkan/GL) -- emulated via a per-draw uniform
        // added to clip-space Z in every 3D vertex shader (see u_depthBias in the .sc sources).
        // SlopeScaleDepthBias is deliberately NOT emulated (project-owner decision, 2026-07-10):
        // a true per-fragment slope computation would force every 3D shader off the early-Z path,
        // even at DepthBias=0, unless duplicate shader variants were added -- documented as a
        // remaining gap instead.
        float depthBias_ = 0.0f;
        // Sampler flags per slot (slots 0–15)
        static constexpr int kMaxSamplerSlots = 16;
        uint32_t samplerFlags_[kMaxSamplerSlots] = {};
        // Blend color for BGFX_STATE_BLEND_FACTOR
        float blendFactorR_ = 1.f, blendFactorG_ = 1.f, blendFactorB_ = 1.f, blendFactorA_ = 1.f;
        uint32_t blendFactorPacked_ = 0xFFFFFFFFu; // packed RGBA8, passed to bgfx::setState
        // Scissor rect, plus a genuinely independent enable flag (Task 768 finding: the rect's own
        // coordinates and RasterizerState.ScissorTestEnable are set via 2 separate GraphicsDevice
        // calls that can happen in either order -- a zero-sized rect can NOT double as "disabled"
        // sentinel, since SetScissorRect() is free to set a real, non-zero rect while
        // ScissorTestEnable is still false, and vice versa. Mirrors EasyGL's own
        // set_scissor_test_enabled(...)/set_scissor(...) split, which are two independent GL calls.
        uint16_t scissorX_ = 0, scissorY_ = 0, scissorW_ = 0, scissorH_ = 0;
        bool     scissorEnabled_ = false;
        // Viewport rect (Task 880) -- storage-only; applied via an explicit bgfx::setViewRect
        // override right before each 3D submit (see DrawPrimitivesEx), deliberately NOT folded
        // into EnsureViewState() to avoid entangling the shared 2D SpriteBatch view-rect reset.
        uint16_t viewportX_ = 0, viewportY_ = 0, viewportW_ = 0, viewportH_ = 0;
        bool     viewportSet_ = false;
        // REMED-GFX-072: the GraphicsDevice.Viewport captured at SpriteBatch-submit time. bgfx view
        // state (setViewRect/setViewTransform) is per-view and applied at frame(); Present()/
        // ReadBackbuffer() may re-run EnsureViewState() after the game has restored the viewport, so
        // the sprite view must be sized/placed from the viewport that was live when the sprite was
        // submitted -- not the current one. Single custom viewport per view/frame only; multiple
        // differing viewports on one view in one frame remain last-wins (REMED-GFX-065). spriteVpValid_
        // is reset after each bgfx::frame().
        uint16_t spriteVpX_ = 0, spriteVpY_ = 0, spriteVpW_ = 0, spriteVpH_ = 0;
        bool     spriteVpSet_ = false;
        bool     spriteVpValid_ = false;

        // REMED-GFX-065/GFX-179: ordered per-frame view routing. Every first operation on a render
        // target, target rebind, later backbuffer cycle, viewport/transform change, and ordered
        // Clear receives the next ID in [1,255). Only the first backbuffer operation may use 0.
        // Consecutive compatible draws reuse the active ID. This applies bgfx's physical limit to
        // view states used in this frame, never to the number of target objects currently alive.
        bgfx::FrameBufferHandle segmentTargetFbo_ = BGFX_INVALID_HANDLE; ///< bound target's fbo (invalid=backbuffer)
        bgfx::ViewId frameNextViewId_ = Detail::kFirstPublicFrameViewId; ///< next free ordered frame id
        bool     segmentActive_ = false;                            ///< has the first operation on this binding happened?
        // REMED-GFX-155: the view ids this frame's public commands used, in the order they were
        // first used. bgfx does NOT execute views in submission order -- it radix-sorts every draw
        // by its view's SORT POSITION, which defaults to the numeric view id. Because the backbuffer
        // owns the lowest id (0) while every render target owns a higher one, a backbuffer draw that
        // samples a render target produced earlier in the same frame executed BEFORE its producer.
        // The correction keeps bgfx's default ordering and makes the IDS ascend with public order
        // instead (see highestViewUsedThisFrame_), so this list is now a record of what was done
        // rather than an instruction to bgfx -- it is what CNA_BGFX_TRACE_VIEW_ORDER prints, and
        // asserting it is monotonically increasing is exactly asserting the contract. Recycled by
        // EndFrameSegments(); membership is O(1) through frameViewOrdered_.
        std::vector<bgfx::ViewId> frameViewOrder_;
        std::array<bool, Detail::kMaxViews> frameViewOrdered_ = {};
        // REMED-GFX-155: the highest view id any public command has used this frame, or -1 before
        // the first one. REMED-GFX-179's unified allocator makes every newly required view
        // monotonically larger by construction, so ascending-id order is public order.
        int highestViewUsedThisFrame_ = -1;
        // REMED-GFX-155: set from CNA_BGFX_TRACE_VIEW_ORDER=1. bgfx's execution order is not
        // observable from the public API, so the ordering this renderer programs is written to stderr
        // on demand -- that trace is the structural evidence behind this task's ordering claims and
        // is how a future ordering regression is diagnosed without re-deriving the id partition.
        bool traceViewOrder_ = false;
        // REMED-GFX-154: set from CNA_BGFX_TRACE_READBACK=1. A multisample RESOLVE is not observable
        // through bgfx's public API either -- there is no handle for the multisample side and no
        // capability bit to ask -- so the whole readback sequence (source handles, blit view, frame
        // numbers, the completion generation bgfx::readTexture returned, and how many frames were
        // processed while waiting for it) is written to stderr on demand. This trace is the evidence
        // behind this task's ordering claims.
        bool traceReadback_ = false;
        /// REMED-GFX-154: how many public readbacks this device has served, for the trace's own index.
        int readbackCallIndex_ = 0;
        // The active view's viewport identity. A draw whose viewport differs from the active view's
        // starts the next ordered draw-only view. Since GFX-018 records Clear on its own full-target
        // ordered view, draw segments preserve colour, depth, and stencil.
        bool     segCurHasVp_ = false;   ///< active view's custom-viewport flag
        uint16_t segCurX_ = 0, segCurY_ = 0, segCurW_ = 0, segCurH_ = 0;  ///< active view's custom viewport
        // REMED-GFX-084: the active view's SpriteBatch view-transform identity. EnsureViewState bakes
        // spriteTransform_ into the view-global setViewTransform (last-wins at bgfx::frame()), so the
        // sprite transform is part of a segment's view-global state exactly like its viewport rect. Set
        // when a SpriteBatch batch commits to the active view; a later same-viewport sprite batch with a
        // different transform starts a new ordered segment. A 3D draw leaves ...Valid_ false (it never
        // programs setViewTransform), so a sprite can still adopt the view afterwards without churn.
        bool     segCurSpriteTransformValid_ = false;
        Matrix   segCurSpriteTransform_ = Matrix::getIdentityProperty();
        // Stencil state (per-draw-call via bgfx::setStencil)
        uint32_t stencilFront_ = BGFX_STENCIL_NONE;
        uint32_t stencilBack_  = BGFX_STENCIL_NONE;
        // Task 764: cached stencil parameters from the last ApplyDepthStencilState call (every
        // field except the reference value itself), so SetReferenceStencil can rebuild
        // stencilFront_/stencilBack_ standalone -- see RebuildStencilState().
        bool stencilEnableCached_       = false;
        int  stencilFuncCached_         = 0;
        int  stencilPassCached_         = 0;
        int  stencilFailCached_         = 0;
        int  stencilDepthFailCached_    = 0;
        int  stencilMaskCached_         = 0x7FFFFFFF;
        int  stencilWriteMaskCached_    = 0x7FFFFFFF;
        bool twoSidedStencilModeCached_ = false;
        int  ccwStencilFuncCached_      = 0;
        int  ccwStencilPassCached_      = 0;
        int  ccwStencilFailCached_      = 0;
        int  ccwStencilDepthFailCached_ = 0;
        int  referenceStencilCached_    = 0;
        void RebuildStencilState();
        // Task 766: expands a TriangleList/TriangleStrip draw's indices into a line-list edge
        // buffer for FillMode::WireFrame emulation. `ib` is null for non-indexed draws (sequential
        // vertex indices are synthesized starting at `firstVertex`). Returns false (nothing
        // allocated) for non-triangle primitives or an empty draw -- caller falls through to the
        // normal solid-fill submission in that case. Mirrors EasyGLRenderer::DrawWireframe.
        bool ExpandWireframeIndices(const BgfxIndexBufferRenderer* ib, PrimitiveType primitive,
                                    int primitiveCount, int startIndex, int baseVertex,
                                    int firstVertex, bgfx::TransientIndexBuffer& outTib);
        // Task 448: the OcclusionQuery currently "active" (between its Begin()/End() calls), set
        // by BgfxOcclusionQueryRenderer::Begin()/End(). Since bgfx submits every 3D draw call
        // synchronously (unlike Vulkan's deferred RecordCommandBuffer -- see Task 447's own
        // BLOCKED write-up), every 3D submit() call made while a query is active is routed through
        // bgfx's own submit(id, program, occlusionQuery, ...) overload instead of the plain one.
        bgfx::OcclusionQueryHandle activeOcclusionQuery_ = BGFX_INVALID_HANDLE;
        // REMED-GFX-113: the (startVertex, numVertices) pair handed to the last non-indexed
        // bgfx::setVertexBuffer call. bgfx exposes no way to read a submitted draw's stream range
        // back, so the exact native binding is recorded here for the range regression to assert.
        uint32_t lastNonIndexedBindStartEXT_ = 0;
        uint32_t lastNonIndexedBindCountEXT_ = 0;
        // REMED-GFX-118: the same record for the instanced indexed path -- the (startVertex,
        // numVertices) pair handed to bgfx::setVertexBuffer, the (firstIndex, numIndices) pair
        // handed to bgfx::setIndexBuffer, and the instance count handed to
        // bgfx::allocInstanceDataBuffer/setInstanceDataBuffer. The whole-buffer overloads these
        // replaced carried none of the public range at all.
        uint32_t lastInstancedVertexBindStartEXT_ = 0;
        uint32_t lastInstancedVertexBindCountEXT_ = 0;
        uint32_t lastInstancedIndexBindStartEXT_ = 0;
        uint32_t lastInstancedIndexBindCountEXT_ = 0;
        uint32_t lastInstancedInstanceCountEXT_ = 0;
        // Callback registered at bgfx init — captures screenshot data for ReadBackbuffer
        BgfxCnaCallback readbackCallback_;
        // Temporary MRT framebuffer (created on SetRenderTargets with count > 1)
        bgfx::FrameBufferHandle mrtFbo_ = BGFX_INVALID_HANDLE;
        // GFX-179: CNA's Bgfx shaders expose one colour output. Keep a sibling framebuffer with
        // every attachment writable for public Clear(), while mrtFbo_ exposes only slot 0 to draws.
        // A clear view and a following draw view remain distinct ordered operations.
        bgfx::FrameBufferHandle mrtClearFbo_ = BGFX_INVALID_HANDLE;
        // 3D shader programs (BGFX_INVALID_HANDLE until bgfx_shaders.hpp binaries are loaded)
        bgfx::ProgramHandle colored3DProgram_         = BGFX_INVALID_HANDLE;
        bgfx::ProgramHandle textured3DProgram_        = BGFX_INVALID_HANDLE;
        bgfx::ProgramHandle coloredTextured3DProgram_ = BGFX_INVALID_HANDLE;
        bgfx::ProgramHandle litTextured3DProgram_     = BGFX_INVALID_HANDLE;
        // Task 1104: real per-vertex-lit sibling of litTextured3DProgram_/skinned3DProgram_,
        // selected when GpuDrawParams::preferPerPixelLighting is false (XNA's real default).
        bgfx::ProgramHandle litTextured3DVertexLitProgram_ = BGFX_INVALID_HANDLE;
        bgfx::ProgramHandle alphaTest3DProgram_       = BGFX_INVALID_HANDLE;
        bgfx::ProgramHandle alphaTestColoredTextured3DProgram_ = BGFX_INVALID_HANDLE; // Task 887
        bgfx::ProgramHandle dualTexture3DProgram_     = BGFX_INVALID_HANDLE;
        bgfx::ProgramHandle dualTextureColored3DProgram_ = BGFX_INVALID_HANDLE; // Task 889
        bgfx::ProgramHandle skinned3DProgram_         = BGFX_INVALID_HANDLE;
        bgfx::ProgramHandle skinned3DVertexLitProgram_ = BGFX_INVALID_HANDLE; // Task 1104
        bgfx::ProgramHandle instanced3DProgram_       = BGFX_INVALID_HANDLE;
        bgfx::ProgramHandle envMap3DProgram_          = BGFX_INVALID_HANDLE;
        /// plan_cnj.md CNB-58/60 (Phase 13A) Bgfx port: PbrEffect (unskinned, stride 48).
        bgfx::ProgramHandle pbr3DProgram_             = BGFX_INVALID_HANDLE;
        /// PBR + skinning combo: SkinnedPbrEffect (stride 68).
        bgfx::ProgramHandle pbrSkinned3DProgram_      = BGFX_INVALID_HANDLE;
        // Uniforms shared across 3D draw calls
        bgfx::UniformHandle wvpUniform_         = BGFX_INVALID_HANDLE;
        /// Task 767: RasterizerState.DepthBias vertex-shader Z-offset emulation, x component only.
        bgfx::UniformHandle depthBiasUnif_      = BGFX_INVALID_HANDLE;
        bgfx::UniformHandle diffuseColor3DUnif_ = BGFX_INVALID_HANDLE;
        bgfx::UniformHandle ambientColor3DUnif_ = BGFX_INVALID_HANDLE;
        bgfx::UniformHandle light0Dir3DUnif_    = BGFX_INVALID_HANDLE;
        bgfx::UniformHandle light0Diff3DUnif_   = BGFX_INVALID_HANDLE;
        bgfx::UniformHandle lightingEn3DUnif_   = BGFX_INVALID_HANDLE;
        /// BasicEffect.DirectionalLight1/2 (lit-textured shader only, Task 885).
        bgfx::UniformHandle light1Dir3DUnif_    = BGFX_INVALID_HANDLE;
        bgfx::UniformHandle light1Diff3DUnif_   = BGFX_INVALID_HANDLE;
        bgfx::UniformHandle light2Dir3DUnif_    = BGFX_INVALID_HANDLE;
        bgfx::UniformHandle light2Diff3DUnif_   = BGFX_INVALID_HANDLE;
        /// BasicEffect specular (lit-textured shader only, Task 886).
        bgfx::UniformHandle light0Spec3DUnif_       = BGFX_INVALID_HANDLE;
        bgfx::UniformHandle light1Spec3DUnif_       = BGFX_INVALID_HANDLE;
        bgfx::UniformHandle light2Spec3DUnif_       = BGFX_INVALID_HANDLE;
        /// xyz = material SpecularColor, w = SpecularPower.
        bgfx::UniformHandle specularColorPower3DUnif_ = BGFX_INVALID_HANDLE;
        /// BasicEffect.VertexColorEnabled gate for the no-texture colored3D path (Task 364).
        bgfx::UniformHandle vertexColorEn3DUnif_ = BGFX_INVALID_HANDLE;
        /// Fog color (xyz) and the CPU-prepared FNA fog vector, shared by every fog-capable 3D
        /// program.
        bgfx::UniformHandle fogColorUnif_  = BGFX_INVALID_HANDLE;
        bgfx::UniformHandle fogParamsUnif_ = BGFX_INVALID_HANDLE;
        bgfx::UniformHandle texColor3DSampler_  = BGFX_INVALID_HANDLE;
        /// 1x1 opaque white fallback, sampled whenever a draw's texture0 is null (Task 379) —
        /// matches EasyGL/Vulkan's identical fallback instead of leaving the previous draw's
        /// texture bound (stale, undefined behavior).
        bgfx::TextureHandle defaultWhiteTexture3D_ = BGFX_INVALID_HANDLE;
        bgfx::UniformHandle alphaTestUnif_      = BGFX_INVALID_HANDLE;
        bgfx::UniformHandle texColor3DSampler2_ = BGFX_INVALID_HANDLE;
        bgfx::UniformHandle bonesUnif_          = BGFX_INVALID_HANDLE;
        /// SkinnedEffect.WeightsPerVertex (x = 1, 2, or 4), Task 895.
        bgfx::UniformHandle weightsPerVertex3DUnif_ = BGFX_INVALID_HANDLE;
        bgfx::UniformHandle vpInstanced3DUnif_  = BGFX_INVALID_HANDLE;
        // EnvironmentMapEffect-specific uniforms
        bgfx::UniformHandle world3DUnif_        = BGFX_INVALID_HANDLE;
        bgfx::UniformHandle normalMatrix3DUnif_ = BGFX_INVALID_HANDLE;
        bgfx::UniformHandle eyePos3DUnif_       = BGFX_INVALID_HANDLE;
        bgfx::UniformHandle emissiveColor3DUnif_= BGFX_INVALID_HANDLE;
        bgfx::UniformHandle envMapAmountUnif_   = BGFX_INVALID_HANDLE;
        bgfx::UniformHandle envMapSpecularUnif_ = BGFX_INVALID_HANDLE;
        bgfx::UniformHandle envMapSampler_      = BGFX_INVALID_HANDLE;
        // PbrEffect/SkinnedPbrEffect-specific uniforms (plan_cnj.md CNB-58/60, Phase 13A Bgfx port).
        /// x = MetallicFactor, y = RoughnessFactor.
        bgfx::UniformHandle metallicRoughnessFactorUnif_ = BGFX_INVALID_HANDLE;
        bgfx::UniformHandle pbrSrgbUnif_                  = BGFX_INVALID_HANDLE;
        bgfx::UniformHandle normalMapSampler_            = BGFX_INVALID_HANDLE;
        bgfx::UniformHandle metallicRoughnessSampler_    = BGFX_INVALID_HANDLE;
        bgfx::UniformHandle emissiveMapSampler_          = BGFX_INVALID_HANDLE;
        bgfx::UniformHandle occlusionMapSampler_         = BGFX_INVALID_HANDLE;
        // REMED-GFX-078: u_rtFlipV -- per-slot (x=slot0, y=slot1, z=slot2, w=slot3) "this sampler
        // reads a render-target color source that must be V-flipped" flag. A RenderTarget2D's FBO
        // color memory is bottom-up on originBottomLeft renderers (OpenGL/GLES/WebGL), so the 3D
        // effect shaders flip the sampled V for flagged slots -- the generic-effect counterpart of
        // REMED-GFX-067's SpriteBatch sample-stage flip. 0 for ordinary textures (sampling
        // unchanged) and for every slot on Vulkan/D3D/Metal (originBottomLeft=false), so ordinary
        // Texture2D output is byte-identical on all renderers. See BindSamplerSlot / SubmitViewProgram.
        bgfx::UniformHandle rtFlipVUnif_                 = BGFX_INVALID_HANDLE;
        // Per-draw scratch: accumulated by BindSamplerSlot before each 3D submit, uploaded to
        // rtFlipVUnif_ and cleared inside SubmitViewProgram. Slot 4 (PBR occlusion) is intentionally
        // outside this vec4 -- a live RenderTarget2D as a PBR occlusion map is not a real material
        // and is left un-compensated; its cast is still made type-safe (no UB).
        float rtFlipV_[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
        /// Tangent-space "flat normal" (0,0,1) encoded as RGB (128,128,255) -- fallback for
        /// PbrEffect::NormalMap when unbound, matching EasyGLRenderer::
        /// EnsureDefaultFlatNormalTexture()'s identical rationale. The other 3 PBR map fallbacks
        /// (metallic-roughness, emissive, occlusion) reuse defaultWhiteTexture3D_ instead.
        bgfx::TextureHandle defaultFlatNormalTexture3D_  = BGFX_INVALID_HANDLE;

        explicit BgfxRenderer(SDL_Window* window, int swapInterval = 1);
        ~BgfxRenderer() override;
        // OcclusionQuery reflects the real, live bgfx::getCaps() device query (BGFX_CAPS_OCCLUSION_QUERY).
        // Everything else CNA::GraphicsCapability currently enumerates is genuinely supported
        // here, so falls through to the shared default (true).
        [[nodiscard]] bool SupportsCapability(CNA::GraphicsCapability capability) const override;
        void Clear(float r, float g, float b, float a) override;
        void Present() override;
        void GetViewportSize(int& width, int& height) override;

        void SetVirtualResolution(int width, int height) override
        {
        } // no-op: Bgfx uses physical viewport
        void SetPresentationMode(int /*mode*/) override
        {
        } // no-op: Bgfx has no logical presentation
        void SetSwapInterval(int interval) override;
        SDL_Window* GetWindowInternal() const override { return window; }
        SDL_Renderer* GetRendererInternal() const override { return nullptr; }

        std::unique_ptr<ITextureRenderer> CreateTexture(const ImageData& data) override;
        std::unique_ptr<ISpriteBatchRenderer> CreateSpriteBatch() override;
        std::unique_ptr<IOcclusionQueryRenderer> CreateOcclusionQuery() override;
        std::unique_ptr<ITexture3DRenderer> CreateTexture3D(int w, int h, int depth, bool mipMap, int surfaceFormat) override;
        std::unique_ptr<ITextureCubeRenderer> CreateTextureCube(int size, bool mipMap, int surfaceFormat) override;
        std::unique_ptr<IEffectRenderer> CreateEffectRenderer(const std::string& vertSrc,
                                                             const std::string& fragSrc) override;
        void ReadBackbuffer(int x, int y, int w, int h, uint8_t* pixels) override;
        /**
         * @brief Blits @p srcTexture's requested region into a temporary readback texture and
         * copies it to @p data, advancing bgfx frames until the deferred read completes.
         *
         * REMED-GFX-127: the readback half of BgfxRenderTargetRenderer::GetData lives here because
         * the frame advance has to be paired with this renderer's own per-frame bookkeeping
         * (viewport-segment recycling and the sprite viewport validity flag), exactly as
         * ReadBackbuffer does. This is an internal renderer helper, not part of the XNA surface.
         *
         * @param srcTexture Source texture handle (a render target's colour attachment).
         * @param level      Source mip level.
         * @param x          Left edge of the requested region, in level pixels.
         * @param y          Top edge of the requested region, in level pixels.
         * @param w          Width of the requested region, in pixels.
         * @param h          Height of the requested region, in pixels.
         * @param data       Destination for @p w * @p h tightly packed RGBA8 pixels.
         * @param layer      Source array layer -- a cube target's face index (REMED-GFX-134);
         *                   0 for the plain 2D render target this helper was written for.
         * @return True once the whole region was written; false if the readback texture could not
         *         be created or the deferred read did not complete.
         */
        [[nodiscard]] bool ReadTextureRegionEXT(bgfx::TextureHandle srcTexture, int level,
                                                int x, int y, int w, int h, void* data,
                                                int layer = 0);
        /**
         * @brief Completes the current bgfx frame so every queued framebuffer RESOLVE has run.
         *
         * REMED-GFX-134: bgfx performs a render target's MSAA resolve and its
         * `BGFX_RESOLVE_AUTO_GEN_MIPS` mip regeneration when it tears the framebuffer down at
         * frame end, not when the last view referencing it is processed. A readback blit queued in
         * the same frame as the draws therefore copies PRE-resolve memory even though it runs on
         * the reserved highest view id -- measurably all-zero for a multisampled cube target and
         * for any mip level above 0. Advancing one frame first makes the blit read the resolved
         * result. Carries the same per-frame bookkeeping every other `bgfx::frame()` call site in
         * this renderer does.
         */
        void CompleteFrameForResolveEXT()
        {
            FinalizeCurrentCubeFaceEXT();
            bgfx::frame();
            spriteVpValid_ = false;
            EndFrameSegments();
        }
        std::unique_ptr<IRenderTargetRenderer> CreateRenderTarget2D(int w, int h, int depthFormat, bool preserveContents = false, bool mipMap = false, int multiSampleCount = 0) override;
        void SetRenderTarget2D(IRenderTargetRenderer* rt) override;
        void SetRenderTargets(const RenderTargetBindingDescriptor* renderTargets,
                              int count) override;
        std::unique_ptr<IRenderTargetCubeRenderer> CreateRenderTargetCube(int size, int depthFormat, bool preserveContents = false, bool mipMap = false, int multiSampleCount = 0) override;
        // Task 907 finding: the shared IGraphicsRenderer::SetRenderTargetCubeFace default only
        // calls BindAsRenderTargetFace -- it never updates currentRtWidth_/currentRtHeight_ (the
        // Task 901 fix for 2D RTs), so any SpriteBatch draw into a cube face was rasterizing into
        // a viewport sized to the full window instead of the face's own size. Overridden here to
        // also set those, mirroring SetRenderTarget2D's own pattern.
        void SetRenderTargetCubeFace(IRenderTargetCubeRenderer* rt, int face) override;

        // Graphics state (stored; applied per-draw in SubmitSprite and future 3D draws)
        void ApplyBlendState(int colorSrcBlend, int alphaSrcBlend,
                             int colorDstBlend, int alphaDstBlend,
                             int colorBlendFunc, int alphaBlendFunc,
                             const BlendWriteState& writeState) override;
        // Task 923: alphaSrcBlend/alphaDstBlend/colorBlendFunc/alphaBlendFunc are now genuinely
        // honored (BGFX_STATE_BLEND_FUNC_SEPARATE/BGFX_STATE_BLEND_EQUATION_SEPARATE), not just
        // colorSrcBlend/colorDstBlend applied to both channels with an implicit Add equation.
        void ApplyDepthStencilState(bool depthEnable, bool depthWriteEnable, int depthFunc,
                                    bool stencilEnable, int stencilFunc,
                                    int stencilPass, int stencilFail, int stencilDepthFail,
                                    int stencilMask, int stencilWriteMask, int referenceStencil,
                                    bool twoSidedStencilMode,
                                    int ccwStencilFunc, int ccwStencilPass,
                                    int ccwStencilFail, int ccwStencilDepthFail) override;
        void ApplyRasterizerState(int cullMode, int fillMode,
                                  bool scissorTestEnable,
                                  float depthBias, float slopeScaleDepthBias) override;
        void SetScissorRect(int x, int y, int w, int h) override;
        void SetViewport(int x, int y, int w, int h, float minDepth, float maxDepth) override;
        void ApplySamplerState(int slot, int filter, int addressU, int addressV,
                               int maxAnisotropy) override;
        void SetBlendFactor(float r, float g, float b, float a) override;
        // Task 764: GraphicsDevice.ReferenceStencil is a real, independent device property that
        // must take effect standalone, without a full DepthStencilState re-application (mirrors
        // Vulkan's SetReferenceStencil, Task 870/319). Rebuilds stencilFront_/stencilBack_ from
        // the cached stencil parameters below plus the new reference value.
        void SetReferenceStencil(int value) override;

        // 3D pipeline — vertex/index buffers implemented; draw calls need colored3DProgram_.
        // @note SetDepth* / SetBlend still throw (not wired to state flags yet).
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
        std::unique_ptr<IIndexBufferRenderer> CreateIndexBuffer32(int index_capacity) override;
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

        // Task 878/879 (closes Task 873): takes the already-resolved bgfx texture handle +
        // dimensions rather than a concrete BgfxTextureRenderer, so callers can supply either a
        // BgfxTextureRenderer's or a BgfxRenderTargetRenderer's real sampleable texture (via
        // IBgfxSamplable::GetBgfxTextureHandle()) without an unsafe cast between unrelated types.
        // REMED-GFX-067/GFX-153: sourceIsRenderTarget mirrors each absolute sampled V around the
        // whole texture on originBottomLeft renderers, keeping full and partial source rectangles
        // upright when a RenderTarget2D's FBO memory is bottom-up on OpenGL.
        void SubmitSprite(bgfx::TextureHandle textureHandle, bool sourceIsRenderTarget,
                          int texWidth, int texHeight,
                          const Rectangle& destinationRectangle,
                          const Rectangle& sourceRectangle,
                          const Color& color,
                          float rotation,
                          const Vector2& origin,
                          SpriteEffects effects,
                          float layerDepth);

    private:
        void EnsureViewState();
        // REMED-GFX-018: record one public Clear operation as an ordered full-target bgfx view
        // with exactly the requested BGFX_CLEAR_* mask. A later clear always gets a new segment,
        // so clears and draws remain interleavable in CNA submission order.
        void RecordClear(uint16_t clearFlags);

        // REMED-GFX-065/GFX-179: select the view id this draw/batch must submit to. The first real
        // operation on a binding allocates its per-frame view lazily (except initial backbuffer view
        // 0); later viewport/transform changes allocate another. Consecutive compatible draws reuse
        // the active view. Redirects currentViewId_ and spriteViewId at the start of both draw paths.
        // REMED-GFX-084: spritePath == true also keys the segment on the SpriteBatch view transform
        // (spriteTransform_), which EnsureViewState bakes into the view-global setViewTransform, so a
        // same-viewport batch with a different Begin(transformMatrix) starts a new ordered segment. The
        // 3D path (spritePath == false) never programs setViewTransform, so it neither keys on nor
        // commits the sprite transform.
        void SelectViewportSegment(bool spritePath);
        // Whether the active GraphicsDevice.Viewport is a genuine custom sub-region of the current target
        // (vs the full-target/default viewport); also returns the target's full pixel size. Shared by
        // SelectViewportSegment() and ApplyViewportOverride(); mirrors EnsureViewState()'s own test.
        bool CurrentCustomViewport(uint16_t& fullW, uint16_t& fullH) const;
        // Allocate the next ordered per-frame public view id; throws when the frame exhausts
        // [1,255), leaving 255 reserved for readback/flush.
        bgfx::ViewId AllocateFrameViewId();
        // Point the tracker at a newly-bound framebuffer. No view is consumed until a real
        // draw/Clear commits state for that binding.
        void ResetSegmentTarget(bgfx::FrameBufferHandle fbo);

        /**
         * @brief Publishes the active multisampled cube face into its public cube image.
         *
         * REMED-GFX-195: when the current bind cycle recorded work, this queues one ordered view
         * that first switches away from the producer framebuffer (triggering bgfx's native MSAA
         * resolve/mip generation) and then blits every resolved level into the selected cube face.
         * No-op for ordinary cubes, idle binds, and an absent cube target.
         */
        void FinalizeCurrentCubeFaceEXT();
        // Recycle the per-frame public view-id pool at a frame boundary (after bgfx::frame()).
        void EndFrameSegments();
        // REMED-GFX-155: record that a public command has just committed to @p id. Idempotent: a
        // view already used this frame keeps its original position, which is what makes a run of
        // consecutive draws on one view cheap.
        void NoteViewUsedEXT(bgfx::ViewId id);
        // Task 880: overrides the current 3D view's rect with a custom Viewport, if one was set
        // via SetViewport(). Since REMED-GFX-063 this applies to render-target views too, and since
        // REMED-GFX-065 it runs after SelectViewportSegment() so it targets the right segment view.
        void ApplyViewportOverride();

        // Task 768: applies the current scissor rect (if any) to the pending 3D draw. Previously
        // only the 2D SpriteBatch path called bgfx::setScissor -- none of the 4 3D draw-dispatch
        // functions did, so RasterizerState.ScissorTestEnable/GraphicsDevice.ScissorRectangle had
        // zero effect on any 3D draw. bgfx resets scissor state per submit() call (does not
        // persist from a prior draw), so this must be called before every 3D bgfx::submit().
        void ApplyScissorOverride();

        /// Task 767: uploads depthBias_ (scaled by kDepthBiasScale) to u_depthBias. bgfx resets
        /// uniform state per submit() call, so this must be called before every 3D bgfx::submit().
        void SetDepthBiasUniform();

        /// plan_cnj.md CNB-58/60 (Phase 13A) Bgfx port: binds PbrEffect/SkinnedPbrEffect's 4
        /// additional texture units (1=normal, 2=metallic-roughness, 3=emissive, 4=occlusion),
        /// each falling back to the "map absent" constant matching its own semantic --
        /// factored out since both DrawPrimitivesEx and DrawIndexedPrimitivesEx need it for both
        /// the unskinned and skinned PBR program variants (4 call sites), unlike this file's
        /// other per-branch texture-binding blocks (which bind only 1-2 units each).
        void BindPbrTextures(const GpuDrawParams& params);

        // REMED-GFX-078: binds a Texture2D-or-RenderTarget2D effect texture to a 3D sampler slot
        // through IBgfxSamplable (never the invalid static_cast<const BgfxTextureRenderer&>, which is
        // UB for a RenderTarget2D whose renderer is the unrelated sibling BgfxRenderTargetRenderer).
        // Resolves the real pooled handle, falls back to the given "map absent" default when texture
        // is null, and -- for a render-target color source on an originBottomLeft renderer -- records
        // the slot in rtFlipV_ (slots 0-3) so SubmitViewProgram's u_rtFlipV upload V-flips it.
        void BindSamplerSlot(int slot, bgfx::UniformHandle sampler,
                             const ITextureRenderer* texture, bgfx::TextureHandle fallback);

        // REMED-GFX-181: prints the whole two-slot EnvironmentMapEffect binding on one line --
        // both captured public sampler words, both resources' bgfx creation flags, and the
        // `_flags` argument each bgfx::setTexture actually received, rendered the way bgfx
        // resolves it. Gated on CNA_BGFX_ENVMAP_TRACE; a no-op otherwise.
        void TraceEnvMapBinding(const char* path, uint16_t viewId,
                                const ITextureRenderer* baseTexture, uint32_t baseArgument,
                                const IBgfxCubeSamplable* cube, uint32_t cubeArgument) const;

        // Task 448: submits a 3D draw call's already-configured bgfx state to currentViewId_,
        // routing through bgfx's occlusion-query submit() overload when activeOcclusionQuery_ is
        // a valid handle (set by BgfxOcclusionQueryRenderer::Begin(), cleared by End()).
        void SubmitViewProgram(bgfx::ProgramHandle program);
    };
}
