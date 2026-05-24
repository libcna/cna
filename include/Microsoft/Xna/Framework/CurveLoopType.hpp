#pragma once

namespace Microsoft::Xna::Framework
{
    /// Defines how a curve is evaluated outside the range covered by its keys.
    enum class CurveLoopType
    {
        /// Use the first key before the curve and the last key after the curve.
        Constant,

        /// Wrap positions from the end back to the beginning.
        Cycle,

        /// Wrap positions and offset the result by the value delta for each cycle.
        CycleOffset,

        /// Alternate between forward and backward curve traversal.
        Oscillate,

        /// Extrapolate linearly outside the curve range.
        Linear
    };
}
