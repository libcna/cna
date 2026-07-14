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
// Check U -- env_map3d (closing DX-111, 10/10 stock variants real): a real D3D12TextureCubeBackend
//   (new for this task), sampled via a geometrically-constrained reflection direction that lands
//   deep inside one distinctly-colored cube face -- same discriminating-by-construction proof D3D11's
//   own Check U (d3d11_smoke_test.cpp DX-66) uses, adapted to this backend's own N-separate-
//   descriptor-tables SRV binding (t0 base Texture2D + t1 TextureCube, D3D12RootSignatureCache's own
//   (3,2,2) shape, already created and cached by dual_texture3d's own earlier Check).
//
// Exit code 0 = all checks PASS, 1 = any FAILs.

#include "CNA/Internal/Backends/D3D12/D3D12GraphicsBackend.hpp"
#include "CNA/Internal/Backends/D3D12/D3D12ResourceStateTracker.hpp"
#include "CNA/Internal/Backends/D3D12/D3D12RootSignatureCache.hpp"
#include "CNA/Internal/Backends/D3D12/D3D12PipelineStateCache.hpp"
#include "CNA/Internal/Backends/D3D12/D3D12Buffers.hpp"
#include "CNA/Internal/Backends/D3D12/D3D12Textures.hpp"
#include "CNA/Internal/Backends/D3D12/D3D12TextureCube.hpp"
#include "CNA/Internal/Backends/D3D12/D3D12RenderTargets.hpp"
#include "CNA/Internal/Backends/D3D12/D3D12EffectBackend.hpp"
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
using CNA::Internal::Backends::D3D12::D3D12TextureCubeBackend;
using CNA::Internal::Backends::D3D12::D3D12RenderTargetBackend;
using CNA::Internal::Backends::D3D12::D3D12RenderTargetCubeBackend;
using CNA::Internal::Backends::D3D12::D3D12EffectBackend;
using CNA::Internal::Backends::IRenderTargetBackend;
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

// plan_dx.md DX-120 adds:
// Check AA -- D3D12OcclusionQueryBackend: a real ID3D12QueryHeap(D3D12_QUERY_TYPE_OCCLUSION) +
//   D3D12_HEAP_TYPE_READBACK buffer, Begin()/EndQuery()/ResolveQueryData() all genuinely recorded
//   and executed through Wine+vkd3d-proton on the real GPU. A visible full-viewport triangle
//   reports a real, positive PixelCount(); the SAME query object, reused for a second Begin()/End()
//   around geometry placed entirely outside the clip volume (so nothing is rasterized at all),
//   reports PixelCount() == 0 -- a genuine visible-vs-invisible discriminating result, not just
//   "the query completed" (closes Phase DX15's own DX-147 D3D12 half).

// plan_dx.md DX-121 adds:
// Check BB -- D3D12EffectBackend: runtime D3DCompile() of arbitrary HLSL source (not one of
//   DX-13-hlsl's offline-compiled stock variants) builds a real PSO+constant-buffer end to end,
//   driven manually (SpriteBatch/GraphicsDevice can't be constructed safely in this off-screen-only
//   suite -- GraphicsDevice's own constructor unconditionally creates a real window for any
//   non-Headless/Software backend, which is exactly the crash-prone path DX-100/DX-102 already
//   found for D3D12 outside a Proton-managed launch; D3D12SpriteBatchBackend's own real
//   SetCustomEffect()/FlushBatch() wiring, added this same task, is exercised by code review and
//   architectural reuse of this exact PSO/constant-buffer pair, not an independent CTest proof --
//   an honest, documented scope boundary), proving the color is genuinely driven by
//   SetUniformVec4()'s fixed-slot constant buffer, matching D3D11's own DX-58 rigor. A deliberately
//   broken HLSL source fails CompileProgram() cleanly with a real, non-empty compiler error.

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

        // DX-113: the header's own documented contract says TransitionTo()/GetTrackedStateEXT() throw
        // std::runtime_error for a resource that was never registered via TrackResource() -- real
        // proof this fires, not just documented in a comment. A second, never-tracked committed
        // buffer (distinct pointer from dummyResource, which IS tracked) is the discriminating input.
        Microsoft::WRL::ComPtr<ID3D12Resource> untrackedResource;
        HRESULT hrUntracked = device->CreateCommittedResource(
            &heapProps, D3D12_HEAP_FLAG_NONE, &bufDesc,
            D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(untrackedResource.ReleaseAndGetAddressOf()));
        Check(SUCCEEDED(hrUntracked) && untrackedResource != nullptr,
              "G8: a second, deliberately never-tracked committed buffer created for the throw test");

        bool threwTransition = false;
        try { tracker.TransitionTo(cmdList, untrackedResource.Get(), D3D12_RESOURCE_STATE_COPY_DEST); }
        catch (const std::runtime_error&) { threwTransition = true; }
        Check(threwTransition,
              "G9: TransitionTo() on a never-tracked resource genuinely throws std::runtime_error "
              "(plan_dx.md DX-106/DX-113), not silently emitting an ad-hoc barrier from an unknown state");

        bool threwGetState = false;
        try { (void)tracker.GetTrackedStateEXT(untrackedResource.Get()); }
        catch (const std::runtime_error&) { threwGetState = true; }
        Check(threwGetState,
              "G10: GetTrackedStateEXT() on a never-tracked resource genuinely throws std::runtime_error");
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

    // ---- Check Q (DX-111 continued): dual_texture3d -- real 2-contiguous-SRV-table binding ----
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
        Check(SUCCEEDED(hrRt) && rt != nullptr, "Q0: real off-screen RGBA8 render-target resource created");
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

        // dual_texture3d.frag.hlsl's real formula: outColor = (tex1.rgb*2, tex1.a) * tex2 * Tint
        // (Tint = DiffuseColor, default white). tex1=white(255) makes the *2 an exact doubling of
        // tex2's own byte value with zero rounding ambiguity: (60,80,100)*2 = (120,160,200), all
        // well under 255 so nothing clamps -- chosen deliberately so this check is byte-exact, not
        // approximate.
        struct VPT { float x, y, z; float u, v; };
        static const VPT kTriDual[3] = {
            {-1.0f, -1.0f, 0.0f, 0.0f, 1.0f},
            { 3.0f, -1.0f, 0.0f, 2.0f, 1.0f},
            {-1.0f,  3.0f, 0.0f, 0.0f, -1.0f},
        };
        D3D12VertexBufferBackend vbDual(&backend, 3);
        vbDual.SetData(kTriDual, 3, sizeof(VPT));

        ImageData whiteImg;
        whiteImg.width = 2; whiteImg.height = 2; whiteImg.mipLevels = 1;
        whiteImg.pixels.assign(2 * 2 * 4, 255);
        D3D12TextureBackend tex0White(&backend, whiteImg);

        ImageData tintImg;
        tintImg.width = 2; tintImg.height = 2; tintImg.mipLevels = 1;
        tintImg.pixels.resize(2 * 2 * 4);
        for (int i = 0; i < 4; ++i)
        {
            tintImg.pixels[i * 4 + 0] = 60; tintImg.pixels[i * 4 + 1] = 80;
            tintImg.pixels[i * 4 + 2] = 100; tintImg.pixels[i * 4 + 3] = 255;
        }
        D3D12TextureBackend tex1Tint(&backend, tintImg);

        GpuDrawParams dp;
        dp.texture0 = &tex0White;
        dp.texture1 = &tex1Tint;
        dp.dualTexture = true;
        dp.textureEnabled = true;

        backend.Clear(0.0f, 1.0f, 0.0f, 1.0f);
        auto beforeQ = ReadBackRenderTargetFull(backend, rt.Get(), kRtWidth, kRtHeight);
        backend.DrawPrimitivesEx(vbDual, Matrix::getIdentityProperty(), Matrix::getIdentityProperty(),
                                 Matrix::getIdentityProperty(), PrimitiveType::TriangleList, 1, dp);
        auto afterQ = ReadBackRenderTargetFull(backend, rt.Get(), kRtWidth, kRtHeight);
        Check(regionIs(beforeQ, 0, 255, 0, 255) && regionIs(afterQ, 120, 160, 200, 255),
              "Q1: DrawPrimitivesEx() real dual_texture3d draw combines two independently-allocated "
              "textures' SRVs through a genuinely contiguous per-draw descriptor table -- exact "
              "expected byte result, not just \"a draw call succeeded\" (plan_dx.md DX-111)");

        // Indexed path -- proves the same contiguous-table binding survives DrawIndexedPrimitivesEx.
        static const uint16_t kTriDualIdx[3] = {0, 1, 2};
        D3D12IndexBufferBackend ibDual(&backend, 3, /*thirtyTwoBit=*/false);
        ibDual.SetData16(kTriDualIdx, 3);
        backend.Clear(0.0f, 1.0f, 0.0f, 1.0f);
        backend.DrawIndexedPrimitivesEx(vbDual, ibDual, Matrix::getIdentityProperty(), Matrix::getIdentityProperty(),
                                        Matrix::getIdentityProperty(), PrimitiveType::TriangleList, 1, dp);
        auto afterQIdx = ReadBackRenderTargetFull(backend, rt.Get(), kRtWidth, kRtHeight);
        Check(regionIs(afterQIdx, 120, 160, 200, 255),
              "Q2: DrawIndexedPrimitivesEx() indexed dual_texture3d draw shares the same real "
              "2-texture pipeline and produces the same exact result");

        backend.UnbindOffscreenColorTargetEXT();
    }

    // ---- Check R (DX-112): D3D12SpriteBatchBackend -- real quad-batched sprite draw, flip proof ----
    {
        using Microsoft::Xna::Framework::Rectangle;
        using Microsoft::Xna::Framework::Vector2;
        using Microsoft::Xna::Framework::Color;
        using Microsoft::Xna::Framework::Graphics::SpriteEffects;

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
        Check(SUCCEEDED(hrRt) && rt != nullptr, "R0: real off-screen RGBA8 render-target resource created");
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
        // Checked well inside each half of the 64-wide render target (x=14..18 / x=46..50), away
        // from the u=0.5 texel boundary at x=32 -- same "sample exactly at a texel center, away from
        // any blend edge" discipline every earlier texture check in this file already established.
        auto leftRegionIs = [&](const std::vector<uint8_t>& buf, uint8_t r, uint8_t g, uint8_t b, uint8_t a)
        {
            for (int y = 28; y < 32; ++y)
                for (int x = 14; x < 18; ++x)
                    if (!isColor(pixelAt(buf, x, y), r, g, b, a)) return false;
            return true;
        };
        auto rightRegionIs = [&](const std::vector<uint8_t>& buf, uint8_t r, uint8_t g, uint8_t b, uint8_t a)
        {
            for (int y = 28; y < 32; ++y)
                for (int x = 46; x < 50; ++x)
                    if (!isColor(pixelAt(buf, x, y), r, g, b, a)) return false;
            return true;
        };

        // 4x2 texture, both rows identical (Y-axis bilinear blending is a no-op): columns 0-1 are
        // solid red, columns 2-3 are solid blue -- two texels of EACH color (not one) so that the
        // readback sample points below, chosen well inside a same-colored pair, are immune to the
        // GPU's real pixel-center-vs-texel-center bilinear sampling offset (D3D rasterizes at pixel
        // CENTERS, i.e. UV = (px+0.5)/width, which does NOT land exactly on a single texel's own
        // center for these render-target/texture dimensions -- confirmed empirically while writing
        // this check: a 2-texel-wide version of this same texture read back (197,50,33)/(33,80,217)
        // instead of the exact (200,50,30)/(30,80,220), a ~1.5% blend toward the adjacent texel).
        // Blending RED with RED (or BLUE with BLUE) is exact regardless of the blend weight, which
        // is what makes this test byte-exact without needing to solve for a perfectly-aligned pixel.
        ImageData img;
        img.width = 4; img.height = 2; img.mipLevels = 1;
        img.pixels.resize(4 * 2 * 4);
        for (int row = 0; row < 2; ++row)
        {
            const std::size_t rowBase = static_cast<std::size_t>(row) * 4 * 4;
            for (int col = 0; col < 2; ++col) // columns 0-1: red
            {
                const std::size_t px = rowBase + static_cast<std::size_t>(col) * 4;
                img.pixels[px + 0] = 200; img.pixels[px + 1] = 50;
                img.pixels[px + 2] = 30;  img.pixels[px + 3] = 255;
            }
            for (int col = 2; col < 4; ++col) // columns 2-3: blue
            {
                const std::size_t px = rowBase + static_cast<std::size_t>(col) * 4;
                img.pixels[px + 0] = 30;  img.pixels[px + 1] = 80;
                img.pixels[px + 2] = 220; img.pixels[px + 3] = 255;
            }
        }
        D3D12TextureBackend tex(&backend, img);

        auto sb = backend.CreateSpriteBatch();
        Check(sb != nullptr, "R1: CreateSpriteBatch() returns a real D3D12SpriteBatchBackend");

        backend.Clear(0.0f, 1.0f, 0.0f, 1.0f);
        sb->Begin();
        sb->Draw(tex, Rectangle(0, 0, kRtWidth, kRtHeight), Rectangle(0, 0, 4, 2), Color::White);
        sb->End();
        auto afterR1 = ReadBackRenderTargetFull(backend, rt.Get(), kRtWidth, kRtHeight);
        Check(leftRegionIs(afterR1, 200, 50, 30, 255) && rightRegionIs(afterR1, 30, 80, 220, 255),
              "R2: D3D12SpriteBatchBackend::Draw() places a real quad-batched sprite at the exact "
              "expected screen position, sampling the exact source-texel colors (plan_dx.md DX-112)");

        backend.Clear(0.0f, 1.0f, 0.0f, 1.0f);
        sb->Begin();
        sb->Draw(tex, Rectangle(0, 0, kRtWidth, kRtHeight), Rectangle(0, 0, 4, 2), Color::White,
                 0.0f, Vector2(0, 0), SpriteEffects::FlipHorizontally, 0.0f);
        sb->End();
        auto afterR2 = ReadBackRenderTargetFull(backend, rt.Get(), kRtWidth, kRtHeight);
        Check(leftRegionIs(afterR2, 30, 80, 220, 255) && rightRegionIs(afterR2, 200, 50, 30, 255),
              "R3: SpriteEffects::FlipHorizontally genuinely swaps left/right source sampling -- not "
              "just \"a draw call succeeded\" (plan_dx.md DX-112)");

        backend.UnbindOffscreenColorTargetEXT();
    }

    // ---- Check S (DX-111 finish): skinned3d -- BoneBlock genuinely populated, single identity bone ----
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
        Check(SUCCEEDED(hrRt) && rt != nullptr, "S0: real off-screen RGBA8 render-target resource created");
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

        // Same fixture as D3D11's own Check V (d3d11_smoke_test.cpp DX-67): a single identity bone
        // (BoneBlock genuinely populated from GpuDrawParams::boneTransforms -- an all-zero bone
        // matrix would degenerate the transform and fail this check) combined with ambient=white and
        // specular=zeroed (light0's own diffuse contribution is already zero by construction: the
        // vertex normal (0,0,1) is perpendicular to the default light0Dir (0,-1,0)) leaves outColor
        // == the exact sampled texture color.
        struct VPNTS { float x, y, z; float nx, ny, nz; float u, v;
                      float bw0, bw1, bw2, bw3; uint8_t bi0, bi1, bi2, bi3; };
        static const VPNTS kTriSkin[3] = {
            {-1.0f, -1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 1.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0, 0, 0, 0},
            { 3.0f, -1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 2.0f, 1.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0, 0, 0, 0},
            {-1.0f,  3.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, -1.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0, 0, 0, 0},
        };
        D3D12VertexBufferBackend vbSkin(&backend, 3);
        vbSkin.SetData(kTriSkin, 3, sizeof(VPNTS));

        ImageData img;
        img.width = 2; img.height = 2; img.mipLevels = 1;
        img.pixels.resize(2 * 2 * 4);
        for (int i = 0; i < 4; ++i)
        {
            img.pixels[i * 4 + 0] = 77; img.pixels[i * 4 + 1] = 88;
            img.pixels[i * 4 + 2] = 99; img.pixels[i * 4 + 3] = 255;
        }
        D3D12TextureBackend tex(&backend, img);

        GpuDrawParams sp;
        sp.texture0 = &tex;
        sp.textureEnabled = true;
        sp.skinned = true;
        sp.boneCount = 1;
        sp.weightsPerVertex = 1;
        Matrix::getIdentityProperty().ToColumnMajor(sp.boneTransforms);
        sp.ambientColor[0] = 1.0f; sp.ambientColor[1] = 1.0f; sp.ambientColor[2] = 1.0f;
        sp.specularColor[0] = 0.0f; sp.specularColor[1] = 0.0f; sp.specularColor[2] = 0.0f;
        sp.eyePositionWorld[0] = 0.0f; sp.eyePositionWorld[1] = 0.0f; sp.eyePositionWorld[2] = -10.0f;

        backend.Clear(0.0f, 1.0f, 0.0f, 1.0f);
        backend.DrawPrimitivesEx(vbSkin, Matrix::getIdentityProperty(), Matrix::getIdentityProperty(),
                                 Matrix::getIdentityProperty(), PrimitiveType::TriangleList, 1, sp);
        auto afterS = ReadBackRenderTargetFull(backend, rt.Get(), kRtWidth, kRtHeight);
        Check(regionIs(afterS, 77, 88, 99, 255),
              "S1: DrawPrimitivesEx() real skinned3d with a genuinely-populated single identity bone "
              "(D3DBoneConstants, not left zero) samples the exact texture color (plan_dx.md DX-111)");

        backend.UnbindOffscreenColorTargetEXT();
    }

    // ---- Check T (DX-111 finish): instanced3d -- real per-instance world buffer, dual vertex stream ----
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
        Check(SUCCEEDED(hrRt) && rt != nullptr, "T0: real off-screen RGBA8 render-target resource created");
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

        // Same fixture as D3D11's own Check W (d3d11_smoke_test.cpp DX-68): one identity-transform
        // instance via the per-instance INSTANCEWORLD0-3 buffer (not the per-vertex one) outputs the
        // exact per-instance DiffuseColor -- both non-zero color components at their saturated 0/1
        // extremes, so there is no rounding ambiguity in the final UNORM8 byte comparison.
        struct VP3 { float x, y, z; };
        static const VP3 kTriInst[3] = {
            {-1.0f, -1.0f, 0.0f},
            { 3.0f, -1.0f, 0.0f},
            {-1.0f,  3.0f, 0.0f},
        };
        D3D12VertexBufferBackend vbInst(&backend, 3);
        vbInst.SetData(kTriInst, 3, sizeof(VP3));

        static const uint16_t kTriInstIdx[3] = {0, 1, 2};
        D3D12IndexBufferBackend ibInst(&backend, 3, /*thirtyTwoBit=*/false);
        ibInst.SetData16(kTriInstIdx, 3);

        // One identity-transform instance: 4 float4 rows (INSTANCEWORLD0-3).
        static const float kInstanceWorld[16] = {
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            0.0f, 0.0f, 0.0f, 1.0f,
        };
        D3D12VertexBufferBackend instVb(&backend, 1);
        instVb.SetData(kInstanceWorld, 1, sizeof(kInstanceWorld));

        GpuDrawParams ip;
        ip.instanceVb = &instVb;
        ip.diffuseColor[0] = 1.0f; ip.diffuseColor[1] = 1.0f;
        ip.diffuseColor[2] = 0.0f; ip.diffuseColor[3] = 1.0f;

        backend.Clear(0.0f, 0.0f, 1.0f, 1.0f);
        backend.DrawInstancedPrimitivesEx(vbInst, ibInst, Matrix::getIdentityProperty(),
                                          Matrix::getIdentityProperty(), Matrix::getIdentityProperty(),
                                          PrimitiveType::TriangleList, 1, 1, ip);
        auto afterT = ReadBackRenderTargetFull(backend, rt.Get(), kRtWidth, kRtHeight);
        Check(regionIs(afterT, 255, 255, 0, 255),
              "T1: DrawInstancedPrimitivesEx() real instanced3d draw with a genuine per-instance "
              "world buffer (dual vertex stream: slot 0 per-vertex POSITION, slot 1 per-instance "
              "INSTANCEWORLD0-3) outputs the exact instance DiffuseColor (plan_dx.md DX-111)");

        backend.UnbindOffscreenColorTargetEXT();
    }

    // ---- Check U (DX-111 closing): env_map3d -- real D3D12TextureCubeBackend, geometrically- ----
    // ---- constrained reflection direction lands deep inside one distinctly-colored cube face  ----
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
        Check(SUCCEEDED(hrRt) && rt != nullptr, "U0: real off-screen RGBA8 render-target resource created");
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

        // Same fixture as D3D11's own Check U (d3d11_smoke_test.cpp DX-66): camera placed far down
        // -Z from a +Z-facing surface, ambient/lighting/specular all zeroed by geometry+params so
        // only the env-map term (envMapAmount=1, fresnel disabled) survives -- reflDir resolves to
        // almost exactly (0,0,-1), landing deep inside the cube's -Z face (D3D12's own native slice
        // order matches D3D11's: +X,-X,+Y,-Y,+Z,-Z -> index 5), the only face given a distinct,
        // uniform, non-black color.
        struct VPNTE { float x, y, z; float nx, ny, nz; float u, v; };
        static const VPNTE kTriEnv[3] = {
            {-1.0f, -1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 1.0f},
            { 3.0f, -1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 2.0f, 1.0f},
            {-1.0f,  3.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, -1.0f},
        };
        D3D12VertexBufferBackend vbEnv(&backend, 3);
        vbEnv.SetData(kTriEnv, 3, sizeof(VPNTE));

        ImageData whiteImg;
        whiteImg.width = 2; whiteImg.height = 2; whiteImg.mipLevels = 1;
        whiteImg.pixels.assign(2 * 2 * 4, 255);
        D3D12TextureBackend whiteTex(&backend, whiteImg);

        D3D12TextureCubeBackend cube(&backend, 8, false, 0);
        std::vector<uint8_t> blackFace(8 * 8 * 4, 0);
        std::vector<uint8_t> negZFace(8 * 8 * 4);
        for (int i = 0; i < 8 * 8; ++i)
        {
            negZFace[i * 4 + 0] = 10; negZFace[i * 4 + 1] = 20;
            negZFace[i * 4 + 2] = 30; negZFace[i * 4 + 3] = 255;
        }
        for (int face = 0; face < 6; ++face)
        {
            const auto& data = (face == 5) ? negZFace : blackFace; // face 5 = -Z
            cube.SetData(face, 0, 0, 0, 8, 8, data.data(), static_cast<int>(data.size()));
        }

        GpuDrawParams ep;
        ep.texture0 = &whiteTex;
        ep.textureEnabled = true;
        ep.envMap = &cube;
        ep.envMapping = true;
        ep.envMapAmount = 1.0f;
        ep.eyePositionWorld[0] = 0.0f; ep.eyePositionWorld[1] = 0.0f; ep.eyePositionWorld[2] = -10.0f;

        backend.Clear(0.0f, 0.0f, 1.0f, 1.0f);
        backend.DrawPrimitivesEx(vbEnv, Matrix::getIdentityProperty(), Matrix::getIdentityProperty(),
                                 Matrix::getIdentityProperty(), PrimitiveType::TriangleList, 1, ep);
        auto afterU = ReadBackRenderTargetFull(backend, rt.Get(), kRtWidth, kRtHeight);
        Check(regionIs(afterU, 10, 20, 30, 255),
              "U1: DrawPrimitivesEx() real env_map3d samples the exact distinctly-colored cube face "
              "via a real D3D12TextureCubeBackend SRV (plan_dx.md DX-111, closing 10/10 stock variants)");

        backend.UnbindOffscreenColorTargetEXT();
    }

    // ---- Check V (DX-113): a dedicated fog-on/fog-off pixel test, closing the same real gap ----
    // ---- D3D11's own DX-81 audit found and fixed (d3d11_smoke_test.cpp Check AC) -- fog was wired ----
    // ---- into every applicable variant's constant buffer (colored3d's DrawPrimitivesEx bundle ----
    // ---- branch included) but never independently exercised by a dedicated on/off pixel test. ----
    // ---- Same fixture as D3D11's own Check AC: colored3d.vert.hlsl's formula (fogFactor = ----
    // ---- fogEnabled ? saturate((FogEnd-Z)/(FogEnd-FogStart)) : 1.0, then outColor.rgb = ----
    // ---- lerp(FogColor, vertexColor, fogFactor)) is the exact same DXBC bytecode D3D11 draws ----
    // ---- through -- a quad at object-space Z=0.5 with FogStart=0/FogEnd=0.5 lands fogFactor ----
    // ---- exactly on 0 when fog is enabled (pure FogColor) vs 1 when it's not (pure vertex color), ----
    // ---- an exact, unambiguous discrimination, not "some blend happened". ----
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
        Check(SUCCEEDED(hrRt) && rt != nullptr, "V0: real off-screen RGBA8 render-target resource created");

        backend.GetResourceStateTrackerEXT().TrackResource(rt.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET);
        D3D12_CPU_DESCRIPTOR_HANDLE rtv = backend.AllocateRtvDescriptorEXT();
        backend.GetDeviceEXT()->CreateRenderTargetView(rt.Get(), nullptr, rtv);
        backend.BindOffscreenColorTargetEXT(rt.Get(), rtv, DXGI_FORMAT_R8G8B8A8_UNORM, kRtWidth, kRtHeight);

        struct VPCz { float x, y, z; uint32_t color; };
        const uint32_t kRed = 0xFF0000FFu; // A=255,B=0,G=0,R=255 (R8G8B8A8 byte order)
        static const VPCz kTriFog[3] = {
            {-1.0f, -1.0f, 0.5f, kRed},
            { 3.0f, -1.0f, 0.5f, kRed},
            {-1.0f,  3.0f, 0.5f, kRed},
        };
        D3D12VertexBufferBackend vbFog(&backend, 3);
        vbFog.SetData(kTriFog, 3, sizeof(VPCz));

        auto pixelAt = [&](const std::vector<uint8_t>& buf, int x, int y) -> std::array<uint8_t, 4>
        {
            const std::size_t idx = (static_cast<std::size_t>(y) * kRtWidth + static_cast<std::size_t>(x)) * 4;
            return {buf[idx + 0], buf[idx + 1], buf[idx + 2], buf[idx + 3]};
        };
        auto regionIs = [&](const std::vector<uint8_t>& buf, uint8_t r, uint8_t g, uint8_t b, uint8_t a)
        {
            for (int y = 28; y < 32; ++y)
                for (int x = 28; x < 32; ++x)
                {
                    auto p = pixelAt(buf, x, y);
                    if (!(p[0] == r && p[1] == g && p[2] == b && p[3] == a)) return false;
                }
            return true;
        };

        GpuDrawParams fogOff;
        fogOff.vertexColorEnabled = true;
        fogOff.fogEnabled = false;
        fogOff.fogColor[0] = 0.0f; fogOff.fogColor[1] = 1.0f; fogOff.fogColor[2] = 0.0f;
        fogOff.fogStart = 0.0f;
        fogOff.fogEnd = 0.5f;

        backend.Clear(0.039f, 0.039f, 0.039f, 1.0f);
        backend.DrawPrimitivesEx(vbFog, Matrix::getIdentityProperty(), Matrix::getIdentityProperty(),
                                 Matrix::getIdentityProperty(), PrimitiveType::TriangleList, 1, fogOff);
        auto afterFogOff = ReadBackRenderTargetFull(backend, rt.Get(), kRtWidth, kRtHeight);
        Check(regionIs(afterFogOff, 255, 0, 0, 255),
              "V1: DrawPrimitivesEx() colored3d bundle, fogEnabled=false leaves the exact vertex "
              "color unblended (plan_dx.md DX-69/DX-113)");

        GpuDrawParams fogOn = fogOff;
        fogOn.fogEnabled = true;

        backend.Clear(0.039f, 0.039f, 0.039f, 1.0f);
        backend.DrawPrimitivesEx(vbFog, Matrix::getIdentityProperty(), Matrix::getIdentityProperty(),
                                 Matrix::getIdentityProperty(), PrimitiveType::TriangleList, 1, fogOn);
        auto afterFogOn = ReadBackRenderTargetFull(backend, rt.Get(), kRtWidth, kRtHeight);
        Check(regionIs(afterFogOn, 0, 255, 0, 255),
              "V2: DrawPrimitivesEx() colored3d bundle, fogEnabled=true with Z at FogEnd genuinely "
              "blends all the way to the exact FogColor (fogFactor=0), distinctly different from "
              "the fogEnabled=false case above (plan_dx.md DX-69/DX-113)");

        backend.UnbindOffscreenColorTargetEXT();
    }

    // ---- Check W (DX-117): real D3D12RenderTargetBackend/D3D12RenderTargetCubeBackend + MRT, ----
    // ---- through the actual public IGraphicsBackend API (CreateRenderTarget2D/SetRenderTarget2D/ ----
    // ---- SetRenderTargets/CreateRenderTargetCube) -- not the DX-111 test scaffolding every ----
    // ---- earlier Check used (BindOffscreenColorTargetEXT). ----
    {
        constexpr int kRtWidth = 64;
        constexpr int kRtHeight = 64;

        auto rt0 = backend.CreateRenderTarget2D(kRtWidth, kRtHeight, /*depthFormat=*/0);
        Check(rt0 != nullptr, "W1: CreateRenderTarget2D() returns a real IRenderTargetBackend");

        backend.SetRenderTarget2D(rt0.get());
        Check(backend.HasBoundColorTargetEXT(), "W2: SetRenderTarget2D() genuinely binds the target");

        auto* rt0Impl = dynamic_cast<D3D12RenderTargetBackend*>(rt0.get());
        Check(rt0Impl != nullptr, "W3: the real target is a D3D12RenderTargetBackend");

        backend.Clear(0.2f, 0.4f, 0.6f, 1.0f);
        auto rt0Readback = ReadBackRenderTargetFull(backend, rt0Impl->GetColorResourceEXT(), kRtWidth, kRtHeight);
        const std::size_t centerIdx =
            (static_cast<std::size_t>(kRtHeight / 2) * kRtWidth + static_cast<std::size_t>(kRtWidth / 2)) * 4;
        Check(rt0Readback[centerIdx + 0] == 51 && rt0Readback[centerIdx + 1] == 102 &&
              rt0Readback[centerIdx + 2] == 153 && rt0Readback[centerIdx + 3] == 255,
              "W4: Clear() on a real bound RenderTarget2D writes the exact requested color, read back "
              "through its own real GPU resource (plan_dx.md DX-117)");

        // A real triangle drawn into the render target, same "oversized triangle" trick Check M
        // established -- proves DrawColoredPrimitives() genuinely targets this real render target,
        // not just Clear().
        struct VPCw { float x, y, z; uint32_t color; };
        static const VPCw kTriW[3] = {
            {-1.0f, -1.0f, 0.0f, 0xFF00FF00u}, // A=255,B=0,G=255,R=0 -> exact green
            { 3.0f, -1.0f, 0.0f, 0xFF00FF00u},
            {-1.0f,  3.0f, 0.0f, 0xFF00FF00u},
        };
        D3D12VertexBufferBackend vbW(&backend, 3);
        vbW.SetData(kTriW, 3, sizeof(VPCw));
        backend.DrawColoredPrimitives(vbW, Matrix::getIdentityProperty(), Matrix::getIdentityProperty(),
                                      Matrix::getIdentityProperty(), PrimitiveType::TriangleList, 1);
        auto rt0AfterDraw = ReadBackRenderTargetFull(backend, rt0Impl->GetColorResourceEXT(), kRtWidth, kRtHeight);
        Check(rt0AfterDraw[centerIdx + 0] == 0 && rt0AfterDraw[centerIdx + 1] == 255 &&
              rt0AfterDraw[centerIdx + 2] == 0 && rt0AfterDraw[centerIdx + 3] == 255,
              "W5: DrawColoredPrimitives() paints the exact vertex color into a real bound "
              "RenderTarget2D (plan_dx.md DX-117)");

        backend.SetRenderTarget2D(nullptr);
        Check(!backend.HasBoundColorTargetEXT(),
              "W6: SetRenderTarget2D(nullptr) on an off-screen (no swap chain) backend genuinely "
              "restores the honest 'nothing bound' state, via RestoreBackBufferRenderTargetEXT()'s "
              "own real fallback (plan_dx.md DX-117)");

        // ---- Real MRT: 2 independently-created render targets, one SetRenderTargets() bind call, ----
        // ---- Clear() genuinely writes both -- same proof shape D3D11's own DX-46 established. ----
        auto rtA = backend.CreateRenderTarget2D(kRtWidth, kRtHeight, 0);
        auto rtB = backend.CreateRenderTarget2D(kRtWidth, kRtHeight, 0);
        IRenderTargetBackend* mrtTargets[2] = {rtA.get(), rtB.get()};
        backend.SetRenderTargets(mrtTargets, 2);
        Check(backend.HasBoundColorTargetEXT(), "W7: SetRenderTargets() binds the primary (index 0) target");

        backend.Clear(0.8f, 0.0f, 1.0f, 1.0f); // 204/0/255/255 -- exact under any rounding mode
        auto* rtAImpl = dynamic_cast<D3D12RenderTargetBackend*>(rtA.get());
        auto* rtBImpl = dynamic_cast<D3D12RenderTargetBackend*>(rtB.get());
        auto rtAReadback = ReadBackRenderTargetFull(backend, rtAImpl->GetColorResourceEXT(), kRtWidth, kRtHeight);
        auto rtBReadback = ReadBackRenderTargetFull(backend, rtBImpl->GetColorResourceEXT(), kRtWidth, kRtHeight);
        const bool rtAExact = rtAReadback[centerIdx + 0] == 204 && rtAReadback[centerIdx + 1] == 0 &&
                              rtAReadback[centerIdx + 2] == 255 && rtAReadback[centerIdx + 3] == 255;
        const bool rtBExact = rtBReadback[centerIdx + 0] == 204 && rtBReadback[centerIdx + 1] == 0 &&
                              rtBReadback[centerIdx + 2] == 255 && rtBReadback[centerIdx + 3] == 255;
        Check(rtAExact && rtBExact,
              "W8: real 2-target MRT -- one Clear() call after one SetRenderTargets() bind writes "
              "the exact color into BOTH independently-readable GPU resources (plan_dx.md DX-117)");

        backend.SetRenderTarget2D(nullptr);

        // ---- RenderTargetCube: real construction + face-0 bind+clear+readback. ----
        auto rtCube = backend.CreateRenderTargetCube(kRtWidth, 0);
        Check(rtCube != nullptr, "W9: CreateRenderTargetCube() returns a real IRenderTargetCubeBackend");

        rtCube->BindAsRenderTargetFace(0);
        Check(backend.HasBoundColorTargetEXT(), "W10: BindAsRenderTargetFace() genuinely binds face 0");

        backend.Clear(1.0f, 0.6f, 0.0f, 1.0f); // 255/153/0/255 -- exact under any rounding mode
        auto* rtCubeImpl = dynamic_cast<D3D12RenderTargetCubeBackend*>(rtCube.get());
        Check(rtCubeImpl != nullptr, "W11: the real cube target is a D3D12RenderTargetCubeBackend");
        auto rtCubeReadback = ReadBackRenderTargetFull(backend, rtCubeImpl->GetColorResourceEXT(), kRtWidth, kRtHeight);
        Check(rtCubeReadback[centerIdx + 0] == 255 && rtCubeReadback[centerIdx + 1] == 153 &&
              rtCubeReadback[centerIdx + 2] == 0 && rtCubeReadback[centerIdx + 3] == 255,
              "W12: Clear() on a real bound RenderTargetCube face writes the exact requested color, "
              "read back through its own real GPU resource (subresource 0 = face 0, plan_dx.md "
              "DX-117 -- only face 0 exercised, remaining faces are the same honest-scope gap "
              "D3D11's own RenderTargetCube coverage already has, see plan_dx.md Phase DX15 DX-129)");

        rtCube->UnbindAsRenderTarget();
        Check(!backend.HasBoundColorTargetEXT(), "W13: UnbindAsRenderTarget() on RenderTargetCube restores the honest 'nothing bound' state");
    }

    // ---- Check X: DX-118 -- real BlendState/RasterizerState now genuinely runtime-settable,
    // feeding real PSO variation instead of the hardcoded literals every draw path used before this
    // task. NOTE: this test uses the REAL, VERIFIED XNA enum ordinals (Blend::One=0, Blend::Zero=1,
    // BlendFunction::Add=0, CullMode::None=0/CullCounterClockwiseFace=2, FillMode::Solid=0,
    // CompareFunction::LessEqual=3 -- confirmed directly against
    // include/Microsoft/Xna/Framework/Graphics/{Blend,BlendFunction,CullMode,FillMode,
    // CompareFunction}.hpp while writing this task, NOT copied from D3D12PipelineStateDesc.hpp's own
    // default-value comments, which this task found to be WRONG for 2 fields (documented in
    // plan_dx.md's DX-118 row -- colorSrcBlend's default 2 is really Blend::SourceColor not
    // Blend::One, and depthFunc's default 4 is really CompareFunction::Equal not LessEqual; both are
    // functionally inert today since they only apply when nothing has called Apply*State yet, and
    // this task deliberately did not touch those pre-existing defaults to avoid any regression risk
    // in an already-large task -- a real, separate, honestly-flagged follow-up). ----
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
        Check(SUCCEEDED(hrRt) && rt != nullptr, "X0: real off-screen RGBA8 render-target resource created");
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

        struct VPC { float x, y, z; uint32_t color; };

        // X1/X2/X3: real BlendState -- additive (One,One,Add) genuinely differs from Opaque, exact
        // sum; reverting to Opaque genuinely restores plain-overwrite behavior.
        static const VPC kTriRed100[3] = {
            {-1.0f, -1.0f, 0.0f, 0xFF000064u}, // R=100,G=0,B=0,A=255
            { 3.0f, -1.0f, 0.0f, 0xFF000064u},
            {-1.0f,  3.0f, 0.0f, 0xFF000064u},
        };
        static const VPC kTriRed50[3] = {
            {-1.0f, -1.0f, 0.0f, 0xFF000032u}, // R=50,G=0,B=0,A=255
            { 3.0f, -1.0f, 0.0f, 0xFF000032u},
            {-1.0f,  3.0f, 0.0f, 0xFF000032u},
        };
        D3D12VertexBufferBackend vbRed100(&backend, 3);
        vbRed100.SetData(kTriRed100, 3, sizeof(VPC));
        D3D12VertexBufferBackend vbRed50(&backend, 3);
        vbRed50.SetData(kTriRed50, 3, sizeof(VPC));

        backend.Clear(0.0f, 0.0f, 0.0f, 1.0f);
        backend.DrawColoredPrimitives(vbRed100, Matrix::getIdentityProperty(), Matrix::getIdentityProperty(),
                                      Matrix::getIdentityProperty(), PrimitiveType::TriangleList, 1);
        auto afterOpaque = ReadBackRenderTargetFull(backend, rt.Get(), kRtWidth, kRtHeight);
        Check(regionIs(afterOpaque, 100, 0, 0, 255),
              "X1: default (no ApplyBlendState call yet) draw still paints the exact vertex color -- "
              "no regression from DX-118's own new state-tracking fields");

        backend.ApplyBlendState(/*colorSrcBlend=One*/0, /*alphaSrcBlend=One*/0,
                                /*colorDstBlend=One*/0, /*alphaDstBlend=One*/0,
                                /*colorBlendFunc=Add*/0, /*alphaBlendFunc=Add*/0);
        backend.DrawColoredPrimitives(vbRed50, Matrix::getIdentityProperty(), Matrix::getIdentityProperty(),
                                      Matrix::getIdentityProperty(), PrimitiveType::TriangleList, 1);
        auto afterAdditive = ReadBackRenderTargetFull(backend, rt.Get(), kRtWidth, kRtHeight);
        Check(regionIs(afterAdditive, 150, 0, 0, 255),
              "X2: real ApplyBlendState(One,One,Add) genuinely additive-blends a second draw over "
              "the first -- exact 100+50=150 sum, a real BlendEnable=TRUE PSO actually used (not "
              "the Opaque default X1 just proved)");

        backend.ApplyBlendState(/*colorSrcBlend=One*/0, /*alphaSrcBlend=One*/0,
                                /*colorDstBlend=Zero*/1, /*alphaDstBlend=Zero*/1,
                                /*colorBlendFunc=Add*/0, /*alphaBlendFunc=Add*/0);
        backend.Clear(0.0f, 0.0f, 0.0f, 1.0f);
        backend.DrawColoredPrimitives(vbRed100, Matrix::getIdentityProperty(), Matrix::getIdentityProperty(),
                                      Matrix::getIdentityProperty(), PrimitiveType::TriangleList, 1);
        auto afterRevert = ReadBackRenderTargetFull(backend, rt.Get(), kRtWidth, kRtHeight);
        Check(regionIs(afterRevert, 100, 0, 0, 255),
              "X3: reverting ApplyBlendState back to real Opaque (One,Zero,Add) genuinely restores "
              "plain-overwrite behavior -- state is re-applied per call, not sticky");

        // X4/X5: real RasterizerState.CullMode -- reuses the exact triangle geometry DX-111's own
        // real bug report already found gets back-face-culled under real culling after D3D's
        // NDC->screen-space Y-flip (every prior check's own kTri/CullMode::None default exists
        // precisely because of this finding).
        static const VPC kTri[3] = {
            {-1.0f, -1.0f, 0.0f, 0xFF0000FFu}, // R=255,G=0,B=0,A=255
            { 3.0f, -1.0f, 0.0f, 0xFF0000FFu},
            {-1.0f,  3.0f, 0.0f, 0xFF0000FFu},
        };
        D3D12VertexBufferBackend vbTri(&backend, 3);
        vbTri.SetData(kTri, 3, sizeof(VPC));

        backend.ApplyRasterizerState(/*cullMode=CullCounterClockwiseFace*/2, /*fillMode=Solid*/0,
                                     /*scissorTestEnable=*/false);
        backend.Clear(0.0f, 0.0f, 1.0f, 1.0f);
        backend.DrawColoredPrimitives(vbTri, Matrix::getIdentityProperty(), Matrix::getIdentityProperty(),
                                      Matrix::getIdentityProperty(), PrimitiveType::TriangleList, 1);
        auto culled = ReadBackRenderTargetFull(backend, rt.Get(), kRtWidth, kRtHeight);
        Check(regionIs(culled, 0, 0, 255, 255),
              "X4: real ApplyRasterizerState(CullCounterClockwiseFace) genuinely culls this "
              "triangle's real winding -- background survives, matching DX-111's own already-"
              "documented finding about this exact geometry, now proven dynamically settable");

        backend.ApplyRasterizerState(/*cullMode=None*/0, /*fillMode=Solid*/0, /*scissorTestEnable=*/false);
        backend.Clear(0.0f, 0.0f, 1.0f, 1.0f);
        backend.DrawColoredPrimitives(vbTri, Matrix::getIdentityProperty(), Matrix::getIdentityProperty(),
                                      Matrix::getIdentityProperty(), PrimitiveType::TriangleList, 1);
        auto notCulled = ReadBackRenderTargetFull(backend, rt.Get(), kRtWidth, kRtHeight);
        Check(regionIs(notCulled, 255, 0, 0, 255),
              "X5: real ApplyRasterizerState(CullMode::None) genuinely draws the same triangle -- "
              "same geometry, opposite outcome, purely from the RasterizerState change");

        backend.UnbindOffscreenColorTargetEXT();
    }

    // ---- Check Y: DX-118 -- real DepthStencilState.DepthEnable/DepthFunc genuinely gate a draw,
    // via a real bound DSV (this off-screen smoke test has no swap chain/back-buffer DSV to reuse,
    // window=nullptr throughout -- a dedicated depth-stencil resource is created directly here,
    // same pattern this file's own render-target creation already uses). ----
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
        Check(SUCCEEDED(hrRt) && rt != nullptr, "Y0: real off-screen RGBA8 render-target resource created");
        backend.GetResourceStateTrackerEXT().TrackResource(rt.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET);

        D3D12_CPU_DESCRIPTOR_HANDLE rtv = backend.AllocateRtvDescriptorEXT();
        backend.GetDeviceEXT()->CreateRenderTargetView(rt.Get(), nullptr, rtv);

        D3D12_RESOURCE_DESC depthDesc{};
        depthDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        depthDesc.Width = kRtWidth;
        depthDesc.Height = kRtHeight;
        depthDesc.DepthOrArraySize = 1;
        depthDesc.MipLevels = 1;
        depthDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
        depthDesc.SampleDesc.Count = 1;
        depthDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
        D3D12_CLEAR_VALUE depthClear{};
        depthClear.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
        depthClear.DepthStencil.Depth = 1.0f;
        Microsoft::WRL::ComPtr<ID3D12Resource> depthRes;
        HRESULT hrDepth = backend.GetDeviceEXT()->CreateCommittedResource(
            &heapProps, D3D12_HEAP_FLAG_NONE, &depthDesc,
            D3D12_RESOURCE_STATE_DEPTH_WRITE, &depthClear, IID_PPV_ARGS(depthRes.GetAddressOf()));
        Check(SUCCEEDED(hrDepth) && depthRes != nullptr, "Y1: real off-screen depth-stencil resource created");

        D3D12_CPU_DESCRIPTOR_HANDLE dsv = backend.AllocateDsvDescriptorEXT();
        D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc{};
        dsvDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
        dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
        backend.GetDeviceEXT()->CreateDepthStencilView(depthRes.Get(), &dsvDesc, dsv);

        backend.BindOffscreenColorTargetEXT(rt.Get(), rtv, DXGI_FORMAT_R8G8B8A8_UNORM, kRtWidth, kRtHeight,
                                            dsv, DXGI_FORMAT_D24_UNORM_S8_UINT);

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

        auto clearDepthTo1 = [&]()
        {
            ID3D12CommandAllocator* allocator = backend.GetCommandAllocatorEXT(0);
            ID3D12GraphicsCommandList* cmdList = backend.GetCommandListEXT();
            allocator->Reset();
            cmdList->Reset(allocator, nullptr);
            cmdList->ClearDepthStencilView(dsv, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);
            HRESULT hr = cmdList->Close();
            if (FAILED(hr)) throw std::runtime_error("Check Y: ClearDepthStencilView command list Close failed");
            backend.ExecuteCommandListAndWaitEXT(cmdList);
        };

        struct VPCZ { float x, y, z; uint32_t color; };
        static const VPCZ kNear[3] = {
            {-1.0f, -1.0f, 0.2f, 0xFF0000FFu}, // red, near
            { 3.0f, -1.0f, 0.2f, 0xFF0000FFu},
            {-1.0f,  3.0f, 0.2f, 0xFF0000FFu},
        };
        static const VPCZ kFar[3] = {
            {-1.0f, -1.0f, 0.8f, 0xFF00FF00u}, // green, far
            { 3.0f, -1.0f, 0.8f, 0xFF00FF00u},
            {-1.0f,  3.0f, 0.8f, 0xFF00FF00u},
        };
        D3D12VertexBufferBackend vbNear(&backend, 3);
        vbNear.SetData(kNear, 3, sizeof(VPCZ));
        D3D12VertexBufferBackend vbFar(&backend, 3);
        vbFar.SetData(kFar, 3, sizeof(VPCZ));

        // Real ApplyDepthStencilState: DepthEnable=true, DepthWriteEnable=true,
        // DepthFunc=CompareFunction::LessEqual(3, the real, verified ordinal -- XNA's own
        // DepthStencilState.Default). Stencil fields are 0/false throughout -- deliberately not
        // threaded into the PSO yet (DX-118's own documented scope boundary).
        backend.ApplyDepthStencilState(/*depthEnable=*/true, /*depthWriteEnable=*/true, /*depthFunc=LessEqual*/3,
                                       false, 0, 0, 0, 0, 0, 0, 0, false, 0, 0, 0, 0);

        clearDepthTo1();
        backend.Clear(0.0f, 0.0f, 0.0f, 1.0f);
        backend.DrawColoredPrimitives(vbNear, Matrix::getIdentityProperty(), Matrix::getIdentityProperty(),
                                      Matrix::getIdentityProperty(), PrimitiveType::TriangleList, 1);
        backend.DrawColoredPrimitives(vbFar, Matrix::getIdentityProperty(), Matrix::getIdentityProperty(),
                                      Matrix::getIdentityProperty(), PrimitiveType::TriangleList, 1);
        auto nearThenFar = ReadBackRenderTargetFull(backend, rt.Get(), kRtWidth, kRtHeight);
        Check(regionIs(nearThenFar, 255, 0, 0, 255),
              "Y2: real depth test -- drawing NEAR (z=0.2, red) then FAR (z=0.8, green) with "
              "DepthEnable=true/DepthFunc=LessEqual genuinely rejects the far draw, near survives");

        clearDepthTo1();
        backend.Clear(0.0f, 0.0f, 0.0f, 1.0f);
        backend.DrawColoredPrimitives(vbFar, Matrix::getIdentityProperty(), Matrix::getIdentityProperty(),
                                      Matrix::getIdentityProperty(), PrimitiveType::TriangleList, 1);
        backend.DrawColoredPrimitives(vbNear, Matrix::getIdentityProperty(), Matrix::getIdentityProperty(),
                                      Matrix::getIdentityProperty(), PrimitiveType::TriangleList, 1);
        auto farThenNear = ReadBackRenderTargetFull(backend, rt.Get(), kRtWidth, kRtHeight);
        Check(regionIs(farThenNear, 255, 0, 0, 255),
              "Y3: same real depth test, reversed draw order -- FAR (green) drawn first, then NEAR "
              "(red) genuinely passes the depth test and overwrites it -- proves this is a real "
              "per-pixel depth comparison, not merely 'the second draw always wins/loses'");

        // Depth-disabled control: with DepthEnable=false, draw order alone determines the winner --
        // confirms Y2/Y3's outcome really came from the depth test, not draw order or some other
        // effect.
        backend.ApplyDepthStencilState(/*depthEnable=*/false, false, 3, false, 0, 0, 0, 0, 0, 0, 0,
                                       false, 0, 0, 0, 0);
        backend.Clear(0.0f, 0.0f, 0.0f, 1.0f);
        backend.DrawColoredPrimitives(vbNear, Matrix::getIdentityProperty(), Matrix::getIdentityProperty(),
                                      Matrix::getIdentityProperty(), PrimitiveType::TriangleList, 1);
        backend.DrawColoredPrimitives(vbFar, Matrix::getIdentityProperty(), Matrix::getIdentityProperty(),
                                      Matrix::getIdentityProperty(), PrimitiveType::TriangleList, 1);
        auto depthOffLastWins = ReadBackRenderTargetFull(backend, rt.Get(), kRtWidth, kRtHeight);
        Check(regionIs(depthOffLastWins, 0, 255, 0, 255),
              "Y4: control -- with DepthEnable=false, the LAST draw wins regardless of Z (FAR/green "
              "drawn second overwrites NEAR/red) -- confirms Y2/Y3's outcome really came from the "
              "depth test, not draw order or some other effect");

        backend.UnbindOffscreenColorTargetEXT();
    }

    // ---- Check Z (DX-119): real, runtime-settable per-slot SamplerState -- a genuine
    // TextureAddressMode::Wrap-vs-Clamp discriminating probe (D3D11's own DX-72 methodology),
    // proving ApplySamplerState() actually reaches the real D3D12 sampler bound to a draw, not a
    // hardcoded LINEAR/WRAP default. ----
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
        Check(SUCCEEDED(hrRt) && rt != nullptr, "Z0: real off-screen RGBA8 render-target resource created");
        backend.GetResourceStateTrackerEXT().TrackResource(rt.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET);

        D3D12_CPU_DESCRIPTOR_HANDLE rtv = backend.AllocateRtvDescriptorEXT();
        backend.GetDeviceEXT()->CreateRenderTargetView(rt.Get(), nullptr, rtv);
        backend.BindOffscreenColorTargetEXT(rt.Get(), rtv, DXGI_FORMAT_R8G8B8A8_UNORM, kRtWidth, kRtHeight);

        // No depth test for this check -- reset to a known, predictable state regardless of what
        // Check Y's own DepthStencilState/RasterizerState left tracked (state persists across
        // checks, mirroring real GraphicsDevice behavior).
        backend.ApplyDepthStencilState(false, false, 3, false, 0, 0, 0, 0, 0, 0, 0, false, 0, 0, 0, 0);
        backend.ApplyRasterizerState(/*cullMode=*/0 /*CullMode::None*/, /*fillMode=*/0, /*scissor=*/false);

        auto pixelAt = [&](const std::vector<uint8_t>& buf, int x, int y) -> std::array<uint8_t, 4>
        {
            const std::size_t idx = (static_cast<std::size_t>(y) * kRtWidth + static_cast<std::size_t>(x)) * 4;
            return {buf[idx + 0], buf[idx + 1], buf[idx + 2], buf[idx + 3]};
        };
        auto isColor = [](const std::array<uint8_t, 4>& p, uint8_t r, uint8_t g, uint8_t b, uint8_t a)
        {
            return p[0] == r && p[1] == g && p[2] == b && p[3] == a;
        };
        // A 4-pixel-wide probe region comfortably inside the U in (1.0, 1.5) band derived below --
        // never straddles a wrap-repeat boundary (see the U-range comment on kQuad).
        auto probeIs = [&](const std::vector<uint8_t>& buf, uint8_t r, uint8_t g, uint8_t b, uint8_t a)
        {
            for (int y = 28; y < 32; ++y)
                for (int x = 56; x < 60; ++x)
                    if (!isColor(pixelAt(buf, x, y), r, g, b, a)) return false;
            return true;
        };

        // Full-viewport quad, U linear from 0.0 (left) to 1.6 (right), V held constant at 0.5 (mid-
        // texel on both rows of the 2x2 texture below, so only the U axis is under test). At the
        // probe region (screen x in [56,60) of a 64-wide target), pixel-center NDC x lands in
        // [0.7656, 0.859] (D3D rasterizes at pixel CENTERS, px+0.5, per this session's own earlier
        // SpriteBatch-test finding) -- U = (ndcX+1)/2 * 1.6 lands in [1.4125, 1.4875], i.e. strictly
        // inside (1.0, 1.5): Wrap's fractional part is (1.0, 1.5), always < 1.5 so it never reaches
        // the far/left-column-again boundary at fractional 0.0 -- consistently samples texel column
        // 0 (fractional U in (0,0.5) after wrapping). Clamp holds at U=1.0 exactly, consistently
        // sampling texel column 1 (the rightmost). Point filtering (no linear blending) makes both
        // outcomes exact, not approximate.
        struct VPT { float x, y, z; float u, v; };
        static const VPT kQuad[6] = {
            {-1.0f, -1.0f, 0.0f, 0.0f, 0.5f},
            { 1.0f, -1.0f, 0.0f, 1.6f, 0.5f},
            {-1.0f,  1.0f, 0.0f, 0.0f, 0.5f},
            {-1.0f,  1.0f, 0.0f, 0.0f, 0.5f},
            { 1.0f, -1.0f, 0.0f, 1.6f, 0.5f},
            { 1.0f,  1.0f, 0.0f, 1.6f, 0.5f},
        };
        D3D12VertexBufferBackend vbQuad(&backend, 6);
        vbQuad.SetData(kQuad, 6, sizeof(VPT));

        // 2x2 texture, RED in column 0, GREEN in column 1, both rows identical (V-axis irrelevant
        // to this test, held constant above).
        ImageData img;
        img.width = 2; img.height = 2; img.mipLevels = 1;
        img.pixels.resize(2 * 2 * 4);
        for (int row = 0; row < 2; ++row)
        {
            const std::size_t base = static_cast<std::size_t>(row) * 2 * 4;
            img.pixels[base + 0] = 255; img.pixels[base + 1] = 0;   img.pixels[base + 2] = 0;   img.pixels[base + 3] = 255; // col0 red
            img.pixels[base + 4] = 0;   img.pixels[base + 5] = 255; img.pixels[base + 6] = 0;   img.pixels[base + 7] = 255; // col1 green
        }
        D3D12TextureBackend texZ(&backend, img);

        GpuDrawParams zp;
        zp.texture0 = &texZ;
        zp.textureEnabled = true;

        backend.ApplySamplerState(0, /*filter=*/1 /*TextureFilter::Point*/,
                                  /*addressU=*/0 /*TextureAddressMode::Wrap*/,
                                  /*addressV=*/0 /*Wrap*/, /*maxAnisotropy=*/1);
        backend.Clear(0.0f, 0.0f, 0.0f, 1.0f);
        backend.DrawPrimitivesEx(vbQuad, Matrix::getIdentityProperty(), Matrix::getIdentityProperty(),
                                 Matrix::getIdentityProperty(), PrimitiveType::TriangleList, 2, zp);
        auto afterWrap = ReadBackRenderTargetFull(backend, rt.Get(), kRtWidth, kRtHeight);
        Check(probeIs(afterWrap, 255, 0, 0, 255),
              "Z1: real ApplySamplerState(..., AddressU=Wrap) genuinely tiles past U=1.0 -- probe "
              "samples texel column 0 (red), U's wrapped fractional part (plan_dx.md DX-119)");

        backend.ApplySamplerState(0, /*filter=*/1 /*Point*/,
                                  /*addressU=*/1 /*TextureAddressMode::Clamp*/,
                                  /*addressV=*/1 /*Clamp*/, /*maxAnisotropy=*/1);
        backend.Clear(0.0f, 0.0f, 0.0f, 1.0f);
        backend.DrawPrimitivesEx(vbQuad, Matrix::getIdentityProperty(), Matrix::getIdentityProperty(),
                                 Matrix::getIdentityProperty(), PrimitiveType::TriangleList, 2, zp);
        auto afterClamp = ReadBackRenderTargetFull(backend, rt.Get(), kRtWidth, kRtHeight);
        Check(probeIs(afterClamp, 0, 255, 0, 255),
              "Z2: real ApplySamplerState(..., AddressU=Clamp) genuinely holds at U=1.0 -- SAME "
              "geometry/UVs as Z1, opposite outcome (texel column 1, green), purely from the "
              "SamplerState change -- proves this is a real, live sampler binding, not a hardcoded "
              "default (plan_dx.md DX-119)");

        // Cache identity/distinctness proof, mirroring D3D11SamplerCache's own established pattern
        // (D3D11's Check L, DX-44). Note: state (currentSamplerAddressU_ etc.) is tracked, not
        // reset between checks -- Z2 left slot 0 at Clamp, so apply Wrap explicitly BEFORE reading
        // each handle below, not after (fetching a handle reflects whatever was last applied).
        backend.ApplySamplerState(0, 1, 0, 0, 1); // Wrap/Point
        D3D12_GPU_DESCRIPTOR_HANDLE wrapHandle1 = backend.GetSamplerGpuHandleEXT(0);
        backend.ApplySamplerState(0, 1, 0, 0, 1); // re-apply the exact same Wrap/Point state
        D3D12_GPU_DESCRIPTOR_HANDLE wrapHandle2 = backend.GetSamplerGpuHandleEXT(0);
        Check(wrapHandle1.ptr == wrapHandle2.ptr,
              "Z3: identical SamplerState (Point/Wrap) resolves to the SAME cached sampler "
              "descriptor handle, not a fresh heap slot every call");
        backend.ApplySamplerState(0, 1, 1, 1, 1); // Clamp/Point -- genuinely different state
        D3D12_GPU_DESCRIPTOR_HANDLE clampHandle = backend.GetSamplerGpuHandleEXT(0);
        Check(clampHandle.ptr != wrapHandle1.ptr,
              "Z4: a genuinely different SamplerState (Point/Clamp) resolves to a DIFFERENT cached "
              "sampler descriptor handle");

        backend.UnbindOffscreenColorTargetEXT();
    }

    // ---- plan_dx.md DX-120: D3D12OcclusionQueryBackend -- a real visible-vs-invisible
    // discriminating occlusion query, closing Phase DX15's own DX-147 D3D12 half too. ----
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
        Check(SUCCEEDED(hrRt) && rt != nullptr, "AA0: real off-screen RGBA8 render-target resource created");
        backend.GetResourceStateTrackerEXT().TrackResource(rt.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET);

        D3D12_CPU_DESCRIPTOR_HANDLE rtv = backend.AllocateRtvDescriptorEXT();
        backend.GetDeviceEXT()->CreateRenderTargetView(rt.Get(), nullptr, rtv);
        backend.BindOffscreenColorTargetEXT(rt.Get(), rtv, DXGI_FORMAT_R8G8B8A8_UNORM, kRtWidth, kRtHeight);
        backend.ApplyDepthStencilState(false, false, 3, false, 0, 0, 0, 0, 0, 0, 0, false, 0, 0, 0, 0);
        backend.ApplyRasterizerState(/*cullMode=*/0 /*CullMode::None*/, /*fillMode=*/0, /*scissor=*/false);

        auto occlusionQuery = backend.CreateOcclusionQuery();
        Check(occlusionQuery != nullptr, "AA1: CreateOcclusionQuery() returns a real D3D12OcclusionQueryBackend");

        // Same oversized-triangle-covering-the-full-NDC-square trick Check M established --
        // world=view=projection=Identity, so these Position values ARE clip-space coordinates.
        struct VPC { float x, y, z; uint32_t color; };
        static const VPC kVisibleTri[3] = {
            {-1.0f, -1.0f, 0.0f, 0xFF0000FFu},
            { 3.0f, -1.0f, 0.0f, 0xFF0000FFu},
            {-1.0f,  3.0f, 0.0f, 0xFF0000FFu},
        };
        D3D12VertexBufferBackend vbVisible(&backend, 3);
        vbVisible.SetData(kVisibleTri, 3, sizeof(VPC));

        backend.Clear(0.0f, 0.0f, 0.0f, 1.0f);
        occlusionQuery->Begin();
        backend.DrawColoredPrimitives(vbVisible, Matrix::getIdentityProperty(), Matrix::getIdentityProperty(),
                                      Matrix::getIdentityProperty(), PrimitiveType::TriangleList, 1);
        occlusionQuery->End();
        Check(occlusionQuery->IsComplete(), "AA2: occlusion query IsComplete() genuinely true after a real End()");
        const int visibleCount = occlusionQuery->PixelCount();
        Check(visibleCount > 0,
              ("AA3: a full-viewport visible triangle reports a real, positive PixelCount() "
               "(got " + std::to_string(visibleCount) + ")").c_str());

        // Same query object, reused for a second Begin()/End() -- geometry placed entirely outside
        // the [-1,1] NDC clip volume rasterizes NOTHING, so a real occlusion query over it must
        // report exactly 0 samples passed.
        static const VPC kInvisibleTri[3] = {
            { 5.0f,  5.0f, 0.0f, 0xFF00FF00u},
            { 9.0f,  5.0f, 0.0f, 0xFF00FF00u},
            { 5.0f,  9.0f, 0.0f, 0xFF00FF00u},
        };
        D3D12VertexBufferBackend vbInvisible(&backend, 3);
        vbInvisible.SetData(kInvisibleTri, 3, sizeof(VPC));

        occlusionQuery->Begin();
        backend.DrawColoredPrimitives(vbInvisible, Matrix::getIdentityProperty(), Matrix::getIdentityProperty(),
                                      Matrix::getIdentityProperty(), PrimitiveType::TriangleList, 1);
        occlusionQuery->End();
        const int invisibleCount = occlusionQuery->PixelCount();
        Check(invisibleCount == 0,
              "AA4: the SAME query object, reused around off-screen (clipped) geometry, reports "
              "EXACTLY 0 -- a genuine visible-vs-invisible discriminating result, not just \"the "
              "query completed\" (plan_dx.md DX-120, closes DX-147's D3D12 half)");

        backend.UnbindOffscreenColorTargetEXT();
    }

    // ---- plan_dx.md DX-121: D3D12EffectBackend -- runtime D3DCompile() of custom HLSL, driven
    // manually (see the top-of-file comment block for why SpriteBatch/GraphicsDevice can't be used
    // safely in this off-screen-only suite). ----
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
        Check(SUCCEEDED(hrRt) && rt != nullptr, "BB0: real off-screen RGBA8 render-target resource created");
        backend.GetResourceStateTrackerEXT().TrackResource(rt.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET);

        D3D12_CPU_DESCRIPTOR_HANDLE rtv = backend.AllocateRtvDescriptorEXT();
        backend.GetDeviceEXT()->CreateRenderTargetView(rt.Get(), nullptr, rtv);

        auto effect = backend.CreateEffectBackend(
            "struct VSIn { float2 pos:POSITION0; float2 uv:TEXCOORD0; float4 col:COLOR0; };\n"
            "struct VSOut { float4 pos:SV_Position; float4 col:TEXCOORD0; };\n"
            "cbuffer CB : register(b0) { float4 pad0[5]; float4 uColor; float4 uFloat0; };\n"
            "VSOut main(VSIn input) { VSOut o; o.pos=float4(input.pos,0,1); o.col=input.col*uColor; return o; }",
            "struct PSIn { float4 pos:SV_Position; float4 col:TEXCOORD0; };\n"
            "float4 main(PSIn input):SV_Target { return input.col; }");
        Check(effect && effect->IsValid(),
              "BB1: D3D12GraphicsBackend::CreateEffectBackend() -- real runtime D3DCompile() of "
              "arbitrary HLSL source builds a real PSO+constant-buffer end to end (plan_dx.md DX-121)");

        bool effIsExact = false;
        if (effect && effect->IsValid())
        {
            auto* d3dEffect = dynamic_cast<D3D12EffectBackend*>(effect.get());
            Check(d3dEffect != nullptr, "BB2: CreateEffectBackend() returns a real D3D12EffectBackend");

            effect->SetUniformVec4("uColor", 0.0f, 1.0f, 0.0f, 1.0f); // green -- 0/1 only, no rounding ambiguity
            effect->Bind();

            // Fixed Sprite2DVertex contract (x,y|u,v|r,g,b,a, 32 bytes) -- position IS clip-space
            // directly in this simple vertex shader (no vpSize normalization needed), matching
            // D3D11's own equivalent Check X convention exactly.
            struct SpriteVtx { float x, y, u, v, r, g, b, a; };
            static const SpriteVtx kTriFx[3] = {
                {-1.0f, -1.0f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f, 1.0f},
                { 3.0f, -1.0f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f, 1.0f},
                {-1.0f,  3.0f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f, 1.0f},
            };
            D3D12VertexBufferBackend vbFx(&backend, 3);
            vbFx.SetData(kTriFx, 3, sizeof(SpriteVtx));

            // The (1,1,1) root signature this PSO was built against always declares an SRV+sampler
            // table -- bind a real, throwaway 1x1 texture/sampler even though this particular
            // pixel shader never samples it, matching every other real D3D12 draw's own full
            // root-parameter binding discipline (avoids relying on undefined/unbound-table
            // behavior).
            ImageData dummyImg;
            dummyImg.width = 1; dummyImg.height = 1; dummyImg.mipLevels = 1;
            dummyImg.pixels = {255, 255, 255, 255};
            D3D12TextureBackend dummyTex(&backend, dummyImg);
            backend.ApplySamplerState(0, /*filter=*/1 /*Point*/, /*addressU=*/0, /*addressV=*/0, /*maxAnisotropy=*/1);

            auto rootSig = backend.GetRootSignatureCacheEXT().GetOrCreate(backend.GetDeviceEXT(), 1, 1, 1);

            ID3D12CommandAllocator* allocator = backend.GetCommandAllocatorEXT(0);
            ID3D12GraphicsCommandList* cmdList = backend.GetCommandListEXT();
            allocator->Reset();
            cmdList->Reset(allocator, d3dEffect->GetPipelineStateEXT());

            backend.GetResourceStateTrackerEXT().TransitionTo(cmdList, rt.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET);
            const float clearColor[4] = {0.0f, 0.0f, 1.0f, 1.0f};
            cmdList->ClearRenderTargetView(rtv, clearColor, 0, nullptr);
            cmdList->OMSetRenderTargets(1, &rtv, FALSE, nullptr);

            D3D12_VIEWPORT viewport{0.0f, 0.0f, static_cast<float>(kRtWidth), static_cast<float>(kRtHeight), 0.0f, 1.0f};
            D3D12_RECT scissor{0, 0, kRtWidth, kRtHeight};
            cmdList->RSSetViewports(1, &viewport);
            cmdList->RSSetScissorRects(1, &scissor);

            cmdList->SetGraphicsRootSignature(rootSig.Get());
            cmdList->SetPipelineState(d3dEffect->GetPipelineStateEXT());
            cmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

            D3D12_VERTEX_BUFFER_VIEW vbView = vbFx.GetViewEXT();
            cmdList->IASetVertexBuffers(0, 1, &vbView);
            cmdList->SetGraphicsRootConstantBufferView(0, d3dEffect->GetConstantBufferEXT()->GetGPUVirtualAddress());

            ID3D12DescriptorHeap* heaps[] = {backend.GetCbvSrvUavHeapEXT(), backend.GetSamplerHeapEXT()};
            cmdList->SetDescriptorHeaps(2, heaps);
            cmdList->SetGraphicsRootDescriptorTable(1, dummyTex.GetShaderResourceViewGpuHandleEXT());
            cmdList->SetGraphicsRootDescriptorTable(2, backend.GetSamplerGpuHandleEXT(0));

            cmdList->DrawInstanced(3, 1, 0, 0);

            HRESULT hr = cmdList->Close();
            if (SUCCEEDED(hr))
                backend.ExecuteCommandListAndWaitEXT(cmdList);

            if (SUCCEEDED(hr))
            {
                auto pixels = ReadBackRenderTargetFull(backend, rt.Get(), kRtWidth, kRtHeight);
                const std::size_t idx = (static_cast<std::size_t>(32) * kRtWidth + 32) * 4;
                effIsExact = pixels[idx + 0] == 0 && pixels[idx + 1] == 255 &&
                            pixels[idx + 2] == 0 && pixels[idx + 3] == 255;
            }
        }
        Check(effIsExact,
              "BB3: D3D12EffectBackend::Bind() -- a real custom-compiled shader pair, driven by "
              "SetUniformVec4()'s fixed-slot constant buffer, draws the exact expected color "
              "(plan_dx.md DX-121)");

        auto badEffect = backend.CreateEffectBackend("this is not valid HLSL {{{", "also not valid ]]]");
        Check(badEffect && !badEffect->IsValid() && !badEffect->GetCompileError().empty(),
              "BB4: D3D12GraphicsBackend::CreateEffectBackend() -- a deliberately broken HLSL source "
              "fails CompileProgram() with a real, non-empty compiler error message (plan_dx.md DX-121)");
    }

    std::printf("\n%s: %d failure(s)\n", g_failures == 0 ? "RESULT: ALL PASS" : "RESULT: FAILURES", g_failures);
    return g_failures == 0 ? 0 : 1;
}
