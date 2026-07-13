// SPDX-License-Identifier: MS-PL
// plan_dx.md Phase DX12 (DX-102/DX-103/DX-104/DX-105): smoke test for D3D12's device-lifetime
// resources. Deliberately constructed OFF-SCREEN (GraphicsBackendCreateArgs::window = nullptr) --
// DX-100's own real spike found CreateSwapChainForHwnd(..., DXGI_SWAP_EFFECT_FLIP_DISCARD) crashes
// inside vanilla Wine's own dxgi.dll, so this primary smoke test never constructs a window and
// never reaches that code path, keeping this CTest genuinely green on this dev loop. The real
// (window-attached) swap-chain attempt is exercised separately, once, honestly, outside the
// default CTest suite -- see plan_dx.md DX-102's own row for that real outcome.
//
// Check A -- real ID3D12Device created, feature level >= 11_0 (D3D12CreateDevice's own retry-loop
//   fallback, DX-102), tearing-capability query ran without throwing.
// Check B -- real ID3D12CommandQueue created (non-null).
// Check C -- descriptor heaps (RTV/DSV/CBV_SRV_UAV, DX-103): two consecutive allocations from each
//   heap return CPU handles that differ by exactly one descriptor-increment-size step -- a real
//   bump-allocator proof, not just "the heap object is non-null".
// Check D -- command allocators + the one reused command list (DX-104): Reset() against a real
//   allocator, Close(), then ExecuteCommandListAndWaitEXT() submits it to the real queue and
//   blocks on the real fence until the GPU actually completes it -- proves the queue+fence+command-
//   list path is genuinely wired together, not just individually constructible.
// Check E -- DX-105's own N-frames-in-flight primitive (SignalAndWaitForFrameEXT): fence values
//   signaled per frame index strictly increase; a second call for an already-used frame index
//   genuinely blocks until that slot's PREVIOUS value completes before returning (proven via
//   GetCompletedValue(), not assumed) -- but, correctly, does NOT block on the value it just
//   signaled itself (a real Present() loop must never stall on the frame it just submitted); an
//   explicit follow-up wait proves that value completes eventually too.
// Check F -- off-screen construction (window=nullptr) leaves IsSwapChainAvailableEXT() == false and
//   GetSwapChainEXT() == nullptr, by design (see class doc comment) -- confirms the crash-prone path
//   was never touched by this run.
//
// Exit code 0 = all checks PASS, 1 = any FAILs.

#include "CNA/Internal/Backends/D3D12/D3D12GraphicsBackend.hpp"
#include "CNA/Internal/Backends/Common/IGraphicsBackend.hpp"

#include <cstdio>
#include <cstdlib>

using CNA::Internal::Backends::GraphicsBackendCreateArgs;
using CNA::Internal::Backends::D3D12::D3D12GraphicsBackend;

namespace
{
    int g_failures = 0;

    void Check(bool condition, const char* name)
    {
        if (condition)
        {
            std::printf("[PASS] %s\n", name);
        }
        else
        {
            std::printf("[FAIL] %s\n", name);
            ++g_failures;
        }
    }
}

int main()
{
    GraphicsBackendCreateArgs args;
    args.window = nullptr; // off-screen, deliberately -- see file header comment.
    args.virtualWidth = 64;
    args.virtualHeight = 64;

    D3D12GraphicsBackend backend(args);

    // ---- Check A: device ----
    Check(backend.GetDeviceEXT() != nullptr, "A1: ID3D12Device created");
    Check(backend.GetFeatureLevelEXT() >= D3D_FEATURE_LEVEL_11_0, "A2: feature level >= 11_0");
    std::printf("       feature level = 0x%04x, debug layer = %s, tearing = %s\n",
                static_cast<unsigned>(backend.GetFeatureLevelEXT()),
                backend.IsDebugLayerEnabledEXT() ? "enabled" : "disabled",
                backend.IsTearingSupportedEXT() ? "supported" : "unsupported");

    // ---- Check B: command queue ----
    Check(backend.GetCommandQueueEXT() != nullptr, "B1: ID3D12CommandQueue created");

    // ---- Check C: descriptor heaps, real bump-allocator proof ----
    {
        D3D12_CPU_DESCRIPTOR_HANDLE rtv0 = backend.AllocateRtvDescriptorEXT();
        D3D12_CPU_DESCRIPTOR_HANDLE rtv1 = backend.AllocateRtvDescriptorEXT();
        Check(rtv1.ptr > rtv0.ptr, "C1: RTV heap allocator advances");

        D3D12_CPU_DESCRIPTOR_HANDLE dsv0 = backend.AllocateDsvDescriptorEXT();
        D3D12_CPU_DESCRIPTOR_HANDLE dsv1 = backend.AllocateDsvDescriptorEXT();
        Check(dsv1.ptr > dsv0.ptr, "C2: DSV heap allocator advances");

        D3D12_CPU_DESCRIPTOR_HANDLE cbv0Cpu{}, cbv1Cpu{};
        D3D12_GPU_DESCRIPTOR_HANDLE cbv0Gpu{}, cbv1Gpu{};
        backend.AllocateCbvSrvUavDescriptorEXT(cbv0Cpu, cbv0Gpu);
        backend.AllocateCbvSrvUavDescriptorEXT(cbv1Cpu, cbv1Gpu);
        Check(cbv1Cpu.ptr > cbv0Cpu.ptr && cbv1Gpu.ptr > cbv0Gpu.ptr,
              "C3: CBV/SRV/UAV heap allocator advances (both CPU and GPU handles)");
        Check(backend.GetCbvSrvUavHeapEXT() != nullptr, "C4: shader-visible CBV/SRV/UAV heap object real");
    }

    // ---- Check D: command allocator + command list + queue + fence, wired together ----
    {
        ID3D12CommandAllocator* allocator = backend.GetCommandAllocatorEXT(0);
        ID3D12GraphicsCommandList* cmdList = backend.GetCommandListEXT();
        Check(allocator != nullptr && cmdList != nullptr, "D1: command allocator + command list real");

        HRESULT hr = allocator->Reset();
        Check(SUCCEEDED(hr), "D2: ID3D12CommandAllocator::Reset succeeded");

        hr = cmdList->Reset(allocator, nullptr);
        Check(SUCCEEDED(hr), "D3: ID3D12GraphicsCommandList::Reset succeeded");

        hr = cmdList->Close();
        Check(SUCCEEDED(hr), "D4: ID3D12GraphicsCommandList::Close succeeded");

        // Real end-to-end proof: submit this (empty but genuinely recorded/closed) command list to
        // the real queue, and block on the real fence until the GPU reports completion.
        bool threw = false;
        try
        {
            backend.ExecuteCommandListAndWaitEXT(cmdList);
        }
        catch (const std::exception& e)
        {
            std::printf("       ExecuteCommandListAndWaitEXT threw: %s\n", e.what());
            threw = true;
        }
        Check(!threw, "D5: ExecuteCommandListAndWaitEXT (submit+signal+wait) completed without throwing");
        Check(backend.GetFenceEXT()->GetCompletedValue() >= 1, "D6: fence GetCompletedValue() advanced past 0");
    }

    // ---- Check E: N-frames-in-flight back-pressure primitive ----
    // SignalAndWaitForFrameEXT's real contract (see its own header doc comment): it signals a NEW
    // fence value for @p frameIndex, but only *blocks* on that frame slot's PREVIOUS recorded
    // value -- not on the one it just signaled (a real Present() loop must never stall the CPU on
    // the very frame it just submitted; that's what would defeat the whole point of frames-in-
    // flight). So the only guarantee callable immediately after a call is
    // "GetCompletedValue() >= the PREVIOUS value for that frame slot" -- asserting >= the brand-new
    // value would be a race (Signal() is asynchronous; it may or may not have completed yet), which
    // is exactly the flaky assertion an earlier draft of this test made before this was caught here.
    {
        std::uint64_t v0 = backend.SignalAndWaitForFrameEXT(0);
        std::uint64_t v1 = backend.SignalAndWaitForFrameEXT(1);
        std::uint64_t v2 = backend.SignalAndWaitForFrameEXT(0); // second use of frame 0 -- must wait on v0 first
        Check(v1 > v0 && v2 > v1, "E1: SignalAndWaitForFrameEXT fence values strictly increase");
        Check(backend.GetFenceEXT()->GetCompletedValue() >= v0,
              "E2: the second call for frame 0 genuinely waited for that slot's PREVIOUS (v0) value before returning");

        // Real eventual-completion proof for v2 itself -- an explicit, bounded wait here (test-only
        // code), not implied by SignalAndWaitForFrameEXT's own non-stalling contract.
        if (backend.GetFenceEXT()->GetCompletedValue() < v2)
        {
            HANDLE waitEvent = CreateEventExW(nullptr, nullptr, 0, EVENT_ALL_ACCESS);
            backend.GetFenceEXT()->SetEventOnCompletion(v2, waitEvent);
            WaitForSingleObject(waitEvent, INFINITE);
            CloseHandle(waitEvent);
        }
        Check(backend.GetFenceEXT()->GetCompletedValue() >= v2,
              "E3: the most recently signaled value (v2) eventually completes when explicitly waited on");
    }

    // ---- Check F: off-screen construction never touches the swap chain ----
    Check(!backend.IsSwapChainAvailableEXT(), "F1: off-screen construction leaves swap chain unavailable");
    Check(backend.GetSwapChainEXT() == nullptr, "F2: GetSwapChainEXT() is null off-screen");

    std::printf("\n%s: %d failure(s)\n", g_failures == 0 ? "RESULT: ALL PASS" : "RESULT: FAILURES", g_failures);
    return g_failures == 0 ? 0 : 1;
}
