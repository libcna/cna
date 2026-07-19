#pragma once

#include "Microsoft/Xna/Framework/Graphics/PrimitiveType.hpp"

// plan_metal.md METAL-34-style extraction: this formula only reads a plain XNA framework enum
// (Microsoft::Xna::Framework::Graphics::PrimitiveType) and plain ints -- zero Objective-C/Metal-
// framework dependency, so it can be genuinely unit-tested on any platform without an Apple
// toolchain, unlike the rest of the Metal backend. MetalGraphicsBackend.mm includes this header
// instead of defining the function inline; logic is unchanged from the original inline definition.
// METAL-13's own history is why this one is worth testing in particular: PointListEXT previously
// fell through to the *3 default (a real, previously-shipping bug, already fixed) -- a real test
// here locks that fix in against a future regression.
namespace CNA::Internal::Backends::Metal
{
    inline int ComputeMetalPrimitiveVertexCount(Microsoft::Xna::Framework::Graphics::PrimitiveType p, int count)
    {
        using PT = Microsoft::Xna::Framework::Graphics::PrimitiveType;
        switch (p) {
            case PT::TriangleList: return count * 3;
            case PT::TriangleStrip: return count + 2;
            case PT::LineList: return count * 2;
            case PT::LineStrip: return count + 1;
            case PT::PointListEXT: return count; // plan_metal.md METAL-13: was falling to the *3 default
            default: return count * 3;
        }
    }
}
