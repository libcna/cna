#pragma once

namespace Microsoft::Xna::Framework
{
    /// Defines how tangents are computed for curve keys.
    enum class CurveTangent
    {
        /// Tangent is always zero.
        Flat,

        /// Tangent is based on the value difference to a neighboring key.
        Linear,

        /// Tangent is smoothed using the values and positions of neighboring keys.
        Smooth
    };
}
