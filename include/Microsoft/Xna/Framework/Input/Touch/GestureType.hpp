// SPDX-License-Identifier: MS-PL
#pragma once

#include <type_traits>

namespace Microsoft::Xna::Framework::Input::Touch
{
    /**
     * @brief Defines the gesture types that can be enabled and read from TouchPanel.
     */
    enum class GestureType : int
    {
        /** @brief No gesture. */
        None = 0,

        /** @brief Single tap gesture. */
        Tap = 1,

        /** @brief Double tap gesture. */
        DoubleTap = 2,

        /** @brief Hold gesture. */
        Hold = 4,

        /** @brief Horizontal drag gesture. */
        HorizontalDrag = 8,

        /** @brief Vertical drag gesture. */
        VerticalDrag = 16,

        /** @brief Free-form drag gesture. */
        FreeDrag = 32,

        /** @brief Pinch gesture. */
        Pinch = 64,

        /** @brief Flick gesture. */
        Flick = 128,

        /** @brief Drag completion gesture. */
        DragComplete = 256,

        /** @brief Pinch completion gesture. */
        PinchComplete = 512
    };

    /**
     * @brief Combines two GestureType values using bitwise OR.
     * @param left The left operand.
     * @param right The right operand.
     * @return The combined GestureType value.
     */
    [[nodiscard]] constexpr GestureType operator|(GestureType left, GestureType right)
    {
        using Underlying = std::underlying_type_t<GestureType>;
        return static_cast<GestureType>(
            static_cast<Underlying>(left) | static_cast<Underlying>(right)
        );
    }

    /**
     * @brief Masks two GestureType values using bitwise AND.
     * @param left The left operand.
     * @param right The right operand.
     * @return The masked GestureType value.
     */
    [[nodiscard]] constexpr GestureType operator&(GestureType left, GestureType right)
    {
        using Underlying = std::underlying_type_t<GestureType>;
        return static_cast<GestureType>(
            static_cast<Underlying>(left) & static_cast<Underlying>(right)
        );
    }

    /**
     * @brief Combines a GestureType value with another using bitwise OR-assignment.
     * @param left The left operand (modified in place).
     * @param right The right operand.
     * @return Reference to the modified left operand.
     */
    constexpr GestureType& operator|=(GestureType& left, GestureType right)
    {
        left = left | right;
        return left;
    }

    /**
     * @brief Masks a GestureType value with another using bitwise AND-assignment.
     * @param left The left operand (modified in place).
     * @param right The right operand.
     * @return Reference to the modified left operand.
     */
    constexpr GestureType& operator&=(GestureType& left, GestureType right)
    {
        left = left & right;
        return left;
    }
}
