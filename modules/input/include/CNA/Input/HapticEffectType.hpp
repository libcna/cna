// SPDX-License-Identifier: MS-PL
#pragma once

#include "CNA/CNAHelper.hpp"

namespace CNA::Input
{
    /**
     * @brief CNAEXT — which force-feedback effect family a `HapticEffectEXT` describes. Mirrors
     *        SDL3's individual `SDL_HAPTIC_*` effect-type constants (as opposed to the capability
     *        bitmask, see `HapticFeatureEXT`).
     */
    CNAEXT enum class HapticEffectTypeEXT
    {
        /** @brief A steady directional push (`SDL_HAPTIC_CONSTANT`). */
        Constant,
        /** @brief A sine-wave periodic effect (`SDL_HAPTIC_SINE`). */
        Sine,
        /** @brief A square-wave periodic effect (`SDL_HAPTIC_SQUARE`). */
        Square,
        /** @brief A triangle-wave periodic effect (`SDL_HAPTIC_TRIANGLE`). */
        Triangle,
        /** @brief An upward-sawtooth periodic effect (`SDL_HAPTIC_SAWTOOTHUP`). */
        SawtoothUp,
        /** @brief A downward-sawtooth periodic effect (`SDL_HAPTIC_SAWTOOTHDOWN`). */
        SawtoothDown,
        /** @brief A linear start-to-end magnitude ramp (`SDL_HAPTIC_RAMP`). */
        Ramp,
        /** @brief Spring-like resistance based on axis position (`SDL_HAPTIC_SPRING`). */
        Spring,
        /** @brief Damper-like resistance based on axis velocity (`SDL_HAPTIC_DAMPER`). */
        Damper,
        /** @brief Inertia-like resistance based on axis acceleration (`SDL_HAPTIC_INERTIA`). */
        Inertia,
        /** @brief Friction-like resistance based on axis movement (`SDL_HAPTIC_FRICTION`). */
        Friction,
        /** @brief Explicit large/small (low/high frequency) motor control (`SDL_HAPTIC_LEFTRIGHT`). */
        LeftRight,
        /** @brief A caller-defined raw waveform sample buffer (`SDL_HAPTIC_CUSTOM`). */
        Custom
    };
}
