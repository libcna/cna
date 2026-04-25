//
// Created by robertvokac on 5/26/25.
//

#pragma once
#include "KeyboardState.hpp"

namespace Microsoft::Xna::Framework::Input {
    /**
     * @brief Provides keyboard input snapshots.
     *
     * @note Status: PARTIAL
     */
    class Keyboard {
    public:
        /**
         * @brief Returns a snapshot of current keyboard state.
         *
         * @note Status: IMPLEMENTED
         */
        static KeyboardState GetState();
    };
}

