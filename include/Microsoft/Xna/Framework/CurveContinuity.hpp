#pragma once

namespace Microsoft::Xna::Framework
{
    /// Defines whether a curve segment is interpolated or stepped.
    enum class CurveContinuity
    {
        /// Values between this key and the next key are interpolated.
        Smooth,

        /// Values between this key and the next key stay on the current key until the next key is reached.
        Step
    };
}
