// plan_dx.md Phase DX12 (DX-102/DX-103/DX-104/DX-105): D3D12 device-lifetime resources -- real
// ID3D12Device + command queue + descriptor heaps + per-frame command allocators/command list +
// fence-based synchronization. Clear()/Present()/draw calls are still honest "not yet implemented"
// stubs -- see D3D12GraphicsBackend.hpp's class doc comment for exactly why and what's next.
#include "CNA/Internal/Backends/D3D12/D3D12GraphicsBackend.hpp"
#include "CNA/Internal/Backends/D3D12/D3D12Buffers.hpp"
#include "CNA/Internal/Backends/D3D12/D3D12Textures.hpp"
#include "CNA/Internal/Backends/D3D12/D3D12SpriteBatch.hpp"
#include "CNA/Internal/Backends/D3DCommon/D3DConstantBuffers.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <string>

namespace CNA::Internal::Backends::D3D12
{
    using namespace CNA::Internal::Backends::D3DCommon;

    namespace
    {
        std::string FormatHr(HRESULT hr)
        {
            char buf[32];
            std::snprintf(buf, sizeof(buf), "0x%08lX", static_cast<unsigned long>(hr));
            return buf;
        }

        /// DX-111: same per-PrimitiveType vertex-count formula D3D11GraphicsBackend.cpp's own
        /// VertexCountForPrimitives (and Vulkan/EasyGL's) already duplicate locally -- not shared via
        /// a common header today, so this follows the existing precedent rather than introducing one.
        int VertexCountForPrimitives(PrimitiveType pt, int primitiveCount)
        {
            switch (pt)
            {
            case PrimitiveType::TriangleList:  return primitiveCount * 3;
            case PrimitiveType::TriangleStrip: return primitiveCount + 2;
            case PrimitiveType::LineList:      return primitiveCount * 2;
            case PrimitiveType::LineStrip:     return primitiveCount + 1;
            }
            return 0;
        }

        /// D3D12_PRIMITIVE_TOPOLOGY is D3D_PRIMITIVE_TOPOLOGY under the hood -- same underlying enum
        /// D3D11 uses for IASetPrimitiveTopology, just a different typedef name.
        D3D12_PRIMITIVE_TOPOLOGY ToD3D12Topology(PrimitiveType pt)
        {
            switch (pt)
            {
            case PrimitiveType::TriangleList:  return D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
            case PrimitiveType::TriangleStrip: return D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP;
            case PrimitiveType::LineList:      return D3D_PRIMITIVE_TOPOLOGY_LINELIST;
            case PrimitiveType::LineStrip:     return D3D_PRIMITIVE_TOPOLOGY_LINESTRIP;
            }
            return D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
        }
    }

    void D3D12GraphicsBackend::NotYetImplemented(const char* what)
    {
        throw std::runtime_error(std::string("D3D12 backend: ") + what +
                                  " not yet implemented (plan_dx.md DX-106 onward -- barriers/PSOs/"
                                  "root signatures/resources)");
    }

    D3D12GraphicsBackend::D3D12GraphicsBackend(const GraphicsBackendCreateArgs& args)
        : window_(args.window)
        , virtualWidth_(args.virtualWidth)
        , virtualHeight_(args.virtualHeight)
    {
        CreateDeviceResources();
        CreateCommandQueueResources();
        CreateDescriptorHeapResources();
        CreateCommandListResources();
        CreateFenceResources();

        // DX-102: only attempted when a real window is supplied -- an off-screen construction
        // (args.window == nullptr, what the primary D3D12 CTest suite always uses on this Wine dev
        // loop, see examples/d3d12_smoke_test.cpp) never touches CreateSwapChainForHwnd at all.
        if (window_)
        {
            CreateSwapChainResources();
        }

        SDL_Log("[D3D12] Device-lifetime resources created (plan_dx.md DX-102/103/104/105) -- "
                "feature level 0x%04x, debug layer %s, tearing %s, swap chain %s.",
                static_cast<unsigned>(featureLevel_),
                debugLayerEnabled_ ? "enabled" : "disabled",
                allowTearingSupported_ ? "supported" : "unsupported",
                swapChainAvailable_ ? "available" : "unavailable");
    }

    D3D12GraphicsBackend::~D3D12GraphicsBackend()
    {
        if (fence_ && fenceEvent_)
        {
            // Best-effort drain so the device isn't torn down mid-flight-GPU-work.
            for (int i = 0; i < kFramesInFlight; ++i)
            {
                if (fence_->GetCompletedValue() < frameFenceValues_[i] && frameFenceValues_[i] != 0)
                {
                    fence_->SetEventOnCompletion(frameFenceValues_[i], fenceEvent_);
                    WaitForSingleObject(fenceEvent_, INFINITE);
                }
            }
        }
        if (fenceEvent_)
        {
            CloseHandle(fenceEvent_);
            fenceEvent_ = nullptr;
        }
    }

    void D3D12GraphicsBackend::CreateDeviceResources()
    {
        UINT factoryFlags = 0;

        // design decision 12's own "debug layer is best-effort, never a hard requirement" applies
        // identically here -- D3D12's debug interface is a separate opt-in call
        // (D3D12GetDebugInterface), unlike D3D11's D3D11_CREATE_DEVICE_DEBUG flag.
        {
            ComPtr<ID3D12Debug> debugController;
            if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(debugController.ReleaseAndGetAddressOf()))))
            {
                debugController->EnableDebugLayer();
                debugLayerEnabled_ = true;
                factoryFlags |= DXGI_CREATE_FACTORY_DEBUG;
            }
            else
            {
                SDL_Log("[D3D12] D3D12 debug layer unavailable; continuing without it.");
            }
        }

        HRESULT hr = CreateDXGIFactory2(factoryFlags, IID_PPV_ARGS(factory_.ReleaseAndGetAddressOf()));
        if (FAILED(hr))
            throw std::runtime_error("CreateDXGIFactory2 failed, hr=" + FormatHr(hr));

        // DX-102: unlike D3D11CreateDevice's own feature-level fallback ARRAY, D3D12CreateDevice
        // only accepts a single MinimumFeatureLevel -- so the fallback here is a real retry loop,
        // not one call with an array.
        static const D3D_FEATURE_LEVEL kFeatureLevels[] = {
            D3D_FEATURE_LEVEL_12_1,
            D3D_FEATURE_LEVEL_12_0,
            D3D_FEATURE_LEVEL_11_1,
            D3D_FEATURE_LEVEL_11_0,
        };

        // Pick the first hardware (non-software) adapter the factory enumerates -- IDXGIFactory6's
        // EnumAdapterByGpuPreference would be a nicer "prefer high performance" query, but a plain
        // EnumAdapters1 walk is sufficient for a first implementation (matches this task's own
        // "simple strategy is fine, document it" allowance, DX-103's row).
        ComPtr<IDXGIAdapter1> chosenAdapter;
        for (UINT i = 0; ; ++i)
        {
            ComPtr<IDXGIAdapter1> adapter;
            if (factory_->EnumAdapters1(i, adapter.ReleaseAndGetAddressOf()) == DXGI_ERROR_NOT_FOUND)
                break;

            DXGI_ADAPTER_DESC1 desc{};
            adapter->GetDesc1(&desc);
            if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)
                continue; // skip the WARP/software adapter for the default path

            chosenAdapter = adapter;
            break;
        }

        bool created = false;
        for (D3D_FEATURE_LEVEL level : kFeatureLevels)
        {
            hr = D3D12CreateDevice(chosenAdapter.Get(), level, IID_PPV_ARGS(device_.ReleaseAndGetAddressOf()));
            if (SUCCEEDED(hr))
            {
                featureLevel_ = level;
                created = true;
                break;
            }
        }

        if (!created)
            throw std::runtime_error("D3D12CreateDevice failed for every feature level in the fallback list, last hr=" + FormatHr(hr));

        // DX-102: tearing-capability query, same DXGI_FEATURE_PRESENT_ALLOW_TEARING check D3D11's
        // own DX-22 uses, just off the factory directly (D3D12 has no IDXGIDevice indirection).
        ComPtr<IDXGIFactory5> factory5;
        if (SUCCEEDED(factory_.As(&factory5)))
        {
            BOOL allowTearing = FALSE;
            if (SUCCEEDED(factory5->CheckFeatureSupport(
                    DXGI_FEATURE_PRESENT_ALLOW_TEARING, &allowTearing, sizeof(allowTearing))))
            {
                allowTearingSupported_ = allowTearing != FALSE;
            }
        }
    }

    void D3D12GraphicsBackend::CreateCommandQueueResources()
    {
        D3D12_COMMAND_QUEUE_DESC desc{};
        desc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
        desc.Priority = D3D12_COMMAND_QUEUE_PRIORITY_NORMAL;
        desc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;

        HRESULT hr = device_->CreateCommandQueue(&desc, IID_PPV_ARGS(commandQueue_.ReleaseAndGetAddressOf()));
        if (FAILED(hr))
            throw std::runtime_error("ID3D12Device::CreateCommandQueue failed, hr=" + FormatHr(hr));
    }

    void D3D12GraphicsBackend::CreateDescriptorHeapResources()
    {
        auto createHeap = [&](D3D12_DESCRIPTOR_HEAP_TYPE type, UINT capacity, bool shaderVisible,
                              ComPtr<ID3D12DescriptorHeap>& outHeap, const char* what)
        {
            D3D12_DESCRIPTOR_HEAP_DESC desc{};
            desc.Type = type;
            desc.NumDescriptors = capacity;
            desc.Flags = shaderVisible ? D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE : D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
            HRESULT hr = device_->CreateDescriptorHeap(&desc, IID_PPV_ARGS(outHeap.ReleaseAndGetAddressOf()));
            if (FAILED(hr))
                throw std::runtime_error(std::string("ID3D12Device::CreateDescriptorHeap(") + what + ") failed, hr=" + FormatHr(hr));
        };

        createHeap(D3D12_DESCRIPTOR_HEAP_TYPE_RTV, kRtvHeapCapacity, false, rtvHeap_, "RTV");
        createHeap(D3D12_DESCRIPTOR_HEAP_TYPE_DSV, kDsvHeapCapacity, false, dsvHeap_, "DSV");
        createHeap(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, kCbvSrvUavHeapCapacity, true, cbvSrvUavHeap_, "CBV_SRV_UAV");

        rtvDescriptorSize_ = device_->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
        dsvDescriptorSize_ = device_->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_DSV);
        cbvSrvUavDescriptorSize_ = device_->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    }

    void D3D12GraphicsBackend::CreateCommandListResources()
    {
        for (int i = 0; i < kFramesInFlight; ++i)
        {
            HRESULT hr = device_->CreateCommandAllocator(
                D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(commandAllocators_[i].ReleaseAndGetAddressOf()));
            if (FAILED(hr))
                throw std::runtime_error("ID3D12Device::CreateCommandAllocator failed, hr=" + FormatHr(hr));
        }

        // A command list is created already-open against allocator 0 -- close it immediately since
        // nothing is being recorded yet; every real caller (tests, and whichever future task lands
        // actual recording) must Reset() it against the correct frame's allocator first.
        HRESULT hr = device_->CreateCommandList(
            0, D3D12_COMMAND_LIST_TYPE_DIRECT, commandAllocators_[0].Get(), nullptr,
            IID_PPV_ARGS(commandList_.ReleaseAndGetAddressOf()));
        if (FAILED(hr))
            throw std::runtime_error("ID3D12Device::CreateCommandList failed, hr=" + FormatHr(hr));

        hr = commandList_->Close();
        if (FAILED(hr))
            throw std::runtime_error("ID3D12GraphicsCommandList::Close (initial) failed, hr=" + FormatHr(hr));
    }

    void D3D12GraphicsBackend::CreateFenceResources()
    {
        HRESULT hr = device_->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(fence_.ReleaseAndGetAddressOf()));
        if (FAILED(hr))
            throw std::runtime_error("ID3D12Device::CreateFence failed, hr=" + FormatHr(hr));

        fenceEvent_ = CreateEventExW(nullptr, nullptr, 0, EVENT_ALL_ACCESS);
        if (!fenceEvent_)
            throw std::runtime_error("CreateEventExW for the D3D12 fence failed");
    }

    void D3D12GraphicsBackend::CreateSwapChainResources()
    {
        HWND hwnd = static_cast<HWND>(SDL_GetPointerProperty(
            SDL_GetWindowProperties(window_), SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr));
        if (!hwnd)
        {
            SDL_Log("[D3D12] Could not obtain HWND from SDL window; swap chain unavailable.");
            swapChainAvailable_ = false;
            return;
        }

        int width = virtualWidth_ > 0 ? virtualWidth_ : 1024;
        int height = virtualHeight_ > 0 ? virtualHeight_ : 768;
        SDL_GetWindowSizeInPixels(window_, &width, &height);

        DXGI_SWAP_CHAIN_DESC1 desc{};
        desc.Width = static_cast<UINT>(width);
        desc.Height = static_cast<UINT>(height);
        desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM; // matches D3D11's own DX-11-fmt SurfaceFormat::Color mapping
        desc.SampleDesc.Count = 1;
        desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        desc.BufferCount = kFramesInFlight;
        desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD; // production-correct convention (matches D3D11's DX-23);
                                                          // DX-100's own spike found this crashes under vanilla
                                                          // Wine's dxgi.dll -- see this method's own real attempt
                                                          // below and the class doc comment for why that's caught,
                                                          // not thrown, and why the primary CTest suite never
                                                          // reaches this code path.
        desc.Flags = allowTearingSupported_ ? DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING : 0;

        ComPtr<IDXGISwapChain1> swapChain1;
        HRESULT hr = factory_->CreateSwapChainForHwnd(
            commandQueue_.Get(), hwnd, &desc, nullptr, nullptr, swapChain1.ReleaseAndGetAddressOf());
        if (FAILED(hr))
        {
            SDL_Log("[D3D12] CreateSwapChainForHwnd failed, hr=%s; continuing off-screen "
                    "(plan_dx.md DX-102's own documented Wine limitation -- real verification is "
                    "DX-114's job, on real Windows hardware).",
                    FormatHr(hr).c_str());
            swapChainAvailable_ = false;
            return;
        }

        hr = swapChain1.As(&swapChain_);
        if (FAILED(hr))
        {
            SDL_Log("[D3D12] IDXGISwapChain1 -> IDXGISwapChain3 QueryInterface failed, hr=%s; "
                    "continuing off-screen.", FormatHr(hr).c_str());
            swapChainAvailable_ = false;
            return;
        }

        swapChainAvailable_ = true;
    }

    D3D12_CPU_DESCRIPTOR_HANDLE D3D12GraphicsBackend::AllocateRtvDescriptorEXT()
    {
        if (rtvHeapNextIndex_ >= kRtvHeapCapacity)
            throw std::runtime_error("D3D12GraphicsBackend: RTV descriptor heap exhausted (DX-103's fixed capacity)");
        D3D12_CPU_DESCRIPTOR_HANDLE handle = rtvHeap_->GetCPUDescriptorHandleForHeapStart();
        handle.ptr += static_cast<SIZE_T>(rtvHeapNextIndex_) * rtvDescriptorSize_;
        ++rtvHeapNextIndex_;
        return handle;
    }

    D3D12_CPU_DESCRIPTOR_HANDLE D3D12GraphicsBackend::AllocateDsvDescriptorEXT()
    {
        if (dsvHeapNextIndex_ >= kDsvHeapCapacity)
            throw std::runtime_error("D3D12GraphicsBackend: DSV descriptor heap exhausted (DX-103's fixed capacity)");
        D3D12_CPU_DESCRIPTOR_HANDLE handle = dsvHeap_->GetCPUDescriptorHandleForHeapStart();
        handle.ptr += static_cast<SIZE_T>(dsvHeapNextIndex_) * dsvDescriptorSize_;
        ++dsvHeapNextIndex_;
        return handle;
    }

    void D3D12GraphicsBackend::AllocateCbvSrvUavDescriptorEXT(D3D12_CPU_DESCRIPTOR_HANDLE& outCpu,
                                                               D3D12_GPU_DESCRIPTOR_HANDLE& outGpu)
    {
        if (cbvSrvUavHeapNextIndex_ >= kCbvSrvUavHeapCapacity)
            throw std::runtime_error("D3D12GraphicsBackend: CBV/SRV/UAV descriptor heap exhausted (DX-103's fixed capacity)");
        outCpu = cbvSrvUavHeap_->GetCPUDescriptorHandleForHeapStart();
        outCpu.ptr += static_cast<SIZE_T>(cbvSrvUavHeapNextIndex_) * cbvSrvUavDescriptorSize_;
        outGpu = cbvSrvUavHeap_->GetGPUDescriptorHandleForHeapStart();
        outGpu.ptr += static_cast<UINT64>(cbvSrvUavHeapNextIndex_) * cbvSrvUavDescriptorSize_;
        ++cbvSrvUavHeapNextIndex_;
    }

    void D3D12GraphicsBackend::ExecuteCommandListAndWaitEXT(ID3D12CommandList* commandList)
    {
        commandQueue_->ExecuteCommandLists(1, &commandList);

        const std::uint64_t valueToSignal = nextFenceValue_++;
        HRESULT hr = commandQueue_->Signal(fence_.Get(), valueToSignal);
        if (FAILED(hr))
        {
            CheckDeviceRemovedEXT(hr); // DX-110: detection-only here, matching D3D11's own convention
            throw std::runtime_error("ID3D12CommandQueue::Signal failed, hr=" + FormatHr(hr));
        }

        if (fence_->GetCompletedValue() < valueToSignal)
        {
            hr = fence_->SetEventOnCompletion(valueToSignal, fenceEvent_);
            if (FAILED(hr))
                throw std::runtime_error("ID3D12Fence::SetEventOnCompletion failed, hr=" + FormatHr(hr));
            WaitForSingleObject(fenceEvent_, INFINITE);
        }
    }

    std::uint64_t D3D12GraphicsBackend::SignalAndWaitForFrameEXT(int frameIndex)
    {
        const std::uint64_t valueToSignal = nextFenceValue_++;
        HRESULT hr = commandQueue_->Signal(fence_.Get(), valueToSignal);
        if (FAILED(hr))
        {
            CheckDeviceRemovedEXT(hr);
            throw std::runtime_error("ID3D12CommandQueue::Signal failed, hr=" + FormatHr(hr));
        }

        const std::uint64_t previousValueForThisFrame = frameFenceValues_[frameIndex];
        if (previousValueForThisFrame != 0 && fence_->GetCompletedValue() < previousValueForThisFrame)
        {
            hr = fence_->SetEventOnCompletion(previousValueForThisFrame, fenceEvent_);
            if (FAILED(hr))
                throw std::runtime_error("ID3D12Fence::SetEventOnCompletion failed, hr=" + FormatHr(hr));
            WaitForSingleObject(fenceEvent_, INFINITE);
        }

        frameFenceValues_[frameIndex] = valueToSignal;
        return valueToSignal;
    }

    void D3D12GraphicsBackend::CheckDeviceRemovedEXT(HRESULT hr) const
    {
        if (hr == DXGI_ERROR_DEVICE_REMOVED || hr == DXGI_ERROR_DEVICE_RESET)
        {
            const HRESULT reason = device_ ? device_->GetDeviceRemovedReason() : hr;
            SDL_Log("[D3D12] Device removed/reset! reason=%s (plan_dx.md DX-110)", FormatHr(reason).c_str());
        }
    }

    void D3D12GraphicsBackend::RecreateDeviceEXT()
    {
        // Best-effort drain of anything in flight before tearing down -- mirrors the destructor's
        // own drain (it's not safe to release a fence/allocator/command list the GPU may still be
        // referencing, even on a real device-removed event, since the removal only affects future
        // submissions, not necessarily work already in the queue).
        if (fence_ && fenceEvent_)
        {
            for (int i = 0; i < kFramesInFlight; ++i)
            {
                if (frameFenceValues_[i] != 0 && fence_->GetCompletedValue() < frameFenceValues_[i])
                {
                    fence_->SetEventOnCompletion(frameFenceValues_[i], fenceEvent_);
                    WaitForSingleObject(fenceEvent_, INFINITE);
                }
            }
        }

        // Drop every ComPtr tied to the (possibly removed) device -- further calls against a
        // removed device's own objects are meaningless, and holding onto them would also leak.
        swapChain_.Reset();
        swapChainAvailable_ = false;
        commandList_.Reset();
        for (auto& allocator : commandAllocators_) allocator.Reset();
        if (fenceEvent_) { CloseHandle(fenceEvent_); fenceEvent_ = nullptr; }
        fence_.Reset();
        cbvSrvUavHeap_.Reset();
        dsvHeap_.Reset();
        rtvHeap_.Reset();
        commandQueue_.Reset();
        device_.Reset();
        factory_.Reset();

        // DX-111: root-signature/PSO caches and the persistent colored3d constant buffers are all
        // tied to the (possibly removed) device too -- a stale cached ID3D12RootSignature/
        // ID3D12PipelineState/mapped ID3D12Resource from before recreation would be meaningless (and
        // dangerous to keep calling into) against the brand-new device below, exactly the same
        // reasoning as every ComPtr reset above. Plain member reassignment is sufficient since both
        // caches are simple movable/copy-assignable value types, not pointers.
        rootSigCache_ = D3D12RootSignatureCache();
        psoCache_ = D3D12PipelineStateCache();
        perDrawConstantBuffer_.Reset();
        perDrawConstantBufferMapped_ = nullptr;
        fogConstantBuffer_.Reset();
        fogConstantBufferMapped_ = nullptr;
        lightingConstantBuffer_.Reset();
        lightingConstantBufferMapped_ = nullptr;
        alphaTestConstantBuffer_.Reset();
        alphaTestConstantBufferMapped_ = nullptr;
        // DX-111 (finish/closing): skinned3d's and env_map3d's own persistent constant buffers, and
        // instanced3d's own hand-built PSO, are just as device-tied as every buffer above -- a real,
        // pre-existing gap in this function (found while landing env_map3d) is fixed here rather than
        // left for a later session to rediscover the same way.
        boneConstantBuffer_.Reset();
        boneConstantBufferMapped_ = nullptr;
        skinnedExtraConstantBuffer_.Reset();
        skinnedExtraConstantBufferMapped_ = nullptr;
        envMapPerDrawConstantBuffer_.Reset();
        envMapPerDrawConstantBufferMapped_ = nullptr;
        envMapConstantBuffer_.Reset();
        envMapConstantBufferMapped_ = nullptr;
        instancedPso_.Reset();
        // The bound off-screen color target (if any) was owned by the caller and lived on the old
        // device -- it's gone too; the caller must recreate its render target and rebind after
        // calling RecreateDeviceEXT(), same as it must recreate any of its own DX-109 resources.
        UnbindOffscreenColorTargetEXT();

        rtvHeapNextIndex_ = 0;
        dsvHeapNextIndex_ = 0;
        cbvSrvUavHeapNextIndex_ = 0;
        nextFenceValue_ = 1;
        for (auto& value : frameFenceValues_) value = 0;
        debugLayerEnabled_ = false;
        allowTearingSupported_ = false;

        // Every previously-tracked resource's real D3D12 object is gone along with the removed
        // device -- DX-109's own vertex/index buffers and textures would need to be recreated by
        // their owning Texture2D/VertexBuffer/etc. wrapper objects (out of this backend's own
        // scope -- GraphicsDevice-level content-reload is a separate, larger concern XNA itself
        // handles via GraphicsDevice::DeviceReset, not something this constructor-time recreation
        // can or should attempt), so the tracker itself must be cleared rather than left holding
        // stale pointers into freed memory.
        resourceStates_.Clear();

        // Recreate every device-lifetime group from scratch, identical to construction.
        CreateDeviceResources();
        CreateCommandQueueResources();
        CreateDescriptorHeapResources();
        CreateCommandListResources();
        CreateFenceResources();
        if (window_) CreateSwapChainResources();

        SDL_Log("[D3D12] RecreateDeviceEXT(): device-lifetime resources fully recreated after a "
                "(real or forced) device-removed event (plan_dx.md DX-110) -- feature level 0x%04x, "
                "debug layer %s, tearing %s, swap chain %s.",
                static_cast<unsigned>(featureLevel_),
                debugLayerEnabled_ ? "enabled" : "disabled",
                allowTearingSupported_ ? "supported" : "unsupported",
                swapChainAvailable_ ? "available" : "unavailable");
    }

    void D3D12GraphicsBackend::BindOffscreenColorTargetEXT(ID3D12Resource* resource,
                                                            D3D12_CPU_DESCRIPTOR_HANDLE rtv,
                                                            DXGI_FORMAT format, int width, int height)
    {
        boundColorResource_ = resource;
        boundColorRtv_ = rtv;
        boundColorFormat_ = format;
        boundColorWidth_ = width;
        boundColorHeight_ = height;
    }

    void D3D12GraphicsBackend::UnbindOffscreenColorTargetEXT()
    {
        boundColorResource_ = nullptr;
        boundColorWidth_ = 0;
        boundColorHeight_ = 0;
    }

    void D3D12GraphicsBackend::CreateUploadConstantBuffer(UINT byteWidth, ComPtr<ID3D12Resource>& outResource,
                                                           void*& outMapped)
    {
        D3D12_HEAP_PROPERTIES heapProps{};
        heapProps.Type = D3D12_HEAP_TYPE_UPLOAD;

        D3D12_RESOURCE_DESC desc{};
        desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        desc.Width = byteWidth;
        desc.Height = 1;
        desc.DepthOrArraySize = 1;
        desc.MipLevels = 1;
        desc.Format = DXGI_FORMAT_UNKNOWN;
        desc.SampleDesc.Count = 1;
        desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

        HRESULT hr = device_->CreateCommittedResource(
            &heapProps, D3D12_HEAP_FLAG_NONE, &desc,
            D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(outResource.ReleaseAndGetAddressOf()));
        if (FAILED(hr))
            throw std::runtime_error("D3D12GraphicsBackend: constant buffer CreateCommittedResource failed, hr=" + FormatHr(hr));

        const D3D12_RANGE readRange{0, 0}; // never read back on the CPU
        hr = outResource->Map(0, &readRange, &outMapped);
        if (FAILED(hr))
            throw std::runtime_error("D3D12GraphicsBackend: constant buffer Map failed, hr=" + FormatHr(hr));
    }

    ID3D12Resource* D3D12GraphicsBackend::GetOrCreatePerDrawConstantBufferEXT()
    {
        if (!perDrawConstantBuffer_)
            CreateUploadConstantBuffer(sizeof(D3DPerDrawConstants), perDrawConstantBuffer_, perDrawConstantBufferMapped_);
        return perDrawConstantBuffer_.Get();
    }

    ID3D12Resource* D3D12GraphicsBackend::GetOrCreateFogConstantBufferEXT()
    {
        if (!fogConstantBuffer_)
            CreateUploadConstantBuffer(sizeof(D3DFogConstants), fogConstantBuffer_, fogConstantBufferMapped_);
        return fogConstantBuffer_.Get();
    }

    ID3D12Resource* D3D12GraphicsBackend::GetOrCreateLightingConstantBufferEXT()
    {
        if (!lightingConstantBuffer_)
            CreateUploadConstantBuffer(sizeof(D3DLightingConstants), lightingConstantBuffer_, lightingConstantBufferMapped_);
        return lightingConstantBuffer_.Get();
    }

    ID3D12Resource* D3D12GraphicsBackend::GetOrCreateAlphaTestConstantBufferEXT()
    {
        if (!alphaTestConstantBuffer_)
            CreateUploadConstantBuffer(sizeof(D3DAlphaTestConstants), alphaTestConstantBuffer_, alphaTestConstantBufferMapped_);
        return alphaTestConstantBuffer_.Get();
    }

    ID3D12Resource* D3D12GraphicsBackend::GetOrCreateBoneConstantBufferEXT()
    {
        if (!boneConstantBuffer_)
            CreateUploadConstantBuffer(sizeof(D3DBoneConstants), boneConstantBuffer_, boneConstantBufferMapped_);
        return boneConstantBuffer_.Get();
    }

    ID3D12Resource* D3D12GraphicsBackend::GetOrCreateSkinnedExtraConstantBufferEXT()
    {
        if (!skinnedExtraConstantBuffer_)
            CreateUploadConstantBuffer(sizeof(D3DSkinnedExtraConstants), skinnedExtraConstantBuffer_, skinnedExtraConstantBufferMapped_);
        return skinnedExtraConstantBuffer_.Get();
    }

    D3D12_GPU_DESCRIPTOR_HANDLE D3D12GraphicsBackend::GetSrvGpuHandleForTextureEXT(const ITextureBackend* tex) const
    {
        D3D12_GPU_DESCRIPTOR_HANDLE handle{};
        if (tex == nullptr) return handle;
        if (const auto* t = dynamic_cast<const D3D12TextureBackend*>(tex))
            return t->GetShaderResourceViewGpuHandleEXT();
        return handle;
    }

    ID3D12Resource* D3D12GraphicsBackend::GetOrCreateEnvMapPerDrawConstantBufferEXT()
    {
        if (!envMapPerDrawConstantBuffer_)
            CreateUploadConstantBuffer(sizeof(D3DEnvMapPerDrawConstants), envMapPerDrawConstantBuffer_, envMapPerDrawConstantBufferMapped_);
        return envMapPerDrawConstantBuffer_.Get();
    }

    ID3D12Resource* D3D12GraphicsBackend::GetOrCreateEnvMapConstantBufferEXT()
    {
        if (!envMapConstantBuffer_)
            CreateUploadConstantBuffer(sizeof(D3DEnvMapConstants), envMapConstantBuffer_, envMapConstantBufferMapped_);
        return envMapConstantBuffer_.Get();
    }

    D3D12_GPU_DESCRIPTOR_HANDLE D3D12GraphicsBackend::GetSrvGpuHandleForTextureCubeEXT(const ITextureCubeBackend* tex) const
    {
        D3D12_GPU_DESCRIPTOR_HANDLE handle{};
        if (tex == nullptr) return handle;
        if (const auto* t = dynamic_cast<const D3D12TextureCubeBackend*>(tex))
            return t->GetShaderResourceViewGpuHandleEXT();
        return handle;
    }

    void D3D12GraphicsBackend::Clear(float r, float g, float b, float a)
    {
        if (!boundColorResource_)
        {
            NotYetImplemented("Clear (no off-screen color target bound -- BindOffscreenColorTargetEXT; "
                              "the swap chain path is unusable under Wine per DX-100, and a full public "
                              "D3D12RenderTargetBackend is not yet implemented, DX-109's own honest scope note)");
        }

        ID3D12CommandAllocator* allocator = GetCommandAllocatorEXT(0);
        ID3D12GraphicsCommandList* cmdList = GetCommandListEXT();
        allocator->Reset();
        cmdList->Reset(allocator, nullptr);

        resourceStates_.TransitionTo(cmdList, boundColorResource_, D3D12_RESOURCE_STATE_RENDER_TARGET);
        const float clearColor[4] = {r, g, b, a};
        cmdList->ClearRenderTargetView(boundColorRtv_, clearColor, 0, nullptr);

        HRESULT hr = cmdList->Close();
        if (FAILED(hr))
            throw std::runtime_error("D3D12GraphicsBackend::Clear: command list Close failed, hr=" + FormatHr(hr));
        ExecuteCommandListAndWaitEXT(cmdList);
    }

    void D3D12GraphicsBackend::Present() { NotYetImplemented("Present"); }

    void D3D12GraphicsBackend::GetViewportSize(int& width, int& height)
    {
        width = virtualWidth_;
        height = virtualHeight_;
    }

    void D3D12GraphicsBackend::SetVirtualResolution(int width, int height)
    {
        virtualWidth_ = width;
        virtualHeight_ = height;
    }

    void D3D12GraphicsBackend::SetPresentationMode(int) { /* no-op until DX-106 onward */ }

    std::unique_ptr<ITextureBackend> D3D12GraphicsBackend::CreateTexture(const ImageData& data)
    {
        return std::make_unique<D3D12TextureBackend>(this, data);
    }

    std::unique_ptr<ITextureCubeBackend> D3D12GraphicsBackend::CreateTextureCube(
        int size, bool mipMap, int surfaceFormat)
    {
        return std::make_unique<D3D12TextureCubeBackend>(this, size, mipMap, surfaceFormat);
    }

    std::unique_ptr<ISpriteBatchBackend> D3D12GraphicsBackend::CreateSpriteBatch()
    {
        return std::make_unique<D3D12SpriteBatchBackend>(this);
    }

    void D3D12GraphicsBackend::ClearColorAndDepth(float, float, float, float, float) { NotYetImplemented("ClearColorAndDepth"); }
    void D3D12GraphicsBackend::ClearDepth(float) { NotYetImplemented("ClearDepth"); }
    void D3D12GraphicsBackend::ClearStencil(int) { NotYetImplemented("ClearStencil"); }
    void D3D12GraphicsBackend::ClearDepthAndStencil(float, int) { NotYetImplemented("ClearDepthAndStencil"); }
    void D3D12GraphicsBackend::ClearColorAndStencil(float, float, float, float, int) { NotYetImplemented("ClearColorAndStencil"); }
    void D3D12GraphicsBackend::ClearColorDepthAndStencil(float, float, float, float, float, int) { NotYetImplemented("ClearColorDepthAndStencil"); }

    void D3D12GraphicsBackend::SetDepthTestEnabled(bool) { NotYetImplemented("SetDepthTestEnabled"); }
    void D3D12GraphicsBackend::SetBlendEnabled(bool) { NotYetImplemented("SetBlendEnabled"); }
    void D3D12GraphicsBackend::SetDepthWriteEnabled(bool) { NotYetImplemented("SetDepthWriteEnabled"); }

    std::unique_ptr<IVertexBufferBackend> D3D12GraphicsBackend::CreateVertexBuffer(int vertex_capacity)
    {
        return std::make_unique<D3D12VertexBufferBackend>(this, vertex_capacity);
    }

    std::unique_ptr<IIndexBufferBackend> D3D12GraphicsBackend::CreateIndexBuffer16(int index_capacity)
    {
        return std::make_unique<D3D12IndexBufferBackend>(this, index_capacity, /*thirtyTwoBit=*/false);
    }

    std::unique_ptr<IIndexBufferBackend> D3D12GraphicsBackend::CreateIndexBuffer32(int index_capacity)
    {
        return std::make_unique<D3D12IndexBufferBackend>(this, index_capacity, /*thirtyTwoBit=*/true);
    }

    void D3D12GraphicsBackend::DrawColoredPrimitives(
        const IVertexBufferBackend& vb, const Matrix& world, const Matrix& view, const Matrix& projection,
        PrimitiveType primitive, int primitiveCount)
    {
        // DX-111: colored3d (stride 16, unlit vertex-color) is the only real D3D12 draw pipeline so
        // far -- mirrors D3D11's own DX-61 scope exactly (same shader variant, same DXBC bytecode,
        // design decision 5). Other strides/variants are a follow-up (D3D11's own Phase DX8 took
        // several further tasks to cover them; nothing here assumes that work carries over for free).
        if (!boundColorResource_)
        {
            NotYetImplemented("DrawColoredPrimitives (no off-screen color target bound -- "
                              "BindOffscreenColorTargetEXT; see Clear()'s own note)");
        }

        const auto& d3dVb = static_cast<const D3D12VertexBufferBackend&>(vb);
        const std::size_t stride = d3dVb.GetStrideEXT() > 0 ? d3dVb.GetStrideEXT() : 16;
        if (stride != 16)
        {
            throw std::runtime_error(
                "D3D12GraphicsBackend::DrawColoredPrimitives: only stride-16 (VertexPositionColor) "
                "is implemented so far (plan_dx.md DX-111); other strides are a follow-up");
        }

        auto rootSig = rootSigCache_.GetOrCreate(device_.Get(), /*numCbvs=*/2, /*numSrvs=*/0, /*numSamplers=*/0);
        if (!rootSig)
            throw std::runtime_error("DrawColoredPrimitives: failed to create colored3d root signature");

        D3D12PipelineStateDesc psoDesc;
        psoDesc.variant = D3DShaderVariant::Colored3d;
        psoDesc.strideInBytes = 16;
        // No depth-stencil view is bound in this off-screen-only draw path (dsvFormat=UNKNOWN,
        // OMSetRenderTargets below passes a null DSV handle) -- the PSO's DepthEnable must agree,
        // or the draw silently produces no visible output (found via this task's own real Wine+
        // vkd3d-proton run: the very first attempt at Check M left the default depthEnable=true and
        // the render target stayed at the Clear() color with no triangle visible at all).
        psoDesc.depthEnable = false;
        // No D3D12 rasterizer-state-cache equivalent to D3D11's Phase DX7 exists yet for this
        // backend (a real, scoped follow-up task, not attempted here) -- CullMode::None (0) is a
        // deliberate, honest hardcoded simplification for this narrow colored3d bootstrap path,
        // matching D3D11's own equivalent test-time convention of applying an explicit known-safe
        // rasterizer baseline before its analogous first-triangle proof (d3d11_smoke_test.cpp's own
        // Check P). Real finding from this task's own first attempt: leaving the PSO's default
        // cullMode (XNA's CullCounterClockwiseFace, i.e. real back-face culling ON) silently
        // produced an empty render target -- the test triangle's real winding, after D3D's NDC->
        // screen-space Y-flip, was being back-face-culled with no error reported (no debug layer is
        // available on this Wine+vkd3d-proton dev loop, DX-102's own already-documented gap) --
        // exactly the kind of silent failure this plan's "verify, don't assume" discipline exists
        // to catch, confirmed here empirically rather than guessed.
        psoDesc.cullMode = 0; // XNA CullMode::None
        auto pso = psoCache_.GetOrCreate(device_.Get(), rootSig.Get(), psoDesc, boundColorFormat_, DXGI_FORMAT_UNKNOWN);
        if (!pso)
            throw std::runtime_error("DrawColoredPrimitives: failed to create colored3d PSO");

        // Same "raw vertex color, no GpuDrawParams" legacy convention D3D11's own DrawColoredPrimitives
        // uses (diffuseColor=white, vertexColorEnabled=true, fog disabled) -- see D3DConstantBuffers.hpp.
        D3DPerDrawConstants perDraw{};
        const Matrix wvp = world * view * projection;
        wvp.ToColumnMajor(perDraw.Mvp);
        perDraw.DiffuseColor[0] = perDraw.DiffuseColor[1] = perDraw.DiffuseColor[2] = perDraw.DiffuseColor[3] = 1.0f;
        perDraw.VertexColorEnabled = 1.0f;

        D3DFogConstants fog{};
        fog.FogStartEnd[1] = 1.0f;

        ID3D12Resource* perDrawCB = GetOrCreatePerDrawConstantBufferEXT();
        ID3D12Resource* fogCB = GetOrCreateFogConstantBufferEXT();
        std::memcpy(perDrawConstantBufferMapped_, &perDraw, sizeof(perDraw));
        std::memcpy(fogConstantBufferMapped_, &fog, sizeof(fog));

        ID3D12CommandAllocator* allocator = GetCommandAllocatorEXT(0);
        ID3D12GraphicsCommandList* cmdList = GetCommandListEXT();
        allocator->Reset();
        cmdList->Reset(allocator, pso.Get());

        resourceStates_.TransitionTo(cmdList, boundColorResource_, D3D12_RESOURCE_STATE_RENDER_TARGET);
        cmdList->OMSetRenderTargets(1, &boundColorRtv_, FALSE, nullptr);

        D3D12_VIEWPORT viewport{0.0f, 0.0f, static_cast<float>(boundColorWidth_), static_cast<float>(boundColorHeight_), 0.0f, 1.0f};
        D3D12_RECT scissor{0, 0, boundColorWidth_, boundColorHeight_};
        cmdList->RSSetViewports(1, &viewport);
        cmdList->RSSetScissorRects(1, &scissor);

        cmdList->SetGraphicsRootSignature(rootSig.Get());
        cmdList->SetPipelineState(pso.Get());
        cmdList->IASetPrimitiveTopology(ToD3D12Topology(primitive));

        D3D12_VERTEX_BUFFER_VIEW vbView = d3dVb.GetViewEXT();
        cmdList->IASetVertexBuffers(0, 1, &vbView);

        cmdList->SetGraphicsRootConstantBufferView(0, perDrawCB->GetGPUVirtualAddress());
        cmdList->SetGraphicsRootConstantBufferView(1, fogCB->GetGPUVirtualAddress());

        const UINT vertexCount = static_cast<UINT>(VertexCountForPrimitives(primitive, primitiveCount));
        cmdList->DrawInstanced(vertexCount, 1, 0, 0);

        HRESULT hr = cmdList->Close();
        if (FAILED(hr))
            throw std::runtime_error("DrawColoredPrimitives: command list Close failed, hr=" + FormatHr(hr));
        ExecuteCommandListAndWaitEXT(cmdList);
    }

    void D3D12GraphicsBackend::DrawIndexedColoredPrimitives(
        const IVertexBufferBackend& vb, const IIndexBufferBackend& ib,
        const Matrix& world, const Matrix& view, const Matrix& projection,
        PrimitiveType primitive, int primitiveCount)
    {
        // DX-111: indexed counterpart of DrawColoredPrimitives above -- same colored3d-only scope.
        if (!boundColorResource_)
        {
            NotYetImplemented("DrawIndexedColoredPrimitives (no off-screen color target bound -- "
                              "BindOffscreenColorTargetEXT; see Clear()'s own note)");
        }

        const auto& d3dVb = static_cast<const D3D12VertexBufferBackend&>(vb);
        const auto& d3dIb = static_cast<const D3D12IndexBufferBackend&>(ib);
        const std::size_t stride = d3dVb.GetStrideEXT() > 0 ? d3dVb.GetStrideEXT() : 16;
        if (stride != 16)
        {
            throw std::runtime_error(
                "D3D12GraphicsBackend::DrawIndexedColoredPrimitives: only stride-16 (VertexPositionColor) "
                "is implemented so far (plan_dx.md DX-111); other strides are a follow-up");
        }

        auto rootSig = rootSigCache_.GetOrCreate(device_.Get(), /*numCbvs=*/2, /*numSrvs=*/0, /*numSamplers=*/0);
        if (!rootSig)
            throw std::runtime_error("DrawIndexedColoredPrimitives: failed to create colored3d root signature");

        D3D12PipelineStateDesc psoDesc;
        psoDesc.variant = D3DShaderVariant::Colored3d;
        psoDesc.strideInBytes = 16;
        // No depth-stencil view is bound in this off-screen-only draw path (dsvFormat=UNKNOWN,
        // OMSetRenderTargets below passes a null DSV handle) -- the PSO's DepthEnable must agree,
        // or the draw silently produces no visible output (found via this task's own real Wine+
        // vkd3d-proton run: the very first attempt at Check M left the default depthEnable=true and
        // the render target stayed at the Clear() color with no triangle visible at all).
        psoDesc.depthEnable = false;
        // No D3D12 rasterizer-state-cache equivalent to D3D11's Phase DX7 exists yet for this
        // backend (a real, scoped follow-up task, not attempted here) -- CullMode::None (0) is a
        // deliberate, honest hardcoded simplification for this narrow colored3d bootstrap path,
        // matching D3D11's own equivalent test-time convention of applying an explicit known-safe
        // rasterizer baseline before its analogous first-triangle proof (d3d11_smoke_test.cpp's own
        // Check P). Real finding from this task's own first attempt: leaving the PSO's default
        // cullMode (XNA's CullCounterClockwiseFace, i.e. real back-face culling ON) silently
        // produced an empty render target -- the test triangle's real winding, after D3D's NDC->
        // screen-space Y-flip, was being back-face-culled with no error reported (no debug layer is
        // available on this Wine+vkd3d-proton dev loop, DX-102's own already-documented gap) --
        // exactly the kind of silent failure this plan's "verify, don't assume" discipline exists
        // to catch, confirmed here empirically rather than guessed.
        psoDesc.cullMode = 0; // XNA CullMode::None
        auto pso = psoCache_.GetOrCreate(device_.Get(), rootSig.Get(), psoDesc, boundColorFormat_, DXGI_FORMAT_UNKNOWN);
        if (!pso)
            throw std::runtime_error("DrawIndexedColoredPrimitives: failed to create colored3d PSO");

        D3DPerDrawConstants perDraw{};
        const Matrix wvp = world * view * projection;
        wvp.ToColumnMajor(perDraw.Mvp);
        perDraw.DiffuseColor[0] = perDraw.DiffuseColor[1] = perDraw.DiffuseColor[2] = perDraw.DiffuseColor[3] = 1.0f;
        perDraw.VertexColorEnabled = 1.0f;

        D3DFogConstants fog{};
        fog.FogStartEnd[1] = 1.0f;

        ID3D12Resource* perDrawCB = GetOrCreatePerDrawConstantBufferEXT();
        ID3D12Resource* fogCB = GetOrCreateFogConstantBufferEXT();
        std::memcpy(perDrawConstantBufferMapped_, &perDraw, sizeof(perDraw));
        std::memcpy(fogConstantBufferMapped_, &fog, sizeof(fog));

        ID3D12CommandAllocator* allocator = GetCommandAllocatorEXT(0);
        ID3D12GraphicsCommandList* cmdList = GetCommandListEXT();
        allocator->Reset();
        cmdList->Reset(allocator, pso.Get());

        resourceStates_.TransitionTo(cmdList, boundColorResource_, D3D12_RESOURCE_STATE_RENDER_TARGET);
        cmdList->OMSetRenderTargets(1, &boundColorRtv_, FALSE, nullptr);

        D3D12_VIEWPORT viewport{0.0f, 0.0f, static_cast<float>(boundColorWidth_), static_cast<float>(boundColorHeight_), 0.0f, 1.0f};
        D3D12_RECT scissor{0, 0, boundColorWidth_, boundColorHeight_};
        cmdList->RSSetViewports(1, &viewport);
        cmdList->RSSetScissorRects(1, &scissor);

        cmdList->SetGraphicsRootSignature(rootSig.Get());
        cmdList->SetPipelineState(pso.Get());
        cmdList->IASetPrimitiveTopology(ToD3D12Topology(primitive));

        D3D12_VERTEX_BUFFER_VIEW vbView = d3dVb.GetViewEXT();
        cmdList->IASetVertexBuffers(0, 1, &vbView);
        D3D12_INDEX_BUFFER_VIEW ibView = d3dIb.GetViewEXT();
        cmdList->IASetIndexBuffer(&ibView);

        cmdList->SetGraphicsRootConstantBufferView(0, perDrawCB->GetGPUVirtualAddress());
        cmdList->SetGraphicsRootConstantBufferView(1, fogCB->GetGPUVirtualAddress());

        const UINT indexCount = static_cast<UINT>(VertexCountForPrimitives(primitive, primitiveCount));
        cmdList->DrawIndexedInstanced(indexCount, 1, 0, 0, 0);

        HRESULT hr = cmdList->Close();
        if (FAILED(hr))
            throw std::runtime_error("DrawIndexedColoredPrimitives: command list Close failed, hr=" + FormatHr(hr));
        ExecuteCommandListAndWaitEXT(cmdList);
    }

    // DX-111 (continued): extends the colored3d-only pipeline above to textured3d/colored_textured3d/
    // lit_textured3d/alpha_test3d -- real GpuDrawParams-driven effect selection, mirroring
    // D3D11GraphicsBackend::DrawPrimitivesExImpl's own priority-chain shape (alpha-test > lit-textured
    // (stride 32) > colored/textured/colored_textured bundle by stride). DualTextureEffect/
    // EnvironmentMapEffect/SkinnedEffect are explicitly NOT handled here -- a separate follow-up task's
    // job (see the named throw below) -- so this stays honestly scoped rather than silently drawing
    // the wrong shader for those effects.
    void D3D12GraphicsBackend::DrawPrimitivesExImpl(
        const IVertexBufferBackend& vb, const IIndexBufferBackend* ib,
        const Matrix& world, const Matrix& view, const Matrix& projection,
        PrimitiveType primitive, int primitiveCount, const GpuDrawParams& params)
    {
        if (!boundColorResource_)
        {
            NotYetImplemented("DrawPrimitivesEx (no off-screen color target bound -- "
                              "BindOffscreenColorTargetEXT; see Clear()'s own note)");
        }

        const auto& d3dVb = static_cast<const D3D12VertexBufferBackend&>(vb);
        const std::size_t stride = d3dVb.GetStrideEXT() > 0 ? d3dVb.GetStrideEXT() : 16;

        const bool needsAlphaTest = (params.alphaTest[3] < 0.0f || params.alphaTest[2] < 0.0f);
        // DX-111 (closing env_map3d): dual_texture3d, skinned3d, and now env_map3d are all real.
        const bool needsDualTex = params.dualTexture && !needsAlphaTest;
        const bool needsEnvMap  = params.envMapping  && !needsAlphaTest && !needsDualTex;
        const bool needsSkinned = params.skinned     && !needsAlphaTest && !needsDualTex && !needsEnvMap;
        // stride==32 always uses lit_textured3d (BasicEffect's VertexPositionNormalTexture path, lit
        // or not -- the shader itself branches on LightingEnabled), unless a higher-priority effect
        // claims the draw first -- same priority D3D11's own DrawPrimitivesExImpl uses.
        const bool needsLitTextured = (stride == 32) && !needsAlphaTest && !needsDualTex
                                     && !needsEnvMap && !needsSkinned;

        // env_map3d.vert.hlsl's VSInput is Position+Normal+UV (32 bytes), same as lit_textured3d.
        if (needsEnvMap && stride != 32)
            throw std::runtime_error(
                "D3D12GraphicsBackend::DrawPrimitivesEx: EnvironmentMapEffect (env_map3d) requires "
                "stride 32 (VertexPositionNormalTexture)");
        // dual_texture3d.vert.hlsl's VSInput is Position+UV only (20 bytes), same as D3D11's own
        // DX-65 finding -- dual_texture_colored3d was never ported (DX-13-hlsl's own row notes).
        if (needsDualTex && stride != 20)
            throw std::runtime_error(
                "D3D12GraphicsBackend::DrawPrimitivesEx: DualTextureEffect (dual_texture3d) only "
                "supports stride 20 (VertexPositionTexture); dual_texture_colored3d was not ported "
                "(plan_dx.md DX-13-hlsl)");
        // skinned3d.vert.hlsl's VSInput is Position+Normal+UV+BoneWeights+BoneIndices (52 bytes).
        if (needsSkinned && stride != 52)
            throw std::runtime_error(
                "D3D12GraphicsBackend::DrawPrimitivesEx: SkinnedEffect (skinned3d) requires stride "
                "52 (VertexPositionNormalTextureSkinned)");

        D3DShaderVariant variant;
        bool hasTexture;
        int numCbvs;
        int numSrvs = 0;
        if (needsAlphaTest)
        {
            variant = D3DShaderVariant::AlphaTest3d;
            hasTexture = true;
            numCbvs = 1; // alpha_test3d's own single combined PerDraw (b0) -- no separate FogParams cbuffer.
            numSrvs = 1;
        }
        else if (needsDualTex)
        {
            variant = D3DShaderVariant::DualTexture3d;
            hasTexture = true;
            // DX-13-hlsl's own note: dual_texture3d's FogParams cbuffer lives at register(b2), not
            // (b1) -- t0/s0 and t1/s1 already occupy the "next free slot" a single-texture variant
            // would use for fog. Root signature still needs 3 contiguous CBV slots (b0/b1/b2); b1
            // goes unused by this shader but is still bound to a valid (dummy) address below so no
            // root descriptor is ever left unset.
            numCbvs = 3;
            numSrvs = 2;
        }
        else if (needsEnvMap)
        {
            variant = D3DShaderVariant::EnvMap3d;
            hasTexture = true;
            // env_map3d's own PerDraw (b0, D3DEnvMapPerDrawConstants) + EnvMapParams (b2,
            // D3DEnvMapConstants) -- b1 unused by this shader (same "3 contiguous CBV slots, b1
            // bound to a valid-but-unread dummy address" shape dual_texture3d's own branch already
            // established). t0 (base Texture2D) + t1 (TextureCube) -- same (3,2,2) root-signature
            // shape dual_texture3d already created, genuinely shared/cached, not a new object.
            numCbvs = 3;
            numSrvs = 2;
        }
        else if (needsSkinned)
        {
            variant = D3DShaderVariant::Skinned3d;
            hasTexture = true;
            numCbvs = 3; // PerDraw (b0) + BoneBlock (b1) + skinned3d's own FogParams-equivalent (b2).
            numSrvs = 1;
        }
        else if (needsLitTextured)
        {
            variant = D3DShaderVariant::LitTextured3d;
            hasTexture = true;
            numCbvs = 2; // PerDraw (b0) + LitLightParams (b1).
            numSrvs = 1;
        }
        else
        {
            hasTexture = (stride != 16);
            numCbvs = 2; // PerDraw (b0) + FogParams (b1) -- including the stride-16 (colored3d) case.
            numSrvs = hasTexture ? 1 : 0;
            switch (stride)
            {
            case 16: variant = D3DShaderVariant::Colored3d; break;
            case 20: variant = D3DShaderVariant::Textured3d; break;
            case 24: variant = D3DShaderVariant::ColoredTextured3d; break;
            default:
                throw std::runtime_error(
                    "D3D12GraphicsBackend::DrawPrimitivesEx: unsupported vertex stride " +
                    std::to_string(stride) + " for the colored/textured bundle (plan_dx.md DX-111)");
            }
        }

        const int numSamplers = numSrvs; // one static sampler per texture slot, s0.. matching t0..

        auto rootSig = rootSigCache_.GetOrCreate(device_.Get(), numCbvs, numSrvs, numSamplers);
        if (!rootSig)
            throw std::runtime_error("DrawPrimitivesEx: failed to create root signature for the selected variant");

        D3D12PipelineStateDesc psoDesc;
        psoDesc.variant = variant;
        psoDesc.strideInBytes = stride;
        // Same honest hardcoded simplification DrawColoredPrimitives' own doc comment already
        // explains at length -- no D3D12 rasterizer/depth-stencil-state-cache equivalent to D3D11's
        // Phase DX7 exists yet for this backend, and no depth-stencil view is bound in this
        // off-screen-only draw path.
        psoDesc.depthEnable = false;
        psoDesc.cullMode = 0; // XNA CullMode::None
        auto pso = psoCache_.GetOrCreate(device_.Get(), rootSig.Get(), psoDesc, boundColorFormat_, DXGI_FORMAT_UNKNOWN);
        if (!pso)
            throw std::runtime_error("DrawPrimitivesEx: failed to create PSO for the selected variant");

        const Matrix wvp = world * view * projection;

        // cbAddresses[i] is bound to root CBV parameter i (b0, b1, ...) -- see each branch below for
        // which struct/register each variant actually needs, field-for-field matching the real HLSL
        // cbuffer declarations (D3DConstantBuffers.hpp).
        D3D12_GPU_VIRTUAL_ADDRESS cbAddresses[3] = {0, 0, 0};
        // params.texture0/texture1 for the (up to 2) SRVs this draw binds -- dual_texture3d uses the
        // 2nd slot for a 2nd Texture2D; env_map3d uses it for a TextureCube instead (srvCubeTexture
        // below), which is why it isn't part of this Texture2D-only array.
        const ITextureBackend* srvTextures[2] = {params.texture0, nullptr};
        const ITextureCubeBackend* srvCubeTexture = nullptr; // only set by the needsEnvMap branch

        if (needsAlphaTest)
        {
            D3DAlphaTestConstants c{};
            wvp.ToColumnMajor(c.Mvp);
            c.DiffuseColor[0] = params.diffuseColor[0];
            c.DiffuseColor[1] = params.diffuseColor[1];
            c.DiffuseColor[2] = params.diffuseColor[2];
            c.DiffuseColor[3] = params.diffuseColor[3];
            c.AlphaRef           = params.alphaTest[0];
            c.AlphaTol           = params.alphaTest[1];
            c.AlphaPassW         = params.alphaTest[2];
            c.AlphaFailW         = params.alphaTest[3];
            c.VertexColorEnabled = params.vertexColorEnabled ? 1.0f : 0.0f;
            c.FogEnabled         = params.fogEnabled ? 1.0f : 0.0f;
            c.FogStart           = params.fogStart;
            c.FogEnd             = params.fogEnd;
            c.FogColor[0] = params.fogColor[0];
            c.FogColor[1] = params.fogColor[1];
            c.FogColor[2] = params.fogColor[2];

            ID3D12Resource* cb = GetOrCreateAlphaTestConstantBufferEXT();
            std::memcpy(alphaTestConstantBufferMapped_, &c, sizeof(c));
            cbAddresses[0] = cb->GetGPUVirtualAddress();
        }
        else if (needsDualTex)
        {
            // DX-13-hlsl's own note: dual_texture3d's PerDraw (b0) is the same shape as
            // D3DPerDrawConstants; its FogParams cbuffer is at register(b2) instead of (b1) -- t0/s0
            // and t1/s1 are already the two texture samplers, so fog moved to the next free slot.
            D3DPerDrawConstants perDraw{};
            wvp.ToColumnMajor(perDraw.Mvp);
            perDraw.DiffuseColor[0] = params.diffuseColor[0];
            perDraw.DiffuseColor[1] = params.diffuseColor[1];
            perDraw.DiffuseColor[2] = params.diffuseColor[2];
            perDraw.DiffuseColor[3] = params.diffuseColor[3];
            perDraw.TextureEnabled = params.textureEnabled ? 1.0f : 0.0f;
            perDraw.VertexColorEnabled = params.vertexColorEnabled ? 1.0f : 0.0f;

            D3DFogConstants fog{};
            fog.FogColorEnabled[0] = params.fogColor[0];
            fog.FogColorEnabled[1] = params.fogColor[1];
            fog.FogColorEnabled[2] = params.fogColor[2];
            fog.FogColorEnabled[3] = params.fogEnabled ? 1.0f : 0.0f;
            fog.FogStartEnd[0] = params.fogStart;
            fog.FogStartEnd[1] = params.fogEnd;

            ID3D12Resource* perDrawCB = GetOrCreatePerDrawConstantBufferEXT();
            ID3D12Resource* fogCB     = GetOrCreateFogConstantBufferEXT();
            std::memcpy(perDrawConstantBufferMapped_, &perDraw, sizeof(perDraw));
            std::memcpy(fogConstantBufferMapped_, &fog, sizeof(fog));
            cbAddresses[0] = perDrawCB->GetGPUVirtualAddress();
            // b1 goes unused by dual_texture3d's own HLSL, but the root signature still declares 3
            // contiguous CBV slots for this variant (see numCbvs=3 above) -- bind a valid (harmless,
            // unread) address rather than leaving a root descriptor unset.
            cbAddresses[1] = perDrawCB->GetGPUVirtualAddress();
            cbAddresses[2] = fogCB->GetGPUVirtualAddress();

            srvTextures[0] = params.texture0;
            srvTextures[1] = params.texture1;
        }
        else if (needsEnvMap)
        {
            // Mirrors D3D11GraphicsBackend::DrawPrimitivesExImpl's own needsEnvMap branch exactly --
            // env_map3d's own PerDraw (b0) is Mvp+World only (D3DEnvMapPerDrawConstants) -- a
            // genuinely different shape from D3DPerDrawConstants; material/lighting/fog/Fresnel live
            // in EnvMapParams (b2, D3DEnvMapConstants) instead, field-for-field matching
            // env_map3d.vert.hlsl/.frag.hlsl's real cbuffer declaration.
            D3DEnvMapPerDrawConstants perDraw{};
            wvp.ToColumnMajor(perDraw.Mvp);
            world.ToColumnMajor(perDraw.World);

            D3DEnvMapConstants c{};
            c.EyePosition[0] = params.eyePositionWorld[0];
            c.EyePosition[1] = params.eyePositionWorld[1];
            c.EyePosition[2] = params.eyePositionWorld[2];
            c.DiffuseColor[0] = params.diffuseColor[0];
            c.DiffuseColor[1] = params.diffuseColor[1];
            c.DiffuseColor[2] = params.diffuseColor[2];
            c.DiffuseColor[3] = params.diffuseColor[3];
            c.EmissiveAmount[0] = params.emissiveColor[0];
            c.EmissiveAmount[1] = params.emissiveColor[1];
            c.EmissiveAmount[2] = params.emissiveColor[2];
            c.EmissiveAmount[3] = params.envMapAmount;
            c.Light0Dir[0] = params.light0Dir[0];
            c.Light0Dir[1] = params.light0Dir[1];
            c.Light0Dir[2] = params.light0Dir[2];
            c.Light0DiffuseFresnel[0] = params.light0Diffuse[0];
            c.Light0DiffuseFresnel[1] = params.light0Diffuse[1];
            c.Light0DiffuseFresnel[2] = params.light0Diffuse[2];
            c.Light0DiffuseFresnel[3] = params.fresnelEnabled ? 1.0f : 0.0f;
            c.EnvMapSpecularFresnel[0] = params.envMapSpecular[0];
            c.EnvMapSpecularFresnel[1] = params.envMapSpecular[1];
            c.EnvMapSpecularFresnel[2] = params.envMapSpecular[2];
            c.EnvMapSpecularFresnel[3] = params.fresnelFactor;
            c.FogColorEnabled[0] = params.fogColor[0];
            c.FogColorEnabled[1] = params.fogColor[1];
            c.FogColorEnabled[2] = params.fogColor[2];
            c.FogColorEnabled[3] = params.fogEnabled ? 1.0f : 0.0f;
            c.FogStartEnd[0] = params.fogStart;
            c.FogStartEnd[1] = params.fogEnd;
            c.Light1Dir[0] = params.light1Dir[0];
            c.Light1Dir[1] = params.light1Dir[1];
            c.Light1Dir[2] = params.light1Dir[2];
            c.Light1Diffuse[0] = params.light1Diffuse[0];
            c.Light1Diffuse[1] = params.light1Diffuse[1];
            c.Light1Diffuse[2] = params.light1Diffuse[2];
            c.Light2Dir[0] = params.light2Dir[0];
            c.Light2Dir[1] = params.light2Dir[1];
            c.Light2Dir[2] = params.light2Dir[2];
            c.Light2Diffuse[0] = params.light2Diffuse[0];
            c.Light2Diffuse[1] = params.light2Diffuse[1];
            c.Light2Diffuse[2] = params.light2Diffuse[2];

            ID3D12Resource* perDrawCB = GetOrCreateEnvMapPerDrawConstantBufferEXT();
            ID3D12Resource* envCB     = GetOrCreateEnvMapConstantBufferEXT();
            std::memcpy(envMapPerDrawConstantBufferMapped_, &perDraw, sizeof(perDraw));
            std::memcpy(envMapConstantBufferMapped_, &c, sizeof(c));
            cbAddresses[0] = perDrawCB->GetGPUVirtualAddress();
            // b1 goes unused by env_map3d's own HLSL -- same dummy-but-valid-address convention
            // needsDualTex's own branch above already established.
            cbAddresses[1] = perDrawCB->GetGPUVirtualAddress();
            cbAddresses[2] = envCB->GetGPUVirtualAddress();

            srvTextures[0] = params.texture0;
            srvCubeTexture = params.envMap;
        }
        else if (needsSkinned)
        {
            // Mirrors D3D11GraphicsBackend::DrawPrimitivesExImpl's own needsSkinned branch exactly --
            // PerDraw (b0) same shape as D3DPerDrawConstants; BoneBlock (b1, D3DBoneConstants, DX-60a)
            // holds the 72-matrix array; skinned3d's own FogParams-equivalent (b2,
            // D3DSkinnedExtraConstants) carries fog + DirectionalLight1/2 + World + EyePosition +
            // specular (PerDraw's 128 bytes have no spare room for those).
            D3DPerDrawConstants perDraw{};
            wvp.ToColumnMajor(perDraw.Mvp);
            perDraw.DiffuseColor[0] = params.diffuseColor[0];
            perDraw.DiffuseColor[1] = params.diffuseColor[1];
            perDraw.DiffuseColor[2] = params.diffuseColor[2];
            perDraw.DiffuseColor[3] = params.diffuseColor[3];
            perDraw.AmbientColor[0] = params.ambientColor[0];
            perDraw.AmbientColor[1] = params.ambientColor[1];
            perDraw.AmbientColor[2] = params.ambientColor[2];
            perDraw.LightingEnabled = params.lightingEnabled ? 1.0f : 0.0f;
            perDraw.Light0Dir[0] = params.light0Dir[0];
            perDraw.Light0Dir[1] = params.light0Dir[1];
            perDraw.Light0Dir[2] = params.light0Dir[2];
            perDraw.TextureEnabled = params.textureEnabled ? 1.0f : 0.0f;
            perDraw.Light0Diffuse[0] = params.light0Diffuse[0];
            perDraw.Light0Diffuse[1] = params.light0Diffuse[1];
            perDraw.Light0Diffuse[2] = params.light0Diffuse[2];
            perDraw.VertexColorEnabled = params.vertexColorEnabled ? 1.0f : 0.0f;

            // params.boneTransforms is filled via Matrix::ToColumnMajor() at the XNA-API call site
            // (SkinnedEffect::SetBoneTransforms) -- the same function DX-60/DX-61's own report
            // established emits raw row-major M11..M44 bytes, exactly what BoneBlock's
            // `row_major float4x4 Bones[72]` wants unchanged. A straight memcpy is correct, no
            // per-matrix transpose needed (D3D11's own DX-67 fork already confirmed this).
            D3DBoneConstants bones{};
            const int boneCount = std::min(params.boneCount, 72);
            if (boneCount > 0)
                std::memcpy(bones.Bones, params.boneTransforms,
                           static_cast<std::size_t>(boneCount) * 16u * sizeof(float));

            D3DSkinnedExtraConstants extra{};
            extra.FogColorEnabled[0] = params.fogColor[0];
            extra.FogColorEnabled[1] = params.fogColor[1];
            extra.FogColorEnabled[2] = params.fogColor[2];
            extra.FogColorEnabled[3] = params.fogEnabled ? 1.0f : 0.0f;
            extra.FogStartEnd[0] = params.fogStart;
            extra.FogStartEnd[1] = params.fogEnd;
            extra.Light1Dir[0] = params.light1Dir[0];
            extra.Light1Dir[1] = params.light1Dir[1];
            extra.Light1Dir[2] = params.light1Dir[2];
            extra.Light1Diffuse[0] = params.light1Diffuse[0];
            extra.Light1Diffuse[1] = params.light1Diffuse[1];
            extra.Light1Diffuse[2] = params.light1Diffuse[2];
            extra.Light2Dir[0] = params.light2Dir[0];
            extra.Light2Dir[1] = params.light2Dir[1];
            extra.Light2Dir[2] = params.light2Dir[2];
            extra.Light2Diffuse[0] = params.light2Diffuse[0];
            extra.Light2Diffuse[1] = params.light2Diffuse[1];
            extra.Light2Diffuse[2] = params.light2Diffuse[2];
            world.ToColumnMajor(extra.World);
            extra.EyePosition[0] = params.eyePositionWorld[0];
            extra.EyePosition[1] = params.eyePositionWorld[1];
            extra.EyePosition[2] = params.eyePositionWorld[2];
            extra.EyePosition[3] = static_cast<float>(params.weightsPerVertex);
            extra.SpecularColorPower[0] = params.specularColor[0];
            extra.SpecularColorPower[1] = params.specularColor[1];
            extra.SpecularColorPower[2] = params.specularColor[2];
            extra.SpecularColorPower[3] = params.specularPower;
            extra.Light0Specular[0] = params.light0Specular[0];
            extra.Light0Specular[1] = params.light0Specular[1];
            extra.Light0Specular[2] = params.light0Specular[2];
            extra.Light1Specular[0] = params.light1Specular[0];
            extra.Light1Specular[1] = params.light1Specular[1];
            extra.Light1Specular[2] = params.light1Specular[2];
            extra.Light2Specular[0] = params.light2Specular[0];
            extra.Light2Specular[1] = params.light2Specular[1];
            extra.Light2Specular[2] = params.light2Specular[2];

            ID3D12Resource* perDrawCB = GetOrCreatePerDrawConstantBufferEXT();
            ID3D12Resource* boneCB    = GetOrCreateBoneConstantBufferEXT();
            ID3D12Resource* extraCB   = GetOrCreateSkinnedExtraConstantBufferEXT();
            std::memcpy(perDrawConstantBufferMapped_, &perDraw, sizeof(perDraw));
            std::memcpy(boneConstantBufferMapped_, &bones, sizeof(bones));
            std::memcpy(skinnedExtraConstantBufferMapped_, &extra, sizeof(extra));
            cbAddresses[0] = perDrawCB->GetGPUVirtualAddress();
            cbAddresses[1] = boneCB->GetGPUVirtualAddress();
            cbAddresses[2] = extraCB->GetGPUVirtualAddress();
        }
        else if (needsLitTextured)
        {
            D3DPerDrawConstants perDraw{};
            wvp.ToColumnMajor(perDraw.Mvp);
            perDraw.DiffuseColor[0] = params.diffuseColor[0];
            perDraw.DiffuseColor[1] = params.diffuseColor[1];
            perDraw.DiffuseColor[2] = params.diffuseColor[2];
            perDraw.DiffuseColor[3] = params.diffuseColor[3];
            perDraw.AmbientColor[0] = params.ambientColor[0];
            perDraw.AmbientColor[1] = params.ambientColor[1];
            perDraw.AmbientColor[2] = params.ambientColor[2];
            perDraw.LightingEnabled = params.lightingEnabled ? 1.0f : 0.0f;
            perDraw.Light0Dir[0] = params.light0Dir[0];
            perDraw.Light0Dir[1] = params.light0Dir[1];
            perDraw.Light0Dir[2] = params.light0Dir[2];
            perDraw.TextureEnabled = params.textureEnabled ? 1.0f : 0.0f;
            perDraw.Light0Diffuse[0] = params.light0Diffuse[0];
            perDraw.Light0Diffuse[1] = params.light0Diffuse[1];
            perDraw.Light0Diffuse[2] = params.light0Diffuse[2];
            perDraw.VertexColorEnabled = params.vertexColorEnabled ? 1.0f : 0.0f;

            D3DLightingConstants lighting{};
            lighting.Light1Dir[0] = params.light1Dir[0];
            lighting.Light1Dir[1] = params.light1Dir[1];
            lighting.Light1Dir[2] = params.light1Dir[2];
            lighting.Light1Diffuse[0] = params.light1Diffuse[0];
            lighting.Light1Diffuse[1] = params.light1Diffuse[1];
            lighting.Light1Diffuse[2] = params.light1Diffuse[2];
            lighting.Light2Dir[0] = params.light2Dir[0];
            lighting.Light2Dir[1] = params.light2Dir[1];
            lighting.Light2Dir[2] = params.light2Dir[2];
            lighting.Light2Diffuse[0] = params.light2Diffuse[0];
            lighting.Light2Diffuse[1] = params.light2Diffuse[1];
            lighting.Light2Diffuse[2] = params.light2Diffuse[2];
            lighting.EmissiveColor[0] = params.emissiveColor[0];
            lighting.EmissiveColor[1] = params.emissiveColor[1];
            lighting.EmissiveColor[2] = params.emissiveColor[2];
            world.ToColumnMajor(lighting.World);
            lighting.EyePosition[0] = params.eyePositionWorld[0];
            lighting.EyePosition[1] = params.eyePositionWorld[1];
            lighting.EyePosition[2] = params.eyePositionWorld[2];
            lighting.Light0Specular[0] = params.light0Specular[0];
            lighting.Light0Specular[1] = params.light0Specular[1];
            lighting.Light0Specular[2] = params.light0Specular[2];
            lighting.Light1Specular[0] = params.light1Specular[0];
            lighting.Light1Specular[1] = params.light1Specular[1];
            lighting.Light1Specular[2] = params.light1Specular[2];
            lighting.Light2Specular[0] = params.light2Specular[0];
            lighting.Light2Specular[1] = params.light2Specular[1];
            lighting.Light2Specular[2] = params.light2Specular[2];
            lighting.SpecularColorPower[0] = params.specularColor[0];
            lighting.SpecularColorPower[1] = params.specularColor[1];
            lighting.SpecularColorPower[2] = params.specularColor[2];
            lighting.SpecularColorPower[3] = params.specularPower;
            lighting.FogColorEnabled[0] = params.fogColor[0];
            lighting.FogColorEnabled[1] = params.fogColor[1];
            lighting.FogColorEnabled[2] = params.fogColor[2];
            lighting.FogColorEnabled[3] = params.fogEnabled ? 1.0f : 0.0f;
            lighting.FogStartEnd[0] = params.fogStart;
            lighting.FogStartEnd[1] = params.fogEnd;

            ID3D12Resource* perDrawCB  = GetOrCreatePerDrawConstantBufferEXT();
            ID3D12Resource* lightingCB = GetOrCreateLightingConstantBufferEXT();
            std::memcpy(perDrawConstantBufferMapped_, &perDraw, sizeof(perDraw));
            std::memcpy(lightingConstantBufferMapped_, &lighting, sizeof(lighting));
            cbAddresses[0] = perDrawCB->GetGPUVirtualAddress();
            cbAddresses[1] = lightingCB->GetGPUVirtualAddress();
        }
        else
        {
            // colored3d/textured3d/colored_textured3d bundle -- PerDraw (b0) + FogParams (b1), real
            // GpuDrawParams values (unlike DrawColoredPrimitives' hardcoded legacy path above).
            D3DPerDrawConstants perDraw{};
            wvp.ToColumnMajor(perDraw.Mvp);
            perDraw.DiffuseColor[0] = params.diffuseColor[0];
            perDraw.DiffuseColor[1] = params.diffuseColor[1];
            perDraw.DiffuseColor[2] = params.diffuseColor[2];
            perDraw.DiffuseColor[3] = params.diffuseColor[3];
            perDraw.TextureEnabled = params.textureEnabled ? 1.0f : 0.0f;
            perDraw.VertexColorEnabled = params.vertexColorEnabled ? 1.0f : 0.0f;

            D3DFogConstants fog{};
            fog.FogColorEnabled[0] = params.fogColor[0];
            fog.FogColorEnabled[1] = params.fogColor[1];
            fog.FogColorEnabled[2] = params.fogColor[2];
            fog.FogColorEnabled[3] = params.fogEnabled ? 1.0f : 0.0f;
            fog.FogStartEnd[0] = params.fogStart;
            fog.FogStartEnd[1] = params.fogEnd;

            ID3D12Resource* perDrawCB = GetOrCreatePerDrawConstantBufferEXT();
            ID3D12Resource* fogCB     = GetOrCreateFogConstantBufferEXT();
            std::memcpy(perDrawConstantBufferMapped_, &perDraw, sizeof(perDraw));
            std::memcpy(fogConstantBufferMapped_, &fog, sizeof(fog));
            cbAddresses[0] = perDrawCB->GetGPUVirtualAddress();
            cbAddresses[1] = fogCB->GetGPUVirtualAddress();
        }

        // DX-111 (dual_texture3d): each texture register (t0, t1, ...) is its own single-descriptor
        // root table parameter now (D3D12RootSignatureCache's own updated layout -- see that file's
        // doc comment for the real empirical finding that a single multi-descriptor table, populated
        // via a fresh per-draw CopyDescriptorsSimple, sampled as all-zero under this machine's Wine+
        // vkd3d-proton dev loop even though the CPU-side descriptor writes were independently
        // verified correct). Every texture's own SRV -- created once, at texture-construction time --
        // is bound directly, exactly like the original single-SRV path; no per-draw descriptor copy
        // is needed for any variant, dual_texture3d included.
        D3D12_GPU_DESCRIPTOR_HANDLE srvHandles[2]{};
        for (int i = 0; i < numSrvs; ++i)
        {
            // env_map3d's 2nd slot (t1) is a TextureCube, not a Texture2D -- srvCubeTexture is only
            // ever set by the needsEnvMap branch above, every other variant's 2nd slot (if any) is a
            // plain Texture2D via srvTextures[1] (dual_texture3d).
            srvHandles[i] = (i == 1 && srvCubeTexture != nullptr)
                ? GetSrvGpuHandleForTextureCubeEXT(srvCubeTexture)
                : GetSrvGpuHandleForTextureEXT(srvTextures[i]);
        }

        ID3D12CommandAllocator* allocator = GetCommandAllocatorEXT(0);
        ID3D12GraphicsCommandList* cmdList = GetCommandListEXT();
        allocator->Reset();
        cmdList->Reset(allocator, pso.Get());

        resourceStates_.TransitionTo(cmdList, boundColorResource_, D3D12_RESOURCE_STATE_RENDER_TARGET);
        cmdList->OMSetRenderTargets(1, &boundColorRtv_, FALSE, nullptr);

        D3D12_VIEWPORT viewport{0.0f, 0.0f, static_cast<float>(boundColorWidth_), static_cast<float>(boundColorHeight_), 0.0f, 1.0f};
        D3D12_RECT scissor{0, 0, boundColorWidth_, boundColorHeight_};
        cmdList->RSSetViewports(1, &viewport);
        cmdList->RSSetScissorRects(1, &scissor);

        cmdList->SetGraphicsRootSignature(rootSig.Get());
        cmdList->SetPipelineState(pso.Get());
        cmdList->IASetPrimitiveTopology(ToD3D12Topology(primitive));

        D3D12_VERTEX_BUFFER_VIEW vbView = d3dVb.GetViewEXT();
        cmdList->IASetVertexBuffers(0, 1, &vbView);
        if (ib != nullptr)
        {
            const auto& d3dIb = static_cast<const D3D12IndexBufferBackend&>(*ib);
            D3D12_INDEX_BUFFER_VIEW ibView = d3dIb.GetViewEXT();
            cmdList->IASetIndexBuffer(&ibView);
        }

        for (int i = 0; i < numCbvs; ++i)
            cmdList->SetGraphicsRootConstantBufferView(i, cbAddresses[i]);

        if (hasTexture)
        {
            // Each texture's own single-descriptor table root parameter comes right after the
            // numCbvs root CBV parameters, at index numCbvs+i for texture i (D3D12RootSignatureCache
            // ::GetOrCreate's own real parameter ordering -- see that file's doc comment).
            ID3D12DescriptorHeap* heaps[] = {cbvSrvUavHeap_.Get()};
            cmdList->SetDescriptorHeaps(1, heaps);
            for (int i = 0; i < numSrvs; ++i)
                cmdList->SetGraphicsRootDescriptorTable(numCbvs + i, srvHandles[i]);
        }

        if (ib != nullptr)
        {
            const UINT indexCount = static_cast<UINT>(VertexCountForPrimitives(primitive, primitiveCount));
            cmdList->DrawIndexedInstanced(indexCount, 1, 0, 0, 0);
        }
        else
        {
            const UINT vertexCount = static_cast<UINT>(VertexCountForPrimitives(primitive, primitiveCount));
            cmdList->DrawInstanced(vertexCount, 1, 0, 0);
        }

        HRESULT hr = cmdList->Close();
        if (FAILED(hr))
            throw std::runtime_error("DrawPrimitivesEx: command list Close failed, hr=" + FormatHr(hr));
        ExecuteCommandListAndWaitEXT(cmdList);
    }

    void D3D12GraphicsBackend::DrawPrimitivesEx(
        const IVertexBufferBackend& vb, const Matrix& world, const Matrix& view, const Matrix& projection,
        PrimitiveType primitive, int primitiveCount, const GpuDrawParams& params)
    {
        DrawPrimitivesExImpl(vb, nullptr, world, view, projection, primitive, primitiveCount, params);
    }

    void D3D12GraphicsBackend::DrawIndexedPrimitivesEx(
        const IVertexBufferBackend& vb, const IIndexBufferBackend& ib,
        const Matrix& world, const Matrix& view, const Matrix& projection,
        PrimitiveType primitive, int primitiveCount, const GpuDrawParams& params)
    {
        DrawPrimitivesExImpl(vb, &ib, world, view, projection, primitive, primitiveCount, params);
    }

    ID3D12PipelineState* D3D12GraphicsBackend::GetOrCreateInstancedPsoEXT(ID3D12RootSignature* rootSig)
    {
        if (instancedPso_)
            return instancedPso_.Get();

        const uint8_t* vsBytes = nullptr; std::size_t vsSize = 0;
        const uint8_t* psBytes = nullptr; std::size_t psSize = 0;
        GetVertexShaderBytecode(D3DShaderVariant::Instanced3d, vsBytes, vsSize);
        GetPixelShaderBytecode(D3DShaderVariant::Instanced3d, psBytes, psSize);
        if (!vsBytes || !psBytes)
            throw std::runtime_error("D3D12GraphicsBackend: missing instanced3d DXBC bytecode");

        // Mirrors D3D11GraphicsBackend::GetOrCreateInstancedInputLayoutEXT()'s own element list
        // exactly -- POSITION0 (per-vertex, slot 0) + INSTANCEWORLD0-3 (per-instance, slot 1).
        static const D3D12_INPUT_ELEMENT_DESC kElements[] = {
            { "POSITION",      0, DXGI_FORMAT_R32G32B32_FLOAT,    0, 0,  D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,   0 },
            { "INSTANCEWORLD", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, 0,  D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA, 1 },
            { "INSTANCEWORLD", 1, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, 16, D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA, 1 },
            { "INSTANCEWORLD", 2, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, 32, D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA, 1 },
            { "INSTANCEWORLD", 3, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, 48, D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA, 1 },
        };

        D3D12_GRAPHICS_PIPELINE_STATE_DESC desc{};
        desc.pRootSignature = rootSig;
        desc.VS = {vsBytes, vsSize};
        desc.PS = {psBytes, psSize};
        desc.InputLayout = {kElements, static_cast<UINT>(std::size(kElements))};
        desc.IBStripCutValue = D3D12_INDEX_BUFFER_STRIP_CUT_VALUE_DISABLED;
        desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        desc.SampleMask = UINT_MAX;
        desc.SampleDesc.Count = 1;
        desc.NodeMask = 0;

        // Same honest hardcoded-defaults simplification every other D3D12 draw uses today (no
        // D3D12 Phase-DX7-equivalent state-object cache exists yet).
        desc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
        desc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
        desc.RasterizerState.FrontCounterClockwise = FALSE;
        desc.RasterizerState.DepthClipEnable = TRUE;

        D3D12_RENDER_TARGET_BLEND_DESC& rt0 = desc.BlendState.RenderTarget[0];
        rt0.BlendEnable = FALSE;
        rt0.SrcBlend = D3D12_BLEND_ONE;
        rt0.DestBlend = D3D12_BLEND_ZERO;
        rt0.BlendOp = D3D12_BLEND_OP_ADD;
        rt0.SrcBlendAlpha = D3D12_BLEND_ONE;
        rt0.DestBlendAlpha = D3D12_BLEND_ZERO;
        rt0.BlendOpAlpha = D3D12_BLEND_OP_ADD;
        rt0.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;

        desc.DepthStencilState.DepthEnable = FALSE;
        desc.DepthStencilState.StencilEnable = FALSE;

        desc.NumRenderTargets = 1;
        desc.RTVFormats[0] = boundColorFormat_;
        desc.DSVFormat = DXGI_FORMAT_UNKNOWN;

        HRESULT hr = device_->CreateGraphicsPipelineState(&desc, IID_PPV_ARGS(instancedPso_.ReleaseAndGetAddressOf()));
        if (FAILED(hr))
            throw std::runtime_error("D3D12GraphicsBackend: instanced3d CreateGraphicsPipelineState failed, hr=" + FormatHr(hr));
        return instancedPso_.Get();
    }

    void D3D12GraphicsBackend::DrawInstancedPrimitivesEx(
        const IVertexBufferBackend& vb, const IIndexBufferBackend& ib,
        const Matrix& world, const Matrix& view, const Matrix& projection,
        PrimitiveType primitive, int primitiveCount, int instanceCount, const GpuDrawParams& params)
    {
        // Matches D3D11GraphicsBackend::DrawInstancedPrimitivesEx's own fallback -- no per-instance
        // VB means this isn't really an instanced draw at all.
        if (params.instanceVb == nullptr)
        {
            DrawIndexedPrimitivesEx(vb, ib, world, view, projection, primitive, primitiveCount, params);
            return;
        }
        if (!boundColorResource_)
        {
            NotYetImplemented("DrawInstancedPrimitivesEx (no off-screen color target bound -- "
                              "BindOffscreenColorTargetEXT; see Clear()'s own note)");
        }

        const auto& d3dVb     = static_cast<const D3D12VertexBufferBackend&>(vb);
        const auto& d3dIb     = static_cast<const D3D12IndexBufferBackend&>(ib);
        const auto& d3dInstVb = static_cast<const D3D12VertexBufferBackend&>(*params.instanceVb);

        auto rootSig = rootSigCache_.GetOrCreate(device_.Get(), /*numCbvs=*/1, /*numSrvs=*/0, /*numSamplers=*/0);
        if (!rootSig)
            throw std::runtime_error("DrawInstancedPrimitivesEx: failed to create root signature");
        ID3D12PipelineState* pso = GetOrCreateInstancedPsoEXT(rootSig.Get());
        if (!pso)
            throw std::runtime_error("DrawInstancedPrimitivesEx: failed to create instanced3d PSO");

        // instanced3d.vert.hlsl's PerDraw (b0) is byte-identical to D3DPerDrawConstants -- its first
        // field is named "Vp" (view*projection only, world comes from the per-instance buffer
        // instead) rather than "Mvp", same struct reused for the byte layout only.
        D3DPerDrawConstants perDraw{};
        const Matrix vp = view * projection;
        vp.ToColumnMajor(perDraw.Mvp);
        perDraw.DiffuseColor[0] = params.diffuseColor[0];
        perDraw.DiffuseColor[1] = params.diffuseColor[1];
        perDraw.DiffuseColor[2] = params.diffuseColor[2];
        perDraw.DiffuseColor[3] = params.diffuseColor[3];

        ID3D12Resource* perDrawCB = GetOrCreatePerDrawConstantBufferEXT();
        std::memcpy(perDrawConstantBufferMapped_, &perDraw, sizeof(perDraw));

        ID3D12CommandAllocator* allocator = GetCommandAllocatorEXT(0);
        ID3D12GraphicsCommandList* cmdList = GetCommandListEXT();
        allocator->Reset();
        cmdList->Reset(allocator, pso);

        resourceStates_.TransitionTo(cmdList, boundColorResource_, D3D12_RESOURCE_STATE_RENDER_TARGET);
        cmdList->OMSetRenderTargets(1, &boundColorRtv_, FALSE, nullptr);

        D3D12_VIEWPORT viewport{0.0f, 0.0f, static_cast<float>(boundColorWidth_), static_cast<float>(boundColorHeight_), 0.0f, 1.0f};
        D3D12_RECT scissor{0, 0, boundColorWidth_, boundColorHeight_};
        cmdList->RSSetViewports(1, &viewport);
        cmdList->RSSetScissorRects(1, &scissor);

        cmdList->SetGraphicsRootSignature(rootSig.Get());
        cmdList->SetPipelineState(pso);
        cmdList->IASetPrimitiveTopology(ToD3D12Topology(primitive));

        D3D12_VERTEX_BUFFER_VIEW vbViews[2] = { d3dVb.GetViewEXT(), d3dInstVb.GetViewEXT() };
        cmdList->IASetVertexBuffers(0, 2, vbViews);
        D3D12_INDEX_BUFFER_VIEW ibView = d3dIb.GetViewEXT();
        cmdList->IASetIndexBuffer(&ibView);

        cmdList->SetGraphicsRootConstantBufferView(0, perDrawCB->GetGPUVirtualAddress());

        const UINT indexCount = static_cast<UINT>(VertexCountForPrimitives(primitive, primitiveCount));
        const UINT instCount = static_cast<UINT>(std::max(1, instanceCount));
        cmdList->DrawIndexedInstanced(indexCount, instCount, 0, 0, 0);

        HRESULT hr = cmdList->Close();
        if (FAILED(hr))
            throw std::runtime_error("DrawInstancedPrimitivesEx: command list Close failed, hr=" + FormatHr(hr));
        ExecuteCommandListAndWaitEXT(cmdList);
    }
}

namespace CNA::Internal::Backends
{
    std::unique_ptr<IGraphicsBackend> CreateGraphicsBackend(const GraphicsBackendCreateArgs& args)
    {
        return std::make_unique<D3D12::D3D12GraphicsBackend>(args);
    }
}
