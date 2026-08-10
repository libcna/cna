#include "CNA/Internal/Renderers/DirectX9/D3D9OcclusionQuery.hpp"

#include <cstdio>
#include <stdexcept>

namespace CNA::Internal::Renderers::DirectX9
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

    D3D9OcclusionQueryRenderer::D3D9OcclusionQueryRenderer(IDirect3DDevice9* device)
        : device_(device)
    {
        const HRESULT hr = device_->CreateQuery(D3DQUERYTYPE_OCCLUSION, query_.GetAddressOf());
        if (FAILED(hr))
            throw std::runtime_error("D3D9OcclusionQueryRenderer: CreateQuery failed, hr=" + FormatHr(hr));
    }

    void D3D9OcclusionQueryRenderer::Begin()
    {
        query_->Issue(D3DISSUE_BEGIN);
    }

    void D3D9OcclusionQueryRenderer::End()
    {
        query_->Issue(D3DISSUE_END);
    }

    bool D3D9OcclusionQueryRenderer::IsComplete() const
    {
        return query_->GetData(nullptr, 0, 0) == S_OK;
    }

    int D3D9OcclusionQueryRenderer::PixelCount() const
    {
        DWORD count = 0;
        const HRESULT hr = query_->GetData(&count, sizeof(count), 0);
        if (hr != S_OK) return 0;
        return static_cast<int>(count);
    }
}
