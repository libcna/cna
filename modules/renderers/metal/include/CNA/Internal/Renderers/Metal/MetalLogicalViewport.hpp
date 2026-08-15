// SPDX-License-Identifier: MS-PL
#pragma once

#include "CNA/Internal/Renderers/Common/IGraphicsRenderer.hpp"
#include <algorithm>

// plan_metal.md METAL-34-style extraction: the actual letterbox/logical-viewport arithmetic is
// pure C++ (plain floats/ints and CnaPresentationMode, a shared enum with zero Objective-C
// dependency) -- only the caller-side physical window snapshot needs a real window, so that step
// stays in MetalRenderer.mm's own computeLogicalViewport() wrapper. This is the established GPU
// renderer's formula (same LogicalViewport shape and math) rather than an independent re-derivation.
namespace CNA::Internal::Renderers::Metal
{
    /** @brief Physical and logical dimensions of Metal's active presentation rectangle. */
    struct MetalLogicalViewport { float x=0, y=0, width=0, height=0, logicalWidth=0, logicalHeight=0; };

    // plan_metal.md METAL-155/156/157/158/159: real virtual-resolution/letterbox math, previously
    // completely bypassed (SpriteBatch drew from raw physical drawable pixels). `pw`/`ph` are the
    // real physical window/drawable size in pixels; `virtualW`/`virtualH` are the requested virtual
    // resolution (<=0 means "none requested", degrading to the physical size unscaled). See
    // CnaPresentationMode's own doc comment on IGraphicsRenderer.hpp for what each mode means.
    /**
     * @brief Computes Metal's presentation rectangle without using native window APIs.
     *
     * @param physicalWidth Physical drawable width in pixels.
     * @param physicalHeight Physical drawable height in pixels.
     * @param mode Active CNA presentation mode.
     * @param virtualWidth Requested logical width, or a non-positive value when unset.
     * @param virtualHeight Requested logical height, or a non-positive value when unset.
     * @return The resulting physical rectangle and logical dimensions.
     */
    [[nodiscard]] inline MetalLogicalViewport ComputeMetalLogicalViewport(
        int physicalWidth, int physicalHeight,
        CNA::Internal::Renderers::CnaPresentationMode mode,
        int virtualWidth, int virtualHeight)
    {
        using CNA::Internal::Renderers::CnaPresentationMode;
        MetalLogicalViewport vp{};
        vp.width = static_cast<float>(physicalWidth > 0 ? physicalWidth : 0);
        vp.height = static_cast<float>(physicalHeight > 0 ? physicalHeight : 0);
        vp.logicalWidth = vp.width; vp.logicalHeight = vp.height;
        if (physicalWidth <= 0 || physicalHeight <= 0) return vp;
        if (mode == CnaPresentationMode::NativeBackBuffer || virtualWidth <= 0 || virtualHeight <= 0) return vp;

        float logicalWidth = static_cast<float>(virtualWidth);
        float logicalHeight = static_cast<float>(virtualHeight);
        if (mode == CnaPresentationMode::FixedHeightDynamicWidth) {
            logicalWidth = logicalHeight * static_cast<float>(physicalWidth) /
                           static_cast<float>(physicalHeight);
            vp.logicalWidth = logicalWidth; vp.logicalHeight = logicalHeight;
            return vp;
        }
        vp.logicalWidth = logicalWidth; vp.logicalHeight = logicalHeight;
        if (mode == CnaPresentationMode::Stretch) return vp;
        const float sx = static_cast<float>(physicalWidth) / logicalWidth;
        const float sy = static_cast<float>(physicalHeight) / logicalHeight;
        const float scale = (mode == CnaPresentationMode::Overscan) ? std::max(sx, sy) : std::min(sx, sy);
        vp.width = logicalWidth * scale; vp.height = logicalHeight * scale;
        vp.x = (static_cast<float>(physicalWidth) - vp.width) * 0.5f;
        vp.y = (static_cast<float>(physicalHeight) - vp.height) * 0.5f;
        return vp;
    }
}
