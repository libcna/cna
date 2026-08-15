// SPDX-License-Identifier: MS-PL
#pragma once

#include "CNA/CNAHelper.hpp"

namespace CNA::Input
{
    /**
     * @brief CNAEXT — which force-feedback effect family a `HapticEffectEXT` describes, using
     *        CNA-owned values distinct from the capability bitmask in `HapticFeatureEXT`.
     */
    CNAEXT enum class HapticEffectTypeEXT
    {
        /** @brief A steady directional push. */
        Constant,
        /** @brief A sine-wave periodic effect. */
        Sine,
        /** @brief A square-wave periodic effect. */
        Square,
        /** @brief A triangle-wave periodic effect. */
        Triangle,
        /** @brief An upward-sawtooth periodic effect. */
        SawtoothUp,
        /** @brief A downward-sawtooth periodic effect. */
        SawtoothDown,
        /** @brief A linear start-to-end magnitude ramp. */
        Ramp,
        /** @brief Spring-like resistance based on axis position. */
        Spring,
        /** @brief Damper-like resistance based on axis velocity. */
        Damper,
        /** @brief Inertia-like resistance based on axis acceleration. */
        Inertia,
        /** @brief Friction-like resistance based on axis movement. */
        Friction,
        /** @brief Explicit large/small (low/high frequency) motor control. */
        LeftRight,
        /** @brief A caller-defined raw waveform sample buffer. */
        Custom
    };
}
