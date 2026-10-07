// SPDX-License-Identifier: MS-PL
#pragma once

#include "Microsoft/Xna/Framework/Graphics/DepthFormat.hpp"

namespace CNA::Internal::Renderers::Metal
{
    /**
     * @brief Resolves the native depth-write flag from independently retained XNA state.
     *
     * @param depthEnabled Whether depth testing/storage is currently enabled.
     * @param depthWriteRequested The caller's retained DepthBufferWriteEnable value.
     * @return True only when depth is enabled and writes were requested.
     */
    [[nodiscard]] constexpr bool MetalEffectiveDepthWriteEnabled(
        bool depthEnabled,
        bool depthWriteRequested) noexcept
    {
        return depthEnabled && depthWriteRequested;
    }

    /**
     * @brief The DepthFormat a Metal render target reports for the one it was created with.
     *
     * plans/plan_apple_m4.md AM4-032. Every Metal target is backed by one
     * `MTLPixelFormatDepth32Float_Stencil8` attachment, so pipelines stay compatible across
     * targets; what a target EXPOSES follows the request. `None` has no depth and no stencil, and
     * `Depth24` has no stencil, exactly as XNA describes them. `Depth16` is reported as `Depth24`
     * -- the stored depth is 32-bit float, and a substitution is reported rather than hidden
     * (plans/plan_vulkan.md VULKAN-348).
     *
     * @param requestedDepthFormat Requested DepthFormat ordinal.
     * @return Applied DepthFormat ordinal.
     */
    [[nodiscard]] constexpr int MetalAppliedRenderTargetDepthFormat(int requestedDepthFormat) noexcept
    {
        using Microsoft::Xna::Framework::Graphics::DepthFormat;
        switch (static_cast<DepthFormat>(requestedDepthFormat))
        {
            case DepthFormat::None:
                return static_cast<int>(DepthFormat::None);
            case DepthFormat::Depth16:
            case DepthFormat::Depth24:
                return static_cast<int>(DepthFormat::Depth24);
            default:
                return static_cast<int>(DepthFormat::Depth24Stencil8);
        }
    }

    /**
     * @brief Whether a target with the given applied DepthFormat has a depth plane.
     *
     * @param appliedDepthFormat Applied DepthFormat ordinal.
     * @return False only for DepthFormat::None.
     */
    [[nodiscard]] constexpr bool MetalDepthFormatHasDepth(int appliedDepthFormat) noexcept
    {
        using Microsoft::Xna::Framework::Graphics::DepthFormat;
        return appliedDepthFormat != static_cast<int>(DepthFormat::None);
    }

    /**
     * @brief Whether a target with the given applied DepthFormat has a stencil plane.
     *
     * @param appliedDepthFormat Applied DepthFormat ordinal.
     * @return True only for DepthFormat::Depth24Stencil8.
     */
    [[nodiscard]] constexpr bool MetalDepthFormatHasStencil(int appliedDepthFormat) noexcept
    {
        using Microsoft::Xna::Framework::Graphics::DepthFormat;
        return appliedDepthFormat == static_cast<int>(DepthFormat::Depth24Stencil8);
    }
}
