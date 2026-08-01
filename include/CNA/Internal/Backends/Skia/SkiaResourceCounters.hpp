#pragma once

#include <cstddef>

namespace CNA::Internal::Backends::Skia
{
    /**
     * Debug-visible accounting for resources owned by the raster backend.
     *
     * These are live backend-owned objects, not Skia allocator totals: a public Texture2D owns two
     * alpha-labelled SkImages, a public RenderTarget2D owns one raster surface, and each target is
     * allowed at most one cached immutable sampling snapshot. That bounded cache is released on
     * the next target write and with its target wrapper.
     */
    struct SkiaResourceStats final
    {
        std::size_t textureBackends = 0;
        std::size_t textureImageViews = 0;
        std::size_t renderTargets = 0;
        std::size_t targetSnapshots = 0;
        std::size_t textureImageBytes = 0;
        std::size_t targetSurfaceBytes = 0;
        std::size_t targetSnapshotBytes = 0;
    };

    /** Single-threaded resource counter shared by one graphics backend and its resource wrappers. */
    class SkiaResourceCounters final
    {
    public:
        void AddTexture(int width, int height) noexcept
        {
            ++stats_.textureBackends;
            stats_.textureImageViews += 2;
            stats_.textureImageBytes += RgbaBytes(width, height) * 2;
        }

        void RemoveTexture(int width, int height) noexcept
        {
            --stats_.textureBackends;
            stats_.textureImageViews -= 2;
            stats_.textureImageBytes -= RgbaBytes(width, height) * 2;
        }

        void AddRenderTarget(int width, int height) noexcept
        {
            ++stats_.renderTargets;
            stats_.targetSurfaceBytes += RgbaBytes(width, height);
        }

        void RemoveRenderTarget(int width, int height) noexcept
        {
            --stats_.renderTargets;
            stats_.targetSurfaceBytes -= RgbaBytes(width, height);
        }

        void AddTargetSnapshot(int width, int height) noexcept
        {
            ++stats_.targetSnapshots;
            stats_.targetSnapshotBytes += RgbaBytes(width, height);
        }

        void RemoveTargetSnapshot(int width, int height) noexcept
        {
            --stats_.targetSnapshots;
            stats_.targetSnapshotBytes -= RgbaBytes(width, height);
        }

        [[nodiscard]] SkiaResourceStats GetStats() const noexcept { return stats_; }

    private:
        [[nodiscard]] static std::size_t RgbaBytes(int width, int height) noexcept
        {
            return width > 0 && height > 0
                ? static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4u
                : 0u;
        }

        SkiaResourceStats stats_;
    };
} // namespace CNA::Internal::Backends::Skia
