// SPDX-License-Identifier: MS-PL
#include "Microsoft/Xna/Framework/Input/GamePadButtons.hpp"

namespace Microsoft::Xna::Framework::Input
{
    GamePadButtons::GamePadButtons()
        : buttons_(static_cast<Buttons>(0))
    {
    }

    GamePadButtons::GamePadButtons(Buttons buttons)
        : buttons_(buttons)
    {
    }

    GamePadButtons GamePadButtons::FromButtons(std::initializer_list<Buttons> btns)
    {
        Buttons mask = static_cast<Buttons>(0);
        for (Buttons b : btns)
            mask |= b;
        return GamePadButtons(mask);
    }

    ButtonState GamePadButtons::ButtonStateFromFlag(Buttons flag) const
    {
        return (buttons_ & flag) == flag ? ButtonState::Pressed : ButtonState::Released;
    }

    ButtonState GamePadButtons::getAProperty() const             { return ButtonStateFromFlag(Buttons::A); }
    ButtonState GamePadButtons::getBProperty() const             { return ButtonStateFromFlag(Buttons::B); }
    ButtonState GamePadButtons::getBackProperty() const          { return ButtonStateFromFlag(Buttons::Back); }
    ButtonState GamePadButtons::getXProperty() const             { return ButtonStateFromFlag(Buttons::X); }
    ButtonState GamePadButtons::getYProperty() const             { return ButtonStateFromFlag(Buttons::Y); }
    ButtonState GamePadButtons::getStartProperty() const         { return ButtonStateFromFlag(Buttons::Start); }
    ButtonState GamePadButtons::getLeftShoulderProperty() const  { return ButtonStateFromFlag(Buttons::LeftShoulder); }
    ButtonState GamePadButtons::getLeftStickProperty() const     { return ButtonStateFromFlag(Buttons::LeftStick); }
    ButtonState GamePadButtons::getRightShoulderProperty() const { return ButtonStateFromFlag(Buttons::RightShoulder); }
    ButtonState GamePadButtons::getRightStickProperty() const    { return ButtonStateFromFlag(Buttons::RightStick); }
    ButtonState GamePadButtons::getBigButtonProperty() const     { return ButtonStateFromFlag(Buttons::BigButton); }

    bool GamePadButtons::Equals(const GamePadButtons& other) const
    {
        return buttons_ == other.buttons_;
    }

    int GamePadButtons::GetHashCode() const
    {
        return static_cast<int>(static_cast<uint32_t>(buttons_));
    }

    bool operator==(const GamePadButtons& left, const GamePadButtons& right)
    {
        return left.Equals(right);
    }

    bool operator!=(const GamePadButtons& left, const GamePadButtons& right)
    {
        return !(left == right);
    }
}
