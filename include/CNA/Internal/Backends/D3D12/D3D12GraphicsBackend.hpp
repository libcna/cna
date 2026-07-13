#pragma once

// plan_dx.md Phase DX12: D3D12 backend. DX-101 landed CMake wiring + an honest all-stub skeleton.
// DX-102/DX-103/DX-104/DX-105 (this revision) make the device-lifetime group real: ID3D12Device +
// command queue + descriptor heaps (RTV/DSV/CBV_SRV_UAV) + per-frame command allocators/command
// list + fence-based frame synchronization. Clear()/Present()/draw calls are STILL honest
// "not yet implemented" stubs -- those need DX-106 (resource barriers), DX-107 (PSOs), DX-108
// (root signatures) and DX-109 (resources) first, none of which exist yet.
//
// Windows-only (see CMakeLists.txt's FATAL_ERROR guard for non-Windows CNA_GRAPHICS_BACKEND=D3D12).

#include "../Common/IGraphicsBackend.hpp"

#include <d3d12.h>
#include <dxgi1_5.h>
#include <wrl/client.h>

#include <cstdint>

namespace CNA::Internal::Backends::D3D12
{
    using Microsoft::WRL::ComPtr;

    /**
     * D3D12 graphics backend (plan_dx.md Phase DX12). DX-100's own spike (real Wine+vkd3d-proton
     * run) found `D3D12CreateDevice`/`CreateCommandQueue`/descriptor-heap/fence/command-allocator/
     * command-list calls all work genuinely well locally (feature level 12_1, DXR 1.1, SM 6.8 on
     * the real GPU), but `CreateSwapChainForHwnd` with `DXGI_SWAP_EFFECT_FLIP_DISCARD` crashes
     * inside vanilla Wine's own `dxgi.dll` -- confirmed again by this revision's own real attempt
     * (see `DX-102`'s plan row). Consequently:
     *   - Device-lifetime resources (device_/factory_/commandQueue_/descriptor heaps/command
     *     allocators+list/fence) are created unconditionally and are the real, Wine-provable part
     *     of this backend today.
     *   - Swap-chain creation is attempted for real (`CreateSwapChainResources()`), matching
     *     D3D11's own `FLIP_DISCARD` production convention, but ONLY when a window is supplied,
     *     and a failure there is caught (HRESULT-level) and downgraded to `swapChainAvailable_ =
     *     false` rather than throwing -- so constructing this backend off-screen (no window) never
     *     touches the crash-prone path at all, and the primary D3D12 CTest suite stays
     *     off-screen/swap-chain-free per DX-100's own recommendation.
     *   - `Clear()`/`Present()`/draw calls remain honest "not yet implemented" stubs -- DX-106
     *     (resource barriers), DX-107 (PSOs), DX-108 (root signatures), DX-109 (resources) are all
     *     still unstarted follow-up work.
     */
    class D3D12GraphicsBackend final : public IGraphicsBackend
    {
    public:
        /// Matches Vulkan's own `MaxFramesInFlight` convention (VulkanGraphicsBackend.hpp) for
        /// this project's established frame-in-flight depth -- D3D12 needs its own explicit
        /// per-frame command-allocator set (DX-104), unlike D3D11's implicit driver-managed model.
        static constexpr int kFramesInFlight = 2;

        explicit D3D12GraphicsBackend(const GraphicsBackendCreateArgs& args);
        ~D3D12GraphicsBackend() override;

        D3D12GraphicsBackend(const D3D12GraphicsBackend&) = delete;
        D3D12GraphicsBackend& operator=(const D3D12GraphicsBackend&) = delete;

        // ---- IGraphicsBackend: honest "not yet implemented" stubs (DX-106 onward) ----
        void Clear(float r, float g, float b, float a) override;
        void Present() override;
        void GetViewportSize(int& width, int& height) override;
        void SetVirtualResolution(int width, int height) override;
        void SetPresentationMode(int mode) override;

        SDL_Window* GetWindowInternal() const override { return window_; }
        SDL_Renderer* GetRendererInternal() const override { return nullptr; }

        std::unique_ptr<ITextureBackend> CreateTexture(const ImageData& data) override;
        std::unique_ptr<ISpriteBatchBackend> CreateSpriteBatch() override;

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

        // ---- NOXNA (DX-102/DX-103/DX-104/DX-105): real device-lifetime accessors for tests and
        // for whichever Phase DX12 task lands next (DX-106 onward) to build on without duplicating
        // this backend's own device/heap/command-list/fence creation. ----

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

        /** @brief Allocates the next free RTV descriptor from the device-lifetime RTV heap
         *  (DX-103's bump allocator) and returns its CPU handle. Throws if the heap is exhausted. */
        [[nodiscard]] D3D12_CPU_DESCRIPTOR_HANDLE AllocateRtvDescriptorEXT();
        /** @brief Same as AllocateRtvDescriptorEXT(), for the DSV heap. */
        [[nodiscard]] D3D12_CPU_DESCRIPTOR_HANDLE AllocateDsvDescriptorEXT();
        /** @brief Allocates the next free slot in the shader-visible CBV/SRV/UAV heap and returns
         *  both its CPU handle (for CreateXxxView calls) and GPU handle (for
         *  SetGraphicsRootDescriptorTable). Throws if the heap is exhausted. */
        void AllocateCbvSrvUavDescriptorEXT(D3D12_CPU_DESCRIPTOR_HANDLE& outCpu,
                                            D3D12_GPU_DESCRIPTOR_HANDLE& outGpu);
        /** @brief The shader-visible CBV/SRV/UAV heap itself, for SetDescriptorHeaps() calls. */
        [[nodiscard]] ID3D12DescriptorHeap* GetCbvSrvUavHeapEXT() const { return cbvSrvUavHeap_.Get(); }

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

    private:
        [[noreturn]] static void NotYetImplemented(const char* what);

        void CreateDeviceResources();
        void CreateCommandQueueResources();
        void CreateDescriptorHeapResources();
        void CreateCommandListResources();
        void CreateFenceResources();
        /// DX-102: real swap-chain attempt, only when window_ != nullptr. Catches HRESULT-level
        /// failure and downgrades to swapChainAvailable_ = false rather than throwing -- see the
        /// class-level doc comment for why. A genuine Wine-level crash (as opposed to a clean
        /// HRESULT failure) cannot be caught here or anywhere in-process; that risk is why the
        /// primary D3D12 CTest suite never constructs this backend with a real window (see
        /// examples/d3d12_smoke_test.cpp's own comment block).
        void CreateSwapChainResources();

        SDL_Window* window_ = nullptr;
        int virtualWidth_ = 0;
        int virtualHeight_ = 0;

        // Device lifetime (plan_dx.md design decision 11's own grouping, reused for D3D12).
        ComPtr<ID3D12Device> device_;
        ComPtr<IDXGIFactory4> factory_;
        ComPtr<ID3D12CommandQueue> commandQueue_;
        D3D_FEATURE_LEVEL featureLevel_ = D3D_FEATURE_LEVEL_11_0;
        bool debugLayerEnabled_ = false;
        bool allowTearingSupported_ = false;

        // DX-103: descriptor heaps + bump allocators. Capacities are a deliberately simple first
        // implementation (plan_dx.md DX-103's own row explicitly allows this) -- no free-list
        // reuse of released descriptors yet, since nothing releases any yet (DX-109 is unstarted).
        static constexpr UINT kRtvHeapCapacity = 8;
        static constexpr UINT kDsvHeapCapacity = 8;
        static constexpr UINT kCbvSrvUavHeapCapacity = 64;
        ComPtr<ID3D12DescriptorHeap> rtvHeap_;
        ComPtr<ID3D12DescriptorHeap> dsvHeap_;
        ComPtr<ID3D12DescriptorHeap> cbvSrvUavHeap_;
        UINT rtvDescriptorSize_ = 0;
        UINT dsvDescriptorSize_ = 0;
        UINT cbvSrvUavDescriptorSize_ = 0;
        UINT rtvHeapNextIndex_ = 0;
        UINT dsvHeapNextIndex_ = 0;
        UINT cbvSrvUavHeapNextIndex_ = 0;

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
    };
}
