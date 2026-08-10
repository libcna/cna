// SPDX-License-Identifier: MS-PL
#pragma once

namespace Microsoft::Xna::Framework::Graphics
{
    /** @brief Defines a function for color blending. */
    enum class BlendFunction
    {
        /** @brief Adds destination to source: (srcColor * srcBlend) + (destColor * destBlend). */
        Add,
        /** @brief Subtracts destination from source: (srcColor * srcBlend) - (destColor * destBlend). */
        Subtract,
        /** @brief Subtracts source from destination: (destColor * destBlend) - (srcColor * srcBlend). */
        ReverseSubtract,
        /** @brief Returns the maximum of source and destination: max((srcColor * srcBlend), (destColor * destBlend)). */
        Max,
        /** @brief Returns the minimum of source and destination: min((srcColor * srcBlend), (destColor * destBlend)). */
        Min,
    };
}
