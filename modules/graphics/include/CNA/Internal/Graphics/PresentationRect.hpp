// SPDX-License-Identifier: MS-PL
#pragma once

#include <cmath>

/**
 * @file
 * @brief Mapping a logical XNA rectangle onto the drawable rectangle a renderer presents into.
 *
 * `GraphicsDevice.Viewport` and `GraphicsDevice.ScissorRectangle` are public XNA state expressed
 * in the game's **logical** (virtual-resolution) space. `IGraphicsRenderer::SetViewport` and
 * `IGraphicsRenderer::SetScissorRect` are not: for the renderers that present the logical content
 * into a sub-rectangle of the window — EasyGL, Magnum and OpenGL2 — those seams take **drawable
 * pixels**, Y-flip them against the physical size and program them verbatim.
 *
 * `GraphicsDevice::UpdateViewportFromWindow()` has always honoured that split, pushing the
 * physical rectangle from `IGraphicsRenderer::GetDefaultViewportRect()` while keeping the public
 * `Viewport` logical. The two public setters did not, so a game that assigned either property —
 * a split-screen sub-viewport, a scissored HUD, or simply re-applying the value it had just read
 * back — programmed the GPU in logical space and bypassed the letterbox/scale placement.
 *
 * The other renderer family (Diligent, Sokol, LLGL, SDL_GPU, WebGPU) deliberately treats the
 * pushed rectangle as logical and rescales internally, and therefore does not override
 * `GetDefaultViewportRect()`. That is exactly the case this mapping leaves alone: when the
 * physical rectangle equals `(0, 0, logicalWidth, logicalHeight)` the mapping is the identity.
 */
namespace CNA::Internal::Graphics
{
    /** @brief A rectangle in whichever space its producer names. */
    struct PresentationRect
    {
        /** @brief Left edge. */
        int x = 0;
        /** @brief Top edge. */
        int y = 0;
        /** @brief Width. */
        int width = 0;
        /** @brief Height. */
        int height = 0;
    };

    /**
     * @brief Maps a logical rectangle onto the drawable rectangle the renderer presents into.
     *
     * Scales about the presentation rectangle's own origin, mapping the rectangle's **edges**
     * rather than its origin and size independently, so that adjacent logical rectangles — the
     * two halves of a split screen — stay adjacent after scaling instead of leaving or
     * overlapping a seam pixel.
     *
     * Returns @p logical unchanged when the presentation rectangle is degenerate, when the
     * logical size is degenerate, or when the two already agree; the last of those is what keeps
     * every renderer that does not override `GetDefaultViewportRect()` bit-for-bit unaffected.
     *
     * @param logical        The rectangle in the game's logical space.
     * @param logicalWidth   Logical presentation width, from `GetViewportSize()`.
     * @param logicalHeight  Logical presentation height, from `GetViewportSize()`.
     * @param presentation   Drawable rectangle the logical content lands on, from
     *                       `GetDefaultViewportRect()`.
     * @return The rectangle to hand to the renderer seam.
     */
    [[nodiscard]] inline PresentationRect MapLogicalRectToPresentation(
        const PresentationRect& logical,
        int logicalWidth,
        int logicalHeight,
        const PresentationRect& presentation)
    {
        if (logicalWidth <= 0 || logicalHeight <= 0)
            return logical;
        if (presentation.width <= 0 || presentation.height <= 0)
            return logical;
        if (presentation.x == 0 && presentation.y == 0 &&
            presentation.width == logicalWidth && presentation.height == logicalHeight)
            return logical;

        const double scaleX =
            static_cast<double>(presentation.width) / static_cast<double>(logicalWidth);
        const double scaleY =
            static_cast<double>(presentation.height) / static_cast<double>(logicalHeight);

        const double left = presentation.x + static_cast<double>(logical.x) * scaleX;
        const double top = presentation.y + static_cast<double>(logical.y) * scaleY;
        const double right =
            presentation.x + static_cast<double>(logical.x + logical.width) * scaleX;
        const double bottom =
            presentation.y + static_cast<double>(logical.y + logical.height) * scaleY;

        PresentationRect mapped;
        mapped.x = static_cast<int>(std::lround(left));
        mapped.y = static_cast<int>(std::lround(top));
        mapped.width = static_cast<int>(std::lround(right)) - mapped.x;
        mapped.height = static_cast<int>(std::lround(bottom)) - mapped.y;
        return mapped;
    }
}
