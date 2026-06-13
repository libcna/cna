// SPDX-License-Identifier: MS-PL
#pragma once

#include "Microsoft/Xna/Framework/Input/Buttons.hpp"
#include "Microsoft/Xna/Framework/Input/ButtonState.hpp"

namespace Microsoft::Xna::Framework::Input
{
    struct GamePadState;

    /**
     * @brief Represents the state of the directional pad on a gamepad.
     */
    struct GamePadDPad
    {
        /**
         * @brief Gets the state of the down button.
         * @return The down button state.
         */
        [[nodiscard]] ButtonState getDownProperty() const;

        /**
         * @brief Gets the state of the left button.
         * @return The left button state.
         */
        [[nodiscard]] ButtonState getLeftProperty() const;

        /**
         * @brief Gets the state of the right button.
         * @return The right button state.
         */
        [[nodiscard]] ButtonState getRightProperty() const;

        /**
         * @brief Gets the state of the up button.
         * @return The up button state.
         */
        [[nodiscard]] ButtonState getUpProperty() const;

        /** @brief Constructs a GamePadDPad with all directions released. */
        GamePadDPad();

        /**
         * @brief Constructs a GamePadDPad with explicit direction states.
         * @param upValue The up direction state.
         * @param downValue The down direction state.
         * @param leftValue The left direction state.
         * @param rightValue The right direction state.
         */
        GamePadDPad(ButtonState upValue, ButtonState downValue,
                    ButtonState leftValue, ButtonState rightValue);

        /**
         * @brief Derives DPad state from a combined Buttons flags value.
         * @param buttons The combined button flags.
         * @return The resulting GamePadDPad.
         */
        static GamePadDPad FromButtons(Buttons buttons);

        /**
         * @brief Compares this instance with another for equality.
         * @param other The other GamePadDPad to compare.
         * @return True if equal; false otherwise.
         */
        [[nodiscard]] bool Equals(const GamePadDPad& other) const;

        /**
         * @brief Gets the hash code for this instance.
         * @return Hash code of the object.
         */
        [[nodiscard]] int GetHashCode() const;

        /**
         * @brief Compares two GamePadDPad instances for equality.
         * @param left The left-hand operand.
         * @param right The right-hand operand.
         * @return True if equal; false otherwise.
         */
        friend bool operator==(const GamePadDPad& left, const GamePadDPad& right);

        /**
         * @brief Compares two GamePadDPad instances for inequality.
         * @param left The left-hand operand.
         * @param right The right-hand operand.
         * @return True if not equal; false otherwise.
         */
        friend bool operator!=(const GamePadDPad& left, const GamePadDPad& right);

    private:
        ButtonState down_;
        ButtonState left_;
        ButtonState right_;
        ButtonState up_;
    };
}
