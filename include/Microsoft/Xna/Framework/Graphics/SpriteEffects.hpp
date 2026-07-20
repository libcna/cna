// SPDX-License-Identifier: MS-PL
#pragma once

#include <type_traits>

namespace Microsoft::Xna::Framework::Graphics
{
    /** @brief Defines sprite visual options for mirroring during rendering. */
    enum class SpriteEffects
    {
        /** @brief No options specified. */
        None = 0,
        /** @brief Render the sprite reversed along the X axis. */
        FlipHorizontally = 1,
        /** @brief Render the sprite reversed along the Y axis. */
        FlipVertically = 2
    };

    /**
     * @brief Combines two SpriteEffects values using bitwise OR.
     * @param left The left operand.
     * @param right The right operand.
     * @return The combined SpriteEffects value.
     */
    [[nodiscard]] constexpr SpriteEffects operator|(SpriteEffects left, SpriteEffects right)
    {
        using Underlying = std::underlying_type_t<SpriteEffects>;
        return static_cast<SpriteEffects>(
            static_cast<Underlying>(left) | static_cast<Underlying>(right)
        );
    }

    /**
     * @brief Masks two SpriteEffects values using bitwise AND.
     * @param left The left operand.
     * @param right The right operand.
     * @return The masked SpriteEffects value.
     */
    [[nodiscard]] constexpr SpriteEffects operator&(SpriteEffects left, SpriteEffects right)
    {
        using Underlying = std::underlying_type_t<SpriteEffects>;
        return static_cast<SpriteEffects>(
            static_cast<Underlying>(left) & static_cast<Underlying>(right)
        );
    }

    /**
     * @brief Combines a SpriteEffects value with another using bitwise OR-assignment.
     * @param left The left operand (modified in place).
     * @param right The right operand.
     * @return Reference to the modified left operand.
     */
    constexpr SpriteEffects& operator|=(SpriteEffects& left, SpriteEffects right)
    {
        left = left | right;
        return left;
    }

    /**
     * @brief Masks a SpriteEffects value with another using bitwise AND-assignment.
     * @param left The left operand (modified in place).
     * @param right The right operand.
     * @return Reference to the modified left operand.
     */
    constexpr SpriteEffects& operator&=(SpriteEffects& left, SpriteEffects right)
    {
        left = left & right;
        return left;
    }
}
