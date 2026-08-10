#include "CNA/Internal/Renderers/Blend2D/Blend2DSurface.hpp"
#include "CNA/Internal/Renderers/Blend2D/Blend2DPixelConvert.hpp"

#include <algorithm>
#include <stdexcept>

namespace CNA::Internal::Renderers::Blend2D
{
    namespace
    {
        [[nodiscard]] std::uint8_t ToByte(float channel)
        {
            const float clamped = std::clamp(channel, 0.0f, 1.0f);
            return static_cast<std::uint8_t>(clamped * 255.0f + 0.5f);
        }
    } // namespace

    Blend2DSurface::Blend2DSurface(int width, int height)
        : width_(width > 0 ? width : 1), height_(height > 0 ? height : 1)
    {
        if (image_.create(width_, height_, BL_FORMAT_PRGB32) != BL_SUCCESS)
            throw std::runtime_error("Blend2DSurface: BLImage::create failed");
        if (context_.begin(image_) != BL_SUCCESS)
            throw std::runtime_error("Blend2DSurface: BLContext::begin failed");
        Clear(0.0f, 0.0f, 0.0f, 0.0f);
    }

    Blend2DSurface::~Blend2DSurface()
    {
        context_.end();
    }

    void Blend2DSurface::Resize(int width, int height)
    {
        width_ = width > 0 ? width : 1;
        height_ = height > 0 ? height : 1;
        context_.end();
        if (image_.create(width_, height_, BL_FORMAT_PRGB32) != BL_SUCCESS)
            throw std::runtime_error("Blend2DSurface::Resize: BLImage::create failed");
        if (context_.begin(image_) != BL_SUCCESS)
            throw std::runtime_error("Blend2DSurface::Resize: BLContext::begin failed");
        Clear(0.0f, 0.0f, 0.0f, 0.0f);
    }

    void Blend2DSurface::Clear(float r, float g, float b, float a)
    {
        context_.set_comp_op(BL_COMP_OP_SRC_COPY);
        context_.fill_all(BLRgba32(ToByte(r), ToByte(g), ToByte(b), ToByte(a)));
        context_.set_comp_op(BL_COMP_OP_SRC_OVER);
    }

    bool Blend2DSurface::ReadPixelsRgba(int x, int y, int w, int h, std::uint8_t* destination) const
    {
        if (x < 0 || y < 0 || w <= 0 || h <= 0 || x + w > width_ || y + h > height_)
            return false;

        BLImageData data{};
        if (image_.get_data(&data) != BL_SUCCESS)
            return false;

        const auto* base = static_cast<const std::uint8_t*>(data.pixel_data);
        for (int row = 0; row < h; ++row)
        {
            const std::uint8_t* srcRow = base + static_cast<std::ptrdiff_t>(y + row) * data.stride
                + static_cast<std::ptrdiff_t>(x) * 4;
            std::uint8_t* dstRow = destination + static_cast<std::ptrdiff_t>(row) * w * 4;
            ConvertPremultipliedBgraRowToStraightRgba(srcRow, dstRow, w);
        }
        return true;
    }
} // namespace CNA::Internal::Renderers::Blend2D
