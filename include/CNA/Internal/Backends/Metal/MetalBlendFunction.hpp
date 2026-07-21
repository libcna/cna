#pragma once

#include "Microsoft/Xna/Framework/Graphics/BlendFunction.hpp"

// plan_metal.md METAL-19: same reasoning as MetalCompareFunction.hpp -- switches on the real
// `BlendFunction` enumerator names instead of raw int literals. Mirrors EasyGL's
// ToEasyGLBlendEquation / Vulkan's ToVkBlendOp exactly.
namespace CNA::Internal::Backends::Metal
{
    enum class MetalBlendOperationKind
    {
        Add, Subtract, ReverseSubtract, Max, Min
    };

    inline MetalBlendOperationKind DescribeMetalBlendOperation(int xnaBlendFunc)
    {
        using BF = Microsoft::Xna::Framework::Graphics::BlendFunction;
        switch (static_cast<BF>(xnaBlendFunc)) {
            case BF::Subtract:        return MetalBlendOperationKind::Subtract;
            case BF::ReverseSubtract: return MetalBlendOperationKind::ReverseSubtract;
            case BF::Max:             return MetalBlendOperationKind::Max;
            case BF::Min:             return MetalBlendOperationKind::Min;
            case BF::Add:
            default:                  return MetalBlendOperationKind::Add;
        }
    }
}
