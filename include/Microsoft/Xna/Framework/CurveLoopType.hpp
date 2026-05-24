#pragma once

namespace Microsoft::Xna::Framework
{
    /// Defines how a Curve is evaluated before its first key or after its last key.
    enum class CurveLoopType
    {
        /// Use the first key before the curve and the last key after the curve.
        Constant,

        /// Wrap positions from the end of the curve back to the beginning.
        Cycle,

        /// Wrap positions and offset the value by the first/last key value difference for each cycle.
        CycleOffset,

        /// Alternate evaluation direction between the start and end of the curve.
        Oscillate,

        /// Extrapolate linearly outside the curve range.
        Linear
    };
}
