// SPDX-License-Identifier: MS-PL
#include "CNA/Internal/Renderers/DirectX12/D3D12StorageBuffer.hpp"

#include "CNA/Internal/Renderers/DirectX12/DirectX12Renderer.hpp"

#include <cstring>
#include <limits>
#include <stdexcept>

namespace CNA::Internal::Renderers::DirectX12
{
    namespace
    {
        constexpr std::uint32_t kTransferSource = UINT32_C(0x02);
        constexpr std::uint32_t kTransferDestination = UINT32_C(0x04);
        constexpr std::uint32_t kStorage = UINT32_C(0x01);
        constexpr std::uint32_t kIndirectArguments = UINT32_C(0x08);
        constexpr std::uint32_t kConstant = UINT32_C(0x40);
        constexpr std::uint32_t kSupportedUsage =
            kStorage | kTransferSource | kTransferDestination |
            kIndirectArguments | kConstant;
        constexpr std::uint32_t kCpuRead = UINT32_C(0x01);
        constexpr std::uint32_t kCpuWrite = UINT32_C(0x02);

        bool FitsRange(std::size_t total, std::size_t offset, std::size_t length)
        {
            return offset <= total && length <= total - offset;
        }

        D3D12_RESOURCE_DESC BufferDescription(std::size_t byteSize,
                                              D3D12_RESOURCE_FLAGS flags = D3D12_RESOURCE_FLAG_NONE)
        {
            D3D12_RESOURCE_DESC desc{};
            desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
            desc.Width = static_cast<UINT64>(byteSize);
            desc.Height = 1;
            desc.DepthOrArraySize = 1;
            desc.MipLevels = 1;
            desc.Format = DXGI_FORMAT_UNKNOWN;
            desc.SampleDesc.Count = 1;
            desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
            desc.Flags = flags;
            return desc;
        }
    }

    D3D12StorageBuffer::D3D12StorageBuffer(
        DirectX12Renderer* renderer, std::size_t byteSize,
        std::uint32_t usage, std::uint32_t cpuAccess)
        : renderer_(renderer, renderer ? renderer->GetLifetimeTokenEXT() : std::weak_ptr<void>{},
                    "D3D12StorageBuffer"),
          device_(renderer ? renderer->GetDeviceEXT() : nullptr),
          byteSize_(byteSize), usage_(usage), cpuAccess_(cpuAccess)
    {
        if (!renderer || !device_ || byteSize == 0 || usage == 0 ||
            (usage & ~kSupportedUsage) != 0 || (cpuAccess & ~(kCpuRead | kCpuWrite)) != 0)
            throw std::invalid_argument("D3D12 storage buffer: invalid descriptor");
        CreateResource();
        renderer_->RegisterRecoverableResourceEXT(this);
    }

    D3D12StorageBuffer::~D3D12StorageBuffer()
    {
        ReleaseDeviceResourcesEXT();
        if (renderer_)
            renderer_->UnregisterRecoverableResourceEXT(this);
    }

    void D3D12StorageBuffer::CreateResource()
    {
        D3D12_HEAP_PROPERTIES heap{};
        heap.Type = D3D12_HEAP_TYPE_DEFAULT;
        const std::size_t alignment = (usage_ & kConstant) != 0 ? 256 : 4;
        if (byteSize_ > std::numeric_limits<std::size_t>::max() - (alignment - 1))
            throw std::invalid_argument("D3D12 storage buffer: size overflows alignment");
        const std::size_t alignedSize =
            (byteSize_ + alignment - 1) & ~(alignment - 1);
        if ((usage_ & kStorage) != 0 &&
            alignedSize / 4 > std::numeric_limits<UINT>::max())
            throw std::invalid_argument("D3D12 storage buffer: raw view count exceeds UINT");
        const auto desc = BufferDescription(
            alignedSize, (usage_ & kStorage) != 0
                ? D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS
                : D3D12_RESOURCE_FLAG_NONE);
        const HRESULT hr = device_->CreateCommittedResource(
            &heap, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_COMMON,
            nullptr, IID_PPV_ARGS(buffer_.ReleaseAndGetAddressOf()));
        if (FAILED(hr))
            throw std::runtime_error("D3D12 storage buffer: DEFAULT heap allocation failed");
        buffer_->SetName(L"CNA StorageBuffer resource");
        renderer_->GetResourceStateTrackerEXT().TrackResource(
            buffer_.Get(), D3D12_RESOURCE_STATE_COMMON);
        if ((usage_ & kStorage) == 0) return;

        heaps_ = renderer_->GetDescriptorHeapsEXT();
        try
        {
            D3D12_UNORDERED_ACCESS_VIEW_DESC uav{};
            uav.Format = DXGI_FORMAT_R32_TYPELESS;
            uav.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
            uav.Buffer.NumElements = static_cast<UINT>(alignedSize / 4);
            uav.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_RAW;
            uavIndex_ = renderer_->CreateCbvSrvUavDescriptorEXT(
                [&](D3D12_CPU_DESCRIPTOR_HANDLE handle)
                {
                    device_->CreateUnorderedAccessView(buffer_.Get(), nullptr, &uav, handle);
                });

            D3D12_SHADER_RESOURCE_VIEW_DESC srv{};
            srv.Format = DXGI_FORMAT_R32_TYPELESS;
            srv.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
            srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
            srv.Buffer.NumElements = static_cast<UINT>(alignedSize / 4);
            srv.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_RAW;
            srvIndex_ = renderer_->CreateCbvSrvUavDescriptorEXT(
                [&](D3D12_CPU_DESCRIPTOR_HANDLE handle)
                {
                    device_->CreateShaderResourceView(buffer_.Get(), &srv, handle);
                });
        }
        catch (...)
        {
            if (uavIndex_ != D3D12ShaderVisibleDescriptorAllocator::kInvalidIndex)
                heaps_->cbvSrvUav.Free(uavIndex_);
            uavIndex_ = D3D12ShaderVisibleDescriptorAllocator::kInvalidIndex;
            heaps_.reset();
            renderer_->GetResourceStateTrackerEXT().UntrackResource(buffer_.Get());
            buffer_.Reset();
            throw;
        }
    }

    void D3D12StorageBuffer::UploadRange(
        std::size_t byteOffset, const void* data, std::size_t byteSize)
    {
        const auto upload = renderer_->AllocateFrameUploadEXT(byteSize, 4);
        std::memcpy(upload.mapped, data, byteSize);
        ID3D12GraphicsCommandList* commands = renderer_->GetFrameCommandListEXT();
        renderer_->RetainFrameObjectEXT(buffer_.Get());
        auto& states = renderer_->GetResourceStateTrackerEXT();
        const auto previous = states.GetTrackedStateEXT(buffer_.Get());
        states.TransitionTo(commands, buffer_.Get(), D3D12_RESOURCE_STATE_COPY_DEST);
        commands->CopyBufferRegion(buffer_.Get(), static_cast<UINT64>(byteOffset),
                                   upload.resource, upload.offset, static_cast<UINT64>(byteSize));
        states.TransitionTo(commands, buffer_.Get(), previous);
    }

    void D3D12StorageBuffer::SetData(const void* data, std::size_t byteSize)
    {
        if (!SetDataRangeEXT(0, data, byteSize))
            throw std::runtime_error("D3D12 storage buffer: upload refused");
    }

    void D3D12StorageBuffer::GetData(void* out, std::size_t byteSize) const
    {
        if (!GetDataRangeEXT(0, out, byteSize))
            throw std::runtime_error("D3D12 storage buffer: readback refused");
    }

    bool D3D12StorageBuffer::SetDataRangeEXT(
        std::size_t byteOffset, const void* data, std::size_t byteSize)
    {
        if ((cpuAccess_ & kCpuWrite) == 0 ||
            !FitsRange(byteSize_, byteOffset, byteSize) ||
            (byteSize != 0 && data == nullptr))
            return false;
        if (byteSize == 0) return true;
        if (recoveryShadow_.empty()) recoveryShadow_.resize(byteSize_, 0);
        UploadRange(byteOffset, data, byteSize);
        std::memcpy(recoveryShadow_.data() + byteOffset, data, byteSize);
        return true;
    }

    bool D3D12StorageBuffer::GetDataRangeEXT(
        std::size_t byteOffset, void* out, std::size_t byteSize) const
    {
        if ((cpuAccess_ & kCpuRead) == 0 ||
            !FitsRange(byteSize_, byteOffset, byteSize) ||
            (byteSize != 0 && out == nullptr))
            return false;
        if (byteSize == 0) return true;

        D3D12_HEAP_PROPERTIES heap{};
        heap.Type = D3D12_HEAP_TYPE_READBACK;
        const auto desc = BufferDescription(byteSize);
        Microsoft::WRL::ComPtr<ID3D12Resource> readback;
        HRESULT hr = device_->CreateCommittedResource(
            &heap, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_COPY_DEST,
            nullptr, IID_PPV_ARGS(readback.GetAddressOf()));
        if (FAILED(hr)) return false;

        ID3D12GraphicsCommandList* commands = renderer_->BeginImmediateCommandsEXT();
        auto& states = renderer_->GetResourceStateTrackerEXT();
        const auto previous = states.GetTrackedStateEXT(buffer_.Get());
        states.TransitionTo(commands, buffer_.Get(), D3D12_RESOURCE_STATE_COPY_SOURCE);
        commands->CopyBufferRegion(readback.Get(), 0, buffer_.Get(),
                                   static_cast<UINT64>(byteOffset),
                                   static_cast<UINT64>(byteSize));
        states.TransitionTo(commands, buffer_.Get(), previous);
        hr = commands->Close();
        if (FAILED(hr)) return false;
        renderer_->ExecuteCommandListAndWaitEXT(commands);

        void* mapped = nullptr;
        const D3D12_RANGE readRange{0, byteSize};
        hr = readback->Map(0, &readRange, &mapped);
        if (FAILED(hr)) return false;
        std::memcpy(out, mapped, byteSize);
        const D3D12_RANGE noWrite{0, 0};
        readback->Unmap(0, &noWrite);
        return true;
    }

    bool D3D12StorageBuffer::CopyToEXT(
        IStorageBufferRenderer& destination,
        std::size_t sourceByteOffset, std::size_t destinationByteOffset,
        std::size_t byteSize)
    {
        auto* target = dynamic_cast<D3D12StorageBuffer*>(&destination);
        if (!target || target->renderer_.Get() != renderer_.Get() ||
            (usage_ & kTransferSource) == 0 ||
            (target->usage_ & kTransferDestination) == 0 ||
            !FitsRange(byteSize_, sourceByteOffset, byteSize) ||
            !FitsRange(target->byteSize_, destinationByteOffset, byteSize))
            return false;
        if (byteSize == 0) return true;

        if (target == this)
        {
            if (sourceByteOffset < destinationByteOffset + byteSize &&
                destinationByteOffset < sourceByteOffset + byteSize)
                return false;
            D3D12_HEAP_PROPERTIES heap{};
            heap.Type = D3D12_HEAP_TYPE_DEFAULT;
            const auto desc = BufferDescription(byteSize);
            Microsoft::WRL::ComPtr<ID3D12Resource> scratch;
            const HRESULT hr = device_->CreateCommittedResource(
                &heap, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_COMMON,
                nullptr, IID_PPV_ARGS(scratch.GetAddressOf()));
            if (FAILED(hr)) return false;

            ID3D12GraphicsCommandList* commands = renderer_->GetFrameCommandListEXT();
            renderer_->RetainFrameObjectEXT(buffer_.Get());
            renderer_->RetainFrameObjectEXT(scratch.Get());
            auto& states = renderer_->GetResourceStateTrackerEXT();
            const auto previous = states.GetTrackedStateEXT(buffer_.Get());
            states.TrackResource(scratch.Get(), D3D12_RESOURCE_STATE_COMMON);
            states.TransitionTo(commands, scratch.Get(), D3D12_RESOURCE_STATE_COPY_DEST);
            states.TransitionTo(commands, buffer_.Get(), D3D12_RESOURCE_STATE_COPY_SOURCE);
            commands->CopyBufferRegion(scratch.Get(), 0, buffer_.Get(),
                                       static_cast<UINT64>(sourceByteOffset),
                                       static_cast<UINT64>(byteSize));
            states.TransitionTo(commands, scratch.Get(), D3D12_RESOURCE_STATE_COPY_SOURCE);
            states.TransitionTo(commands, buffer_.Get(), D3D12_RESOURCE_STATE_COPY_DEST);
            commands->CopyBufferRegion(buffer_.Get(),
                                       static_cast<UINT64>(destinationByteOffset),
                                       scratch.Get(), 0, static_cast<UINT64>(byteSize));
            states.TransitionTo(commands, buffer_.Get(), previous);
            states.UntrackResource(scratch.Get());
            if (recoveryShadow_.size() == byteSize_)
                std::memmove(recoveryShadow_.data() + destinationByteOffset,
                             recoveryShadow_.data() + sourceByteOffset, byteSize);
            return true;
        }

        ID3D12GraphicsCommandList* commands = renderer_->GetFrameCommandListEXT();
        renderer_->RetainFrameObjectEXT(buffer_.Get());
        renderer_->RetainFrameObjectEXT(target->buffer_.Get());
        auto& states = renderer_->GetResourceStateTrackerEXT();
        const auto sourcePrevious = states.GetTrackedStateEXT(buffer_.Get());
        const auto targetPrevious = states.GetTrackedStateEXT(target->buffer_.Get());
        states.TransitionTo(commands, buffer_.Get(), D3D12_RESOURCE_STATE_COPY_SOURCE);
        states.TransitionTo(commands, target->buffer_.Get(), D3D12_RESOURCE_STATE_COPY_DEST);
        commands->CopyBufferRegion(target->buffer_.Get(),
                                   static_cast<UINT64>(destinationByteOffset),
                                   buffer_.Get(), static_cast<UINT64>(sourceByteOffset),
                                   static_cast<UINT64>(byteSize));
        states.TransitionTo(commands, buffer_.Get(), sourcePrevious);
        states.TransitionTo(commands, target->buffer_.Get(), targetPrevious);

        if (recoveryShadow_.size() == byteSize_)
        {
            if (target->recoveryShadow_.empty())
                target->recoveryShadow_.resize(target->byteSize_, 0);
            std::memcpy(target->recoveryShadow_.data() + destinationByteOffset,
                        recoveryShadow_.data() + sourceByteOffset, byteSize);
        }
        else
            target->recoveryShadow_.clear();
        return true;
    }

    void D3D12StorageBuffer::ReleaseDeviceResourcesEXT() noexcept
    {
        if (renderer_ && buffer_)
            renderer_->GetResourceStateTrackerEXT().UntrackResource(buffer_.Get());
        if (heaps_)
        {
            if (uavIndex_ != D3D12ShaderVisibleDescriptorAllocator::kInvalidIndex)
                heaps_->cbvSrvUav.Free(uavIndex_);
            if (srvIndex_ != D3D12ShaderVisibleDescriptorAllocator::kInvalidIndex)
                heaps_->cbvSrvUav.Free(srvIndex_);
        }
        uavIndex_ = D3D12ShaderVisibleDescriptorAllocator::kInvalidIndex;
        srvIndex_ = D3D12ShaderVisibleDescriptorAllocator::kInvalidIndex;
        heaps_.reset();
        buffer_.Reset();
        device_.Reset();
    }

    void D3D12StorageBuffer::RecreateDeviceResourcesEXT()
    {
        device_ = renderer_->GetDeviceEXT();
        CreateResource();
        if (!recoveryShadow_.empty())
            UploadRange(0, recoveryShadow_.data(), recoveryShadow_.size());
    }
}
