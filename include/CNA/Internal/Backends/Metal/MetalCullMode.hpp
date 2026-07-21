#pragma once

#include "Microsoft/Xna/Framework/Graphics/CullMode.hpp"

// plan_metal.md METAL-19/METAL-5: same reasoning as MetalCompareFunction.hpp -- switches on the
// real `CullMode` enumerator names instead of the raw `c==1?...:(c==2?...:...)` ternary chain
// ApplyRasterizerState() previously used inline. METAL-5's own explicit mapping
// (CullClockwiseFace(1)->Front, CullCounterClockwiseFace(2)->Back) is preserved exactly, just made
// enum-reordering-safe and independently unit-testable.
namespace CNA::Internal::Backends::Metal
{
    enum class MetalCullModeKind
    {
        None, Front, Back
    };

    inline MetalCullModeKind DescribeMetalCullMode(int xnaCull)
    {
        using CM = Microsoft::Xna::Framework::Graphics::CullMode;
        switch (static_cast<CM>(xnaCull)) {
            case CM::CullClockwiseFace:        return MetalCullModeKind::Front;
            case CM::CullCounterClockwiseFace: return MetalCullModeKind::Back;
            case CM::None:
            default:                           return MetalCullModeKind::None;
        }
    }
}
