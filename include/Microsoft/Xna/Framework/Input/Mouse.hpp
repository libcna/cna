//
// Created by robertvokac on 5/25/25.
//

#pragma once
#include "MouseCursor.hpp"
#include "MouseState.hpp"


namespace Microsoft::Xna::Framework::Input {
class Mouse {
public:
    /**
     * @brief Vrátí snapshot aktuálního stavu myši.
     *
     * @note Status: IMPLEMENTED
     */
    static MouseState GetState();

    /**
     * @brief Nastaví kurzor myši.
     *
     * @note Status: PARTIAL
     */
    static void SetCursor(MouseCursor arrow);
};
}


