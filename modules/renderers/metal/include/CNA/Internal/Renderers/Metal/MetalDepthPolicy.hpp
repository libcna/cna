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
     * `MTLPixelFormatDepth32Float_Stencil8` attachment except a `Depth16` one, and what a target
     * EXPOSES follows the request: `None` has no depth and no stencil, and `Depth24` has no
     * stencil, exactly as XNA describes them. plans/plan_apple_m4.md AM4-151: `Depth16` is stored
     * natively (`MTLPixelFormatDepth16Unorm`, MetalDepthStorageFor), so it is reported as itself;
     * it used to be reported as `Depth24`, the substitution of its then 32-bit float storage
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
                return static_cast<int>(DepthFormat::Depth16);
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

    /** @brief The native texture behind a Metal target's depth/stencil attachment. */
    enum class MetalDepthStorage
    {
        /** @brief MTLPixelFormatDepth16Unorm -- Depth16; no stencil attachment. */
        Depth16Unorm,
        /** @brief MTLPixelFormatDepth32Float_Stencil8 -- every other DepthFormat; a plane the
         *         format does not have is never tested or written (MetalDepthFormatHasStencil). */
        Depth32FloatStencil8
    };

    /**
     * @brief The depth/stencil storage of a target with the given applied DepthFormat.
     *
     * plans/plan_apple_m4.md AM4-151. Metal has no 24-bit depth on Apple GPUs, so `Depth24` keeps
     * the 32-bit float storage (more precision than requested); `Depth16` gets XNA's own 16-bit
     * depth, its precision included.
     *
     * @param appliedDepthFormat Applied DepthFormat ordinal.
     * @return The storage.
     */
    [[nodiscard]] constexpr MetalDepthStorage MetalDepthStorageFor(int appliedDepthFormat) noexcept
    {
        using Microsoft::Xna::Framework::Graphics::DepthFormat;
        return appliedDepthFormat == static_cast<int>(DepthFormat::Depth16)
            ? MetalDepthStorage::Depth16Unorm
            : MetalDepthStorage::Depth32FloatStencil8;
    }
}
