#pragma once

#include <string_view>

namespace CNA::Internal::Backends::Skia
{
    /// Official Skia source revision selected by the raster integration and build documentation.
    inline constexpr std::string_view kSkiaPinnedRevision =
        "ebf50520d720a1ce9d842d942d04c6c39c3fbc7b";

    /// Stable, single-line startup capability report for the selected raster execution mode.
    inline constexpr std::string_view kSkiaStartupDiagnostic =
        "CNA: Skia capabilities -- revision=ebf50520d720a1ce9d842d942d04c6c39c3fbc7b; "
        "surface=raster; colour=RGBA_8888/premultiplied; samples=0; "
        "anisotropic filtering=unsupported; "
        "blend=all-valid-13-factor/5-function-tuples; blend-constant=live; "
        "colour-write-mask=target0";

    static_assert(kSkiaStartupDiagnostic.find(kSkiaPinnedRevision) != std::string_view::npos);
    static_assert(kSkiaStartupDiagnostic.find('\n') == std::string_view::npos);
    static_assert(kSkiaStartupDiagnostic.find('\r') == std::string_view::npos);
} // namespace CNA::Internal::Backends::Skia
