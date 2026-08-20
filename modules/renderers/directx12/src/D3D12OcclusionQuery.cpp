// plans/plan_dx.md Phase DX13 (DX-120).
#include "CNA/Internal/Renderers/DirectX12/D3D12OcclusionQuery.hpp"
#include "CNA/Internal/Renderers/DirectX12/DirectX12Renderer.hpp"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <string>

namespace CNA::Internal::Renderers::DirectX12
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

    D3D12OcclusionQueryRenderer::D3D12OcclusionQueryRenderer(DirectX12Renderer* renderer)
        : renderer_(renderer)
    {
        ID3D12Device* device = renderer_->GetDeviceEXT();

        D3D12_QUERY_HEAP_DESC heapDesc{};
        heapDesc.Type = D3D12_QUERY_HEAP_TYPE_OCCLUSION;
        heapDesc.Count = 1;

        HRESULT hr = device->CreateQueryHeap(&heapDesc, IID_PPV_ARGS(queryHeap_.ReleaseAndGetAddressOf()));
        if (FAILED(hr))
            throw std::runtime_error("D3D12OcclusionQueryRenderer: CreateQueryHeap failed, hr=" + FormatHr(hr));

        D3D12_HEAP_PROPERTIES heapProps{};
        heapProps.Type = D3D12_HEAP_TYPE_READBACK;

        D3D12_RESOURCE_DESC bufferDesc{};
        bufferDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        bufferDesc.Width = sizeof(std::uint64_t);
        bufferDesc.Height = 1;
        bufferDesc.DepthOrArraySize = 1;
        bufferDesc.MipLevels = 1;
        bufferDesc.Format = DXGI_FORMAT_UNKNOWN;
        bufferDesc.SampleDesc.Count = 1;
        bufferDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

        hr = device->CreateCommittedResource(
            &heapProps, D3D12_HEAP_FLAG_NONE, &bufferDesc,
            D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(readbackBuffer_.ReleaseAndGetAddressOf()));
        if (FAILED(hr))
            throw std::runtime_error("D3D12OcclusionQueryRenderer: readback CreateCommittedResource failed, hr=" + FormatHr(hr));
    }

    void D3D12OcclusionQueryRenderer::Begin()
    {
        // Real, non-obvious constraint found while landing this task: BeginQuery()/EndQuery() must
        // be recorded within the SAME command-list submission as the draw(s) they bracket (a
        // Vulkan/vkd3d-proton requirement) -- a genuinely separate Begin()-only submission here,
        // followed by the game's own separately-submitted draw call(s), followed by a separately-
        // submitted End(), does NOT correctly capture any samples (confirmed: PixelCount() reported
        // 0 for a visible full-viewport triangle before this fix). Rather than record BeginQuery
        // here, this now just marks the query heap "active" on the owning renderer --
        // DirectX12Renderer's own draw-recording methods (DrawColoredPrimitives/
        // DrawIndexedColoredPrimitives/DrawPrimitivesExImpl/DrawInstancedPrimitivesEx) check
        // SetActiveOcclusionQueryEXT()'s tracked heap and record BeginQuery/EndQuery around their
        // own single command-list submission when set -- correct for exactly one draw call between
        // Begin()/End() (see this class's own header doc comment for the multi-draw gap).
        resolved_ = false;
        renderer_->SetActiveOcclusionQueryEXT(queryHeap_.Get());
    }

    void D3D12OcclusionQueryRenderer::End()
    {
        renderer_->SetActiveOcclusionQueryEXT(nullptr);

        // The EndQuery() itself already happened inside the intervening draw call's own command
        // list (see Begin()'s own doc comment) -- this submission only resolves the now-complete
        // query result into the CPU-readable readback buffer.
        ID3D12CommandAllocator* allocator = renderer_->GetCommandAllocatorEXT(0);
        ID3D12GraphicsCommandList* cmdList = renderer_->GetCommandListEXT();
        allocator->Reset();
        cmdList->Reset(allocator, nullptr);

        cmdList->ResolveQueryData(queryHeap_.Get(), D3D12_QUERY_TYPE_OCCLUSION, 0, 1, readbackBuffer_.Get(), 0);

        HRESULT hr = cmdList->Close();
        if (FAILED(hr))
            throw std::runtime_error("D3D12OcclusionQueryRenderer::End: command list Close failed, hr=" + FormatHr(hr));
        renderer_->ExecuteCommandListAndWaitEXT(cmdList);

        // ExecuteCommandListAndWaitEXT only returns once the GPU has genuinely finished this
        // submission, including the ResolveQueryData call above -- see this class's own header
        // doc comment for why that makes "complete" trivially true here, unlike D3D11's async
        // GetData(DONOTFLUSH) polling.
        resolved_ = true;
    }

    bool D3D12OcclusionQueryRenderer::IsComplete() const
    {
        return resolved_;
    }

    int D3D12OcclusionQueryRenderer::PixelCount() const
    {
        if (!resolved_) return 0;

        std::uint64_t count = 0;
        void* mapped = nullptr;
        const D3D12_RANGE readRange{0, sizeof(count)};
        HRESULT hr = readbackBuffer_->Map(0, &readRange, &mapped);
        if (FAILED(hr)) return 0;
        std::memcpy(&count, mapped, sizeof(count));
        const D3D12_RANGE writtenRange{0, 0}; // CPU never wrote through this mapping
        readbackBuffer_->Unmap(0, &writtenRange);

        return static_cast<int>(std::min<std::uint64_t>(count, static_cast<std::uint64_t>(INT32_MAX)));
    }
}
