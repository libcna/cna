#pragma once

#include "Microsoft/Xna/Framework/Input/GamePadDeadZone.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"

namespace Microsoft::Xna::Framework::Input
{
    class GamePad;

    /// Represents the position of the left and right thumbsticks on a gamepad.
    struct GamePadThumbSticks
    {
        /// Gets the position of the left thumbstick. Range: [-1, 1] per axis.
        [[nodiscard]] const Microsoft::Xna::Framework::Vector2& getLeftProperty() const;
        /// Gets the position of the right thumbstick. Range: [-1, 1] per axis.
        [[nodiscard]] const Microsoft::Xna::Framework::Vector2& getRightProperty() const;

        /// Constructs with both sticks at rest.
        GamePadThumbSticks();

        /// Constructs with given positions; applies square clamp to [-1, 1].
        GamePadThumbSticks(const Microsoft::Xna::Framework::Vector2& leftPosition,
                           const Microsoft::Xna::Framework::Vector2& rightPosition);

        [[nodiscard]] bool Equals(const GamePadThumbSticks& other) const;
        [[nodiscard]] int GetHashCode() const;

        friend bool operator==(const GamePadThumbSticks& left, const GamePadThumbSticks& right);
        friend bool operator!=(const GamePadThumbSticks& left, const GamePadThumbSticks& right);

    private:
        Microsoft::Xna::Framework::Vector2 left_;
        Microsoft::Xna::Framework::Vector2 right_;

        /// Internal constructor that applies dead zone processing then clamping.
        GamePadThumbSticks(const Microsoft::Xna::Framework::Vector2& leftPosition,
                           const Microsoft::Xna::Framework::Vector2& rightPosition,
                           GamePadDeadZone deadZoneMode);

        void ApplySquareClamp();
        void ApplyCircularClamp();
        void ApplyDeadZone(GamePadDeadZone dz);
        static Microsoft::Xna::Framework::Vector2 ExcludeCircularDeadZone(
            const Microsoft::Xna::Framework::Vector2& value, float deadZone);

        friend class GamePad;
    };
}
