#pragma once

// plans/plan_dx.md Phase DX12: D3D12 renderer. DX-101 landed CMake wiring + an honest all-stub skeleton.
// DX-102/DX-103/DX-104/DX-105 (this revision) make the device-lifetime group real: ID3D12Device +
// command queue + descriptor heaps (RTV/DSV/CBV_SRV_UAV) + per-frame command allocators/command
// list + fence-based frame synchronization. Clear()/Present()/draw calls are STILL honest
// "not yet implemented" stubs -- those need DX-106 (resource barriers), DX-107 (PSOs), DX-108
// (root signatures) and DX-109 (resources) first, none of which exist yet.
//
// Windows-only (see CMakeLists.txt's FATAL_ERROR guard for non-Windows CNA_GRAPHICS_RENDERER=D3D12).

#include "CNA/Internal/Renderers/Common/IGraphicsRenderer.hpp"
#include "CNA/Internal/Renderers/Common/PlatformRendererSurfaceState.hpp"
#include "D3D12DescriptorHeaps.hpp"
#include "D3D12ResourceStateTracker.hpp"
#include "D3D12PipelineStateCache.hpp"
#include "D3D12RootSignatureCache.hpp"
#include "D3D12SamplerCache.hpp"
#include "D3D12TextureCube.hpp"

#include <d3d12.h>
#include <dxgi1_5.h>
#include <wrl/client.h>

#include <cstdint>
#include <functional>
#include <memory>
#include <unordered_map>

namespace CNA::Internal::Renderers::DirectX12
{
    using Microsoft::WRL::ComPtr;

    /**
     * D3D12 graphics renderer (plans/plan_dx.md Phase DX12). DX-100's own spike (real Wine+vkd3d-proton
     * run) found `D3D12CreateDevice`/`CreateCommandQueue`/descriptor-heap/fence/command-allocator/
     * command-list calls all work genuinely well locally (feature level 12_1, DXR 1.1, SM 6.8 on
     * the real GPU), but `CreateSwapChainForHwnd` with `DXGI_SWAP_EFFECT_FLIP_DISCARD` crashes
     * inside vanilla Wine's own `dxgi.dll` -- confirmed again by this revision's own real attempt
     * (see `DX-102`'s plan row). Consequently:
     *   - Device-lifetime resources (device_/factory_/commandQueue_/descriptor heaps/command
     *     allocators+list/fence) are created unconditionally and are the real, Wine-provable part
     *     of this renderer today.
     *   - Swap-chain creation is attempted for real (`CreateSwapChainResources()`), matching
     *     D3D11's own `FLIP_DISCARD` production convention, but ONLY when a window is supplied,
     *     and a failure there is caught (HRESULT-level) and downgraded to `swapChainAvailable_ =
     *     false` rather than throwing -- so constructing this renderer off-screen (no window) never
     *     touches the crash-prone path at all, and the primary D3D12 CTest suite stays
     *     off-screen/swap-chain-free per DX-100's own recommendation.
     *   - `Clear()`/`Present()`/draw calls remain honest "not yet implemented" stubs -- DX-106
     *     (resource barriers), DX-107 (PSOs), DX-108 (root signatures), DX-109 (resources) are all
     *     still unstarted follow-up work.
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

        // ---- IGraphicsRenderer: honest "not yet implemented" stubs (DX-106 onward) ----
        void Clear(float r, float g, float b, float a) override;
        /// DX-116: real Present() -- transitions the currently-bound back buffer to PRESENT,
        /// calls IDXGISwapChain3::Present() for real, then re-binds the (new) current back buffer
        /// as the default draw target for the next frame. Throws (NotYetImplemented) if no real
        /// swap chain is available (e.g. off-screen construction, or this dev loop's own
        /// documented Wine/vkd3d-proton swap-chain limitation -- see DX-100/DX-102).
        void Present() override;
        void GetViewportSize(int& width, int& height) override;
        void SetVirtualResolution(int width, int height) override;
        void SetPresentationMode(int mode) override;
        void OnSurfaceChanged(const RendererSurfaceInfo& surface) override;
        /// DX-116: mirrors DirectX11Renderer::SetSwapInterval exactly -- sync interval is
        /// renderer state applied at the next Present(), not a direct D3D12 API call ahead of time.
        void SetSwapInterval(int interval) override;


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
        void SetRenderTarget2D(IRenderTargetRenderer* rt) override;
        /// DX-117: real D3D12RenderTargetCubeRenderer, no longer the inherited default (-> nullptr).
        std::unique_ptr<IRenderTargetCubeRenderer> CreateRenderTargetCube(int size, int depthFormat,
                                                                          bool preserveContents = false,
                                                                          bool mipMap = false,
                                                                          int multiSampleCount = 0) override;
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
        /// REMED-GFX-064: real, runtime-settable GraphicsDevice.Viewport. Stores the sub-region
        /// viewport rect + depth range; every draw path re-records its own command list fresh
        /// (immediate-per-draw model), so each RSSetViewports site simply reads the stored value
        /// back through GetEffectiveViewportEXT() -- no capture/replay is needed (unlike the
        /// deferred GPU renderers). Before this task D3D12 never overrode the no-op base
        /// SetViewport and hardcoded a full-target D3D12_VIEWPORT at all four RSSetViewports sites
        /// (+ the sprite path), so a custom Viewport was a total no-op on backbuffer and RT alike.
        void SetViewport(int x, int y, int w, int h, float minDepth, float maxDepth) override;
        /// REMED-GFX-064: the D3D12_VIEWPORT every draw must set -- the custom Viewport stored by
        /// SetViewport() if one was set (unclamped rect, top-left origin, depth clamped to [0,1]),
        /// otherwise the full bound-target rect ({0,0,boundColorWidth_,boundColorHeight_,0,1},
        /// byte-identical to the pre-fix hardcode). Shared by the 4 renderer draw paths and the
        /// D3D12 SpriteBatch flush (which reads it via owner_).
        [[nodiscard]] D3D12_VIEWPORT GetEffectiveViewportEXT() const;

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

        /// DX-111 (finish): real instanced3d dispatch -- mirrors
        /// DirectX11Renderer::DrawInstancedPrimitivesEx's own fallback (no per-instance VB means
        /// this isn't really an instanced draw) and hand-built dual-vertex-stream PSO/input-layout
        /// shape (POSITION @ slot 0 per-vertex, INSTANCEWORLD0-3 @ slot 1 per-instance) -- see
        /// GetOrCreateInstancedPsoEXT's own doc comment for why this bypasses D3D12PipelineStateCache.
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

        /** @brief Real per-frame command allocator for @p frameIndex (0..kFramesInFlight-1). */
        [[nodiscard]] ID3D12CommandAllocator* GetCommandAllocatorEXT(int frameIndex) const
        {
            return commandAllocators_[frameIndex].Get();
        }
        /** @brief The single, reused direct command list (DX-104) -- callers must Reset() it
         *  against the correct frame's allocator (GetCommandAllocatorEXT()) before recording, and
         *  Close() it before ExecuteCommandLists(). Created already-Close()'d. */
        [[nodiscard]] ID3D12GraphicsCommandList* GetCommandListEXT() const { return commandList_.Get(); }

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

        /** @brief DX-110: logs (does not throw or recover) when @p hr is
         *  DXGI_ERROR_DEVICE_REMOVED/DXGI_ERROR_DEVICE_RESET, including the real
         *  GetDeviceRemovedReason() -- mirrors DirectX11Renderer::CheckDeviceRemoved's own
         *  detection-only convention exactly (design decision 12's "never assume, always check"
         *  discipline applied to device loss too). Called from ExecuteCommandListAndWaitEXT()/
         *  SignalAndWaitForFrameEXT() whenever ID3D12CommandQueue::Signal() itself fails. */
        void CheckDeviceRemovedEXT(HRESULT hr) const;

        /** @brief DX-110: the real recovery path -- unlike D3D11's own DX-27 (detection+logging
         *  only, full recovery deferred to DX-90's real hardware), D3D12 documentation treats
         *  device-removed recovery as expected to handle from the start, so this genuinely tears
         *  down and recreates every device-lifetime resource DX-102 through DX-105 built (device,
         *  factory, command queue, all 3 descriptor heaps with their bump allocators reset to 0,
         *  every per-frame command allocator + the shared command list, the fence + its counters),
         *  clears the shared D3D12ResourceStateTracker (every previously-tracked resource's D3D12
         *  object is gone along with the removed device), and re-attempts the swap chain if a
         *  window was supplied. Honest scope boundary: this recreation logic is real and directly
         *  callable/testable (see modules/renderers/directx12/examples/directx12_smoke_test.cpp), but this dev loop cannot trigger
         *  a genuine DXGI_ERROR_DEVICE_REMOVED to prove the *trigger* path -- only DX-90/DX-114's
         *  real hardware can. CNAEXT -- not part of any IGraphicsRenderer contract. */
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
                                         DXGI_FORMAT dsvFormat = DXGI_FORMAT_UNKNOWN);
        /** @brief DX-117: real MRT bind -- @p resources[0]/@p rtvs[0] become the primary bound
         *  target via BindOffscreenColorTargetEXT() (so every existing single-target draw path is
         *  completely unaffected), and @p resources[1..count-1]/@p rtvs[1..count-1] (up to 7 more)
         *  are recorded as additional targets Clear() also independently clears. Every resource
         *  must already be registered with GetResourceStateTrackerEXT() by its owner (same
         *  convention BindOffscreenColorTargetEXT() itself documents). CNAEXT. */
        void BindOffscreenColorTargetsEXT(ID3D12Resource* const* resources,
                                          const D3D12_CPU_DESCRIPTOR_HANDLE* rtvs,
                                          int count, DXGI_FORMAT format, int width, int height);
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
        /** @brief The currently bound off-screen color target's width/height in pixels (CNAEXT --
         *  D3D12SpriteBatchRenderer uses this as sprite2d's ViewportSize, and for the D3D12_VIEWPORT/
         *  D3D12_RECT it must set up itself, exactly mirroring how DrawPrimitivesExImpl does it). */
        [[nodiscard]] int GetBoundColorWidthEXT() const { return boundColorWidth_; }
        [[nodiscard]] int GetBoundColorHeightEXT() const { return boundColorHeight_; } ///< @copydoc GetBoundColorWidthEXT
        /** @brief The shared root-signature cache (CNAEXT -- D3D12SpriteBatchRenderer reuses the
         *  (1,1,1) shape already established by alpha_test3d, same binding-slot layout sprite2d
         *  needs: 1 CBV @ b0, 1 SRV @ t0, 1 static sampler @ s0). */
        [[nodiscard]] D3D12RootSignatureCache& GetRootSignatureCacheEXT() { return rootSigCache_; }

        /** @brief DX-120 CNAEXT: sets/clears the currently-active occlusion query heap (slot 0)
         *  that every draw-recording method (DrawColoredPrimitives/DrawIndexedColoredPrimitives/
         *  DrawPrimitivesExImpl/DrawInstancedPrimitivesEx) brackets its own single command-list
         *  recording with (BeginQuery right after Reset(), EndQuery right before Close()) -- a
         *  real Vulkan/vkd3d-proton constraint that BeginQuery/EndQuery must share one command-list
         *  submission with the draw(s) they bracket, which this renderer's own per-draw-call
         *  self-submission architecture doesn't naturally satisfy otherwise. Pass nullptr to clear. */
        void SetActiveOcclusionQueryEXT(ID3D12QueryHeap* heap) { activeOcclusionQueryHeap_ = heap; }

    private:
        friend class D3D12SpriteBatchRenderer;

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
        /// depth-stencil resource+DSV (mirrors D3D11's own DX-24 default, though not yet wired
        /// into any OMSetRenderTargets call -- every draw path still hardcodes a null DSV,
        /// DX-107/DX-111's own documented depthEnable=false simplification; created here for
        /// parity/completeness, real depth-test support is DX-118's job), and binds the current
        /// back buffer as the default Clear()/draw target -- mirrors D3D11's own
        /// CreateWindowSizeDependentViews() making the back buffer the default target immediately
        /// after construction. Only called when CreateSwapChainResources() actually succeeded
        /// (swapChainAvailable_ == true).
        void CreateWindowSizeDependentViews();
        /// DX-116: releases every window-size-dependent resource CreateWindowSizeDependentViews()
        /// created -- back-buffer resources/RTV handles and the depth-stencil resource/DSV handle
        /// (RTV/DSV heap slot *indices* are not reclaimed, matching DX-103's own documented
        /// no-free-list-yet bump-allocator simplification). Used by RecreateDeviceEXT() (DX-110)
        /// before tearing down the whole device; window resize (D3D11's own DX-29 equivalent) is
        /// real, scoped follow-up work this task does not attempt.
        void ReleaseWindowSizeDependentViews();

        /// DX-111: lazily creates (once) an UPLOAD-heap, persistently-mapped constant buffer of
        /// exactly @p byteWidth bytes -- the standard D3D12 dynamic-CB idiom (map once at creation,
        /// never Unmap, just memcpy new contents in before each draw; safe here because every draw
        /// in this renderer still submits synchronously via ExecuteCommandListAndWaitEXT(), so there
        /// is no in-flight GPU access for a new memcpy to race with -- same honest scope note
        /// D3D12Buffers.hpp's own file comment already makes for SetDataOptions).
        void CreateUploadConstantBuffer(UINT byteWidth, ComPtr<ID3D12Resource>& outResource, void*& outMapped);
        ID3D12Resource* GetOrCreatePerDrawConstantBufferEXT();
        ID3D12Resource* GetOrCreateFogConstantBufferEXT();
        /// DX-111 (continued): LitLightParams (b1) for lit_textured3d -- see D3DLightingConstants.
        ID3D12Resource* GetOrCreateLightingConstantBufferEXT();
        /// DX-111 (continued): alpha_test3d's own single combined PerDraw (b0) -- see
        /// D3DAlphaTestConstants's own doc comment for why this isn't D3DPerDrawConstants.
        ID3D12Resource* GetOrCreateAlphaTestConstantBufferEXT();
        /// DX-111 (finish): skinned3d's BoneBlock (b1, D3DBoneConstants, DX-60a) -- the 72-matrix array.
        ID3D12Resource* GetOrCreateBoneConstantBufferEXT();
        /// DX-111 (finish): skinned3d's own FogParams-equivalent (b2, D3DSkinnedExtraConstants) --
        /// fog + DirectionalLight1/2 + World + EyePosition + specular (mirrors D3D11's own
        /// GetOrCreateSkinnedExtraConstantBufferEXT, same reasoning D3DSkinnedExtraConstants's own
        /// doc comment already gives: PerDraw's 128 bytes have no spare room for these fields).
        ID3D12Resource* GetOrCreateSkinnedExtraConstantBufferEXT();
        /// DX-111 (closing env_map3d): env_map3d's own PerDraw (b0, D3DEnvMapPerDrawConstants --
        /// Mvp+World only, a genuinely different shape from D3DPerDrawConstants).
        ID3D12Resource* GetOrCreateEnvMapPerDrawConstantBufferEXT();
        /// DX-111 (closing env_map3d): EnvMapParams (b2, D3DEnvMapConstants) -- material/lighting/
        /// fog/Fresnel fields, mirrors D3D11's own GetOrCreateEnvMapConstantBufferEXT.
        ID3D12Resource* GetOrCreateEnvMapConstantBufferEXT();
        /// D3D12 PBR/skinned-vertex-color reconciliation follow-up: pbr3d.vert.hlsl/.frag.hlsl's
        /// and pbr_skinned3d.vert.hlsl/.frag.hlsl's shared PerDraw (b0, D3DPbrPerDrawConstants) --
        /// same "map once, reused every draw" convention as every other GetOrCreate*ConstantBufferEXT
        /// above, mirrors D3D11's own GetOrCreatePbrPerDrawConstantBufferEXT.
        ID3D12Resource* GetOrCreatePbrPerDrawConstantBufferEXT();
        /// D3D12 PBR reconciliation follow-up: pbr3d/pbr_skinned3d's shared PbrLights cbuffer
        /// (register(b1) for the unskinned Pbr3d variant, (b2) for PbrSkinned3d since BoneBlock
        /// claims (b1) there instead, D3DPbrLightConstants) -- one shared buffer object; which root
        /// CBV slot it's bound to is decided per-draw by DrawPrimitivesExImpl, mirrors D3D11's own
        /// GetOrCreatePbrLightsConstantBufferEXT.
        ID3D12Resource* GetOrCreatePbrLightsConstantBufferEXT();

        /// DX-111 (finish): instanced3d's own hand-built PSO -- deliberately NOT resolved via
        /// D3D12PipelineStateCache/D3DVertexFormatHelper::InputElementsForStrideD3D12 (which only
        /// covers a single per-vertex stream), since instanced3d needs a genuinely different
        /// 2-input-slot layout: POSITION0 (12 bytes, per-vertex, slot 0) + INSTANCEWORLD0-3 (4 x
        /// float4 rows, per-instance, slot 1) -- mirrors DirectX11Renderer's own
        /// GetOrCreateInstancedInputLayoutEXT() element list exactly, and D3D12SpriteBatchRenderer's
        /// own precedent for hand-building a PSO when the stride-keyed cache's assumptions don't fit
        /// (this file's own instancedPsos_ use the (1,0,0) root-signature shape -- PerDraw@b0 only,
        /// no texture -- matching instanced3d.frag.hlsl's own real (textureless) declaration).
        ID3D12PipelineState* GetOrCreateInstancedPsoEXT(ID3D12RootSignature* rootSig,
                                                       UINT instanceStepRate);

        /// DX-111 (continued): resolves the real SRV GPU descriptor handle to bind for a
        /// GpuDrawParams texture slot -- mirrors DirectX11Renderer's own GetSrvForTextureEXT,
        /// but D3D12TextureRenderer is the only real ITextureRenderer concrete type this renderer has
        /// (D3D12RenderTargetRenderer, the D3D11 equivalent's second concrete type, is still owed --
        /// DX-109's own honest scope note) -- a single dynamic_cast is sufficient today, not a
        /// gap, just not yet a two-type resolution like D3D11's. Returns a zero-initialized handle
        /// (ptr==0) if @p tex is null or the cast fails.
        D3D12_GPU_DESCRIPTOR_HANDLE GetSrvGpuHandleForTextureEXT(const ITextureRenderer* tex) const;

        /// DX-111 (closing env_map3d): same convention as GetSrvGpuHandleForTextureEXT, for
        /// env_map3d's 2nd texture slot (a TextureCube, not a Texture2D) -- mirrors D3D11's own
        /// GetSrvForTextureCubeEXT. Returns a zero-initialized handle if @p tex is null or the
        /// dynamic_cast to D3D12TextureCubeRenderer fails.
        D3D12_GPU_DESCRIPTOR_HANDLE GetSrvGpuHandleForTextureCubeEXT(const ITextureCubeRenderer* tex) const;

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

        // DX-104: per-frame command allocators + one reused direct command list.
        ComPtr<ID3D12CommandAllocator> commandAllocators_[kFramesInFlight];
        ComPtr<ID3D12GraphicsCommandList> commandList_;

        // DX-105: single shared fence + monotonically increasing counter + the fence value last
        // recorded for each frame index (the actual N-frames-in-flight back-pressure state).
        ComPtr<ID3D12Fence> fence_;
        HANDLE fenceEvent_ = nullptr;
        std::uint64_t nextFenceValue_ = 1;
        std::uint64_t frameFenceValues_[kFramesInFlight] = {};

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

        // DX-116: window-size-lifetime resources -- real back-buffer RTVs (one per
        // kFramesInFlight) + a shared depth-stencil buffer, mirroring D3D11's own DX-24 group.
        ComPtr<ID3D12Resource> backBufferResources_[kFramesInFlight];
        D3D12_CPU_DESCRIPTOR_HANDLE backBufferRtvs_[kFramesInFlight]{};
        ComPtr<ID3D12Resource> depthStencilResource_;
        D3D12_CPU_DESCRIPTOR_HANDLE depthStencilViewEXT_{};

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

        // DX-106/DX-109: single shared per-resource barrier-state tracker, registered with by every
        // real D3D12 resource this renderer creates (vertex/index buffers, textures -- DX-109).
        D3D12ResourceStateTracker resourceStates_;

        // DX-111: root-signature/PSO caches (shared across every draw call) and the colored3d
        // constant buffers -- same "persistent, reused across draws" convention D3D11's own
        // perDrawConstantBuffer_/fogConstantBuffer_ established.
        D3D12RootSignatureCache rootSigCache_;
        D3D12PipelineStateCache psoCache_;
        ComPtr<ID3D12Resource> perDrawConstantBuffer_;
        void* perDrawConstantBufferMapped_ = nullptr;
        ComPtr<ID3D12Resource> fogConstantBuffer_;
        void* fogConstantBufferMapped_ = nullptr;
        // DX-111 (continued): textured3d/colored_textured3d/lit_textured3d/alpha_test3d's own
        // persistent constant buffers, same "map once, reused every draw" convention as above.
        ComPtr<ID3D12Resource> lightingConstantBuffer_;
        void* lightingConstantBufferMapped_ = nullptr;
        ComPtr<ID3D12Resource> alphaTestConstantBuffer_;
        void* alphaTestConstantBufferMapped_ = nullptr;
        // DX-111 (finish): skinned3d's own persistent constant buffers -- same convention as above.
        ComPtr<ID3D12Resource> boneConstantBuffer_;
        void* boneConstantBufferMapped_ = nullptr;
        ComPtr<ID3D12Resource> skinnedExtraConstantBuffer_;
        void* skinnedExtraConstantBufferMapped_ = nullptr;
        // DX-111 (closing env_map3d): env_map3d's own persistent constant buffers -- same
        // "map once, reused every draw" convention as above.
        ComPtr<ID3D12Resource> envMapPerDrawConstantBuffer_;
        void* envMapPerDrawConstantBufferMapped_ = nullptr;
        ComPtr<ID3D12Resource> envMapConstantBuffer_;
        void* envMapConstantBufferMapped_ = nullptr;
        // D3D12 PBR reconciliation follow-up: pbr3d/pbr_skinned3d's own persistent constant
        // buffers -- same "map once, reused every draw" convention as above.
        ComPtr<ID3D12Resource> pbrPerDrawConstantBuffer_;
        void* pbrPerDrawConstantBufferMapped_ = nullptr;
        ComPtr<ID3D12Resource> pbrLightsConstantBuffer_;
        void* pbrLightsConstantBufferMapped_ = nullptr;
        // D3D12 PBR reconciliation follow-up: lazily-created 1x1 fallback textures for PbrEffect/
        // SkinnedPbrEffect's optional map slots (see GetOrCreateDefaultWhiteTextureEXT()/
        // GetOrCreateDefaultFlatNormalTextureEXT()'s own doc comments) -- owned here, reused
        // across every draw that needs a fallback.
        std::unique_ptr<ITextureRenderer> defaultWhiteTexture_;
        std::unique_ptr<ITextureRenderer> defaultFlatNormalTexture_;
        // DX-111 (finish): instanced3d's own hand-built PSO (see GetOrCreateInstancedPsoEXT's doc
        // comment) -- reused across DrawInstancedPrimitivesEx calls.
        // REMED-GFX-123: keyed by InstanceDataStepRate. The rate is baked into the PSO's input
        // layout, so a single cached PSO would silently reuse the previous draw's
        // VertexBufferBinding.InstanceFrequency. One entry per distinct rate (one or two in
        // practice), so alternating frequencies never build a PSO per draw.
        std::unordered_map<UINT, ComPtr<ID3D12PipelineState>> instancedPsos_;

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
        // REMED-GFX-077: BlendState output-merger write state, folded into the PSO cache key/desc.
        int currentColorWriteMask_ = 15;              // ColorWriteChannels.All (bit0=R..bit3=A)
        unsigned int currentSampleMask_ = 0xFFFFFFFFu; // MultiSampleMask == -1 (all samples)
        bool currentDepthEnable_ = false;
        bool currentDepthWriteEnable_ = false;
        int currentDepthFunc_ = 3;       // CompareFunction::LessEqual (real XNA ordinal)
        int currentCullMode_ = 0;        // CullMode::None
        int currentFillMode_ = 0;        // FillMode::Solid

        // DX-117: additional MRT targets beyond the primary (index 0, tracked by boundColor*_
        // above) -- Clear() independently transitions+clears each; draws remain single-target
        // (boundColorRtv_ only), see BindOffscreenColorTargetsEXT's own doc comment for why.
        static constexpr int kMaxExtraMrtTargets = 7; // 8 total (D3D12_SIMULTANEOUS_RENDER_TARGET_COUNT) - 1 primary
        ID3D12Resource* extraMrtResources_[kMaxExtraMrtTargets] = {};
        D3D12_CPU_DESCRIPTOR_HANDLE extraMrtRtvs_[kMaxExtraMrtTargets]{};
        int extraMrtCount_ = 0;

        // DX-120: the currently-active occlusion query heap (non-owning, nullptr when no query is
        // active), always slot 0. Real, non-obvious constraint discovered while landing DX-120:
        // BeginQuery()/EndQuery() must be recorded within the SAME command-list submission as the
        // draw(s) they bracket (a Vulkan/vkd3d-proton requirement this renderer's own per-draw-call
        // self-submission architecture doesn't naturally satisfy) -- so every draw-recording method
        // (DrawColoredPrimitives/DrawIndexedColoredPrimitives/DrawPrimitivesExImpl/
        // DrawInstancedPrimitivesEx) checks this field and, when set, wraps its own single
        // command-list recording with BeginQuery right after Reset() and EndQuery right before
        // Close() -- correct for exactly one draw call between Begin()/End() (this design's own
        // honest scope boundary; see D3D12OcclusionQueryRenderer's own header doc comment for the
        // multi-draw-accumulation gap this does not attempt to solve).
        ID3D12QueryHeap* activeOcclusionQueryHeap_ = nullptr;
    };
}
