// SPDX-License-Identifier: MS-PL
#include "CNA/Internal/Renderers/Common/GpuPresentation.hpp"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <vector>

namespace CNA::Internal::Renderers
{
    bool IsForeignGpuPresentEXT(const IGraphicsRenderer::RendererPresentRegionEXT& region,
                               CNA::Platform::WindowId ownWindow)
    {
        return region.targetWindowId != 0 && region.targetWindowId != ownWindow;
    }

    GpuPresentMappingEXT ResolveGpuPresentMappingEXT(
        const IGraphicsRenderer::RendererPresentRegionEXT& region,
        int logicalWidth, int logicalHeight, const std::array<float, 4>& presentation,
        int bufferWidth, int bufferHeight)
    {
        if (logicalWidth <= 0 || logicalHeight <= 0 || bufferWidth <= 0 || bufferHeight <= 0 ||
            region.targetWidth <= 0 || region.targetHeight <= 0 ||
            !std::all_of(presentation.begin(), presentation.end(), [](float v) { return std::isfinite(v); }) ||
            presentation[2] <= 0 || presentation[3] <= 0)
            throw std::invalid_argument("GPU Present requires positive source and target dimensions");
        const int sx = region.hasSourceRectangle ? region.sourceX : 0;
        const int sy = region.hasSourceRectangle ? region.sourceY : 0;
        const int sw = region.hasSourceRectangle ? region.sourceWidth : logicalWidth;
        const int sh = region.hasSourceRectangle ? region.sourceHeight : logicalHeight;
        if (sx < 0 || sy < 0 || sw <= 0 || sh <= 0 ||
            static_cast<std::int64_t>(sx) + sw > logicalWidth ||
            static_cast<std::int64_t>(sy) + sh > logicalHeight)
            throw std::invalid_argument("GPU Present source exceeds the live logical backbuffer");
        GpuPresentMappingEXT mapping;
        mapping.sourceWidth = sw;
        mapping.sourceHeight = sh;
        const double scaleX = static_cast<double>(presentation[2]) / logicalWidth;
        const double scaleY = static_cast<double>(presentation[3]) / logicalHeight;
        const double x = presentation[0] + sx * scaleX;
        const double y = presentation[1] + sy * scaleY;
        const double right = presentation[0] + (static_cast<double>(sx) + sw) * scaleX;
        const double bottom = presentation[1] + (static_cast<double>(sy) + sh) * scaleY;
        // Match CNA's PresentedPixelSpan/GetBackBufferData adaptation: physical backbuffers
        // sample logical texel centers and clamp overscan pixels to the nearest visible edge.
        // This is exact at scale 1 and in flat regions, not a claim of native XNA overscan parity.
        mapping.source = {static_cast<float>(x), static_cast<float>(y),
                          static_cast<float>(right - x), static_cast<float>(bottom - y)};
        const int captureX = static_cast<int>(std::clamp(std::floor(x), 0.0, static_cast<double>(bufferWidth - 1)));
        const int captureY = static_cast<int>(std::clamp(std::floor(y), 0.0, static_cast<double>(bufferHeight - 1)));
        const int captureRight = static_cast<int>(std::clamp(std::ceil(right), static_cast<double>(captureX + 1),
                                                            static_cast<double>(bufferWidth)));
        const int captureBottom = static_cast<int>(std::clamp(std::ceil(bottom), static_cast<double>(captureY + 1),
                                                             static_cast<double>(bufferHeight)));
        mapping.capture = {captureX, captureY, captureRight - captureX, captureBottom - captureY};
        mapping.destination = region.hasDestinationRectangle
            ? CNA::Platform::SurfaceDestination{region.destinationX, region.destinationY,
                                                region.destinationWidth, region.destinationHeight}
            : CNA::Platform::SurfaceDestination{0, 0, region.targetWidth, region.targetHeight};
        const auto& d = mapping.destination;
        if (d.width <= 0 || d.height <= 0)
            throw std::invalid_argument("GPU Present destination must have positive extent");
        // Refresh Present IL_0046-0082 forwards the original RECT. Clip writes, not its mapping.
        using I64 = std::int64_t;
        const I64 left = std::clamp<I64>(d.x, 0, region.targetWidth);
        const I64 top = std::clamp<I64>(d.y, 0, region.targetHeight);
        const I64 endX = std::clamp<I64>(static_cast<I64>(d.x) + d.width, 0, region.targetWidth);
        const I64 endY = std::clamp<I64>(static_cast<I64>(d.y) + d.height, 0, region.targetHeight);
        mapping.clip = {static_cast<int>(left), static_cast<int>(top),
                        static_cast<int>(std::max<I64>(0, endX - left)),
                        static_cast<int>(std::max<I64>(0, endY - top))};
        return mapping;
    }

    namespace
    {
        std::size_t PixelBytes(int width, int height)
        {
            if (width <= 0 || height <= 0 || width > std::numeric_limits<int>::max() / 4 ||
                static_cast<std::size_t>(height) > std::numeric_limits<std::size_t>::max() /
                    (static_cast<std::size_t>(width) * 4))
                throw std::length_error("GPU Present pixel buffer size overflow");
            return static_cast<std::size_t>(width) * height * 4;
        }
    }

    bool PresentGpuReadbackEXT(
        const IGraphicsRenderer::RendererPresentRegionEXT& region,
        const GpuPresentMappingEXT& mapping,
        const std::function<void(int, int, int, int, std::uint8_t*)>& readback)
    {
        if (region.targetPresenter == nullptr) return false;
        if (mapping.clip.width == 0 || mapping.clip.height == 0) return true;
        const auto& c = mapping.capture;
        std::vector<std::uint8_t> physical(PixelBytes(c.width, c.height));
        std::vector<std::uint8_t> logical(PixelBytes(mapping.sourceWidth, mapping.sourceHeight));
        readback(c.x, c.y, c.width, c.height, physical.data());
        // Recover logical texel centers from the physical image. Nearest sampling is explicit;
        // native filtering parity beyond flat interiors remains owner qualification.
        for (int y = 0; y < mapping.sourceHeight; ++y)
            for (int x = 0; x < mapping.sourceWidth; ++x)
            {
                const int px = static_cast<int>(std::clamp(std::floor(mapping.source[0] +
                    (x + 0.5) * mapping.source[2] / mapping.sourceWidth) - c.x, 0.0, static_cast<double>(c.width - 1)));
                const int py = static_cast<int>(std::clamp(std::floor(mapping.source[1] +
                    (y + 0.5) * mapping.source[3] / mapping.sourceHeight) - c.y, 0.0, static_cast<double>(c.height - 1)));
                const auto* source = physical.data() + (static_cast<std::size_t>(py) * c.width + px) * 4;
                auto* destination = logical.data() + (static_cast<std::size_t>(y) * mapping.sourceWidth + x) * 4;
                std::copy_n(source, 4, destination);
            }
        region.targetPresenter->SetScaleMode(CNA::Platform::PresentScaleMode::Stretch,
                                             CNA::Platform::PresentFilter::Nearest);
        const CNA::Platform::SurfaceFrame frame{logical.data(), mapping.sourceWidth,
                                                mapping.sourceHeight, mapping.sourceWidth * 4};
        return region.targetPresenter->PresentRegion(frame, mapping.destination);
    }
}
