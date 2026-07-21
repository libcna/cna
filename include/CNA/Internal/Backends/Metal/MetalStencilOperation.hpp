#pragma once

#include "Microsoft/Xna/Framework/Graphics/StencilOperation.hpp"

// plan_metal.md METAL-19: same reasoning as MetalCompareFunction.hpp -- switches on the real
// `StencilOperation` enumerator names instead of raw int literals, so a future reordering of the
// enum cannot silently desync this table. XNA's Increment/Decrement wrap
// (D3DSTENCILOP_INCR/DECR); the *Saturation variants clamp (D3DSTENCILOP_INCRSAT/DECRSAT) --
// confirmed against VulkanGraphicsBackend's own already-tested ToVkStencilOp.
namespace CNA::Internal::Backends::Metal
{
    enum class MetalStencilOperationKind
    {
        Keep, Zero, Replace, IncrementWrap, DecrementWrap, IncrementClamp, DecrementClamp, Invert
    };

    inline MetalStencilOperationKind DescribeMetalStencilOperation(int xnaOp)
    {
        using SO = Microsoft::Xna::Framework::Graphics::StencilOperation;
        switch (static_cast<SO>(xnaOp)) {
            case SO::Zero:                 return MetalStencilOperationKind::Zero;
            case SO::Replace:              return MetalStencilOperationKind::Replace;
            case SO::Increment:            return MetalStencilOperationKind::IncrementWrap;
            case SO::Decrement:            return MetalStencilOperationKind::DecrementWrap;
            case SO::IncrementSaturation:  return MetalStencilOperationKind::IncrementClamp;
            case SO::DecrementSaturation:  return MetalStencilOperationKind::DecrementClamp;
            case SO::Invert:               return MetalStencilOperationKind::Invert;
            case SO::Keep:
            default:                       return MetalStencilOperationKind::Keep;
        }
    }
}
