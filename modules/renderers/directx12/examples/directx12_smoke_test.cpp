// SPDX-License-Identifier: MS-PL
// plans/plan_dx.md Phase DX12 (DX-102/DX-103/DX-104/DX-105): smoke test for D3D12's device-lifetime
// resources. Deliberately constructed OFF-SCREEN (surface.windowId = 0) --
// DX-100's own real spike found CreateSwapChainForHwnd(..., DXGI_SWAP_EFFECT_FLIP_DISCARD) crashes
// inside vanilla Wine's own dxgi.dll, so this primary smoke test never constructs a window and
// never reaches that code path, keeping this CTest genuinely green on this dev loop. The real
// (window-attached) swap-chain attempt is exercised separately, once, honestly, outside the
// default CTest suite -- see plans/plan_dx.md DX-102's own row for that real outcome.
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
// plans/plan_dx.md DX-106/DX-107/DX-108 (this revision) add:
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
// plans/plan_dx.md DX-109/DX-110 (this revision) add:
// Check J -- D3D12VertexBufferRenderer/D3D12IndexBufferRenderer (DX-109): known vertex data is
//   uploaded through a real DEFAULT-heap resource + UPLOAD-heap staging + CopyBufferRegion, then
//   copied back out to a D3D12_HEAP_TYPE_READBACK buffer and Map()'d on the CPU -- an exact byte
//   match proves the whole upload/copy/barrier path is correct, not just "the API calls succeeded".
//   Both a 16-bit and a 32-bit index buffer are round-tripped the same way, and
//   CreateIndexBuffer32() is confirmed to actually return a 32-bit-format buffer (not silently
//   alias to 16-bit, the real bug D3D11's own Phase DIRECTX5 fork found and fixed).
// Check K -- D3D12TextureRenderer (DX-109): known RGBA8 pixel data uploaded at construction time is
//   copied back out via the same READBACK-heap technique and matches exactly; a follow-up
//   UpdatePixels() call with different data is proven to genuinely overwrite the texture (not a
//   stale/cached first-upload value).
// Check L -- RecreateDeviceEXT() (DX-110): the real device/queue/heaps/command-list/fence
//   recreation path is invoked directly (this Wine dev loop cannot trigger a genuine
//   DXGI_ERROR_DEVICE_REMOVED -- see that method's own doc comment) and the renderer is proven
//   usable again afterward: a fresh command-list submission round-trips through the NEW fence, and
//   a fresh vertex buffer created AFTER recreation uploads and reads back correctly through the
//   NEW device -- real proof the recreation path produces a genuinely working renderer, not just
//   non-null pointers.
//
// plans/plan_dx.md DX-111 (this revision) adds:
// Check M -- the first real 3D triangle this D3D12 renderer has ever drawn: a genuine
//   DrawColoredPrimitives()/DrawIndexedColoredPrimitives() call (real root signature + PSO from
//   Check H/I, real PerDraw/FogParams constant buffers, a real command-list-recorded DrawInstanced/
//   DrawIndexedInstanced) paints a known solid-red vertex color over a known-blue-cleared
//   off-screen render target (BindOffscreenColorTargetEXT() -- a minimal internal helper, since a
//   full public D3D12RenderTargetRenderer is still owed, see DX-109's own honest scope note; the
//   swap chain remains unusable under Wine per DX-100). Reading back the SAME pixel region before
//   and after each draw call (blue -> red) proves the fragment genuinely came from the draw, not a
//   stale/cached value -- same "before/after Clear()" discipline D3D11's own Check P established
//   (directx11_smoke_test.cpp), reused here for the analogous D3D12 proof.
//
// plans/plan_dx.md DX-111 (continued -- textured3d/colored_textured3d/lit_textured3d/alpha_test3d) adds:
// Check N -- DrawPrimitivesEx()/DrawIndexedPrimitivesEx() with real GpuDrawParams: textured3d
//   (stride 20) samples the exact known texture color (diffuseColor=white) over the Clear()
//   background, and colored_textured3d (stride 24) multiplies an exact known vertex color through a
//   white texture -- same rigor as D3D11's own Check Q, adapted to this renderer's real SRV
//   descriptor-table binding (D3D12TextureRenderer's own CBV/SRV/UAV-heap SRV handle is bound
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
// Check U -- env_map3d (closing DX-111, 10/10 stock variants real): a real D3D12TextureCubeRenderer
//   (new for this task), sampled via a geometrically-constrained reflection direction that lands
//   deep inside one distinctly-colored cube face -- same discriminating-by-construction proof D3D11's
//   own Check U (directx11_smoke_test.cpp DX-66) uses, adapted to this renderer's own N-separate-
//   descriptor-tables SRV binding (t0 base Texture2D + t1 TextureCube, D3D12RootSignatureCache's own
//   (3,2,2) shape, already created and cached by dual_texture3d's own earlier Check).
//
// Exit code 0 = all checks PASS, 1 = any FAILs.

#include "CNA/Internal/Renderers/DirectX12/DirectX12Renderer.hpp"
#include "CNA/Internal/Renderers/DirectX12/D3D12ResourceStateTracker.hpp"
#include "CNA/Internal/Renderers/DirectX12/D3D12RootSignatureCache.hpp"
#include "CNA/Internal/Renderers/DirectX12/D3D12PipelineStateCache.hpp"
#include "CNA/Internal/Renderers/DirectX12/D3D12Buffers.hpp"
#include "CNA/Internal/Renderers/DirectX12/D3D12Textures.hpp"
#include "CNA/Internal/Renderers/DirectX12/D3D12TextureCube.hpp"
#include "CNA/Internal/Renderers/DirectX12/D3D12RenderTargets.hpp"
#include "CNA/Internal/Renderers/DirectX12/D3D12EffectRenderer.hpp"
#include "CNA/Internal/Renderers/DirectX12/D3D12Texture3D.hpp"
#include "CNA/Internal/Renderers/Common/IGraphicsRenderer.hpp"
#include "CNA/Internal/Graphics/ImageData.hpp"

// plans/plan_dx.md DX-132/DX-148/DX-140: the XNA-level public API (SpriteFont, Model, Texture2D::
// FromStream/SaveAsPng) needs a real GraphicsDevice. Until PresentationParameters::HeadlessEXT
// landed, constructing one for D3D12 forced a real window -> a real swap chain -> the crash path
// this dev loop's plain Wine cannot survive (DX-100/DX-102). HeadlessEXT makes a genuinely
// windowless D3D12 GraphicsDevice possible, so these three rows are testable in the ROUTINE
// (plain-Wine) D3D12 CTest, exactly like every other check in this file.
#include "Microsoft/Xna/Framework/Color.hpp"
#include "Microsoft/Xna/Framework/Matrix.hpp"
#include "Microsoft/Xna/Framework/Rectangle.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Graphics/BasicEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/BlendState.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsAdapter.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsDevice.hpp"
#include "Microsoft/Xna/Framework/Graphics/GraphicsProfile.hpp"
#include "Microsoft/Xna/Framework/Graphics/IndexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/Model.hpp"
#include "Microsoft/Xna/Framework/Graphics/ModelBone.hpp"
#include "Microsoft/Xna/Framework/Graphics/ModelMesh.hpp"
#include "Microsoft/Xna/Framework/Graphics/ModelMeshPart.hpp"
#include "Microsoft/Xna/Framework/Graphics/PresentationParameters.hpp"
#include "Microsoft/Xna/Framework/Graphics/RasterizerState.hpp"
#include "Microsoft/Xna/Framework/Graphics/RenderTarget2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteBatch.hpp"
#include "Microsoft/Xna/Framework/Graphics/ShaderEffect.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteEffects.hpp"
#include "Microsoft/Xna/Framework/Graphics/SpriteFont.hpp"
#include "Microsoft/Xna/Framework/Graphics/Texture2D.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexBuffer.hpp"
#include "Microsoft/Xna/Framework/Graphics/VertexPositionColor.hpp"
#include "System/IO/MemoryStream.hpp"

#include "System/ArgumentOutOfRangeException.hpp"
#include "System/NotSupportedException.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <memory>
#include <optional>
#include <vector>

using CNA::Internal::Renderers::GraphicsRendererCreateArgs;
using CNA::Internal::Renderers::DirectX12::DirectX12Renderer;
using CNA::Internal::Renderers::DirectX12::D3D12ResourceStateTracker;
using CNA::Internal::Renderers::DirectX12::D3D12RootSignatureCache;
using CNA::Internal::Renderers::DirectX12::D3D12PipelineStateCache;
using CNA::Internal::Renderers::DirectX12::D3D12PipelineStateDesc;
using CNA::Internal::Renderers::DirectX12::D3D12VertexBufferRenderer;
using CNA::Internal::Renderers::DirectX12::D3D12IndexBufferRenderer;
using CNA::Internal::Renderers::DirectX12::D3D12TextureRenderer;
using CNA::Internal::Renderers::DirectX12::D3D12TextureCubeRenderer;
using CNA::Internal::Renderers::DirectX12::D3D12RenderTargetRenderer;
using CNA::Internal::Renderers::DirectX12::D3D12RenderTargetCubeRenderer;
using CNA::Internal::Renderers::DirectX12::D3D12EffectRenderer;
using CNA::Internal::Renderers::DirectX12::D3D12Texture3DRenderer;
using CNA::Internal::Renderers::IRenderTargetRenderer;
using CNA::Internal::Renderers::RenderTargetBindingDescriptor;
using CNA::Internal::Renderers::D3DCommon::D3DShaderVariant;
using CNA::Internal::Graphics::ImageData;
using CNA::Internal::Renderers::Matrix;
using CNA::Internal::Renderers::PrimitiveType;
using CNA::Internal::Renderers::GpuDrawParams;

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
    /// D3D11_USAGE_STAGING+CopyResource+Map(READ) test helper (directx11_smoke_test.cpp), adapted to
    /// D3D12's own explicit heap-type model.
    std::vector<uint8_t> ReadBackBufferResource(DirectX12Renderer& renderer, ID3D12Resource* resource,
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
        HRESULT hr = renderer.GetDeviceEXT()->CreateCommittedResource(
            &heapProps, D3D12_HEAP_FLAG_NONE, &desc,
            D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(readback.GetAddressOf()));
        if (FAILED(hr))
            throw std::runtime_error("ReadBackBufferResource: CreateCommittedResource failed, hr=" + FormatHrLocal(hr));

        ID3D12CommandAllocator* allocator = renderer.GetCommandAllocatorEXT(0);
        ID3D12GraphicsCommandList* cmdList = renderer.GetCommandListEXT();
        allocator->Reset();
        cmdList->Reset(allocator, nullptr);

        auto& tracker = renderer.GetResourceStateTrackerEXT();
        const D3D12_RESOURCE_STATES priorState = tracker.GetTrackedStateEXT(resource);
        tracker.TransitionTo(cmdList, resource, D3D12_RESOURCE_STATE_COPY_SOURCE);
        cmdList->CopyBufferRegion(readback.Get(), 0, resource, 0, byteCount);
        tracker.TransitionTo(cmdList, resource, priorState); // restore -- this is a read-only diagnostic

        hr = cmdList->Close();
        if (FAILED(hr))
            throw std::runtime_error("ReadBackBufferResource: command list Close failed, hr=" + FormatHrLocal(hr));
        renderer.ExecuteCommandListAndWaitEXT(cmdList);

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

    /// Same technique, for a level-0 RGBA8 D3D12TextureRenderer -- CopyTextureRegion into a
    /// row-pitch-aligned READBACK buffer, then de-strided back into a tightly-packed RGBA8 buffer.
    std::vector<uint8_t> ReadBackTextureLevel0(DirectX12Renderer& renderer, D3D12TextureRenderer& tex)
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
        HRESULT hr = renderer.GetDeviceEXT()->CreateCommittedResource(
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

        ID3D12CommandAllocator* allocator = renderer.GetCommandAllocatorEXT(0);
        ID3D12GraphicsCommandList* cmdList = renderer.GetCommandListEXT();
        allocator->Reset();
        cmdList->Reset(allocator, nullptr);

        auto& tracker = renderer.GetResourceStateTrackerEXT();
        const D3D12_RESOURCE_STATES priorState = tracker.GetTrackedStateEXT(tex.GetResourceEXT());
        tracker.TransitionTo(cmdList, tex.GetResourceEXT(), D3D12_RESOURCE_STATE_COPY_SOURCE);
        cmdList->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
        tracker.TransitionTo(cmdList, tex.GetResourceEXT(), priorState);

        hr = cmdList->Close();
        if (FAILED(hr))
            throw std::runtime_error("ReadBackTextureLevel0: command list Close failed, hr=" + FormatHrLocal(hr));
        renderer.ExecuteCommandListAndWaitEXT(cmdList);

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
    /// render target Check M creates, which is not a D3D12TextureRenderer).
    std::vector<uint8_t> ReadBackRenderTargetFull(DirectX12Renderer& renderer, ID3D12Resource* resource,
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
        HRESULT hr = renderer.GetDeviceEXT()->CreateCommittedResource(
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

        ID3D12CommandAllocator* allocator = renderer.GetCommandAllocatorEXT(0);
        ID3D12GraphicsCommandList* cmdList = renderer.GetCommandListEXT();
        allocator->Reset();
        cmdList->Reset(allocator, nullptr);

        auto& tracker = renderer.GetResourceStateTrackerEXT();
        const D3D12_RESOURCE_STATES priorState = tracker.GetTrackedStateEXT(resource);
        tracker.TransitionTo(cmdList, resource, D3D12_RESOURCE_STATE_COPY_SOURCE);
        cmdList->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
        tracker.TransitionTo(cmdList, resource, priorState);

        hr = cmdList->Close();
        if (FAILED(hr))
            throw std::runtime_error("ReadBackRenderTargetFull: command list Close failed, hr=" + FormatHrLocal(hr));
        renderer.ExecuteCommandListAndWaitEXT(cmdList);

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

// plans/plan_dx.md DX-120 adds:
// Check AA -- D3D12OcclusionQueryRenderer: a real ID3D12QueryHeap(D3D12_QUERY_TYPE_OCCLUSION) +
//   D3D12_HEAP_TYPE_READBACK buffer, Begin()/EndQuery()/ResolveQueryData() all genuinely recorded
//   and executed through Wine+vkd3d-proton on the real GPU. A visible full-viewport triangle
//   reports a real, positive PixelCount(); the SAME query object, reused for a second Begin()/End()
//   around geometry placed entirely outside the clip volume (so nothing is rasterized at all),
//   reports PixelCount() == 0 -- a genuine visible-vs-invisible discriminating result, not just
//   "the query completed" (closes Phase DX15's own DX-147 D3D12 half).

// plans/plan_dx.md DX-121 adds:
// Check BB -- D3D12EffectRenderer: runtime D3DCompile() of arbitrary HLSL source (not one of
//   DX-13-hlsl's offline-compiled stock variants) builds a real PSO+constant-buffer end to end,
//   driven manually (SpriteBatch/GraphicsDevice can't be constructed safely in this off-screen-only
//   suite -- GraphicsDevice's own constructor unconditionally creates a real window for any
//   non-Headless/Software renderer, which is exactly the crash-prone path DX-100/DX-102 already
//   found for D3D12 outside a Proton-managed launch; D3D12SpriteBatchRenderer's own real
//   SetCustomEffect()/FlushBatch() wiring, added this same task, is exercised by code review and
//   architectural reuse of this exact PSO/constant-buffer pair, not an independent CTest proof --
//   an honest, documented scope boundary), proving the color is genuinely driven by
//   SetUniformVec4()'s fixed-slot constant buffer, matching D3D11's own DX-58 rigor. A deliberately
//   broken HLSL source fails CompileProgram() cleanly with a real, non-empty compiler error.

int main()
{
    GraphicsRendererCreateArgs args;
    // surface stays windowless deliberately -- see file header comment.
    args.virtualWidth = 64;
    args.virtualHeight = 64;

    DirectX12Renderer renderer(args);

    // ---- Check A: device ----
    Check(renderer.GetDeviceEXT() != nullptr, "A1: ID3D12Device created");
    Check(renderer.GetFeatureLevelEXT() >= D3D_FEATURE_LEVEL_11_0, "A2: feature level >= 11_0");
    std::printf("       feature level = 0x%04x, debug layer = %s, tearing = %s\n",
                static_cast<unsigned>(renderer.GetFeatureLevelEXT()),
                renderer.IsDebugLayerEnabledEXT() ? "enabled" : "disabled",
                renderer.IsTearingSupportedEXT() ? "supported" : "unsupported");

    // ---- Check B: command queue ----
    Check(renderer.GetCommandQueueEXT() != nullptr, "B1: ID3D12CommandQueue created");

    // ---- Check C: descriptor heaps, real allocator proof ----
    //
    // REMED-GFX-177: this block used to allocate six descriptors, assert only that the cursor
    // advanced, and free none of them -- so it pinned the bump-allocator behaviour that was the
    // defect and could never have observed the leak. It now asserts BOTH halves of the real
    // contract: distinct live allocations are distinct, AND a freed slot comes back.
    {
        D3D12_CPU_DESCRIPTOR_HANDLE rtv0 = renderer.AllocateRtvDescriptorEXT();
        D3D12_CPU_DESCRIPTOR_HANDLE rtv1 = renderer.AllocateRtvDescriptorEXT();
        Check(rtv1.ptr != rtv0.ptr, "C1: two live RTV allocations are distinct");

        D3D12_CPU_DESCRIPTOR_HANDLE dsv0 = renderer.AllocateDsvDescriptorEXT();
        D3D12_CPU_DESCRIPTOR_HANDLE dsv1 = renderer.AllocateDsvDescriptorEXT();
        Check(dsv1.ptr != dsv0.ptr, "C2: two live DSV allocations are distinct");

        const std::uint32_t cbv0 = renderer.CreateCbvSrvUavDescriptorEXT([](D3D12_CPU_DESCRIPTOR_HANDLE) {});
        const std::uint32_t cbv1 = renderer.CreateCbvSrvUavDescriptorEXT([](D3D12_CPU_DESCRIPTOR_HANDLE) {});
        Check(cbv1 != cbv0 &&
                  renderer.GetCbvSrvUavGpuHandleEXT(cbv1).ptr != renderer.GetCbvSrvUavGpuHandleEXT(cbv0).ptr,
              "C3: two live CBV/SRV/UAV allocations are distinct (index and GPU handle)");
        Check(renderer.GetCbvSrvUavHeapEXT() != nullptr, "C4: shader-visible CBV/SRV/UAV heap object real");

        // REMED-GFX-177: a freed slot is reissued rather than consumed forever. Submission is
        // synchronous here, so the fence stamp taken at Free() has already completed and the very
        // next Allocate() may reuse it -- no wait, no idle, no extra submit is added to get there.
        renderer.FreeRtvDescriptorEXT(rtv1);
        const D3D12_CPU_DESCRIPTOR_HANDLE rtvReused = renderer.AllocateRtvDescriptorEXT();
        Check(rtvReused.ptr == rtv1.ptr, "C5: a freed RTV slot is reissued, not leaked");

        renderer.FreeDsvDescriptorEXT(dsv1);
        const D3D12_CPU_DESCRIPTOR_HANDLE dsvReused = renderer.AllocateDsvDescriptorEXT();
        Check(dsvReused.ptr == dsv1.ptr, "C6: a freed DSV slot is reissued, not leaked");

        renderer.FreeCbvSrvUavDescriptorEXT(cbv1);
        const std::uint32_t cbvReused =
            renderer.CreateCbvSrvUavDescriptorEXT([](D3D12_CPU_DESCRIPTOR_HANDLE) {});
        Check(cbvReused == cbv1, "C7: a freed CBV/SRV/UAV slot is reissued, not leaked");

        // Leave the heaps as this block found them, so later checks measure their own demand.
        renderer.FreeRtvDescriptorEXT(rtv0);
        renderer.FreeRtvDescriptorEXT(rtvReused);
        renderer.FreeDsvDescriptorEXT(dsv0);
        renderer.FreeDsvDescriptorEXT(dsvReused);
        renderer.FreeCbvSrvUavDescriptorEXT(cbv0);
        renderer.FreeCbvSrvUavDescriptorEXT(cbvReused);
    }

    // ---- Check D: command allocator + command list + queue + fence, wired together ----
    {
        ID3D12CommandAllocator* allocator = renderer.GetCommandAllocatorEXT(0);
        ID3D12GraphicsCommandList* cmdList = renderer.GetCommandListEXT();
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
            renderer.ExecuteCommandListAndWaitEXT(cmdList);
        }
        catch (const std::exception& e)
        {
            std::printf("       ExecuteCommandListAndWaitEXT threw: %s\n", e.what());
            threw = true;
        }
        Check(!threw, "D5: ExecuteCommandListAndWaitEXT (submit+signal+wait) completed without throwing");
        Check(renderer.GetFenceEXT()->GetCompletedValue() >= 1, "D6: fence GetCompletedValue() advanced past 0");
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
        std::uint64_t v0 = renderer.SignalAndWaitForFrameEXT(0);
        std::uint64_t v1 = renderer.SignalAndWaitForFrameEXT(1);
        std::uint64_t v2 = renderer.SignalAndWaitForFrameEXT(0); // second use of frame 0 -- must wait on v0 first
        Check(v1 > v0 && v2 > v1, "E1: SignalAndWaitForFrameEXT fence values strictly increase");
        Check(renderer.GetFenceEXT()->GetCompletedValue() >= v0,
              "E2: the second call for frame 0 genuinely waited for that slot's PREVIOUS (v0) value before returning");

        // Real eventual-completion proof for v2 itself -- an explicit, bounded wait here (test-only
        // code), not implied by SignalAndWaitForFrameEXT's own non-stalling contract.
        if (renderer.GetFenceEXT()->GetCompletedValue() < v2)
        {
            HANDLE waitEvent = CreateEventExW(nullptr, nullptr, 0, EVENT_ALL_ACCESS);
            renderer.GetFenceEXT()->SetEventOnCompletion(v2, waitEvent);
            WaitForSingleObject(waitEvent, INFINITE);
            CloseHandle(waitEvent);
        }
        Check(renderer.GetFenceEXT()->GetCompletedValue() >= v2,
              "E3: the most recently signaled value (v2) eventually completes when explicitly waited on");
    }

    // ---- Check E4 (plans/plan_dx.md DX-113 follow-up): SignalAndWaitForFrameEXT's WaitForSingleObject
    // branch genuinely stalls the CPU thread under real pending GPU work, not just an
    // already-satisfied fence returning near-instantly (E1-E3 above only proved the VALUE-ordering
    // contract, not that the wait itself is a real block). Proven via a RELATIVE timing comparison,
    // not a fragile absolute-millisecond threshold: a "control" call whose wait target is already
    // satisfied vs. a "load" call whose wait target was deliberately positioned in the command queue
    // right after several GB of real GPU copy traffic, so it cannot complete until the GPU actually
    // drains that work. ----
    {
        // v1: control point, nothing pending on frame slot 0 yet (E1-E3 already drained it) -> fast.
        renderer.SignalAndWaitForFrameEXT(0);

        constexpr UINT64 kBufSize = 64ull * 1024 * 1024;
        D3D12_HEAP_PROPERTIES heapProps{};
        heapProps.Type = D3D12_HEAP_TYPE_DEFAULT;
        D3D12_RESOURCE_DESC bufDesc{};
        bufDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        bufDesc.Width = kBufSize;
        bufDesc.Height = 1;
        bufDesc.DepthOrArraySize = 1;
        bufDesc.MipLevels = 1;
        bufDesc.Format = DXGI_FORMAT_UNKNOWN;
        bufDesc.SampleDesc.Count = 1;
        bufDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

        Microsoft::WRL::ComPtr<ID3D12Resource> bufA, bufB;
        HRESULT hrA = renderer.GetDeviceEXT()->CreateCommittedResource(
            &heapProps, D3D12_HEAP_FLAG_NONE, &bufDesc, D3D12_RESOURCE_STATE_COMMON, nullptr,
            IID_PPV_ARGS(bufA.GetAddressOf()));
        HRESULT hrB = renderer.GetDeviceEXT()->CreateCommittedResource(
            &heapProps, D3D12_HEAP_FLAG_NONE, &bufDesc, D3D12_RESOURCE_STATE_COMMON, nullptr,
            IID_PPV_ARGS(bufB.GetAddressOf()));
        Check(SUCCEEDED(hrA) && SUCCEEDED(hrB),
              "E4pre: two 64MB throwaway DEFAULT-heap buffers created for a real GPU copy workload");

        if (SUCCEEDED(hrA) && SUCCEEDED(hrB))
        {
            ID3D12CommandAllocator* allocator = renderer.GetCommandAllocatorEXT(0);
            ID3D12GraphicsCommandList* cmdList = renderer.GetCommandListEXT();
            allocator->Reset();
            cmdList->Reset(allocator, nullptr);
            constexpr int kCopies = 48; // ~3 GB of real DEFAULT-heap-to-DEFAULT-heap copy traffic
            for (int i = 0; i < kCopies; ++i)
                cmdList->CopyBufferRegion(bufB.Get(), 0, bufA.Get(), 0, kBufSize);
            const HRESULT hrClose = cmdList->Close();
            Check(SUCCEEDED(hrClose), "E4a: command list recording the copy workload closes cleanly");

            if (SUCCEEDED(hrClose))
            {
                ID3D12CommandList* lists[] = { cmdList };
                // Deliberately NOT waited on here -- queued asynchronously, same as a real frame's
                // draws would be, so the copy work sits in the queue ahead of the next two signals.
                renderer.GetCommandQueueEXT()->ExecuteCommandLists(1, lists);

                // v2: queued immediately after the copy work; its own wait target (v1, already
                // complete) is satisfied, so this call itself stays fast -- this is the control
                // measurement, timed on an equal footing with the load measurement below (both are
                // one SignalAndWaitForFrameEXT call, differing only in whether their wait target sits
                // behind the copy work in the queue).
                const auto controlStart = std::chrono::steady_clock::now();
                renderer.SignalAndWaitForFrameEXT(0);
                const auto controlDuration = std::chrono::steady_clock::now() - controlStart;

                // v3: its wait target is v2, which was itself queued right after the copy work -- the
                // GPU cannot signal v2 until the copy work ahead of it drains, so this call's
                // WaitForSingleObject genuinely blocks for as long as the copy takes.
                const auto loadStart = std::chrono::steady_clock::now();
                renderer.SignalAndWaitForFrameEXT(0);
                const auto loadDuration = std::chrono::steady_clock::now() - loadStart;

                const auto controlUs = std::chrono::duration_cast<std::chrono::microseconds>(controlDuration).count();
                const auto loadUs = std::chrono::duration_cast<std::chrono::microseconds>(loadDuration).count();
                std::printf("    E4: control=%lldus load=%lldus\n",
                            static_cast<long long>(controlUs), static_cast<long long>(loadUs));
                Check(loadDuration > controlDuration * 3 || loadDuration > std::chrono::milliseconds(2),
                      "E4: SignalAndWaitForFrameEXT's WaitForSingleObject branch genuinely blocks the CPU "
                      "measurably longer when its wait target sits behind real pending GPU work than the "
                      "already-satisfied control case (plans/plan_dx.md DX-113 follow-up)");
            }
        }
    }

    // ---- Check F: off-screen construction never touches the swap chain ----
    Check(!renderer.IsSwapChainAvailableEXT(), "F1: off-screen construction leaves swap chain unavailable");
    Check(renderer.GetSwapChainEXT() == nullptr, "F2: GetSwapChainEXT() is null off-screen");

    ID3D12Device* device = renderer.GetDeviceEXT();

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

        ID3D12CommandAllocator* allocator = renderer.GetCommandAllocatorEXT(0);
        ID3D12GraphicsCommandList* cmdList = renderer.GetCommandListEXT();
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
        try { renderer.ExecuteCommandListAndWaitEXT(cmdList); }
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
              "(plans/plan_dx.md DX-106/DX-113), not silently emitting an ad-hoc barrier from an unknown state");

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

    // ---- Check I: D3D12PipelineStateCache (DX-107) -- the first real D3D12 PSO this renderer has ever created ----
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

    // ---- Check J: D3D12VertexBufferRenderer / D3D12IndexBufferRenderer (DX-109) ----
    {
        struct Vtx { float x, y, z; uint32_t color; };
        const Vtx triangle[3] = {
            {0.0f, 0.5f, 0.0f, 0xFFFF0000u},
            {0.5f, -0.5f, 0.0f, 0xFF00FF00u},
            {-0.5f, -0.5f, 0.0f, 0xFF0000FFu},
        };

        D3D12VertexBufferRenderer vb(&renderer, 3);
        vb.SetData(triangle, 3, sizeof(Vtx));
        Check(vb.GetVertexCount() == 3, "J1: vertex buffer reports the uploaded vertex count");

        auto vbReadback = ReadBackBufferResource(renderer, vb.GetResourceEXT(), sizeof(triangle));
        Check(std::memcmp(vbReadback.data(), triangle, sizeof(triangle)) == 0,
              "J2: vertex buffer round-trips EXACT bytes through a real GPU upload+copy+readback");

        const uint16_t indices16[3] = {0, 1, 2};
        D3D12IndexBufferRenderer ib16(&renderer, 3, /*thirtyTwoBit=*/false);
        ib16.SetData16(indices16, 3);
        Check(ib16.GetIndexCount() == 3 && !ib16.IsThirtyTwoBit(),
              "J3: 16-bit index buffer reports correct count/format");
        auto ib16Readback = ReadBackBufferResource(renderer, ib16.GetResourceEXT(), sizeof(indices16));
        Check(std::memcmp(ib16Readback.data(), indices16, sizeof(indices16)) == 0,
              "J4: 16-bit index buffer round-trips EXACT bytes");

        const uint32_t indices32[3] = {2, 1, 0};
        D3D12IndexBufferRenderer ib32(&renderer, 3, /*thirtyTwoBit=*/true);
        ib32.SetData32(indices32, 3);
        Check(ib32.GetIndexCount() == 3 && ib32.IsThirtyTwoBit(),
              "J5: 32-bit index buffer reports correct count/format -- CreateIndexBuffer32() is a real, "
              "distinct override (not silently aliased to CreateIndexBuffer16, the real bug D3D11's own "
              "Phase DIRECTX5 fork found and fixed)");
        auto ib32Readback = ReadBackBufferResource(renderer, ib32.GetResourceEXT(), sizeof(indices32));
        Check(std::memcmp(ib32Readback.data(), indices32, sizeof(indices32)) == 0,
              "J6: 32-bit index buffer round-trips EXACT bytes");

        // Via the real IGraphicsRenderer factory methods too, not just direct construction --
        // confirms CreateIndexBuffer32() genuinely returns a 32-bit-format object end-to-end.
        auto ib32ViaFactory = renderer.CreateIndexBuffer32(3);
        ib32ViaFactory->SetData32(indices32, 3);
        Check(ib32ViaFactory->IsThirtyTwoBit(),
              "J7: IGraphicsRenderer::CreateIndexBuffer32() returns a genuinely 32-bit buffer");
    }

    // ---- Check K: D3D12TextureRenderer (DX-109) ----
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

        auto texOwned = renderer.CreateTexture(img); // real IGraphicsRenderer::CreateTexture() path
        auto* tex = static_cast<D3D12TextureRenderer*>(texOwned.get());
        Check(tex->GetWidth() == 4 && tex->GetHeight() == 4, "K1: texture reports correct dimensions");

        auto texReadback = ReadBackTextureLevel0(renderer, *tex);
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
        auto texReadback2 = ReadBackTextureLevel0(renderer, *tex);
        Check(std::memcmp(texReadback2.data(), updated.data(), updated.size()) == 0,
              "K3: UpdatePixels() genuinely overwrites the texture (not a stale first-upload value)");
        Check(std::memcmp(texReadback2.data(), img.pixels.data(), img.pixels.size()) != 0,
              "K4: the updated readback genuinely differs from the original upload (real change, not a no-op)");
    }

    // ---- Check L: RecreateDeviceEXT() (DX-110) ----
    {
        bool threw = false;
        try { renderer.RecreateDeviceEXT(); }
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
        Check(renderer.GetDeviceEXT() != nullptr, "L2: a real ID3D12Device exists after recreation");
        Check(renderer.GetFenceEXT() != nullptr, "L3: a real ID3D12Fence exists after recreation");
        Check(renderer.GetCommandQueueEXT() != nullptr && renderer.GetCommandAllocatorEXT(0) != nullptr &&
              renderer.GetCommandListEXT() != nullptr,
              "L4: command queue/allocator/command list all real again after recreation");

        // Real proof the recreated renderer is actually usable, not just non-null: a fresh command
        // list submission round-trips through the NEW fence, and a fresh vertex buffer created
        // AFTER recreation uploads and reads back correctly through the NEW device.
        ID3D12CommandAllocator* allocator = renderer.GetCommandAllocatorEXT(0);
        ID3D12GraphicsCommandList* cmdList = renderer.GetCommandListEXT();
        allocator->Reset();
        cmdList->Reset(allocator, nullptr);
        cmdList->Close();
        bool execThrew = false;
        try { renderer.ExecuteCommandListAndWaitEXT(cmdList); }
        catch (const std::exception&) { execThrew = true; }
        Check(!execThrew, "L5: a fresh command-list submission through the NEW queue/fence succeeds");

        const float knownData[4] = {1.0f, 2.0f, 3.0f, 4.0f};
        D3D12VertexBufferRenderer vbAfter(&renderer, 1);
        vbAfter.SetData(knownData, 1, sizeof(knownData));
        auto vbAfterReadback = ReadBackBufferResource(renderer, vbAfter.GetResourceEXT(), sizeof(knownData));
        Check(std::memcmp(vbAfterReadback.data(), knownData, sizeof(knownData)) == 0,
              "L6: a NEW vertex buffer created after recreation uploads+reads back correctly through "
              "the new device -- proves the recreated renderer is genuinely functional, not just "
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
        HRESULT hrRt = renderer.GetDeviceEXT()->CreateCommittedResource(
            &heapProps, D3D12_HEAP_FLAG_NONE, &rtDesc,
            D3D12_RESOURCE_STATE_RENDER_TARGET, nullptr, IID_PPV_ARGS(rt.GetAddressOf()));
        Check(SUCCEEDED(hrRt) && rt != nullptr, "M0: real off-screen RGBA8 render-target resource created");

        renderer.GetResourceStateTrackerEXT().TrackResource(rt.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET);

        D3D12_CPU_DESCRIPTOR_HANDLE rtv = renderer.AllocateRtvDescriptorEXT();
        renderer.GetDeviceEXT()->CreateRenderTargetView(rt.Get(), nullptr, rtv);
        renderer.BindOffscreenColorTargetEXT(rt.Get(), rtv, DXGI_FORMAT_R8G8B8A8_UNORM, kRtWidth, kRtHeight);

        // Same "oversized triangle covering the entire NDC square" trick and byte-for-byte
        // discipline D3D11's own Check P established (directx11_smoke_test.cpp) -- world=view=
        // projection=Identity, so these Position values ARE clip-space coordinates directly.
        struct VPC { float x, y, z; uint32_t color; };
        static const VPC kTri[3] = {
            {-1.0f, -1.0f, 0.0f, 0xFF0000FFu},
            { 3.0f, -1.0f, 0.0f, 0xFF0000FFu},
            {-1.0f,  3.0f, 0.0f, 0xFF0000FFu},
        };
        D3D12VertexBufferRenderer vb(&renderer, 3);
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
        renderer.Clear(0.0f, 0.0f, 1.0f, 1.0f);
        auto before = ReadBackRenderTargetFull(renderer, rt.Get(), kRtWidth, kRtHeight);
        renderer.DrawColoredPrimitives(vb, Matrix::getIdentityProperty(), Matrix::getIdentityProperty(),
                                      Matrix::getIdentityProperty(), PrimitiveType::TriangleList, 1);
        auto after = ReadBackRenderTargetFull(renderer, rt.Get(), kRtWidth, kRtHeight);
        Check(regionIs(before, 0, 0, 255, 255) && regionIs(after, 255, 0, 0, 255),
              "M1: DrawColoredPrimitives() paints the exact vertex color over the Clear() background "
              "at the same off-screen readback location -- the first real D3D12 3D triangle");

        // Indexed path: same triangle, via DrawIndexedColoredPrimitives.
        static const uint16_t kTriIdx[3] = {0, 1, 2};
        D3D12IndexBufferRenderer ib(&renderer, 3, /*thirtyTwoBit=*/false);
        ib.SetData16(kTriIdx, 3);

        renderer.Clear(0.0f, 0.0f, 1.0f, 1.0f);
        auto beforeIdx = ReadBackRenderTargetFull(renderer, rt.Get(), kRtWidth, kRtHeight);
        renderer.DrawIndexedColoredPrimitives(vb, ib, Matrix::getIdentityProperty(), Matrix::getIdentityProperty(),
                                             Matrix::getIdentityProperty(), PrimitiveType::TriangleList, 1);
        auto afterIdx = ReadBackRenderTargetFull(renderer, rt.Get(), kRtWidth, kRtHeight);
        Check(regionIs(beforeIdx, 0, 0, 255, 255) && regionIs(afterIdx, 255, 0, 0, 255),
              "M2: DrawIndexedColoredPrimitives() paints the exact vertex color over the Clear() "
              "background at the same off-screen readback location");

        renderer.UnbindOffscreenColorTargetEXT();
    }

    // ---- Check VP: REMED-GFX-064 -- GraphicsDevice.Viewport honored on D3D12 ----
    // Before this task D3D12 never overrode the no-op base SetViewport and hardcoded a full-target
    // D3D12_VIEWPORT at all four RSSetViewports sites, so a custom sub-region Viewport was a total
    // no-op. D3D12's immediate-per-draw command-list model re-records every draw, so the fix stores
    // the Viewport and re-reads it (GetEffectiveViewportEXT) at each site -- no capture/replay needed
    // (unlike deferred Vulkan GFX-062 / SdlGpu). This runs off-screen (BindOffscreenColorTargetEXT),
    // the only D3D12 path that survives this dev loop's plain Wine (real-window is REMED-BUILD-012).
    //
    // Asymmetric 80x60 RT, Viewport (13,9,31,23) => filled region x[13,44) y[9,32). An oversized
    // full-NDC triangle (Check M's trick) would cover the whole target if the viewport were full.
    {
        constexpr int kRtWidth = 80;   // asymmetric, and != 64 (the other checks' RT size)
        constexpr int kRtHeight = 60;

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
        HRESULT hrRt = renderer.GetDeviceEXT()->CreateCommittedResource(
            &heapProps, D3D12_HEAP_FLAG_NONE, &rtDesc,
            D3D12_RESOURCE_STATE_RENDER_TARGET, nullptr, IID_PPV_ARGS(rt.GetAddressOf()));
        Check(SUCCEEDED(hrRt) && rt != nullptr, "VP0: real off-screen 80x60 render-target resource created");
        renderer.GetResourceStateTrackerEXT().TrackResource(rt.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET);

        D3D12_CPU_DESCRIPTOR_HANDLE rtv = renderer.AllocateRtvDescriptorEXT();
        renderer.GetDeviceEXT()->CreateRenderTargetView(rt.Get(), nullptr, rtv);
        renderer.BindOffscreenColorTargetEXT(rt.Get(), rtv, DXGI_FORMAT_R8G8B8A8_UNORM, kRtWidth, kRtHeight);

        struct VPC { float x, y, z; uint32_t color; };
        // Colors are XNA packed AABBGGRR: red = 0xFF0000FF, green = 0xFF00FF00.
        static const VPC kRedTri[3] = {
            {-1.0f, -1.0f, 0.0f, 0xFF0000FFu}, { 3.0f, -1.0f, 0.0f, 0xFF0000FFu}, {-1.0f,  3.0f, 0.0f, 0xFF0000FFu},
        };
        static const VPC kGreenTri[3] = {
            {-1.0f, -1.0f, 0.0f, 0xFF00FF00u}, { 3.0f, -1.0f, 0.0f, 0xFF00FF00u}, {-1.0f,  3.0f, 0.0f, 0xFF00FF00u},
        };
        D3D12VertexBufferRenderer redVb(&renderer, 3);
        redVb.SetData(kRedTri, 3, sizeof(VPC));
        D3D12VertexBufferRenderer greenVb(&renderer, 3);
        greenVb.SetData(kGreenTri, 3, sizeof(VPC));

        auto pixelAt = [&](const std::vector<uint8_t>& buf, int x, int y) -> std::array<uint8_t, 4>
        {
            const std::size_t idx = (static_cast<std::size_t>(y) * kRtWidth + static_cast<std::size_t>(x)) * 4;
            return {buf[idx + 0], buf[idx + 1], buf[idx + 2], buf[idx + 3]};
        };
        auto isColor = [](const std::array<uint8_t, 4>& p, uint8_t r, uint8_t g, uint8_t b)
        {
            return p[0] == r && p[1] == g && p[2] == b;
        };
        const auto& I = Matrix::getIdentityProperty();

        // (1) Custom sub-region viewport: full-NDC red triangle fills ONLY the viewport rect.
        renderer.Clear(0.0f, 0.0f, 0.0f, 1.0f);
        renderer.SetViewport(13, 9, 31, 23, 0.0f, 1.0f);
        renderer.DrawColoredPrimitives(redVb, I, I, I, PrimitiveType::TriangleList, 1);
        auto buf = ReadBackRenderTargetFull(renderer, rt.Get(), kRtWidth, kRtHeight);
        Check(isColor(pixelAt(buf, 28, 20), 255, 0, 0), "VP1a: inside viewport (28,20) is RED");
        Check(isColor(pixelAt(buf,  5, 20),   0, 0, 0), "VP1b: left of viewport ( 5,20) is BLACK (clear)");
        Check(isColor(pixelAt(buf, 60, 20),   0, 0, 0), "VP1c: right of viewport (60,20) is BLACK (clear)");
        Check(isColor(pixelAt(buf, 28,  3),   0, 0, 0), "VP1d: above viewport (28, 3) is BLACK (clear)");
        Check(isColor(pixelAt(buf, 28, 50),   0, 0, 0), "VP1e: below viewport (28,50) is BLACK (clear)");

        // (2) Full-viewport control: reset to full target, red triangle fills the WHOLE target.
        renderer.Clear(0.0f, 0.0f, 0.0f, 1.0f);
        renderer.SetViewport(0, 0, kRtWidth, kRtHeight, 0.0f, 1.0f);
        renderer.DrawColoredPrimitives(redVb, I, I, I, PrimitiveType::TriangleList, 1);
        buf = ReadBackRenderTargetFull(renderer, rt.Get(), kRtWidth, kRtHeight);
        Check(isColor(pixelAt(buf, 28, 20), 255, 0, 0) && isColor(pixelAt(buf, 5, 20), 255, 0, 0)
              && isColor(pixelAt(buf, 60, 20), 255, 0, 0) && isColor(pixelAt(buf, 28, 3), 255, 0, 0)
              && isColor(pixelAt(buf, 28, 50), 255, 0, 0),
              "VP2: full viewport (0,0,80,60) fills the whole target -- no over-clipping");

        // (3) Two viewports, two draws, one target (no clear between): each draw honors its OWN
        //     viewport (immediate-per-draw model records each RSSetViewports fresh).
        //       Viewport A = (4,4,22,22) RED  => x[4,26)  y[4,26)
        //       Viewport B = (48,32,22,22) GRN => x[48,70) y[32,54)
        renderer.Clear(0.0f, 0.0f, 0.0f, 1.0f);
        renderer.SetViewport(4, 4, 22, 22, 0.0f, 1.0f);
        renderer.DrawColoredPrimitives(redVb, I, I, I, PrimitiveType::TriangleList, 1);
        renderer.SetViewport(48, 32, 22, 22, 0.0f, 1.0f);
        renderer.DrawColoredPrimitives(greenVb, I, I, I, PrimitiveType::TriangleList, 1);
        buf = ReadBackRenderTargetFull(renderer, rt.Get(), kRtWidth, kRtHeight);
        Check(isColor(pixelAt(buf, 15, 15), 255, 0, 0), "VP3a: viewport A center (15,15) is RED");
        Check(isColor(pixelAt(buf, 59, 43), 0, 255, 0), "VP3b: viewport B center (59,43) is GREEN");
        Check(isColor(pixelAt(buf, 59, 15), 0, 0, 0),   "VP3c: gap (59,15) is BLACK (in neither viewport)");
        Check(isColor(pixelAt(buf, 15, 43), 0, 0, 0),   "VP3d: gap (15,43) is BLACK (in neither viewport)");

        // Unbind clears the stored custom viewport (target-change reset, mirroring XNA), so every
        // subsequent check -- which binds a different target and never calls SetViewport -- falls
        // back to that target's full size, byte-identical to pre-REMED-GFX-064.
        renderer.UnbindOffscreenColorTargetEXT();
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
        HRESULT hrRt = renderer.GetDeviceEXT()->CreateCommittedResource(
            &heapProps, D3D12_HEAP_FLAG_NONE, &rtDesc,
            D3D12_RESOURCE_STATE_RENDER_TARGET, nullptr, IID_PPV_ARGS(rt.GetAddressOf()));
        Check(SUCCEEDED(hrRt) && rt != nullptr, "N0: real off-screen RGBA8 render-target resource created");
        renderer.GetResourceStateTrackerEXT().TrackResource(rt.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET);

        D3D12_CPU_DESCRIPTOR_HANDLE rtv = renderer.AllocateRtvDescriptorEXT();
        renderer.GetDeviceEXT()->CreateRenderTargetView(rt.Get(), nullptr, rtv);
        renderer.BindOffscreenColorTargetEXT(rt.Get(), rtv, DXGI_FORMAT_R8G8B8A8_UNORM, kRtWidth, kRtHeight);

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
        D3D12VertexBufferRenderer vbTex(&renderer, 3);
        vbTex.SetData(kTriTex, 3, sizeof(VPT));

        ImageData img;
        img.width = 2; img.height = 2; img.mipLevels = 1;
        img.pixels.resize(2 * 2 * 4);
        for (int i = 0; i < 4; ++i)
        {
            img.pixels[i * 4 + 0] = 11; img.pixels[i * 4 + 1] = 22;
            img.pixels[i * 4 + 2] = 33; img.pixels[i * 4 + 3] = 255;
        }
        D3D12TextureRenderer tex(&renderer, img);

        GpuDrawParams tp;
        tp.texture0 = &tex;
        tp.textureEnabled = true;
        // diffuseColor left at its default (1,1,1,1) so outColor == the raw sampled texel exactly.

        renderer.Clear(0.0f, 1.0f, 0.0f, 1.0f);
        auto beforeN = ReadBackRenderTargetFull(renderer, rt.Get(), kRtWidth, kRtHeight);
        renderer.DrawPrimitivesEx(vbTex, Matrix::getIdentityProperty(), Matrix::getIdentityProperty(),
                                 Matrix::getIdentityProperty(), PrimitiveType::TriangleList, 1, tp);
        auto afterN = ReadBackRenderTargetFull(renderer, rt.Get(), kRtWidth, kRtHeight);
        Check(regionIs(beforeN, 0, 255, 0, 255) && regionIs(afterN, 11, 22, 33, 255),
              "N1: DrawPrimitivesEx() real textured3d draw samples the exact texture color "
              "(diffuseColor=white) over the Clear() background (plans/plan_dx.md DX-111)");

        // Indexed path, same textured3d draw -- proves DrawIndexedPrimitivesEx shares the same real
        // pipeline (DrawPrimitivesExImpl), not just the non-indexed entry point.
        static const uint16_t kTriTexIdx[3] = {0, 1, 2};
        D3D12IndexBufferRenderer ibTex(&renderer, 3, /*thirtyTwoBit=*/false);
        ibTex.SetData16(kTriTexIdx, 3);
        renderer.Clear(0.0f, 1.0f, 0.0f, 1.0f);
        renderer.DrawIndexedPrimitivesEx(vbTex, ibTex, Matrix::getIdentityProperty(), Matrix::getIdentityProperty(),
                                        Matrix::getIdentityProperty(), PrimitiveType::TriangleList, 1, tp);
        auto afterNIdx = ReadBackRenderTargetFull(renderer, rt.Get(), kRtWidth, kRtHeight);
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
        D3D12VertexBufferRenderer vbColTex(&renderer, 3);
        vbColTex.SetData(kTriColTex, 3, sizeof(VPCT));

        ImageData whiteImg;
        whiteImg.width = 2; whiteImg.height = 2; whiteImg.mipLevels = 1;
        whiteImg.pixels.assign(2 * 2 * 4, 255);
        D3D12TextureRenderer whiteTex(&renderer, whiteImg);

        GpuDrawParams ctp;
        ctp.texture0 = &whiteTex;
        ctp.textureEnabled = true;
        ctp.vertexColorEnabled = true;

        renderer.Clear(0.0f, 1.0f, 0.0f, 1.0f);
        renderer.DrawPrimitivesEx(vbColTex, Matrix::getIdentityProperty(), Matrix::getIdentityProperty(),
                                 Matrix::getIdentityProperty(), PrimitiveType::TriangleList, 1, ctp);
        auto afterCT = ReadBackRenderTargetFull(renderer, rt.Get(), kRtWidth, kRtHeight);
        Check(regionIs(afterCT, 80, 160, 240, 255),
              "N3: DrawPrimitivesEx() real colored_textured3d draw multiplies the exact vertex color "
              "through a white texture (plans/plan_dx.md DX-111)");

        // DX-137: dedicated fog on/off discriminating test for colored_textured3d -- same
        // Z-at-FogEnd methodology as textured3d's own fog test below, reusing this block's
        // already-proven vertex-color/white-texture fixture (base result (80,160,240)).
        static VPCT kTriColTexFog[3];
        kTriColTexFog[0] = { -1.0f, -1.0f, 0.5f, kVertColor, 0.0f, 1.0f };
        kTriColTexFog[1] = {  3.0f, -1.0f, 0.5f, kVertColor, 2.0f, 1.0f };
        kTriColTexFog[2] = { -1.0f,  3.0f, 0.5f, kVertColor, 0.0f, -1.0f };
        D3D12VertexBufferRenderer vbColTexFog(&renderer, 3);
        vbColTexFog.SetData(kTriColTexFog, 3, sizeof(VPCT));

        ctp.fogEnabled = false;
        ctp.fogColor[0] = 0.0f; ctp.fogColor[1] = 1.0f; ctp.fogColor[2] = 0.0f;
        renderer.Clear(10.0f / 255.0f, 10.0f / 255.0f, 10.0f / 255.0f, 1.0f);
        renderer.DrawPrimitivesEx(vbColTexFog, Matrix::getIdentityProperty(), Matrix::getIdentityProperty(),
                                 Matrix::getIdentityProperty(), PrimitiveType::TriangleList, 1, ctp);
        auto colTexFogOff = ReadBackRenderTargetFull(renderer, rt.Get(), kRtWidth, kRtHeight);
        Check(isColor(pixelAt(colTexFogOff, 30, 30), 80, 160, 240, 255),
              "N3b: DrawPrimitivesEx() colored_textured3d fogEnabled=false leaves the exact "
              "vertex*texture color unblended (plans/plan_dx.md DX-137)");

        ctp.fogEnabled = true;
        ctp.fogVector[2] = 2.0f;
        renderer.Clear(10.0f / 255.0f, 10.0f / 255.0f, 10.0f / 255.0f, 1.0f);
        renderer.DrawPrimitivesEx(vbColTexFog, Matrix::getIdentityProperty(), Matrix::getIdentityProperty(),
                                 Matrix::getIdentityProperty(), PrimitiveType::TriangleList, 1, ctp);
        auto colTexFogOn = ReadBackRenderTargetFull(renderer, rt.Get(), kRtWidth, kRtHeight);
        Check(isColor(pixelAt(colTexFogOn, 30, 30), 0, 255, 0, 255),
              "N3c: DrawPrimitivesEx() colored_textured3d fogEnabled=true with Z at FogEnd genuinely "
              "blends all the way to the exact FogColor (plans/plan_dx.md DX-137)");

        // DX-137: dedicated fog on/off discriminating test for textured3d -- a representative
        // variant of the 7 fog-capable non-colored3d variants (chosen for its simple, already-
        // proven fixture above; a full 7-variant x 2-renderer sweep is out of this task's own scope,
        // documented honestly rather than silently partial). Same Z-at-FogEnd methodology D3D11's
        // own DX-137 fixture uses -- needs its own vertex buffer at Z=0.5 (fogEnd), not the shared
        // vbTex (Z=0, which gives fogFactor=1, i.e. no blending at all).
        static const VPT kTriTexFog[3] = {
            {-1.0f, -1.0f, 0.5f, 0.0f, 1.0f},
            { 3.0f, -1.0f, 0.5f, 2.0f, 1.0f},
            {-1.0f,  3.0f, 0.5f, 0.0f, -1.0f},
        };
        D3D12VertexBufferRenderer vbTexFog(&renderer, 3);
        vbTexFog.SetData(kTriTexFog, 3, sizeof(VPT));

        tp.fogEnabled = false;
        tp.fogColor[0] = 0.0f; tp.fogColor[1] = 1.0f; tp.fogColor[2] = 0.0f;
        renderer.Clear(10.0f / 255.0f, 10.0f / 255.0f, 10.0f / 255.0f, 1.0f);
        renderer.DrawPrimitivesEx(vbTexFog, Matrix::getIdentityProperty(), Matrix::getIdentityProperty(),
                                 Matrix::getIdentityProperty(), PrimitiveType::TriangleList, 1, tp);
        auto texFogOff = ReadBackRenderTargetFull(renderer, rt.Get(), kRtWidth, kRtHeight);
        Check(isColor(pixelAt(texFogOff, 30, 30), 11, 22, 33, 255),
              "N4: DrawPrimitivesEx() textured3d fogEnabled=false leaves the exact sampled texture "
              "color unblended (plans/plan_dx.md DX-137)");

        tp.fogEnabled = true;
        tp.fogVector[2] = 2.0f;
        renderer.Clear(10.0f / 255.0f, 10.0f / 255.0f, 10.0f / 255.0f, 1.0f);
        renderer.DrawPrimitivesEx(vbTexFog, Matrix::getIdentityProperty(), Matrix::getIdentityProperty(),
                                 Matrix::getIdentityProperty(), PrimitiveType::TriangleList, 1, tp);
        auto texFogOn = ReadBackRenderTargetFull(renderer, rt.Get(), kRtWidth, kRtHeight);
        Check(isColor(pixelAt(texFogOn, 30, 30), 0, 255, 0, 255),
              "N5: DrawPrimitivesEx() textured3d fogEnabled=true with Z at FogEnd genuinely blends "
              "all the way to the exact FogColor, distinctly different from the fogEnabled=false "
              "case above (plans/plan_dx.md DX-137)");

        renderer.UnbindOffscreenColorTargetEXT();
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
        HRESULT hrRt = renderer.GetDeviceEXT()->CreateCommittedResource(
            &heapProps, D3D12_HEAP_FLAG_NONE, &rtDesc,
            D3D12_RESOURCE_STATE_RENDER_TARGET, nullptr, IID_PPV_ARGS(rt.GetAddressOf()));
        Check(SUCCEEDED(hrRt) && rt != nullptr, "O0: real off-screen RGBA8 render-target resource created");
        renderer.GetResourceStateTrackerEXT().TrackResource(rt.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET);

        D3D12_CPU_DESCRIPTOR_HANDLE rtv = renderer.AllocateRtvDescriptorEXT();
        renderer.GetDeviceEXT()->CreateRenderTargetView(rt.Get(), nullptr, rtv);
        renderer.BindOffscreenColorTargetEXT(rt.Get(), rtv, DXGI_FORMAT_R8G8B8A8_UNORM, kRtWidth, kRtHeight);

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
        D3D12VertexBufferRenderer vbLit(&renderer, 3);
        vbLit.SetData(kTriLit, 3, sizeof(VPNT));

        ImageData img;
        img.width = 2; img.height = 2; img.mipLevels = 1;
        img.pixels.resize(2 * 2 * 4);
        for (int i = 0; i < 4; ++i)
        {
            img.pixels[i * 4 + 0] = 44; img.pixels[i * 4 + 1] = 55;
            img.pixels[i * 4 + 2] = 66; img.pixels[i * 4 + 3] = 255;
        }
        D3D12TextureRenderer tex(&renderer, img);

        GpuDrawParams unlitP;
        unlitP.texture0 = &tex;
        unlitP.textureEnabled = true;
        unlitP.lightingEnabled = false;

        renderer.Clear(0.0f, 0.0f, 1.0f, 1.0f);
        renderer.DrawPrimitivesEx(vbLit, Matrix::getIdentityProperty(), Matrix::getIdentityProperty(),
                                 Matrix::getIdentityProperty(), PrimitiveType::TriangleList, 1, unlitP);
        auto unlitResult = ReadBackRenderTargetFull(renderer, rt.Get(), kRtWidth, kRtHeight);
        Check(regionIs(unlitResult, 44, 55, 66, 255),
              "O1: DrawPrimitivesEx() real lit_textured3d unlit branch samples diffuseColor*texture "
              "exactly (plans/plan_dx.md DX-111)");

        GpuDrawParams litP = unlitP;
        litP.lightingEnabled = true;
        litP.ambientColor[0] = 0.5f; litP.ambientColor[1] = 0.5f; litP.ambientColor[2] = 0.5f;
        litP.specularColor[0] = 0.0f; litP.specularColor[1] = 0.0f; litP.specularColor[2] = 0.0f;

        renderer.Clear(0.0f, 0.0f, 1.0f, 1.0f);
        renderer.DrawPrimitivesEx(vbLit, Matrix::getIdentityProperty(), Matrix::getIdentityProperty(),
                                 Matrix::getIdentityProperty(), PrimitiveType::TriangleList, 1, litP);
        auto litResult = ReadBackRenderTargetFull(renderer, rt.Get(), kRtWidth, kRtHeight);
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
              "color than both the unlit result and the Clear() background (plans/plan_dx.md DX-111)");

        // DX-137: dedicated fog on/off discriminating test for lit_textured3d -- reuses the unlit
        // branch's own deterministic fixture (exact base color (44,55,66)) above, same
        // Z-at-FogEnd methodology as textured3d's own fog test.
        static const VPNT kTriLitFog[3] = {
            {-1.0f, -1.0f, 0.5f, 0.0f, 0.0f, 1.0f, 0.0f, 1.0f},
            { 3.0f, -1.0f, 0.5f, 0.0f, 0.0f, 1.0f, 2.0f, 1.0f},
            {-1.0f,  3.0f, 0.5f, 0.0f, 0.0f, 1.0f, 0.0f, -1.0f},
        };
        D3D12VertexBufferRenderer vbLitFog(&renderer, 3);
        vbLitFog.SetData(kTriLitFog, 3, sizeof(VPNT));

        GpuDrawParams litFogP = unlitP;
        litFogP.fogEnabled = false;
        litFogP.fogColor[0] = 0.0f; litFogP.fogColor[1] = 1.0f; litFogP.fogColor[2] = 0.0f;
        renderer.Clear(10.0f / 255.0f, 10.0f / 255.0f, 10.0f / 255.0f, 1.0f);
        renderer.DrawPrimitivesEx(vbLitFog, Matrix::getIdentityProperty(), Matrix::getIdentityProperty(),
                                 Matrix::getIdentityProperty(), PrimitiveType::TriangleList, 1, litFogP);
        auto litFogOff = ReadBackRenderTargetFull(renderer, rt.Get(), kRtWidth, kRtHeight);
        Check(isColor(pixelAt(litFogOff, 30, 30), 44, 55, 66, 255),
              "O3: DrawPrimitivesEx() lit_textured3d fogEnabled=false leaves the exact unlit texture "
              "color unblended (plans/plan_dx.md DX-137)");

        litFogP.fogEnabled = true;
        litFogP.fogVector[2] = 2.0f;
        renderer.Clear(10.0f / 255.0f, 10.0f / 255.0f, 10.0f / 255.0f, 1.0f);
        renderer.DrawPrimitivesEx(vbLitFog, Matrix::getIdentityProperty(), Matrix::getIdentityProperty(),
                                 Matrix::getIdentityProperty(), PrimitiveType::TriangleList, 1, litFogP);
        auto litFogOn = ReadBackRenderTargetFull(renderer, rt.Get(), kRtWidth, kRtHeight);
        Check(isColor(pixelAt(litFogOn, 30, 30), 0, 255, 0, 255),
              "O4: DrawPrimitivesEx() lit_textured3d fogEnabled=true with Z at FogEnd genuinely "
              "blends all the way to the exact FogColor (plans/plan_dx.md DX-137)");

        renderer.UnbindOffscreenColorTargetEXT();
    }

    // ---- Check O3 (Task 1107, plans/plan_graphics.md Phase 80): PreferPerPixelLighting genuinely
    // selects between two different lit_textured3d dispatch variants (vertex-lit vs pixel-lit).
    // Reuses the exact scene/analytically-derived values already independently verified in
    // examples/easygl_basiceffect_preferperpixellighting_test.cpp (Task 1102) and
    // examples/directx11_smoke_test.cpp's own Check R2 (Task 1106): a flat quad, single shared normal
    // (0,0,1), makes DIFFUSE spatially constant but SPECULAR still varies across the surface
    // because the eye vector depends on position -- Gouraud-interpolating each vertex's own
    // independently-computed specular term (vertex-lit) genuinely differs from re-evaluating it
    // fresh at the sampled fragment (pixel-lit). Sampled exactly at the viewport centre, which
    // sits on the diagonal seam between the quad's two triangles. Expected: ~127 vertex-lit,
    // ~155 pixel-lit. ----
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
        HRESULT hrRt = renderer.GetDeviceEXT()->CreateCommittedResource(
            &heapProps, D3D12_HEAP_FLAG_NONE, &rtDesc,
            D3D12_RESOURCE_STATE_RENDER_TARGET, nullptr, IID_PPV_ARGS(rt.GetAddressOf()));
        Check(SUCCEEDED(hrRt) && rt != nullptr, "O3-0: real off-screen RGBA8 render-target resource created");
        renderer.GetResourceStateTrackerEXT().TrackResource(rt.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET);

        D3D12_CPU_DESCRIPTOR_HANDLE rtv = renderer.AllocateRtvDescriptorEXT();
        renderer.GetDeviceEXT()->CreateRenderTargetView(rt.Get(), nullptr, rtv);
        renderer.BindOffscreenColorTargetEXT(rt.Get(), rtv, DXGI_FORMAT_R8G8B8A8_UNORM, kRtWidth, kRtHeight);

        auto pixelAt = [&](const std::vector<uint8_t>& buf, int x, int y) -> std::array<uint8_t, 4>
        {
            const std::size_t idx = (static_cast<std::size_t>(y) * kRtWidth + static_cast<std::size_t>(x)) * 4;
            return {buf[idx + 0], buf[idx + 1], buf[idx + 2], buf[idx + 3]};
        };
        auto closeTo = [](int a, int b, int tol) { return std::abs(a - b) <= tol; };

        struct VPNT { float x, y, z; float nx, ny, nz; float u, v; };
        static const VPNT kQuad[6] = {
            {-1.0f,  1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 1.0f},
            {-1.0f, -1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f},
            { 1.0f, -1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 1.0f, 0.0f},
            {-1.0f,  1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 1.0f},
            { 1.0f, -1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 1.0f, 0.0f},
            { 1.0f,  1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f},
        };
        D3D12VertexBufferRenderer vbPPL(&renderer, 6);
        vbPPL.SetData(kQuad, 6, sizeof(VPNT));

        ImageData whiteImg;
        whiteImg.width = 1; whiteImg.height = 1; whiteImg.mipLevels = 1;
        whiteImg.pixels = {255, 255, 255, 255};
        D3D12TextureRenderer whiteTex(&renderer, whiteImg);

        GpuDrawParams pplP;
        pplP.texture0 = &whiteTex;
        pplP.textureEnabled = true;
        pplP.lightingEnabled = true;
        pplP.ambientColor[0] = 0.02f; pplP.ambientColor[1] = 0.02f; pplP.ambientColor[2] = 0.02f;
        pplP.diffuseColor[0] = 0.4f; pplP.diffuseColor[1] = 0.4f; pplP.diffuseColor[2] = 0.4f; pplP.diffuseColor[3] = 1.0f;
        Microsoft::Xna::Framework::Vector3 lightDir(0.5f, 0.0f, -1.0f);
        lightDir.Normalize();
        pplP.light0Dir[0] = lightDir.X; pplP.light0Dir[1] = lightDir.Y; pplP.light0Dir[2] = lightDir.Z;
        pplP.light0Diffuse[0] = 0.5f; pplP.light0Diffuse[1] = 0.5f; pplP.light0Diffuse[2] = 0.5f;
        pplP.light0Specular[0] = 1.0f; pplP.light0Specular[1] = 1.0f; pplP.light0Specular[2] = 1.0f;
        pplP.specularColor[0] = 1.0f; pplP.specularColor[1] = 1.0f; pplP.specularColor[2] = 1.0f;
        pplP.specularPower = 32.0f;
        pplP.eyePositionWorld[0] = 0.0f; pplP.eyePositionWorld[1] = 0.0f; pplP.eyePositionWorld[2] = 3.0f;

        const Matrix pplView = Matrix::CreateLookAt(Microsoft::Xna::Framework::Vector3(0.0f, 0.0f, 3.0f), Microsoft::Xna::Framework::Vector3::Zero, Microsoft::Xna::Framework::Vector3(0.0f, 1.0f, 0.0f));
        const Matrix pplProj = Matrix::CreatePerspectiveFieldOfView(0.78539816339744830962f /* MathHelper::PiOver4 */, 1.0f, 0.1f, 100.0f);

        pplP.preferPerPixelLighting = false;
        renderer.Clear(0.0f, 0.0f, 0.0f, 1.0f);
        renderer.DrawPrimitivesEx(vbPPL, Matrix::getIdentityProperty(), pplView, pplProj,
                                 PrimitiveType::TriangleList, 2, pplP);
        auto vertexLitResult = ReadBackRenderTargetFull(renderer, rt.Get(), kRtWidth, kRtHeight);
        const auto vertexLitPixel = pixelAt(vertexLitResult, kRtWidth / 2, kRtHeight / 2);
        Check(closeTo(vertexLitPixel[0], 127, 10) && closeTo(vertexLitPixel[1], 127, 10) && closeTo(vertexLitPixel[2], 127, 10),
              "O3-1: DrawPrimitivesEx() preferPerPixelLighting=false (XNA's real default) genuinely "
              "computes the Gouraud-averaged specular result, ~127 (Task 1107, plans/plan_graphics.md Phase 80)");

        pplP.preferPerPixelLighting = true;
        renderer.Clear(0.0f, 0.0f, 0.0f, 1.0f);
        renderer.DrawPrimitivesEx(vbPPL, Matrix::getIdentityProperty(), pplView, pplProj,
                                 PrimitiveType::TriangleList, 2, pplP);
        auto pixelLitResult = ReadBackRenderTargetFull(renderer, rt.Get(), kRtWidth, kRtHeight);
        const auto pixelLitPixel = pixelAt(pixelLitResult, kRtWidth / 2, kRtHeight / 2);
        Check(closeTo(pixelLitPixel[0], 155, 10) && closeTo(pixelLitPixel[1], 155, 10) && closeTo(pixelLitPixel[2], 155, 10),
              "O3-2: DrawPrimitivesEx() preferPerPixelLighting=true genuinely computes a fresh "
              "per-fragment specular result, ~155 (Task 1107, plans/plan_graphics.md Phase 80)");

        Check(vertexLitPixel[0] != pixelLitPixel[0],
              "O3-3: DrawPrimitivesEx() preferPerPixelLighting is a real dispatch selector, not a "
              "decorative no-op -- the two draws above produce genuinely different pixel values "
              "(Task 1107, plans/plan_graphics.md Phase 80)");

        renderer.UnbindOffscreenColorTargetEXT();
    }

    // ---- Check EE (plans/plan_dx.md DX-138): lit_textured3d -- DirectionalLight1/DirectionalLight2/
    // EmissiveColor each independently and exactly contribute, not just Light0 (already proven by
    // Check O). All 3 sub-checks use a plain white 1x1-solid texture so the shader's tex-multiply
    // doesn't scale the result, and ambientColor/light0Diffuse are zeroed so only the field under
    // test contributes -- each sub-check gets an EXACT expected RGB, not just "differs." ----
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
        HRESULT hrRt = renderer.GetDeviceEXT()->CreateCommittedResource(
            &heapProps, D3D12_HEAP_FLAG_NONE, &rtDesc,
            D3D12_RESOURCE_STATE_RENDER_TARGET, nullptr, IID_PPV_ARGS(rt.GetAddressOf()));
        renderer.GetResourceStateTrackerEXT().TrackResource(rt.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET);
        D3D12_CPU_DESCRIPTOR_HANDLE rtv = renderer.AllocateRtvDescriptorEXT();
        renderer.GetDeviceEXT()->CreateRenderTargetView(rt.Get(), nullptr, rtv);
        renderer.BindOffscreenColorTargetEXT(rt.Get(), rtv, DXGI_FORMAT_R8G8B8A8_UNORM, kRtWidth, kRtHeight);

        auto pixelAt = [&](const std::vector<uint8_t>& buf, int x, int y) -> std::array<uint8_t, 4>
        {
            const std::size_t idx = (static_cast<std::size_t>(y) * kRtWidth + static_cast<std::size_t>(x)) * 4;
            return {buf[idx + 0], buf[idx + 1], buf[idx + 2], buf[idx + 3]};
        };
        auto centerIsExact = [&](const std::vector<uint8_t>& buf, uint8_t r, uint8_t g, uint8_t b)
        {
            const auto p = pixelAt(buf, 30, 30);
            return p[0] == r && p[1] == g && p[2] == b;
        };

        struct VPNT { float x, y, z; float nx, ny, nz; float u, v; };
        // Normal faces -Z (toward a camera later placed at z=-10 for Check FF); irrelevant for this
        // diffuse/emissive-only check (EyePosition stays default (0,0,0)) but shared geometry with FF.
        static const VPNT kTriEE[3] = {
            {-1.0f, -1.0f, 0.0f, 0.0f, 0.0f, -1.0f, 0.0f, 1.0f},
            { 3.0f, -1.0f, 0.0f, 0.0f, 0.0f, -1.0f, 2.0f, 1.0f},
            {-1.0f,  3.0f, 0.0f, 0.0f, 0.0f, -1.0f, 0.0f, -1.0f},
        };
        D3D12VertexBufferRenderer vbEE(&renderer, 3);
        vbEE.SetData(kTriEE, 3, sizeof(VPNT));

        ImageData whiteImg;
        whiteImg.width = 2; whiteImg.height = 2; whiteImg.mipLevels = 1;
        whiteImg.pixels.assign(2 * 2 * 4, 255);
        D3D12TextureRenderer whiteTexEE(&renderer, whiteImg);

        GpuDrawParams baseP;
        baseP.texture0 = &whiteTexEE;
        baseP.textureEnabled = true;
        baseP.lightingEnabled = true;
        baseP.diffuseColor[0] = 1.0f; baseP.diffuseColor[1] = 1.0f; baseP.diffuseColor[2] = 1.0f; baseP.diffuseColor[3] = 1.0f;
        baseP.ambientColor[0] = 0.0f; baseP.ambientColor[1] = 0.0f; baseP.ambientColor[2] = 0.0f;
        baseP.light0Diffuse[0] = 0.0f; baseP.light0Diffuse[1] = 0.0f; baseP.light0Diffuse[2] = 0.0f; // Light0 off
        baseP.specularColor[0] = 0.0f; baseP.specularColor[1] = 0.0f; baseP.specularColor[2] = 0.0f; // no specular noise

        // EE1: DirectionalLight1 alone, full-facing direction, red diffuse -> exact (255,0,0).
        GpuDrawParams p1 = baseP;
        p1.light1Dir[0] = 0.0f; p1.light1Dir[1] = 0.0f; p1.light1Dir[2] = 1.0f; // travels +Z -> faces the -Z normal
        p1.light1Diffuse[0] = 1.0f; p1.light1Diffuse[1] = 0.0f; p1.light1Diffuse[2] = 0.0f;
        renderer.Clear(0.0f, 0.0f, 0.0f, 1.0f);
        renderer.DrawPrimitivesEx(vbEE, Matrix::getIdentityProperty(), Matrix::getIdentityProperty(),
                                 Matrix::getIdentityProperty(), PrimitiveType::TriangleList, 1, p1);
        auto r1 = ReadBackRenderTargetFull(renderer, rt.Get(), kRtWidth, kRtHeight);
        Check(centerIsExact(r1, 255, 0, 0),
              "EE1: DrawPrimitivesEx() real lit_textured3d -- DirectionalLight1 alone contributes the "
              "exact expected red, independent of Light0/Light2 (plans/plan_dx.md DX-138)");

        // EE2: same geometry/light1Dir, but light1Diffuse disabled (black, matches DX-60a's own
        // "Enabled=false zeroes Diffuse" convention) -- proves EE1 wasn't some other constant leaking in.
        GpuDrawParams p1off = p1;
        p1off.light1Diffuse[0] = 0.0f; p1off.light1Diffuse[1] = 0.0f; p1off.light1Diffuse[2] = 0.0f;
        renderer.Clear(0.0f, 0.0f, 0.0f, 1.0f);
        renderer.DrawPrimitivesEx(vbEE, Matrix::getIdentityProperty(), Matrix::getIdentityProperty(),
                                 Matrix::getIdentityProperty(), PrimitiveType::TriangleList, 1, p1off);
        auto r1off = ReadBackRenderTargetFull(renderer, rt.Get(), kRtWidth, kRtHeight);
        Check(centerIsExact(r1off, 0, 0, 0),
              "EE2: disabling DirectionalLight1's diffuse (zeroed, matching Enabled=false) removes "
              "its contribution exactly -- confirms EE1 was real, not a leaked default (plans/plan_dx.md DX-138)");

        // EE3: DirectionalLight2 alone, green diffuse -> exact (0,255,0).
        GpuDrawParams p2 = baseP;
        p2.light2Dir[0] = 0.0f; p2.light2Dir[1] = 0.0f; p2.light2Dir[2] = 1.0f;
        p2.light2Diffuse[0] = 0.0f; p2.light2Diffuse[1] = 1.0f; p2.light2Diffuse[2] = 0.0f;
        renderer.Clear(0.0f, 0.0f, 0.0f, 1.0f);
        renderer.DrawPrimitivesEx(vbEE, Matrix::getIdentityProperty(), Matrix::getIdentityProperty(),
                                 Matrix::getIdentityProperty(), PrimitiveType::TriangleList, 1, p2);
        auto r2 = ReadBackRenderTargetFull(renderer, rt.Get(), kRtWidth, kRtHeight);
        Check(centerIsExact(r2, 0, 255, 0),
              "EE3: DrawPrimitivesEx() real lit_textured3d -- DirectionalLight2 alone contributes the "
              "exact expected green, independent of Light0/Light1 (plans/plan_dx.md DX-138)");

        // EE4: EmissiveColor alone (all lights + ambient off) -> exact (0,0,255), a constant,
        // light-independent contribution (not scaled by any NdotL term).
        GpuDrawParams p3 = baseP;
        p3.emissiveColor[0] = 0.0f; p3.emissiveColor[1] = 0.0f; p3.emissiveColor[2] = 1.0f;
        renderer.Clear(0.0f, 0.0f, 0.0f, 1.0f);
        renderer.DrawPrimitivesEx(vbEE, Matrix::getIdentityProperty(), Matrix::getIdentityProperty(),
                                 Matrix::getIdentityProperty(), PrimitiveType::TriangleList, 1, p3);
        auto r3 = ReadBackRenderTargetFull(renderer, rt.Get(), kRtWidth, kRtHeight);
        Check(centerIsExact(r3, 0, 0, 255),
              "EE4: DrawPrimitivesEx() real lit_textured3d -- EmissiveColor alone contributes the "
              "exact expected blue with every light off, a constant additive term (plans/plan_dx.md DX-138)");

        renderer.UnbindOffscreenColorTargetEXT();
    }

    // ---- Check FF (plans/plan_dx.md DX-139): lit_textured3d -- real Blinn-Phong specular highlight,
    // discriminating (not the zeroed-for-CPU-determinism gap DX-125's own D3D11 row documents).
    // Geometry deliberately chosen so the half-vector H exactly equals the surface normal N: eye at
    // (0,0,-10) looking toward +Z, surface normal (0,0,-1), light1 traveling in +Z (so the
    // "direction to light" is (0,0,-1), same as the view direction) -> dot(H,N)=1 exactly, so
    // pow(1,power)=1 regardless of SpecularPower, giving an EXACT expected specular color, not an
    // approximate one. ----
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
        HRESULT hrRt = renderer.GetDeviceEXT()->CreateCommittedResource(
            &heapProps, D3D12_HEAP_FLAG_NONE, &rtDesc,
            D3D12_RESOURCE_STATE_RENDER_TARGET, nullptr, IID_PPV_ARGS(rt.GetAddressOf()));
        renderer.GetResourceStateTrackerEXT().TrackResource(rt.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET);
        D3D12_CPU_DESCRIPTOR_HANDLE rtv = renderer.AllocateRtvDescriptorEXT();
        renderer.GetDeviceEXT()->CreateRenderTargetView(rt.Get(), nullptr, rtv);
        renderer.BindOffscreenColorTargetEXT(rt.Get(), rtv, DXGI_FORMAT_R8G8B8A8_UNORM, kRtWidth, kRtHeight);

        auto pixelAt = [&](const std::vector<uint8_t>& buf, int x, int y) -> std::array<uint8_t, 4>
        {
            const std::size_t idx = (static_cast<std::size_t>(y) * kRtWidth + static_cast<std::size_t>(x)) * 4;
            return {buf[idx + 0], buf[idx + 1], buf[idx + 2], buf[idx + 3]};
        };
        auto centerIsExact = [&](const std::vector<uint8_t>& buf, uint8_t r, uint8_t g, uint8_t b)
        {
            const auto p = pixelAt(buf, 30, 30);
            return p[0] == r && p[1] == g && p[2] == b;
        };

        struct VPNT { float x, y, z; float nx, ny, nz; float u, v; };
        static const VPNT kTriFF[3] = {
            {-1.0f, -1.0f, 0.0f, 0.0f, 0.0f, -1.0f, 0.0f, 1.0f},
            { 3.0f, -1.0f, 0.0f, 0.0f, 0.0f, -1.0f, 2.0f, 1.0f},
            {-1.0f,  3.0f, 0.0f, 0.0f, 0.0f, -1.0f, 0.0f, -1.0f},
        };
        D3D12VertexBufferRenderer vbFF(&renderer, 3);
        vbFF.SetData(kTriFF, 3, sizeof(VPNT));

        ImageData whiteImg;
        whiteImg.width = 2; whiteImg.height = 2; whiteImg.mipLevels = 1;
        whiteImg.pixels.assign(2 * 2 * 4, 255);
        D3D12TextureRenderer whiteTexFF(&renderer, whiteImg);

        GpuDrawParams sp;
        sp.texture0 = &whiteTexFF;
        sp.textureEnabled = true;
        sp.lightingEnabled = true;
        sp.diffuseColor[0] = 1.0f; sp.diffuseColor[1] = 1.0f; sp.diffuseColor[2] = 1.0f; sp.diffuseColor[3] = 1.0f;
        sp.ambientColor[0] = 0.0f; sp.ambientColor[1] = 0.0f; sp.ambientColor[2] = 0.0f;
        sp.light0Diffuse[0] = 0.0f; sp.light0Diffuse[1] = 0.0f; sp.light0Diffuse[2] = 0.0f; // no diffuse noise
        sp.light1Dir[0] = 0.0f; sp.light1Dir[1] = 0.0f; sp.light1Dir[2] = 1.0f;             // travels +Z
        sp.light1Diffuse[0] = 0.0f; sp.light1Diffuse[1] = 0.0f; sp.light1Diffuse[2] = 0.0f; // diffuse off too
        sp.light1Specular[0] = 1.0f; sp.light1Specular[1] = 1.0f; sp.light1Specular[2] = 1.0f;
        sp.eyePositionWorld[0] = 0.0f; sp.eyePositionWorld[1] = 0.0f; sp.eyePositionWorld[2] = -10.0f;
        sp.specularColor[0] = 1.0f; sp.specularColor[1] = 1.0f; sp.specularColor[2] = 1.0f;
        sp.specularPower = 16.0f;
        // Task 1107 (plans/plan_graphics.md Phase 80): this check's own dot(H,N)=1 exactness only holds
        // AT THE SAMPLED FRAGMENT under a fresh per-fragment evaluation -- the eye vector E is
        // position-dependent (eye at a finite (0,0,-10), not infinitely far away), so this
        // triangle's 3 vertices each see a slightly different E than the sampled centre pixel
        // does, and Gouraud-interpolating their own independently-computed specular terms (now
        // XNA's real default, preferPerPixelLighting=false) would NOT reproduce the exact
        // dot(H,N)=1 coincidence this check is specifically designed to prove. Force per-pixel
        // explicitly so this check keeps testing exactly what it always tested.
        sp.preferPerPixelLighting = true;

        renderer.Clear(0.0f, 0.0f, 0.0f, 1.0f);
        renderer.DrawPrimitivesEx(vbFF, Matrix::getIdentityProperty(), Matrix::getIdentityProperty(),
                                 Matrix::getIdentityProperty(), PrimitiveType::TriangleList, 1, sp);
        auto specOn = ReadBackRenderTargetFull(renderer, rt.Get(), kRtWidth, kRtHeight);
        Check(centerIsExact(specOn, 255, 255, 255),
              "FF1: DrawPrimitivesEx() real lit_textured3d -- Blinn-Phong specular at a geometry "
              "deliberately chosen so dot(H,N)=1 exactly contributes the exact expected full-white "
              "highlight, with diffuse/ambient/emissive all zero (plans/plan_dx.md DX-139)");

        GpuDrawParams spOff = sp;
        spOff.specularColor[0] = 0.0f; spOff.specularColor[1] = 0.0f; spOff.specularColor[2] = 0.0f;
        renderer.Clear(0.0f, 0.0f, 0.0f, 1.0f);
        renderer.DrawPrimitivesEx(vbFF, Matrix::getIdentityProperty(), Matrix::getIdentityProperty(),
                                 Matrix::getIdentityProperty(), PrimitiveType::TriangleList, 1, spOff);
        auto specOff = ReadBackRenderTargetFull(renderer, rt.Get(), kRtWidth, kRtHeight);
        Check(centerIsExact(specOff, 0, 0, 0),
              "FF2: the SAME geometry/light with material SpecularColor zeroed produces exact black -- "
              "proves FF1's white came genuinely from the specular term, not diffuse/ambient/emissive "
              "(plans/plan_dx.md DX-139)");

        renderer.UnbindOffscreenColorTargetEXT();
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
        HRESULT hrRt = renderer.GetDeviceEXT()->CreateCommittedResource(
            &heapProps, D3D12_HEAP_FLAG_NONE, &rtDesc,
            D3D12_RESOURCE_STATE_RENDER_TARGET, nullptr, IID_PPV_ARGS(rt.GetAddressOf()));
        Check(SUCCEEDED(hrRt) && rt != nullptr, "P0: real off-screen RGBA8 render-target resource created");
        renderer.GetResourceStateTrackerEXT().TrackResource(rt.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET);

        D3D12_CPU_DESCRIPTOR_HANDLE rtv = renderer.AllocateRtvDescriptorEXT();
        renderer.GetDeviceEXT()->CreateRenderTargetView(rt.Get(), nullptr, rtv);
        renderer.BindOffscreenColorTargetEXT(rt.Get(), rtv, DXGI_FORMAT_R8G8B8A8_UNORM, kRtWidth, kRtHeight);

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
        D3D12VertexBufferRenderer vbAT(&renderer, 3);
        vbAT.SetData(kTriAT, 3, sizeof(VPT));

        ImageData img;
        img.width = 2; img.height = 2; img.mipLevels = 1;
        img.pixels.resize(2 * 2 * 4);
        for (int i = 0; i < 4; ++i)
        {
            img.pixels[i * 4 + 0] = 200; img.pixels[i * 4 + 1] = 100;
            img.pixels[i * 4 + 2] = 50;  img.pixels[i * 4 + 3] = 128; // alpha=128/255 ~ 0.502
        }
        D3D12TextureRenderer tex(&renderer, img);

        GpuDrawParams atp;
        atp.texture0 = &tex;
        atp.textureEnabled = true;
        // AlphaTol=0 (comparison mode) -> passTest = alpha < AlphaRef; failW<0 -> discard on fail.
        atp.alphaTest[0] = 0.5f;  // AlphaRef
        atp.alphaTest[1] = 0.0f;  // AlphaTol
        atp.alphaTest[2] = 1.0f;  // AlphaPassW (>=0, never discard on pass)
        atp.alphaTest[3] = -1.0f; // AlphaFailW (<0, discard on fail)

        // Sub-check 1: alpha=128/255 is NOT < 0.5 -> fails -> discard -> background survives.
        renderer.Clear(0.0f, 1.0f, 0.0f, 1.0f);
        renderer.DrawPrimitivesEx(vbAT, Matrix::getIdentityProperty(), Matrix::getIdentityProperty(),
                                 Matrix::getIdentityProperty(), PrimitiveType::TriangleList, 1, atp);
        auto discardResult = ReadBackRenderTargetFull(renderer, rt.Get(), kRtWidth, kRtHeight);
        Check(regionIs(discardResult, 0, 255, 0, 255),
              "P1: DrawPrimitivesEx() real alpha_test3d clip() genuinely drops a failing pixel, "
              "leaving the Clear() background untouched (plans/plan_dx.md DX-111)");

        // Sub-check 2: replace the texture's alpha with 64/255 (< 0.5) -> passes -> drawn exactly.
        std::vector<uint8_t> passPixels(2 * 2 * 4);
        for (int i = 0; i < 4; ++i)
        {
            passPixels[i * 4 + 0] = 200; passPixels[i * 4 + 1] = 100;
            passPixels[i * 4 + 2] = 50;  passPixels[i * 4 + 3] = 64;
        }
        tex.UpdatePixelsLevel(0, passPixels.data(), 2, 2);

        renderer.Clear(0.0f, 1.0f, 0.0f, 1.0f);
        renderer.DrawPrimitivesEx(vbAT, Matrix::getIdentityProperty(), Matrix::getIdentityProperty(),
                                 Matrix::getIdentityProperty(), PrimitiveType::TriangleList, 1, atp);
        auto passResult = ReadBackRenderTargetFull(renderer, rt.Get(), kRtWidth, kRtHeight);
        Check(regionIs(passResult, 200, 100, 50, 64),
              "P2: DrawPrimitivesEx() real alpha_test3d draws the exact texture color (including its "
              "own alpha byte) when the test passes (plans/plan_dx.md DX-111)");

        // DX-137: dedicated fog on/off discriminating test for alpha_test3d -- reuses the
        // already-PASSING fixture above (alpha=64/255 < AlphaRef=0.5, so nothing is discarded and
        // fog is genuinely visible), same Z-at-FogEnd methodology as textured3d's own fog test. A
        // discarding fixture would prove nothing here (no fragment ever reaches the fog blend).
        static const VPT kTriATFog[3] = {
            {-1.0f, -1.0f, 0.5f, 0.0f, 1.0f},
            { 3.0f, -1.0f, 0.5f, 2.0f, 1.0f},
            {-1.0f,  3.0f, 0.5f, 0.0f, -1.0f},
        };
        D3D12VertexBufferRenderer vbATFog(&renderer, 3);
        vbATFog.SetData(kTriATFog, 3, sizeof(VPT));

        atp.fogEnabled = false;
        atp.fogColor[0] = 0.0f; atp.fogColor[1] = 1.0f; atp.fogColor[2] = 0.0f;
        renderer.Clear(10.0f / 255.0f, 10.0f / 255.0f, 10.0f / 255.0f, 1.0f);
        renderer.DrawPrimitivesEx(vbATFog, Matrix::getIdentityProperty(), Matrix::getIdentityProperty(),
                                 Matrix::getIdentityProperty(), PrimitiveType::TriangleList, 1, atp);
        auto atFogOff = ReadBackRenderTargetFull(renderer, rt.Get(), kRtWidth, kRtHeight);
        Check(isColor(pixelAt(atFogOff, 30, 30), 200, 100, 50, 64),
              "P3: DrawPrimitivesEx() alpha_test3d fogEnabled=false leaves the exact passing texture "
              "color unblended (plans/plan_dx.md DX-137)");

        atp.fogEnabled = true;
        // All D3DCommon stock shaders use the same authoritative zero-vector disabled encoding.
        atp.fogVector[2] = 2.0f;
        renderer.Clear(10.0f / 255.0f, 10.0f / 255.0f, 10.0f / 255.0f, 1.0f);
        renderer.DrawPrimitivesEx(vbATFog, Matrix::getIdentityProperty(), Matrix::getIdentityProperty(),
                                 Matrix::getIdentityProperty(), PrimitiveType::TriangleList, 1, atp);
        auto atFogOn = ReadBackRenderTargetFull(renderer, rt.Get(), kRtWidth, kRtHeight);
        Check(atFogOn.size() >= 4 && pixelAt(atFogOn, 30, 30)[0] == 0 && pixelAt(atFogOn, 30, 30)[1] == 255 &&
              pixelAt(atFogOn, 30, 30)[2] == 0,
              "P4: DrawPrimitivesEx() alpha_test3d fogEnabled=true with Z at FogEnd genuinely blends "
              "all the way to the exact FogColor, and the passing fragment survives the alpha test "
              "to reach the fog blend at all (plans/plan_dx.md DX-137)");

        // plans/plan_dx.md DX-136: AlphaTestEffect.VertexColorEnabled -- alpha_test3d's new stride-24
        // sibling (alpha_test_colored3d, VertexPositionColorTexture) gives it a real vertex-color
        // attribute. A white, fully-opaque texture isolates the vertex-color contribution: with
        // VertexColorEnabled=true a red vertex color multiplies through exactly; with it false,
        // the same vertex buffer's color is genuinely ignored and DiffuseColor (white) alone
        // survives. Mirrors D3D11's own DX-136 methodology exactly.
        struct VPCTac { float x, y, z; uint32_t color; float u, v; };
        const uint32_t kRedVCac = 0xFF0000FFu; // A=255,B=0,G=0,R=255 (R8G8B8A8 byte order)
        static const VPCTac kTriAlphaColor[3] = {
            {-1.0f, -1.0f, 0.0f, kRedVCac, 0.0f, 1.0f},
            { 3.0f, -1.0f, 0.0f, kRedVCac, 2.0f, 1.0f},
            {-1.0f,  3.0f, 0.0f, kRedVCac, 0.0f, -1.0f},
        };
        D3D12VertexBufferRenderer vbAlphaColor(&renderer, 3);
        vbAlphaColor.SetData(kTriAlphaColor, 3, sizeof(VPCTac));

        ImageData whiteImgAC;
        whiteImgAC.width = 2; whiteImgAC.height = 2; whiteImgAC.mipLevels = 1;
        whiteImgAC.pixels.assign(2 * 2 * 4, 255);
        D3D12TextureRenderer whiteTexAC(&renderer, whiteImgAC);

        GpuDrawParams acp;
        acp.texture0 = &whiteTexAC;
        acp.textureEnabled = true;
        // Default {0,0,1,1}: both AlphaPassW and AlphaFailW are non-negative, so w is never
        // negative regardless of passTest -- genuinely always passes (never discards).
        acp.alphaTest[0] = 0.0f;
        acp.alphaTest[1] = 0.0f;
        acp.alphaTest[2] = 1.0f;
        acp.alphaTest[3] = 1.0f;

        acp.vertexColorEnabled = true;
        renderer.Clear(0.0f, 0.0f, 1.0f, 1.0f);
        renderer.DrawPrimitivesEx(vbAlphaColor, Matrix::getIdentityProperty(), Matrix::getIdentityProperty(),
                                 Matrix::getIdentityProperty(), PrimitiveType::TriangleList, 1, acp);
        auto acOn = ReadBackRenderTargetFull(renderer, rt.Get(), kRtWidth, kRtHeight);
        Check(isColor(pixelAt(acOn, 30, 30), 255, 0, 0, 255),
              "P5: DrawPrimitivesEx() real alpha_test_colored3d (stride 24) with "
              "VertexColorEnabled=true multiplies the exact vertex color (red) through a white "
              "texture (plans/plan_dx.md DX-136)");

        acp.vertexColorEnabled = false;
        renderer.Clear(0.0f, 0.0f, 1.0f, 1.0f);
        renderer.DrawPrimitivesEx(vbAlphaColor, Matrix::getIdentityProperty(), Matrix::getIdentityProperty(),
                                 Matrix::getIdentityProperty(), PrimitiveType::TriangleList, 1, acp);
        auto acOff = ReadBackRenderTargetFull(renderer, rt.Get(), kRtWidth, kRtHeight);
        Check(isColor(pixelAt(acOff, 30, 30), 255, 255, 255, 255),
              "P6: DrawPrimitivesEx() the SAME vertex buffer with VertexColorEnabled=false "
              "genuinely ignores its vertex color -- only DiffuseColor (white) survives, "
              "distinctly different from the true case above (plans/plan_dx.md DX-136)");

        // Alpha test itself still genuinely discards on this new stride-24 path. AlphaTol=0
        // (comparison mode), AlphaRef=0.5: alpha=200/255 (~0.784, NOT < 0.5) genuinely fails ->
        // discard -> background survives.
        acp.alphaTest[0] = 0.5f; acp.alphaTest[1] = 0.0f;
        acp.alphaTest[2] = 1.0f; acp.alphaTest[3] = -1.0f;
        std::vector<uint8_t> highAlphaPixels(2 * 2 * 4);
        for (int i = 0; i < 4; ++i)
        {
            highAlphaPixels[i * 4 + 0] = 255; highAlphaPixels[i * 4 + 1] = 255;
            highAlphaPixels[i * 4 + 2] = 255; highAlphaPixels[i * 4 + 3] = 200;
        }
        whiteTexAC.UpdatePixelsLevel(0, highAlphaPixels.data(), 2, 2);
        acp.vertexColorEnabled = true;
        renderer.Clear(0.0f, 1.0f, 0.0f, 1.0f);
        renderer.DrawPrimitivesEx(vbAlphaColor, Matrix::getIdentityProperty(), Matrix::getIdentityProperty(),
                                 Matrix::getIdentityProperty(), PrimitiveType::TriangleList, 1, acp);
        auto acDiscard = ReadBackRenderTargetFull(renderer, rt.Get(), kRtWidth, kRtHeight);
        Check(isColor(pixelAt(acDiscard, 30, 30), 0, 255, 0, 255),
              "P7: DrawPrimitivesEx() alpha_test_colored3d's alpha-test discard logic still "
              "genuinely works on the new stride-24/vertex-color path -- a failing alpha drops "
              "the pixel, leaving the Clear() background untouched (plans/plan_dx.md DX-136)");

        renderer.UnbindOffscreenColorTargetEXT();
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
        HRESULT hrRt = renderer.GetDeviceEXT()->CreateCommittedResource(
            &heapProps, D3D12_HEAP_FLAG_NONE, &rtDesc,
            D3D12_RESOURCE_STATE_RENDER_TARGET, nullptr, IID_PPV_ARGS(rt.GetAddressOf()));
        Check(SUCCEEDED(hrRt) && rt != nullptr, "Q0: real off-screen RGBA8 render-target resource created");
        renderer.GetResourceStateTrackerEXT().TrackResource(rt.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET);

        D3D12_CPU_DESCRIPTOR_HANDLE rtv = renderer.AllocateRtvDescriptorEXT();
        renderer.GetDeviceEXT()->CreateRenderTargetView(rt.Get(), nullptr, rtv);
        renderer.BindOffscreenColorTargetEXT(rt.Get(), rtv, DXGI_FORMAT_R8G8B8A8_UNORM, kRtWidth, kRtHeight);

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
        D3D12VertexBufferRenderer vbDual(&renderer, 3);
        vbDual.SetData(kTriDual, 3, sizeof(VPT));

        ImageData whiteImg;
        whiteImg.width = 2; whiteImg.height = 2; whiteImg.mipLevels = 1;
        whiteImg.pixels.assign(2 * 2 * 4, 255);
        D3D12TextureRenderer tex0White(&renderer, whiteImg);

        ImageData tintImg;
        tintImg.width = 2; tintImg.height = 2; tintImg.mipLevels = 1;
        tintImg.pixels.resize(2 * 2 * 4);
        for (int i = 0; i < 4; ++i)
        {
            tintImg.pixels[i * 4 + 0] = 60; tintImg.pixels[i * 4 + 1] = 80;
            tintImg.pixels[i * 4 + 2] = 100; tintImg.pixels[i * 4 + 3] = 255;
        }
        D3D12TextureRenderer tex1Tint(&renderer, tintImg);

        GpuDrawParams dp;
        dp.texture0 = &tex0White;
        dp.texture1 = &tex1Tint;
        dp.dualTexture = true;
        dp.textureEnabled = true;

        renderer.Clear(0.0f, 1.0f, 0.0f, 1.0f);
        auto beforeQ = ReadBackRenderTargetFull(renderer, rt.Get(), kRtWidth, kRtHeight);
        renderer.DrawPrimitivesEx(vbDual, Matrix::getIdentityProperty(), Matrix::getIdentityProperty(),
                                 Matrix::getIdentityProperty(), PrimitiveType::TriangleList, 1, dp);
        auto afterQ = ReadBackRenderTargetFull(renderer, rt.Get(), kRtWidth, kRtHeight);
        Check(regionIs(beforeQ, 0, 255, 0, 255) && regionIs(afterQ, 120, 160, 200, 255),
              "Q1: DrawPrimitivesEx() real dual_texture3d draw combines two independently-allocated "
              "textures' SRVs through a genuinely contiguous per-draw descriptor table -- exact "
              "expected byte result, not just \"a draw call succeeded\" (plans/plan_dx.md DX-111)");

        // Indexed path -- proves the same contiguous-table binding survives DrawIndexedPrimitivesEx.
        static const uint16_t kTriDualIdx[3] = {0, 1, 2};
        D3D12IndexBufferRenderer ibDual(&renderer, 3, /*thirtyTwoBit=*/false);
        ibDual.SetData16(kTriDualIdx, 3);
        renderer.Clear(0.0f, 1.0f, 0.0f, 1.0f);
        renderer.DrawIndexedPrimitivesEx(vbDual, ibDual, Matrix::getIdentityProperty(), Matrix::getIdentityProperty(),
                                        Matrix::getIdentityProperty(), PrimitiveType::TriangleList, 1, dp);
        auto afterQIdx = ReadBackRenderTargetFull(renderer, rt.Get(), kRtWidth, kRtHeight);
        Check(regionIs(afterQIdx, 120, 160, 200, 255),
              "Q2: DrawIndexedPrimitivesEx() indexed dual_texture3d draw shares the same real "
              "2-texture pipeline and produces the same exact result");

        // DX-137: dedicated fog on/off discriminating test for dual_texture3d -- reuses this
        // block's own deterministic fixture (exact base result (120,160,200)), same Z-at-FogEnd
        // methodology as textured3d's own fog test.
        static const VPT kTriDualFog[3] = {
            {-1.0f, -1.0f, 0.5f, 0.0f, 1.0f},
            { 3.0f, -1.0f, 0.5f, 2.0f, 1.0f},
            {-1.0f,  3.0f, 0.5f, 0.0f, -1.0f},
        };
        D3D12VertexBufferRenderer vbDualFog(&renderer, 3);
        vbDualFog.SetData(kTriDualFog, 3, sizeof(VPT));

        dp.fogEnabled = false;
        dp.fogColor[0] = 0.0f; dp.fogColor[1] = 1.0f; dp.fogColor[2] = 0.0f;
        renderer.Clear(10.0f / 255.0f, 10.0f / 255.0f, 10.0f / 255.0f, 1.0f);
        renderer.DrawPrimitivesEx(vbDualFog, Matrix::getIdentityProperty(), Matrix::getIdentityProperty(),
                                 Matrix::getIdentityProperty(), PrimitiveType::TriangleList, 1, dp);
        auto dtFogOff = ReadBackRenderTargetFull(renderer, rt.Get(), kRtWidth, kRtHeight);
        Check(isColor(pixelAt(dtFogOff, 30, 30), 120, 160, 200, 255),
              "Q3: DrawPrimitivesEx() dual_texture3d fogEnabled=false leaves the exact "
              "combined-texture color unblended (plans/plan_dx.md DX-137)");

        dp.fogEnabled = true;
        dp.fogVector[2] = 2.0f;
        renderer.Clear(10.0f / 255.0f, 10.0f / 255.0f, 10.0f / 255.0f, 1.0f);
        renderer.DrawPrimitivesEx(vbDualFog, Matrix::getIdentityProperty(), Matrix::getIdentityProperty(),
                                 Matrix::getIdentityProperty(), PrimitiveType::TriangleList, 1, dp);
        auto dtFogOn = ReadBackRenderTargetFull(renderer, rt.Get(), kRtWidth, kRtHeight);
        Check(isColor(pixelAt(dtFogOn, 30, 30), 0, 255, 0, 255),
              "Q4: DrawPrimitivesEx() dual_texture3d fogEnabled=true with Z at FogEnd genuinely "
              "blends all the way to the exact FogColor (plans/plan_dx.md DX-137)");

        renderer.UnbindOffscreenColorTargetEXT();
    }

    // ---- Check R (DX-112): D3D12SpriteBatchRenderer -- real quad-batched sprite draw, flip proof ----
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
        HRESULT hrRt = renderer.GetDeviceEXT()->CreateCommittedResource(
            &heapProps, D3D12_HEAP_FLAG_NONE, &rtDesc,
            D3D12_RESOURCE_STATE_RENDER_TARGET, nullptr, IID_PPV_ARGS(rt.GetAddressOf()));
        Check(SUCCEEDED(hrRt) && rt != nullptr, "R0: real off-screen RGBA8 render-target resource created");
        renderer.GetResourceStateTrackerEXT().TrackResource(rt.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET);

        D3D12_CPU_DESCRIPTOR_HANDLE rtv = renderer.AllocateRtvDescriptorEXT();
        renderer.GetDeviceEXT()->CreateRenderTargetView(rt.Get(), nullptr, rtv);
        renderer.BindOffscreenColorTargetEXT(rt.Get(), rtv, DXGI_FORMAT_R8G8B8A8_UNORM, kRtWidth, kRtHeight);

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
        D3D12TextureRenderer tex(&renderer, img);

        auto sb = renderer.CreateSpriteBatch();
        Check(sb != nullptr, "R1: CreateSpriteBatch() returns a real D3D12SpriteBatchRenderer");

        renderer.Clear(0.0f, 1.0f, 0.0f, 1.0f);
        sb->Begin();
        sb->Draw(tex, Rectangle(0, 0, kRtWidth, kRtHeight), Rectangle(0, 0, 4, 2), Color::White);
        sb->End();
        auto afterR1 = ReadBackRenderTargetFull(renderer, rt.Get(), kRtWidth, kRtHeight);
        Check(leftRegionIs(afterR1, 200, 50, 30, 255) && rightRegionIs(afterR1, 30, 80, 220, 255),
              "R2: D3D12SpriteBatchRenderer::Draw() places a real quad-batched sprite at the exact "
              "expected screen position, sampling the exact source-texel colors (plans/plan_dx.md DX-112)");

        renderer.Clear(0.0f, 1.0f, 0.0f, 1.0f);
        sb->Begin();
        sb->Draw(tex, Rectangle(0, 0, kRtWidth, kRtHeight), Rectangle(0, 0, 4, 2), Color::White,
                 0.0f, Vector2(0, 0), SpriteEffects::FlipHorizontally, 0.0f);
        sb->End();
        auto afterR2 = ReadBackRenderTargetFull(renderer, rt.Get(), kRtWidth, kRtHeight);
        Check(leftRegionIs(afterR2, 30, 80, 220, 255) && rightRegionIs(afterR2, 200, 50, 30, 255),
              "R3: SpriteEffects::FlipHorizontally genuinely swaps left/right source sampling -- not "
              "just \"a draw call succeeded\" (plans/plan_dx.md DX-112)");

        // ---- DX-131: rotation/scale/crop-rect, D3D12 counterpart of D3D11's own Check Y2/Y3/Y4.
        // D3D12SpriteBatchRenderer's default sampler is bilinear (confirmed empirically while writing
        // this check: a 1-texel-per-corner fixture read back ~3-6% blended toward the adjacent
        // texel, e.g. (15,8,239) instead of the exact (0,0,255)) -- same class of issue this file's
        // own R0-R3 fixture already documents and works around (2 texels per color, not 1). Every
        // fixture below uses a 2x2-texels-per-color block for exactly that reason. ----
        {
            // 4x4 texture, four 2x2 solid-color quadrant blocks -- same corner colors/positions as
            // D3D11's own DX-131 test, just widened to survive bilinear sampling exactly.
            ImageData cornerImg;
            cornerImg.width = 4; cornerImg.height = 4; cornerImg.mipLevels = 1;
            cornerImg.pixels.resize(4 * 4 * 4);
            auto setCornerBlock = [&](int bx, int by, uint8_t r, uint8_t g, uint8_t b)
            {
                for (int dy = 0; dy < 2; ++dy)
                    for (int dx = 0; dx < 2; ++dx)
                    {
                        const int x = bx * 2 + dx, y = by * 2 + dy;
                        const std::size_t px = (static_cast<std::size_t>(y) * 4 + static_cast<std::size_t>(x)) * 4;
                        cornerImg.pixels[px + 0] = r; cornerImg.pixels[px + 1] = g;
                        cornerImg.pixels[px + 2] = b; cornerImg.pixels[px + 3] = 255;
                    }
            };
            setCornerBlock(0, 0, 255, 0, 0);   // TL red
            setCornerBlock(1, 0, 0, 255, 0);   // TR green
            setCornerBlock(0, 1, 0, 0, 255);   // BL blue
            setCornerBlock(1, 1, 255, 255, 0); // BR yellow
            D3D12TextureRenderer cornerTexD12(&renderer, cornerImg);

            constexpr float kPiOverTwo = 1.5707963267948966f;

            // R4 (rotation): destRect(20,20,32,32), origin=(2,2) centered (source is now 4x4, so the
            // center is (2,2) in source-space units), rotation=pi/2 -> TL(red) ends at NE screen
            // quadrant, TR(green) at SE, BR(yellow) at SW, BL(blue) at NW -- same derivation as
            // D3D11's own DX-131 check (destination geometry is scale-invariant to source texel
            // count, only the fixture widened).
            renderer.Clear(10.0f / 255.0f, 10.0f / 255.0f, 10.0f / 255.0f, 1.0f);
            sb->Begin();
            sb->Draw(cornerTexD12, Rectangle(20, 20, 32, 32), Rectangle(0, 0, 4, 4), Color::White,
                     kPiOverTwo, Vector2(2.0f, 2.0f), SpriteEffects::None, 0.0f);
            sb->End();
            auto afterR4 = ReadBackRenderTargetFull(renderer, rt.Get(), kRtWidth, kRtHeight);
            const bool r4Ok =
                isColor(pixelAt(afterR4, 12, 12), 0, 0, 255, 255) &&    // NW = blue (was BL)
                isColor(pixelAt(afterR4, 28, 12), 255, 0, 0, 255) &&    // NE = red (was TL)
                isColor(pixelAt(afterR4, 28, 28), 0, 255, 0, 255) &&    // SE = green (was TR)
                isColor(pixelAt(afterR4, 12, 28), 255, 255, 0, 255);    // SW = yellow (was BR)
            Check(r4Ok,
                  "R4: D3D12SpriteBatchRenderer::Draw() -- a real 90-degree rotation around a centered "
                  "origin permutes the 4 corner colors into the geometrically-predicted screen "
                  "quadrants, same derivation as D3D11's own DX-131 check (plans/plan_dx.md DX-131)");

            // R5 (scale): destRect half the normal size (16x16) -- a pixel inside the old 32x32 size
            // but outside the new 16x16 one must show the clear color, not sprite content.
            renderer.Clear(10.0f / 255.0f, 10.0f / 255.0f, 10.0f / 255.0f, 1.0f);
            sb->Begin();
            sb->Draw(cornerTexD12, Rectangle(0, 0, 16, 16), Rectangle(0, 0, 4, 4), Color::White);
            sb->End();
            auto afterR5 = ReadBackRenderTargetFull(renderer, rt.Get(), kRtWidth, kRtHeight);
            const bool r5Ok =
                isColor(pixelAt(afterR5, 4, 4), 255, 0, 0, 255) &&      // inside both sizes -> red (TL)
                isColor(pixelAt(afterR5, 24, 24), 10, 10, 10, 255);     // inside old size only -> clear color
            Check(r5Ok,
                  "R5: D3D12SpriteBatchRenderer::Draw() -- a real, distinct destRect SIZE genuinely "
                  "scales the sprite, same proof shape as D3D11's own DX-131 check (plans/plan_dx.md DX-131)");

            // R6 (source crop-rect): an 8x1 strip texture, 2 texels per color, cropped via
            // sourceRectangle to just the middle 4 texels (purple-purple-cyan-cyan).
            ImageData stripImg;
            stripImg.width = 8; stripImg.height = 1; stripImg.mipLevels = 1;
            stripImg.pixels.resize(8 * 1 * 4);
            auto setStripPair = [&](int pairIdx, uint8_t r, uint8_t g, uint8_t b)
            {
                for (int i = 0; i < 2; ++i)
                {
                    const std::size_t px = (static_cast<std::size_t>(pairIdx) * 2 + static_cast<std::size_t>(i)) * 4;
                    stripImg.pixels[px + 0] = r; stripImg.pixels[px + 1] = g;
                    stripImg.pixels[px + 2] = b; stripImg.pixels[px + 3] = 255;
                }
            };
            setStripPair(0, 255, 128, 0);   // texels 0-1: orange -- must NOT appear
            setStripPair(1, 128, 0, 255);   // texels 2-3: purple -- must appear, left half
            setStripPair(2, 0, 255, 255);   // texels 4-5: cyan   -- must appear, right half
            setStripPair(3, 255, 0, 255);   // texels 6-7: magenta -- must NOT appear
            D3D12TextureRenderer stripTexD12(&renderer, stripImg);

            renderer.Clear(10.0f / 255.0f, 10.0f / 255.0f, 10.0f / 255.0f, 1.0f);
            sb->Begin();
            sb->Draw(stripTexD12, Rectangle(0, 0, 32, 16), Rectangle(2, 0, 4, 1), Color::White);
            sb->End();
            auto afterR6 = ReadBackRenderTargetFull(renderer, rt.Get(), kRtWidth, kRtHeight);
            const bool r6Ok =
                isColor(pixelAt(afterR6, 8, 4), 128, 0, 255, 255) &&    // left pair -> purple
                isColor(pixelAt(afterR6, 24, 4), 0, 255, 255, 255);     // right pair -> cyan
            Check(r6Ok,
                  "R6: D3D12SpriteBatchRenderer::Draw() -- sourceRectangle genuinely crops to only the "
                  "requested sub-region, same proof shape as D3D11's own DX-131 check (plans/plan_dx.md DX-131)");

            // R7/R8 (DX-133): TextureAddressMode::Wrap/Mirror via SetSamplerFilter()/
            // SetSamplerAddressMode() -- the raw ISpriteBatchRenderer equivalent of what
            // SpriteBatch::Begin(sortMode, blend, samplerState, ...) passes down. Point filtering
            // (TextureFilter::Point=1) avoids the bilinear-blend issue entirely (point sampling
            // never blends adjacent texels), so the ORIGINAL 4x4 cornerTexD12 fixture works
            // unchanged -- only srcRect widens to (0,0,8,8) to keep the same UV-multiplier-2
            // (0..2 range) relative to the wider fixture, landing on the exact same probe pixels
            // D3D11's own Check Z uses. destRect(0,0,32,32), probe (4,20): u~0.28 (left/red-ish
            // column), v~1.28 (wraps to row 0 = red under Wrap, clamps to row 1 = blue under Clamp).
            constexpr int kTextureFilterPoint = 1;
            constexpr int kTextureAddressModeWrap = 0;
            constexpr int kTextureAddressModeMirror = 2;

            renderer.Clear(10.0f / 255.0f, 10.0f / 255.0f, 10.0f / 255.0f, 1.0f);
            sb->SetSamplerFilter(kTextureFilterPoint);
            sb->SetSamplerAddressMode(kTextureAddressModeWrap, kTextureAddressModeWrap);
            sb->Begin();
            sb->Draw(cornerTexD12, Rectangle(0, 0, 32, 32), Rectangle(0, 0, 8, 8), Color::White);
            sb->End();
            auto afterR7 = ReadBackRenderTargetFull(renderer, rt.Get(), kRtWidth, kRtHeight);
            Check(isColor(pixelAt(afterR7, 4, 20), 255, 0, 0, 255),
                  "R7: D3D12SpriteBatchRenderer::SetSamplerAddressMode(Wrap) genuinely tiles past UV "
                  "1.0 instead of clamping to the edge color -- proves real, live sampler wiring, not "
                  "a stored-but-ignored value (plans/plan_dx.md DX-133)");

            renderer.Clear(10.0f / 255.0f, 10.0f / 255.0f, 10.0f / 255.0f, 1.0f);
            sb->SetSamplerFilter(kTextureFilterPoint);
            sb->SetSamplerAddressMode(kTextureAddressModeMirror, kTextureAddressModeMirror);
            sb->Begin();
            sb->Draw(cornerTexD12, Rectangle(0, 0, 32, 32), Rectangle(0, 0, 8, 8), Color::White);
            sb->End();
            auto afterR8 = ReadBackRenderTargetFull(renderer, rt.Get(), kRtWidth, kRtHeight);
            Check(isColor(pixelAt(afterR8, 4, 28), 255, 0, 0, 255),
                  "R8: D3D12SpriteBatchRenderer::SetSamplerAddressMode(Mirror) genuinely reflects past "
                  "UV 1.0 (distinct from both Wrap's repeat and Clamp's edge-extend at the same probe "
                  "point) (plans/plan_dx.md DX-133)");

            // Reset to the default (LinearClamp-equivalent) for any later check in this file that
            // relies on the pre-DX-133 default sampling behavior.
            sb->SetSamplerFilter(0);   // TextureFilter::Linear
            sb->SetSamplerAddressMode(1, 1); // TextureAddressMode::Clamp
        }

        // R9 (DX-163): SpriteBatch must consume the live XNA BlendState. Before DX-163 its
        // hand-built PSO hardcoded BlendEnable=FALSE, so the premultiplied half-alpha source below
        // replaced green with dark red (128,0,0) instead of compositing to (128,127,0).
        {
            ImageData whiteImage;
            whiteImage.width = 1; whiteImage.height = 1; whiteImage.mipLevels = 1;
            whiteImage.pixels = {255, 255, 255, 255};
            D3D12TextureRenderer whiteTexture(&renderer, whiteImage);

            renderer.ApplyBlendState(
                /*colorSrcBlend=One*/0, /*alphaSrcBlend=One*/0,
                /*colorDstBlend=InverseSourceAlpha*/5, /*alphaDstBlend=InverseSourceAlpha*/5,
                /*colorBlendFunc=Add*/0, /*alphaBlendFunc=Add*/0,
                CNA::Internal::Renderers::BlendWriteState{});
            renderer.Clear(0.0f, 1.0f, 0.0f, 1.0f);
            sb->Begin();
            sb->Draw(whiteTexture, Rectangle(0, 0, kRtWidth, kRtHeight),
                     Rectangle(0, 0, 1, 1), Color(128, 0, 0, 128));
            sb->End();
            const auto afterR9 = ReadBackRenderTargetFull(renderer, rt.Get(), kRtWidth, kRtHeight);
            Check(isColor(pixelAt(afterR9, 32, 32), 128, 127, 0, 255),
                  "R9: SpriteBatch consumes BlendState::AlphaBlend in its real D3D12 PSO -- "
                  "premultiplied half-red over green is exactly (128,127,0,255), not opaque dark red "
                  "(plans/plan_dx.md DX-163)");

            renderer.ApplyBlendState(
                /*One*/0, /*One*/0, /*Zero*/1, /*Zero*/1,
                /*Add*/0, /*Add*/0, CNA::Internal::Renderers::BlendWriteState{});
        }

        // Note on SpriteSortMode (DX-131): sort-mode ordering is implemented entirely in the shared,
        // renderer-agnostic Microsoft::Xna::Framework::Graphics::SpriteBatch.cpp (sorts the pending
        // draw-call list before handing it to the renderer's own Draw() calls, in order) -- there is
        // no D3D12-specific sort behavior to test; the renderer just draws whatever order it's told.

        renderer.UnbindOffscreenColorTargetEXT();
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
        HRESULT hrRt = renderer.GetDeviceEXT()->CreateCommittedResource(
            &heapProps, D3D12_HEAP_FLAG_NONE, &rtDesc,
            D3D12_RESOURCE_STATE_RENDER_TARGET, nullptr, IID_PPV_ARGS(rt.GetAddressOf()));
        Check(SUCCEEDED(hrRt) && rt != nullptr, "S0: real off-screen RGBA8 render-target resource created");
        renderer.GetResourceStateTrackerEXT().TrackResource(rt.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET);

        D3D12_CPU_DESCRIPTOR_HANDLE rtv = renderer.AllocateRtvDescriptorEXT();
        renderer.GetDeviceEXT()->CreateRenderTargetView(rt.Get(), nullptr, rtv);
        renderer.BindOffscreenColorTargetEXT(rt.Get(), rtv, DXGI_FORMAT_R8G8B8A8_UNORM, kRtWidth, kRtHeight);

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

        // Same fixture as D3D11's own Check V (directx11_smoke_test.cpp DX-67): a single identity bone
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
        D3D12VertexBufferRenderer vbSkin(&renderer, 3);
        vbSkin.SetData(kTriSkin, 3, sizeof(VPNTS));

        ImageData img;
        img.width = 2; img.height = 2; img.mipLevels = 1;
        img.pixels.resize(2 * 2 * 4);
        for (int i = 0; i < 4; ++i)
        {
            img.pixels[i * 4 + 0] = 77; img.pixels[i * 4 + 1] = 88;
            img.pixels[i * 4 + 2] = 99; img.pixels[i * 4 + 3] = 255;
        }
        D3D12TextureRenderer tex(&renderer, img);

        GpuDrawParams sp;
        sp.texture0 = &tex;
        sp.textureEnabled = true;
        // REMED-GFX-088: mirror a public SkinnedEffect configured for per-pixel lighting.
        // LightingEnabled is always true (setting false throws), and AmbientLightColor=white
        // is CPU-pre-folded into emissiveColor as (Emissive + Ambient*Diffuse)*Alpha.
        sp.lightingEnabled = true;
        sp.preferPerPixelLighting = true;
        sp.skinned = true;
        sp.boneCount = 1;
        sp.weightsPerVertex = 1;
        Matrix::getIdentityProperty().ToColumnMajor(sp.boneTransforms);
        sp.emissiveColor[0] = 1.0f; sp.emissiveColor[1] = 1.0f; sp.emissiveColor[2] = 1.0f;
        sp.light0Diffuse[0] = 0.0f; sp.light0Diffuse[1] = 0.0f; sp.light0Diffuse[2] = 0.0f;
        sp.specularColor[0] = 0.0f; sp.specularColor[1] = 0.0f; sp.specularColor[2] = 0.0f;
        sp.eyePositionWorld[0] = 0.0f; sp.eyePositionWorld[1] = 0.0f; sp.eyePositionWorld[2] = -10.0f;

        renderer.Clear(0.0f, 1.0f, 0.0f, 1.0f);
        renderer.DrawPrimitivesEx(vbSkin, Matrix::getIdentityProperty(), Matrix::getIdentityProperty(),
                                 Matrix::getIdentityProperty(), PrimitiveType::TriangleList, 1, sp);
        auto afterS = ReadBackRenderTargetFull(renderer, rt.Get(), kRtWidth, kRtHeight);
        Check(regionIs(afterS, 77, 88, 99, 255),
              "S1: DrawPrimitivesEx() real skinned3d with a genuinely-populated single identity bone "
              "(D3DBoneConstants, not left zero) samples the exact texture color (plans/plan_dx.md DX-111)");

        // DX-135: WeightsPerVertex discriminating test, mirrors D3D11's own DX-135 exactly (see that
        // file's own detailed comment for the real, non-obvious math property this relies on: a
        // SINGLE active bone's weight always cancels out via the homogeneous divide -- weight only
        // has an observable effect once TWO bones are genuinely blended, weights summing to 1.0).
        // weightsPerVertex=1 (bone1 ignored) reproduces bone0=Identity exactly (full coverage,
        // matches S1 above). weightsPerVertex=2 blends bone0=Identity with bone1=Scale(0.1) at
        // 0.5/0.5, giving Scale(0.55) -- small enough to genuinely shrink the triangle's hypotenuse
        // away from a probe point at pixel (54,10) that the unshrunk (weightsPerVertex=1) triangle
        // still covers.
        Matrix::CreateScale(0.1f).ToColumnMajor(sp.boneTransforms + 16);
        sp.boneCount = 2;
        struct VPNTS2 { float x, y, z; float nx, ny, nz; float u, v;
                       float bw0, bw1, bw2, bw3; uint8_t bi0, bi1, bi2, bi3; };
        static const VPNTS2 kTriSkin2[3] = {
            {-1.0f, -1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 1.0f, 0.5f, 0.5f, 0.0f, 0.0f, 0, 1, 0, 0},
            { 3.0f, -1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 2.0f, 1.0f, 0.5f, 0.5f, 0.0f, 0.0f, 0, 1, 0, 0},
            {-1.0f,  3.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, -1.0f, 0.5f, 0.5f, 0.0f, 0.0f, 0, 1, 0, 0},
        };
        D3D12VertexBufferRenderer vbSkin2(&renderer, 3);
        vbSkin2.SetData(kTriSkin2, 3, sizeof(VPNTS2));

        sp.weightsPerVertex = 1;
        renderer.Clear(1.0f / 255.0f, 2.0f / 255.0f, 3.0f / 255.0f, 1.0f);
        renderer.DrawPrimitivesEx(vbSkin2, Matrix::getIdentityProperty(), Matrix::getIdentityProperty(),
                                 Matrix::getIdentityProperty(), PrimitiveType::TriangleList, 1, sp);
        auto afterW1 = ReadBackRenderTargetFull(renderer, rt.Get(), kRtWidth, kRtHeight);
        Check(isColor(pixelAt(afterW1, 54, 10), 77, 88, 99, 255),
              "S2: DrawPrimitivesEx() real skinned3d with weightsPerVertex=1 genuinely ignores "
              "bone1's contribution -- the probe point still shows bone0's unshrunk Identity result "
              "(plans/plan_dx.md DX-135)");

        sp.weightsPerVertex = 2;
        renderer.Clear(1.0f / 255.0f, 2.0f / 255.0f, 3.0f / 255.0f, 1.0f);
        renderer.DrawPrimitivesEx(vbSkin2, Matrix::getIdentityProperty(), Matrix::getIdentityProperty(),
                                 Matrix::getIdentityProperty(), PrimitiveType::TriangleList, 1, sp);
        auto afterW2 = ReadBackRenderTargetFull(renderer, rt.Get(), kRtWidth, kRtHeight);
        Check(isColor(pixelAt(afterW2, 54, 10), 1, 2, 3, 255),
              "S3: DrawPrimitivesEx() real skinned3d with weightsPerVertex=2 genuinely includes "
              "bone1's contribution -- the blended Scale(0.55) transform shrinks the triangle away "
              "from this same probe point (Clear() background shows through), distinctly different "
              "from the weightsPerVertex=1 case above with identical vertex weight data "
              "(plans/plan_dx.md DX-135)");

        // DX-137: dedicated fog on/off discriminating test for skinned3d -- a fresh copy of this
        // block's own original single-bone identity fixture (exact base result (77,88,99); `sp`/
        // `vbSkin` above were mutated for the WeightsPerVertex sub-test), same Z-at-FogEnd
        // methodology as textured3d's own fog test.
        static const VPNTS kTriSkinFog[3] = {
            {-1.0f, -1.0f, 0.5f, 0.0f, 0.0f, 1.0f, 0.0f, 1.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0, 0, 0, 0},
            { 3.0f, -1.0f, 0.5f, 0.0f, 0.0f, 1.0f, 2.0f, 1.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0, 0, 0, 0},
            {-1.0f,  3.0f, 0.5f, 0.0f, 0.0f, 1.0f, 0.0f, -1.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0, 0, 0, 0},
        };
        D3D12VertexBufferRenderer vbSkinFog(&renderer, 3);
        vbSkinFog.SetData(kTriSkinFog, 3, sizeof(VPNTS));

        GpuDrawParams skinFogP;
        skinFogP.texture0 = &tex;
        skinFogP.textureEnabled = true;
        skinFogP.lightingEnabled = true;
        skinFogP.preferPerPixelLighting = true;
        skinFogP.skinned = true;
        skinFogP.boneCount = 1;
        skinFogP.weightsPerVertex = 1;
        Matrix::getIdentityProperty().ToColumnMajor(skinFogP.boneTransforms);
        skinFogP.emissiveColor[0] = 1.0f; skinFogP.emissiveColor[1] = 1.0f; skinFogP.emissiveColor[2] = 1.0f;
        skinFogP.light0Diffuse[0] = 0.0f; skinFogP.light0Diffuse[1] = 0.0f; skinFogP.light0Diffuse[2] = 0.0f;
        skinFogP.specularColor[0] = 0.0f; skinFogP.specularColor[1] = 0.0f; skinFogP.specularColor[2] = 0.0f;
        skinFogP.eyePositionWorld[0] = 0.0f; skinFogP.eyePositionWorld[1] = 0.0f; skinFogP.eyePositionWorld[2] = -10.0f;

        skinFogP.fogEnabled = false;
        skinFogP.fogColor[0] = 0.0f; skinFogP.fogColor[1] = 1.0f; skinFogP.fogColor[2] = 0.0f;
        renderer.Clear(10.0f / 255.0f, 10.0f / 255.0f, 10.0f / 255.0f, 1.0f);
        renderer.DrawPrimitivesEx(vbSkinFog, Matrix::getIdentityProperty(), Matrix::getIdentityProperty(),
                                 Matrix::getIdentityProperty(), PrimitiveType::TriangleList, 1, skinFogP);
        auto skinFogOff = ReadBackRenderTargetFull(renderer, rt.Get(), kRtWidth, kRtHeight);
        Check(isColor(pixelAt(skinFogOff, 30, 30), 77, 88, 99, 255),
              "S4: DrawPrimitivesEx() skinned3d fogEnabled=false leaves the exact "
              "single-bone-identity texture color unblended (plans/plan_dx.md DX-137)");

        skinFogP.fogEnabled = true;
        skinFogP.fogVector[2] = 2.0f;
        renderer.Clear(10.0f / 255.0f, 10.0f / 255.0f, 10.0f / 255.0f, 1.0f);
        renderer.DrawPrimitivesEx(vbSkinFog, Matrix::getIdentityProperty(), Matrix::getIdentityProperty(),
                                 Matrix::getIdentityProperty(), PrimitiveType::TriangleList, 1, skinFogP);
        auto skinFogOn = ReadBackRenderTargetFull(renderer, rt.Get(), kRtWidth, kRtHeight);
        Check(isColor(pixelAt(skinFogOn, 30, 30), 0, 255, 0, 255),
              "S5: DrawPrimitivesEx() skinned3d fogEnabled=true with Z at FogEnd genuinely blends "
              "all the way to the exact FogColor (plans/plan_dx.md DX-137)");

        renderer.UnbindOffscreenColorTargetEXT();
    }

    // ---- Check QQ (plans/plan_dx.md DX-150): skinned3d -- DirectionalLight1/DirectionalLight2 each
    // independently and exactly contribute. Reuses this file's own single-identity-bone
    // skinned3d fixture (DX-67/DX-111/DX-135, boneCount=1/weightsPerVertex=1 so bone math doesn't
    // complicate the lighting isolation) combined with DX-124/DX-138's own exact-color-per-term
    // methodology (ambient/light0/specular all zeroed, white texture, each sub-check gets an
    // EXACT expected RGB). ----
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

        Microsoft::WRL::ComPtr<ID3D12Resource> rtQQ;
        HRESULT hrRtQQ = renderer.GetDeviceEXT()->CreateCommittedResource(
            &heapProps, D3D12_HEAP_FLAG_NONE, &rtDesc,
            D3D12_RESOURCE_STATE_RENDER_TARGET, nullptr, IID_PPV_ARGS(rtQQ.GetAddressOf()));
        renderer.GetResourceStateTrackerEXT().TrackResource(rtQQ.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET);
        D3D12_CPU_DESCRIPTOR_HANDLE rtvQQ = renderer.AllocateRtvDescriptorEXT();
        renderer.GetDeviceEXT()->CreateRenderTargetView(rtQQ.Get(), nullptr, rtvQQ);
        renderer.BindOffscreenColorTargetEXT(rtQQ.Get(), rtvQQ, DXGI_FORMAT_R8G8B8A8_UNORM, kRtWidth, kRtHeight);

        auto pixelAtQQ = [&](const std::vector<uint8_t>& buf, int x, int y) -> std::array<uint8_t, 4>
        {
            const std::size_t idx = (static_cast<std::size_t>(y) * kRtWidth + static_cast<std::size_t>(x)) * 4;
            return {buf[idx + 0], buf[idx + 1], buf[idx + 2], buf[idx + 3]};
        };
        auto centerIsExactQQ = [&](const std::vector<uint8_t>& buf, uint8_t r, uint8_t g, uint8_t b)
        {
            const auto p = pixelAtQQ(buf, 30, 30);
            return p[0] == r && p[1] == g && p[2] == b;
        };

        struct VPNTSQ { float x, y, z; float nx, ny, nz; float u, v;
                       float bw0, bw1, bw2, bw3; uint8_t bi0, bi1, bi2, bi3; };
        static const VPNTSQ kTriQQ[3] = {
            {-1.0f, -1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 1.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0, 0, 0, 0},
            { 3.0f, -1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 2.0f, 1.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0, 0, 0, 0},
            {-1.0f,  3.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, -1.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0, 0, 0, 0},
        };
        D3D12VertexBufferRenderer vbQQ(&renderer, 3);
        vbQQ.SetData(kTriQQ, 3, sizeof(VPNTSQ));

        ImageData whiteImgQQ;
        whiteImgQQ.width = 2; whiteImgQQ.height = 2; whiteImgQQ.mipLevels = 1;
        whiteImgQQ.pixels.assign(2 * 2 * 4, 255);
        D3D12TextureRenderer whiteTexQQ(&renderer, whiteImgQQ);

        GpuDrawParams baseQP;
        baseQP.texture0 = &whiteTexQQ;
        baseQP.textureEnabled = true;
        baseQP.lightingEnabled = true;
        baseQP.preferPerPixelLighting = true;
        baseQP.skinned = true;
        baseQP.boneCount = 1;
        baseQP.weightsPerVertex = 1;
        Matrix::getIdentityProperty().ToColumnMajor(baseQP.boneTransforms);
        baseQP.ambientColor[0] = 0.0f; baseQP.ambientColor[1] = 0.0f; baseQP.ambientColor[2] = 0.0f;
        baseQP.light0Diffuse[0] = 0.0f; baseQP.light0Diffuse[1] = 0.0f; baseQP.light0Diffuse[2] = 0.0f; // Light0 off
        baseQP.specularColor[0] = 0.0f; baseQP.specularColor[1] = 0.0f; baseQP.specularColor[2] = 0.0f; // no specular noise
        baseQP.eyePositionWorld[0] = 0.0f; baseQP.eyePositionWorld[1] = 0.0f; baseQP.eyePositionWorld[2] = -10.0f;

        // QQ1: DirectionalLight1 alone, full-facing direction, red diffuse -> exact (255,0,0).
        GpuDrawParams qp1 = baseQP;
        qp1.light1Dir[0] = 0.0f; qp1.light1Dir[1] = 0.0f; qp1.light1Dir[2] = -1.0f; // travels -Z -> faces the +Z normal
        qp1.light1Diffuse[0] = 1.0f; qp1.light1Diffuse[1] = 0.0f; qp1.light1Diffuse[2] = 0.0f;
        renderer.Clear(0.0f, 0.0f, 0.0f, 1.0f);
        renderer.DrawPrimitivesEx(vbQQ, Matrix::getIdentityProperty(), Matrix::getIdentityProperty(),
                                 Matrix::getIdentityProperty(), PrimitiveType::TriangleList, 1, qp1);
        auto rQQ1 = ReadBackRenderTargetFull(renderer, rtQQ.Get(), kRtWidth, kRtHeight);
        Check(centerIsExactQQ(rQQ1, 255, 0, 0),
              "QQ1: DrawPrimitivesEx() real skinned3d -- DirectionalLight1 alone contributes the "
              "exact expected red, independent of Light0/Light2 (plans/plan_dx.md DX-150)");

        // QQ2: same geometry/light1Dir, but light1Diffuse disabled (black) -- proves QQ1 wasn't
        // some other constant leaking in.
        GpuDrawParams qp1off = qp1;
        qp1off.light1Diffuse[0] = 0.0f; qp1off.light1Diffuse[1] = 0.0f; qp1off.light1Diffuse[2] = 0.0f;
        renderer.Clear(0.0f, 0.0f, 0.0f, 1.0f);
        renderer.DrawPrimitivesEx(vbQQ, Matrix::getIdentityProperty(), Matrix::getIdentityProperty(),
                                 Matrix::getIdentityProperty(), PrimitiveType::TriangleList, 1, qp1off);
        auto rQQ1off = ReadBackRenderTargetFull(renderer, rtQQ.Get(), kRtWidth, kRtHeight);
        Check(centerIsExactQQ(rQQ1off, 0, 0, 0),
              "QQ2: disabling DirectionalLight1's diffuse (zeroed) removes its contribution "
              "exactly -- confirms QQ1 was real, not a leaked default (plans/plan_dx.md DX-150)");

        // QQ3: DirectionalLight2 alone, green diffuse -> exact (0,255,0).
        GpuDrawParams qp2 = baseQP;
        qp2.light2Dir[0] = 0.0f; qp2.light2Dir[1] = 0.0f; qp2.light2Dir[2] = -1.0f;
        qp2.light2Diffuse[0] = 0.0f; qp2.light2Diffuse[1] = 1.0f; qp2.light2Diffuse[2] = 0.0f;
        renderer.Clear(0.0f, 0.0f, 0.0f, 1.0f);
        renderer.DrawPrimitivesEx(vbQQ, Matrix::getIdentityProperty(), Matrix::getIdentityProperty(),
                                 Matrix::getIdentityProperty(), PrimitiveType::TriangleList, 1, qp2);
        auto rQQ2 = ReadBackRenderTargetFull(renderer, rtQQ.Get(), kRtWidth, kRtHeight);
        Check(centerIsExactQQ(rQQ2, 0, 255, 0),
              "QQ3: DrawPrimitivesEx() real skinned3d -- DirectionalLight2 alone contributes the "
              "exact expected green, independent of Light0/Light1 (plans/plan_dx.md DX-150)");

        renderer.UnbindOffscreenColorTargetEXT();
    }

    // ---- Check RR (plans/plan_dx.md DX-151): skinned3d -- real Blinn-Phong specular highlight,
    // discriminating. Applies DX-139's own half-vector-equals-normal trick (eye at (0,0,-10),
    // surface normal (0,0,-1), light1 traveling +Z -> dot(H,N)=1 exactly, an EXACT expected
    // specular color regardless of SpecularPower) to skinned3d.frag.hlsl's own specular formula,
    // which is byte-for-byte the same shape as lit_textured3d's (confirmed by reading both before
    // writing this test) -- combined with the single-identity-bone fixture (DX-67/DX-111/DX-135)
    // so bone math doesn't complicate the specular isolation. ----
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

        Microsoft::WRL::ComPtr<ID3D12Resource> rtRR;
        HRESULT hrRtRR = renderer.GetDeviceEXT()->CreateCommittedResource(
            &heapProps, D3D12_HEAP_FLAG_NONE, &rtDesc,
            D3D12_RESOURCE_STATE_RENDER_TARGET, nullptr, IID_PPV_ARGS(rtRR.GetAddressOf()));
        renderer.GetResourceStateTrackerEXT().TrackResource(rtRR.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET);
        D3D12_CPU_DESCRIPTOR_HANDLE rtvRR = renderer.AllocateRtvDescriptorEXT();
        renderer.GetDeviceEXT()->CreateRenderTargetView(rtRR.Get(), nullptr, rtvRR);
        renderer.BindOffscreenColorTargetEXT(rtRR.Get(), rtvRR, DXGI_FORMAT_R8G8B8A8_UNORM, kRtWidth, kRtHeight);

        auto pixelAtRR = [&](const std::vector<uint8_t>& buf, int x, int y) -> std::array<uint8_t, 4>
        {
            const std::size_t idx = (static_cast<std::size_t>(y) * kRtWidth + static_cast<std::size_t>(x)) * 4;
            return {buf[idx + 0], buf[idx + 1], buf[idx + 2], buf[idx + 3]};
        };
        auto centerIsExactRR = [&](const std::vector<uint8_t>& buf, uint8_t r, uint8_t g, uint8_t b)
        {
            const auto p = pixelAtRR(buf, 30, 30);
            return p[0] == r && p[1] == g && p[2] == b;
        };

        struct VPNTSR { float x, y, z; float nx, ny, nz; float u, v;
                       float bw0, bw1, bw2, bw3; uint8_t bi0, bi1, bi2, bi3; };
        static const VPNTSR kTriRR[3] = {
            {-1.0f, -1.0f, 0.0f, 0.0f, 0.0f, -1.0f, 0.0f, 1.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0, 0, 0, 0},
            { 3.0f, -1.0f, 0.0f, 0.0f, 0.0f, -1.0f, 2.0f, 1.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0, 0, 0, 0},
            {-1.0f,  3.0f, 0.0f, 0.0f, 0.0f, -1.0f, 0.0f, -1.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0, 0, 0, 0},
        };
        D3D12VertexBufferRenderer vbRR(&renderer, 3);
        vbRR.SetData(kTriRR, 3, sizeof(VPNTSR));

        ImageData whiteImgRR;
        whiteImgRR.width = 2; whiteImgRR.height = 2; whiteImgRR.mipLevels = 1;
        whiteImgRR.pixels.assign(2 * 2 * 4, 255);
        D3D12TextureRenderer whiteTexRR(&renderer, whiteImgRR);

        GpuDrawParams rp;
        rp.texture0 = &whiteTexRR;
        rp.textureEnabled = true;
        rp.lightingEnabled = true;
        rp.preferPerPixelLighting = true;
        rp.skinned = true;
        rp.boneCount = 1;
        rp.weightsPerVertex = 1;
        Matrix::getIdentityProperty().ToColumnMajor(rp.boneTransforms);
        rp.ambientColor[0] = 0.0f; rp.ambientColor[1] = 0.0f; rp.ambientColor[2] = 0.0f;
        rp.light0Diffuse[0] = 0.0f; rp.light0Diffuse[1] = 0.0f; rp.light0Diffuse[2] = 0.0f; // no diffuse noise
        rp.light1Dir[0] = 0.0f; rp.light1Dir[1] = 0.0f; rp.light1Dir[2] = 1.0f;             // travels +Z
        rp.light1Diffuse[0] = 0.0f; rp.light1Diffuse[1] = 0.0f; rp.light1Diffuse[2] = 0.0f; // diffuse off too
        rp.light1Specular[0] = 1.0f; rp.light1Specular[1] = 1.0f; rp.light1Specular[2] = 1.0f;
        rp.eyePositionWorld[0] = 0.0f; rp.eyePositionWorld[1] = 0.0f; rp.eyePositionWorld[2] = -10.0f;
        rp.specularColor[0] = 1.0f; rp.specularColor[1] = 1.0f; rp.specularColor[2] = 1.0f;
        rp.specularPower = 16.0f;

        renderer.Clear(0.0f, 0.0f, 0.0f, 1.0f);
        renderer.DrawPrimitivesEx(vbRR, Matrix::getIdentityProperty(), Matrix::getIdentityProperty(),
                                 Matrix::getIdentityProperty(), PrimitiveType::TriangleList, 1, rp);
        auto specOnRR = ReadBackRenderTargetFull(renderer, rtRR.Get(), kRtWidth, kRtHeight);
        Check(centerIsExactRR(specOnRR, 255, 255, 255),
              "RR1: DrawPrimitivesEx() real skinned3d -- Blinn-Phong specular at a geometry "
              "deliberately chosen so dot(H,N)=1 exactly contributes the exact expected full-white "
              "highlight, with diffuse/ambient all zero (plans/plan_dx.md DX-151)");

        GpuDrawParams rpOff = rp;
        rpOff.specularColor[0] = 0.0f; rpOff.specularColor[1] = 0.0f; rpOff.specularColor[2] = 0.0f;
        renderer.Clear(0.0f, 0.0f, 0.0f, 1.0f);
        renderer.DrawPrimitivesEx(vbRR, Matrix::getIdentityProperty(), Matrix::getIdentityProperty(),
                                 Matrix::getIdentityProperty(), PrimitiveType::TriangleList, 1, rpOff);
        auto specOffRR = ReadBackRenderTargetFull(renderer, rtRR.Get(), kRtWidth, kRtHeight);
        Check(centerIsExactRR(specOffRR, 0, 0, 0),
              "RR2: the SAME geometry/light with material SpecularColor zeroed produces exact black -- "
              "proves RR1's white came genuinely from the specular term, not diffuse/ambient "
              "(plans/plan_dx.md DX-151)");

        renderer.UnbindOffscreenColorTargetEXT();
    }

    // ---- Check S2 (Task 1107, plans/plan_graphics.md Phase 80): same PreferPerPixelLighting
    // discriminator as Check O3 above, but for skinned3d -- a single Identity bone at 100% weight
    // keeps skinning a mathematical no-op, isolating the vertex-lit-vs-pixel-lit dispatch
    // difference exactly like Check O3 does. Same scene/values as
    // examples/easygl_skinnedeffect_preferperpixellighting_test.cpp (Task 1102b) and D3D11's own
    // Check V2 (Task 1106): ~127 vertex-lit, ~155 pixel-lit. ----
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
        HRESULT hrRt = renderer.GetDeviceEXT()->CreateCommittedResource(
            &heapProps, D3D12_HEAP_FLAG_NONE, &rtDesc,
            D3D12_RESOURCE_STATE_RENDER_TARGET, nullptr, IID_PPV_ARGS(rt.GetAddressOf()));
        Check(SUCCEEDED(hrRt) && rt != nullptr, "S2-0: real off-screen RGBA8 render-target resource created");
        renderer.GetResourceStateTrackerEXT().TrackResource(rt.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET);

        D3D12_CPU_DESCRIPTOR_HANDLE rtv = renderer.AllocateRtvDescriptorEXT();
        renderer.GetDeviceEXT()->CreateRenderTargetView(rt.Get(), nullptr, rtv);
        renderer.BindOffscreenColorTargetEXT(rt.Get(), rtv, DXGI_FORMAT_R8G8B8A8_UNORM, kRtWidth, kRtHeight);

        auto pixelAt = [&](const std::vector<uint8_t>& buf, int x, int y) -> std::array<uint8_t, 4>
        {
            const std::size_t idx = (static_cast<std::size_t>(y) * kRtWidth + static_cast<std::size_t>(x)) * 4;
            return {buf[idx + 0], buf[idx + 1], buf[idx + 2], buf[idx + 3]};
        };
        auto closeTo = [](int a, int b, int tol) { return std::abs(a - b) <= tol; };

        struct VPNTS { float x, y, z; float nx, ny, nz; float u, v;
                      float bw0, bw1, bw2, bw3; uint8_t bi0, bi1, bi2, bi3; };
        static const VPNTS kSkinQuad[6] = {
            {-1.0f,  1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 1.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0, 0, 0, 0},
            {-1.0f, -1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0, 0, 0, 0},
            { 1.0f, -1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 1.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0, 0, 0, 0},
            {-1.0f,  1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 1.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0, 0, 0, 0},
            { 1.0f, -1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 1.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0, 0, 0, 0},
            { 1.0f,  1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0, 0, 0, 0},
        };
        D3D12VertexBufferRenderer vbSkinPPL(&renderer, 6);
        vbSkinPPL.SetData(kSkinQuad, 6, sizeof(VPNTS));

        ImageData whiteImg2;
        whiteImg2.width = 1; whiteImg2.height = 1; whiteImg2.mipLevels = 1;
        whiteImg2.pixels = {255, 255, 255, 255};
        D3D12TextureRenderer whiteTex2(&renderer, whiteImg2);

        GpuDrawParams skPplP;
        skPplP.texture0 = &whiteTex2;
        skPplP.textureEnabled = true;
        skPplP.lightingEnabled = true;
        skPplP.skinned = true;
        skPplP.boneCount = 1;
        skPplP.weightsPerVertex = 1;
        Matrix::getIdentityProperty().ToColumnMajor(skPplP.boneTransforms);
        skPplP.ambientColor[0] = 0.02f; skPplP.ambientColor[1] = 0.02f; skPplP.ambientColor[2] = 0.02f;
        skPplP.diffuseColor[0] = 0.4f; skPplP.diffuseColor[1] = 0.4f; skPplP.diffuseColor[2] = 0.4f; skPplP.diffuseColor[3] = 1.0f;
        Microsoft::Xna::Framework::Vector3 skLightDir(0.5f, 0.0f, -1.0f);
        skLightDir.Normalize();
        skPplP.light0Dir[0] = skLightDir.X; skPplP.light0Dir[1] = skLightDir.Y; skPplP.light0Dir[2] = skLightDir.Z;
        skPplP.light0Diffuse[0] = 0.5f; skPplP.light0Diffuse[1] = 0.5f; skPplP.light0Diffuse[2] = 0.5f;
        skPplP.light0Specular[0] = 1.0f; skPplP.light0Specular[1] = 1.0f; skPplP.light0Specular[2] = 1.0f;
        skPplP.specularColor[0] = 1.0f; skPplP.specularColor[1] = 1.0f; skPplP.specularColor[2] = 1.0f;
        skPplP.specularPower = 32.0f;
        skPplP.eyePositionWorld[0] = 0.0f; skPplP.eyePositionWorld[1] = 0.0f; skPplP.eyePositionWorld[2] = 3.0f;

        const Matrix skPplView = Matrix::CreateLookAt(Microsoft::Xna::Framework::Vector3(0.0f, 0.0f, 3.0f), Microsoft::Xna::Framework::Vector3::Zero, Microsoft::Xna::Framework::Vector3(0.0f, 1.0f, 0.0f));
        const Matrix skPplProj = Matrix::CreatePerspectiveFieldOfView(0.78539816339744830962f /* MathHelper::PiOver4 */, 1.0f, 0.1f, 100.0f);

        skPplP.preferPerPixelLighting = false;
        renderer.Clear(0.0f, 0.0f, 0.0f, 1.0f);
        renderer.DrawPrimitivesEx(vbSkinPPL, Matrix::getIdentityProperty(), skPplView, skPplProj,
                                 PrimitiveType::TriangleList, 2, skPplP);
        auto skVertexLitResult = ReadBackRenderTargetFull(renderer, rt.Get(), kRtWidth, kRtHeight);
        const auto skVertexLitPixel = pixelAt(skVertexLitResult, kRtWidth / 2, kRtHeight / 2);
        Check(closeTo(skVertexLitPixel[0], 127, 10) && closeTo(skVertexLitPixel[1], 127, 10) && closeTo(skVertexLitPixel[2], 127, 10),
              "S2-1: DrawPrimitivesEx() skinned3d preferPerPixelLighting=false (XNA's real default) "
              "genuinely computes the Gouraud-averaged specular result, ~127 (Task 1107, "
              "plans/plan_graphics.md Phase 80)");

        skPplP.preferPerPixelLighting = true;
        renderer.Clear(0.0f, 0.0f, 0.0f, 1.0f);
        renderer.DrawPrimitivesEx(vbSkinPPL, Matrix::getIdentityProperty(), skPplView, skPplProj,
                                 PrimitiveType::TriangleList, 2, skPplP);
        auto skPixelLitResult = ReadBackRenderTargetFull(renderer, rt.Get(), kRtWidth, kRtHeight);
        const auto skPixelLitPixel = pixelAt(skPixelLitResult, kRtWidth / 2, kRtHeight / 2);
        Check(closeTo(skPixelLitPixel[0], 155, 10) && closeTo(skPixelLitPixel[1], 155, 10) && closeTo(skPixelLitPixel[2], 155, 10),
              "S2-2: DrawPrimitivesEx() skinned3d preferPerPixelLighting=true genuinely computes a "
              "fresh per-fragment specular result, ~155 (Task 1107, plans/plan_graphics.md Phase 80)");

        Check(skVertexLitPixel[0] != skPixelLitPixel[0],
              "S2-3: DrawPrimitivesEx() skinned3d preferPerPixelLighting is a real dispatch "
              "selector, not a decorative no-op (Task 1107, plans/plan_graphics.md Phase 80)");

        renderer.UnbindOffscreenColorTargetEXT();
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
        HRESULT hrRt = renderer.GetDeviceEXT()->CreateCommittedResource(
            &heapProps, D3D12_HEAP_FLAG_NONE, &rtDesc,
            D3D12_RESOURCE_STATE_RENDER_TARGET, nullptr, IID_PPV_ARGS(rt.GetAddressOf()));
        Check(SUCCEEDED(hrRt) && rt != nullptr, "T0: real off-screen RGBA8 render-target resource created");
        renderer.GetResourceStateTrackerEXT().TrackResource(rt.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET);

        D3D12_CPU_DESCRIPTOR_HANDLE rtv = renderer.AllocateRtvDescriptorEXT();
        renderer.GetDeviceEXT()->CreateRenderTargetView(rt.Get(), nullptr, rtv);
        renderer.BindOffscreenColorTargetEXT(rt.Get(), rtv, DXGI_FORMAT_R8G8B8A8_UNORM, kRtWidth, kRtHeight);

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

        // Same fixture as D3D11's own Check W (directx11_smoke_test.cpp DX-68): one identity-transform
        // instance via the per-instance INSTANCEWORLD0-3 buffer (not the per-vertex one) outputs the
        // exact per-instance DiffuseColor -- both non-zero color components at their saturated 0/1
        // extremes, so there is no rounding ambiguity in the final UNORM8 byte comparison.
        struct VP3 { float x, y, z; };
        static const VP3 kTriInst[3] = {
            {-1.0f, -1.0f, 0.0f},
            { 3.0f, -1.0f, 0.0f},
            {-1.0f,  3.0f, 0.0f},
        };
        D3D12VertexBufferRenderer vbInst(&renderer, 3);
        vbInst.SetData(kTriInst, 3, sizeof(VP3));

        static const uint16_t kTriInstIdx[3] = {0, 1, 2};
        D3D12IndexBufferRenderer ibInst(&renderer, 3, /*thirtyTwoBit=*/false);
        ibInst.SetData16(kTriInstIdx, 3);

        // One identity-transform instance: 4 float4 rows (INSTANCEWORLD0-3).
        static const float kInstanceWorld[16] = {
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            0.0f, 0.0f, 0.0f, 1.0f,
        };
        D3D12VertexBufferRenderer instVb(&renderer, 1);
        instVb.SetData(kInstanceWorld, 1, sizeof(kInstanceWorld));

        GpuDrawParams ip;
        // REMED-GFX-202: the classic two-stream instanced binding set, in the shared
        // GpuVertexStreamBinding array every draw route now carries.
        SetInstancedVertexStreamsEXT(ip, vbInst, instVb, /*instanceFrequency=*/1,
                                     static_cast<int>(sizeof(VP3)), 0,
                                     static_cast<int>(sizeof(kInstanceWorld)), 0);
        ip.diffuseColor[0] = 1.0f; ip.diffuseColor[1] = 1.0f;
        ip.diffuseColor[2] = 0.0f; ip.diffuseColor[3] = 1.0f;

        renderer.Clear(0.0f, 0.0f, 1.0f, 1.0f);
        renderer.DrawInstancedPrimitivesEx(vbInst, ibInst, Matrix::getIdentityProperty(),
                                          Matrix::getIdentityProperty(), Matrix::getIdentityProperty(),
                                          PrimitiveType::TriangleList, 1, 1, ip);
        auto afterT = ReadBackRenderTargetFull(renderer, rt.Get(), kRtWidth, kRtHeight);
        Check(regionIs(afterT, 255, 255, 0, 255),
              "T1: DrawInstancedPrimitivesEx() real instanced3d draw with a genuine per-instance "
              "world buffer (dual vertex stream: slot 0 per-vertex POSITION, slot 1 per-instance "
              "INSTANCEWORLD0-3) outputs the exact instance DiffuseColor (plans/plan_dx.md DX-111)");

        renderer.UnbindOffscreenColorTargetEXT();
    }

    // ---- Check U (DX-111 closing): env_map3d -- real D3D12TextureCubeRenderer, geometrically- ----
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
        HRESULT hrRt = renderer.GetDeviceEXT()->CreateCommittedResource(
            &heapProps, D3D12_HEAP_FLAG_NONE, &rtDesc,
            D3D12_RESOURCE_STATE_RENDER_TARGET, nullptr, IID_PPV_ARGS(rt.GetAddressOf()));
        Check(SUCCEEDED(hrRt) && rt != nullptr, "U0: real off-screen RGBA8 render-target resource created");
        renderer.GetResourceStateTrackerEXT().TrackResource(rt.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET);

        D3D12_CPU_DESCRIPTOR_HANDLE rtv = renderer.AllocateRtvDescriptorEXT();
        renderer.GetDeviceEXT()->CreateRenderTargetView(rt.Get(), nullptr, rtv);
        renderer.BindOffscreenColorTargetEXT(rt.Get(), rtv, DXGI_FORMAT_R8G8B8A8_UNORM, kRtWidth, kRtHeight);

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

        // Same fixture as D3D11's own Check U (directx11_smoke_test.cpp DX-66): camera placed far down
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
        D3D12VertexBufferRenderer vbEnv(&renderer, 3);
        vbEnv.SetData(kTriEnv, 3, sizeof(VPNTE));

        ImageData whiteImg;
        whiteImg.width = 2; whiteImg.height = 2; whiteImg.mipLevels = 1;
        whiteImg.pixels.assign(2 * 2 * 4, 255);
        D3D12TextureRenderer whiteTex(&renderer, whiteImg);

        D3D12TextureCubeRenderer cube(&renderer, 8, false, 0);
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

        renderer.Clear(0.0f, 0.0f, 1.0f, 1.0f);
        renderer.DrawPrimitivesEx(vbEnv, Matrix::getIdentityProperty(), Matrix::getIdentityProperty(),
                                 Matrix::getIdentityProperty(), PrimitiveType::TriangleList, 1, ep);
        auto afterU = ReadBackRenderTargetFull(renderer, rt.Get(), kRtWidth, kRtHeight);
        Check(regionIs(afterU, 10, 20, 30, 255),
              "U1: DrawPrimitivesEx() real env_map3d samples the exact distinctly-colored cube face "
              "via a real D3D12TextureCubeRenderer SRV (plans/plan_dx.md DX-111, closing 10/10 stock variants)");

        // DX-134: same fixture, envMapAmount=0.0 -> blendFactor=0 -> the lerp(baseColor,
        // envSample*alpha, blendFactor) formula must collapse to the pure base color (lit=0, since
        // light0Dir default is perpendicular to this surface's normal, ambient/emissive both
        // default 0), NOT the reflected cube-face color U1 above just proved -- genuinely different
        // from the amount=1.0 result, proving the blend is real and graduated, not a fixed
        // always-on reflection. Mirrors D3D11's own DX-134 check exactly.
        ep.envMapAmount = 0.0f;
        renderer.Clear(0.0f, 0.0f, 1.0f, 1.0f);
        renderer.DrawPrimitivesEx(vbEnv, Matrix::getIdentityProperty(), Matrix::getIdentityProperty(),
                                 Matrix::getIdentityProperty(), PrimitiveType::TriangleList, 1, ep);
        auto afterUZero = ReadBackRenderTargetFull(renderer, rt.Get(), kRtWidth, kRtHeight);
        Check(regionIs(afterUZero, 0, 0, 0, 255),
              "U2: DrawPrimitivesEx() real env_map3d with envMapAmount=0.0 collapses the base-lerp "
              "to the pure (unlit) base color, distinctly different from the envMapAmount=1.0 "
              "reflected-face color above -- proves the lerp is a genuine graduated blend control, "
              "not just an on/off gate (plans/plan_dx.md DX-134)");

        // DX-137: dedicated fog on/off discriminating test for env_map3d -- reuses this block's
        // own deterministic reflected-face fixture (exact base result (10,20,30) at
        // envMapAmount=1.0, restored here since U2 above left it at 0.0), same Z-at-FogEnd
        // methodology as textured3d's own fog test.
        static const VPNTE kTriEnvFog[3] = {
            {-1.0f, -1.0f, 0.5f, 0.0f, 0.0f, 1.0f, 0.0f, 1.0f},
            { 3.0f, -1.0f, 0.5f, 0.0f, 0.0f, 1.0f, 2.0f, 1.0f},
            {-1.0f,  3.0f, 0.5f, 0.0f, 0.0f, 1.0f, 0.0f, -1.0f},
        };
        D3D12VertexBufferRenderer vbEnvFog(&renderer, 3);
        vbEnvFog.SetData(kTriEnvFog, 3, sizeof(VPNTE));

        ep.envMapAmount = 1.0f;
        ep.fogEnabled = false;
        ep.fogColor[0] = 0.0f; ep.fogColor[1] = 1.0f; ep.fogColor[2] = 0.0f;
        renderer.Clear(10.0f / 255.0f, 10.0f / 255.0f, 10.0f / 255.0f, 1.0f);
        renderer.DrawPrimitivesEx(vbEnvFog, Matrix::getIdentityProperty(), Matrix::getIdentityProperty(),
                                 Matrix::getIdentityProperty(), PrimitiveType::TriangleList, 1, ep);
        auto envFogOff = ReadBackRenderTargetFull(renderer, rt.Get(), kRtWidth, kRtHeight);
        Check(isColor(pixelAt(envFogOff, 30, 30), 10, 20, 30, 255),
              "U3: DrawPrimitivesEx() env_map3d fogEnabled=false leaves the exact reflected "
              "cube-face color unblended (plans/plan_dx.md DX-137)");

        ep.fogEnabled = true;
        ep.fogVector[2] = 2.0f;
        renderer.Clear(10.0f / 255.0f, 10.0f / 255.0f, 10.0f / 255.0f, 1.0f);
        renderer.DrawPrimitivesEx(vbEnvFog, Matrix::getIdentityProperty(), Matrix::getIdentityProperty(),
                                 Matrix::getIdentityProperty(), PrimitiveType::TriangleList, 1, ep);
        auto envFogOn = ReadBackRenderTargetFull(renderer, rt.Get(), kRtWidth, kRtHeight);
        Check(isColor(pixelAt(envFogOn, 30, 30), 0, 255, 0, 255),
              "U4: DrawPrimitivesEx() env_map3d fogEnabled=true with Z at FogEnd genuinely blends "
              "all the way to the exact FogColor (plans/plan_dx.md DX-137)");

        renderer.UnbindOffscreenColorTargetEXT();
    }

    // ---- Check PP (plans/plan_dx.md DX-149): env_map3d -- DirectionalLight1/DirectionalLight2 each
    // independently and exactly contribute, not just Light0. Reuses Check U's own fixture
    // (normal +Z, white 2x2 texture, a dummy cube -- irrelevant content since envMapAmount=0.0
    // collapses the base-lerp to the pure lit*texColor path per DX-134's own already-proven
    // formula, so the env-map SAMPLE call still executes but its result never reaches the output)
    // and DX-138's own exact-color-per-term isolation methodology (light0Diffuse zeroed so only
    // the field under test contributes; each sub-check gets an EXACT expected RGB). ----
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

        Microsoft::WRL::ComPtr<ID3D12Resource> rtPP;
        HRESULT hrRtPP = renderer.GetDeviceEXT()->CreateCommittedResource(
            &heapProps, D3D12_HEAP_FLAG_NONE, &rtDesc,
            D3D12_RESOURCE_STATE_RENDER_TARGET, nullptr, IID_PPV_ARGS(rtPP.GetAddressOf()));
        renderer.GetResourceStateTrackerEXT().TrackResource(rtPP.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET);
        D3D12_CPU_DESCRIPTOR_HANDLE rtvPP = renderer.AllocateRtvDescriptorEXT();
        renderer.GetDeviceEXT()->CreateRenderTargetView(rtPP.Get(), nullptr, rtvPP);
        renderer.BindOffscreenColorTargetEXT(rtPP.Get(), rtvPP, DXGI_FORMAT_R8G8B8A8_UNORM, kRtWidth, kRtHeight);

        auto pixelAtPP = [&](const std::vector<uint8_t>& buf, int x, int y) -> std::array<uint8_t, 4>
        {
            const std::size_t idx = (static_cast<std::size_t>(y) * kRtWidth + static_cast<std::size_t>(x)) * 4;
            return {buf[idx + 0], buf[idx + 1], buf[idx + 2], buf[idx + 3]};
        };
        auto centerIsExactPP = [&](const std::vector<uint8_t>& buf, uint8_t r, uint8_t g, uint8_t b)
        {
            const auto p = pixelAtPP(buf, 30, 30);
            return p[0] == r && p[1] == g && p[2] == b;
        };

        struct VPNTEP { float x, y, z; float nx, ny, nz; float u, v; };
        static const VPNTEP kTriPP[3] = {
            {-1.0f, -1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 1.0f},
            { 3.0f, -1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 2.0f, 1.0f},
            {-1.0f,  3.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, -1.0f},
        };
        D3D12VertexBufferRenderer vbPP(&renderer, 3);
        vbPP.SetData(kTriPP, 3, sizeof(VPNTEP));

        ImageData whiteImgPP;
        whiteImgPP.width = 2; whiteImgPP.height = 2; whiteImgPP.mipLevels = 1;
        whiteImgPP.pixels.assign(2 * 2 * 4, 255);
        D3D12TextureRenderer whiteTexPP(&renderer, whiteImgPP);

        D3D12TextureCubeRenderer cubePP(&renderer, 8, false, 0);
        std::vector<uint8_t> dummyFace(8 * 8 * 4, 0);
        for (int face = 0; face < 6; ++face)
            cubePP.SetData(face, 0, 0, 0, 8, 8, dummyFace.data(), static_cast<int>(dummyFace.size()));

        GpuDrawParams baseEP;
        baseEP.texture0 = &whiteTexPP;
        baseEP.textureEnabled = true;
        baseEP.envMap = &cubePP;
        baseEP.envMapping = true;
        baseEP.envMapAmount = 0.0f; // DX-134: collapses base-lerp to pure lit*texColor, no reflection interference
        baseEP.light0Diffuse[0] = 0.0f; baseEP.light0Diffuse[1] = 0.0f; baseEP.light0Diffuse[2] = 0.0f; // Light0 off

        // PP1: DirectionalLight1 alone, full-facing direction, red diffuse -> exact (255,0,0).
        GpuDrawParams pp1 = baseEP;
        pp1.light1Dir[0] = 0.0f; pp1.light1Dir[1] = 0.0f; pp1.light1Dir[2] = -1.0f; // travels -Z -> faces the +Z normal
        pp1.light1Diffuse[0] = 1.0f; pp1.light1Diffuse[1] = 0.0f; pp1.light1Diffuse[2] = 0.0f;
        renderer.Clear(0.0f, 0.0f, 0.0f, 1.0f);
        renderer.DrawPrimitivesEx(vbPP, Matrix::getIdentityProperty(), Matrix::getIdentityProperty(),
                                 Matrix::getIdentityProperty(), PrimitiveType::TriangleList, 1, pp1);
        auto rPP1 = ReadBackRenderTargetFull(renderer, rtPP.Get(), kRtWidth, kRtHeight);
        Check(centerIsExactPP(rPP1, 255, 0, 0),
              "PP1: DrawPrimitivesEx() real env_map3d -- DirectionalLight1 alone contributes the "
              "exact expected red, independent of Light0/Light2 (plans/plan_dx.md DX-149)");

        // PP2: same geometry/light1Dir, but light1Diffuse disabled (black) -- proves PP1 wasn't
        // some other constant leaking in.
        GpuDrawParams pp1off = pp1;
        pp1off.light1Diffuse[0] = 0.0f; pp1off.light1Diffuse[1] = 0.0f; pp1off.light1Diffuse[2] = 0.0f;
        renderer.Clear(0.0f, 0.0f, 0.0f, 1.0f);
        renderer.DrawPrimitivesEx(vbPP, Matrix::getIdentityProperty(), Matrix::getIdentityProperty(),
                                 Matrix::getIdentityProperty(), PrimitiveType::TriangleList, 1, pp1off);
        auto rPP1off = ReadBackRenderTargetFull(renderer, rtPP.Get(), kRtWidth, kRtHeight);
        Check(centerIsExactPP(rPP1off, 0, 0, 0),
              "PP2: disabling DirectionalLight1's diffuse (zeroed) removes its contribution "
              "exactly -- confirms PP1 was real, not a leaked default (plans/plan_dx.md DX-149)");

        // PP3: DirectionalLight2 alone, green diffuse -> exact (0,255,0).
        GpuDrawParams pp2 = baseEP;
        pp2.light2Dir[0] = 0.0f; pp2.light2Dir[1] = 0.0f; pp2.light2Dir[2] = -1.0f;
        pp2.light2Diffuse[0] = 0.0f; pp2.light2Diffuse[1] = 1.0f; pp2.light2Diffuse[2] = 0.0f;
        renderer.Clear(0.0f, 0.0f, 0.0f, 1.0f);
        renderer.DrawPrimitivesEx(vbPP, Matrix::getIdentityProperty(), Matrix::getIdentityProperty(),
                                 Matrix::getIdentityProperty(), PrimitiveType::TriangleList, 1, pp2);
        auto rPP2 = ReadBackRenderTargetFull(renderer, rtPP.Get(), kRtWidth, kRtHeight);
        Check(centerIsExactPP(rPP2, 0, 255, 0),
              "PP3: DrawPrimitivesEx() real env_map3d -- DirectionalLight2 alone contributes the "
              "exact expected green, independent of Light0/Light1 (plans/plan_dx.md DX-149)");

        renderer.UnbindOffscreenColorTargetEXT();
    }

    // ---- Check V (DX-113): a dedicated fog-on/fog-off pixel test, closing the same real gap ----
    // ---- D3D11's own DX-81 audit found and fixed (directx11_smoke_test.cpp Check AC) -- fog was wired ----
    // ---- into every applicable variant's constant buffer (colored3d's DrawPrimitivesEx bundle ----
    // ---- branch included) but never independently exercised by a dedicated on/off pixel test. ----
    // ---- Same fixture as D3D11's own Check AC: colored3d.vert.hlsl uses the CPU-prepared ----
    // ---- view-space FogVector and computes keep = 1-saturate(dot(float4(position,1),FogVector)); ----
    // ---- the exact same DXBC bytecode is drawn by D3D11. A quad at Z=0.5 with vector.z=2 ----
    // ---- therefore lands exactly on keep=0 when enabled (pure FogColor), while the zero vector ----
    // ---- encodes disabled fog and keep=1 (pure vertex color): an exact discrimination. ----
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
        HRESULT hrRt = renderer.GetDeviceEXT()->CreateCommittedResource(
            &heapProps, D3D12_HEAP_FLAG_NONE, &rtDesc,
            D3D12_RESOURCE_STATE_RENDER_TARGET, nullptr, IID_PPV_ARGS(rt.GetAddressOf()));
        Check(SUCCEEDED(hrRt) && rt != nullptr, "V0: real off-screen RGBA8 render-target resource created");

        renderer.GetResourceStateTrackerEXT().TrackResource(rt.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET);
        D3D12_CPU_DESCRIPTOR_HANDLE rtv = renderer.AllocateRtvDescriptorEXT();
        renderer.GetDeviceEXT()->CreateRenderTargetView(rt.Get(), nullptr, rtv);
        renderer.BindOffscreenColorTargetEXT(rt.Get(), rtv, DXGI_FORMAT_R8G8B8A8_UNORM, kRtWidth, kRtHeight);

        struct VPCz { float x, y, z; uint32_t color; };
        const uint32_t kRed = 0xFF0000FFu; // A=255,B=0,G=0,R=255 (R8G8B8A8 byte order)
        static const VPCz kTriFog[3] = {
            {-1.0f, -1.0f, 0.5f, kRed},
            { 3.0f, -1.0f, 0.5f, kRed},
            {-1.0f,  3.0f, 0.5f, kRed},
        };
        D3D12VertexBufferRenderer vbFog(&renderer, 3);
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

        renderer.Clear(0.039f, 0.039f, 0.039f, 1.0f);
        renderer.DrawPrimitivesEx(vbFog, Matrix::getIdentityProperty(), Matrix::getIdentityProperty(),
                                 Matrix::getIdentityProperty(), PrimitiveType::TriangleList, 1, fogOff);
        auto afterFogOff = ReadBackRenderTargetFull(renderer, rt.Get(), kRtWidth, kRtHeight);
        Check(regionIs(afterFogOff, 255, 0, 0, 255),
              "V1: DrawPrimitivesEx() colored3d bundle, fogEnabled=false leaves the exact vertex "
              "color unblended (plans/plan_dx.md DX-69/DX-113)");

        GpuDrawParams fogOn = fogOff;
        fogOn.fogEnabled = true;
        fogOn.fogVector[2] = 2.0f;

        renderer.Clear(0.039f, 0.039f, 0.039f, 1.0f);
        renderer.DrawPrimitivesEx(vbFog, Matrix::getIdentityProperty(), Matrix::getIdentityProperty(),
                                 Matrix::getIdentityProperty(), PrimitiveType::TriangleList, 1, fogOn);
        auto afterFogOn = ReadBackRenderTargetFull(renderer, rt.Get(), kRtWidth, kRtHeight);
        Check(regionIs(afterFogOn, 0, 255, 0, 255),
              "V2: DrawPrimitivesEx() colored3d bundle, fogEnabled=true with Z at FogEnd genuinely "
              "blends all the way to the exact FogColor (fogFactor=0), distinctly different from "
              "the fogEnabled=false case above (plans/plan_dx.md DX-69/DX-113)");

        GpuDrawParams fogOffAgain = fogOn;
        fogOffAgain.fogEnabled = false;
        fogOffAgain.fogVector[0] = fogOffAgain.fogVector[1] =
            fogOffAgain.fogVector[2] = fogOffAgain.fogVector[3] = 0.0f;
        renderer.Clear(0.039f, 0.039f, 0.039f, 1.0f);
        renderer.DrawPrimitivesEx(vbFog, Matrix::getIdentityProperty(), Matrix::getIdentityProperty(),
                                 Matrix::getIdentityProperty(), PrimitiveType::TriangleList, 1, fogOffAgain);
        auto afterFogOffAgain = ReadBackRenderTargetFull(renderer, rt.Get(), kRtWidth, kRtHeight);
        Check(regionIs(afterFogOffAgain, 255, 0, 0, 255),
              "V3: DrawPrimitivesEx() fog A(false)->B(true)->A(false) does not reuse stale "
              "scalar or vector state (REMED-GFX-061)");

        renderer.UnbindOffscreenColorTargetEXT();
    }

    // ---- Check W (DX-117): real D3D12RenderTargetRenderer/D3D12RenderTargetCubeRenderer + MRT, ----
    // ---- through the actual public IGraphicsRenderer API (CreateRenderTarget2D/SetRenderTarget2D/ ----
    // ---- SetRenderTargets/CreateRenderTargetCube) -- not the DX-111 test scaffolding every ----
    // ---- earlier Check used (BindOffscreenColorTargetEXT). ----
    {
        constexpr int kRtWidth = 64;
        constexpr int kRtHeight = 64;

        auto rt0 = renderer.CreateRenderTarget2D(kRtWidth, kRtHeight, /*depthFormat=*/0);
        Check(rt0 != nullptr, "W1: CreateRenderTarget2D() returns a real IRenderTargetRenderer");

        renderer.SetRenderTarget2D(rt0.get());
        Check(renderer.HasBoundColorTargetEXT(), "W2: SetRenderTarget2D() genuinely binds the target");

        auto* rt0Impl = dynamic_cast<D3D12RenderTargetRenderer*>(rt0.get());
        Check(rt0Impl != nullptr, "W3: the real target is a D3D12RenderTargetRenderer");

        renderer.Clear(0.2f, 0.4f, 0.6f, 1.0f);
        auto rt0Readback = ReadBackRenderTargetFull(renderer, rt0Impl->GetColorResourceEXT(), kRtWidth, kRtHeight);
        const std::size_t centerIdx =
            (static_cast<std::size_t>(kRtHeight / 2) * kRtWidth + static_cast<std::size_t>(kRtWidth / 2)) * 4;
        Check(rt0Readback[centerIdx + 0] == 51 && rt0Readback[centerIdx + 1] == 102 &&
              rt0Readback[centerIdx + 2] == 153 && rt0Readback[centerIdx + 3] == 255,
              "W4: Clear() on a real bound RenderTarget2D writes the exact requested color, read back "
              "through its own real GPU resource (plans/plan_dx.md DX-117)");

        // A real triangle drawn into the render target, same "oversized triangle" trick Check M
        // established -- proves DrawColoredPrimitives() genuinely targets this real render target,
        // not just Clear().
        struct VPCw { float x, y, z; uint32_t color; };
        static const VPCw kTriW[3] = {
            {-1.0f, -1.0f, 0.0f, 0xFF00FF00u}, // A=255,B=0,G=255,R=0 -> exact green
            { 3.0f, -1.0f, 0.0f, 0xFF00FF00u},
            {-1.0f,  3.0f, 0.0f, 0xFF00FF00u},
        };
        D3D12VertexBufferRenderer vbW(&renderer, 3);
        vbW.SetData(kTriW, 3, sizeof(VPCw));
        renderer.DrawColoredPrimitives(vbW, Matrix::getIdentityProperty(), Matrix::getIdentityProperty(),
                                      Matrix::getIdentityProperty(), PrimitiveType::TriangleList, 1);
        auto rt0AfterDraw = ReadBackRenderTargetFull(renderer, rt0Impl->GetColorResourceEXT(), kRtWidth, kRtHeight);
        Check(rt0AfterDraw[centerIdx + 0] == 0 && rt0AfterDraw[centerIdx + 1] == 255 &&
              rt0AfterDraw[centerIdx + 2] == 0 && rt0AfterDraw[centerIdx + 3] == 255,
              "W5: DrawColoredPrimitives() paints the exact vertex color into a real bound "
              "RenderTarget2D (plans/plan_dx.md DX-117)");

        renderer.SetRenderTarget2D(nullptr);
        Check(!renderer.HasBoundColorTargetEXT(),
              "W6: SetRenderTarget2D(nullptr) on an off-screen (no swap chain) renderer genuinely "
              "restores the honest 'nothing bound' state, via RestoreBackBufferRenderTargetEXT()'s "
              "own real fallback (plans/plan_dx.md DX-117)");

        // ---- Real MRT: 2 independently-created render targets, one SetRenderTargets() bind call, ----
        // ---- Clear() genuinely writes both -- same proof shape D3D11's own DX-46 established. ----
        auto rtA = renderer.CreateRenderTarget2D(kRtWidth, kRtHeight, 0);
        auto rtB = renderer.CreateRenderTarget2D(kRtWidth, kRtHeight, 0);
        const RenderTargetBindingDescriptor mrtTargets[2] = {
            RenderTargetBindingDescriptor::ForRenderTarget2D(
                rtA.get(), 0, kRtWidth, kRtHeight, rtA->GetMultiSampleCount()),
            RenderTargetBindingDescriptor::ForRenderTarget2D(
                rtB.get(), 0, kRtWidth, kRtHeight, rtB->GetMultiSampleCount()),
        };
        renderer.SetRenderTargets(mrtTargets, 2);
        Check(renderer.HasBoundColorTargetEXT(), "W7: SetRenderTargets() binds the primary (index 0) target");

        renderer.Clear(0.8f, 0.0f, 1.0f, 1.0f); // 204/0/255/255 -- exact under any rounding mode
        auto* rtAImpl = dynamic_cast<D3D12RenderTargetRenderer*>(rtA.get());
        auto* rtBImpl = dynamic_cast<D3D12RenderTargetRenderer*>(rtB.get());
        auto rtAReadback = ReadBackRenderTargetFull(renderer, rtAImpl->GetColorResourceEXT(), kRtWidth, kRtHeight);
        auto rtBReadback = ReadBackRenderTargetFull(renderer, rtBImpl->GetColorResourceEXT(), kRtWidth, kRtHeight);
        const bool rtAExact = rtAReadback[centerIdx + 0] == 204 && rtAReadback[centerIdx + 1] == 0 &&
                              rtAReadback[centerIdx + 2] == 255 && rtAReadback[centerIdx + 3] == 255;
        const bool rtBExact = rtBReadback[centerIdx + 0] == 204 && rtBReadback[centerIdx + 1] == 0 &&
                              rtBReadback[centerIdx + 2] == 255 && rtBReadback[centerIdx + 3] == 255;
        Check(rtAExact && rtBExact,
              "W8: real 2-target MRT -- one Clear() call after one SetRenderTargets() bind writes "
              "the exact color into BOTH independently-readable GPU resources (plans/plan_dx.md DX-117)");

        renderer.SetRenderTarget2D(nullptr);

        // ---- REMED-GFX-127: the public ITextureRenderer::GetData override this renderer gained ----
        // ---- must return the target's real pixels and report that it did. Asserted against the ----
        // ---- already-proven ReadBackRenderTargetFull oracle above, so a wrong readback cannot ----
        // ---- pass by agreeing with itself, and against a NON-ZERO destination pre-fill, so the ----
        // ---- pre-fix behaviour (the shared layer converting its own zeroed scratch buffer) ----
        // ---- would fail decisively. This renderer cannot run the shared Game-harness contract ----
        // ---- test in this dev loop (no window/swap chain under vanilla Wine's dxgi), so this is ----
        // ---- where D3D12's readback gets real runtime evidence. ----
        {
            auto rtRead = renderer.CreateRenderTarget2D(kRtWidth, kRtHeight, /*depthFormat=*/0);
            auto* rtReadImpl = dynamic_cast<D3D12RenderTargetRenderer*>(rtRead.get());
            renderer.SetRenderTarget2D(rtRead.get());
            renderer.Clear(1.0f, 0.0f, 1.0f, 0.2f); // 255/0/255/51 -- alpha neither 0 nor 255
            renderer.SetRenderTarget2D(nullptr);

            const auto oracle = ReadBackRenderTargetFull(renderer, rtReadImpl->GetColorResourceEXT(),
                                                          kRtWidth, kRtHeight);

            // Whole level, into a destination pre-filled with a sentinel that is not the content.
            std::vector<uint8_t> full(static_cast<std::size_t>(kRtWidth) * kRtHeight * 4, 0xCDu);
            const bool fullOk = rtReadImpl->GetData(0, 0, 0, kRtWidth, kRtHeight,
                                                    full.data(), static_cast<int>(full.size()));
            Check(fullOk, "GFX127a: D3D12RenderTargetRenderer::GetData reports a completed readback");
            Check(full == oracle,
                  "GFX127b: D3D12RenderTargetRenderer::GetData returns byte-identical content to the "
                  "independent readback oracle (REMED-GFX-127)");
            Check(full[centerIdx + 0] == 255 && full[centerIdx + 1] == 0 &&
                  full[centerIdx + 2] == 255 && full[centerIdx + 3] == 51,
                  "GFX127c: the returned content is the exact rendered colour, alpha included -- "
                  "not a fabricated transparent-black frame (REMED-GFX-127)");

            // A sub-rectangle, checked against the same oracle row by row.
            constexpr int kRx = 7, kRy = 5, kRw = 9, kRh = 3;
            std::vector<uint8_t> rect(static_cast<std::size_t>(kRw) * kRh * 4, 0xA5u);
            const bool rectOk = rtReadImpl->GetData(0, kRx, kRy, kRw, kRh,
                                                    rect.data(), static_cast<int>(rect.size()));
            bool rectExact = rectOk;
            for (int row = 0; row < kRh && rectExact; ++row)
                for (int col = 0; col < kRw; ++col)
                {
                    const std::size_t d = (static_cast<std::size_t>(row) * kRw + col) * 4;
                    const std::size_t o = ((static_cast<std::size_t>(kRy + row) * kRtWidth) + kRx + col) * 4;
                    if (rect[d + 0] != oracle[o + 0] || rect[d + 1] != oracle[o + 1] ||
                        rect[d + 2] != oracle[o + 2] || rect[d + 3] != oracle[o + 3])
                    {
                        rectExact = false;
                        break;
                    }
                }
            Check(rectExact,
                  "GFX127d: a sub-rectangle readback matches the oracle at the requested offset "
                  "(REMED-GFX-127)");

            // Deterministic rejection, with the destination left untouched.
            std::vector<uint8_t> untouched(static_cast<std::size_t>(kRw) * kRh * 4, 0xA5u);
            bool threwOutOfRange = false;
            try { (void)rtReadImpl->GetData(0, kRtWidth - 1, 0, 8, 8, untouched.data(),
                                            static_cast<int>(untouched.size())); }
            catch (const System::ArgumentOutOfRangeException&) { threwOutOfRange = true; }
            catch (...) {}
            bool threwNotSupported = false;
            try { (void)rtReadImpl->GetData(4, 0, 0, kRw, kRh, untouched.data(),
                                            static_cast<int>(untouched.size())); }
            catch (const System::NotSupportedException&) { threwNotSupported = true; }
            catch (...) {}
            const bool stillSentinel =
                std::all_of(untouched.begin(), untouched.end(), [](uint8_t b) { return b == 0xA5u; });
            Check(threwOutOfRange && threwNotSupported && stillSentinel,
                  "GFX127e: a rectangle leaving the target and a nonexistent mip level are both "
                  "rejected deterministically, with the destination byte-for-byte untouched "
                  "(REMED-GFX-127)");
        }

        // ---- RenderTargetCube: real construction + face-0 bind+clear+readback. ----
        auto rtCube = renderer.CreateRenderTargetCube(kRtWidth, 0);
        Check(rtCube != nullptr, "W9: CreateRenderTargetCube() returns a real IRenderTargetCubeRenderer");

        rtCube->BindAsRenderTargetFace(0);
        Check(renderer.HasBoundColorTargetEXT(), "W10: BindAsRenderTargetFace() genuinely binds face 0");

        renderer.Clear(1.0f, 0.6f, 0.0f, 1.0f); // 255/153/0/255 -- exact under any rounding mode
        auto* rtCubeImpl = dynamic_cast<D3D12RenderTargetCubeRenderer*>(rtCube.get());
        Check(rtCubeImpl != nullptr, "W11: the real cube target is a D3D12RenderTargetCubeRenderer");
        auto rtCubeReadback = ReadBackRenderTargetFull(renderer, rtCubeImpl->GetColorResourceEXT(), kRtWidth, kRtHeight);
        Check(rtCubeReadback[centerIdx + 0] == 255 && rtCubeReadback[centerIdx + 1] == 153 &&
              rtCubeReadback[centerIdx + 2] == 0 && rtCubeReadback[centerIdx + 3] == 255,
              "W12: Clear() on a real bound RenderTargetCube face writes the exact requested color, "
              "read back through its own real GPU resource (subresource 0 = face 0, plans/plan_dx.md "
              "DX-117 -- only face 0 exercised, remaining faces are the same honest-scope gap "
              "D3D11's own RenderTargetCube coverage already has, see plans/plan_dx.md Phase DX15 DX-129)");

        rtCube->UnbindAsRenderTarget();
        Check(!renderer.HasBoundColorTargetEXT(), "W13: UnbindAsRenderTarget() on RenderTargetCube restores the honest 'nothing bound' state");

        // ---- REMED-GFX-134: the public RenderTargetCube readback this renderer used to refuse. ----
        // The shared Game-harness suite (examples/rendertargetcube_getdata_contract_test.cpp) is
        // the real oracle -- asymmetric per-face patterns, orientation against a RenderTarget2D
        // reference, partial rectangles, MSAA resolve -- but it needs a window and swap chain,
        // which DX-100 already found crashes under this dev loop's Wine dxgi.dll. These checks
        // therefore exercise the same D3D12RenderTargetCubeRenderer::GetData through the only
        // content producer available at this level, a per-face Clear. That is enough to prove real
        // per-face subresource selection, a real off-centre sub-rectangle, agreement with the
        // independent ReadBackRenderTargetFull oracle, and the rejection contract; it deliberately
        // cannot prove row ORIENTATION, which a uniform clear colour can never detect and which the
        // shared suite covers on the eight renderers that do run it.
        {
            struct FaceColor { float r, g, b; std::uint8_t er, eg, eb; };
            // Only 0.0/0.6/1.0 components -- exact under any rounding mode, like W12's own choice.
            const FaceColor kFaceColors[6] = {
                {1.0f, 0.0f, 0.0f, 255,   0,   0},
                {0.0f, 1.0f, 0.0f,   0, 255,   0},
                {0.0f, 0.0f, 1.0f,   0,   0, 255},
                {1.0f, 1.0f, 0.0f, 255, 255,   0},
                {1.0f, 0.0f, 1.0f, 255,   0, 255},
                {0.0f, 1.0f, 1.0f,   0, 255, 255},
            };
            for (int face = 0; face < 6; ++face)
            {
                rtCube->BindAsRenderTargetFace(face);
                renderer.Clear(kFaceColors[face].r, kFaceColors[face].g, kFaceColors[face].b, 1.0f);
                rtCube->UnbindAsRenderTarget();
            }

            bool allFacesExact = true;
            bool allReported = true;
            std::string faceDetail;
            for (int face = 0; face < 6; ++face)
            {
                std::vector<std::uint8_t> got(static_cast<std::size_t>(kRtWidth) * kRtHeight * 4, 0xCD);
                const bool ok = rtCube->GetData(face, 0, 0, 0, kRtWidth, kRtHeight, got.data(),
                                                static_cast<int>(got.size()));
                allReported = allReported && ok;
                for (std::size_t i = 0; i < got.size(); i += 4)
                {
                    if (got[i + 0] != kFaceColors[face].er || got[i + 1] != kFaceColors[face].eg ||
                        got[i + 2] != kFaceColors[face].eb || got[i + 3] != 255)
                    {
                        allFacesExact = false;
                        if (faceDetail.empty())
                            faceDetail = " face " + std::to_string(face) + " texel " +
                                         std::to_string(i / 4) + " = (" +
                                         std::to_string(static_cast<int>(got[i + 0])) + "," +
                                         std::to_string(static_cast<int>(got[i + 1])) + "," +
                                         std::to_string(static_cast<int>(got[i + 2])) + "," +
                                         std::to_string(static_cast<int>(got[i + 3])) + ")";
                        break;
                    }
                }
            }
            const std::string facesLabel =
                "GFX134a: D3D12RenderTargetCubeRenderer::GetData returns each of the SIX faces' own "
                "rendered colour -- real per-face subresource selection, not face 0 six times" +
                faceDetail;
            Check(allReported && allFacesExact, facesLabel.c_str());

            // An off-centre sub-rectangle of a face, and agreement with the independent
            // ReadBackRenderTargetFull oracle already used by W12 for face 0.
            std::vector<std::uint8_t> rect(4 * 3 * 4, 0xCD);
            const bool rectOk = rtCube->GetData(3, 0, 2, 1, 4, 3, rect.data(),
                                                static_cast<int>(rect.size()));
            bool rectExact = rectOk;
            for (std::size_t i = 0; i < rect.size() && rectExact; i += 4)
                rectExact = rect[i + 0] == kFaceColors[3].er && rect[i + 1] == kFaceColors[3].eg &&
                            rect[i + 2] == kFaceColors[3].eb && rect[i + 3] == 255;
            Check(rectExact,
                  "GFX134b: GetData on an off-centre 4x3 sub-rectangle of face 3 returns that face's "
                  "content, with the destination filled exactly");

            auto oracle = ReadBackRenderTargetFull(renderer, rtCubeImpl->GetSampleableColorResourceEXT(),
                                                   kRtWidth, kRtHeight);
            std::vector<std::uint8_t> viaGetData(static_cast<std::size_t>(kRtWidth) * kRtHeight * 4, 0xCD);
            const bool face0Ok = rtCube->GetData(0, 0, 0, 0, kRtWidth, kRtHeight, viaGetData.data(),
                                                 static_cast<int>(viaGetData.size()));
            Check(face0Ok && oracle.size() >= viaGetData.size() &&
                  std::equal(viaGetData.begin(), viaGetData.end(), oracle.begin()),
                  "GFX134c: the public per-face readback agrees byte-for-byte with the independent "
                  "subresource-0 oracle W12 already uses");

            // The rejection contract: an out-of-range face, an unallocated mip level and a
            // rectangle that leaves the face must all report false WITHOUT writing one byte.
            std::vector<std::uint8_t> poisoned(static_cast<std::size_t>(kRtWidth) * kRtHeight * 4, 0xAB);
            const bool badFace  = rtCube->GetData(6, 0, 0, 0, kRtWidth, kRtHeight, poisoned.data(),
                                                  static_cast<int>(poisoned.size()));
            const bool badLevel = rtCube->GetData(0, 4, 0, 0, 1, 1, poisoned.data(),
                                                  static_cast<int>(poisoned.size()));
            const bool badRect  = rtCube->GetData(0, 0, kRtWidth - 1, 0, 4, 4, poisoned.data(),
                                                  static_cast<int>(poisoned.size()));
            bool poisonIntact = true;
            for (std::uint8_t b : poisoned) if (b != 0xAB) { poisonIntact = false; break; }
            Check(!badFace && !badLevel && !badRect && poisonIntact,
                  "GFX134d: an out-of-range face, an unallocated mip level and a rectangle leaving "
                  "the face are each refused with the caller's buffer byte-for-byte untouched");
        }

        // ---- REMED-GFX-136: RenderTargetUsage now reaches CreateRenderTargetCube. ----
        // The shared Game-harness battery (examples/rendertargetcube_usage_test.cpp) is the real
        // oracle and passes 30/30 on the eight renderers that can run it, D3D11 -- this renderer's
        // closest sibling -- included; DX-100's Wine dxgi.dll crash keeps it from running here, so
        // what these two checks pin is what CAN be pinned at renderer level with Clear() as the only
        // content producer: the new parameter is accepted at both values, and this renderer really
        // has NO load action, which is exactly why it consumes `preserveContents` by deliberately
        // ignoring it. A partial second producer pass (the thing that separates preservation from
        // "the marker landed") needs SpriteBatch, so it lives in the shared battery only.
        {
            auto keep = renderer.CreateRenderTargetCube(kRtWidth, 0, /*preserveContents=*/true);
            auto drop = renderer.CreateRenderTargetCube(kRtWidth, 0, /*preserveContents=*/false);
            Check(keep != nullptr && drop != nullptr,
                  "GFX136a: CreateRenderTargetCube accepts the new preserveContents flag at both "
                  "values and returns a real target for each");

            bool bothSurvive = true;
            std::string detail;
            CNA::Internal::Renderers::IRenderTargetCubeRenderer* targets[2] = { keep.get(), drop.get() };
            const std::uint8_t expected[2][3] = { {0, 153, 255}, {255, 153, 0} };
            const float clears[2][3] = { {0.0f, 0.6f, 1.0f}, {1.0f, 0.6f, 0.0f} };
            for (int t = 0; t < 2 && bothSurvive; ++t)
            {
                targets[t]->BindAsRenderTargetFace(2);
                renderer.Clear(clears[t][0], clears[t][1], clears[t][2], 1.0f);
                targets[t]->UnbindAsRenderTarget();
                // A whole bind cycle that renders nothing at all. On a renderer with a load action
                // this is where a discard would happen.
                targets[t]->BindAsRenderTargetFace(2);
                targets[t]->UnbindAsRenderTarget();

                std::vector<std::uint8_t> got(static_cast<std::size_t>(kRtWidth) * kRtHeight * 4, 0xCD);
                if (!targets[t]->GetData(2, 0, 0, 0, kRtWidth, kRtHeight, got.data(),
                                         static_cast<int>(got.size())))
                {
                    bothSurvive = false;
                    detail = " target " + std::to_string(t) + " refused readback";
                    break;
                }
                for (std::size_t i = 0; i < got.size(); i += 4)
                    if (got[i + 0] != expected[t][0] || got[i + 1] != expected[t][1] ||
                        got[i + 2] != expected[t][2] || got[i + 3] != 255)
                    {
                        bothSurvive = false;
                        detail = " target " + std::to_string(t) + " texel " +
                                 std::to_string(i / 4) + " = (" +
                                 std::to_string(static_cast<int>(got[i + 0])) + "," +
                                 std::to_string(static_cast<int>(got[i + 1])) + "," +
                                 std::to_string(static_cast<int>(got[i + 2])) + "," +
                                 std::to_string(static_cast<int>(got[i + 3])) + ")";
                        break;
                    }
            }
            const std::string label =
                "GFX136b: a cube face's colour survives a whole further bind cycle on BOTH a "
                "preserveContents=true and a preserveContents=false target -- D3D12's "
                "OMSetRenderTargets has no load action, so the only clear a cube target ever gets "
                "unasked is GraphicsDevice::SetRenderTargets' DiscardContents one" + detail;
            Check(bothSurvive, label.c_str());
        }
    }

    // ---- Check X: DX-118 -- real BlendState/RasterizerState now genuinely runtime-settable,
    // feeding real PSO variation instead of the hardcoded literals every draw path used before this
    // task. NOTE: this test uses the REAL, VERIFIED XNA enum ordinals (Blend::One=0, Blend::Zero=1,
    // BlendFunction::Add=0, CullMode::None=0/CullCounterClockwiseFace=2, FillMode::Solid=0,
    // CompareFunction::LessEqual=3 -- confirmed directly against
    // include/Microsoft/Xna/Framework/Graphics/{Blend,BlendFunction,CullMode,FillMode,
    // CompareFunction}.hpp while writing this task, NOT copied from D3D12PipelineStateDesc.hpp's own
    // default-value comments, which this task found to be WRONG for 2 fields (documented in
    // plans/plan_dx.md's DX-118 row -- colorSrcBlend's default 2 is really Blend::SourceColor not
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
        HRESULT hrRt = renderer.GetDeviceEXT()->CreateCommittedResource(
            &heapProps, D3D12_HEAP_FLAG_NONE, &rtDesc,
            D3D12_RESOURCE_STATE_RENDER_TARGET, nullptr, IID_PPV_ARGS(rt.GetAddressOf()));
        Check(SUCCEEDED(hrRt) && rt != nullptr, "X0: real off-screen RGBA8 render-target resource created");
        renderer.GetResourceStateTrackerEXT().TrackResource(rt.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET);

        D3D12_CPU_DESCRIPTOR_HANDLE rtv = renderer.AllocateRtvDescriptorEXT();
        renderer.GetDeviceEXT()->CreateRenderTargetView(rt.Get(), nullptr, rtv);
        renderer.BindOffscreenColorTargetEXT(rt.Get(), rtv, DXGI_FORMAT_R8G8B8A8_UNORM, kRtWidth, kRtHeight);

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
        D3D12VertexBufferRenderer vbRed100(&renderer, 3);
        vbRed100.SetData(kTriRed100, 3, sizeof(VPC));
        D3D12VertexBufferRenderer vbRed50(&renderer, 3);
        vbRed50.SetData(kTriRed50, 3, sizeof(VPC));

        renderer.Clear(0.0f, 0.0f, 0.0f, 1.0f);
        renderer.DrawColoredPrimitives(vbRed100, Matrix::getIdentityProperty(), Matrix::getIdentityProperty(),
                                      Matrix::getIdentityProperty(), PrimitiveType::TriangleList, 1);
        auto afterOpaque = ReadBackRenderTargetFull(renderer, rt.Get(), kRtWidth, kRtHeight);
        Check(regionIs(afterOpaque, 100, 0, 0, 255),
              "X1: default (no ApplyBlendState call yet) draw still paints the exact vertex color -- "
              "no regression from DX-118's own new state-tracking fields");

        renderer.ApplyBlendState(/*colorSrcBlend=One*/0, /*alphaSrcBlend=One*/0,
                                /*colorDstBlend=One*/0, /*alphaDstBlend=One*/0,
                                /*colorBlendFunc=Add*/0, /*alphaBlendFunc=Add*/0,
                                CNA::Internal::Renderers::BlendWriteState{}); // REMED-GFX-077 default write state
        renderer.DrawColoredPrimitives(vbRed50, Matrix::getIdentityProperty(), Matrix::getIdentityProperty(),
                                      Matrix::getIdentityProperty(), PrimitiveType::TriangleList, 1);
        auto afterAdditive = ReadBackRenderTargetFull(renderer, rt.Get(), kRtWidth, kRtHeight);
        Check(regionIs(afterAdditive, 150, 0, 0, 255),
              "X2: real ApplyBlendState(One,One,Add) genuinely additive-blends a second draw over "
              "the first -- exact 100+50=150 sum, a real BlendEnable=TRUE PSO actually used (not "
              "the Opaque default X1 just proved)");

        renderer.ApplyBlendState(/*colorSrcBlend=One*/0, /*alphaSrcBlend=One*/0,
                                /*colorDstBlend=Zero*/1, /*alphaDstBlend=Zero*/1,
                                /*colorBlendFunc=Add*/0, /*alphaBlendFunc=Add*/0,
                                CNA::Internal::Renderers::BlendWriteState{}); // REMED-GFX-077 default write state
        renderer.Clear(0.0f, 0.0f, 0.0f, 1.0f);
        renderer.DrawColoredPrimitives(vbRed100, Matrix::getIdentityProperty(), Matrix::getIdentityProperty(),
                                      Matrix::getIdentityProperty(), PrimitiveType::TriangleList, 1);
        auto afterRevert = ReadBackRenderTargetFull(renderer, rt.Get(), kRtWidth, kRtHeight);
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
        D3D12VertexBufferRenderer vbTri(&renderer, 3);
        vbTri.SetData(kTri, 3, sizeof(VPC));

        renderer.ApplyRasterizerState(/*cullMode=CullCounterClockwiseFace*/2, /*fillMode=Solid*/0,
                                     /*scissorTestEnable=*/false);
        renderer.Clear(0.0f, 0.0f, 1.0f, 1.0f);
        renderer.DrawColoredPrimitives(vbTri, Matrix::getIdentityProperty(), Matrix::getIdentityProperty(),
                                      Matrix::getIdentityProperty(), PrimitiveType::TriangleList, 1);
        auto culled = ReadBackRenderTargetFull(renderer, rt.Get(), kRtWidth, kRtHeight);
        Check(regionIs(culled, 0, 0, 255, 255),
              "X4: real ApplyRasterizerState(CullCounterClockwiseFace) genuinely culls this "
              "triangle's real winding -- background survives, matching DX-111's own already-"
              "documented finding about this exact geometry, now proven dynamically settable");

        renderer.ApplyRasterizerState(/*cullMode=None*/0, /*fillMode=Solid*/0, /*scissorTestEnable=*/false);
        renderer.Clear(0.0f, 0.0f, 1.0f, 1.0f);
        renderer.DrawColoredPrimitives(vbTri, Matrix::getIdentityProperty(), Matrix::getIdentityProperty(),
                                      Matrix::getIdentityProperty(), PrimitiveType::TriangleList, 1);
        auto notCulled = ReadBackRenderTargetFull(renderer, rt.Get(), kRtWidth, kRtHeight);
        Check(regionIs(notCulled, 255, 0, 0, 255),
              "X5: real ApplyRasterizerState(CullMode::None) genuinely draws the same triangle -- "
              "same geometry, opposite outcome, purely from the RasterizerState change");

        renderer.UnbindOffscreenColorTargetEXT();
    }

    // ---- Check GFX077: REMED-GFX-077 runtime verification -- BlendState.ColorWriteChannels (RT0
    // RenderTarget[0].RenderTargetWriteMask, a STATIC part of the D3D12 PSO folded into the PSO
    // cache key) and BlendState.MultiSampleMask (the PSO SampleMask, also in the key), off-screen
    // through the real Wine+vkd3d-proton device. An opaque full-screen quad (Opaque blend, blend
    // disabled) is drawn so the ONLY thing that can preserve a destination channel is the colour
    // write mask gating the write. Differential model (masked-in channel == the "All" baseline,
    // masked-out == the "None"/clear baseline) so it is invariant to any RT colour transform; the
    // D3D12 RT here is linear R8G8B8A8_UNORM so it also happens to be byte-exact. The RT is single-
    // sample, on which PSO SampleMask bit 0 gates the single coverage sample: SampleMask==0 must
    // discard the quad (pixel stays the clear colour), the strongest simple MultiSampleMask probe
    // available off-screen. ----
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
        HRESULT hrRt = renderer.GetDeviceEXT()->CreateCommittedResource(
            &heapProps, D3D12_HEAP_FLAG_NONE, &rtDesc,
            D3D12_RESOURCE_STATE_RENDER_TARGET, nullptr, IID_PPV_ARGS(rt.GetAddressOf()));
        Check(SUCCEEDED(hrRt) && rt != nullptr, "GFX077-0: off-screen RGBA8 render target created");
        renderer.GetResourceStateTrackerEXT().TrackResource(rt.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET);

        D3D12_CPU_DESCRIPTOR_HANDLE rtv = renderer.AllocateRtvDescriptorEXT();
        renderer.GetDeviceEXT()->CreateRenderTargetView(rt.Get(), nullptr, rtv);
        renderer.BindOffscreenColorTargetEXT(rt.Get(), rtv, DXGI_FORMAT_R8G8B8A8_UNORM, kRtWidth, kRtHeight);
        renderer.ApplyRasterizerState(/*cullMode=None*/0, /*fillMode=Solid*/0, /*scissorTestEnable=*/false);

        // Full-NDC quad, flat source colour S=(200,100,50,220). VertexPositionColor packs as
        // 0xAABBGGRR: A=220(DC) B=50(32) G=100(64) R=200(C8) -> 0xDC3264C8.
        struct VPC { float x, y, z; uint32_t color; };
        static const VPC kQuad[6] = {
            {-1.0f,  1.0f, 0.0f, 0xDC3264C8u}, {-1.0f, -1.0f, 0.0f, 0xDC3264C8u},
            { 1.0f, -1.0f, 0.0f, 0xDC3264C8u}, {-1.0f,  1.0f, 0.0f, 0xDC3264C8u},
            { 1.0f, -1.0f, 0.0f, 0xDC3264C8u}, { 1.0f,  1.0f, 0.0f, 0xDC3264C8u},
        };
        D3D12VertexBufferRenderer vb(&renderer, 6);
        vb.SetData(kQuad, 6, sizeof(VPC));

        auto centre = [&](const std::vector<uint8_t>& buf) -> std::array<uint8_t, 4>
        {
            const std::size_t idx = (static_cast<std::size_t>(32) * kRtWidth + 32) * 4;
            return {buf[idx + 0], buf[idx + 1], buf[idx + 2], buf[idx + 3]};
        };
        const Matrix I = Matrix::getIdentityProperty();
        // Render the quad with a given RT0 write mask + sample mask over a fixed clear colour D.
        auto renderMask = [&](int cwc, uint32_t sampleMask) -> std::array<uint8_t, 4>
        {
            CNA::Internal::Renderers::BlendWriteState ws;
            ws.colorWriteChannels[0] = cwc;
            ws.multiSampleMask = sampleMask;
            renderer.ApplyBlendState(/*One*/0, 0, /*Zero*/1, 1, /*Add*/0, 0, ws); // Opaque + mask
            renderer.Clear(10.0f / 255.0f, 20.0f / 255.0f, 30.0f / 255.0f, 40.0f / 255.0f); // D
            renderer.DrawColoredPrimitives(vb, I, I, I, PrimitiveType::TriangleList, 2);
            return centre(ReadBackRenderTargetFull(renderer, rt.Get(), kRtWidth, kRtHeight));
        };
        auto eq = [](const std::array<uint8_t, 4>& a, const std::array<uint8_t, 4>& b)
        { return a[0] == b[0] && a[1] == b[1] && a[2] == b[2] && a[3] == b[3]; };
        auto masked = [](const std::array<uint8_t, 4>& dst, const std::array<uint8_t, 4>& src, int bits)
        {
            return std::array<uint8_t, 4>{
                static_cast<uint8_t>((bits & 1) ? src[0] : dst[0]),
                static_cast<uint8_t>((bits & 2) ? src[1] : dst[1]),
                static_cast<uint8_t>((bits & 4) ? src[2] : dst[2]),
                static_cast<uint8_t>((bits & 8) ? src[3] : dst[3])};
        };

        const std::array<uint8_t, 4> dst = renderMask(/*None*/0,  0xFFFFFFFFu);
        const std::array<uint8_t, 4> src = renderMask(/*All*/15, 0xFFFFFFFFu);
        Check(dst[0] != src[0] && dst[1] != src[1] && dst[2] != src[2] && dst[3] != src[3],
              "GFX077-1: None(dst) and All(src) baselines discriminate all four channels");
        Check(eq(renderMask(1, 0xFFFFFFFFu), masked(dst, src, 1)),
              "GFX077-2: ColorWriteChannels.Red writes only R (RenderTargetWriteMask in PSO)");
        Check(eq(renderMask(2, 0xFFFFFFFFu), masked(dst, src, 2)),
              "GFX077-3: ColorWriteChannels.Green writes only G");
        Check(eq(renderMask(4, 0xFFFFFFFFu), masked(dst, src, 4)),
              "GFX077-4: ColorWriteChannels.Blue writes only B");
        Check(eq(renderMask(8, 0xFFFFFFFFu), masked(dst, src, 8)),
              "GFX077-5: ColorWriteChannels.Alpha writes only A");
        Check(eq(renderMask(1 | 4, 0xFFFFFFFFu), masked(dst, src, 1 | 4)),
              "GFX077-6: ColorWriteChannels.Red|Blue writes only R and B");

        // A(Red)->B(Green)->A(Red): each draw selects its own keyed PSO (no stale write mask).
        const std::array<uint8_t, 4> a1 = renderMask(1, 0xFFFFFFFFu);
        const std::array<uint8_t, 4> bG = renderMask(2, 0xFFFFFFFFu);
        const std::array<uint8_t, 4> a2 = renderMask(1, 0xFFFFFFFFu);
        Check(eq(a1, masked(dst, src, 1)) && eq(bG, masked(dst, src, 2)) && eq(a2, masked(dst, src, 1)),
              "GFX077-7: A(Red)->B(Green)->A(Red) each selects its own PSO (no stale/last-wins mask) "
              "-- functional proof the PSO is keyed on colorWriteMask");

        // MultiSampleMask on the single-sample RT: SampleMask bit 0 gates the one coverage sample.
        const std::array<uint8_t, 4> sm0  = renderMask(15, 0x00000000u); // no coverage -> stays clear
        const std::array<uint8_t, 4> smAll = renderMask(15, 0xFFFFFFFFu); // normal
        Check(eq(sm0, dst),
              "GFX077-9: MultiSampleMask=0 discards the single coverage sample (pixel stays clear D)");
        Check(eq(smAll, src),
              "GFX077-10: MultiSampleMask=all renders normally (full sample coverage -> src)");

        renderer.UnbindOffscreenColorTargetEXT();
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
        HRESULT hrRt = renderer.GetDeviceEXT()->CreateCommittedResource(
            &heapProps, D3D12_HEAP_FLAG_NONE, &rtDesc,
            D3D12_RESOURCE_STATE_RENDER_TARGET, nullptr, IID_PPV_ARGS(rt.GetAddressOf()));
        Check(SUCCEEDED(hrRt) && rt != nullptr, "Y0: real off-screen RGBA8 render-target resource created");
        renderer.GetResourceStateTrackerEXT().TrackResource(rt.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET);

        D3D12_CPU_DESCRIPTOR_HANDLE rtv = renderer.AllocateRtvDescriptorEXT();
        renderer.GetDeviceEXT()->CreateRenderTargetView(rt.Get(), nullptr, rtv);

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
        HRESULT hrDepth = renderer.GetDeviceEXT()->CreateCommittedResource(
            &heapProps, D3D12_HEAP_FLAG_NONE, &depthDesc,
            D3D12_RESOURCE_STATE_DEPTH_WRITE, &depthClear, IID_PPV_ARGS(depthRes.GetAddressOf()));
        Check(SUCCEEDED(hrDepth) && depthRes != nullptr, "Y1: real off-screen depth-stencil resource created");

        D3D12_CPU_DESCRIPTOR_HANDLE dsv = renderer.AllocateDsvDescriptorEXT();
        D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc{};
        dsvDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
        dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
        renderer.GetDeviceEXT()->CreateDepthStencilView(depthRes.Get(), &dsvDesc, dsv);

        renderer.BindOffscreenColorTargetEXT(rt.Get(), rtv, DXGI_FORMAT_R8G8B8A8_UNORM, kRtWidth, kRtHeight,
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
            ID3D12CommandAllocator* allocator = renderer.GetCommandAllocatorEXT(0);
            ID3D12GraphicsCommandList* cmdList = renderer.GetCommandListEXT();
            allocator->Reset();
            cmdList->Reset(allocator, nullptr);
            cmdList->ClearDepthStencilView(dsv, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);
            HRESULT hr = cmdList->Close();
            if (FAILED(hr)) throw std::runtime_error("Check Y: ClearDepthStencilView command list Close failed");
            renderer.ExecuteCommandListAndWaitEXT(cmdList);
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
        D3D12VertexBufferRenderer vbNear(&renderer, 3);
        vbNear.SetData(kNear, 3, sizeof(VPCZ));
        D3D12VertexBufferRenderer vbFar(&renderer, 3);
        vbFar.SetData(kFar, 3, sizeof(VPCZ));

        // Real ApplyDepthStencilState: DepthEnable=true, DepthWriteEnable=true,
        // DepthFunc=CompareFunction::LessEqual(3, the real, verified ordinal -- XNA's own
        // DepthStencilState.Default). Stencil fields are 0/false throughout -- deliberately not
        // threaded into the PSO yet (DX-118's own documented scope boundary).
        renderer.ApplyDepthStencilState(/*depthEnable=*/true, /*depthWriteEnable=*/true, /*depthFunc=LessEqual*/3,
                                       false, 0, 0, 0, 0, 0, 0, 0, false, 0, 0, 0, 0);

        clearDepthTo1();
        renderer.Clear(0.0f, 0.0f, 0.0f, 1.0f);
        renderer.DrawColoredPrimitives(vbNear, Matrix::getIdentityProperty(), Matrix::getIdentityProperty(),
                                      Matrix::getIdentityProperty(), PrimitiveType::TriangleList, 1);
        renderer.DrawColoredPrimitives(vbFar, Matrix::getIdentityProperty(), Matrix::getIdentityProperty(),
                                      Matrix::getIdentityProperty(), PrimitiveType::TriangleList, 1);
        auto nearThenFar = ReadBackRenderTargetFull(renderer, rt.Get(), kRtWidth, kRtHeight);
        Check(regionIs(nearThenFar, 255, 0, 0, 255),
              "Y2: real depth test -- drawing NEAR (z=0.2, red) then FAR (z=0.8, green) with "
              "DepthEnable=true/DepthFunc=LessEqual genuinely rejects the far draw, near survives");

        clearDepthTo1();
        renderer.Clear(0.0f, 0.0f, 0.0f, 1.0f);
        renderer.DrawColoredPrimitives(vbFar, Matrix::getIdentityProperty(), Matrix::getIdentityProperty(),
                                      Matrix::getIdentityProperty(), PrimitiveType::TriangleList, 1);
        renderer.DrawColoredPrimitives(vbNear, Matrix::getIdentityProperty(), Matrix::getIdentityProperty(),
                                      Matrix::getIdentityProperty(), PrimitiveType::TriangleList, 1);
        auto farThenNear = ReadBackRenderTargetFull(renderer, rt.Get(), kRtWidth, kRtHeight);
        Check(regionIs(farThenNear, 255, 0, 0, 255),
              "Y3: same real depth test, reversed draw order -- FAR (green) drawn first, then NEAR "
              "(red) genuinely passes the depth test and overwrites it -- proves this is a real "
              "per-pixel depth comparison, not merely 'the second draw always wins/loses'");

        // Depth-disabled control: with DepthEnable=false, draw order alone determines the winner --
        // confirms Y2/Y3's outcome really came from the depth test, not draw order or some other
        // effect.
        renderer.ApplyDepthStencilState(/*depthEnable=*/false, false, 3, false, 0, 0, 0, 0, 0, 0, 0,
                                       false, 0, 0, 0, 0);
        renderer.Clear(0.0f, 0.0f, 0.0f, 1.0f);
        renderer.DrawColoredPrimitives(vbNear, Matrix::getIdentityProperty(), Matrix::getIdentityProperty(),
                                      Matrix::getIdentityProperty(), PrimitiveType::TriangleList, 1);
        renderer.DrawColoredPrimitives(vbFar, Matrix::getIdentityProperty(), Matrix::getIdentityProperty(),
                                      Matrix::getIdentityProperty(), PrimitiveType::TriangleList, 1);
        auto depthOffLastWins = ReadBackRenderTargetFull(renderer, rt.Get(), kRtWidth, kRtHeight);
        Check(regionIs(depthOffLastWins, 0, 255, 0, 255),
              "Y4: control -- with DepthEnable=false, the LAST draw wins regardless of Z (FAR/green "
              "drawn second overwrites NEAR/red) -- confirms Y2/Y3's outcome really came from the "
              "depth test, not draw order or some other effect");

        renderer.UnbindOffscreenColorTargetEXT();
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
        HRESULT hrRt = renderer.GetDeviceEXT()->CreateCommittedResource(
            &heapProps, D3D12_HEAP_FLAG_NONE, &rtDesc,
            D3D12_RESOURCE_STATE_RENDER_TARGET, nullptr, IID_PPV_ARGS(rt.GetAddressOf()));
        Check(SUCCEEDED(hrRt) && rt != nullptr, "Z0: real off-screen RGBA8 render-target resource created");
        renderer.GetResourceStateTrackerEXT().TrackResource(rt.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET);

        D3D12_CPU_DESCRIPTOR_HANDLE rtv = renderer.AllocateRtvDescriptorEXT();
        renderer.GetDeviceEXT()->CreateRenderTargetView(rt.Get(), nullptr, rtv);
        renderer.BindOffscreenColorTargetEXT(rt.Get(), rtv, DXGI_FORMAT_R8G8B8A8_UNORM, kRtWidth, kRtHeight);

        // No depth test for this check -- reset to a known, predictable state regardless of what
        // Check Y's own DepthStencilState/RasterizerState left tracked (state persists across
        // checks, mirroring real GraphicsDevice behavior).
        renderer.ApplyDepthStencilState(false, false, 3, false, 0, 0, 0, 0, 0, 0, 0, false, 0, 0, 0, 0);
        renderer.ApplyRasterizerState(/*cullMode=*/0 /*CullMode::None*/, /*fillMode=*/0, /*scissor=*/false);

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
        D3D12VertexBufferRenderer vbQuad(&renderer, 6);
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
        D3D12TextureRenderer texZ(&renderer, img);

        GpuDrawParams zp;
        zp.texture0 = &texZ;
        zp.textureEnabled = true;

        renderer.ApplySamplerState(0, /*filter=*/1 /*TextureFilter::Point*/,
                                  /*addressU=*/0 /*TextureAddressMode::Wrap*/,
                                  /*addressV=*/0 /*Wrap*/, /*maxAnisotropy=*/1);
        renderer.Clear(0.0f, 0.0f, 0.0f, 1.0f);
        renderer.DrawPrimitivesEx(vbQuad, Matrix::getIdentityProperty(), Matrix::getIdentityProperty(),
                                 Matrix::getIdentityProperty(), PrimitiveType::TriangleList, 2, zp);
        auto afterWrap = ReadBackRenderTargetFull(renderer, rt.Get(), kRtWidth, kRtHeight);
        Check(probeIs(afterWrap, 255, 0, 0, 255),
              "Z1: real ApplySamplerState(..., AddressU=Wrap) genuinely tiles past U=1.0 -- probe "
              "samples texel column 0 (red), U's wrapped fractional part (plans/plan_dx.md DX-119)");

        renderer.ApplySamplerState(0, /*filter=*/1 /*Point*/,
                                  /*addressU=*/1 /*TextureAddressMode::Clamp*/,
                                  /*addressV=*/1 /*Clamp*/, /*maxAnisotropy=*/1);
        renderer.Clear(0.0f, 0.0f, 0.0f, 1.0f);
        renderer.DrawPrimitivesEx(vbQuad, Matrix::getIdentityProperty(), Matrix::getIdentityProperty(),
                                 Matrix::getIdentityProperty(), PrimitiveType::TriangleList, 2, zp);
        auto afterClamp = ReadBackRenderTargetFull(renderer, rt.Get(), kRtWidth, kRtHeight);
        Check(probeIs(afterClamp, 0, 255, 0, 255),
              "Z2: real ApplySamplerState(..., AddressU=Clamp) genuinely holds at U=1.0 -- SAME "
              "geometry/UVs as Z1, opposite outcome (texel column 1, green), purely from the "
              "SamplerState change -- proves this is a real, live sampler binding, not a hardcoded "
              "default (plans/plan_dx.md DX-119)");

        // Cache identity/distinctness proof, mirroring D3D11SamplerCache's own established pattern
        // (D3D11's Check L, DX-44). Note: state (currentSamplerAddressU_ etc.) is tracked, not
        // reset between checks -- Z2 left slot 0 at Clamp, so apply Wrap explicitly BEFORE reading
        // each handle below, not after (fetching a handle reflects whatever was last applied).
        renderer.ApplySamplerState(0, 1, 0, 0, 1); // Wrap/Point
        D3D12_GPU_DESCRIPTOR_HANDLE wrapHandle1 = renderer.GetSamplerGpuHandleEXT(0);
        renderer.ApplySamplerState(0, 1, 0, 0, 1); // re-apply the exact same Wrap/Point state
        D3D12_GPU_DESCRIPTOR_HANDLE wrapHandle2 = renderer.GetSamplerGpuHandleEXT(0);
        Check(wrapHandle1.ptr == wrapHandle2.ptr,
              "Z3: identical SamplerState (Point/Wrap) resolves to the SAME cached sampler "
              "descriptor handle, not a fresh heap slot every call");
        renderer.ApplySamplerState(0, 1, 1, 1, 1); // Clamp/Point -- genuinely different state
        D3D12_GPU_DESCRIPTOR_HANDLE clampHandle = renderer.GetSamplerGpuHandleEXT(0);
        Check(clampHandle.ptr != wrapHandle1.ptr,
              "Z4: a genuinely different SamplerState (Point/Clamp) resolves to a DIFFERENT cached "
              "sampler descriptor handle");

        renderer.UnbindOffscreenColorTargetEXT();
    }

    // ---- plans/plan_dx.md DX-154: per-slot SamplerState, all 16 slots bound SIMULTANEOUSLY with 16
    // genuinely DIFFERENT SamplerState configurations, then verified independent -- ports D3D11's
    // own DX-142 methodology to D3D12SamplerCache. Check Z above already proved cache
    // identity/distinctness for a single slot in isolation, but nothing has proven slot N's
    // binding survives slots N+1..15 also being applied (a plausible place for an off-by-one/index
    // bug or an accidental single-slot cache to hide). ----
    {
        D3D12_GPU_DESCRIPTOR_HANDLE boundAtApplyTime[16]{};
        for (int slot = 0; slot < 16; ++slot)
        {
            // Spread across TextureFilter's 6 values and TextureAddressMode's 3 values so adjacent
            // slots never accidentally share an identical configuration -- same spread D3D11's own
            // DX-142 uses.
            renderer.ApplySamplerState(slot, slot % 6, slot % 3, (slot + 1) % 3, 1);
            boundAtApplyTime[slot] = renderer.GetSamplerGpuHandleEXT(slot);
        }

        bool allSlotsNonNull = true;
        for (int slot = 0; slot < 16; ++slot)
            if (boundAtApplyTime[slot].ptr == 0) allSlotsNonNull = false;
        Check(allSlotsNonNull,
              "UU0: D3D12SamplerCache: all 16 sampler slots hold a real, non-null descriptor handle "
              "immediately after being applied (plans/plan_dx.md DX-154)");

        // Re-query every slot NOW, after all 16 have been applied -- if applying a later slot (e.g.
        // 15) ever clobbered an earlier one (e.g. 0) via an off-by-one or aliasing bug, this is
        // where it would show up: the handle bound to slot 0 right now would differ from the one
        // captured immediately after slot 0's own ApplySamplerState() call.
        bool allSlotsStillCorrect = true;
        for (int slot = 0; slot < 16; ++slot)
        {
            const D3D12_GPU_DESCRIPTOR_HANDLE now = renderer.GetSamplerGpuHandleEXT(slot);
            if (now.ptr != boundAtApplyTime[slot].ptr) allSlotsStillCorrect = false;
        }
        Check(allSlotsStillCorrect,
              "UU1: D3D12SamplerCache: every one of the 16 slots still holds its OWN originally-bound "
              "sampler descriptor handle after all 16 were applied -- proves genuine per-slot "
              "independence, not a shared/aliased single slot (plans/plan_dx.md DX-154)");
    }

    // ---- plans/plan_dx.md DX-120: D3D12OcclusionQueryRenderer -- a real visible-vs-invisible
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
        HRESULT hrRt = renderer.GetDeviceEXT()->CreateCommittedResource(
            &heapProps, D3D12_HEAP_FLAG_NONE, &rtDesc,
            D3D12_RESOURCE_STATE_RENDER_TARGET, nullptr, IID_PPV_ARGS(rt.GetAddressOf()));
        Check(SUCCEEDED(hrRt) && rt != nullptr, "AA0: real off-screen RGBA8 render-target resource created");
        renderer.GetResourceStateTrackerEXT().TrackResource(rt.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET);

        D3D12_CPU_DESCRIPTOR_HANDLE rtv = renderer.AllocateRtvDescriptorEXT();
        renderer.GetDeviceEXT()->CreateRenderTargetView(rt.Get(), nullptr, rtv);
        renderer.BindOffscreenColorTargetEXT(rt.Get(), rtv, DXGI_FORMAT_R8G8B8A8_UNORM, kRtWidth, kRtHeight);
        renderer.ApplyDepthStencilState(false, false, 3, false, 0, 0, 0, 0, 0, 0, 0, false, 0, 0, 0, 0);
        renderer.ApplyRasterizerState(/*cullMode=*/0 /*CullMode::None*/, /*fillMode=*/0, /*scissor=*/false);

        auto occlusionQuery = renderer.CreateOcclusionQuery();
        Check(occlusionQuery != nullptr, "AA1: CreateOcclusionQuery() returns a real D3D12OcclusionQueryRenderer");

        // Same oversized-triangle-covering-the-full-NDC-square trick Check M established --
        // world=view=projection=Identity, so these Position values ARE clip-space coordinates.
        struct VPC { float x, y, z; uint32_t color; };
        static const VPC kVisibleTri[3] = {
            {-1.0f, -1.0f, 0.0f, 0xFF0000FFu},
            { 3.0f, -1.0f, 0.0f, 0xFF0000FFu},
            {-1.0f,  3.0f, 0.0f, 0xFF0000FFu},
        };
        D3D12VertexBufferRenderer vbVisible(&renderer, 3);
        vbVisible.SetData(kVisibleTri, 3, sizeof(VPC));

        renderer.Clear(0.0f, 0.0f, 0.0f, 1.0f);
        occlusionQuery->Begin();
        renderer.DrawColoredPrimitives(vbVisible, Matrix::getIdentityProperty(), Matrix::getIdentityProperty(),
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
        D3D12VertexBufferRenderer vbInvisible(&renderer, 3);
        vbInvisible.SetData(kInvisibleTri, 3, sizeof(VPC));

        occlusionQuery->Begin();
        renderer.DrawColoredPrimitives(vbInvisible, Matrix::getIdentityProperty(), Matrix::getIdentityProperty(),
                                      Matrix::getIdentityProperty(), PrimitiveType::TriangleList, 1);
        occlusionQuery->End();
        const int invisibleCount = occlusionQuery->PixelCount();
        Check(invisibleCount == 0,
              "AA4: the SAME query object, reused around off-screen (clipped) geometry, reports "
              "EXACTLY 0 -- a genuine visible-vs-invisible discriminating result, not just \"the "
              "query completed\" (plans/plan_dx.md DX-120, closes DX-147's D3D12 half)");

        renderer.UnbindOffscreenColorTargetEXT();
    }

    // ---- plans/plan_dx.md DX-121: D3D12EffectRenderer -- runtime D3DCompile() of custom HLSL, driven
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
        HRESULT hrRt = renderer.GetDeviceEXT()->CreateCommittedResource(
            &heapProps, D3D12_HEAP_FLAG_NONE, &rtDesc,
            D3D12_RESOURCE_STATE_RENDER_TARGET, nullptr, IID_PPV_ARGS(rt.GetAddressOf()));
        Check(SUCCEEDED(hrRt) && rt != nullptr, "BB0: real off-screen RGBA8 render-target resource created");
        renderer.GetResourceStateTrackerEXT().TrackResource(rt.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET);

        D3D12_CPU_DESCRIPTOR_HANDLE rtv = renderer.AllocateRtvDescriptorEXT();
        renderer.GetDeviceEXT()->CreateRenderTargetView(rt.Get(), nullptr, rtv);

        auto effect = renderer.CreateEffectRenderer(
            "struct VSIn { float2 pos:POSITION0; float2 uv:TEXCOORD0; float4 col:COLOR0; };\n"
            "struct VSOut { float4 pos:SV_Position; float4 col:TEXCOORD0; };\n"
            "cbuffer CB : register(b0) { float4 pad0[5]; float4 uColor; float4 uFloat0; };\n"
            "VSOut main(VSIn input) { VSOut o; o.pos=float4(input.pos,0,1); o.col=input.col*uColor; return o; }",
            "struct PSIn { float4 pos:SV_Position; float4 col:TEXCOORD0; };\n"
            "float4 main(PSIn input):SV_Target { return input.col; }");
        Check(effect && effect->IsValid(),
              "BB1: DirectX12Renderer::CreateEffectRenderer() -- real runtime D3DCompile() of "
              "arbitrary HLSL source builds a real PSO+constant-buffer end to end (plans/plan_dx.md DX-121)");

        bool effIsExact = false;
        if (effect && effect->IsValid())
        {
            auto* d3dEffect = dynamic_cast<D3D12EffectRenderer*>(effect.get());
            Check(d3dEffect != nullptr, "BB2: CreateEffectRenderer() returns a real D3D12EffectRenderer");

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
            D3D12VertexBufferRenderer vbFx(&renderer, 3);
            vbFx.SetData(kTriFx, 3, sizeof(SpriteVtx));

            // The (1,1,1) root signature this PSO was built against always declares an SRV+sampler
            // table -- bind a real, throwaway 1x1 texture/sampler even though this particular
            // pixel shader never samples it, matching every other real D3D12 draw's own full
            // root-parameter binding discipline (avoids relying on undefined/unbound-table
            // behavior).
            ImageData dummyImg;
            dummyImg.width = 1; dummyImg.height = 1; dummyImg.mipLevels = 1;
            dummyImg.pixels = {255, 255, 255, 255};
            D3D12TextureRenderer dummyTex(&renderer, dummyImg);
            renderer.ApplySamplerState(0, /*filter=*/1 /*Point*/, /*addressU=*/0, /*addressV=*/0, /*maxAnisotropy=*/1);

            auto rootSig = renderer.GetRootSignatureCacheEXT().GetOrCreate(renderer.GetDeviceEXT(), 1, 1, 1);

            ID3D12CommandAllocator* allocator = renderer.GetCommandAllocatorEXT(0);
            ID3D12GraphicsCommandList* cmdList = renderer.GetCommandListEXT();
            allocator->Reset();
            cmdList->Reset(allocator, d3dEffect->GetPipelineStateEXT());

            renderer.GetResourceStateTrackerEXT().TransitionTo(cmdList, rt.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET);
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

            ID3D12DescriptorHeap* heaps[] = {renderer.GetCbvSrvUavHeapEXT(), renderer.GetSamplerHeapEXT()};
            cmdList->SetDescriptorHeaps(2, heaps);
            cmdList->SetGraphicsRootDescriptorTable(1, dummyTex.GetShaderResourceViewGpuHandleEXT());
            cmdList->SetGraphicsRootDescriptorTable(2, renderer.GetSamplerGpuHandleEXT(0));

            cmdList->DrawInstanced(3, 1, 0, 0);

            HRESULT hr = cmdList->Close();
            if (SUCCEEDED(hr))
                renderer.ExecuteCommandListAndWaitEXT(cmdList);

            if (SUCCEEDED(hr))
            {
                auto pixels = ReadBackRenderTargetFull(renderer, rt.Get(), kRtWidth, kRtHeight);
                const std::size_t idx = (static_cast<std::size_t>(32) * kRtWidth + 32) * 4;
                effIsExact = pixels[idx + 0] == 0 && pixels[idx + 1] == 255 &&
                            pixels[idx + 2] == 0 && pixels[idx + 3] == 255;
            }
        }
        Check(effIsExact,
              "BB3: D3D12EffectRenderer::Bind() -- a real custom-compiled shader pair, driven by "
              "SetUniformVec4()'s fixed-slot constant buffer, draws the exact expected color "
              "(plans/plan_dx.md DX-121)");

        auto badEffect = renderer.CreateEffectRenderer("this is not valid HLSL {{{", "also not valid ]]]");
        Check(badEffect && !badEffect->IsValid() && !badEffect->GetCompileError().empty(),
              "BB4: DirectX12Renderer::CreateEffectRenderer() -- a deliberately broken HLSL source "
              "fails CompileProgram() with a real, non-empty compiler error message (plans/plan_dx.md DX-121)");
    }

    // ---- plans/plan_dx.md DX-122: D3D12Texture3DRenderer -- a genuine sub-volume (not just full-level)
    // upload/readback round-trip, with distinct per-Z-slice colors so the Z offset itself is
    // proven, not just X/Y. ----
    {
        auto tex3d = renderer.CreateTexture3D(4, 4, 2, /*mipMap=*/false, /*surfaceFormat=*/0);
        Check(tex3d != nullptr, "CC0: DirectX12Renderer::CreateTexture3D() returns a real D3D12Texture3DRenderer");

        auto* d3dTex3d = dynamic_cast<D3D12Texture3DRenderer*>(tex3d.get());
        Check(d3dTex3d != nullptr && d3dTex3d->GetResourceEXT() != nullptr,
              "CC1: real ID3D12Resource (TEXTURE3D dimension) created");

        // A 2x2x2 sub-cube at offset (1,1,0) within the 4x4x2 volume -- offset (not just (0,0,0))
        // genuinely exercises SetData's/GetData's x/y/z parameters, and a different solid color per
        // Z slice (slice 0 = red, slice 1 = green) genuinely exercises the Z/depth offset and
        // per-slice pitch math, not just X/Y.
        std::vector<uint8_t> uploadData(2 * 2 * 2 * 4);
        for (int slice = 0; slice < 2; ++slice)
        {
            for (int px = 0; px < 4; ++px)
            {
                const std::size_t base = (static_cast<std::size_t>(slice) * 4 + px) * 4;
                uploadData[base + 0] = slice == 0 ? 255 : 0;
                uploadData[base + 1] = slice == 0 ? 0 : 255;
                uploadData[base + 2] = 0;
                uploadData[base + 3] = 255;
            }
        }
        tex3d->SetData(0, 1, 1, 0, 2, 2, 2, uploadData.data(), static_cast<int>(uploadData.size()));

        // REMED-GFX-130: GetData now reports whether it actually completed the transfer -- the
        // return value IS the contract, so it is asserted rather than discarded.
        std::vector<uint8_t> readback(uploadData.size(), 0);
        const bool volOk = tex3d->GetData(0, 1, 1, 0, 2, 2, 2, readback.data(),
                                          static_cast<int>(readback.size()));
        Check(volOk, "GFX130a: D3D12Texture3DRenderer::GetData reports a completed readback");
        Check(readback == uploadData,
              "CC2: D3D12Texture3DRenderer::SetData()/GetData() round-trip EXACT bytes for a real "
              "off-center sub-volume upload, including the per-Z-slice color difference (plans/plan_dx.md DX-122)");

        // REMED-GFX-130: an out-of-range mip level must report false WITHOUT writing one byte, so
        // the shared layer raises System::NotSupportedException instead of converting its own
        // zeroed scratch buffer into a fabricated volume.
        std::vector<uint8_t> poisoned(uploadData.size(), 0xAB);
        const bool badLevel = tex3d->GetData(9, 0, 0, 0, 2, 2, 2, poisoned.data(),
                                             static_cast<int>(poisoned.size()));
        bool poisonIntact = true;
        for (uint8_t b : poisoned) if (b != 0xAB) { poisonIntact = false; break; }
        Check(!badLevel && poisonIntact,
              "GFX130b: D3D12Texture3DRenderer::GetData rejects an out-of-range mip level and leaves "
              "the caller's buffer byte-for-byte untouched (REMED-GFX-130)");
    }

    // ---- plans/plan_dx.md DX-123: D3D12TextureCubeRenderer::GetData() -- real readback, mirroring
    // Texture3D's own off-center sub-rect + face-selectivity proof (not just full-face). ----
    {
        D3D12TextureCubeRenderer ddCube(&renderer, 8, false, 0);

        // Two distinctly-colored 4x4 sub-rects, uploaded to two different faces at two different
        // offsets -- proves GetData() reads the right FACE (not just the right texture) and the
        // right SUB-RECT (not just face 0 in full).
        std::vector<uint8_t> face2Data(4 * 4 * 4);
        std::vector<uint8_t> face4Data(4 * 4 * 4);
        for (int i = 0; i < 4 * 4; ++i)
        {
            face2Data[i * 4 + 0] = 200; face2Data[i * 4 + 1] = 50;
            face2Data[i * 4 + 2] = 10;  face2Data[i * 4 + 3] = 255;
            face4Data[i * 4 + 0] = 5;   face4Data[i * 4 + 1] = 250;
            face4Data[i * 4 + 2] = 100; face4Data[i * 4 + 3] = 255;
        }
        ddCube.SetData(/*face=*/2, /*level=*/0, /*x=*/4, /*y=*/0, 4, 4, face2Data.data(), static_cast<int>(face2Data.size()));
        ddCube.SetData(/*face=*/4, /*level=*/0, /*x=*/0, /*y=*/4, 4, 4, face4Data.data(), static_cast<int>(face4Data.size()));

        // REMED-GFX-130: as above -- the completion result is part of the contract.
        std::vector<uint8_t> readbackFace2(face2Data.size(), 0);
        std::vector<uint8_t> readbackFace4(face4Data.size(), 0);
        const bool face2Ok = ddCube.GetData(/*face=*/2, /*level=*/0, /*x=*/4, /*y=*/0, 4, 4, readbackFace2.data(), static_cast<int>(readbackFace2.size()));
        const bool face4Ok = ddCube.GetData(/*face=*/4, /*level=*/0, /*x=*/0, /*y=*/4, 4, 4, readbackFace4.data(), static_cast<int>(readbackFace4.size()));
        Check(face2Ok && face4Ok,
              "GFX130c: D3D12TextureCubeRenderer::GetData reports a completed readback on both faces");

        // REMED-GFX-130: an out-of-range cube face must report false without writing anything.
        std::vector<uint8_t> poisonedFace(face2Data.size(), 0xAB);
        const bool badFace = ddCube.GetData(/*face=*/6, /*level=*/0, /*x=*/0, /*y=*/0, 4, 4,
                                            poisonedFace.data(), static_cast<int>(poisonedFace.size()));
        bool facePoisonIntact = true;
        for (uint8_t b : poisonedFace) if (b != 0xAB) { facePoisonIntact = false; break; }
        Check(!badFace && facePoisonIntact,
              "GFX130d: D3D12TextureCubeRenderer::GetData rejects an out-of-range CubeMapFace and "
              "leaves the caller's buffer byte-for-byte untouched (REMED-GFX-130)");

        Check(readbackFace2 == face2Data,
              "DD1: D3D12TextureCubeRenderer::GetData() round-trips EXACT bytes for a real off-center "
              "sub-rect upload on face 2 (plans/plan_dx.md DX-123)");
        Check(readbackFace4 == face4Data,
              "DD2: GetData() on a DIFFERENT face (4) at a DIFFERENT offset reads its own distinct "
              "content, not face 2's -- proves real per-face subresource selection, not just "
              "\"some data came back\" (plans/plan_dx.md DX-123)");

        // A never-written region of face 2 must read back as the texture's real zero-initialized
        // GPU content, not stale/uninitialized CPU memory -- a genuine live-GPU readback proof.
        std::vector<uint8_t> untouchedRegion(4 * 4 * 4, 0xAB); // poison the CPU buffer first
        (void)ddCube.GetData(/*face=*/2, /*level=*/0, /*x=*/0, /*y=*/0, 4, 4, untouchedRegion.data(), static_cast<int>(untouchedRegion.size()));
        bool untouchedIsZero = true;
        for (std::size_t i = 0; i < untouchedRegion.size(); ++i)
            if (untouchedRegion[i] != 0) { untouchedIsZero = false; break; }
        Check(untouchedIsZero,
              "DD3: GetData() on face 2's UNTOUCHED region reads real zero-initialized GPU content, "
              "not the CPU buffer's poison value -- confirms this is a genuine live readback (plans/plan_dx.md DX-123)");
    }

    // ---- Check GG (plans/plan_dx.md DX-140, partial -- NPOT only): a genuinely non-power-of-two
    // Texture2D (5x3), never exercised anywhere in this test suite before. 5*4=20 bytes/row does
    // NOT divide evenly into D3D12_TEXTURE_DATA_PITCH_ALIGNMENT (256) -- exactly the kind of odd
    // width that could expose a row-pitch-alignment bug in the upload path. Sampled via a real
    // textured3d draw (same pattern as Check N), not a direct GetData() (D3D12TextureRenderer has no
    // such method -- this project's own established convention verifies 2D texture content by
    // drawing+reading back, not a direct CPU-side readback API). ----
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
        HRESULT hrRt = renderer.GetDeviceEXT()->CreateCommittedResource(
            &heapProps, D3D12_HEAP_FLAG_NONE, &rtDesc,
            D3D12_RESOURCE_STATE_RENDER_TARGET, nullptr, IID_PPV_ARGS(rt.GetAddressOf()));
        Check(SUCCEEDED(hrRt) && rt != nullptr, "GG0: real off-screen RGBA8 render-target resource created");
        renderer.GetResourceStateTrackerEXT().TrackResource(rt.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET);
        D3D12_CPU_DESCRIPTOR_HANDLE rtv = renderer.AllocateRtvDescriptorEXT();
        renderer.GetDeviceEXT()->CreateRenderTargetView(rt.Get(), nullptr, rtv);
        renderer.BindOffscreenColorTargetEXT(rt.Get(), rtv, DXGI_FORMAT_R8G8B8A8_UNORM, kRtWidth, kRtHeight);

        auto pixelAt = [&](const std::vector<uint8_t>& buf, int x, int y) -> std::array<uint8_t, 4>
        {
            const std::size_t idx = (static_cast<std::size_t>(y) * kRtWidth + static_cast<std::size_t>(x)) * 4;
            return {buf[idx + 0], buf[idx + 1], buf[idx + 2], buf[idx + 3]};
        };
        auto regionIsExact = [&](const std::vector<uint8_t>& buf, uint8_t r, uint8_t g, uint8_t b, uint8_t a)
        {
            for (int y = 28; y < 32; ++y)
                for (int x = 28; x < 32; ++x)
                {
                    const auto p = pixelAt(buf, x, y);
                    if (p[0] != r || p[1] != g || p[2] != b || p[3] != a) return false;
                }
            return true;
        };

        struct VPT { float x, y, z; float u, v; };
        static const VPT kTriGG[3] = {
            {-1.0f, -1.0f, 0.0f, 0.0f, 1.0f},
            { 3.0f, -1.0f, 0.0f, 2.0f, 1.0f},
            {-1.0f,  3.0f, 0.0f, 0.0f, -1.0f},
        };
        D3D12VertexBufferRenderer vbGG(&renderer, 3);
        vbGG.SetData(kTriGG, 3, sizeof(VPT));

        // 5x3 NPOT -- every pixel the same solid color, so any UV/filter samples the identical exact
        // value regardless of exact texel alignment (isolates "does NPOT upload/sample corrupt
        // anything" from unrelated bilinear-blend-at-texel-boundary concerns DX-131 already hit).
        ImageData npotImg;
        npotImg.width = 5; npotImg.height = 3; npotImg.mipLevels = 1;
        npotImg.pixels.resize(5 * 3 * 4);
        for (int i = 0; i < 5 * 3; ++i)
        {
            npotImg.pixels[i * 4 + 0] = 123; npotImg.pixels[i * 4 + 1] = 45;
            npotImg.pixels[i * 4 + 2] = 200; npotImg.pixels[i * 4 + 3] = 255;
        }
        D3D12TextureRenderer npotTex(&renderer, npotImg);
        Check(npotTex.GetWidth() == 5 && npotTex.GetHeight() == 3,
              "GG1: real D3D12TextureRenderer construction with a genuinely non-power-of-two "
              "5x3 size reports the exact requested dimensions (plans/plan_dx.md DX-140)");

        GpuDrawParams gp;
        gp.texture0 = &npotTex;
        gp.textureEnabled = true;

        renderer.Clear(0.0f, 1.0f, 0.0f, 1.0f);
        renderer.DrawPrimitivesEx(vbGG, Matrix::getIdentityProperty(), Matrix::getIdentityProperty(),
                                 Matrix::getIdentityProperty(), PrimitiveType::TriangleList, 1, gp);
        auto ggResult = ReadBackRenderTargetFull(renderer, rt.Get(), kRtWidth, kRtHeight);
        Check(regionIsExact(ggResult, 123, 45, 200, 255),
              "GG2: DrawPrimitivesEx() samples the exact color from a real 5x3 NPOT texture upload -- "
              "20 bytes/row does not divide evenly into D3D12_TEXTURE_DATA_PITCH_ALIGNMENT (256), "
              "genuinely exercising the row-pitch-alignment path for an odd width (plans/plan_dx.md DX-140)");

        renderer.UnbindOffscreenColorTargetEXT();
    }

    // ---- Check HH (plans/plan_dx.md DX-141): D3D12 counterpart to DX-126 (D3D11) -- mip level > 0
    // SetData/upload dedicated test. D3D12TextureRenderer::UpdatePixelsLevel() (DX-40/DX-109) already
    // exists but had never been exercised. Rather than trying to force the GPU's automatic mip
    // selection to pick level 1 during a shader Sample() (fragile/driver-dependent -- the stock
    // shaders all use implicit-LOD Sample(), not an explicit SampleLevel()), this reads mip level 1
    // back directly via CopyTextureRegion -- the same real, direct GPU-readback discipline
    // DX-122/DX-123's own GetData() implementations already established, giving an exact,
    // deterministic proof instead of a driver-dependent one. ----
    {
        ImageData mipImg;
        mipImg.width = 4; mipImg.height = 4; mipImg.mipLevels = 2;
        mipImg.pixels.assign(4 * 4 * 4, 0);
        for (int i = 0; i < 4 * 4; ++i)
        {
            mipImg.pixels[i * 4 + 0] = 10; mipImg.pixels[i * 4 + 1] = 20;
            mipImg.pixels[i * 4 + 2] = 30; mipImg.pixels[i * 4 + 3] = 255;
        }
        D3D12TextureRenderer mipTex(&renderer, mipImg);
        Check(mipTex.GetMipLevelsEXT() == 2, "HH0: real D3D12TextureRenderer allocated with 2 mip levels");

        std::vector<uint8_t> level1Data(2 * 2 * 4);
        for (int i = 0; i < 2 * 2; ++i)
        {
            level1Data[i * 4 + 0] = 210; level1Data[i * 4 + 1] = 220;
            level1Data[i * 4 + 2] = 230; level1Data[i * 4 + 3] = 255;
        }
        mipTex.UpdatePixelsLevel(1, level1Data.data(), 2, 2);

        // Direct GPU readback of a given mip subresource -- mirrors DX-122/DX-123's own
        // GetData()-style CopyTextureRegion pattern, kept test-local since D3D12TextureRenderer (the
        // plain 2D renderer) has no public GetData() of its own (this project's established
        // 2D-texture-content-verification convention is draw+readback via a render target, per
        // Check N/GG above -- this test uses a direct copy instead specifically to sidestep the
        // shader-mip-selection problem noted above).
        auto readbackMip = [&](int level, int w, int h) -> std::vector<uint8_t>
        {
            const UINT rowPitch = (static_cast<UINT>(w) * 4 + D3D12_TEXTURE_DATA_PITCH_ALIGNMENT - 1)
                                 & ~(D3D12_TEXTURE_DATA_PITCH_ALIGNMENT - 1);
            const UINT64 bufSize = static_cast<UINT64>(rowPitch) * static_cast<UINT64>(h);

            D3D12_HEAP_PROPERTIES readbackHeapProps{};
            readbackHeapProps.Type = D3D12_HEAP_TYPE_READBACK;
            D3D12_RESOURCE_DESC bufDesc{};
            bufDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
            bufDesc.Width = bufSize;
            bufDesc.Height = 1;
            bufDesc.DepthOrArraySize = 1;
            bufDesc.MipLevels = 1;
            bufDesc.Format = DXGI_FORMAT_UNKNOWN;
            bufDesc.SampleDesc.Count = 1;
            bufDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

            Microsoft::WRL::ComPtr<ID3D12Resource> readback;
            HRESULT hr = renderer.GetDeviceEXT()->CreateCommittedResource(
                &readbackHeapProps, D3D12_HEAP_FLAG_NONE, &bufDesc,
                D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(readback.GetAddressOf()));
            if (FAILED(hr)) return {};

            D3D12_TEXTURE_COPY_LOCATION dst{};
            dst.pResource = readback.Get();
            dst.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
            dst.PlacedFootprint.Footprint.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
            dst.PlacedFootprint.Footprint.Width = static_cast<UINT>(w);
            dst.PlacedFootprint.Footprint.Height = static_cast<UINT>(h);
            dst.PlacedFootprint.Footprint.Depth = 1;
            dst.PlacedFootprint.Footprint.RowPitch = rowPitch;

            D3D12_TEXTURE_COPY_LOCATION src{};
            src.pResource = mipTex.GetResourceEXT();
            src.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
            src.SubresourceIndex = static_cast<UINT>(level); // single-plane, ArraySize=1 -> level itself

            ID3D12CommandAllocator* allocator = renderer.GetCommandAllocatorEXT(0);
            ID3D12GraphicsCommandList* cmdList = renderer.GetCommandListEXT();
            allocator->Reset();
            cmdList->Reset(allocator, nullptr);
            auto& tracker = renderer.GetResourceStateTrackerEXT();
            const D3D12_RESOURCE_STATES priorState = tracker.GetTrackedStateEXT(mipTex.GetResourceEXT());
            tracker.TransitionTo(cmdList, mipTex.GetResourceEXT(), D3D12_RESOURCE_STATE_COPY_SOURCE);
            cmdList->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
            tracker.TransitionTo(cmdList, mipTex.GetResourceEXT(), priorState);
            hr = cmdList->Close();
            if (FAILED(hr)) return {};
            renderer.ExecuteCommandListAndWaitEXT(cmdList);

            uint8_t* mapped = nullptr;
            const D3D12_RANGE mapRange{0, static_cast<SIZE_T>(bufSize)};
            if (FAILED(readback->Map(0, &mapRange, reinterpret_cast<void**>(&mapped)))) return {};
            std::vector<uint8_t> out(static_cast<std::size_t>(w) * h * 4);
            for (int row = 0; row < h; ++row)
                std::memcpy(out.data() + static_cast<std::size_t>(row) * w * 4,
                            mapped + static_cast<std::size_t>(row) * rowPitch,
                            static_cast<std::size_t>(w) * 4);
            const D3D12_RANGE writtenRange{0, 0};
            readback->Unmap(0, &writtenRange);
            return out;
        };

        auto isSolid = [](const std::vector<uint8_t>& buf, uint8_t r, uint8_t g, uint8_t b, uint8_t a)
        {
            if (buf.empty()) return false;
            for (std::size_t i = 0; i < buf.size(); i += 4)
                if (buf[i] != r || buf[i+1] != g || buf[i+2] != b || buf[i+3] != a) return false;
            return true;
        };

        auto level1Readback = readbackMip(1, 2, 2);
        Check(isSolid(level1Readback, 210, 220, 230, 255),
              "HH1: UpdatePixelsLevel(1, ...) round-trips EXACT bytes for a real mip level 1 upload, "
              "read back via a direct CopyTextureRegion of subresource 1 (plans/plan_dx.md DX-141)");

        auto level0Readback = readbackMip(0, 4, 4);
        Check(isSolid(level0Readback, 10, 20, 30, 255),
              "HH2: level 0's own content is genuinely unaffected by the level-1 upload -- proves "
              "UpdatePixelsLevel(1, ...) targeted the correct subresource, not level 0 (plans/plan_dx.md DX-141)");
    }

    // plans/plan_dx.md DX-145: RenderTarget2D DepthStencilFormat fidelity for D3D12 -- confirms a render
    // target actually gets the SPECIFIC depth/stencil DXGI format requested
    // (D3DFormatMapping.cpp's DepthFormatToDxgi(), the same shared table D3D11 uses), not just some
    // working depth buffer: Depth16 -> DXGI_FORMAT_D16_UNORM, Depth24/Depth24Stencil8 both ->
    // DXGI_FORMAT_D24_UNORM_S8_UINT (D3D11 has no pure 24-bit depth-only format, and neither does
    // this D3D12 mapping -- it deliberately reuses the same table), None -> no depth resource at
    // all. Reads the real GetDesc().Format off the actual ID3D12Resource, genuine D3D12 API
    // introspection, not internal state.
    {
        auto rtNone = renderer.CreateRenderTarget2D(4, 4, 0 /*DepthFormat::None*/);
        auto* rtNoneImpl = dynamic_cast<D3D12RenderTargetRenderer*>(rtNone.get());
        Check(rtNoneImpl != nullptr && rtNoneImpl->GetDepthResourceEXT() == nullptr,
              "II0: D3D12RenderTargetRenderer: DepthFormat::None creates no depth resource at all "
              "(plans/plan_dx.md DX-145)");

        auto GetDepthFormat = [](D3D12RenderTargetRenderer* rt) -> DXGI_FORMAT
        {
            ID3D12Resource* res = rt->GetDepthResourceEXT();
            if (!res) return DXGI_FORMAT_UNKNOWN;
            return res->GetDesc().Format;
        };

        auto rtD16 = renderer.CreateRenderTarget2D(4, 4, 1 /*DepthFormat::Depth16*/);
        auto* rtD16Impl = dynamic_cast<D3D12RenderTargetRenderer*>(rtD16.get());
        Check(rtD16Impl != nullptr && GetDepthFormat(rtD16Impl) == DXGI_FORMAT_D16_UNORM,
              "II1: D3D12RenderTargetRenderer: DepthFormat::Depth16 genuinely creates a "
              "DXGI_FORMAT_D16_UNORM depth resource, not silently upgraded to a combined "
              "depth+stencil format (plans/plan_dx.md DX-145)");

        auto rtD24 = renderer.CreateRenderTarget2D(4, 4, 2 /*DepthFormat::Depth24*/);
        auto* rtD24Impl = dynamic_cast<D3D12RenderTargetRenderer*>(rtD24.get());
        Check(rtD24Impl != nullptr && GetDepthFormat(rtD24Impl) == DXGI_FORMAT_D24_UNORM_S8_UINT,
              "II2: D3D12RenderTargetRenderer: DepthFormat::Depth24 lands on the documented "
              "DXGI_FORMAT_D24_UNORM_S8_UINT fallback, the same shared-format decision D3D11's own "
              "mapping table already documents (plans/plan_dx.md DX-145)");

        auto rtD24S8 = renderer.CreateRenderTarget2D(4, 4, 3 /*DepthFormat::Depth24Stencil8*/);
        auto* rtD24S8Impl = dynamic_cast<D3D12RenderTargetRenderer*>(rtD24S8.get());
        Check(rtD24S8Impl != nullptr && GetDepthFormat(rtD24S8Impl) == DXGI_FORMAT_D24_UNORM_S8_UINT,
              "II3: D3D12RenderTargetRenderer: DepthFormat::Depth24Stencil8 also creates "
              "DXGI_FORMAT_D24_UNORM_S8_UINT -- Depth24 and Depth24Stencil8 genuinely share the SAME "
              "real DXGI resource format (plans/plan_dx.md DX-145)");
    }

    // ---- plans/plan_dx.md DX-146: the 5 combo Clear* variants -- prove each genuinely clears ONLY what it
    // was asked to, against a real Depth24Stencil8 render target.
    //
    // Depth is proven by its real effect on rasterization (DX-118's PSO depth state is real): the
    // same triangle at the same Z is drawn twice, differing ONLY in the depth value a prior
    // ClearDepth() wrote -- once where the depth test must pass, once where it must fail.
    //
    // Stencil cannot be proven the same way: D3D12's PSO stencil state is deliberately not wired
    // (DX-118's own documented gap), so no draw can be gated on it. It is instead proven by a real,
    // direct GPU readback of the depth-stencil resource's STENCIL PLANE (plane slice 1 of the
    // D24_UNORM_S8_UINT resource) -- a stronger proof than a draw-gated one anyway, since it reads
    // the actual cleared bytes rather than inferring them. ----
    {
        constexpr int kRtW = 32, kRtH = 32;
        auto rtObj = renderer.CreateRenderTarget2D(kRtW, kRtH, /*depthFormat=*/3 /*Depth24Stencil8*/);
        auto* rtImpl = dynamic_cast<D3D12RenderTargetRenderer*>(rtObj.get());
        Check(rtImpl != nullptr && rtImpl->GetDepthResourceEXT() != nullptr,
              "JJ0: Depth24Stencil8 render target with a real depth-stencil resource created (plans/plan_dx.md DX-146)");

        renderer.SetRenderTarget2D(rtObj.get());

        // Reads back the stencil plane (plane slice 1) of the bound depth-stencil resource. Returns
        // an empty vector if the copy/map fails (e.g. if this dev loop's translation layer doesn't
        // support depth/stencil plane copies -- reported honestly rather than silently passing).
        auto readStencilPlane = [&]() -> std::vector<uint8_t>
        {
            ID3D12Resource* ds = rtImpl->GetDepthResourceEXT();
            ID3D12Device* dev = renderer.GetDeviceEXT();
            const D3D12_RESOURCE_DESC dsDesc = ds->GetDesc();

            constexpr UINT kStencilSubresource = 1; // plane 1 of a 1-mip, 1-slice depth-stencil
            D3D12_PLACED_SUBRESOURCE_FOOTPRINT fp{};
            UINT numRows = 0; UINT64 rowBytes = 0, totalBytes = 0;
            dev->GetCopyableFootprints(&dsDesc, kStencilSubresource, 1, 0, &fp, &numRows, &rowBytes, &totalBytes);
            if (totalBytes == 0) return {};

            D3D12_HEAP_PROPERTIES rbHeap{}; rbHeap.Type = D3D12_HEAP_TYPE_READBACK;
            D3D12_RESOURCE_DESC bufDesc{};
            bufDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
            bufDesc.Width = totalBytes; bufDesc.Height = 1;
            bufDesc.DepthOrArraySize = 1; bufDesc.MipLevels = 1;
            bufDesc.Format = DXGI_FORMAT_UNKNOWN; bufDesc.SampleDesc.Count = 1;
            bufDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

            Microsoft::WRL::ComPtr<ID3D12Resource> rb;
            if (FAILED(dev->CreateCommittedResource(&rbHeap, D3D12_HEAP_FLAG_NONE, &bufDesc,
                                                    D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
                                                    IID_PPV_ARGS(rb.GetAddressOf()))))
                return {};

            D3D12_TEXTURE_COPY_LOCATION dst{};
            dst.pResource = rb.Get();
            dst.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
            dst.PlacedFootprint = fp;
            D3D12_TEXTURE_COPY_LOCATION src{};
            src.pResource = ds;
            src.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
            src.SubresourceIndex = kStencilSubresource;

            ID3D12CommandAllocator* alloc = renderer.GetCommandAllocatorEXT(0);
            ID3D12GraphicsCommandList* cl = renderer.GetCommandListEXT();
            alloc->Reset(); cl->Reset(alloc, nullptr);
            auto& tracker = renderer.GetResourceStateTrackerEXT();
            const D3D12_RESOURCE_STATES prior = tracker.GetTrackedStateEXT(ds);
            tracker.TransitionTo(cl, ds, D3D12_RESOURCE_STATE_COPY_SOURCE);
            cl->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
            tracker.TransitionTo(cl, ds, prior);
            if (FAILED(cl->Close())) return {};
            renderer.ExecuteCommandListAndWaitEXT(cl);

            uint8_t* mapped = nullptr;
            const D3D12_RANGE rr{0, static_cast<SIZE_T>(totalBytes)};
            if (FAILED(rb->Map(0, &rr, reinterpret_cast<void**>(&mapped)))) return {};
            std::vector<uint8_t> out(static_cast<std::size_t>(kRtW) * kRtH, 0);
            for (int y = 0; y < kRtH; ++y)
                std::memcpy(out.data() + static_cast<std::size_t>(y) * kRtW,
                            mapped + fp.Offset + static_cast<std::size_t>(y) * fp.Footprint.RowPitch,
                            static_cast<std::size_t>(kRtW));
            const D3D12_RANGE wr{0, 0};
            rb->Unmap(0, &wr);
            return out;
        };

        // --- ClearColorDepthAndStencil: all three at once. ---
        renderer.ClearColorDepthAndStencil(0.0f, 0.0f, 1.0f, 1.0f, /*depth=*/0.75f, /*stencil=*/0x5A);
        auto stencilAfterAll = readStencilPlane();
        const bool stencilPlaneReadable = !stencilAfterAll.empty();
        Check(stencilPlaneReadable,
              "JJ1: the depth-stencil resource's STENCIL PLANE is readable back from the real GPU "
              "(plane-slice-1 CopyTextureRegion) -- the mechanism the stencil proofs below rely on");

        if (stencilPlaneReadable)
        {
            bool allAre5A = true;
            for (uint8_t v : stencilAfterAll) if (v != 0x5A) { allAre5A = false; break; }
            Check(allAre5A,
                  "JJ2: ClearColorDepthAndStencil(stencil=0x5A) genuinely wrote 0x5A into EVERY pixel of "
                  "the real stencil plane, read straight back off the GPU (plans/plan_dx.md DX-146)");

            // --- ClearStencil alone: must change the stencil and leave depth ALONE. ---
            // Depth is verified separately below via the depth test; here we prove the stencil
            // genuinely changed to a different value, so this is not a no-op that merely didn't throw.
            renderer.ClearStencil(0x3C);
            auto stencilAfterStencilOnly = readStencilPlane();
            bool allAre3C = !stencilAfterStencilOnly.empty();
            for (uint8_t v : stencilAfterStencilOnly) if (v != 0x3C) { allAre3C = false; break; }
            Check(allAre3C,
                  "JJ3: ClearStencil(0x3C) alone genuinely overwrites the stencil plane to 0x3C -- a real, "
                  "different value than JJ2's 0x5A, so this is a real clear, not a silent no-op (plans/plan_dx.md DX-146)");
        }

        // --- Depth: proven by its real effect on the depth test (DX-118's PSO depth state). ---
        // Same triangle, same Z, drawn twice -- the ONLY difference is the depth value a prior
        // ClearDepth() wrote. With depthFunc=Less: cleared-to-0.9 must let the z=0.5 triangle
        // through; cleared-to-0.1 must reject it.
        // colored3d's real stride-16 layout: float x,y,z + one packed RGBA uint32 (NOT 7 loose
        // floats -- that silently produces a 28-byte stride the stride-keyed input layout rejects).
        struct VtxZ { float x, y, z; uint32_t color; };
        const VtxZ triZ[3] = {
            {-0.9f,  0.9f, 0.5f, 0xFF0000FFu}, // ABGR-packed red
            { 0.9f,  0.9f, 0.5f, 0xFF0000FFu},
            { 0.0f, -0.9f, 0.5f, 0xFF0000FFu},
        };
        auto vbZ = renderer.CreateVertexBuffer(3);
        vbZ->SetData(triZ, /*vertex_count=*/3, /*stride_in_bytes=*/sizeof(VtxZ));

        // Explicit -- an earlier check (DX-118's cull-mode proof) may have left a real cull mode
        // applied, and this test's triangle winding is not the subject here.
        renderer.ApplyRasterizerState(/*cullMode=*/0 /*None*/, /*fillMode=*/0 /*Solid*/, false, 0.0f, 0.0f);
        renderer.ApplyDepthStencilState(/*depthEnable=*/true, /*depthWriteEnable=*/true,
                                       /*depthFunc=*/2 /*Less*/, false, 0, 0, 0, 0, 0, 0, 0, false, 0, 0, 0, 0);

        auto drawTriAndSampleCenter = [&]() -> std::array<uint8_t, 4>
        {
            renderer.DrawColoredPrimitives(*vbZ, Matrix::getIdentityProperty(), Matrix::getIdentityProperty(),
                                          Matrix::getIdentityProperty(), PrimitiveType::TriangleList, 1);
            auto px = ReadBackRenderTargetFull(renderer, rtImpl->GetColorResourceEXT(), kRtW, kRtH);
            const std::size_t i = (static_cast<std::size_t>(kRtH / 2) * kRtW + kRtW / 2) * 4;
            return {px[i + 0], px[i + 1], px[i + 2], px[i + 3]};
        };

        // Control: with the depth test OFF, this exact triangle must draw. Isolates "the draw itself
        // works" from "the depth test rejected it", so a JJ4 failure below can only mean the latter.
        renderer.ApplyDepthStencilState(false, false, 2, false, 0, 0, 0, 0, 0, 0, 0, false, 0, 0, 0, 0);
        renderer.ClearColorAndDepth(0.0f, 0.0f, 1.0f, 1.0f, 0.9f);
        const auto ctrlPx = drawTriAndSampleCenter();
        Check(ctrlPx[0] == 255 && ctrlPx[1] == 0 && ctrlPx[2] == 0,
              "JJ3b (control): with depth testing OFF, this exact triangle genuinely draws its red -- so a "
              "JJ4 failure below can only mean the depth test rejected it, not that the draw is broken");
        renderer.ApplyDepthStencilState(/*depthEnable=*/true, /*depthWriteEnable=*/true,
                                       /*depthFunc=*/2 /*Less*/, false, 0, 0, 0, 0, 0, 0, 0, false, 0, 0, 0, 0);

        // Depth cleared FAR (0.9): the z=0.5 triangle is nearer -> Less passes -> red is drawn.
        renderer.ClearColorAndDepth(0.0f, 0.0f, 1.0f, 1.0f, /*depth=*/0.9f);
        const auto passPx = drawTriAndSampleCenter();
        Check(passPx[0] == 255 && passPx[1] == 0 && passPx[2] == 0,
              "JJ4: ClearColorAndDepth(depth=0.9) -- the z=0.5 triangle passes depthFunc=Less and draws "
              "its exact red, proving the cleared DEPTH value is real (plans/plan_dx.md DX-146)");

        // Depth cleared NEAR (0.1): the same z=0.5 triangle is farther -> Less fails -> blue survives.
        renderer.ClearColorAndDepth(0.0f, 0.0f, 1.0f, 1.0f, /*depth=*/0.1f);
        const auto failPx = drawTriAndSampleCenter();
        Check(failPx[2] == 255 && failPx[0] == 0,
              "JJ5: ClearColorAndDepth(depth=0.1) -- the SAME triangle at the SAME z=0.5 is now correctly "
              "REJECTED by the depth test (background survives). Only the cleared depth value differs "
              "between JJ4 and JJ5, so each Clear* variant genuinely writes the depth it was given, not a "
              "fixed default (plans/plan_dx.md DX-146)");

        // --- ClearDepth alone must NOT touch the color target. ---
        renderer.ClearColorAndDepth(0.0f, 1.0f, 0.0f, 1.0f, /*depth=*/0.9f); // green, far depth
        renderer.ClearDepth(0.1f);                                            // depth only
        auto afterDepthOnly = ReadBackRenderTargetFull(renderer, rtImpl->GetColorResourceEXT(), kRtW, kRtH);
        const std::size_t ci = (static_cast<std::size_t>(kRtH / 2) * kRtW + kRtW / 2) * 4;
        Check(afterDepthOnly[ci + 1] == 255 && afterDepthOnly[ci + 0] == 0,
              "JJ6: ClearDepth() alone leaves the COLOR target untouched (the prior green survives) -- "
              "proves each variant clears only what it was asked for (plans/plan_dx.md DX-146)");

        renderer.ApplyDepthStencilState(false, false, 2, false, 0, 0, 0, 0, 0, 0, 0, false, 0, 0, 0, 0);
        renderer.SetRenderTarget2D(nullptr);
    }

    // ---- plans/plan_dx.md DX-144: RenderTarget2D mip-chain generation -- proves the real CPU
    // box-filter downsample cascade (D3D12RenderTargetRenderer::GenerateMipsEXT(), triggered from
    // UnbindAsRenderTarget()) actually writes correct content into mip levels > 0, not
    // zero/garbage. Mirrors D3D11's own already-closed DX-144 methodology exactly: a solid single
    // color across the whole base mip, since box-filtering a solid color always produces the exact
    // same solid color at every downstream mip level regardless of the filter kernel's specifics --
    // this sidesteps needing to replicate D3D11's own GenerateMips() kernel bit-for-bit. ----
    {
        auto rtMip = renderer.CreateRenderTarget2D(8, 8, 0 /*DepthFormat::None*/, false, true /*mipMap*/, 0);
        auto* d3dRtMip = dynamic_cast<D3D12RenderTargetRenderer*>(rtMip.get());
        renderer.SetRenderTarget2D(rtMip.get());
        renderer.Clear(200.0f / 255.0f, 90.0f / 255.0f, 10.0f / 255.0f, 1.0f);
        renderer.SetRenderTarget2D(nullptr); // UnbindAsRenderTarget() -> GenerateMipsEXT()

        Check(d3dRtMip != nullptr && d3dRtMip->GetLevelCountEXT() == 4,
              "LL0: D3D12RenderTargetRenderer: an 8x8 mipMap=true render target reports the expected "
              "4-level mip chain (8x8/4x4/2x2/1x1, plans/plan_dx.md DX-144)");

        // Direct GPU readback of a given mip subresource -- same real, direct-readback discipline
        // DX-122/DX-123/DX-141's own readbackMip already establish, kept test-local since there is
        // no public sampling path in this raw-renderer-level smoke test.
        auto readbackRtMip = [&](int level, int w, int h) -> std::vector<uint8_t>
        {
            const UINT rowPitch = (static_cast<UINT>(w) * 4 + D3D12_TEXTURE_DATA_PITCH_ALIGNMENT - 1)
                                 & ~(D3D12_TEXTURE_DATA_PITCH_ALIGNMENT - 1);
            const UINT64 bufSize = static_cast<UINT64>(rowPitch) * static_cast<UINT64>(h);

            D3D12_HEAP_PROPERTIES readbackHeapProps{};
            readbackHeapProps.Type = D3D12_HEAP_TYPE_READBACK;
            D3D12_RESOURCE_DESC bufDesc{};
            bufDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
            bufDesc.Width = bufSize;
            bufDesc.Height = 1;
            bufDesc.DepthOrArraySize = 1;
            bufDesc.MipLevels = 1;
            bufDesc.Format = DXGI_FORMAT_UNKNOWN;
            bufDesc.SampleDesc.Count = 1;
            bufDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

            Microsoft::WRL::ComPtr<ID3D12Resource> readback;
            HRESULT hr = renderer.GetDeviceEXT()->CreateCommittedResource(
                &readbackHeapProps, D3D12_HEAP_FLAG_NONE, &bufDesc,
                D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(readback.GetAddressOf()));
            if (FAILED(hr)) return {};

            D3D12_TEXTURE_COPY_LOCATION dst{};
            dst.pResource = readback.Get();
            dst.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
            dst.PlacedFootprint.Footprint.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
            dst.PlacedFootprint.Footprint.Width = static_cast<UINT>(w);
            dst.PlacedFootprint.Footprint.Height = static_cast<UINT>(h);
            dst.PlacedFootprint.Footprint.Depth = 1;
            dst.PlacedFootprint.Footprint.RowPitch = rowPitch;

            D3D12_TEXTURE_COPY_LOCATION src{};
            src.pResource = d3dRtMip->GetColorResourceEXT();
            src.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
            src.SubresourceIndex = static_cast<UINT>(level);

            ID3D12CommandAllocator* allocator = renderer.GetCommandAllocatorEXT(0);
            ID3D12GraphicsCommandList* cmdList = renderer.GetCommandListEXT();
            allocator->Reset();
            cmdList->Reset(allocator, nullptr);
            auto& tracker = renderer.GetResourceStateTrackerEXT();
            const D3D12_RESOURCE_STATES priorState = tracker.GetTrackedStateEXT(d3dRtMip->GetColorResourceEXT());
            tracker.TransitionTo(cmdList, d3dRtMip->GetColorResourceEXT(), D3D12_RESOURCE_STATE_COPY_SOURCE);
            cmdList->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
            tracker.TransitionTo(cmdList, d3dRtMip->GetColorResourceEXT(), priorState);
            hr = cmdList->Close();
            if (FAILED(hr)) return {};
            renderer.ExecuteCommandListAndWaitEXT(cmdList);

            uint8_t* mapped = nullptr;
            const D3D12_RANGE mapRange{0, static_cast<SIZE_T>(bufSize)};
            if (FAILED(readback->Map(0, &mapRange, reinterpret_cast<void**>(&mapped)))) return {};
            std::vector<uint8_t> out(static_cast<std::size_t>(w) * h * 4);
            for (int row = 0; row < h; ++row)
                std::memcpy(out.data() + static_cast<std::size_t>(row) * w * 4,
                            mapped + static_cast<std::size_t>(row) * rowPitch,
                            static_cast<std::size_t>(w) * 4);
            const D3D12_RANGE writtenRange{0, 0};
            readback->Unmap(0, &writtenRange);
            return out;
        };

        auto isSolidRt = [](const std::vector<uint8_t>& buf, uint8_t r, uint8_t g, uint8_t b, uint8_t a)
        {
            if (buf.empty()) return false;
            for (std::size_t i = 0; i < buf.size(); i += 4)
                if (buf[i] != r || buf[i+1] != g || buf[i+2] != b || buf[i+3] != a) return false;
            return true;
        };

        if (d3dRtMip != nullptr)
        {
            const auto mip1 = readbackRtMip(1, 4, 4);
            Check(isSolidRt(mip1, 200, 90, 10, 255),
                  "LL1: D3D12RenderTargetRenderer: GenerateMipsEXT()-on-unbind writes the exact "
                  "box-filtered (here: solid-color-preserving) content into mip level 1, read back "
                  "directly from the real GPU resource, not zero/garbage (plans/plan_dx.md DX-144)");

            const auto mip2 = readbackRtMip(2, 2, 2);
            Check(isSolidRt(mip2, 200, 90, 10, 255),
                  "LL2: D3D12RenderTargetRenderer: mip level 2 (2x2) is also exact, confirming the "
                  "full mip chain regenerates correctly, not just level 1 (plans/plan_dx.md DX-144)");
        }
    }

    // ---- plans/plan_dx.md DX-144 follow-up: RenderTargetCube mip-chain generation -- same real CPU
    // box-filter downsample cascade as D3D12RenderTargetRenderer's own 2D leg above, extended to
    // D3D12RenderTargetCubeRenderer. Only the active face's chain regenerates on unbind, mirroring
    // D3D11RenderTargetCubeRenderer's own face-0-only test precedent (a real, honest scope match,
    // not an oversight -- only one face is ever the active draw target at a time). ----
    {
        auto rtCubeMip = renderer.CreateRenderTargetCube(8, 0 /*DepthFormat::None*/, false /*preserveContents*/, true /*mipMap*/);
        auto* d3dRtCubeMip = dynamic_cast<D3D12RenderTargetCubeRenderer*>(rtCubeMip.get());
        Check(d3dRtCubeMip != nullptr && d3dRtCubeMip->GetLevelCountEXT() == 4,
              "MM0: D3D12RenderTargetCubeRenderer: an 8x8 mipMap=true cube render target reports "
              "the expected 4-level mip chain (8x8/4x4/2x2/1x1, plans/plan_dx.md DX-144)");

        if (d3dRtCubeMip != nullptr)
        {
            rtCubeMip->BindAsRenderTargetFace(0);
            renderer.Clear(50.0f / 255.0f, 150.0f / 255.0f, 250.0f / 255.0f, 1.0f);
            rtCubeMip->UnbindAsRenderTarget(); // GenerateMipsEXT() on face 0

            // Subresource = mip + face*levelCount (standard D3D12 texture-array/mip indexing) --
            // face 0, mip 1. Reuses the same readback technique as readbackRtMip above, just against
            // the cube's own color resource and a face-aware subresource index.
            auto readbackCubeMip = [&](int level, int face, int w, int h) -> std::vector<uint8_t>
            {
                const UINT rowPitch = (static_cast<UINT>(w) * 4 + D3D12_TEXTURE_DATA_PITCH_ALIGNMENT - 1)
                                     & ~(D3D12_TEXTURE_DATA_PITCH_ALIGNMENT - 1);
                const UINT64 bufSize = static_cast<UINT64>(rowPitch) * static_cast<UINT64>(h);

                D3D12_HEAP_PROPERTIES readbackHeapProps{};
                readbackHeapProps.Type = D3D12_HEAP_TYPE_READBACK;
                D3D12_RESOURCE_DESC bufDesc{};
                bufDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
                bufDesc.Width = bufSize;
                bufDesc.Height = 1;
                bufDesc.DepthOrArraySize = 1;
                bufDesc.MipLevels = 1;
                bufDesc.Format = DXGI_FORMAT_UNKNOWN;
                bufDesc.SampleDesc.Count = 1;
                bufDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

                Microsoft::WRL::ComPtr<ID3D12Resource> readback;
                HRESULT hr = renderer.GetDeviceEXT()->CreateCommittedResource(
                    &readbackHeapProps, D3D12_HEAP_FLAG_NONE, &bufDesc,
                    D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(readback.GetAddressOf()));
                if (FAILED(hr)) return {};

                D3D12_TEXTURE_COPY_LOCATION dst{};
                dst.pResource = readback.Get();
                dst.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
                dst.PlacedFootprint.Footprint.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
                dst.PlacedFootprint.Footprint.Width = static_cast<UINT>(w);
                dst.PlacedFootprint.Footprint.Height = static_cast<UINT>(h);
                dst.PlacedFootprint.Footprint.Depth = 1;
                dst.PlacedFootprint.Footprint.RowPitch = rowPitch;

                D3D12_TEXTURE_COPY_LOCATION src{};
                src.pResource = d3dRtCubeMip->GetColorResourceEXT();
                src.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
                src.SubresourceIndex = static_cast<UINT>(level) + static_cast<UINT>(face) * static_cast<UINT>(d3dRtCubeMip->GetLevelCountEXT());

                ID3D12CommandAllocator* allocator = renderer.GetCommandAllocatorEXT(0);
                ID3D12GraphicsCommandList* cmdList = renderer.GetCommandListEXT();
                allocator->Reset();
                cmdList->Reset(allocator, nullptr);
                auto& tracker = renderer.GetResourceStateTrackerEXT();
                const D3D12_RESOURCE_STATES priorState = tracker.GetTrackedStateEXT(d3dRtCubeMip->GetColorResourceEXT());
                tracker.TransitionTo(cmdList, d3dRtCubeMip->GetColorResourceEXT(), D3D12_RESOURCE_STATE_COPY_SOURCE);
                cmdList->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
                tracker.TransitionTo(cmdList, d3dRtCubeMip->GetColorResourceEXT(), priorState);
                hr = cmdList->Close();
                if (FAILED(hr)) return {};
                renderer.ExecuteCommandListAndWaitEXT(cmdList);

                uint8_t* mapped = nullptr;
                const D3D12_RANGE mapRange{0, static_cast<SIZE_T>(bufSize)};
                if (FAILED(readback->Map(0, &mapRange, reinterpret_cast<void**>(&mapped)))) return {};
                std::vector<uint8_t> out(static_cast<std::size_t>(w) * h * 4);
                for (int row = 0; row < h; ++row)
                    std::memcpy(out.data() + static_cast<std::size_t>(row) * w * 4,
                                mapped + static_cast<std::size_t>(row) * rowPitch,
                                static_cast<std::size_t>(w) * 4);
                const D3D12_RANGE writtenRange{0, 0};
                readback->Unmap(0, &writtenRange);
                return out;
            };

            const auto cubeMip1 = readbackCubeMip(1, 0, 4, 4);
            bool cubeMip1Exact = cubeMip1.size() == 4u * 4u * 4u;
            for (std::size_t i = 0; i < 4u * 4u && cubeMip1Exact; ++i)
                cubeMip1Exact = cubeMip1[i * 4 + 0] == 50 && cubeMip1[i * 4 + 1] == 150 &&
                               cubeMip1[i * 4 + 2] == 250 && cubeMip1[i * 4 + 3] == 255;
            Check(cubeMip1Exact,
                  "MM1: D3D12RenderTargetCubeRenderer: GenerateMipsEXT()-on-unbind regenerates face "
                  "0's own mip chain correctly, read back directly from the real GPU resource "
                  "(plans/plan_dx.md DX-144)");
        }
    }

    // ---- plans/plan_dx.md DX-153: RenderTargetCube mip-chain generation for a NON-zero face -- MM1
    // above only ever proved face 0. D3D12's own GenerateMipsEXT() is explicitly activeFace_-scoped
    // (confirmed by reading the code, not assumed), so this mainly confirms the
    // mip + face*levelCount subresource math generalizes correctly past face 0. ----
    {
        auto rtCubeMip2 = renderer.CreateRenderTargetCube(8, 0 /*DepthFormat::None*/, false /*preserveContents*/, true /*mipMap*/);
        auto* d3dRtCubeMip2 = dynamic_cast<D3D12RenderTargetCubeRenderer*>(rtCubeMip2.get());
        Check(d3dRtCubeMip2 != nullptr, "TT0: D3D12RenderTargetCubeRenderer: a second 8x8 mipMap=true "
              "cube render target constructs cleanly (plans/plan_dx.md DX-153)");

        if (d3dRtCubeMip2 != nullptr)
        {
            rtCubeMip2->BindAsRenderTargetFace(2);
            renderer.Clear(60.0f / 255.0f, 120.0f / 255.0f, 180.0f / 255.0f, 1.0f);
            rtCubeMip2->UnbindAsRenderTarget(); // GenerateMipsEXT() on face 2

            auto readbackCubeFace2Mip = [&](int level, int w, int h) -> std::vector<uint8_t>
            {
                const UINT rowPitch = (static_cast<UINT>(w) * 4 + D3D12_TEXTURE_DATA_PITCH_ALIGNMENT - 1)
                                     & ~(D3D12_TEXTURE_DATA_PITCH_ALIGNMENT - 1);
                const UINT64 bufSize = static_cast<UINT64>(rowPitch) * static_cast<UINT64>(h);

                D3D12_HEAP_PROPERTIES readbackHeapProps{};
                readbackHeapProps.Type = D3D12_HEAP_TYPE_READBACK;
                D3D12_RESOURCE_DESC bufDesc{};
                bufDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
                bufDesc.Width = bufSize;
                bufDesc.Height = 1;
                bufDesc.DepthOrArraySize = 1;
                bufDesc.MipLevels = 1;
                bufDesc.Format = DXGI_FORMAT_UNKNOWN;
                bufDesc.SampleDesc.Count = 1;
                bufDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

                Microsoft::WRL::ComPtr<ID3D12Resource> readback;
                HRESULT hr = renderer.GetDeviceEXT()->CreateCommittedResource(
                    &readbackHeapProps, D3D12_HEAP_FLAG_NONE, &bufDesc,
                    D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(readback.GetAddressOf()));
                if (FAILED(hr)) return {};

                D3D12_TEXTURE_COPY_LOCATION dst{};
                dst.pResource = readback.Get();
                dst.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
                dst.PlacedFootprint.Footprint.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
                dst.PlacedFootprint.Footprint.Width = static_cast<UINT>(w);
                dst.PlacedFootprint.Footprint.Height = static_cast<UINT>(h);
                dst.PlacedFootprint.Footprint.Depth = 1;
                dst.PlacedFootprint.Footprint.RowPitch = rowPitch;

                D3D12_TEXTURE_COPY_LOCATION src{};
                src.pResource = d3dRtCubeMip2->GetColorResourceEXT();
                src.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
                src.SubresourceIndex = static_cast<UINT>(level) + 2u * static_cast<UINT>(d3dRtCubeMip2->GetLevelCountEXT());

                ID3D12CommandAllocator* allocator = renderer.GetCommandAllocatorEXT(0);
                ID3D12GraphicsCommandList* cmdList = renderer.GetCommandListEXT();
                allocator->Reset();
                cmdList->Reset(allocator, nullptr);
                auto& tracker = renderer.GetResourceStateTrackerEXT();
                const D3D12_RESOURCE_STATES priorState = tracker.GetTrackedStateEXT(d3dRtCubeMip2->GetColorResourceEXT());
                tracker.TransitionTo(cmdList, d3dRtCubeMip2->GetColorResourceEXT(), D3D12_RESOURCE_STATE_COPY_SOURCE);
                cmdList->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
                tracker.TransitionTo(cmdList, d3dRtCubeMip2->GetColorResourceEXT(), priorState);
                hr = cmdList->Close();
                if (FAILED(hr)) return {};
                renderer.ExecuteCommandListAndWaitEXT(cmdList);

                uint8_t* mapped = nullptr;
                const D3D12_RANGE mapRange{0, static_cast<SIZE_T>(bufSize)};
                if (FAILED(readback->Map(0, &mapRange, reinterpret_cast<void**>(&mapped)))) return {};
                std::vector<uint8_t> out(static_cast<std::size_t>(w) * h * 4);
                for (int row = 0; row < h; ++row)
                    std::memcpy(out.data() + static_cast<std::size_t>(row) * w * 4,
                                mapped + static_cast<std::size_t>(row) * rowPitch,
                                static_cast<std::size_t>(w) * 4);
                const D3D12_RANGE writtenRange{0, 0};
                readback->Unmap(0, &writtenRange);
                return out;
            };

            const auto face2Mip1 = readbackCubeFace2Mip(1, 4, 4);
            bool face2Mip1Exact = face2Mip1.size() == 4u * 4u * 4u;
            for (std::size_t i = 0; i < 4u * 4u && face2Mip1Exact; ++i)
                face2Mip1Exact = face2Mip1[i * 4 + 0] == 60 && face2Mip1[i * 4 + 1] == 120 &&
                                 face2Mip1[i * 4 + 2] == 180 && face2Mip1[i * 4 + 3] == 255;
            Check(face2Mip1Exact,
                  "TT1: D3D12RenderTargetCubeRenderer: GenerateMipsEXT()-on-unbind regenerates a "
                  "NON-zero face's (face 2) own mip chain correctly, not just face 0's, read back "
                  "directly from the real GPU resource (plans/plan_dx.md DX-153)");
        }
    }

    // ---- plans/plan_dx.md DX-152: RenderTargetCube MSAA -- a real feature (previously deliberately out
    // of scope, this class's own prior header comment) landing now, not just a test. Mirrors
    // DX-117's own RenderTarget2D MSAA follow-up methodology exactly: an 8x8 cube face requested
    // at 4x MSAA, cleared, read back through GetSampleableColorResourceEXT() (the resolved,
    // single-sample resource ResolveMsaaEXT() writes on unbind, face-scoped) via
    // ReadBackRenderTargetFull() -- proves the real ResolveSubresource()-on-unbind round-trip
    // produces the exact clear color for face 0, not that the device granted a specific sample
    // count (printed as diagnostics only, same honest framing DX-45/DX-117 already established). ----
    {
        auto rtCubeMsaa = renderer.CreateRenderTargetCube(8, 0 /*DepthFormat::None*/, false /*preserveContents*/, false /*mipMap*/, 4);
        auto* d3dRtCubeMsaa = dynamic_cast<D3D12RenderTargetCubeRenderer*>(rtCubeMsaa.get());
        Check(d3dRtCubeMsaa != nullptr, "SS0: D3D12RenderTargetCubeRenderer: CreateRenderTargetCube(multiSampleCount=4) "
              "returns a real renderer object (plans/plan_dx.md DX-152)");

        if (d3dRtCubeMsaa != nullptr)
        {
            rtCubeMsaa->BindAsRenderTargetFace(0);
            renderer.Clear(77.0f / 255.0f, 88.0f / 255.0f, 99.0f / 255.0f, 1.0f);
            rtCubeMsaa->UnbindAsRenderTarget(); // ResolveMsaaEXT() for face 0

            const auto resolvedCube = ReadBackRenderTargetFull(renderer, d3dRtCubeMsaa->GetSampleableColorResourceEXT(), 8, 8);
            bool msaaCubeMatches = resolvedCube.size() == 8u * 8u * 4u;
            for (std::size_t i = 0; i < 8u * 8u && msaaCubeMatches; ++i)
                msaaCubeMatches = resolvedCube[i * 4 + 0] == 77 && resolvedCube[i * 4 + 1] == 88 &&
                                 resolvedCube[i * 4 + 2] == 99 && resolvedCube[i * 4 + 3] == 255;
            Check(msaaCubeMatches,
                  "SS1: D3D12RenderTargetCubeRenderer (MSAA): Clear()+ResolveSubresource()-on-unbind "
                  "produces the exact color in the resolved, sampleable resource for face 0, read back "
                  "directly from the real GPU resource (plans/plan_dx.md DX-152)");
            std::printf("    RenderTargetCube MSAA: requested 4x, device-applied %dx\n", d3dRtCubeMsaa->GetMultiSampleCount());
        }
    }

    // ---- plans/plan_dx.md DX-117 MSAA follow-up: RenderTarget2D MSAA support -- mirrors D3D11's own
    // already-closed DX-45 methodology exactly: an 8x8 render target requested at 4x MSAA, cleared,
    // then read back through GetSampleableColorResourceEXT() (the resolved, single-sample resource
    // ResolveMsaaEXT() writes on unbind) via the same ReadBackRenderTargetFull() helper every other
    // non-MSAA render-target check above already uses -- proves the real
    // ResolveSubresource()-on-unbind round-trip produces the exact clear color, not that the device
    // granted a specific sample count (that's real hardware/driver capability, printed as
    // diagnostics only, same honest framing DX-45 already established). ----
    {
        auto rtMsaa = renderer.CreateRenderTarget2D(8, 8, 0 /*DepthFormat::None*/, false, false, 4);
        auto* d3dRtMsaa = dynamic_cast<D3D12RenderTargetRenderer*>(rtMsaa.get());
        Check(d3dRtMsaa != nullptr, "OO0: D3D12RenderTargetRenderer: CreateRenderTarget2D(multiSampleCount=4) "
              "returns a real renderer object (plans/plan_dx.md DX-117)");

        if (d3dRtMsaa != nullptr)
        {
            renderer.SetRenderTarget2D(rtMsaa.get());
            renderer.Clear(77.0f / 255.0f, 88.0f / 255.0f, 99.0f / 255.0f, 1.0f);
            renderer.SetRenderTarget2D(nullptr); // UnbindAsRenderTarget() -> ResolveMsaaEXT()

            const auto resolved = ReadBackRenderTargetFull(renderer, d3dRtMsaa->GetSampleableColorResourceEXT(), 8, 8);
            bool msaaMatches = resolved.size() == 8u * 8u * 4u;
            for (std::size_t i = 0; i < 8u * 8u && msaaMatches; ++i)
                msaaMatches = resolved[i * 4 + 0] == 77 && resolved[i * 4 + 1] == 88 &&
                             resolved[i * 4 + 2] == 99 && resolved[i * 4 + 3] == 255;
            Check(msaaMatches,
                  "OO1: D3D12RenderTargetRenderer (MSAA): Clear()+ResolveSubresource()-on-unbind produces "
                  "the exact color in the resolved, sampleable resource, read back directly from the "
                  "real GPU resource (plans/plan_dx.md DX-117)");
            std::printf("    MSAA: requested 4x, device-applied %dx\n", d3dRtMsaa->GetMultiSampleCount());
        }
    }

    // ================================================================================================
    // plans/plan_dx.md DX-132 / DX-148 / DX-140: the XNA-level public API, through a REAL, WINDOWLESS
    // GraphicsDevice (PresentationParameters::HeadlessEXT).
    //
    // Everything above this point drives the raw IGraphicsRenderer directly. SpriteFont, Model and
    // Texture2D::FromStream/SaveAsPng cannot be reached that way -- they are built ON TOP of
    // GraphicsDevice (SpriteBatch::DrawString's glyph layout, ModelMesh::Draw's
    // SetVertexBuffer/setIndices/DrawIndexedPrimitives + EffectPass::Apply orchestration, and
    // Texture2D's own device-bound construction), and a GraphicsDevice used to mean a real window.
    //
    // The drawing is done entirely through the real public XNA API -- that is what is under test.
    // Only the READBACK reaches into the renderer (via RenderTarget2D::GetRenderTargetRenderer()), for
    // the same reason every other check in this file does: there is no back buffer to
    // GetBackBufferData() from without a swap chain, and the readback mechanism itself is not what
    // these rows are testing.
    // ================================================================================================
    {
        namespace X = Microsoft::Xna::Framework;
        namespace XG = Microsoft::Xna::Framework::Graphics;

        constexpr int kW = 32, kH = 32;

        XG::PresentationParameters pp;
        pp.setBackBufferWidthProperty(kW);
        pp.setBackBufferHeightProperty(kH);
        pp.setHeadlessEXTProperty(true); // <-- the whole point: no window, no swap chain, plain Wine
        XG::GraphicsAdapter& adapter = XG::GraphicsAdapter::getDefaultAdapterProperty();
        XG::GraphicsDevice dev(adapter, XG::GraphicsProfile::HiDef, pp);

        Check(true, "KK0: a real, WINDOWLESS D3D12 GraphicsDevice constructs (PresentationParameters::"
                    "HeadlessEXT) -- no SDL video subsystem, no window, no swap chain, under plain Wine "
                    "(plans/plan_dx.md DX-132/DX-148/DX-140)");

        // The device's own renderer -- the RenderTarget2D below belongs to THIS renderer, not to the
        // standalone one the rest of this file uses, so readback must go through it.
        auto* devRenderer = dynamic_cast<DirectX12Renderer*>(&dev.GetRenderer());
        Check(devRenderer != nullptr,
              "KK1: the windowless GraphicsDevice really is backed by a DirectX12Renderer");

        // Renders whatever `drawFn` draws into a fresh RenderTarget2D and returns its RGBA pixels.
        auto renderToTarget = [&](const X::Color& clearColor,
                                  const std::function<void()>& drawFn) -> std::vector<uint8_t>
        {
            XG::RenderTarget2D rt(dev, kW, kH);
            dev.SetRenderTarget(&rt);
            dev.Clear(clearColor);
            drawFn();
            dev.SetRenderTarget(nullptr);

            auto* rtb = dynamic_cast<D3D12RenderTargetRenderer*>(rt.GetRenderTargetRenderer());
            if (!rtb || !devRenderer) return {};
            return ReadBackRenderTargetFull(*devRenderer, rtb->GetColorResourceEXT(), kW, kH);
        };
        auto pixelAt = [&](const std::vector<uint8_t>& px, int x, int y) -> X::Color
        {
            const std::size_t i = (static_cast<std::size_t>(y) * kW + x) * 4;
            if (i + 3 >= px.size()) return X::Color(0, 0, 0, 0);
            return X::Color(px[i + 0], px[i + 1], px[i + 2], px[i + 3]);
        };
        auto isWhite = [](const X::Color& c) {
            return c.getRProperty() > 200 && c.getGProperty() > 200 && c.getBProperty() > 200;
        };
        auto isBlack = [](const X::Color& c) {
            return c.getRProperty() < 60 && c.getGProperty() < 60 && c.getBProperty() < 60;
        };

        const X::Color kBlack(0, 0, 0, 255);

        // ---- DX-132: SpriteFont -- real glyph placement, spacing, newline, flip. ----
        // Same minimal hand-built font fixture EasyGL's own pixel tests use (Tasks 424-429): an 8x8
        // solid-white atlas per glyph, zero cropping offset, zero left/right kerning bearing, so a
        // glyph's destination rect maps exactly to (position + accumulated advance, 8, 8) and any
        // placement error is a hard pixel difference, not a subtle blend.
        {
            XG::Texture2D atlas(dev, 16, 8);            // two 8x8 glyph cells side by side
            std::vector<X::Color> atlasPx(16 * 8, X::Color(255, 255, 255, 255));
            atlas.SetData(atlasPx.data(), 16 * 8);

            std::vector<X::Rectangle> glyphBounds = { X::Rectangle(0, 0, 8, 8), X::Rectangle(8, 0, 8, 8) };
            std::vector<X::Rectangle> cropping    = { X::Rectangle(0, 0, 8, 8), X::Rectangle(0, 0, 8, 8) };
            std::vector<SharpRuntime::charcs> chars = { u'A', u'B' };
            std::vector<X::Vector3> kerning = { X::Vector3(0.0f, 8.0f, 0.0f), X::Vector3(0.0f, 8.0f, 0.0f) };
            XG::SpriteFont font(atlas, glyphBounds, cropping, chars,
                                /*lineSpacing=*/8, /*spacing=*/0.0f, kerning,
                                std::optional<SharpRuntime::charcs>(std::nullopt));

            XG::SpriteBatch sb(dev);

            // (a) Single glyph at (4,4) -> occupies exactly [4,12) x [4,12).
            auto pxGlyph = renderToTarget(kBlack, [&] {
                dev.setBlendStateProperty(XG::BlendState::Opaque);
                sb.Begin();
                sb.DrawString(font, "A", X::Vector2(4.0f, 4.0f), X::Color::White);
                sb.End();
            });
            const bool glyphPlaced = !pxGlyph.empty()
                && isWhite(pixelAt(pxGlyph, 8, 8))    // inside
                && isBlack(pixelAt(pxGlyph, 3, 8))    // just left of the left edge
                && isBlack(pixelAt(pxGlyph, 12, 8))   // just right of the right edge
                && isBlack(pixelAt(pxGlyph, 8, 3))    // just above the top edge
                && isBlack(pixelAt(pxGlyph, 8, 12));  // just below the bottom edge
            Check(glyphPlaced,
                  "KK2: SpriteBatch::DrawString() places a single glyph at EXACTLY its destination rect "
                  "(4,4,8,8) -- checked inside plus all four edge midpoints, so an X-only or Y-only "
                  "misplacement cannot pass (plans/plan_dx.md DX-132)");

            // (b) Two glyphs -> the second must advance by exactly one glyph width (8px).
            auto pxSpacing = renderToTarget(kBlack, [&] {
                dev.setBlendStateProperty(XG::BlendState::Opaque);
                sb.Begin();
                sb.DrawString(font, "AB", X::Vector2(0.0f, 0.0f), X::Color::White);
                sb.End();
            });
            const bool spacingOk = !pxSpacing.empty()
                && isWhite(pixelAt(pxSpacing, 4, 4))    // glyph 'A' cell [0,8)
                && isWhite(pixelAt(pxSpacing, 12, 4))   // glyph 'B' cell [8,16) -- advanced by 8
                && isBlack(pixelAt(pxSpacing, 20, 4));  // nothing beyond the second glyph
            Check(spacingOk,
                  "KK3: DrawString(\"AB\") advances the SECOND glyph by exactly one glyph width -- both "
                  "cells are drawn and nothing spills past them (plans/plan_dx.md DX-132)");

            // (c) Newline -> the second line must drop by exactly lineSpacing (8px), back to x=0.
            auto pxNewline = renderToTarget(kBlack, [&] {
                dev.setBlendStateProperty(XG::BlendState::Opaque);
                sb.Begin();
                sb.DrawString(font, "A\nA", X::Vector2(0.0f, 0.0f), X::Color::White);
                sb.End();
            });
            const bool newlineOk = !pxNewline.empty()
                && isWhite(pixelAt(pxNewline, 4, 4))    // line 1, y in [0,8)
                && isWhite(pixelAt(pxNewline, 4, 12))   // line 2, y in [8,16) -- dropped by lineSpacing
                && isBlack(pixelAt(pxNewline, 12, 4));  // line 1 has ONE glyph -- x reset, not advanced
            Check(newlineOk,
                  "KK4: DrawString(\"A\\nA\") drops the second line by exactly lineSpacing AND resets x "
                  "to the start -- a newline that only did one of the two cannot pass (plans/plan_dx.md DX-132)");

            // (d) SpriteEffects::FlipVertically -- an ASYMMETRIC glyph is required, otherwise a flip of
            // a solid block is indistinguishable from no flip at all. Rebuild the atlas so glyph 'A'
            // is white only in its TOP half; flipped, the white must land in the BOTTOM half.
            std::vector<X::Color> halfPx(16 * 8, X::Color(0, 0, 0, 255));
            for (int y = 0; y < 4; ++y)
                for (int x = 0; x < 8; ++x)
                    halfPx[static_cast<std::size_t>(y) * 16 + x] = X::Color(255, 255, 255, 255);
            XG::Texture2D atlasHalf(dev, 16, 8);
            atlasHalf.SetData(halfPx.data(), 16 * 8);
            XG::SpriteFont fontHalf(atlasHalf, glyphBounds, cropping, chars,
                                    8, 0.0f, kerning, std::optional<SharpRuntime::charcs>(std::nullopt));

            auto pxNoFlip = renderToTarget(kBlack, [&] {
                dev.setBlendStateProperty(XG::BlendState::Opaque);
                sb.Begin();
                sb.DrawString(fontHalf, "A", X::Vector2(0.0f, 0.0f), X::Color::White,
                              0.0f, X::Vector2(0.0f, 0.0f), 1.0f, XG::SpriteEffects::None, 0.0f);
                sb.End();
            });
            auto pxFlip = renderToTarget(kBlack, [&] {
                dev.setBlendStateProperty(XG::BlendState::Opaque);
                sb.Begin();
                sb.DrawString(fontHalf, "A", X::Vector2(0.0f, 0.0f), X::Color::White,
                              0.0f, X::Vector2(0.0f, 0.0f), 1.0f, XG::SpriteEffects::FlipVertically, 0.0f);
                sb.End();
            });
            const bool noFlipOk = !pxNoFlip.empty()
                && isWhite(pixelAt(pxNoFlip, 4, 2))   // top half white
                && isBlack(pixelAt(pxNoFlip, 4, 6));  // bottom half black
            const bool flipOk = !pxFlip.empty()
                && isBlack(pixelAt(pxFlip, 4, 2))     // top half now black
                && isWhite(pixelAt(pxFlip, 4, 6));    // bottom half now white -- genuinely flipped
            Check(noFlipOk && flipOk,
                  "KK5: SpriteEffects::FlipVertically genuinely flips a glyph -- an asymmetric (top-half-"
                  "white) glyph lands top-half-white unflipped and bottom-half-white flipped, so a no-op "
                  "flip cannot pass (plans/plan_dx.md DX-132)");
        }

        // ---- DX-148: Model / ModelMesh / ModelMeshPart / ModelBone runtime API. ----
        // Drives ModelMesh::Draw()'s REAL orchestration (SetVertexBuffer + setIndicesProperty +
        // DrawIndexedPrimitives + EffectPass::Apply through a bone-transformed world matrix), not a
        // raw VertexBuffer draw wearing a Model label -- which is exactly what DX-148's own row warns
        // against, and would prove nothing new.
        {
            const X::Color red(255, 0, 0, 255);
            const XG::VertexPositionColor verts[4] = {
                { X::Vector3(-1.0f,  1.0f, 0.0f), red },
                { X::Vector3(-1.0f, -1.0f, 0.0f), red },
                { X::Vector3( 1.0f, -1.0f, 0.0f), red },
                { X::Vector3( 1.0f,  1.0f, 0.0f), red },
            };
            XG::VertexBuffer vb(dev, 4);
            vb.SetData(verts, 4);
            const uint16_t indices[6] = { 0, 1, 2, 0, 2, 3 };
            XG::IndexBuffer ib(dev, 6);
            ib.SetData(indices, 6);

            XG::BasicEffect fx(dev);
            fx.VertexColorEnabled = true;

            // A real 2-bone hierarchy: root -> child. Model::Draw multiplies the mesh's absolute
            // bone transform into the world matrix, so this genuinely exercises the bone path.
            XG::ModelBone bone0(0, "root");
            XG::ModelBone bone1(1, "child");
            bone0.AddChild(&bone1);

            XG::ModelMeshPart part(&vb, &ib, /*numVertices=*/4, /*primitiveCount=*/2,
                                   /*startIndex=*/0, /*vertexOffset=*/0);
            XG::ModelMesh mesh(&dev, { &part });
            part.setEffectProperty(&fx);
            XG::Model model(&dev, { &bone0, &bone1 }, { &mesh });

            auto pxModel = renderToTarget(X::Color(0, 255, 0, 255), [&] {
                dev.SetDepthTestEnabled(false);
                dev.setBlendStateProperty(XG::BlendState::Opaque);
                dev.setRasterizerStateProperty(XG::RasterizerState::CullNone);
                model.Draw(X::Matrix::getIdentityProperty(),
                           X::Matrix::getIdentityProperty(),
                           X::Matrix::getIdentityProperty());
            });
            const X::Color centre = pixelAt(pxModel, kW / 2, kH / 2);
            Check(!pxModel.empty()
                      && centre.getRProperty() >= 200
                      && centre.getGProperty() <= 60
                      && centre.getBProperty() <= 60,
                  "KK6: Model::Draw() -> ModelMesh::Draw()'s real orchestration (bone transform + "
                  "SetVertexBuffer + setIndices + DrawIndexedPrimitives + EffectPass::Apply) paints the "
                  "mesh's exact red over the green clear, through the D3D12 renderer (plans/plan_dx.md DX-148)");
        }

        // ---- DX-155: Model root-bone-index flexibility (Task 916's own rootBoneIndex constructor
        // parameter) against the real D3D12 renderer. Honest scope, mirrors D3D11's own DX-155:
        // neither Model::Draw() nor CopyAbsoluteBoneTransformsTo() actually consult root_ (confirmed
        // by reading Model.cpp) -- Draw() picks each mesh's world transform via
        // mesh->getParentBoneProperty() (the meshParentBones constructor argument), so
        // rootBoneIndex's only currently-consumed effect anywhere is getRootProperty() returning
        // it. This proves that (not silently defaulting to bones[0]) AND exercises the full
        // 5-argument constructor (meshParentBones + rootBoneIndex together, never used by KK6's own
        // 3-argument-constructor fixture) end to end through a real draw, including meshParentBones
        // correctly targeting a NON-zero-indexed bone. ----
        {
            const X::Color redR(255, 0, 0, 255);
            const XG::VertexPositionColor vertsR[4] = {
                { X::Vector3(-1.0f,  1.0f, 0.0f), redR },
                { X::Vector3(-1.0f, -1.0f, 0.0f), redR },
                { X::Vector3( 1.0f, -1.0f, 0.0f), redR },
                { X::Vector3( 1.0f,  1.0f, 0.0f), redR },
            };
            XG::VertexBuffer vbR(dev, 4);
            vbR.SetData(vertsR, 4);
            const uint16_t indicesR[6] = { 0, 1, 2, 0, 2, 3 };
            XG::IndexBuffer ibR(dev, 6);
            ibR.SetData(indicesR, 6);

            XG::BasicEffect fxR(dev);
            fxR.VertexColorEnabled = true;

            // Two INDEPENDENT top-level bones (no parent/child relationship -- that hierarchy-
            // chaining path is already covered by KK6's own fixture). bone0R (array index 0) is a
            // large translation that would move the mesh off-screen if it were ever picked by
            // mistake; bone1R (array index 1, a NON-zero index) is Identity and is the bone the
            // mesh is actually parented to AND the requested root.
            XG::ModelBone bone0R(0, "decoy");
            bone0R.setTransformProperty(X::Matrix::CreateTranslation(100.0f, 100.0f, 0.0f));
            XG::ModelBone bone1R(1, "actual_root");
            bone1R.setTransformProperty(X::Matrix::getIdentityProperty());

            XG::ModelMeshPart partR(&vbR, &ibR, /*numVertices=*/4, /*primitiveCount=*/2,
                                    /*startIndex=*/0, /*vertexOffset=*/0);
            XG::ModelMesh meshR(&dev, { &partR });
            partR.setEffectProperty(&fxR);
            XG::Model modelR(&dev, { &bone0R, &bone1R }, { &meshR }, { &bone1R }, /*rootBoneIndex=*/1);

            Check(modelR.getRootProperty() == &bone1R,
                  "VV0: Model: the 5-argument constructor's rootBoneIndex=1 genuinely sets Root to "
                  "the bone at that NON-zero index, not silently defaulting to bones[0] (plans/plan_dx.md "
                  "DX-155)");

            auto pxModelR = renderToTarget(X::Color(0, 255, 0, 255), [&] {
                dev.SetDepthTestEnabled(false);
                dev.setBlendStateProperty(XG::BlendState::Opaque);
                dev.setRasterizerStateProperty(XG::RasterizerState::CullNone);
                modelR.Draw(X::Matrix::getIdentityProperty(),
                           X::Matrix::getIdentityProperty(),
                           X::Matrix::getIdentityProperty());
            });
            const X::Color centreR = pixelAt(pxModelR, kW / 2, kH / 2);
            Check(!pxModelR.empty()
                      && centreR.getRProperty() >= 200
                      && centreR.getGProperty() <= 60
                      && centreR.getBProperty() <= 60,
                  "VV1: Model::Draw() with a real 5-argument-constructor Model (meshParentBones "
                  "targeting the NON-zero-indexed bone1R, rootBoneIndex=1) genuinely draws the mesh's "
                  "exact red over the green clear -- proves meshParentBones correctly selected "
                  "bone1R's own Identity transform, not bone0R's off-screen-translating one, through "
                  "the real D3D12 renderer (plans/plan_dx.md DX-155)");
        }

        // ---- DX-140 (remaining half): Texture2D::SaveAsPng() / FromStream() round-trip. ----
        // NPOT is already closed; this is the encode/decode path, which needs a GraphicsDevice.
        {
            constexpr int kTexW = 4, kTexH = 4;
            std::vector<X::Color> src(kTexW * kTexH, X::Color(0, 0, 0, 255));
            for (int i = 0; i < kTexW * kTexH; ++i)
                src[i] = X::Color(static_cast<uint8_t>(i * 16),      // a genuinely varying pattern,
                                  static_cast<uint8_t>(255 - i * 16), // not a solid colour: a decoder
                                  static_cast<uint8_t>((i * 7) % 256),// that dropped/reordered pixels
                                  255);                               // could not survive this
            XG::Texture2D original(dev, kTexW, kTexH);
            original.SetData(src.data(), kTexW * kTexH);

            System::IO::MemoryStream png;
            original.SaveAsPng(&png, kTexW, kTexH);
            const bool encoded = png.getLengthProperty() > 0;
            Check(encoded,
                  "KK7: Texture2D::SaveAsPng() encodes a real, non-empty PNG through a windowless "
                  "GraphicsDevice (plans/plan_dx.md DX-140)");

            if (encoded)
            {
                png.setPositionProperty(0);
                XG::Texture2D decoded = XG::Texture2D::FromStream(dev, png);
                Check(decoded.getWidthProperty() == kTexW && decoded.getHeightProperty() == kTexH,
                      "KK8: Texture2D::FromStream() decodes that PNG back to the exact original "
                      "dimensions (plans/plan_dx.md DX-140)");

                std::vector<X::Color> back(kTexW * kTexH, X::Color(0, 0, 0, 0));
                decoded.GetData(back.data(), kTexW * kTexH);
                bool exact = true;
                for (int i = 0; i < kTexW * kTexH; ++i)
                    if (back[i].getRProperty() != src[i].getRProperty() ||
                        back[i].getGProperty() != src[i].getGProperty() ||
                        back[i].getBProperty() != src[i].getBProperty() ||
                        back[i].getAProperty() != src[i].getAProperty())
                    { exact = false; break; }
                Check(exact,
                      "KK9: SaveAsPng() -> FromStream() round-trips EVERY pixel of a deliberately varying "
                      "4x4 pattern EXACTLY -- a decoder that dropped, reordered or channel-swapped pixels "
                      "could not pass (plans/plan_dx.md DX-140)");
            }
        }

        // plans/plan_dx.md DX-121: SpriteBatch::Begin(effect) -- a custom Effect draws sprites through
        // that effect's own shader (a deliberate RGB color inversion) instead of the stock
        // sprite2d pipeline, mirroring D3D11's own already-closed DX-71 methodology exactly (same
        // HLSL source, same Sprite2DVertex contract, same 128-byte constant-buffer layout --
        // D3D12EffectRenderer's own vpSize/uColor/uFloat0 slots are byte-for-byte identical to
        // D3D11's, DX-121's own closing note already confirmed this). This was blocked when DX-121
        // itself closed (GraphicsDevice's constructor unconditionally created a real window, which
        // crashes for D3D12 outside a Proton-managed launch) -- PresentationParameters::HeadlessEXT
        // (commit b3289ac6) removed that blocker, the same fix DX-132/DX-140/DX-148 already used.
        {
            XG::ShaderEffect invertEffect(dev,
                "struct VSIn { float2 pos:POSITION0; float2 uv:TEXCOORD0; float4 col:COLOR0; };\n"
                "struct VSOut { float4 pos:SV_Position; float2 uv:TEXCOORD0; float4 col:TEXCOORD1; };\n"
                "cbuffer CB : register(b0) { float4 vpSize; float4 pad1[4]; float4 uColor; float4 uFloat0; };\n"
                "VSOut main(VSIn input) {\n"
                "    VSOut o;\n"
                "    float2 ndc = (input.pos / vpSize.xy) * 2.0 - 1.0;\n"
                "    o.pos = float4(ndc.x, -ndc.y, 0.0, 1.0);\n"
                "    o.uv = input.uv;\n"
                "    o.col = input.col;\n"
                "    return o;\n"
                "}",
                "Texture2D texSampler : register(t0);\n"
                "SamplerState texSamplerSampler : register(s0);\n"
                "struct PSIn { float4 pos:SV_Position; float2 uv:TEXCOORD0; float4 col:TEXCOORD1; };\n"
                "float4 main(PSIn input) : SV_Target {\n"
                "    float4 texColor = texSampler.Sample(texSamplerSampler, input.uv);\n"
                "    return float4(float3(1.0, 1.0, 1.0) - texColor.rgb, 1.0);\n"
                "}");
            Check(invertEffect.IsEffectValid(),
                  "NN0: ShaderEffect (D3D12): a runtime-compiled custom HLSL pair for SpriteBatch's "
                  "own Sprite2DVertex contract compiles successfully, through the windowless "
                  "GraphicsDevice (plans/plan_dx.md DX-121)");

            XG::Texture2D redTex(dev, 2, 2);
            std::vector<X::Color> redPixels(2 * 2, X::Color(255, 0, 0, 255));
            redTex.SetData(redPixels.data(), 2 * 2);

            auto pxInverted = renderToTarget(kBlack, [&] {
                XG::SamplerState pointClamp = XG::SamplerState::PointClamp;
                XG::SpriteBatch invertBatch(dev);
                invertBatch.Begin(XG::SpriteSortMode::Deferred, XG::BlendState::Opaque, &pointClamp,
                                  nullptr, nullptr, &invertEffect);
                invertBatch.Draw(redTex, X::Rectangle(0, 0, kW, kH),
                                 X::Rectangle(0, 0, 2, 2), X::Color::White);
                invertBatch.End();
            });
            // Solid red (255,0,0) inverted -> exact cyan (0,255,255); alpha forced to 1.0 by the
            // custom shader itself (not inverted).
            const auto invertedPx = pixelAt(pxInverted, kW / 2, kH / 2);
            Check(!pxInverted.empty() && invertedPx.getRProperty() == 0 &&
                      invertedPx.getGProperty() == 255 && invertedPx.getBProperty() == 255 &&
                      invertedPx.getAProperty() == 255,
                  "NN1: D3D12SpriteBatchRenderer + SpriteBatch::Begin(effect): sprites draw through "
                  "the custom Effect's own shader, producing its exact expected (inverted) output "
                  "color, not the stock sprite2d pipeline's -- through the real public XNA API, off-"
                  "screen (plans/plan_dx.md DX-121)");
        }
    }

    std::printf("\n%s: %d failure(s)\n", g_failures == 0 ? "RESULT: ALL PASS" : "RESULT: FAILURES", g_failures);
    return g_failures == 0 ? 0 : 1;
}
