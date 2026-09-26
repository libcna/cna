// SPDX-License-Identifier: MS-PL
#include "CNA/Internal/Renderers/DirectX12/D3D12GpuTimer.hpp"
#include "CNA/Internal/Renderers/DirectX12/DirectX12Renderer.hpp"

#include <cstring>
#include <memory>
#include <stdexcept>

namespace CNA::Internal::Renderers::DirectX12
{
    D3D12GpuTimer::D3D12GpuTimer(DirectX12Renderer* renderer)
        : renderer_(renderer, renderer ? renderer->GetLifetimeTokenEXT() : std::weak_ptr<void>{},
                    "D3D12GpuTimer")
    {
        if (!renderer)
            throw std::invalid_argument("D3D12 GPU timer requires a renderer");
        CreateResources();
        renderer_->RegisterRecoverableResourceEXT(this);
    }

    D3D12GpuTimer::~D3D12GpuTimer()
    {
        if (renderer_)
            renderer_->UnregisterRecoverableResourceEXT(this);
        ReleaseDeviceResourcesEXT();
    }

    void D3D12GpuTimer::CreateResources()
    {
        ID3D12Device* device = renderer_->GetDeviceEXT();
        ID3D12CommandQueue* queue = renderer_->GetCommandQueueEXT();
        if (!device || !queue || FAILED(queue->GetTimestampFrequency(&frequency_)) || frequency_ == 0)
            throw std::runtime_error("D3D12 GPU timer: timestamp frequency unavailable");

        D3D12_QUERY_HEAP_DESC heapDesc{};
        heapDesc.Type = D3D12_QUERY_HEAP_TYPE_TIMESTAMP;
        heapDesc.Count = 2;
        if (FAILED(device->CreateQueryHeap(&heapDesc, IID_PPV_ARGS(queryHeap_.ReleaseAndGetAddressOf()))))
            throw std::runtime_error("D3D12 GPU timer: timestamp query heap creation failed");

        D3D12_HEAP_PROPERTIES heapProperties{};
        heapProperties.Type = D3D12_HEAP_TYPE_READBACK;
        D3D12_RESOURCE_DESC bufferDesc{};
        bufferDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        bufferDesc.Width = 2 * sizeof(std::uint64_t);
        bufferDesc.Height = 1;
        bufferDesc.DepthOrArraySize = 1;
        bufferDesc.MipLevels = 1;
        bufferDesc.SampleDesc.Count = 1;
        bufferDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        if (FAILED(device->CreateCommittedResource(
                &heapProperties, D3D12_HEAP_FLAG_NONE, &bufferDesc,
                D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
                IID_PPV_ARGS(readback_.ReleaseAndGetAddressOf()))))
            throw std::runtime_error("D3D12 GPU timer: readback buffer creation failed");
    }

    void D3D12GpuTimer::Begin()
    {
        if (active_ || (completionFenceValue_ != 0 && !IsResultAvailable()))
            return;
        ID3D12GraphicsCommandList* commandList = renderer_->GetFrameCommandListEXT();
        renderer_->RetainFrameObjectEXT(queryHeap_.Get());
        commandList->EndQuery(queryHeap_.Get(), D3D12_QUERY_TYPE_TIMESTAMP, 0);
        active_ = true;
        aborted_ = false;
        ready_ = false;
        completionFenceValue_ = 0;
        elapsedNanoseconds_ = 0;
    }

    void D3D12GpuTimer::End()
    {
        if (!active_) return;
        ID3D12GraphicsCommandList* commandList = renderer_->GetFrameCommandListEXT();
        renderer_->RetainFrameObjectEXT(queryHeap_.Get());
        renderer_->RetainFrameObjectEXT(readback_.Get());
        commandList->EndQuery(queryHeap_.Get(), D3D12_QUERY_TYPE_TIMESTAMP, 1);
        commandList->ResolveQueryData(queryHeap_.Get(), D3D12_QUERY_TYPE_TIMESTAMP,
                                      0, 2, readback_.Get(), 0);
        completionFenceValue_ = renderer_->GetActiveFrameFenceValueEXT();
        active_ = false;
    }

    bool D3D12GpuTimer::IsResultAvailable() const
    {
        if (aborted_ || ready_) return true;
        return completionFenceValue_ != 0 && renderer_ && renderer_->GetFenceEXT() &&
               renderer_->GetFenceEXT()->GetCompletedValue() >= completionFenceValue_;
    }

    std::uint64_t D3D12GpuTimer::ElapsedNanoseconds() const
    {
        if (!IsResultAvailable()) return 0;
        if (aborted_ || ready_) return elapsedNanoseconds_;

        const D3D12_RANGE readRange{0, 2 * sizeof(std::uint64_t)};
        void* mapped = nullptr;
        if (FAILED(readback_->Map(0, &readRange, &mapped))) return 0;
        std::uint64_t timestamps[2]{};
        std::memcpy(timestamps, mapped, sizeof(timestamps));
        const D3D12_RANGE writtenRange{0, 0};
        readback_->Unmap(0, &writtenRange);
        if (timestamps[1] >= timestamps[0] && frequency_ != 0)
            elapsedNanoseconds_ = static_cast<std::uint64_t>(
                static_cast<long double>(timestamps[1] - timestamps[0]) * 1.0e9L /
                static_cast<long double>(frequency_));
        ready_ = true;
        return elapsedNanoseconds_;
    }

    void D3D12GpuTimer::ReleaseDeviceResourcesEXT() noexcept
    {
        aborted_ = active_ || completionFenceValue_ != 0;
        active_ = false;
        ready_ = false;
        elapsedNanoseconds_ = 0;
        completionFenceValue_ = 0;
        frequency_ = 0;
        readback_.Reset();
        queryHeap_.Reset();
    }

    void D3D12GpuTimer::RecreateDeviceResourcesEXT()
    {
        CreateResources();
    }
}
