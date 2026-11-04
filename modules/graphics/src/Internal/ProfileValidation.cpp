// SPDX-License-Identifier: MS-PL
#include "ProfileValidation.hpp"
#include <array>

namespace CNA::Internal::Graphics
{
    std::string ProfileDiagnostic(
        Microsoft::Xna::Framework::Graphics::GraphicsProfile profile, std::string_view detail)
    {
        using Microsoft::Xna::Framework::Graphics::GraphicsProfile;
        return std::string("XNA Framework ") + (profile == GraphicsProfile::Reach ? "Reach" : "HiDef")
             + " profile " + std::string(detail);
    }

    std::string ProfileFormatName(Microsoft::Xna::Framework::Graphics::SurfaceFormat format)
    {
        static constexpr std::array<std::string_view, 20> names{
            "Color", "Bgr565", "Bgra5551", "Bgra4444", "Dxt1", "Dxt3", "Dxt5",
            "NormalizedByte2", "NormalizedByte4", "Rgba1010102", "Rg32", "Rgba64", "Alpha8",
            "Single", "Vector2", "Vector4", "HalfSingle", "HalfVector2", "HalfVector4", "HdrBlendable"};
        const auto ordinal = static_cast<unsigned int>(format);
        return ordinal < names.size() ? std::string(names[ordinal])
                                     : std::to_string(static_cast<int>(format));
    }
}
