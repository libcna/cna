// SPDX-License-Identifier: MS-PL
#pragma once

#include "CNA/CNAHelper.hpp"
#include "Microsoft/Xna/Framework/Input/GamePadDeadZone.hpp"
#include "Microsoft/Xna/Framework/Vector2.hpp"

#include <any>
#include <string>

namespace Microsoft::Xna::Framework::Input
{
    class GamePad;

    /**
     * @brief Represents the position of the left and right thumbsticks on a gamepad.
     */
    struct GamePadThumbSticks
    {
        /**
         * @brief Gets the position of the left thumbstick. Range: [-1, 1] per axis.
         * @return The left thumbstick position.
         */
        [[nodiscard]] const Microsoft::Xna::Framework::Vector2& getLeftProperty() const;

        /**
         * @brief Gets the position of the right thumbstick. Range: [-1, 1] per axis.
         * @return The right thumbstick position.
         */
        [[nodiscard]] const Microsoft::Xna::Framework::Vector2& getRightProperty() const;

        /** @brief Constructs with both sticks at rest. */
        CNAEXT GamePadThumbSticks();

        /**
         * @brief Constructs with given positions; applies square clamp to [-1, 1].
         * @param leftPosition The left thumbstick position.
         * @param rightPosition The right thumbstick position.
         */
        GamePadThumbSticks(const Microsoft::Xna::Framework::Vector2& leftPosition,
                           const Microsoft::Xna::Framework::Vector2& rightPosition);

        /**
         * @brief Compares this GamePadThumbSticks with a boxed object for equality.
         *
         * Mirrors the CLR `Equals(object)` contract: an empty object, or an object holding a
         * different type, is unequal; otherwise the comparison is the typed one above.
         *
         * @param obj The boxed object to compare against.
         * @return @c true if @p obj holds an equal GamePadThumbSticks; @c false otherwise.
         */
        [[nodiscard]] bool Equals(const std::any& obj) const;

        /**
         * @brief Returns a string representation of this GamePadThumbSticks.
         *
         * @return `{Left:<left> Right:<right>}`, each stick in Vector2's own string form.
         */
        [[nodiscard]] std::string ToString() const;

        /**
         * @brief Compares this instance with another for equality.
         * @param other The other GamePadThumbSticks to compare.
         * @return True if equal; false otherwise.
         */
        [[nodiscard]] bool Equals(const GamePadThumbSticks& other) const;

        /**
         * @brief Gets the hash code for this instance.
         * @return Hash code of the object.
         */
        [[nodiscard]] int GetHashCode() const;

        /**
         * @brief Compares two GamePadThumbSticks instances for equality.
         * @param left The left-hand operand.
         * @param right The right-hand operand.
         * @return True if equal; false otherwise.
         */
        friend bool operator==(const GamePadThumbSticks& left, const GamePadThumbSticks& right);

        /**
         * @brief Compares two GamePadThumbSticks instances for inequality.
         * @param left The left-hand operand.
         * @param right The right-hand operand.
         * @return True if not equal; false otherwise.
         */
        friend bool operator!=(const GamePadThumbSticks& left, const GamePadThumbSticks& right);

    private:
        Microsoft::Xna::Framework::Vector2 left_;
        Microsoft::Xna::Framework::Vector2 right_;

        /** @brief Internal constructor that applies dead zone processing then clamping. */
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
