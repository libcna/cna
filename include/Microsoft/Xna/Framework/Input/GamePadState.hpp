// SPDX-License-Identifier: MS-PL
#pragma once

#include "CNA/CNAHelper.hpp"
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
    /**
     * @brief Represents specific information about the state of a controller,
     *        including the current state of buttons and sticks.
     */
    struct GamePadState
    {
        /**
         * @brief Indicates whether the controller is connected.
         * @return True if connected; false otherwise.
         */
        [[nodiscard]] bool getIsConnectedProperty() const;

        /**
         * @brief Gets the packet number associated with this state.
         * @return The packet number.
         */
        [[nodiscard]] int getPacketNumberProperty() const;

        /**
         * @brief Sets the packet number. FNA declares `PacketNumber`'s setter `internal`
         * (platform-layer use only), not part of the public XNA API.
         * @param value The new packet number.
         */
        NOXNA void setPacketNumberProperty(int value);

        /**
         * @brief Returns a structure identifying which buttons on the controller are pressed.
         * @return The gamepad buttons state.
         */
        [[nodiscard]] const GamePadButtons& getButtonsProperty() const;

        /**
         * @brief Returns a structure identifying which directions of the directional pad are pressed.
         * @return The directional pad state.
         */
        [[nodiscard]] const GamePadDPad& getDPadProperty() const;

        /**
         * @brief Returns a structure indicating the position of the controller thumbsticks.
         * @return The thumbstick positions.
         */
        [[nodiscard]] const GamePadThumbSticks& getThumbSticksProperty() const;

        /**
         * @brief Returns a structure identifying the position of triggers on the controller.
         * @return The trigger positions.
         */
        [[nodiscard]] const GamePadTriggers& getTriggersProperty() const;

        /** @brief Constructs a disconnected state with all values at rest. */
        GamePadState();

        /**
         * @brief Constructs a fully specified connected gamepad state.
         * @param thumbSticks Initial thumbstick state.
         * @param triggers Initial trigger state.
         * @param buttons Initial button state.
         * @param dPad Initial directional pad state.
         */
        GamePadState(const GamePadThumbSticks& thumbSticks,
                     const GamePadTriggers& triggers,
                     const GamePadButtons& buttons,
                     const GamePadDPad& dPad);

        /**
         * @brief Constructs from raw stick, trigger, and button flag values.
         * @param leftThumbStick Left stick value. Each axis is clamped to [-1, 1].
         * @param rightThumbStick Right stick value. Each axis is clamped to [-1, 1].
         * @param leftTrigger Left trigger value, clamped to [0, 1].
         * @param rightTrigger Right trigger value, clamped to [0, 1].
         * @param buttons List of Buttons flags to initialize as pressed.
         */
        GamePadState(const Microsoft::Xna::Framework::Vector2& leftThumbStick,
                     const Microsoft::Xna::Framework::Vector2& rightThumbStick,
                     float leftTrigger,
                     float rightTrigger,
                     std::initializer_list<Buttons> buttons);

        /**
         * @brief Determines whether the specified button(s) are pressed.
         * @param button Buttons to query; can be combined with bitwise OR.
         * @return True if all specified buttons are pressed; false otherwise.
         */
        [[nodiscard]] bool IsButtonDown(Buttons button) const;

        /**
         * @brief Determines whether the specified button(s) are not pressed.
         * @param button Buttons to query; can be combined with bitwise OR.
         * @return True if all specified buttons are up; false otherwise.
         */
        [[nodiscard]] bool IsButtonUp(Buttons button) const;

        /**
         * @brief Compares this instance with another for equality.
         * @param other The other GamePadState to compare.
         * @return True if equal; false otherwise.
         */
        [[nodiscard]] bool Equals(const GamePadState& other) const;

        /**
         * @brief Gets the hash code for this instance.
         * @return Hash code of the object.
         */
        [[nodiscard]] int GetHashCode() const;

        /**
         * @brief Retrieves a string representation of this object.
         * @return The string representation.
         */
        [[nodiscard]] std::string ToString() const;

        /**
         * @brief Determines whether two GamePadState instances are equal.
         * @param left Object on the left of the equal sign.
         * @param right Object on the right of the equal sign.
         * @return True if equal; false otherwise.
         */
        friend bool operator==(const GamePadState& left, const GamePadState& right);

        /**
         * @brief Determines whether two GamePadState instances are not equal.
         * @param left Object on the left of the equal sign.
         * @param right Object on the right of the equal sign.
         * @return True if not equal; false otherwise.
         */
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
