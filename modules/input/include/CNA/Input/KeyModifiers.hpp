// SPDX-License-Identifier: MS-PL
#pragma once

#include "CNA/CNAHelper.hpp"

#include <cstdint>
#include <type_traits>

namespace CNA::Input
{
    /**
     * @brief CNAEXT — active keyboard modifier keys and lock states, as a bit flag set.
     *
     * XNA 4.0's `KeyboardState` exposes only individual key up/down; it has no aggregate modifier or
     * lock-state query. This CNA extension projects the platform contract's implementation-neutral
     * `KeyModifier` mask (which collapses left/right variants), so text/UI code can branch on
     * Shift/Ctrl/Alt/Gui and read the Caps/Num/Scroll lock toggles without naming a native backend.
     */
    CNAEXT enum class KeyModifiersEXT : std::uint32_t
    {
        /** @brief No modifier active. */
        None   = 0,
        /** @brief Either Shift key is held. */
        Shift  = 1u << 0,
        /** @brief Either Control key is held. */
        Ctrl   = 1u << 1,
        /** @brief Either Alt key is held. */
        Alt    = 1u << 2,
        /** @brief Either GUI (Windows/Command) key is held. */
        Gui    = 1u << 3,
        /** @brief Caps Lock is on. */
        Caps   = 1u << 4,
        /** @brief Num Lock is on. */
        Num    = 1u << 5,
        /** @brief Scroll Lock is on. */
        Scroll = 1u << 6,
        /** @brief The AltGr / Mode key is held. */
        Mode   = 1u << 7
    };

    /**
     * @brief Combines two modifier sets using bitwise OR.
     * @param left The left operand.
     * @param right The right operand.
     * @return The combined modifier set.
     */
    [[nodiscard]] constexpr KeyModifiersEXT operator|(KeyModifiersEXT left, KeyModifiersEXT right)
    {
        using U = std::underlying_type_t<KeyModifiersEXT>;
        return static_cast<KeyModifiersEXT>(static_cast<U>(left) | static_cast<U>(right));
    }

    /**
     * @brief Masks two modifier sets using bitwise AND.
     * @param left The left operand.
     * @param right The right operand.
     * @return The masked modifier set.
     */
    [[nodiscard]] constexpr KeyModifiersEXT operator&(KeyModifiersEXT left, KeyModifiersEXT right)
    {
        using U = std::underlying_type_t<KeyModifiersEXT>;
        return static_cast<KeyModifiersEXT>(static_cast<U>(left) & static_cast<U>(right));
    }

    /**
     * @brief Inverts a modifier set using bitwise NOT.
     * @param value The modifier set to invert.
     * @return The inverted modifier set.
     */
    [[nodiscard]] constexpr KeyModifiersEXT operator~(KeyModifiersEXT value)
    {
        using U = std::underlying_type_t<KeyModifiersEXT>;
        return static_cast<KeyModifiersEXT>(~static_cast<U>(value));
    }

    /**
     * @brief Combines a modifier set with another using bitwise OR-assignment.
     * @param left The left operand (modified in place).
     * @param right The right operand.
     * @return Reference to the modified left operand.
     */
    constexpr KeyModifiersEXT& operator|=(KeyModifiersEXT& left, KeyModifiersEXT right)
    {
        left = left | right;
        return left;
    }

    /**
     * @brief Masks a modifier set with another using bitwise AND-assignment.
     * @param left The left operand (modified in place).
     * @param right The right operand.
     * @return Reference to the modified left operand.
     */
    constexpr KeyModifiersEXT& operator&=(KeyModifiersEXT& left, KeyModifiersEXT right)
    {
        left = left & right;
        return left;
    }
}
