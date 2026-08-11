#include "CNA/Internal/Renderers/Blend2D/Blend2DSurface.hpp"
#include "CNA/Internal/Renderers/Blend2D/Blend2DCheckedCallEXT.hpp"
#include "CNA/Internal/Renderers/Blend2D/Blend2DPixelConvert.hpp"

#include <algorithm>
#include <cstdint>
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
        Blend2DCheckEXT(image_.create(width_, height_, BL_FORMAT_PRGB32), "Blend2DSurface: BLImage::create");
        Blend2DCheckEXT(context_.begin(image_), "Blend2DSurface: BLContext::begin");
        Clear(0.0f, 0.0f, 0.0f, 0.0f);
    }

    Blend2DSurface::~Blend2DSurface()
    {
        // BLContext::end() intentionally not checked: this destructor must not throw, and by the
        // time a surface is being destroyed there is no further use of the context to protect.
        (void)context_.end();
    }

    void Blend2DSurface::Resize(int width, int height)
    {
        const int newWidth = width > 0 ? width : 1;
        const int newHeight = height > 0 ? height : 1;
        Blend2DCheckEXT(context_.end(), "Blend2DSurface::Resize: BLContext::end");
        // Only commit the new dimensions after end() succeeds, so a failed resize leaves width_/
        // height_ matching the still-live image_ rather than describing a surface that was never
        // actually reallocated.
        width_ = newWidth;
        height_ = newHeight;
        Blend2DCheckEXT(image_.create(width_, height_, BL_FORMAT_PRGB32), "Blend2DSurface::Resize: BLImage::create");
        Blend2DCheckEXT(context_.begin(image_), "Blend2DSurface::Resize: BLContext::begin");
        Clear(0.0f, 0.0f, 0.0f, 0.0f);
    }

    void Blend2DSurface::Clear(float r, float g, float b, float a)
    {
        Blend2DCheckEXT(context_.set_comp_op(BL_COMP_OP_SRC_COPY), "Blend2DSurface::Clear: BLContext::set_comp_op");
        Blend2DCheckEXT(context_.fill_all(BLRgba32(ToByte(r), ToByte(g), ToByte(b), ToByte(a))),
                        "Blend2DSurface::Clear: BLContext::fill_all");
        Blend2DCheckEXT(context_.set_comp_op(BL_COMP_OP_SRC_OVER), "Blend2DSurface::Clear: BLContext::set_comp_op");
    }

    bool Blend2DSurface::ReadPixelsRgba(int x, int y, int w, int h, std::uint8_t* destination) const
    {
        // 64-bit bounds arithmetic: x/y/w/h are untrusted public dimensions (GraphicsDevice::
        // GetBackBufferData/Texture2D::GetData), so x + w must not overflow int32 before the
        // comparison runs.
        if (x < 0 || y < 0 || w <= 0 || h <= 0 ||
            static_cast<std::int64_t>(x) + w > width_ || static_cast<std::int64_t>(y) + h > height_)
        {
            return false;
        }

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
