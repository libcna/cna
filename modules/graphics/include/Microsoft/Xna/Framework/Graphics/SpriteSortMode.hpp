// SPDX-License-Identifier: MS-PL
#pragma once

namespace Microsoft::Xna::Framework::Graphics
{
    /** @brief Defines sprite sort rendering options for SpriteBatch::Begin. */
    enum class SpriteSortMode
    {
        /** @brief All sprites are drawn when SpriteBatch::End is called, in draw-call order. Depth is ignored. */
        Deferred,

        /** @brief Each sprite is drawn with an individual draw call rather than at SpriteBatch::End. Depth is ignored. */
        Immediate,
        /** @brief Same as Deferred, except sprites are sorted by texture prior to drawing. Depth is ignored. */
        Texture,
        /** @brief Same as Deferred, except sprites are sorted by depth in back-to-front order prior to drawing. */
        BackToFront,
        /** @brief Same as Deferred, except sprites are sorted by depth in front-to-back order prior to drawing. */
        FrontToBack,
    };
}
