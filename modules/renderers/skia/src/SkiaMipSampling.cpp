#include "CNA/Internal/Renderers/Skia/SkiaMipSampling.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace CNA::Internal::Renderers::Skia
{
    SkiaMipFilter DecodeSkiaMipFilter(int textureFilter)
    {
        using Within = SkiaWithinLevelFilter;
        using Mip = SkiaMipLevelFilter;
        switch (textureFilter)
        {
            case 0: return {Within::Linear, Within::Linear, Mip::Linear};
            case 1: return {Within::Point, Within::Point, Mip::Point};
            // The selected CPU-raster renderer has no anisotropic footprint. Its documented exact
            // fallback is the complete Linear filter, including Linear's mip component.
            case 2: return {Within::Linear, Within::Linear, Mip::Linear};
            case 3: return {Within::Linear, Within::Linear, Mip::Point};
            case 4: return {Within::Point, Within::Point, Mip::Linear};
            case 5: return {Within::Linear, Within::Point, Mip::Linear};
            case 6: return {Within::Linear, Within::Point, Mip::Point};
            case 7: return {Within::Point, Within::Linear, Mip::Linear};
            case 8: return {Within::Point, Within::Linear, Mip::Point};
            default:
                throw std::invalid_argument(
                    "Skia SpriteBatch received an invalid TextureFilter ordinal.");
        }
    }

    float SkiaTexelRate(const SkMatrix& localToDevice) noexcept
    {
        const float m00 = localToDevice.getScaleX();
        const float m01 = localToDevice.getSkewX();
        const float m10 = localToDevice.getSkewY();
        const float m11 = localToDevice.getScaleY();
        const float determinant = m00 * m11 - m01 * m10;
        if (!std::isfinite(determinant) || !(std::abs(determinant) > 1.0e-12f))
            return 1.0f;

        const float inverse00 = m11 / determinant;
        const float inverse01 = -m01 / determinant;
        const float inverse10 = -m10 / determinant;
        const float inverse11 = m00 / determinant;
        const float rateX = std::hypot(inverse00, inverse10);
        const float rateY = std::hypot(inverse01, inverse11);
        const float rate = std::max(rateX, rateY);
        return std::isfinite(rate) && rate > 1.0f ? rate : 1.0f;
    }

    SkiaMipSelection SelectSkiaMipLevels(
        const SkiaMipFilter& filter, int levelCount, float texelRate)
    {
        if (levelCount <= 0)
            throw std::invalid_argument("Skia mip sampling requires at least one level.");

        SkiaMipSelection result;
        result.magnifying = !(std::isfinite(texelRate) && texelRate > 1.0f);
        result.withinLevel = result.magnifying
            ? filter.magnification
            : filter.minification;
        if (levelCount == 1 || result.magnifying)
            return result;

        const float maxLevel = static_cast<float>(levelCount - 1);
        const float lambda = std::min(std::log2(texelRate), maxLevel);
        if (filter.mip == SkiaMipLevelFilter::Point)
        {
            // GL/FNA's nearest-mip rule resolves an exact half-level toward the lower level.
            int level = static_cast<int>(std::ceil(lambda + 0.5f)) - 1;
            result.lowerLevel = std::clamp(level, 0, levelCount - 1);
            result.upperLevel = result.lowerLevel;
            return result;
        }

        result.lowerLevel = std::clamp(
            static_cast<int>(std::floor(lambda)), 0, levelCount - 1);
        result.upperWeight = lambda - static_cast<float>(result.lowerLevel);
        if (!(result.upperWeight > 0.0f) || result.lowerLevel == levelCount - 1)
        {
            result.upperLevel = result.lowerLevel;
            result.upperWeight = 0.0f;
            return result;
        }
        result.upperLevel = result.lowerLevel + 1;
        return result;
    }
} // namespace CNA::Internal::Renderers::Skia
