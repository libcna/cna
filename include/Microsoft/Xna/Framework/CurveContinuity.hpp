// SPDX-License-Identifier: MS-PL

#pragma once

namespace Microsoft::Xna::Framework
{
    /** @brief Defines the continuity of keys on a Curve. */
    enum class CurveContinuity
    {
        /** @brief Interpolation can be used between this key and the next. */
        Smooth = 0,
        /** @brief Interpolation cannot be used. A position between the two points returns this point's value. */
        Step   = 1
    };
}
