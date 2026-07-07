// SPDX-License-Identifier: MS-PL
#pragma once

#include "CNA/CNAHelper.hpp"

#include <cstdint>
#include <type_traits>

namespace CNA::Input
{
    /**
     * @brief NOXNA — the force-feedback effect families and global capabilities a haptic device
     *        supports, as a bit flag set. Mirrors SDL3's `SDL_HapticFeatures` bitmask.
     *
     * XNA 4.0 has no force-feedback API beyond `GamePad::SetVibration`'s simple dual-motor rumble;
     * this CNA extension exposes SDL3's richer `SDL_haptic` capability query.
     */
    NOXNA enum class HapticFeatureEXT : std::uint32_t
    {
        /** @brief No effect families or global capabilities supported. */
        None = 0,
        /** @brief Constant-force effects (`SDL_HAPTIC_CONSTANT`). */
        Constant = 1u << 0,
        /** @brief Sine-wave periodic effects (`SDL_HAPTIC_SINE`). */
        Sine = 1u << 1,
        /** @brief Square-wave periodic effects (`SDL_HAPTIC_SQUARE`). */
        Square = 1u << 2,
        /** @brief Triangle-wave periodic effects (`SDL_HAPTIC_TRIANGLE`). */
        Triangle = 1u << 3,
        /** @brief Upward-sawtooth periodic effects (`SDL_HAPTIC_SAWTOOTHUP`). */
        SawtoothUp = 1u << 4,
        /** @brief Downward-sawtooth periodic effects (`SDL_HAPTIC_SAWTOOTHDOWN`). */
        SawtoothDown = 1u << 5,
        /** @brief Ramp (linear start-to-end) effects (`SDL_HAPTIC_RAMP`). */
        Ramp = 1u << 6,
        /** @brief Spring condition effects (`SDL_HAPTIC_SPRING`). */
        Spring = 1u << 7,
        /** @brief Damper condition effects (`SDL_HAPTIC_DAMPER`). */
        Damper = 1u << 8,
        /** @brief Inertia condition effects (`SDL_HAPTIC_INERTIA`). */
        Inertia = 1u << 9,
        /** @brief Friction condition effects (`SDL_HAPTIC_FRICTION`). */
        Friction = 1u << 10,
        /** @brief Left/right (large/small motor) effects (`SDL_HAPTIC_LEFTRIGHT`). */
        LeftRight = 1u << 11,
        /** @brief Custom raw-sample-buffer effects (`SDL_HAPTIC_CUSTOM`). */
        Custom = 1u << 15,
        /** @brief Overall effect gain can be set (`SDL_HAPTIC_GAIN`). */
        Gain = 1u << 16,
        /** @brief Autocenter strength can be set (`SDL_HAPTIC_AUTOCENTER`). */
        Autocenter = 1u << 17,
        /** @brief Effect play/stop status can be queried (`SDL_HAPTIC_STATUS`). */
        Status = 1u << 18,
        /** @brief Effects can be paused/resumed (`SDL_HAPTIC_PAUSE`). */
        Pause = 1u << 19
    };

    /**
     * @brief Combines two feature sets using bitwise OR.
     * @param left The left operand.
     * @param right The right operand.
     * @return The combined feature set.
     */
    [[nodiscard]] constexpr HapticFeatureEXT operator|(HapticFeatureEXT left, HapticFeatureEXT right)
    {
        using U = std::underlying_type_t<HapticFeatureEXT>;
        return static_cast<HapticFeatureEXT>(static_cast<U>(left) | static_cast<U>(right));
    }

    /**
     * @brief Masks two feature sets using bitwise AND.
     * @param left The left operand.
     * @param right The right operand.
     * @return The masked feature set.
     */
    [[nodiscard]] constexpr HapticFeatureEXT operator&(HapticFeatureEXT left, HapticFeatureEXT right)
    {
        using U = std::underlying_type_t<HapticFeatureEXT>;
        return static_cast<HapticFeatureEXT>(static_cast<U>(left) & static_cast<U>(right));
    }

    /**
     * @brief Inverts a feature set using bitwise NOT.
     * @param value The feature set to invert.
     * @return The inverted feature set.
     */
    [[nodiscard]] constexpr HapticFeatureEXT operator~(HapticFeatureEXT value)
    {
        using U = std::underlying_type_t<HapticFeatureEXT>;
        return static_cast<HapticFeatureEXT>(~static_cast<U>(value));
    }

    /**
     * @brief Combines a feature set with another using bitwise OR-assignment.
     * @param left The left operand (modified in place).
     * @param right The right operand.
     * @return Reference to the modified left operand.
     */
    constexpr HapticFeatureEXT& operator|=(HapticFeatureEXT& left, HapticFeatureEXT right)
    {
        left = left | right;
        return left;
    }

    /**
     * @brief Masks a feature set with another using bitwise AND-assignment.
     * @param left The left operand (modified in place).
     * @param right The right operand.
     * @return Reference to the modified left operand.
     */
    constexpr HapticFeatureEXT& operator&=(HapticFeatureEXT& left, HapticFeatureEXT right)
    {
        left = left & right;
        return left;
    }
}
