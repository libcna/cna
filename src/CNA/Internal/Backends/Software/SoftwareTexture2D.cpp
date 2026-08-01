// SPDX-License-Identifier: MS-PL

#include "CNA/Internal/Backends/Software/SoftwareGraphicsBackend.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <vector>

namespace CNA::Internal::Backends::Software
{
    SoftwareTextureBackend::SoftwareTextureBackend(const ImageData& data)
        : width_(data.width), height_(data.height)
        , declaredLevels_(data.mipLevels > 0 ? data.mipLevels : 1)
    {
        pixels_.assign(data.pixels.begin(), data.pixels.end());
    }

    SoftwareTextureBackend::SoftwareTextureBackend(int width, int height)
        : width_(width), height_(height)
    {
        pixels_.assign(static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4u, 0u);
    }

    void SoftwareTextureBackend::UpdatePixels(const uint8_t* rgba, int stride)
    {
        if (rgba == nullptr)
            throw std::runtime_error("SoftwareTextureBackend::UpdatePixels: rgba must not be null");
        const std::size_t rowBytes = static_cast<std::size_t>(width_) * 4u;
        const std::size_t effectiveStride = stride > 0 ? static_cast<std::size_t>(stride) : rowBytes;
        pixels_.resize(rowBytes * static_cast<std::size_t>(height_));
        for (int y = 0; y < height_; ++y)
        {
            std::copy(rgba + static_cast<std::size_t>(y) * effectiveStride,
                     rgba + static_cast<std::size_t>(y) * effectiveStride + rowBytes,
                     pixels_.begin() + static_cast<std::ptrdiff_t>(y) * static_cast<std::ptrdiff_t>(rowBytes));
        }
    }

    // REMED-GFX-175: mip levels above 0 used to be dropped on the floor here, which is why NO
    // TextureFilter ordinal could mip-filter on this backend -- not merely ordinals 0 and 1. The
    // level a caller supplies is now STORED, exactly as given: nothing is downsampled, nothing is
    // generated, and a level the caller never writes is never invented. `storedLevels_` counts only
    // the levels held CONTIGUOUSLY from 0, so a chain written out of order, or abandoned half way,
    // bounds the sampler at the last level that really exists instead of exposing a gap.
    void SoftwareTextureBackend::UpdatePixelsLevel(int level, const uint8_t* rgba,
                                                   int levelW, int levelH)
    {
        if (level <= 0 || rgba == nullptr) return;
        if (level >= declaredLevels_) return;
        if (levelW <= 0 || levelH <= 0) return;

        if (static_cast<int>(mipLevels_.size()) < level)
            mipLevels_.resize(static_cast<std::size_t>(level));

        MipLevel& dst = mipLevels_[static_cast<std::size_t>(level - 1)];
        dst.width = levelW;
        dst.height = levelH;
        const std::size_t bytes = static_cast<std::size_t>(levelW) *
                                  static_cast<std::size_t>(levelH) * 4u;
        dst.pixels.assign(rgba, rgba + bytes);

        // Recount from level 1 upward: a level only counts once every level below it is present.
        int contiguous = 1;
        for (std::size_t i = 0; i < mipLevels_.size(); ++i)
        {
            if (mipLevels_[i].pixels.empty()) break;
            ++contiguous;
        }
        storedLevels_ = contiguous;
    }

    int SoftwareTextureBackend::ColorWidth(int level) const
    {
        if (level <= 0 || level > static_cast<int>(mipLevels_.size())) return width_;
        return mipLevels_[static_cast<std::size_t>(level - 1)].width;
    }

    int SoftwareTextureBackend::ColorHeight(int level) const
    {
        if (level <= 0 || level > static_cast<int>(mipLevels_.size())) return height_;
        return mipLevels_[static_cast<std::size_t>(level - 1)].height;
    }

    const std::vector<std::uint8_t>& SoftwareTextureBackend::ColorPixels(int level) const
    {
        if (level <= 0 || level > static_cast<int>(mipLevels_.size())) return pixels_;
        return mipLevels_[static_cast<std::size_t>(level - 1)].pixels;
    }
}
