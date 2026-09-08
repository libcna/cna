// plans/plan_dx.md Phase DIRECTX6 (DX-40/DX-41/DX-42).
#include "CNA/Internal/Renderers/DirectX11/D3D11Textures.hpp"
#include "CNA/Internal/Renderers/D3DCommon/D3DFormatMapping.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <vector>

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

        /// Mirrors EasyGLRenderer.cpp's CalculateRenderTargetMipLevels / Texture2D.cpp's
        /// own CalculateMipLevels: full mip chain down to a 1x1 level.
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

        void ResolveSurfaceFormat(int surfaceFormat, DXGI_FORMAT& dxgiFormat, int& bytesPerTexel,
                                  const char* owner)
        {
            using namespace D3DCommon;
            dxgiFormat = SurfaceFormatToDxgi(surfaceFormat);
            bytesPerTexel = SurfaceFormatBytesPerTexel(surfaceFormat);
            if (!IsXnaUncompressedSurfaceFormat(surfaceFormat) ||
                dxgiFormat == DXGI_FORMAT_UNKNOWN || bytesPerTexel == 0)
            {
                throw std::invalid_argument(
                    std::string(owner) + ": unsupported SurfaceFormat::" +
                    SurfaceFormatName(surfaceFormat) + " (ordinal " +
                    std::to_string(surfaceFormat) + ").");
            }
        }
    }

    // -------------------------------------------------------------------------
    // D3D11TextureRenderer
    // -------------------------------------------------------------------------

    D3D11TextureRenderer::D3D11TextureRenderer(
        ID3D11Device* device, ID3D11DeviceContext* context, const ImageData& data)
        : device_(device), context_(context)
        , width_(data.width), height_(data.height)
        , mipLevels_(data.mipLevels > 0 ? data.mipLevels : 1)
        , surfaceFormat_(data.surfaceFormat)
    {
        ResolveSurfaceFormat(surfaceFormat_, dxgiFormat_, bytesPerTexel_, "D3D11TextureRenderer");
        D3D11_TEXTURE2D_DESC desc{};
        desc.Width = static_cast<UINT>(width_);
        desc.Height = static_cast<UINT>(height_);
        desc.MipLevels = static_cast<UINT>(mipLevels_);
        desc.ArraySize = 1;
        desc.Format = dxgiFormat_;
        desc.SampleDesc.Count = 1;
        desc.Usage = D3D11_USAGE_DEFAULT;
        desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

        HRESULT hr = device_->CreateTexture2D(&desc, nullptr, texture_.GetAddressOf());
        if (FAILED(hr))
            throw std::runtime_error("D3D11TextureRenderer: CreateTexture2D failed, hr=" + FormatHr(hr));

        if (!data.pixels.empty())
        {
            const std::size_t required = static_cast<std::size_t>(width_) *
                                         static_cast<std::size_t>(height_) *
                                         static_cast<std::size_t>(bytesPerTexel_);
            if (data.pixels.size() < required)
                throw std::invalid_argument(
                    "D3D11TextureRenderer: level-zero pixel buffer is too small for SurfaceFormat::" +
                    std::string(D3DCommon::SurfaceFormatName(surfaceFormat_)) + ".");
            context_->UpdateSubresource(texture_.Get(), 0, nullptr, data.pixels.data(),
                                        static_cast<UINT>(width_ * bytesPerTexel_), 0);
        }

        hr = device_->CreateShaderResourceView(texture_.Get(), nullptr, srv_.GetAddressOf());
        if (FAILED(hr))
            throw std::runtime_error("D3D11TextureRenderer: CreateShaderResourceView failed, hr=" + FormatHr(hr));
    }

    void D3D11TextureRenderer::UpdatePixels(const uint8_t* rgba, int stride)
    {
        const UINT rowPitch = stride > 0 ? static_cast<UINT>(stride)
                                         : static_cast<UINT>(width_ * bytesPerTexel_);
        context_->UpdateSubresource(texture_.Get(), 0, nullptr, rgba, rowPitch, 0);
    }

    void D3D11TextureRenderer::UpdatePixelsLevel(int level, const uint8_t* rgba, int levelW, int levelH)
    {
        (void)levelH;
        if (level < 0 || level >= mipLevels_) return;
        const UINT subresource = D3D11CalcSubresource(static_cast<UINT>(level), 0, static_cast<UINT>(mipLevels_));
        context_->UpdateSubresource(texture_.Get(), subresource, nullptr, rgba,
                                    static_cast<UINT>(levelW * bytesPerTexel_), 0);
    }

    bool D3D11TextureRenderer::GetData(int level, int x, int y, int w, int h,
                                       void* data, int dataLength) const
    {
        if (level < 0 || level >= mipLevels_ || w <= 0 || h <= 0 || data == nullptr) return false;
        const int levelW = std::max(1, width_ >> level);
        const int levelH = std::max(1, height_ >> level);
        if (x < 0 || y < 0 || x + w > levelW || y + h > levelH) return false;
        const std::size_t rowBytes = static_cast<std::size_t>(w) * bytesPerTexel_;
        const std::size_t required = rowBytes * static_cast<std::size_t>(h);
        if (dataLength < 0 || static_cast<std::size_t>(dataLength) < required) return false;

        D3D11_TEXTURE2D_DESC stagingDesc{};
        texture_->GetDesc(&stagingDesc);
        stagingDesc.Usage = D3D11_USAGE_STAGING;
        stagingDesc.BindFlags = 0;
        stagingDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        stagingDesc.MiscFlags = 0;

        ComPtr<ID3D11Texture2D> staging;
        if (FAILED(device_->CreateTexture2D(&stagingDesc, nullptr, staging.GetAddressOf()))) return false;
        context_->CopyResource(staging.Get(), texture_.Get());

        const UINT subresource = D3D11CalcSubresource(static_cast<UINT>(level), 0,
                                                       static_cast<UINT>(mipLevels_));
        D3D11_MAPPED_SUBRESOURCE mapped{};
        if (FAILED(context_->Map(staging.Get(), subresource, D3D11_MAP_READ, 0, &mapped))) return false;

        auto* dst = static_cast<uint8_t*>(data);
        for (int row = 0; row < h; ++row)
        {
            const auto* src = static_cast<const uint8_t*>(mapped.pData)
                              + static_cast<std::size_t>(y + row) * mapped.RowPitch
                              + static_cast<std::size_t>(x) * bytesPerTexel_;
            std::memcpy(dst + static_cast<std::size_t>(row) * rowBytes, src, rowBytes);
        }
        context_->Unmap(staging.Get(), subresource);
        return true;
    }

    // -------------------------------------------------------------------------
    // D3D11TextureCubeRenderer
    // -------------------------------------------------------------------------

    D3D11TextureCubeRenderer::D3D11TextureCubeRenderer(
        ID3D11Device* device, ID3D11DeviceContext* context, int size, bool mipMap, int surfaceFormat)
        : device_(device), context_(context)
        , size_(size), mipLevels_(mipMap ? CalculateMipLevels(size, size) : 1)
        , surfaceFormat_(surfaceFormat)
    {
        ResolveSurfaceFormat(surfaceFormat_, dxgiFormat_, bytesPerTexel_,
                             "D3D11TextureCubeRenderer");
        D3D11_TEXTURE2D_DESC desc{};
        desc.Width = static_cast<UINT>(size_);
        desc.Height = static_cast<UINT>(size_);
        desc.MipLevels = static_cast<UINT>(mipLevels_);
        desc.ArraySize = 6;
        desc.Format = dxgiFormat_;
        desc.SampleDesc.Count = 1;
        desc.Usage = D3D11_USAGE_DEFAULT;
        desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        desc.MiscFlags = D3D11_RESOURCE_MISC_TEXTURECUBE;

        HRESULT hr = device_->CreateTexture2D(&desc, nullptr, texture_.GetAddressOf());
        if (FAILED(hr))
            throw std::runtime_error("D3D11TextureCubeRenderer: CreateTexture2D failed, hr=" + FormatHr(hr));

        D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc{};
        srvDesc.Format = dxgiFormat_;
        srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURECUBE;
        srvDesc.TextureCube.MostDetailedMip = 0;
        srvDesc.TextureCube.MipLevels = static_cast<UINT>(mipLevels_);

        hr = device_->CreateShaderResourceView(texture_.Get(), &srvDesc, srv_.GetAddressOf());
        if (FAILED(hr))
            throw std::runtime_error("D3D11TextureCubeRenderer: CreateShaderResourceView failed, hr=" + FormatHr(hr));
    }

    bool D3D11TextureCubeRenderer::SetData(int face, int level, int x, int y, int w, int h,
                                          const void* data, int dataLength)
    {
        // REMED-GFX-135: these used to be a silent `return` the shared layer read as a completed
        // upload, and neither the source pointer nor the rectangle was checked at all.
        if (level < 0 || level >= mipLevels_ || face < 0 || face >= 6) return false;
        if (data == nullptr || w <= 0 || h <= 0) return false;
        const int levelSize = std::max(1, size_ >> level);
        if (x < 0 || y < 0 || x + w > levelSize || y + h > levelSize) return false;
        const std::size_t rowBytes = static_cast<std::size_t>(w) * bytesPerTexel_;
        const std::size_t required = rowBytes * static_cast<std::size_t>(h);
        if (dataLength < 0 || static_cast<std::size_t>(dataLength) < required) return false;
        const UINT subresource = D3D11CalcSubresource(static_cast<UINT>(level), static_cast<UINT>(face),
                                                       static_cast<UINT>(mipLevels_));
        D3D11_BOX box{};
        box.left = static_cast<UINT>(x);
        box.top = static_cast<UINT>(y);
        box.front = 0;
        box.right = static_cast<UINT>(x + w);
        box.bottom = static_cast<UINT>(y + h);
        box.back = 1;
        context_->UpdateSubresource(texture_.Get(), subresource, &box, data,
                                    static_cast<UINT>(rowBytes), static_cast<UINT>(required));
        // UpdateSubresource copies out of `data` before returning -- nothing here still depends on
        // caller memory once this call completes (REMED-GFX-135).
        return true;
    }

    bool D3D11TextureCubeRenderer::GetData(int face, int level, int x, int y, int w, int h,
                                          void* data, int dataLength) const
    {
        // REMED-GFX-130: each silent `return` here became a complete transparent-black face once
        // the shared layer converted its own zeroed scratch buffer regardless.
        if (level < 0 || level >= mipLevels_ || face < 0 || face >= 6 || w <= 0 || h <= 0) return false;
        if (data == nullptr) return false;
        const std::size_t rowBytes = static_cast<std::size_t>(w) * bytesPerTexel_;
        const std::size_t required = rowBytes * static_cast<std::size_t>(h);
        if (dataLength < 0 || static_cast<std::size_t>(dataLength) < required) return false;

        D3D11_TEXTURE2D_DESC desc{};
        texture_->GetDesc(&desc);
        D3D11_TEXTURE2D_DESC stagingDesc = desc;
        stagingDesc.Usage = D3D11_USAGE_STAGING;
        stagingDesc.BindFlags = 0;
        stagingDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        stagingDesc.MiscFlags = 0;

        ComPtr<ID3D11Texture2D> staging;
        if (FAILED(device_->CreateTexture2D(&stagingDesc, nullptr, staging.GetAddressOf()))) return false;
        context_->CopyResource(staging.Get(), texture_.Get());

        const UINT subresource = D3D11CalcSubresource(static_cast<UINT>(level), static_cast<UINT>(face),
                                                       static_cast<UINT>(mipLevels_));
        D3D11_MAPPED_SUBRESOURCE mapped{};
        if (FAILED(context_->Map(staging.Get(), subresource, D3D11_MAP_READ, 0, &mapped))) return false;

        uint8_t* dst = static_cast<uint8_t*>(data);
        for (int row = 0; row < h; ++row)
        {
            const uint8_t* src = static_cast<const uint8_t*>(mapped.pData)
                                + static_cast<std::size_t>(y + row) * mapped.RowPitch
                                + static_cast<std::size_t>(x) * bytesPerTexel_;
            std::memcpy(dst + static_cast<std::size_t>(row) * rowBytes, src, rowBytes);
        }
        context_->Unmap(staging.Get(), subresource);
        return true;
    }

    // -------------------------------------------------------------------------
    // D3D11Texture3DRenderer
    // -------------------------------------------------------------------------

    D3D11Texture3DRenderer::D3D11Texture3DRenderer(
        ID3D11Device* device, ID3D11DeviceContext* context,
        int w, int h, int depth, bool mipMap, int surfaceFormat)
        : device_(device), context_(context)
        , width_(w), height_(h), depth_(depth)
        , mipLevels_(mipMap ? CalculateMipLevels(std::max(w, std::max(h, depth)), 1) : 1)
        , surfaceFormat_(surfaceFormat)
    {
        ResolveSurfaceFormat(surfaceFormat_, dxgiFormat_, bytesPerTexel_,
                             "D3D11Texture3DRenderer");
        D3D11_TEXTURE3D_DESC desc{};
        desc.Width = static_cast<UINT>(width_);
        desc.Height = static_cast<UINT>(height_);
        desc.Depth = static_cast<UINT>(depth_);
        desc.MipLevels = static_cast<UINT>(mipLevels_);
        desc.Format = dxgiFormat_;
        desc.Usage = D3D11_USAGE_DEFAULT;
        desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

        HRESULT hr = device_->CreateTexture3D(&desc, nullptr, texture_.GetAddressOf());
        if (FAILED(hr))
            throw std::runtime_error("D3D11Texture3DRenderer: CreateTexture3D failed, hr=" + FormatHr(hr));

        hr = device_->CreateShaderResourceView(texture_.Get(), nullptr, srv_.GetAddressOf());
        if (FAILED(hr))
            throw std::runtime_error("D3D11Texture3DRenderer: CreateShaderResourceView failed, hr=" + FormatHr(hr));
    }

    bool D3D11Texture3DRenderer::SetData(int level, int x, int y, int z, int w, int h, int depth,
                                        const void* data, int dataLength)
    {
        // REMED-GFX-135: see D3D11TextureCubeRenderer::SetData -- silent returns looked like writes.
        if (level < 0 || level >= mipLevels_) return false;
        if (data == nullptr || w <= 0 || h <= 0 || depth <= 0) return false;
        const int levelW = std::max(1, width_ >> level);
        const int levelH = std::max(1, height_ >> level);
        const int levelD = std::max(1, depth_ >> level);
        if (x < 0 || y < 0 || z < 0 || x + w > levelW || y + h > levelH || z + depth > levelD)
            return false;
        const std::size_t rowBytes = static_cast<std::size_t>(w) * bytesPerTexel_;
        const std::size_t sliceBytes = rowBytes * static_cast<std::size_t>(h);
        const std::size_t required = sliceBytes * static_cast<std::size_t>(depth);
        if (dataLength < 0 || static_cast<std::size_t>(dataLength) < required) return false;
        D3D11_BOX box{};
        box.left = static_cast<UINT>(x);
        box.top = static_cast<UINT>(y);
        box.front = static_cast<UINT>(z);
        box.right = static_cast<UINT>(x + w);
        box.bottom = static_cast<UINT>(y + h);
        box.back = static_cast<UINT>(z + depth);
        context_->UpdateSubresource(texture_.Get(), static_cast<UINT>(level), &box, data,
                                    static_cast<UINT>(rowBytes), static_cast<UINT>(sliceBytes));
        return true;
    }

    bool D3D11Texture3DRenderer::GetData(int level, int x, int y, int z, int w, int h, int depth,
                                        void* data, int dataLength) const
    {
        // REMED-GFX-130: see D3D11TextureCubeRenderer::GetData above.
        if (level < 0 || level >= mipLevels_ || w <= 0 || h <= 0 || depth <= 0) return false;
        if (data == nullptr) return false;
        const std::size_t rowBytes = static_cast<std::size_t>(w) * bytesPerTexel_;
        const std::size_t sliceBytes = rowBytes * static_cast<std::size_t>(h);
        const std::size_t required = sliceBytes * static_cast<std::size_t>(depth);
        if (dataLength < 0 || static_cast<std::size_t>(dataLength) < required) return false;

        D3D11_TEXTURE3D_DESC desc{};
        texture_->GetDesc(&desc);
        D3D11_TEXTURE3D_DESC stagingDesc = desc;
        stagingDesc.Usage = D3D11_USAGE_STAGING;
        stagingDesc.BindFlags = 0;
        stagingDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        stagingDesc.MiscFlags = 0;

        ComPtr<ID3D11Texture3D> staging;
        if (FAILED(device_->CreateTexture3D(&stagingDesc, nullptr, staging.GetAddressOf()))) return false;
        context_->CopyResource(staging.Get(), texture_.Get());

        D3D11_MAPPED_SUBRESOURCE mapped{};
        if (FAILED(context_->Map(staging.Get(), static_cast<UINT>(level), D3D11_MAP_READ, 0, &mapped))) return false;

        uint8_t* dst = static_cast<uint8_t*>(data);
        for (int slice = 0; slice < depth; ++slice)
        {
            for (int row = 0; row < h; ++row)
            {
                const uint8_t* src = static_cast<const uint8_t*>(mapped.pData)
                                    + static_cast<std::size_t>(z + slice) * mapped.DepthPitch
                                    + static_cast<std::size_t>(y + row) * mapped.RowPitch
                                    + static_cast<std::size_t>(x) * bytesPerTexel_;
                uint8_t* dstRow = dst
                                 + (static_cast<std::size_t>(slice) * static_cast<std::size_t>(h) + row)
                                       * rowBytes;
                std::memcpy(dstRow, src, rowBytes);
            }
        }
        context_->Unmap(staging.Get(), static_cast<UINT>(level));
        return true;
    }
}
