// SPDX-License-Identifier: MS-PL
//
// Created by robertvokac on 5/24/25.
//

#pragma once

namespace Microsoft::Xna::Framework::Graphics
{
    /// Defines the sprite-batch drawing order for SpriteBatch.Begin.
    enum class SpriteSortMode
    {
        /// Sprites are not drawn until SpriteBatch.End is called; textures are not sorted.
        Deferred,

        /// Each sprite is drawn with a separate draw call and is rendered immediately.
        Immediate,
        /// Sprites are sorted by texture prior to drawing.
        Texture,
        /// Sprites are sorted by depth in back-to-front order prior to drawing.
        BackToFront,
        /// Sprites are sorted by depth in front-to-back order prior to drawing.
        FrontToBack,
    };
}
