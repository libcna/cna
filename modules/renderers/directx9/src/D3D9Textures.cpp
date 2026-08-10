#include "CNA/Internal/Renderers/DirectX9/D3D9Textures.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>
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

        /// Mirrors D3D11Textures.cpp's own CalculateMipLevels: full mip chain down to 1x1.
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
    }

    // -------------------------------------------------------------------------
    // D3D9TextureRenderer
    // -------------------------------------------------------------------------

    D3D9TextureRenderer::D3D9TextureRenderer(IDirect3DDevice9* device, const ImageData& data)
        : device_(device)
        , width_(data.width), height_(data.height)
        , mipLevels_(data.mipLevels > 0 ? data.mipLevels : 1)
    {
        const HRESULT hr = device_->CreateTexture(
            static_cast<UINT>(width_), static_cast<UINT>(height_), static_cast<UINT>(mipLevels_),
            0, D3DFMT_A8B8G8R8, D3DPOOL_MANAGED, texture_.GetAddressOf(), nullptr);
        if (FAILED(hr))
            throw std::runtime_error("D3D9TextureRenderer: CreateTexture failed, hr=" + FormatHr(hr));

        if (!data.pixels.empty())
            UpdatePixels(data.pixels.data(), width_ * 4);
    }

    void D3D9TextureRenderer::UpdatePixels(const uint8_t* rgba, int stride)
    {
        const std::size_t rowBytes = stride > 0 ? static_cast<std::size_t>(stride)
                                                  : static_cast<std::size_t>(width_) * 4;
        D3DLOCKED_RECT locked{};
        const HRESULT hr = texture_->LockRect(0, &locked, nullptr, 0);
        if (FAILED(hr))
            throw std::runtime_error("D3D9TextureRenderer::UpdatePixels: LockRect failed, hr=" + FormatHr(hr));

        auto* dst = static_cast<uint8_t*>(locked.pBits);
        for (int row = 0; row < height_; ++row)
        {
            std::memcpy(dst + static_cast<std::size_t>(row) * locked.Pitch,
                        rgba + static_cast<std::size_t>(row) * rowBytes,
                        static_cast<std::size_t>(width_) * 4);
        }
        texture_->UnlockRect(0);
    }

    void D3D9TextureRenderer::UpdatePixelsLevel(int level, const uint8_t* rgba, int levelW, int levelH)
    {
        if (level < 0 || level >= mipLevels_) return;

        D3DLOCKED_RECT locked{};
        const HRESULT hr = texture_->LockRect(static_cast<UINT>(level), &locked, nullptr, 0);
        if (FAILED(hr))
            throw std::runtime_error("D3D9TextureRenderer::UpdatePixelsLevel: LockRect failed, hr=" + FormatHr(hr));

        auto* dst = static_cast<uint8_t*>(locked.pBits);
        const std::size_t rowBytes = static_cast<std::size_t>(levelW) * 4;
        for (int row = 0; row < levelH; ++row)
        {
            std::memcpy(dst + static_cast<std::size_t>(row) * locked.Pitch,
                        rgba + static_cast<std::size_t>(row) * rowBytes, rowBytes);
        }
        texture_->UnlockRect(static_cast<UINT>(level));
    }

    // -------------------------------------------------------------------------
    // D3D9TextureCubeRenderer
    // -------------------------------------------------------------------------

    D3D9TextureCubeRenderer::D3D9TextureCubeRenderer(
        IDirect3DDevice9* device, int size, bool mipMap, int /*surfaceFormat*/)
        : device_(device)
        , size_(size), mipLevels_(mipMap ? CalculateMipLevels(size, size) : 1)
    {
        const HRESULT hr = device_->CreateCubeTexture(
            static_cast<UINT>(size_), static_cast<UINT>(mipLevels_), 0,
            D3DFMT_A8B8G8R8, D3DPOOL_MANAGED, texture_.GetAddressOf(), nullptr);
        if (FAILED(hr))
            throw std::runtime_error("D3D9TextureCubeRenderer: CreateCubeTexture failed, hr=" + FormatHr(hr));
    }

    bool D3D9TextureCubeRenderer::SetData(int face, int level, int x, int y, int w, int h,
                                         const void* data, int dataLength)
    {
        // REMED-GFX-135: these used to be a silent `return` the shared layer read as a completed
        // upload, and neither the source pointer nor the rectangle was checked at all.
        if (level < 0 || level >= mipLevels_ || face < 0 || face >= 6) return false;
        if (data == nullptr || w <= 0 || h <= 0) return false;
        const int levelSize = std::max(1, size_ >> level);
        if (x < 0 || y < 0 || x + w > levelSize || y + h > levelSize) return false;
        if (dataLength < w * h * 4) return false;

        RECT rect{ x, y, x + w, y + h };
        D3DLOCKED_RECT locked{};
        const HRESULT hr = texture_->LockRect(static_cast<D3DCUBEMAP_FACES>(face),
                                              static_cast<UINT>(level), &locked, &rect, 0);
        if (FAILED(hr))
            throw std::runtime_error("D3D9TextureCubeRenderer::SetData: LockRect failed, hr=" + FormatHr(hr));

        const auto* src = static_cast<const uint8_t*>(data);
        auto* dst = static_cast<uint8_t*>(locked.pBits);
        const std::size_t rowBytes = static_cast<std::size_t>(w) * 4;
        for (int row = 0; row < h; ++row)
        {
            std::memcpy(dst + static_cast<std::size_t>(row) * locked.Pitch,
                        src + static_cast<std::size_t>(row) * rowBytes, rowBytes);
        }
        texture_->UnlockRect(static_cast<D3DCUBEMAP_FACES>(face), static_cast<UINT>(level));
        return true;
    }

    bool D3D9TextureCubeRenderer::GetData(int face, int level, int x, int y, int w, int h,
                                         void* data, int dataLength) const
    {
        // REMED-GFX-130: each silent `return` here became a complete transparent-black face once
        // the shared layer converted its own zeroed scratch buffer regardless.
        if (level < 0 || level >= mipLevels_ || face < 0 || face >= 6 || w <= 0 || h <= 0) return false;
        if (data == nullptr || dataLength < w * h * 4) return false;

        RECT rect{ x, y, x + w, y + h };
        D3DLOCKED_RECT locked{};
        const HRESULT hr = texture_->LockRect(static_cast<D3DCUBEMAP_FACES>(face),
                                              static_cast<UINT>(level), &locked, &rect, D3DLOCK_READONLY);
        if (FAILED(hr)) return false;

        auto* dst = static_cast<uint8_t*>(data);
        const auto* src = static_cast<const uint8_t*>(locked.pBits);
        const std::size_t rowBytes = static_cast<std::size_t>(w) * 4;
        for (int row = 0; row < h; ++row)
        {
            std::memcpy(dst + static_cast<std::size_t>(row) * rowBytes,
                        src + static_cast<std::size_t>(row) * locked.Pitch, rowBytes);
        }
        texture_->UnlockRect(static_cast<D3DCUBEMAP_FACES>(face), static_cast<UINT>(level));
        return true;
    }

    // -------------------------------------------------------------------------
    // D3D9Texture3DRenderer
    // -------------------------------------------------------------------------

    D3D9Texture3DRenderer::D3D9Texture3DRenderer(
        IDirect3DDevice9* device, int w, int h, int depth, bool mipMap, int /*surfaceFormat*/)
        : device_(device)
        , width_(w), height_(h), depth_(depth)
        , mipLevels_(mipMap ? CalculateMipLevels(std::max(w, std::max(h, depth)), 1) : 1)
    {
        const HRESULT hr = device_->CreateVolumeTexture(
            static_cast<UINT>(width_), static_cast<UINT>(height_), static_cast<UINT>(depth_),
            static_cast<UINT>(mipLevels_), 0, D3DFMT_A8B8G8R8, D3DPOOL_MANAGED,
            texture_.GetAddressOf(), nullptr);
        if (FAILED(hr))
            throw std::runtime_error("D3D9Texture3DRenderer: CreateVolumeTexture failed, hr=" + FormatHr(hr));
    }

    bool D3D9Texture3DRenderer::SetData(int level, int x, int y, int z, int w, int h, int depth,
                                       const void* data, int dataLength)
    {
        // REMED-GFX-135: see D3D9TextureCubeRenderer::SetData -- silent returns looked like writes.
        if (level < 0 || level >= mipLevels_) return false;
        if (data == nullptr || w <= 0 || h <= 0 || depth <= 0) return false;
        const int levelW = std::max(1, width_ >> level);
        const int levelH = std::max(1, height_ >> level);
        const int levelD = std::max(1, depth_ >> level);
        if (x < 0 || y < 0 || z < 0 || x + w > levelW || y + h > levelH || z + depth > levelD)
            return false;
        if (dataLength < w * h * depth * 4) return false;

        D3DBOX box{};
        box.Left = static_cast<UINT>(x);
        box.Top = static_cast<UINT>(y);
        box.Front = static_cast<UINT>(z);
        box.Right = static_cast<UINT>(x + w);
        box.Bottom = static_cast<UINT>(y + h);
        box.Back = static_cast<UINT>(z + depth);

        D3DLOCKED_BOX locked{};
        const HRESULT hr = texture_->LockBox(static_cast<UINT>(level), &locked, &box, 0);
        if (FAILED(hr))
            throw std::runtime_error("D3D9Texture3DRenderer::SetData: LockBox failed, hr=" + FormatHr(hr));

        const auto* src = static_cast<const uint8_t*>(data);
        auto* dst = static_cast<uint8_t*>(locked.pBits);
        const std::size_t rowBytes = static_cast<std::size_t>(w) * 4;
        for (int slice = 0; slice < depth; ++slice)
        {
            for (int row = 0; row < h; ++row)
            {
                const uint8_t* srcRow = src
                    + (static_cast<std::size_t>(slice) * static_cast<std::size_t>(h) + row) * rowBytes;
                uint8_t* dstRow = dst
                    + static_cast<std::size_t>(slice) * locked.SlicePitch
                    + static_cast<std::size_t>(row) * locked.RowPitch;
                std::memcpy(dstRow, srcRow, rowBytes);
            }
        }
        texture_->UnlockBox(static_cast<UINT>(level));
        return true;
    }

    bool D3D9Texture3DRenderer::GetData(int level, int x, int y, int z, int w, int h, int depth,
                                       void* data, int dataLength) const
    {
        // REMED-GFX-130: see D3D9TextureCubeRenderer::GetData above.
        if (level < 0 || level >= mipLevels_ || w <= 0 || h <= 0 || depth <= 0) return false;
        if (data == nullptr || dataLength < w * h * depth * 4) return false;

        D3DBOX box{};
        box.Left = static_cast<UINT>(x);
        box.Top = static_cast<UINT>(y);
        box.Front = static_cast<UINT>(z);
        box.Right = static_cast<UINT>(x + w);
        box.Bottom = static_cast<UINT>(y + h);
        box.Back = static_cast<UINT>(z + depth);

        D3DLOCKED_BOX locked{};
        const HRESULT hr = texture_->LockBox(static_cast<UINT>(level), &locked, &box, D3DLOCK_READONLY);
        if (FAILED(hr)) return false;

        auto* dst = static_cast<uint8_t*>(data);
        const auto* src = static_cast<const uint8_t*>(locked.pBits);
        const std::size_t rowBytes = static_cast<std::size_t>(w) * 4;
        for (int slice = 0; slice < depth; ++slice)
        {
            for (int row = 0; row < h; ++row)
            {
                const uint8_t* srcRow = src
                    + static_cast<std::size_t>(slice) * locked.SlicePitch
                    + static_cast<std::size_t>(row) * locked.RowPitch;
                uint8_t* dstRow = dst
                    + (static_cast<std::size_t>(slice) * static_cast<std::size_t>(h) + row) * rowBytes;
                std::memcpy(dstRow, srcRow, rowBytes);
            }
        }
        texture_->UnlockBox(static_cast<UINT>(level));
        return true;
    }
}
