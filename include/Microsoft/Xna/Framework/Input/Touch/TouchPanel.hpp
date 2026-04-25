//
// Created by robertvokac on 5/24/25.
//
#pragma once
#include "TouchCollection.hpp"
#include "TouchPanelCapabilities.hpp"

namespace Microsoft::Xna::Framework::Input::Touch {
    /**
     * @brief Provides access to touch input state.
     *
     * @note Status: PARTIAL
     */
    class TouchPanel {
    public:
        /**
         * @brief Creates a touch panel object.
         */
        TouchPanel();

        /**
         * @brief Returns touch panel capabilities.
         *
         * @note Status: PARTIAL
         */
        static TouchPanelCapabilities GetCapabilities();

        /**
         * @brief Returns current touch state snapshot.
         *
         * @note Status: PARTIAL
         */
        static TouchCollection GetState();
    };
}

