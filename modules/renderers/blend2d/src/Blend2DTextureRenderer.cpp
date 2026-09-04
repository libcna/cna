#include "CNA/Internal/Renderers/Blend2D/Blend2DRenderer.hpp"
#include "CNA/Internal/Renderers/Blend2D/Blend2DCheckedCallEXT.hpp"
#include "CNA/Internal/Renderers/Blend2D/Blend2DPixelConvert.hpp"

#include <cstdint>
#include <stdexcept>

namespace CNA::Internal::Renderers::Blend2D
{
    Blend2DTextureRenderer::Blend2DTextureRenderer(int width, int height, const std::uint8_t* rgba)
        : width_(width > 0 ? width : 1), height_(height > 0 ? height : 1)
    {
        Blend2DCheckEXT(image_.create(width_, height_, BL_FORMAT_PRGB32),
                        "Blend2DTextureRenderer: BLImage::create");

        BLImageData data{};
        Blend2DCheckEXT(image_.make_mutable(&data), "Blend2DTextureRenderer: BLImage::make_mutable");

        // ImageData::pixels (CNA's own upload contract, Blend2DRenderer::CreateTexture) is always
        // tightly packed -- unlike UpdatePixels below, there is no caller-supplied stride here, so
        // width_ * 4 is the correct, exact source row pitch, not an assumption.
        auto* base = static_cast<std::uint8_t*>(data.pixel_data);
        for (int row = 0; row < height_; ++row)
        {
            const std::uint8_t* srcRow = rgba + static_cast<std::ptrdiff_t>(row) * width_ * 4;
            std::uint8_t* dstRow = base + static_cast<std::ptrdiff_t>(row) * data.stride;
            ConvertStraightRgbaRowToPremultipliedBgra(srcRow, dstRow, width_);
        }
    }

    void Blend2DTextureRenderer::UpdatePixels(const std::uint8_t* rgba, int stride)
    {
        // ITextureRenderer::UpdatePixels's stride parameter is the SOURCE row pitch in bytes --
        // it need not equal width_ * 4 (a caller may supply a larger stride with padding between
        // rows). The previous implementation ignored this parameter entirely and always advanced
        // by width_ * 4, silently reading the wrong bytes (or running past the caller's buffer)
        // for any non-tightly-packed upload.
        const std::ptrdiff_t rowStride = stride > 0 ? static_cast<std::ptrdiff_t>(stride)
                                                     : static_cast<std::ptrdiff_t>(width_) * 4;

        BLImageData data{};
        Blend2DCheckEXT(image_.make_mutable(&data), "Blend2DTextureRenderer::UpdatePixels: BLImage::make_mutable");

        auto* base = static_cast<std::uint8_t*>(data.pixel_data);
        for (int row = 0; row < height_; ++row)
        {
            const std::uint8_t* srcRow = rgba + static_cast<std::ptrdiff_t>(row) * rowStride;
            std::uint8_t* dstRow = base + static_cast<std::ptrdiff_t>(row) * data.stride;
            ConvertStraightRgbaRowToPremultipliedBgra(srcRow, dstRow, width_);
        }
    }

    bool Blend2DTextureRenderer::GetData(int level, int x, int y, int w, int h, void* data,
                                         int dataLength) const
    {
        if (level != 0) return false;
        if (x < 0 || y < 0 || w <= 0 || h <= 0 ||
            static_cast<std::int64_t>(x) + w > width_ || static_cast<std::int64_t>(y) + h > height_)
        {
            return false;
        }
        // 64-bit product: w/h are bounded by width_/height_ above, but dataLength is caller-
        // supplied and the multiplication must not overflow int before the comparison runs.
        if (dataLength < 0 ||
            static_cast<std::int64_t>(w) * static_cast<std::int64_t>(h) * 4 > dataLength)
        {
            return false;
        }

        BLImageData imageData{};
        if (image_.get_data(&imageData) != BL_SUCCESS) return false;

        const auto* base = static_cast<const std::uint8_t*>(imageData.pixel_data);
        auto* dst = static_cast<std::uint8_t*>(data);
        for (int row = 0; row < h; ++row)
        {
            const std::uint8_t* srcRow = base + static_cast<std::ptrdiff_t>(y + row) * imageData.stride
                + static_cast<std::ptrdiff_t>(x) * 4;
            std::uint8_t* dstRow = dst + static_cast<std::ptrdiff_t>(row) * w * 4;
            ConvertPremultipliedBgraRowToStraightRgba(srcRow, dstRow, w);
        }
        return true;
    }
} // namespace CNA::Internal::Renderers::Blend2D
