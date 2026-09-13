// plans/plan_dx.md DX-120/DX-240.
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
        : renderer_(renderer, renderer ? renderer->GetLifetimeTokenEXT() : std::weak_ptr<void>{},
                    "D3D12OcclusionQueryRenderer")
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

    D3D12OcclusionQueryRenderer::~D3D12OcclusionQueryRenderer()
    {
        if (!active_ || !renderer_)
            return;

        // FNA does not validate an active query's disposal. Balance the native command so the
        // shared frame list remains valid, but do not manufacture a result for a disposed object.
        try
        {
            ID3D12GraphicsCommandList* cmdList = renderer_->GetFrameCommandListEXT();
            renderer_->RetainFrameObjectEXT(queryHeap_.Get());
            cmdList->EndQuery(queryHeap_.Get(), D3D12_QUERY_TYPE_OCCLUSION, 0);
        }
        catch (...)
        {
            // Destruction cannot recover a renderer that is itself being torn down.
        }
    }

    void D3D12OcclusionQueryRenderer::Begin()
    {
        // FNA forwards invalid sequences without validation. A duplicate Begin therefore remains
        // non-throwing, but must not emit an invalid nested D3D12 query command.
        if (active_)
            return;

        ID3D12GraphicsCommandList* cmdList = renderer_->GetFrameCommandListEXT();
        renderer_->RetainFrameObjectEXT(queryHeap_.Get());
        cmdList->BeginQuery(queryHeap_.Get(), D3D12_QUERY_TYPE_OCCLUSION, 0);
        completionFenceValue_ = 0;
        active_ = true;
    }

    void D3D12OcclusionQueryRenderer::End()
    {
        // Preserve FNA's non-throwing End-before-Begin and duplicate-End behavior without recording
        // an unmatched native EndQuery command.
        if (!active_)
            return;

        ID3D12GraphicsCommandList* cmdList = renderer_->GetFrameCommandListEXT();
        renderer_->RetainFrameObjectEXT(queryHeap_.Get());
        renderer_->RetainFrameObjectEXT(readbackBuffer_.Get());
        cmdList->EndQuery(queryHeap_.Get(), D3D12_QUERY_TYPE_OCCLUSION, 0);
        cmdList->ResolveQueryData(queryHeap_.Get(), D3D12_QUERY_TYPE_OCCLUSION, 0, 1, readbackBuffer_.Get(), 0);
        completionFenceValue_ = renderer_->GetActiveFrameFenceValueEXT();
        active_ = false;
    }

    bool D3D12OcclusionQueryRenderer::IsComplete() const
    {
        return completionFenceValue_ != 0 &&
               renderer_->GetFenceEXT()->GetCompletedValue() >= completionFenceValue_;
    }

    int D3D12OcclusionQueryRenderer::PixelCount() const
    {
        if (!IsComplete()) return 0;

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
