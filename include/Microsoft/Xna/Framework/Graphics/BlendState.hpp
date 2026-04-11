#pragma once

namespace Microsoft::Xna::Framework::Graphics {

    /**
     * @brief Represents a sprite blending configuration.
     *
     * This is currently a minimal XNA-like subset.
     */
    class BlendState {
    public:
        /**
         * @brief Standard alpha blending state.
         */
        static BlendState AlphaBlend;
    };
}