#pragma once

#include "../Common/IGraphicsBackend.hpp"

#if __has_include(<webgpu/webgpu.h>)
#include <webgpu/webgpu.h>
#elif __has_include(<webgpu-headers/webgpu.h>)
#include <webgpu-headers/webgpu.h>
#elif __has_include(<webgpu.h>)
#include <webgpu.h>
#else
#error "CNA WebGPU backend requires webgpu.h from wgpu-native"
#endif

#include <array>
#include <atomic>
#include <cstdint>
#include <memory>
#include <unordered_map>
#include <vector>

namespace CNA::Internal::Backends::WebGPU
{
    class WebGPUGraphicsBackend;
    class WebGPURenderTargetBackend;

    /// Small backend-local interface (mirrors VulkanGraphicsBackend's IVulkanSamplable) shared by
    /// WebGPUTextureBackend and WebGPURenderTargetBackend so every Queue*Draw()'s stored "sampled
    /// texture" pointer can resolve either concrete backend's WGPUTextureView safely.
    /// WebGPURenderTargetBackend cannot simply derive from WebGPUTextureBackend to reuse its
    /// View() -- both already derive from ITextureBackend (IRenderTargetBackend : ITextureBackend
    /// too), which would create an ambiguous diamond -- so this is the same "shared capability via
    /// a small interface, not shared implementation" pattern Vulkan already established. Before
    /// RenderTarget2D support existed, every GpuDrawParams::textureN this backend ever saw was
    /// guaranteed to be a WebGPUTextureBackend (the only ITextureBackend-implementing class this
    /// backend had), so every Queue*Draw() used an unchecked `static_cast<const
    /// WebGPUTextureBackend*>` to store it -- safe only by that former guarantee. Introducing
    /// WebGPURenderTargetBackend (a second, unrelated concrete class implementing ITextureBackend)
    /// makes that cast a real hazard: this interface plus ResolveSamplable() (see the .cpp) turns
    /// it into a safe dynamic_cast that degrades to "treat as unbound" for any incompatible type,
    /// which is never worse than before and is now also correct for a RenderTarget2D.
    class IWebGPUSamplable
    {
    public:
        virtual ~IWebGPUSamplable() = default;
        [[nodiscard]] virtual WGPUTextureView View() const = 0;
    };

    /// WEBGPU-114: the cube-map sibling of IWebGPUSamplable -- lets both WebGPUTextureCubeBackend
    /// (upload-only) and WebGPURenderTargetCubeBackend (render-into) resolve safely to their own
    /// whole-cube WGPUTextureViewDimension_Cube view wherever a GpuDrawParams::envMap needs to be
    /// sampled (EnvironmentMapEffect), via a dynamic_cast that degrades to nullptr for a null or
    /// incompatible-type input -- exact same pattern IWebGPUSamplable already established for
    /// ITextureBackend/texture0. Before this, EnvMapDrawCommand::envMap was hardcoded to
    /// `const WebGPUTextureCubeBackend*`, so a RenderTargetCube (a different concrete class) bound
    /// as EnvironmentMapEffect.EnvironmentMap would silently fail that cast and render with the
    /// 1x1 white cube fallback instead of the real dynamically-rendered content.
    class IWebGPUCubeSamplable
    {
    public:
        virtual ~IWebGPUCubeSamplable() = default;
        [[nodiscard]] virtual WGPUTextureView CubeView() const = 0;
    };

    class WebGPUTextureBackend final : public ITextureBackend, public IWebGPUSamplable
    {
    public:
        WebGPUTextureBackend(WebGPUGraphicsBackend& owner, const ImageData& data);
        ~WebGPUTextureBackend() override;

        WebGPUTextureBackend(const WebGPUTextureBackend&) = delete;
        WebGPUTextureBackend& operator=(const WebGPUTextureBackend&) = delete;

        [[nodiscard]] int GetWidth() const override { return width_; }
        [[nodiscard]] int GetHeight() const override { return height_; }
        [[nodiscard]] SDL_Texture* GetNativeTexture() const override { return nullptr; }
        void UpdatePixels(const uint8_t* rgba, int stride) override;
        void UpdatePixelsLevel(int level, const uint8_t* rgba, int levelW, int levelH) override;
        /// WEBGPU-51: real CPU readback of an arbitrary Texture2D backend, via the same staged
        /// MAP_READ-buffer/aligned-row/async-map-and-poll technique WEBGPU-91's ReadBackbuffer()
        /// and WebGPURenderTargetBackend::GetData() already established -- copies the WHOLE
        /// requested mip level to a temporary readback buffer (this texture is always
        /// WGPUTextureFormat_RGBA8Unorm, so unlike the swapchain/RenderTarget2D there is never a
        /// BGRA byte-swap to worry about), then extracts the @p x,@p y,@p w,@p h sub-rectangle
        /// from the mapped memory on the CPU side.
        ///
        /// REMED-GFX-127: returns true only once the whole requested rectangle has been written.
        /// An empty request, a torn-down owner, an unmappable readback buffer or a destination too
        /// small for the rectangle now return false, where the destination used to be memset to
        /// zero and reported as a successful readback.
        [[nodiscard]] bool GetData(int level, int x, int y, int w, int h,
                                   void* data, int dataLength) const override;

        [[nodiscard]] WGPUTexture Texture() const { return texture_; }
        [[nodiscard]] WGPUTextureView View() const override { return view_; }

    private:
        WebGPUGraphicsBackend* owner_ = nullptr;
        WGPUTexture texture_ = nullptr;
        WGPUTextureView view_ = nullptr;
        int width_ = 0;
        int height_ = 0;
        int mipLevels_ = 1;
    };

    /// WEBGPU-53/54: off-screen render target backing a RenderTarget2D. Owns its own colour
    /// texture (always created in this backend's chosen swapchain format, surfaceFormat_ -- see
    /// this class's .cpp constructor comment for why) and its own depth+stencil texture (always
    /// Depth24PlusStencil8, regardless of the requested DepthFormat, mirroring
    /// VulkanGraphicsBackend's own "always allocate a combined depth+stencil buffer using the
    /// device-wide format regardless of the exact value requested" documented simplification --
    /// see IGraphicsBackend::CreateRenderTarget2D's own doc comment). Mip regeneration is
    /// deliberately NOT implemented yet: CreateRenderTarget2D() throws for mipMap=true rather
    /// than silently under-delivering a requested mip chain.
    /// WEBGPU-58: MSAA is real, but -- unlike VulkanRenderTargetBackend's own per-instance
    /// "requestedMultiSampleCount>0 AND owner_->sampleCount_>1" opt-in gate -- this backend
    /// unconditionally mirrors the OWNER's CURRENT global sampleCount_ at construction time,
    /// ignoring the per-instance requested value entirely (the same "regardless of the exact
    /// value requested" simplification already established for DepthFormat above). This is
    /// necessary, not just simpler: WebGPUGraphicsBackend's ~10 GetOrCreatePipeline*3D() families
    /// bake ONE single backend-global WGPUMultisampleState.count into every pipeline object (see
    /// that class's own sampleCount_ comment) rather than selecting between an MSAA/non-MSAA
    /// pipeline variant per draw target the way Vulkan's RecordCommandBuffer() does -- so a
    /// RenderTarget2D that opted OUT of MSAA while the backend's own sampleCount_ is > 1 would be
    /// pipeline-INCOMPATIBLE (a hard wgpu-native validation error) the moment anything drew 3D
    /// content into it, not merely sub-optimal. Reported via GetMultiSampleCount() as the real,
    /// applied value (0 when the backend has no MSAA active), matching
    /// IRenderTargetBackend::GetMultiSampleCount()'s own "real, device-clamped value" contract.
    /// A known, narrow, documented gap: an instance created BEFORE a later
    /// WebGPUGraphicsBackend::ApplyMultiSampleCount() call changes sampleCount_ does not get
    /// retroactively resized/promoted -- exactly like VulkanGraphicsBackend::ApplyMultiSampleCount()
    /// itself does not touch any already-live VulkanRenderTargetBackend either. This is expected
    /// to be rare in practice (games normally finish GraphicsDeviceManager.ApplyChanges() before
    /// LoadContent() creates any RenderTarget2D instances).
    class WebGPURenderTargetBackend final : public IRenderTargetBackend, public IWebGPUSamplable
    {
    public:
        WebGPURenderTargetBackend(WebGPUGraphicsBackend& owner, int width, int height,
                                  int depthFormat, bool preserveContents);
        ~WebGPURenderTargetBackend() override;

        WebGPURenderTargetBackend(const WebGPURenderTargetBackend&) = delete;
        WebGPURenderTargetBackend& operator=(const WebGPURenderTargetBackend&) = delete;

        [[nodiscard]] int GetWidth() const override { return width_; }
        [[nodiscard]] int GetHeight() const override { return height_; }
        [[nodiscard]] SDL_Texture* GetNativeTexture() const override { return nullptr; }
        /// REMED-GFX-127: returns true only once the whole requested rectangle has been written;
        /// false for an empty request, a torn-down owner or a destination too small for it. The
        /// former behaviour -- memset the destination to zero and return -- reported a fabricated
        /// transparent-black frame as a successful readback.
        [[nodiscard]] bool GetData(int level, int x, int y, int w, int h,
                                   void* data, int dataLength) const override;

        void BindAsRenderTarget() override;
        void UnbindAsRenderTarget() override;

        /// Always the single-sample colour view: the resolve destination sampled back through
        /// SpriteBatch/BasicEffect.Texture/GetData(), regardless of whether this instance is
        /// currently multisampled (WEBGPU-58) -- unaffected by MSAA, matching this member's
        /// pre-existing role exactly.
        [[nodiscard]] WGPUTextureView View() const override { return colorView_; }
        [[nodiscard]] WGPUTextureView DepthView() const { return depthView_; }
        [[nodiscard]] bool PreserveContents() const { return preserveContents_; }
        [[nodiscard]] int GetMultiSampleCount() const override { return appliedMultiSampleCount_; }
        /// WEBGPU-58: the actual render-pass colour attachment to draw into -- the multisampled
        /// texture's view when this instance is multisampled, otherwise the same single-sample
        /// colorView_ that View() returns (identical to before MSAA existed).
        [[nodiscard]] WGPUTextureView ColorAttachmentView() const { return msaaColorView_ != nullptr ? msaaColorView_ : colorView_; }
        /// WEBGPU-58: the render pass's resolveTarget -- colorView_ when multisampled (wgpu-native
        /// resolves into it automatically as the pass ends), or nullptr otherwise (no resolve
        /// needed/possible for a single-sample attachment).
        [[nodiscard]] WGPUTextureView ResolveTargetView() const { return msaaColorView_ != nullptr ? colorView_ : nullptr; }
        /// REMED-GFX-102: exact colour-attachment format captured by this target object. Sprite
        /// pipeline identity uses format compatibility, never this target object's identity.
        [[nodiscard]] WGPUTextureFormat ColorFormat() const { return colorFormat_; }

    private:
        WebGPUGraphicsBackend* owner_ = nullptr;
        int width_ = 0;
        int height_ = 0;
        bool preserveContents_ = false;
        WGPUTextureFormat colorFormat_ = WGPUTextureFormat_Undefined;
        WGPUTexture colorTexture_ = nullptr;
        WGPUTextureView colorView_ = nullptr;
        WGPUTexture depthTexture_ = nullptr;
        WGPUTextureView depthView_ = nullptr;
        /// WEBGPU-58: only non-null when this instance mirrors the owner's sampleCount_ > 1 at
        /// construction time -- see this class's own top-of-class doc comment.
        WGPUTexture msaaColorTexture_ = nullptr;
        WGPUTextureView msaaColorView_ = nullptr;
        /// The real, applied MSAA sample count captured once at construction (0 = none),
        /// returned verbatim by GetMultiSampleCount().
        int appliedMultiSampleCount_ = 0;
    };

    /// WEBGPU-56/74 (EnvironmentMapEffect): a minimal, read-only-after-upload cube-map texture
    /// backend -- just enough for a game to build a `TextureCube` via `SetData()` and use it as
    /// `EnvironmentMapEffect.EnvironmentMap`. A WebGPU cube map is a plain `WGPUTextureDimension_2D`
    /// texture with 6 array layers (`WGPUTextureUsage_CUBE_COMPATIBLE` does not exist as a distinct
    /// flag in this API the way Vulkan/D3D need one -- the view alone selects
    /// `WGPUTextureViewDimension_Cube`), matching `EasyGLTextureCubeBackend`'s own
    /// `CalculateCubeMipLevels()` mip-count convention. Deliberately NOT a full `TextureCube`/
    /// `RenderTargetCube` implementation (was true through WEBGPU-113): `GetData()` now has a real
    /// per-face readback (see below), and `WebGPURenderTargetCubeBackend` (WEBGPU-114) now exists
    /// too, giving this backend real render-into-a-cube-face support.
    class WebGPUTextureCubeBackend final : public ITextureCubeBackend, public IWebGPUCubeSamplable
    {
    public:
        WebGPUTextureCubeBackend(WebGPUGraphicsBackend& owner, int size, bool mipMap);
        ~WebGPUTextureCubeBackend() override;

        WebGPUTextureCubeBackend(const WebGPUTextureCubeBackend&) = delete;
        WebGPUTextureCubeBackend& operator=(const WebGPUTextureCubeBackend&) = delete;

        /// REMED-GFX-135: true only once the whole region has been handed to
        /// `wgpuQueueWriteTexture`, which copies out of the caller's memory before returning; false
        /// for an out-of-range face/level/rectangle or a source buffer too small for the region.
        /// Those rejections used to be `std::out_of_range`/`std::invalid_argument` thrown from a
        /// backend, which escaped the public API as raw std exceptions instead of the shared
        /// layer's own deterministic `System::NotSupportedException`.
        [[nodiscard]] bool SetData(int face, int level, int x, int y, int w, int h,
                                   const void* data, int dataLength) override;
        /// WEBGPU-113: real per-face CPU readback, closing this class's former no-op
        /// `ITextureCubeBackend::GetData()` default. Same staged-copy/row-alignment/async-map
        /// technique as `WebGPUTextureBackend::GetData()` (WEBGPU-51), with `origin.z = face`
        /// selecting the requested array layer (this backend's cube-face convention: 0=+X, 1=-X,
        /// 2=+Y, 3=-Y, 4=+Z, 5=-Z, matching `IRenderTargetCubeBackend`'s own documented mapping).
        /// REMED-GFX-130: true only when the whole requested face rectangle was written; false
        /// when the readback buffer could not be mapped or the request reaches outside the level.
        /// Pre-fix that failure path `memset` the caller's destination to zero and returned as if
        /// it had succeeded -- the same fabrication the shared layer was doing, one level down.
        [[nodiscard]] bool GetData(int face, int level, int x, int y, int w, int h,
                                   void* data, int dataLength) const override;

        [[nodiscard]] WGPUTexture Texture() const { return texture_; }
        /// The single `WGPUTextureViewDimension_Cube` view sampled by `texture_cube<f32>` in
        /// env_map3d.wgsl's fragment shader.
        [[nodiscard]] WGPUTextureView CubeView() const override { return cubeView_; }

    private:
        WebGPUGraphicsBackend* owner_ = nullptr;
        WGPUTexture texture_ = nullptr;
        WGPUTextureView cubeView_ = nullptr;
        int size_ = 0;
        int mipLevels_ = 1;
    };

    /// WEBGPU-114: off-screen render target backing a RenderTargetCube -- the cube-map sibling of
    /// WebGPURenderTargetBackend (RenderTarget2D, WEBGPU-53/54). Owns ONE shared 6-array-layer
    /// colour `WGPUTexture` (exactly like WebGPUTextureCubeBackend's own texture_) plus ONE shared
    /// `size_`x`size_` depth+stencil texture reused across all 6 faces -- safe because, mirroring
    /// VulkanRenderTargetCubeBackend's identical "one shared depth image" choice, only ever ONE
    /// face is bound/rendered-into at a time (this backend's whole architecture is
    /// one-render-target-current-at-once; see WebGPUGraphicsBackend::currentRenderTargetCubeFace_).
    /// Each face gets its own `WGPUTextureViewDimension_2D` view (`faceViews_[face]`,
    /// `baseArrayLayer = face`) used as that face's own render-pass colour attachment, plus ONE
    /// `WGPUTextureViewDimension_Cube` view spanning all 6 layers (`cubeView_`) for sampling back as
    /// `EnvironmentMapEffect.EnvironmentMap` (through `IWebGPUCubeSamplable` -- CNA's
    /// `ITextureCubeBackend` has no plain-2D sampling path, only the cube one, matching XNA's own
    /// RenderTargetCube/TextureCube API shape).
    ///
    /// Deliberately, honestly NOT implemented (documented scope cuts, matching
    /// WebGPURenderTargetBackend's own precedent): mip-chain regeneration (`mipMap=true` throws,
    /// same as `CreateRenderTarget2D`) and MSAA (the `multiSampleCount` constructor argument is
    /// ignored; `GetMultiSampleCount()` always reports 0) -- unlike `WebGPURenderTargetBackend`,
    /// this class does not attempt to mirror the backend's global `sampleCount_`, since a
    /// multisampled array texture resolved per-layer is meaningfully more machinery (a per-face
    /// MSAA colour view/resolve target, still only one live at a time) than this task's time
    /// budget affords landing well-tested; see `plan_webgpu.md`'s `WEBGPU-114` row.
    class WebGPURenderTargetCubeBackend final : public IRenderTargetCubeBackend, public IWebGPUCubeSamplable
    {
    public:
        WebGPURenderTargetCubeBackend(WebGPUGraphicsBackend& owner, int size, int depthFormat, bool mipMap);
        ~WebGPURenderTargetCubeBackend() override;

        WebGPURenderTargetCubeBackend(const WebGPURenderTargetCubeBackend&) = delete;
        WebGPURenderTargetCubeBackend& operator=(const WebGPURenderTargetCubeBackend&) = delete;

        [[nodiscard]] int GetSize() const override { return size_; }
        void BindAsRenderTargetFace(int face) override;
        void UnbindAsRenderTarget() override;
        [[nodiscard]] int GetMultiSampleCount() const override { return 0; }
        /// WEBGPU-114: real per-face CPU readback, same staged-copy technique as
        /// `WebGPUTextureCubeBackend::GetData()` (WEBGPU-113); flushes this face's own pending
        /// draws first if it is still the currently-bound render target (mirrors
        /// `WebGPURenderTargetBackend::GetData()`'s identical on-demand-flush pattern).
        /// REMED-GFX-130: true only when the whole requested face rectangle was written; false
        /// when the readback buffer could not be mapped or the request reaches outside the level.
        /// Pre-fix that failure path `memset` the caller's destination to zero and returned as if
        /// it had succeeded -- the same fabrication the shared layer was doing, one level down.
        [[nodiscard]] bool GetData(int face, int level, int x, int y, int w, int h,
                                   void* data, int dataLength) const override;

        [[nodiscard]] WGPUTexture Texture() const { return texture_; }
        /// The whole-cube sampling view (all 6 layers) -- IWebGPUCubeSamplable's contract.
        [[nodiscard]] WGPUTextureView CubeView() const override { return cubeView_; }
        /// This face's own single-layer 2D view, used as a render-pass colour attachment.
        [[nodiscard]] WGPUTextureView ColorAttachmentView(int face) const { return faceViews_[static_cast<std::size_t>(face)]; }
        /// The one depth+stencil view shared by all 6 faces (only one is ever bound at once).
        [[nodiscard]] WGPUTextureView DepthView() const { return depthView_; }
        /// REMED-GFX-102: exact colour-attachment format shared by all faces. Sprite pipeline
        /// identity uses this format, not the cube object or face identity.
        [[nodiscard]] WGPUTextureFormat ColorFormat() const { return colorFormat_; }

    private:
        WebGPUGraphicsBackend* owner_ = nullptr;
        int size_ = 0;
        /// Mirrors owner_->surfaceFormat_ at construction time (see this class's own .cpp
        /// constructor comment) -- captured so GetData() can decide whether a BGRA->RGBA byte
        /// swap is needed, exactly like WebGPURenderTargetBackend::colorFormat_'s identical role.
        WGPUTextureFormat colorFormat_ = WGPUTextureFormat_Undefined;
        WGPUTexture texture_ = nullptr;
        WGPUTextureView cubeView_ = nullptr;
        std::array<WGPUTextureView, 6> faceViews_{};
        WGPUTexture depthTexture_ = nullptr;
        WGPUTextureView depthView_ = nullptr;
    };

    /// WEBGPU-57/112: a plain volume (3D) texture -- `WGPUTextureDimension_3D`, uploaded/read back
    /// via `wgpuQueueWriteTexture`/a staged `wgpuCommandEncoderCopyTextureToBuffer` readback
    /// exactly like `WebGPUTextureBackend`'s own 2D equivalents, just with a third (depth) extent
    /// dimension. Mirrors `VulkanTexture3DBackend`'s minimal scope: upload/readback only, no
    /// render-target-ness (XNA's `Texture3D` itself is never renderable). Mip-level COUNT uses the
    /// same width/height-only `CalculateMipLevels()` formula as `Texture3D.cpp` (depth does not
    /// participate in the count, matching FNA's `Texture3D` constructor) -- wgpu-native still
    /// halves the actual per-level depth extent automatically (standard 3D-texture mip rules),
    /// this only affects how many levels are allocated.
    class WebGPUTexture3DBackend final : public ITexture3DBackend
    {
    public:
        WebGPUTexture3DBackend(WebGPUGraphicsBackend& owner, int width, int height, int depth, bool mipMap);
        ~WebGPUTexture3DBackend() override;

        WebGPUTexture3DBackend(const WebGPUTexture3DBackend&) = delete;
        WebGPUTexture3DBackend& operator=(const WebGPUTexture3DBackend&) = delete;

        /// REMED-GFX-135: same explicit completion contract as WebGPUTextureCubeBackend::SetData.
        [[nodiscard]] bool SetData(int level, int x, int y, int z,
                                   int w, int h, int depth,
                                   const void* data, int dataLength) override;
        /// Same staged-copy/row-alignment/async-map technique as `WebGPUTextureBackend::GetData()`
        /// (WEBGPU-51), extended to a 3rd (depth) dimension: the whole requested level is copied
        /// to a temporary readback buffer sized `alignedBytesPerRow * levelHeight * levelDepth`,
        /// then the @p x,@p y,@p z,@p w,@p h,@p depth sub-volume is extracted from the mapped
        /// memory on the CPU side.
        /// REMED-GFX-130: same explicit completion contract as WebGPUTextureCubeBackend::GetData.
        [[nodiscard]] bool GetData(int level, int x, int y, int z,
                                   int w, int h, int depth,
                                   void* data, int dataLength) const override;

        [[nodiscard]] WGPUTexture Texture() const { return texture_; }

    private:
        WebGPUGraphicsBackend* owner_ = nullptr;
        WGPUTexture texture_ = nullptr;
        int width_ = 0;
        int height_ = 0;
        int depth_ = 0;
        int mipLevels_ = 1;
    };

    class WebGPUVertexBufferBackend final : public IVertexBufferBackend
    {
    public:
        WebGPUVertexBufferBackend(WebGPUGraphicsBackend& owner, int vertexCapacity);
        ~WebGPUVertexBufferBackend() override;

        void SetData(const void* data, int vertexCount, std::size_t strideInBytes) override;
        void SetVertexDeclaration(const VertexDeclaration&) override {}
        void SetDataWithOptions(const void* data, int vertexCount, std::size_t strideInBytes, SetDataOptions options) override;
        [[nodiscard]] int GetVertexCount() const override { return vertexCount_; }

        [[nodiscard]] WGPUBuffer Buffer() const { return buffer_; }
        [[nodiscard]] std::size_t Stride() const { return stride_; }
        // CPU-side copy of the most recent SetData() upload. WGPUBuffer objects are not
        // host-readable without an async GPU->CPU copy, but DrawColoredPrimitives() (Phase 57
        // vertical slice) needs the raw bytes *synchronously* at call time -- the caller's
        // IVertexBufferBackend is typically a function-local temporary (see
        // GraphicsDevice::DrawUserPrimitives()) destroyed before this backend's deferred
        // per-frame render actually runs, so the bytes must be copied out now, not referenced.
        [[nodiscard]] const std::vector<std::uint8_t>& ShadowData() const { return shadowData_; }

    private:
        WebGPUGraphicsBackend* owner_ = nullptr;
        WGPUBuffer buffer_ = nullptr;
        std::uint64_t capacityBytes_ = 0;
        int vertexCapacity_ = 0;
        int vertexCount_ = 0;
        std::size_t stride_ = 0;
        std::vector<std::uint8_t> shadowData_;
    };

    class WebGPUIndexBufferBackend final : public IIndexBufferBackend
    {
    public:
        WebGPUIndexBufferBackend(WebGPUGraphicsBackend& owner, int indexCapacity, bool thirtyTwoBit);
        ~WebGPUIndexBufferBackend() override;

        void SetData16(const void* data, int indexCount) override;
        void SetData32(const void* data, int indexCount) override;
        void SetData16WithOptions(const void* data, int indexCount, SetDataOptions options) override;
        void SetData32WithOptions(const void* data, int indexCount, SetDataOptions options) override;
        [[nodiscard]] int GetIndexCount() const override { return indexCount_; }
        [[nodiscard]] bool IsThirtyTwoBit() const override { return thirtyTwoBit_; }

        [[nodiscard]] WGPUBuffer Buffer() const { return buffer_; }
        // See WebGPUVertexBufferBackend::ShadowData() for why this exists.
        [[nodiscard]] const std::vector<std::uint8_t>& ShadowData() const { return shadowData_; }

    private:
        void Upload(const void* data, int indexCount, bool dataIsThirtyTwoBit);

        WebGPUGraphicsBackend* owner_ = nullptr;
        WGPUBuffer buffer_ = nullptr;
        std::uint64_t capacityBytes_ = 0;
        int indexCapacity_ = 0;
        int indexCount_ = 0;
        bool thirtyTwoBit_ = false;
        std::vector<std::uint8_t> shadowData_;
    };

    /**
     * @brief REMED-GFX-102 normalized, by-value state for one SpriteBatch Begin/End cycle.
     *
     * The six blend factors/functions, attachment write mask, coverage mask, and target
     * compatibility fields are static pipeline state. blendFactorRGBA is genuine WebGPU dynamic
     * state and is intentionally absent from sprite-pipeline cache identity.
     */
    struct WebGPUSpriteBlendSnapshot
    {
        bool blendEnabled = false;
        int colorSrc = 0;
        int colorDst = 1;
        int alphaSrc = 0;
        int alphaDst = 1;
        int colorFunc = 0;
        int alphaFunc = 0;
        int colorWriteMask = 15;
        std::uint32_t multiSampleMask = 0xFFFFFFFFu;
        float blendFactorR = 1.0f;
        float blendFactorG = 1.0f;
        float blendFactorB = 1.0f;
        float blendFactorA = 1.0f;
        WGPUTextureFormat targetFormat = WGPUTextureFormat_Undefined;
        std::uint32_t sampleCount = 1;
    };

    class WebGPUSpriteBatchBackend final : public ISpriteBatchBackend
    {
    public:
        explicit WebGPUSpriteBatchBackend(WebGPUGraphicsBackend& owner);
        ~WebGPUSpriteBatchBackend() override = default;

        void Begin() override;
        void End() override;
        void SetTransformMatrix(const Matrix& matrix) override { transform_ = matrix; }
        void SetCustomEffect(Effect* effect) override;
        void SetSamplerFilter(int textureFilter) override { textureFilter_ = textureFilter; }
        void SetSamplerAddressMode(int addressU, int addressV) override { addressU_ = addressU; addressV_ = addressV; }
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
        WebGPUGraphicsBackend* owner_ = nullptr;
        bool begun_ = false;
        Matrix transform_ = Matrix::getIdentityProperty();
        int textureFilter_ = 0;
        int addressU_ = 1;
        int addressV_ = 1;
        // REMED-GFX-102: captured once in Begin(), before any Draw calls are deferred/sorted.
        // Every resulting SpriteCommand receives this complete value snapshot.
        WebGPUSpriteBlendSnapshot blendSnapshot_{};
    };

    class WebGPUGraphicsBackend final : public IGraphicsBackend
    {
    public:
        /// WEBGPU-78: real per-draw BlendState factors/op, mirroring
        /// VulkanGraphicsBackend::BlendKeyParams field-for-field. Declared early because it is
        /// used by both generic 3D pipelines and the GFX-102 sprite-pipeline translator.
        struct BlendKeyParams
        {
            int colorSrc = 0;   ///< Blend::One (BlendState.Opaque's default source factor)
            int colorDst = 1;   ///< Blend::Zero
            int alphaSrc = 0;
            int alphaDst = 1;
            int colorFunc = 0;  ///< BlendFunction::Add
            int alphaFunc = 0;
        };

        struct SpriteVertex
        {
            float position[3];
            float uv[2];
            float color[4];
        };

        struct SpriteCommand
        {
            const IWebGPUSamplable* texture = nullptr;
            std::array<SpriteVertex, 6> vertices{};
            int textureFilter = 0;
            int addressU = 1;
            int addressV = 1;
            // REMED-GFX-102: complete per-Begin BlendState + BlendFactor capture by value. No
            // BlendState pointer, live backend state, texture identity, sprite identity, or target
            // object identity participates in replay or pipeline selection.
            WebGPUSpriteBlendSnapshot blend{};
            // REMED-GFX-072: the GraphicsDevice.Viewport that was active when this sprite was
            // enqueued. The vertices above are baked VIEWPORT-LOCAL (normalized by Viewport.W/H)
            // when viewportCustom is true; RenderSprites() then sets the rasterizer viewport to this
            // captured sub-region per draw, so a viewport restore before Present cannot mis-place it
            // (WebGPU records the pass viewport live at flush time, unlike Vulkan/SdlGpu snapshots).
            bool viewportCustom = false;
            int viewportX = 0, viewportY = 0, viewportW = 0, viewportH = 0;
            float viewportMinDepth = 0.0f, viewportMaxDepth = 1.0f;
        };

        struct SpritePipelineKey
        {
            bool blendEnabled = false;
            int colorSrc = 0, colorDst = 1;
            int alphaSrc = 0, alphaDst = 1;
            int colorFunc = 0, alphaFunc = 0;
            int colorWriteMask = 15;
            std::uint32_t multiSampleMask = 0xFFFFFFFFu;
            WGPUTextureFormat targetFormat = WGPUTextureFormat_Undefined;
            std::uint32_t sampleCount = 1;

            [[nodiscard]] bool operator==(const SpritePipelineKey& other) const noexcept
            {
                return blendEnabled == other.blendEnabled &&
                    colorSrc == other.colorSrc && colorDst == other.colorDst &&
                    alphaSrc == other.alphaSrc && alphaDst == other.alphaDst &&
                    colorFunc == other.colorFunc && alphaFunc == other.alphaFunc &&
                    colorWriteMask == other.colorWriteMask &&
                    multiSampleMask == other.multiSampleMask &&
                    targetFormat == other.targetFormat && sampleCount == other.sampleCount;
            }
        };

        struct SpritePipelineKeyHash
        {
            [[nodiscard]] std::size_t operator()(const SpritePipelineKey& key) const noexcept
            {
                std::size_t hash = 1469598103934665603ull;
                const auto mix = [&hash](std::size_t value)
                {
                    hash ^= value;
                    hash *= 1099511628211ull;
                };
                mix(key.blendEnabled ? 1u : 0u);
                mix(static_cast<std::size_t>(key.colorSrc));
                mix(static_cast<std::size_t>(key.colorDst));
                mix(static_cast<std::size_t>(key.alphaSrc));
                mix(static_cast<std::size_t>(key.alphaDst));
                mix(static_cast<std::size_t>(key.colorFunc));
                mix(static_cast<std::size_t>(key.alphaFunc));
                mix(static_cast<std::size_t>(key.colorWriteMask));
                mix(static_cast<std::size_t>(key.multiSampleMask));
                mix(static_cast<std::size_t>(key.targetFormat));
                mix(static_cast<std::size_t>(key.sampleCount));
                return hash;
            }
        };

        WebGPUGraphicsBackend(SDL_Window* window, int virtualWidth, int virtualHeight,
                              CnaPresentationMode presentationMode, int swapInterval);
        ~WebGPUGraphicsBackend() override;

        WebGPUGraphicsBackend(const WebGPUGraphicsBackend&) = delete;
        WebGPUGraphicsBackend& operator=(const WebGPUGraphicsBackend&) = delete;

        /// REMED-GFX-102 diagnostic used by the permanent regression. Dynamic BlendFactor RGBA,
        /// texture/sprite identity, and target object identity must not grow this cache.
        [[nodiscard]] std::size_t GetSpritePipelineCacheSizeEXT() const noexcept
        {
            return spritePipelines_.size();
        }
        /// REMED-GFX-105 diagnostic used to prove format-compatible index-buffer objects reuse a
        /// Colored3D pipeline while Uint16/Uint32 indexed strips require exactly two variants.
        [[nodiscard]] std::size_t GetColoredPipelineCacheSizeEXT() const noexcept
        {
            return coloredPipelines_.size();
        }
        /// Native validation callback count since device creation. The GFX-102 regression checks
        /// this after all render-target/backbuffer/MSAA transitions and logs the exact total.
        [[nodiscard]] std::size_t GetUncapturedErrorCountEXT() const noexcept
        {
            return uncapturedErrorCount_.load();
        }

        void Clear(float r, float g, float b, float a) override;
        void Present() override;
        void ReadBackbuffer(int x, int y, int w, int h, uint8_t* pixels) override;
        void GetViewportSize(int& width, int& height) override;
        void SetVirtualResolution(int width, int height) override;
        void SetPresentationMode(int mode) override;
        void SetSwapInterval(int interval) override;
        /// WEBGPU-58: reconfigures this backend's single GLOBAL MSAA sample count in place --
        /// mirroring VulkanGraphicsBackend::ApplyMultiSampleCount()'s own "one value, invalidate
        /// every pipeline cache, rebuild lazily" design (see sampleCount_'s own comment for why
        /// this is a backend-wide value rather than a per-pipeline-cache-key dimension). Returns
        /// the real, device-clamped applied count (0 = disabled) via GetMultiSampleCount().
        int ApplyMultiSampleCount(int requestedMultiSampleCount) override;
        /// WEBGPU-58: the backbuffer's real, applied MSAA sample count (0 = none).
        [[nodiscard]] int GetMultiSampleCount() const override;
        bool TransformWindowToLogical(float windowX, float windowY, float& logicalX, float& logicalY) const override;
        bool TransformLogicalToWindow(float logicalX, float logicalY, float& windowX, float& windowY) const override;

        [[nodiscard]] SDL_Window* GetWindowInternal() const override { return window_; }
        [[nodiscard]] SDL_Renderer* GetRendererInternal() const override { return nullptr; }

        std::unique_ptr<ITextureBackend> CreateTexture(const ImageData& data) override;
        std::unique_ptr<ISpriteBatchBackend> CreateSpriteBatch() override;

        void ClearColorAndDepth(float r, float g, float b, float a, float depth) override;
        void ClearDepth(float depth) override;
        void ClearStencil(int stencil) override;
        void ClearDepthAndStencil(float depth, int stencil) override;
        void ClearColorAndStencil(float r, float g, float b, float a, int stencil) override;
        void ClearColorDepthAndStencil(float r, float g, float b, float a, float depth, int stencil) override;
        void SetDepthTestEnabled(bool enabled) override { depthTestEnabled_ = enabled; }
        void SetBlendEnabled(bool enabled) override { blendEnabled_ = enabled; }
        void SetDepthWriteEnabled(bool enabled) override { depthWriteEnabled_ = enabled; }
        // Depth portion was the original Phase 57/63 vertical slice; WEBGPU-83 extends this to
        // also STORE the stencil parameters (dsParams_ below) for a future task to bake into each
        // pipeline's WGPUStencilFaceState -- see dsParams_'s own comment for why that bake-in step
        // is deliberately still deferred. Without the depth half of this override,
        // GraphicsDevice.DepthStencilState (the real XNA API surface almost every game/effect
        // uses, as opposed to the older SetDepthTestEnabled()/SetDepthWriteEnabled() convenience
        // methods above) had zero effect on this backend -- found and fixed while verifying
        // DrawColoredPrimitives' own depth-test pixel test.
        void ApplyDepthStencilState(bool depthEnable, bool depthWriteEnable, int depthFunc,
                                    bool stencilEnable, int stencilFunc,
                                    int stencilPass, int stencilFail, int stencilDepthFail,
                                    int stencilMask, int stencilWriteMask, int referenceStencil,
                                    bool twoSidedStencilMode,
                                    int ccwStencilFunc, int ccwStencilPass,
                                    int ccwStencilFail, int ccwStencilDepthFail) override;
        // WEBGPU-78: BlendState -> WGPUBlendState (storage-only; consumed by every 3D
        // GetOrCreatePipeline*() below at pipeline-creation time -- wgpu-native bakes blend state
        // into the pipeline object, there is no dynamic override).
        void ApplyBlendState(int colorSrcBlend, int alphaSrcBlend, int colorDstBlend, int alphaDstBlend,
                             int colorBlendFunc, int alphaBlendFunc,
                             const BlendWriteState& writeState) override;
        // WEBGPU-41/79: CullMode/FillMode -> WGPUPrimitiveState (storage-only; also baked into the
        // pipeline object). FillMode::WireFrame is stored but has no rendering effect -- wgpu-native
        // has no polygon-mode API at all (WEBGPU-115, a documented, accepted deviation, same as
        // Vulkan's own comment references). DepthBias/SlopeScaleDepthBias ARE genuinely supported by
        // WGPUDepthStencilState (unlike a dynamic vkCmdSetDepthBias-style override) and are baked in too.
        void ApplyRasterizerState(int cullMode, int fillMode, bool scissorTestEnable,
                                  float depthBias = 0.0f, float slopeScaleDepthBias = 0.0f) override;
        // WEBGPU-82: per-slot SamplerState -> a genuine WGPUSampler cache (GetOrCreateSlotSampler()
        // below), distinct from the SpriteBatch-only 18-entry samplerCache_ (see that member's own
        // comment). Storage-only here; actually read by every texture-consuming Queue*Draw() at
        // queue time for slot 0 (see each command's textureFilter/addressU/addressV capture).
        void ApplySamplerState(int slot, int filter, int addressU, int addressV, int maxAnisotropy) override;
        // WEBGPU-80/81/29/30: scissor rect, viewport, blend constant and stencil reference are all
        // genuinely dynamic wgpu-native render-pass state (wgpuRenderPassEncoderSetScissorRect/
        // SetViewport/SetBlendConstant/SetStencilReference), exactly like Vulkan's own
        // vkCmdSetScissor/Viewport/BlendConstants/StencilReference -- storage-only here, applied
        // once per render pass in EnsureFrameRendered() from whatever is current at record time
        // (this backend's single-deferred-pass-per-frame model, matching
        // VulkanGraphicsBackend::SetViewport()'s own documented simplification).
        void SetBlendFactor(float r, float g, float b, float a) override;
        void SetReferenceStencil(int value) override;
        void SetScissorRect(int x, int y, int w, int h) override;
        void SetViewport(int x, int y, int w, int h, float minDepth, float maxDepth) override;
        std::unique_ptr<IVertexBufferBackend> CreateVertexBuffer(int vertexCapacity) override;
        std::unique_ptr<IIndexBufferBackend> CreateIndexBuffer16(int indexCapacity) override;
        std::unique_ptr<IIndexBufferBackend> CreateIndexBuffer32(int indexCapacity) override;
        void DrawColoredPrimitives(const IVertexBufferBackend& vb,
                                   const Matrix& world, const Matrix& view, const Matrix& projection,
                                   PrimitiveType primitive, int primitiveCount) override;
        void DrawIndexedColoredPrimitives(const IVertexBufferBackend& vb,
                                          const IIndexBufferBackend& ib,
                                          const Matrix& world, const Matrix& view, const Matrix& projection,
                                          PrimitiveType primitive, int primitiveCount) override;
        // Real GpuDrawParams dispatch for stride-16 (VertexPositionColor) draws only -- reuses
        // the exact same colored3d.wgsl/GetOrCreatePipelineColored3D() infrastructure as
        // DrawColoredPrimitives(), but fills the uniform buffer from the caller's real
        // DiffuseColor/VertexColorEnabled instead of hardcoded white/true. Other strides (20/24/32,
        // needing textured3d/lit_textured3d -- not yet written, see plan_webgpu.md Phase 58) and
        // other effects (alpha test/dual texture/env map/skinned) fall back to
        // DrawColoredPrimitives()/DrawIndexedColoredPrimitives(), replicating exactly what
        // IGraphicsBackend's own default implementation already did before this override existed.
        void DrawPrimitivesEx(const IVertexBufferBackend& vb,
                              const Matrix& world, const Matrix& view, const Matrix& projection,
                              PrimitiveType primitive, int primitiveCount,
                              const GpuDrawParams& params) override;
        void DrawIndexedPrimitivesEx(const IVertexBufferBackend& vb, const IIndexBufferBackend& ib,
                                     const Matrix& world, const Matrix& view, const Matrix& projection,
                                     PrimitiveType primitive, int primitiveCount,
                                     const GpuDrawParams& params) override;
        // WEBGPU-27/38/68: real per-instance mat4 world transform via a genuine second
        // WGPUVertexStepMode_Instance vertex buffer binding -- see instancedShader_'s own doc
        // comment. params.instanceVb == nullptr falls back to a real (non-instanced)
        // DrawIndexedPrimitivesEx() dispatch, matching every other backend's identical fallback
        // (VulkanGraphicsBackend::DrawInstancedPrimitivesEx's own precedent).
        void DrawInstancedPrimitivesEx(const IVertexBufferBackend& vb, const IIndexBufferBackend& ib,
                                       const Matrix& world, const Matrix& view, const Matrix& projection,
                                       PrimitiveType primitive, int primitiveCount, int instanceCount,
                                       const GpuDrawParams& params) override;

        void QueueSprite(const ITextureBackend& texture,
                         const IWebGPUSamplable& samplable,
                         const Rectangle& destinationRectangle,
                         const Rectangle& sourceRectangle,
                         const Color& color,
                         float rotation,
                         const Vector2& origin,
                         SpriteEffects effects,
                         float layerDepth,
                         const Matrix& transform,
                         int textureFilter,
                         int addressU,
                         int addressV,
                         const WebGPUSpriteBlendSnapshot& blendSnapshot);

        [[nodiscard]] WGPUDevice Device() const { return device_; }
        [[nodiscard]] WGPUQueue Queue() const { return queue_; }
        [[nodiscard]] WGPUInstance Instance() const { return instance_; }

        // WEBGPU-53/54: RenderTarget2D support (single-target only; MRT is WEBGPU-85/86/87).
        std::unique_ptr<IRenderTargetBackend> CreateRenderTarget2D(int w, int h, int depthFormat,
                                                                    bool preserveContents = false,
                                                                    bool mipMap = false,
                                                                    int multiSampleCount = 0) override;
        void SetRenderTarget2D(IRenderTargetBackend* rt) override;
        void SetRenderTargets(const RenderTargetBindingDescriptor* renderTargets,
                              int count) override;

        /// WEBGPU-56/74: minimal cube-map texture creation, sufficient for
        /// `EnvironmentMapEffect.EnvironmentMap` -- see `WebGPUTextureCubeBackend`'s own doc
        /// comment for the documented TextureCube/RenderTargetCube parity gaps this does NOT close.
        std::unique_ptr<ITextureCubeBackend> CreateTextureCube(int size, bool mipMap, int surfaceFormat) override;

        /// WEBGPU-57/112: real volume (3D) texture creation -- see `WebGPUTexture3DBackend`'s own
        /// doc comment. `surfaceFormat` is currently ignored (always `WGPUTextureFormat_RGBA8Unorm`,
        /// matching `WebGPUTextureBackend`/`WebGPUTextureCubeBackend`'s own established convention
        /// for this backend).
        std::unique_ptr<ITexture3DBackend> CreateTexture3D(int w, int h, int depth, bool mipMap, int surfaceFormat) override;

        /// WEBGPU-114: real render-into-a-cube-face support -- see `WebGPURenderTargetCubeBackend`'s
        /// own doc comment for exactly what this does and does not implement (no mip regen, no MSAA).
        std::unique_ptr<IRenderTargetCubeBackend> CreateRenderTargetCube(int size, int depthFormat,
                                                                          bool mipMap = false,
                                                                          int multiSampleCount = 0) override;

    private:
        friend class WebGPURenderTargetBackend;
        friend class WebGPURenderTargetCubeBackend;
        friend class WebGPUTextureBackend;
        friend class WebGPUTextureCubeBackend;
        friend class WebGPUSpriteBatchBackend;
        struct LogicalViewport
        {
            float x = 0.0f;
            float y = 0.0f;
            float width = 0.0f;
            float height = 0.0f;
            float logicalWidth = 0.0f;
            float logicalHeight = 0.0f;
        };

        void CreateSurface();
        void RequestAdapterAndDevice();
        void ConfigureSurface(bool force = false);
        void CreateSpriteResources();
        void DestroySpriteResources();
        [[nodiscard]] WebGPUSpriteBlendSnapshot CaptureSpriteBlendSnapshot() const;
        [[nodiscard]] WGPURenderPipeline GetOrCreateSpritePipeline(
            const WebGPUSpriteBlendSnapshot& snapshot);
        void CreateColoredResources();
        void DestroyColoredResources();
        void RecreateDepthTexture();
        // WEBGPU-58: (re)creates/destroys the backbuffer's own multisampled colour texture
        // (msaaColorTexture_/msaaColorView_), sized to physicalWidth_/physicalHeight_ at the
        // CURRENT sampleCount_ -- a no-op (leaves both null) whenever sampleCount_ <= 1. Called
        // alongside RecreateDepthTexture() from ConfigureSurface() (resize) and
        // ApplyMultiSampleCount() (MSAA-count change) -- the same two triggers that already
        // invalidate the depth texture.
        void RecreateMsaaColorTexture();
        // WEBGPU-58/GFX-102: releases every WGPURenderPipeline in all 3D pipeline-cache maps plus
        // the keyed SpriteBatch pipeline cache, then recreates their shared resources --
        // called only from ApplyMultiSampleCount() when sampleCount_ actually changes, mirroring
        // VulkanGraphicsBackend::ApplyMultiSampleCount()'s own "clear every pipeline cache, rebuild
        // lazily on next use" approach. Every GetOrCreatePipeline*3D() reads the NEW sampleCount_
        // the next time it is called for a given cache key, since none of them treat msaa as part
        // of Make3DPipelineKey() (WEBGPU-30's own documented scope).
        void ClearAllPipelineCaches();
        // WEBGPU-58: PickSampleCount(requested)'s own device-capability half -- empirically
        // verifies (via a scratch wgpuDeviceCreateTexture + WGPUErrorFilter_Validation error-scope
        // round trip, not merely assumed from the WebGPU spec's "count must be 1 or 4" text) that
        // this concrete adapter/device/surfaceFormat_ combination can really create a
        // sampleCount=4 RENDER_ATTACHMENT texture. Cached after the first call (a real GPU
        // round-trip, not free) since ApplyMultiSampleCount() may be called more than once.
        [[nodiscard]] bool Supports4xMsaa();
        // WEBGPU-58: clamps a requested MultiSampleCount to this backend's only two legal
        // WGPUMultisampleState.count values (1 or 4 -- unlike Vulkan's 1/2/4/8/16/32/64 range,
        // wgpu-native/WebGPU has no adjustable per-device "max sample count" limit at all; see
        // Supports4xMsaa()'s own comment). Mirrors VulkanGraphicsBackend's own PickSampleCount()
        // "highest legal value <= requested that the device actually supports" semantics: a
        // requested count in [2,4] rounds down to 1 unless 4x is supported (in which case it
        // rounds UP to 4, since 4 is the only value above 1), and anything >= 4 clamps down to 4.
        [[nodiscard]] int PickSampleCount(int requestedMultiSampleCount);
        void RenderSprites(WGPURenderPassEncoder pass, std::uint32_t targetWidth,
                           std::uint32_t targetHeight, WGPUTextureFormat targetFormat,
                           std::uint32_t targetSampleCount);
        void RenderColoredDraws(WGPURenderPassEncoder pass);
        [[nodiscard]] WGPURenderPipeline GetOrCreatePipelineColored3D(WGPUPrimitiveTopology topology,
                                                                       WGPUIndexFormat stripIndexFormat,
                                                                       bool depthTest, bool depthWrite,
                                                                       int depthFunc,
                                                       bool blend, const BlendKeyParams& blendParams,
                                                       int cullMode, bool wireframe,
                                                       float depthBias, float slopeScaleDepthBias);
        // params == nullptr: the legacy DrawColoredPrimitives path (hardcoded white diffuse,
        // vertexColorEnabled=true, vertexStart=0). params != nullptr: DrawPrimitivesEx's real
        // GpuDrawParams dispatch (stride-16 only -- caller must have already verified the stride).
        void QueueColoredDraw(const IVertexBufferBackend& vb, const IIndexBufferBackend* ib,
                              const Matrix& world, const Matrix& view, const Matrix& projection,
                              PrimitiveType primitive, int primitiveCount,
                              const GpuDrawParams* params = nullptr);
        void CaptureReadback(WGPUCommandEncoder encoder, WGPUTexture surfaceTexture);
        // Acquires a swapchain texture if none is currently held, and renders any pending
        // Clear()/sprite work into it. Called on demand by both Present() and ReadBackbuffer()
        // so that GetBackBufferData() can observe work queued earlier in the same logical frame,
        // matching the Vulkan/Bgfx backends' own on-demand-submit readback semantics. Returns
        // false if the surface isn't presentable right now (minimized, lost, etc).
        bool EnsureFrameRendered();
        // WEBGPU-53/54: renders every currently queued Clear()/3D-draw/SpriteBatch command into a
        // single render pass targeting the given RenderTarget2D's own colour+depth attachments,
        // then submits and clears every per-frame draw-command queue -- the RT-targeting sibling
        // of EnsureFrameRendered()'s swapchain-targeting pass. See SetRenderTarget2D()'s own
        // comment for why this "flush eagerly on every target switch" design (rather than tagging
        // every draw command with a target and replaying grouped-by-target at final-flush time,
        // closer to VulkanGraphicsBackend's own deferred-recording model) was chosen.
        void RenderPendingDrawsToRenderTarget(WebGPURenderTargetBackend* target);
        // WEBGPU-114: the cube-face counterpart of RenderPendingDrawsToRenderTarget() -- same
        // "one deferred render pass, flushed the instant the bound target changes" model, just
        // targeting one face's own ColorAttachmentView(face)/the shared DepthView() instead of a
        // RenderTarget2D's own single pair.
        void RenderPendingDrawsToRenderTargetCubeFace(WebGPURenderTargetCubeBackend* target, int face);
        // WEBGPU-114: single shared "flush whatever is currently bound" entry point -- used by
        // both SetRenderTarget2D() and WebGPURenderTargetCubeBackend::BindAsRenderTargetFace() so
        // switching between ANY combination of {backbuffer, RenderTarget2D, a RenderTargetCube
        // face} always flushes the OUTGOING target's own pending draws into its own render pass
        // first, exactly like the pre-existing RenderTarget2D-only switch logic did. Does not
        // itself clear/reassign currentRenderTarget_/currentRenderTargetCubeFace_ -- callers own
        // that, mirroring the pre-existing SetRenderTarget2D() call-then-assign structure.
        void FlushCurrentRenderTarget();
        [[nodiscard]] LogicalViewport ComputeLogicalViewport() const;
        [[nodiscard]] WGPUSampler GetOrCreateSampler(int textureFilter, int addressU, int addressV);
        // WEBGPU-82: full per-slot SamplerState cache (filter + address mode + anisotropy), used
        // by every texture-consuming 3D Queue*Draw() -- distinct from GetOrCreateSampler() above,
        // which only ever needs SpriteBatch's simpler binary linear/nearest split.
        [[nodiscard]] WGPUSampler GetOrCreateSlotSampler(int filter, int addressU, int addressV, int maxAnisotropy);
        [[nodiscard]] WGPUPrimitiveTopology ToTopology(PrimitiveType primitive) const;
        [[nodiscard]] int PrimitiveVertexCount(PrimitiveType primitive, int primitiveCount) const;
        [[nodiscard]] int PrimitiveIndexCount(PrimitiveType primitive, int primitiveCount) const;
        [[noreturn]] static void ThrowUnsupported3DDraw(const char* method);

        // WEBGPU-52: real, genuinely-linear-filtered mip generation for a plain Texture2D/
        // TextureCube, via a render pass per mip level that draws a full-screen triangle sampling
        // the PREVIOUS level through a real linear WGPUSampler into the NEXT level's own
        // render-attachment view -- wgpu-native has no vkCmdBlitImage-equivalent filtered-
        // downsample primitive (verified against this project's pinned wgpu-native v29.0.1.1
        // webgpu.h: wgpuCommandEncoderCopyTextureToTexture is a raw same-size copy, nothing else
        // resembling a blit exists), so this is the same technique other WebGPU-based engines use.
        // IMPORTANT, DELIBERATE DIVERGENCE (documented, not silently introduced): unlike every
        // other CNA graphics backend (VulkanGraphicsBackend/EasyGLGraphicsBackend), which only
        // regenerate mip content for a RENDER TARGET being unbound (matching real FNA3D's
        // OPENGL_ResolveTarget/vkCmdBlitImage-on-unbind semantics -- a PLAIN Texture2D/TextureCube
        // never auto-generates mip content from level 0 in FNA/XNA itself, matching every other
        // CNA backend's own plain-texture behaviour too), this WebGPU-only helper DOES generate
        // real mip content for a plain Texture2D/TextureCube automatically whenever mipMap=true
        // AND non-empty initial pixel data is supplied at construction time. This makes WebGPU's
        // plain-texture mip behaviour genuinely BETTER (never garbage/undefined content at
        // level>0) but ALSO genuinely DIFFERENT from FNA and every sibling CNA backend for the
        // identical public Texture2D/TextureCube(mipMap=true) constructor call -- see
        // plan_webgpu.md's WEBGPU-52 row and docs/webgpu-backend.md for the full investigation
        // this finding is based on. Explicit per-level SetData(level>0, ...) calls made AFTER
        // construction are, as before, never auto-regenerated (matches every other backend).
        void EnsureMipBlitPipeline();
        // Shared implementation: generates levels 1..mipLevels-1 of ONE array layer of `texture`
        // from that layer's own level 0. GenerateMips2D()/GenerateMipsCubeFace() are thin
        // wrappers (layer=0 for a plain 2D texture, layer=face for one cube face).
        void GenerateMipsForLayer(WGPUTexture texture, int layer, int width, int height, int mipLevels);
        // Generates levels 1..mipLevels-1 of a plain (non-array) 2D texture from level 0.
        void GenerateMips2D(WGPUTexture texture, int width, int height, int mipLevels);
        // Generates levels 1..mipLevels-1 of ONE array layer (cube face) of a 6-layer texture,
        // leaving the other 5 faces' mip chains untouched -- called once per face.
        void GenerateMipsCubeFace(WGPUTexture texture, int face, int size, int mipLevels);

        SDL_Window* window_ = nullptr;
        void* metalView_ = nullptr;
        WGPUInstance instance_ = nullptr;
        WGPUSurface surface_ = nullptr;
        WGPUAdapter adapter_ = nullptr;
        WGPUDevice device_ = nullptr;
        WGPUQueue queue_ = nullptr;
        WGPUSurfaceConfiguration surfaceConfig_{};
        WGPUTextureFormat surfaceFormat_ = WGPUTextureFormat_Undefined;
        WGPUTexture depthTexture_ = nullptr;
        WGPUTextureView depthView_ = nullptr;

        // WEBGPU-58: single backend-GLOBAL MSAA sample count (1 = disabled), mirroring
        // VulkanGraphicsBackend::sampleCount_'s own "one value shared by every pipeline;
        // invalidate/rebuild everything on an (expected-rare) change" simplification -- NOT a new
        // per-pipeline-cache-key dimension (Make3DPipelineKey()/WEBGPU-30 is unchanged). Every one
        // of the ~10 GetOrCreatePipeline*3D() families plus every keyed SpriteBatch pipeline bake
        // this SAME value into their own WGPUMultisampleState.count. Only ever 1 or 4 -- see
        // PickSampleCount()'s own comment for why WebGPU has no wider range here, unlike Vulkan.
        int sampleCount_ = 1;
        // Cached result of Supports4xMsaa()'s own real device-capability probe: -1 = not probed
        // yet, 0 = unsupported, 1 = supported.
        int msaa4xSupported_ = -1;
        // WEBGPU-58: the backbuffer's own multisampled colour attachment, only non-null while
        // sampleCount_ > 1 -- the swapchain's own per-frame texture (acquiredTexture_) becomes the
        // RESOLVE target every frame instead of being drawn into directly (see
        // EnsureFrameRendered()). Recreated by RecreateMsaaColorTexture() on resize/MSAA-count
        // change, exactly like depthTexture_/depthView_ above.
        WGPUTexture msaaColorTexture_ = nullptr;
        WGPUTextureView msaaColorView_ = nullptr;

        WGPUShaderModule spriteShader_ = nullptr;
        WGPUBindGroupLayout spriteBindGroupLayout_ = nullptr;
        WGPUPipelineLayout spritePipelineLayout_ = nullptr;
        std::unordered_map<SpritePipelineKey, WGPURenderPipeline, SpritePipelineKeyHash>
            spritePipelines_;
        WGPUBuffer spriteVertexBuffer_ = nullptr;
        std::uint64_t spriteVertexCapacityBytes_ = 0;
        std::array<WGPUSampler, 18> samplerCache_{};

        // WEBGPU-52: mip-blit resources -- see EnsureMipBlitPipeline()'s own doc comment. Created
        // lazily on first use (a game that never requests mipMap=true with initial pixel data
        // never pays for these). Always targets WGPUTextureFormat_RGBA8Unorm, matching
        // WebGPUTextureBackend/WebGPUTextureCubeBackend's own fixed format -- no per-format cache
        // needed, unlike the ~10 GetOrCreatePipeline*3D() families.
        WGPUShaderModule mipBlitShader_ = nullptr;
        WGPUBindGroupLayout mipBlitBindGroupLayout_ = nullptr;
        WGPUPipelineLayout mipBlitPipelineLayout_ = nullptr;
        WGPURenderPipeline mipBlitPipeline_ = nullptr;
        WGPUSampler mipBlitSampler_ = nullptr;

        std::vector<SpriteCommand> spriteCommands_;
        std::atomic<std::size_t> uncapturedErrorCount_{0};
        int physicalWidth_ = 0;
        int physicalHeight_ = 0;
        int virtualWidth_ = 0;
        int virtualHeight_ = 0;
        CnaPresentationMode presentationMode_ = CnaPresentationMode::FixedHeightDynamicWidth;
        int swapInterval_ = 1;
        bool surfaceConfigured_ = false;
        bool clearColorPending_ = true;
        bool clearDepthPending_ = true;
        bool clearStencilPending_ = false;
        WGPUColor clearColor_{0.0, 0.0, 0.0, 1.0};
        float clearDepth_ = 1.0f;
        std::uint32_t clearStencil_ = 0;
        bool depthTestEnabled_ = false;
        bool depthWriteEnabled_ = false;
        bool blendEnabled_ = true;
        int depthCompareFunction_ = 3;  ///< XNA CompareFunction ordinal; 3 = LessEqual (DepthStencilState.Default)

        // WEBGPU-41/77/78/79/80/81/82/83: real BlendState/RasterizerState/SamplerState/scissor/
        // viewport/blend-factor/reference-stencil storage, extending the depth-only slice above.
        // See ApplyBlendState()/ApplyRasterizerState()/ApplySamplerState()/SetBlendFactor()/
        // SetReferenceStencil()/SetScissorRect()/SetViewport()'s own declarations (top of this
        // class) for which of these are baked into every 3D pipeline object vs. applied once per
        // render pass as genuine wgpu-native dynamic state.
        BlendKeyParams blendParams_{};
        /// REMED-GFX-077: BlendState.ColorWriteChannels slot 0 (bit0=R..bit3=A) and MultiSampleMask.
        /// Both are STATIC wgpu-native pipeline state (WGPUColorTargetState.writeMask /
        /// WGPUMultisampleState.mask) and are folded into Make3DPipelineKey for the keyed 3D
        /// pipelines. Defaults match XNA (All / 0xFFFFFFFF).
        int colorWriteMask_ = 15;
        std::uint32_t sampleMask_ = 0xFFFFFFFFu;
        /// XNA ColorWriteChannels (R=1,G=2,B=4,A=8) is bit-identical to WGPUColorWriteMask_*.
        [[nodiscard]] WGPUColorWriteMask CurrentWriteMask() const
        { return static_cast<WGPUColorWriteMask>(colorWriteMask_ & 0xF); }
        [[nodiscard]] std::uint32_t CurrentSampleMask() const { return sampleMask_; }
        int cullMode_ = 0;                ///< XNA CullMode::None
        bool fillModeWireframe_ = false;  ///< XNA FillMode::WireFrame -- stored only, see WEBGPU-115
        bool scissorEnabled_ = false;
        float depthBias_ = 0.0f;
        float slopeScaleDepthBias_ = 0.0f;
        int scissorX_ = 0;
        int scissorY_ = 0;
        int scissorW_ = 0;
        int scissorH_ = 0;
        bool viewportSet_ = false;
        int viewportX_ = 0;
        int viewportY_ = 0;
        int viewportW_ = 0;
        int viewportH_ = 0;
        float viewportMinDepth_ = 0.0f;
        float viewportMaxDepth_ = 1.0f;
        float blendFactorR_ = 1.0f;
        float blendFactorG_ = 1.0f;
        float blendFactorB_ = 1.0f;
        float blendFactorA_ = 1.0f;
        int referenceStencil_ = 0;
        // WEBGPU-83: stencil op storage only -- captured for a future task to bake into each
        // pipeline's WGPUStencilFaceState. Deliberately NOT yet applied to any pipeline (stencil
        // test stays Always/Keep everywhere, unchanged from prior behavior, so this is not a
        // regression): VulkanGraphicsBackend::FillDepthStencilState()'s own comment documents a
        // real, empirically-discovered front/back-winding quirk it had to work around on that
        // backend, and baking stencil ops into this backend's 10 pipeline families without an
        // equivalent WebGPU-side differential test risks the same silent-wrongness class this
        // project explicitly avoids -- see plan_webgpu.md's WEBGPU-83 row for the scope cut.
        bool stencilEnable_ = false;
        int stencilFunc_ = 0;
        int stencilPass_ = 0;
        int stencilFail_ = 0;
        int stencilDepthFail_ = 0;
        int stencilReadMask_ = ~0;
        int stencilWriteMask_ = ~0;
        bool twoSidedStencilMode_ = false;
        int ccwStencilFunc_ = 0;
        int ccwStencilPass_ = 0;
        int ccwStencilFail_ = 0;
        int ccwStencilDepthFail_ = 0;

        // WEBGPU-82: per-slot SamplerState (slot 0 is texture0, read by every texture-consuming 3D
        // Queue*Draw() at queue time -- see GetOrCreateSlotSampler()).
        struct SlotSamplerState
        {
            int filter = 0;    ///< XNA TextureFilter::Linear
            int addressU = 1;  ///< matches every *DrawCommand struct's own textureFilter/addressU/
            int addressV = 1;  ///< addressV default (Clamp) -- this file's established convention.
            int maxAnisotropy = 4;
        };
        std::array<SlotSamplerState, 16> slotSamplers_{};
        std::unordered_map<std::uint32_t, WGPUSampler> slotSamplerCache_;

        WGPUBuffer readbackBuffer_ = nullptr;
        std::uint64_t readbackBufferCapacity_ = 0;
        std::uint32_t readbackBytesPerRow_ = 0;
        int readbackWidth_ = 0;
        int readbackHeight_ = 0;
        bool readbackValid_ = false;

        bool hasAcquiredTexture_ = false;
        WGPUTexture acquiredTexture_ = nullptr;
        bool framePending_ = true;

        // WEBGPU-53/54: the currently bound RenderTarget2D backend, or nullptr for the swapchain
        // backbuffer. Present()/ReadBackbuffer() always target the backbuffer specifically
        // (EnsureFrameRendered() does not consult this) -- matching their pre-existing hardcoded
        // swapchain-only behaviour -- so a render target left bound across a Present() call has
        // its own pending draws deferred until the next SetRenderTarget2D() switch rather than
        // appearing on screen; this is a narrow, documented edge case (a game is expected to
        // restore the backbuffer via SetRenderTarget(nullptr) before Present(), same convention
        // every other CNA backend already assumes).
        WebGPURenderTargetBackend* currentRenderTarget_ = nullptr;

        // WEBGPU-114: the cube-face sibling of currentRenderTarget_ above -- non-null exactly
        // when a RenderTargetCube face is the currently-bound render target (mutually exclusive
        // with currentRenderTarget_; only one of the two, or neither -- the backbuffer -- is ever
        // set). currentRenderTargetCubeFaceIndex_ is only meaningful while
        // currentRenderTargetCubeFace_ is non-null.
        WebGPURenderTargetCubeBackend* currentRenderTargetCubeFace_ = nullptr;
        int currentRenderTargetCubeFaceIndex_ = -1;

        // Phase 57/63 vertical slice: DrawColoredPrimitives()/DrawIndexedColoredPrimitives() only
        // (VertexPositionColor, stride 16 -- see plan_webgpu.md's Phase 57 entry-point note). The
        // 128-float uniform layout matches VulkanGraphicsBackend::FillExtPushConst() byte-for-byte
        // so the same shader/uniform shape can be reused once DrawPrimitivesEx (full BasicEffect
        // dispatch) lands -- IGraphicsBackend::DrawPrimitivesEx's own default implementation
        // already falls back to DrawColoredPrimitives, so this also unblocks simple (unlit,
        // untextured) Model/BasicEffect draws going through that fallback.
        struct ColoredDrawCommand
        {
            std::vector<std::uint8_t> vertexData;
            std::vector<std::uint8_t> indexData;   ///< empty for a non-indexed draw
            bool indexed = false;
            bool index32 = false;
            std::uint32_t vertexCount = 0;
            std::uint32_t indexCount = 0;
            std::uint32_t firstIndex = 0;
            std::int32_t baseVertex = 0;
            WGPUPrimitiveTopology topology = WGPUPrimitiveTopology_TriangleList;
            std::array<float, 32> uniforms{};
            bool depthTest = false;
            bool depthWrite = false;
            int depthFunc = 3;  ///< XNA CompareFunction ordinal; 3 = LessEqual
            // WEBGPU-41/77/78/79: baked into the pipeline object alongside depthTest/
            // depthWrite/depthFunc above (see BlendKeyParams' own comment for why this must be
            // captured per-draw-command, not read as frame-global state at render time).
            bool blend = true;
            BlendKeyParams blendParams{};
            int cullMode = 0;
            bool wireframe = false;
            float depthBias = 0.0f;
            float slopeScaleDepthBias = 0.0f;
        };
        WGPUShaderModule coloredShader_ = nullptr;
        WGPUBindGroupLayout coloredBindGroupLayout_ = nullptr;
        WGPUPipelineLayout coloredPipelineLayout_ = nullptr;
        std::unordered_map<std::uint64_t, WGPURenderPipeline> coloredPipelines_;  ///< keyed by Make3DPipelineKey() (WEBGPU-30)
        std::vector<ColoredDrawCommand> coloredDrawCommands_;

        // WEBGPU-20/33: textured3d (stride 20, VertexPositionTexture). Shares the same UBO layout/
        // bind group (group 0, coloredBindGroupLayout_) as colored3d; adds a second bind group
        // (group 1: sampler + texture, mirrors the SpriteBatch bind group layout exactly) for the
        // texture itself. No fog (same deliberate deferral as colored3d.wgsl -- not tracked as its
        // own WEBGPU-N task).
        struct TexturedDrawCommand
        {
            std::vector<std::uint8_t> vertexData;
            std::vector<std::uint8_t> indexData;
            bool indexed = false;
            bool index32 = false;
            std::uint32_t vertexCount = 0;
            std::uint32_t indexCount = 0;
            std::uint32_t firstIndex = 0;
            std::int32_t baseVertex = 0;
            WGPUPrimitiveTopology topology = WGPUPrimitiveTopology_TriangleList;
            std::array<float, 32> uniforms{};
            bool depthTest = false;
            bool depthWrite = false;
            int depthFunc = 3;
            // WEBGPU-41/77/78/79: baked into the pipeline object alongside depthTest/
            // depthWrite/depthFunc above (see BlendKeyParams' own comment for why this must be
            // captured per-draw-command, not read as frame-global state at render time).
            bool blend = true;
            BlendKeyParams blendParams{};
            int cullMode = 0;
            bool wireframe = false;
            float depthBias = 0.0f;
            float slopeScaleDepthBias = 0.0f;
            // Not shadow-copied like vertex/index data: a bound Texture2D's WebGPUTextureBackend
            // is owned by long-lived game/content state (unlike DrawUserPrimitives' transient
            // vertex buffers), so it is guaranteed to still be alive when this command actually
            // renders later in the same frame.
            const IWebGPUSamplable* texture = nullptr;
            int textureFilter = 0;
            int addressU = 1;
            int addressV = 1;
            int maxAnisotropy = 4;  ///< WEBGPU-82: per-slot SamplerState anisotropy
            // WEBGPU-21: stride 24 (VertexPositionColorTexture) instead of stride 20
            // (VertexPositionTexture) -- selects coloredTexturedShader_/
            // GetOrCreatePipelineColoredTextured3D() instead of texturedShader_/
            // GetOrCreatePipelineTextured3D() in RenderTexturedDraws(), same bind groups
            // (texturedPipelineLayout_) either way, just a different vertex buffer layout/shader.
            bool hasVertexColor = false;
        };
        void CreateTexturedResources();
        void DestroyTexturedResources();
        [[nodiscard]] WGPURenderPipeline GetOrCreatePipelineTextured3D(WGPUPrimitiveTopology topology,
                                                                        WGPUIndexFormat stripIndexFormat,
                                                                        bool depthTest, bool depthWrite,
                                                                        int depthFunc,
                                                       bool blend, const BlendKeyParams& blendParams,
                                                       int cullMode, bool wireframe,
                                                       float depthBias, float slopeScaleDepthBias);
        [[nodiscard]] WGPURenderPipeline GetOrCreatePipelineColoredTextured3D(WGPUPrimitiveTopology topology,
                                                                               WGPUIndexFormat stripIndexFormat,
                                                                               bool depthTest, bool depthWrite,
                                                                               int depthFunc,
                                                       bool blend, const BlendKeyParams& blendParams,
                                                       int cullMode, bool wireframe,
                                                       float depthBias, float slopeScaleDepthBias);
        void QueueTexturedDraw(const IVertexBufferBackend& vb, const IIndexBufferBackend* ib,
                               const Matrix& world, const Matrix& view, const Matrix& projection,
                               PrimitiveType primitive, int primitiveCount, const GpuDrawParams& params);
        void RenderTexturedDraws(WGPURenderPassEncoder pass);

        WGPUShaderModule texturedShader_ = nullptr;
        WGPUShaderModule coloredTexturedShader_ = nullptr;
        WGPUBindGroupLayout texturedBindGroupLayout_ = nullptr;   ///< group 1: sampler + texture
        WGPUPipelineLayout texturedPipelineLayout_ = nullptr;     ///< group 0 (UBO) + group 1 (texture); shared by textured3d and colored_textured3d
        std::unordered_map<std::uint64_t, WGPURenderPipeline> texturedPipelines_;
        std::unordered_map<std::uint64_t, WGPURenderPipeline> coloredTexturedPipelines_;
        std::vector<TexturedDrawCommand> texturedDrawCommands_;

        // Per-draw vertex/uniform/index buffers and bind groups are transient (created fresh
        // every RenderColoredDraws()/RenderTexturedDraws() call) but must not be released until
        // after the frame's command buffer is actually submitted -- releasing while the encoder
        // still references them (before wgpuQueueSubmit) would race the recorded-but-not-yet-
        // executed commands.
        std::vector<WGPUBuffer> pendingBufferReleases_;
        std::vector<WGPUBindGroup> pendingBindGroupReleases_;

        // WEBGPU-22: lit_textured3d (stride 32, VertexPositionNormalTexture). Real Blinn-Phong
        // lighting (FNA's Lighting.fxh ComputeLights(): 3 directional lights, ambient, specular,
        // emissive), ported from VulkanGraphicsBackend's lit_textured3d.{vert,frag}.glsl. This is
        // the first WebGPU 3D shader that needs a SECOND UBO (litBindGroupLayout_, group 0
        // binding 1) alongside the existing 128-byte primary uniforms (binding 0, still used for
        // MVP/diffuseColor/ambientColor/lightingEnabled/light0Dir/light0Diffuse/textureEnabled),
        // since that buffer is already fully packed. Reuses texturedBindGroupLayout_ (group 1:
        // sampler + texture) unchanged. No fog (same deliberate deferral as the other 3D shaders).
        struct LitTexturedDrawCommand
        {
            std::vector<std::uint8_t> vertexData;
            std::vector<std::uint8_t> indexData;
            bool indexed = false;
            bool index32 = false;
            std::uint32_t vertexCount = 0;
            std::uint32_t indexCount = 0;
            std::uint32_t firstIndex = 0;
            std::int32_t baseVertex = 0;
            WGPUPrimitiveTopology topology = WGPUPrimitiveTopology_TriangleList;
            std::array<float, 32> uniforms{};
            std::array<float, 68> lightUniforms{};
            bool depthTest = false;
            bool depthWrite = false;
            int depthFunc = 3;
            // WEBGPU-41/77/78/79: baked into the pipeline object alongside depthTest/
            // depthWrite/depthFunc above (see BlendKeyParams' own comment for why this must be
            // captured per-draw-command, not read as frame-global state at render time).
            bool blend = true;
            BlendKeyParams blendParams{};
            int cullMode = 0;
            bool wireframe = false;
            float depthBias = 0.0f;
            float slopeScaleDepthBias = 0.0f;
            const IWebGPUSamplable* texture = nullptr;
            int textureFilter = 0;
            int addressU = 1;
            int addressV = 1;
            int maxAnisotropy = 4;  ///< WEBGPU-82: per-slot SamplerState anisotropy
            /// Task 1105 (plan_graphics.md Phase 80): true selects the per-vertex-lit sibling
            /// pipeline/shader (XNA's real BasicEffect.PreferPerPixelLighting==false default);
            /// false keeps the existing per-pixel-lit one. Computed once at queue time from
            /// GpuDrawParams::lightingEnabled/preferPerPixelLighting -- irrelevant when lighting
            /// is disabled (both pipelines render the identical unlit result in that case).
            bool preferVertexLit = false;
        };
        void CreateLitTexturedResources();
        void DestroyLitTexturedResources();
        [[nodiscard]] WGPURenderPipeline GetOrCreatePipelineLitTextured3D(WGPUPrimitiveTopology topology,
                                                                           WGPUIndexFormat stripIndexFormat,
                                                                           bool depthTest, bool depthWrite,
                                                                           int depthFunc,
                                                       bool blend, const BlendKeyParams& blendParams,
                                                       int cullMode, bool wireframe,
                                                       float depthBias, float slopeScaleDepthBias);
        /// Task 1105: real per-vertex-lit sibling to GetOrCreatePipelineLitTextured3D above --
        /// identical Blinn-Phong math (FNA's Lighting.fxh ComputeLights()), moved into the vertex
        /// stage and passed to the fragment shader as litRGB/specularRGB varyings instead of being
        /// recomputed per fragment. Reuses litBindGroupLayout_/litPipelineLayout_/
        /// texturedBindGroupLayout_ unchanged (identical UBO/binding shape to the per-pixel-lit
        /// shader) -- only a new shader module and a new pipeline cache are needed.
        [[nodiscard]] WGPURenderPipeline GetOrCreatePipelineLitTextured3DVertexLit(WGPUPrimitiveTopology topology,
                                                                                    WGPUIndexFormat stripIndexFormat,
                                                                                    bool depthTest, bool depthWrite,
                                                                                    int depthFunc,
                                                       bool blend, const BlendKeyParams& blendParams,
                                                       int cullMode, bool wireframe,
                                                       float depthBias, float slopeScaleDepthBias);
        void QueueLitTexturedDraw(const IVertexBufferBackend& vb, const IIndexBufferBackend* ib,
                                  const Matrix& world, const Matrix& view, const Matrix& projection,
                                  PrimitiveType primitive, int primitiveCount, const GpuDrawParams& params);
        void RenderLitTexturedDraws(WGPURenderPassEncoder pass);

        WGPUShaderModule litTexturedShader_ = nullptr;
        WGPUBindGroupLayout litBindGroupLayout_ = nullptr;   ///< group 0: primary UBO (binding 0) + LitLightParams UBO (binding 1)
        WGPUPipelineLayout litPipelineLayout_ = nullptr;     ///< group 0 (lit UBOs) + group 1 (texture, texturedBindGroupLayout_ reused)
        std::unordered_map<std::uint64_t, WGPURenderPipeline> litTexturedPipelines_;
        std::vector<LitTexturedDrawCommand> litTexturedDrawCommands_;
        /// Task 1105: real per-vertex-lit sibling shader module + its own pipeline cache, keyed
        /// identically to litTexturedPipelines_ above. Shares litBindGroupLayout_/litPipelineLayout_
        /// with the per-pixel-lit shader (same UBO/binding shape).
        WGPUShaderModule litTexturedVertexLitShader_ = nullptr;
        std::unordered_map<std::uint64_t, WGPURenderPipeline> litTexturedVertexLitPipelines_;

        // WEBGPU-23/34/72: alpha_test3d.wgsl -- AlphaTestEffect's per-pixel alpha discard, matching
        // VulkanGraphicsBackend's alpha_test3d.{vert,frag}.glsl / alpha_test_colored3d.vert.glsl.
        // AlphaTestEffect has no lighting, so its 32-float uniform block repurposes the
        // ambient/light0 slots (Phase 57's [20..23]) for {alphaRef, alphaTolerance, passWeight,
        // failWeight} instead -- same 128-byte size/shape as colored3d's UBO, so this reuses
        // coloredBindGroupLayout_ (group 0) and texturedBindGroupLayout_ (group 1) unchanged, no
        // new bind group layout needed. Supports strides 20 (VertexPositionTexture), 24
        // (VertexPositionColorTexture, vertex-colour tint), and 32 (VertexPositionNormalTexture,
        // normal attribute simply unread) via one shared "uncolored" shader module for strides
        // 20/32 (only the vertex buffer layout differs) plus a separate "colored" shader module
        // for stride 24. No fog (same deliberate deferral as the other 3D shaders).
        struct AlphaTestDrawCommand
        {
            std::vector<std::uint8_t> vertexData;
            std::vector<std::uint8_t> indexData;
            bool indexed = false;
            bool index32 = false;
            std::uint32_t vertexCount = 0;
            std::uint32_t indexCount = 0;
            std::uint32_t firstIndex = 0;
            std::int32_t baseVertex = 0;
            WGPUPrimitiveTopology topology = WGPUPrimitiveTopology_TriangleList;
            std::array<float, 32> uniforms{};
            bool depthTest = false;
            bool depthWrite = false;
            int depthFunc = 3;
            // WEBGPU-41/77/78/79: baked into the pipeline object alongside depthTest/
            // depthWrite/depthFunc above (see BlendKeyParams' own comment for why this must be
            // captured per-draw-command, not read as frame-global state at render time).
            bool blend = true;
            BlendKeyParams blendParams{};
            int cullMode = 0;
            bool wireframe = false;
            float depthBias = 0.0f;
            float slopeScaleDepthBias = 0.0f;
            const IWebGPUSamplable* texture = nullptr;
            int textureFilter = 0;
            int addressU = 1;
            int addressV = 1;
            int maxAnisotropy = 4;  ///< WEBGPU-82: per-slot SamplerState anisotropy
            std::size_t stride = 20;   ///< 20, 24, or 32 -- selects vertex layout + shader module
            bool hasVertexColor = false;
        };
        void CreateAlphaTestResources();
        void DestroyAlphaTestResources();
        [[nodiscard]] WGPURenderPipeline GetOrCreatePipelineAlphaTest3D(std::size_t stride,
                                                                        WGPUPrimitiveTopology topology,
                                                                        WGPUIndexFormat stripIndexFormat,
                                                                        bool depthTest, bool depthWrite,
                                                                        int depthFunc,
                                                       bool blend, const BlendKeyParams& blendParams,
                                                       int cullMode, bool wireframe,
                                                       float depthBias, float slopeScaleDepthBias);
        void QueueAlphaTestDraw(const IVertexBufferBackend& vb, const IIndexBufferBackend* ib,
                                const Matrix& world, const Matrix& view, const Matrix& projection,
                                PrimitiveType primitive, int primitiveCount, const GpuDrawParams& params);
        void RenderAlphaTestDraws(WGPURenderPassEncoder pass);

        WGPUShaderModule alphaTestShader_ = nullptr;          ///< strides 20/32 (no vertex colour)
        WGPUShaderModule alphaTestColoredShader_ = nullptr;   ///< stride 24 (vertex colour tint)
        std::unordered_map<std::uint64_t, WGPURenderPipeline> alphaTestPipelines_;
        std::unordered_map<std::uint64_t, WGPURenderPipeline> alphaTestColoredPipelines_;
        std::vector<AlphaTestDrawCommand> alphaTestDrawCommands_;

        // WEBGPU-24: dual_texture3d.wgsl -- DualTextureEffect (two texture layers sampled at the
        // same UV, `tex1.rgb*=2.0; result=tex1*tex2*tint`, matching
        // VulkanGraphicsBackend's dual_texture3d.{vert,frag}.glsl /
        // dual_texture_colored3d.vert.glsl). Genuinely new bind-group shape (unlike alpha_test3d,
        // which reused textured3d's layouts unchanged): group 1 needs THREE bindings (one shared
        // sampler + two textures), so this is the first WebGPU 3D shader family with its own
        // dedicated dualTextureBindGroupLayout_/dualTexturePipelineLayout_ -- group 0 (the primary
        // UBO) is still coloredBindGroupLayout_, reused unchanged (DualTextureEffect has no
        // lighting and no alpha test, so FillExtUniforms()'s existing layout already covers it).
        // No fog (same deliberate deferral as the other 3D shaders).
        struct DualTextureDrawCommand
        {
            std::vector<std::uint8_t> vertexData;
            std::vector<std::uint8_t> indexData;
            bool indexed = false;
            bool index32 = false;
            std::uint32_t vertexCount = 0;
            std::uint32_t indexCount = 0;
            std::uint32_t firstIndex = 0;
            std::int32_t baseVertex = 0;
            WGPUPrimitiveTopology topology = WGPUPrimitiveTopology_TriangleList;
            std::array<float, 32> uniforms{};
            bool depthTest = false;
            bool depthWrite = false;
            int depthFunc = 3;
            // WEBGPU-41/77/78/79: baked into the pipeline object alongside depthTest/
            // depthWrite/depthFunc above (see BlendKeyParams' own comment for why this must be
            // captured per-draw-command, not read as frame-global state at render time).
            bool blend = true;
            BlendKeyParams blendParams{};
            int cullMode = 0;
            bool wireframe = false;
            float depthBias = 0.0f;
            float slopeScaleDepthBias = 0.0f;
            const IWebGPUSamplable* texture0 = nullptr;
            const IWebGPUSamplable* texture1 = nullptr;
            int textureFilter = 0;
            int addressU = 1;
            int addressV = 1;
            int maxAnisotropy = 4;  ///< WEBGPU-82: per-slot SamplerState anisotropy
            bool hasVertexColor = false;   ///< stride 24 vs stride 20
        };
        void CreateDualTextureResources();
        void DestroyDualTextureResources();
        [[nodiscard]] WGPURenderPipeline GetOrCreatePipelineDualTexture3D(std::size_t stride,
                                                                          WGPUPrimitiveTopology topology,
                                                                          WGPUIndexFormat stripIndexFormat,
                                                                          bool depthTest, bool depthWrite,
                                                                          int depthFunc,
                                                       bool blend, const BlendKeyParams& blendParams,
                                                       int cullMode, bool wireframe,
                                                       float depthBias, float slopeScaleDepthBias);
        void QueueDualTextureDraw(const IVertexBufferBackend& vb, const IIndexBufferBackend* ib,
                                  const Matrix& world, const Matrix& view, const Matrix& projection,
                                  PrimitiveType primitive, int primitiveCount, const GpuDrawParams& params);
        void RenderDualTextureDraws(WGPURenderPassEncoder pass);

        WGPUShaderModule dualTextureShader_ = nullptr;          ///< stride 20 (no vertex colour)
        WGPUShaderModule dualTextureColoredShader_ = nullptr;   ///< stride 24 (vertex colour tint)
        WGPUBindGroupLayout dualTextureBindGroupLayout_ = nullptr;  ///< group 1: sampler + texture0 + texture1
        WGPUPipelineLayout dualTexturePipelineLayout_ = nullptr;    ///< group 0 (UBO, coloredBindGroupLayout_) + group 1
        std::unordered_map<std::uint64_t, WGPURenderPipeline> dualTexturePipelines_;
        std::unordered_map<std::uint64_t, WGPURenderPipeline> dualTextureColoredPipelines_;
        std::vector<DualTextureDrawCommand> dualTextureDrawCommands_;

        // WEBGPU-25/36/74: env_map3d.wgsl -- EnvironmentMapEffect's cube-map reflection shader,
        // ported from VulkanGraphicsBackend's env_map3d.{vert,frag}.glsl (cross-checked against
        // EasyGLGraphicsBackend::EnsureEnvMapped3DProgram()'s identical GLSL formula before
        // porting -- both already agree field-for-field). Stride 32
        // (VertexPositionNormalTexture, the same vertex layout as lit_textured3d.wgsl). Group 0
        // binding 0 is a small Transform UBO (mvp+world, 128 bytes -- WebGPU has no push constants,
        // so this stands in for Vulkan's own 128-byte push-constant range); binding 1 is the larger
        // EnvMapParams UBO (240 bytes: eye position, diffuse, emissive+envMapAmount, all 3
        // directional lights, envMapSpecular+fresnelFactor/fresnelEnabled, fog, and a
        // CPU-precomputed 3x3 normal matrix packed as 3 vec4f columns -- WGSL has no inverse(),
        // the same reason CreateLitTexturedResources()'s own LitLightParams UBO precomputes one).
        // Group 1 is a genuinely new 3-binding shape (mirrors dualTextureBindGroupLayout_'s own
        // 3-binding group 1, swapping the second texture_2d for a texture_cube): one shared
        // sampler + a 2D base texture + a texture_cube<f32> reflection map. Both textures fall back
        // to a 1x1 default (white 2D / white cube) when EnvironmentMapEffect leaves that map
        // unbound, matching EnsurePbrDefaultTextures()'s own "map absent" convention. No fog
        // deferral here -- unlike every other WebGPU 3D shader, EnvironmentMapEffect's own fog
        // support already exists in the reference GLSL this was ported from, so it is wired in.
        struct EnvMapDrawCommand
        {
            std::vector<std::uint8_t> vertexData;
            std::vector<std::uint8_t> indexData;
            bool indexed = false;
            bool index32 = false;
            std::uint32_t vertexCount = 0;
            std::uint32_t indexCount = 0;
            std::uint32_t firstIndex = 0;
            std::int32_t baseVertex = 0;
            WGPUPrimitiveTopology topology = WGPUPrimitiveTopology_TriangleList;
            std::array<float, 32> transformUniforms{};  ///< group 0 binding 0: mvp (16) + world (16)
            std::array<float, 60> envMapUniforms{};     ///< group 0 binding 1: 15 vec4f (240 bytes)
            bool depthTest = false;
            bool depthWrite = false;
            int depthFunc = 3;
            // WEBGPU-41/77/78/79: baked into the pipeline object alongside depthTest/
            // depthWrite/depthFunc above (see BlendKeyParams' own comment for why this must be
            // captured per-draw-command, not read as frame-global state at render time).
            bool blend = true;
            BlendKeyParams blendParams{};
            int cullMode = 0;
            bool wireframe = false;
            float depthBias = 0.0f;
            float slopeScaleDepthBias = 0.0f;
            /// EnvironmentMapEffect::Texture -- may legitimately be null (falls back to a 1x1
            /// white texture at render time), unlike every other stride-32+ effect family, which
            /// requires a non-null texture0 at dispatch time.
            const IWebGPUSamplable* texture = nullptr;
            /// EnvironmentMapEffect::EnvironmentMap -- may legitimately be null (falls back to a
            /// 1x1 white cube map at render time, matching VulkanGraphicsBackend's own
            /// defaultWhiteCubeView_ fallback). WEBGPU-114: IWebGPUCubeSamplable rather than the
            /// concrete WebGPUTextureCubeBackend, so a WebGPURenderTargetCubeBackend (a distinct
            /// concrete class) bound as EnvironmentMapEffect.EnvironmentMap resolves correctly too
            /// -- see IWebGPUCubeSamplable's own doc comment.
            const IWebGPUCubeSamplable* envMap = nullptr;
            int textureFilter = 0;
            int addressU = 1;
            int addressV = 1;
            int maxAnisotropy = 4;  ///< WEBGPU-82: per-slot SamplerState anisotropy
        };
        void CreateEnvMapResources();
        void DestroyEnvMapResources();
        /// Lazily creates the 1x1 white 2D texture / 1x1 white cube map used whenever
        /// EnvironmentMapEffect::Texture/EnvironmentMap is left unbound. Mirrors
        /// EnsurePbrDefaultTextures()'s own lazy-at-first-draw pattern (not tied to surface
        /// format/sample count, so it does not need to be recreated by
        /// CreateEnvMapResources()/ClearAllPipelineCaches()).
        void EnsureEnvMapDefaultTextures();
        [[nodiscard]] WGPURenderPipeline GetOrCreatePipelineEnvMap3D(WGPUPrimitiveTopology topology,
                                                                      WGPUIndexFormat stripIndexFormat,
                                                                      bool depthTest, bool depthWrite,
                                                                      int depthFunc,
                                                   bool blend, const BlendKeyParams& blendParams,
                                                   int cullMode, bool wireframe,
                                                   float depthBias, float slopeScaleDepthBias);
        void QueueEnvMapDraw(const IVertexBufferBackend& vb, const IIndexBufferBackend* ib,
                             const Matrix& world, const Matrix& view, const Matrix& projection,
                             PrimitiveType primitive, int primitiveCount, const GpuDrawParams& params);
        void RenderEnvMapDraws(WGPURenderPassEncoder pass);

        WGPUShaderModule envMapShader_ = nullptr;
        WGPUBindGroupLayout envMapBindGroupLayout_ = nullptr;         ///< group 0: Transform UBO (binding 0) + EnvMapParams UBO (binding 1)
        WGPUBindGroupLayout envMapTextureBindGroupLayout_ = nullptr; ///< group 1: sampler + texture_2d + texture_cube
        WGPUPipelineLayout envMapPipelineLayout_ = nullptr;
        std::unordered_map<std::uint64_t, WGPURenderPipeline> envMapPipelines_;
        std::vector<EnvMapDrawCommand> envMapDrawCommands_;
        std::unique_ptr<WebGPUTextureBackend> envMapDefaultWhiteTexture_;
        std::unique_ptr<WebGPUTextureCubeBackend> envMapDefaultWhiteCube_;

        // WEBGPU-27/38/68: instanced3d.wgsl -- a genuinely SECOND vertex buffer binding
        // (WGPUVertexStepMode_Instance) carrying a per-instance mat4 world transform, ported from
        // VulkanGraphicsBackend's instanced3d.{vert,frag}.glsl / GetOrCreatePipelineInstanced3D().
        // Unlike every bind-group-shaped "genuinely new" family above (dual-texture/env-map), a
        // second vertex buffer needs NO new WGPUBindGroupLayout at all -- vertex buffers are set
        // via wgpuRenderPassEncoderSetVertexBuffer(), completely separate from bind groups -- so
        // this reuses coloredBindGroupLayout_/coloredPipelineLayout_ UNCHANGED (group 0 only, the
        // same 128-byte Uniforms shape as colored3d.wgsl: mvp replaced by vp -- world comes from
        // the per-instance stream, mirroring FillInstancedPushConst()'s own deliberate choice to
        // ignore the caller's own World matrix entirely rather than combine it). The flat
        // diffuseColor-only fragment output also matches instanced3d.frag.glsl exactly (no
        // lighting/texture -- this shader family has neither).
        struct InstancedDrawCommand
        {
            std::vector<std::uint8_t> vertexData;
            std::vector<std::uint8_t> indexData;
            std::vector<std::uint8_t> instVbData;
            bool indexed = false;
            bool index32 = false;
            std::uint32_t vertexCount = 0;
            std::uint32_t indexCount = 0;
            std::uint32_t firstIndex = 0;
            std::int32_t baseVertex = 0;
            std::uint32_t instanceCount = 1;
            /// Real per-vertex/per-instance buffer strides (not hardcoded), so the pipeline's own
            /// WGPUVertexBufferLayout::arrayStride genuinely matches each buffer's declared
            /// stride -- a real, deliberate improvement over VulkanGraphicsBackend's own instanced3d
            /// pipeline, which hardcodes a 64-byte (sizeof(mat4)) instance binding stride
            /// regardless of the instance vertex buffer's own declared stride (latent, harmless
            /// only because every existing caller happens to always use a plain mat4-only instance
            /// declaration).
            std::size_t pvStride = 16;
            std::size_t instVbStride = 64;
            WGPUPrimitiveTopology topology = WGPUPrimitiveTopology_TriangleList;
            std::array<float, 32> uniforms{};
            bool depthTest = false;
            bool depthWrite = false;
            int depthFunc = 3;
            bool blend = true;
            BlendKeyParams blendParams{};
            int cullMode = 0;
            bool wireframe = false;
            float depthBias = 0.0f;
            float slopeScaleDepthBias = 0.0f;
        };
        void CreateInstancedResources();
        void DestroyInstancedResources();
        [[nodiscard]] WGPURenderPipeline GetOrCreatePipelineInstanced3D(std::size_t pvStride, std::size_t instVbStride,
                                                                         WGPUPrimitiveTopology topology,
                                                                         WGPUIndexFormat stripIndexFormat,
                                                                         bool depthTest, bool depthWrite, int depthFunc,
                                                    bool blend, const BlendKeyParams& blendParams,
                                                    int cullMode, bool wireframe,
                                                    float depthBias, float slopeScaleDepthBias);
        void RenderInstancedDraws(WGPURenderPassEncoder pass);

        WGPUShaderModule instancedShader_ = nullptr;
        std::unordered_map<std::uint64_t, WGPURenderPipeline> instancedPipelines_;
        std::vector<InstancedDrawCommand> instancedDrawCommands_;

        // pbr3d.wgsl -- PbrEffect's real glTF 2.0 metallic-roughness BRDF (GGX distribution +
        // Smith-Schlick-GGX visibility + Schlick Fresnel), ported from
        // EasyGLGraphicsBackend::EnsurePbrProgram()'s GLSL shader. UNSKINNED only (stride 48,
        // VertexPositionNormalTangentTexture) -- this backend has no skinning shader variant at
        // all yet (see needsUnsupportedEffect's own skinned gate in DrawPrimitivesEx()), so
        // SkinnedPbrEffect (stride 68) is a separate, pre-existing, out-of-scope gap and keeps
        // falling back exactly as it did before. Genuinely new bind-group shapes on both sides:
        // group 0 needs a THIRD uniform buffer (PbrFactors: metallic/roughness factors -- the
        // existing 128-byte Uniforms and 272-byte LitLightParams blocks are both already fully
        // packed and are reused verbatim via the existing FillExtUniforms()/
        // FillLitLightUniforms() helpers), and group 1 needs FIVE textures (base color, normal,
        // metallic-roughness, emissive, occlusion) behind one shared sampler, each falling back to
        // a 1x1 default texture (matching EasyGLGraphicsBackend's own
        // EnsureDefaultFlatNormalTexture()/EnsureDefaultWhiteTexture() "map absent" convention)
        // when PbrEffect leaves that map unbound. Fog and alpha-test are deliberately NOT wired
        // into this shader: every other WebGPU 3D shader already defers fog identically (see
        // CreateLitTexturedResources()'s own comment), and PbrEffect::FillGpuDrawParams() never
        // touches GpuDrawParams::alphaTest at all (stays the default always-pass value), so
        // embedding that branch here would be permanently dead code.
        struct PbrDrawCommand
        {
            std::vector<std::uint8_t> vertexData;
            std::vector<std::uint8_t> indexData;
            bool indexed = false;
            bool index32 = false;
            std::uint32_t vertexCount = 0;
            std::uint32_t indexCount = 0;
            std::uint32_t firstIndex = 0;
            std::int32_t baseVertex = 0;
            WGPUPrimitiveTopology topology = WGPUPrimitiveTopology_TriangleList;
            std::array<float, 32> uniforms{};
            std::array<float, 68> lightUniforms{};
            std::array<float, 4> pbrFactors{};
            bool depthTest = false;
            bool depthWrite = false;
            int depthFunc = 3;
            // WEBGPU-41/77/78/79: baked into the pipeline object alongside depthTest/
            // depthWrite/depthFunc above (see BlendKeyParams' own comment for why this must be
            // captured per-draw-command, not read as frame-global state at render time).
            bool blend = true;
            BlendKeyParams blendParams{};
            int cullMode = 0;
            bool wireframe = false;
            float depthBias = 0.0f;
            float slopeScaleDepthBias = 0.0f;
            const IWebGPUSamplable* baseColorTexture = nullptr;
            const IWebGPUSamplable* normalMap = nullptr;
            const IWebGPUSamplable* metallicRoughnessMap = nullptr;
            const IWebGPUSamplable* emissiveMap = nullptr;
            const IWebGPUSamplable* occlusionMap = nullptr;
            int textureFilter = 0;
            int addressU = 1;
            int addressV = 1;
            int maxAnisotropy = 4;  ///< WEBGPU-82: per-slot SamplerState anisotropy
        };
        void CreatePbrResources();
        void DestroyPbrResources();
        /// Lazily creates the 1x1 default white / flat-normal fallback textures used whenever
        /// PbrEffect leaves an optional map (normal/metallic-roughness/emissive/occlusion)
        /// unbound. Idempotent; safe to call from every QueuePbrDraw().
        void EnsurePbrDefaultTextures();
        [[nodiscard]] WGPURenderPipeline GetOrCreatePipelinePbr3D(WGPUPrimitiveTopology topology,
                                                                    WGPUIndexFormat stripIndexFormat,
                                                                    bool depthTest, bool depthWrite,
                                                                    int depthFunc,
                                                       bool blend, const BlendKeyParams& blendParams,
                                                       int cullMode, bool wireframe,
                                                       float depthBias, float slopeScaleDepthBias);
        void QueuePbrDraw(const IVertexBufferBackend& vb, const IIndexBufferBackend* ib,
                          const Matrix& world, const Matrix& view, const Matrix& projection,
                          PrimitiveType primitive, int primitiveCount, const GpuDrawParams& params);
        void RenderPbrDraws(WGPURenderPassEncoder pass);

        WGPUShaderModule pbrShader_ = nullptr;
        WGPUBindGroupLayout pbrBindGroupLayout0_ = nullptr;  ///< group 0: Uniforms + LitLightParams + PbrFactors UBOs
        WGPUBindGroupLayout pbrBindGroupLayout1_ = nullptr;  ///< group 1: sampler + 5 textures
        WGPUPipelineLayout pbrPipelineLayout_ = nullptr;
        std::unordered_map<std::uint64_t, WGPURenderPipeline> pbrPipelines_;
        std::vector<PbrDrawCommand> pbrDrawCommands_;
        std::unique_ptr<WebGPUTextureBackend> pbrDefaultWhiteTexture_;
        std::unique_ptr<WebGPUTextureBackend> pbrDefaultFlatNormalTexture_;

        // plan_cnj.md Phase 14J: closing this backend's pre-existing "no skinning shader at all"
        // gap for SkinnedEffect -- both PreferPerPixelLighting lighting variants (matching every
        // other backend's own established "two skinned shader variants" convention) plus
        // SkinnedEffect.VertexColorEnabled (stride-56 vertex colour on skinned meshes). Ported
        // from EasyGLGraphicsBackend::EnsureSkinnedProgram()/EnsureSkinnedVertexLitProgram()'s GLSL
        // shaders line-for-line. SkinnedEffect::FillGpuDrawParams pre-folds
        // (EmissiveColor + AmbientLightColor*DiffuseColor)*Alpha into emissiveColor, so every
        // variant consumes it with FNA's `lightSum*diffuseColor + emissiveColor` formula; the
        // already-folded emissive term is not re-multiplied by DiffuseColor or Alpha. The vertex-colour
        // gate multiplies the FINAL combined diffuse+specular output (applied AFTER the specular
        // add, not just to the diffuse term -- a real ordering bug was once found and fixed in the
        // EasyGL reference, replicated here to avoid the same trap). Bone-palette skinning matches
        // Task 895's convention (weightsPerVertex 1/2/4 -- only the first N weight/index pairs are
        // summed). Since a WebGPU pipeline must supply every vertex-shader-referenced attribute
        // location from its own vertex buffer layout (unlike GL's "simply leave an attribute
        // unbound" precedent for stride 52 in EasyGLGraphicsBackend::ApplyLayout), the stride-52
        // and stride-56 cases each get their own shader module pair (mirrors this backend's own
        // existing texturedShader_/coloredTexturedShader_ precedent for the analogous stride-20/24
        // split), all four sharing skinnedBindGroupLayout_/skinnedPipelineLayout_ (identical UBO
        // shape). No fog/alpha-test wired in, matching every other WebGPU 3D shader's own
        // deliberate deferral (SkinnedEffect never sets GpuDrawParams::alphaTest away from its
        // always-pass default).
        struct SkinnedDrawCommand
        {
            std::vector<std::uint8_t> vertexData;
            std::vector<std::uint8_t> indexData;
            bool indexed = false;
            bool index32 = false;
            std::uint32_t vertexCount = 0;
            std::uint32_t indexCount = 0;
            std::uint32_t firstIndex = 0;
            std::int32_t baseVertex = 0;
            WGPUPrimitiveTopology topology = WGPUPrimitiveTopology_TriangleList;
            std::array<float, 32> uniforms{};
            std::array<float, 68> lightUniforms{};
            /// SkinningParams UBO: [0] = WeightsPerVertex (Task 895's 1/2/4 convention, stored as a
            /// float in the x component of a padding vec4), [4 .. 4+72*16) = 72 column-major bone
            /// matrices (FillSkinningParams()).
            std::array<float, 4 + 72 * 16> skinningParams{};
            bool depthTest = false;
            bool depthWrite = false;
            int depthFunc = 3;
            // WEBGPU-41/77/78/79: baked into the pipeline object alongside depthTest/
            // depthWrite/depthFunc above (see BlendKeyParams' own comment for why this must be
            // captured per-draw-command, not read as frame-global state at render time).
            bool blend = true;
            BlendKeyParams blendParams{};
            int cullMode = 0;
            bool wireframe = false;
            float depthBias = 0.0f;
            float slopeScaleDepthBias = 0.0f;
            const IWebGPUSamplable* texture = nullptr;
            int textureFilter = 0;
            int addressU = 1;
            int addressV = 1;
            int maxAnisotropy = 4;  ///< WEBGPU-82: per-slot SamplerState anisotropy
            std::size_t stride = 52;   ///< 52 (no vertex colour) or 56 (vertex colour appended)
            bool preferVertexLit = false;
        };
        void CreateSkinnedResources();
        void DestroySkinnedResources();
        [[nodiscard]] WGPURenderPipeline GetOrCreatePipelineSkinned3D(std::size_t stride, bool preferVertexLit,
                                                                        WGPUPrimitiveTopology topology,
                                                                        WGPUIndexFormat stripIndexFormat,
                                                                        bool depthTest, bool depthWrite,
                                                                        int depthFunc,
                                                       bool blend, const BlendKeyParams& blendParams,
                                                       int cullMode, bool wireframe,
                                                       float depthBias, float slopeScaleDepthBias);
        void QueueSkinnedDraw(const IVertexBufferBackend& vb, const IIndexBufferBackend* ib,
                              const Matrix& world, const Matrix& view, const Matrix& projection,
                              PrimitiveType primitive, int primitiveCount, const GpuDrawParams& params);
        void RenderSkinnedDraws(WGPURenderPassEncoder pass);

        WGPUShaderModule skinnedShader_ = nullptr;               ///< stride 52, per-pixel-lit
        WGPUShaderModule skinnedColorShader_ = nullptr;          ///< stride 56, per-pixel-lit
        WGPUShaderModule skinnedVertexLitShader_ = nullptr;      ///< stride 52, per-vertex-lit
        WGPUShaderModule skinnedVertexLitColorShader_ = nullptr; ///< stride 56, per-vertex-lit
        WGPUBindGroupLayout skinnedBindGroupLayout_ = nullptr;   ///< group 0: Uniforms + LitLightParams + SkinningParams UBOs
        WGPUPipelineLayout skinnedPipelineLayout_ = nullptr;     ///< group 0 (above) + group 1 (texture, texturedBindGroupLayout_ reused)
        std::unordered_map<std::uint64_t, WGPURenderPipeline> skinnedPipelines_;
        std::unordered_map<std::uint64_t, WGPURenderPipeline> skinnedColorPipelines_;
        std::unordered_map<std::uint64_t, WGPURenderPipeline> skinnedVertexLitPipelines_;
        std::unordered_map<std::uint64_t, WGPURenderPipeline> skinnedVertexLitColorPipelines_;
        std::vector<SkinnedDrawCommand> skinnedDrawCommands_;

        // SkinnedPbrEffect (PBR + skinning combo, stride 68, VertexPositionNormalTangentTextureSkinned).
        // Bone-palette skinning (the same vertex transform as the SkinnedEffect shaders above,
        // extended to also skin Tangent) feeding pbr3d.wgsl's own fragment BRDF unchanged, matching
        // EasyGLGraphicsBackend::EnsurePbrSkinnedProgram()'s combination exactly -- including its
        // own (different from unskinned PbrEffect) normal/tangent transform, which uses the raw
        // World rotation directly (mat3(uWorld)*(skinMat3*normal)) rather than the precomputed
        // inverse-transpose normal matrix pbr3d.wgsl's own unskinned vertex shader uses. No vertex
        // colour (SkinnedPbrEffect has no VertexColorEnabled, matching the EasyGL reference).
        struct SkinnedPbrDrawCommand
        {
            std::vector<std::uint8_t> vertexData;
            std::vector<std::uint8_t> indexData;
            bool indexed = false;
            bool index32 = false;
            std::uint32_t vertexCount = 0;
            std::uint32_t indexCount = 0;
            std::uint32_t firstIndex = 0;
            std::int32_t baseVertex = 0;
            WGPUPrimitiveTopology topology = WGPUPrimitiveTopology_TriangleList;
            std::array<float, 32> uniforms{};
            std::array<float, 68> lightUniforms{};
            std::array<float, 4> pbrFactors{};
            std::array<float, 4 + 72 * 16> skinningParams{};
            bool depthTest = false;
            bool depthWrite = false;
            int depthFunc = 3;
            // WEBGPU-41/77/78/79: baked into the pipeline object alongside depthTest/
            // depthWrite/depthFunc above (see BlendKeyParams' own comment for why this must be
            // captured per-draw-command, not read as frame-global state at render time).
            bool blend = true;
            BlendKeyParams blendParams{};
            int cullMode = 0;
            bool wireframe = false;
            float depthBias = 0.0f;
            float slopeScaleDepthBias = 0.0f;
            const IWebGPUSamplable* baseColorTexture = nullptr;
            const IWebGPUSamplable* normalMap = nullptr;
            const IWebGPUSamplable* metallicRoughnessMap = nullptr;
            const IWebGPUSamplable* emissiveMap = nullptr;
            const IWebGPUSamplable* occlusionMap = nullptr;
            int textureFilter = 0;
            int addressU = 1;
            int addressV = 1;
            int maxAnisotropy = 4;  ///< WEBGPU-82: per-slot SamplerState anisotropy
        };
        void CreateSkinnedPbrResources();
        void DestroySkinnedPbrResources();
        [[nodiscard]] WGPURenderPipeline GetOrCreatePipelineSkinnedPbr3D(WGPUPrimitiveTopology topology,
                                                                           WGPUIndexFormat stripIndexFormat,
                                                                           bool depthTest, bool depthWrite,
                                                                           int depthFunc,
                                                       bool blend, const BlendKeyParams& blendParams,
                                                       int cullMode, bool wireframe,
                                                       float depthBias, float slopeScaleDepthBias);
        void QueueSkinnedPbrDraw(const IVertexBufferBackend& vb, const IIndexBufferBackend* ib,
                                 const Matrix& world, const Matrix& view, const Matrix& projection,
                                 PrimitiveType primitive, int primitiveCount, const GpuDrawParams& params);
        void RenderSkinnedPbrDraws(WGPURenderPassEncoder pass);

        WGPUShaderModule skinnedPbrShader_ = nullptr;
        WGPUBindGroupLayout skinnedPbrBindGroupLayout0_ = nullptr;  ///< group 0: Uniforms + LitLightParams + PbrFactors + SkinningParams UBOs
        WGPUPipelineLayout skinnedPbrPipelineLayout_ = nullptr;     ///< group 0 (above) + group 1 (pbrBindGroupLayout1_ reused)
        std::unordered_map<std::uint64_t, WGPURenderPipeline> skinnedPbrPipelines_;
        std::vector<SkinnedPbrDrawCommand> skinnedPbrDrawCommands_;
    };
}
