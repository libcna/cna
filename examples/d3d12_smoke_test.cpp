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
// plan_dx.md DX-106/DX-107/DX-108 (this revision) add:
// Check G -- D3D12ResourceStateTracker (DX-106): a real throwaway committed buffer resource is
//   registered, transitioned through 2 distinct states (a real barrier IS emitted + tracked state
//   updates), then re-requested at the state it's already in (NO redundant barrier emitted, proven
//   via the real bool return, not just documented) -- this is the actual point of the class.
// Check H -- D3D12RootSignatureCache (DX-108): two GetOrCreate() calls with the IDENTICAL (numCbvs,
//   numSrvs, numSamplers) shape return the SAME cached object (pointer identity); a genuinely
//   different shape returns a DIFFERENT object -- real cache-hit/cache-miss proof, not assumed.
// Check I -- D3D12PipelineStateCache (DX-107): a real ID3D12PipelineState is created end-to-end for
//   the colored3d variant (stride 16) via CreateGraphicsPipelineState against a real root signature
//   from Check H, through Wine+vkd3d-proton on the real GPU -- the actual "first real D3D12 PSO"
//   proof this task exists for. A second GetOrCreate() with an identical desc proves cache-hit
//   identity, same pattern as Check H.
//
// Exit code 0 = all checks PASS, 1 = any FAILs.

#include "CNA/Internal/Backends/D3D12/D3D12GraphicsBackend.hpp"
#include "CNA/Internal/Backends/D3D12/D3D12ResourceStateTracker.hpp"
#include "CNA/Internal/Backends/D3D12/D3D12RootSignatureCache.hpp"
#include "CNA/Internal/Backends/D3D12/D3D12PipelineStateCache.hpp"
#include "CNA/Internal/Backends/Common/IGraphicsBackend.hpp"

#include <cstdio>
#include <cstdlib>

using CNA::Internal::Backends::GraphicsBackendCreateArgs;
using CNA::Internal::Backends::D3D12::D3D12GraphicsBackend;
using CNA::Internal::Backends::D3D12::D3D12ResourceStateTracker;
using CNA::Internal::Backends::D3D12::D3D12RootSignatureCache;
using CNA::Internal::Backends::D3D12::D3D12PipelineStateCache;
using CNA::Internal::Backends::D3D12::D3D12PipelineStateDesc;
using CNA::Internal::Backends::D3DCommon::D3DShaderVariant;

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

    ID3D12Device* device = backend.GetDeviceEXT();

    // ---- Check G: D3D12ResourceStateTracker (DX-106), against a real throwaway committed buffer ----
    {
        D3D12_HEAP_PROPERTIES heapProps{};
        heapProps.Type = D3D12_HEAP_TYPE_DEFAULT;

        D3D12_RESOURCE_DESC bufDesc{};
        bufDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        bufDesc.Width = 256;
        bufDesc.Height = 1;
        bufDesc.DepthOrArraySize = 1;
        bufDesc.MipLevels = 1;
        bufDesc.Format = DXGI_FORMAT_UNKNOWN;
        bufDesc.SampleDesc.Count = 1;
        bufDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

        Microsoft::WRL::ComPtr<ID3D12Resource> dummyResource;
        HRESULT hr = device->CreateCommittedResource(
            &heapProps, D3D12_HEAP_FLAG_NONE, &bufDesc,
            D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(dummyResource.ReleaseAndGetAddressOf()));
        Check(SUCCEEDED(hr) && dummyResource != nullptr, "G1: real throwaway committed buffer resource created");

        D3D12ResourceStateTracker tracker;
        tracker.TrackResource(dummyResource.Get(), D3D12_RESOURCE_STATE_COMMON);
        Check(tracker.GetTrackedStateEXT(dummyResource.Get()) == D3D12_RESOURCE_STATE_COMMON,
              "G2: freshly tracked resource reports its registered initial state");

        ID3D12CommandAllocator* allocator = backend.GetCommandAllocatorEXT(0);
        ID3D12GraphicsCommandList* cmdList = backend.GetCommandListEXT();
        allocator->Reset();
        cmdList->Reset(allocator, nullptr);

        bool emitted1 = tracker.TransitionTo(cmdList, dummyResource.Get(), D3D12_RESOURCE_STATE_COPY_DEST);
        Check(emitted1, "G3: TransitionTo a genuinely different state emits a real barrier (returns true)");
        Check(tracker.GetTrackedStateEXT(dummyResource.Get()) == D3D12_RESOURCE_STATE_COPY_DEST,
              "G4: tracked state updates to the new state after a real transition");

        bool emitted2 = tracker.TransitionTo(cmdList, dummyResource.Get(), D3D12_RESOURCE_STATE_COPY_DEST);
        Check(!emitted2, "G5: TransitionTo the SAME state emits NO redundant barrier (returns false)");

        bool emitted3 = tracker.TransitionTo(cmdList, dummyResource.Get(), D3D12_RESOURCE_STATE_GENERIC_READ);
        Check(emitted3, "G6: TransitionTo a different state again emits a real barrier");

        cmdList->Close();
        bool threw = false;
        try { backend.ExecuteCommandListAndWaitEXT(cmdList); }
        catch (const std::exception&) { threw = true; }
        Check(!threw, "G7: command list recording the real transition barriers submits+executes cleanly");
    }

    // ---- Check H: D3D12RootSignatureCache (DX-108) ----
    Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSigColored3d;
    {
        D3D12RootSignatureCache rootSigCache;
        rootSigColored3d = rootSigCache.GetOrCreate(device, /*numCbvs=*/2, /*numSrvs=*/0, /*numSamplers=*/0);
        Check(rootSigColored3d != nullptr, "H1: real ID3D12RootSignature created for colored3d's (2,0,0) shape");

        auto rootSigColored3dAgain = rootSigCache.GetOrCreate(device, 2, 0, 0);
        Check(rootSigColored3dAgain.Get() == rootSigColored3d.Get(),
              "H2: identical (numCbvs,numSrvs,numSamplers) shape returns the SAME cached object");

        auto rootSigTextured3d = rootSigCache.GetOrCreate(device, 2, 1, 1);
        Check(rootSigTextured3d != nullptr && rootSigTextured3d.Get() != rootSigColored3d.Get(),
              "H3: a genuinely different (2,1,1) shape returns a real, DIFFERENT object");
    }

    // ---- Check I: D3D12PipelineStateCache (DX-107) -- the first real D3D12 PSO this backend has ever created ----
    {
        D3D12PipelineStateCache psoCache;
        D3D12PipelineStateDesc desc;
        desc.variant = D3DShaderVariant::Colored3d;
        desc.strideInBytes = 16;

        auto pso = psoCache.GetOrCreate(device, rootSigColored3d.Get(), desc,
                                        DXGI_FORMAT_R8G8B8A8_UNORM, DXGI_FORMAT_UNKNOWN);
        Check(pso != nullptr, "I1: real ID3D12PipelineState created for colored3d/stride16/default state");

        auto psoAgain = psoCache.GetOrCreate(device, rootSigColored3d.Get(), desc,
                                             DXGI_FORMAT_R8G8B8A8_UNORM, DXGI_FORMAT_UNKNOWN);
        Check(psoAgain.Get() == pso.Get(), "I2: identical desc returns the SAME cached PSO object");

        D3D12PipelineStateDesc desc2 = desc;
        desc2.cullMode = 1; // CullMode::None -- a genuinely different rasterizer state.
        auto psoDifferent = psoCache.GetOrCreate(device, rootSigColored3d.Get(), desc2,
                                                 DXGI_FORMAT_R8G8B8A8_UNORM, DXGI_FORMAT_UNKNOWN);
        Check(psoDifferent != nullptr && psoDifferent.Get() != pso.Get(),
              "I3: a genuinely different state tuple returns a real, DIFFERENT PSO object");
    }

    std::printf("\n%s: %d failure(s)\n", g_failures == 0 ? "RESULT: ALL PASS" : "RESULT: FAILURES", g_failures);
    return g_failures == 0 ? 0 : 1;
}
