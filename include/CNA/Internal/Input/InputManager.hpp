#pragma once

#include "Microsoft/Xna/Framework/Input/ButtonState.hpp"
#include "Microsoft/Xna/Framework/Input/MouseState.hpp"

namespace CNA::Internal::Input {
    /**
     * @brief Interní identifikace podporovaných tlačítek myši.
     *
     * @note Status: IMPLEMENTED
     */
    enum class MouseButton {
        Left,
        Right,
        Middle,
    };

    /**
     * @brief Interní správce vstupního stavu CNA.
     *
     * Aktuálně udržuje pouze stav myši a poskytuje snapshot pro veřejné
     * XNA-like API (`Microsoft::Xna::Framework::Input::Mouse`).
     *
     * @note Status: PARTIAL (zatím implementována pouze myš)
     */
    class InputManager {
    public:
        /**
         * @brief Aktualizuje pozici kurzoru myši.
         */
        static void SetMousePosition(int x, int y);

        /**
         * @brief Aktualizuje stav vybraného tlačítka myši.
         */
        static void SetMouseButtonState(
            MouseButton button,
            Microsoft::Xna::Framework::Input::ButtonState state
        );

        /**
         * @brief Přičte delta hodnotu kolečka myši do interního stavu.
         */
        static void AddScrollWheelDelta(int delta);

        /**
         * @brief Vrátí snapshot aktuálního stavu myši.
         */
        static Microsoft::Xna::Framework::Input::MouseState GetMouseState();
    };
}
