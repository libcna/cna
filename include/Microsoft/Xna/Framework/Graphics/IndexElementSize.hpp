// SPDX-License-Identifier: MS-PL
#pragma once

namespace Microsoft::Xna::Framework::Graphics
{
    /** @brief Defines the size of each index element in an IndexBuffer or DynamicIndexBuffer. */
    enum class IndexElementSize
    {
        /** @brief 16-bit short/ushort index value. */
        SixteenBits = 0,
        /** @brief 32-bit int/uint index value. */
        ThirtyTwoBits = 1,
    };
}
