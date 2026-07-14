// plan_dx.md Phase DX13 (DX-117).
#include "CNA/Internal/Backends/D3D12/D3D12RenderTargets.hpp"
#include "CNA/Internal/Backends/D3D12/D3D12GraphicsBackend.hpp"
#include "CNA/Internal/Backends/D3DCommon/D3DFormatMapping.hpp"

#include <cstdio>
#include <stdexcept>

namespace CNA::Internal::Backends::D3D12
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

    // -------------------------------------------------------------------------
    // D3D12RenderTargetBackend
    // -------------------------------------------------------------------------

    D3D12RenderTargetBackend::D3D12RenderTargetBackend(
        D3D12GraphicsBackend* owner, ID3D12Device* device, int w, int h, int depthFormat)
        : owner_(owner), device_(device), width_(w), height_(h)
    {
        D3D12_HEAP_PROPERTIES heapProps{};
        heapProps.Type = D3D12_HEAP_TYPE_DEFAULT;

        D3D12_RESOURCE_DESC colorDesc{};
        colorDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        colorDesc.Width = static_cast<UINT64>(w);
        colorDesc.Height = static_cast<UINT>(h);
        colorDesc.DepthOrArraySize = 1;
        colorDesc.MipLevels = 1;
        colorDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        colorDesc.SampleDesc.Count = 1;
        colorDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
        colorDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;

        D3D12_CLEAR_VALUE colorClear{};
        colorClear.Format = DXGI_FORMAT_R8G8B8A8_UNORM;

        HRESULT hr = device_->CreateCommittedResource(
            &heapProps, D3D12_HEAP_FLAG_NONE, &colorDesc,
            D3D12_RESOURCE_STATE_RENDER_TARGET, &colorClear, IID_PPV_ARGS(colorResource_.ReleaseAndGetAddressOf()));
        if (FAILED(hr))
            throw std::runtime_error("D3D12RenderTargetBackend: CreateCommittedResource(color) failed, hr=" + FormatHr(hr));

        owner_->GetResourceStateTrackerEXT().TrackResource(colorResource_.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET);

        rtv_ = owner_->AllocateRtvDescriptorEXT();
        device_->CreateRenderTargetView(colorResource_.Get(), nullptr, rtv_);

        D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
        srvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srvDesc.Texture2D.MipLevels = 1;
        owner_->AllocateCbvSrvUavDescriptorEXT(srvCpu_, srvGpu_);
        device_->CreateShaderResourceView(colorResource_.Get(), &srvDesc, srvCpu_);

        const DXGI_FORMAT depthDxgiFormat = D3DCommon::DepthFormatToDxgi(depthFormat);
        hasDepth_ = depthDxgiFormat != DXGI_FORMAT_UNKNOWN;
        if (hasDepth_)
        {
            D3D12_RESOURCE_DESC depthDesc{};
            depthDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
            depthDesc.Width = static_cast<UINT64>(w);
            depthDesc.Height = static_cast<UINT>(h);
            depthDesc.DepthOrArraySize = 1;
            depthDesc.MipLevels = 1;
            depthDesc.Format = depthDxgiFormat;
            depthDesc.SampleDesc.Count = 1;
            depthDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;

            D3D12_CLEAR_VALUE depthClear{};
            depthClear.Format = depthDxgiFormat;
            depthClear.DepthStencil.Depth = 1.0f;

            hr = device_->CreateCommittedResource(
                &heapProps, D3D12_HEAP_FLAG_NONE, &depthDesc,
                D3D12_RESOURCE_STATE_DEPTH_WRITE, &depthClear, IID_PPV_ARGS(depthResource_.ReleaseAndGetAddressOf()));
            if (FAILED(hr))
                throw std::runtime_error("D3D12RenderTargetBackend: CreateCommittedResource(depth) failed, hr=" + FormatHr(hr));
            owner_->GetResourceStateTrackerEXT().TrackResource(depthResource_.Get(), D3D12_RESOURCE_STATE_DEPTH_WRITE);

            dsv_ = owner_->AllocateDsvDescriptorEXT();
            D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc{};
            dsvDesc.Format = depthDxgiFormat;
            dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
            device_->CreateDepthStencilView(depthResource_.Get(), &dsvDesc, dsv_);
        }
    }

    void D3D12RenderTargetBackend::BindAsRenderTarget()
    {
        if (owner_)
        {
            owner_->BindOffscreenColorTargetEXT(colorResource_.Get(), rtv_,
                                                DXGI_FORMAT_R8G8B8A8_UNORM, width_, height_);
        }
    }

    void D3D12RenderTargetBackend::UnbindAsRenderTarget()
    {
        if (owner_) owner_->RestoreBackBufferRenderTargetEXT();
    }

    // -------------------------------------------------------------------------
    // D3D12RenderTargetCubeBackend
    // -------------------------------------------------------------------------

    D3D12RenderTargetCubeBackend::D3D12RenderTargetCubeBackend(
        D3D12GraphicsBackend* owner, ID3D12Device* device, int size, int depthFormat)
        : owner_(owner), device_(device), size_(size)
    {
        D3D12_HEAP_PROPERTIES heapProps{};
        heapProps.Type = D3D12_HEAP_TYPE_DEFAULT;

        D3D12_RESOURCE_DESC colorDesc{};
        colorDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        colorDesc.Width = static_cast<UINT64>(size_);
        colorDesc.Height = static_cast<UINT>(size_);
        colorDesc.DepthOrArraySize = 6;
        colorDesc.MipLevels = 1;
        colorDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        colorDesc.SampleDesc.Count = 1;
        colorDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
        colorDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;

        D3D12_CLEAR_VALUE colorClear{};
        colorClear.Format = DXGI_FORMAT_R8G8B8A8_UNORM;

        HRESULT hr = device_->CreateCommittedResource(
            &heapProps, D3D12_HEAP_FLAG_NONE, &colorDesc,
            D3D12_RESOURCE_STATE_RENDER_TARGET, &colorClear, IID_PPV_ARGS(colorResource_.ReleaseAndGetAddressOf()));
        if (FAILED(hr))
            throw std::runtime_error("D3D12RenderTargetCubeBackend: CreateCommittedResource(color) failed, hr=" + FormatHr(hr));

        owner_->GetResourceStateTrackerEXT().TrackResource(colorResource_.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET);

        for (UINT face = 0; face < 6; ++face)
        {
            rtv_[face] = owner_->AllocateRtvDescriptorEXT();
            D3D12_RENDER_TARGET_VIEW_DESC rtvDesc{};
            rtvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
            rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2DARRAY;
            rtvDesc.Texture2DArray.MipSlice = 0;
            rtvDesc.Texture2DArray.FirstArraySlice = face;
            rtvDesc.Texture2DArray.ArraySize = 1;
            device_->CreateRenderTargetView(colorResource_.Get(), &rtvDesc, rtv_[face]);
        }

        D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
        srvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBE;
        srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srvDesc.TextureCube.MipLevels = 1;
        owner_->AllocateCbvSrvUavDescriptorEXT(srvCpu_, srvGpu_);
        device_->CreateShaderResourceView(colorResource_.Get(), &srvDesc, srvCpu_);

        const DXGI_FORMAT depthDxgiFormat = D3DCommon::DepthFormatToDxgi(depthFormat);
        hasDepth_ = depthDxgiFormat != DXGI_FORMAT_UNKNOWN;
        if (hasDepth_)
        {
            D3D12_RESOURCE_DESC depthDesc{};
            depthDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
            depthDesc.Width = static_cast<UINT64>(size_);
            depthDesc.Height = static_cast<UINT>(size_);
            depthDesc.DepthOrArraySize = 1;
            depthDesc.MipLevels = 1;
            depthDesc.Format = depthDxgiFormat;
            depthDesc.SampleDesc.Count = 1;
            depthDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;

            D3D12_CLEAR_VALUE depthClear{};
            depthClear.Format = depthDxgiFormat;
            depthClear.DepthStencil.Depth = 1.0f;

            hr = device_->CreateCommittedResource(
                &heapProps, D3D12_HEAP_FLAG_NONE, &depthDesc,
                D3D12_RESOURCE_STATE_DEPTH_WRITE, &depthClear, IID_PPV_ARGS(depthResource_.ReleaseAndGetAddressOf()));
            if (FAILED(hr))
                throw std::runtime_error("D3D12RenderTargetCubeBackend: CreateCommittedResource(depth) failed, hr=" + FormatHr(hr));
            owner_->GetResourceStateTrackerEXT().TrackResource(depthResource_.Get(), D3D12_RESOURCE_STATE_DEPTH_WRITE);

            dsv_ = owner_->AllocateDsvDescriptorEXT();
            D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc{};
            dsvDesc.Format = depthDxgiFormat;
            dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
            device_->CreateDepthStencilView(depthResource_.Get(), &dsvDesc, dsv_);
        }
    }

    void D3D12RenderTargetCubeBackend::BindAsRenderTargetFace(int face)
    {
        activeFace_ = face;
        if (owner_)
        {
            owner_->BindOffscreenColorTargetEXT(colorResource_.Get(), rtv_[face],
                                                DXGI_FORMAT_R8G8B8A8_UNORM, size_, size_);
        }
    }

    void D3D12RenderTargetCubeBackend::UnbindAsRenderTarget()
    {
        activeFace_ = -1;
        if (owner_) owner_->RestoreBackBufferRenderTargetEXT();
    }
}
