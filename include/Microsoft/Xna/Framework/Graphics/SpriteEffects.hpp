// SPDX-License-Identifier: MS-PL
#pragma once

namespace Microsoft::Xna::Framework::Graphics
{
    /** @brief Defines sprite visual options for mirroring during rendering. */
    enum class SpriteEffects
    {
        /** @brief No options specified. */
        None = 0,
        /** @brief Render the sprite reversed along the X axis. */
        FlipHorizontally = 1,
        /** @brief Render the sprite reversed along the Y axis. */
        FlipVertically = 2
    };
}
