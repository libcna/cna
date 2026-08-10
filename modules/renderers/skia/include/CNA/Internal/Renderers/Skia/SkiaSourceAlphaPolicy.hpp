#pragma once

#include "CNA/Internal/Renderers/Skia/SkiaImageSource.hpp"
#include "Microsoft/Xna/Framework/Color.hpp"

namespace CNA::Internal::Renderers::Skia
{
    struct SkiaTintScale final
    {
        float red;
        float green;
        float blue;
        float alpha;
    };

    /**
     * Produces the scale applied in Skia's premultiplied working space. Straight-labelled texture
     * RGB is already multiplied by source alpha during image evaluation, so tint alpha is folded
     * into RGB exactly once. Premultiplied-labelled source components remain independent.
     */
    [[nodiscard]] inline SkiaTintScale MakeSkiaTintScale(
        const Microsoft::Xna::Framework::Color& color,
        SkiaSourceAlphaConvention alphaConvention) noexcept
    {
        const float alpha = static_cast<float>(color.getAProperty()) / 255.0f;
        const float straightAlphaScale = alphaConvention == SkiaSourceAlphaConvention::Straight
            ? alpha
            : 1.0f;
        return {
            static_cast<float>(color.getRProperty()) / 255.0f * straightAlphaScale,
            static_cast<float>(color.getGProperty()) / 255.0f * straightAlphaScale,
            static_cast<float>(color.getBProperty()) / 255.0f * straightAlphaScale,
            alpha,
        };
    }
} // namespace CNA::Internal::Renderers::Skia
