// SPDX-License-Identifier: MS-PL
#pragma once

namespace Microsoft::Xna::Framework::Graphics
{
    /** @brief Defines stencil buffer operations. */
    enum class StencilOperation
    {
        /** @brief Does not update the stencil buffer entry. */
        Keep,
        /** @brief Sets the stencil buffer entry to 0. */
        Zero,
        /** @brief Replaces the stencil buffer entry with the reference value. */
        Replace,
        /** @brief Increments the stencil buffer entry, wrapping to 0 if the new value exceeds the maximum. */
        Increment,
        /** @brief Decrements the stencil buffer entry, wrapping to the maximum value if the new value goes below 0. */
        Decrement,
        /** @brief Increments the stencil buffer entry, clamping to the maximum value. */
        IncrementSaturation,
        /** @brief Decrements the stencil buffer entry, clamping to 0. */
        DecrementSaturation,
        /** @brief Inverts the bits in the stencil buffer entry. */
        Invert,
    };
}
