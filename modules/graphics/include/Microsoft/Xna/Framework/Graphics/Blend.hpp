// SPDX-License-Identifier: MS-PL
#pragma once

namespace Microsoft::Xna::Framework::Graphics
{
    /** @brief Defines a blend mode. */
    enum class Blend
    {
        /** @brief Each component of the color is multiplied by {1, 1, 1, 1}. */
        One,
        /** @brief Each component of the color is multiplied by {0, 0, 0, 0}. */
        Zero,
        /** @brief Each component is multiplied by the source color: {Rs, Gs, Bs, As}. */
        SourceColor,
        /** @brief Each component is multiplied by the inverse of the source color: {1-Rs, 1-Gs, 1-Bs, 1-As}. */
        InverseSourceColor,
        /** @brief Each component is multiplied by the source alpha: {As, As, As, As}. */
        SourceAlpha,
        /** @brief Each component is multiplied by the inverse of the source alpha: {1-As, 1-As, 1-As, 1-As}. */
        InverseSourceAlpha,
        /** @brief Each component is multiplied by the destination color: {Rd, Gd, Bd, Ad}. */
        DestinationColor,
        /** @brief Each component is multiplied by the inverse of the destination color: {1-Rd, 1-Gd, 1-Bd, 1-Ad}. */
        InverseDestinationColor,
        /** @brief Each component is multiplied by the destination alpha: {Ad, Ad, Ad, Ad}. */
        DestinationAlpha,
        /** @brief Each component is multiplied by the inverse of the destination alpha: {1-Ad, 1-Ad, 1-Ad, 1-Ad}. */
        InverseDestinationAlpha,
        /** @brief Each component is multiplied by a constant set in GraphicsDevice::BlendFactor. */
        BlendFactor,
        /** @brief Each component is multiplied by the inverse of the GraphicsDevice::BlendFactor constant. */
        InverseBlendFactor,
        /** @brief RGB is multiplied by min(As, 1-Ad); alpha is multiplied by 1. */
        SourceAlphaSaturation,
    };
}
