// SPDX-License-Identifier: MS-PL
#pragma once

#include "Microsoft/Xna/Framework/Input/GamePadDeadZone.hpp"

namespace Microsoft::Xna::Framework::Input
{
    class GamePad;

    /// Represents the position of the left and right triggers on a gamepad.
    struct GamePadTriggers
    {
        /// Gets the position of the left trigger. Range: [0, 1].
        [[nodiscard]] float getLeftProperty() const;
        /// Gets the position of the right trigger. Range: [0, 1].
        [[nodiscard]] float getRightProperty() const;

        /// Constructs with both triggers at rest.
        GamePadTriggers();

        /// Constructs with clamped trigger values.
        GamePadTriggers(float leftTrigger, float rightTrigger);

        [[nodiscard]] bool Equals(const GamePadTriggers& other) const;
        [[nodiscard]] int GetHashCode() const;

        friend bool operator==(const GamePadTriggers& left, const GamePadTriggers& right);
        friend bool operator!=(const GamePadTriggers& left, const GamePadTriggers& right);

    private:
        float left_;
        float right_;

        /// Internal constructor that applies dead zone processing before clamping.
        GamePadTriggers(float leftTrigger, float rightTrigger, GamePadDeadZone deadZoneMode);

        friend class GamePad;
    };
}
