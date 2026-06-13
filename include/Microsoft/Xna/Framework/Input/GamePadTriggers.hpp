// SPDX-License-Identifier: MS-PL
#pragma once

#include "CNA/CNAHelper.hpp"
#include "Microsoft/Xna/Framework/Input/GamePadDeadZone.hpp"

namespace Microsoft::Xna::Framework::Input
{
    class GamePad;

    /**
     * @brief Represents the position of the left and right triggers on a gamepad.
     */
    struct GamePadTriggers
    {
        /**
         * @brief Gets the position of the left trigger. Range: [0, 1].
         * @return The left trigger value.
         */
        [[nodiscard]] float getLeftProperty() const;

        /**
         * @brief Gets the position of the right trigger. Range: [0, 1].
         * @return The right trigger value.
         */
        [[nodiscard]] float getRightProperty() const;

        /** @brief Constructs with both triggers at rest. */
        NOXNA GamePadTriggers();

        /**
         * @brief Constructs with clamped trigger values.
         * @param leftTrigger The left trigger value, clamped to [0, 1].
         * @param rightTrigger The right trigger value, clamped to [0, 1].
         */
        GamePadTriggers(float leftTrigger, float rightTrigger);

        /**
         * @brief Compares this instance with another for equality.
         * @param other The other GamePadTriggers to compare.
         * @return True if equal; false otherwise.
         */
        [[nodiscard]] bool Equals(const GamePadTriggers& other) const;

        /**
         * @brief Gets the hash code for this instance.
         * @return Hash code of the object.
         */
        [[nodiscard]] int GetHashCode() const;

        /**
         * @brief Compares two GamePadTriggers instances for equality.
         * @param left The left-hand operand.
         * @param right The right-hand operand.
         * @return True if equal; false otherwise.
         */
        friend bool operator==(const GamePadTriggers& left, const GamePadTriggers& right);

        /**
         * @brief Compares two GamePadTriggers instances for inequality.
         * @param left The left-hand operand.
         * @param right The right-hand operand.
         * @return True if not equal; false otherwise.
         */
        friend bool operator!=(const GamePadTriggers& left, const GamePadTriggers& right);

    private:
        float left_;
        float right_;

        /** @brief Internal constructor that applies dead zone processing before clamping. */
        GamePadTriggers(float leftTrigger, float rightTrigger, GamePadDeadZone deadZoneMode);

        friend class GamePad;
    };
}
