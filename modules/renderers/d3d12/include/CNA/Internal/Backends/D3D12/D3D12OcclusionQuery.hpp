#pragma once

// plan_dx.md Phase DX13 (DX-120): real D3D12 occlusion query backend, mirroring D3D11's own
// D3D11OcclusionQueryBackend (DX-47) XNA-level contract exactly.

#include "CNA/Internal/Backends/Common/IGraphicsBackend.hpp"

#include <d3d12.h>
#include <wrl/client.h>

namespace CNA::Internal::Backends::D3D12
{
    using Microsoft::WRL::ComPtr;

    class D3D12GraphicsBackend;

    /// Real ID3D12QueryHeap(D3D12_QUERY_HEAP_TYPE_OCCLUSION)-backed occlusion query (DX-120).
    /// Unlike D3D11's ID3D11Query (queried asynchronously via GetData(DONOTFLUSH), which can
    /// genuinely return "not ready yet" while GPU work is still in flight), every operation this
    /// whole D3D12 backend performs is already synchronous (record -> Close -> Execute -> wait for
    /// the fence, ExecuteCommandListAndWaitEXT) -- so End() only ever returns after the GPU has
    /// actually finished resolving the query result, making IsComplete() trivially true once End()
    /// has run and false before it. A deliberate, honest simplification consistent with this
    /// backend's existing synchronous-submission design (matches
    /// D3D12GraphicsBackend::CreateUploadConstantBuffer's own "safe because every draw submits
    /// synchronously" note) -- real overlapped/async query polling is a future concern once this
    /// backend does real per-frame pipelining, not something DX-120 needs to solve.
    class D3D12OcclusionQueryBackend final : public IOcclusionQueryBackend
    {
    public:
        explicit D3D12OcclusionQueryBackend(D3D12GraphicsBackend* backend);

        void Begin() override;
        void End() override;
        [[nodiscard]] bool IsComplete() const override;
        /// IOcclusionQueryBackend::PixelCount() returns int, but D3D12 reports a UINT64 --
        /// explicitly clamped to INT32_MAX rather than silently truncated, matching D3D11's own
        /// D3D11OcclusionQueryBackend::PixelCount() convention exactly. Returns 0 if End() has not
        /// completed yet (IsComplete() == false), matching D3D11's own "GetData failed -> 0" path.
        [[nodiscard]] int PixelCount() const override;

    private:
        D3D12GraphicsBackend* backend_;
        ComPtr<ID3D12QueryHeap> queryHeap_;
        // D3D12_HEAP_TYPE_READBACK resources must always stay in D3D12_RESOURCE_STATE_COPY_DEST
        // (the only state a readback heap resource may ever be in) -- created once in that state
        // and never transitioned, so this is deliberately NOT registered with
        // D3D12ResourceStateTracker (nothing ever calls TransitionTo() on it).
        ComPtr<ID3D12Resource> readbackBuffer_;
        bool resolved_ = false;
    };
}
