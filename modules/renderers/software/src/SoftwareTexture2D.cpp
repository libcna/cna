// SPDX-License-Identifier: MS-PL

#include "CNA/Internal/Renderers/Software/SoftwareRenderer.hpp"
#include "SoftwareTextureErrors.hpp"

#include "System/ArgumentOutOfRangeException.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <new>
#include <stdexcept>
#include <string>
#include <vector>

namespace CNA::Internal::Renderers::Software
{
    // GDI-076: GDI's own 16,384-per-axis ceiling made a single square RGBA8 level as large as
    // 1 GiB before this planner existed -- neither this constructor, GdiRenderer's
    // CreateTexture forward, nor the direct-ImageData boundary rejected it, and a caller-supplied
    // pixel buffer smaller than width*height*4 was copied verbatim rather than validated, an
    // out-of-bounds read waiting to happen the first time the rasterizer sampled past it.
    SoftwareTextureRenderer::SoftwareTextureRenderer(const ImageData& data)
        : width_(data.width), height_(data.height)
        , declaredLevels_(data.mipLevels > 0 ? data.mipLevels : 1)
    {
        const SoftwareTextureAllocationRequest request{width_, height_, declaredLevels_};
        const SoftwareTextureAllocationLayout layout = PlanSoftwareTextureAllocation(request);
        if (!layout.IsValid())
            ThrowInvalidTextureLayout(request, layout.error);
        if (data.pixels.size() < layout.baseColorBytes)
            ThrowTexturePixelDataMismatch(width_, height_, layout.baseColorBytes,
                                         data.pixels.size());

        try
        {
            // Only ever retain exactly one level 0 worth of bytes: a caller that supplied a
            // larger buffer than width*height*4 must not have its excess silently retained.
            pixels_.assign(data.pixels.begin(),
                           data.pixels.begin() +
                               static_cast<std::ptrdiff_t>(layout.baseColorBytes));
        }
        catch (const std::bad_alloc&)
        {
            ThrowTextureAllocationFailure(request, layout);
        }
        catch (const std::length_error&)
        {
            ThrowTextureAllocationFailure(request, layout);
        }
    }

    SoftwareTextureRenderer::SoftwareTextureRenderer(int width, int height)
        : width_(width), height_(height)
    {
        const SoftwareTextureAllocationRequest request{width_, height_, declaredLevels_};
        const SoftwareTextureAllocationLayout layout = PlanSoftwareTextureAllocation(request);
        if (!layout.IsValid())
            ThrowInvalidTextureLayout(request, layout.error);

        try
        {
            pixels_.assign(layout.baseColorBytes, 0u);
        }
        catch (const std::bad_alloc&)
        {
            ThrowTextureAllocationFailure(request, layout);
        }
        catch (const std::length_error&)
        {
            ThrowTextureAllocationFailure(request, layout);
        }
    }

    void SoftwareTextureRenderer::UpdatePixels(const uint8_t* rgba, int stride)
    {
        if (rgba == nullptr)
            throw std::runtime_error("SoftwareTextureRenderer::UpdatePixels: rgba must not be null");
        const std::size_t rowBytes = static_cast<std::size_t>(width_) * 4u;
        // REMED-GFX-229: a positive pitch smaller than one complete RGBA8 row makes the
        // row-by-row copy overlap the prior row and read past the caller's final row. Validate it
        // before resizing or changing the authoritative texture bytes. Zero/negative retains the
        // existing renderer contract of selecting a tightly packed upload.
        if (stride > 0 && static_cast<std::size_t>(stride) < rowBytes)
            throw System::ArgumentOutOfRangeException(
                "stride", std::to_string(stride),
                "A positive texture upload stride must be at least " +
                    std::to_string(rowBytes) + " bytes (width * 4 for RGBA8).");
        const std::size_t effectiveStride = stride > 0 ? static_cast<std::size_t>(stride) : rowBytes;
        try
        {
            pixels_.resize(rowBytes * static_cast<std::size_t>(height_));
        }
        catch (const std::bad_alloc&)
        {
            ThrowTextureAllocationFailure(
                {width_, height_, declaredLevels_},
                PlanSoftwareTextureAllocation({width_, height_, declaredLevels_}));
        }
        catch (const std::length_error&)
        {
            ThrowTextureAllocationFailure(
                {width_, height_, declaredLevels_},
                PlanSoftwareTextureAllocation({width_, height_, declaredLevels_}));
        }
        for (int y = 0; y < height_; ++y)
        {
            std::copy(rgba + static_cast<std::size_t>(y) * effectiveStride,
                     rgba + static_cast<std::size_t>(y) * effectiveStride + rowBytes,
                     pixels_.begin() + static_cast<std::ptrdiff_t>(y) * static_cast<std::ptrdiff_t>(rowBytes));
        }
    }

    // REMED-GFX-175: mip levels above 0 used to be dropped on the floor here, which is why NO
    // TextureFilter ordinal could mip-filter on this renderer -- not merely ordinals 0 and 1. The
    // level a caller supplies is now STORED, exactly as given: nothing is downsampled, nothing is
    // generated, and a level the caller never writes is never invented. `storedLevels_` counts only
    // the levels held CONTIGUOUSLY from 0, so a chain written out of order, or abandoned half way,
    // bounds the sampler at the last level that really exists instead of exposing a gap.
    void SoftwareTextureRenderer::UpdatePixelsLevel(int level, const uint8_t* rgba,
                                                   int levelW, int levelH)
    {
        if (level <= 0 || rgba == nullptr) return;
        if (level >= declaredLevels_) return;
        if (levelW <= 0 || levelH <= 0) return;

        // GDI-076: this level's own bytes still participate in the same per-resource budget as
        // level 0 -- a caller-supplied levelW/levelH is trusted for shape (there is no length
        // parameter here to check against), but not for size.
        const SoftwareTextureAllocationRequest levelRequest{levelW, levelH, 1};
        const SoftwareTextureAllocationLayout levelLayout =
            PlanSoftwareTextureAllocation(levelRequest);
        if (!levelLayout.IsValid())
            ThrowInvalidTextureLayout(levelRequest, levelLayout.error);

        if (static_cast<int>(mipLevels_.size()) < level)
            mipLevels_.resize(static_cast<std::size_t>(level));

        MipLevel& dst = mipLevels_[static_cast<std::size_t>(level - 1)];
        dst.width = levelW;
        dst.height = levelH;
        try
        {
            dst.pixels.assign(rgba, rgba + levelLayout.baseColorBytes);
        }
        catch (const std::bad_alloc&)
        {
            ThrowTextureAllocationFailure(levelRequest, levelLayout);
        }
        catch (const std::length_error&)
        {
            ThrowTextureAllocationFailure(levelRequest, levelLayout);
        }

        // Recount from level 1 upward: a level only counts once every level below it is present.
        int contiguous = 1;
        for (std::size_t i = 0; i < mipLevels_.size(); ++i)
        {
            if (mipLevels_[i].pixels.empty()) break;
            ++contiguous;
        }
        storedLevels_ = contiguous;
    }

    int SoftwareTextureRenderer::ColorWidth(int level) const
    {
        if (level <= 0 || level > static_cast<int>(mipLevels_.size())) return width_;
        return mipLevels_[static_cast<std::size_t>(level - 1)].width;
    }

    int SoftwareTextureRenderer::ColorHeight(int level) const
    {
        if (level <= 0 || level > static_cast<int>(mipLevels_.size())) return height_;
        return mipLevels_[static_cast<std::size_t>(level - 1)].height;
    }

    const std::vector<std::uint8_t>& SoftwareTextureRenderer::ColorPixels(int level) const
    {
        if (level <= 0 || level > static_cast<int>(mipLevels_.size())) return pixels_;
        return mipLevels_[static_cast<std::size_t>(level - 1)].pixels;
    }
}
