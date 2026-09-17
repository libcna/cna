// SPDX-License-Identifier: MIT
//
// plans/plan_directx12_parity.md DX12-0003: a Direct3D 12 probe with no CNA in it.
//
// It answers, for whatever Windows machine it runs on, the questions the DirectX12 renderer's
// WARP validation path depends on -- before the renderer is involved at all:
//
//   1. which DXGI adapters exist, which are software, and which of them D3D12 accepts;
//   2. whether IDXGIFactory4::EnumWarpAdapter yields a device, and at what feature level;
//   3. whether that device can create the objects a renderer needs (queue, fence, the four
//      descriptor heap types, allocator, command list);
//   4. whether it actually rasterizes -- a clear read back through GetCopyableFootprints;
//   5. optionally, whether a real HWND swap chain on it clears, presents and resizes;
//   6. what the debug layer, DRED and DXGI's live-object report say about all of the above.
//
// Build (MSVC developer prompt):
//   cl /nologo /EHsc /O2 /std:c++17 /W4 warp_probe.cpp /link d3d12.lib dxgi.lib dxguid.lib user32.lib
//
// Usage:
//   warp_probe.exe [--debug] [--dxgi-debug] [--gbv] [--dred] [--swapchain <frames>] [--hardware]
//
// --debug enables the D3D12 debug layer; --dxgi-debug creates the DXGI factory with
// DXGI_CREATE_FACTORY_DEBUG. They are separate because they are separate layers.
//
// --hardware tries the first hardware adapter instead of WARP for steps 3-5 (the control).
// Output is one "key=value" line per fact. Exit: 0 every attempted step passed, 1 a step failed,
// 2 the requested adapter could not create a D3D12 device (an environment answer, not a defect).

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <d3d12.h>
#include <d3d12sdklayers.h>
#include <dxgi1_6.h>
#include <dxgidebug.h>
#include <wrl/client.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iterator>
#include <string>
#include <vector>

using Microsoft::WRL::ComPtr;

namespace
{
    bool g_failed = false;
    ID3D12Device* g_device = nullptr; // for the exception logger; owned by main's Gpu

    void Fact(const char* key, const std::string& value) { std::printf("%s=%s\n", key, value.c_str()); }

    std::string Hex(unsigned long value)
    {
        char buf[16];
        std::snprintf(buf, sizeof(buf), "0x%08lX", value);
        return buf;
    }

    std::string Narrow(const wchar_t* text)
    {
        const int n = WideCharToMultiByte(CP_UTF8, 0, text, -1, nullptr, 0, nullptr, nullptr);
        std::string out(n > 0 ? static_cast<std::size_t>(n - 1) : 0u, '\0');
        if (n > 1) WideCharToMultiByte(CP_UTF8, 0, text, -1, out.data(), n, nullptr, nullptr);
        return out;
    }

    bool Check(const char* step, HRESULT hr)
    {
        Fact(step, SUCCEEDED(hr) ? std::string("ok") : "FAILED hr=" + Hex(static_cast<unsigned long>(hr)));
        if (FAILED(hr)) g_failed = true;
        return SUCCEEDED(hr);
    }

    std::string FeatureLevelName(D3D_FEATURE_LEVEL level)
    {
        switch (level)
        {
        case D3D_FEATURE_LEVEL_12_2: return "12_2";
        case D3D_FEATURE_LEVEL_12_1: return "12_1";
        case D3D_FEATURE_LEVEL_12_0: return "12_0";
        case D3D_FEATURE_LEVEL_11_1: return "11_1";
        case D3D_FEATURE_LEVEL_11_0: return "11_0";
        default: return Hex(static_cast<unsigned long>(level));
        }
    }

    void DescribeAdapter(const char* prefix, IDXGIAdapter1* adapter)
    {
        DXGI_ADAPTER_DESC1 d{};
        adapter->GetDesc1(&d);
        std::string p(prefix);
        Fact((p + ".description").c_str(), Narrow(d.Description));
        Fact((p + ".vendor").c_str(), Hex(d.VendorId));
        Fact((p + ".device").c_str(), Hex(d.DeviceId));
        Fact((p + ".subsys").c_str(), Hex(d.SubSysId));
        Fact((p + ".revision").c_str(), std::to_string(d.Revision));
        Fact((p + ".dedicated_video_mb").c_str(), std::to_string(d.DedicatedVideoMemory / (1024 * 1024)));
        Fact((p + ".dedicated_system_mb").c_str(), std::to_string(d.DedicatedSystemMemory / (1024 * 1024)));
        Fact((p + ".shared_system_mb").c_str(), std::to_string(d.SharedSystemMemory / (1024 * 1024)));
        Fact((p + ".software").c_str(), (d.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) ? "true" : "false");
        const HRESULT probe = D3D12CreateDevice(adapter, D3D_FEATURE_LEVEL_11_0, __uuidof(ID3D12Device), nullptr);
        Fact((p + ".d3d12_fl11_0").c_str(),
             probe == S_FALSE || SUCCEEDED(probe) ? std::string("supported")
                                                  : "unsupported hr=" + Hex(static_cast<unsigned long>(probe)));
    }

    void DrainInfoQueue(ID3D12Device* device, const char* phase)
    {
        ComPtr<ID3D12InfoQueue> queue;
        if (!device || FAILED(device->QueryInterface(IID_PPV_ARGS(queue.GetAddressOf())))) return;
        const UINT64 count = queue->GetNumStoredMessages();
        for (UINT64 i = 0; i < count; ++i)
        {
            SIZE_T length = 0;
            queue->GetMessage(i, nullptr, &length);
            std::vector<char> storage(length);
            auto* message = reinterpret_cast<D3D12_MESSAGE*>(storage.data());
            if (FAILED(queue->GetMessage(i, message, &length))) continue;
            std::printf("debug.%s.message[%llu]=severity %d id %d: %.*s\n", phase,
                        static_cast<unsigned long long>(i), static_cast<int>(message->Severity),
                        static_cast<int>(message->ID), static_cast<int>(message->DescriptionByteLength),
                        message->pDescription);
            // A clear whose colour differs from the resource's optimized clear value is a documented
            // performance hint, not a correctness message: a swap-chain back buffer has no clear
            // value at all, and XNA clears to whatever colour the game names.
            const bool clearValueHint =
                message->ID == D3D12_MESSAGE_ID_CLEARRENDERTARGETVIEW_MISMATCHINGCLEARVALUE ||
                message->ID == D3D12_MESSAGE_ID_CLEARDEPTHSTENCILVIEW_MISMATCHINGCLEARVALUE;
            if (message->Severity <= D3D12_MESSAGE_SEVERITY_WARNING && !clearValueHint) g_failed = true;
        }
        Fact((std::string("debug.") + phase + ".stored_messages").c_str(), std::to_string(count));
        queue->ClearStoredMessages();
    }

    struct Gpu
    {
        ComPtr<ID3D12Device> device;
        ComPtr<ID3D12CommandQueue> queue;
        ComPtr<ID3D12Fence> fence;
        HANDLE event = nullptr;
        UINT64 fenceValue = 0;
        ComPtr<ID3D12CommandAllocator> allocator;
        ComPtr<ID3D12GraphicsCommandList> list;

        bool Submit()
        {
            if (FAILED(list->Close())) return false;
            ID3D12CommandList* lists[] = {list.Get()};
            queue->ExecuteCommandLists(1, lists);
            const UINT64 value = ++fenceValue;
            if (FAILED(queue->Signal(fence.Get(), value))) return false;
            if (fence->GetCompletedValue() < value)
            {
                if (FAILED(fence->SetEventOnCompletion(value, event))) return false;
                WaitForSingleObject(event, INFINITE);
            }
            return SUCCEEDED(allocator->Reset()) && SUCCEEDED(list->Reset(allocator.Get(), nullptr));
        }

        // A flip-model Present is itself queued work that references the back buffer. Signalling a
        // fresh fence value after it and waiting is what makes releasing those buffers safe -- the
        // debug layer terminates the process with ID 921 (OBJECT_DELETED_WHILE_STILL_IN_USE)
        // otherwise, which is exactly what the first version of this probe did on every resize.
        bool WaitForQueueIdle()
        {
            const UINT64 value = ++fenceValue;
            if (FAILED(queue->Signal(fence.Get(), value))) return false;
            if (fence->GetCompletedValue() < value)
            {
                if (FAILED(fence->SetEventOnCompletion(value, event))) return false;
                WaitForSingleObject(event, INFINITE);
            }
            return true;
        }
    };

    void Transition(ID3D12GraphicsCommandList* list, ID3D12Resource* resource,
                    D3D12_RESOURCE_STATES from, D3D12_RESOURCE_STATES to)
    {
        D3D12_RESOURCE_BARRIER barrier{};
        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Transition.pResource = resource;
        barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        barrier.Transition.StateBefore = from;
        barrier.Transition.StateAfter = to;
        list->ResourceBarrier(1, &barrier);
    }

    // Clears a small RGBA8 target and reads it back: the proof that the device rasterizes rather
    // than merely constructs. GetCopyableFootprints decides the row pitch, never an assumption.
    bool OffscreenClearReadback(Gpu& gpu)
    {
        constexpr UINT W = 5, H = 3; // deliberately not a power of two or a multiple of 4
        D3D12_HEAP_PROPERTIES defaultHeap{};
        defaultHeap.Type = D3D12_HEAP_TYPE_DEFAULT;
        D3D12_RESOURCE_DESC desc{};
        desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        desc.Width = W;
        desc.Height = H;
        desc.DepthOrArraySize = 1;
        desc.MipLevels = 1;
        desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        desc.SampleDesc.Count = 1;
        desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
        const float clear[4] = {0.25f, 0.5f, 0.75f, 1.0f};
        D3D12_CLEAR_VALUE optimizedClear{};
        optimizedClear.Format = desc.Format;
        std::memcpy(optimizedClear.Color, clear, sizeof(clear));
        ComPtr<ID3D12Resource> target;
        if (!Check("offscreen.create_target", gpu.device->CreateCommittedResource(
                &defaultHeap, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_RENDER_TARGET, &optimizedClear,
                IID_PPV_ARGS(target.GetAddressOf())))) return false;

        D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc{};
        rtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
        rtvHeapDesc.NumDescriptors = 1;
        ComPtr<ID3D12DescriptorHeap> rtvHeap;
        if (!Check("offscreen.rtv_heap", gpu.device->CreateDescriptorHeap(&rtvHeapDesc, IID_PPV_ARGS(rtvHeap.GetAddressOf()))))
            return false;
        const D3D12_CPU_DESCRIPTOR_HANDLE rtv = rtvHeap->GetCPUDescriptorHandleForHeapStart();
        gpu.device->CreateRenderTargetView(target.Get(), nullptr, rtv);

        D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};
        UINT rows = 0;
        UINT64 rowSize = 0, total = 0;
        gpu.device->GetCopyableFootprints(&desc, 0, 1, 0, &footprint, &rows, &rowSize, &total);
        Fact("offscreen.footprint", "rowpitch " + std::to_string(footprint.Footprint.RowPitch) +
                                        " rowsize " + std::to_string(rowSize) + " total " + std::to_string(total));

        D3D12_HEAP_PROPERTIES readbackHeap{};
        readbackHeap.Type = D3D12_HEAP_TYPE_READBACK;
        D3D12_RESOURCE_DESC bufferDesc{};
        bufferDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        bufferDesc.Width = total;
        bufferDesc.Height = 1;
        bufferDesc.DepthOrArraySize = 1;
        bufferDesc.MipLevels = 1;
        bufferDesc.SampleDesc.Count = 1;
        bufferDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        ComPtr<ID3D12Resource> readback;
        if (!Check("offscreen.create_readback", gpu.device->CreateCommittedResource(
                &readbackHeap, D3D12_HEAP_FLAG_NONE, &bufferDesc, D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
                IID_PPV_ARGS(readback.GetAddressOf())))) return false;

        gpu.list->OMSetRenderTargets(1, &rtv, FALSE, nullptr);
        gpu.list->ClearRenderTargetView(rtv, clear, 0, nullptr);
        Transition(gpu.list.Get(), target.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_SOURCE);
        D3D12_TEXTURE_COPY_LOCATION dst{};
        dst.pResource = readback.Get();
        dst.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        dst.PlacedFootprint = footprint;
        D3D12_TEXTURE_COPY_LOCATION src{};
        src.pResource = target.Get();
        src.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        gpu.list->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
        Transition(gpu.list.Get(), target.Get(), D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_RENDER_TARGET);
        if (!gpu.Submit()) { Fact("offscreen.submit", "FAILED"); g_failed = true; return false; }

        std::uint8_t* mapped = nullptr;
        const D3D12_RANGE readRange{0, static_cast<SIZE_T>(total)};
        if (!Check("offscreen.map", readback->Map(0, &readRange, reinterpret_cast<void**>(&mapped)))) return false;
        int mismatches = 0;
        for (UINT y = 0; y < H; ++y)
            for (UINT x = 0; x < W; ++x)
            {
                const std::uint8_t* p = mapped + footprint.Offset + y * footprint.Footprint.RowPitch + x * 4;
                if (p[0] != 64 || p[1] != 128 || p[2] != 191 || p[3] != 255) ++mismatches;
            }
        char first[64];
        std::snprintf(first, sizeof(first), "%u,%u,%u,%u", mapped[footprint.Offset], mapped[footprint.Offset + 1],
                      mapped[footprint.Offset + 2], mapped[footprint.Offset + 3]);
        const D3D12_RANGE noWrite{0, 0};
        readback->Unmap(0, &noWrite);
        Fact("offscreen.first_pixel", first);
        Fact("offscreen.readback", mismatches == 0 ? std::string("ok 64,128,191,255 in every pixel")
                                                   : "FAILED " + std::to_string(mismatches) + " mismatching pixels");
        if (mismatches != 0) g_failed = true;
        return mismatches == 0;
    }

    // The debug layers report some messages by raising a first-chance exception for an attached
    // debugger. Logging the code and arguments here is what turns an otherwise silent process exit
    // into a named message.
    LONG CALLBACK LogException(EXCEPTION_POINTERS* info)
    {
        const EXCEPTION_RECORD* r = info->ExceptionRecord;
        if (r->ExceptionCode == 0x40010006 || r->ExceptionCode == 0x4001000A) // OutputDebugString A/W
            return EXCEPTION_CONTINUE_SEARCH;
        std::printf("exception.code=0x%08lX params=%lu", static_cast<unsigned long>(r->ExceptionCode),
                    static_cast<unsigned long>(r->NumberParameters));
        for (DWORD i = 0; i < r->NumberParameters && i < 4; ++i)
            std::printf(" p%lu=0x%llX", static_cast<unsigned long>(i),
                        static_cast<unsigned long long>(r->ExceptionInformation[i]));
        std::printf("\n");
        if (r->ExceptionCode == 0x87D && g_device) DrainInfoQueue(g_device, "at_exception");
        return EXCEPTION_CONTINUE_SEARCH;
    }

    LRESULT CALLBACK ProbeWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
    {
        return DefWindowProcW(hwnd, msg, wp, lp);
    }

    void PumpMessages()
    {
        MSG msg;
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) { TranslateMessage(&msg); DispatchMessageW(&msg); }
    }

    // Clear + Present on a flip-model swap chain, with a ResizeBuffers every 50 frames: the
    // renderer's own presentation shape, without the renderer.
    void SwapChainFrames(IDXGIFactory4* factory, Gpu& gpu, int frames)
    {
        WNDCLASSW wc{};
        wc.lpfnWndProc = ProbeWndProc;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.lpszClassName = L"CnaD3D12WarpProbe";
        RegisterClassW(&wc);
        HWND hwnd = CreateWindowExW(0, wc.lpszClassName, L"warp probe", WS_OVERLAPPEDWINDOW | WS_VISIBLE,
                                    100, 100, 320, 240, nullptr, nullptr, wc.hInstance, nullptr);
        if (!hwnd) { Fact("swapchain.window", "FAILED"); g_failed = true; return; }

        constexpr UINT kBuffers = 2;
        DXGI_SWAP_CHAIN_DESC1 desc{};
        desc.Width = 0;
        desc.Height = 0;
        desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        desc.SampleDesc.Count = 1;
        desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        desc.BufferCount = kBuffers;
        desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
        ComPtr<IDXGISwapChain1> chain1;
        if (!Check("swapchain.create", factory->CreateSwapChainForHwnd(gpu.queue.Get(), hwnd, &desc, nullptr, nullptr,
                                                                     chain1.GetAddressOf())))
        { DestroyWindow(hwnd); return; }
        ComPtr<IDXGISwapChain3> chain;
        chain1.As(&chain);

        D3D12_DESCRIPTOR_HEAP_DESC heapDesc{};
        heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
        heapDesc.NumDescriptors = kBuffers;
        ComPtr<ID3D12DescriptorHeap> heap;
        gpu.device->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(heap.GetAddressOf()));
        const UINT increment = gpu.device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

        ComPtr<ID3D12Resource> buffers[kBuffers];
        auto acquire = [&]()
        {
            for (UINT i = 0; i < kBuffers; ++i)
            {
                chain->GetBuffer(i, IID_PPV_ARGS(buffers[i].ReleaseAndGetAddressOf()));
                D3D12_CPU_DESCRIPTOR_HANDLE h = heap->GetCPUDescriptorHandleForHeapStart();
                h.ptr += static_cast<SIZE_T>(i) * increment;
                gpu.device->CreateRenderTargetView(buffers[i].Get(), nullptr, h);
            }
        };
        acquire();

        int presentFailures = 0, resizes = 0;
        LARGE_INTEGER freq{}, start{}, end{};
        QueryPerformanceFrequency(&freq);
        QueryPerformanceCounter(&start);
        for (int frame = 0; frame < frames; ++frame)
        {
            PumpMessages();
            const UINT index = chain->GetCurrentBackBufferIndex();
            D3D12_CPU_DESCRIPTOR_HANDLE h = heap->GetCPUDescriptorHandleForHeapStart();
            h.ptr += static_cast<SIZE_T>(index) * increment;
            ID3D12Resource* back = buffers[index].Get();
            Transition(gpu.list.Get(), back, D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET);
            const float c = static_cast<float>(frame % 60) / 60.0f;
            const float color[4] = {c, 0.25f, 1.0f - c, 1.0f};
            gpu.list->OMSetRenderTargets(1, &h, FALSE, nullptr);
            gpu.list->ClearRenderTargetView(h, color, 0, nullptr);
            Transition(gpu.list.Get(), back, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT);
            if (!gpu.Submit()) { Fact("swapchain.submit", "FAILED at frame " + std::to_string(frame)); g_failed = true; break; }
            if (FAILED(chain->Present(0, 0))) ++presentFailures;
            if (frame < 3 || frame % 250 == 0) Fact("swapchain.progress", "frame " + std::to_string(frame));

            if (frame % 50 == 49)
            {
                gpu.WaitForQueueIdle();
                for (auto& b : buffers) b.Reset();
                const UINT w = 200 + static_cast<UINT>((frame / 50) % 5) * 40;
                const UINT hgt = 150 + static_cast<UINT>((frame / 50) % 3) * 30;
                SetWindowPos(hwnd, nullptr, 0, 0, static_cast<int>(w), static_cast<int>(hgt), SWP_NOMOVE | SWP_NOZORDER);
                PumpMessages();
                if (FAILED(chain->ResizeBuffers(kBuffers, 0, 0, DXGI_FORMAT_UNKNOWN, 0)))
                { Fact("swapchain.resize", "FAILED at frame " + std::to_string(frame)); g_failed = true; break; }
                ++resizes;
                acquire();
            }
        }
        QueryPerformanceCounter(&end);
        const double seconds = static_cast<double>(end.QuadPart - start.QuadPart) / static_cast<double>(freq.QuadPart);
        Fact("swapchain.frames", std::to_string(frames));
        Fact("swapchain.resizes", std::to_string(resizes));
        Fact("swapchain.present_failures", std::to_string(presentFailures));
        char fps[32];
        std::snprintf(fps, sizeof(fps), "%.1f", seconds > 0 ? frames / seconds : 0.0);
        Fact("swapchain.fps", fps);
        if (presentFailures) g_failed = true;
        gpu.WaitForQueueIdle();
        for (auto& b : buffers) b.Reset();
        chain.Reset();
        chain1.Reset();
        DestroyWindow(hwnd);
    }
}

int main(int argc, char** argv)
{
    // Unbuffered: a crash inside the driver must not take the facts printed before it along.
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    AddVectoredExceptionHandler(1, LogException);
    bool debug = false, gbv = false, dred = false, hardware = false, dxgiDebugFactory = false;
    int swapFrames = 0;
    for (int i = 1; i < argc; ++i)
    {
        if (!std::strcmp(argv[i], "--debug")) debug = true;
        else if (!std::strcmp(argv[i], "--dxgi-debug")) dxgiDebugFactory = true;
        else if (!std::strcmp(argv[i], "--gbv")) debug = gbv = true;
        else if (!std::strcmp(argv[i], "--dred")) dred = true;
        else if (!std::strcmp(argv[i], "--hardware")) hardware = true;
        else if (!std::strcmp(argv[i], "--swapchain") && i + 1 < argc) swapFrames = std::atoi(argv[++i]);
    }

    if (debug)
    {
        ComPtr<ID3D12Debug> controller;
        if (Check("debug.layer", D3D12GetDebugInterface(IID_PPV_ARGS(controller.GetAddressOf()))))
        {
            controller->EnableDebugLayer();
            ComPtr<ID3D12Debug1> controller1;
            if (gbv && SUCCEEDED(controller.As(&controller1)))
            {
                controller1->SetEnableGPUBasedValidation(TRUE);
                Fact("debug.gpu_based_validation", "enabled");
            }
        }
    }
    if (dred)
    {
        ComPtr<ID3D12DeviceRemovedExtendedDataSettings> settings;
        if (Check("dred.settings", D3D12GetDebugInterface(IID_PPV_ARGS(settings.GetAddressOf()))))
        {
            settings->SetAutoBreadcrumbsEnablement(D3D12_DRED_ENABLEMENT_FORCED_ON);
            settings->SetPageFaultEnablement(D3D12_DRED_ENABLEMENT_FORCED_ON);
        }
    }

    ComPtr<IDXGIInfoQueue> dxgiInfoQueue;
    if ((debug || dxgiDebugFactory) && SUCCEEDED(DXGIGetDebugInterface1(0, IID_PPV_ARGS(dxgiInfoQueue.GetAddressOf()))))
        dxgiInfoQueue->ClearStoredMessages(DXGI_DEBUG_ALL);

    ComPtr<IDXGIFactory4> factory;
    if (!Check("dxgi.factory", CreateDXGIFactory2(dxgiDebugFactory ? DXGI_CREATE_FACTORY_DEBUG : 0,
                                                  IID_PPV_ARGS(factory.GetAddressOf()))))
        return 1;

    // 1. every adapter DXGI enumerates, in enumeration order.
    ComPtr<IDXGIAdapter1> firstHardware;
    for (UINT i = 0;; ++i)
    {
        ComPtr<IDXGIAdapter1> adapter;
        if (factory->EnumAdapters1(i, adapter.GetAddressOf()) == DXGI_ERROR_NOT_FOUND) break;
        DescribeAdapter(("adapter[" + std::to_string(i) + "]").c_str(), adapter.Get());
        DXGI_ADAPTER_DESC1 d{};
        adapter->GetDesc1(&d);
        if (!firstHardware && !(d.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)) firstHardware = adapter;
    }

    // 2. WARP, by API rather than by name.
    ComPtr<IDXGIAdapter1> warp;
    if (Check("warp.enum", factory->EnumWarpAdapter(IID_PPV_ARGS(warp.GetAddressOf()))))
        DescribeAdapter("warp", warp.Get());

    IDXGIAdapter1* chosen = hardware ? firstHardware.Get() : warp.Get();
    Fact("probe.adapter", hardware ? "first hardware adapter" : "WARP");
    if (!chosen) { Fact("probe.result", "ENVIRONMENT no such adapter"); return 2; }

    Gpu gpu;
    HRESULT hr = D3D12CreateDevice(chosen, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(gpu.device.GetAddressOf()));
    if (FAILED(hr))
    {
        Fact("device.create", "FAILED hr=" + Hex(static_cast<unsigned long>(hr)));
        Fact("probe.result", "ENVIRONMENT the adapter refused D3D12");
        return 2;
    }
    Fact("device.create", "ok");
    g_device = gpu.device.Get();

    if (debug)
    {
        ComPtr<ID3D12InfoQueue> queue;
        Fact("debug.info_queue", SUCCEEDED(gpu.device.As(&queue)) ? "available" : "unavailable");
        if (queue)
            Fact("debug.break_on", std::string("corruption=") +
                 (queue->GetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_CORRUPTION) ? "1" : "0") +
                 " error=" + (queue->GetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_ERROR) ? "1" : "0") +
                 " warning=" + (queue->GetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_WARNING) ? "1" : "0"));
    }

    // Feature facts a renderer would ask.
    {
        static const D3D_FEATURE_LEVEL requested[] = {D3D_FEATURE_LEVEL_12_2, D3D_FEATURE_LEVEL_12_1, D3D_FEATURE_LEVEL_12_0,
                                                      D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0};
        D3D12_FEATURE_DATA_FEATURE_LEVELS levels{};
        levels.NumFeatureLevels = static_cast<UINT>(std::size(requested));
        levels.pFeatureLevelsRequested = requested;
        if (SUCCEEDED(gpu.device->CheckFeatureSupport(D3D12_FEATURE_FEATURE_LEVELS, &levels, sizeof(levels))))
            Fact("feature.max_level", FeatureLevelName(levels.MaxSupportedFeatureLevel));
        D3D12_FEATURE_DATA_SHADER_MODEL sm{};
        sm.HighestShaderModel = D3D_SHADER_MODEL_6_5;
        if (SUCCEEDED(gpu.device->CheckFeatureSupport(D3D12_FEATURE_SHADER_MODEL, &sm, sizeof(sm))))
            Fact("feature.shader_model", Hex(static_cast<unsigned long>(sm.HighestShaderModel)));
        D3D12_FEATURE_DATA_ROOT_SIGNATURE rs{};
        rs.HighestVersion = D3D_ROOT_SIGNATURE_VERSION_1_1;
        if (SUCCEEDED(gpu.device->CheckFeatureSupport(D3D12_FEATURE_ROOT_SIGNATURE, &rs, sizeof(rs))))
            Fact("feature.root_signature", rs.HighestVersion == D3D_ROOT_SIGNATURE_VERSION_1_1 ? "1.1" : "1.0");
        D3D12_FEATURE_DATA_D3D12_OPTIONS options{};
        if (SUCCEEDED(gpu.device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS, &options, sizeof(options))))
        {
            Fact("feature.resource_binding_tier", std::to_string(options.ResourceBindingTier));
            Fact("feature.tiled_resources_tier", std::to_string(options.TiledResourcesTier));
            Fact("feature.conservative_raster_tier", std::to_string(options.ConservativeRasterizationTier));
            Fact("feature.output_merger_logic_op", options.OutputMergerLogicOp ? "true" : "false");
        }
        D3D12_FEATURE_DATA_MULTISAMPLE_QUALITY_LEVELS msaa{};
        msaa.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        std::string counts;
        for (UINT count : {2u, 4u, 8u})
        {
            msaa.SampleCount = count;
            if (SUCCEEDED(gpu.device->CheckFeatureSupport(D3D12_FEATURE_MULTISAMPLE_QUALITY_LEVELS, &msaa, sizeof(msaa))) &&
                msaa.NumQualityLevels > 0)
                counts += (counts.empty() ? "" : ",") + std::to_string(count);
        }
        Fact("feature.rgba8_msaa_counts", counts.empty() ? "none" : counts);
    }

    // 3. the objects DirectX12Renderer creates at construction.
    D3D12_COMMAND_QUEUE_DESC queueDesc{};
    queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    Check("device.command_queue", gpu.device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(gpu.queue.GetAddressOf())));
    Check("device.fence", gpu.device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(gpu.fence.GetAddressOf())));
    gpu.event = CreateEventExW(nullptr, nullptr, 0, EVENT_ALL_ACCESS);
    Fact("device.fence_event", gpu.event ? "ok" : "FAILED");
    const struct { D3D12_DESCRIPTOR_HEAP_TYPE type; UINT count; bool shaderVisible; const char* name; } heaps[] = {
        {D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 64, false, "device.heap_rtv"},
        {D3D12_DESCRIPTOR_HEAP_TYPE_DSV, 8, false, "device.heap_dsv"},
        {D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 64, true, "device.heap_cbv_srv_uav"},
        {D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER, 16, true, "device.heap_sampler"},
    };
    std::vector<ComPtr<ID3D12DescriptorHeap>> heapObjects;
    for (const auto& h : heaps)
    {
        D3D12_DESCRIPTOR_HEAP_DESC d{};
        d.Type = h.type;
        d.NumDescriptors = h.count;
        d.Flags = h.shaderVisible ? D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE : D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
        ComPtr<ID3D12DescriptorHeap> object;
        Check(h.name, gpu.device->CreateDescriptorHeap(&d, IID_PPV_ARGS(object.GetAddressOf())));
        heapObjects.push_back(object);
    }
    Check("device.command_allocator", gpu.device->CreateCommandAllocator(
        D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(gpu.allocator.GetAddressOf())));
    Check("device.command_list", gpu.device->CreateCommandList(
        0, D3D12_COMMAND_LIST_TYPE_DIRECT, gpu.allocator.Get(), nullptr, IID_PPV_ARGS(gpu.list.GetAddressOf())));
    DrainInfoQueue(gpu.device.Get(), "construct");

    // 4. it rasterizes.
    if (gpu.queue && gpu.fence && gpu.event && gpu.list)
    {
        OffscreenClearReadback(gpu);
        DrainInfoQueue(gpu.device.Get(), "offscreen");
    }

    // 5. a real HWND swap chain on the same device.
    if (swapFrames > 0 && gpu.queue)
    {
        SwapChainFrames(factory.Get(), gpu, swapFrames);
        DrainInfoQueue(gpu.device.Get(), "swapchain");
    }

    const HRESULT removed = gpu.device->GetDeviceRemovedReason();
    Fact("device.removed_reason", removed == S_OK ? std::string("none") : Hex(static_cast<unsigned long>(removed)));
    if (removed != S_OK) g_failed = true;

    // 6. teardown, then DXGI's own account of what is still alive.
    gpu.list.Reset();
    gpu.allocator.Reset();
    heapObjects.clear();
    gpu.fence.Reset();
    gpu.queue.Reset();
    g_device = nullptr;
    gpu.device.Reset();
    if (gpu.event) CloseHandle(gpu.event);
    warp.Reset();
    firstHardware.Reset();
    factory.Reset();
    if (debug)
    {
        ComPtr<IDXGIDebug1> dxgiDebug;
        if (SUCCEEDED(DXGIGetDebugInterface1(0, IID_PPV_ARGS(dxgiDebug.GetAddressOf()))))
        {
            if (dxgiInfoQueue) dxgiInfoQueue->ClearStoredMessages(DXGI_DEBUG_ALL);
            dxgiDebug->ReportLiveObjects(DXGI_DEBUG_ALL,
                static_cast<DXGI_DEBUG_RLO_FLAGS>(DXGI_DEBUG_RLO_DETAIL | DXGI_DEBUG_RLO_IGNORE_INTERNAL));
            UINT64 live = 0;
            if (dxgiInfoQueue)
            {
                live = dxgiInfoQueue->GetNumStoredMessages(DXGI_DEBUG_ALL);
                for (UINT64 i = 0; i < live; ++i)
                {
                    SIZE_T length = 0;
                    dxgiInfoQueue->GetMessage(DXGI_DEBUG_ALL, i, nullptr, &length);
                    std::vector<char> storage(length);
                    auto* message = reinterpret_cast<DXGI_INFO_QUEUE_MESSAGE*>(storage.data());
                    if (SUCCEEDED(dxgiInfoQueue->GetMessage(DXGI_DEBUG_ALL, i, message, &length)))
                        std::printf("debug.live_object[%llu]=%.*s\n", static_cast<unsigned long long>(i),
                                    static_cast<int>(message->DescriptionByteLength), message->pDescription);
                }
            }
            // With every reference released above, anything reported still alive is a leak.
            Fact("debug.live_objects_reported", std::to_string(live));
            if (live != 0) g_failed = true;
        }
    }

    Fact("probe.result", g_failed ? "FAIL" : "PASS");
    return g_failed ? 1 : 0;
}
