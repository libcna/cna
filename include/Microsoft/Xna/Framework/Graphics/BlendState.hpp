#pragma once

namespace Microsoft::Xna::Framework::Graphics {

    /**
     * @brief Represents a sprite blending configuration.
     *
     * This is currently only a lightweight placeholder for the subset
     * needed by the game.
     */
    class BlendState {
    public:
        /**
         * @brief Standard alpha blending state.
         */
        static BlendState AlphaBlend;
    };
}