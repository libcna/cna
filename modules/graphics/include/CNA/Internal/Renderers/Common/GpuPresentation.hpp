// SPDX-License-Identifier: MS-PL
#pragma once
#include "CNA/Internal/Renderers/Common/IGraphicsRenderer.hpp"
#include "CNA/Platform/IPlatformSurfacePresenter.hpp"
#include <array>
#include <functional>

namespace CNA::Internal::Renderers
{
    /** @brief Source sampling and destination clipping for explicit GPU presentation. */
    struct GpuPresentMappingEXT
    {
        /** @brief Physical source x/y/width/height, before destination clipping. */
        std::array<float, 4> source{};
        /** @brief Original destination affine map in physical client pixels. */
        CNA::Platform::SurfaceDestination destination;
        /** @brief Intersection of destination and selected client; never a new affine map. */
        CNA::Platform::SurfaceDestination clip;
        /** @brief Width and height of the selected logical source image. */
        int sourceWidth = 0, sourceHeight = 0;
        /** @brief Smallest physical pixel rectangle covering the selected source. */
        CNA::Platform::SurfaceDestination capture;
    };

    /**
     * @brief Distinguishes a selected foreign client from an alias of the own native window.
     * @param region Selected platform window snapshot.
     * @param ownWindow Own stable platform identity.
     * @return Whether the native dirty-rectangle readback route is needed.
     */
    bool IsForeignGpuPresentEXT(const IGraphicsRenderer::RendererPresentRegionEXT& region,
                               CNA::Platform::WindowId ownWindow);

    /**
     * @brief Resolves logical source coordinates and preserves the original destination mapping.
     * @param region Validated public request and selected physical client snapshot.
     * @param logicalWidth Live logical backbuffer width.
     * @param logicalHeight Live logical backbuffer height.
     * @param presentation Physical x/y/width/height of the logical image within the source texture.
     * @param bufferWidth Physical source texture width.
     * @param bufferHeight Physical source texture height.
     * @return Source sampling, original destination and clipped write bounds.
     */
    GpuPresentMappingEXT ResolveGpuPresentMappingEXT(
        const IGraphicsRenderer::RendererPresentRegionEXT& region,
        int logicalWidth, int logicalHeight, const std::array<float, 4>& presentation,
        int bufferWidth, int bufferHeight);

    /**
     * @brief Reads the selected source and presents it through the borrowed native dirty presenter.
     * @param region Selected presenter, kept alive by GraphicsDevice.
     * @param mapping Logical source and physical capture/destination mapping.
     * @param readback Reads tightly packed physical RGBA8 pixels without presenting any window.
     * @return False when the selected presenter cannot honor the request; no fallback whole present.
     */
    bool PresentGpuReadbackEXT(
        const IGraphicsRenderer::RendererPresentRegionEXT& region,
        const GpuPresentMappingEXT& mapping,
        const std::function<void(int, int, int, int, std::uint8_t*)>& readback);
}
