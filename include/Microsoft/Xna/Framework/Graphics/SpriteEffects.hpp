// SPDX-License-Identifier: MS-PL
#pragma once

namespace Microsoft::Xna::Framework::Graphics
{
    /// Defines sprite visual options for mirroring during rendering.
    enum class SpriteEffects
    {
        /// No mirroring is applied.
        None = 0,
        /// The sprite is flipped horizontally.
        FlipHorizontally = 1,
        /// The sprite is flipped vertically.
        FlipVertically = 2
    };
}
