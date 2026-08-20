// plans/plan_dx.md Phase DIRECTX6 (DX-43/DX-45).
#include "CNA/Internal/Renderers/DirectX11/D3D11RenderTargets.hpp"
#include "CNA/Internal/Renderers/DirectX11/DirectX11Renderer.hpp"
#include "CNA/Internal/Renderers/D3DCommon/D3DFormatMapping.hpp"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <string>

#include "System/ArgumentOutOfRangeException.hpp"
#include "System/NotSupportedException.hpp"

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
    // D3D11RenderTargetRenderer
    // -------------------------------------------------------------------------

    D3D11RenderTargetRenderer::D3D11RenderTargetRenderer(
        DirectX11Renderer* owner, ID3D11Device* device, ID3D11DeviceContext* context,
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
            throw std::runtime_error("D3D11RenderTargetRenderer: CreateTexture2D(color) failed, hr=" + FormatHr(hr));

        hr = device_->CreateRenderTargetView(colorTexture_.Get(), nullptr, rtv_.GetAddressOf());
        if (FAILED(hr))
            throw std::runtime_error("D3D11RenderTargetRenderer: CreateRenderTargetView failed, hr=" + FormatHr(hr));

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
                throw std::runtime_error("D3D11RenderTargetRenderer: CreateTexture2D(resolve) failed, hr=" + FormatHr(hr));
            hr = device_->CreateShaderResourceView(resolveTexture_.Get(), nullptr, srv_.GetAddressOf());
        }
        else
        {
            hr = device_->CreateShaderResourceView(colorTexture_.Get(), nullptr, srv_.GetAddressOf());
        }
        if (FAILED(hr))
            throw std::runtime_error("D3D11RenderTargetRenderer: CreateShaderResourceView failed, hr=" + FormatHr(hr));

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
                throw std::runtime_error("D3D11RenderTargetRenderer: CreateTexture2D(depth) failed, hr=" + FormatHr(hr));
            hr = device_->CreateDepthStencilView(depthTexture_.Get(), nullptr, dsv_.GetAddressOf());
            if (FAILED(hr))
                throw std::runtime_error("D3D11RenderTargetRenderer: CreateDepthStencilView failed, hr=" + FormatHr(hr));
        }
    }

    void D3D11RenderTargetRenderer::BindAsRenderTarget()
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

    void D3D11RenderTargetRenderer::ResolveAndGenerateMipsEXT()
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

    void D3D11RenderTargetRenderer::UnbindAsRenderTarget()
    {
        ResolveAndGenerateMipsEXT();
        if (owner_) owner_->RestoreBackBufferRenderTargetEXT();
    }

    bool D3D11RenderTargetRenderer::GetData(int level, int x, int y, int w, int h,
                                           void* data, int dataLength) const
    {
        if (level < 0)
            throw System::ArgumentOutOfRangeException(
                "level", std::to_string(level), "level must not be negative.");
        if (level >= levelCount_)
            throw System::NotSupportedException(
                "D3D11RenderTargetRenderer::GetData: this render target has " +
                std::to_string(levelCount_) + " mip level(s); level " + std::to_string(level) +
                " was requested.");

        const int levelW = std::max(1, width_ >> level);
        const int levelH = std::max(1, height_ >> level);
        // 64-bit throughout, so a rectangle near INT_MAX is rejected rather than wrapping.
        const std::int64_t right = static_cast<std::int64_t>(x) + static_cast<std::int64_t>(w);
        const std::int64_t bottom = static_cast<std::int64_t>(y) + static_cast<std::int64_t>(h);
        if (x < 0 || y < 0 || w <= 0 || h <= 0 ||
            right > static_cast<std::int64_t>(levelW) || bottom > static_cast<std::int64_t>(levelH))
            throw System::ArgumentOutOfRangeException(
                "rect",
                std::to_string(x) + "," + std::to_string(y) + "," + std::to_string(w) + "," +
                    std::to_string(h),
                "The requested rectangle leaves the " + std::to_string(levelW) + "x" +
                    std::to_string(levelH) + " mip level.");
        const std::int64_t requiredBytes =
            static_cast<std::int64_t>(w) * static_cast<std::int64_t>(h) * 4;
        if (static_cast<std::int64_t>(dataLength) < requiredBytes)
            throw System::ArgumentOutOfRangeException(
                "dataLength", std::to_string(dataLength),
                "The destination holds fewer than the " + std::to_string(requiredBytes) +
                    " bytes the requested rectangle needs.");

        ID3D11Texture2D* const source = GetSampleableTextureEXT();
        if (!device_ || !context_ || !source || data == nullptr)
            return false;

        D3D11_TEXTURE2D_DESC stagingDesc{};
        stagingDesc.Width = static_cast<UINT>(w);
        stagingDesc.Height = static_cast<UINT>(h);
        stagingDesc.MipLevels = 1;
        stagingDesc.ArraySize = 1;
        stagingDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        stagingDesc.SampleDesc.Count = 1;
        stagingDesc.Usage = D3D11_USAGE_STAGING;
        stagingDesc.BindFlags = 0;
        stagingDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        stagingDesc.MiscFlags = 0;

        ComPtr<ID3D11Texture2D> staging;
        if (FAILED(device_->CreateTexture2D(&stagingDesc, nullptr, staging.GetAddressOf())))
            return false;

        const D3D11_BOX box{ static_cast<UINT>(x), static_cast<UINT>(y), 0,
                             static_cast<UINT>(x + w), static_cast<UINT>(y + h), 1 };
        context_->CopySubresourceRegion(staging.Get(), 0, 0, 0, 0, source,
                                        static_cast<UINT>(level), &box);

        D3D11_MAPPED_SUBRESOURCE mapped{};
        if (FAILED(context_->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped)))
            return false;

        auto* dst = static_cast<std::uint8_t*>(data);
        const auto* src = static_cast<const std::uint8_t*>(mapped.pData);
        const std::size_t rowBytes = static_cast<std::size_t>(w) * 4u;
        for (int row = 0; row < h; ++row)
            std::memcpy(dst + static_cast<std::size_t>(row) * rowBytes,
                        src + static_cast<std::size_t>(row) * mapped.RowPitch, rowBytes);

        context_->Unmap(staging.Get(), 0);
        return true;
    }

    // -------------------------------------------------------------------------
    // D3D11RenderTargetCubeRenderer
    // -------------------------------------------------------------------------

    D3D11RenderTargetCubeRenderer::D3D11RenderTargetCubeRenderer(
        DirectX11Renderer* owner, ID3D11Device* device, ID3D11DeviceContext* context,
        int size, int depthFormat, bool mipMap, int multiSampleCount)
        : owner_(owner), device_(device), context_(context)
        , size_(size)
    {
        appliedMultiSampleCount_ = ClampMultiSampleCount(device_.Get(), DXGI_FORMAT_R8G8B8A8_UNORM, multiSampleCount);
        isMsaa_ = appliedMultiSampleCount_ > 0;
        // Mutually exclusive on the same attachment, same rationale D3D11RenderTargetRenderer's own
        // DX-45 already established -- a full mip chain needs a single-sample source.
        mipMap_ = mipMap && !isMsaa_;
        levelCount_ = mipMap_ ? CalculateMipLevels(size, size) : 1;

        D3D11_TEXTURE2D_DESC desc{};
        desc.Width = static_cast<UINT>(size_);
        desc.Height = static_cast<UINT>(size_);
        desc.MipLevels = static_cast<UINT>(levelCount_);
        desc.ArraySize = 6;
        desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        desc.SampleDesc.Count = isMsaa_ ? static_cast<UINT>(appliedMultiSampleCount_) : 1;
        // D3D11 cannot combine D3D11_RESOURCE_MISC_TEXTURECUBE with SampleDesc.Count > 1 on one
        // resource (a TextureCube SRV can never be multisampled) -- when MSAA, this becomes a
        // plain (non-cube) 6-slice Texture2DMSArray used ONLY as an RTV target, never sampled
        // directly; resolveTexture_ below is the real, single-sample, cube-flagged resource the
        // SRV actually targets.
        desc.Usage = D3D11_USAGE_DEFAULT;
        desc.BindFlags = D3D11_BIND_RENDER_TARGET | (isMsaa_ ? 0 : D3D11_BIND_SHADER_RESOURCE);
        desc.MiscFlags = (isMsaa_ ? 0 : D3D11_RESOURCE_MISC_TEXTURECUBE) | (mipMap_ ? D3D11_RESOURCE_MISC_GENERATE_MIPS : 0);

        HRESULT hr = device_->CreateTexture2D(&desc, nullptr, texture_.GetAddressOf());
        if (FAILED(hr))
            throw std::runtime_error("D3D11RenderTargetCubeRenderer: CreateTexture2D failed, hr=" + FormatHr(hr));

        for (int face = 0; face < 6; ++face)
        {
            D3D11_RENDER_TARGET_VIEW_DESC rtvDesc{};
            rtvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
            if (isMsaa_)
            {
                rtvDesc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2DMSARRAY;
                rtvDesc.Texture2DMSArray.FirstArraySlice = static_cast<UINT>(face);
                rtvDesc.Texture2DMSArray.ArraySize = 1;
            }
            else
            {
                rtvDesc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2DARRAY;
                rtvDesc.Texture2DArray.MipSlice = 0;
                rtvDesc.Texture2DArray.FirstArraySlice = static_cast<UINT>(face);
                rtvDesc.Texture2DArray.ArraySize = 1;
            }

            hr = device_->CreateRenderTargetView(texture_.Get(), &rtvDesc, rtv_[face].GetAddressOf());
            if (FAILED(hr))
                throw std::runtime_error("D3D11RenderTargetCubeRenderer: CreateRenderTargetView(face) failed, hr=" + FormatHr(hr));
        }

        if (isMsaa_)
        {
            D3D11_TEXTURE2D_DESC resolveDesc{};
            resolveDesc.Width = static_cast<UINT>(size_);
            resolveDesc.Height = static_cast<UINT>(size_);
            resolveDesc.MipLevels = static_cast<UINT>(levelCount_);
            resolveDesc.ArraySize = 6;
            resolveDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
            resolveDesc.SampleDesc.Count = 1;
            resolveDesc.Usage = D3D11_USAGE_DEFAULT;
            resolveDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
            resolveDesc.MiscFlags = D3D11_RESOURCE_MISC_TEXTURECUBE;
            hr = device_->CreateTexture2D(&resolveDesc, nullptr, resolveTexture_.GetAddressOf());
            if (FAILED(hr))
                throw std::runtime_error("D3D11RenderTargetCubeRenderer: CreateTexture2D(resolve) failed, hr=" + FormatHr(hr));
        }

        D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc{};
        srvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURECUBE;
        srvDesc.TextureCube.MostDetailedMip = 0;
        srvDesc.TextureCube.MipLevels = static_cast<UINT>(levelCount_);
        hr = device_->CreateShaderResourceView(isMsaa_ ? resolveTexture_.Get() : texture_.Get(), &srvDesc, srv_.GetAddressOf());
        if (FAILED(hr))
            throw std::runtime_error("D3D11RenderTargetCubeRenderer: CreateShaderResourceView failed, hr=" + FormatHr(hr));

        const DXGI_FORMAT depthDxgiFormat = D3DCommon::DepthFormatToDxgi(depthFormat);
        if (depthDxgiFormat != DXGI_FORMAT_UNKNOWN)
        {
            D3D11_TEXTURE2D_DESC depthDesc{};
            depthDesc.Width = static_cast<UINT>(size_);
            depthDesc.Height = static_cast<UINT>(size_);
            depthDesc.MipLevels = 1;
            depthDesc.ArraySize = 1;
            depthDesc.Format = depthDxgiFormat;
            depthDesc.SampleDesc.Count = desc.SampleDesc.Count; // MSAA depth matches MSAA color
            depthDesc.Usage = D3D11_USAGE_DEFAULT;
            depthDesc.BindFlags = D3D11_BIND_DEPTH_STENCIL;

            hr = device_->CreateTexture2D(&depthDesc, nullptr, depthTexture_.GetAddressOf());
            if (FAILED(hr))
                throw std::runtime_error("D3D11RenderTargetCubeRenderer: CreateTexture2D(depth) failed, hr=" + FormatHr(hr));
            hr = device_->CreateDepthStencilView(depthTexture_.Get(), nullptr, dsv_.GetAddressOf());
            if (FAILED(hr))
                throw std::runtime_error("D3D11RenderTargetCubeRenderer: CreateDepthStencilView failed, hr=" + FormatHr(hr));
        }
    }

    void D3D11RenderTargetCubeRenderer::BindAsRenderTargetFace(int face)
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

    void D3D11RenderTargetCubeRenderer::ResolveMsaaEXT()
    {
        if (!isMsaa_ || !resolveTexture_ || activeFace_ < 0) return;
        // Only the currently-active face -- matches this class's own existing "only one face is
        // ever the active draw target at a time" mip-regen convention. Both the MSAA source (no
        // mips) and the resolve destination's base mip level use the same per-face subresource
        // formula (levelCount_ is always 1 here since mipMap_ is forced false when isMsaa_).
        const UINT srcSubresource = static_cast<UINT>(activeFace_);
        const UINT dstSubresource = static_cast<UINT>(activeFace_) * static_cast<UINT>(levelCount_);
        context_->ResolveSubresource(resolveTexture_.Get(), dstSubresource, texture_.Get(), srcSubresource,
                                     DXGI_FORMAT_R8G8B8A8_UNORM);
    }

    bool D3D11RenderTargetCubeRenderer::GetData(int face, int level, int x, int y, int w, int h,
                                               void* data, int dataLength) const
    {
        // REMED-GFX-134: closes the refusal this class inherited from ITextureCubeRenderer's
        // `return false` default. Same staging-copy mechanism as D3D11TextureCubeRenderer::GetData.
        if (!device_ || !context_ || data == nullptr) return false;
        if (face < 0 || face >= 6 || w <= 0 || h <= 0) return false;
        if (level < 0 || level >= levelCount_) return false;
        const int levelSize = std::max(1, size_ >> level);
        if (x < 0 || y < 0 || x + w > levelSize || y + h > levelSize) return false;
        if (dataLength < w * h * 4) return false;

        // Always the resolved single-sample resource: the MSAA array cannot be staged, and its
        // content has already been resolved into this one per face by UnbindAsRenderTarget.
        ID3D11Texture2D* source = GetSampleableTextureEXT();
        if (source == nullptr) return false;

        D3D11_TEXTURE2D_DESC desc{};
        source->GetDesc(&desc);
        D3D11_TEXTURE2D_DESC stagingDesc = desc;
        stagingDesc.Usage = D3D11_USAGE_STAGING;
        stagingDesc.BindFlags = 0;
        stagingDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        stagingDesc.MiscFlags = 0;

        ComPtr<ID3D11Texture2D> staging;
        if (FAILED(device_->CreateTexture2D(&stagingDesc, nullptr, staging.GetAddressOf())))
            return false;
        context_->CopyResource(staging.Get(), source);

        const UINT subresource = D3D11CalcSubresource(static_cast<UINT>(level),
                                                      static_cast<UINT>(face),
                                                      static_cast<UINT>(levelCount_));
        D3D11_MAPPED_SUBRESOURCE mapped{};
        if (FAILED(context_->Map(staging.Get(), subresource, D3D11_MAP_READ, 0, &mapped)))
            return false;

        auto* dst = static_cast<std::uint8_t*>(data);
        for (int row = 0; row < h; ++row)
        {
            const auto* src = static_cast<const std::uint8_t*>(mapped.pData)
                            + static_cast<std::size_t>(y + row) * mapped.RowPitch
                            + static_cast<std::size_t>(x) * 4;
            std::memcpy(dst + static_cast<std::size_t>(row) * static_cast<std::size_t>(w) * 4, src,
                        static_cast<std::size_t>(w) * 4);
        }
        context_->Unmap(staging.Get(), subresource);
        return true;
    }

    void D3D11RenderTargetCubeRenderer::UnbindAsRenderTarget()
    {
        ResolveMsaaEXT();
        if (mipMap_ && srv_)
        {
            context_->GenerateMips(srv_.Get());
        }
        activeFace_ = -1;
        if (owner_) owner_->RestoreBackBufferRenderTargetEXT();
    }
}
