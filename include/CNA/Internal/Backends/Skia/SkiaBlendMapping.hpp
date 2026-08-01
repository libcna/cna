#pragma once

#include "CNA/Internal/Backends/Skia/SkiaImageSource.hpp"

#include "include/core/SkBlendMode.h"

#include <string>

namespace CNA::Internal::Backends::Skia
{
    enum class SkiaBlendMappingRoute
    {
        DirectBlendMode,
        RuntimeDestinationColorPrototype,
    };

    /** An accepted Skia mapping whose input-alpha convention has explicit pixel evidence. */
    struct SkiaBlendMapping
    {
        int colorSourceBlend;
        int alphaSourceBlend;
        int colorDestinationBlend;
        int alphaDestinationBlend;
        SkiaBlendMappingRoute route;
        SkBlendMode mode;
        SkiaSourceAlphaConvention sourceAlphaConvention;
        const char* name;
    };

    // XNA Blend ordinals: One=0, Zero=1, SourceAlpha=4, InverseSourceAlpha=5,
    // DestinationColor=6. A general BlendState does not identify whether texture bytes are
    // straight or premultiplied, so this table contains only tuples with a pixel-proven convention.
    // SKIA-54's custom entry is intentionally a single opaque-source runtime-blender probe, not a
    // license to infer a convention for other arbitrary factor combinations.
    inline constexpr SkiaBlendMapping kSkiaBlendMappings[] = {
        {0, 0, 1, 1, SkiaBlendMappingRoute::DirectBlendMode,
         SkBlendMode::kSrc,     SkiaSourceAlphaConvention::Premultiplied, "Opaque"},
        {0, 0, 5, 5, SkiaBlendMappingRoute::DirectBlendMode,
         SkBlendMode::kSrcOver, SkiaSourceAlphaConvention::Premultiplied, "AlphaBlend"},
        {4, 4, 5, 5, SkiaBlendMappingRoute::DirectBlendMode,
         SkBlendMode::kSrcOver, SkiaSourceAlphaConvention::Straight,       "NonPremultiplied"},
        {4, 4, 0, 0, SkiaBlendMappingRoute::DirectBlendMode,
         SkBlendMode::kPlus,    SkiaSourceAlphaConvention::Straight,       "Additive"},
        {6, 0, 1, 1, SkiaBlendMappingRoute::RuntimeDestinationColorPrototype,
         SkBlendMode::kSrcOver, SkiaSourceAlphaConvention::Premultiplied, "DestinationColorPrototype"},
    };

    [[nodiscard]] inline const SkiaBlendMapping* FindSkiaBlendMapping(
        int colorSourceBlend, int alphaSourceBlend,
        int colorDestinationBlend, int alphaDestinationBlend,
        int colorBlendFunction, int alphaBlendFunction) noexcept
    {
        // BlendFunction::Add is the only equation with public pixel evidence so far. Keeping this
        // explicit prevents a subtract/min/max request from silently using SourceOver merely
        // because its factors happened to match a mapped tuple.
        if (colorBlendFunction != 0 || alphaBlendFunction != 0)
            return nullptr;

        for (const auto& mapping : kSkiaBlendMappings)
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

    [[nodiscard]] inline const SkiaBlendMapping* FindSkiaDirectBlendMapping(
        int colorSourceBlend, int alphaSourceBlend,
        int colorDestinationBlend, int alphaDestinationBlend,
        int colorBlendFunction, int alphaBlendFunction) noexcept
    {
        const SkiaBlendMapping* mapping = FindSkiaBlendMapping(
            colorSourceBlend, alphaSourceBlend, colorDestinationBlend, alphaDestinationBlend,
            colorBlendFunction, alphaBlendFunction);
        return mapping && mapping->route == SkiaBlendMappingRoute::DirectBlendMode ? mapping : nullptr;
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
        return std::string("Skia raster backend has no tested direct or runtime-blender mapping for ")
            + "this BlendState; received color(source="
            + SkiaBlendFactorName(colorSourceBlend) + ", destination="
            + SkiaBlendFactorName(colorDestinationBlend) + ", function="
            + SkiaBlendFunctionName(colorBlendFunction) + "), alpha(source="
            + SkiaBlendFactorName(alphaSourceBlend) + ", destination="
            + SkiaBlendFactorName(alphaDestinationBlend) + ", function="
            + SkiaBlendFunctionName(alphaBlendFunction) + ").";
    }
} // namespace CNA::Internal::Backends::Skia
