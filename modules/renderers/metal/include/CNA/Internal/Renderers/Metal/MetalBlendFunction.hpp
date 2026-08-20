// SPDX-License-Identifier: MS-PL
#pragma once

#include "Microsoft/Xna/Framework/Graphics/BlendFunction.hpp"

// plans/plan_metal.md METAL-19: same reasoning as MetalCompareFunction.hpp -- switches on the real
// `BlendFunction` enumerator names instead of raw int literals. Mirrors EasyGL's
// ToEasyGLBlendEquation / Vulkan's ToVkBlendOp exactly.
namespace CNA::Internal::Renderers::Metal
{
    /** @brief Renderer-neutral Metal blend-operation classifications. */
    enum class MetalBlendOperationKind
    {
        /** @brief Adds the weighted source and destination. */
        Add,
        /** @brief Subtracts the destination from the source. */
        Subtract,
        /** @brief Subtracts the source from the destination. */
        ReverseSubtract,
        /** @brief Selects the component-wise maximum. */
        Max,
        /** @brief Selects the component-wise minimum. */
        Min
    };

    /**
     * @brief Translates an XNA blend-operation ordinal to its Metal classification.
     *
     * @param xnaBlendFunc Raw Microsoft.Xna.Framework.Graphics.BlendFunction ordinal.
     * @return The corresponding renderer-neutral Metal blend operation.
     */
    [[nodiscard]] inline MetalBlendOperationKind DescribeMetalBlendOperation(int xnaBlendFunc)
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
