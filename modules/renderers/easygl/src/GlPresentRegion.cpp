// SPDX-License-Identifier: MS-PL
#include "CNA/Internal/Renderers/EasyGL/GlPresentRegion.hpp"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <stdexcept>

namespace CNA::Internal::Renderers::EasyGL
{
    std::optional<GlPresentBlitEXT> BuildGlPresentBlitEXT(
        const GpuPresentMappingEXT& mapping, int sourceWidth, int sourceHeight, int targetHeight)
    {
        const auto& s = mapping.source;
        if (sourceWidth <= 0 || sourceHeight <= 0 || targetHeight <= 0 ||
            !std::all_of(s.begin(), s.end(), [](float v) { return std::isfinite(v) && std::floor(v) == v; }) ||
            s[0] < 0 || s[1] < 0 || s[2] <= 0 || s[3] <= 0 ||
            static_cast<double>(s[0]) + s[2] > sourceWidth ||
            static_cast<double>(s[1]) + s[3] > sourceHeight)
            return std::nullopt;
        const auto& d = mapping.destination;
        using I64 = std::int64_t;
        const std::array<I64, 4> destination{
            d.x, static_cast<I64>(targetHeight) - d.y - d.height,
            static_cast<I64>(d.x) + d.width, static_cast<I64>(targetHeight) - d.y};
        if (d.width <= 0 || d.height <= 0 ||
            !std::all_of(destination.begin(), destination.end(), [](I64 v) {
                return v >= std::numeric_limits<int>::min() && v <= std::numeric_limits<int>::max();
            })) return std::nullopt;
        // Refresh Present IL_0014-0077 preserves rectangle endpoints. XNA uses top-left pixels;
        // GL uses bottom-left: [y,y+h) becomes [H-y-h,H-y), with each surface's OWN height.
        GlPresentBlitEXT result;
        result.source = {static_cast<int>(s[0]),
                         sourceHeight - static_cast<int>(static_cast<double>(s[1]) + s[3]),
                         static_cast<int>(static_cast<double>(s[0]) + s[2]),
                         sourceHeight - static_cast<int>(s[1])};
        std::transform(destination.begin(), destination.end(), result.destination.begin(),
                       [](I64 v) { return static_cast<int>(v); });
        return result;
    }

    GlPresentPixelsEXT ClipGlPresentPixelsEXT(
        const CNA::Platform::SurfaceFrame& frame, const GpuPresentMappingEXT& mapping)
    {
        const auto& clip = mapping.clip;
        const auto& d = mapping.destination;
        if (frame.width <= 0 || frame.height <= 0 || frame.pixels == nullptr ||
            frame.width > std::numeric_limits<int>::max() / 4 ||
            frame.strideBytes < 0 || (frame.strideBytes != 0 && frame.strideBytes < frame.width * 4) ||
            d.width <= 0 || d.height <= 0 || clip.width < 0 || clip.height < 0 ||
            static_cast<std::size_t>(clip.width) > std::numeric_limits<std::size_t>::max() / 4 /
                static_cast<std::size_t>(std::max(1, clip.height)))
            throw std::invalid_argument("EasyGL Present has malformed pixel mapping");
        GlPresentPixelsEXT result{clip, std::vector<std::uint8_t>(
            static_cast<std::size_t>(clip.width) * clip.height * 4)};
        const auto stride = frame.strideBytes != 0 ? frame.strideBytes : frame.width * 4;
        for (int y = 0; y < clip.height; ++y)
            for (int x = 0; x < clip.width; ++x)
            {
                // Center sampling retains the ORIGINAL map even when its origin is negative.
                const int sx = static_cast<int>(std::clamp(std::floor(
                    (static_cast<double>(clip.x) + x + 0.5 - d.x) * frame.width / d.width),
                    0.0, static_cast<double>(frame.width - 1)));
                const int sy = static_cast<int>(std::clamp(std::floor(
                    (static_cast<double>(clip.y) + y + 0.5 - d.y) * frame.height / d.height),
                    0.0, static_cast<double>(frame.height - 1)));
                const auto* source = frame.pixels + static_cast<std::size_t>(sy) * stride + sx * 4;
                std::copy_n(source, 4, result.pixels.data() +
                    (static_cast<std::size_t>(y) * clip.width + x) * 4);
            }
        return result;
    }
}
