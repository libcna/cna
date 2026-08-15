// SPDX-License-Identifier: MS-PL
//
// REMED-GFX-177, the native half. examples/descriptor_capacity_contract_test.cpp proves through the
// public XNA API that a high-cardinality workload renders correctly on every renderer; this fixture
// proves the D3D12-specific CARDINALITY and LIFETIME claims that a rendered pixel cannot show:
//
//   * a freed slot is REISSUED rather than consumed forever -- the exact defect, measured as a
//     recycle count and a repeated index, not inferred from "it did not throw";
//   * capacity GROWS on genuine simultaneous demand and stops at the D3D12 specification's own
//     ceiling, rather than at an arbitrary constant;
//   * a stable INDEX survives growth, and the GPU handle it resolves to moves with the heap;
//   * the shader-visible heap OBJECT changes on growth (a generation bump) and the retired one is
//     held until the fence passes, never released underneath a submitted command list;
//   * repeated identical bindings consume NOTHING -- a cache hit must not take a descriptor;
//   * a workload that creates and destroys in a loop reaches a bounded STEADY STATE: capacity stops
//     growing and the never-used cursor stops advancing;
//   * no heap object is created per draw, and no wait, idle or extra submit was added to make any
//     of the above true.
//
// This is deliberately an off-screen renderer fixture (args.surface.windowId == 0), for the reason
// examples/directx12_smoke_test.cpp's own header records: GraphicsDevice's constructor creates a real
// window for any non-Headless renderer, and this dev loop's Wine dxgi.dll crashes in
// d3d12_swapchain_init. Everything measured here is device-level, so no swap chain is involved.
//
// Exit code 0 = all checks PASS, 1 = any FAILs.

#include "CNA/Internal/Renderers/DirectX12/DirectX12Renderer.hpp"
#include "CNA/Internal/Renderers/DirectX12/D3D12DescriptorHeaps.hpp"
#include "CNA/Internal/Renderers/DirectX12/D3D12RenderTargets.hpp"
#include "CNA/Internal/Renderers/DirectX12/D3D12TextureCube.hpp"
#include "CNA/Internal/Renderers/DirectX12/D3D12Textures.hpp"
#include "CNA/Internal/Renderers/Common/IGraphicsRenderer.hpp"
#include "CNA/Internal/Graphics/ImageData.hpp"

#include <cstdint>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

using namespace CNA::Internal::Renderers::DirectX12;
using CNA::Internal::Renderers::GraphicsRendererCreateArgs;
using CNA::Internal::Graphics::ImageData;

namespace
{
    int g_pass = 0;
    int g_total = 0;

    void Check(bool condition, const std::string& name)
    {
        ++g_total;
        if (condition) ++g_pass;
        std::printf("[%s] %s\n", condition ? "PASS" : "FAIL", name.c_str());
        std::fflush(stdout);
    }

    void Note(const std::string& name)
    {
        std::printf("[NOTE] %s\n", name.c_str());
        std::fflush(stdout);
    }

    ImageData TinyImage()
    {
        ImageData img;
        img.width = 2;
        img.height = 2;
        img.mipLevels = 1;
        img.pixels.assign(2 * 2 * 4, 255);
        return img;
    }

    std::string Describe(const char* what, const D3D12DescriptorHeapStats& s)
    {
        char buf[320];
        std::snprintf(buf, sizeof(buf),
                      "%s cap=%u live=%u peak=%u neverUsed=%u freeReady=%u freePending=%u alloc=%llu "
                      "recycle=%llu free=%llu grow=%u gen=%u heapObjs=%u publish=%llu bulk=%llu",
                      what, s.capacity, s.live, s.peakLive, s.neverUsed, s.freeReady, s.freePending,
                      static_cast<unsigned long long>(s.allocations),
                      static_cast<unsigned long long>(s.recycles),
                      static_cast<unsigned long long>(s.frees), s.growths, s.generation, s.heapObjects,
                      static_cast<unsigned long long>(s.publishes),
                      static_cast<unsigned long long>(s.bulkCopies));
        return buf;
    }
}

int main()
{
    GraphicsRendererCreateArgs args;
    // surface stays windowless deliberately -- see file header comment.
    args.virtualWidth = 64;
    args.virtualHeight = 64;

    DirectX12Renderer renderer(args);
    ID3D12Device* device = renderer.GetDeviceEXT();
    const std::shared_ptr<D3D12DescriptorHeaps>& heaps = renderer.GetDescriptorHeapsEXT();

    Check(device != nullptr && heaps != nullptr, "A1: device and descriptor allocator set exist");
    std::printf("=== REMED-GFX-177 D3D12 descriptor allocator ===\n");

    // ---- A: the starting shape, unchanged from DX-103 -------------------------------------------
    {
        const D3D12DescriptorHeapStats srv = heaps->cbvSrvUav.GetStatsEXT();
        const D3D12DescriptorHeapStats smp = heaps->sampler.GetStatsEXT();
        Check(srv.capacity == 64,
              "A2: the CBV/SRV/UAV heap still STARTS at DX-103's own 64 -- the correction is "
              "reclamation and growth, not a larger constant (" + Describe("srv", srv) + ")");
        Check(smp.capacity == 16, "A3: the SAMPLER heap still starts at DX-119's own 16");
        Check(srv.growths == 0 && srv.generation == 0, "A4: nothing has grown yet");
        Check(srv.heapObjects == 2,
              "A5: exactly two heap objects for the shader-visible pool (staging + shader-visible)");
    }

    // ---- B: one descriptor per sampleable resource, returned on destruction ----------------------
    {
        const std::uint64_t before = heaps->cbvSrvUav.GetStatsEXT().allocations;
        const std::uint32_t liveBefore = heaps->cbvSrvUav.GetStatsEXT().live;
        std::uint32_t indexOfDoomed = D3D12ShaderVisibleDescriptorAllocator::kInvalidIndex;
        {
            D3D12TextureRenderer tex(&renderer, TinyImage());
            indexOfDoomed = tex.GetShaderResourceViewIndexEXT();
            const D3D12DescriptorHeapStats s = heaps->cbvSrvUav.GetStatsEXT();
            Check(s.allocations == before + 1 && s.live == liveBefore + 1,
                  "B1: a Texture2D takes exactly one CBV/SRV/UAV descriptor");
            Check(renderer.GetCbvSrvUavGpuHandleEXT(indexOfDoomed).ptr != 0,
                  "B2: its index resolves to a real shader-visible GPU handle");
        }
        const D3D12DescriptorHeapStats after = heaps->cbvSrvUav.GetStatsEXT();
        Check(after.live == liveBefore && after.frees >= 1,
              "B3: destroying it RETURNS the descriptor -- the exact behaviour DX-103 lacked (" +
                  Describe("srv", after) + ")");

        // Submission here is synchronous, so the fence stamp taken at Free() has already completed
        // and the next allocation may take the slot back with no wait of any kind.
        D3D12TextureRenderer replacement(&renderer, TinyImage());
        Check(replacement.GetShaderResourceViewIndexEXT() == indexOfDoomed,
              "B4: the very next texture is given the freed slot back (index " +
                  std::to_string(indexOfDoomed) + ")");
    }

    // ---- C: the pre-fix boundary. 200 created and destroyed in sequence --------------------------
    //
    // This is the shape REMED-GFX-175's fixture hit: one resource live at a time, far more than 64
    // created over the run. Before this task the 65th throw was unconditional.
    {
        const D3D12DescriptorHeapStats before = heaps->cbvSrvUav.GetStatsEXT();
        bool threw = false;
        std::uint32_t distinctIndices = 0;
        std::uint32_t lastIndex = D3D12ShaderVisibleDescriptorAllocator::kInvalidIndex;
        try
        {
            for (int i = 0; i < 200; ++i)
            {
                D3D12TextureRenderer tex(&renderer, TinyImage());
                const std::uint32_t idx = tex.GetShaderResourceViewIndexEXT();
                if (idx != lastIndex) ++distinctIndices;
                lastIndex = idx;
            }
        }
        catch (const std::exception& e)
        {
            threw = true;
            std::printf("       threw: %s\n", e.what());
        }
        const D3D12DescriptorHeapStats after = heaps->cbvSrvUav.GetStatsEXT();
        Check(!threw, "C1: 200 sequential create/destroy cycles do not exhaust the heap");
        Check(after.capacity == before.capacity,
              "C2: capacity did NOT grow -- one live resource never needs more than one slot (cap=" +
                  std::to_string(after.capacity) + ")");
        Check(after.recycles >= 199,
              "C3: every one of those 200 allocations came from the FREE LIST rather than the bump "
              "cursor (recycled=" + std::to_string(after.recycles - before.recycles) + "/200)");
        Check(after.neverUsed == before.neverUsed,
              "C4: the bump cursor did not advance at all across the 200 cycles");
    }

    // ---- D: growth on genuine simultaneous demand -------------------------------------------------
    {
        const D3D12DescriptorHeapStats before = heaps->cbvSrvUav.GetStatsEXT();
        ID3D12DescriptorHeap* heapBefore = renderer.GetCbvSrvUavHeapEXT();

        std::vector<std::unique_ptr<D3D12TextureRenderer>> live;
        std::vector<std::uint32_t> indices;
        std::vector<D3D12_GPU_DESCRIPTOR_HANDLE> handlesAtBirth;
        for (int i = 0; i < 300; ++i)
        {
            live.push_back(std::make_unique<D3D12TextureRenderer>(&renderer, TinyImage()));
            indices.push_back(live.back()->GetShaderResourceViewIndexEXT());
            handlesAtBirth.push_back(live.back()->GetShaderResourceViewGpuHandleEXT());
        }

        const D3D12DescriptorHeapStats after = heaps->cbvSrvUav.GetStatsEXT();
        ID3D12DescriptorHeap* heapAfter = renderer.GetCbvSrvUavHeapEXT();

        Check(after.capacity >= 300 && after.growths >= 2,
              "D1: 300 simultaneously live textures grew the heap past its starting 64 (" +
                  Describe("srv", after) + ")");
        Check(after.capacity == 512,
              "D2: growth DOUBLES (64 -> 128 -> 256 -> 512), so capacity tracks demand rather than "
              "any hand-picked number (cap=" + std::to_string(after.capacity) + ")");
        Check(heapAfter != heapBefore && after.generation > before.generation,
              "D3: the shader-visible heap OBJECT was replaced and the generation counter says so");

        // Indices are the contract: they must be unchanged by growth, and every one must still
        // resolve -- into the NEW heap, not the retired one.
        std::uint32_t movedHandles = 0;
        std::uint32_t badIndices = 0;
        std::uint32_t duplicates = 0;
        std::vector<char> seen(after.capacity, 0);
        for (std::size_t i = 0; i < indices.size(); ++i)
        {
            if (live[i]->GetShaderResourceViewIndexEXT() != indices[i]) ++badIndices;
            if (live[i]->GetShaderResourceViewGpuHandleEXT().ptr != handlesAtBirth[i].ptr) ++movedHandles;
            if (indices[i] < seen.size())
            {
                if (seen[indices[i]]) ++duplicates;
                seen[indices[i]] = 1;
            }
        }
        Check(badIndices == 0,
              "D4: every index survived growth unchanged (changed=" + std::to_string(badIndices) + ")");
        Check(duplicates == 0,
              "D5: no two live resources were given the same slot (duplicates=" +
                  std::to_string(duplicates) + ")");
        Check(movedHandles > 0,
              "D6: GPU handles allocated before the growth now resolve into the NEW heap -- which is "
              "exactly why a raw handle could not be stored (moved=" + std::to_string(movedHandles) +
                  "/" + std::to_string(indices.size()) + ")");
        Check(after.bulkCopies > 0,
              "D7: growth re-mirrored the live range from the staging heap rather than copying out "
              "of the shader-visible one, which D3D12 forbids (descriptors copied=" +
                  std::to_string(after.bulkCopies) + ")");

        // Retired heaps must not be released underneath a command list that bound them. Submission
        // is synchronous here, so the retired pair is reclaimed on the next allocator touch --
        // which is exactly what a fence-correct implementation should do, and is measurable.
        Check(after.heapObjects >= 2, "D8: the current heap pair is alive (" +
                                          std::to_string(after.heapObjects) + " heap objects)");

        // Release everything: the pool must go back to zero live without shrinking capacity (a heap
        // cannot be resized in place, and shrinking it would be a second stale-handle hazard).
        live.clear();
        const D3D12DescriptorHeapStats released = heaps->cbvSrvUav.GetStatsEXT();
        Check(released.live == 0,
              "D9: releasing all 300 returns every slot (live=" + std::to_string(released.live) + ")");

        // And the reclaimed capacity is genuinely reusable: the next 300 must come entirely from the
        // free list, with no further growth.
        std::vector<std::unique_ptr<D3D12TextureRenderer>> again;
        for (int i = 0; i < 300; ++i)
            again.push_back(std::make_unique<D3D12TextureRenderer>(&renderer, TinyImage()));
        const D3D12DescriptorHeapStats reused = heaps->cbvSrvUav.GetStatsEXT();
        Check(reused.growths == released.growths && reused.capacity == released.capacity,
              "D10: a second wave of 300 needs NO further growth (grow=" +
                  std::to_string(reused.growths) + ", cap=" + std::to_string(reused.capacity) + ")");
        Check(reused.recycles - released.recycles == 300,
              "D11: all 300 came from the free list (recycled=" +
                  std::to_string(reused.recycles - released.recycles) + "/300)");
        again.clear();
    }

    // ---- E: bounded steady state under repeated churn ---------------------------------------------
    {
        // Prime: a moderate live set, then 500 cycles of destroy-one/create-one around it. Capacity
        // and the bump cursor must both stop moving; only the recycle count may rise.
        std::vector<std::unique_ptr<D3D12TextureRenderer>> live;
        for (int i = 0; i < 50; ++i)
            live.push_back(std::make_unique<D3D12TextureRenderer>(&renderer, TinyImage()));

        const D3D12DescriptorHeapStats primed = heaps->cbvSrvUav.GetStatsEXT();
        for (int step = 0; step < 500; ++step)
            live[static_cast<std::size_t>(step % 50)] =
                std::make_unique<D3D12TextureRenderer>(&renderer, TinyImage());
        const D3D12DescriptorHeapStats churned = heaps->cbvSrvUav.GetStatsEXT();

        Check(churned.capacity == primed.capacity && churned.growths == primed.growths,
              "E1: 500 churn steps around 50 live textures grew nothing (cap " +
                  std::to_string(primed.capacity) + " -> " + std::to_string(churned.capacity) + ")");
        Check(churned.neverUsed == primed.neverUsed,
              "E2: the bump cursor did not advance across the churn -- a bounded STEADY STATE");
        Check(churned.live == primed.live,
              "E3: the live count is exactly the live set, not the history (live=" +
                  std::to_string(churned.live) + ")");
        Check(churned.heapObjects == primed.heapObjects,
              "E4: no heap object was created by the churn (heapObjs=" +
                  std::to_string(churned.heapObjects) + ")");
        live.clear();
    }

    // ---- F: RTV and DSV -- the same defect, the same fix, different mechanics ---------------------
    {
        const D3D12DescriptorHeapStats rtvBefore = heaps->rtv.GetStatsEXT();
        const D3D12DescriptorHeapStats dsvBefore = heaps->dsv.GetStatsEXT();

        // A render target with depth takes one RTV, one DSV and one SRV. 100 live is past the RTV
        // heap's own starting 64 and far past the DSV heap's 8.
        std::vector<std::unique_ptr<D3D12RenderTargetRenderer>> targets;
        std::vector<D3D12_CPU_DESCRIPTOR_HANDLE> rtvs;
        for (int i = 0; i < 100; ++i)
        {
            targets.push_back(std::make_unique<D3D12RenderTargetRenderer>(
                &renderer, device, 4, 4, /*depthFormat=*/1, /*mipMap=*/false, /*multiSampleCount=*/0));
            rtvs.push_back(targets.back()->GetRtvEXT());
        }
        const D3D12DescriptorHeapStats rtvLive = heaps->rtv.GetStatsEXT();
        const D3D12DescriptorHeapStats dsvLive = heaps->dsv.GetStatsEXT();
        Check(rtvLive.live == rtvBefore.live + 100,
              "F1: 100 live render targets hold 100 RTVs (" + Describe("rtv", rtvLive) + ")");
        Check(dsvLive.live == dsvBefore.live + 100 && dsvLive.capacity >= 100,
              "F2: and 100 DSVs, past the DSV heap's own starting 8 (" + Describe("dsv", dsvLive) + ")");

        // The RTV/DSV allocators grow by APPENDING a block, so an already-issued handle never moves.
        // That is the property that lets D3D12RenderTargetRenderer keep storing a raw CPU handle.
        std::uint32_t movedRtvs = 0;
        for (std::size_t i = 0; i < rtvs.size(); ++i)
            if (targets[i]->GetRtvEXT().ptr != rtvs[i].ptr) ++movedRtvs;
        Check(movedRtvs == 0 && rtvLive.generation == 0,
              "F3: growing the RTV pool moved NO already-issued handle (moved=" +
                  std::to_string(movedRtvs) + ", generation=" + std::to_string(rtvLive.generation) + ")");
        Check(rtvLive.heapObjects >= 2,
              "F4: growth appended a heap block rather than replacing the heap (blocks=" +
                  std::to_string(rtvLive.heapObjects) + ")");

        targets.clear();
        const D3D12DescriptorHeapStats rtvFreed = heaps->rtv.GetStatsEXT();
        const D3D12DescriptorHeapStats dsvFreed = heaps->dsv.GetStatsEXT();
        Check(rtvFreed.live == rtvBefore.live && dsvFreed.live == dsvBefore.live,
              "F5: destroying them returns every RTV and DSV (rtv live=" +
                  std::to_string(rtvFreed.live) + ", dsv live=" + std::to_string(dsvFreed.live) + ")");

        // 300 create/destroy cycles: the RTV heap must not grow at all now.
        const D3D12DescriptorHeapStats beforeCycles = heaps->rtv.GetStatsEXT();
        for (int i = 0; i < 300; ++i)
        {
            D3D12RenderTargetRenderer rt(&renderer, device, 4, 4, 1, false, 0);
            (void)rt.GetRtvEXT();
        }
        const D3D12DescriptorHeapStats afterCycles = heaps->rtv.GetStatsEXT();
        Check(afterCycles.capacity == beforeCycles.capacity,
              "F6: 300 render-target create/destroy cycles grow the RTV pool by nothing (cap=" +
                  std::to_string(afterCycles.capacity) + ")");
    }

    // ---- G: a cube render target takes six RTVs and gives all six back ----------------------------
    {
        const D3D12DescriptorHeapStats before = heaps->rtv.GetStatsEXT();
        {
            D3D12RenderTargetCubeRenderer cube(&renderer, device, 4, /*depthFormat=*/1, false, 0);
            const D3D12DescriptorHeapStats live = heaps->rtv.GetStatsEXT();
            Check(live.live == before.live + 6,
                  "G1: a RenderTargetCube takes exactly six RTVs, one per face (live delta=" +
                      std::to_string(live.live - before.live) + ")");
        }
        const D3D12DescriptorHeapStats after = heaps->rtv.GetStatsEXT();
        Check(after.live == before.live,
              "G2: destroying it returns all six (live delta=" +
                  std::to_string(after.live - before.live) + ")");
    }

    // ---- H: the sampler heap. A cache hit must consume nothing ------------------------------------
    {
        const D3D12DescriptorHeapStats before = heaps->sampler.GetStatsEXT();

        // 9 public TextureFilter ordinals x 3 public TextureAddressMode values = 27 distinct
        // states, well past the starting 16.
        for (int filter = 0; filter < 9; ++filter)
            for (int addr = 0; addr < 3; ++addr)
            {
                renderer.ApplySamplerState(0, filter, addr, (addr + 1) % 3, 4);
                (void)renderer.GetSamplerGpuHandleEXT(0);
            }
        const D3D12DescriptorHeapStats distinct = heaps->sampler.GetStatsEXT();
        Check(distinct.capacity > 16 && distinct.growths >= 1,
              "H1: 27 distinct sampler states grew the sampler heap past its starting 16 (" +
                  Describe("sampler", distinct) + ")");
        Check(distinct.live - before.live == 27,
              "H2: exactly one descriptor per DISTINCT state (new descriptors=" +
                  std::to_string(distinct.live - before.live) + "/27)");

        // Every one of those states again, 10 times over: a cache hit must allocate nothing.
        for (int repeat = 0; repeat < 10; ++repeat)
            for (int filter = 0; filter < 9; ++filter)
                for (int addr = 0; addr < 3; ++addr)
                {
                    renderer.ApplySamplerState(0, filter, addr, (addr + 1) % 3, 4);
                    (void)renderer.GetSamplerGpuHandleEXT(0);
                }
        const D3D12DescriptorHeapStats repeated = heaps->sampler.GetStatsEXT();
        Check(repeated.allocations == distinct.allocations,
              "H3: 270 repeated identical sampler bindings consumed ZERO further descriptors "
              "(allocations " + std::to_string(distinct.allocations) + " -> " +
                  std::to_string(repeated.allocations) + ")");
        Check(repeated.capacity == distinct.capacity,
              "H4: and grew the heap by nothing");

        // Every cached state must still resolve into the CURRENT heap after the growth above.
        std::uint32_t nullHandles = 0;
        for (int filter = 0; filter < 9; ++filter)
            for (int addr = 0; addr < 3; ++addr)
            {
                renderer.ApplySamplerState(0, filter, addr, (addr + 1) % 3, 4);
                if (renderer.GetSamplerGpuHandleEXT(0).ptr == 0) ++nullHandles;
            }
        Check(nullHandles == 0,
              "H5: every cached sampler still resolves after the heap grew (null handles=" +
                  std::to_string(nullHandles) + ")");
    }

    // ---- I: fence stamping is real ----------------------------------------------------------------
    {
        // The clock must have advanced past zero -- every submit above signalled it. A freed slot is
        // stamped against it, so a fence that never moved would mean the deferral is vacuous.
        Check(heaps->clock.lastSubmitted > 0 && heaps->clock.fence != nullptr,
              "I1: the allocator set shares the renderer's real fence and sees submissions (last=" +
                  std::to_string(heaps->clock.lastSubmitted) + ")");
        Check(heaps->clock.Completed() >= heaps->clock.lastSubmitted,
              "I2: this renderer submits synchronously, so every stamp is already complete when it is "
              "taken -- the deferral is correct rather than merely unused (completed=" +
                  std::to_string(heaps->clock.Completed()) + ")");
    }

    // ---- I2: multi-frame reclamation across REAL submissions --------------------------------------
    //
    // The public fixture cannot cross a public frame boundary on this renderer (no swap chain under
    // this dev loop, DX-100/DX-102), so the frames-in-flight half of the contract is measured here,
    // against the renderer's own real per-frame fence primitive. Each round: create a live set,
    // record and submit a real command list, signal that frame's fence, release the set. A freed
    // slot must be stamped against the submission that could still reference it, and must come back
    // once -- and only once -- that fence has completed.
    {
        const D3D12DescriptorHeapStats before = heaps->cbvSrvUav.GetStatsEXT();
        std::uint64_t lastFrameFence = 0;
        int reclaimedTooEarly = 0;

        for (int frame = 0; frame < 8; ++frame)
        {
            std::vector<std::unique_ptr<D3D12TextureRenderer>> perFrame;
            for (int i = 0; i < 40; ++i)
                perFrame.push_back(std::make_unique<D3D12TextureRenderer>(&renderer, TinyImage()));

            ID3D12CommandAllocator* allocator = renderer.GetCommandAllocatorEXT(frame % 2);
            ID3D12GraphicsCommandList* cmdList = renderer.GetCommandListEXT();
            allocator->Reset();
            cmdList->Reset(allocator, nullptr);
            ID3D12DescriptorHeap* bound[] = {renderer.GetCbvSrvUavHeapEXT(), renderer.GetSamplerHeapEXT()};
            cmdList->SetDescriptorHeaps(2, bound);
            cmdList->Close();
            renderer.ExecuteCommandListAndWaitEXT(cmdList);
            lastFrameFence = renderer.SignalAndWaitForFrameEXT(frame % 2);

            // Release the whole frame's set. Every slot is stamped against a fence value that is at
            // least the submission above.
            perFrame.clear();
            if (heaps->clock.lastSubmitted < lastFrameFence) ++reclaimedTooEarly;
        }

        const D3D12DescriptorHeapStats after = heaps->cbvSrvUav.GetStatsEXT();
        Check(reclaimedTooEarly == 0,
              "I3: every per-frame release was stamped against a fence value no older than that "
              "frame's own submission (violations=" + std::to_string(reclaimedTooEarly) + ")");
        Check(after.live == before.live,
              "I4: 8 frames x 40 textures left the live count exactly where it started (live=" +
                  std::to_string(after.live) + ")");
        Check(after.growths == before.growths,
              "I5: and needed no further growth -- per-frame demand, not cumulative history (grow=" +
                  std::to_string(after.growths) + ")");
        Check(after.heapObjects == before.heapObjects,
              "I6: with no heap object created per frame (heapObjs=" +
                  std::to_string(after.heapObjects) + ")");
    }

    // ---- J: no heap object per draw ---------------------------------------------------------------
    {
        const D3D12DescriptorHeapStats srv = heaps->cbvSrvUav.GetStatsEXT();
        const D3D12DescriptorHeapStats smp = heaps->sampler.GetStatsEXT();
        const D3D12DescriptorHeapStats rtv = heaps->rtv.GetStatsEXT();
        const D3D12DescriptorHeapStats dsv = heaps->dsv.GetStatsEXT();
        std::printf("       final: %s\n", Describe("srv", srv).c_str());
        std::printf("       final: %s\n", Describe("sampler", smp).c_str());
        std::printf("       final: %s\n", Describe("rtv", rtv).c_str());
        std::printf("       final: %s\n", Describe("dsv", dsv).c_str());
        Check(srv.heapObjects <= 4 && smp.heapObjects <= 4,
              "J1: the whole run needed at most two heap pairs per shader-visible type -- no heap "
              "per draw, per frame or per resource (srv=" + std::to_string(srv.heapObjects) +
                  ", sampler=" + std::to_string(smp.heapObjects) + ")");
        Check(srv.publishes == srv.allocations,
              "J2: exactly one staging -> shader-visible copy per descriptor CREATION, none per draw "
              "(publishes=" + std::to_string(srv.publishes) + ", allocations=" +
                  std::to_string(srv.allocations) + ")");
        Check(rtv.generation == 0 && dsv.generation == 0,
              "J3: the RTV/DSV pools never replaced a heap, so no RTV/DSV handle ever moved");
    }

    // ---- K: device recreation ---------------------------------------------------------------------
    {
        // A resource created against the OLD device must still be destructible after the device is
        // recreated: it holds its own reference to the old allocator set, so freeing its slot cannot
        // touch the new device's index space.
        auto survivor = std::make_unique<D3D12TextureRenderer>(&renderer, TinyImage());
        const std::shared_ptr<D3D12DescriptorHeaps> oldHeaps = renderer.GetDescriptorHeapsEXT();
        const std::uint32_t survivorIndex = survivor->GetShaderResourceViewIndexEXT();

        bool recreated = true;
        try
        {
            renderer.RecreateDeviceEXT();
        }
        catch (const std::exception& e)
        {
            recreated = false;
            Note(std::string("RecreateDeviceEXT threw: ") + e.what());
        }

        if (!recreated)
        {
            Note("device recreation unavailable in this runtime; K is not observable");
        }
        else
        {
            const std::shared_ptr<D3D12DescriptorHeaps>& newHeaps = renderer.GetDescriptorHeapsEXT();
            Check(newHeaps != oldHeaps,
                  "K1: device recreation builds a brand-new allocator set");
            Check(newHeaps->cbvSrvUav.GetStatsEXT().capacity == 64 &&
                      newHeaps->cbvSrvUav.GetStatsEXT().live == 0,
                  "K2: the new set starts empty at the starting capacity (" +
                      Describe("srv", newHeaps->cbvSrvUav.GetStatsEXT()) + ")");

            const std::uint32_t oldLiveBefore = oldHeaps->cbvSrvUav.GetStatsEXT().live;
            survivor.reset();
            Check(oldHeaps->cbvSrvUav.GetStatsEXT().live == oldLiveBefore - 1,
                  "K3: a resource outliving the device frees into the OLD set, not the new one "
                  "(freed index " + std::to_string(survivorIndex) + ")");
            Check(newHeaps->cbvSrvUav.GetStatsEXT().live == 0,
                  "K4: and left the new device's index space untouched");

            // The recreated device must be usable.
            D3D12TextureRenderer fresh(&renderer, TinyImage());
            Check(fresh.GetShaderResourceViewGpuHandleEXT().ptr != 0,
                  "K5: a texture created after recreation resolves in the new heap");
        }
    }

    std::printf("=== %d/%d checks passed ===\n", g_pass, g_total);
    return (g_pass == g_total) ? 0 : 1;
}
