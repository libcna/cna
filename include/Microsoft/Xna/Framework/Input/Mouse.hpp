//
// Created by robertvokac on 5/25/25.
//

#pragma once
#include "MouseCursor.hpp"
#include "MouseState.hpp"


namespace Microsoft::Xna::Framework::Input
{
    class Mouse
    {
    public:
        /**
         * @brief Returns a snapshot of the current mouse state.
         *
         * @note Status: IMPLEMENTED
         */
        static MouseState GetState();

        /**
         * @brief Sets the mouse cursor.
         *
         * @note Status: PARTIAL
         */
        static void SetCursor(MouseCursor arrow);
    };
}


