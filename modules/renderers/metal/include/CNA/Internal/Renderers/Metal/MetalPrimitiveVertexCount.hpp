// SPDX-License-Identifier: MS-PL
#pragma once

#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"

// plans/plan_metal.md METAL-34-style extraction: this formula only reads a plain XNA framework enum
// (Microsoft::Xna::Framework::Graphics::PrimitiveType) and plain ints -- zero Objective-C/Metal-
// framework dependency, so it can be genuinely unit-tested on any platform without an Apple
// toolchain, unlike the rest of the Metal renderer. MetalRenderer.mm includes this header
// instead of defining the function inline; logic is unchanged from the original inline definition.
// METAL-13's own history is why this one is worth testing in particular: PointListEXT previously
// fell through to the *3 default (a real, previously-shipping bug, already fixed) -- a real test
// here locks that fix in against a future regression.
namespace CNA::Internal::Renderers::Metal
{
    /**
     * @brief Computes the number of vertices required by a non-indexed primitive draw.
     *
     * @param primitiveType XNA primitive topology.
     * @param primitiveCount Number of primitives requested by the draw.
     * @return Number of vertices consumed by the topology.
     */
    [[nodiscard]] inline int ComputeMetalPrimitiveVertexCount(
        Microsoft::Xna::Framework::Graphics::PrimitiveType primitiveType, int primitiveCount)
    {
        using PT = Microsoft::Xna::Framework::Graphics::PrimitiveType;
        switch (primitiveType) {
            case PT::TriangleList: return primitiveCount * 3;
            case PT::TriangleStrip: return primitiveCount + 2;
            case PT::LineList: return primitiveCount * 2;
            case PT::LineStrip: return primitiveCount + 1;
            case PT::PointListEXT: return primitiveCount;
            default: return primitiveCount * 3;
        }
    }
}
