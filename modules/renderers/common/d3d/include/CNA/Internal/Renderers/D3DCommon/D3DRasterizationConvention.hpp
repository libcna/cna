// SPDX-License-Identifier: MS-PL
#pragma once

#include "Microsoft/Xna/Framework/Matrix.hpp"

namespace CNA::Internal::Renderers::D3DCommon
{
    /**
     * @brief Converts a D3D10+ post-projection transform to XNA's D3D9 pixel-centre convention.
     *
     * @param transform Transform that maps vertices into clip space.
     * @param viewportWidth Width of the active viewport in pixels.
     * @param viewportHeight Height of the active viewport in pixels.
     * @param multisampledDestination Whether the active render target uses multisampling.
     * @return The transform with XNA's pixel-centre translation applied when appropriate.
     */
    [[nodiscard]] inline Microsoft::Xna::Framework::Matrix ApplyXnaPixelCenter(
        const Microsoft::Xna::Framework::Matrix& transform,
        float viewportWidth,
        float viewportHeight,
        bool multisampledDestination)
    {
        if (viewportWidth <= 0.0f || viewportHeight <= 0.0f || multisampledDestination)
            return transform;

        // XNA/D3D9 addresses integer window coordinates. D3D10+ addresses half-integers, so move
        // clip-space geometry just under half a pixel right/down. The sub-half margin keeps a
        // one-pixel right triangle on the covered side of the top-left fill rule; it is the same
        // measured convention EasyGL uses for the renderer-neutral XNA contract.
        constexpr float kPixelCenterScale = 63.0f / 64.0f;
        return transform * Microsoft::Xna::Framework::Matrix::CreateTranslation(
            kPixelCenterScale / viewportWidth,
            -kPixelCenterScale / viewportHeight,
            0.0f);
    }
}
