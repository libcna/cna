#pragma once

#include <stdexcept>
#include <string>
#include <string_view>

namespace CNA::Internal::Backends::Skia
{
    /** Stable diagnostic shared by every rejected Skia 3D entry point. */
    inline constexpr std::string_view kSkiaUnsupported3DPrefix =
        "Skia (raster 2D) does not support 3D: ";

    [[noreturn]] inline void ThrowSkiaUnsupported3D(std::string_view operation)
    {
        throw std::runtime_error(std::string(kSkiaUnsupported3DPrefix) + std::string(operation));
    }
} // namespace CNA::Internal::Backends::Skia
