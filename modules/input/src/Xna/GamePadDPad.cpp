// SPDX-License-Identifier: MS-PL
#include "Microsoft/Xna/Framework/Input/GamePadDPad.hpp"

#include <string>
#include <utility>

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

    GamePadDPad GamePadDPad::FromButtonArray(std::initializer_list<Buttons> buttons)
    {
        Buttons mask = static_cast<Buttons>(0);
        for (Buttons b : buttons)
            mask |= b;

        return GamePadDPad(
            flagToState(mask, Buttons::DPadUp),
            flagToState(mask, Buttons::DPadDown),
            flagToState(mask, Buttons::DPadLeft),
            flagToState(mask, Buttons::DPadRight)
        );
    }

    std::string GamePadDPad::ToString() const
    {
        // Same shape as GamePadButtons, in the documented order Up, Down, Left, Right.
        std::string pressed;
        const std::pair<ButtonState, const char*> order[] = {
            {getUpProperty(), "Up"},
            {getDownProperty(), "Down"},
            {getLeftProperty(), "Left"},
            {getRightProperty(), "Right"},
        };
        for (const auto& [state, label] : order)
        {
            if (state != ButtonState::Pressed)
            {
                continue;
            }
            if (!pressed.empty())
            {
                pressed += ' ';
            }
            pressed += label;
        }
        if (pressed.empty())
        {
            pressed = "None";
        }
        return "{DPad:" + pressed + "}";
    }

    bool GamePadDPad::Equals(const std::any& obj) const
    {
        const GamePadDPad* other = std::any_cast<GamePadDPad>(&obj);
        return other != nullptr && Equals(*other);
    }

    bool GamePadDPad::Equals(const GamePadDPad& other) const
    {
        return down_ == other.down_ && left_ == other.left_ &&
               right_ == other.right_ && up_ == other.up_;
    }

    int GamePadDPad::GetHashCode() const
    {
        // XNA GetHashCode IL_0000..0010 + Helpers.SmartGetHashCode IL_0028..0048.
        const int hash = static_cast<int>(up_) ^ static_cast<int>(down_)
            ^ static_cast<int>(left_) ^ static_cast<int>(right_);
        return hash == 0 ? 0x7fffffff : hash;
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
