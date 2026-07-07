// SPDX-License-Identifier: MS-PL
#include "Microsoft/Xna/Framework/Input/GamePadState.hpp"
#include "Microsoft/Xna/Framework/Input/GamePad.hpp"

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
        if (triggers_.getLeftProperty()  > GamePad::TriggerThreshold)
            buttons_.buttons_ |= Buttons::LeftTrigger;
        if (triggers_.getRightProperty() > GamePad::TriggerThreshold)
            buttons_.buttons_ |= Buttons::RightTrigger;

        buttons_.buttons_ |= StickToButtons(
            thumbSticks_.getLeftProperty(),
            Buttons::LeftThumbstickLeft, Buttons::LeftThumbstickRight,
            Buttons::LeftThumbstickUp,   Buttons::LeftThumbstickDown,
            GamePad::LeftDeadZone);
        buttons_.buttons_ |= StickToButtons(
            thumbSticks_.getRightProperty(),
            Buttons::RightThumbstickLeft, Buttons::RightThumbstickRight,
            Buttons::RightThumbstickUp,   Buttons::RightThumbstickDown,
            GamePad::RightDeadZone);
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
        return (buttons_.buttons_ & button) == button;
    }

    bool GamePadState::IsButtonUp(Buttons button) const
    {
        return (buttons_.buttons_ & button) != button;
    }

    Buttons GamePadState::StickToButtons(const Microsoft::Xna::Framework::Vector2& stick,
                                         Buttons left, Buttons right,
                                         Buttons up, Buttons down,
                                         float deadZoneSize)
    {
        Buttons b = static_cast<Buttons>(0);
        if (stick.X >  deadZoneSize) b |= right;
        if (stick.X < -deadZoneSize) b |= left;
        if (stick.Y >  deadZoneSize) b |= up;
        if (stick.Y < -deadZoneSize) b |= down;
        return b;
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
        // Unsigned wraparound avoids signed-overflow UB in packetNumber_ * 31 (INPUT-BUILD-006).
        return static_cast<int>(static_cast<unsigned>(buttons_.GetHashCode())
                                ^ (static_cast<unsigned>(packetNumber_) * 31u));
    }

    std::string GamePadState::ToString() const
    {
        // FNA's GamePadState.ToString() is `return base.ToString();`; GamePadState never
        // overrides ToString, so ValueType's default applies — the fully-qualified type
        // name, regardless of field values.
        return "Microsoft.Xna.Framework.Input.GamePadState";
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
