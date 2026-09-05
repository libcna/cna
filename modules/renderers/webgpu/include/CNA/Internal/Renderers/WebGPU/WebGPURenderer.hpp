#pragma once

#include "CNA/Internal/Renderers/Common/IGraphicsRenderer.hpp"
#include "CNA/Internal/Renderers/Common/PlatformRendererSurfaceState.hpp"
#include "CNA/Internal/Graphics/VertexDeclarationFidelity.hpp"
#include "CNA/Internal/Graphics/StockVertexSemantics.hpp"

#if __has_include(<webgpu/webgpu.h>)
#include <webgpu/webgpu.h>
#elif __has_include(<webgpu-headers/webgpu.h>)
#include <webgpu-headers/webgpu.h>
#elif __has_include(<webgpu.h>)
#include <webgpu.h>
#else
#error "CNA WebGPU renderer requires webgpu.h from wgpu-native"
#endif

#include <array>
#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace CNA::Internal::Renderers::WebGPU
{
    class WebGPURenderer;
    class WebGPURenderTargetRenderer;

    /// Small renderer-local interface (mirrors VulkanRenderer's IVulkanSamplable) shared by
    /// WebGPUTextureRenderer and WebGPURenderTargetRenderer so every Queue*Draw()'s stored "sampled
    /// texture" pointer can resolve either concrete renderer's WGPUTextureView safely.
    /// WebGPURenderTargetRenderer cannot simply derive from WebGPUTextureRenderer to reuse its
    /// View() -- both already derive from ITextureRenderer (IRenderTargetRenderer : ITextureRenderer
    /// too), which would create an ambiguous diamond -- so this is the same "shared capability via
    /// a small interface, not shared implementation" pattern Vulkan already established. Before
    /// RenderTarget2D support existed, every GpuDrawParams::textureN this renderer ever saw was
    /// guaranteed to be a WebGPUTextureRenderer (the only ITextureRenderer-implementing class this
    /// renderer had), so every Queue*Draw() used an unchecked `static_cast<const
    /// WebGPUTextureRenderer*>` to store it -- safe only by that former guarantee. Introducing
    /// WebGPURenderTargetRenderer (a second, unrelated concrete class implementing ITextureRenderer)
    /// makes that cast a real hazard: this interface plus ResolveSamplable() (see the .cpp) turns
    /// it into a safe dynamic_cast that degrades to "treat as unbound" for any incompatible type,
    /// which is never worse than before and is now also correct for a RenderTarget2D.
    /**
     * @brief One native reference on a sampleable texture and its view (REMED-GFX-167). CNAEXT.
     *
     * This renderer records a whole frame and replays it later — a draw queued now is issued at
     * `SetRenderTarget()`'s flush or, for a backbuffer destination, not until `Present()`. The
     * public `Texture2D`/`RenderTarget2D` it sampled may be a short-lived local that is already
     * destroyed by then, and its renderer releases its `WGPUTexture`/`WGPUTextureView` in its own
     * destructor. Holding this object keeps both native handles valid for exactly as long as some
     * queued command can still bind them.
     *
     * `wgpuTextureRelease`/`wgpuTextureViewRelease` are refcount decrements, not the destructive
     * `wgpuTextureDestroy`, so an extra reference genuinely keeps the resource usable rather than
     * merely keeping a freed handle addressable. Both handles are referenced: a view alone would
     * rely on wgpu-native's internal parent reference, which this renderer does not need to assume.
     */
    class WebGPUSampledResourceEXT
    {
    public:
        /** @brief Takes one native reference on each of @p texture and @p view. */
        WebGPUSampledResourceEXT(WGPUTexture texture, WGPUTextureView view);
        /** @brief Releases the reference taken on each handle by the constructor. */
        ~WebGPUSampledResourceEXT();

        WebGPUSampledResourceEXT(const WebGPUSampledResourceEXT&) = delete;
        WebGPUSampledResourceEXT& operator=(const WebGPUSampledResourceEXT&) = delete;

        /** @brief The sampleable view this object keeps alive. */
        [[nodiscard]] WGPUTextureView View() const noexcept { return view_; }

    private:
        WGPUTexture texture_ = nullptr;
        WGPUTextureView view_ = nullptr;
    };

    /**
     * @brief The one bindable form of a sampled texture in this renderer (REMED-GFX-167). CNAEXT.
     *
     * Every deferred command stores this by value instead of a pointer to the resource's renderer
     * object. That closes both halves of the same defect at once: the view is resolved once, at
     * the public draw call, so replay never dereferences a wrapper that may since have been
     * destroyed; and @ref keepAlive holds the native handles open until the last command carrying
     * it is gone.
     *
     * Copying one costs a refcount increment; it allocates nothing (each samplable renderer builds
     * its @ref keepAlive once, at construction).
     */
    struct WebGPUSampledTextureEXT
    {
        /** @brief The already-resolved sampleable view, or null for an unbound slot. */
        WGPUTextureView view = nullptr;
        /** @brief Keeps @ref view valid while a queued command can still bind it. */
        std::shared_ptr<const WebGPUSampledResourceEXT> keepAlive;

        /** @brief Whether a texture was actually resolved (an absent optional slot yields false). */
        [[nodiscard]] explicit operator bool() const noexcept { return view != nullptr; }
        /** @brief The resolved view, for the bind-group entry that samples it. */
        [[nodiscard]] WGPUTextureView View() const noexcept { return view; }
    };

    class IWebGPUSamplable
    {
    public:
        virtual ~IWebGPUSamplable() = default;
        [[nodiscard]] virtual WGPUTextureView View() const = 0;
        /// REMED-GFX-167: the same view plus the reference that keeps it alive past this object's
        /// own destruction. Every deferred draw stores THIS, never `this` — see
        /// WebGPUSampledTextureEXT's own doc comment.
        [[nodiscard]] virtual WebGPUSampledTextureEXT Sampled() const = 0;
    };

    /// WEBGPU-114: the cube-map sibling of IWebGPUSamplable -- lets both WebGPUTextureCubeRenderer
    /// (upload-only) and WebGPURenderTargetCubeRenderer (render-into) resolve safely to their own
    /// whole-cube WGPUTextureViewDimension_Cube view wherever a GpuDrawParams::envMap needs to be
    /// sampled (EnvironmentMapEffect), via a dynamic_cast that degrades to nullptr for a null or
    /// incompatible-type input -- exact same pattern IWebGPUSamplable already established for
    /// ITextureRenderer/texture0. Before this, EnvMapDrawCommand::envMap was hardcoded to
    /// `const WebGPUTextureCubeRenderer*`, so a RenderTargetCube (a different concrete class) bound
    /// as EnvironmentMapEffect.EnvironmentMap would silently fail that cast and render with the
    /// 1x1 white cube fallback instead of the real dynamically-rendered content.
    class IWebGPUCubeSamplable
    {
    public:
        virtual ~IWebGPUCubeSamplable() = default;
        [[nodiscard]] virtual WGPUTextureView CubeView() const = 0;
        /// REMED-GFX-167: the cube counterpart of IWebGPUSamplable::Sampled() — the whole-cube view
        /// plus the reference that keeps it alive past this object's own destruction.
        [[nodiscard]] virtual WebGPUSampledTextureEXT SampledCube() const = 0;
    };

    class WebGPUTextureRenderer final : public ITextureRenderer, public IWebGPUSamplable
    {
    public:
        WebGPUTextureRenderer(WebGPURenderer& owner, const ImageData& data);
        ~WebGPUTextureRenderer() override;

        WebGPUTextureRenderer(const WebGPUTextureRenderer&) = delete;
        WebGPUTextureRenderer& operator=(const WebGPUTextureRenderer&) = delete;

        [[nodiscard]] int GetWidth() const override { return width_; }
        [[nodiscard]] int GetHeight() const override { return height_; }

        void UpdatePixels(const uint8_t* rgba, int stride) override;
        void UpdatePixelsLevel(int level, const uint8_t* rgba, int levelW, int levelH) override;
        /**
         * @brief Keeps a reference to the framework's own CPU pixel buffer for this texture.
         *
         * `WEBGPU-181`: the bytes a device recreate would re-upload. `Texture2D` owns the buffer and
         * hands it here; holding a `shared_ptr` to it costs nothing while the texture lives, because
         * it is the SAME allocation `Texture2D::cpuPixels_` already holds -- no copy is made. The
         * reference is dropped when `SetContextRecoveryEnabled(false)` says nothing will ever need
         * re-uploading, which is what lets a game that never wants recovery pay nothing for it.
         *
         * @param pixels The shared level-0 RGBA buffer, or null.
         */
        void ShareCpuPixels(std::shared_ptr<std::vector<uint8_t>> pixels) override;

        /** @brief The shared CPU pixels, or null. @return The retained buffer. */
        [[nodiscard]] const std::shared_ptr<std::vector<uint8_t>>& SharedCpuPixelsEXT() const
        {
            return cpuPixels_;
        }
        /// WEBGPU-51: real CPU readback of an arbitrary Texture2D renderer, via the same staged
        /// MAP_READ-buffer/aligned-row/async-map-and-poll technique WEBGPU-91's ReadBackbuffer()
        /// and WebGPURenderTargetRenderer::GetData() already established -- copies the WHOLE
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
        [[nodiscard]] WebGPUSampledTextureEXT Sampled() const override { return {view_, sampled_}; }

    private:
        WebGPURenderer* owner_ = nullptr;
        WGPUTexture texture_ = nullptr;
        WGPUTextureView view_ = nullptr;
        /// REMED-GFX-167: built once, at construction; handed to every deferred command that
        /// samples this texture so the native handles outlive this object when one does.
        std::shared_ptr<const WebGPUSampledResourceEXT> sampled_;
        /// WEBGPU-181: the framework's own level-0 pixels, shared rather than copied. Empty unless
        /// Texture2D handed them over AND context recovery is enabled.
        std::shared_ptr<std::vector<uint8_t>> cpuPixels_;
        int width_ = 0;
        int height_ = 0;
        int mipLevels_ = 1;
        /// WEBGPU-144: the XNA SurfaceFormat ordinal and its resolved WGPU format. A block-compressed
        /// texture uploads its BC blocks natively (no CPU decompress); `blockBytes_` is the bytes per
        /// 4x4 block (8 for BC1, 16 for BC2/3/7). `compressedLevels_` is the authoritative per-mip
        /// block store the framework's `GetData` reads back (Texture2D keeps no compressed shadow).
        int surfaceFormat_ = 0;
        WGPUTextureFormat wgpuFormat_ = WGPUTextureFormat_RGBA8Unorm;
        bool compressed_ = false;
        int blockBytes_ = 4;
        std::vector<std::vector<std::uint8_t>> compressedLevels_;
    };

    /// WEBGPU-53/54: off-screen render target backing a RenderTarget2D. Owns its own colour
    /// texture (always created in this renderer's chosen swapchain format, surfaceFormat_ -- see
    /// this class's .cpp constructor comment for why) and its own depth texture. WEBGPU-39: the
    /// depth texture is created in the EXACT format the requested DepthFormat maps to via
    /// MapDepthFormatEXT() (None -> no depth texture at all; Depth16 -> Depth16Unorm; Depth24 ->
    /// Depth24Plus; Depth24Stencil8 -> Depth24PlusStencil8), the same per-value mapping
    /// VulkanRenderer::PickDepthFormat()/EasyGL/Bgfx already do -- not a device-wide fixed format.
    /// DepthFormat()/DepthHasStencil() report what was actually allocated so the pass attachment,
    /// pipeline depthStencil state and stencil load/store ops all match. Mip regeneration is
    /// deliberately NOT implemented yet: CreateRenderTarget2D() throws for mipMap=true rather
    /// than silently under-delivering a requested mip chain.
    /// WEBGPU-58: MSAA is real, but -- unlike VulkanRenderTargetRenderer's own per-instance
    /// "requestedMultiSampleCount>0 AND owner_->sampleCount_>1" opt-in gate -- this renderer
    /// unconditionally mirrors the OWNER's CURRENT global sampleCount_ at construction time,
    /// ignoring the per-instance requested value entirely. This is
    /// necessary, not just simpler: WebGPURenderer's ~10 GetOrCreatePipeline*3D() families
    /// bake ONE single renderer-global WGPUMultisampleState.count into every pipeline object (see
    /// that class's own sampleCount_ comment) rather than selecting between an MSAA/non-MSAA
    /// pipeline variant per draw target the way Vulkan's RecordCommandBuffer() does -- so a
    /// RenderTarget2D that opted OUT of MSAA while the renderer's own sampleCount_ is > 1 would be
    /// pipeline-INCOMPATIBLE (a hard wgpu-native validation error) the moment anything drew 3D
    /// content into it, not merely sub-optimal. Reported via GetMultiSampleCount() as the real,
    /// applied value (0 when the renderer has no MSAA active), matching
    /// IRenderTargetRenderer::GetMultiSampleCount()'s own "real, device-clamped value" contract.
    /// A known, narrow, documented gap: an instance created BEFORE a later
    /// WebGPURenderer::ApplyMultiSampleCount() call changes sampleCount_ does not get
    /// retroactively resized/promoted -- exactly like VulkanRenderer::ApplyMultiSampleCount()
    /// itself does not touch any already-live VulkanRenderTargetRenderer either. This is expected
    /// to be rare in practice (games normally finish GraphicsDeviceManager.ApplyChanges() before
    /// LoadContent() creates any RenderTarget2D instances).
    class WebGPURenderTargetRenderer final : public IRenderTargetRenderer, public IWebGPUSamplable
    {
    public:
        WebGPURenderTargetRenderer(WebGPURenderer& owner, int width, int height,
                                  int depthFormat, bool preserveContents, bool mipMap = false,
                                  int multiSampleCount = 0, int surfaceFormat = 0);
        ~WebGPURenderTargetRenderer() override;

        WebGPURenderTargetRenderer(const WebGPURenderTargetRenderer&) = delete;
        WebGPURenderTargetRenderer& operator=(const WebGPURenderTargetRenderer&) = delete;

        [[nodiscard]] int GetWidth() const override { return width_; }
        [[nodiscard]] int GetHeight() const override { return height_; }

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
        [[nodiscard]] WebGPUSampledTextureEXT Sampled() const override { return {colorView_, sampled_}; }
        [[nodiscard]] WGPUTextureView DepthView() const { return depthView_; }
        /**
         * @brief WEBGPU-164: this target's mip-chain length; 1 unless it was created with mipMap.
         *
         * `RenderPendingDrawsToRenderTarget` regenerates levels 1.. from the just-rendered level 0
         * when this is greater than one, which is FNA3D's `ResolveTarget` timing and the same rule
         * `WebGPURenderTargetCubeRenderer` already follows per face.
         */
        [[nodiscard]] int LevelCount() const { return levelCount_; }
        /** @brief WEBGPU-164: the colour texture itself, for the mip-regeneration cascade. */
        [[nodiscard]] WGPUTexture ColorTexture() const { return colorTexture_; }
        [[nodiscard]] bool PreserveContents() const { return preserveContents_; }
        [[nodiscard]] int GetMultiSampleCount() const override { return appliedMultiSampleCount_; }
        /// WEBGPU-58: the actual render-pass colour attachment to draw into -- the multisampled
        /// texture's view when this instance is multisampled, otherwise the same single-sample
        /// colorView_ that View() returns (identical to before MSAA existed).
        /// WEBGPU-164: `colorLevel0View_`, not `colorView_`. A render-pass colour attachment must
        /// name exactly one mip level, and `colorView_` spans the whole chain so it can be SAMPLED.
        /// They are the same view's worth of texels on a single-level target, which is why this
        /// distinction did not exist before mipped targets did.
        [[nodiscard]] WGPUTextureView ColorAttachmentView() const { return msaaColorView_ != nullptr ? msaaColorView_ : colorLevel0View_; }
        /// WEBGPU-58: the render pass's resolveTarget -- colorView_ when multisampled (wgpu-native
        /// resolves into it automatically as the pass ends), or nullptr otherwise (no resolve
        /// needed/possible for a single-sample attachment).
        [[nodiscard]] WGPUTextureView ResolveTargetView() const { return msaaColorView_ != nullptr ? colorLevel0View_ : nullptr; }
        /// REMED-GFX-102: exact colour-attachment format captured by this target object. Sprite
        /// pipeline identity uses format compatibility, never this target object's identity.
        [[nodiscard]] WGPUTextureFormat ColorFormat() const { return colorFormat_; }
        /// WEBGPU-39: this target's depth attachment WGPU format (Undefined for DepthFormat::None) and
        /// whether it carries a stencil aspect. Pipelines drawn into this target declare exactly this.
        [[nodiscard]] WGPUTextureFormat DepthFormat() const { return depthFormat_; }
        [[nodiscard]] bool DepthHasStencil() const { return depthHasStencil_; }

    private:
        WebGPURenderer* owner_ = nullptr;
        int width_ = 0;
        int height_ = 0;
        bool preserveContents_ = false;
        WGPUTextureFormat colorFormat_ = WGPUTextureFormat_Undefined;
        WGPUTexture colorTexture_ = nullptr;
        /// The SAMPLING view: every mip level, so a shader reading this target sees the whole chain.
        WGPUTextureView colorView_ = nullptr;
        /// WEBGPU-164: the RENDER-ATTACHMENT (and MSAA resolve) view, level 0 only.
        WGPUTextureView colorLevel0View_ = nullptr;
        /// WEBGPU-164: `CalculateMipLevels(width, height)` when created with mipMap, else 1.
        int levelCount_ = 1;
        /// WEBGPU-39: mapped depth attachment format (Undefined = None, no depth texture created).
        WGPUTextureFormat depthFormat_ = WGPUTextureFormat_Depth24PlusStencil8;
        bool depthHasStencil_ = true;
        /// REMED-GFX-167: keeps colorTexture_/colorView_ — the pair a consumer draw SAMPLES —
        /// alive for any queued command still carrying it. The depth and multisample attachments
        /// below are deliberately not in it: they are only ever a render pass's own attachments,
        /// and this renderer builds and consumes a pass destination synchronously, while the
        /// target is still bound.
        std::shared_ptr<const WebGPUSampledResourceEXT> sampled_;
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
    /// renderer -- just enough for a game to build a `TextureCube` via `SetData()` and use it as
    /// `EnvironmentMapEffect.EnvironmentMap`. A WebGPU cube map is a plain `WGPUTextureDimension_2D`
    /// texture with 6 array layers (`WGPUTextureUsage_CUBE_COMPATIBLE` does not exist as a distinct
    /// flag in this API the way Vulkan/D3D need one -- the view alone selects
    /// `WGPUTextureViewDimension_Cube`), matching `EasyGLTextureCubeRenderer`'s own
    /// `CalculateCubeMipLevels()` mip-count convention. Deliberately NOT a full `TextureCube`/
    /// `RenderTargetCube` implementation (was true through WEBGPU-113): `GetData()` now has a real
    /// per-face readback (see below), and `WebGPURenderTargetCubeRenderer` (WEBGPU-114) now exists
    /// too, giving this renderer real render-into-a-cube-face support.
    class WebGPUTextureCubeRenderer final : public ITextureCubeRenderer, public IWebGPUCubeSamplable
    {
    public:
        WebGPUTextureCubeRenderer(WebGPURenderer& owner, int size, bool mipMap,
                                  int surfaceFormat = 0);
        ~WebGPUTextureCubeRenderer() override;

        WebGPUTextureCubeRenderer(const WebGPUTextureCubeRenderer&) = delete;
        WebGPUTextureCubeRenderer& operator=(const WebGPUTextureCubeRenderer&) = delete;

        /// REMED-GFX-135: true only once the whole region has been handed to
        /// `wgpuQueueWriteTexture`, which copies out of the caller's memory before returning; false
        /// for an out-of-range face/level/rectangle or a source buffer too small for the region.
        /// Those rejections used to be `std::out_of_range`/`std::invalid_argument` thrown from a
        /// renderer, which escaped the public API as raw std exceptions instead of the shared
        /// layer's own deterministic `System::NotSupportedException`.
        /**
         * @brief WEBGPU-206: uploads one cube face's mip level as raw block-compressed blocks.
         *
         * `TextureCube::SetData`'s compressed overload has already validated block alignment and
         * computed the exact payload size, so this performs the native copy and keeps the blocks as
         * the authoritative store a readback would need.
         *
         * @param face Cube face index, 0..5.
         * @param level Mip level.
         * @param x Left edge in texels; must be 0.
         * @param y Top edge in texels; must be 0.
         * @param w Width in texels; must be the level's full size.
         * @param h Height in texels; must be the level's full size.
         * @param data The blocks.
         * @param dataLength Size of @p data in bytes.
         * @return Whether the whole level was stored.
         */
        [[nodiscard]] bool SetCompressedDataEXT(int face, int level, int x, int y, int w, int h,
                                                const void* data, int dataLength) override;

        [[nodiscard]] bool SetData(int face, int level, int x, int y, int w, int h,
                                   const void* data, int dataLength) override;
        /// WEBGPU-113: real per-face CPU readback, closing this class's former no-op
        /// `ITextureCubeRenderer::GetData()` default. Same staged-copy/row-alignment/async-map
        /// technique as `WebGPUTextureRenderer::GetData()` (WEBGPU-51), with `origin.z = face`
        /// selecting the requested array layer (this renderer's cube-face convention: 0=+X, 1=-X,
        /// 2=+Y, 3=-Y, 4=+Z, 5=-Z, matching `IRenderTargetCubeRenderer`'s own documented mapping).
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
        [[nodiscard]] WebGPUSampledTextureEXT SampledCube() const override { return {cubeView_, sampled_}; }

        /**
         * @brief Keeps a reference to the framework's own CPU pixels for one cube face.
         *
         * `WEBGPU-181`, the per-face spelling of `WebGPUTextureRenderer::ShareCpuPixels()` -- same
         * purpose, same no-copy sharing, same drop when context recovery is off.
         *
         * @param face The cube face index, 0..5.
         * @param pixels That face's shared level-0 RGBA buffer, or null.
         */
        void ShareCpuPixels(int face, std::shared_ptr<std::vector<uint8_t>> pixels) override;

        /**
         * @brief One face's shared CPU pixels, or null.
         * @param face The cube face index, 0..5.
         * @return The retained buffer for that face.
         */
        [[nodiscard]] const std::shared_ptr<std::vector<uint8_t>>& SharedCpuPixelsEXT(int face) const;

    private:
        WebGPURenderer* owner_ = nullptr;
        WGPUTexture texture_ = nullptr;
        WGPUTextureView cubeView_ = nullptr;
        /// WEBGPU-181: the framework's own level-0 pixels, one entry per face, shared not copied.
        std::array<std::shared_ptr<std::vector<uint8_t>>, 6> cpuPixels_{};
        /// REMED-GFX-167: see WebGPUTextureRenderer::sampled_.
        std::shared_ptr<const WebGPUSampledResourceEXT> sampled_;
        int size_ = 0;
        int mipLevels_ = 1;
        /**
         * @brief WEBGPU-206: this cube's requested `SurfaceFormat` and what it means natively.
         *
         * A cube used to be RGBA8 whatever it was asked for, which is why `WEBGPU-163` had to stop
         * the format query promising otherwise. The pieces mirror `WebGPUTextureRenderer`'s exactly:
         * `blockBytes_` is bytes per 4x4 block (8 for BC1, 16 for BC2/3/7) and `compressedLevels_`
         * is the authoritative per-face, per-mip block store the framework's `GetData` reads back,
         * because a compressed level cannot be recovered from the GPU as blocks on every backend.
         */
        int surfaceFormat_ = 0;
        WGPUTextureFormat wgpuFormat_ = WGPUTextureFormat_RGBA8Unorm;
        bool compressed_ = false;
        int blockBytes_ = 4;
        /// Indexed `[face * mipLevels_ + level]`, so one flat vector covers all six faces.
        std::vector<std::vector<std::uint8_t>> compressedLevels_;
    };

    /// WEBGPU-114: off-screen render target backing a RenderTargetCube -- the cube-map sibling of
    /// WebGPURenderTargetRenderer (RenderTarget2D, WEBGPU-53/54). Owns ONE shared 6-array-layer
    /// colour `WGPUTexture` (exactly like WebGPUTextureCubeRenderer's own texture_) plus ONE shared
    /// `size_`x`size_` depth+stencil texture reused across all 6 faces -- safe because, mirroring
    /// VulkanRenderTargetCubeRenderer's identical "one shared depth image" choice, only ever ONE
    /// face is bound/rendered-into at a time (this renderer's whole architecture is
    /// one-render-target-current-at-once; see WebGPURenderer::currentRenderTargetCubeFace_).
    /// Each face gets its own `WGPUTextureViewDimension_2D` view (`faceViews_[face]`,
    /// `baseArrayLayer = face`) used as that face's own render-pass colour attachment, plus ONE
    /// `WGPUTextureViewDimension_Cube` view spanning all 6 layers (`cubeView_`) for sampling back as
    /// `EnvironmentMapEffect.EnvironmentMap` (through `IWebGPUCubeSamplable` -- CNA's
    /// `ITextureCubeRenderer` has no plain-2D sampling path, only the cube one, matching XNA's own
    /// RenderTargetCube/TextureCube API shape).
    ///
    /// WEBGPU-114 per-face MSAA: when the renderer's global `sampleCount_` > 1 this target renders
    /// each face into its own multisampled colour attachment and resolves it into that face's
    /// single-sample layer (`msaaColorTextures_`/`msaaColorViews_`, six separate single-layer
    /// textures because WebGPU forbids a multisampled ARRAY texture); the shared depth attachment
    /// is allocated at the same sample count. `GetMultiSampleCount()` reports the applied count,
    /// mirroring `WebGPURenderTargetRenderer`. The per-instance `multiSampleCount` argument is not
    /// read, exactly like the 2D sibling.
    ///
    /// WEBGPU-114 mipMap: when `mipMap=true` the colour texture carries a full mip chain
    /// (`levelCount_` = CalculateMipLevels(size)) and each face's chain is regenerated from its
    /// resolved level 0 after that face's render pass (`GenerateMipsForLayer`, the WEBGPU-52
    /// downsample cascade, in `colorFormat_`) -- the same on-unbind timing FNA3D's own
    /// `ResolveTarget` uses. `GetData` accepts levels 0..`levelCount_`-1. MSAA and mipMap compose:
    /// the resolve writes level 0, then the cascade downsamples it.
    class WebGPURenderTargetCubeRenderer final : public IRenderTargetCubeRenderer, public IWebGPUCubeSamplable
    {
    public:
        WebGPURenderTargetCubeRenderer(WebGPURenderer& owner, int size, int depthFormat,
                                      bool preserveContents, bool mipMap,
                                      int multiSampleCount = 0, int surfaceFormat = 0);
        ~WebGPURenderTargetCubeRenderer() override;

        WebGPURenderTargetCubeRenderer(const WebGPURenderTargetCubeRenderer&) = delete;
        WebGPURenderTargetCubeRenderer& operator=(const WebGPURenderTargetCubeRenderer&) = delete;

        [[nodiscard]] int GetSize() const override { return size_; }
        void BindAsRenderTargetFace(int face) override;
        void UnbindAsRenderTarget() override;
        /// WEBGPU-114: mirrors WebGPURenderTargetRenderer::GetMultiSampleCount() -- the count this
        /// target actually renders at, captured from the owner's global sampleCount_ at
        /// construction (0/1 when MSAA is not engaged). Per-face MSAA renders into one shared
        /// multisampled colour texture that resolves into each face layer (see MsaaColorView()).
        [[nodiscard]] int GetMultiSampleCount() const override { return appliedMultiSampleCount_; }
        /// WEBGPU-114: real per-face CPU readback, same staged-copy technique as
        /// REMED-GFX-134: refuses an out-of-range face/level/rectangle by returning false (the
        /// shared layer's deterministic System::NotSupportedException) instead of throwing a raw
        /// std exception through the public XNA surface, and flushes only when draws are actually
        /// pending -- flushing unconditionally cleared the very face this call was reading.
        /// `WebGPUTextureCubeRenderer::GetData()` (WEBGPU-113); flushes this face's own pending
        /// draws first if it is still the currently-bound render target (mirrors
        /// `WebGPURenderTargetRenderer::GetData()`'s identical on-demand-flush pattern).
        /// REMED-GFX-130: true only when the whole requested face rectangle was written; false
        /// when the readback buffer could not be mapped or the request reaches outside the level.
        /// Pre-fix that failure path `memset` the caller's destination to zero and returned as if
        /// it had succeeded -- the same fabrication the shared layer was doing, one level down.
        [[nodiscard]] bool GetData(int face, int level, int x, int y, int w, int h,
                                   void* data, int dataLength) const override;

        [[nodiscard]] WGPUTexture Texture() const { return texture_; }
        /// The whole-cube sampling view (all 6 layers) -- IWebGPUCubeSamplable's contract.
        [[nodiscard]] WGPUTextureView CubeView() const override { return cubeView_; }
        [[nodiscard]] WebGPUSampledTextureEXT SampledCube() const override { return {cubeView_, sampled_}; }
        /// This face's own single-layer 2D view (mip 0), used directly as a render-pass colour
        /// attachment at 1x, or as the resolve target for this face's multisampled attachment
        /// when MSAA is engaged. It is a view of texture_ layer @p face, so GetData()/sampling
        /// always read what a 1x render wrote or an MSAA render resolved.
        [[nodiscard]] WGPUTextureView ColorAttachmentView(int face) const { return faceViews_[static_cast<std::size_t>(face)]; }
        /// WEBGPU-114: this face's OWN multisampled colour view, non-null exactly when
        /// GetMultiSampleCount() > 1. Six per-face views (not one shared attachment) so a
        /// PreserveContents face rebound for a partial update loads its own samples, never
        /// whichever face rendered last -- the same six-per-face choice Vulkan/EasyGL settled on
        /// (REMED-GFX-141). The pass makes this the colour attachment and resolves it into
        /// ColorAttachmentView(face).
        [[nodiscard]] WGPUTextureView MsaaColorAttachmentView(int face) const { return msaaColorViews_[static_cast<std::size_t>(face)]; }
        /// The one depth+stencil view shared by all 6 faces (only one is ever bound at once).
        [[nodiscard]] WGPUTextureView DepthView() const { return depthView_; }
        /// WEBGPU-39: mapped depth attachment format (Undefined for DepthFormat::None) + stencil aspect.
        [[nodiscard]] WGPUTextureFormat DepthFormat() const { return depthFormat_; }
        [[nodiscard]] bool DepthHasStencil() const { return depthHasStencil_; }
        /// REMED-GFX-102: exact colour-attachment format shared by all faces. Sprite pipeline
        /// identity uses this format, not the cube object or face identity.
        [[nodiscard]] WGPUTextureFormat ColorFormat() const { return colorFormat_; }
        /// REMED-GFX-136: this target's own RenderTargetUsage, collapsed by
        /// RenderTargetUsagePreservesContentsEXT() -- true selects WGPULoadOp_Load for a face's
        /// colour attachment, mirroring WebGPURenderTargetRenderer::PreserveContents() exactly.
        /// Immutable after construction, so the load op recorded for a pass can never come from a
        /// value some LATER target changed.
        [[nodiscard]] bool PreserveContents() const { return preserveContents_; }
        /// WEBGPU-114: mip-chain length (1 unless mipMap=true), matching CalculateMipLevels(size).
        /// The colour texture carries this many levels; each face's chain is regenerated from its
        /// resolved level 0 after that face's render pass, and GetData accepts levels 0..this-1.
        [[nodiscard]] int LevelCount() const { return levelCount_; }

    private:
        WebGPURenderer* owner_ = nullptr;
        int size_ = 0;
        /// WEBGPU-114: number of mip levels in texture_ (1 unless mipMap=true).
        int levelCount_ = 1;
        /// Mirrors owner_->surfaceFormat_ at construction time (see this class's own .cpp
        /// constructor comment) -- captured so GetData() can decide whether a BGRA->RGBA byte
        /// swap is needed, exactly like WebGPURenderTargetRenderer::colorFormat_'s identical role.
        WGPUTextureFormat colorFormat_ = WGPUTextureFormat_Undefined;
        WGPUTexture texture_ = nullptr;
        WGPUTextureView cubeView_ = nullptr;
        /// REMED-GFX-167: keeps texture_/cubeView_ — the pair EnvironmentMapEffect samples — alive
        /// for any queued command still carrying it. The per-face and depth views are attachments
        /// only, consumed synchronously while this target is bound (see the RenderTarget2D twin).
        std::shared_ptr<const WebGPUSampledResourceEXT> sampled_;
        std::array<WGPUTextureView, 6> faceViews_{};
        /// WEBGPU-114: per-face multisampled colour, created only when MSAA is engaged. SIX
        /// separate single-layer textures (one per face) rather than one 6-layer texture, because
        /// WebGPU forbids a multisampled ARRAY texture ("depth or array layers must be 1"); this
        /// still gives each face its own attachment (see MsaaColorAttachmentView()).
        std::array<WGPUTexture, 6> msaaColorTextures_{};
        std::array<WGPUTextureView, 6> msaaColorViews_{};
        /// The MSAA sample count this target actually renders at, captured from the owner's global
        /// sampleCount_ at construction (0 when MSAA is not engaged). Mirrors
        /// WebGPURenderTargetRenderer::appliedMultiSampleCount_.
        int appliedMultiSampleCount_ = 0;
        WGPUTexture depthTexture_ = nullptr;
        WGPUTextureView depthView_ = nullptr;
        /// WEBGPU-39: mapped depth attachment format (Undefined = None, no depth texture created).
        WGPUTextureFormat depthFormat_ = WGPUTextureFormat_Depth24PlusStencil8;
        bool depthHasStencil_ = true;
        bool preserveContents_ = false;
    };

    /// WEBGPU-57/112: a plain volume (3D) texture -- `WGPUTextureDimension_3D`, uploaded/read back
    /// via `wgpuQueueWriteTexture`/a staged `wgpuCommandEncoderCopyTextureToBuffer` readback
    /// exactly like `WebGPUTextureRenderer`'s own 2D equivalents, just with a third (depth) extent
    /// dimension. Mirrors `VulkanTexture3DRenderer`'s minimal scope: upload/readback only, no
    /// render-target-ness (XNA's `Texture3D` itself is never renderable). Mip-level COUNT uses the
    /// same width/height-only `CalculateMipLevels()` formula as `Texture3D.cpp` (depth does not
    /// participate in the count, matching FNA's `Texture3D` constructor) -- wgpu-native still
    /// halves the actual per-level depth extent automatically (standard 3D-texture mip rules),
    /// this only affects how many levels are allocated.
    class WebGPUTexture3DRenderer final : public ITexture3DRenderer
    {
    public:
        WebGPUTexture3DRenderer(WebGPURenderer& owner, int width, int height, int depth, bool mipMap);
        ~WebGPUTexture3DRenderer() override;

        WebGPUTexture3DRenderer(const WebGPUTexture3DRenderer&) = delete;
        WebGPUTexture3DRenderer& operator=(const WebGPUTexture3DRenderer&) = delete;

        /// REMED-GFX-135: same explicit completion contract as WebGPUTextureCubeRenderer::SetData.
        [[nodiscard]] bool SetData(int level, int x, int y, int z,
                                   int w, int h, int depth,
                                   const void* data, int dataLength) override;
        /// Same staged-copy/row-alignment/async-map technique as `WebGPUTextureRenderer::GetData()`
        /// (WEBGPU-51), extended to a 3rd (depth) dimension: the whole requested level is copied
        /// to a temporary readback buffer sized `alignedBytesPerRow * levelHeight * levelDepth`,
        /// then the @p x,@p y,@p z,@p w,@p h,@p depth sub-volume is extracted from the mapped
        /// memory on the CPU side.
        /// REMED-GFX-130: same explicit completion contract as WebGPUTextureCubeRenderer::GetData.
        [[nodiscard]] bool GetData(int level, int x, int y, int z,
                                   int w, int h, int depth,
                                   void* data, int dataLength) const override;

        [[nodiscard]] WGPUTexture Texture() const { return texture_; }

    private:
        WebGPURenderer* owner_ = nullptr;
        WGPUTexture texture_ = nullptr;
        int width_ = 0;
        int height_ = 0;
        int depth_ = 0;
        int mipLevels_ = 1;
    };

    class WebGPUVertexBufferRenderer final : public IVertexBufferRenderer
    {
    public:
        WebGPUVertexBufferRenderer(WebGPURenderer& owner, int vertexCapacity);
        ~WebGPUVertexBufferRenderer() override;

        void SetData(const void* data, int vertexCount, std::size_t strideInBytes) override;
        // REMED-GFX-DECL-GUARD: this renderer still selects its WGPUVertexBufferLayout from the
        // byte stride (REMED-GFX-217), but the declaration is remembered rather than discarded so
        // a draw can refuse a declaration that layout would silently reinterpret.
        void SetVertexDeclaration(const VertexDeclaration& vertexDeclaration) override
        {
            declaration_.Remember(vertexDeclaration);
        }
        void SetDataWithOptions(const void* data, int vertexCount, std::size_t strideInBytes, SetDataOptions options) override;
        [[nodiscard]] int GetVertexCount() const override { return vertexCount_; }

        [[nodiscard]] WGPUBuffer Buffer() const { return buffer_; }
        [[nodiscard]] std::size_t Stride() const { return stride_; }
        /// The declaration this buffer carries, for REMED-GFX-DECL-GUARD's fidelity check.
        [[nodiscard]] const CNA::Internal::Graphics::DeclaredVertexLayout& Declaration() const
        {
            return declaration_;
        }
        // CPU-side copy of the most recent SetData() upload. WGPUBuffer objects are not
        // host-readable without an async GPU->CPU copy, but DrawColoredPrimitives() (Phase 57
        // vertical slice) needs the raw bytes *synchronously* at call time -- the caller's
        // IVertexBufferRenderer is typically a function-local temporary (see
        // GraphicsDevice::DrawUserPrimitives()) destroyed before this renderer's deferred
        // per-frame render actually runs, so the bytes must be copied out now, not referenced.
        [[nodiscard]] const std::vector<std::uint8_t>& ShadowData() const { return shadowData_; }

    private:
        WebGPURenderer* owner_ = nullptr;
        WGPUBuffer buffer_ = nullptr;
        std::uint64_t capacityBytes_ = 0;
        int vertexCapacity_ = 0;
        int vertexCount_ = 0;
        std::size_t stride_ = 0;
        std::vector<std::uint8_t> shadowData_;
        CNA::Internal::Graphics::DeclaredVertexLayout declaration_;
    };

    class WebGPUIndexBufferRenderer final : public IIndexBufferRenderer
    {
    public:
        WebGPUIndexBufferRenderer(WebGPURenderer& owner, int indexCapacity, bool thirtyTwoBit);
        ~WebGPUIndexBufferRenderer() override;

        void SetData16(const void* data, int indexCount) override;
        void SetData32(const void* data, int indexCount) override;
        void SetData16WithOptions(const void* data, int indexCount, SetDataOptions options) override;
        void SetData32WithOptions(const void* data, int indexCount, SetDataOptions options) override;
        [[nodiscard]] int GetIndexCount() const override { return indexCount_; }
        [[nodiscard]] bool IsThirtyTwoBit() const override { return thirtyTwoBit_; }

        [[nodiscard]] WGPUBuffer Buffer() const { return buffer_; }
        // See WebGPUVertexBufferRenderer::ShadowData() for why this exists.
        [[nodiscard]] const std::vector<std::uint8_t>& ShadowData() const { return shadowData_; }

    private:
        void Upload(const void* data, int indexCount, bool dataIsThirtyTwoBit);

        WebGPURenderer* owner_ = nullptr;
        WGPUBuffer buffer_ = nullptr;
        std::uint64_t capacityBytes_ = 0;
        int indexCapacity_ = 0;
        int indexCount_ = 0;
        bool thirtyTwoBit_ = false;
        std::vector<std::uint8_t> shadowData_;
    };

    /**
     * @brief WEBGPU-84: a GPU occlusion query — counts the samples that pass depth/stencil for the
     *        draws issued between `OcclusionQuery.Begin()` and `End()`.
     *
     * The renderer drives the real GPU recording (a slot in a shared occlusion `WGPUQuerySet`,
     * `BeginOcclusionQuery`/`EndOcclusionQuery` around this query's tagged draws, and the resolve +
     * readback), so `WebGPURenderer` is a one-directional `friend` — the same idiom
     * `VulkanOcclusionQueryRenderer` uses. Every code path is gated on an active query, so a frame
     * that uses none records exactly what it did before this type existed.
     */
    class WebGPUOcclusionQueryRenderer final : public IOcclusionQueryRenderer
    {
        friend class WebGPURenderer;

    public:
        /** @brief Constructs the query and reserves its slot in the renderer's occlusion query set. */
        explicit WebGPUOcclusionQueryRenderer(WebGPURenderer* owner);
        /** @brief Frees the slot and detaches from every pending renderer reference. */
        ~WebGPUOcclusionQueryRenderer() override;

        /** @brief Makes subsequent draws count toward this query until `End()`. */
        void Begin() override;
        /** @brief Stops tagging draws for this query. */
        void End() override;
        /** @brief Whether a resolved sample count is available. */
        [[nodiscard]] bool IsComplete() const override;
        /** @brief The number of samples that passed for the tagged draws (0 until resolved). */
        [[nodiscard]] int PixelCount() const override;

    private:
        WebGPURenderer* owner_ = nullptr;
        int slot_ = -1;                    ///< Fixed slot in the shared occlusion `WGPUQuerySet`.
        bool ended_ = false;               ///< `End()` seen since the last `Begin()`.
        bool recordedThisFlush_ = false;   ///< Its first contiguous draw run this flush is wrapped.
        bool complete_ = false;            ///< A resolved result is available.
        mutable int pixelCount_ = 0;       ///< The last resolved sample count.
    };

    /**
     * @brief WEBGPU-76: one vertex attribute a custom `ShaderEffect` draw feeds its WGSL vertex
     * stage, captured by value into the deferred draw command. CNAEXT.
     *
     * The bound `VertexBuffer`'s `VertexDeclaration` drives this: `shaderLocation` is the element's
     * INDEX in the declaration's element list (the same "location = declaration index" convention
     * EasyGL's custom-effect route uses, so one WGSL source runs unchanged across the renderer
     * family), `format` is `WebGPUVertexFormatFromVEF(element.format)` (WEBGPU-116) and `offset` is
     * the element's own byte offset within the vertex.
     */
    struct CustomEffectVertexAttr
    {
        /** @brief The WGPU vertex format, mapped from the declaration element's `VertexElementFormat`. */
        WGPUVertexFormat format{};
        /** @brief The element's byte offset within one vertex. */
        std::uint64_t offset = 0;
        /** @brief The WGSL `@location(i)` this attribute feeds — the element's declaration index. */
        std::uint32_t shaderLocation = 0;
    };

    /**
     * @brief WEBGPU-76: the WebGPU implementation of a custom-WGSL `ShaderEffect` (`IEffectRenderer`).
     * CNAEXT.
     *
     * Constructed by `WebGPURenderer::CreateEffectRenderer(vertWgsl, fragWgsl)` from the two source
     * strings a `ShaderEffect` carries. Unlike EasyGL's loose named GL uniforms, WGSL has no loose
     * uniforms — every parameter lives in a uniform block — so this renderer implements
     * `DeclareUniformBlockEXT` (the name→byte-offset bridge the interface documents for exactly this
     * case) and every `SetUniform*` writes into a CPU-side staging block that a draw uploads to a
     * `@group(0) @binding(0)` uniform buffer.
     *
     * Binding convention the custom WGSL must follow (documented in `docs/webgpu-renderer.md`):
     *  - `@group(0) @binding(0) var<uniform> …` — the uniform block (visible to both stages);
     *  - `@group(0) @binding(1) var …: sampler;` and `@binding(2) var …: texture_2d<f32>;` — only
     *    when the fragment source samples a texture (detected by a source scan for `texture_2d`);
     *  - vertex entry point `vs_main`, fragment entry point `fs_main`;
     *  - a vertex input at `@location(i)` for the i-th declared vertex element.
     *
     * The renderer sets `World`/`View`/`Projection` (column-major, untransposed) on this effect at
     * queue time exactly as EasyGL's `BindCustomEffectMatrices` does, so a 3D custom shader reads
     * those three by name from its uniform block. Pipelines are cached here, keyed by the concrete
     * render-pass state (vertex layout, target format/count, sample count, blend, depth); the
     * colour-target count is a parameter so MRT (`WEBGPU-85/86/87`) reuses this builder for N slots.
     */
    class WebGPUEffectRenderer final : public IEffectRenderer
    {
        friend class WebGPURenderer;

    public:
        /** @brief Constructs an unbuilt effect owned by @p owner; `CompileProgram` builds it. */
        explicit WebGPUEffectRenderer(WebGPURenderer* owner);
        /** @brief Releases the shader modules, layouts, sampler and every cached pipeline. */
        ~WebGPUEffectRenderer() override;

        /**
         * @brief Compiles the vertex and fragment WGSL into two `WGPUShaderModule`s.
         * @param vertSrc Complete WGSL module with a `@vertex fn vs_main`.
         * @param fragSrc Complete WGSL module with a `@fragment fn fs_main`.
         * @return True on success; false with `GetCompileError()` set on failure (never throws, so
         *         the tolerant neutral clone test survives an invalid placeholder source).
         */
        bool CompileProgram(const std::string& vertSrc, const std::string& fragSrc) override;
        /** @brief Marks this the active custom effect on the owning renderer. */
        void Bind() override;
        /** @brief Clears the active custom effect if it is this one. */
        void Unbind() override;
        /** @brief Whether both modules and the layouts were built successfully. */
        [[nodiscard]] bool IsValid() const override;
        /** @brief The last compile/validation error, or empty. */
        [[nodiscard]] std::string GetCompileError() const override;

        /**
         * @brief Declares the WGSL uniform block's size and member byte offsets by name.
         * @param blockSizeBytes std140/WGSL-padded size of the whole block.
         * @param names Member names, @p count of them.
         * @param offsets Each member's byte offset from the start of the block.
         * @param count Number of members.
         */
        void DeclareUniformBlockEXT(int blockSizeBytes, const char* const* names,
                                    const int* offsets, int count) override;
        /** @brief Writes a float into the staging block at @p name's declared offset. */
        void SetUniformFloat(const char* name, float value) override;
        /** @brief Writes an int (as a 32-bit word) into the staging block at @p name's offset. */
        void SetUniformInt(const char* name, int value) override;
        /** @brief Writes a vec2 into the staging block at @p name's declared offset. */
        void SetUniformVec2(const char* name, float x, float y) override;
        /** @brief Writes a vec3 into the staging block at @p name's declared offset. */
        void SetUniformVec3(const char* name, float x, float y, float z) override;
        /** @brief Writes a vec4 into the staging block at @p name's declared offset. */
        void SetUniformVec4(const char* name, float x, float y, float z, float w) override;
        /** @brief Writes a column-major 4×4 matrix into the staging block at @p name's offset. */
        void SetUniformMat4(const char* name, const float* matrix) override;
        /** @brief Writes @p count floats into the staging block at @p name's declared offset. */
        void SetUniformFloatArray(const char* name, const float* values, int count) override;
        /** @brief Writes @p count vec2 values into the staging block at @p name's offset. */
        void SetUniformVec2Array(const char* name, const float* values, int count) override;
        /** @brief Writes @p count vec3 values into the staging block at @p name's offset. */
        void SetUniformVec3Array(const char* name, const float* values, int count) override;
        /** @brief Writes @p count column-major matrices into the staging block at @p name's offset. */
        void SetUniformMat4Array(const char* name, const float* matrices, int count) override;
        /** @brief Binds a 2D texture for the custom shader's `@binding(2)`; only unit 0 is used. */
        void BindTexture(int unit, ITextureRenderer* texture) override;

    private:
        /// Compiles one WGSL string into a module inside a validation error scope; null on failure.
        WGPUShaderModule CompileModule(const std::string& wgsl, const char* label);
        /// Copies @p byteCount bytes from @p data into the staging block at @p name's offset.
        void WriteUniformBytes(const char* name, const void* data, int byteCount);

        WebGPURenderer* owner_ = nullptr;
        WGPUShaderModule vertexModule_ = nullptr;
        WGPUShaderModule fragmentModule_ = nullptr;
        WGPUBindGroupLayout bindGroupLayout_ = nullptr;
        WGPUPipelineLayout pipelineLayout_ = nullptr;
        WGPUSampler sampler_ = nullptr;         ///< Default filtering sampler for `@binding(1)`.
        bool valid_ = false;
        bool samplesTexture_ = false;           ///< Source scan: the fragment declares a `texture_2d`.
        std::string compileError_;
        std::vector<std::uint8_t> uniformStaging_;          ///< CPU uniform block, uploaded per draw.
        std::unordered_map<std::string, int> uniformOffsets_;  ///< Member name → byte offset.
        const ITextureRenderer* boundTexture0_ = nullptr;   ///< App-bound unit-0 texture (or null).
        std::unordered_map<std::uint64_t, WGPURenderPipeline> pipelineCache_;  ///< Keyed by pass state.
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

    /**
     * @brief REMED-GFX-116: the complete GraphicsDevice.Viewport captured BY VALUE at the exact
     *        public draw call that queued a deferred command.
     *
     * This renderer defers every draw of a bind cycle into one render pass that is recorded later.
     * The viewport is genuinely dynamic wgpu-native pass state, so it may be recorded late -- but
     * it must not be RESOLVED late: reading the renderer's live viewportX_/.../viewportMaxDepth_
     * members while recording gives every already-queued draw whatever viewport happens to be
     * current at flush time, which is exactly the defect this type removes. All six XNA Viewport
     * fields travel with the command; nothing here is a pointer to, or an index into, mutable
     * state, and nothing here participates in any pipeline cache key (a viewport change must never
     * create a pipeline variant or split a render pass).
     */
    struct WebGPUViewportSnapshot
    {
        /// False when no GraphicsDevice.Viewport was ever set; replay then uses the whole target.
        bool set = false;
        int x = 0;
        int y = 0;
        int width = 0;
        int height = 0;
        float minDepth = 0.0f;
        float maxDepth = 1.0f;
    };

    /**
     * @brief REMED-GFX-116: one render pass's viewport bookkeeping.
     *
     * Holds the target extent every captured snapshot is clamped against plus the exact values
     * last handed to wgpuRenderPassEncoderSetViewport, so a run of draws that share a viewport
     * issues one native call instead of one per draw. Correctness never depends on that skip --
     * dropping it would only add redundant identical calls.
     */
    struct WebGPUPassViewportState
    {
        int targetWidth = 0;
        int targetHeight = 0;
        bool applied = false;
        float x = 0.0f;
        float y = 0.0f;
        float width = 0.0f;
        float height = 0.0f;
        float minDepth = 0.0f;
        float maxDepth = 1.0f;
    };

    /**
     * @brief REMED-GFX-146: the complete scissor state captured BY VALUE at the exact public draw
     *        call that queued a deferred command.
     *
     * The scissor counterpart of WebGPUViewportSnapshot, and it exists for the same reason: the
     * native rectangle is dynamic wgpu-native render-pass state, so it may be RECORDED late, but
     * resolving it from the renderer's live `scissorEnabled_`/`scissorX_`/.../`scissorH_` members
     * while recording hands every already-queued draw whatever rectangle happens to be current at
     * flush time. Both halves of the decision travel together -- a snapshot that carried the
     * rectangle but read the enable flag live would still be wrong, since `SetRenderTarget` resets
     * `GraphicsDevice.ScissorRectangle` to the newly bound target's full size on every bind. Nothing
     * here is a pointer to, or an index into, mutable state, and nothing here participates in any
     * pipeline cache key: a rectangle change must never create a pipeline variant or split a pass.
     */
    struct WebGPUScissorSnapshot
    {
        /// RasterizerState.ScissorTestEnable as it stood at the public draw call.
        bool enabled = false;
        int x = 0;
        int y = 0;
        int width = 0;
        int height = 0;
    };

    /**
     * @brief REMED-GFX-146: one render pass's scissor bookkeeping.
     *
     * Holds the target extent every captured snapshot is clamped against plus the exact values last
     * handed to wgpuRenderPassEncoderSetScissorRect, so a run of draws that share a rectangle
     * issues one native call instead of one per draw. Correctness never depends on that skip --
     * dropping it would only add redundant identical calls.
     */
    struct WebGPUPassScissorState
    {
        int targetWidth = 0;
        int targetHeight = 0;
        bool applied = false;
        std::uint32_t x = 0;
        std::uint32_t y = 0;
        std::uint32_t width = 0;
        std::uint32_t height = 0;
    };

    class WebGPUSpriteBatchRenderer final : public ISpriteBatchRenderer
    {
    public:
        explicit WebGPUSpriteBatchRenderer(WebGPURenderer& owner);
        ~WebGPUSpriteBatchRenderer() override = default;

        void Begin() override;
        void End() override;
        void SetTransformMatrix(const Matrix& matrix) override { transform_ = matrix; }
        void SetCustomEffect(Effect* effect) override;
        void SetSamplerFilter(int textureFilter) override { textureFilter_ = textureFilter; }
        void SetSamplerAddressMode(int addressU, int addressV) override { addressU_ = addressU; addressV_ = addressV; }
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
        WebGPURenderer* owner_ = nullptr;
        bool begun_ = false;
        Matrix transform_ = Matrix::getIdentityProperty();
        int textureFilter_ = 0;
        int addressU_ = 1;
        int addressV_ = 1;
        // REMED-GFX-102: captured once in Begin(), before any Draw calls are deferred/sorted.
        // Every resulting SpriteCommand receives this complete value snapshot.
        WebGPUSpriteBlendSnapshot blendSnapshot_{};
    };

    class WebGPURenderer final : public IGraphicsRenderer
    {
    public:
        /// WEBGPU-78: real per-draw BlendState factors/op, mirroring
        /// VulkanRenderer::BlendKeyParams field-for-field. Declared early because it is
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

        /// WEBGPU-83: the XNA stencil state baked into a pipeline's `WGPUDepthStencilState`, captured
        /// per draw command (a stamp draw and a gated draw in one frame carry different stencil
        /// state, so it cannot be read as frame-global at replay). In WebGPU the read/write masks are
        /// pipeline state (unlike Vulkan's dynamic masks), so they fold into the pipeline cache key
        /// too; only the reference value is dynamic (`wgpuRenderPassEncoderSetStencilReference`).
        /// Enum ordinals are XNA's: CompareFunction (Always=0..NotEqual=7), StencilOperation
        /// (Keep=0, Zero=1, Replace=2, Increment=3, Decrement=4, IncrementSaturation=5,
        /// DecrementSaturation=6, Invert=7).
        struct StencilKeyParams
        {
            bool enable = false;
            int func = 0;         ///< front/CW compare
            int stencilPass = 0;  ///< front/CW ops
            int stencilFail = 0;
            int depthFail = 0;
            int readMask = 0xFF;
            int writeMask = 0xFF;
            bool twoSided = false;
            int ccwFunc = 0;      ///< back/CCW compare + ops (only when twoSided)
            int ccwPass = 0;
            int ccwFail = 0;
            int ccwDepthFail = 0;
        };

        struct SpriteVertex
        {
            float position[3];
            float uv[2];
            float color[4];
        };

        struct SpriteCommand
        {
            WebGPUSampledTextureEXT texture;
            std::array<SpriteVertex, 6> vertices{};
            int textureFilter = 0;
            int addressU = 1;
            int addressV = 1;
            int addressW = 1;  ///< WEBGPU-160: the third addressing axis.
            int maxMipLevel = 0;  ///< WEBGPU-161: SamplerState.MaxMipLevel.
            // REMED-GFX-102: complete per-Begin BlendState + BlendFactor capture by value. No
            // BlendState pointer, live renderer state, texture identity, sprite identity, or target
            // object identity participates in replay or pipeline selection.
            WebGPUSpriteBlendSnapshot blend{};
            // REMED-GFX-072: true when the Viewport active at enqueue time was a genuine
            // sub-region of the target, in which case the vertices above are baked VIEWPORT-LOCAL
            // (normalized by Viewport.Width/Height) instead of target-relative. This is purely the
            // NDC-baking decision; where the sprite LANDS is driven by the snapshot below.
            bool viewportCustom = false;
            // REMED-GFX-116: the complete Viewport active at this sprite's own public Draw call,
            // captured unconditionally. GFX-072 captured it only for a genuine sub-region, so a
            // sprite baked target-relative was still replayed under whatever viewport was live at
            // flush time -- setting a sub-Viewport after the batch, before the flush, squeezed it.
            WebGPUViewportSnapshot viewport{};
            /// REMED-GFX-146: the complete scissor state (ScissorTestEnable plus the four
            /// rectangle components) active at this draw's own public call, captured by
            /// value. Never resolved from live state during replay.
            WebGPUScissorSnapshot scissor{};
            /// WEBGPU-142: the custom-WGSL ShaderEffect bound by `SpriteBatch.Begin(..., effect)`,
            /// or null for a stock SpriteBatch draw. Supplies the sprite's shader modules/layout;
            /// its uniform block is captured by value in `customUniforms` (a second Begin with
            /// different uniforms must not see this sprite's block at replay).
            WebGPUEffectRenderer* customEffect = nullptr;
            std::vector<std::uint8_t> customUniforms;  ///< the effect's uniform block, by value
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
            /// WEBGPU-86 MRT: a sprite drawn into an MRT set needs a matching N-target pipeline
            /// (writing attachment 0 only); 1 for the backbuffer / single target / cube face.
            int colorAttachmentCount = 1;
            /// WEBGPU-39: the active pass's depth attachment format (Undefined for DepthFormat::None).
            /// A sprite pipeline must declare the same depth format as the pass it draws into.
            WGPUTextureFormat depthFormat = WGPUTextureFormat_Depth24PlusStencil8;

            [[nodiscard]] bool operator==(const SpritePipelineKey& other) const noexcept
            {
                return blendEnabled == other.blendEnabled &&
                    colorSrc == other.colorSrc && colorDst == other.colorDst &&
                    alphaSrc == other.alphaSrc && alphaDst == other.alphaDst &&
                    colorFunc == other.colorFunc && alphaFunc == other.alphaFunc &&
                    colorWriteMask == other.colorWriteMask &&
                    multiSampleMask == other.multiSampleMask &&
                    targetFormat == other.targetFormat && sampleCount == other.sampleCount &&
                    colorAttachmentCount == other.colorAttachmentCount &&
                    depthFormat == other.depthFormat;
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
                mix(static_cast<std::size_t>(key.colorAttachmentCount));
                mix(static_cast<std::size_t>(key.depthFormat));
                return hash;
            }
        };

        explicit WebGPURenderer(const GraphicsRendererCreateArgs& args);
        ~WebGPURenderer() override;

        WebGPURenderer(const WebGPURenderer&) = delete;
        WebGPURenderer& operator=(const WebGPURenderer&) = delete;

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
        /// REMED-GFX-116: native wgpuRenderPassEncoderSetViewport calls issued since device
        /// creation. Its permanent regression asserts the exact count for known draw sequences,
        /// so a per-viewport render-pass split or a per-draw redundant call cannot creep back in.
        [[nodiscard]] std::size_t GetSetViewportCallCountEXT() const noexcept
        {
            return setViewportCallCount_;
        }
        /// REMED-GFX-146: native wgpuRenderPassEncoderSetScissorRect calls issued since device
        /// creation. Its permanent regression asserts the exact count for known draw sequences, so
        /// a per-rectangle render-pass split or a per-draw redundant call cannot creep back in.
        [[nodiscard]] std::size_t GetSetScissorCallCountEXT() const noexcept
        {
            return setScissorCallCount_;
        }
        /// REMED-GFX-116: render passes begun since device creation. A viewport change is dynamic
        /// pass state and must never open a new pass.
        [[nodiscard]] std::size_t GetRenderPassCountEXT() const noexcept
        {
            return renderPassCount_;
        }
        /// REMED-GFX-116: wgpuQueueSubmit calls since device creation, for the same reason.
        [[nodiscard]] std::size_t GetQueueSubmitCountEXT() const noexcept
        {
            return queueSubmitCount_;
        }
        /// REMED-GFX-211/213 diagnostic used by the permanent regression. Neither binding's
        /// VertexOffset nor the InstanceFrequency is part of this cache's key -- the offsets select
        /// source records and the divisor is expanded into the instance staging copy, so a draw
        /// that changes either must reuse the variant an otherwise-identical draw already built.
        [[nodiscard]] std::size_t GetInstancedPipelineCacheSizeEXT() const noexcept
        {
            return instancedPipelines_.size();
        }
        /// WEBGPU-115: draw commands appended to @ref drawOrder_ since device creation, across
        /// every family. Cumulative and never reset, unlike drawOrder_ itself, which a flush
        /// drains -- so a refused draw can be proven to have queued nothing even across the bind
        /// cycle that would have replayed it.
        [[nodiscard]] std::size_t GetQueuedDrawCommandCountEXT() const noexcept
        {
            return queuedDrawCommandCount_;
        }
        /// WEBGPU-115: queued draw commands actually replayed into a native render pass since
        /// device creation. It is the count of native wgpuRenderPassEncoderDraw*/sprite issues, so
        /// "nothing was submitted for the refused draw" is measured rather than inferred.
        [[nodiscard]] std::size_t GetNativeDrawIssueCountEXT() const noexcept
        {
            return nativeDrawIssueCount_;
        }
        /// WEBGPU-12/59: lifetime count of real wgpuDeviceCreateBuffer calls for transient per-draw
        /// buffers. Bounded once the pool warms up -- a repeating scene stops allocating. The stress
        /// test asserts this plateaus while GetTransientBufferReuseCountEXT keeps climbing.
        [[nodiscard]] std::size_t GetTransientBufferCreateCountEXT() const noexcept
        {
            return transientBuffersCreatedEXT_;
        }
        /// WEBGPU-12/59: lifetime count of transient per-draw buffers served from the pool (reuses).
        /**
         * @brief plans/plan_webgpu.md WEBGPU-160: how many distinct native samplers the slot cache holds.
         *
         * `SamplerState.AddressW` is observable in PIXELS only where a renderer samples a volume
         * texture, and this one has no public route that does (`IEffectRenderer::BindTexture3D` is
         * unimplemented here). This counter is what makes the state's arrival measurable anyway:
         * three draws that differ only in `AddressW` must create three distinct samplers, and a
         * fourth repeating one of them must create none. Before the fix they collided onto a single
         * cache entry, because `addressW` was in neither the key nor the descriptor.
         *
         * @return The number of cached slot samplers.
         */
        [[nodiscard]] std::size_t GetSlotSamplerCacheSizeEXT() const noexcept
        {
            return slotSamplerCache_.size();
        }
        [[nodiscard]] std::size_t GetTransientBufferReuseCountEXT() const noexcept
        {
            return transientBuffersReusedEXT_;
        }

        /**
         * @brief WEBGPU-28: compiles every WGSL shader source (webgpu_shaders.hpp) through this
         *        device inside a validation error scope, including shaders a scene never draws (the
         *        lazy mipBlit shader and the Pbr/SkinnedPbr expanded variants), so a WGSL error is
         *        caught deterministically rather than only at that effect's first pipeline creation.
         *        Also reachable at device init via the CNA_WEBGPU_VALIDATE_SHADERS environment
         *        variable, and exercised by the WebGPU_ShaderValidation test.
         *
         * @param failedLabels Optional; on return holds the label of every shader that failed.
         * @return The number of shaders that failed to compile cleanly (0 on a healthy renderer).
         */
        CNAEXT [[nodiscard]] int ValidateAllShadersEXT(
            std::vector<std::string>* failedLabels = nullptr) const;

        /**
         * @brief Reports whether @p capability is genuinely available on this renderer.
         *
         * WEBGPU-115: `GraphicsCapability::WireFrame` answers **false** here. wgpu-native exposes
         * no polygon mode at all -- `WGPUPrimitiveState` has no field a wireframe fill could reach
         * -- so the shared permissive default (which this renderer used to inherit) was an
         * affirmative false claim: it promised a mode the renderer then silently replaced with a
         * solid fill. Everything else falls through to `IGraphicsRenderer`'s own answer, including
         * `MultiStreamVertexInput`, whose shared default is already false (REMED-GFX-201).
         *
         * @param capability The capability to query.
         * @return True when this renderer can genuinely honour @p capability.
         */
        [[nodiscard]] bool SupportsCapability(CNA::GraphicsCapability capability) const override;

        /**
         * @brief WEBGPU-198: whether a `RenderTarget2D` may be created with @p surfaceFormat here.
         *
         * Probed, not tabulated: a 1x1 `WGPUTextureUsage_RenderAttachment` texture of the real
         * native format is created inside a `WGPUErrorFilter_Validation` scope, the same technique
         * `Supports4xMsaa()` uses, so the answer describes the adapter in front of us rather than a
         * spec table. `Color` is `Supported` without a probe -- it is the swap chain's own format,
         * which the renderer is already rendering into.
         *
         * @param surfaceFormat SurfaceFormat ordinal.
         * @return Supported, Unsupported, or Defer for a format with no render-target mapping.
         */
        [[nodiscard]] CNA::Internal::Renderers::RendererFormatVerdict
        ClassifyRenderTargetFormatEXT(int surfaceFormat) const override;

        /**
         * @brief WEBGPU-198: the cached render-attachment probe behind
         *        @ref ClassifyRenderTargetFormatEXT.
         *
         * @param format The native format to probe.
         * @return Whether the device will create a render-attachment texture of it.
         */
        [[nodiscard]] bool ProbeRenderTargetFormatEXT(WGPUTextureFormat format) const;

        /**
         * @brief WEBGPU-200: whether 32-bit float colour targets may be blended into.
         *
         * The device feature, unless a test has forced it off -- see
         * @ref DebugForceFloat32FeaturesAbsentEXT.
         *
         * @return Whether blending into an R32/RG32/RGBA32Float target is available.
         */
        [[nodiscard]] bool Float32BlendableEXT() const
        {
            return float32Blendable_ && !forceFloat32FeaturesAbsent_;
        }

        /**
         * @brief WEBGPU-200: whether 32-bit float textures may be sampled with a filtering sampler.
         *
         * @return Whether `WGPUFeatureName_Float32Filterable` is in effect.
         */
        [[nodiscard]] bool Float32FilterableEXT() const
        {
            return float32Filterable_ && !forceFloat32FeaturesAbsent_;
        }

        /**
         * @brief WEBGPU-200: makes this renderer behave as if the two float32 features were absent.
         *
         * Every adapter available to this project has both, so the refusal paths that depend on
         * their absence would otherwise be unreachable code that no test ever executes -- a branch
         * asserted only by reading it. This forces the flags off so those paths run; it does not
         * remove the real device features, and what it therefore tests is CNA's own behaviour,
         * which is the part that can be wrong.
         *
         * @param absent Whether to report both features absent.
         */
        CNAEXT void DebugForceFloat32FeaturesAbsentEXT(bool absent)
        {
            forceFloat32FeaturesAbsent_ = absent;
        }

        /**
         * @brief WEBGPU-198: creates a `RenderTarget2D` in the format it was actually asked for.
         *
         * `IGraphicsRenderer`'s default forwards to the format-less overload and drops the
         * argument, which is what let a target be classified in one format and allocated in
         * another. A format this adapter refuses is refused here by name.
         *
         * @param w Width in pixels.
         * @param h Height in pixels.
         * @param depthFormat XNA DepthFormat ordinal.
         * @param preserveContents Whether the target's contents survive a rebind.
         * @param mipMap Whether to allocate and regenerate a mip chain.
         * @param multiSampleCount This target's own requested sample count.
         * @param surfaceFormat SurfaceFormat ordinal.
         * @return The target renderer.
         */
        std::unique_ptr<IRenderTargetRenderer> CreateRenderTarget2DEXT(
            int w, int h, int depthFormat, bool preserveContents, bool mipMap,
            int multiSampleCount, int surfaceFormat) override;

        /**
         * @brief WEBGPU-198: the cube twin of @ref CreateRenderTarget2DEXT.
         *
         * @param size Edge length in pixels.
         * @param depthFormat XNA DepthFormat ordinal.
         * @param preserveContents Whether the target's contents survive a rebind.
         * @param mipMap Whether to allocate and regenerate a mip chain per face.
         * @param multiSampleCount This target's own requested sample count.
         * @param surfaceFormat SurfaceFormat ordinal.
         * @return The cube target renderer.
         */
        std::unique_ptr<IRenderTargetCubeRenderer> CreateRenderTargetCubeEXT(
            int size, int depthFormat, bool preserveContents, bool mipMap,
            int multiSampleCount, int surfaceFormat) override;

        /**
         * @brief WEBGPU-172: how many `VertexBufferBinding`s of one input rate this device takes.
         *
         * Derived from the device's own `maxVertexBuffers` limit, less the one slot reserved for
         * the neutral record, and clamped to the resolver's stream table so a binding array is
         * never silently truncated.
         *
         * @return The maximum number of streams of a single input rate.
         */
        [[nodiscard]] int GetMaxVertexStreams() const override;

        void Clear(float r, float g, float b, float a) override;
        void Present() override;
        void ReadBackbuffer(int x, int y, int w, int h, uint8_t* pixels) override;
        void GetViewportSize(int& width, int& height) override;
        void SetVirtualResolution(int width, int height) override;
        void SetPresentationMode(int mode) override;
        void SetSwapInterval(int interval) override;
        void OnSurfaceChanged(const RendererSurfaceInfo& surface) override;
        /// WEBGPU-58: reconfigures this renderer's single GLOBAL MSAA sample count in place --
        /// mirroring VulkanRenderer::ApplyMultiSampleCount()'s own "one value, invalidate
        /// every pipeline cache, rebuild lazily" design (see sampleCount_'s own comment for why
        /// this is a renderer-wide value rather than a per-pipeline-cache-key dimension). Returns
        /// the real, device-clamped applied count (0 = disabled) via GetMultiSampleCount().
        int ApplyMultiSampleCount(int requestedMultiSampleCount) override;
        /// WEBGPU-58: the backbuffer's real, applied MSAA sample count (0 = none).
        [[nodiscard]] int GetMultiSampleCount() const override;
        bool TransformWindowToLogical(float windowX, float windowY, float& logicalX, float& logicalY) const override;
        bool TransformLogicalToWindow(float logicalX, float logicalY, float& windowX, float& windowY) const override;


        std::unique_ptr<ITextureRenderer> CreateTexture(const ImageData& data) override;
        std::unique_ptr<ISpriteBatchRenderer> CreateSpriteBatch() override;

        void ClearColorAndDepth(float r, float g, float b, float a, float depth) override;
        void ClearDepth(float depth) override;
        void ClearStencil(int stencil) override;
        void ClearDepthAndStencil(float depth, int stencil) override;
        void ClearColorAndStencil(float r, float g, float b, float a, int stencil) override;
        void ClearColorDepthAndStencil(float r, float g, float b, float a, float depth, int stencil) override;
        void SetDepthTestEnabled(bool enabled) override { depthTestEnabled_ = enabled; }
        void SetBlendEnabled(bool enabled) override { blendEnabled_ = enabled; }
        void SetDepthWriteEnabled(bool enabled) override { depthWriteEnabled_ = enabled; }
        // Depth portion was the original Phase 57/63 vertical slice; WEBGPU-83 (complete) also stores
        // the stencil parameters (dsParams_ below) AND bakes them into every 3D pipeline's
        // WGPUStencilFaceState (via the pipeline cache key), applying the reference per draw. Without
        // the depth half of this override,
        // GraphicsDevice.DepthStencilState (the real XNA API surface almost every game/effect
        // uses, as opposed to the older SetDepthTestEnabled()/SetDepthWriteEnabled() convenience
        // methods above) had zero effect on this renderer -- found and fixed while verifying
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
        // pipeline object). WEBGPU-115: FillMode::WireFrame is still STORED here and this call
        // still succeeds -- selecting a RasterizerState is a state operation in XNA's lifecycle and
        // a state setter cannot know whether a draw will follow, exactly as REMED-GFX-DECL-GUARD
        // found for SetVertexDeclaration. The refusal happens at the first draw that would consume
        // it (RequireSupportedFillModeEXT below). DepthBias/SlopeScaleDepthBias ARE genuinely
        // supported by WGPUDepthStencilState (unlike a dynamic vkCmdSetDepthBias-style override)
        // and are baked in too.
        void ApplyRasterizerState(int cullMode, int fillMode, bool scissorTestEnable,
                                  float depthBias = 0.0f, float slopeScaleDepthBias = 0.0f) override;
        // WEBGPU-82: per-slot SamplerState -> a genuine WGPUSampler cache (GetOrCreateSlotSampler()
        // below), which REMED-GFX-170 made the renderer's only sampler translation -- SpriteBatch
        // reaches the same table. Storage-only here; actually read by every texture-consuming Queue*Draw() at
        // queue time for slot 0 (see each command's textureFilter/addressU/addressV capture).
        void ApplySamplerState(int slot, int filter, int addressU, int addressV, int maxAnisotropy) override;
        /**
         * @brief plans/plan_webgpu.md WEBGPU-160: the third addressing axis of a `SamplerState`.
         *
         * Separate from `ApplySamplerState` in the interface because it is observable only where a
         * renderer samples a volume texture, so each renderer adopts it explicitly. It was not
         * overridden here, so `SamplerState.AddressW` was discarded in silence -- including for
         * `Texture3D`, where it is the only axis that matters.
         *
         * @param slot The texture slot, 0-15.
         * @param addressW The raw `TextureAddressMode` ordinal for W.
         */
        void ApplySamplerAddressW(int slot, int addressW) override;
        /**
         * @brief plans/plan_webgpu.md WEBGPU-161: `SamplerState.MaxMipLevel` reaches the sampler.
         *
         * `ApplySamplerMipState` was not overridden, so `MaxMipLevel` was discarded in silence.
         * XNA's `MaxMipLevel` is the highest-resolution mip level sampling may use -- level 0 means
         * "no restriction" -- which is `WGPUSamplerDescriptor::lodMinClamp`, the same mapping the
         * reference renderer makes to `GL_TEXTURE_MIN_LOD` and FNA3D's SDL_GPU driver to `min_lod`.
         *
         * The `lodBias` argument is deliberately NOT consumed here: `WEBGPU-205` owns it, and it is
         * an implementation task rather than a limitation, so nothing in this renderer may claim
         * LOD bias is unsupported.
         *
         * @param slot The texture slot, 0-15.
         * @param maxMipLevel The highest-resolution mip level sampling may use.
         * @param lodBias `WEBGPU-205`'s, not consumed yet.
         */
        void ApplySamplerMipState(int slot, int maxMipLevel, float lodBias) override;

        /**
         * @brief plans/plan_webgpu.md WEBGPU-205: the routes that do not apply a sampler LOD bias.
         *
         * The state is implemented on every stock 3D route by WGSL `textureSampleBias`. Three
         * routes do not carry it, and the row's rule is that no route may ignore it in silence --
         * so they are named here rather than left for a caller to discover from pixels.
         *
         * @return The renderer-specific limitations text.
         */
        [[nodiscard]] std::string_view GetAdditionalLimitationsTextEXT() const override;
        // WEBGPU-80/81/29/30: scissor rect, viewport, blend constant and stencil reference are all
        // genuinely dynamic wgpu-native render-pass state (wgpuRenderPassEncoderSetScissorRect/
        // SetViewport/SetBlendConstant/SetStencilReference), exactly like Vulkan's own
        // vkCmdSetScissor/Viewport/BlendConstants/StencilReference -- storage-only here, applied
        // once per render pass in EnsureFrameRendered() from whatever is current at record time
        // (this renderer's single-deferred-pass-per-frame model, matching
        // VulkanRenderer::SetViewport()'s own documented simplification).
        void SetBlendFactor(float r, float g, float b, float a) override;
        void SetReferenceStencil(int value) override;
        void SetScissorRect(int x, int y, int w, int h) override;
        void SetViewport(int x, int y, int w, int h, float minDepth, float maxDepth) override;
        std::unique_ptr<IVertexBufferRenderer> CreateVertexBuffer(int vertexCapacity) override;
        std::unique_ptr<IIndexBufferRenderer> CreateIndexBuffer16(int indexCapacity) override;
        std::unique_ptr<IIndexBufferRenderer> CreateIndexBuffer32(int indexCapacity) override;
        void DrawColoredPrimitives(const IVertexBufferRenderer& vb,
                                   const Matrix& world, const Matrix& view, const Matrix& projection,
                                   PrimitiveType primitive, int primitiveCount) override;
        void DrawIndexedColoredPrimitives(const IVertexBufferRenderer& vb,
                                          const IIndexBufferRenderer& ib,
                                          const Matrix& world, const Matrix& view, const Matrix& projection,
                                          PrimitiveType primitive, int primitiveCount) override;
        // Real GpuDrawParams dispatch for stride-16 (VertexPositionColor) draws only -- reuses
        // the exact same colored3d.wgsl/GetOrCreatePipelineColored3D() infrastructure as
        // DrawColoredPrimitives(), but fills the uniform buffer from the caller's real
        // DiffuseColor/VertexColorEnabled instead of hardcoded white/true. Other strides (20/24/32,
        // needing textured3d/lit_textured3d -- not yet written, see plans/plan_webgpu.md Phase 58) and
        // other effects (alpha test/dual texture/env map/skinned) fall back to
        // DrawColoredPrimitives()/DrawIndexedColoredPrimitives(), replicating exactly what
        // IGraphicsRenderer's own default implementation already did before this override existed.
        /**
         * @brief plans/plan_webgpu.md WEBGPU-155: the ONE stock dispatch, shared by the indexed and
         *        non-indexed entry points.
         *
         * Selects the stock family from the vertex declaration's semantics plus the effect state,
         * then queues the matching draw. The skinned and PBR families were deliberately not
         * converted by that task and keep the stride cascade plus the REMED-GFX-DECL-GUARD refusal.
         *
         * @param vb The draw's vertex buffer.
         * @param ib The index buffer, or null for a non-indexed draw.
         * @param world The world matrix.
         * @param view The view matrix.
         * @param projection The projection matrix.
         * @param primitive The primitive type.
         * @param primitiveCount How many primitives to draw.
         * @param params The draw's effect state.
         * @param route The route's name, for the declaration-guard diagnostic.
         */
        void DispatchStockDrawEXT(const IVertexBufferRenderer& vb, const IIndexBufferRenderer* ib,
                                  const Matrix& world, const Matrix& view, const Matrix& projection,
                                  PrimitiveType primitive, int primitiveCount,
                                  const GpuDrawParams& params, const char* route);
        void DrawPrimitivesEx(const IVertexBufferRenderer& vb,
                              const Matrix& world, const Matrix& view, const Matrix& projection,
                              PrimitiveType primitive, int primitiveCount,
                              const GpuDrawParams& params) override;
        void DrawIndexedPrimitivesEx(const IVertexBufferRenderer& vb, const IIndexBufferRenderer& ib,
                                     const Matrix& world, const Matrix& view, const Matrix& projection,
                                     PrimitiveType primitive, int primitiveCount,
                                     const GpuDrawParams& params) override;
        // WEBGPU-27/38/68: real per-instance mat4 world transform via a genuine second
        // WGPUVertexStepMode_Instance vertex buffer binding -- see instancedShader_'s own doc
        // comment. A binding set with no per-instance stream (REMED-GFX-202:
        // FirstInstanceStream(params) == nullptr) falls back to a real (non-instanced)
        // DrawIndexedPrimitivesEx() dispatch, matching every other renderer's identical fallback
        // (VulkanRenderer::DrawInstancedPrimitivesEx's own precedent).
        void DrawInstancedPrimitivesEx(const IVertexBufferRenderer& vb, const IIndexBufferRenderer& ib,
                                       const Matrix& world, const Matrix& view, const Matrix& projection,
                                       PrimitiveType primitive, int primitiveCount, int instanceCount,
                                       const GpuDrawParams& params) override;

        void QueueSprite(const ITextureRenderer& texture,
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
        std::unique_ptr<IRenderTargetRenderer> CreateRenderTarget2D(int w, int h, int depthFormat,
                                                                    bool preserveContents = false,
                                                                    bool mipMap = false,
                                                                    int multiSampleCount = 0) override;
        void SetRenderTarget2D(IRenderTargetRenderer* rt) override;
        void SetRenderTargets(const RenderTargetBindingDescriptor* renderTargets,
                              int count) override;

        /// WEBGPU-56/74: minimal cube-map texture creation, sufficient for
        /// `EnvironmentMapEffect.EnvironmentMap` -- see `WebGPUTextureCubeRenderer`'s own doc
        /// comment for the documented TextureCube/RenderTargetCube parity gaps this does NOT close.
        std::unique_ptr<ITextureCubeRenderer> CreateTextureCube(int size, bool mipMap, int surfaceFormat) override;

        /// WEBGPU-57/112: real volume (3D) texture creation -- see `WebGPUTexture3DRenderer`'s own
        /// doc comment. `surfaceFormat` is currently ignored (always `WGPUTextureFormat_RGBA8Unorm`,
        /// matching `WebGPUTextureRenderer`/`WebGPUTextureCubeRenderer`'s own established convention
        /// for this renderer).
        std::unique_ptr<ITexture3DRenderer> CreateTexture3D(int w, int h, int depth, bool mipMap, int surfaceFormat) override;

        /// WEBGPU-114: real render-into-a-cube-face support -- see `WebGPURenderTargetCubeRenderer`'s
        /// own doc comment for exactly what this does and does not implement (no mip regen, no MSAA).
        std::unique_ptr<IRenderTargetCubeRenderer> CreateRenderTargetCube(int size, int depthFormat,
                                                                          bool preserveContents = false,
                                                                          bool mipMap = false,
                                                                          int multiSampleCount = 0) override;

        /** @brief WEBGPU-84: creates a GPU occlusion query backed by a slot in the shared query set. */
        std::unique_ptr<IOcclusionQueryRenderer> CreateOcclusionQuery() override;

        /**
         * @brief WEBGPU-76: compiles a custom `ShaderEffect` from a WGSL vertex/fragment source pair.
         * @param vertSrc WGSL vertex module (entry `vs_main`).
         * @param fragSrc WGSL fragment module (entry `fs_main`).
         * @return A `WebGPUEffectRenderer`; invalid (but non-null) when the source fails to compile.
         */
        std::unique_ptr<IEffectRenderer> CreateEffectRenderer(const std::string& vertSrc,
                                                              const std::string& fragSrc) override;

        /**
         * @brief WEBGPU-76: this renderer genuinely compiles and runs the WGSL source a
         * `ShaderEffect` carries (unlike Vulkan, which takes SPIR-V, or SOFTWARE/HEADLESS, which
         * accept a source and ignore it), so a custom effect's pixels are its shader's.
         */
        [[nodiscard]] bool ExecutesShaderEffectSourceEXT() const override { return true; }

        /** @brief WEBGPU-76: the source dialect a custom `ShaderEffect` must be written in. */
        [[nodiscard]] ShaderDialectEXT GetShaderDialectEXT() const override
        {
            return ShaderDialectEXT::Wgsl;
        }

        /**
         * @brief WEBGPU-144: whether a block-compressed format is uploaded to the GPU natively
         * (as BC blocks) rather than CPU-decompressed to RGBA8 first. True for the DXT/BC7 formats
         * when the adapter/device advertises `WGPUFeatureName_TextureCompressionBC` (checked at
         * device creation); the framework then routes SetData/GetData through the block path.
         */
        [[nodiscard]] bool IsCompressedTransferFormatEXT(int surfaceFormat) const override;

        /**
         * @brief WEBGPU-206: whether a `TextureCube` transfers @p surfaceFormat as raw BC blocks.
         *
         * The same answer as the 2D query. A cube in WebGPU is a six-layer 2D texture plus a cube
         * view, and the BC feature does not restrict block-compressed storage to non-array 2D, so
         * there is no reason for the two to differ -- and while they did, `WEBGPU-163` had to refuse
         * a BC cube at construction to stop the format query promising what `SetData` would refuse.
         *
         * @param surfaceFormat SurfaceFormat ordinal.
         * @return Whether a cube stores this format as blocks here.
         */
        [[nodiscard]] bool IsCompressedCubeTransferFormatEXT(int surfaceFormat) const override;

        /**
         * @brief WEBGPU-144 Phase 2: WebGPU keeps loaded block-compressed content compressed.
         *
         * The `Texture2D::FromStream` (DDS) and `.xnb` content loaders keep the raw DXT/BC blocks
         * and upload them to a `WGPUTextureFormat_BC*` instead of CPU-decompressing to `Color`. The
         * actual per-format capability (and the `bcSupported_` device-feature gate) is still enforced
         * by @ref IsCompressedTransferFormatEXT, which the loaders AND with this flag.
         *
         * @return true.
         */
        [[nodiscard]] bool LoadsCompressedContentNativelyEXT() const override;

        /** @brief WEBGPU-144: the BC formats this renderer stores natively are Supported; else Defer. */
        [[nodiscard]] RendererFormatVerdict ClassifySurfaceFormatEXT(int surfaceFormat) const override;
        /**
         * @brief plans/plan_webgpu.md WEBGPU-163: the CUBE storage verdict, which is narrower here.
         *
         * `WEBGPU-144` made block-compressed `Texture2D` real, and `ClassifySurfaceFormatEXT`
         * answers `Supported` for BC whenever the device enabled the feature. That one classifier
         * served both kinds, but `CreateTextureCube` discards the format and
         * `WebGPUTextureCubeRenderer` is RGBA8-only -- so `TextureCube(device, size, mipMap,
         * SurfaceFormat::Dxt1)` CONSTRUCTED and then threw from every `SetData`. Refusing at
         * construction is the interim honest state; `WEBGPU-206` is the parity state, because a
         * cube is a six-layer 2D texture plus a cube view and nothing in
         * `WGPUFeatureName_TextureCompressionBC` restricts it. This override must not outlive that
         * row, and must never be cited as a platform limitation.
         *
         * @param surfaceFormat SurfaceFormat ordinal.
         * @return `Unsupported` for a block-compressed format, otherwise the 2D verdict.
         */
        [[nodiscard]] RendererFormatVerdict ClassifyTextureCubeFormatEXT(int surfaceFormat) const override;

    private:
        friend class WebGPURenderTargetRenderer;
        friend class WebGPURenderTargetCubeRenderer;
        friend class WebGPUTextureRenderer;
        friend class WebGPUTextureCubeRenderer;
        friend class WebGPUEffectRenderer;
        friend class WebGPUSpriteBatchRenderer;
        friend class WebGPUOcclusionQueryRenderer;
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
        // VulkanRenderer::ApplyMultiSampleCount()'s own "clear every pipeline cache, rebuild
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
        [[nodiscard]] bool Supports4xMsaa() const;
        // WEBGPU-58: clamps a requested MultiSampleCount to this renderer's only two legal
        // WGPUMultisampleState.count values (1 or 4 -- unlike Vulkan's 1/2/4/8/16/32/64 range,
        // wgpu-native/WebGPU has no adjustable per-device "max sample count" limit at all; see
        // Supports4xMsaa()'s own comment). Mirrors VulkanRenderer's own PickSampleCount()
        // "highest legal value <= requested that the device actually supports" semantics: a
        // requested count in [2,4] rounds down to 1 unless 4x is supported (in which case it
        // rounds UP to 4, since 4 is the only value above 1), and anything >= 4 clamps down to 4.
        [[nodiscard]] int PickSampleCount(int requestedMultiSampleCount);
        // ------------------------------------------------------------------------------------
        // REMED-GFX-159: one ordered reference stream across every deferred draw family.
        //
        // This renderer keeps one command vector per family (eleven of them, below) and used to
        // replay them as a FIXED LIST of per-family loops -- colored, textured, litTextured,
        // alphaTest, dualTexture, envMap, instanced, pbr, skinned, skinnedPbr, then sprites -- in
        // all three of its pass recorders. That list, not the game's call order, decided what
        // executed first, so `SpriteBatch.Draw(); effect.Apply(); DrawPrimitives()` came out with
        // the sprite ON TOP, and (measured, not assumed) two 3D draws using different stock
        // effects inverted against each other for the same reason, with no SpriteBatch involved.
        //
        // The correction keeps the per-family storage exactly as it is -- every command struct,
        // every capture, every Queue*Draw() is unchanged -- and adds ONE stream of (family, index)
        // references pushed in public call order. Replay walks that stream. There is no sort, no
        // per-family pass, no submit or wait at a family change, and no order value in any
        // pipeline or cache key: an entry is two bytes plus an index, appended once per queued
        // draw and released with the command vectors at the end of the pass.
        // ------------------------------------------------------------------------------------

        /** @brief Which deferred command vector a queued draw lives in. */
        enum class DrawFamily : std::uint8_t
        {
            Sprite,       ///< spriteCommands_
            Colored,      ///< coloredDrawCommands_
            Textured,     ///< texturedDrawCommands_
            LitTextured,  ///< litTexturedDrawCommands_
            AlphaTest,    ///< alphaTestDrawCommands_
            DualTexture,  ///< dualTextureDrawCommands_
            EnvMap,       ///< envMapDrawCommands_
            Instanced,    ///< instancedDrawCommands_
            Pbr,          ///< pbrDrawCommands_
            Skinned,      ///< skinnedDrawCommands_
            SkinnedPbr,   ///< skinnedPbrDrawCommands_
            CustomEffect  ///< customEffectDrawCommands_ (WEBGPU-76)
        };

        /**
         * @brief One queued draw's position in public call order.
         *
         * An INDEX, never a pointer: the family vectors grow while a bind cycle is being recorded,
         * and a pointer into one of them would dangle the moment it reallocated.
         */
        /**
         * @brief REMED-GFX-156: what one entry of the ordered stream actually is.
         *
         * Clear() is a public graphics command with an observable position among the draws, so it
         * belongs in the SAME monotonic stream rather than in a side channel consumed at pass
         * construction. `Draw` entries address a family command vector; `Clear` entries address
         * @ref clearCommands_ and @ref DrawOrderEntry::family is not read at all.
         */
        enum class OrderedKind : std::uint8_t
        {
            Draw,
            Clear
        };

        struct DrawOrderEntry
        {
            DrawFamily    family;  ///< Which vector @ref index addresses (Draw entries only).
            std::uint32_t index;   ///< Position inside that family's own command vector.
            /**
             * @brief This draw's public call number within the bind cycle.
             *
             * Redundant with the entry's own position while the stream is in public order, which
             * is exactly why it is stored: it is what lets the draw-order trace state the public
             * position and the issue position as two independently sourced numbers, so a replay
             * that reordered the stream reports them differing instead of relabelling itself.
             */
            std::uint32_t order;
            /// REMED-GFX-156: draw or ordered Clear(). Trailing, so existing three-field
            /// aggregate initialisations at the eleven draw-recording sites keep their meaning.
            OrderedKind kind = OrderedKind::Draw;
            /// WEBGPU-84: the occlusion query active when this draw was queued (nullptr = none).
            /// Trailing with a default, so the same three-field aggregate inits stay valid.
            WebGPUOcclusionQueryRenderer* occlusionQuery = nullptr;
        };

        /**
         * @brief REMED-GFX-156: one public GraphicsDevice.Clear() and the aspects it named.
         *
         * Values are captured HERE, at the public call, rather than read from the renderer's global
         * clearColor_/clearDepth_/clearStencil_ when the pass is built -- otherwise two Clear()s of
         * one bind cycle would both deliver the last one's colour, which is precisely how "the last
         * Clear wins" survived as a defect.
         */
        struct OrderedClearCommand
        {
            bool color = false;    ///< ClearOptions::Target was named.
            bool depth = false;    ///< ClearOptions::DepthBuffer was named.
            bool stencil = false;  ///< ClearOptions::Stencil was named.
            WGPUColor colorValue{0.0, 0.0, 0.0, 1.0};
            float depthValue = 1.0f;
            std::uint32_t stencilValue = 0;
        };

        /**
         * @brief REMED-GFX-156: one native render pass's worth of the ordered stream.
         *
         * wgpu delivers a clear only through a pass load action, and a pass has exactly one set of
         * them, so an ordered Clear() that follows a draw is a PASS BOUNDARY: the stream is cut
         * into segments, each of which is one native render pass whose load actions are the clear
         * the segment opens with and whose remaining attachments Load. Consecutive Clear()s with no
         * draw between them share a segment, so a run of clears still costs one pass.
         */
        struct PassSegmentPlan
        {
            OrderedClearCommand clear;   ///< Aspects this segment's pass must clear at load.
            std::size_t firstEntry = 0;  ///< First @ref drawOrder_ index this segment issues.
            std::size_t entryCount = 0;  ///< How many entries from @ref firstEntry it issues.
        };

        /**
         * @brief REMED-GFX-156: everything a pass recorder needs that differs per destination.
         *
         * The backbuffer, a RenderTarget2D and a RenderTargetCube face used to each build their own
         * attachment descriptors around an identical replay call; segmenting the stream means every
         * one of them must be able to build those descriptors once PER SEGMENT, so the parts that
         * actually differ are gathered here and the pass loop itself is written once.
         */
        struct PassDestination
        {
            WGPUTextureView colorView = nullptr;    ///< Slot 0 colour attachment (multisampled if any).
            WGPUTextureView resolveView = nullptr;  ///< Slot 0 resolve target, or null when 1x.
            WGPUTextureView depthView = nullptr;    ///< Depth/stencil attachment, or null. Shared by MRT.
            /// WEBGPU-39: the depth attachment's WGPU format (Undefined when depthView is null, i.e.
            /// DepthFormat::None). Pipelines built for this pass must declare exactly this format.
            WGPUTextureFormat depthFormat = WGPUTextureFormat_Depth24PlusStencil8;
            /// WEBGPU-39: whether depthFormat carries a stencil aspect (only Depth24PlusStencil8 does).
            bool depthHasStencil = true;
            WGPUTextureFormat colorFormat = WGPUTextureFormat_Undefined;  ///< Slot 0 colour format.
            std::uint32_t sampleCount = 1;
            int width = 0;
            int height = 0;
            /// RenderTargetUsage::DiscardContents (or PlatformContents resolving to it): the FIRST
            /// segment of the bind cycle clears whatever the game did not ask to clear itself.
            bool discardFirstSegment = false;
            const char* passLabel = "CNA WebGPU RenderPass";
            const char* traceName = "destination";
            /// WEBGPU-85 MRT: 1 (the slot-0 fields above describe the whole pass) or 2..4. For N>1 the
            /// EXTRA slots 1..N-1 live in the arrays below; slot 0 stays the single fields above, so
            /// every single-target caller is byte-identical. The depth attachment stays shared.
            int colorAttachmentCount = 1;
            std::array<WGPUTextureView, 4> mrtColorViews{};    ///< Slots 1..3 (index 0 unused).
            std::array<WGPUTextureView, 4> mrtResolveViews{};  ///< Slots 1..3 (index 0 unused).
            std::array<WGPUTextureFormat, 4> mrtColorFormats{};///< Slots 1..3 (index 0 unused).

            /** @brief The colour view for slot @p i (slot 0 is the single field). */
            [[nodiscard]] WGPUTextureView ColorViewAt(int i) const
            {
                return i == 0 ? colorView : mrtColorViews[static_cast<std::size_t>(i)];
            }
            /** @brief The resolve view for slot @p i (slot 0 is the single field). */
            [[nodiscard]] WGPUTextureView ResolveViewAt(int i) const
            {
                return i == 0 ? resolveView : mrtResolveViews[static_cast<std::size_t>(i)];
            }
            /** @brief The colour format for slot @p i (slot 0 is the single field). */
            [[nodiscard]] WGPUTextureFormat ColorFormatAt(int i) const
            {
                return i == 0 ? colorFormat : mrtColorFormats[static_cast<std::size_t>(i)];
            }
        };

        /**
         * @brief Cross-family state tracking for one ordered replay.
         *
         * Every family used to own the whole pass from its first draw to its last, so each could
         * assume nothing else had touched the encoder since. Interleaving removes that, and this
         * carries exactly the three pieces of pass state a neighbouring family can now disturb.
         */
        struct ReplayState
        {
            /// The pipeline currently bound by ANY family, so the sprite path's redundant-bind
            /// skip can never skip across a 3D draw that bound its own.
            WGPURenderPipeline boundPipeline = nullptr;
            /// Whether vertex slot 0 still holds the shared sprite vertex buffer. Every 3D draw
            /// binds its own vertex buffer into the same slot.
            bool spriteVerticesBound = false;
            /// Whether the blend constant is still the pass-level value. Sprites set their own per
            /// draw (REMED-GFX-102); 3D draws read the pass-level one, which before this change
            /// they were guaranteed because they always ran first.
            bool blendConstantIsPassDefault = true;
            /// REMED-GFX-172, trace only: the public enqueue position of the draw currently being
            /// issued. Carried here rather than added to every Issue* signature, and read by
            /// nothing but the multi-texture sampler trace.
            std::uint32_t publicOrder = 0;
            /// REMED-GFX-172, trace only: that draw's position within the replayed segment.
            std::size_t replayPosition = 0;
        };

        /**
         * @brief Name of a deferred draw family, for the draw-order trace.
         *
         * A family is exactly one shader/pipeline family here, so this doubles as the program
         * identity the trace reports.
         *
         * @param family The family to name.
         * @return A stable lower-camel identifier owned by the renderer.
         */
        [[nodiscard]] static const char* DrawFamilyName(DrawFamily family) noexcept;

        /**
         * @brief WEBGPU-115: refuses a draw that would consume the unsupported WireFrame fill.
         *
         * Called once at the top of each of this renderer's five public 3D draw entry points, which
         * is the narrowest boundary every one of the eleven Queue*Draw() command families passes
         * through. That placement is what makes the refusal safe: it runs before any command is
         * built or appended to @ref drawOrder_, before any pipeline key is computed, before any
         * WGPURenderPipeline is created, before a render pass is encoded and before anything is
         * submitted -- so a refused draw creates nothing, queues nothing, mutates no target and
         * leaves no partial native object behind. The next Solid draw is unaffected.
         *
         * ONLY POLYGON TOPOLOGIES ARE REFUSED. A fill mode selects how a POLYGON's interior is
         * rasterized; a line or point list has no interior, so `Solid` and `WireFrame` mean the
         * identical thing for them on every renderer and this renderer substitutes nothing. Refusing
         * those would delete a draw that is already correct -- the failure mode of an over-wide
         * guard, not a safety property. The refusal is scoped to exactly the case where the output
         * would otherwise be a solid fill standing in for a wireframe.
         *
         * @param primitive The topology the draw will rasterize.
         * @param route The route's name (for example `ordinary-indexed`), for the diagnostic.
         * @throws System::NotSupportedException When FillMode::WireFrame is the active fill mode
         *         and @p primitive is a polygon topology.
         */
        void RequireSupportedFillModeEXT(PrimitiveType primitive, const char* route) const;

        /**
         * @brief WEBGPU-200: refuses a filtering sample of a 32-bit float target by name.
         *
         * `WGPUFeatureName_Float32Filterable` is what makes a filtering sampler legal on an
         * R32/RG32/RGBA32Float texture. Without it wgpu-native raises a validation error naming the
         * native format, which is true but names neither the XNA `SurfaceFormat` the caller chose
         * nor the `SamplerState` that made the sample a filtering one.
         *
         * @param params The draw's parameters, whose texture slots are examined.
         * @param route The public route name, for the diagnostic.
         */
        void RequireFloat32SamplingAllowedEXT(const GpuDrawParams& params, const char* route) const;

        /** @brief Appends @p index of @p family to the ordered stream at its public call. */
        void RecordDrawOrder(DrawFamily family, std::size_t index);

        /**
         * @brief Replays one pass segment's queued draws into @p pass in public call order.
         *
         * Walks the segment's slice of the ordered stream once, dispatching each entry to its
         * family's own single-command issue function. REMED-GFX-156: the slice, and the clearing of
         * the command vectors afterwards, moved out to @ref ReplayOrderedSegments -- the vectors
         * have to outlive every segment of the cycle, not just the first.
         *
         * @param pass The open render pass encoder for this segment.
         * @param targetFormat The colour format sprite pipelines must have been built against.
         * @param targetSampleCount The sample count sprite pipelines must have been built against.
         * @param destination Which of the three pass recorders is replaying, for the order trace.
         * @param firstEntry First @ref drawOrder_ index belonging to this segment.
         * @param entryCount How many entries from @p firstEntry belong to it.
         */
        void ReplayDrawsInOrder(WGPURenderPassEncoder pass, const PassDestination& destination,
                                std::size_t firstEntry, std::size_t entryCount);

        /**
         * @brief REMED-GFX-156: cuts the ordered stream into one plan per native render pass.
         *
         * Pure bookkeeping over @ref drawOrder_ and @ref clearCommands_ -- no GPU calls -- so the
         * segmentation can be reasoned about, traced and asserted independently of any encoder.
         *
         * @return At least one segment, always: a bind cycle that queued nothing still records the
         *         one pass that applies its RenderTargetUsage and any Clear() it did issue.
         */
        [[nodiscard]] std::vector<PassSegmentPlan> BuildPassSegments() const;

        /**
         * @brief REMED-GFX-156: records this bind cycle's whole ordered stream into @p encoder.
         *
         * One native render pass per segment, each opened with the load actions its own segment
         * asks for and Load for every attachment it does not, then replayed in public order. Every
         * pass reopens the full pass state (viewport, scissor, blend constant, stencil reference)
         * and every draw reapplies its own captured state, because none of it survives the end of a
         * render pass. Consumes the stream: the family vectors, @ref clearCommands_ and
         * @ref drawOrder_ are all empty on return.
         *
         * @param encoder The command encoder this bind cycle is being recorded into.
         * @param destination Attachments, extents and usage policy of the bound destination.
         */
        void ReplayOrderedSegments(WGPUCommandEncoder encoder, const PassDestination& destination);
        /// REMED-GFX-167: drops the ordered stream and every family vector together, releasing the
        /// native references their commands hold. Called at the tail of a replay and, crucially,
        /// FIRST in the destructor -- these vectors are members, so left populated they would
        /// release textures after the destructor body has already released the device.
        void DiscardQueuedCommands();

        /**
         * @brief REMED-GFX-156: appends one public Clear() to the ordered stream.
         *
         * @param color True when ClearOptions::Target was named.
         * @param depth True when ClearOptions::DepthBuffer was named.
         * @param stencil True when ClearOptions::Stencil was named.
         */
        void RecordOrderedClear(bool color, bool depth, bool stencil);

        /** @brief Drops queued sprites, and their ordered-stream entries, without replaying them. */
        void DiscardQueuedSprites();

        /** @brief Uploads every queued sprite's vertices into the shared sprite vertex buffer. */
        void UploadSpriteVertices();

        /**
         * @brief Restores the pass state a preceding sprite may have changed, before a 3D draw.
         *
         * @param pass The open render pass encoder.
         * @param state The running cross-family state for this replay.
         */
        void Begin3DDrawState(WGPURenderPassEncoder pass, ReplayState& state);

        // REMED-GFX-116: the target extent used to be passed in only to clamp the per-sprite
        // viewport; that clamp now lives in ApplyDrawViewport(), against the pass state
        // BeginPassViewport() recorded, so every family clamps identically.
        void IssueSprite(WGPURenderPassEncoder pass, const SpriteCommand& command,
                         std::uint32_t spriteIndex, WGPUTextureFormat targetFormat,
                         std::uint32_t targetSampleCount, ReplayState& state);
        /// WEBGPU-142: replays one sprite whose `SpriteBatch.Begin` bound a custom WGSL ShaderEffect
        /// -- builds/fetches a pipeline from the effect's modules with the fixed SpriteVertex layout
        /// (position/uv/color, no matrices: sprite vertices are already NDC) and binds
        /// {uniform block @0, the sprite's sampler @1, the sprite's texture @2}. Slot 0 carries the
        /// sprite's blend; extra MRT slots write nothing (a sprite effect writes @location(0)).
        void IssueSpriteWithCustomEffect(WGPURenderPassEncoder pass, const SpriteCommand& command,
                                         std::uint32_t spriteIndex, WGPUTextureFormat targetFormat,
                                         std::uint32_t targetSampleCount, ReplayState& state);
        /// WEBGPU-142: the custom-WGSL ShaderEffect bound by the currently-open `SpriteBatch.Begin`
        /// (set via `WebGPUSpriteBatchRenderer::SetCustomEffect`), captured into each queued sprite.
        WebGPUEffectRenderer* activeSpriteCustomEffect_ = nullptr;
        /**
         * @brief REMED-GFX-116: the complete GraphicsDevice.Viewport as it stands right now.
         *
         * Called by every Queue*Draw()/QueueSprite() at the public draw call, so the returned
         * value can never observe a later SetViewport().
         *
         * @return A by-value snapshot of all six XNA Viewport fields.
         */
        [[nodiscard]] WebGPUViewportSnapshot CaptureViewport() const noexcept;
        /**
         * @brief REMED-GFX-116: starts a render pass's viewport bookkeeping.
         *
         * Sets the pass viewport to the whole target -- deliberately NOT to the live renderer
         * viewport, which is the state every deferred draw must be independent of -- and records
         * it as the currently applied value. Every draw then applies its own captured snapshot.
         *
         * @param pass The render pass encoder that was just begun.
         * @param targetWidth Width in pixels of the pass's colour target.
         * @param targetHeight Height in pixels of the pass's colour target.
         */
        void BeginPassViewport(WGPURenderPassEncoder pass, int targetWidth, int targetHeight);
        /**
         * @brief REMED-GFX-116: makes one queued command's captured Viewport current.
         *
         * Clamps the snapshot to the pass target exactly as the pass-level code always did, then
         * calls wgpuRenderPassEncoderSetViewport only when the resulting six values differ from
         * what the pass already has.
         *
         * @param pass The render pass encoder being recorded.
         * @param snapshot The Viewport captured when the draw was queued.
         */
        void ApplyDrawViewport(WGPURenderPassEncoder pass, const WebGPUViewportSnapshot& snapshot);
        /**
         * @brief REMED-GFX-146: the complete scissor state as it stands right now.
         *
         * Called by every Queue*Draw()/QueueSprite() at the public draw call, so the returned value
         * can never observe a later SetScissorRect() or ApplyRasterizerState().
         *
         * @return A by-value snapshot of ScissorTestEnable and all four rectangle components.
         */
        [[nodiscard]] WebGPUScissorSnapshot CaptureScissor() const noexcept;
        /**
         * @brief REMED-GFX-146: starts a render pass's scissor bookkeeping.
         *
         * Sets the pass scissor to the whole target -- deliberately NOT to the live renderer
         * rectangle, which is the state every deferred draw must be independent of -- and records
         * it as the currently applied value. Every draw then applies its own captured snapshot.
         * WebGPU has no separate "scissor test enable" toggle, so the whole attachment extent is
         * what "disabled" means here.
         *
         * @param pass The render pass encoder that was just begun.
         * @param targetWidth Width in pixels of the pass's colour target.
         * @param targetHeight Height in pixels of the pass's colour target.
         */
        void BeginPassScissor(WGPURenderPassEncoder pass, int targetWidth, int targetHeight);
        /**
         * @brief REMED-GFX-146: makes one queued command's captured scissor state current.
         *
         * A disabled scissor test resolves to the whole target. An enabled one is clipped to the
         * attachment in signed arithmetic before the unsigned native call, so a negative or
         * oversized public rectangle can never wrap. Calls wgpuRenderPassEncoderSetScissorRect only
         * when the resulting four values differ from what the pass already has.
         *
         * @param pass The render pass encoder being recorded.
         * @param snapshot The scissor state captured when the draw was queued.
         */
        void ApplyDrawScissor(WGPURenderPassEncoder pass, const WebGPUScissorSnapshot& snapshot);

        /**
         * @brief plans/plan_webgpu.md WEBGPU-155: which stock shader family one draw is bound to.
         *
         * Chosen from the DECLARATION's semantics plus the effect state, never from the buffer's
         * byte stride: 24 bytes is `VertexPositionColorTexture` and also `Position+Normal`, 32 is
         * `VertexPositionNormalTexture` and also a padded `Position+Colour`, and the stock
         * `ModelProcessor` emits a 36-byte vertex that used to match nothing at all. A declaration
         * is the only thing that answers "is this a lit vertex" or "does it carry a second UV set".
         */
        enum class StockVertexShapeEXT
        {
            Colored,             ///< Position + Color.
            Textured,            ///< Position + TexCoord0.
            ColoredTextured,     ///< Position + Color + TexCoord0.
            /// Position + Normal, with TexCoord0 and Color optional -- per-pixel lighting.
            ///
            /// WEBGPU-156/157: this ONE family covers `VertexPositionNormal` (stride 24),
            /// `VertexPositionNormalTexture` (32) and the stock `ModelProcessor`'s
            /// Position+Normal+Colour+UV vertex (36). It does not need three shaders, because a
            /// semantic the declaration does not name is supplied by the neutral record and the
            /// existing `textureEnabled` / `vertexColorEnabled` uniforms already gate its use.
            Lit,
            LitVertexLit,        ///< The Gouraud sibling of Lit.
            AlphaTest,           ///< Position + TexCoord0, alpha-tested.
            AlphaTestColored,    ///< Position + Color + TexCoord0, alpha-tested.
            DualTextured,        ///< Position + TexCoord0 + TexCoord1.
            DualTexturedColored, ///< Position + Color + TexCoord0 + TexCoord1.
            EnvMapped,           ///< Position + Normal + TexCoord0, reflection-mapped.
            /// WEBGPU-172: Position + the per-instance world matrix as TEXCOORD1..TEXCOORD4.
            ///
            /// Never returned by `SelectStockVertexShapeEXT` -- the instanced route is entered
            /// through `DrawInstancedPrimitivesEx`, not chosen from a declaration -- but resolved by
            /// exactly the same code as every other family, which is what lets an instance stream
            /// be split across bindings.
            Instanced,
            InstancedColored,    ///< The Instanced family with a per-vertex COLOR0.
            /// Not converted by WEBGPU-155: skinned and PBR draws keep the stride-derived layout
            /// and the REMED-GFX-DECL-GUARD refusal that makes it safe. See WEBGPU-177.
            StrideDerived
        };

        /**
         * @brief WEBGPU-155: picks the stock family for a draw from its declaration and effect state.
         *
         * WEBGPU-172 widened the question from one declaration to the whole bound set: a semantic
         * counts as present when ANY per-vertex stream declares it, because `SetVertexBuffers` lets
         * a vertex's elements be split across bindings and asking only slot 0 would pick an unlit
         * program for a mesh whose normals are simply in another buffer.
         *
         * @param streams The draw's per-vertex streams, in public binding order. Never null.
         * @param streamCount How many entries @p streams holds; at least 1.
         * @param stride Stream 0's byte stride, used only for the families this task did not convert.
         * @param params The draw's effect state.
         * @return The selected family, or `StrideDerived` when the draw belongs to an unconverted route.
         */
        [[nodiscard]] static StockVertexShapeEXT SelectStockVertexShapeEXT(
            const CNA::Internal::Graphics::StockVertexStreamEXT* streams,
            std::size_t streamCount,
            std::size_t stride,
            const GpuDrawParams& params);

        /**
         * @brief WEBGPU-172: one bound per-vertex stream, as the queue path needs it.
         *
         * The resolver only wants a declaration and a stride; capturing the bytes also needs the
         * buffer itself and the binding's own `VertexOffset`, so both views are collected together
         * and stay index-aligned.
         */
        /**
         * @brief WEBGPU-172: one bound vertex stream, captured at queue time.
         *
         * This renderer is deferred: a queued draw owns a copy of the bytes it will read, because
         * the public buffer may be written again before the frame is flushed. A multi-stream draw
         * therefore needs one copy per stream, each already sliced by ITS own stride and
         * `VertexBufferBinding.VertexOffset` -- which is also why the replay binds every stream at
         * native byte offset zero and needs no per-binding offset channel.
         */
        struct CapturedVertexStreamEXT
        {
            /** @brief The stream's bytes, from its own first selected record onward. */
            std::vector<std::uint8_t> data;
            /** @brief The stream's own byte stride. Never another stream's. */
            std::uint32_t stride = 0;
        };

        struct PerVertexStreamSourceEXT
        {
            const WebGPUVertexBufferRenderer* buffer = nullptr;  ///< The stream's buffer. Never null.
            int stride = 0;              ///< The binding's own stride.
            int vertexOffset = 0;        ///< `VertexBufferBinding.VertexOffset`, in elements.
            int slot = 0;                ///< The public `SetVertexBuffers` slot, for diagnostics.
            int instanceFrequency = 0;   ///< `InstanceFrequency`; 0 means the stream steps per vertex.
            int vertexCount = 0;         ///< Records the stream's buffer holds, for the range gate.
        };

        /** @brief WEBGPU-172: every per-vertex stream of one draw, in public binding order. */
        struct DrawVertexStreamsEXT
        {
            /// The declarations, as `ResolveStockVertexLayoutAcrossStreamsEXT` wants them.
            std::array<CNA::Internal::Graphics::StockVertexStreamEXT,
                       CNA::Internal::Graphics::kMaxStockVertexStreamsEXT> declarations{};
            /// The matching buffers and offsets, at the same indices.
            std::array<PerVertexStreamSourceEXT,
                       CNA::Internal::Graphics::kMaxStockVertexStreamsEXT> sources{};
            /**
             * @brief Storage for a stream whose buffer carries no declaration of its own.
             *
             * The low-level `IGraphicsRenderer::CreateVertexBuffer(count)` entry point makes
             * buffers with no declaration at all, and the instanced examples use it. When a stream
             * arrives that way a canonical declaration is synthesized here and
             * `declarations[i].elements` points at this entry, which is why this struct is filled
             * through an out-parameter and never returned by value: moving it would move the vector
             * objects these pointers name.
             */
            std::array<std::vector<Microsoft::Xna::Framework::Graphics::VertexElement>,
                       CNA::Internal::Graphics::kMaxStockVertexStreamsEXT> synthesized{};
            /// How many entries of the arrays are valid; at least 1.
            std::size_t count = 0;
        };

        /**
         * @brief WEBGPU-172: collects a draw's per-vertex streams.
         *
         * A draw that bound no `GpuDrawParams` -- the legacy `DrawColoredPrimitives` entry points --
         * has exactly one stream, which is @p vb itself; that is the single-stream shape this
         * renderer has always had, spelled through the same type so there is one code path.
         *
         * @param vb The buffer named by the draw call, which is always stream 0.
         * @param params The draw's parameters, or null for a legacy entry point.
         * @param includePerInstance Whether per-INSTANCE bindings are collected too. False on the
         *        ordinary routes, whose pipelines have no instance step mode at all; true on the
         *        instanced route, where an instance binding is simply a stream whose
         *        `InstanceFrequency` is greater than zero.
         * @return The collected streams; `count` is at least 1.
         */
        static void CollectPerVertexStreamsEXT(
            const WebGPUVertexBufferRenderer& vb, const GpuDrawParams* params,
            DrawVertexStreamsEXT& out, bool includePerInstance = false);

        /**
         * @brief WEBGPU-172: fills in a canonical declaration for any stream that carries none.
         *
         * Only the instanced route needs this, and only because its examples build their buffers
         * through the low-level `CreateVertexBuffer(count)` entry point, which propagates no
         * declaration. A per-vertex stream gets the canonical layout for its stride (position-only
         * for a stride the table does not list, which is what this route has always assumed); a
         * per-instance stream gets the four `Vector4` world-matrix columns at 0/16/32/48, which is
         * exactly the attribute array the family used to hardcode.
         *
         * @param streams The collected streams, modified in place.
         */
        static void SynthesizeMissingStreamDeclarationsEXT(DrawVertexStreamsEXT& streams);

        /**
         * @brief WEBGPU-172: the instanced family's resolved layout.
         *
         * Its per-VERTEX inputs are resolved by semantic like every other family. Its per-INSTANCE
         * columns are **not**: they are taken positionally, the k-th element across the
         * concatenated per-instance declarations feeding world-matrix column k. That is the
         * reference renderer's own rule (`EasyGLRenderer::PlaceInstanceStreams` assigns consecutive
         * locations from `kStockInstanceBaseLocation` in declaration order, whatever semantic each
         * element names), and it is the only rule both existing corpora satisfy: the shared oracle
         * spells the columns `TEXCOORD1`-`TEXCOORD4` while this renderer's own instanced examples
         * spell them `POSITION1`-`POSITION4`. A world matrix has no XNA-defined semantic to be
         * faithful to, so matching the reference is the answer rather than picking one spelling and
         * breaking the other.
         *
         * @param streams The collected streams, already given synthesized declarations.
         * @param hasVertexColor Whether a per-vertex stream declares COLOR0.
         * @return The resolved layout.
         */
        [[nodiscard]] static CNA::Internal::Graphics::ResolvedStockVertexLayoutEXT
        ResolveInstancedVertexLayoutEXT(const DrawVertexStreamsEXT& streams, bool hasVertexColor);

        /**
         * @brief WEBGPU-155: the ordered input list one stock family declares.
         *
         * @param shape The family.
         * @param inputs Receives the input list, in shader-location order.
         * @param count Receives the number of inputs.
         * @param programName Receives the family's WGSL name, for a diagnostic.
         */
        static void StockVertexInputsForShapeEXT(
            StockVertexShapeEXT shape,
            const CNA::Internal::Graphics::StockProgramInput*& inputs,
            std::size_t& count,
            const char*& programName);

        /**
         * @brief WEBGPU-155: validates and resolves one draw's vertex layout against its declaration.
         *
         * Raises the shared format diagnostic first (nothing native exists yet if it throws), then
         * binds each of the family's inputs at the declared element's own offset and format. A
         * buffer that carries no declaration keeps this renderer's historical stride-derived layout,
         * which is what every existing stock draw already relies on.
         *
         * @param vb The draw's vertex buffer -- stream 0, and the only stream of a legacy draw.
         * @param streams The draw's per-vertex streams, from `CollectPerVertexStreamsEXT`.
         * @param streamCount How many entries @p streams holds.
         * @param shape The selected stock family.
         * @return The resolved layout.
         */
        [[nodiscard]] CNA::Internal::Graphics::ResolvedStockVertexLayoutEXT ResolveStockVertexLayoutForDrawEXT(
            const WebGPUVertexBufferRenderer& vb,
            const CNA::Internal::Graphics::StockVertexStreamEXT* streams,
            std::size_t streamCount,
            StockVertexShapeEXT shape) const;

        /**
         * @brief WEBGPU-172: copies each resolved stream's own bytes into a queued draw.
         *
         * Stream 0 lands in @p stream0Data, exactly as before this task, so a single-stream draw's
         * captured bytes are byte-identical to their pre-172 form and @p extra stays empty. Every
         * further resolved stream is sliced by ITS own stride and `VertexOffset`, so the replay can
         * bind all of them at native byte offset zero.
         *
         * @param layout The draw's resolved layout, whose stream table says which bindings are read.
         * @param streams The draw's collected per-vertex streams.
         * @param vertexStart The draw's shared first-vertex, already folded out of the per-stream
         *        offsets by `GraphicsDevice`.
         * @param stream0Data Receives the bytes of the layout's first resolved stream.
         * @param extra Receives one entry per further resolved stream, in native slot order.
         * @param instanceCount How many instances the draw submits. A per-instance stream is
         *        materialized as exactly this many records, because WebGPU's `Instance` step mode
         *        has an implicit divisor of one and no step rate: XNA's `InstanceFrequency` is
         *        honoured by repeating each source record @c frequency times here, which keeps the
         *        divisor a data-preparation concern that never reaches the pipeline or its key.
         */
        static void CaptureStockVertexStreamsEXT(
            const CNA::Internal::Graphics::ResolvedStockVertexLayoutEXT& layout,
            const DrawVertexStreamsEXT& streams,
            int vertexStart,
            std::vector<std::uint8_t>& stream0Data,
            std::vector<CapturedVertexStreamEXT>& extra,
            int instanceCount = 1);

        /**
         * @brief WEBGPU-172: binds a replayed draw's further vertex streams at slots 1..n.
         *
         * Slot 0 is bound by the caller, which already owns the transient buffer for it. Each
         * further stream gets its own transient buffer here, released with the frame like every
         * other deferred allocation.
         *
         * @param pass The render pass encoder being recorded.
         * @param label A debug label for the created buffers.
         * @param extra The draw's captured further streams; empty for a single-stream draw.
         */
        void BindExtraVertexStreamsEXT(WGPURenderPassEncoder pass, const char* label,
                                       const std::vector<CapturedVertexStreamEXT>& extra);

        /// WEBGPU-155: the assembled native vertex state for one resolved layout. Two buffer slots:
        /// slot 0 is the draw's own vertex record, slot 1 the zero-stride neutral record that
        /// supplies any semantic the declaration does not name.
        struct StockVertexStateEXT
        {
            /**
             * @brief Every record attribute, grouped by the native slot that reads it.
             *
             * WEBGPU-172: one `WGPUVertexBufferLayout` may only point at a contiguous run of
             * attributes, so the attributes are grouped here rather than kept in shader-location
             * order. A single-stream draw has exactly one group and is therefore laid out
             * identically to its pre-172 form.
             */
            std::array<WGPUVertexAttribute,
                       CNA::Internal::Graphics::kMaxStockVertexAttributes> recordAttributes{};
            std::array<WGPUVertexAttribute,
                       CNA::Internal::Graphics::kMaxStockVertexAttributes> neutralAttributes{};
            /// One per resolved stream, plus one for the neutral record when the draw needs it.
            std::array<WGPUVertexBufferLayout,
                       CNA::Internal::Graphics::kMaxStockVertexStreamsEXT + 1> buffers{};
            std::size_t bufferCount = 0;
        };


        /**
         * @brief WEBGPU-155: turns a resolved layout into `WGPUVertexBufferLayout`s for a pipeline.
         *
         * @param layout The resolved layout.
         * @param out Receives the assembled attribute arrays and buffer layouts; it must outlive the
         *            `Build3DPipelineEXT` call, because the descriptor points into it.
         */
        static void BuildStockVertexStateEXT(
            const CNA::Internal::Graphics::ResolvedStockVertexLayoutEXT& layout,
            StockVertexStateEXT& out);

        /**
         * @brief WEBGPU-155: the shared zero-stride buffer holding the `(0, 0, 0, 1)` neutral record.
         *
         * Created once and reused, because a neutral value is the same for every draw. WebGPU's own
         * `arrayStride = 0` is what makes every vertex read it.
         *
         * @return The neutral vertex buffer.
         */
        [[nodiscard]] WGPUBuffer GetOrCreateNeutralVertexBufferEXT();

        /**
         * @brief WEBGPU-155: binds the neutral record when a draw's layout needs it.
         *
         * WEBGPU-172: at the slot AFTER the draw's real streams, not at slot 1 -- a two-stream draw
         * uses slots 0 and 1 for its own buffers, so the neutral record moves to slot 2. Its slot is
         * `layout.streamCount`, which is 1 for every single-stream draw and therefore leaves that
         * case exactly where it was.
         *
         * @param pass The render pass encoder being recorded.
         * @param layout The draw's resolved layout.
         */
        void BindNeutralVertexBufferEXT(
            WGPURenderPassEncoder pass,
            const CNA::Internal::Graphics::ResolvedStockVertexLayoutEXT& layout);

        /// WEBGPU-29: the inputs that vary between the 12 GetOrCreatePipeline*3D families. Everything
        /// else in a 3D WGPURenderPipelineDescriptor -- colour target (surfaceFormat_ + CurrentWriteMask
        /// + MRT via InitStockColorTargetsEXT), fragment/vertex entry points, CCW front face,
        /// ToWGPUCullMode, the baked blend/multisample/depth-bias/stencil state and the depth-format
        /// gating -- is identical across all 12 and is assembled by Build3DPipelineEXT. The per-family
        /// vertex layout, cache key and cache map stay in each GetOrCreatePipeline* function.
        struct Pipeline3DDescEXT
        {
            const char* label = nullptr;                 ///< Pipeline debug label.
            WGPUPipelineLayout layout = nullptr;         ///< The family's bind-group pipeline layout.
            WGPUShaderModule vertexModule = nullptr;     ///< vs_main provider.
            WGPUShaderModule fragmentModule = nullptr;   ///< fs_main provider (often the same module).
            const WGPUVertexBufferLayout* vertexBuffers = nullptr;  ///< Already-assembled layout(s).
            std::size_t vertexBufferCount = 0;           ///< 1 for most, 2 for the instanced family.
            WGPUPrimitiveTopology topology = WGPUPrimitiveTopology_TriangleList;
            WGPUIndexFormat stripIndexFormat = WGPUIndexFormat_Undefined;
            bool depthTest = true;
            bool depthWrite = true;
            int depthFunc = 0;
            float depthBias = 0.0f;
            float slopeScaleDepthBias = 0.0f;
            bool blend = false;
            BlendKeyParams blendParams{};
            int cullMode = 0;
            StencilKeyParams stencil{};
        };
        /// WEBGPU-29: assembles the shared 3D WGPURenderPipelineDescriptor from @p desc and creates
        /// the pipeline. Byte-identical to the former per-function assembly. Throws on failure.
        [[nodiscard]] WGPURenderPipeline Build3DPipelineEXT(const Pipeline3DDescEXT& desc);

        [[nodiscard]] WGPURenderPipeline GetOrCreatePipelineColored3D(WGPUPrimitiveTopology topology,
                                                                       WGPUIndexFormat stripIndexFormat,
                                                                       bool depthTest, bool depthWrite,
                                                                       int depthFunc,
                                                       bool blend, const BlendKeyParams& blendParams,
                                                       int cullMode, bool wireframe,
                                                       float depthBias, float slopeScaleDepthBias,
                                                       const StencilKeyParams& stencil,
                                                       const CNA::Internal::Graphics::ResolvedStockVertexLayoutEXT& vertexLayout);  // WEBGPU-83
        // params == nullptr: the legacy DrawColoredPrimitives path (hardcoded white diffuse,
        // vertexColorEnabled=true, vertexStart=0). params != nullptr: DrawPrimitivesEx's real
        // GpuDrawParams dispatch (stride-16 only -- caller must have already verified the stride).
        void QueueColoredDraw(const IVertexBufferRenderer& vb, const IIndexBufferRenderer* ib,
                              const Matrix& world, const Matrix& view, const Matrix& projection,
                              PrimitiveType primitive, int primitiveCount,
                              const GpuDrawParams* params = nullptr,
                              StockVertexShapeEXT shape = StockVertexShapeEXT::Colored);
        void CaptureReadback(WGPUCommandEncoder encoder, WGPUTexture surfaceTexture);
        // Acquires a swapchain texture if none is currently held, and renders any pending
        // Clear()/sprite work into it. Called on demand by both Present() and ReadBackbuffer()
        // so that GetBackBufferData() can observe work queued earlier in the same logical frame,
        // matching the Vulkan/Bgfx renderers' own on-demand-submit readback semantics. Returns
        // false if the surface isn't presentable right now (minimized, lost, etc).
        bool EnsureFrameRendered();
        // WEBGPU-133: releases the currently acquired canvas surface texture and marks it re-
        // acquirable. Used on Emscripten after a readback wait yields to the browser (which presents
        // and invalidates the current texture), so the next flush obtains a fresh one.
        void DiscardAcquiredBackbuffer();
        // WEBGPU-53/54: renders every currently queued Clear()/3D-draw/SpriteBatch command into a
        // single render pass targeting the given RenderTarget2D's own colour+depth attachments,
        // then submits and clears every per-frame draw-command queue -- the RT-targeting sibling
        // of EnsureFrameRendered()'s swapchain-targeting pass. See SetRenderTarget2D()'s own
        // comment for why this "flush eagerly on every target switch" design (rather than tagging
        // every draw command with a target and replaying grouped-by-target at final-flush time,
        // closer to VulkanRenderer's own deferred-recording model) was chosen.
        void RenderPendingDrawsToRenderTarget(WebGPURenderTargetRenderer* target);
        // WEBGPU-114: the cube-face counterpart of RenderPendingDrawsToRenderTarget() -- same
        // "one deferred render pass, flushed the instant the bound target changes" model, just
        // targeting one face's own ColorAttachmentView(face)/the shared DepthView() instead of a
        // RenderTarget2D's own single pair.
        void RenderPendingDrawsToRenderTargetCubeFace(WebGPURenderTargetCubeRenderer* target, int face);
        // WEBGPU-114: single shared "flush whatever is currently bound" entry point -- used by
        // both SetRenderTarget2D() and WebGPURenderTargetCubeRenderer::BindAsRenderTargetFace() so
        // switching between ANY combination of {backbuffer, RenderTarget2D, a RenderTargetCube
        // face} always flushes the OUTGOING target's own pending draws into its own render pass
        // first, exactly like the pre-existing RenderTarget2D-only switch logic did. Does not
        // itself clear/reassign currentRenderTarget_/currentRenderTargetCubeFace_ -- callers own
        // that, mirroring the pre-existing SetRenderTarget2D() call-then-assign structure.
        void FlushCurrentRenderTarget();
        [[nodiscard]] LogicalViewport ComputeLogicalViewport() const;

    public:
        /**
         * @brief Whether a game `Draw`/`Present` pair may begin.
         *
         * `WEBGPU-181`. False while the device is lost, and on this renderer that is a safety
         * mechanism rather than a convenience: `WEBGPU-180` measured that
         * `wgpuSurfaceGetCurrentTexture` on a surface whose device is lost does not return a failure
         * status -- it panics inside wgpu-native and, because the panic cannot unwind across the C
         * ABI, aborts the process. There is nothing to catch, so the loss has to be a gate BEFORE
         * the acquire.
         *
         * @return True when the device is usable.
         */
        [[nodiscard]] bool CanBeginDrawEXT() const override { return !deviceLost_; }

        /**
         * @brief Turns CPU shadow retention for context recovery on or off.
         *
         * `WEBGPU-181`. With recovery enabled (the default) each texture renderer keeps a reference
         * to the buffer `Texture2D`/`TextureCube` already owns, so a device recreate has bytes to
         * re-upload without the game re-supplying them. Disabling it drops those references and
         * stops future ones being taken, which is the whole cost of recovery on this renderer --
         * no copy is ever made either way, only a `shared_ptr` held or not held.
         *
         * @param enabled True to retain CPU shadows.
         */
        void SetContextRecoveryEnabled(bool enabled) override;

        /** @brief Whether CPU shadows are being retained. @return The current setting. */
        [[nodiscard]] bool IsContextRecoveryEnabledEXT() const { return contextRecoveryEnabled_; }

    private:
        /// WEBGPU-181: set while the device is unusable; the only thing standing between a lost
        /// device and the process abort WEBGPU-180 measured. WEBGPU-182 is what will set it from a
        /// real destroy and clear it from a real recreate.
        bool deviceLost_ = false;
        /// WEBGPU-181: whether texture renderers keep a reference to the framework's CPU pixels.
        bool contextRecoveryEnabled_ = true;


        /**
         * @brief The logical viewport a given PHYSICAL surface size would produce.
         *
         * Split out of @ref ComputeLogicalViewport for `WEBGPU-178`: the size the renderer has
         * last CONFIGURED and the size the platform has last REPORTED are not the same thing for
         * one call, and `GetViewportSize()` has to answer with the reported one.
         *
         * @param physicalWidth The surface width to reason about, in physical pixels.
         * @param physicalHeight The surface height to reason about, in physical pixels.
         * @return The presentation rectangle and logical extent for that surface.
         */
        [[nodiscard]] LogicalViewport ComputeLogicalViewportForEXT(int physicalWidth,
                                                                   int physicalHeight) const;
        // WEBGPU-82: full SamplerState cache (filter + address mode + anisotropy), keyed on the
        // COMPLETE description and filled by FillWGPUSamplerDescriptor's explicit ordinal table.
        // REMED-GFX-170: this is now the renderer's ONE sampler translation. SpriteBatch used to
        // resolve its own with a binary `filter == 0 ? Linear : Nearest`, and to cache it in an
        // 18-entry array whose index collapsed every non-Linear ordinal onto one slot, so the
        // descriptor and the cache key were wrong in the same way. `family` names the public draw
        // family for CNA_WEBGPU_SAMPLER_TRACE and has no effect on the cache key.
        [[nodiscard]] WGPUSampler GetOrCreateSlotSampler(int filter, int addressU, int addressV,
                                                        int addressW, int maxMipLevel,
                                                        int maxAnisotropy, const char* family);
        /** @brief Releases every native sampler in slotSamplerCache_ and empties it. */
        void ReleaseSamplerCache();
        [[nodiscard]] WGPUPrimitiveTopology ToTopology(PrimitiveType primitive) const;
        [[nodiscard]] int PrimitiveVertexCount(PrimitiveType primitive, int primitiveCount) const;
        [[nodiscard]] int PrimitiveIndexCount(PrimitiveType primitive, int primitiveCount) const;

        // WEBGPU-52: real, genuinely-linear-filtered mip generation for a plain Texture2D/
        // TextureCube, via a render pass per mip level that draws a full-screen triangle sampling
        // the PREVIOUS level through a real linear WGPUSampler into the NEXT level's own
        // render-attachment view -- wgpu-native has no vkCmdBlitImage-equivalent filtered-
        // downsample primitive (verified against this project's pinned wgpu-native v29.0.1.1
        // webgpu.h: wgpuCommandEncoderCopyTextureToTexture is a raw same-size copy, nothing else
        // resembling a blit exists), so this is the same technique other WebGPU-based engines use.
        // IMPORTANT, DELIBERATE DIVERGENCE (documented, not silently introduced): unlike every
        // other CNA graphics renderer (VulkanRenderer/EasyGLRenderer), which only
        // regenerate mip content for a RENDER TARGET being unbound (matching real FNA3D's
        // OPENGL_ResolveTarget/vkCmdBlitImage-on-unbind semantics -- a PLAIN Texture2D/TextureCube
        // never auto-generates mip content from level 0 in FNA/XNA itself, matching every other
        // CNA renderer's own plain-texture behaviour too), this WebGPU-only helper DOES generate
        // real mip content for a plain Texture2D/TextureCube automatically whenever mipMap=true
        // AND non-empty initial pixel data is supplied at construction time. This makes WebGPU's
        // plain-texture mip behaviour genuinely BETTER (never garbage/undefined content at
        // level>0) but ALSO genuinely DIFFERENT from FNA and every sibling CNA renderer for the
        // identical public Texture2D/TextureCube(mipMap=true) constructor call -- see
        // plans/plan_webgpu.md's WEBGPU-52 row and docs/webgpu-renderer.md for the full investigation
        // this finding is based on. Explicit per-level SetData(level>0, ...) calls made AFTER
        // construction are, as before, never auto-regenerated (matches every other renderer).
        // WEBGPU-114: the blit pipeline's colour-target format must equal the texture being
        // downsampled. A plain Texture2D/TextureCube is always RGBA8Unorm, but a RenderTargetCube
        // is surfaceFormat_ (often BGRA), so the pipeline is cached per format (mipBlitPipelines_).
        // The shader/bind-group-layout/pipeline-layout/sampler are format-independent, created once.
        [[nodiscard]] WGPURenderPipeline EnsureMipBlitPipeline(WGPUTextureFormat format);
        /// Creates the format-independent mip-blit shader/layouts/sampler exactly once (guarded on
        /// mipBlitShader_ == nullptr by the caller). Split out of EnsureMipBlitPipeline so the
        /// per-format pipeline cache does not re-run this.
        void EnsureMipBlitSharedResources();
        // Shared implementation: generates levels 1..mipLevels-1 of ONE array layer of `texture`
        // from that layer's own level 0, sampling/rendering in `format`. GenerateMips2D()/
        // GenerateMipsCubeFace() are thin wrappers (layer=0 for a plain 2D texture, layer=face for
        // one cube face).
        void GenerateMipsForLayer(WGPUTexture texture, int layer, int width, int height,
                                  int mipLevels, WGPUTextureFormat format);
        // Generates levels 1..mipLevels-1 of a plain (non-array) 2D texture from level 0.
        void GenerateMips2D(WGPUTexture texture, int width, int height, int mipLevels);
        // Generates levels 1..mipLevels-1 of ONE array layer (cube face) of a 6-layer texture,
        // leaving the other 5 faces' mip chains untouched -- called once per face.
        void GenerateMipsCubeFace(WGPUTexture texture, int face, int size, int mipLevels);

        PlatformRendererSurfaceState surfaceState_;
        void* metalSurfaceOwner_ = nullptr;
        WGPUInstance instance_ = nullptr;
        WGPUSurface surface_ = nullptr;
        WGPUAdapter adapter_ = nullptr;
        WGPUDevice device_ = nullptr;
        WGPUQueue queue_ = nullptr;
        /// WEBGPU-172: the device's own `maxVertexBuffers` limit, read once at device creation.
        /// 8 is WebGPU's guaranteed floor and stands in until the real value is known.
        int maxVertexBuffers_ = 8;
        /// WEBGPU-144: true when the adapter advertised WGPUFeatureName_TextureCompressionBC and it
        /// was requested at device creation, so BC1/2/3/7 textures upload natively.
        bool bcSupported_ = false;
        WGPUSurfaceConfiguration surfaceConfig_{};
        // REMED-GFX-131: two distinct formats, deliberately separated.
        //
        // surfaceFormat_ is CNA's SurfaceFormat::Color mapping: the format every render pipeline
        // declares as its colour target, every RenderTarget2D/RenderTargetCube allocates, and every
        // backbuffer view is created with. XNA's SurfaceFormat.Color is a plain 8-bit UNORM byte
        // format -- SurfaceFormat.ColorSrgbEXT is the separate, explicitly gamma-encoded one -- so
        // this is ALWAYS a non-sRGB format. It used to be the surface's configured format directly,
        // which made the hardware apply a linear-to-sRGB encode on every render-pass store: a
        // channel written as 128 read back as 188.
        //
        // surfaceConfiguredFormat_ is what the SURFACE itself is configured with, which the
        // platform may only offer as an sRGB variant. When the two differ, the surface is
        // configured with the non-sRGB counterpart listed in viewFormats and every backbuffer view
        // is created in surfaceFormat_, so the byte semantics above hold with no extra pipeline,
        // pass, texture or per-pixel conversion. They are equal on every adapter that offers a
        // non-sRGB surface format at all, which is the overwhelmingly common case.
        WGPUTextureFormat surfaceFormat_ = WGPUTextureFormat_Undefined;
        WGPUTextureFormat surfaceConfiguredFormat_ = WGPUTextureFormat_Undefined;
        /// Backing storage for surfaceConfig_.viewFormats, which only borrows the array.
        std::array<WGPUTextureFormat, 1> surfaceViewFormats_{WGPUTextureFormat_Undefined};
        WGPUTexture depthTexture_ = nullptr;
        WGPUTextureView depthView_ = nullptr;

        // WEBGPU-58: single renderer-GLOBAL MSAA sample count (1 = disabled), mirroring
        // VulkanRenderer::sampleCount_'s own "one value shared by every pipeline;
        // invalidate/rebuild everything on an (expected-rare) change" simplification -- NOT a new
        // per-pipeline-cache-key dimension (Make3DPipelineKey()/WEBGPU-30 is unchanged). Every one
        // of the ~10 GetOrCreatePipeline*3D() families plus every keyed SpriteBatch pipeline bake
        // this SAME value into their own WGPUMultisampleState.count. Only ever 1 or 4 -- see
        // PickSampleCount()'s own comment for why WebGPU has no wider range here, unlike Vulkan.
        int sampleCount_ = 1;
        // Cached result of Supports4xMsaa()'s own real device-capability probe: -1 = not probed
        // yet, 0 = unsupported, 1 = supported.
        /// WEBGPU-195: the probe's cached answer. Mutable because the probe is a
        /// question about the device, not a change to the renderer, and
        /// `SupportsCapability()` is const and has to be able to ask it.
        mutable int msaa4xSupported_ = -1;

        /// WEBGPU-200: whether the device was created with `WGPUFeatureName_Float32Filterable`.
        /// Without it a 32-bit float texture may only be sampled with a NON-filtering sampler --
        /// the format is still renderable, so this gates the sampler, never the target.
        bool float32Filterable_ = false;
        /// WEBGPU-200: whether the device was created with `WGPUFeatureName_Float32Blendable`.
        /// Without it a 32-bit float colour target may not be blended into; a blend state on such a
        /// target is refused by name rather than silently ignored.
        bool float32Blendable_ = false;
        /// WEBGPU-201: whether the device was created with wgpu-native's TextureFormat16bitNorm,
        /// which is what makes `RGBA16Unorm` (`SurfaceFormat::Rgba64`) usable. Always false in the
        /// browser, where the feature does not exist.
        bool rgba16UnormSupported_ = false;
        /// Composed lazily by `GetAdditionalLimitationsTextEXT()`, which returns a `string_view` and
        /// therefore needs storage that outlives the call. Its inputs are fixed once the device
        /// exists, so it is built once.
        mutable std::string additionalLimitations_;
        /// WEBGPU-200: test-only override making both float32 features report absent. See
        /// `DebugForceFloat32FeaturesAbsentEXT`.
        bool forceFloat32FeaturesAbsent_ = false;

        /// WEBGPU-198: cached results of the render-attachment probe, one entry per native format
        /// actually asked about. Empty until something asks; a probe costs a 1x1 texture creation
        /// inside a validation error scope, so it is worth caching but not worth pre-warming.
        mutable std::unordered_map<std::uint32_t, bool> renderTargetFormatProbe_;
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
        // WEBGPU-59: the SpriteBatch dynamic vertex buffer is a THREE-slot ring. Each bind cycle's
        // UploadSpriteVertices() rotates to the next slot before writing, so the buffer being
        // written for this cycle is never the one the previous two submissions are still reading --
        // the classic dynamic-vertex-buffer ring. (Uploads use wgpuQueueWriteBuffer, which the queue
        // already serialises, so the ring is defensive rather than fixing a live stall; it also
        // matches the reference renderers' 3-frame ring.) Each slot grows in place when a cycle needs
        // more than it holds. spriteVertexBuffer_ is a NON-owning alias of the active slot so the
        // write/bind sites are unchanged; the ring owns the buffers.
        static constexpr std::size_t kSpriteVertexRing = 3;
        std::array<WGPUBuffer, kSpriteVertexRing> spriteVertexRing_{};
        std::array<std::uint64_t, kSpriteVertexRing> spriteVertexRingCapacity_{};
        std::size_t spriteVertexRingIndex_ = 0;
        WGPUBuffer spriteVertexBuffer_ = nullptr;        ///< Non-owning alias of the active ring slot.
        /// WEBGPU-155: the shared 16-byte (0,0,0,1) neutral vertex record, bound at array stride 0
        /// for any stock shader input the draw's declaration does not name. Created on first use.
        WGPUBuffer neutralVertexBuffer_ = nullptr;
        std::uint64_t spriteVertexCapacityBytes_ = 0;    ///< Active slot's capacity.
        /// REMED-GFX-159: bytes UploadSpriteVertices() wrote for the cycle being replayed. The
        /// sprite vertex buffer used to be bound once for the whole sprite run; interleaved 3D
        /// draws bind their own into the same slot, so it is rebound per sprite run and the bind
        /// needs the size separately from the capacity.
        std::uint64_t spriteVertexBytes_ = 0;

        // WEBGPU-52: mip-blit resources -- see EnsureMipBlitPipeline()'s own doc comment. Created
        // lazily on first use (a game that never requests mipMap=true with initial pixel data
        // never pays for these). The shader/layouts/sampler are format-independent; the pipeline is
        // cached per colour-target format (mipBlitPipelines_): RGBA8Unorm for a plain Texture2D/
        // TextureCube, surfaceFormat_ for a RenderTargetCube (WEBGPU-114).
        WGPUShaderModule mipBlitShader_ = nullptr;
        WGPUBindGroupLayout mipBlitBindGroupLayout_ = nullptr;
        WGPUPipelineLayout mipBlitPipelineLayout_ = nullptr;
        std::unordered_map<WGPUTextureFormat, WGPURenderPipeline> mipBlitPipelines_;
        WGPUSampler mipBlitSampler_ = nullptr;

        std::vector<SpriteCommand> spriteCommands_;
        /**
         * @brief REMED-GFX-159: this bind cycle's queued draws in exact public call order.
         *
         * One entry per queued draw, appended by the Queue*Draw()/QueueSprite() that produced it
         * and consumed once by ReplayDrawsInOrder(). This is the ONLY thing that decides execution
         * order; the eleven family vectors decide only where a command's payload lives.
         */
        std::vector<DrawOrderEntry> drawOrder_;
        /**
         * @brief REMED-GFX-156: the payload of every ordered Clear() of the current bind cycle.
         *
         * A twelfth "family" vector in every respect that matters: entries are appended at the
         * public call, addressed from @ref drawOrder_ by INDEX (never a pointer -- this vector
         * reallocates as the cycle is recorded), and cleared together with the eleven draw families
         * once the cycle has been replayed.
         */
        std::vector<OrderedClearCommand> clearCommands_;
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
        /// WEBGPU-143 MRT: the full per-slot `BlendState.ColorWriteChannels`/`1`/`2`/`3`. Slot 0 is
        /// `colorWriteMask_` (kept for the stock/single-target path); a custom-effect MRT draw writes
        /// its own `@location(0..N-1)`, so each slot honours its own mask here. XNA has one blend
        /// EQUATION for all targets, so only the write mask is per-slot.
        std::array<int, 4> colorWriteChannels_ = {15, 15, 15, 15};
        /// XNA ColorWriteChannels (R=1,G=2,B=4,A=8) is bit-identical to WGPUColorWriteMask_*.
        [[nodiscard]] WGPUColorWriteMask CurrentWriteMask() const
        { return static_cast<WGPUColorWriteMask>(colorWriteMask_ & 0xF); }
        [[nodiscard]] std::uint32_t CurrentSampleMask() const { return sampleMask_; }
        int cullMode_ = 0;                ///< XNA CullMode::None
        /// XNA FillMode::WireFrame, as last selected through ApplyRasterizerState. WEBGPU-115: this
        /// is the state the draw-time guard reads; no pipeline ever consumes it, because no draw
        /// that would reach one gets past RequireSupportedFillModeEXT().
        bool fillModeWireframe_ = false;
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
        // REMED-GFX-116: pass-local viewport bookkeeping, rebuilt at the top of every render pass
        // and consumed only while that pass is being recorded. It holds the target extent the
        // captured snapshots are clamped against and the last values actually handed to
        // wgpuRenderPassEncoderSetViewport -- it is NOT a source of viewport values (those come
        // only from each command's own WebGPUViewportSnapshot).
        WebGPUPassViewportState passViewport_{};
        // REMED-GFX-146: the scissor counterpart of passViewport_, with identical lifetime and the
        // identical rule -- it is NOT a source of rectangle values (those come only from each
        // command's own WebGPUScissorSnapshot), only the bookkeeping that lets consecutive draws
        // sharing a rectangle issue one native call.
        WebGPUPassScissorState passScissor_{};
        std::size_t setViewportCallCount_ = 0;
        std::size_t setScissorCallCount_ = 0;
        std::size_t renderPassCount_ = 0;
        std::size_t queueSubmitCount_ = 0;
        /// WEBGPU-115: see GetQueuedDrawCommandCountEXT().
        std::size_t queuedDrawCommandCount_ = 0;
        /// WEBGPU-115: see GetNativeDrawIssueCountEXT().
        std::size_t nativeDrawIssueCount_ = 0;

        // WEBGPU-84: occlusion query state. Every field is inert until the first OcclusionQuery is
        // created (EnsureOcclusionResources), and every replay path is gated on activeOcclusionQuery_
        // / DrawOrderEntry::occlusionQuery, so a frame with no query behaves exactly as before.
        static constexpr int kMaxOcclusionSlots = 32;
        WGPUQuerySet occlusionQuerySet_ = nullptr;
        WGPUBuffer occlusionResolveBuffer_ = nullptr;    ///< QueryResolve|CopySrc, kMaxOcclusionSlots * u64.
        WGPUBuffer occlusionReadbackBuffer_ = nullptr;   ///< MapRead|CopyDst, same size.
        WebGPUOcclusionQueryRenderer* activeOcclusionQuery_ = nullptr;  ///< Set between Begin()/End().
        std::array<WebGPUOcclusionQueryRenderer*, kMaxOcclusionSlots> occlusionSlots_{};  ///< slot -> query.
        std::vector<WebGPUOcclusionQueryRenderer*> occlusionResolvedThisFlush_;  ///< queries to read back.
        bool occlusionReadbackPending_ = false;

        void EnsureOcclusionResources();
        int AllocateOcclusionSlot(WebGPUOcclusionQueryRenderer* query);
        void FreeOcclusionSlot(int slot);
        void PurgeOcclusionQuery(WebGPUOcclusionQueryRenderer* query);
        void ReadbackOcclusionResults();
        float blendFactorR_ = 1.0f;
        float blendFactorG_ = 1.0f;
        float blendFactorB_ = 1.0f;
        float blendFactorA_ = 1.0f;
        int referenceStencil_ = 0;
        // WEBGPU-83 (complete): this stencil parameter set is both stored here AND baked into every
        // 3D pipeline's WGPUStencilFaceState (FillWGPUStencilState, folded into the pipeline cache
        // key since WebGPU keeps masks as pipeline state), with the reference applied per draw via
        // wgpuRenderPassEncoderSetStencilReference. Two-sided front/back winding is pixel-verified
        // (WebGPU_StencilTwoSided) -- see plans/plan_webgpu.md's WEBGPU-83 row.
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
        /// WEBGPU-83: snapshots the current stencil state (set by ApplyDepthStencilState) into the
        /// per-draw StencilKeyParams a colored3d draw command captures.
        [[nodiscard]] StencilKeyParams CaptureStencilStateEXT() const
        {
            StencilKeyParams s;
            s.enable = stencilEnable_;
            s.func = stencilFunc_;
            s.stencilPass = stencilPass_;
            s.stencilFail = stencilFail_;
            s.depthFail = stencilDepthFail_;
            s.readMask = stencilReadMask_;
            s.writeMask = stencilWriteMask_;
            s.twoSided = twoSidedStencilMode_;
            s.ccwFunc = ccwStencilFunc_;
            s.ccwPass = ccwStencilPass_;
            s.ccwFail = ccwStencilFail_;
            s.ccwDepthFail = ccwStencilDepthFail_;
            return s;
        }

        // WEBGPU-82: per-slot SamplerState (slot 0 is texture0, read by every texture-consuming 3D
        // Queue*Draw() at queue time -- see GetOrCreateSlotSampler()).
        struct SlotSamplerState
        {
            int filter = 0;    ///< XNA TextureFilter::Linear
            int addressU = 1;  ///< matches every *DrawCommand struct's own textureFilter/addressU/
            int addressV = 1;  ///< addressV default (Clamp) -- this file's established convention.
            /// WEBGPU-160: the third addressing axis. Observable wherever a volume texture is
            /// sampled, and Clamp by default like the other two.
            int addressW = 1;
            /// WEBGPU-161: `SamplerState.MaxMipLevel` -- the highest-resolution mip level sampling
            /// may use. XNA's default is 0, i.e. no restriction.
            int maxMipLevel = 0;
            /// WEBGPU-205: `SamplerState.MipMapLevelOfDetailBias`. NOT a sampler-descriptor field --
            /// `WGPUSamplerDescriptor` has no `lodBias` -- so it travels in the per-draw uniform
            /// block and is applied by WGSL's `textureSampleBias` in the fragment stage.
            float lodBias = 0.0f;
            int maxAnisotropy = 4;
        };
        std::array<SlotSamplerState, 16> slotSamplers_{};
        /// WEBGPU-160: keyed 64-bit, because the 32-bit key was exactly full -- filter, addressU,
        /// addressV and anisotropy took one byte each -- and the W axis lives above bit 32. A
        /// uint32_t map here silently TRUNCATED it, so three states with different W hashed to one
        /// entry and the first sampler was handed to all three.
        std::unordered_map<std::uint64_t, WGPUSampler> slotSamplerCache_;

        WGPUBuffer readbackBuffer_ = nullptr;
        std::uint64_t readbackBufferCapacity_ = 0;
        std::uint32_t readbackBytesPerRow_ = 0;
        int readbackWidth_ = 0;
        int readbackHeight_ = 0;
        bool readbackValid_ = false;

        bool hasAcquiredTexture_ = false;
        WGPUTexture acquiredTexture_ = nullptr;
        // WEBGPU-133 (Emscripten): set when a readback's browser-yielding map has let the browser
        // invalidate the acquired surface texture; the next render discards and re-acquires, while a
        // same-frame re-read still reuses readbackBuffer_. Always false on native.
        bool acquiredBackbufferStale_ = false;
        bool framePending_ = true;

        // WEBGPU-53/54: the currently bound RenderTarget2D renderer, or nullptr for the swapchain
        // backbuffer. Present()/ReadBackbuffer() always target the backbuffer specifically
        // (EnsureFrameRendered() does not consult this) -- matching their pre-existing hardcoded
        // swapchain-only behaviour -- so a render target left bound across a Present() call has
        // its own pending draws deferred until the next SetRenderTarget2D() switch rather than
        // appearing on screen; this is a narrow, documented edge case (a game is expected to
        // restore the backbuffer via SetRenderTarget(nullptr) before Present(), same convention
        // every other CNA renderer already assumes).
        WebGPURenderTargetRenderer* currentRenderTarget_ = nullptr;

        // WEBGPU-85/87 MRT: the ADDITIONAL render targets bound alongside currentRenderTarget_
        // (which is always slot 0). Empty for a single target / cube face / the backbuffer; holds
        // slots 1..N-1 (N up to 4) while an MRT set is bound. All slots share width/height/sample
        // count/depth, validated at SetRenderTargets() bind time. CurrentColorAttachmentCountEXT()
        // is 1 + this size (or 1 when no RenderTarget2D is bound).
        std::vector<WebGPURenderTargetRenderer*> mrtExtraTargets_;

        // WEBGPU-114: the cube-face sibling of currentRenderTarget_ above -- non-null exactly
        // when a RenderTargetCube face is the currently-bound render target (mutually exclusive
        // with currentRenderTarget_; only one of the two, or neither -- the backbuffer -- is ever
        // set). currentRenderTargetCubeFaceIndex_ is only meaningful while
        // currentRenderTargetCubeFace_ is non-null.
        WebGPURenderTargetCubeRenderer* currentRenderTargetCubeFace_ = nullptr;
        int currentRenderTargetCubeFaceIndex_ = -1;

        /// WEBGPU-86 MRT: how many colour attachments the pass currently being replayed has. Set at
        /// the start of `ReplayDrawsInOrder` from the `PassDestination`, read by every stock pipeline
        /// builder (via `ExpandStockColorTargetsEXT`) and folded into its cache key. 1 outside MRT,
        /// so single-target pipelines are byte-identical to before.
        int replayColorAttachmentCount_ = 1;

        /// WEBGPU-39: the WGPU depth format of the render pass currently being replayed/built. Every 3D
        /// pipeline's depthStencil.format and the pipeline cache key read this, so a pipeline built for a
        /// Depth16 target is distinct from one for Depth24PlusStencil8 (WebGPU rejects a format mismatch
        /// between pipeline and attachment). Set from the active target's own depth format at replay.
        WGPUTextureFormat replayDepthFormat_ = WGPUTextureFormat_Depth24PlusStencil8;

        /**
         * @brief WEBGPU-197: the COLOUR format of the render pass currently being replayed.
         *
         * A WebGPU render pipeline bakes `WGPUColorTargetState.format`, and every 3D family used to
         * read the swap-chain `surfaceFormat_` -- which is why no non-`Color` target could ever be
         * drawn into. It is set from the pass's own destination at replay, exactly as
         * @ref replayDepthFormat_ is, and folded into every 3D pipeline key so a pipeline built for
         * one target format is never handed to a pass using another. `Undefined` means "not inside a
         * replay", and the builders fall back to `surfaceFormat_` then.
         *
         * The SpriteBatch path has always done this (`WebGPUSpriteBlendSnapshot::targetFormat`);
         * this is the 3D side catching up.
         */
        WGPUTextureFormat replayColorFormat_ = WGPUTextureFormat_Undefined;
        /**
         * @brief WEBGPU-197: the SAMPLE COUNT of the render pass currently being replayed.
         *
         * The pipeline's other baked target property, and the reason a `RenderTarget2D` could not
         * honour its own `multiSampleCount` (`WEBGPU-165`). 0 means "not inside a replay", and the
         * builders fall back to the renderer-global `sampleCount_` then.
         */
        std::uint32_t replaySampleCount_ = 0;
        /// WEBGPU-197: the extra MRT slots' own colour formats for the pass being replayed. Index 0
        /// is unused; slot 0's format is @ref replayColorFormat_.
        std::array<WGPUTextureFormat, 4> replayMrtColorFormats_{};
        /// WEBGPU-39: whether the active pass's depth format carries a stencil aspect (only the combined
        /// Depth24PlusStencil8 does). Stencil ops (WEBGPU-83) are baked only when true; the depth pass's
        /// own stencil load/store ops are likewise omitted for a depth-only format.
        bool replayDepthHasStencil_ = true;

        /// WEBGPU-86 MRT: fills a stock draw's EXTRA colour targets (slots 1..N-1) for the pass's
        /// attachment count and returns that count. Slot 0 is left default -- the builder holds a
        /// reference to `out[0]` and fills its real format/write-mask/blend itself (so a `blend`
        /// assigned AFTER this call still lands in the array). Slots 1..N-1 keep `surfaceFormat_`
        /// but write NOTHING (`writeMask` 0), so a stock draw into an MRT set writes attachment 0
        /// only -- the "the 2D/stock pipeline writes attachment 0" behaviour every renderer has.
        int InitStockColorTargetsEXT(std::array<WGPUColorTargetState, 4>& out) const;

        // Phase 57/63 vertical slice: DrawColoredPrimitives()/DrawIndexedColoredPrimitives() only
        // (VertexPositionColor, stride 16 -- see plans/plan_webgpu.md's Phase 57 entry-point note). The
        // 128-float uniform layout matches VulkanRenderer::FillExtPushConst() byte-for-byte
        // so the same shader/uniform shape can be reused once DrawPrimitivesEx (full BasicEffect
        // dispatch) lands -- IGraphicsRenderer::DrawPrimitivesEx's own default implementation
        // already falls back to DrawColoredPrimitives, so this also unblocks simple (unlit,
        // untextured) Model/BasicEffect draws going through that fallback.
        struct ColoredDrawCommand
        {
            /// WEBGPU-155: this draw's vertex layout, resolved from its own VertexDeclaration at
            /// queue time. Captured per command for the same reason the pipeline state is: a later
            /// SetVertexBuffer must not retroactively relayout an already-queued draw.
            CNA::Internal::Graphics::ResolvedStockVertexLayoutEXT vertexLayout{};
            std::vector<std::uint8_t> vertexData;
            /**
             * @brief WEBGPU-172: the draw's SECOND and further per-vertex streams, if any.
             *
             * Empty for every single-stream draw, which is all this renderer accepted before this
             * task; `vertexData` above is always the first resolved stream, so nothing about the
             * one-buffer case moved. Each entry is already sliced by its own stride and
             * `VertexOffset`, so the replay binds it at native byte offset zero.
             */
            std::vector<CapturedVertexStreamEXT> extraStreams;
            std::vector<std::uint8_t> indexData;   ///< empty for a non-indexed draw
            bool indexed = false;
            bool index32 = false;
            std::uint32_t vertexCount = 0;
            std::uint32_t indexCount = 0;
            std::uint32_t firstIndex = 0;
            std::int32_t baseVertex = 0;
            WGPUPrimitiveTopology topology = WGPUPrimitiveTopology_TriangleList;
            /// WEBGPU-205: 44 floats. The 40-float block was exactly full (128 base
            /// + 32 fog tail), and the per-slot LOD bias needs a channel of its own.
            std::array<float, 44> uniforms{};
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
            /// REMED-GFX-116: the complete GraphicsDevice.Viewport active at this draw's own
            /// public call, captured by value. Never resolved from live state during replay.
            WebGPUViewportSnapshot viewport{};
            /// REMED-GFX-146: the complete scissor state (ScissorTestEnable plus the four
            /// rectangle components) active at this draw's own public call, captured by
            /// value. Never resolved from live state during replay.
            WebGPUScissorSnapshot scissor{};
            /// WEBGPU-83: the stencil state baked into this draw's pipeline + its dynamic reference.
            StencilKeyParams stencil{};
            int stencilRef = 0;
        };
        WGPUShaderModule coloredShader_ = nullptr;
        WGPUBindGroupLayout coloredBindGroupLayout_ = nullptr;
        WGPUPipelineLayout coloredPipelineLayout_ = nullptr;
        std::unordered_map<std::uint64_t, WGPURenderPipeline> coloredPipelines_;  ///< keyed by Make3DPipelineKey() (WEBGPU-30)
        std::vector<ColoredDrawCommand> coloredDrawCommands_;
        void IssueColoredDraw(WGPURenderPassEncoder pass, const ColoredDrawCommand& command,
                              ReplayState& state);

        // WEBGPU-20/33: textured3d (stride 20, VertexPositionTexture). Shares the same UBO layout/
        // bind group (group 0, coloredBindGroupLayout_) as colored3d; adds a second bind group
        // (group 1: sampler + texture, mirrors the SpriteBatch bind group layout exactly) for the
        // texture itself. No fog (same deliberate deferral as colored3d.wgsl -- not tracked as its
        // own WEBGPU-N task).
        struct TexturedDrawCommand
        {
            /// WEBGPU-155: this draw's vertex layout, resolved from its own VertexDeclaration at
            /// queue time. Captured per command for the same reason the pipeline state is: a later
            /// SetVertexBuffer must not retroactively relayout an already-queued draw.
            CNA::Internal::Graphics::ResolvedStockVertexLayoutEXT vertexLayout{};
            /// WEBGPU-83: stencil state baked into this draw's pipeline + its dynamic reference.
            StencilKeyParams stencil{};
            int stencilRef = 0;
            std::vector<std::uint8_t> vertexData;
            /**
             * @brief WEBGPU-172: the draw's SECOND and further per-vertex streams, if any.
             *
             * Empty for every single-stream draw, which is all this renderer accepted before this
             * task; `vertexData` above is always the first resolved stream, so nothing about the
             * one-buffer case moved. Each entry is already sliced by its own stride and
             * `VertexOffset`, so the replay binds it at native byte offset zero.
             */
            std::vector<CapturedVertexStreamEXT> extraStreams;
            std::vector<std::uint8_t> indexData;
            bool indexed = false;
            bool index32 = false;
            std::uint32_t vertexCount = 0;
            std::uint32_t indexCount = 0;
            std::uint32_t firstIndex = 0;
            std::int32_t baseVertex = 0;
            WGPUPrimitiveTopology topology = WGPUPrimitiveTopology_TriangleList;
            /// WEBGPU-205: 44 floats. The 40-float block was exactly full (128 base
            /// + 32 fog tail), and the per-slot LOD bias needs a channel of its own.
            std::array<float, 44> uniforms{};
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
            /// REMED-GFX-116: the complete GraphicsDevice.Viewport active at this draw's own
            /// public call, captured by value. Never resolved from live state during replay.
            WebGPUViewportSnapshot viewport{};
            /// REMED-GFX-146: the complete scissor state (ScissorTestEnable plus the four
            /// rectangle components) active at this draw's own public call, captured by
            /// value. Never resolved from live state during replay.
            WebGPUScissorSnapshot scissor{};
            // REMED-GFX-167: resolved ONCE, here at the public draw call. This used to be a raw
            // `const IWebGPUSamplable*` justified by "a bound Texture2D's renderer is owned by
            // long-lived game/content state, so it is guaranteed to still be alive when this
            // command actually renders later in the same frame". Nothing guarantees that: a
            // RenderTarget2D produced, sampled and dropped inside one Draw() is destroyed while
            // this command is still only queued, and replay then made a VIRTUAL call on freed
            // memory. Storing the resolved view plus its keep-alive removes the dereference and
            // the dangling handle together.
            WebGPUSampledTextureEXT texture;
            int textureFilter = 0;
            int addressU = 1;
            int addressV = 1;
            int addressW = 1;  ///< WEBGPU-160: the third addressing axis.
            int maxMipLevel = 0;  ///< WEBGPU-161: SamplerState.MaxMipLevel.
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
                                                       float depthBias, float slopeScaleDepthBias,
                                                       const StencilKeyParams& stencil,
                                                       const CNA::Internal::Graphics::ResolvedStockVertexLayoutEXT& vertexLayout);
        [[nodiscard]] WGPURenderPipeline GetOrCreatePipelineColoredTextured3D(WGPUPrimitiveTopology topology,
                                                                               WGPUIndexFormat stripIndexFormat,
                                                                               bool depthTest, bool depthWrite,
                                                                               int depthFunc,
                                                       bool blend, const BlendKeyParams& blendParams,
                                                       int cullMode, bool wireframe,
                                                       float depthBias, float slopeScaleDepthBias,
                                                       const StencilKeyParams& stencil,
                                                       const CNA::Internal::Graphics::ResolvedStockVertexLayoutEXT& vertexLayout);
        void QueueTexturedDraw(const IVertexBufferRenderer& vb, const IIndexBufferRenderer* ib,
                               const Matrix& world, const Matrix& view, const Matrix& projection,
                               PrimitiveType primitive, int primitiveCount, const GpuDrawParams& params,
                               StockVertexShapeEXT shape);
        void IssueTexturedDraw(WGPURenderPassEncoder pass, const TexturedDrawCommand& command,
                              ReplayState& state);

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

        // WEBGPU-12/59: transient per-draw buffer pool. Every 3D draw used to create a fresh
        // WGPUBuffer for its vertices/uniforms/indices and release it once the frame's command
        // buffer was submitted -- correct, but churning wgpuDeviceCreateBuffer/wgpuBufferRelease
        // every draw, every frame. Instead, buffers are ACQUIRED from a pool keyed by
        // (usage, power-of-two size class) and RECYCLED back into it after submit rather than
        // released, so a warmed-up scene reuses a bounded working set and allocates nothing new.
        // This is safe WITHOUT explicit fencing because every write (wgpuQueueWriteBuffer) and read
        // (the recorded draw) goes through the single device queue in FIFO order, and wgpu-native
        // keeps a buffer alive internally until the submission that references it completes -- so a
        // recycled buffer reused in a later cycle is written only after the prior cycle's reads.
        std::unordered_map<std::uint64_t, std::vector<WGPUBuffer>> transientBufferPool_;
        std::size_t transientBuffersCreatedEXT_ = 0;   ///< Lifetime count of real wgpuDeviceCreateBuffer calls.
        std::size_t transientBuffersReusedEXT_ = 0;     ///< Lifetime count of pool hits (reuses).
        static constexpr std::size_t kTransientPoolPerClassCap = 128;  ///< Free buffers kept per size class.
        /// Round @p size up to this pool's power-of-two size class (min 256 covers 128/160B UBOs).
        [[nodiscard]] static std::uint64_t TransientSizeClassEXT(std::uint64_t size);
        /// Acquire a pooled buffer of at least @p size with exactly @p usage (reused or freshly made).
        [[nodiscard]] WGPUBuffer AcquireTransientBuffer(WGPUBufferUsage usage, std::uint64_t size);
        /// Return @p buffer to the pool for reuse (or release it if the class cap is reached).
        void RecycleTransientBuffer(WGPUBuffer buffer);
        /// Release every pooled buffer (teardown).
        void DestroyTransientBufferPool();

        // WEBGPU-22: lit_textured3d (stride 32, VertexPositionNormalTexture). Real Blinn-Phong
        // lighting (FNA's Lighting.fxh ComputeLights(): 3 directional lights, ambient, specular,
        // emissive), ported from VulkanRenderer's lit_textured3d.{vert,frag}.glsl. This is
        // the first WebGPU 3D shader that needs a SECOND UBO (litBindGroupLayout_, group 0
        // binding 1) alongside the existing 128-byte primary uniforms (binding 0, still used for
        // MVP/diffuseColor/ambientColor/lightingEnabled/light0Dir/light0Diffuse/textureEnabled),
        // since that buffer is already fully packed. Reuses texturedBindGroupLayout_ (group 1:
        // sampler + texture) unchanged. No fog (same deliberate deferral as the other 3D shaders).
        struct LitTexturedDrawCommand
        {
            /// WEBGPU-155: this draw's vertex layout, resolved from its own VertexDeclaration at
            /// queue time. Captured per command for the same reason the pipeline state is: a later
            /// SetVertexBuffer must not retroactively relayout an already-queued draw.
            CNA::Internal::Graphics::ResolvedStockVertexLayoutEXT vertexLayout{};
            /// WEBGPU-83: stencil state baked into this draw's pipeline + its dynamic reference.
            StencilKeyParams stencil{};
            int stencilRef = 0;
            std::vector<std::uint8_t> vertexData;
            /**
             * @brief WEBGPU-172: the draw's SECOND and further per-vertex streams, if any.
             *
             * Empty for every single-stream draw, which is all this renderer accepted before this
             * task; `vertexData` above is always the first resolved stream, so nothing about the
             * one-buffer case moved. Each entry is already sliced by its own stride and
             * `VertexOffset`, so the replay binds it at native byte offset zero.
             */
            std::vector<CapturedVertexStreamEXT> extraStreams;
            std::vector<std::uint8_t> indexData;
            bool indexed = false;
            bool index32 = false;
            std::uint32_t vertexCount = 0;
            std::uint32_t indexCount = 0;
            std::uint32_t firstIndex = 0;
            std::int32_t baseVertex = 0;
            WGPUPrimitiveTopology topology = WGPUPrimitiveTopology_TriangleList;
            /// WEBGPU-205: 44 floats. The 40-float block was exactly full (128 base
            /// + 32 fog tail), and the per-slot LOD bias needs a channel of its own.
            std::array<float, 44> uniforms{};
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
            /// REMED-GFX-116: the complete GraphicsDevice.Viewport active at this draw's own
            /// public call, captured by value. Never resolved from live state during replay.
            WebGPUViewportSnapshot viewport{};
            /// REMED-GFX-146: the complete scissor state (ScissorTestEnable plus the four
            /// rectangle components) active at this draw's own public call, captured by
            /// value. Never resolved from live state during replay.
            WebGPUScissorSnapshot scissor{};
            WebGPUSampledTextureEXT texture;
            int textureFilter = 0;
            int addressU = 1;
            int addressV = 1;
            int addressW = 1;  ///< WEBGPU-160: the third addressing axis.
            int maxMipLevel = 0;  ///< WEBGPU-161: SamplerState.MaxMipLevel.
            int maxAnisotropy = 4;  ///< WEBGPU-82: per-slot SamplerState anisotropy
            /// Task 1105 (plans/plan_graphics.md Phase 80): true selects the per-vertex-lit sibling
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
                                                       float depthBias, float slopeScaleDepthBias,
                                                       const StencilKeyParams& stencil,
                                                       const CNA::Internal::Graphics::ResolvedStockVertexLayoutEXT& vertexLayout);
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
                                                       float depthBias, float slopeScaleDepthBias,
                                                       const StencilKeyParams& stencil,
                                                       const CNA::Internal::Graphics::ResolvedStockVertexLayoutEXT& vertexLayout);
        void QueueLitTexturedDraw(const IVertexBufferRenderer& vb, const IIndexBufferRenderer* ib,
                                  const Matrix& world, const Matrix& view, const Matrix& projection,
                                  PrimitiveType primitive, int primitiveCount, const GpuDrawParams& params,
                                  StockVertexShapeEXT shape);
        void IssueLitTexturedDraw(WGPURenderPassEncoder pass, const LitTexturedDrawCommand& command,
                              ReplayState& state);

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
        // VulkanRenderer's alpha_test3d.{vert,frag}.glsl / alpha_test_colored3d.vert.glsl.
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
            /// WEBGPU-155: this draw's vertex layout, resolved from its own VertexDeclaration at
            /// queue time. Captured per command for the same reason the pipeline state is: a later
            /// SetVertexBuffer must not retroactively relayout an already-queued draw.
            CNA::Internal::Graphics::ResolvedStockVertexLayoutEXT vertexLayout{};
            /// WEBGPU-83: stencil state baked into this draw's pipeline + its dynamic reference.
            StencilKeyParams stencil{};
            int stencilRef = 0;
            std::vector<std::uint8_t> vertexData;
            /**
             * @brief WEBGPU-172: the draw's SECOND and further per-vertex streams, if any.
             *
             * Empty for every single-stream draw, which is all this renderer accepted before this
             * task; `vertexData` above is always the first resolved stream, so nothing about the
             * one-buffer case moved. Each entry is already sliced by its own stride and
             * `VertexOffset`, so the replay binds it at native byte offset zero.
             */
            std::vector<CapturedVertexStreamEXT> extraStreams;
            std::vector<std::uint8_t> indexData;
            bool indexed = false;
            bool index32 = false;
            std::uint32_t vertexCount = 0;
            std::uint32_t indexCount = 0;
            std::uint32_t firstIndex = 0;
            std::int32_t baseVertex = 0;
            WGPUPrimitiveTopology topology = WGPUPrimitiveTopology_TriangleList;
            /// WEBGPU-205: 44 floats. The 40-float block was exactly full (128 base
            /// + 32 fog tail), and the per-slot LOD bias needs a channel of its own.
            std::array<float, 44> uniforms{};
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
            /// REMED-GFX-116: the complete GraphicsDevice.Viewport active at this draw's own
            /// public call, captured by value. Never resolved from live state during replay.
            WebGPUViewportSnapshot viewport{};
            /// REMED-GFX-146: the complete scissor state (ScissorTestEnable plus the four
            /// rectangle components) active at this draw's own public call, captured by
            /// value. Never resolved from live state during replay.
            WebGPUScissorSnapshot scissor{};
            WebGPUSampledTextureEXT texture;
            int textureFilter = 0;
            int addressU = 1;
            int addressV = 1;
            int addressW = 1;  ///< WEBGPU-160: the third addressing axis.
            int maxMipLevel = 0;  ///< WEBGPU-161: SamplerState.MaxMipLevel.
            int maxAnisotropy = 4;  ///< WEBGPU-82: per-slot SamplerState anisotropy
            std::size_t stride = 20;   ///< 20, 24, or 32 -- selects vertex layout + shader module
            bool hasVertexColor = false;
        };
        void CreateAlphaTestResources();
        void DestroyAlphaTestResources();
        [[nodiscard]] WGPURenderPipeline GetOrCreatePipelineAlphaTest3D(bool colored,
                                                                        WGPUPrimitiveTopology topology,
                                                                        WGPUIndexFormat stripIndexFormat,
                                                                        bool depthTest, bool depthWrite,
                                                                        int depthFunc,
                                                       bool blend, const BlendKeyParams& blendParams,
                                                       int cullMode, bool wireframe,
                                                       float depthBias, float slopeScaleDepthBias,
                                                       const StencilKeyParams& stencil,
                                                       const CNA::Internal::Graphics::ResolvedStockVertexLayoutEXT& vertexLayout);
        void QueueAlphaTestDraw(const IVertexBufferRenderer& vb, const IIndexBufferRenderer* ib,
                                const Matrix& world, const Matrix& view, const Matrix& projection,
                                PrimitiveType primitive, int primitiveCount, const GpuDrawParams& params,
                                StockVertexShapeEXT shape);
        void IssueAlphaTestDraw(WGPURenderPassEncoder pass, const AlphaTestDrawCommand& command,
                              ReplayState& state);

        WGPUShaderModule alphaTestShader_ = nullptr;          ///< strides 20/32 (no vertex colour)
        WGPUShaderModule alphaTestColoredShader_ = nullptr;   ///< stride 24 (vertex colour tint)
        std::unordered_map<std::uint64_t, WGPURenderPipeline> alphaTestPipelines_;
        std::unordered_map<std::uint64_t, WGPURenderPipeline> alphaTestColoredPipelines_;
        std::vector<AlphaTestDrawCommand> alphaTestDrawCommands_;

        // WEBGPU-24: dual_texture3d.wgsl -- DualTextureEffect (two texture layers sampled at the
        // same UV, `tex1.rgb*=2.0; result=tex1*tex2*tint`, matching
        // VulkanRenderer's dual_texture3d.{vert,frag}.glsl /
        // dual_texture_colored3d.vert.glsl). Genuinely new bind-group shape (unlike alpha_test3d,
        // which reused textured3d's layouts unchanged): group 1 needs FOUR bindings -- one sampler
        // and one texture per public sampler slot, since REMED-GFX-172 established that Texture and
        // Texture2 are filtered by SamplerStates[0] and SamplerStates[1] independently -- so this is
        // the first WebGPU 3D shader family with its own
        // dedicated dualTextureBindGroupLayout_/dualTexturePipelineLayout_ -- group 0 (the primary
        // UBO) is still coloredBindGroupLayout_, reused unchanged (DualTextureEffect has no
        // lighting and no alpha test, so FillExtUniforms()'s existing layout already covers it).
        // No fog (same deliberate deferral as the other 3D shaders).
        struct DualTextureDrawCommand
        {
            /// WEBGPU-155: this draw's vertex layout, resolved from its own VertexDeclaration at
            /// queue time. Captured per command for the same reason the pipeline state is: a later
            /// SetVertexBuffer must not retroactively relayout an already-queued draw.
            CNA::Internal::Graphics::ResolvedStockVertexLayoutEXT vertexLayout{};
            /// WEBGPU-83: stencil state baked into this draw's pipeline + its dynamic reference.
            StencilKeyParams stencil{};
            int stencilRef = 0;
            std::vector<std::uint8_t> vertexData;
            /**
             * @brief WEBGPU-172: the draw's SECOND and further per-vertex streams, if any.
             *
             * Empty for every single-stream draw, which is all this renderer accepted before this
             * task; `vertexData` above is always the first resolved stream, so nothing about the
             * one-buffer case moved. Each entry is already sliced by its own stride and
             * `VertexOffset`, so the replay binds it at native byte offset zero.
             */
            std::vector<CapturedVertexStreamEXT> extraStreams;
            std::vector<std::uint8_t> indexData;
            bool indexed = false;
            bool index32 = false;
            std::uint32_t vertexCount = 0;
            std::uint32_t indexCount = 0;
            std::uint32_t firstIndex = 0;
            std::int32_t baseVertex = 0;
            WGPUPrimitiveTopology topology = WGPUPrimitiveTopology_TriangleList;
            /// WEBGPU-205: 44 floats. The 40-float block was exactly full (128 base
            /// + 32 fog tail), and the per-slot LOD bias needs a channel of its own.
            std::array<float, 44> uniforms{};
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
            /// REMED-GFX-116: the complete GraphicsDevice.Viewport active at this draw's own
            /// public call, captured by value. Never resolved from live state during replay.
            WebGPUViewportSnapshot viewport{};
            /// REMED-GFX-146: the complete scissor state (ScissorTestEnable plus the four
            /// rectangle components) active at this draw's own public call, captured by
            /// value. Never resolved from live state during replay.
            WebGPUScissorSnapshot scissor{};
            WebGPUSampledTextureEXT texture0;
            WebGPUSampledTextureEXT texture1;
            ///@{ REMED-GFX-172: `DualTextureEffect.Texture`'s own GraphicsDevice.SamplerStates[0].
            int textureFilter = 0;
            int addressU = 1;
            int addressV = 1;
            int addressW = 1;  ///< WEBGPU-160: the third addressing axis.
            int maxMipLevel = 0;  ///< WEBGPU-161: SamplerState.MaxMipLevel.
            int maxAnisotropy = 4;  ///< WEBGPU-82: per-slot SamplerState anisotropy
            ///@}
            ///@{ REMED-GFX-172: `DualTextureEffect.Texture2`'s own GraphicsDevice.SamplerStates[1],
            /// captured by value at the public draw call for the same reason slot 0 is -- FNA's
            /// DualTextureEffect.fx declares `DECLARE_TEXTURE(Texture, 0)` and
            /// `DECLARE_TEXTURE(Texture2, 1)`, so the two layers have independent public sampler
            /// slots and a later ApplySamplerState must not reach an already-queued draw.
            int texture1Filter = 0;
            int texture1AddressU = 1;
            int texture1AddressV = 1;
            int texture1AddressW = 1;  ///< WEBGPU-160.
            int texture1MaxMipLevel = 0;  ///< WEBGPU-161.
            int texture1MaxAnisotropy = 4;
            ///@}
            bool hasVertexColor = false;   ///< stride 24 vs stride 20
        };
        void CreateDualTextureResources();
        void DestroyDualTextureResources();
        [[nodiscard]] WGPURenderPipeline GetOrCreatePipelineDualTexture3D(bool colored,
                                                                          WGPUPrimitiveTopology topology,
                                                                          WGPUIndexFormat stripIndexFormat,
                                                                          bool depthTest, bool depthWrite,
                                                                          int depthFunc,
                                                       bool blend, const BlendKeyParams& blendParams,
                                                       int cullMode, bool wireframe,
                                                       float depthBias, float slopeScaleDepthBias,
                                                       const StencilKeyParams& stencil,
                                                       const CNA::Internal::Graphics::ResolvedStockVertexLayoutEXT& vertexLayout);
        void QueueDualTextureDraw(const IVertexBufferRenderer& vb, const IIndexBufferRenderer* ib,
                                  const Matrix& world, const Matrix& view, const Matrix& projection,
                                  PrimitiveType primitive, int primitiveCount, const GpuDrawParams& params,
                                  StockVertexShapeEXT shape);
        void IssueDualTextureDraw(WGPURenderPassEncoder pass, const DualTextureDrawCommand& command,
                              ReplayState& state);

        WGPUShaderModule dualTextureShader_ = nullptr;          ///< stride 20 (no vertex colour)
        WGPUShaderModule dualTextureColoredShader_ = nullptr;   ///< stride 24 (vertex colour tint)
        WGPUBindGroupLayout dualTextureBindGroupLayout_ = nullptr;  ///< group 1: sampler0 + texture0 + texture1 + sampler1
        WGPUPipelineLayout dualTexturePipelineLayout_ = nullptr;    ///< group 0 (UBO, coloredBindGroupLayout_) + group 1
        std::unordered_map<std::uint64_t, WGPURenderPipeline> dualTexturePipelines_;
        std::unordered_map<std::uint64_t, WGPURenderPipeline> dualTextureColoredPipelines_;
        std::vector<DualTextureDrawCommand> dualTextureDrawCommands_;

        // WEBGPU-25/36/74: env_map3d.wgsl -- EnvironmentMapEffect's cube-map reflection shader,
        // ported from VulkanRenderer's env_map3d.{vert,frag}.glsl (cross-checked against
        // EasyGLRenderer::EnsureEnvMapped3DProgram()'s identical GLSL formula before
        // porting -- both already agree field-for-field). Stride 32
        // (VertexPositionNormalTexture, the same vertex layout as lit_textured3d.wgsl). Group 0
        // binding 0 is a small Transform UBO (mvp+world, 128 bytes -- WebGPU has no push constants,
        // so this stands in for Vulkan's own 128-byte push-constant range); binding 1 is the larger
        // EnvMapParams UBO (240 bytes: eye position, diffuse, emissive+envMapAmount, all 3
        // directional lights, envMapSpecular+fresnelFactor/fresnelEnabled, fog, and a
        // CPU-precomputed 3x3 normal matrix packed as 3 vec4f columns -- WGSL has no inverse(),
        // the same reason CreateLitTexturedResources()'s own LitLightParams UBO precomputes one).
        // Group 1 is a genuinely new 4-binding shape (mirrors dualTextureBindGroupLayout_'s own
        // 4-binding group 1, swapping the second texture_2d for a texture_cube): a 2D base texture
        // with SamplerStates[0] and a texture_cube<f32> reflection map with its own independent
        // SamplerStates[1] (REMED-GFX-172). Both textures fall back
        // to a 1x1 default (white 2D / white cube) when EnvironmentMapEffect leaves that map
        // unbound, matching EnsurePbrDefaultTextures()'s own "map absent" convention. No fog
        // deferral here -- unlike every other WebGPU 3D shader, EnvironmentMapEffect's own fog
        // support already exists in the reference GLSL this was ported from, so it is wired in.
        struct EnvMapDrawCommand
        {
            /// WEBGPU-155: this draw's vertex layout, resolved from its own VertexDeclaration at
            /// queue time. Captured per command for the same reason the pipeline state is: a later
            /// SetVertexBuffer must not retroactively relayout an already-queued draw.
            CNA::Internal::Graphics::ResolvedStockVertexLayoutEXT vertexLayout{};
            /// WEBGPU-83: stencil state baked into this draw's pipeline + its dynamic reference.
            StencilKeyParams stencil{};
            int stencilRef = 0;
            std::vector<std::uint8_t> vertexData;
            /**
             * @brief WEBGPU-172: the draw's SECOND and further per-vertex streams, if any.
             *
             * Empty for every single-stream draw, which is all this renderer accepted before this
             * task; `vertexData` above is always the first resolved stream, so nothing about the
             * one-buffer case moved. Each entry is already sliced by its own stride and
             * `VertexOffset`, so the replay binds it at native byte offset zero.
             */
            std::vector<CapturedVertexStreamEXT> extraStreams;
            std::vector<std::uint8_t> indexData;
            bool indexed = false;
            bool index32 = false;
            std::uint32_t vertexCount = 0;
            std::uint32_t indexCount = 0;
            std::uint32_t firstIndex = 0;
            std::int32_t baseVertex = 0;
            WGPUPrimitiveTopology topology = WGPUPrimitiveTopology_TriangleList;
            std::array<float, 32> transformUniforms{};  ///< group 0 binding 0: mvp (16) + world (16)
            /// WEBGPU-205: 16 vec4f (256 bytes). The sixteenth carries the per-slot LOD bias --
            /// [60]=slot 0 (the base texture), [61]=slot 1 (the environment cube).
            std::array<float, 64> envMapUniforms{};     ///< group 0 binding 1: 16 vec4f (256 bytes)
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
            /// REMED-GFX-116: the complete GraphicsDevice.Viewport active at this draw's own
            /// public call, captured by value. Never resolved from live state during replay.
            WebGPUViewportSnapshot viewport{};
            /// REMED-GFX-146: the complete scissor state (ScissorTestEnable plus the four
            /// rectangle components) active at this draw's own public call, captured by
            /// value. Never resolved from live state during replay.
            WebGPUScissorSnapshot scissor{};
            /// EnvironmentMapEffect::Texture -- may legitimately be null (falls back to a 1x1
            /// white texture at render time), unlike every other stride-32+ effect family, which
            /// requires a non-null texture0 at dispatch time.
            WebGPUSampledTextureEXT texture;
            /// EnvironmentMapEffect::EnvironmentMap -- may legitimately be null (falls back to a
            /// 1x1 white cube map at render time, matching VulkanRenderer's own
            /// defaultWhiteCubeView_ fallback). WEBGPU-114: IWebGPUCubeSamplable rather than the
            /// concrete WebGPUTextureCubeRenderer, so a WebGPURenderTargetCubeRenderer (a distinct
            /// concrete class) bound as EnvironmentMapEffect.EnvironmentMap resolves correctly too
            /// -- see IWebGPUCubeSamplable's own doc comment.
            WebGPUSampledTextureEXT envMap;
            ///@{ REMED-GFX-172: the base 2D texture's own GraphicsDevice.SamplerStates[0].
            int textureFilter = 0;
            int addressU = 1;
            int addressV = 1;
            int addressW = 1;  ///< WEBGPU-160: the third addressing axis.
            int maxMipLevel = 0;  ///< WEBGPU-161: SamplerState.MaxMipLevel.
            int maxAnisotropy = 4;  ///< WEBGPU-82: per-slot SamplerState anisotropy
            ///@}
            ///@{ REMED-GFX-172: the reflection cube's own GraphicsDevice.SamplerStates[1], captured
            /// by value at the public draw call. FNA's EnvironmentMapEffect.fx declares
            /// `DECLARE_TEXTURE(Texture, 0)` and `DECLARE_CUBEMAP(EnvironmentMap, 1)`, so the cube
            /// has its own public sampler slot; the same field names the native GPU backend's
            /// EnvMapDrawCommand uses since REMED-GFX-173.
            int envMapFilter = 0;
            int envMapAddressU = 1;
            int envMapAddressV = 1;
            int envMapAddressW = 1;  ///< WEBGPU-160.
            int envMapMaxMipLevel = 0;  ///< WEBGPU-161.
            int envMapMaxAnisotropy = 4;
            ///@}
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
                                                   float depthBias, float slopeScaleDepthBias,
                                                       const StencilKeyParams& stencil,
                                                       const CNA::Internal::Graphics::ResolvedStockVertexLayoutEXT& vertexLayout);
        void QueueEnvMapDraw(const IVertexBufferRenderer& vb, const IIndexBufferRenderer* ib,
                             const Matrix& world, const Matrix& view, const Matrix& projection,
                             PrimitiveType primitive, int primitiveCount, const GpuDrawParams& params,
                             StockVertexShapeEXT shape);
        void IssueEnvMapDraw(WGPURenderPassEncoder pass, const EnvMapDrawCommand& command,
                              ReplayState& state);

        WGPUShaderModule envMapShader_ = nullptr;
        WGPUBindGroupLayout envMapBindGroupLayout_ = nullptr;         ///< group 0: Transform UBO (binding 0) + EnvMapParams UBO (binding 1)
        WGPUBindGroupLayout envMapTextureBindGroupLayout_ = nullptr; ///< group 1: sampler0 + texture_2d + texture_cube + sampler1
        WGPUPipelineLayout envMapPipelineLayout_ = nullptr;
        std::unordered_map<std::uint64_t, WGPURenderPipeline> envMapPipelines_;
        std::vector<EnvMapDrawCommand> envMapDrawCommands_;
        std::unique_ptr<WebGPUTextureRenderer> envMapDefaultWhiteTexture_;
        std::unique_ptr<WebGPUTextureCubeRenderer> envMapDefaultWhiteCube_;

        // WEBGPU-27/38/68: instanced3d.wgsl -- a genuinely SECOND vertex buffer binding
        // (WGPUVertexStepMode_Instance) carrying a per-instance mat4 world transform, ported from
        // VulkanRenderer's instanced3d.{vert,frag}.glsl / GetOrCreatePipelineInstanced3D().
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
            /// WEBGPU-83: stencil state baked into this draw's pipeline + its dynamic reference.
            StencilKeyParams stencil{};
            int stencilRef = 0;
            std::vector<std::uint8_t> vertexData;
            std::vector<std::uint8_t> indexData;
            /**
             * @brief WEBGPU-172: every stream after the first, per-vertex and per-instance alike.
             *
             * The classic instanced draw resolves to exactly two streams -- the geometry buffer
             * and the world-matrix buffer -- so `vertexData` is the geometry and `extraStreams[0]`
             * is the instance data, at native slots 0 and 1, which is precisely where this family
             * has always bound them. What is new is only that a THIRD stream is now possible, and
             * that the slot each stream lands in is decided by the resolved layout rather than by
             * the pair of names this struct used to carry.
             */
            std::vector<CapturedVertexStreamEXT> extraStreams;
            /// WEBGPU-172: the semantic layout the pipeline and every binding are built from.
            CNA::Internal::Graphics::ResolvedStockVertexLayoutEXT vertexLayout{};
            /// WEBGPU-172: whether the resolved layout supplies COLOR0, which picks the WGSL module.
            bool instancedHasColor = false;
            bool indexed = false;
            bool index32 = false;
            std::uint32_t vertexCount = 0;
            std::uint32_t indexCount = 0;
            std::uint32_t firstIndex = 0;
            std::int32_t baseVertex = 0;
            std::uint32_t instanceCount = 1;
            /// Real per-vertex/per-instance buffer strides (not hardcoded), so the pipeline's own
            /// WGPUVertexBufferLayout::arrayStride genuinely matches each buffer's declared
            /// stride -- a real, deliberate improvement over VulkanRenderer's own instanced3d
            /// pipeline, which hardcodes a 64-byte (sizeof(mat4)) instance binding stride
            /// regardless of the instance vertex buffer's own declared stride (latent, harmless
            /// only because every existing caller happens to always use a plain mat4-only instance
            /// declaration).
            std::size_t pvStride = 16;
            WGPUPrimitiveTopology topology = WGPUPrimitiveTopology_TriangleList;
            /// WEBGPU-205: 44 floats. The 40-float block was exactly full (128 base
            /// + 32 fog tail), and the per-slot LOD bias needs a channel of its own.
            std::array<float, 44> uniforms{};
            bool depthTest = false;
            bool depthWrite = false;
            int depthFunc = 3;
            bool blend = true;
            BlendKeyParams blendParams{};
            int cullMode = 0;
            bool wireframe = false;
            float depthBias = 0.0f;
            float slopeScaleDepthBias = 0.0f;
            /// REMED-GFX-116: the complete GraphicsDevice.Viewport active at this draw's own
            /// public call, captured by value. Never resolved from live state during replay.
            WebGPUViewportSnapshot viewport{};
            /// REMED-GFX-146: the complete scissor state (ScissorTestEnable plus the four
            /// rectangle components) active at this draw's own public call, captured by
            /// value. Never resolved from live state during replay.
            WebGPUScissorSnapshot scissor{};
        };
        void CreateInstancedResources();
        void DestroyInstancedResources();
        [[nodiscard]] WGPURenderPipeline GetOrCreatePipelineInstanced3D(
                                                                         const CNA::Internal::Graphics::ResolvedStockVertexLayoutEXT& vertexLayout,
                                                                         bool hasVertexColor,
                                                                         WGPUPrimitiveTopology topology,
                                                                         WGPUIndexFormat stripIndexFormat,
                                                                         bool depthTest, bool depthWrite, int depthFunc,
                                                    bool blend, const BlendKeyParams& blendParams,
                                                    int cullMode, bool wireframe,
                                                    float depthBias, float slopeScaleDepthBias,
                                                       const StencilKeyParams& stencil);
        void IssueInstancedDraw(WGPURenderPassEncoder pass, const InstancedDrawCommand& command,
                              ReplayState& state);

        WGPUShaderModule instancedShader_ = nullptr;
        /// REMED-GFX-212: the position+colour Instanced3D module, used when the geometry stride's
        /// packed layout carries a COLOR0 element. WebGPU requires every shader vertex input to be
        /// backed by a WGPUVertexAttribute, so a position-only geometry stride and a
        /// position+colour one need different modules -- the same split the ordinary route already
        /// makes between colored3d.wgsl and textured3d.wgsl.
        WGPUShaderModule instancedColoredShader_ = nullptr;
        std::unordered_map<std::uint64_t, WGPURenderPipeline> instancedPipelines_;
        std::vector<InstancedDrawCommand> instancedDrawCommands_;

        // pbr3d.wgsl -- PbrEffect's real glTF 2.0 metallic-roughness BRDF (GGX distribution +
        // Smith-Schlick-GGX visibility + Schlick Fresnel), ported from
        // EasyGLRenderer::EnsurePbrProgram()'s GLSL shader. UNSKINNED only (stride 48,
        // VertexPositionNormalTangentTexture) -- this renderer has no skinning shader variant at
        // all yet (see needsUnsupportedEffect's own skinned gate in DrawPrimitivesEx()), so
        // SkinnedPbrEffect (stride 68) is a separate, pre-existing, out-of-scope gap and keeps
        // falling back exactly as it did before. Genuinely new bind-group shapes on both sides:
        // group 0 needs a THIRD uniform buffer (PbrFactors: metallic/roughness, alpha coverage,
        // and glTF colour-space flags -- the
        // existing 128-byte Uniforms and 272-byte LitLightParams blocks are both already fully
        // packed and are reused verbatim via the existing FillExtUniforms()/
        // FillLitLightUniforms() helpers), and group 1 needs FIVE textures (base color, normal,
        // metallic-roughness, emissive, occlusion) behind one shared sampler, each falling back to
        // a 1x1 default texture (matching EasyGLRenderer's own
        // EnsureDefaultFlatNormalTexture()/EnsureDefaultWhiteTexture() "map absent" convention)
        // when PbrEffect leaves that map unbound. Fog remains deferred like the other WebGPU 3D
        // shaders; alpha coverage stays in this PBR shader for glTF MASK draws.
        struct PbrDrawCommand
        {
            /// WEBGPU-83: stencil state baked into this draw's pipeline + its dynamic reference.
            StencilKeyParams stencil{};
            int stencilRef = 0;
            std::vector<std::uint8_t> vertexData;
            std::vector<std::uint8_t> indexData;
            /// plans/plan_gltf.md GLTF-465: true for stride 60, the record that carries COLOR_0.
            bool colored = false;
            bool indexed = false;
            bool index32 = false;
            std::uint32_t vertexCount = 0;
            std::uint32_t indexCount = 0;
            std::uint32_t firstIndex = 0;
            std::int32_t baseVertex = 0;
            WGPUPrimitiveTopology topology = WGPUPrimitiveTopology_TriangleList;
            /// WEBGPU-205: 44 floats. The 40-float block was exactly full (128 base
            /// + 32 fog tail), and the per-slot LOD bias needs a channel of its own.
            std::array<float, 44> uniforms{};
            std::array<float, 68> lightUniforms{};
            /// plans/plan_gltf.md GLTF-344: 76 floats (304 bytes) -- the base 56 plus specular F0/factor
            /// and the two specular map transforms.
            std::array<float, 76> pbrFactors{};
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
            /// REMED-GFX-116: the complete GraphicsDevice.Viewport active at this draw's own
            /// public call, captured by value. Never resolved from live state during replay.
            WebGPUViewportSnapshot viewport{};
            /// REMED-GFX-146: the complete scissor state (ScissorTestEnable plus the four
            /// rectangle components) active at this draw's own public call, captured by
            /// value. Never resolved from live state during replay.
            WebGPUScissorSnapshot scissor{};
            WebGPUSampledTextureEXT baseColorTexture;
            WebGPUSampledTextureEXT normalMap;
            WebGPUSampledTextureEXT metallicRoughnessMap;
            WebGPUSampledTextureEXT emissiveMap;
            WebGPUSampledTextureEXT occlusionMap;
            /// plans/plan_gltf.md GLTF-344: KHR_materials_specular's strength and colour maps.
            WebGPUSampledTextureEXT specularMap;
            WebGPUSampledTextureEXT specularColorMap;
            int textureFilter = 0;
            int addressU = 1;
            int addressV = 1;
            int addressW = 1;  ///< WEBGPU-160: the third addressing axis.
            int maxMipLevel = 0;  ///< WEBGPU-161: SamplerState.MaxMipLevel.
            int maxAnisotropy = 4;  ///< WEBGPU-82: per-slot SamplerState anisotropy
        };
        void CreatePbrResources();
        void DestroyPbrResources();
        /// Lazily creates the 1x1 default white / flat-normal fallback textures used whenever
        /// PbrEffect leaves an optional map (normal/metallic-roughness/emissive/occlusion)
        /// unbound. Idempotent; safe to call from every QueuePbrDraw().
        void EnsurePbrDefaultTextures();
        [[nodiscard]] WGPURenderPipeline GetOrCreatePipelinePbr3D(bool colored,
                                                                  WGPUPrimitiveTopology topology,
                                                                    WGPUIndexFormat stripIndexFormat,
                                                                    bool depthTest, bool depthWrite,
                                                                    int depthFunc,
                                                       bool blend, const BlendKeyParams& blendParams,
                                                       int cullMode, bool wireframe,
                                                       float depthBias, float slopeScaleDepthBias,
                                                       const StencilKeyParams& stencil);
        void QueuePbrDraw(const IVertexBufferRenderer& vb, const IIndexBufferRenderer* ib,
                          const Matrix& world, const Matrix& view, const Matrix& projection,
                          PrimitiveType primitive, int primitiveCount, const GpuDrawParams& params);
        void IssuePbrDraw(WGPURenderPassEncoder pass, const PbrDrawCommand& command,
                              ReplayState& state);

        WGPUShaderModule pbrShader_ = nullptr;
        /// plans/plan_gltf.md GLTF-465: the stride-60 twin, whose vertex input declares COLOR_0.
        WGPUShaderModule pbrColorShader_ = nullptr;
        WGPUBindGroupLayout pbrBindGroupLayout0_ = nullptr;  ///< group 0: Uniforms + LitLightParams + PbrFactors UBOs
        WGPUBindGroupLayout pbrBindGroupLayout1_ = nullptr;  ///< group 1: sampler + 5 textures
        WGPUPipelineLayout pbrPipelineLayout_ = nullptr;
        std::unordered_map<std::uint64_t, WGPURenderPipeline> pbrPipelines_;
        /// plans/plan_gltf.md GLTF-465: the stride-60 pipelines; a separate cache because the vertex
        /// layout and the shader module both differ, so the key alone cannot separate them.
        std::unordered_map<std::uint64_t, WGPURenderPipeline> pbrColorPipelines_;
        std::vector<PbrDrawCommand> pbrDrawCommands_;
        std::unique_ptr<WebGPUTextureRenderer> pbrDefaultWhiteTexture_;
        std::unique_ptr<WebGPUTextureRenderer> pbrDefaultFlatNormalTexture_;

        // plans/plan_cnj.md Phase 14J: closing this renderer's pre-existing "no skinning shader at all"
        // gap for SkinnedEffect -- both PreferPerPixelLighting lighting variants (matching every
        // other renderer's own established "two skinned shader variants" convention) plus
        // SkinnedEffect.VertexColorEnabled (stride-56 vertex colour on skinned meshes). Ported
        // from EasyGLRenderer::EnsureSkinnedProgram()/EnsureSkinnedVertexLitProgram()'s GLSL
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
        // unbound" precedent for stride 52 in EasyGLRenderer::ApplyLayout), the stride-52
        // and stride-56 cases each get their own shader module pair (mirrors this renderer's own
        // existing texturedShader_/coloredTexturedShader_ precedent for the analogous stride-20/24
        // split), all four sharing skinnedBindGroupLayout_/skinnedPipelineLayout_ (identical UBO
        // shape). No fog/alpha-test wired in, matching every other WebGPU 3D shader's own
        // deliberate deferral (SkinnedEffect never sets GpuDrawParams::alphaTest away from its
        // always-pass default).
        struct SkinnedDrawCommand
        {
            /// WEBGPU-83: stencil state baked into this draw's pipeline + its dynamic reference.
            StencilKeyParams stencil{};
            int stencilRef = 0;
            std::vector<std::uint8_t> vertexData;
            std::vector<std::uint8_t> indexData;
            bool indexed = false;
            bool index32 = false;
            std::uint32_t vertexCount = 0;
            std::uint32_t indexCount = 0;
            std::uint32_t firstIndex = 0;
            std::int32_t baseVertex = 0;
            WGPUPrimitiveTopology topology = WGPUPrimitiveTopology_TriangleList;
            /// WEBGPU-205: 44 floats. The 40-float block was exactly full (128 base
            /// + 32 fog tail), and the per-slot LOD bias needs a channel of its own.
            std::array<float, 44> uniforms{};
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
            /// REMED-GFX-116: the complete GraphicsDevice.Viewport active at this draw's own
            /// public call, captured by value. Never resolved from live state during replay.
            WebGPUViewportSnapshot viewport{};
            /// REMED-GFX-146: the complete scissor state (ScissorTestEnable plus the four
            /// rectangle components) active at this draw's own public call, captured by
            /// value. Never resolved from live state during replay.
            WebGPUScissorSnapshot scissor{};
            WebGPUSampledTextureEXT texture;
            int textureFilter = 0;
            int addressU = 1;
            int addressV = 1;
            int addressW = 1;  ///< WEBGPU-160: the third addressing axis.
            int maxMipLevel = 0;  ///< WEBGPU-161: SamplerState.MaxMipLevel.
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
                                                       float depthBias, float slopeScaleDepthBias,
                                                       const StencilKeyParams& stencil);
        void QueueSkinnedDraw(const IVertexBufferRenderer& vb, const IIndexBufferRenderer* ib,
                              const Matrix& world, const Matrix& view, const Matrix& projection,
                              PrimitiveType primitive, int primitiveCount, const GpuDrawParams& params);
        void IssueSkinnedDraw(WGPURenderPassEncoder pass, const SkinnedDrawCommand& command,
                              ReplayState& state);

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
        // EasyGLRenderer::EnsurePbrSkinnedProgram()'s combination exactly -- including its
        // own (different from unskinned PbrEffect) normal/tangent transform, which uses the raw
        // World rotation directly (mat3(uWorld)*(skinMat3*normal)) rather than the precomputed
        // inverse-transpose normal matrix pbr3d.wgsl's own unskinned vertex shader uses. No vertex
        // colour (SkinnedPbrEffect has no VertexColorEnabled, matching the EasyGL reference).
        struct SkinnedPbrDrawCommand
        {
            /// WEBGPU-83: stencil state baked into this draw's pipeline + its dynamic reference.
            StencilKeyParams stencil{};
            int stencilRef = 0;
            std::vector<std::uint8_t> vertexData;
            std::vector<std::uint8_t> indexData;
            /// plans/plan_gltf.md GLTF-463/GLTF-465: true for stride 80.
            bool colored = false;
            bool indexed = false;
            bool index32 = false;
            std::uint32_t vertexCount = 0;
            std::uint32_t indexCount = 0;
            std::uint32_t firstIndex = 0;
            std::int32_t baseVertex = 0;
            WGPUPrimitiveTopology topology = WGPUPrimitiveTopology_TriangleList;
            /// WEBGPU-205: 44 floats. The 40-float block was exactly full (128 base
            /// + 32 fog tail), and the per-slot LOD bias needs a channel of its own.
            std::array<float, 44> uniforms{};
            std::array<float, 68> lightUniforms{};
            /// plans/plan_gltf.md GLTF-344: 76 floats (304 bytes) -- the base 56 plus specular F0/factor
            /// and the two specular map transforms.
            std::array<float, 76> pbrFactors{};
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
            /// REMED-GFX-116: the complete GraphicsDevice.Viewport active at this draw's own
            /// public call, captured by value. Never resolved from live state during replay.
            WebGPUViewportSnapshot viewport{};
            /// REMED-GFX-146: the complete scissor state (ScissorTestEnable plus the four
            /// rectangle components) active at this draw's own public call, captured by
            /// value. Never resolved from live state during replay.
            WebGPUScissorSnapshot scissor{};
            WebGPUSampledTextureEXT baseColorTexture;
            WebGPUSampledTextureEXT normalMap;
            WebGPUSampledTextureEXT metallicRoughnessMap;
            WebGPUSampledTextureEXT emissiveMap;
            WebGPUSampledTextureEXT occlusionMap;
            /// plans/plan_gltf.md GLTF-344: KHR_materials_specular's strength and colour maps.
            WebGPUSampledTextureEXT specularMap;
            WebGPUSampledTextureEXT specularColorMap;
            int textureFilter = 0;
            int addressU = 1;
            int addressV = 1;
            int addressW = 1;  ///< WEBGPU-160: the third addressing axis.
            int maxMipLevel = 0;  ///< WEBGPU-161: SamplerState.MaxMipLevel.
            int maxAnisotropy = 4;  ///< WEBGPU-82: per-slot SamplerState anisotropy
        };
        void CreateSkinnedPbrResources();
        void DestroySkinnedPbrResources();
        [[nodiscard]] WGPURenderPipeline GetOrCreatePipelineSkinnedPbr3D(bool colored,
                                                                         WGPUPrimitiveTopology topology,
                                                                           WGPUIndexFormat stripIndexFormat,
                                                                           bool depthTest, bool depthWrite,
                                                                           int depthFunc,
                                                       bool blend, const BlendKeyParams& blendParams,
                                                       int cullMode, bool wireframe,
                                                       float depthBias, float slopeScaleDepthBias,
                                                       const StencilKeyParams& stencil);
        void QueueSkinnedPbrDraw(const IVertexBufferRenderer& vb, const IIndexBufferRenderer* ib,
                                 const Matrix& world, const Matrix& view, const Matrix& projection,
                                 PrimitiveType primitive, int primitiveCount, const GpuDrawParams& params);
        void IssueSkinnedPbrDraw(WGPURenderPassEncoder pass, const SkinnedPbrDrawCommand& command,
                              ReplayState& state);

        WGPUShaderModule skinnedPbrShader_ = nullptr;
        /// plans/plan_gltf.md GLTF-463/GLTF-465: the stride-80 twin.
        WGPUShaderModule skinnedPbrColorShader_ = nullptr;
        WGPUBindGroupLayout skinnedPbrBindGroupLayout0_ = nullptr;  ///< group 0: Uniforms + LitLightParams + PbrFactors + SkinningParams UBOs
        WGPUPipelineLayout skinnedPbrPipelineLayout_ = nullptr;     ///< group 0 (above) + group 1 (pbrBindGroupLayout1_ reused)
        std::unordered_map<std::uint64_t, WGPURenderPipeline> skinnedPbrPipelines_;
        /// plans/plan_gltf.md GLTF-463/GLTF-465: the stride-80 pipelines.
        std::unordered_map<std::uint64_t, WGPURenderPipeline> skinnedPbrColorPipelines_;
        std::vector<SkinnedPbrDrawCommand> skinnedPbrDrawCommands_;

        /**
         * @brief WEBGPU-76: one deferred custom-`ShaderEffect` draw, captured by value at its public
         * `DrawPrimitivesEx`/`DrawIndexedPrimitivesEx` call and replayed later like every other
         * family. The uniform block bytes are captured per draw (a second draw of the same effect
         * with different `SetUniform*` values must not see this one's), exactly as the stock
         * families capture their fixed `uniforms` array; the `effect` pointer supplies only the
         * per-effect shader modules, layouts and pipeline cache, which do not change between draws.
         */
        struct CustomEffectDrawCommand
        {
            std::vector<std::uint8_t> vertexData;
            std::vector<std::uint8_t> indexData;   ///< empty for a non-indexed draw
            bool indexed = false;
            bool index32 = false;
            std::uint32_t vertexCount = 0;
            std::uint32_t indexCount = 0;
            std::uint32_t firstIndex = 0;
            std::int32_t baseVertex = 0;
            std::uint64_t arrayStride = 0;                     ///< the bound vertex's byte stride
            std::vector<CustomEffectVertexAttr> attributes;    ///< derived from the VertexDeclaration
            WGPUPrimitiveTopology topology = WGPUPrimitiveTopology_TriangleList;
            std::vector<std::uint8_t> uniforms;                ///< the effect's uniform block, by value
            WebGPUSampledTextureEXT texture;                   ///< captured unit-0 texture, if any
            WebGPUEffectRenderer* effect = nullptr;            ///< program/layout/pipeline-cache owner
            bool depthTest = false;
            bool depthWrite = false;
            int depthFunc = 3;  ///< XNA CompareFunction ordinal; 3 = LessEqual
            bool blend = true;
            BlendKeyParams blendParams{};
            /// WEBGPU-143 MRT: the per-slot ColorWriteChannels (slot i masks `@location(i)`); a custom
            /// effect that writes N outputs honours each attachment's own BlendState mask.
            std::array<int, 4> colorWriteChannels{15, 15, 15, 15};
            int cullMode = 0;
            /// WEBGPU-153: this draw's own FillMode, captured like every other pipeline
            /// input. A custom WGSL effect draws triangles like any other route, so a
            /// wireframe request must reach it too rather than silently filling.
            bool wireframe = false;
            float depthBias = 0.0f;
            float slopeScaleDepthBias = 0.0f;
            WebGPUViewportSnapshot viewport{};
            WebGPUScissorSnapshot scissor{};
        };
        std::vector<CustomEffectDrawCommand> customEffectDrawCommands_;
        /// WEBGPU-76: builds the deferred custom-effect command from a `DrawPrimitivesEx` /
        /// `DrawIndexedPrimitivesEx` call whose `params.customEffectRenderer` is set.
        void QueueCustomEffectDraw(const IVertexBufferRenderer& vb, const IIndexBufferRenderer* ib,
                                   const Matrix& world, const Matrix& view, const Matrix& projection,
                                   PrimitiveType primitive, int primitiveCount, const GpuDrawParams& params);
        /// WEBGPU-76/86: replays one custom-effect command — builds/fetches its pipeline (keyed by
        /// the concrete pass state incl. `destination.colorAttachmentCount`, 1 for a single target
        /// and 2..4 for an MRT set) and issues the draw. The fragment must write `@location(0..N-1)`.
        void IssueCustomEffectDraw(WGPURenderPassEncoder pass, const CustomEffectDrawCommand& command,
                                   const PassDestination& destination, ReplayState& state);
    };
}
