// plans/plan_dx.md Phase DIRECTX6 (DX-47).
#include "CNA/Internal/Renderers/DirectX11/D3D11OcclusionQuery.hpp"

#include <algorithm>
#include <cstdio>
#include <stdexcept>

namespace CNA::Internal::Renderers::DirectX11
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

    D3D11OcclusionQueryRenderer::D3D11OcclusionQueryRenderer(ID3D11Device* device, ID3D11DeviceContext* context)
        : device_(device), context_(context)
    {
        D3D11_QUERY_DESC desc{};
        desc.Query = D3D11_QUERY_OCCLUSION;
        desc.MiscFlags = 0;

        const HRESULT hr = device_->CreateQuery(&desc, query_.GetAddressOf());
        if (FAILED(hr))
            throw std::runtime_error("D3D11OcclusionQueryRenderer: CreateQuery failed, hr=" + FormatHr(hr));
    }

    void D3D11OcclusionQueryRenderer::Begin()
    {
        context_->Begin(query_.Get());
    }

    void D3D11OcclusionQueryRenderer::End()
    {
        context_->End(query_.Get());
    }

    bool D3D11OcclusionQueryRenderer::IsComplete() const
    {
        return context_->GetData(query_.Get(), nullptr, 0, D3D11_ASYNC_GETDATA_DONOTFLUSH) == S_OK;
    }

    int D3D11OcclusionQueryRenderer::PixelCount() const
    {
        UINT64 count = 0;
        const HRESULT hr = context_->GetData(query_.Get(), &count, sizeof(count), 0);
        if (FAILED(hr)) return 0;
        return static_cast<int>(std::min<UINT64>(count, static_cast<UINT64>(INT32_MAX)));
    }
}
