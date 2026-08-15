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
     * @brief The single mapping from a public RenderTargetUsage to the renderer's `preserveContents`
     *        flag.
     *
     * REMED-GFX-136. Every render target that reaches a renderer goes through this one function --
     * `RenderTarget2D` and `RenderTargetCube` alike -- so the two cannot drift apart, and a reader
     * has exactly one place to look for what `PlatformContents` means in this project.
     *
     * The rule is FNA's own: everything except `DiscardContents` preserves. That matches what
     * `GraphicsDevice::SetRenderTargets` has always done at the shared layer, where only a
     * `DiscardContents` target is cleared on bind (FNA's `if (clearTarget == DiscardContents)
     * Clear(...)`); before this helper existed the two public targets passed `usage ==
     * PreserveContents` instead, so the shared layer and the renderer disagreed about
     * `PlatformContents` and whichever one a given renderer happened to honour decided the result.
     *
     * @par What is preserved
     * REMED-GFX-142: **colour, depth AND stencil** -- all three, not colour alone. FNA3D's public
     * header documents the flag this maps to as "Set this to 1 to store the color/depth/stencil
     * contents for future use", and all three of its drivers agree (an OpenGL FBO attachment and a
     * D3D11 DSV simply persist; one GPU driver loads both depth and stencil unless a clear is
     * pending and always stores them). So for `PreserveContents` and `PlatformContents`:
     * - colour, depth and stencil each survive a full unbind/rebind cycle,
     * - across a trip through another render target or through the backbuffer,
     * - at sample count 1 and under multisampling, where the MULTISAMPLE depth attachment itself
     *   survives (depth is never resolved),
     * - and for `RenderTargetCube`, depth/stencil is ONE per-TARGET buffer shared by all six faces,
     *   exactly as FNA allocates it (a single `glDepthStencilBuffer` per cube), so switching faces
     *   neither switches depth buffers nor gives you a fresh one. Colour is per-face; depth is not.
     *
     * @par What DiscardContents replaces it with
     * Not "undefined": `GraphicsDevice::SetRenderTargets` clears a `DiscardContents` target on every
     * bind, mirroring FNA's own `Clear(ClearOptions.Target | ClearOptions.DepthBuffer |
     * ClearOptions.Stencil, DiscardColor, Viewport.MaxDepth, 0)` -- colour to `(0, 0, 0, 255)`,
     * depth to 1.0, stencil to 0. A renderer is free to deliver that through a native clear load
     * action, but the observable result is that deterministic triple.
     *
     * An explicit `Clear()` issued inside a bind cycle is a separate, ordered command that always
     * wins over the bind-time behaviour (REMED-GFX-129), and it is per-aspect: clearing depth never
     * touches stencil or colour, and vice versa.
     *
     * Contents of a target that has never been rendered into are not specified -- a brand-new
     * preserving target has no previous content to preserve. Nothing survives device recreation.
     * Depth and stencil are observable only indirectly, through what a later depth-tested or
     * stencil-tested draw does; `examples/rendertarget_depthstencil_usage_test` is the oracle that
     * enforces every clause above on all fourteen renderers.
     *
     * @param usage The public usage the render target was constructed with.
     * @return True when the renderer must load this target's existing colour, depth and stencil
     *         contents when it is bound.
     */
    CNAEXT [[nodiscard]] constexpr bool RenderTargetUsagePreservesContentsEXT(RenderTargetUsage usage) noexcept
    {
        return usage != RenderTargetUsage::DiscardContents;
    }
}
