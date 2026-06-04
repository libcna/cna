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
              GamePadButtons::FromButtons(buttons),
              GamePadDPad::FromButtons(GamePadButtons::FromButtons(buttons).buttons_))
    {
    }

    bool GamePadState::getIsConnectedProperty() const { return isConnected_; }
    int  GamePadState::getPacketNumberProperty() const { return packetNumber_; }

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
        return buttons_.GetHashCode() ^ (packetNumber_ * 31);
    }

    std::string GamePadState::ToString() const
    {
        return "[GamePadState IsConnected=" + std::string(isConnected_ ? "true" : "false") + "]";
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
