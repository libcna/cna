#pragma once

#include "CNA/Internal/Backends/Skia/SkiaImageSource.hpp"

#include "include/core/SkBlender.h"

#include <array>
#include <cstddef>
#include <string>

namespace CNA::Internal::Backends::Skia
{
    inline constexpr int kSkiaBlendFactorCount = 13;
    inline constexpr int kSkiaBlendFunctionCount = 5;

    /** Raw XNA enum ordinals for one independent RGB/alpha blend operation. */
    struct SkiaGeneratedBlendSelectors final
    {
        int colorSourceFactor = 0;
        int alphaSourceFactor = 0;
        int colorDestinationFactor = 1;
        int alphaDestinationFactor = 1;
        int colorFunction = 0;
        int alphaFunction = 0;
    };

    /** Generated routes preserve the shader/source vector and apply every factor explicitly. */
    inline constexpr SkiaSourceAlphaConvention kSkiaGeneratedBlendSourceAlphaConvention
        = SkiaSourceAlphaConvention::Premultiplied;

    /**
     * Creates a blender from the one cached generic SkSL program. Returns null and an actionable
     * deterministic error for invalid selectors/constants or a construction failure.
     */
    [[nodiscard]] sk_sp<SkBlender> TryMakeSkiaGeneratedBlender(
        const SkiaGeneratedBlendSelectors& selectors,
        const std::array<float, 4>& blendFactor,
        std::string& error);

    /** Number of generic SkSL compilation attempts in this process (zero or one). */
    [[nodiscard]] std::size_t SkiaGeneratedBlenderCompileCountEXT() noexcept;
} // namespace CNA::Internal::Backends::Skia
