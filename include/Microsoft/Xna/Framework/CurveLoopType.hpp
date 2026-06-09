// SPDX-License-Identifier: MS-PL

#pragma once

namespace Microsoft::Xna::Framework
{
    /// Defines how a Curve is evaluated before its first key or after its last key.
    enum class CurveLoopType
    {
        Constant    = 0, ///< Clamp to the value of the first or last key.
        Cycle       = 1, ///< Wrap positions from the end back to the beginning.
        CycleOffset = 2, ///< Wrap and offset the value by the first/last key difference per cycle.
        Oscillate   = 3, ///< Alternate evaluation direction between start and end.
        Linear      = 4  ///< Extrapolate linearly beyond the curve range.
    };
}
