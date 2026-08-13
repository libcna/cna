#pragma once

#include "CNA/CNAHelper.hpp"

#include <cstddef>
#include <cstdint>
#include <vector>

#include "include/core/SkColorType.h"
#include "include/core/SkImageInfo.h"
#include "include/core/SkSurface.h"

class SkCanvas;

namespace CNA::Internal::Renderers::Skia
{
    /**
     * CPU raster surface used by the first SKIA renderer slice.
     *
     * The surface owns Skia's pixels and defines CNA's canonical top-left RGBA8 readback
     * convention. Presentation is deliberately outside this class: the graphics renderer uploads
     * these pixels to its platform presenter after Flush(), which keeps Skia resource ownership
     * independent from native window ownership.
     */
    class SkiaSurface final
    {
    public:
        SkiaSurface();
        SkiaSurface(int width, int height);
        /**
         * SKIA-142: constructs (or resizes into) a surface whose native pixel storage is
         * @p colorType/@p alphaType rather than the default straight/premultiplied RGBA8 used by
         * the backbuffer and SurfaceFormat::Color render targets. ReadPixels/WritePixels/Resize's
         * resource accounting all operate in this native format's own byte layout; SnapshotRgba()
         * remains RGBA8-only and is never called for a non-default-format surface (backbuffer
         * presentation is always Color).
         */
        SkiaSurface(int width, int height, SkColorType colorType, SkAlphaType alphaType);
        SkiaSurface(const SkiaSurface&) = delete;
        SkiaSurface& operator=(const SkiaSurface&) = delete;
        SkiaSurface(SkiaSurface&&) = delete;
        SkiaSurface& operator=(SkiaSurface&&) = delete;

        void Resize(int width, int height);
        void Clear(float r, float g, float b, float a);
        void Flush();

        [[nodiscard]] int Width() const noexcept { return width_; }
        [[nodiscard]] int Height() const noexcept { return height_; }
        /// Process-local identity of this logical target. It remains stable across Resize().
        [[nodiscard]] std::uint64_t Identity() const noexcept { return identity_; }
        [[nodiscard]] SkCanvas* Canvas() const noexcept;
        CNAEXT [[nodiscard]] SkColorType NativeColorTypeEXT() const noexcept { return colorType_; }
        CNAEXT [[nodiscard]] SkAlphaType NativeAlphaTypeEXT() const noexcept { return alphaType_; }
        CNAEXT [[nodiscard]] std::size_t NativeBytesPerPixelEXT() const noexcept
        {
            return bytesPerPixel_;
        }

        /// Reads a complete rectangular region into tightly packed, top-row-first bytes in this
        /// surface's own native pixel format (RGBA8 unless constructed with an explicit format).
        /// Returns false when the requested rectangle cannot be read; never partially writes.
        [[nodiscard]] bool ReadPixels(int x, int y, int width, int height,
                                      std::uint8_t* destination, int destinationRowBytes) const;
        /// Replaces a complete rectangular region from tightly packed, top-row-first bytes in
        /// this surface's own native pixel format (straight RGBA8 unless constructed otherwise).
        [[nodiscard]] bool WritePixels(int x, int y, int width, int height,
                                       const std::uint8_t* source, int sourceRowBytes);

        /// Returns an immutable Skia snapshot suitable for sampling by a later canvas draw.
        [[nodiscard]] sk_sp<SkImage> SnapshotImage() const;

        /// Produces a complete, tightly packed RGBA8 image for platform presentation. Only valid for
        /// the default-format (Color) surface used by the backbuffer.
        [[nodiscard]] std::vector<std::uint8_t> SnapshotRgba() const;

    private:
        /// The SkImageInfo used both to create the surface and to request reads/writes so no
        /// unwanted colour-space conversion occurs; @p alphaType lets read/write ask for the
        /// straight-alpha convention while creation keeps the native (possibly premultiplied)
        /// storage convention.
        [[nodiscard]] SkImageInfo NativeImageInfoEXT(int width, int height,
                                                      SkAlphaType alphaType) const;

        const std::uint64_t identity_;
        sk_sp<SkSurface> surface_;
        int width_ = 0;
        int height_ = 0;
        SkColorType colorType_ = kRGBA_8888_SkColorType;
        SkAlphaType alphaType_ = kPremul_SkAlphaType;
        std::size_t bytesPerPixel_ = 4u;
    };
} // namespace CNA::Internal::Renderers::Skia
