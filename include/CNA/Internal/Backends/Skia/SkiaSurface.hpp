#pragma once

#include <cstdint>
#include <vector>

#include "include/core/SkSurface.h"

class SkCanvas;

namespace CNA::Internal::Backends::Skia
{
    /**
     * CPU raster surface used by the first SKIA backend slice.
     *
     * The surface owns Skia's pixels and defines CNA's canonical top-left RGBA8 readback
     * convention. Presentation is deliberately outside this class: the graphics backend uploads
     * these pixels to its SDL presentation texture after Flush(), which keeps Skia resource
     * ownership independent from SDL window ownership.
     */
    class SkiaSurface final
    {
    public:
        SkiaSurface();
        SkiaSurface(int width, int height);
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

        /// Reads a complete rectangular region into tightly packed, top-row-first RGBA8 bytes.
        /// Returns false when the requested rectangle cannot be read; never partially writes.
        [[nodiscard]] bool ReadPixels(int x, int y, int width, int height,
                                      std::uint8_t* destination, int destinationRowBytes) const;
        /// Replaces a complete rectangular region from tightly packed, top-row-first straight RGBA8 bytes.
        [[nodiscard]] bool WritePixels(int x, int y, int width, int height,
                                       const std::uint8_t* source, int sourceRowBytes);

        /// Returns an immutable Skia snapshot suitable for sampling by a later canvas draw.
        [[nodiscard]] sk_sp<SkImage> SnapshotImage() const;

        /// Produces a complete, tightly packed RGBA8 image for SDL presentation.
        [[nodiscard]] std::vector<std::uint8_t> SnapshotRgba() const;

    private:
        const std::uint64_t identity_;
        sk_sp<SkSurface> surface_;
        int width_ = 0;
        int height_ = 0;
    };
} // namespace CNA::Internal::Backends::Skia
