// SPDX-License-Identifier: MS-PL
#pragma once

#include <type_traits>

namespace Microsoft::Xna::Framework::Graphics
{
    /** @brief Defines how vertex or index buffer data will be flushed during a SetData operation. */
    enum class SetDataOptions
    {
        /** @brief SetData may overwrite portions of existing data. */
        None        = 0,
        /** @brief Discard the entire buffer; a new memory region is returned and rendering from the old region does not stall. */
        Discard     = 1,
        /** @brief SetData will not overwrite existing data, allowing the driver to return immediately and continue rendering. */
        NoOverwrite = 2,
    };

    /**
     * @brief Combines two SetDataOptions flags with bitwise OR.
     * @param left First flag set.
     * @param right Second flag set.
     * @return The combined flag set.
     */
    [[nodiscard]] constexpr SetDataOptions operator|(SetDataOptions left, SetDataOptions right)
    {
        using U = std::underlying_type_t<SetDataOptions>;
        return static_cast<SetDataOptions>(static_cast<U>(left) | static_cast<U>(right));
    }

    /**
     * @brief Masks two SetDataOptions flags with bitwise AND.
     * @param left First flag set.
     * @param right Second flag set.
     * @return The flags present in both operands.
     */
    [[nodiscard]] constexpr SetDataOptions operator&(SetDataOptions left, SetDataOptions right)
    {
        using U = std::underlying_type_t<SetDataOptions>;
        return static_cast<SetDataOptions>(static_cast<U>(left) & static_cast<U>(right));
    }

    /**
     * @brief Returns the bitwise complement of a SetDataOptions value.
     * @param value Flag set to complement.
     * @return The bitwise complement of @p value.
     */
    [[nodiscard]] constexpr SetDataOptions operator~(SetDataOptions value)
    {
        using U = std::underlying_type_t<SetDataOptions>;
        return static_cast<SetDataOptions>(~static_cast<U>(value));
    }

    /**
     * @brief Combines flags into @p left with bitwise OR-assignment.
     * @param left Flag set to modify.
     * @param right Flags to add.
     * @return A reference to @p left.
     */
    constexpr SetDataOptions& operator|=(SetDataOptions& left, SetDataOptions right)
    {
        left = left | right;
        return left;
    }

    /**
     * @brief Masks @p left with @p right using bitwise AND-assignment.
     * @param left Flag set to modify.
     * @param right Mask to apply.
     * @return A reference to @p left.
     */
    constexpr SetDataOptions& operator&=(SetDataOptions& left, SetDataOptions right)
    {
        left = left & right;
        return left;
    }
}
