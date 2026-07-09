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
        // Sampler flags per slot (slots 0–15)
        static constexpr int kMaxSamplerSlots = 16;
        uint32_t samplerFlags_[kMaxSamplerSlots] = {};
        // Blend color for BGFX_STATE_BLEND_FACTOR
        float blendFactorR_ = 1.f, blendFactorG_ = 1.f, blendFactorB_ = 1.f, blendFactorA_ = 1.f;
        uint32_t blendFactorPacked_ = 0xFFFFFFFFu; // packed RGBA8, passed to bgfx::setState
        // Scissor rect (0,0,0,0 = disabled)
        uint16_t scissorX_ = 0, scissorY_ = 0, scissorW_ = 0, scissorH_ = 0;
        // Viewport rect (Task 880) -- storage-only; applied via an explicit bgfx::setViewRect
        // override right before each 3D submit (see DrawPrimitivesEx), deliberately NOT folded
        // into EnsureViewState() to avoid entangling the shared 2D SpriteBatch view-rect reset.
        uint16_t viewportX_ = 0, viewportY_ = 0, viewportW_ = 0, viewportH_ = 0;
        bool     viewportSet_ = false;
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
        bgfx::ProgramHandle alphaTest3DProgram_       = BGFX_INVALID_HANDLE;
        bgfx::ProgramHandle alphaTestColoredTextured3DProgram_ = BGFX_INVALID_HANDLE; // Task 887
        bgfx::ProgramHandle dualTexture3DProgram_     = BGFX_INVALID_HANDLE;
        bgfx::ProgramHandle skinned3DProgram_         = BGFX_INVALID_HANDLE;
        bgfx::ProgramHandle instanced3DProgram_       = BGFX_INVALID_HANDLE;
        bgfx::ProgramHandle envMap3DProgram_          = BGFX_INVALID_HANDLE;
        // Uniforms shared across 3D draw calls
        bgfx::UniformHandle wvpUniform_         = BGFX_INVALID_HANDLE;
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
        bgfx::UniformHandle vpInstanced3DUnif_  = BGFX_INVALID_HANDLE;
        // EnvironmentMapEffect-specific uniforms
        bgfx::UniformHandle world3DUnif_        = BGFX_INVALID_HANDLE;
        bgfx::UniformHandle normalMatrix3DUnif_ = BGFX_INVALID_HANDLE;
        bgfx::UniformHandle eyePos3DUnif_       = BGFX_INVALID_HANDLE;
        bgfx::UniformHandle emissiveColor3DUnif_= BGFX_INVALID_HANDLE;
        bgfx::UniformHandle envMapAmountUnif_   = BGFX_INVALID_HANDLE;
        bgfx::UniformHandle envMapSpecularUnif_ = BGFX_INVALID_HANDLE;
        bgfx::UniformHandle envMapSampler_      = BGFX_INVALID_HANDLE;

        explicit BgfxGraphicsBackend(SDL_Window* window, int swapInterval = 1);
        ~BgfxGraphicsBackend() override;
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
        void SubmitSprite(bgfx::TextureHandle textureHandle, int texWidth, int texHeight,
                          const Rectangle& destinationRectangle,
                          const Rectangle& sourceRectangle,
                          const Color& color,
                          float rotation,
                          const Vector2& origin,
                          SpriteEffects effects,
                          float layerDepth);

    private:
        void EnsureViewState();

        // Task 880: overrides the current 3D view's rect with a custom Viewport, if one was set
        // via SetViewport() and the current view is the backbuffer (view 0). RT passes are
        // deliberately left at their full-RT-size default -- see viewportX_/Y_/W_/H_'s comment.
        void ApplyViewportOverride();

        // Task 448: submits a 3D draw call's already-configured bgfx state to currentViewId_,
        // routing through bgfx's occlusion-query submit() overload when activeOcclusionQuery_ is
        // a valid handle (set by BgfxOcclusionQueryBackend::Begin(), cleared by End()).
        void SubmitViewProgram(bgfx::ProgramHandle program);
    };
}
