// SPDX-License-Identifier: MS-PL
#include "Microsoft/Xna/Framework/Input/GamePadButtons.hpp"

#include <string>
#include <utility>

namespace Microsoft::Xna::Framework::Input
{
    GamePadButtons::GamePadButtons()
        : buttons_(static_cast<Buttons>(0))
    {
    }

    GamePadButtons::GamePadButtons(Buttons buttons)
        // XNA GamePadButtons ctor IL_0000..00f6 extracts physical buttons only.
        // Keep CNA's additional physical buttons, but discard DPad and virtual analog flags.
        : buttons_(static_cast<Buttons>(static_cast<uint32_t>(buttons) & 0x001ffff0u))
    {
    }

    GamePadButtons GamePadButtons::FromButtonArray(std::initializer_list<Buttons> btns)
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

    std::string GamePadButtons::ToString() const
    {
        // Microsoft appends each pressed button's name in this order, separating with a single
        // space, and substitutes "None" for an empty list.
        static constexpr std::pair<Buttons, const char*> order[] = {
            {Buttons::A, "A"},
            {Buttons::B, "B"},
            {Buttons::X, "X"},
            {Buttons::Y, "Y"},
            {Buttons::LeftShoulder, "LeftShoulder"},
            {Buttons::RightShoulder, "RightShoulder"},
            {Buttons::LeftStick, "LeftStick"},
            {Buttons::RightStick, "RightStick"},
            {Buttons::Start, "Start"},
            {Buttons::Back, "Back"},
            {Buttons::BigButton, "BigButton"},
        };

        std::string pressed;
        for (const auto& [flag, label] : order)
        {
            if (ButtonStateFromFlag(flag) != ButtonState::Pressed)
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
        return "{Buttons:" + pressed + "}";
    }

    bool GamePadButtons::Equals(const std::any& obj) const
    {
        const GamePadButtons* other = std::any_cast<GamePadButtons>(&obj);
        return other != nullptr && Equals(*other);
    }

    bool GamePadButtons::Equals(const GamePadButtons& other) const
    {
        return buttons_ == other.buttons_;
    }

    int GamePadButtons::GetHashCode() const
    {
        // XNA GetHashCode IL_0000..0010 calls Helpers.SmartGetHashCode IL_0028..0048.
        // XOR XNA's eleven int32 ButtonState fields, not CNA's packed mask.
        // CNA-only physical flags have no XNA field and may share a hash.
        const int hash = static_cast<int>(getAProperty()) ^ static_cast<int>(getBProperty())
            ^ static_cast<int>(getXProperty()) ^ static_cast<int>(getYProperty())
            ^ static_cast<int>(getStartProperty()) ^ static_cast<int>(getBackProperty())
            ^ static_cast<int>(getLeftStickProperty()) ^ static_cast<int>(getRightStickProperty())
            ^ static_cast<int>(getLeftShoulderProperty()) ^ static_cast<int>(getRightShoulderProperty())
            ^ static_cast<int>(getBigButtonProperty());
        return hash == 0 ? 0x7fffffff : hash;
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
