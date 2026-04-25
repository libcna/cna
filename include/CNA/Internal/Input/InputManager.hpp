#pragma once

#include "Microsoft/Xna/Framework/Input/ButtonState.hpp"
#include "Microsoft/Xna/Framework/Input/KeyboardState.hpp"
#include "Microsoft/Xna/Framework/Input/MouseState.hpp"
#include "Microsoft/Xna/Framework/Input/Touch/TouchCollection.hpp"
#include "Microsoft/Xna/Framework/Input/Touch/TouchLocationState.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"

namespace CNA::Internal::Input {
    /**
     * @brief Internal identification of supported mouse buttons.
     *
     * @note Status: IMPLEMENTED
     */
    enum class MouseButton {
        Left,
        Right,
        Middle,
    };

    /**
     * @brief Internal CNA input state manager.
     *
     * Keeps current input state and provides snapshots for the public
     * XNA-like API.
     *
     * Currently supports Mouse, basic Keyboard and basic TouchPanel state.
     *
     * @note Status: PARTIAL
     */
    class InputManager {
    public:
        /**
         * @brief Updates mouse cursor position.
         */
        static void SetMousePosition(int x, int y);

        /**
         * @brief Updates selected mouse button state.
         */
        static void SetMouseButtonState(
            MouseButton button,
            Microsoft::Xna::Framework::Input::ButtonState state
        );

        /**
         * @brief Adds mouse wheel delta to internal state.
         */
        static void AddScrollWheelDelta(int delta);

        /**
         * @brief Sets pressed/released state for one keyboard key.
         */
        static void SetKeyState(Microsoft::Xna::Framework::Input::Keys key, bool pressed);

        /**
         * @brief Updates one touch point in the internal touch state.
         */
        static void SetTouchState(
            int touchId,
            Microsoft::Xna::Framework::Input::Touch::TouchLocationState state,
            const Microsoft::Xna::Framework::Vector2& position
        );

        /**
         * @brief Returns a snapshot of current mouse state.
         */
        static Microsoft::Xna::Framework::Input::MouseState GetMouseState();

        /**
         * @brief Returns a snapshot of current keyboard state.
         */
        static Microsoft::Xna::Framework::Input::KeyboardState GetKeyboardState();

        /**
         * @brief Returns a snapshot of current touch state.
         */
        static Microsoft::Xna::Framework::Input::Touch::TouchCollection GetTouchState();
    };
}
