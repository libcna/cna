#pragma once

#include "Microsoft/Xna/Framework/Input/Buttons.hpp"
#include "Microsoft/Xna/Framework/Input/GamePadButtons.hpp"
#include "Microsoft/Xna/Framework/Input/GamePadDPad.hpp"
#include "Microsoft/Xna/Framework/Input/GamePadThumbSticks.hpp"
#include "Microsoft/Xna/Framework/Input/GamePadTriggers.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"

#include <initializer_list>
#include <string>

namespace Microsoft::Xna::Framework::Input
{
    /// Represents the current state of a gamepad controller.
    struct GamePadState
    {
        /// Gets whether the controller is connected.
        [[nodiscard]] bool getIsConnectedProperty() const;

        /// Gets the packet number associated with this state.
        [[nodiscard]] int getPacketNumberProperty() const;

        /// Gets the digital button states.
        [[nodiscard]] const GamePadButtons& getButtonsProperty() const;

        /// Gets the directional pad state.
        [[nodiscard]] const GamePadDPad& getDPadProperty() const;

        /// Gets the thumbstick positions.
        [[nodiscard]] const GamePadThumbSticks& getThumbSticksProperty() const;

        /// Gets the trigger positions.
        [[nodiscard]] const GamePadTriggers& getTriggersProperty() const;

        /// Constructs a disconnected state (all values at rest).
        GamePadState();

        /// Constructs a fully specified connected state.
        GamePadState(const GamePadThumbSticks& thumbSticks,
                     const GamePadTriggers& triggers,
                     const GamePadButtons& buttons,
                     const GamePadDPad& dPad);

        /// Constructs from raw stick, trigger, and button flag values.
        GamePadState(const Microsoft::Xna::Framework::Vector2& leftThumbStick,
                     const Microsoft::Xna::Framework::Vector2& rightThumbStick,
                     float leftTrigger,
                     float rightTrigger,
                     std::initializer_list<Buttons> buttons);

        /// Returns true if the specified button(s) are pressed.
        [[nodiscard]] bool IsButtonDown(Buttons button) const;

        /// Returns true if the specified button(s) are not pressed.
        [[nodiscard]] bool IsButtonUp(Buttons button) const;

        [[nodiscard]] bool Equals(const GamePadState& other) const;
        [[nodiscard]] int GetHashCode() const;
        [[nodiscard]] std::string ToString() const;

        friend bool operator==(const GamePadState& left, const GamePadState& right);
        friend bool operator!=(const GamePadState& left, const GamePadState& right);

    private:
        bool isConnected_;
        int packetNumber_;
        GamePadButtons buttons_;
        GamePadDPad dPad_;
        GamePadThumbSticks thumbSticks_;
        GamePadTriggers triggers_;

        static Buttons StickToButtons(const Microsoft::Xna::Framework::Vector2& stick,
                                      Buttons left, Buttons right,
                                      Buttons up, Buttons down,
                                      float deadZoneSize);
    };
}
