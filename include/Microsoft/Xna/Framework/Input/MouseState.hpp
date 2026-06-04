#pragma once

#include "Microsoft/Xna/Framework/Input/ButtonState.hpp"

#include <string>

namespace Microsoft::Xna::Framework::Input
{
    /// Represents a mouse state with cursor position and button press information.
    struct MouseState
    {
        [[nodiscard]] int getXProperty() const;
        [[nodiscard]] int getYProperty() const;
        [[nodiscard]] ButtonState getLeftButtonProperty() const;
        [[nodiscard]] ButtonState getRightButtonProperty() const;
        [[nodiscard]] ButtonState getMiddleButtonProperty() const;
        [[nodiscard]] ButtonState getXButton1Property() const;
        [[nodiscard]] ButtonState getXButton2Property() const;
        [[nodiscard]] int getScrollWheelValueProperty() const;

        MouseState();

        /// Constructs a MouseState with all pointer and button values.
        MouseState(int x, int y, int scrollWheel,
                   ButtonState leftButton, ButtonState middleButton,
                   ButtonState rightButton,
                   ButtonState xButton1, ButtonState xButton2);

        [[nodiscard]] bool Equals(const MouseState& other) const;
        [[nodiscard]] int GetHashCode() const;
        [[nodiscard]] std::string ToString() const;

        friend bool operator==(const MouseState& left, const MouseState& right);
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
