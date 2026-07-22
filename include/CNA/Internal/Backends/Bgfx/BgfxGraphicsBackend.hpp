#pragma once

#include "../Common/IGraphicsBackend.hpp"
#include <bgfx/bgfx.h>
#include <SDL3/SDL.h>
#include <cstdarg>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace CNA::Internal::Backends::Bgfx
{
    namespace Detail
    {
        bgfx::RendererType::Enum GetDefaultRendererType();
        bgfx::RendererType::Enum ParseRendererTypeOverride(const char* value);
        bgfx::RendererType::Enum ResolveRendererType(const char* value);

        /// Sentinel meaning "no view id currently allocated" (0 and every real bgfx::ViewId are
        /// valid allocations, so this must be outside that range -- bgfx caps view ids at 255).
        inline constexpr bgfx::ViewId kInvalidRtViewId = 0xFFFF;

        // Task 951: the single highest bgfx view id (255) is permanently reserved as a dedicated
        // "backbuffer flush" view -- never handed out by AllocateRtViewId(). bgfx processes every
        // configured view in ascending id order each frame, so whichever view was configured last
        // (highest id) is left as the bound GL framebuffer once that processing finishes; a real,
        // concurrently-bound render target's view (ids [1,254]) would otherwise be that last view
        // whenever one is active in the same frame, corrupting/crashing BgfxGraphicsBackend::
        // ReadBackbuffer()'s bgfx::requestScreenShot()-based glReadPixels. Explicitly binding this
        // reserved view to the backbuffer and bgfx::touch()-ing it right before that screenshot
        // request guarantees the real backbuffer is what ends up GL-bound, without ever touching
        // (and so without ever discarding the still-pending, unflushed draws queued against) any
        // render target's own view.
        inline constexpr bgfx::ViewId kBackbufferFlushViewId = 255;

        // REMED-GFX-065: bgfx view state (setViewRect / setViewTransform) is PER-VIEW (resolved once
        // at bgfx::frame(), last-write-wins), so two DIFFERENT GraphicsDevice.Viewport states drawn to
        // one target within a frame would collapse onto its single base view id -- the first draw's
        // viewport is lost. The fix allocates an ordered per-FRAME "viewport segment" view id whenever
        // the viewport changes on a target, keeping each draw under its own rect/ortho while bgfx's
        // ascending-view-id execution order preserves submission order. View-id space is partitioned:
        //   0                              backbuffer base view
        //   [1, kFirstSegmentViewId)       persistent render-target base ids (AllocateRtViewId)
        //   [kFirstSegmentViewId, 255)     per-frame ephemeral viewport-segment ids (recycled each frame)
        //   255 (kBackbufferFlushViewId)   reserved backbuffer-flush view
        // Segment ids sit ABOVE every RT base id, so within one target its base view (lowest id) plus
        // its later segments (higher ids, monotonic in submission order) always execute in draw order.
        inline constexpr bgfx::ViewId kFirstSegmentViewId = 192;

        // Task 910: each concurrently-live render target (2D or cube) needs its own bgfx view id
        // -- bgfx::setViewFrameBuffer(viewId, fbo) is a per-view-per-*frame* setting, resolved
        // once at bgfx::frame(), not per bgfx::submit() call. Every render target previously
        // shared one hardcoded view id (1), so binding a 2nd render target within the same
        // un-advanced frame silently redirected the 1st one's already-submitted draws into the
        // 2nd's framebuffer once the frame actually flushed. Allocated at RT construction time
        // (stable for the RT's whole lifetime) and released at destruction; free-list-backed so
        // long-running games that create/destroy render targets don't exhaust bgfx's ~256 view ids.
        bgfx::ViewId AllocateRtViewId();
        void ReleaseRtViewId(bgfx::ViewId id);
    }

    class BgfxGraphicsBackend;

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
        void traceVargs(const char*, uint16_t, const char*, va_list) override {}
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

    /// bgfx-backed effect backend.
    /// bgfx uses pre-compiled binary shaders; CompileProgram always returns false.
    /// Load binary shaders directly via the bgfx::createProgram path.
    class BgfxEffectBackend : public IEffectBackend
    {
    public:
        bgfx::ProgramHandle program = BGFX_INVALID_HANDLE;

        BgfxEffectBackend() = default;
        ~BgfxEffectBackend() override;

        bool CompileProgram(const std::string& /*vertSrc*/, const std::string& /*fragSrc*/) override { return false; }
        void Bind() override;
        void Unbind() override {}
        [[nodiscard]] bool IsValid() const override { return bgfx::isValid(program); }
        [[nodiscard]] std::string GetCompileError() const override
        {
            return "bgfx backend requires pre-compiled binary shaders (not GLSL source)";
        }
    };

    /// bgfx-backed occlusion query.
    class BgfxOcclusionQueryBackend : public IOcclusionQueryBackend
    {
    public:
        bgfx::OcclusionQueryHandle handle = BGFX_INVALID_HANDLE;

        explicit BgfxOcclusionQueryBackend(BgfxGraphicsBackend* owner);
        ~BgfxOcclusionQueryBackend() override;

        // Task 448: marks/unmarks this query as the backend's "active" occlusion query, so every
        // 3D submit() call made in between is routed through bgfx's own occlusion-query submit()
        // overload (see BgfxGraphicsBackend::SubmitViewProgram). bgfx submits synchronously
        // (unlike Vulkan's deferred command recording -- see Task 447's own BLOCKED write-up), so
        // no further correlation machinery is needed.
        void Begin() override;
        void End()   override;
        [[nodiscard]] bool IsComplete() const override;
        [[nodiscard]] int  PixelCount() const override;

    private:
        BgfxGraphicsBackend* owner_ = nullptr;
    };

    // -------------------------------------------------------------------------
    // IBgfxSamplable — common interface for any Bgfx object that can be bound as
    // a sampled texture (regular Texture2D and RenderTarget2D). Task 878/879 fix
    // (closes Task 873): BgfxSpriteBatchBackend::Draw previously did an unsafe
    // static_cast<const BgfxTextureBackend&> on whatever ITextureBackend it was
    // handed, which silently read BgfxRenderTargetBackend::fbo (a framebuffer-pool
    // handle) where BgfxTextureBackend::textureHandle (a texture-pool handle) was
    // expected whenever the argument was actually a render target. This accessor
    // lets each concrete backend report its own real sampleable texture handle.
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
    };

    // -------------------------------------------------------------------------
    // IBgfxCubeSamplable — the cube-map sibling of IBgfxSamplable (Task 907, closes Task 874):
    // BgfxGraphicsBackend::DrawPrimitivesEx's EnvironmentMapEffect dispatch previously did an
    // unsafe static_cast<const BgfxTextureCubeBackend&> on whatever ITextureCubeBackend it was
    // handed, reading BgfxRenderTargetCubeBackend::fbo (a framebuffer-pool handle) where
    // BgfxTextureCubeBackend::handle (a texture-pool handle) was expected whenever the argument
    // was actually a RenderTargetCube -- the identical bug shape Task 873 already fixed for
    // RenderTarget2D via IBgfxSamplable.
    // -------------------------------------------------------------------------

    struct IBgfxCubeSamplable
    {
        virtual ~IBgfxCubeSamplable() = default;
        virtual bgfx::TextureHandle GetBgfxCubeTextureHandle() const = 0;
    };

    class BgfxTextureBackend : public ITextureBackend, public IBgfxSamplable
    {
    public:
        bgfx::TextureHandle textureHandle = BGFX_INVALID_HANDLE;
        int width = 0;
        int height = 0;

        explicit BgfxTextureBackend(const ImageData& data);
        ~BgfxTextureBackend() override;
        int GetWidth() const override { return width; }
        int GetHeight() const override { return height; }
        SDL_Texture* GetNativeTexture() const override { return nullptr; }
        bgfx::TextureHandle GetBgfxTextureHandle() const override { return textureHandle; }
        // Task 926 (split from Task 867): real GPU upload for level 0 and level>0, mirroring
        // BgfxTextureCubeBackend::SetData's established bgfx::updateTextureCube pattern.
        void UpdatePixels(const uint8_t* rgba, int stride) override;
        void UpdatePixelsLevel(int level, const uint8_t* rgba, int levelW, int levelH) override;
    };

    /// bgfx-backed cube map texture.
    class BgfxTextureCubeBackend : public ITextureCubeBackend, public IBgfxCubeSamplable
    {
    public:
        bgfx::TextureHandle handle = BGFX_INVALID_HANDLE;
        int size_ = 0;

        BgfxTextureCubeBackend(int size, bool mipMap, int surfaceFormat);
        bgfx::TextureHandle GetBgfxCubeTextureHandle() const override { return handle; }
        ~BgfxTextureCubeBackend() override;

        void SetData(int face, int level, int x, int y, int w, int h,
                     const void* data, int dataLength) override;
        /// Task 914: real GPU readback via a temporary BGFX_TEXTURE_BLIT_DST|BGFX_TEXTURE_READ_BACK
        /// 2D texture (blit the requested face/mip/region into it, then bgfx::readTexture()).
        void GetData(int face, int level, int x, int y, int w, int h,
                     void* data, int dataLength) const override;
    };

    /// bgfx-backed 3D (volume) texture.
    class BgfxTexture3DBackend : public ITexture3DBackend
    {
    public:
        bgfx::TextureHandle handle = BGFX_INVALID_HANDLE;

        BgfxTexture3DBackend(int w, int h, int depth, bool mipMap, int surfaceFormat);
        ~BgfxTexture3DBackend() override;

        void SetData(int level, int x, int y, int z,
                     int w, int h, int depth,
                     const void* data, int dataLength) override;
        /// Task 914: real GPU readback via a temporary BGFX_TEXTURE_BLIT_DST|BGFX_TEXTURE_READ_BACK
        /// 3D texture sized to the requested region (blit from the requested mip/offset, then
        /// bgfx::readTexture()).
        void GetData(int level, int x, int y, int z,
                     int w, int h, int depth,
                     void* data, int dataLength) const override;
    };

    /// bgfx-backed 2D render target (bgfx framebuffer with color + depth textures).
    class BgfxRenderTargetBackend : public IRenderTargetBackend, public IBgfxSamplable
    {
    public:
        bgfx::FrameBufferHandle fbo = BGFX_INVALID_HANDLE;
        bgfx::TextureHandle colorTex = BGFX_INVALID_HANDLE;
        int  width            = 0;
        int  height           = 0;
        bool preserveContents = false;
        // Task 878/879: real, backend-clamped applied MultiSampleCount (0 = no MSAA). bgfx
        // resolves an RT_MSAA_Xn color attachment into a sampleable single-sample image
        // internally -- no explicit resolve step is needed on this backend.
        int  multiSampleCount = 0;
        // Task 910: this instance's own stable bgfx view id, allocated at construction --
        // see Detail::AllocateRtViewId()'s comment for why every RT needs a distinct one.
        bgfx::ViewId viewId_ = Detail::kInvalidRtViewId;

        BgfxRenderTargetBackend(int w, int h, int depthFormat, bool preserveContents = false,
                                 int requestedMultiSampleCount = 0, bool mipMap = false);
        ~BgfxRenderTargetBackend() override;

        int GetWidth()  const override { return width; }
        int GetHeight() const override { return height; }
        SDL_Texture* GetNativeTexture() const override { return nullptr; }
        void UpdatePixels(const uint8_t* rgba, int stride) override {}
        void BindGL() const override {}
        int GetMultiSampleCount() const override { return multiSampleCount; }
        bgfx::TextureHandle GetBgfxTextureHandle() const override { return colorTex; }
        // REMED-GFX-067: this IS a render-target color attachment — see IBgfxSamplable.
        bool IsRenderTargetColorSource() const override { return true; }

        void BindAsRenderTarget()   override;
        void UnbindAsRenderTarget() override;
    };

    /// bgfx-backed cube map render target.
    class BgfxRenderTargetCubeBackend : public IRenderTargetCubeBackend, public IBgfxCubeSamplable
    {
    public:
        bgfx::FrameBufferHandle fbo = BGFX_INVALID_HANDLE;
        bgfx::TextureHandle cubeTex = BGFX_INVALID_HANDLE;
        // Task 877: single 2D depth/stencil texture shared across all 6 faces (mirrors
        // VulkanRenderTargetCubeBackend's shared depthImage_) -- BGFX_INVALID_HANDLE when the
        // requested DepthFormat is None. Re-attached alongside whichever face's color view is
        // bound in BindAsRenderTargetFace, same as the color attachment's per-face rebuild.
        bgfx::TextureHandle depthTex = BGFX_INVALID_HANDLE;
        int size_ = 0;
        // Task 903: real, backend-clamped applied MultiSampleCount (0 = no MSAA), mirroring
        // BgfxRenderTargetBackend's identical Task 878/879 field.
        int  multiSampleCount = 0;
        // Task 910: this instance's own stable bgfx view id (shared by all 6 faces), allocated
        // at construction -- see Detail::AllocateRtViewId()'s comment.
        bgfx::ViewId viewId_ = Detail::kInvalidRtViewId;

        BgfxRenderTargetCubeBackend(int size, int depthFormat, bool mipMap = false,
                                     int requestedMultiSampleCount = 0);
        ~BgfxRenderTargetCubeBackend() override;

        [[nodiscard]] int GetSize() const override { return size_; }
        void BindAsRenderTargetFace(int face) override;
        void UnbindAsRenderTarget() override;
        int GetMultiSampleCount() const override { return multiSampleCount; }
        [[nodiscard]] unsigned int GetGLHandle() const override { return 0; }
        bgfx::TextureHandle GetBgfxCubeTextureHandle() const override { return cubeTex; }
    };

    /// bgfx dynamic vertex buffer.
    class BgfxVertexBufferBackend : public IVertexBufferBackend
    {
    public:
        bgfx::DynamicVertexBufferHandle handle = BGFX_INVALID_HANDLE;
        bgfx::VertexLayout layout;
        int vertexCount = 0;
        std::size_t stride = 0;
        std::vector<uint8_t> cpuData; ///< CPU copy kept for per-instance reads in instanced draws.

        explicit BgfxVertexBufferBackend(int capacity);
        ~BgfxVertexBufferBackend() override;

        void SetData(const void* data, int vertex_count, std::size_t stride_in_bytes) override;
        [[nodiscard]] int GetVertexCount() const override { return vertexCount; }
    };

    /// bgfx dynamic index buffer (16-bit).
    class BgfxIndexBufferBackend : public IIndexBufferBackend
    {
    public:
        bgfx::DynamicIndexBufferHandle handle = BGFX_INVALID_HANDLE;
        int indexCount = 0;
        bool is32bit = false;
        /// Task 766: CPU copy of the raw index bytes, needed to re-expand triangle indices into a
        /// line-list for FillMode::WireFrame emulation (mirrors EasyGLIndexBufferBackend's own
        /// GetCpuBytes(), bgfx has no equivalent read-back API for a DynamicIndexBufferHandle).
        std::vector<uint8_t> cpuData;

        explicit BgfxIndexBufferBackend(int capacity, bool thirtyTwoBit = false);
        ~BgfxIndexBufferBackend() override;

        void SetData16(const void* data, int index_count) override;
        void SetData32(const void* data, int index_count) override;
        [[nodiscard]] int  GetIndexCount()  const override { return indexCount; }
        [[nodiscard]] bool IsThirtyTwoBit() const override { return is32bit; }
    };

    class BgfxSpriteBatchBackend : public ISpriteBatchBackend
    {
    public:
        BgfxGraphicsBackend& graphicsBackend;
        bool begun = false;

        explicit BgfxSpriteBatchBackend(BgfxGraphicsBackend& graphicsBackend);
        ~BgfxSpriteBatchBackend() override = default;
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

        // Task 750: SpriteBatch::Begin()'s SamplerState (Filter/AddressU/AddressV) previously had
        // no effect at all on this backend (silent no-op via ISpriteBatchBackend's default empty
        // bodies) -- same bug shape already fixed on EasyGL (Task 269) and Vulkan (Task 665).
        void SetSamplerFilter(int textureFilter) override;
        void SetSamplerAddressMode(int addressU, int addressV) override;

        // Task 808: SpriteBatch::Begin()'s transformMatrix parameter previously had no effect at
        // all on this backend (same "silent no-op via ISpriteBatchBackend's default empty body"
        // shape as Task 750's SamplerState fix). Stored on the owning BgfxGraphicsBackend (not
        // here) since the combined transform is applied in EnsureViewState(), which every 2D/3D
        // draw path calls.
        void SetTransformMatrix(const Matrix& m) override;

    private:
        int pendingFilter_   = 0; // TextureFilter::Linear
        int pendingAddressU_ = 1; // TextureAddressMode::Clamp
        int pendingAddressV_ = 1; // TextureAddressMode::Clamp
    };

    class BgfxGraphicsBackend : public IGraphicsBackend
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
        uint32_t clearRgba = 0x000000ff;
        // Task 871: threaded into EnsureViewState()'s bgfx::setViewClear() call, replacing a
        // previously-hardcoded stencil clear value of 0.
        uint8_t clearStencilValue_ = 0;
        // Task 950: threaded into EnsureViewState()'s bgfx::setViewClear() call, replacing a
        // previously-hardcoded depth clear value of 1.0f.
        float clearDepthValue_ = 1.0f;
        // Task 808: SpriteBatch::Begin()'s transformMatrix, set by BgfxSpriteBatchBackend::
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

        // REMED-GFX-065: ordered per-frame viewport segmentation. When the active GraphicsDevice.Viewport
        // changes on a target within one un-advanced frame, SelectViewportSegment() redirects
        // currentViewId_/spriteViewId to a freshly-allocated segment view (id in [kFirstSegmentViewId,255))
        // configured for the SAME framebuffer with clear suppressed, so each draw keeps its own view rect
        // (3D) / offset ortho (sprite) and bgfx's ascending-view-id order preserves submission order.
        // The base view of the current target (0 for the backbuffer, the RT/MRT's own id otherwise) is
        // always tried first; only a genuine viewport change consumes a segment id. Reset each frame.
        bgfx::ViewId segmentTargetBaseId_ = 0;                       ///< base view id of the bound target
        bgfx::FrameBufferHandle segmentTargetFbo_ = BGFX_INVALID_HANDLE; ///< bound target's fbo (invalid=backbuffer)
        bgfx::ViewId segmentNextId_ = Detail::kFirstSegmentViewId;  ///< next free per-frame segment id
        bool     segmentActive_ = false;                            ///< has the first draw on this binding happened?
        // The active view's viewport identity. The FIRST viewport on a target uses its BASE view (view 0 /
        // RT id) exactly as before this task -- a custom viewport still shrinks that base view's rect
        // (REMED-GFX-063), so a single-viewport frame is byte-identical to pre-GFX-065. A draw whose
        // viewport differs from the active view's starts the next ordered segment view (which clears its
        // own depth+stencil, since bgfx's per-view clear was scoped to the first viewport's rect); once
        // off the base view we never return to it (that would reorder it before the segments).
        bool     segCurIsBase_ = true;   ///< is the active view the target's base view (vs a segment)?
        bool     segCurHasVp_ = false;   ///< active view's custom-viewport flag
        uint16_t segCurX_ = 0, segCurY_ = 0, segCurW_ = 0, segCurH_ = 0;  ///< active view's custom viewport
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
        // normal solid-fill submission in that case. Mirrors EasyGLGraphicsBackend::DrawWireframe.
        bool ExpandWireframeIndices(const BgfxIndexBufferBackend* ib, PrimitiveType primitive,
                                    int primitiveCount, int startIndex, int baseVertex,
                                    int firstVertex, bgfx::TransientIndexBuffer& outTib);
        // Task 448: the OcclusionQuery currently "active" (between its Begin()/End() calls), set
        // by BgfxOcclusionQueryBackend::Begin()/End(). Since bgfx submits every 3D draw call
        // synchronously (unlike Vulkan's deferred RecordCommandBuffer -- see Task 447's own
        // BLOCKED write-up), every 3D submit() call made while a query is active is routed through
        // bgfx's own submit(id, program, occlusionQuery, ...) overload instead of the plain one.
        bgfx::OcclusionQueryHandle activeOcclusionQuery_ = BGFX_INVALID_HANDLE;
        // Callback registered at bgfx init — captures screenshot data for ReadBackbuffer
        BgfxCnaCallback readbackCallback_;
        // Temporary MRT framebuffer (created on SetRenderTargets with count > 1)
        bgfx::FrameBufferHandle mrtFbo_ = BGFX_INVALID_HANDLE;
        // Task 910: MRT's own stable view id for as long as mrtFbo_ is valid -- allocated fresh
        // each SetRenderTargets(count>1) call, released whenever mrtFbo_ is torn down.
        bgfx::ViewId mrtViewId_ = Detail::kInvalidRtViewId;
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
        /// Task 888: fog color (xyz) and fog params (fogEnabled, fogStart, fogEnd), shared by
        /// every 3D program that supports fog.
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
        /// PbrEffect::NormalMap when unbound, matching EasyGLGraphicsBackend::
        /// EnsureDefaultFlatNormalTexture()'s identical rationale. The other 3 PBR map fallbacks
        /// (metallic-roughness, emissive, occlusion) reuse defaultWhiteTexture3D_ instead.
        bgfx::TextureHandle defaultFlatNormalTexture3D_  = BGFX_INVALID_HANDLE;

        explicit BgfxGraphicsBackend(SDL_Window* window, int swapInterval = 1);
        ~BgfxGraphicsBackend() override;
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

        std::unique_ptr<ITextureBackend> CreateTexture(const ImageData& data) override;
        std::unique_ptr<ISpriteBatchBackend> CreateSpriteBatch() override;
        std::unique_ptr<IOcclusionQueryBackend> CreateOcclusionQuery() override;
        std::unique_ptr<ITexture3DBackend> CreateTexture3D(int w, int h, int depth, bool mipMap, int surfaceFormat) override;
        std::unique_ptr<ITextureCubeBackend> CreateTextureCube(int size, bool mipMap, int surfaceFormat) override;
        std::unique_ptr<IEffectBackend> CreateEffectBackend(const std::string& vertSrc,
                                                             const std::string& fragSrc) override;
        void ReadBackbuffer(int x, int y, int w, int h, uint8_t* pixels) override;
        std::unique_ptr<IRenderTargetBackend> CreateRenderTarget2D(int w, int h, int depthFormat, bool preserveContents = false, bool mipMap = false, int multiSampleCount = 0) override;
        void SetRenderTarget2D(IRenderTargetBackend* rt) override;
        void SetRenderTargets(IRenderTargetBackend* const* rts, int count) override;
        std::unique_ptr<IRenderTargetCubeBackend> CreateRenderTargetCube(int size, int depthFormat, bool mipMap = false, int multiSampleCount = 0) override;
        // Task 907 finding: the shared IGraphicsBackend::SetRenderTargetCubeFace default only
        // calls BindAsRenderTargetFace -- it never updates currentRtWidth_/currentRtHeight_ (the
        // Task 901 fix for 2D RTs), so any SpriteBatch draw into a cube face was rasterizing into
        // a viewport sized to the full window instead of the face's own size. Overridden here to
        // also set those, mirroring SetRenderTarget2D's own pattern.
        void SetRenderTargetCubeFace(IRenderTargetCubeBackend* rt, int face) override;

        // Graphics state (stored; applied per-draw in SubmitSprite and future 3D draws)
        void ApplyBlendState(int colorSrcBlend, int alphaSrcBlend,
                             int colorDstBlend, int alphaDstBlend,
                             int colorBlendFunc, int alphaBlendFunc) override;
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
        std::unique_ptr<IVertexBufferBackend> CreateVertexBuffer(int vertex_capacity) override;
        std::unique_ptr<IIndexBufferBackend> CreateIndexBuffer16(int index_capacity) override;
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

        // Task 878/879 (closes Task 873): takes the already-resolved bgfx texture handle +
        // dimensions rather than a concrete BgfxTextureBackend, so callers can supply either a
        // BgfxTextureBackend's or a BgfxRenderTargetBackend's real sampleable texture (via
        // IBgfxSamplable::GetBgfxTextureHandle()) without an unsafe cast between unrelated types.
        // REMED-GFX-067: sourceIsRenderTarget flips the sampled V on originBottomLeft renderers so
        // a RenderTarget2D sampled back (its FBO memory is bottom-up on OpenGL) appears upright.
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

        // REMED-GFX-065: select the view id this draw/batch must submit to, allocating an ordered
        // per-frame viewport-segment view when the viewport changed on the current target (see the
        // segment* members). Redirects currentViewId_ and spriteViewId; called at the very start of
        // the 3D (ApplyViewportOverride) and SpriteBatch (SubmitSprite) submit paths. In the common
        // single-viewport case it keeps the target's base view and allocates nothing.
        void SelectViewportSegment();
        // Whether the active GraphicsDevice.Viewport is a genuine custom sub-region of the current target
        // (vs the full-target/default viewport); also returns the target's full pixel size. Shared by
        // SelectViewportSegment() and ApplyViewportOverride(); mirrors EnsureViewState()'s own test.
        bool CurrentCustomViewport(uint16_t& fullW, uint16_t& fullH) const;
        // Allocate the next ordered per-frame segment view id; throws when the frame exhausts the
        // reserved [kFirstSegmentViewId, 255) range (a deliberate, deterministic cap -- see the .cpp).
        bgfx::ViewId AllocateSegmentViewId();
        // Point the segment tracker at a newly-bound target (base view id + framebuffer), resetting so
        // the next draw starts from that target's base view. Called by SetRenderTarget* and the cube path.
        void ResetSegmentTarget(bgfx::ViewId baseId, bgfx::FrameBufferHandle fbo);
        // Recycle the per-frame segment id pool at a frame boundary (after bgfx::frame()).
        void EndFrameSegments();

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
        // through IBgfxSamplable (never the invalid static_cast<const BgfxTextureBackend&>, which is
        // UB for a RenderTarget2D whose backend is the unrelated sibling BgfxRenderTargetBackend).
        // Resolves the real pooled handle, falls back to the given "map absent" default when texture
        // is null, and -- for a render-target color source on an originBottomLeft renderer -- records
        // the slot in rtFlipV_ (slots 0-3) so SubmitViewProgram's u_rtFlipV upload V-flips it.
        void BindSamplerSlot(int slot, bgfx::UniformHandle sampler,
                             const ITextureBackend* texture, bgfx::TextureHandle fallback);

        // Task 448: submits a 3D draw call's already-configured bgfx state to currentViewId_,
        // routing through bgfx's occlusion-query submit() overload when activeOcclusionQuery_ is
        // a valid handle (set by BgfxOcclusionQueryBackend::Begin(), cleared by End()).
        void SubmitViewProgram(bgfx::ProgramHandle program);
    };
}
