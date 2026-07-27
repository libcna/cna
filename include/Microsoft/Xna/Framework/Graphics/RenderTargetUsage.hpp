// SPDX-License-Identifier: MS-PL
#pragma once

#include "CNA/CNAHelper.hpp"

namespace Microsoft::Xna::Framework::Graphics
{
    /** @brief Defines whether the previous render-target content is preserved when it is set on the graphics device. */
    enum class RenderTargetUsage
    {
        /** @brief The render target content will not be preserved. */
        DiscardContents,
        /** @brief The render target content will be preserved even if it is slow or requires extra memory. */
        PreserveContents,
        /** @brief The render target content may be preserved if the platform can do so without a performance or memory penalty. */
        PlatformContents
    };

    /**
     * @brief The single mapping from a public RenderTargetUsage to the backend's `preserveContents`
     *        flag.
     *
     * REMED-GFX-136. Every render target that reaches a backend goes through this one function --
     * `RenderTarget2D` and `RenderTargetCube` alike -- so the two cannot drift apart, and a reader
     * has exactly one place to look for what `PlatformContents` means in this project.
     *
     * The rule is FNA's own: everything except `DiscardContents` preserves. That matches what
     * `GraphicsDevice::SetRenderTargets` has always done at the shared layer, where only a
     * `DiscardContents` target is cleared on bind (FNA's `if (clearTarget == DiscardContents)
     * Clear(...)`); before this helper existed the two public targets passed `usage ==
     * PreserveContents` instead, so the shared layer and the backend disagreed about
     * `PlatformContents` and whichever one a given backend happened to honour decided the result.
     *
     * @param usage The public usage the render target was constructed with.
     * @return True when the backend must load this target's existing contents when it is bound.
     */
    NOXNA [[nodiscard]] constexpr bool RenderTargetUsagePreservesContentsEXT(RenderTargetUsage usage) noexcept
    {
        return usage != RenderTargetUsage::DiscardContents;
    }
}
