// SPDX-License-Identifier: MS-PL
#pragma once

// REMED-GFX-177: bounded, fence-safe D3D12 descriptor allocation.
//
// DX-103 gave this renderer four descriptor heaps and one monotonic bump cursor each, with no free
// list and no growth -- every heap slot handed out was consumed for the lifetime of the process,
// whether or not the resource that asked for it was still alive. A `Texture2D`, `TextureCube`,
// `Texture3D`, `RenderTarget2D` or `RenderTargetCube` takes exactly one CBV/SRV/UAV slot at
// construction and released none at destruction, so the 65th sampleable resource EVER CREATED threw
// even when six were live. This file replaces those four cursors with two allocators that reclaim
// and grow, keeping DX-103's original capacities as the STARTING size rather than the ceiling.
//
// Two allocators, because D3D12 imposes two genuinely different constraints:
//
//  * RTV and DSV descriptors are never bound as a heap -- OMSetRenderTargets takes loose CPU
//    handles and does not care which heap each came from. A CHAIN OF FIXED-SIZE BLOCKS is therefore
//    correct and strictly better than resizing: an already-issued handle stays valid forever, so
//    nothing that stores one has to be told about growth. D3D12CpuDescriptorAllocator.
//
//  * A shader-visible CBV/SRV/UAV or SAMPLER heap is bound whole (only one of each type at a time),
//    so its descriptors must be contiguous and a table may only point into the heap that is
//    currently bound. Blocks are impossible; the heap has to be replaced. D3D12 also forbids
//    CopyDescriptors from a shader-visible SOURCE, so a shader-visible heap cannot be copied into
//    its own larger successor. D3D12ShaderVisibleDescriptorAllocator therefore keeps the
//    authoritative copy of every descriptor in a NON-shader-visible staging heap and mirrors it into
//    the shader-visible one; growth re-mirrors the whole live range from the staging heap, which is
//    a legal copy source. Callers hold a stable INDEX, never a raw handle, so a replaced heap
//    invalidates nothing they own.
//
// Fence safety. A descriptor referenced by a submitted command list must stay valid until the GPU
// has finished with it, and a heap object must outlive every command list that bound it. Both
// allocators stamp a freed index (and a retired heap) with the fence value of the last submission
// that could possibly reference it, and only recycle/release once the shared fence has passed that
// value. This renderer currently submits synchronously (ExecuteCommandListAndWaitEXT), so in
// practice the stamp is always already complete -- the machinery is written to the real D3D12 rule
// rather than to that simplification, so it stays correct if submission ever becomes asynchronous.
//
// Nothing here waits, idles the device, submits, or allocates a heap per draw: growth is driven only
// by simultaneous live demand that the free list cannot satisfy.

#include <d3d12.h>
#include <wrl/client.h>

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace CNA::Internal::Renderers::DirectX12
{
    using Microsoft::WRL::ComPtr;

    /**
     * @brief CNAEXT. The shared fence reading that both allocators use to decide when a freed
     *        descriptor slot or a retired heap is safe to reuse or release.
     *
     * Owned by D3D12DescriptorHeaps and pointed at (never copied) by each allocator, so the two
     * always agree on what the GPU has completed.
     */
    struct D3D12DescriptorFenceClock
    {
        /** @brief The renderer's shared fence. Held as a ComPtr so the clock stays usable even if
         *  the renderer is destroyed before the last resource that frees a descriptor. */
        ComPtr<ID3D12Fence> fence;
        /** @brief The highest fence value signalled so far -- the newest submission that could
         *  still be referencing a descriptor freed right now. */
        std::uint64_t lastSubmitted = 0;

        /** @brief The highest fence value the GPU has actually reached, or lastSubmitted when there
         *  is no fence (an uninitialised or already-torn-down device), so teardown never blocks. */
        [[nodiscard]] std::uint64_t Completed() const
        {
            return fence ? fence->GetCompletedValue() : lastSubmitted;
        }
    };

    /**
     * @brief CNAEXT. Per-heap counters, exposed for tests and for the CNA_D3D12_DESCRIPTOR_TRACE
     *        output. Every field is a real count taken from the allocator, never an estimate.
     */
    struct D3D12DescriptorHeapStats
    {
        /** @brief Total descriptor slots currently addressable across every live heap/block. */
        std::uint32_t capacity = 0;
        /** @brief Slots handed out and not yet freed. */
        std::uint32_t live = 0;
        /** @brief The largest value @ref live has ever reached. */
        std::uint32_t peakLive = 0;
        /** @brief Slots that have never been handed out (the bump cursor's remaining range). */
        std::uint32_t neverUsed = 0;
        /** @brief Freed slots immediately available again. */
        std::uint32_t freeReady = 0;
        /** @brief Freed slots still waiting for the GPU to pass their fence stamp. */
        std::uint32_t freePending = 0;
        /** @brief Lifetime count of Allocate() calls. */
        std::uint64_t allocations = 0;
        /** @brief Lifetime count of Allocate() calls served from the free list rather than the
         *  bump cursor -- the number of slots that would each have leaked before this task. */
        std::uint64_t recycles = 0;
        /** @brief Lifetime count of Free() calls. */
        std::uint64_t frees = 0;
        /** @brief How many times capacity had to grow. */
        std::uint32_t growths = 0;
        /** @brief How many native ID3D12DescriptorHeap objects are currently alive (blocks for the
         *  CPU allocator, current + not-yet-released retired for the shader-visible one). */
        std::uint32_t heapObjects = 0;
        /** @brief Bumped every time the shader-visible heap object is replaced; always 0 for the
         *  block-chained CPU allocator, whose handles never move. */
        std::uint32_t generation = 0;
        /** @brief Lifetime count of single-descriptor staging -> shader-visible copies. */
        std::uint64_t publishes = 0;
        /** @brief Lifetime count of descriptors re-mirrored in bulk by a growth. */
        std::uint64_t bulkCopies = 0;
    };

    /**
     * @brief CNAEXT. A growable RTV/DSV descriptor allocator whose handles never move.
     *
     * Capacity grows by appending another fixed-size heap block, so a CPU handle issued from an
     * earlier block stays valid for the lifetime of the allocator. Freed handles go back to a
     * fence-stamped free list and are reissued before a new block is created, so a workload that
     * repeatedly creates and destroys render targets reaches a bounded steady state.
     */
    class D3D12CpuDescriptorAllocator
    {
    public:
        /**
         * @brief Prepares the allocator and creates its first block.
         *
         * @param device Device to create heap blocks from.
         * @param type RTV or DSV -- the only two non-shader-visible types this renderer uses.
         * @param blockCapacity Descriptors per block; also the starting capacity.
         * @param clock Shared fence reading used to decide when a freed handle may be reissued.
         * @param name Short heap name used by the CNA_D3D12_DESCRIPTOR_TRACE output.
         */
        void Initialize(ID3D12Device* device, D3D12_DESCRIPTOR_HEAP_TYPE type, std::uint32_t blockCapacity,
                        const D3D12DescriptorFenceClock* clock, const char* name);

        /**
         * @brief Returns a free descriptor handle, reusing a completed one when possible and
         *        appending a block only when live demand genuinely exceeds current capacity.
         *
         * @return A CPU descriptor handle that stays valid until it is passed to Free().
         */
        [[nodiscard]] D3D12_CPU_DESCRIPTOR_HANDLE Allocate();

        /**
         * @brief Returns a handle to the allocator, reusable once the GPU has passed the fence value
         *        current at the time of the call. A null or foreign handle is ignored.
         *
         * @param handle A handle previously returned by Allocate().
         */
        void Free(D3D12_CPU_DESCRIPTOR_HANDLE handle);

        /** @brief Real, measured counters for this heap.
         *  @return A snapshot of the allocator's own state. */
        [[nodiscard]] D3D12DescriptorHeapStats GetStatsEXT() const;

    private:
        struct PendingFree
        {
            std::uint32_t index;
            std::uint64_t fenceStamp;
        };
        /// One heap object plus the half-open index range it owns. Blocks double in size, so the
        /// number of heap objects stays logarithmic in capacity while every issued handle stays put.
        struct Block
        {
            ComPtr<ID3D12DescriptorHeap> heap;
            std::uint32_t firstIndex;
            std::uint32_t capacity;
        };

        void ReclaimCompleted();
        void AppendBlock();
        [[nodiscard]] bool IndexOf(D3D12_CPU_DESCRIPTOR_HANDLE handle, std::uint32_t& outIndex) const;
        [[nodiscard]] D3D12_CPU_DESCRIPTOR_HANDLE HandleAt(std::uint32_t index) const;
        [[nodiscard]] std::uint32_t Capacity() const;
        void Trace(const char* event, std::uint32_t index) const;

        ID3D12Device* device_ = nullptr;
        const D3D12DescriptorFenceClock* clock_ = nullptr;
        D3D12_DESCRIPTOR_HEAP_TYPE type_ = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
        std::uint32_t blockCapacity_ = 0;
        std::uint32_t descriptorSize_ = 0;
        std::string name_;
        std::vector<Block> blocks_;
        std::uint32_t nextIndex_ = 0;
        std::uint32_t liveCount_ = 0;
        std::uint32_t peakLive_ = 0;
        std::vector<std::uint32_t> freeList_;
        std::vector<PendingFree> pendingFree_;
        std::uint64_t allocations_ = 0;
        std::uint64_t recycles_ = 0;
        std::uint64_t frees_ = 0;
        std::uint32_t growths_ = 0;
    };

    /**
     * @brief CNAEXT. A growable shader-visible CBV/SRV/UAV or SAMPLER allocator addressed by stable
     *        index rather than by handle.
     *
     * The authoritative copy of every descriptor lives in a non-shader-visible staging heap; the
     * shader-visible heap is a mirror of it at identical indices. Publish() copies one descriptor
     * across after it is written; growth creates a larger pair and re-mirrors the whole live range
     * from the staging heap (D3D12 forbids copying OUT of a shader-visible heap, which is exactly
     * why the staging heap exists). Indices survive growth, so callers never hold a stale handle.
     */
    class D3D12ShaderVisibleDescriptorAllocator
    {
    public:
        /** @brief The value Allocate() never returns and Free() ignores. */
        static constexpr std::uint32_t kInvalidIndex = 0xFFFFFFFFu;

        /**
         * @brief Prepares the allocator and creates its first staging/shader-visible heap pair.
         *
         * @param device Device to create heaps from.
         * @param type CBV_SRV_UAV or SAMPLER -- the two shader-visible types D3D12 defines.
         * @param initialCapacity Starting descriptor count (DX-103's own original capacity).
         * @param maxCapacity Hard ceiling from the D3D12 specification for this heap type.
         * @param clock Shared fence reading used to decide when a freed index may be reissued and a
         *        replaced heap released.
         * @param name Short heap name used by the CNA_D3D12_DESCRIPTOR_TRACE output.
         */
        void Initialize(ID3D12Device* device, D3D12_DESCRIPTOR_HEAP_TYPE type, std::uint32_t initialCapacity,
                        std::uint32_t maxCapacity, const D3D12DescriptorFenceClock* clock, const char* name);

        /**
         * @brief Returns a free descriptor index, reusing a completed one when possible and growing
         *        only when live demand genuinely exceeds current capacity.
         *
         * @return A stable index valid until it is passed to Free(). Growth never changes it.
         */
        [[nodiscard]] std::uint32_t Allocate();

        /**
         * @brief Returns an index to the allocator, reusable once the GPU has passed the fence value
         *        current at the time of the call. kInvalidIndex is ignored.
         *
         * @param index An index previously returned by Allocate().
         */
        void Free(std::uint32_t index);

        /**
         * @brief The staging (non-shader-visible) handle for @p index -- where a descriptor is
         *        created or re-created.
         *
         * @param index An index returned by Allocate().
         * @return The CPU handle to write the descriptor into. Call Publish() afterwards.
         */
        [[nodiscard]] D3D12_CPU_DESCRIPTOR_HANDLE StagingCpuHandle(std::uint32_t index) const;

        /**
         * @brief Mirrors the staging descriptor at @p index into the shader-visible heap.
         *
         * @param index An index whose staging descriptor has just been written.
         */
        void Publish(std::uint32_t index);

        /**
         * @brief The shader-visible GPU handle for @p index, resolved against the CURRENT heap.
         *
         * Never cache the result across a possible growth -- resolve it at the point of use, which
         * is what every draw path in this renderer does.
         *
         * @param index An index returned by Allocate().
         * @return The GPU handle for SetGraphicsRootDescriptorTable.
         */
        [[nodiscard]] D3D12_GPU_DESCRIPTOR_HANDLE GpuHandle(std::uint32_t index) const;

        /** @brief The currently bound-able shader-visible heap, for SetDescriptorHeaps().
         *  @return The heap object, or nullptr before Initialize(). */
        [[nodiscard]] ID3D12DescriptorHeap* ShaderVisibleHeap() const { return gpuHeap_.Get(); }

        /** @brief Real, measured counters for this heap.
         *  @return A snapshot of the allocator's own state. */
        [[nodiscard]] D3D12DescriptorHeapStats GetStatsEXT() const;

    private:
        struct PendingFree
        {
            std::uint32_t index;
            std::uint64_t fenceStamp;
        };
        struct RetiredHeaps
        {
            ComPtr<ID3D12DescriptorHeap> staging;
            ComPtr<ID3D12DescriptorHeap> gpu;
            std::uint64_t fenceStamp;
        };

        void ReclaimCompleted();
        void Grow();
        void CreateHeapPair(std::uint32_t capacity, ComPtr<ID3D12DescriptorHeap>& outStaging,
                            ComPtr<ID3D12DescriptorHeap>& outGpu) const;
        void Trace(const char* event, std::uint32_t index) const;

        ID3D12Device* device_ = nullptr;
        const D3D12DescriptorFenceClock* clock_ = nullptr;
        D3D12_DESCRIPTOR_HEAP_TYPE type_ = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        std::uint32_t capacity_ = 0;
        std::uint32_t maxCapacity_ = 0;
        std::uint32_t descriptorSize_ = 0;
        std::string name_;
        ComPtr<ID3D12DescriptorHeap> stagingHeap_;
        ComPtr<ID3D12DescriptorHeap> gpuHeap_;
        std::uint32_t nextIndex_ = 0;
        std::uint32_t liveCount_ = 0;
        std::uint32_t peakLive_ = 0;
        std::vector<std::uint32_t> freeList_;
        std::vector<PendingFree> pendingFree_;
        std::vector<RetiredHeaps> retired_;
        std::uint64_t allocations_ = 0;
        std::uint64_t recycles_ = 0;
        std::uint64_t frees_ = 0;
        std::uint64_t publishes_ = 0;
        std::uint64_t bulkCopies_ = 0;
        std::uint32_t growths_ = 0;
        std::uint32_t generation_ = 0;
    };

    /**
     * @brief CNAEXT. The four descriptor allocators plus the fence reading they share.
     *
     * Held by the renderer through a std::shared_ptr and captured by every resource that owns a
     * descriptor, so a resource destroyed AFTER its renderer still frees its slot into a live
     * allocator instead of dereferencing a dangling pointer. The set owns no raw device pointer
     * beyond what its allocators keep for heap creation, and its heaps hold the device alive through
     * ordinary COM reference counting.
     */
    struct D3D12DescriptorHeaps
    {
        /** @brief Shared fence reading; updated by the renderer after every submission. */
        D3D12DescriptorFenceClock clock;
        /** @brief Non-shader-visible render-target views. */
        D3D12CpuDescriptorAllocator rtv;
        /** @brief Non-shader-visible depth-stencil views. */
        D3D12CpuDescriptorAllocator dsv;
        /** @brief Shader-visible CBV/SRV/UAV descriptors -- one per sampleable resource. */
        D3D12ShaderVisibleDescriptorAllocator cbvSrvUav;
        /** @brief Shader-visible sampler descriptors -- one per distinct XNA SamplerState. */
        D3D12ShaderVisibleDescriptorAllocator sampler;

        /**
         * @brief Records that work up to @p fenceValue has been submitted, so descriptors freed from
         *        now on are stamped against it.
         *
         * @param fenceValue The value just signalled on the shared fence.
         */
        void OnSubmittedEXT(std::uint64_t fenceValue)
        {
            if (fenceValue > clock.lastSubmitted) clock.lastSubmitted = fenceValue;
        }
    };

    /** @brief True when CNA_D3D12_DESCRIPTOR_TRACE is set to something other than "0" -- read once
     *  per process, so an untraced run pays a single environment lookup and nothing else.
     *  @return Whether descriptor tracing is enabled. */
    [[nodiscard]] bool D3D12DescriptorTraceEnabled();
}
