// SPDX-License-Identifier: MS-PL
#pragma once

#include "Microsoft/Xna/Framework/Graphics/Blend.hpp"

// plans/plan_metal.md METAL-19: same reasoning as MetalCompareFunction.hpp -- switches on the real
// `Blend` enumerator names instead of raw int literals. Mirrors EasyGL's ToEasyGLBlendFactor /
// Vulkan's ToVkBlendFactor exactly, including their identical no-RGB/Alpha-channel-distinction
// choice for BlendFactor/InverseBlendFactor -- SourceColor/DestinationColor/BlendFactor as an
// *Alpha*-slot factor is not a combination real D3D9/XNA content legally produces, and every
// established CNA renderer already made this same simplifying choice, not just this one.
namespace CNA::Internal::Renderers::Metal
{
    /** @brief Renderer-neutral Metal blend-factor classifications. */
    enum class MetalBlendFactorKind
    {
        /** @brief Constant factor one. */
        One,
        /** @brief Constant factor zero. */
        Zero,
        /** @brief Source RGB components. */
        SourceColor,
        /** @brief One minus the source RGB components. */
        OneMinusSourceColor,
        /** @brief Source alpha component. */
        SourceAlpha,
        /** @brief One minus the source alpha component. */
        OneMinusSourceAlpha,
        /** @brief Destination RGB components. */
        DestinationColor,
        /** @brief One minus the destination RGB components. */
        OneMinusDestinationColor,
        /** @brief Destination alpha component. */
        DestinationAlpha,
        /** @brief One minus the destination alpha component. */
        OneMinusDestinationAlpha,
        /** @brief Constant blend color. */
        BlendColor,
        /** @brief One minus the constant blend color. */
        OneMinusBlendColor,
        /** @brief Saturated source-alpha factor. */
        SourceAlphaSaturated
    };

    /**
     * @brief Translates an XNA blend-factor ordinal to its Metal classification.
     *
     * @param xnaBlend Raw Microsoft.Xna.Framework.Graphics.Blend ordinal.
     * @return The corresponding renderer-neutral Metal blend factor.
     */
    [[nodiscard]] inline MetalBlendFactorKind DescribeMetalBlendFactor(int xnaBlend)
    {
        using B = Microsoft::Xna::Framework::Graphics::Blend;
        switch (static_cast<B>(xnaBlend)) {
            case B::Zero:                    return MetalBlendFactorKind::Zero;
            case B::SourceColor:             return MetalBlendFactorKind::SourceColor;
            case B::InverseSourceColor:      return MetalBlendFactorKind::OneMinusSourceColor;
            case B::SourceAlpha:             return MetalBlendFactorKind::SourceAlpha;
            case B::InverseSourceAlpha:      return MetalBlendFactorKind::OneMinusSourceAlpha;
            case B::DestinationColor:        return MetalBlendFactorKind::DestinationColor;
            case B::InverseDestinationColor: return MetalBlendFactorKind::OneMinusDestinationColor;
            case B::DestinationAlpha:        return MetalBlendFactorKind::DestinationAlpha;
            case B::InverseDestinationAlpha: return MetalBlendFactorKind::OneMinusDestinationAlpha;
            case B::BlendFactor:             return MetalBlendFactorKind::BlendColor;
            case B::InverseBlendFactor:      return MetalBlendFactorKind::OneMinusBlendColor;
            case B::SourceAlphaSaturation:   return MetalBlendFactorKind::SourceAlphaSaturated;
            case B::One:
            default:                         return MetalBlendFactorKind::One;
        }
    }
}
