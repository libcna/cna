#include "CNA/Internal/Renderers/Blend2D/Blend2DRenderer.hpp"
#include "CNA/Internal/Renderers/Blend2D/Blend2DPixelConvert.hpp"

#include <stdexcept>

namespace CNA::Internal::Renderers::Blend2D
{
    Blend2DTextureRenderer::Blend2DTextureRenderer(int width, int height, const std::uint8_t* rgba)
        : width_(width > 0 ? width : 1), height_(height > 0 ? height : 1)
    {
        if (image_.create(width_, height_, BL_FORMAT_PRGB32) != BL_SUCCESS)
            throw std::runtime_error("Blend2DTextureRenderer: BLImage::create failed");

        BLImageData data{};
        if (image_.make_mutable(&data) != BL_SUCCESS)
            throw std::runtime_error("Blend2DTextureRenderer: BLImage::make_mutable failed");

        auto* base = static_cast<std::uint8_t*>(data.pixel_data);
        for (int row = 0; row < height_; ++row)
        {
            const std::uint8_t* srcRow = rgba + static_cast<std::ptrdiff_t>(row) * width_ * 4;
            std::uint8_t* dstRow = base + static_cast<std::ptrdiff_t>(row) * data.stride;
            ConvertStraightRgbaRowToPremultipliedBgra(srcRow, dstRow, width_);
        }
    }

    void Blend2DTextureRenderer::UpdatePixels(const std::uint8_t* rgba, int /*stride*/)
    {
        BLImageData data{};
        if (image_.make_mutable(&data) != BL_SUCCESS)
            throw std::runtime_error("Blend2DTextureRenderer::UpdatePixels: BLImage::make_mutable failed");

        auto* base = static_cast<std::uint8_t*>(data.pixel_data);
        for (int row = 0; row < height_; ++row)
        {
            const std::uint8_t* srcRow = rgba + static_cast<std::ptrdiff_t>(row) * width_ * 4;
            std::uint8_t* dstRow = base + static_cast<std::ptrdiff_t>(row) * data.stride;
            ConvertStraightRgbaRowToPremultipliedBgra(srcRow, dstRow, width_);
        }
    }

    bool Blend2DTextureRenderer::GetData(int level, int x, int y, int w, int h, void* data,
                                         int dataLength) const
    {
        if (level != 0) return false;
        if (x < 0 || y < 0 || w <= 0 || h <= 0 || x + w > width_ || y + h > height_) return false;
        if (dataLength < w * h * 4) return false;

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
