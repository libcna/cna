// plan_dx.md Phase DX6 (DX-43/DX-45).
#include "CNA/Internal/Backends/D3D11/D3D11RenderTargets.hpp"
#include "CNA/Internal/Backends/D3D11/D3D11GraphicsBackend.hpp"
#include "CNA/Internal/Backends/D3DCommon/D3DFormatMapping.hpp"

#include <algorithm>
#include <cstdio>
#include <stdexcept>

namespace CNA::Internal::Backends::D3D11
{
    namespace
    {
        std::string FormatHr(HRESULT hr)
        {
            char buf[32];
            std::snprintf(buf, sizeof(buf), "0x%08lX", static_cast<unsigned long>(hr));
            return buf;
        }

        int CalculateMipLevels(int w, int h)
        {
            int levels = 1;
            while (w > 1 || h > 1)
            {
                w = std::max(1, w / 2);
                h = std::max(1, h / 2);
                ++levels;
            }
            return levels;
        }

        /// Real, device-queried MSAA support (DX-45) -- never assumes a requested sample count is
        /// supported. Returns 0 (no MSAA) if requestedCount <= 1 or the device reports zero
        /// quality levels for it.
        int ClampMultiSampleCount(ID3D11Device* device, DXGI_FORMAT format, int requestedCount)
        {
            if (requestedCount <= 1) return 0;
            UINT qualityLevels = 0;
            if (FAILED(device->CheckMultisampleQualityLevels(
                    format, static_cast<UINT>(requestedCount), &qualityLevels)) || qualityLevels == 0)
            {
                return 0;
            }
            return requestedCount;
        }
    }

    // -------------------------------------------------------------------------
    // D3D11RenderTargetBackend
    // -------------------------------------------------------------------------

    D3D11RenderTargetBackend::D3D11RenderTargetBackend(
        D3D11GraphicsBackend* owner, ID3D11Device* device, ID3D11DeviceContext* context,
        int w, int h, int depthFormat, bool mipMap, int multiSampleCount)
        : owner_(owner), device_(device), context_(context)
        , width_(w), height_(h)
    {
        appliedMultiSampleCount_ = ClampMultiSampleCount(device_.Get(), DXGI_FORMAT_R8G8B8A8_UNORM, multiSampleCount);
        isMsaa_ = appliedMultiSampleCount_ > 0;
        // A mip chain requires GenerateMips(), which requires a single-sample source -- MSAA and
        // a full mip chain are mutually exclusive here (matches this project's own EasyGL/Vulkan
        // precedent: a render target is either "resolved once, then mip-cascaded" or "rendered
        // straight to a mip-chain texture", never both on the same attachment).
        mipMap_ = mipMap && !isMsaa_;
        levelCount_ = mipMap_ ? CalculateMipLevels(w, h) : 1;

        D3D11_TEXTURE2D_DESC colorDesc{};
        colorDesc.Width = static_cast<UINT>(w);
        colorDesc.Height = static_cast<UINT>(h);
        colorDesc.MipLevels = static_cast<UINT>(levelCount_);
        colorDesc.ArraySize = 1;
        colorDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        colorDesc.SampleDesc.Count = isMsaa_ ? static_cast<UINT>(appliedMultiSampleCount_) : 1;
        colorDesc.SampleDesc.Quality = 0;
        colorDesc.Usage = D3D11_USAGE_DEFAULT;
        colorDesc.BindFlags = D3D11_BIND_RENDER_TARGET | (isMsaa_ ? 0 : D3D11_BIND_SHADER_RESOURCE);
        colorDesc.MiscFlags = mipMap_ ? D3D11_RESOURCE_MISC_GENERATE_MIPS : 0;

        HRESULT hr = device_->CreateTexture2D(&colorDesc, nullptr, colorTexture_.GetAddressOf());
        if (FAILED(hr))
            throw std::runtime_error("D3D11RenderTargetBackend: CreateTexture2D(color) failed, hr=" + FormatHr(hr));

        hr = device_->CreateRenderTargetView(colorTexture_.Get(), nullptr, rtv_.GetAddressOf());
        if (FAILED(hr))
            throw std::runtime_error("D3D11RenderTargetBackend: CreateRenderTargetView failed, hr=" + FormatHr(hr));

        if (isMsaa_)
        {
            // The MSAA texture itself is never sampled directly -- ResolveSubresource() into this
            // separate single-sample texture on UnbindAsRenderTarget() (DX-45's own design note).
            D3D11_TEXTURE2D_DESC resolveDesc = colorDesc;
            resolveDesc.SampleDesc.Count = 1;
            resolveDesc.MiscFlags = 0;
            resolveDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
            hr = device_->CreateTexture2D(&resolveDesc, nullptr, resolveTexture_.GetAddressOf());
            if (FAILED(hr))
                throw std::runtime_error("D3D11RenderTargetBackend: CreateTexture2D(resolve) failed, hr=" + FormatHr(hr));
            hr = device_->CreateShaderResourceView(resolveTexture_.Get(), nullptr, srv_.GetAddressOf());
        }
        else
        {
            hr = device_->CreateShaderResourceView(colorTexture_.Get(), nullptr, srv_.GetAddressOf());
        }
        if (FAILED(hr))
            throw std::runtime_error("D3D11RenderTargetBackend: CreateShaderResourceView failed, hr=" + FormatHr(hr));

        const DXGI_FORMAT depthDxgiFormat = D3DCommon::DepthFormatToDxgi(depthFormat);
        if (depthDxgiFormat != DXGI_FORMAT_UNKNOWN)
        {
            D3D11_TEXTURE2D_DESC depthDesc{};
            depthDesc.Width = static_cast<UINT>(w);
            depthDesc.Height = static_cast<UINT>(h);
            depthDesc.MipLevels = 1;
            depthDesc.ArraySize = 1;
            depthDesc.Format = depthDxgiFormat;
            depthDesc.SampleDesc.Count = colorDesc.SampleDesc.Count;
            depthDesc.Usage = D3D11_USAGE_DEFAULT;
            depthDesc.BindFlags = D3D11_BIND_DEPTH_STENCIL;

            hr = device_->CreateTexture2D(&depthDesc, nullptr, depthTexture_.GetAddressOf());
            if (FAILED(hr))
                throw std::runtime_error("D3D11RenderTargetBackend: CreateTexture2D(depth) failed, hr=" + FormatHr(hr));
            hr = device_->CreateDepthStencilView(depthTexture_.Get(), nullptr, dsv_.GetAddressOf());
            if (FAILED(hr))
                throw std::runtime_error("D3D11RenderTargetBackend: CreateDepthStencilView failed, hr=" + FormatHr(hr));
        }
    }

    void D3D11RenderTargetBackend::BindAsRenderTarget()
    {
        ID3D11RenderTargetView* rtv = rtv_.Get();
        context_->OMSetRenderTargets(1, &rtv, dsv_.Get());

        D3D11_VIEWPORT vp{};
        vp.TopLeftX = 0.0f;
        vp.TopLeftY = 0.0f;
        vp.Width = static_cast<float>(width_);
        vp.Height = static_cast<float>(height_);
        vp.MinDepth = 0.0f;
        vp.MaxDepth = 1.0f;
        context_->RSSetViewports(1, &vp);

        if (owner_) owner_->TrackCurrentRenderTargetEXT(&rtv, 1, dsv_.Get());
    }

    void D3D11RenderTargetBackend::ResolveAndGenerateMipsEXT()
    {
        if (isMsaa_ && resolveTexture_)
        {
            context_->ResolveSubresource(resolveTexture_.Get(), 0, colorTexture_.Get(), 0,
                                         DXGI_FORMAT_R8G8B8A8_UNORM);
        }
        if (mipMap_ && srv_)
        {
            context_->GenerateMips(srv_.Get());
        }
    }

    void D3D11RenderTargetBackend::UnbindAsRenderTarget()
    {
        ResolveAndGenerateMipsEXT();
        if (owner_) owner_->RestoreBackBufferRenderTargetEXT();
    }

    // -------------------------------------------------------------------------
    // D3D11RenderTargetCubeBackend
    // -------------------------------------------------------------------------

    D3D11RenderTargetCubeBackend::D3D11RenderTargetCubeBackend(
        D3D11GraphicsBackend* owner, ID3D11Device* device, ID3D11DeviceContext* context,
        int size, int depthFormat, bool mipMap)
        : owner_(owner), device_(device), context_(context)
        , size_(size), mipMap_(mipMap), levelCount_(mipMap ? CalculateMipLevels(size, size) : 1)
    {
        D3D11_TEXTURE2D_DESC desc{};
        desc.Width = static_cast<UINT>(size_);
        desc.Height = static_cast<UINT>(size_);
        desc.MipLevels = static_cast<UINT>(levelCount_);
        desc.ArraySize = 6;
        desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        desc.SampleDesc.Count = 1;
        desc.Usage = D3D11_USAGE_DEFAULT;
        desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
        desc.MiscFlags = D3D11_RESOURCE_MISC_TEXTURECUBE | (mipMap_ ? D3D11_RESOURCE_MISC_GENERATE_MIPS : 0);

        HRESULT hr = device_->CreateTexture2D(&desc, nullptr, texture_.GetAddressOf());
        if (FAILED(hr))
            throw std::runtime_error("D3D11RenderTargetCubeBackend: CreateTexture2D failed, hr=" + FormatHr(hr));

        for (int face = 0; face < 6; ++face)
        {
            D3D11_RENDER_TARGET_VIEW_DESC rtvDesc{};
            rtvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
            rtvDesc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2DARRAY;
            rtvDesc.Texture2DArray.MipSlice = 0;
            rtvDesc.Texture2DArray.FirstArraySlice = static_cast<UINT>(face);
            rtvDesc.Texture2DArray.ArraySize = 1;

            hr = device_->CreateRenderTargetView(texture_.Get(), &rtvDesc, rtv_[face].GetAddressOf());
            if (FAILED(hr))
                throw std::runtime_error("D3D11RenderTargetCubeBackend: CreateRenderTargetView(face) failed, hr=" + FormatHr(hr));
        }

        D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc{};
        srvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURECUBE;
        srvDesc.TextureCube.MostDetailedMip = 0;
        srvDesc.TextureCube.MipLevels = static_cast<UINT>(levelCount_);
        hr = device_->CreateShaderResourceView(texture_.Get(), &srvDesc, srv_.GetAddressOf());
        if (FAILED(hr))
            throw std::runtime_error("D3D11RenderTargetCubeBackend: CreateShaderResourceView failed, hr=" + FormatHr(hr));

        const DXGI_FORMAT depthDxgiFormat = D3DCommon::DepthFormatToDxgi(depthFormat);
        if (depthDxgiFormat != DXGI_FORMAT_UNKNOWN)
        {
            D3D11_TEXTURE2D_DESC depthDesc{};
            depthDesc.Width = static_cast<UINT>(size_);
            depthDesc.Height = static_cast<UINT>(size_);
            depthDesc.MipLevels = 1;
            depthDesc.ArraySize = 1;
            depthDesc.Format = depthDxgiFormat;
            depthDesc.SampleDesc.Count = 1;
            depthDesc.Usage = D3D11_USAGE_DEFAULT;
            depthDesc.BindFlags = D3D11_BIND_DEPTH_STENCIL;

            hr = device_->CreateTexture2D(&depthDesc, nullptr, depthTexture_.GetAddressOf());
            if (FAILED(hr))
                throw std::runtime_error("D3D11RenderTargetCubeBackend: CreateTexture2D(depth) failed, hr=" + FormatHr(hr));
            hr = device_->CreateDepthStencilView(depthTexture_.Get(), nullptr, dsv_.GetAddressOf());
            if (FAILED(hr))
                throw std::runtime_error("D3D11RenderTargetCubeBackend: CreateDepthStencilView failed, hr=" + FormatHr(hr));
        }
    }

    void D3D11RenderTargetCubeBackend::BindAsRenderTargetFace(int face)
    {
        if (face < 0 || face >= 6) return;
        activeFace_ = face;
        ID3D11RenderTargetView* rtv = rtv_[face].Get();
        context_->OMSetRenderTargets(1, &rtv, dsv_.Get());

        D3D11_VIEWPORT vp{};
        vp.TopLeftX = 0.0f;
        vp.TopLeftY = 0.0f;
        vp.Width = static_cast<float>(size_);
        vp.Height = static_cast<float>(size_);
        vp.MinDepth = 0.0f;
        vp.MaxDepth = 1.0f;
        context_->RSSetViewports(1, &vp);

        if (owner_) owner_->TrackCurrentRenderTargetEXT(&rtv, 1, dsv_.Get());
    }

    void D3D11RenderTargetCubeBackend::UnbindAsRenderTarget()
    {
        if (mipMap_ && srv_)
        {
            context_->GenerateMips(srv_.Get());
        }
        activeFace_ = -1;
        if (owner_) owner_->RestoreBackBufferRenderTargetEXT();
    }
}
