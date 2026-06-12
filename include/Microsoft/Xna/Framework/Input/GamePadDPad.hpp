// SPDX-License-Identifier: MS-PL
#pragma once

#include "Microsoft/Xna/Framework/Input/Buttons.hpp"
#include "Microsoft/Xna/Framework/Input/ButtonState.hpp"

namespace Microsoft::Xna::Framework::Input
{
    struct GamePadState;

    /// Represents the state of the directional pad on a gamepad.
    struct GamePadDPad
    {
        /// Gets the state of the down button.
        [[nodiscard]] ButtonState getDownProperty() const;
        /// Gets the state of the left button.
        [[nodiscard]] ButtonState getLeftProperty() const;
        /// Gets the state of the right button.
        [[nodiscard]] ButtonState getRightProperty() const;
        /// Gets the state of the up button.
        [[nodiscard]] ButtonState getUpProperty() const;

        /// Constructs a GamePadDPad with all directions released.
        GamePadDPad();

        /// Constructs a GamePadDPad with explicit direction states.
        GamePadDPad(ButtonState upValue, ButtonState downValue,
                    ButtonState leftValue, ButtonState rightValue);

        /// Derives DPad state from a combined Buttons flags value.
        static GamePadDPad FromButtons(Buttons buttons);

        [[nodiscard]] bool Equals(const GamePadDPad& other) const;
        [[nodiscard]] int GetHashCode() const;

        friend bool operator==(const GamePadDPad& left, const GamePadDPad& right);
        friend bool operator!=(const GamePadDPad& left, const GamePadDPad& right);

    private:
        ButtonState down_;
        ButtonState left_;
        ButtonState right_;
        ButtonState up_;
    };
}
