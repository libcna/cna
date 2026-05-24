#pragma once

namespace Microsoft::Xna::Framework
{
    /// Defines whether interpolation is used between two curve keys.
    enum class CurveContinuity
    {
        /// Interpolation may be used between this key and the next one.
        Smooth,

        /// Interpolation is disabled; values stay on the current key until the next key is reached.
        Step
    };
}
