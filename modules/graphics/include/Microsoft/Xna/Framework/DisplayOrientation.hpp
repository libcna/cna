// SPDX-License-Identifier: MS-PL

#pragma once

#include <type_traits>

namespace Microsoft::Xna::Framework
{
    /** @brief Defines the orientation flags that can be used by a display. */
    enum class DisplayOrientation : int
    {
        /** @brief The default display orientation. */
        Default = 0,

        /** @brief The display is rotated counterclockwise into a landscape orientation. Width is greater than height. */
        LandscapeLeft = 1,

        /** @brief The display is rotated clockwise into a landscape orientation. Width is greater than height. */
        LandscapeRight = 2,

        /** @brief The display is rotated as portrait, where height is greater than width. */
        Portrait = 4
    };

    /**
     * @brief Combines two orientation flags.
     * @param left The first orientation operand.
     * @param right The second orientation operand.
     * @return The bitwise OR of both operands.
     */
    [[nodiscard]] constexpr DisplayOrientation operator|(DisplayOrientation left, DisplayOrientation right)
    {
        using Underlying = std::underlying_type_t<DisplayOrientation>;
        return static_cast<DisplayOrientation>(
            static_cast<Underlying>(left) | static_cast<Underlying>(right)
        );
    }

    /**
     * @brief Returns the flags present in both operands.
     * @param left The first orientation operand.
     * @param right The second orientation operand.
     * @return The bitwise AND of both operands.
     */
    [[nodiscard]] constexpr DisplayOrientation operator&(DisplayOrientation left, DisplayOrientation right)
    {
        using Underlying = std::underlying_type_t<DisplayOrientation>;
        return static_cast<DisplayOrientation>(
            static_cast<Underlying>(left) & static_cast<Underlying>(right)
        );
    }

    /**
     * @brief Returns the bitwise complement of the orientation flags.
     * @param value The orientation value to complement.
     * @return The bitwise NOT of the value.
     */
    [[nodiscard]] constexpr DisplayOrientation operator~(DisplayOrientation value)
    {
        using Underlying = std::underlying_type_t<DisplayOrientation>;
        return static_cast<DisplayOrientation>(~static_cast<Underlying>(value));
    }

    /**
     * @brief Adds orientation flags in-place.
     * @param left The orientation variable to update.
     * @param right The flags to add.
     * @return A reference to the updated left operand.
     */
    constexpr DisplayOrientation& operator|=(DisplayOrientation& left, DisplayOrientation right)
    {
        left = left | right;
        return left;
    }

    /**
     * @brief Keeps only the flags present in both operands in-place.
     * @param left The orientation variable to update.
     * @param right The mask to AND with.
     * @return A reference to the updated left operand.
     */
    constexpr DisplayOrientation& operator&=(DisplayOrientation& left, DisplayOrientation right)
    {
        left = left & right;
        return left;
    }
}
