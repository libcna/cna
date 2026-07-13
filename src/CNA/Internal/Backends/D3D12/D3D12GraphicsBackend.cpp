// plan_dx.md Phase DX12 (DX-102/DX-103/DX-104/DX-105): D3D12 device-lifetime resources -- real
// ID3D12Device + command queue + descriptor heaps + per-frame command allocators/command list +
// fence-based synchronization. Clear()/Present()/draw calls are still honest "not yet implemented"
// stubs -- see D3D12GraphicsBackend.hpp's class doc comment for exactly why and what's next.
#include "CNA/Internal/Backends/D3D12/D3D12GraphicsBackend.hpp"

#include <SDL3/SDL.h>

#include <cstdio>
#include <stdexcept>
#include <string>

namespace CNA::Internal::Backends::D3D12
{
    namespace
    {
        std::string FormatHr(HRESULT hr)
        {
            char buf[32];
            std::snprintf(buf, sizeof(buf), "0x%08lX", static_cast<unsigned long>(hr));
            return buf;
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
            throw std::runtime_error("ID3D12CommandQueue::Signal failed, hr=" + FormatHr(hr));

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
            throw std::runtime_error("ID3D12CommandQueue::Signal failed, hr=" + FormatHr(hr));

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

    void D3D12GraphicsBackend::Clear(float, float, float, float) { NotYetImplemented("Clear"); }
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

    std::unique_ptr<ITextureBackend> D3D12GraphicsBackend::CreateTexture(const ImageData&)
    {
        NotYetImplemented("CreateTexture");
    }

    std::unique_ptr<ISpriteBatchBackend> D3D12GraphicsBackend::CreateSpriteBatch()
    {
        NotYetImplemented("CreateSpriteBatch");
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

    std::unique_ptr<IVertexBufferBackend> D3D12GraphicsBackend::CreateVertexBuffer(int)
    {
        NotYetImplemented("CreateVertexBuffer");
    }

    std::unique_ptr<IIndexBufferBackend> D3D12GraphicsBackend::CreateIndexBuffer16(int)
    {
        NotYetImplemented("CreateIndexBuffer16");
    }

    void D3D12GraphicsBackend::DrawColoredPrimitives(const IVertexBufferBackend&,
                                                      const Matrix&, const Matrix&, const Matrix&,
                                                      PrimitiveType, int)
    {
        NotYetImplemented("DrawColoredPrimitives");
    }

    void D3D12GraphicsBackend::DrawIndexedColoredPrimitives(const IVertexBufferBackend&, const IIndexBufferBackend&,
                                                             const Matrix&, const Matrix&, const Matrix&,
                                                             PrimitiveType, int)
    {
        NotYetImplemented("DrawIndexedColoredPrimitives");
    }
}

namespace CNA::Internal::Backends
{
    std::unique_ptr<IGraphicsBackend> CreateGraphicsBackend(const GraphicsBackendCreateArgs& args)
    {
        return std::make_unique<D3D12::D3D12GraphicsBackend>(args);
    }
}
