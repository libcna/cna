// SPDX-License-Identifier: MS-PL
#pragma once

#include "Microsoft/Xna/Framework/Graphics/StencilOperation.hpp"

// plans/plan_metal.md METAL-19: same reasoning as MetalCompareFunction.hpp -- switches on the real
// `StencilOperation` enumerator names instead of raw int literals, so a future reordering of the
// enum cannot silently desync this table. XNA's Increment/Decrement wrap
// (D3DSTENCILOP_INCR/DECR); the *Saturation variants clamp (D3DSTENCILOP_INCRSAT/DECRSAT) --
// confirmed against VulkanRenderer's own already-tested ToVkStencilOp.
namespace CNA::Internal::Renderers::Metal
{
    /** @brief Renderer-neutral Metal stencil-operation classifications. */
    enum class MetalStencilOperationKind
    {
        /** @brief Keeps the existing stencil value. */
        Keep,
        /** @brief Writes zero. */
        Zero,
        /** @brief Writes the stencil reference value. */
        Replace,
        /** @brief Increments and wraps on overflow. */
        IncrementWrap,
        /** @brief Decrements and wraps on underflow. */
        DecrementWrap,
        /** @brief Increments and clamps at the maximum. */
        IncrementClamp,
        /** @brief Decrements and clamps at zero. */
        DecrementClamp,
        /** @brief Bitwise-inverts the existing stencil value. */
        Invert
    };

    /**
     * @brief Translates an XNA stencil-operation ordinal to its Metal classification.
     *
     * @param xnaOp Raw Microsoft.Xna.Framework.Graphics.StencilOperation ordinal.
     * @return The corresponding renderer-neutral Metal stencil operation.
     */
    [[nodiscard]] inline MetalStencilOperationKind DescribeMetalStencilOperation(int xnaOp)
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
