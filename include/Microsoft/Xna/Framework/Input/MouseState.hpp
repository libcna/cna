// SPDX-License-Identifier: MS-PL
#pragma once

#include "Microsoft/Xna/Framework/Input/ButtonState.hpp"

#include <string>

namespace Microsoft::Xna::Framework::Input
{
    /**
     * @brief Represents a mouse state with cursor position and button press information.
     */
    struct MouseState
    {
        /**
         * @brief Gets the horizontal position of the cursor.
         * @return The x coordinate.
         */
        [[nodiscard]] int getXProperty() const;

        /**
         * @brief Gets the vertical position of the cursor.
         * @return The y coordinate.
         */
        [[nodiscard]] int getYProperty() const;

        /**
         * @brief Gets the state of the left mouse button.
         * @return The left button state.
         */
        [[nodiscard]] ButtonState getLeftButtonProperty() const;

        /**
         * @brief Gets the state of the right mouse button.
         * @return The right button state.
         */
        [[nodiscard]] ButtonState getRightButtonProperty() const;

        /**
         * @brief Gets the state of the middle mouse button.
         * @return The middle button state.
         */
        [[nodiscard]] ButtonState getMiddleButtonProperty() const;

        /**
         * @brief Gets the state of XButton1.
         * @return The XButton1 state.
         */
        [[nodiscard]] ButtonState getXButton1Property() const;

        /**
         * @brief Gets the state of XButton2.
         * @return The XButton2 state.
         */
        [[nodiscard]] ButtonState getXButton2Property() const;

        /**
         * @brief Returns the cumulative scroll wheel value since the game started.
         * @return The scroll wheel value.
         */
        [[nodiscard]] int getScrollWheelValueProperty() const;

        /** @brief Constructs a MouseState with all values at rest. */
        MouseState();

        /**
         * @brief Constructs a MouseState with all pointer and button values specified.
         * @param x Horizontal position of the cursor.
         * @param y Vertical position of the cursor.
         * @param scrollWheel The scroll wheel value.
         * @param leftButton The left mouse button state.
         * @param middleButton The middle mouse button state.
         * @param rightButton The right mouse button state.
         * @param xButton1 The XButton1 state.
         * @param xButton2 The XButton2 state.
         */
        MouseState(int x, int y, int scrollWheel,
                   ButtonState leftButton, ButtonState middleButton,
                   ButtonState rightButton,
                   ButtonState xButton1, ButtonState xButton2);

        /**
         * @brief Compares this instance with another for equality.
         * @param other The other MouseState to compare.
         * @return True if equal; false otherwise.
         */
        [[nodiscard]] bool Equals(const MouseState& other) const;

        /**
         * @brief Gets the hash code for this MouseState instance.
         * @return Hash code of the object.
         */
        [[nodiscard]] int GetHashCode() const;

        /**
         * @brief Returns a string describing the mouse state.
         * @return The string representation.
         */
        [[nodiscard]] std::string ToString() const;

        /**
         * @brief Compares two MouseState instances for equality.
         * @param left MouseState on the left of the equal sign.
         * @param right MouseState on the right of the equal sign.
         * @return True if equal; false otherwise.
         */
        friend bool operator==(const MouseState& left, const MouseState& right);

        /**
         * @brief Compares two MouseState instances for inequality.
         * @param left MouseState on the left of the equal sign.
         * @param right MouseState on the right of the equal sign.
         * @return True if not equal; false otherwise.
         */
        friend bool operator!=(const MouseState& left, const MouseState& right);

    private:
        int x_;
        int y_;
        ButtonState leftButton_;
        ButtonState rightButton_;
        ButtonState middleButton_;
        ButtonState xButton1_;
        ButtonState xButton2_;
        int scrollWheelValue_;
    };
}
