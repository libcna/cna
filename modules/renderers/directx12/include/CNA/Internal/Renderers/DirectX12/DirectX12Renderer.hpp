#pragma once

// plans/plan_dx.md Phase DX17: production D3D12 renderer. DX-237 records clears, draws, resolves,
// SpriteBatch flushes and per-draw constants into one command list per frame slot, retaining every
// referenced COM object until that slot's fence completes. Synchronous upload/readback work uses a
// separate immediate command list pending DX-238's staging-ring work.
//
// Windows-only (see CMakeLists.txt's FATAL_ERROR guard for non-Windows CNA_GRAPHICS_RENDERER=D3D12).

#include "CNA/Internal/Renderers/Common/IGraphicsRenderer.hpp"
#include "CNA/Internal/Renderers/Common/PlatformRendererSurfaceState.hpp"
#include "CNA/Internal/Renderers/D3DCommon/ID3DDeviceRecoverableEXT.hpp"
#include "D3D12DescriptorHeaps.hpp"
#include "D3D12ResourceStateTracker.hpp"
#include "D3D12PipelineStateCache.hpp"
#include "D3D12RootSignatureCache.hpp"
#include "D3D12SamplerCache.hpp"
#include "D3D12TextureCube.hpp"

#include <d3d12.h>
#include <dxgi1_5.h>
#include <wrl/client.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <vector>
#include <memory>

namespace CNA::Internal::Renderers::DirectX12
{
    using Microsoft::WRL::ComPtr;

    /**
     * @brief D3D12 graphics renderer with two frame slots and explicit resource synchronization.
     *
     * Windowed devices use the swap-chain back-buffer index; headless devices rotate the same two
     * slots at Present(). Each slot owns its allocator, command list, constant arena and retained
     * resource set. Uploads and CPU readbacks remain explicit synchronization boundaries until
     * DX-238 replaces their one-shot staging resources with frame-owned upload ranges.
     */
    class DirectX12Renderer final : public IGraphicsRenderer
    {
    public:
        /// Matches Vulkan's own `MaxFramesInFlight` convention (VulkanRenderer.hpp) for
        /// this project's established frame-in-flight depth -- D3D12 needs its own explicit
        /// per-frame command-allocator set (DX-104), unlike D3D11's implicit driver-managed model.
        static constexpr int kFramesInFlight = 2;

        explicit DirectX12Renderer(const GraphicsRendererCreateArgs& args);
        ~DirectX12Renderer() override;

        DirectX12Renderer(const DirectX12Renderer&) = delete;
        DirectX12Renderer& operator=(const DirectX12Renderer&) = delete;

        void Clear(float r, float g, float b, float a) override;
        /// DX-116/DX-237: submits the current frame and presents a real swap chain when available;
        /// a headless device rotates frame slots without attempting presentation.
        void Present() override;
        /// plans/plan_dx.md DX-203: GraphicsDevice.ReferenceStencil, which XNA exposes as a standalone
        /// property rather than as part of DepthStencilState. On D3D12 it is not pipeline state at
        /// all -- it is OMSetStencilRef() on the command list -- so setting it only records the
        /// value here and every subsequent draw applies it, with no PSO rebuild and no cache entry.
        void SetReferenceStencil(int referenceStencil) override;
        /// plans/plan_dx.md DX-204: GraphicsDevice.BlendFactor -- the constant `Blend::BlendFactor` and
        /// `Blend::InverseBlendFactor` sample. Like the stencil reference, it is command-list state
        /// on D3D12 (`OMSetBlendFactor`), not part of the pipeline state, so setting it costs no PSO
        /// rebuild and adds no cache entry; every subsequent draw records it.
        void SetBlendFactor(float r, float g, float b, float a) override;
        /// plans/plan_dx.md DX-201: GraphicsDevice.ScissorRectangle. Stored only -- consumed at each
        /// RSSetScissorRects site through GetEffectiveScissorEXT(), for the same reason SetViewport()
        /// stores rather than applies: each draw records the effective value into the active frame
        /// command list, so there is no persistent immediate context to update here.
        void SetScissorRect(int x, int y, int w, int h) override;
        /// plans/plan_dx.md DX-205: real GPU->CPU readback of this device's back buffer, the same
        /// contract D3D11's own DX-28 override implements. The source is the swap chain's current
        /// back buffer when there is a swap chain, and DX-241's implicit off-screen back buffer
        /// otherwise, so a windowless device reads what it drew instead of throwing
        /// IGraphicsRenderer's "not implemented in this renderer". @p x / @p y are top-left in
        /// back-buffer pixels and @p pixels must hold w*h*4 RGBA8 bytes; GraphicsDevice has already
        /// validated the rectangle against PresentationParameters, and rows/columns that still fall
        /// outside the real resource are zero-filled exactly as D3D11 zero-fills them.
        void ReadBackbuffer(int x, int y, int w, int h, uint8_t* pixels) override;

        /// plans/plan_dx.md DX-212: TRUE, because the source handed to CreateEffectRenderer() genuinely
        /// determines the pixels here. The default is FALSE, which means "this renderer ACCEPTS an
        /// effect and keeps rendering with its own fixed path" -- the SOFTWARE/HEADLESS answer, and
        /// the reason a caller is told to ask this IN ADDITION to GraphicsCapability::CustomEffects.
        /// D3D12EffectRenderer::CompileProgram() runs a real D3DCompile() on the caller's HLSL and binds the resulting shader
        /// objects, so a post-process pass that believes its shader ran is right.
        [[nodiscard]] bool ExecutesShaderEffectSourceEXT() const override { return true; }

        /**
         * @brief Reports the complete runtime-backed D3D12 capability surface.
         *
         * @param capability Capability to query.
         * @return true only when this renderer and its current device implement the capability.
         */
        [[nodiscard]] bool SupportsCapability(CNA::GraphicsCapability capability) const override;
        void GetViewportSize(int& width, int& height) override;
        /**
         * @brief Returns the physical back-buffer rectangle used for logical presentation.
         * @param x Receives the rectangle's left edge in drawable pixels.
         * @param y Receives the rectangle's top edge in drawable pixels.
         * @param width Receives the rectangle's width in drawable pixels.
         * @param height Receives the rectangle's height in drawable pixels.
         */
        void GetDefaultViewportRect(int& x, int& y, int& width, int& height) override;
        void SetVirtualResolution(int width, int height) override;
        /**
         * @brief Rebuilds the default back-buffer render surface with a device-supported sample count.
         * @param requestedMultiSampleCount Preferred number of samples; zero or one disables MSAA.
         * @return The sample count actually applied, or zero when multisampling is disabled.
         */
        int ApplyMultiSampleCount(int requestedMultiSampleCount) override;
        /** @brief Returns the sample count actually used by the default render surface. */
        [[nodiscard]] int GetMultiSampleCount() const override { return appliedMultiSampleCount_; }
        /**
         * @brief Maps a requested sample count to the count currently applied by this renderer.
         * @param requestedMultiSampleCount The caller's requested count.
         * @return The current device-clamped count.
         */
        [[nodiscard]] int GetAppliedMultiSampleCountEXT(
            int requestedMultiSampleCount) const override
        {
            (void) requestedMultiSampleCount;
            return appliedMultiSampleCount_;
        }
        /**
         * @brief Reports the fixed XNA surface format of the DXGI back-buffer destination.
         * @param requestedFormat The caller's requested SurfaceFormat ordinal.
         * @return SurfaceFormat::Color, matching the actual R8G8B8A8_UNORM resource.
         */
        [[nodiscard]] int GetAppliedBackBufferFormatEXT(int requestedFormat) const override;
        /**
         * @brief Reports the fixed XNA depth format of the default D3D12 depth resource.
         * @param requestedFormat The caller's requested DepthFormat ordinal.
         * @return DepthFormat::Depth24Stencil8, matching the actual D24S8 resource.
         */
        [[nodiscard]] int GetAppliedDepthStencilFormatEXT(int requestedFormat) const override;
        void SetPresentationMode(int mode) override;
        void OnSurfaceChanged(const RendererSurfaceInfo& surface) override;
        /// DX-116: mirrors DirectX11Renderer::SetSwapInterval exactly -- sync interval is
        /// renderer state applied at the next Present(), not a direct D3D12 API call ahead of time.
        void SetSwapInterval(int interval) override;
        /** @brief Enables registration of subsequently-created resources for device recovery. */
        void SetContextRecoveryEnabled(bool enabled) override;
        /** @brief Reports whether the renderer currently accepts drawing commands. */
        [[nodiscard]] bool CanBeginDrawEXT() const override { return !deviceLost_; }
        /** @brief Enters the deterministic lost-device state used by lifecycle tests. */
        void DebugSimulateContextLoss() override;
        /** @brief Recreates the device and registered resources after a simulated loss. */
        void DebugRestoreContext() override;
        /**
         * @brief Returns the most recently requested DXGI presentation interval.
         * @return The exact interval most recently passed to SetSwapInterval.
         */
        CNAEXT [[nodiscard]] int GetSwapIntervalEXT() const override { return swapInterval_; }
        /**
         * @brief Maps a platform-window point into the current logical presentation.
         * @param windowX Window-space X coordinate.
         * @param windowY Window-space Y coordinate.
         * @param logX Receives the logical X coordinate.
         * @param logY Receives the logical Y coordinate.
         * @return true when the point lies inside the presented image.
         */
        bool TransformWindowToLogical(float windowX, float windowY,
                                      float& logX, float& logY) const override;
        /**
         * @brief Maps a logical point into platform-window coordinates.
         * @param logX Logical X coordinate.
         * @param logY Logical Y coordinate.
         * @param windowX Receives the window-space X coordinate.
         * @param windowY Receives the window-space Y coordinate.
         * @return true when presentation geometry is available.
         */
        bool TransformLogicalToWindow(float logX, float logY,
                                      float& windowX, float& windowY) const override;


        /** @brief Classifies core XNA surface formats backed by native D3D12 storage. */
        [[nodiscard]] RendererFormatVerdict ClassifySurfaceFormatEXT(int surfaceFormat) const override;
        /**
         * @brief Classifies XNA render-target formats using actual D3D12 device support.
         * @param surfaceFormat SurfaceFormat ordinal.
         * @return Supported only when the format is an XNA render-target format that this device
         *         can render to and sample.
         */
        [[nodiscard]] RendererFormatVerdict ClassifyRenderTargetFormatEXT(int surfaceFormat) const override;
        /** @brief Restricts Color-shaped transfers to actual Color storage. */
        [[nodiscard]] RendererFormatVerdict ClassifyColorTransferFormatEXT(int surfaceFormat) const override;
        /**
         * @brief Reports whether D3D12 transfers the specified surface format as compressed blocks.
         *
         * @param surfaceFormat SurfaceFormat ordinal.
         * @return true for the core XNA Dxt1, Dxt3, and Dxt5 formats.
         */
        [[nodiscard]] bool IsCompressedTransferFormatEXT(int surfaceFormat) const override;
        /**
         * @brief Reports whether D3D12 accepts compressed block transfers for cube faces.
         *
         * @param surfaceFormat SurfaceFormat ordinal.
         * @return true for the core XNA Dxt1, Dxt3, and Dxt5 formats.
         */
        [[nodiscard]] bool IsCompressedCubeTransferFormatEXT(int surfaceFormat) const override;
        /**
         * @brief Keeps supported compressed content in its native block-compressed form.
         *
         * @return true because D3D12 uploads XNA DXT blocks without CPU decompression.
         */
        [[nodiscard]] bool LoadsCompressedContentNativelyEXT() const override;
        std::unique_ptr<ITextureRenderer> CreateTexture(const ImageData& data) override;
        /// DX-111 (closing env_map3d): real D3D12TextureCubeRenderer, no longer the inherited
        /// default (IGraphicsRenderer::CreateTextureCube() -> nullptr).
        std::unique_ptr<ITextureCubeRenderer> CreateTextureCube(int size, bool mipMap, int surfaceFormat) override;
        /// DX-122: real D3D12Texture3DRenderer, no longer the inherited default (-> nullptr).
        std::unique_ptr<ITexture3DRenderer> CreateTexture3D(int w, int h, int depth, bool mipMap, int surfaceFormat) override;
        std::unique_ptr<ISpriteBatchRenderer> CreateSpriteBatch() override;
        /// DX-120: real D3D12OcclusionQueryRenderer, no longer the inherited default (-> nullptr).
        std::unique_ptr<IOcclusionQueryRenderer> CreateOcclusionQuery() override;
        /// DX-121: real D3D12EffectRenderer, no longer the inherited default (-> nullptr). Mirrors
        /// DirectX11Renderer::CreateEffectRenderer's own convention: if both sources are
        /// non-empty, compiles immediately and returns the renderer regardless of compile success
        /// (the caller checks IsValid()/GetCompileError()).
        std::unique_ptr<IEffectRenderer> CreateEffectRenderer(const std::string& vertSrc,
                                                             const std::string& fragSrc) override;

        /// DX-117: real D3D12RenderTargetRenderer, no longer the inherited default (-> nullptr).
        std::unique_ptr<IRenderTargetRenderer> CreateRenderTarget2D(int w, int h, int depthFormat,
                                                                    bool preserveContents = false,
                                                                    bool mipMap = false,
                                                                    int multiSampleCount = 0) override;
        /**
         * @brief Creates a D3D12 2D render target in the requested XNA surface format.
         * @param w Width in pixels.
         * @param h Height in pixels.
         * @param depthFormat DepthFormat ordinal.
         * @param preserveContents Whether previous contents should be preserved.
         * @param mipMap Whether to allocate a full mip chain.
         * @param multiSampleCount Requested sample count.
         * @param surfaceFormat SurfaceFormat ordinal.
         * @return The native D3D12 render target.
         */
        std::unique_ptr<IRenderTargetRenderer> CreateRenderTarget2DEXT(
            int w, int h, int depthFormat, bool preserveContents, bool mipMap,
            int multiSampleCount, int surfaceFormat) override;
        void SetRenderTarget2D(IRenderTargetRenderer* rt) override;
        /// DX-117: real D3D12RenderTargetCubeRenderer, no longer the inherited default (-> nullptr).
        std::unique_ptr<IRenderTargetCubeRenderer> CreateRenderTargetCube(int size, int depthFormat,
                                                                          bool preserveContents = false,
                                                                          bool mipMap = false,
                                                                          int multiSampleCount = 0) override;
        /**
         * @brief Creates a D3D12 cube render target in the requested XNA surface format.
         * @param size Face width and height in pixels.
         * @param depthFormat DepthFormat ordinal.
         * @param preserveContents Whether previous contents should be preserved.
         * @param mipMap Whether to allocate a full mip chain.
         * @param multiSampleCount Requested sample count.
         * @param surfaceFormat SurfaceFormat ordinal.
         * @return The native D3D12 cube render target.
         */
        std::unique_ptr<IRenderTargetCubeRenderer> CreateRenderTargetCubeEXT(
            int size, int depthFormat, bool preserveContents, bool mipMap,
            int multiSampleCount, int surfaceFormat) override;
        /// DX-117: real MRT -- binds every target's own RTV (up to 8, D3D12's own
        /// D3D12_SIMULTANEOUS_RENDER_TARGET_COUNT, though this project's shared GraphicsDevice
        /// code already caps at 4, MAX_RENDERTARGET_BINDINGS). Draws themselves remain
        /// single-target (index 0) -- no CNA shader declares more than one SV_Target output; only
        /// Clear() genuinely clears every bound target independently, matching D3D11's own DX-46
        /// proof shape.
        void SetRenderTargets(const RenderTargetBindingDescriptor* renderTargets,
                              int count) override;
        /// REMED-GFX-134: overrides IGraphicsRenderer's default so a bound cube face is TRACKED and
        /// therefore finalized (MSAA resolve + mip regeneration) when the binding changes.
        void SetRenderTargetCubeFace(IRenderTargetCubeRenderer* rt, int face) override;

        void ClearColorAndDepth(float r, float g, float b, float a, float depth) override;
        void ClearDepth(float depth) override;
        void ClearStencil(int stencil) override;
        void ClearDepthAndStencil(float depth, int stencil) override;
        void ClearColorAndStencil(float r, float g, float b, float a, int stencil) override;
        void ClearColorDepthAndStencil(float r, float g, float b, float a, float depth, int stencil) override;
        /// DX-146: shared implementation behind Clear()'s 5 combo variants above -- clears the bound
        /// color target(s) (including DX-117's real MRT set) and/or the bound depth-stencil view,
        /// clearing only what @p clearColor / @p depthStencilFlags actually ask for. Depth/stencil
        /// is a genuine no-op (not an error) when no DSV is bound, matching XNA's own semantics and
        /// DirectX11Renderer's own `if (currentDSV_)` behavior. @p what names the calling variant
        /// for error messages.
        void ClearImpl(bool clearColor, float r, float g, float b, float a,
                       D3D12_CLEAR_FLAGS depthStencilFlags, float depth, int stencil,
                       const char* what);

        void SetDepthTestEnabled(bool enabled) override;
        void SetBlendEnabled(bool enabled) override;
        void SetDepthWriteEnabled(bool enabled) override;

        /// DX-118: real, runtime-settable BlendState -- updates the tracked current-blend fields
        /// fed into every psoCache_.GetOrCreate() call site's D3D12PipelineStateDesc, so a draw
        /// after this call genuinely gets a differently-blended PSO (D3D12 bakes blend state into
        /// the PSO itself, unlike D3D11's separate ID3D11BlendState objects -- design decision 4's
        /// own D3DStateMapping tables are reused unchanged for the raw ordinal mapping).
        void ApplyBlendState(int colorSrcBlend, int alphaSrcBlend,
                             int colorDstBlend, int alphaDstBlend,
                             int colorBlendFunc, int alphaBlendFunc,
                             const BlendWriteState& writeState) override;
        /// DX-118: real, runtime-settable DepthStencilState -- depthEnable/depthWriteEnable/
        /// depthFunc feed the PSO cache key exactly like ApplyBlendState above. Stencil fields are
        /// deliberately NOT threaded through yet -- matches D3D12PipelineStateCache's own already-
        /// documented "stencil deliberately NOT part of this first key/desc" scope (DX-107); a real,
        /// honest follow-up gap, not silently dropped.
        void ApplyDepthStencilState(bool depthEnable, bool depthWriteEnable,
                                    int depthFunc,
                                    bool stencilEnable, int stencilFunc,
                                    int stencilPass, int stencilFail, int stencilDepthFail,
                                    int stencilMask, int stencilWriteMask, int referenceStencil,
                                    bool twoSidedStencilMode,
                                    int ccwStencilFunc, int ccwStencilPass,
                                    int ccwStencilFail, int ccwStencilDepthFail) override;
        /// DX-118: real, runtime-settable RasterizerState -- cullMode/fillMode feed the PSO cache
        /// key exactly like ApplyBlendState above. scissorTestEnable/depthBias/slopeScaleDepthBias
        /// are deliberately NOT threaded through yet (same documented first-implementation-subset
        /// scope as the stencil fields above) -- a real, honest follow-up gap.
        void ApplyRasterizerState(int cullMode, int fillMode,
                                  bool scissorTestEnable,
                                  float depthBias = 0.0f,
                                  float slopeScaleDepthBias = 0.0f) override;
        /// DX-119: real, runtime-settable per-slot SamplerState -- tracks the given slot's raw XNA
        /// TextureFilter/AddressU/AddressV/MaxAnisotropy ordinals (cheap; D3D12 has no persistent
        /// pipeline sampler-binding state to update immediately, unlike D3D11's PSSetSamplers).
        /// Real resolution into a D3D12SamplerCache descriptor happens at draw time
        /// (GetSamplerGpuHandleEXT), for whichever of the 2 texture slots (0/1) this renderer's
        /// shaders actually sample -- GraphicsDevice.SamplerStates has 16 slots total, but no CNA
        /// stock shader declares more than 2 texture registers (t0/t1), so slots 2-15 are tracked
        /// (harmless, no-op) but never consumed by any draw.
        void ApplySamplerState(int slot, int filter, int addressU, int addressV,
                               int maxAnisotropy) override;
        /// plans/plan_dx.md DX-216: SamplerState.MaxMipLevel (XNA's MOST DETAILED level index, i.e. D3D's
        /// MinLOD) and MipMapLevelOfDetailBias (MipLODBias), tracked per slot; the sampler
        /// descriptor is rebuilt lazily by GetSamplerGpuHandleEXT() on the next draw.
        void ApplySamplerMipState(int slot, int maxMipLevel, float lodBias) override;
        /// DX-216: SamplerState.AddressW -- previously mirrored from AddressV, which the interface
        /// documentation names as the one thing a renderer without W support must not do.
        void ApplySamplerAddressW(int slot, int addressW) override;
        /// REMED-GFX-064: real, runtime-settable GraphicsDevice.Viewport. Stores the sub-region
        /// viewport rect + depth range; each draw records it into the shared active frame list via
        /// GetEffectiveViewportEXT(). Before this task D3D12 never overrode the no-op base
        /// SetViewport and hardcoded a full-target D3D12_VIEWPORT at all four RSSetViewports sites
        /// (+ the sprite path), so a custom Viewport was a total no-op on backbuffer and RT alike.
        void SetViewport(int x, int y, int w, int h, float minDepth, float maxDepth) override;
        /// REMED-GFX-064: the D3D12_VIEWPORT every draw must set -- the custom Viewport stored by
        /// SetViewport() if one was set (unclamped rect, top-left origin, depth clamped to [0,1]),
        /// otherwise the full bound-target rect ({0,0,boundColorWidth_,boundColorHeight_,0,1},
        /// byte-identical to the pre-fix hardcode). Shared by the 4 renderer draw paths and the
        /// D3D12 SpriteBatch flush (which reads it via owner_).
        [[nodiscard]] D3D12_VIEWPORT GetEffectiveViewportEXT() const;
        /**
         * @brief Returns the active SpriteBatch projection size in logical viewport units.
         * @param width Receives the logical viewport width.
         * @param height Receives the logical viewport height.
         */
        CNAEXT void GetSpriteViewportSizeEXT(float& width, float& height) const;

        std::unique_ptr<IVertexBufferRenderer> CreateVertexBuffer(int vertex_capacity) override;
        std::unique_ptr<IIndexBufferRenderer> CreateIndexBuffer16(int index_capacity) override;
        /// DX-109: real 32-bit index buffer -- explicitly overridden. D3D11's own Phase DIRECTX5 fork
        /// found and fixed a real bug where forgetting this override let every 32-bit index-buffer
        /// request silently alias to a 16-bit buffer (IGraphicsRenderer's own default just delegates
        /// to CreateIndexBuffer16); overriding it here from the start avoids repeating that bug.
        std::unique_ptr<IIndexBufferRenderer> CreateIndexBuffer32(int index_capacity) override;

        void DrawColoredPrimitives(const IVertexBufferRenderer& vb,
                                   const Matrix& world, const Matrix& view, const Matrix& projection,
                                   PrimitiveType primitive, int primitiveCount) override;
        void DrawIndexedColoredPrimitives(const IVertexBufferRenderer& vb, const IIndexBufferRenderer& ib,
                                          const Matrix& world, const Matrix& view, const Matrix& projection,
                                          PrimitiveType primitive, int primitiveCount) override;

        /// DX-111 (continued): real effect-aware dispatch for textured3d/colored_textured3d/
        /// lit_textured3d/alpha_test3d -- mirrors DirectX11Renderer::DrawPrimitivesEx's own
        /// priority-chain shape (alpha-test > lit-textured (stride 32) > colored/textured/
        /// colored_textured bundle), minus the dual-tex/env-map/skinned branches a separate
        /// follow-up task still owes (see DrawPrimitivesExImpl's own doc comment). Without this
        /// override, IGraphicsRenderer's own default falls back to DrawColoredPrimitives, which is
        /// stride-16-only and would throw for every one of these variants.
        void DrawPrimitivesEx(const IVertexBufferRenderer& vb,
                              const Matrix& world, const Matrix& view, const Matrix& projection,
                              PrimitiveType primitive, int primitiveCount,
                              const GpuDrawParams& params) override;
        /// Indexed counterpart of DrawPrimitivesEx above.
        void DrawIndexedPrimitivesEx(const IVertexBufferRenderer& vb, const IIndexBufferRenderer& ib,
                                     const Matrix& world, const Matrix& view, const Matrix& projection,
                                     PrimitiveType primitive, int primitiveCount,
                                     const GpuDrawParams& params) override;

        /// DX-111/DX-222: real instanced3d dispatch. Every bound declaration, input slot,
        /// instance step rate and stream-local offset enters the shared pipeline-state cache.
        void DrawInstancedPrimitivesEx(const IVertexBufferRenderer& vb, const IIndexBufferRenderer& ib,
                                       const Matrix& world, const Matrix& view, const Matrix& projection,
                                       PrimitiveType primitive, int primitiveCount, int instanceCount,
                                       const GpuDrawParams& params) override;

        // ---- CNAEXT (DX-102/DX-103/DX-104/DX-105): real device-lifetime accessors for tests and
        // for whichever Phase DX12 task lands next (DX-106 onward) to build on without duplicating
        // this renderer's own device/heap/command-list/fence creation. ----

        /** @brief Real device pointer, or nullptr if construction somehow left it unset. */
        [[nodiscard]] ID3D12Device* GetDeviceEXT() const { return device_.Get(); }
        /** @brief Real direct command queue. */
        [[nodiscard]] ID3D12CommandQueue* GetCommandQueueEXT() const { return commandQueue_.Get(); }
        /** @brief Negotiated feature level (DX-102). */
        [[nodiscard]] D3D_FEATURE_LEVEL GetFeatureLevelEXT() const { return featureLevel_; }
        /** @brief Whether the D3D12 debug layer actually ended up enabled (best-effort, DX-102). */
        [[nodiscard]] bool IsDebugLayerEnabledEXT() const { return debugLayerEnabled_; }
        /** @brief Whether the adapter/factory reported tearing support (DX-102). */
        [[nodiscard]] bool IsTearingSupportedEXT() const { return allowTearingSupported_; }
        /** @brief Whether CreateSwapChainResources() actually produced a usable swap chain --
         *  false on this Wine dev loop today (see class-level doc comment), by design not a throw. */
        [[nodiscard]] bool IsSwapChainAvailableEXT() const { return swapChainAvailable_; }
        /// plans/plan_dx.md DX-201: the scissor rectangle every draw and clear actually records. D3D12 has
        /// no ScissorEnable in D3D12_RASTERIZER_DESC -- the scissor test is always on -- so
        /// "RasterizerState.ScissorTestEnable == false" means exactly "the rectangle is the whole
        /// bound target", which is why this is not part of the pipeline-state key. An unset or
        /// degenerate rectangle also falls back to the full target, byte-identical to the hardcoded
        /// D3D12_RECT{0, 0, boundColorWidth_, boundColorHeight_} it replaces.
        [[nodiscard]] D3D12_RECT GetEffectiveScissorEXT() const;
        /// plans/plan_dx.md DX-207: the sample count of the currently bound colour target, read from the
        /// resource itself rather than tracked alongside it -- a tracked copy is one more thing that
        /// can disagree with reality, and every bind site would have to remember to set it. Returns
        /// 1 when nothing is bound, which is what a single-sample pipeline state wants.
        [[nodiscard]] unsigned int GetBoundColorSampleCountEXT() const;
        /// DX-204: the four floats OMSetBlendFactor takes, for the sprite path, which records its
        /// own command lists.
        [[nodiscard]] const float* GetBlendFactorEXT() const { return currentBlendFactor_; }
        /// DX-210: the value OMSetStencilRef takes, for the sprite path.
        [[nodiscard]] int GetReferenceStencilEXT() const { return currentReferenceStencil_; }
        /// The resource this device's back buffer currently lives in: the swap chain's current
        /// buffer, or DX-241's implicit off-screen one when there is no swap chain. Null only if
        /// the device was never fully constructed.
        [[nodiscard]] ID3D12Resource* GetCurrentBackBufferResourceEXT() const;
        /// Reads one subresource of @p resource back as a tightly packed @p w x @p h RGBA8 buffer,
        /// via a READBACK-heap CopyTextureRegion and a full GPU wait. Returns an empty vector on
        /// any failure -- an honest bail-out, never a fabricated frame. Lives here rather than in
        /// D3D12RenderTargets.cpp (its original home) because ReadBackbuffer() needs the same
        /// mechanism against a resource that is not a render target.
        [[nodiscard]] std::vector<std::uint8_t> ReadbackSubresourceRGBA8EXT(
            ID3D12Resource* resource, UINT subresource, int w, int h);
        /**
         * @brief Reads one texture subresource into tightly packed format-native rows.
         * @param resource Source texture resource.
         * @param subresource Source subresource index.
         * @param w Width in texels.
         * @param h Height in texels.
         * @param bytesPerTexel Native byte count for one texel.
         * @return Tightly packed rows, or an empty vector if the synchronous readback failed.
         */
        [[nodiscard]] std::vector<std::uint8_t> ReadbackSubresourceEXT(
            ID3D12Resource* resource, UINT subresource, int w, int h, int bytesPerTexel);
        /** @brief Real swap chain, or null if IsSwapChainAvailableEXT() is false. */
        [[nodiscard]] IDXGISwapChain3* GetSwapChainEXT() const { return swapChain_.Get(); }

        /** @brief Allocates an RTV descriptor and returns its CPU handle. REMED-GFX-177: the handle
         *  is stable for the lifetime of the allocator (RTV capacity grows by appending a heap
         *  block, never by moving descriptors), and it is reusable once released through
         *  FreeRtvDescriptorEXT().
         *  @return A CPU handle valid until it is passed to FreeRtvDescriptorEXT(). */
        [[nodiscard]] D3D12_CPU_DESCRIPTOR_HANDLE AllocateRtvDescriptorEXT();
        /** @brief REMED-GFX-177: returns an RTV descriptor for reuse once the GPU has finished with
         *  it. A null or foreign handle is ignored.
         *  @param handle A handle previously returned by AllocateRtvDescriptorEXT(). */
        void FreeRtvDescriptorEXT(D3D12_CPU_DESCRIPTOR_HANDLE handle);
        /** @brief Same as AllocateRtvDescriptorEXT(), for the DSV heap.
         *  @return A CPU handle valid until it is passed to FreeDsvDescriptorEXT(). */
        [[nodiscard]] D3D12_CPU_DESCRIPTOR_HANDLE AllocateDsvDescriptorEXT();
        /** @brief Same as FreeRtvDescriptorEXT(), for the DSV heap.
         *  @param handle A handle previously returned by AllocateDsvDescriptorEXT(). */
        void FreeDsvDescriptorEXT(D3D12_CPU_DESCRIPTOR_HANDLE handle);

        /** @brief REMED-GFX-177: allocates a shader-visible CBV/SRV/UAV slot, lets @p createView
         *  write the descriptor into the staging heap, mirrors it into the shader-visible heap and
         *  returns the slot's STABLE INDEX.
         *
         *  Callers keep the index, not a handle: growing the heap replaces the underlying object, so
         *  a stored GPU handle would go stale while an index never does.
         *
         *  @param createView Invoked with the staging CPU handle the descriptor must be created at.
         *  @return A stable descriptor index valid until FreeCbvSrvUavDescriptorEXT(). */
        std::uint32_t CreateCbvSrvUavDescriptorEXT(
            const std::function<void(D3D12_CPU_DESCRIPTOR_HANDLE)>& createView);
        /** @brief REMED-GFX-177: returns a CBV/SRV/UAV slot for reuse once the GPU has finished
         *  with it.
         *  @param index An index previously returned by CreateCbvSrvUavDescriptorEXT(). */
        void FreeCbvSrvUavDescriptorEXT(std::uint32_t index);
        /** @brief REMED-GFX-177: the shader-visible GPU handle for @p index, resolved against the
         *  heap object that is current right now. Resolve at the point of use; never cache it.
         *  @param index An index from CreateCbvSrvUavDescriptorEXT().
         *  @return The GPU handle for SetGraphicsRootDescriptorTable. */
        [[nodiscard]] D3D12_GPU_DESCRIPTOR_HANDLE GetCbvSrvUavGpuHandleEXT(std::uint32_t index) const;
        /** @brief The shader-visible CBV/SRV/UAV heap itself, for SetDescriptorHeaps() calls.
         *  @return The heap currently bound-able for this type. */
        [[nodiscard]] ID3D12DescriptorHeap* GetCbvSrvUavHeapEXT() const
        {
            return heaps_ ? heaps_->cbvSrvUav.ShaderVisibleHeap() : nullptr;
        }
        /** @brief DX-119: the shader-visible SAMPLER heap itself, for SetDescriptorHeaps() calls.
         *  @return The heap currently bound-able for this type. */
        [[nodiscard]] ID3D12DescriptorHeap* GetSamplerHeapEXT() const
        {
            return heaps_ ? heaps_->sampler.ShaderVisibleHeap() : nullptr;
        }
        /** @brief REMED-GFX-177: the shared descriptor-allocator set, captured by every resource
         *  that owns a descriptor so it can free the slot even if it outlives this renderer.
         *  @return The shared allocator set. */
        [[nodiscard]] const std::shared_ptr<D3D12DescriptorHeaps>& GetDescriptorHeapsEXT() const
        {
            return heaps_;
        }
        /** @brief DX-119: resolves a real GPU sampler descriptor handle for texture slot @p slot
         *  (0 or 1, this renderer's only real texture registers) from whatever SamplerState was last
         *  applied via ApplySamplerState(slot, ...) -- defaults to LINEAR/WRAP (this renderer's own
         *  pre-DX-119 hardcoded default) if ApplySamplerState was never called for that slot. */
        D3D12_GPU_DESCRIPTOR_HANDLE GetSamplerGpuHandleEXT(int slot);

        /** @brief DX-106/DX-109: the single, shared per-resource barrier-state tracker every real
         *  D3D12 resource (buffers, textures -- DX-109) registers with and transitions through, so
         *  barrier correctness is enforced in one place rather than ad-hoc per call site. */
        [[nodiscard]] D3D12ResourceStateTracker& GetResourceStateTrackerEXT() { return resourceStates_; }
        /** @brief Registers a live resource for D3D12 device recreation. */
        void RegisterRecoverableResourceEXT(D3DCommon::ID3DDeviceRecoverableEXT* resource);
        /** @brief Removes a live resource from the D3D12 recovery registry. */
        void UnregisterRecoverableResourceEXT(D3DCommon::ID3DDeviceRecoverableEXT* resource) noexcept;
        /** @brief Returns the number of resources currently tracked for recovery. */
        [[nodiscard]] std::size_t GetRecoverableResourceCountEXT() const noexcept
        {
            return recoverableResources_.size();
        }

        /** @brief Real per-frame command allocator for @p frameIndex (0..kFramesInFlight-1). */
        [[nodiscard]] ID3D12CommandAllocator* GetCommandAllocatorEXT(int frameIndex) const
        {
            return commandAllocators_[frameIndex].Get();
        }
        /** @brief The synchronous immediate direct command list used by native tests. */
        [[nodiscard]] ID3D12GraphicsCommandList* GetCommandListEXT() const { return commandList_.Get(); }

        /** @brief Returns the open command list for the current frame, beginning one if necessary. */
        ID3D12GraphicsCommandList* GetFrameCommandListEXT();
        /** @brief Flushes an open frame segment, then resets and returns the synchronous immediate list. */
        ID3D12GraphicsCommandList* BeginImmediateCommandsEXT(
            ID3D12PipelineState* initialState = nullptr);
        /** @brief Allocates and copies one 256-byte-aligned constant range owned by the current frame. */
        D3D12_GPU_VIRTUAL_ADDRESS AllocateFrameConstantDataEXT(
            const void* data, std::size_t byteCount);
        /** @brief Retains a COM object until the current frame slot's fence has completed. */
        void RetainFrameObjectEXT(IUnknown* object);
        /** @brief Submits the currently open frame list without waiting for its new fence. */
        std::uint64_t SubmitFrameCommandsEXT();
        /** @brief Number of actual waits required before a busy frame slot could be reused. */
        [[nodiscard]] std::uint64_t GetFrameFenceWaitCountEXT() const noexcept
        {
            return frameFenceWaitCountEXT_;
        }
        /** @brief Number of all actual fence-event waits, including synchronous upload/readback. */
        [[nodiscard]] std::uint64_t GetGpuWaitCountEXT() const noexcept { return gpuWaitCountEXT_; }
        /** @brief Number of submitted frame command-list segments. */
        [[nodiscard]] std::uint64_t GetFrameSubmissionCountEXT() const noexcept
        {
            return frameSubmissionCountEXT_;
        }
        /** @brief Number of synchronously submitted immediate command lists. */
        [[nodiscard]] std::uint64_t GetImmediateSubmissionCountEXT() const noexcept
        {
            return immediateSubmissionCountEXT_;
        }
        /** @brief Resets the synchronization counters used by focused performance contracts. */
        void ResetSynchronizationCountersEXT() noexcept
        {
            frameFenceWaitCountEXT_ = 0;
            gpuWaitCountEXT_ = 0;
            frameSubmissionCountEXT_ = 0;
            immediateSubmissionCountEXT_ = 0;
        }

        /** @brief Real shared fence object (DX-105). */
        [[nodiscard]] ID3D12Fence* GetFenceEXT() const { return fence_.Get(); }
        /** @brief Submits @p commandList to the direct queue, signals the shared fence with a new
         *  monotonically increasing value, then blocks (via SetEventOnCompletion) until the GPU
         *  actually reaches it -- a simple, correctness-first "wait for GPU idle" helper for tests
         *  and for whichever future task needs synchronous submission before real per-frame
         *  back-pressure (DX-105's own N-frames-in-flight scheme) is wired into Present(). */
        void ExecuteCommandListAndWaitEXT(ID3D12CommandList* commandList);
        /** @brief DX-105's real N-frames-in-flight back-pressure primitive: signals the fence for
         *  frame @p frameIndex with the next monotonically increasing value and records it, then
         *  -- only if that frame index's *previous* recorded fence value hasn't completed yet --
         *  blocks until it has, exactly mirroring the wait-before-reuse pattern a real Present()
         *  loop needs before resetting that frame's command allocator. Returns the fence value
         *  just signaled (for tests to assert against GetCompletedValue()). */
        std::uint64_t SignalAndWaitForFrameEXT(int frameIndex);

        /** @brief Routes a device-removed result into the shared lost-device state when @p hr is
         *  DXGI_ERROR_DEVICE_REMOVED/DXGI_ERROR_DEVICE_RESET, including the real
         *  GetDeviceRemovedReason(). Called from ExecuteCommandListAndWaitEXT()/
         *  SignalAndWaitForFrameEXT() whenever ID3D12CommandQueue::Signal() itself fails. The
         *  deterministic restore half is separately executable through DebugRestoreContext(). */
        void CheckDeviceRemovedEXT(HRESULT hr);

        /** @brief Tears down and recreates the complete D3D12 device domain.
         *
         * Rebuilds the factory, device, queue, descriptor heaps, command allocators/list, fence,
         * presentation resources and every registered long-lived buffer, texture and render
         * target. The shared state tracker is cleared between the old and replacement domains.
         * This environment executes the deterministic simulated path; proving that a genuine
         * DXGI_ERROR_DEVICE_REMOVED reaches it remains DX-90/DX-114's native-hardware gate.
         */
        void RecreateDeviceEXT();

        /** @brief DX-111: binds an off-screen color target for Clear()/DrawColoredPrimitives()/
         *  DrawIndexedColoredPrimitives() to render into. @p resource must already be registered
         *  with GetResourceStateTrackerEXT() by its owner (matches this project's own convention --
         *  every real D3D12 resource registers itself at creation, e.g. D3D12VertexBufferRenderer's
         *  own EnsureCapacity()) -- this call only remembers the binding, it does not create or
         *  track the resource itself. Honest scope note: this is deliberately minimal test/draw
         *  scaffolding, not a full public D3D12RenderTargetRenderer (still owed, matches DX-109's own
         *  honest triage of render targets out of that task's first pass) -- the real swap-chain
         *  back buffer would be the production equivalent once DX-100's Wine/vkd3d-proton
         *  presentation gap is resolved on real Windows hardware (DX-114). CNAEXT. */
        /// @param dsv/@p dsvFormat DX-118: optional real depth-stencil view to bind alongside the
        /// color target -- defaults to an unbound handle/DXGI_FORMAT_UNKNOWN, so every existing
        /// caller (tests, and this class's own pre-DX-118 call sites) is completely unaffected. The
        /// real back buffer's own depth-stencil buffer (DX-116) is passed here now; a bare
        /// off-screen D3D12RenderTargetRenderer (DX-117) does not yet create/pass its own DSV --
        /// a real, honest follow-up gap.
        void BindOffscreenColorTargetEXT(ID3D12Resource* resource, D3D12_CPU_DESCRIPTOR_HANDLE rtv,
                                         DXGI_FORMAT format, int width, int height,
                                         D3D12_CPU_DESCRIPTOR_HANDLE dsv = D3D12_CPU_DESCRIPTOR_HANDLE{},
                                         DXGI_FORMAT dsvFormat = DXGI_FORMAT_UNKNOWN,
                                         ID3D12Resource* depthResource = nullptr);
        /** @brief DX-117: real MRT bind -- @p resources[0]/@p rtvs[0] become the primary bound
         *  target via BindOffscreenColorTargetEXT() (so every existing single-target draw path is
         *  completely unaffected), and @p resources[1..count-1]/@p rtvs[1..count-1] (up to 7 more)
         *  are recorded as additional targets every clear and draw binds in the same order. Every
         *  resource must already be registered with GetResourceStateTrackerEXT() by its owner (same
         *  convention BindOffscreenColorTargetEXT() itself documents). CNAEXT. */
        void BindOffscreenColorTargetsEXT(ID3D12Resource* const* resources,
                                          const D3D12_CPU_DESCRIPTOR_HANDLE* rtvs,
                                          int count, DXGI_FORMAT format, int width, int height,
                                          D3D12_CPU_DESCRIPTOR_HANDLE dsv = D3D12_CPU_DESCRIPTOR_HANDLE{},
                                          DXGI_FORMAT dsvFormat = DXGI_FORMAT_UNKNOWN,
                                          ID3D12Resource* depthResource = nullptr);
        /** @brief Clears the off-screen binding set by BindOffscreenColorTargetEXT() -- subsequent
         *  Clear()/draw calls fall back to the honest "not yet implemented" throw. CNAEXT. */
        void UnbindOffscreenColorTargetEXT();
        /** @brief DX-117: restores the real swap-chain back buffer as the bound color target (the
         *  current back-buffer index, re-resolved every call since it changes on every Present())
         *  if a real swap chain is available; otherwise falls back to
         *  UnbindOffscreenColorTargetEXT()'s honest "nothing bound" state -- matches this renderer's
         *  existing off-screen-test convention exactly. Used by D3D12RenderTargetRenderer's/
         *  D3D12RenderTargetCubeRenderer's own UnbindAsRenderTarget(), mirroring
         *  DirectX11Renderer::RestoreBackBufferRenderTargetEXT(). CNAEXT. */
        void RestoreBackBufferRenderTargetEXT();
        /** @brief Detaches a dying 2D target from every non-owning current-binding slot. */
        void NotifyRenderTargetDestroyedEXT(IRenderTargetRenderer* target) noexcept;
        /** @brief Detaches a dying cube target from the non-owning current cube binding. */
        void NotifyRenderTargetCubeDestroyedEXT(IRenderTargetCubeRenderer* target) noexcept;
        /** @brief Reports whether a 2D target occupies the active single-target or MRT binding. */
        [[nodiscard]] bool IsRenderTargetActiveEXT(
            const IRenderTargetRenderer* target) const noexcept;
        /** @brief Returns a weak token that expires before this renderer can be dereferenced. */
        [[nodiscard]] std::weak_ptr<void> GetLifetimeTokenEXT() const noexcept
        {
            return lifetimeToken_;
        }
        /** @brief Whether an off-screen color target is currently bound (CNAEXT diagnostics/tests). */
        [[nodiscard]] bool HasBoundColorTargetEXT() const { return boundColorResource_ != nullptr; }
        /** @brief The currently bound off-screen color resource, or nullptr (CNAEXT --
         *  D3D12SpriteBatchRenderer needs this for its own resource-state transition). */
        [[nodiscard]] ID3D12Resource* GetBoundColorResourceEXT() const { return boundColorResource_; }
        /** @brief The currently bound off-screen color target's RTV handle (CNAEXT). */
        [[nodiscard]] D3D12_CPU_DESCRIPTOR_HANDLE GetBoundColorRtvEXT() const { return boundColorRtv_; }
        /** @brief The currently bound off-screen color target's DXGI_FORMAT (CNAEXT -- PSO creation
         *  bakes RTV format in, D3D12SpriteBatchRenderer's own PSO needs this). */
        [[nodiscard]] DXGI_FORMAT GetBoundColorFormatEXT() const { return boundColorFormat_; }
        /// plans/plan_dx.md DX-210: the bound depth-stencil view and its format, for the sprite path,
        /// which records its own command lists and builds its own pipeline states. Both are
        /// zero/UNKNOWN when nothing with a depth buffer is bound.
        [[nodiscard]] D3D12_CPU_DESCRIPTOR_HANDLE GetBoundDsvEXT() const { return boundDsv_; }
        [[nodiscard]] DXGI_FORMAT GetBoundDsvFormatEXT() const { return boundDsvFormat_; }
        /** @brief Returns the actual default depth-stencil resource for diagnostics and tests. */
        [[nodiscard]] ID3D12Resource* GetDefaultDepthStencilResourceEXT() const
        {
            return depthStencilResource_.Get();
        }
        /** @brief The currently bound off-screen color target's width/height in pixels (CNAEXT --
         *  D3D12SpriteBatchRenderer uses this as sprite2d's ViewportSize, and for the D3D12_VIEWPORT/
         *  D3D12_RECT it must set up itself, exactly mirroring how DrawPrimitivesExImpl does it). */
        [[nodiscard]] int GetBoundColorWidthEXT() const { return boundColorWidth_; }
        [[nodiscard]] int GetBoundColorHeightEXT() const { return boundColorHeight_; } ///< @copydoc GetBoundColorWidthEXT
        /** @brief The shared root-signature cache (CNAEXT -- D3D12SpriteBatchRenderer reuses the
         *  (1,1,1) shape already established by alpha_test3d, same binding-slot layout sprite2d
         *  needs: 1 CBV @ b0, 1 SRV @ t0, 1 static sampler @ s0). */
        [[nodiscard]] D3D12RootSignatureCache& GetRootSignatureCacheEXT() { return rootSigCache_; }

        /** @brief DX-120 CNAEXT: sets or clears the active slot-zero occlusion-query heap.
         *
         * Draw methods currently bracket each individual draw with BeginQuery/EndQuery on the
         * shared frame list. This preserves the established one-draw contract; DX-240 moves the
         * begin/end commands to the public query boundaries so one query can span several draws.
         */
        void SetActiveOcclusionQueryEXT(ID3D12QueryHeap* heap) { activeOcclusionQueryHeap_ = heap; }

    private:
        std::shared_ptr<void> lifetimeToken_ = std::make_shared<int>(0);
        bool contextRecoveryEnabled_ = true;
        bool deviceLost_ = false;
        std::function<void(RendererDeviceEvent)> deviceEventCallback_;
        std::vector<D3DCommon::ID3DDeviceRecoverableEXT*> recoverableResources_;

        friend class D3D12SpriteBatchRenderer;
        friend class D3D12EffectRenderer;

        [[noreturn]] static void NotYetImplemented(const char* what);

        void CreateDeviceResources();
        void CreateCommandQueueResources();
        void CreateDescriptorHeapResources();
        void CreateCommandListResources();
        void CreateFenceResources();
        /// DX-102: real swap-chain attempt, only when a native HWND is available. Catches HRESULT-level
        /// failure and downgrades to swapChainAvailable_ = false rather than throwing -- see the
        /// class-level doc comment for why. A genuine Wine-level crash (as opposed to a clean
        /// HRESULT failure) cannot be caught here or anywhere in-process; that risk is why the
        /// primary D3D12 CTest suite never constructs this renderer with a real window (see
        /// modules/renderers/directx12/examples/directx12_smoke_test.cpp's own comment block). Stores the real swap-chain pixel
        /// size into width_/height_ (DX-116) -- previously local-only, now needed by
        /// CreateWindowSizeDependentViews()'s own depth-stencil-buffer sizing.
        void CreateSwapChainResources();
        /// DX-116: acquires each of the kFramesInFlight real back-buffer resources (GetBuffer()) +
        /// their RTVs, registers each with the shared D3D12ResourceStateTracker (DX-106) in its
        /// real starting state (D3D12_RESOURCE_STATE_PRESENT), creates a back-buffer-sized
        /// depth-stencil resource+DSV (mirrors D3D11's own DX-24 default), and binds the current
        /// back buffer as the default Clear()/draw target -- mirrors D3D11's own
        /// CreateWindowSizeDependentViews() making the back buffer the default target immediately
        /// after construction. Only called when CreateSwapChainResources() actually succeeded
        /// (swapChainAvailable_ == true).
        void CreateWindowSizeDependentViews();
        /// plans/plan_dx.md DX-241: the implicit off-screen back buffer of a device that has no swap
        /// chain -- either because no window was supplied at all
        /// (PresentationParameters::HeadlessEXT, the mode GraphicsDevice names this renderer as the
        /// intended user of) or because CreateSwapChainResources() downgraded to
        /// swapChainAvailable_ = false. It is a real committed R8G8B8A8_UNORM render target plus the
        /// same D24_UNORM_S8_UINT depth-stencil a windowed device gets, sized from the
        /// PresentationParameters back-buffer size the renderer was constructed with, bound as the
        /// default target exactly as CreateWindowSizeDependentViews() binds the real back buffer.
        /// Without it, Clear() and every draw threw for a headless device until the game bound a
        /// RenderTarget2D of its own, and SetRenderTarget2D(nullptr) put it straight back into that
        /// state -- neither of which is what a "render off-screen and read the result back" mode can
        /// mean. EasyGL always has framebuffer 0 and D3D11 always has a swap chain; this is D3D12's
        /// equivalent of the target that is simply always there.
        void CreateOffscreenBackBufferResources();
        /** @brief Resizes a live swap chain to the latest platform drawable size when needed. */
        void EnsureSwapChainSize();
        /// Rebuilds the default multisampled colour surface and matching depth-stencil resource,
        /// then binds that colour surface (or the single-sample destination when MSAA is off).
        void RecreateDefaultRenderSurfaces(int requestedMultiSampleCount);
        /// Resolves the multisampled default colour surface into the current single-sample
        /// swap-chain/off-screen destination. A no-op when back-buffer MSAA is disabled.
        void ResolveBackBufferMsaaEXT();
        [[nodiscard]] ID3D12Resource* GetBackBufferDrawResourceEXT() const;
        [[nodiscard]] D3D12_CPU_DESCRIPTOR_HANDLE GetBackBufferDrawRtvEXT() const;

        [[nodiscard]] Matrix ApplyXnaPixelCenterEXT(const Matrix& transform) const;
        /// DX-116/DX-218: releases every window-size-dependent resource and returns its RTV/DSV
        /// descriptors to the fence-safe allocators. Used by resize and device recreation.
        void ReleaseWindowSizeDependentViews();

        /** @brief Submits any open frame work, then blocks until all prior work has completed. */
        void WaitForGpuIdle();
        void ReleaseCompletedFrameObjectsEXT();

        /// DX-111 (continued): resolves the real SRV GPU descriptor handle to bind for a
        /// GpuDrawParams texture slot -- mirrors DirectX11Renderer's own GetSrvForTextureEXT,
        /// but D3D12TextureRenderer is the only real ITextureRenderer concrete type this renderer has
        /// (D3D12RenderTargetRenderer, the D3D11 equivalent's second concrete type, is still owed --
        /// DX-109's own honest scope note) -- a single dynamic_cast is sufficient today, not a
        /// gap, just not yet a two-type resolution like D3D11's. Returns a zero-initialized handle
        /// (ptr==0) if @p tex is null or the cast fails.
        D3D12_GPU_DESCRIPTOR_HANDLE GetSrvGpuHandleForTextureEXT(const ITextureRenderer* tex);

        /// DX-111 (closing env_map3d): same convention as GetSrvGpuHandleForTextureEXT, for
        /// env_map3d's 2nd texture slot (a TextureCube, not a Texture2D) -- mirrors D3D11's own
        /// GetSrvForTextureCubeEXT. Returns a zero-initialized handle if @p tex is null or the
        /// dynamic_cast to D3D12TextureCubeRenderer fails.
        D3D12_GPU_DESCRIPTOR_HANDLE GetSrvGpuHandleForTextureCubeEXT(const ITextureCubeRenderer* tex);

        /// D3D12 PBR reconciliation follow-up: lazily creates (once) a 1x1 opaque-white
        /// (255,255,255,255) texture -- the fallback bound for PbrEffect/SkinnedPbrEffect's
        /// metallic-roughness/emissive/occlusion map texture slots when the corresponding
        /// GpuDrawParams pointer is null, so "map absent" reads as the correct neutral value
        /// (factor*1.0=factor; emissive tint*1.0=tint; occlusion 1.0=unoccluded) instead of an
        /// unbound/zero SRV -- mirrors D3D11's own GetOrCreateDefaultWhiteSrvEXT and
        /// EasyGLRenderer.cpp's own EnsureDefaultWhiteTexture(). Built via the existing
        /// CreateTexture(ImageData) path (real D3D12TextureRenderer, real SRV), not a hand-rolled
        /// resource -- same "reuse the established creation path" convention this class's other
        /// GetOrCreate*EXT accessors already follow.
        ITextureRenderer* GetOrCreateDefaultWhiteTextureEXT();
        /// D3D12 PBR reconciliation follow-up: lazily creates (once) a 1x1 "flat" tangent-space
        /// normal (128,128,255,255), decoding to the geometric normal unperturbed (rgb*2-1 ==
        /// (0,0,1)) -- the fallback bound for PbrEffect/SkinnedPbrEffect's NormalMap slot when
        /// GpuDrawParams::pbrNormalMap is null. Mirrors D3D11's own
        /// GetOrCreateDefaultFlatNormalSrvEXT / EnsureDefaultFlatNormalTexture().
        ITextureRenderer* GetOrCreateDefaultFlatNormalTextureEXT();

        /// DX-111 (continued): shared implementation for DrawPrimitivesEx/DrawIndexedPrimitivesEx --
        /// @p ib may be null for the non-indexed path (mirrors DirectX11Renderer's own
        /// DrawPrimitivesExImpl(vb, ib-or-null, ...) shape exactly).
        void DrawPrimitivesExImpl(const IVertexBufferRenderer& vb, const IIndexBufferRenderer* ib,
                                  const Matrix& world, const Matrix& view, const Matrix& projection,
                                  PrimitiveType primitive, int primitiveCount, const GpuDrawParams& params);

        /// plans/plan_dx.md DX-202: copies every tracked XNA render-state ordinal into @p psoDesc. This used
        /// to be the same thirteen assignments written out at each of the three draw sites, which is
        /// how the stencil half stayed missing from all three at once. A single function is also
        /// what keeps a *new* PSO field from being wired into two sites and forgotten in the third --
        /// a mistake whose only symptom would be one draw route silently using another's state.
        /// The variant and vertex layout stay per-site; the complete bound RTV/DSV format set is
        /// copied here because D3D12 bakes it into the PSO.
        void FillPsoStateFromCurrentEXT(D3D12PipelineStateDesc& psoDesc) const;

        /// DX-224: transitions every active colour target and binds the complete ordered RTV set
        /// with target zero's depth-stencil view. Every draw path uses this one operation so PSO
        /// target shape and output-merger binding cannot diverge.
        void TransitionAndBindRenderTargetsEXT(ID3D12GraphicsCommandList* commandList);

        std::unique_ptr<PlatformRendererSurfaceState> surface_;
        HWND hwnd_ = nullptr;
        int virtualWidth_ = 0;
        int virtualHeight_ = 0;

        // Device lifetime (plans/plan_dx.md design decision 11's own grouping, reused for D3D12).
        ComPtr<ID3D12Device> device_;
        ComPtr<IDXGIFactory4> factory_;
        ComPtr<ID3D12CommandQueue> commandQueue_;
        D3D_FEATURE_LEVEL featureLevel_ = D3D_FEATURE_LEVEL_11_0;
        bool debugLayerEnabled_ = false;
        bool allowTearingSupported_ = false;

        // DX-103 gave each of the four heaps a fixed capacity and a monotonic bump cursor with no
        // free list, so a descriptor was consumed for the lifetime of the PROCESS rather than of the
        // resource that asked for it. Every raise this comment used to record (8 -> 32 -> 48 -> 64)
        // was that defect being paid for again by a slightly larger test suite. REMED-GFX-177
        // replaces the four cursors with allocators that reclaim a freed slot and grow on genuine
        // simultaneous demand (see D3D12DescriptorHeaps.hpp); these constants are now the STARTING
        // capacity, deliberately left at DX-103's own numbers so the correction is reclamation and
        // growth rather than a larger arbitrary constant.
        static constexpr UINT kRtvHeapInitialCapacity = 64;
        static constexpr UINT kDsvHeapInitialCapacity = 8;
        static constexpr UINT kCbvSrvUavHeapInitialCapacity = 64;
        // DX-119: one sampler slot per distinct XNA SamplerState combination actually used, not per
        // draw (D3D12SamplerCache only allocates on a genuine cache miss). 16 matches
        // SamplerStateCollection::MaxSamplers; beyond that the allocator grows to the D3D12 ceiling.
        static constexpr UINT kSamplerHeapInitialCapacity = 16;
        /// REMED-GFX-177: shared with every resource that owns a descriptor, so a resource destroyed
        /// after this renderer still frees its slot into a live allocator.
        std::shared_ptr<D3D12DescriptorHeaps> heaps_;

        // DX-119: real sampler cache + per-slot tracked XNA-level SamplerState (updated by
        // ApplySamplerState, matching GraphicsDevice::applySamplerStatesToRenderer()'s own
        // SamplerStateCollection::MaxSamplers=16 slot count). Defaults match this renderer's own
        // pre-DX-119 hardcoded LINEAR/WRAP default exactly, so a draw against a texture slot that
        // never had ApplySamplerState() called on it behaves identically to before this task.
        static constexpr int kMaxSamplerSlots = 16;
        D3D12SamplerCache samplerCache_;
        int currentSamplerFilter_[kMaxSamplerSlots];
        int currentSamplerAddressU_[kMaxSamplerSlots];
        int currentSamplerAddressV_[kMaxSamplerSlots];
        int currentSamplerMaxAnisotropy_[kMaxSamplerSlots];
        // plans/plan_dx.md DX-216: the three SamplerState fields the cache used to drop. Defaults match
        // XNA's own -- MaxMipLevel 0 is the most detailed level and bias 0 is no shift; AddressW
        // starts at Wrap, the same value AddressU/AddressV start at.
        int currentSamplerAddressW_[kMaxSamplerSlots];
        int currentSamplerMaxMipLevel_[kMaxSamplerSlots];
        float currentSamplerLodBias_[kMaxSamplerSlots];

        struct FrameConstantChunk
        {
            ComPtr<ID3D12Resource> resource;
            std::uint8_t* mapped = nullptr;
            std::size_t capacity = 0;
            std::size_t cursor = 0;
        };

        // DX-237: one allocator/list and one lifetime domain per frame slot. commandList_ and
        // immediateCommandAllocator_ are a separate synchronous upload/readback path until DX-238.
        ComPtr<ID3D12CommandAllocator> commandAllocators_[kFramesInFlight];
        ComPtr<ID3D12GraphicsCommandList> frameCommandLists_[kFramesInFlight];
        ComPtr<ID3D12CommandAllocator> immediateCommandAllocator_;
        ComPtr<ID3D12GraphicsCommandList> commandList_;
        std::vector<FrameConstantChunk> frameConstantChunks_[kFramesInFlight];
        std::vector<ComPtr<IUnknown>> frameRetainedObjects_[kFramesInFlight];
        int activeFrameIndex_ = -1;
        int headlessFrameIndex_ = 0;
        std::uint64_t activeFrameFenceValue_ = 0;

        // DX-105: single shared fence + monotonically increasing counter + the fence value last
        // recorded for each frame index (the actual N-frames-in-flight back-pressure state).
        ComPtr<ID3D12Fence> fence_;
        HANDLE fenceEvent_ = nullptr;
        std::uint64_t nextFenceValue_ = 1;
        std::uint64_t frameFenceValues_[kFramesInFlight] = {};
        std::uint64_t frameFenceWaitCountEXT_ = 0;
        std::uint64_t gpuWaitCountEXT_ = 0;
        std::uint64_t frameSubmissionCountEXT_ = 0;
        std::uint64_t immediateSubmissionCountEXT_ = 0;

        // Swap-chain lifetime (DX-102) -- see class-level doc comment for why this is allowed to
        // fail gracefully instead of throwing.
        ComPtr<IDXGISwapChain3> swapChain_;
        bool swapChainAvailable_ = false;
        int width_ = 0;
        int height_ = 0;

        // DX-116: Present() policy state -- mirrors DirectX11Renderer's own vsyncEnabled_/
        // allowTearingRequested_/exclusiveFullscreen_ exactly (design decision 13's own
        // capability-vs-policy split, reused unchanged for D3D12).
        bool vsyncEnabled_ = true;
        bool allowTearingRequested_ = true;
        bool exclusiveFullscreen_ = false;
        int swapInterval_ = 1;
        CnaPresentationMode presentationMode_ = CnaPresentationMode::FixedHeightDynamicWidth;
        // GraphicsDeviceManager reapplies its default mode before every Reset. A windowless
        // renderer keeps legacy local-pixel behavior until a caller explicitly selects a mode
        // after setting the virtual resolution.
        bool windowlessPresentationExplicit_ = false;

        // DX-116: window-size-lifetime resources -- real back-buffer RTVs (one per
        // kFramesInFlight) + a shared depth-stencil buffer, mirroring D3D11's own DX-24 group.
        ComPtr<ID3D12Resource> backBufferResources_[kFramesInFlight];
        D3D12_CPU_DESCRIPTOR_HANDLE backBufferRtvs_[kFramesInFlight]{};
        // DX-241: the implicit off-screen back buffer, present exactly when swapChainAvailable_ is
        // false. It is the resource ReadBackbuffer() reads (DX-205) and the one
        // RestoreBackBufferRenderTargetEXT() rebinds, so a windowless device behaves like a device
        // with a back buffer in every respect except putting pixels on a screen.
        ComPtr<ID3D12Resource> offscreenBackBufferResource_;
        D3D12_CPU_DESCRIPTOR_HANDLE offscreenBackBufferRtv_{};
        ComPtr<ID3D12Resource> backBufferMsaaResource_;
        D3D12_CPU_DESCRIPTOR_HANDLE backBufferMsaaRtv_{};
        ComPtr<ID3D12Resource> depthStencilResource_;
        D3D12_CPU_DESCRIPTOR_HANDLE depthStencilViewEXT_{};
        int requestedMultiSampleCount_ = 0;
        int appliedMultiSampleCount_ = 0;

        // DX-144: tracks the currently-bound custom (non-back-buffer) render target, mirroring
        // DirectX11Renderer's own currentCustomRT_ exactly -- SetRenderTarget2D(nullptr) needs
        // to call the PREVIOUSLY bound target's own UnbindAsRenderTarget() (which is where
        // GenerateMipsEXT() lives) before restoring the back buffer, not just blindly restore.
        IRenderTargetRenderer* currentCustomRT_ = nullptr;

        /// REMED-GFX-134: the same "finalize whatever was previously bound" need as
        /// `currentCustomRT_`, for a cube face. Nothing tracked a bound RenderTargetCube before, so
        /// `D3D12RenderTargetCubeRenderer::UnbindAsRenderTarget()` -- which is where this renderer's
        /// per-face `ResolveSubresource()` and `GenerateMipsEXT()` live -- was never reached from
        /// the SetRenderTarget/SetRenderTargets path: a multisampled cube target's resolve resource
        /// stayed empty and a mipMap=true cube target's levels above 0 were never regenerated.
        /// Non-owning, same lifetime reasoning as `currentCustomRT_`.
        IRenderTargetCubeRenderer* currentCubeRT_ = nullptr;
        /// REMED-GFX-134: finalizes and forgets the currently tracked cube target, if any.
        void FlushPendingCubeResolveEXT();
        /// DX-255: finalizes a previously bound MRT set (or single target bound through
        /// SetRenderTargets) by calling UnbindAsRenderTarget() on each of its targets, then clears
        /// the tracking. Idempotent; a no-op when nothing was bound that way.
        void FlushPendingMrtResolveEXT();

        // DX-106/DX-109: single shared per-resource barrier-state tracker, registered with by every
        // real D3D12 resource this renderer creates (vertex/index buffers, textures -- DX-109).
        D3D12ResourceStateTracker resourceStates_;

        // Root-signature/PSO caches are shared across draws. DX-237 moved all draw constants into
        // frame-owned ranges above; one mutable mapped resource per constant kind is not in-flight safe.
        D3D12RootSignatureCache rootSigCache_;
        D3D12PipelineStateCache psoCache_;
        // D3D12 PBR reconciliation follow-up: lazily-created 1x1 fallback textures for PbrEffect/
        // SkinnedPbrEffect's optional map slots (see GetOrCreateDefaultWhiteTextureEXT()/
        // GetOrCreateDefaultFlatNormalTextureEXT()'s own doc comments) -- owned here, reused
        // across every draw that needs a fallback.
        std::unique_ptr<ITextureRenderer> defaultWhiteTexture_;
        std::unique_ptr<ITextureRenderer> defaultFlatNormalTexture_;
        // DX-111: the currently-bound off-screen color target (see BindOffscreenColorTargetEXT's own
        // doc comment) -- non-owning, the caller/test retains ownership of the resource itself.
        ID3D12Resource* boundColorResource_ = nullptr;
        D3D12_CPU_DESCRIPTOR_HANDLE boundColorRtv_{};
        DXGI_FORMAT boundColorFormat_ = DXGI_FORMAT_R8G8B8A8_UNORM;
        int boundColorWidth_ = 0;
        int boundColorHeight_ = 0;
        // REMED-GFX-064: current GraphicsDevice.Viewport, set by SetViewport() and consumed at
        // every RSSetViewports site via GetEffectiveViewportEXT(). viewportSet_ stays false until
        // a viewport is pushed, so pre-viewport draws fall back to the full bound-target rect,
        // byte-identical to the pre-fix hardcode. SetRenderTarget resets Viewport to the new
        // target's full size at the GraphicsDevice layer (ResetViewportAndScissorForRenderTarget
        // -> SetViewport(0,0,w,h)), so no per-target reset is needed inside the renderer.
        bool viewportSet_ = false;
        int viewportX_ = 0;
        int viewportY_ = 0;
        int viewportW_ = 0;
        int viewportH_ = 0;
        float viewportMinDepth_ = 0.0f;
        float viewportMaxDepth_ = 1.0f;
        // DX-118: optional real DSV bound alongside the color target above -- ptr==0 means unbound,
        // matching every draw path's pre-DX-118 "null DSV" behavior exactly when nothing sets one.
        D3D12_CPU_DESCRIPTOR_HANDLE boundDsv_{};
        DXGI_FORMAT boundDsvFormat_ = DXGI_FORMAT_UNKNOWN;
        ID3D12Resource* boundDepthResource_ = nullptr;

        // DX-118: currently-applied XNA-level state (updated by ApplyBlendState/
        // ApplyDepthStencilState/ApplyRasterizerState), fed into every psoCache_.GetOrCreate() call
        // site instead of the hardcoded literals those call sites used before this task. Defaults
        // match EXACTLY what those hardcoded literals were (depthEnable=false, cullMode=None,
        // Opaque blend) -- so a draw that never had one of these 3 methods called on it first
        // (every pixel test that existed before this task) gets byte-identical behavior to before;
        // only a test/game that explicitly calls one of them gets genuinely different PSO state.
        int currentColorSrcBlend_ = 0;   // Blend::One (real XNA ordinal -- Blend.hpp: One=0)
        int currentAlphaSrcBlend_ = 0;   // Blend::One
        int currentColorDstBlend_ = 1;   // Blend::Zero
        int currentAlphaDstBlend_ = 1;   // Blend::Zero
        int currentColorBlendFunc_ = 0;  // BlendFunction::Add
        int currentAlphaBlendFunc_ = 0;  // BlendFunction::Add
        // REMED-GFX-077/DX-224: all four XNA MRT output masks, folded into the PSO key/desc.
        std::array<int, 4> currentColorWriteMasks_{15, 15, 15, 15};
        unsigned int currentSampleMask_ = 0xFFFFFFFFu; // MultiSampleMask == -1 (all samples)
        bool currentDepthEnable_ = false;
        bool currentDepthWriteEnable_ = false;
        int currentDepthFunc_ = 3;       // CompareFunction::LessEqual (real XNA ordinal)
        // DX-202/DX-203: the stencil half of DepthStencilState, tracked exactly like the depth half
        // above and defaulting to XNA's own DepthStencilState.Default (stencil off,
        // CompareFunction::Always, StencilOperation::Keep, full masks, single-sided, reference 0).
        // currentReferenceStencil_ is deliberately NOT a PSO field: D3D12 takes it through
        // OMSetStencilRef() at record time, which is why GraphicsDevice.ReferenceStencil can change
        // between draws without rebuilding a pipeline state.
        bool currentStencilEnable_ = false;
        int currentStencilFunc_ = 0;
        int currentStencilPass_ = 0;
        int currentStencilFail_ = 0;
        int currentStencilDepthFail_ = 0;
        int currentStencilMask_ = 0xFF;
        int currentStencilWriteMask_ = 0xFF;
        bool currentTwoSidedStencilMode_ = false;
        int currentCcwStencilFunc_ = 0;
        int currentCcwStencilPass_ = 0;
        int currentCcwStencilFail_ = 0;
        int currentCcwStencilDepthFail_ = 0;
        int currentReferenceStencil_ = 0;
        // DX-204: GraphicsDevice.BlendFactor. XNA's default is Color.White (1,1,1,1), which is also
        // D3D12's own default blend factor, so a game that never sets it is unaffected.
        float currentBlendFactor_[4] = {1.0f, 1.0f, 1.0f, 1.0f};
        int currentCullMode_ = 0;        // CullMode::None
        int currentFillMode_ = 0;        // FillMode::Solid
        // DX-201: RasterizerState.ScissorTestEnable plus GraphicsDevice.ScissorRectangle. Not PSO
        // state on D3D12 (see GetEffectiveScissorEXT), so no cache key entry and no PSO rebuild when
        // a game moves the rectangle between draws.
        bool currentScissorTestEnable_ = false;
        bool scissorSet_ = false;
        int scissorX_ = 0;
        int scissorY_ = 0;
        int scissorW_ = 0;
        int scissorH_ = 0;
        // DX-206: RasterizerState.DepthBias / SlopeScaleDepthBias. These ARE pipeline state
        // (D3D12_RASTERIZER_DESC::DepthBias / SlopeScaledDepthBias), so they join the PSO key.
        float currentDepthBias_ = 0.0f;
        float currentSlopeScaleDepthBias_ = 0.0f;

        // DX-117/DX-224: additional MRT targets beyond the primary (index 0, tracked by
        // boundColor*_ above). Clear and every draw transition and bind the complete ordered set.
        static constexpr int kMaxExtraMrtTargets = 7; // 8 total (D3D12_SIMULTANEOUS_RENDER_TARGET_COUNT) - 1 primary
        ID3D12Resource* extraMrtResources_[kMaxExtraMrtTargets] = {};
        D3D12_CPU_DESCRIPTOR_HANDLE extraMrtRtvs_[kMaxExtraMrtTargets]{};
        DXGI_FORMAT extraMrtFormats_[kMaxExtraMrtTargets]{};
        int extraMrtCount_ = 0;

        /// plans/plan_dx.md DX-255: the render targets a SetRenderTargets() call bound, so each one's own
        /// UnbindAsRenderTarget() -- where the per-target MSAA resolve and mip regeneration live --
        /// runs when the set is replaced. `currentCustomRT_` is a single pointer and cannot
        /// represent N targets; this is the same split DirectX11Renderer's own
        /// currentMRTTargets_/FlushPendingMRTResolveEXT() already uses (DX-143), which D3D12 never
        /// got. The first entry is also tracked here rather than in currentCustomRT_, so exactly one
        /// of the two mechanisms owns a given bind.
        static constexpr int kMaxMrtTargets = kMaxExtraMrtTargets + 1;
        IRenderTargetRenderer* currentMrtTargets_[kMaxMrtTargets] = {};
        int currentMrtCount_ = 0;

        // DX-120: the currently-active occlusion query heap (non-owning, nullptr when no query is
        // active), always slot 0. Real, non-obvious constraint discovered while landing DX-120:
        // BeginQuery()/EndQuery() must be recorded in the same command-list submission as the draws
        // they bracket. The current implementation wraps each draw individually on the shared frame
        // list, preserving the original one-draw behavior. DX-240 moves those commands to the query
        // object's Begin()/End() boundaries to support multi-draw accumulation.
        ID3D12QueryHeap* activeOcclusionQueryHeap_ = nullptr;
    };
}
