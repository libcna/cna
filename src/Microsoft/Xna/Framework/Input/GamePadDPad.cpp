#include "Microsoft/Xna/Framework/Input/GamePadDPad.hpp"

namespace Microsoft::Xna::Framework::Input
{
    namespace
    {
        ButtonState flagToState(Buttons buttons, Buttons flag)
        {
            return (buttons & flag) == flag ? ButtonState::Pressed : ButtonState::Released;
        }
    }

    GamePadDPad::GamePadDPad()
        : down_(ButtonState::Released),
          left_(ButtonState::Released),
          right_(ButtonState::Released),
          up_(ButtonState::Released)
    {
    }

    GamePadDPad::GamePadDPad(ButtonState upValue, ButtonState downValue,
                              ButtonState leftValue, ButtonState rightValue)
        : down_(downValue),
          left_(leftValue),
          right_(rightValue),
          up_(upValue)
    {
    }

    ButtonState GamePadDPad::getDownProperty() const  { return down_; }
    ButtonState GamePadDPad::getLeftProperty() const  { return left_; }
    ButtonState GamePadDPad::getRightProperty() const { return right_; }
    ButtonState GamePadDPad::getUpProperty() const    { return up_; }

    GamePadDPad GamePadDPad::FromButtons(Buttons buttons)
    {
        return GamePadDPad(
            flagToState(buttons, Buttons::DPadUp),
            flagToState(buttons, Buttons::DPadDown),
            flagToState(buttons, Buttons::DPadLeft),
            flagToState(buttons, Buttons::DPadRight)
        );
    }

    bool GamePadDPad::Equals(const GamePadDPad& other) const
    {
        return down_ == other.down_ && left_ == other.left_ &&
               right_ == other.right_ && up_ == other.up_;
    }

    int GamePadDPad::GetHashCode() const
    {
        return (down_  == ButtonState::Pressed ? 1 : 0) +
               (left_  == ButtonState::Pressed ? 2 : 0) +
               (right_ == ButtonState::Pressed ? 4 : 0) +
               (up_    == ButtonState::Pressed ? 8 : 0);
    }

    bool operator==(const GamePadDPad& left, const GamePadDPad& right)
    {
        return left.Equals(right);
    }

    bool operator!=(const GamePadDPad& left, const GamePadDPad& right)
    {
        return !(left == right);
    }
}
