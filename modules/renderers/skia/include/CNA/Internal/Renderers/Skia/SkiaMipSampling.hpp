#pragma once

#include "include/core/SkMatrix.h"

namespace CNA::Internal::Renderers::Skia
{
    enum class SkiaWithinLevelFilter
    {
        Point,
        Linear,
    };

    enum class SkiaMipLevelFilter
    {
        Point,
        Linear,
    };

    /** Exact minification, magnification, and mip components of one TextureFilter ordinal. */
    struct SkiaMipFilter final
    {
        SkiaWithinLevelFilter minification = SkiaWithinLevelFilter::Linear;
        SkiaWithinLevelFilter magnification = SkiaWithinLevelFilter::Linear;
        SkiaMipLevelFilter mip = SkiaMipLevelFilter::Linear;
    };

    /** One bounded mip decision for an affine SpriteBatch draw. */
    struct SkiaMipSelection final
    {
        int lowerLevel = 0;
        int upperLevel = 0;
        float upperWeight = 0.0f;
        bool magnifying = true;
        SkiaWithinLevelFilter withinLevel = SkiaWithinLevelFilter::Linear;
    };

    /** Decomposes all nine public TextureFilter ordinals; rejects every other raw value. */
    [[nodiscard]] SkiaMipFilter DecodeSkiaMipFilter(int textureFilter);

    /**
     * Returns texels per device pixel from the affine local-source-pixel to device transform.
     * Degenerate and non-finite matrices conservatively return 1 (level zero).
     */
    [[nodiscard]] float SkiaTexelRate(const SkMatrix& localToDevice) noexcept;

    /** Selects and, for mip-linear filters, brackets the complete allocated chain. */
    [[nodiscard]] SkiaMipSelection SelectSkiaMipLevels(
        const SkiaMipFilter& filter, int levelCount, float texelRate);
} // namespace CNA::Internal::Renderers::Skia
