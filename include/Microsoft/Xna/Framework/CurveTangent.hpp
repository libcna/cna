// SPDX-License-Identifier: MS-PL

#pragma once

namespace Microsoft::Xna::Framework
{
    /// Defines how tangents are calculated for CurveKey points.
    enum class CurveTangent
    {
        Flat   = 0, ///< Tangent is always zero.
        Linear = 1, ///< Tangent equals the difference to the neighboring key value.
        Smooth = 2  ///< Tangent is smoothed using both neighboring keys.
    };
}
