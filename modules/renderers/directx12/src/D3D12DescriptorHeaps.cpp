// SPDX-License-Identifier: MS-PL

#include "CNA/Internal/Renderers/DirectX12/D3D12DescriptorHeaps.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <stdexcept>

namespace CNA::Internal::Renderers::DirectX12
{
    namespace
    {
        std::string FormatHrLocal(HRESULT hr)
        {
            char buf[32];
            std::snprintf(buf, sizeof(buf), "0x%08lX", static_cast<unsigned long>(hr));
            return buf;
        }
    }

    bool D3D12DescriptorTraceEnabled()
    {
        static const bool enabled = []
        {
            const char* v = std::getenv("CNA_D3D12_DESCRIPTOR_TRACE");
            return v != nullptr && v[0] != '\0' && !(v[0] == '0' && v[1] == '\0');
        }();
        return enabled;
    }

    // -------------------------------------------------------------------------------------------
    // D3D12CpuDescriptorAllocator -- RTV/DSV, block-chained, handles stable for life
    // -------------------------------------------------------------------------------------------

    void D3D12CpuDescriptorAllocator::Initialize(ID3D12Device* device, D3D12_DESCRIPTOR_HEAP_TYPE type,
                                                 std::uint32_t blockCapacity,
                                                 const D3D12DescriptorFenceClock* clock, const char* name)
    {
        device_ = device;
        clock_ = clock;
        type_ = type;
        blockCapacity_ = blockCapacity > 0 ? blockCapacity : 1;
        name_ = name;
        descriptorSize_ = device_->GetDescriptorHandleIncrementSize(type_);
        blocks_.clear();
        freeList_.clear();
        pendingFree_.clear();
        nextIndex_ = 0;
        liveCount_ = 0;
        peakLive_ = 0;
        allocations_ = 0;
        recycles_ = 0;
        frees_ = 0;
        growths_ = 0;
        AppendBlock();
    }

    std::uint32_t D3D12CpuDescriptorAllocator::Capacity() const
    {
        return blocks_.empty() ? 0 : blocks_.back().firstIndex + blocks_.back().capacity;
    }

    void D3D12CpuDescriptorAllocator::AppendBlock()
    {
        // Each new block is as large as everything before it, so capacity doubles and the number of
        // heap objects stays logarithmic. A block is never freed or resized, which is precisely what
        // keeps every handle already handed out valid for the lifetime of the allocator.
        const std::uint32_t existing = Capacity();
        const std::uint32_t newBlockCapacity = existing == 0 ? blockCapacity_ : existing;

        D3D12_DESCRIPTOR_HEAP_DESC desc{};
        desc.Type = type_;
        desc.NumDescriptors = newBlockCapacity;
        desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;

        ComPtr<ID3D12DescriptorHeap> heap;
        const HRESULT hr = device_->CreateDescriptorHeap(&desc, IID_PPV_ARGS(heap.GetAddressOf()));
        if (FAILED(hr))
            throw std::runtime_error("D3D12CpuDescriptorAllocator(" + name_ +
                                     "): CreateDescriptorHeap failed, hr=" + FormatHrLocal(hr));
        blocks_.push_back(Block{heap, existing, newBlockCapacity});
        if (blocks_.size() > 1) ++growths_;
    }

    D3D12_CPU_DESCRIPTOR_HANDLE D3D12CpuDescriptorAllocator::HandleAt(std::uint32_t index) const
    {
        D3D12_CPU_DESCRIPTOR_HANDLE handle{};
        for (const Block& b : blocks_)
        {
            if (index < b.firstIndex || index >= b.firstIndex + b.capacity) continue;
            handle = b.heap->GetCPUDescriptorHandleForHeapStart();
            handle.ptr += static_cast<SIZE_T>(index - b.firstIndex) * descriptorSize_;
            return handle;
        }
        return handle;
    }

    bool D3D12CpuDescriptorAllocator::IndexOf(D3D12_CPU_DESCRIPTOR_HANDLE handle, std::uint32_t& outIndex) const
    {
        if (handle.ptr == 0) return false;
        for (const Block& b : blocks_)
        {
            const SIZE_T start = b.heap->GetCPUDescriptorHandleForHeapStart().ptr;
            const SIZE_T end = start + static_cast<SIZE_T>(b.capacity) * descriptorSize_;
            if (handle.ptr < start || handle.ptr >= end) continue;
            const SIZE_T offset = handle.ptr - start;
            if (offset % descriptorSize_ != 0) return false;
            outIndex = b.firstIndex + static_cast<std::uint32_t>(offset / descriptorSize_);
            return true;
        }
        return false;
    }

    void D3D12CpuDescriptorAllocator::ReclaimCompleted()
    {
        if (pendingFree_.empty()) return;
        const std::uint64_t completed = clock_ ? clock_->Completed() : 0;
        auto stillPending = std::remove_if(pendingFree_.begin(), pendingFree_.end(),
                                           [&](const PendingFree& p)
                                           {
                                               if (p.fenceStamp > completed) return false;
                                               freeList_.push_back(p.index);
                                               return true;
                                           });
        pendingFree_.erase(stillPending, pendingFree_.end());
    }

    D3D12_CPU_DESCRIPTOR_HANDLE D3D12CpuDescriptorAllocator::Allocate()
    {
        ReclaimCompleted();

        std::uint32_t index;
        if (!freeList_.empty())
        {
            index = freeList_.back();
            freeList_.pop_back();
            ++recycles_;
        }
        else
        {
            if (nextIndex_ >= Capacity()) AppendBlock();
            index = nextIndex_++;
        }

        ++allocations_;
        ++liveCount_;
        peakLive_ = std::max(peakLive_, liveCount_);
        Trace("alloc", index);
        return HandleAt(index);
    }

    void D3D12CpuDescriptorAllocator::Free(D3D12_CPU_DESCRIPTOR_HANDLE handle)
    {
        std::uint32_t index = 0;
        if (!IndexOf(handle, index)) return;
        ++frees_;
        if (liveCount_ > 0) --liveCount_;
        pendingFree_.push_back({index, clock_ ? clock_->lastSubmitted : 0});
        Trace("free", index);
    }

    D3D12DescriptorHeapStats D3D12CpuDescriptorAllocator::GetStatsEXT() const
    {
        const std::uint32_t capacity = Capacity();
        D3D12DescriptorHeapStats s;
        s.capacity = capacity;
        s.live = liveCount_;
        s.peakLive = peakLive_;
        s.neverUsed = capacity - nextIndex_;
        s.freeReady = static_cast<std::uint32_t>(freeList_.size());
        s.freePending = static_cast<std::uint32_t>(pendingFree_.size());
        s.allocations = allocations_;
        s.recycles = recycles_;
        s.frees = frees_;
        s.growths = growths_;
        s.heapObjects = static_cast<std::uint32_t>(blocks_.size());
        s.generation = 0; // block-chained: an issued handle never moves
        return s;
    }

    void D3D12CpuDescriptorAllocator::Trace(const char* event, std::uint32_t index) const
    {
        if (!D3D12DescriptorTraceEnabled()) return;
        const D3D12DescriptorHeapStats s = GetStatsEXT();
        std::fprintf(stderr,
                     "[D3D12-DESC] heap=%s kind=cpu-blocks event=%-5s index=%u cap=%u cursor=%u live=%u "
                     "peak=%u freeReady=%u freePending=%u alloc=%llu recycle=%llu free=%llu blocks=%u "
                     "fenceSubmitted=%llu fenceCompleted=%llu\n",
                     name_.c_str(), event, index, s.capacity, nextIndex_, s.live, s.peakLive, s.freeReady,
                     s.freePending, static_cast<unsigned long long>(s.allocations),
                     static_cast<unsigned long long>(s.recycles), static_cast<unsigned long long>(s.frees),
                     s.heapObjects,
                     static_cast<unsigned long long>(clock_ ? clock_->lastSubmitted : 0),
                     static_cast<unsigned long long>(clock_ ? clock_->Completed() : 0));
        std::fflush(stderr);
    }

    // -------------------------------------------------------------------------------------------
    // D3D12ShaderVisibleDescriptorAllocator -- CBV/SRV/UAV and SAMPLER, staging + mirrored heap
    // -------------------------------------------------------------------------------------------

    void D3D12ShaderVisibleDescriptorAllocator::Initialize(ID3D12Device* device,
                                                           D3D12_DESCRIPTOR_HEAP_TYPE type,
                                                           std::uint32_t initialCapacity,
                                                           std::uint32_t maxCapacity,
                                                           const D3D12DescriptorFenceClock* clock,
                                                           const char* name)
    {
        device_ = device;
        clock_ = clock;
        type_ = type;
        maxCapacity_ = maxCapacity;
        capacity_ = std::min(initialCapacity > 0 ? initialCapacity : 1, maxCapacity_);
        name_ = name;
        descriptorSize_ = device_->GetDescriptorHandleIncrementSize(type_);
        freeList_.clear();
        pendingFree_.clear();
        retired_.clear();
        nextIndex_ = 0;
        liveCount_ = 0;
        peakLive_ = 0;
        allocations_ = 0;
        recycles_ = 0;
        frees_ = 0;
        publishes_ = 0;
        bulkCopies_ = 0;
        growths_ = 0;
        generation_ = 0;
        CreateHeapPair(capacity_, stagingHeap_, gpuHeap_);
    }

    void D3D12ShaderVisibleDescriptorAllocator::CreateHeapPair(std::uint32_t capacity,
                                                               ComPtr<ID3D12DescriptorHeap>& outStaging,
                                                               ComPtr<ID3D12DescriptorHeap>& outGpu) const
    {
        D3D12_DESCRIPTOR_HEAP_DESC desc{};
        desc.Type = type_;
        desc.NumDescriptors = capacity;

        desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
        HRESULT hr = device_->CreateDescriptorHeap(&desc, IID_PPV_ARGS(outStaging.ReleaseAndGetAddressOf()));
        if (FAILED(hr))
            throw std::runtime_error("D3D12ShaderVisibleDescriptorAllocator(" + name_ +
                                     "): CreateDescriptorHeap(staging) failed, hr=" + FormatHrLocal(hr));

        desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        hr = device_->CreateDescriptorHeap(&desc, IID_PPV_ARGS(outGpu.ReleaseAndGetAddressOf()));
        if (FAILED(hr))
            throw std::runtime_error("D3D12ShaderVisibleDescriptorAllocator(" + name_ +
                                     "): CreateDescriptorHeap(shader-visible) failed, hr=" + FormatHrLocal(hr));
    }

    void D3D12ShaderVisibleDescriptorAllocator::ReclaimCompleted()
    {
        const std::uint64_t completed = clock_ ? clock_->Completed() : 0;

        if (!pendingFree_.empty())
        {
            auto stillPending = std::remove_if(pendingFree_.begin(), pendingFree_.end(),
                                               [&](const PendingFree& p)
                                               {
                                                   if (p.fenceStamp > completed) return false;
                                                   freeList_.push_back(p.index);
                                                   return true;
                                               });
            pendingFree_.erase(stillPending, pendingFree_.end());
        }

        if (!retired_.empty())
        {
            auto stillHeld = std::remove_if(retired_.begin(), retired_.end(),
                                            [&](const RetiredHeaps& r) { return r.fenceStamp <= completed; });
            retired_.erase(stillHeld, retired_.end());
        }
    }

    void D3D12ShaderVisibleDescriptorAllocator::Grow()
    {
        if (capacity_ >= maxCapacity_)
            throw std::runtime_error("D3D12ShaderVisibleDescriptorAllocator(" + name_ +
                                     "): live descriptor demand exceeds the D3D12 maximum shader-visible "
                                     "heap size for this type (" + std::to_string(maxCapacity_) + ")");

        const std::uint32_t newCapacity =
            capacity_ > maxCapacity_ / 2 ? maxCapacity_ : capacity_ * 2;

        ComPtr<ID3D12DescriptorHeap> newStaging;
        ComPtr<ID3D12DescriptorHeap> newGpu;
        CreateHeapPair(newCapacity, newStaging, newGpu);

        if (nextIndex_ > 0)
        {
            // The staging heap is the authoritative copy AND the only legal copy source: D3D12
            // forbids CopyDescriptors from a shader-visible heap, so the shader-visible successor is
            // mirrored from the staging heap rather than from its own predecessor.
            device_->CopyDescriptorsSimple(nextIndex_,
                                           newStaging->GetCPUDescriptorHandleForHeapStart(),
                                           stagingHeap_->GetCPUDescriptorHandleForHeapStart(), type_);
            device_->CopyDescriptorsSimple(nextIndex_,
                                           newGpu->GetCPUDescriptorHandleForHeapStart(),
                                           newStaging->GetCPUDescriptorHandleForHeapStart(), type_);
            bulkCopies_ += 2ull * nextIndex_;
        }

        // The heaps being replaced may still be bound by a submitted command list, so they are
        // retired against the newest submission rather than released here.
        retired_.push_back({stagingHeap_, gpuHeap_, clock_ ? clock_->lastSubmitted : 0});
        stagingHeap_ = newStaging;
        gpuHeap_ = newGpu;
        capacity_ = newCapacity;
        ++growths_;
        ++generation_;
        Trace("grow", nextIndex_);
    }

    std::uint32_t D3D12ShaderVisibleDescriptorAllocator::Allocate()
    {
        ReclaimCompleted();

        std::uint32_t index;
        if (!freeList_.empty())
        {
            index = freeList_.back();
            freeList_.pop_back();
            ++recycles_;
        }
        else
        {
            if (nextIndex_ >= capacity_) Grow();
            index = nextIndex_++;
        }

        ++allocations_;
        ++liveCount_;
        peakLive_ = std::max(peakLive_, liveCount_);
        Trace("alloc", index);
        return index;
    }

    void D3D12ShaderVisibleDescriptorAllocator::Free(std::uint32_t index)
    {
        if (index == kInvalidIndex || index >= nextIndex_) return;
        ++frees_;
        if (liveCount_ > 0) --liveCount_;
        pendingFree_.push_back({index, clock_ ? clock_->lastSubmitted : 0});
        Trace("free", index);
    }

    D3D12_CPU_DESCRIPTOR_HANDLE D3D12ShaderVisibleDescriptorAllocator::StagingCpuHandle(std::uint32_t index) const
    {
        D3D12_CPU_DESCRIPTOR_HANDLE handle{};
        if (!stagingHeap_ || index >= capacity_) return handle;
        handle = stagingHeap_->GetCPUDescriptorHandleForHeapStart();
        handle.ptr += static_cast<SIZE_T>(index) * descriptorSize_;
        return handle;
    }

    void D3D12ShaderVisibleDescriptorAllocator::Publish(std::uint32_t index)
    {
        if (!stagingHeap_ || !gpuHeap_ || index >= capacity_) return;
        D3D12_CPU_DESCRIPTOR_HANDLE dst = gpuHeap_->GetCPUDescriptorHandleForHeapStart();
        dst.ptr += static_cast<SIZE_T>(index) * descriptorSize_;
        device_->CopyDescriptorsSimple(1, dst, StagingCpuHandle(index), type_);
        ++publishes_;
        Trace("publish", index);
    }

    D3D12_GPU_DESCRIPTOR_HANDLE D3D12ShaderVisibleDescriptorAllocator::GpuHandle(std::uint32_t index) const
    {
        D3D12_GPU_DESCRIPTOR_HANDLE handle{};
        if (!gpuHeap_ || index >= capacity_) return handle;
        handle = gpuHeap_->GetGPUDescriptorHandleForHeapStart();
        handle.ptr += static_cast<UINT64>(index) * descriptorSize_;
        return handle;
    }

    D3D12DescriptorHeapStats D3D12ShaderVisibleDescriptorAllocator::GetStatsEXT() const
    {
        D3D12DescriptorHeapStats s;
        s.capacity = capacity_;
        s.live = liveCount_;
        s.peakLive = peakLive_;
        s.neverUsed = capacity_ - nextIndex_;
        s.freeReady = static_cast<std::uint32_t>(freeList_.size());
        s.freePending = static_cast<std::uint32_t>(pendingFree_.size());
        s.allocations = allocations_;
        s.recycles = recycles_;
        s.frees = frees_;
        s.growths = growths_;
        // One staging + one shader-visible pair, plus any retired pair not yet released.
        s.heapObjects = 2u + 2u * static_cast<std::uint32_t>(retired_.size());
        s.generation = generation_;
        s.publishes = publishes_;
        s.bulkCopies = bulkCopies_;
        return s;
    }

    void D3D12ShaderVisibleDescriptorAllocator::Trace(const char* event, std::uint32_t index) const
    {
        if (!D3D12DescriptorTraceEnabled()) return;
        const D3D12DescriptorHeapStats s = GetStatsEXT();
        std::fprintf(stderr,
                     "[D3D12-DESC] heap=%s kind=shader-visible event=%-7s index=%u cap=%u cursor=%u live=%u "
                     "peak=%u freeReady=%u freePending=%u alloc=%llu recycle=%llu free=%llu publish=%llu "
                     "bulk=%llu grow=%u gen=%u heapObjs=%u retired=%u gpuHeap=%p "
                     "fenceSubmitted=%llu fenceCompleted=%llu\n",
                     name_.c_str(), event, index, s.capacity, nextIndex_, s.live, s.peakLive, s.freeReady,
                     s.freePending, static_cast<unsigned long long>(s.allocations),
                     static_cast<unsigned long long>(s.recycles), static_cast<unsigned long long>(s.frees),
                     static_cast<unsigned long long>(s.publishes),
                     static_cast<unsigned long long>(s.bulkCopies), s.growths, s.generation, s.heapObjects,
                     static_cast<unsigned>(retired_.size()), static_cast<const void*>(gpuHeap_.Get()),
                     static_cast<unsigned long long>(clock_ ? clock_->lastSubmitted : 0),
                     static_cast<unsigned long long>(clock_ ? clock_->Completed() : 0));
        std::fflush(stderr);
    }
}
