#pragma once

#include "CNA/Internal/Backends/Common/IGraphicsBackend.hpp"
#include <algorithm>

// plan_metal.md METAL-34-style extraction: the actual letterbox/logical-viewport arithmetic is
// pure C++ (plain floats/ints and CnaPresentationMode, a shared enum with zero Objective-C
// dependency) -- only the caller-side "ask SDL for the physical window size" step needs a real
// window, so that step stays in MetalGraphicsBackend.mm's own computeLogicalViewport() wrapper.
// This is the same real formula plan_metal.md METAL-153/154's own comment says was "ported from
// SdlGpuGraphicsBackend::TransformWindowToLogical/TransformLogicalToWindow verbatim (same
// LogicalViewport shape, same formula) rather than re-derived" -- extracting Metal's own copy here
// does not touch SdlGpuGraphicsBackend's independent implementation.
namespace CNA::Internal::Backends::Metal
{
    struct MetalLogicalViewport { float x=0, y=0, width=0, height=0, logicalWidth=0, logicalHeight=0; };

    // plan_metal.md METAL-155/156/157/158/159: real virtual-resolution/letterbox math, previously
    // completely bypassed (SpriteBatch drew from raw physical drawable pixels). `pw`/`ph` are the
    // real physical window/drawable size in pixels; `virtualW`/`virtualH` are the requested virtual
    // resolution (<=0 means "none requested", degrading to the physical size unscaled). See
    // CnaPresentationMode's own doc comment on IGraphicsBackend.hpp for what each mode means.
    inline MetalLogicalViewport ComputeMetalLogicalViewport(int pw, int ph,
                                                              CNA::Internal::Backends::CnaPresentationMode mode,
                                                              int virtualW, int virtualH)
    {
        using CNA::Internal::Backends::CnaPresentationMode;
        MetalLogicalViewport vp{};
        vp.width = (float)(pw > 0 ? pw : 0); vp.height = (float)(ph > 0 ? ph : 0);
        vp.logicalWidth = vp.width; vp.logicalHeight = vp.height;
        if (pw <= 0 || ph <= 0) return vp;
        if (mode == CnaPresentationMode::NativeBackBuffer || virtualW <= 0 || virtualH <= 0) return vp;

        float logicalWidth = (float)virtualW;
        float logicalHeight = (float)virtualH;
        if (mode == CnaPresentationMode::FixedHeightDynamicWidth) {
            logicalWidth = logicalHeight * (float)pw / (float)ph;
            vp.logicalWidth = logicalWidth; vp.logicalHeight = logicalHeight;
            return vp;
        }
        vp.logicalWidth = logicalWidth; vp.logicalHeight = logicalHeight;
        if (mode == CnaPresentationMode::Stretch) return vp;
        const float sx = (float)pw / logicalWidth;
        const float sy = (float)ph / logicalHeight;
        const float scale = (mode == CnaPresentationMode::Overscan) ? std::max(sx, sy) : std::min(sx, sy);
        vp.width = logicalWidth * scale; vp.height = logicalHeight * scale;
        vp.x = ((float)pw - vp.width) * 0.5f; vp.y = ((float)ph - vp.height) * 0.5f;
        return vp;
    }
}
