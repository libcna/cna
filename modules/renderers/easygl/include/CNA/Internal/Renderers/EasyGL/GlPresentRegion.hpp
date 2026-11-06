// SPDX-License-Identifier: MS-PL
#pragma once
#include "CNA/Internal/Renderers/Common/GpuPresentation.hpp"
#include <array>
#include <optional>
#include <vector>

namespace CNA::Internal::Renderers::EasyGL
{
    /** @brief Integer GL endpoint pairs for a source-to-destination framebuffer blit. */
    struct GlPresentBlitEXT
    {
        /** @brief Source left/bottom/right/top in bottom-left framebuffer coordinates. */
        std::array<int, 4> source;
        /** @brief Original destination endpoints, before framebuffer clipping. */
        std::array<int, 4> destination;
    };

    /**
     * @brief Converts a physical mapping to exact GL endpoints where integer blits suffice.
     * @param mapping Validated source and original destination affine map.
     * @param sourceWidth Physical source width.
     * @param sourceHeight Physical source height.
     * @param targetHeight Selected physical client height.
     * @return Exact integer blit, or no value when fractional/clamped sampling needs a CPU patch.
     */
    std::optional<GlPresentBlitEXT> BuildGlPresentBlitEXT(
        const GpuPresentMappingEXT& mapping, int sourceWidth, int sourceHeight, int targetHeight);

    /** @brief A tightly packed patch covering just the visible destination intersection. */
    struct GlPresentPixelsEXT
    {
        /** @brief Visible destination in top-left physical pixels. */
        CNA::Platform::SurfaceDestination destination;
        /** @brief Top-down RGBA8 image, already scaled to destination. */
        std::vector<std::uint8_t> pixels;
    };

    /**
     * @brief Samples a logical source at visible destination pixel centers, preserving clipping.
     * @param frame Borrowed logical source with its parent row stride.
     * @param mapping Original destination and its selected-client intersection.
     * @return Finished visible patch, without reading or writing a native client.
     */
    GlPresentPixelsEXT ClipGlPresentPixelsEXT(
        const CNA::Platform::SurfaceFrame& frame, const GpuPresentMappingEXT& mapping);
}
