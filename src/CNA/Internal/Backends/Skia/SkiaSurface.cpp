#include "CNA/Internal/Backends/Skia/SkiaSurface.hpp"

#include "include/core/SkCanvas.h"
#include "include/core/SkColor.h"
#include "include/core/SkImageInfo.h"
#include "include/core/SkPixmap.h"
#include "include/core/SkSurface.h"

#include <algorithm>
#include <stdexcept>
#include <utility>

namespace CNA::Internal::Backends::Skia
{
    namespace
    {
        [[nodiscard]] std::uint8_t ToByte(float value)
        {
            return static_cast<std::uint8_t>(std::clamp(value, 0.0f, 1.0f) * 255.0f + 0.5f);
        }

        [[nodiscard]] SkImageInfo RgbaPremulInfo(int width, int height)
        {
            return SkImageInfo::Make(width, height, kRGBA_8888_SkColorType, kPremul_SkAlphaType);
        }

        [[nodiscard]] SkImageInfo RgbaUnpremulInfo(int width, int height)
        {
            return SkImageInfo::Make(width, height, kRGBA_8888_SkColorType, kUnpremul_SkAlphaType);
        }
    }

    SkiaSurface::SkiaSurface(int width, int height)
    {
        Resize(width, height);
    }

    void SkiaSurface::Resize(int width, int height)
    {
        if (width <= 0 || height <= 0)
            throw std::runtime_error("Skia raster surface dimensions must be positive.");

        auto nextSurface = SkSurfaces::Raster(RgbaPremulInfo(width, height));
        if (!nextSurface)
            throw std::runtime_error("Skia failed to allocate a raster surface.");

        surface_ = std::move(nextSurface);
        width_ = width;
        height_ = height;
    }

    void SkiaSurface::Clear(float r, float g, float b, float a)
    {
        surface_->getCanvas()->clear(SkColorSetARGB(ToByte(a), ToByte(r), ToByte(g), ToByte(b)));
    }

    void SkiaSurface::Flush()
    {
        // Raster Skia writes synchronously into CPU memory. Unlike a Ganesh/Graphite surface,
        // there is no deferred command queue to submit before readback or SDL upload.
    }

    SkCanvas* SkiaSurface::Canvas() const noexcept
    {
        return surface_ ? surface_->getCanvas() : nullptr;
    }

    bool SkiaSurface::ReadPixels(int x, int y, int width, int height,
                                 std::uint8_t* destination, int destinationRowBytes) const
    {
        if (!surface_ || destination == nullptr || width < 0 || height < 0 || x < 0 || y < 0
            || x > width_ - width || y > height_ - height || destinationRowBytes < width * 4)
            return false;

        return surface_->readPixels(RgbaUnpremulInfo(width, height), destination,
                                    static_cast<std::size_t>(destinationRowBytes), x, y);
    }

    bool SkiaSurface::WritePixels(int x, int y, int width, int height,
                                  const std::uint8_t* source, int sourceRowBytes)
    {
        if (!surface_ || source == nullptr || width < 0 || height < 0 || x < 0 || y < 0
            || x > width_ - width || y > height_ - height || sourceRowBytes < width * 4)
        {
            return false;
        }

        const SkPixmap pixmap(RgbaUnpremulInfo(width, height), source,
                              static_cast<std::size_t>(sourceRowBytes));
        surface_->writePixels(pixmap, x, y);
        return true;
    }

    sk_sp<SkImage> SkiaSurface::SnapshotImage() const
    {
        return surface_ ? surface_->makeImageSnapshot() : nullptr;
    }

    std::vector<std::uint8_t> SkiaSurface::SnapshotRgba() const
    {
        std::vector<std::uint8_t> pixels(static_cast<std::size_t>(width_) * height_ * 4u);
        if (!ReadPixels(0, 0, width_, height_, pixels.data(), width_ * 4))
            throw std::runtime_error("Skia failed to read its raster backbuffer.");
        return pixels;
    }
} // namespace CNA::Internal::Backends::Skia
