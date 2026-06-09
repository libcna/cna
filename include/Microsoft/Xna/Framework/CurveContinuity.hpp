// SPDX-License-Identifier: MS-PL

#pragma once

namespace Microsoft::Xna::Framework
{
    /// Defines the continuity of keys on a Curve.
    enum class CurveContinuity
    {
        Smooth = 0, ///< Interpolation can be used between this key and the next.
        Step   = 1  ///< Interpolation cannot be used; position returns this key's value.
    };
}
