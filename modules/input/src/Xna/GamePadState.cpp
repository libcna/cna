// SPDX-License-Identifier: MS-PL
#include "Microsoft/Xna/Framework/Input/GamePadState.hpp"
#include "Microsoft/Xna/Framework/Input/GamePad.hpp"
#include <cmath>

namespace Microsoft::Xna::Framework::Input
{
    GamePadState::GamePadState()
        : isConnected_(false),
          packetNumber_(0),
          buttons_(),
          dPad_(),
          thumbSticks_(),
          triggers_()
    {
    }

    GamePadState::GamePadState(const GamePadThumbSticks& thumbSticks,
                               const GamePadTriggers& triggers,
                               const GamePadButtons& buttons,
                               const GamePadDPad& dPad)
        : isConnected_(true),
          packetNumber_(0),
          buttons_(buttons),
          dPad_(dPad),
          thumbSticks_(thumbSticks),
          triggers_(triggers)
    {
        // XNA FillInternalState IL_02b0..0390 truncates analog samples to byte/short
        // before IsButtonDown applies integer dead zones. The public Buttons value stays
        // unchanged (four-argument ctor IL_001c..0021), unlike FNA's packed virtual flags.
        if (std::trunc(triggers_.getLeftProperty() * 255.0f) > 30.0f)
            virtualButtons_ |= Buttons::LeftTrigger;
        if (std::trunc(triggers_.getRightProperty() * 255.0f) > 30.0f)
            virtualButtons_ |= Buttons::RightTrigger;

        virtualButtons_ |= StickToButtons(
            thumbSticks_.getLeftProperty(),
            Buttons::LeftThumbstickLeft, Buttons::LeftThumbstickRight,
            Buttons::LeftThumbstickUp,   Buttons::LeftThumbstickDown,
            7849.0f);
        virtualButtons_ |= StickToButtons(
            thumbSticks_.getRightProperty(),
            Buttons::RightThumbstickLeft, Buttons::RightThumbstickRight,
            Buttons::RightThumbstickUp,   Buttons::RightThumbstickDown,
            8689.0f);
    }

    GamePadState::GamePadState(const Microsoft::Xna::Framework::Vector2& leftThumbStick,
                               const Microsoft::Xna::Framework::Vector2& rightThumbStick,
                               float leftTrigger,
                               float rightTrigger,
                               std::initializer_list<Buttons> buttons)
        : GamePadState(
              GamePadThumbSticks(leftThumbStick, rightThumbStick),
              GamePadTriggers(leftTrigger, rightTrigger),
              GamePadButtons::FromButtonArray(buttons),
              GamePadDPad::FromButtonArray(buttons))
    {
    }

    bool GamePadState::getIsConnectedProperty() const { return isConnected_; }
    int  GamePadState::getPacketNumberProperty() const { return packetNumber_; }
    void GamePadState::setPacketNumberProperty(int value) { packetNumber_ = value; }

    const GamePadButtons&    GamePadState::getButtonsProperty()     const { return buttons_; }
    const GamePadDPad&       GamePadState::getDPadProperty()        const { return dPad_; }
    const GamePadThumbSticks& GamePadState::getThumbSticksProperty() const { return thumbSticks_; }
    const GamePadTriggers&   GamePadState::getTriggersProperty()    const { return triggers_; }

    bool GamePadState::IsButtonDown(Buttons button) const
    {
        // XNA GamePadState.FillInternalState IL_0207..02af takes directions from DPad,
        // not GamePadButtons. IsButtonDown IL_0000..0016 reads those native button bits.
        Buttons flags = buttons_.buttons_ | virtualButtons_;
        if (dPad_.getUpProperty() == ButtonState::Pressed) flags |= Buttons::DPadUp;
        if (dPad_.getDownProperty() == ButtonState::Pressed) flags |= Buttons::DPadDown;
        if (dPad_.getLeftProperty() == ButtonState::Pressed) flags |= Buttons::DPadLeft;
        if (dPad_.getRightProperty() == ButtonState::Pressed) flags |= Buttons::DPadRight;
        return (flags & button) == button;
    }

    bool GamePadState::IsButtonUp(Buttons button) const
    {
        return !IsButtonDown(button);
    }

    Buttons GamePadState::StickToButtons(const Microsoft::Xna::Framework::Vector2& stick,
                                         Buttons left, Buttons right,
                                         Buttons up, Buttons down,
                                         float deadZoneSize)
    {
        Buttons b = static_cast<Buttons>(0);
        const float x = std::trunc(stick.X * 32767.0f);
        const float y = std::trunc(stick.Y * 32767.0f);
        if (x >  deadZoneSize) b |= right;
        if (x < -deadZoneSize) b |= left;
        if (y >  deadZoneSize) b |= up;
        if (y < -deadZoneSize) b |= down;
        return b;
    }

    bool GamePadState::Equals(const std::any& obj) const
    {
        const GamePadState* other = std::any_cast<GamePadState>(&obj);
        return other != nullptr && Equals(*other);
    }

    bool GamePadState::Equals(const GamePadState& other) const
    {
        return isConnected_ == other.isConnected_ &&
               packetNumber_ == other.packetNumber_ &&
               buttons_ == other.buttons_ &&
               dPad_ == other.dPad_ &&
               thumbSticks_ == other.thumbSticks_ &&
               triggers_ == other.triggers_;
    }

    int GamePadState::GetHashCode() const
    {
        // XNA GamePadState.GetHashCode IL_0000..005f XORs all six public value fields.
        return thumbSticks_.GetHashCode() ^ triggers_.GetHashCode() ^ buttons_.GetHashCode()
            ^ static_cast<int>(isConnected_) ^ dPad_.GetHashCode() ^ packetNumber_;
    }

    std::string GamePadState::ToString() const
    {
        // XNA 4.0's GamePadState.ToString IL overrides ValueType's and formats
        // "{{IsConnected:{0}}}" with the Boolean's "True"/"False"; FNA leaves the type name.
        return isConnected_ ? "{IsConnected:True}" : "{IsConnected:False}";
    }

    bool operator==(const GamePadState& left, const GamePadState& right)
    {
        return left.Equals(right);
    }

    bool operator!=(const GamePadState& left, const GamePadState& right)
    {
        return !(left == right);
    }
}
