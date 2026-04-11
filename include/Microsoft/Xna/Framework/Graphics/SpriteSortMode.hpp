//
// Created by robertvokac on 5/24/25.
//

#pragma once
/**
 * @brief Specifies how sprites are sorted during a SpriteBatch.
 * Only Deferred is currently implemented as a no-op batching mode.
 */
namespace Microsoft::Xna::Framework::Graphics {
    enum class SpriteSortMode {
        Deferred,

        Immediate,
        Texture,
        BackToFront,
        FrontToBack,
    };
}

