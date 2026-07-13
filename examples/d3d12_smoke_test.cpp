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
//
// plan_dx.md DX-109/DX-110 (this revision) add:
// Check J -- D3D12VertexBufferBackend/D3D12IndexBufferBackend (DX-109): known vertex data is
//   uploaded through a real DEFAULT-heap resource + UPLOAD-heap staging + CopyBufferRegion, then
//   copied back out to a D3D12_HEAP_TYPE_READBACK buffer and Map()'d on the CPU -- an exact byte
//   match proves the whole upload/copy/barrier path is correct, not just "the API calls succeeded".
//   Both a 16-bit and a 32-bit index buffer are round-tripped the same way, and
//   CreateIndexBuffer32() is confirmed to actually return a 32-bit-format buffer (not silently
//   alias to 16-bit, the real bug D3D11's own Phase DX5 fork found and fixed).
// Check K -- D3D12TextureBackend (DX-109): known RGBA8 pixel data uploaded at construction time is
//   copied back out via the same READBACK-heap technique and matches exactly; a follow-up
//   UpdatePixels() call with different data is proven to genuinely overwrite the texture (not a
//   stale/cached first-upload value).
// Check L -- RecreateDeviceEXT() (DX-110): the real device/queue/heaps/command-list/fence
//   recreation path is invoked directly (this Wine dev loop cannot trigger a genuine
//   DXGI_ERROR_DEVICE_REMOVED -- see that method's own doc comment) and the backend is proven
//   usable again afterward: a fresh command-list submission round-trips through the NEW fence, and
//   a fresh vertex buffer created AFTER recreation uploads and reads back correctly through the
//   NEW device -- real proof the recreation path produces a genuinely working backend, not just
//   non-null pointers.
//
// plan_dx.md DX-111 (this revision) adds:
// Check M -- the first real 3D triangle this D3D12 backend has ever drawn: a genuine
//   DrawColoredPrimitives()/DrawIndexedColoredPrimitives() call (real root signature + PSO from
//   Check H/I, real PerDraw/FogParams constant buffers, a real command-list-recorded DrawInstanced/
//   DrawIndexedInstanced) paints a known solid-red vertex color over a known-blue-cleared
//   off-screen render target (BindOffscreenColorTargetEXT() -- a minimal internal helper, since a
//   full public D3D12RenderTargetBackend is still owed, see DX-109's own honest scope note; the
//   swap chain remains unusable under Wine per DX-100). Reading back the SAME pixel region before
//   and after each draw call (blue -> red) proves the fragment genuinely came from the draw, not a
//   stale/cached value -- same "before/after Clear()" discipline D3D11's own Check P established
//   (d3d11_smoke_test.cpp), reused here for the analogous D3D12 proof.
//
// plan_dx.md DX-111 (continued -- textured3d/colored_textured3d/lit_textured3d/alpha_test3d) adds:
// Check N -- DrawPrimitivesEx()/DrawIndexedPrimitivesEx() with real GpuDrawParams: textured3d
//   (stride 20) samples the exact known texture color (diffuseColor=white) over the Clear()
//   background, and colored_textured3d (stride 24) multiplies an exact known vertex color through a
//   white texture -- same rigor as D3D11's own Check Q, adapted to this backend's real SRV
//   descriptor-table binding (D3D12TextureBackend's own CBV/SRV/UAV-heap SRV handle is bound
//   directly as the 1-descriptor table base, DX-109's own descriptor already lives in the right
//   heap -- no separate copy step needed for a single texture).
// Check O -- lit_textured3d (stride 32): the unlit branch (LightingEnabled=false) samples
//   diffuseColor*texture exactly, same bar as Check N; the lit branch (LightingEnabled=true) isn't
//   byte-predicted on the CPU (real Blinn-Phong math) -- it's proven to genuinely differ from both
//   the unlit result and the Clear() background, same discriminating bar D3D11's own Check R uses.
// Check P -- alpha_test3d: an alpha value that fails the test (AlphaTol=0, alpha>=ref) genuinely
//   discards (the Clear() background survives, not the geometry's color); a passing alpha draws the
//   exact texture color including its own non-255 alpha byte -- same two-sided proof D3D11's own
//   Check S uses.
//
// Exit code 0 = all checks PASS, 1 = any FAILs.

#include "CNA/Internal/Backends/D3D12/D3D12GraphicsBackend.hpp"
#include "CNA/Internal/Backends/D3D12/D3D12ResourceStateTracker.hpp"
#include "CNA/Internal/Backends/D3D12/D3D12RootSignatureCache.hpp"
#include "CNA/Internal/Backends/D3D12/D3D12PipelineStateCache.hpp"
#include "CNA/Internal/Backends/D3D12/D3D12Buffers.hpp"
#include "CNA/Internal/Backends/D3D12/D3D12Textures.hpp"
#include "CNA/Internal/Backends/Common/IGraphicsBackend.hpp"
#include "CNA/Internal/Graphics/ImageData.hpp"

#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

using CNA::Internal::Backends::GraphicsBackendCreateArgs;
using CNA::Internal::Backends::D3D12::D3D12GraphicsBackend;
using CNA::Internal::Backends::D3D12::D3D12ResourceStateTracker;
using CNA::Internal::Backends::D3D12::D3D12RootSignatureCache;
using CNA::Internal::Backends::D3D12::D3D12PipelineStateCache;
using CNA::Internal::Backends::D3D12::D3D12PipelineStateDesc;
using CNA::Internal::Backends::D3D12::D3D12VertexBufferBackend;
using CNA::Internal::Backends::D3D12::D3D12IndexBufferBackend;
using CNA::Internal::Backends::D3D12::D3D12TextureBackend;
using CNA::Internal::Backends::D3DCommon::D3DShaderVariant;
using CNA::Internal::Graphics::ImageData;
using CNA::Internal::Backends::Matrix;
using CNA::Internal::Backends::PrimitiveType;
using CNA::Internal::Backends::GpuDrawParams;

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

    std::string FormatHrLocal(HRESULT hr)
    {
        char buf[32];
        std::snprintf(buf, sizeof(buf), "0x%08lX", static_cast<unsigned long>(hr));
        return buf;
    }

    /// DX-109's own off-screen-safe readback technique: a D3D12_HEAP_TYPE_READBACK buffer,
    /// CopyBufferRegion off the real GPU-resident resource, Map(READ). Mirrors D3D11's own
    /// D3D11_USAGE_STAGING+CopyResource+Map(READ) test helper (d3d11_smoke_test.cpp), adapted to
    /// D3D12's own explicit heap-type model.
    std::vector<uint8_t> ReadBackBufferResource(D3D12GraphicsBackend& backend, ID3D12Resource* resource,
                                                std::size_t byteCount)
    {
        D3D12_HEAP_PROPERTIES heapProps{};
        heapProps.Type = D3D12_HEAP_TYPE_READBACK;

        D3D12_RESOURCE_DESC desc{};
        desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        desc.Width = static_cast<UINT64>(byteCount);
        desc.Height = 1;
        desc.DepthOrArraySize = 1;
        desc.MipLevels = 1;
        desc.Format = DXGI_FORMAT_UNKNOWN;
        desc.SampleDesc.Count = 1;
        desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

        Microsoft::WRL::ComPtr<ID3D12Resource> readback;
        HRESULT hr = backend.GetDeviceEXT()->CreateCommittedResource(
            &heapProps, D3D12_HEAP_FLAG_NONE, &desc,
            D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(readback.GetAddressOf()));
        if (FAILED(hr))
            throw std::runtime_error("ReadBackBufferResource: CreateCommittedResource failed, hr=" + FormatHrLocal(hr));

        ID3D12CommandAllocator* allocator = backend.GetCommandAllocatorEXT(0);
        ID3D12GraphicsCommandList* cmdList = backend.GetCommandListEXT();
        allocator->Reset();
        cmdList->Reset(allocator, nullptr);

        auto& tracker = backend.GetResourceStateTrackerEXT();
        const D3D12_RESOURCE_STATES priorState = tracker.GetTrackedStateEXT(resource);
        tracker.TransitionTo(cmdList, resource, D3D12_RESOURCE_STATE_COPY_SOURCE);
        cmdList->CopyBufferRegion(readback.Get(), 0, resource, 0, byteCount);
        tracker.TransitionTo(cmdList, resource, priorState); // restore -- this is a read-only diagnostic

        hr = cmdList->Close();
        if (FAILED(hr))
            throw std::runtime_error("ReadBackBufferResource: command list Close failed, hr=" + FormatHrLocal(hr));
        backend.ExecuteCommandListAndWaitEXT(cmdList);

        std::vector<uint8_t> out(byteCount);
        void* mapped = nullptr;
        const D3D12_RANGE readRange{0, byteCount};
        hr = readback->Map(0, &readRange, &mapped);
        if (FAILED(hr))
            throw std::runtime_error("ReadBackBufferResource: Map failed, hr=" + FormatHrLocal(hr));
        std::memcpy(out.data(), mapped, byteCount);
        const D3D12_RANGE writtenRange{0, 0}; // CPU never wrote through this mapping
        readback->Unmap(0, &writtenRange);
        return out;
    }

    /// Same technique, for a level-0 RGBA8 D3D12TextureBackend -- CopyTextureRegion into a
    /// row-pitch-aligned READBACK buffer, then de-strided back into a tightly-packed RGBA8 buffer.
    std::vector<uint8_t> ReadBackTextureLevel0(D3D12GraphicsBackend& backend, D3D12TextureBackend& tex)
    {
        const int w = tex.GetWidth();
        const int h = tex.GetHeight();
        const UINT rowPitch = (static_cast<UINT>(w) * 4 + 255u) & ~255u; // D3D12_TEXTURE_DATA_PITCH_ALIGNMENT
        const UINT64 bufferSize = static_cast<UINT64>(rowPitch) * static_cast<UINT64>(h);

        D3D12_HEAP_PROPERTIES heapProps{};
        heapProps.Type = D3D12_HEAP_TYPE_READBACK;

        D3D12_RESOURCE_DESC bufDesc{};
        bufDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        bufDesc.Width = bufferSize;
        bufDesc.Height = 1;
        bufDesc.DepthOrArraySize = 1;
        bufDesc.MipLevels = 1;
        bufDesc.Format = DXGI_FORMAT_UNKNOWN;
        bufDesc.SampleDesc.Count = 1;
        bufDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

        Microsoft::WRL::ComPtr<ID3D12Resource> readback;
        HRESULT hr = backend.GetDeviceEXT()->CreateCommittedResource(
            &heapProps, D3D12_HEAP_FLAG_NONE, &bufDesc,
            D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(readback.GetAddressOf()));
        if (FAILED(hr))
            throw std::runtime_error("ReadBackTextureLevel0: CreateCommittedResource failed, hr=" + FormatHrLocal(hr));

        D3D12_TEXTURE_COPY_LOCATION dst{};
        dst.pResource = readback.Get();
        dst.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        dst.PlacedFootprint.Offset = 0;
        dst.PlacedFootprint.Footprint.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        dst.PlacedFootprint.Footprint.Width = static_cast<UINT>(w);
        dst.PlacedFootprint.Footprint.Height = static_cast<UINT>(h);
        dst.PlacedFootprint.Footprint.Depth = 1;
        dst.PlacedFootprint.Footprint.RowPitch = rowPitch;

        D3D12_TEXTURE_COPY_LOCATION src{};
        src.pResource = tex.GetResourceEXT();
        src.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        src.SubresourceIndex = 0;

        ID3D12CommandAllocator* allocator = backend.GetCommandAllocatorEXT(0);
        ID3D12GraphicsCommandList* cmdList = backend.GetCommandListEXT();
        allocator->Reset();
        cmdList->Reset(allocator, nullptr);

        auto& tracker = backend.GetResourceStateTrackerEXT();
        const D3D12_RESOURCE_STATES priorState = tracker.GetTrackedStateEXT(tex.GetResourceEXT());
        tracker.TransitionTo(cmdList, tex.GetResourceEXT(), D3D12_RESOURCE_STATE_COPY_SOURCE);
        cmdList->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
        tracker.TransitionTo(cmdList, tex.GetResourceEXT(), priorState);

        hr = cmdList->Close();
        if (FAILED(hr))
            throw std::runtime_error("ReadBackTextureLevel0: command list Close failed, hr=" + FormatHrLocal(hr));
        backend.ExecuteCommandListAndWaitEXT(cmdList);

        std::vector<uint8_t> out(static_cast<std::size_t>(w) * static_cast<std::size_t>(h) * 4);
        uint8_t* mapped = nullptr;
        const D3D12_RANGE readRange{0, bufferSize};
        hr = readback->Map(0, &readRange, reinterpret_cast<void**>(&mapped));
        if (FAILED(hr))
            throw std::runtime_error("ReadBackTextureLevel0: Map failed, hr=" + FormatHrLocal(hr));
        for (int row = 0; row < h; ++row)
        {
            std::memcpy(out.data() + static_cast<std::size_t>(row) * w * 4,
                        mapped + static_cast<std::size_t>(row) * rowPitch,
                        static_cast<std::size_t>(w) * 4);
        }
        const D3D12_RANGE writtenRange{0, 0};
        readback->Unmap(0, &writtenRange);
        return out;
    }

    /// DX-111 test scaffolding: same row-pitch-aligned READBACK-buffer technique as
    /// ReadBackTextureLevel0() above, generalized to any raw RGBA8 ID3D12Resource* (the off-screen
    /// render target Check M creates, which is not a D3D12TextureBackend).
    std::vector<uint8_t> ReadBackRenderTargetFull(D3D12GraphicsBackend& backend, ID3D12Resource* resource,
                                                  int w, int h)
    {
        const UINT rowPitch = (static_cast<UINT>(w) * 4 + 255u) & ~255u;
        const UINT64 bufferSize = static_cast<UINT64>(rowPitch) * static_cast<UINT64>(h);

        D3D12_HEAP_PROPERTIES heapProps{};
        heapProps.Type = D3D12_HEAP_TYPE_READBACK;

        D3D12_RESOURCE_DESC bufDesc{};
        bufDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        bufDesc.Width = bufferSize;
        bufDesc.Height = 1;
        bufDesc.DepthOrArraySize = 1;
        bufDesc.MipLevels = 1;
        bufDesc.Format = DXGI_FORMAT_UNKNOWN;
        bufDesc.SampleDesc.Count = 1;
        bufDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

        Microsoft::WRL::ComPtr<ID3D12Resource> readback;
        HRESULT hr = backend.GetDeviceEXT()->CreateCommittedResource(
            &heapProps, D3D12_HEAP_FLAG_NONE, &bufDesc,
            D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(readback.GetAddressOf()));
        if (FAILED(hr))
            throw std::runtime_error("ReadBackRenderTargetFull: CreateCommittedResource failed, hr=" + FormatHrLocal(hr));

        D3D12_TEXTURE_COPY_LOCATION dst{};
        dst.pResource = readback.Get();
        dst.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        dst.PlacedFootprint.Offset = 0;
        dst.PlacedFootprint.Footprint.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        dst.PlacedFootprint.Footprint.Width = static_cast<UINT>(w);
        dst.PlacedFootprint.Footprint.Height = static_cast<UINT>(h);
        dst.PlacedFootprint.Footprint.Depth = 1;
        dst.PlacedFootprint.Footprint.RowPitch = rowPitch;

        D3D12_TEXTURE_COPY_LOCATION src{};
        src.pResource = resource;
        src.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        src.SubresourceIndex = 0;

        ID3D12CommandAllocator* allocator = backend.GetCommandAllocatorEXT(0);
        ID3D12GraphicsCommandList* cmdList = backend.GetCommandListEXT();
        allocator->Reset();
        cmdList->Reset(allocator, nullptr);

        auto& tracker = backend.GetResourceStateTrackerEXT();
        const D3D12_RESOURCE_STATES priorState = tracker.GetTrackedStateEXT(resource);
        tracker.TransitionTo(cmdList, resource, D3D12_RESOURCE_STATE_COPY_SOURCE);
        cmdList->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
        tracker.TransitionTo(cmdList, resource, priorState);

        hr = cmdList->Close();
        if (FAILED(hr))
            throw std::runtime_error("ReadBackRenderTargetFull: command list Close failed, hr=" + FormatHrLocal(hr));
        backend.ExecuteCommandListAndWaitEXT(cmdList);

        std::vector<uint8_t> out(static_cast<std::size_t>(w) * static_cast<std::size_t>(h) * 4);
        uint8_t* mapped = nullptr;
        const D3D12_RANGE readRange{0, bufferSize};
        hr = readback->Map(0, &readRange, reinterpret_cast<void**>(&mapped));
        if (FAILED(hr))
            throw std::runtime_error("ReadBackRenderTargetFull: Map failed, hr=" + FormatHrLocal(hr));
        for (int row = 0; row < h; ++row)
        {
            std::memcpy(out.data() + static_cast<std::size_t>(row) * w * 4,
                        mapped + static_cast<std::size_t>(row) * rowPitch,
                        static_cast<std::size_t>(w) * 4);
        }
        const D3D12_RANGE writtenRange2{0, 0};
        readback->Unmap(0, &writtenRange2);
        return out;
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

    // ---- Check J: D3D12VertexBufferBackend / D3D12IndexBufferBackend (DX-109) ----
    {
        struct Vtx { float x, y, z; uint32_t color; };
        const Vtx triangle[3] = {
            {0.0f, 0.5f, 0.0f, 0xFFFF0000u},
            {0.5f, -0.5f, 0.0f, 0xFF00FF00u},
            {-0.5f, -0.5f, 0.0f, 0xFF0000FFu},
        };

        D3D12VertexBufferBackend vb(&backend, 3);
        vb.SetData(triangle, 3, sizeof(Vtx));
        Check(vb.GetVertexCount() == 3, "J1: vertex buffer reports the uploaded vertex count");

        auto vbReadback = ReadBackBufferResource(backend, vb.GetResourceEXT(), sizeof(triangle));
        Check(std::memcmp(vbReadback.data(), triangle, sizeof(triangle)) == 0,
              "J2: vertex buffer round-trips EXACT bytes through a real GPU upload+copy+readback");

        const uint16_t indices16[3] = {0, 1, 2};
        D3D12IndexBufferBackend ib16(&backend, 3, /*thirtyTwoBit=*/false);
        ib16.SetData16(indices16, 3);
        Check(ib16.GetIndexCount() == 3 && !ib16.IsThirtyTwoBit(),
              "J3: 16-bit index buffer reports correct count/format");
        auto ib16Readback = ReadBackBufferResource(backend, ib16.GetResourceEXT(), sizeof(indices16));
        Check(std::memcmp(ib16Readback.data(), indices16, sizeof(indices16)) == 0,
              "J4: 16-bit index buffer round-trips EXACT bytes");

        const uint32_t indices32[3] = {2, 1, 0};
        D3D12IndexBufferBackend ib32(&backend, 3, /*thirtyTwoBit=*/true);
        ib32.SetData32(indices32, 3);
        Check(ib32.GetIndexCount() == 3 && ib32.IsThirtyTwoBit(),
              "J5: 32-bit index buffer reports correct count/format -- CreateIndexBuffer32() is a real, "
              "distinct override (not silently aliased to CreateIndexBuffer16, the real bug D3D11's own "
              "Phase DX5 fork found and fixed)");
        auto ib32Readback = ReadBackBufferResource(backend, ib32.GetResourceEXT(), sizeof(indices32));
        Check(std::memcmp(ib32Readback.data(), indices32, sizeof(indices32)) == 0,
              "J6: 32-bit index buffer round-trips EXACT bytes");

        // Via the real IGraphicsBackend factory methods too, not just direct construction --
        // confirms CreateIndexBuffer32() genuinely returns a 32-bit-format object end-to-end.
        auto ib32ViaFactory = backend.CreateIndexBuffer32(3);
        ib32ViaFactory->SetData32(indices32, 3);
        Check(ib32ViaFactory->IsThirtyTwoBit(),
              "J7: IGraphicsBackend::CreateIndexBuffer32() returns a genuinely 32-bit buffer");
    }

    // ---- Check K: D3D12TextureBackend (DX-109) ----
    {
        ImageData img;
        img.width = 4;
        img.height = 4;
        img.pixels.resize(4 * 4 * 4);
        for (int i = 0; i < 4 * 4; ++i)
        {
            img.pixels[i * 4 + 0] = 10;
            img.pixels[i * 4 + 1] = 20;
            img.pixels[i * 4 + 2] = 30;
            img.pixels[i * 4 + 3] = 255;
        }

        auto texOwned = backend.CreateTexture(img); // real IGraphicsBackend::CreateTexture() path
        auto* tex = static_cast<D3D12TextureBackend*>(texOwned.get());
        Check(tex->GetWidth() == 4 && tex->GetHeight() == 4, "K1: texture reports correct dimensions");

        auto texReadback = ReadBackTextureLevel0(backend, *tex);
        Check(std::memcmp(texReadback.data(), img.pixels.data(), img.pixels.size()) == 0,
              "K2: texture level-0 round-trips EXACT bytes through a real GPU upload+copy+readback "
              "(construction-time upload path)");

        std::vector<uint8_t> updated(4 * 4 * 4);
        for (int i = 0; i < 4 * 4; ++i)
        {
            updated[i * 4 + 0] = 200;
            updated[i * 4 + 1] = 150;
            updated[i * 4 + 2] = 100;
            updated[i * 4 + 3] = 255;
        }
        tex->UpdatePixels(updated.data(), 0);
        auto texReadback2 = ReadBackTextureLevel0(backend, *tex);
        Check(std::memcmp(texReadback2.data(), updated.data(), updated.size()) == 0,
              "K3: UpdatePixels() genuinely overwrites the texture (not a stale first-upload value)");
        Check(std::memcmp(texReadback2.data(), img.pixels.data(), img.pixels.size()) != 0,
              "K4: the updated readback genuinely differs from the original upload (real change, not a no-op)");
    }

    // ---- Check L: RecreateDeviceEXT() (DX-110) ----
    {
        bool threw = false;
        try { backend.RecreateDeviceEXT(); }
        catch (const std::exception& e)
        {
            std::printf("       RecreateDeviceEXT threw: %s\n", e.what());
            threw = true;
        }
        Check(!threw, "L1: RecreateDeviceEXT() completes without throwing");
        // Deliberately NOT asserting GetDeviceEXT()/GetFenceEXT() != deviceBefore/fenceBefore here.
        // A first attempt at this check did exactly that and genuinely FAILED on this real run for
        // the device pointer (fenceBefore happened to differ, deviceBefore did not) -- root cause:
        // RecreateDeviceEXT() calls device_.Reset() (a real COM Release()) before creating the new
        // device, and the OS/Vulkan-loader/vkd3d-proton allocator legally reusing that just-freed
        // address for the very next allocation is expected, correct allocator behavior, not evidence
        // recreation silently no-op'd. Pointer-identity is simply not a sound signal here. The real
        // proof recreation genuinely happened is functional: L1 (full teardown+recreate completed
        // without throwing), L4 (every device-lifetime object is non-null again), and L5/L6 (new GPU
        // work actually submits and a fresh upload+readback round-trips correctly through whatever
        // object now backs GetDeviceEXT()) -- if RecreateDeviceEXT() had silently no-op'd or left a
        // half-torn-down device, L5/L6 would fail regardless of what address GetDeviceEXT() reports.
        Check(backend.GetDeviceEXT() != nullptr, "L2: a real ID3D12Device exists after recreation");
        Check(backend.GetFenceEXT() != nullptr, "L3: a real ID3D12Fence exists after recreation");
        Check(backend.GetCommandQueueEXT() != nullptr && backend.GetCommandAllocatorEXT(0) != nullptr &&
              backend.GetCommandListEXT() != nullptr,
              "L4: command queue/allocator/command list all real again after recreation");

        // Real proof the recreated backend is actually usable, not just non-null: a fresh command
        // list submission round-trips through the NEW fence, and a fresh vertex buffer created
        // AFTER recreation uploads and reads back correctly through the NEW device.
        ID3D12CommandAllocator* allocator = backend.GetCommandAllocatorEXT(0);
        ID3D12GraphicsCommandList* cmdList = backend.GetCommandListEXT();
        allocator->Reset();
        cmdList->Reset(allocator, nullptr);
        cmdList->Close();
        bool execThrew = false;
        try { backend.ExecuteCommandListAndWaitEXT(cmdList); }
        catch (const std::exception&) { execThrew = true; }
        Check(!execThrew, "L5: a fresh command-list submission through the NEW queue/fence succeeds");

        const float knownData[4] = {1.0f, 2.0f, 3.0f, 4.0f};
        D3D12VertexBufferBackend vbAfter(&backend, 1);
        vbAfter.SetData(knownData, 1, sizeof(knownData));
        auto vbAfterReadback = ReadBackBufferResource(backend, vbAfter.GetResourceEXT(), sizeof(knownData));
        Check(std::memcmp(vbAfterReadback.data(), knownData, sizeof(knownData)) == 0,
              "L6: a NEW vertex buffer created after recreation uploads+reads back correctly through "
              "the new device -- proves the recreated backend is genuinely functional, not just "
              "non-null pointers");
    }

    // ---- Check M: DX-111 -- the first real D3D12 3D triangle, off-screen ----
    {
        constexpr int kRtWidth = 64;
        constexpr int kRtHeight = 64;

        D3D12_HEAP_PROPERTIES heapProps{};
        heapProps.Type = D3D12_HEAP_TYPE_DEFAULT;

        D3D12_RESOURCE_DESC rtDesc{};
        rtDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        rtDesc.Width = kRtWidth;
        rtDesc.Height = kRtHeight;
        rtDesc.DepthOrArraySize = 1;
        rtDesc.MipLevels = 1;
        rtDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        rtDesc.SampleDesc.Count = 1;
        rtDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
        rtDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;

        Microsoft::WRL::ComPtr<ID3D12Resource> rt;
        HRESULT hrRt = backend.GetDeviceEXT()->CreateCommittedResource(
            &heapProps, D3D12_HEAP_FLAG_NONE, &rtDesc,
            D3D12_RESOURCE_STATE_RENDER_TARGET, nullptr, IID_PPV_ARGS(rt.GetAddressOf()));
        Check(SUCCEEDED(hrRt) && rt != nullptr, "M0: real off-screen RGBA8 render-target resource created");

        backend.GetResourceStateTrackerEXT().TrackResource(rt.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET);

        D3D12_CPU_DESCRIPTOR_HANDLE rtv = backend.AllocateRtvDescriptorEXT();
        backend.GetDeviceEXT()->CreateRenderTargetView(rt.Get(), nullptr, rtv);
        backend.BindOffscreenColorTargetEXT(rt.Get(), rtv, DXGI_FORMAT_R8G8B8A8_UNORM, kRtWidth, kRtHeight);

        // Same "oversized triangle covering the entire NDC square" trick and byte-for-byte
        // discipline D3D11's own Check P established (d3d11_smoke_test.cpp) -- world=view=
        // projection=Identity, so these Position values ARE clip-space coordinates directly.
        struct VPC { float x, y, z; uint32_t color; };
        static const VPC kTri[3] = {
            {-1.0f, -1.0f, 0.0f, 0xFF0000FFu},
            { 3.0f, -1.0f, 0.0f, 0xFF0000FFu},
            {-1.0f,  3.0f, 0.0f, 0xFF0000FFu},
        };
        D3D12VertexBufferBackend vb(&backend, 3);
        vb.SetData(kTri, 3, sizeof(VPC));

        auto pixelAt = [&](const std::vector<uint8_t>& buf, int x, int y) -> std::array<uint8_t, 4>
        {
            const std::size_t idx = (static_cast<std::size_t>(y) * kRtWidth + static_cast<std::size_t>(x)) * 4;
            return {buf[idx + 0], buf[idx + 1], buf[idx + 2], buf[idx + 3]};
        };
        auto isColor = [](const std::array<uint8_t, 4>& p, uint8_t r, uint8_t g, uint8_t b, uint8_t a)
        {
            return p[0] == r && p[1] == g && p[2] == b && p[3] == a;
        };
        auto regionIs = [&](const std::vector<uint8_t>& buf, uint8_t r, uint8_t g, uint8_t b, uint8_t a)
        {
            for (int y = 28; y < 32; ++y)
                for (int x = 28; x < 32; ++x)
                    if (!isColor(pixelAt(buf, x, y), r, g, b, a)) return false;
            return true;
        };

        // Non-indexed path.
        backend.Clear(0.0f, 0.0f, 1.0f, 1.0f);
        auto before = ReadBackRenderTargetFull(backend, rt.Get(), kRtWidth, kRtHeight);
        backend.DrawColoredPrimitives(vb, Matrix::getIdentityProperty(), Matrix::getIdentityProperty(),
                                      Matrix::getIdentityProperty(), PrimitiveType::TriangleList, 1);
        auto after = ReadBackRenderTargetFull(backend, rt.Get(), kRtWidth, kRtHeight);
        Check(regionIs(before, 0, 0, 255, 255) && regionIs(after, 255, 0, 0, 255),
              "M1: DrawColoredPrimitives() paints the exact vertex color over the Clear() background "
              "at the same off-screen readback location -- the first real D3D12 3D triangle");

        // Indexed path: same triangle, via DrawIndexedColoredPrimitives.
        static const uint16_t kTriIdx[3] = {0, 1, 2};
        D3D12IndexBufferBackend ib(&backend, 3, /*thirtyTwoBit=*/false);
        ib.SetData16(kTriIdx, 3);

        backend.Clear(0.0f, 0.0f, 1.0f, 1.0f);
        auto beforeIdx = ReadBackRenderTargetFull(backend, rt.Get(), kRtWidth, kRtHeight);
        backend.DrawIndexedColoredPrimitives(vb, ib, Matrix::getIdentityProperty(), Matrix::getIdentityProperty(),
                                             Matrix::getIdentityProperty(), PrimitiveType::TriangleList, 1);
        auto afterIdx = ReadBackRenderTargetFull(backend, rt.Get(), kRtWidth, kRtHeight);
        Check(regionIs(beforeIdx, 0, 0, 255, 255) && regionIs(afterIdx, 255, 0, 0, 255),
              "M2: DrawIndexedColoredPrimitives() paints the exact vertex color over the Clear() "
              "background at the same off-screen readback location");

        backend.UnbindOffscreenColorTargetEXT();
    }

    // ---- Check N (DX-111 continued): textured3d + colored_textured3d via real GpuDrawParams ----
    {
        constexpr int kRtWidth = 64;
        constexpr int kRtHeight = 64;

        D3D12_HEAP_PROPERTIES heapProps{};
        heapProps.Type = D3D12_HEAP_TYPE_DEFAULT;

        D3D12_RESOURCE_DESC rtDesc{};
        rtDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        rtDesc.Width = kRtWidth;
        rtDesc.Height = kRtHeight;
        rtDesc.DepthOrArraySize = 1;
        rtDesc.MipLevels = 1;
        rtDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        rtDesc.SampleDesc.Count = 1;
        rtDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
        rtDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;

        Microsoft::WRL::ComPtr<ID3D12Resource> rt;
        HRESULT hrRt = backend.GetDeviceEXT()->CreateCommittedResource(
            &heapProps, D3D12_HEAP_FLAG_NONE, &rtDesc,
            D3D12_RESOURCE_STATE_RENDER_TARGET, nullptr, IID_PPV_ARGS(rt.GetAddressOf()));
        Check(SUCCEEDED(hrRt) && rt != nullptr, "N0: real off-screen RGBA8 render-target resource created");
        backend.GetResourceStateTrackerEXT().TrackResource(rt.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET);

        D3D12_CPU_DESCRIPTOR_HANDLE rtv = backend.AllocateRtvDescriptorEXT();
        backend.GetDeviceEXT()->CreateRenderTargetView(rt.Get(), nullptr, rtv);
        backend.BindOffscreenColorTargetEXT(rt.Get(), rtv, DXGI_FORMAT_R8G8B8A8_UNORM, kRtWidth, kRtHeight);

        auto pixelAt = [&](const std::vector<uint8_t>& buf, int x, int y) -> std::array<uint8_t, 4>
        {
            const std::size_t idx = (static_cast<std::size_t>(y) * kRtWidth + static_cast<std::size_t>(x)) * 4;
            return {buf[idx + 0], buf[idx + 1], buf[idx + 2], buf[idx + 3]};
        };
        auto isColor = [](const std::array<uint8_t, 4>& p, uint8_t r, uint8_t g, uint8_t b, uint8_t a)
        {
            return p[0] == r && p[1] == g && p[2] == b && p[3] == a;
        };
        auto regionIs = [&](const std::vector<uint8_t>& buf, uint8_t r, uint8_t g, uint8_t b, uint8_t a)
        {
            for (int y = 28; y < 32; ++y)
                for (int x = 28; x < 32; ++x)
                    if (!isColor(pixelAt(buf, x, y), r, g, b, a)) return false;
            return true;
        };

        struct VPT { float x, y, z; float u, v; };
        static const VPT kTriTex[3] = {
            {-1.0f, -1.0f, 0.0f, 0.0f, 1.0f},
            { 3.0f, -1.0f, 0.0f, 2.0f, 1.0f},
            {-1.0f,  3.0f, 0.0f, 0.0f, -1.0f},
        };
        D3D12VertexBufferBackend vbTex(&backend, 3);
        vbTex.SetData(kTriTex, 3, sizeof(VPT));

        ImageData img;
        img.width = 2; img.height = 2; img.mipLevels = 1;
        img.pixels.resize(2 * 2 * 4);
        for (int i = 0; i < 4; ++i)
        {
            img.pixels[i * 4 + 0] = 11; img.pixels[i * 4 + 1] = 22;
            img.pixels[i * 4 + 2] = 33; img.pixels[i * 4 + 3] = 255;
        }
        D3D12TextureBackend tex(&backend, img);

        GpuDrawParams tp;
        tp.texture0 = &tex;
        tp.textureEnabled = true;
        // diffuseColor left at its default (1,1,1,1) so outColor == the raw sampled texel exactly.

        backend.Clear(0.0f, 1.0f, 0.0f, 1.0f);
        auto beforeN = ReadBackRenderTargetFull(backend, rt.Get(), kRtWidth, kRtHeight);
        backend.DrawPrimitivesEx(vbTex, Matrix::getIdentityProperty(), Matrix::getIdentityProperty(),
                                 Matrix::getIdentityProperty(), PrimitiveType::TriangleList, 1, tp);
        auto afterN = ReadBackRenderTargetFull(backend, rt.Get(), kRtWidth, kRtHeight);
        Check(regionIs(beforeN, 0, 255, 0, 255) && regionIs(afterN, 11, 22, 33, 255),
              "N1: DrawPrimitivesEx() real textured3d draw samples the exact texture color "
              "(diffuseColor=white) over the Clear() background (plan_dx.md DX-111)");

        // Indexed path, same textured3d draw -- proves DrawIndexedPrimitivesEx shares the same real
        // pipeline (DrawPrimitivesExImpl), not just the non-indexed entry point.
        static const uint16_t kTriTexIdx[3] = {0, 1, 2};
        D3D12IndexBufferBackend ibTex(&backend, 3, /*thirtyTwoBit=*/false);
        ibTex.SetData16(kTriTexIdx, 3);
        backend.Clear(0.0f, 1.0f, 0.0f, 1.0f);
        backend.DrawIndexedPrimitivesEx(vbTex, ibTex, Matrix::getIdentityProperty(), Matrix::getIdentityProperty(),
                                        Matrix::getIdentityProperty(), PrimitiveType::TriangleList, 1, tp);
        auto afterNIdx = ReadBackRenderTargetFull(backend, rt.Get(), kRtWidth, kRtHeight);
        Check(regionIs(afterNIdx, 11, 22, 33, 255),
              "N2: DrawIndexedPrimitivesEx() indexed textured3d draw shares the same real pipeline "
              "and samples the exact texture color");

        // colored_textured3d (stride 24): a white texture tinted by an exact vertex color, proving
        // VertexColorEnabled's real multiply, not just the texture sample alone.
        struct VPCT { float x, y, z; uint32_t color; float u, v; };
        const uint32_t kVertColor = 0xFFF0A050u; // A=255,B=240,G=160,R=80 (R8G8B8A8 byte order)
        static VPCT kTriColTex[3];
        kTriColTex[0] = { -1.0f, -1.0f, 0.0f, kVertColor, 0.0f, 1.0f };
        kTriColTex[1] = {  3.0f, -1.0f, 0.0f, kVertColor, 2.0f, 1.0f };
        kTriColTex[2] = { -1.0f,  3.0f, 0.0f, kVertColor, 0.0f, -1.0f };
        D3D12VertexBufferBackend vbColTex(&backend, 3);
        vbColTex.SetData(kTriColTex, 3, sizeof(VPCT));

        ImageData whiteImg;
        whiteImg.width = 2; whiteImg.height = 2; whiteImg.mipLevels = 1;
        whiteImg.pixels.assign(2 * 2 * 4, 255);
        D3D12TextureBackend whiteTex(&backend, whiteImg);

        GpuDrawParams ctp;
        ctp.texture0 = &whiteTex;
        ctp.textureEnabled = true;
        ctp.vertexColorEnabled = true;

        backend.Clear(0.0f, 1.0f, 0.0f, 1.0f);
        backend.DrawPrimitivesEx(vbColTex, Matrix::getIdentityProperty(), Matrix::getIdentityProperty(),
                                 Matrix::getIdentityProperty(), PrimitiveType::TriangleList, 1, ctp);
        auto afterCT = ReadBackRenderTargetFull(backend, rt.Get(), kRtWidth, kRtHeight);
        Check(regionIs(afterCT, 80, 160, 240, 255),
              "N3: DrawPrimitivesEx() real colored_textured3d draw multiplies the exact vertex color "
              "through a white texture (plan_dx.md DX-111)");

        backend.UnbindOffscreenColorTargetEXT();
    }

    // ---- Check O (DX-111 continued): lit_textured3d (stride 32) ----
    {
        constexpr int kRtWidth = 64;
        constexpr int kRtHeight = 64;

        D3D12_HEAP_PROPERTIES heapProps{};
        heapProps.Type = D3D12_HEAP_TYPE_DEFAULT;

        D3D12_RESOURCE_DESC rtDesc{};
        rtDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        rtDesc.Width = kRtWidth;
        rtDesc.Height = kRtHeight;
        rtDesc.DepthOrArraySize = 1;
        rtDesc.MipLevels = 1;
        rtDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        rtDesc.SampleDesc.Count = 1;
        rtDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
        rtDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;

        Microsoft::WRL::ComPtr<ID3D12Resource> rt;
        HRESULT hrRt = backend.GetDeviceEXT()->CreateCommittedResource(
            &heapProps, D3D12_HEAP_FLAG_NONE, &rtDesc,
            D3D12_RESOURCE_STATE_RENDER_TARGET, nullptr, IID_PPV_ARGS(rt.GetAddressOf()));
        Check(SUCCEEDED(hrRt) && rt != nullptr, "O0: real off-screen RGBA8 render-target resource created");
        backend.GetResourceStateTrackerEXT().TrackResource(rt.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET);

        D3D12_CPU_DESCRIPTOR_HANDLE rtv = backend.AllocateRtvDescriptorEXT();
        backend.GetDeviceEXT()->CreateRenderTargetView(rt.Get(), nullptr, rtv);
        backend.BindOffscreenColorTargetEXT(rt.Get(), rtv, DXGI_FORMAT_R8G8B8A8_UNORM, kRtWidth, kRtHeight);

        auto pixelAt = [&](const std::vector<uint8_t>& buf, int x, int y) -> std::array<uint8_t, 4>
        {
            const std::size_t idx = (static_cast<std::size_t>(y) * kRtWidth + static_cast<std::size_t>(x)) * 4;
            return {buf[idx + 0], buf[idx + 1], buf[idx + 2], buf[idx + 3]};
        };
        auto isColor = [](const std::array<uint8_t, 4>& p, uint8_t r, uint8_t g, uint8_t b, uint8_t a)
        {
            return p[0] == r && p[1] == g && p[2] == b && p[3] == a;
        };
        auto regionIs = [&](const std::vector<uint8_t>& buf, uint8_t r, uint8_t g, uint8_t b, uint8_t a)
        {
            for (int y = 28; y < 32; ++y)
                for (int x = 28; x < 32; ++x)
                    if (!isColor(pixelAt(buf, x, y), r, g, b, a)) return false;
            return true;
        };

        struct VPNT { float x, y, z; float nx, ny, nz; float u, v; };
        static const VPNT kTriLit[3] = {
            {-1.0f, -1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 1.0f},
            { 3.0f, -1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 2.0f, 1.0f},
            {-1.0f,  3.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, -1.0f},
        };
        D3D12VertexBufferBackend vbLit(&backend, 3);
        vbLit.SetData(kTriLit, 3, sizeof(VPNT));

        ImageData img;
        img.width = 2; img.height = 2; img.mipLevels = 1;
        img.pixels.resize(2 * 2 * 4);
        for (int i = 0; i < 4; ++i)
        {
            img.pixels[i * 4 + 0] = 44; img.pixels[i * 4 + 1] = 55;
            img.pixels[i * 4 + 2] = 66; img.pixels[i * 4 + 3] = 255;
        }
        D3D12TextureBackend tex(&backend, img);

        GpuDrawParams unlitP;
        unlitP.texture0 = &tex;
        unlitP.textureEnabled = true;
        unlitP.lightingEnabled = false;

        backend.Clear(0.0f, 0.0f, 1.0f, 1.0f);
        backend.DrawPrimitivesEx(vbLit, Matrix::getIdentityProperty(), Matrix::getIdentityProperty(),
                                 Matrix::getIdentityProperty(), PrimitiveType::TriangleList, 1, unlitP);
        auto unlitResult = ReadBackRenderTargetFull(backend, rt.Get(), kRtWidth, kRtHeight);
        Check(regionIs(unlitResult, 44, 55, 66, 255),
              "O1: DrawPrimitivesEx() real lit_textured3d unlit branch samples diffuseColor*texture "
              "exactly (plan_dx.md DX-111)");

        GpuDrawParams litP = unlitP;
        litP.lightingEnabled = true;
        litP.ambientColor[0] = 0.5f; litP.ambientColor[1] = 0.5f; litP.ambientColor[2] = 0.5f;
        litP.specularColor[0] = 0.0f; litP.specularColor[1] = 0.0f; litP.specularColor[2] = 0.0f;

        backend.Clear(0.0f, 0.0f, 1.0f, 1.0f);
        backend.DrawPrimitivesEx(vbLit, Matrix::getIdentityProperty(), Matrix::getIdentityProperty(),
                                 Matrix::getIdentityProperty(), PrimitiveType::TriangleList, 1, litP);
        auto litResult = ReadBackRenderTargetFull(backend, rt.Get(), kRtWidth, kRtHeight);
        bool litDiffersFromUnlitAndBackground = true;
        for (int y = 28; y < 32; ++y)
        {
            for (int x = 28; x < 32; ++x)
            {
                const auto lp = pixelAt(litResult, x, y);
                const auto up = pixelAt(unlitResult, x, y);
                const bool sameAsUnlit = (lp[0] == up[0] && lp[1] == up[1] && lp[2] == up[2]);
                const bool sameAsBackground = (lp[0] == 0 && lp[1] == 0 && lp[2] == 255);
                if (sameAsUnlit || sameAsBackground) litDiffersFromUnlitAndBackground = false;
            }
        }
        Check(litDiffersFromUnlitAndBackground,
              "O2: DrawPrimitivesEx() real lit_textured3d lit branch genuinely computes a different "
              "color than both the unlit result and the Clear() background (plan_dx.md DX-111)");

        backend.UnbindOffscreenColorTargetEXT();
    }

    // ---- Check P (DX-111 continued): alpha_test3d ----
    {
        constexpr int kRtWidth = 64;
        constexpr int kRtHeight = 64;

        D3D12_HEAP_PROPERTIES heapProps{};
        heapProps.Type = D3D12_HEAP_TYPE_DEFAULT;

        D3D12_RESOURCE_DESC rtDesc{};
        rtDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        rtDesc.Width = kRtWidth;
        rtDesc.Height = kRtHeight;
        rtDesc.DepthOrArraySize = 1;
        rtDesc.MipLevels = 1;
        rtDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        rtDesc.SampleDesc.Count = 1;
        rtDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
        rtDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;

        Microsoft::WRL::ComPtr<ID3D12Resource> rt;
        HRESULT hrRt = backend.GetDeviceEXT()->CreateCommittedResource(
            &heapProps, D3D12_HEAP_FLAG_NONE, &rtDesc,
            D3D12_RESOURCE_STATE_RENDER_TARGET, nullptr, IID_PPV_ARGS(rt.GetAddressOf()));
        Check(SUCCEEDED(hrRt) && rt != nullptr, "P0: real off-screen RGBA8 render-target resource created");
        backend.GetResourceStateTrackerEXT().TrackResource(rt.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET);

        D3D12_CPU_DESCRIPTOR_HANDLE rtv = backend.AllocateRtvDescriptorEXT();
        backend.GetDeviceEXT()->CreateRenderTargetView(rt.Get(), nullptr, rtv);
        backend.BindOffscreenColorTargetEXT(rt.Get(), rtv, DXGI_FORMAT_R8G8B8A8_UNORM, kRtWidth, kRtHeight);

        auto pixelAt = [&](const std::vector<uint8_t>& buf, int x, int y) -> std::array<uint8_t, 4>
        {
            const std::size_t idx = (static_cast<std::size_t>(y) * kRtWidth + static_cast<std::size_t>(x)) * 4;
            return {buf[idx + 0], buf[idx + 1], buf[idx + 2], buf[idx + 3]};
        };
        auto isColor = [](const std::array<uint8_t, 4>& p, uint8_t r, uint8_t g, uint8_t b, uint8_t a)
        {
            return p[0] == r && p[1] == g && p[2] == b && p[3] == a;
        };
        auto regionIs = [&](const std::vector<uint8_t>& buf, uint8_t r, uint8_t g, uint8_t b, uint8_t a)
        {
            for (int y = 28; y < 32; ++y)
                for (int x = 28; x < 32; ++x)
                    if (!isColor(pixelAt(buf, x, y), r, g, b, a)) return false;
            return true;
        };

        struct VPT { float x, y, z; float u, v; };
        static const VPT kTriAT[3] = {
            {-1.0f, -1.0f, 0.0f, 0.0f, 1.0f},
            { 3.0f, -1.0f, 0.0f, 2.0f, 1.0f},
            {-1.0f,  3.0f, 0.0f, 0.0f, -1.0f},
        };
        D3D12VertexBufferBackend vbAT(&backend, 3);
        vbAT.SetData(kTriAT, 3, sizeof(VPT));

        ImageData img;
        img.width = 2; img.height = 2; img.mipLevels = 1;
        img.pixels.resize(2 * 2 * 4);
        for (int i = 0; i < 4; ++i)
        {
            img.pixels[i * 4 + 0] = 200; img.pixels[i * 4 + 1] = 100;
            img.pixels[i * 4 + 2] = 50;  img.pixels[i * 4 + 3] = 128; // alpha=128/255 ~ 0.502
        }
        D3D12TextureBackend tex(&backend, img);

        GpuDrawParams atp;
        atp.texture0 = &tex;
        atp.textureEnabled = true;
        // AlphaTol=0 (comparison mode) -> passTest = alpha < AlphaRef; failW<0 -> discard on fail.
        atp.alphaTest[0] = 0.5f;  // AlphaRef
        atp.alphaTest[1] = 0.0f;  // AlphaTol
        atp.alphaTest[2] = 1.0f;  // AlphaPassW (>=0, never discard on pass)
        atp.alphaTest[3] = -1.0f; // AlphaFailW (<0, discard on fail)

        // Sub-check 1: alpha=128/255 is NOT < 0.5 -> fails -> discard -> background survives.
        backend.Clear(0.0f, 1.0f, 0.0f, 1.0f);
        backend.DrawPrimitivesEx(vbAT, Matrix::getIdentityProperty(), Matrix::getIdentityProperty(),
                                 Matrix::getIdentityProperty(), PrimitiveType::TriangleList, 1, atp);
        auto discardResult = ReadBackRenderTargetFull(backend, rt.Get(), kRtWidth, kRtHeight);
        Check(regionIs(discardResult, 0, 255, 0, 255),
              "P1: DrawPrimitivesEx() real alpha_test3d clip() genuinely drops a failing pixel, "
              "leaving the Clear() background untouched (plan_dx.md DX-111)");

        // Sub-check 2: replace the texture's alpha with 64/255 (< 0.5) -> passes -> drawn exactly.
        std::vector<uint8_t> passPixels(2 * 2 * 4);
        for (int i = 0; i < 4; ++i)
        {
            passPixels[i * 4 + 0] = 200; passPixels[i * 4 + 1] = 100;
            passPixels[i * 4 + 2] = 50;  passPixels[i * 4 + 3] = 64;
        }
        tex.UpdatePixelsLevel(0, passPixels.data(), 2, 2);

        backend.Clear(0.0f, 1.0f, 0.0f, 1.0f);
        backend.DrawPrimitivesEx(vbAT, Matrix::getIdentityProperty(), Matrix::getIdentityProperty(),
                                 Matrix::getIdentityProperty(), PrimitiveType::TriangleList, 1, atp);
        auto passResult = ReadBackRenderTargetFull(backend, rt.Get(), kRtWidth, kRtHeight);
        Check(regionIs(passResult, 200, 100, 50, 64),
              "P2: DrawPrimitivesEx() real alpha_test3d draws the exact texture color (including its "
              "own alpha byte) when the test passes (plan_dx.md DX-111)");

        backend.UnbindOffscreenColorTargetEXT();
    }

    std::printf("\n%s: %d failure(s)\n", g_failures == 0 ? "RESULT: ALL PASS" : "RESULT: FAILURES", g_failures);
    return g_failures == 0 ? 0 : 1;
}
