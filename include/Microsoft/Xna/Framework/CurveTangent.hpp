#pragma once

namespace Microsoft::Xna::Framework
{
    /// Defines how tangents are calculated for CurveKey points.
    enum class CurveTangent
    {
        /// Tangent is always zero.
        Flat,

        /// Tangent is based on the neighboring key value difference.
        Linear,

        /// Tangent is smoothed using the neighboring keys.
        Smooth
    };
}
