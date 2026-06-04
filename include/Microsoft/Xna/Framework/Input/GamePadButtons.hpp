#pragma once

#include "Microsoft/Xna/Framework/Input/Buttons.hpp"
#include "Microsoft/Xna/Framework/Input/ButtonState.hpp"
#include "CNA/CNAHelper.hpp"

#include <initializer_list>

namespace Microsoft::Xna::Framework::Input
{
    struct GamePadState;

    /// Represents the state of the digital buttons on a gamepad.
    struct GamePadButtons
    {
        [[nodiscard]] ButtonState getAProperty() const;
        [[nodiscard]] ButtonState getBProperty() const;
        [[nodiscard]] ButtonState getBackProperty() const;
        [[nodiscard]] ButtonState getXProperty() const;
        [[nodiscard]] ButtonState getYProperty() const;
        [[nodiscard]] ButtonState getStartProperty() const;
        [[nodiscard]] ButtonState getLeftShoulderProperty() const;
        [[nodiscard]] ButtonState getLeftStickProperty() const;
        [[nodiscard]] ButtonState getRightShoulderProperty() const;
        [[nodiscard]] ButtonState getRightStickProperty() const;
        [[nodiscard]] ButtonState getBigButtonProperty() const;

        /// Constructs with no buttons pressed.
        GamePadButtons();

        /// Constructs from a combined Buttons flags value.
        explicit GamePadButtons(Buttons buttons);

        [[nodiscard]] bool Equals(const GamePadButtons& other) const;
        [[nodiscard]] int GetHashCode() const;

        friend bool operator==(const GamePadButtons& left, const GamePadButtons& right);
        friend bool operator!=(const GamePadButtons& left, const GamePadButtons& right);

        /// Derives a GamePadButtons from a list of Buttons flags values.
        static GamePadButtons FromButtons(std::initializer_list<Buttons> btns);

        /// Packed button flags. For internal library use only.
        NOXNA Buttons buttons_;

    private:
        [[nodiscard]] ButtonState ButtonStateFromFlag(Buttons flag) const;

        friend struct GamePadState;
    };
}
