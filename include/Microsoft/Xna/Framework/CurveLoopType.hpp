// SPDX-License-Identifier: MS-PL

#pragma once

namespace Microsoft::Xna::Framework
{
    /** @brief Defines how a Curve is evaluated before its first key or after its last key. */
    enum class CurveLoopType
    {
        /** @brief The curve value is clamped to the value of the first or last key. */
        Constant    = 0,
        /** @brief Positions wrap around from the end back to the beginning of the curve. */
        Cycle       = 1,
        /** @brief Positions wrap and the value is offset by the first/last key difference per cycle. */
        CycleOffset = 2,
        /** @brief The evaluation direction alternates between start and end each cycle. */
        Oscillate   = 3,
        /** @brief Linear interpolation is used to extrapolate beyond the curve range. */
        Linear      = 4
    };
}
