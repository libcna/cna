// SPDX-License-Identifier: MS-PL
#pragma once
#include <algorithm>

namespace CNA::Internal::Renderers
{
    /**
     * @brief Negotiates a Refresh sample preference against exact supported counts.
     * @param requested Preferred count; <=1 means no multisampling.
     * @param supports Predicate for one exact integer candidate and the selected color format.
     * @return First supported candidate, or 0 when no candidate in [2,min(requested,16)] works.
     */
    template<class Supports>
    int NegotiateRenderTargetSamples(int requested, Supports&& supports)
    {
        // XNA 4.0 Refresh GraphicsAdapter.QueryMultiSampleCount IL_0000-0046.
        // FNA MathHelper.ClosestMSAAPower pre-rounds and misses supported odd counts.
        for (int candidate = std::min(requested, 16); candidate > 1; --candidate)
            if (supports(candidate)) return candidate;
        return 0;
    }

    /**
     * @brief Forwards an unrounded Refresh preference to the format-aware 2D allocator.
     * @param renderer The active device renderer.
     * @param w Width in pixels.
     * @param h Height in pixels.
     * @param depth Depth format ordinal.
     * @param preserve Whether native contents must survive bind cycles.
     * @param mipMap Whether a mip chain is requested.
     * @param requested Preferred sample count.
     * @param format Selected color format ordinal.
     * @return The renderer's target with its actual applied count.
     */
    template<class Renderer>
    auto CreateRenderTarget2DWithSamplePreference(Renderer& renderer, int w, int h, int depth,
        bool preserve, bool mipMap, int requested, int format)
    {
        return renderer.CreateRenderTarget2DEXT(w, h, depth, preserve, mipMap,
            requested <= 1 ? 0 : std::min(requested, 16), format);
    }

    /**
     * @brief Forwards an unrounded Refresh preference to the format-aware cube allocator.
     * @param renderer The active device renderer.
     * @param size Face size in pixels.
     * @param depth Depth format ordinal.
     * @param preserve Whether native contents must survive bind cycles.
     * @param mipMap Whether a mip chain is requested.
     * @param requested Preferred sample count.
     * @param format Selected color format ordinal.
     * @return The renderer's target with its actual applied count.
     */
    template<class Renderer>
    auto CreateRenderTargetCubeWithSamplePreference(Renderer& renderer, int size, int depth,
        bool preserve, bool mipMap, int requested, int format)
    {
        return renderer.CreateRenderTargetCubeEXT(size, depth, preserve, mipMap,
            requested <= 1 ? 0 : std::min(requested, 16), format);
    }
}
