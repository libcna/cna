#include "CNA/Internal/Renderers/Skia/SkiaSurface.hpp"
#include "CNA/Internal/Renderers/Skia/SkiaResourcePolicy.hpp"

#include "System/NotSupportedException.hpp"

#include "include/core/SkCanvas.h"
#include "include/core/SkColor.h"
#include "include/core/SkColorSpace.h"
#include "include/core/SkImageInfo.h"
#include "include/core/SkPixmap.h"
#include "include/core/SkSurface.h"

#include <algorithm>
#include <atomic>
#include <stdexcept>
#include <utility>

namespace CNA::Internal::Renderers::Skia
{
    namespace
    {
        [[nodiscard]] std::uint8_t ToByte(float value)
        {
            return static_cast<std::uint8_t>(std::clamp(value, 0.0f, 1.0f) * 255.0f + 0.5f);
        }

        [[nodiscard]] std::uint64_t NextSurfaceIdentity()
        {
            static std::atomic<std::uint64_t> nextIdentity{1};
            const std::uint64_t identity = nextIdentity.fetch_add(1, std::memory_order_relaxed);
            if (identity == 0)
                throw std::runtime_error("Skia raster surface identity space is exhausted.");
            return identity;
        }

        /// The read/write request always asks for the caller-facing straight-alpha convention
        /// (matching the default RGBA8 path's existing hardcoded kUnpremul_SkAlphaType exactly);
        /// an opaque native format has no premul/unpremul distinction to make.
        [[nodiscard]] SkAlphaType RequestedAlphaType(SkAlphaType nativeAlphaType) noexcept
        {
            return nativeAlphaType == kOpaque_SkAlphaType ? kOpaque_SkAlphaType
                                                           : kUnpremul_SkAlphaType;
        }
    }

    SkiaSurface::SkiaSurface()
        : identity_(NextSurfaceIdentity())
    {
    }

    SkiaSurface::SkiaSurface(int width, int height)
        : SkiaSurface()
    {
        Resize(width, height);
    }

    SkiaSurface::SkiaSurface(int width, int height, SkColorType colorType, SkAlphaType alphaType)
        : identity_(NextSurfaceIdentity())
        , colorType_(colorType)
        , alphaType_(alphaType)
        , bytesPerPixel_(static_cast<std::size_t>(SkColorTypeBytesPerPixel(colorType)))
    {
        Resize(width, height);
    }

    SkImageInfo SkiaSurface::NativeImageInfoEXT(int width, int height, SkAlphaType alphaType) const
    {
        // kSRGBA_8888 performs the sRGB transfer decode while gathering texels; tagging every
        // request (creation and read/write alike) with the same linear-sRGB working colour space
        // keeps every request an identity conversion in colour-space terms -- only ToByte-style
        // alpha-type premul/unpremul conversion happens, never an accidental second sRGB decode.
        if (colorType_ == kSRGBA_8888_SkColorType)
            return SkImageInfo::Make(width, height, colorType_, alphaType,
                                     SkColorSpace::MakeSRGBLinear());
        return SkImageInfo::Make(width, height, colorType_, alphaType);
    }

    void SkiaSurface::Resize(int width, int height)
    {
        if (width <= 0 || height <= 0)
            throw std::runtime_error("Skia raster surface dimensions must be positive.");
        std::size_t bytes = 0;
        if (!IsValidSkiaResourceAxis(width) || !IsValidSkiaResourceAxis(height)
            || !CheckedTexelBytes2D(static_cast<std::size_t>(width),
                                    static_cast<std::size_t>(height), bytesPerPixel_, bytes)
            || bytes > kSkiaCpuTextureStorageLimitBytes)
        {
            throw System::NotSupportedException(
                "Skia raster surface exceeds the checked 16384-axis/256-MiB resource policy.");
        }

        auto nextSurface = SkSurfaces::Raster(NativeImageInfoEXT(width, height, alphaType_));
        if (!nextSurface)
            throw std::runtime_error("Skia failed to allocate a raster surface.");

        surface_ = std::move(nextSurface);
        width_ = width;
        height_ = height;
    }

    void SkiaSurface::Clear(float r, float g, float b, float a)
    {
        if (!surface_)
            throw std::runtime_error("Skia raster surface must be allocated before Clear().");
        surface_->getCanvas()->clear(SkColorSetARGB(ToByte(a), ToByte(r), ToByte(g), ToByte(b)));
    }

    void SkiaSurface::Flush()
    {
        // Raster Skia writes synchronously into CPU memory. Unlike a Ganesh/Graphite surface,
        // there is no deferred command queue to submit before readback or platform presentation.
    }

    SkCanvas* SkiaSurface::Canvas() const noexcept
    {
        return surface_ ? surface_->getCanvas() : nullptr;
    }

    bool SkiaSurface::ReadPixels(int x, int y, int width, int height,
                                 std::uint8_t* destination, int destinationRowBytes) const
    {
        const int minRowBytes = width * static_cast<int>(bytesPerPixel_);
        if (!surface_ || destination == nullptr || width < 0 || height < 0 || x < 0 || y < 0
            || x > width_ - width || y > height_ - height || destinationRowBytes < minRowBytes)
            return false;

        return surface_->readPixels(
            NativeImageInfoEXT(width, height, RequestedAlphaType(alphaType_)), destination,
            static_cast<std::size_t>(destinationRowBytes), x, y);
    }

    bool SkiaSurface::WritePixels(int x, int y, int width, int height,
                                  const std::uint8_t* source, int sourceRowBytes)
    {
        const int minRowBytes = width * static_cast<int>(bytesPerPixel_);
        if (!surface_ || source == nullptr || width < 0 || height < 0 || x < 0 || y < 0
            || x > width_ - width || y > height_ - height || sourceRowBytes < minRowBytes)
        {
            return false;
        }

        const SkPixmap pixmap(NativeImageInfoEXT(width, height, RequestedAlphaType(alphaType_)),
                              source, static_cast<std::size_t>(sourceRowBytes));
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
} // namespace CNA::Internal::Renderers::Skia
