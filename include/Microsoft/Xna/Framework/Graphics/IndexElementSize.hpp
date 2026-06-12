// SPDX-License-Identifier: MS-PL
#pragma once

namespace Microsoft::Xna::Framework::Graphics
{
    /// Specifies the size of each index element in an index buffer.
    enum class IndexElementSize
    {
        /// 16-bit indices (std::uint16_t). Fully supported.
        SixteenBits = 16,
        /// 32-bit indices (std::uint32_t). Not yet implemented.
        ThirtyTwoBits = 32,
    };
}
