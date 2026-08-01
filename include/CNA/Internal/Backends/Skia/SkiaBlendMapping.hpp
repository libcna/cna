#pragma once

#include "CNA/Internal/Backends/Skia/SkiaImageSource.hpp"

#include "include/core/SkBlendMode.h"

#include <string>

namespace CNA::Internal::Backends::Skia
{
    /** A direct Skia mapping whose input-alpha convention is fully determined by a CNA preset. */
    struct SkiaBlendMapping
    {
        int colorSourceBlend;
        int alphaSourceBlend;
        int colorDestinationBlend;
        int alphaDestinationBlend;
        SkBlendMode mode;
        SkiaSourceAlphaConvention sourceAlphaConvention;
        const char* name;
    };

    // XNA Blend ordinals: One=0, Zero=1, SourceAlpha=4, InverseSourceAlpha=5.  A general XNA
    // BlendState does not identify whether texture bytes are straight or premultiplied; these four
    // preset tuples are the combinations for which that choice is an established public contract.
    inline constexpr SkiaBlendMapping kSkiaDirectBlendMappings[] = {
        {0, 0, 1, 1, SkBlendMode::kSrc,     SkiaSourceAlphaConvention::Premultiplied, "Opaque"},
        {0, 0, 5, 5, SkBlendMode::kSrcOver, SkiaSourceAlphaConvention::Premultiplied, "AlphaBlend"},
        {4, 4, 5, 5, SkBlendMode::kSrcOver, SkiaSourceAlphaConvention::Straight,       "NonPremultiplied"},
        {4, 4, 0, 0, SkBlendMode::kPlus,    SkiaSourceAlphaConvention::Straight,       "Additive"},
    };

    [[nodiscard]] inline const SkiaBlendMapping* FindSkiaDirectBlendMapping(
        int colorSourceBlend, int alphaSourceBlend,
        int colorDestinationBlend, int alphaDestinationBlend,
        int colorBlendFunction, int alphaBlendFunction) noexcept
    {
        // BlendFunction::Add is the only equation covered by the current direct table.  Keeping
        // this explicit prevents a subtract/min/max request from being silently rendered as a
        // source-over SkPaint merely because its factors happened to match a preset.
        if (colorBlendFunction != 0 || alphaBlendFunction != 0)
            return nullptr;

        for (const auto& mapping : kSkiaDirectBlendMappings)
        {
            if (mapping.colorSourceBlend == colorSourceBlend
                && mapping.alphaSourceBlend == alphaSourceBlend
                && mapping.colorDestinationBlend == colorDestinationBlend
                && mapping.alphaDestinationBlend == alphaDestinationBlend)
            {
                return &mapping;
            }
        }
        return nullptr;
    }

    [[nodiscard]] inline const char* SkiaBlendFactorName(int factor) noexcept
    {
        switch (factor)
        {
            case 0: return "One";
            case 1: return "Zero";
            case 2: return "SourceColor";
            case 3: return "InverseSourceColor";
            case 4: return "SourceAlpha";
            case 5: return "InverseSourceAlpha";
            case 6: return "DestinationColor";
            case 7: return "InverseDestinationColor";
            case 8: return "DestinationAlpha";
            case 9: return "InverseDestinationAlpha";
            case 10: return "BlendFactor";
            case 11: return "InverseBlendFactor";
            case 12: return "SourceAlphaSaturation";
            default: return "<invalid Blend>";
        }
    }

    [[nodiscard]] inline const char* SkiaBlendFunctionName(int function) noexcept
    {
        switch (function)
        {
            case 0: return "Add";
            case 1: return "Subtract";
            case 2: return "ReverseSubtract";
            case 3: return "Max";
            case 4: return "Min";
            default: return "<invalid BlendFunction>";
        }
    }

    [[nodiscard]] inline std::string DescribeUnsupportedSkiaBlendState(
        int colorSourceBlend, int alphaSourceBlend,
        int colorDestinationBlend, int alphaDestinationBlend,
        int colorBlendFunction, int alphaBlendFunction)
    {
        return std::string("Skia raster backend supports only the direct Opaque, AlphaBlend, NonPremultiplied, ")
            + "and Additive BlendState mappings; received color(source="
            + SkiaBlendFactorName(colorSourceBlend) + ", destination="
            + SkiaBlendFactorName(colorDestinationBlend) + ", function="
            + SkiaBlendFunctionName(colorBlendFunction) + "), alpha(source="
            + SkiaBlendFactorName(alphaSourceBlend) + ", destination="
            + SkiaBlendFactorName(alphaDestinationBlend) + ", function="
            + SkiaBlendFunctionName(alphaBlendFunction) + ").";
    }
} // namespace CNA::Internal::Backends::Skia
