// SPDX-License-Identifier: MS-PL

#pragma once

namespace Microsoft::Xna::Framework
{
    /** @brief Defines the different tangent types to be calculated for CurveKey points in a Curve. */
    enum class CurveTangent
    {
        /** @brief The tangent always has a value equal to zero. */
        Flat   = 0,
        /** @brief The tangent equals the difference between the current value and the neighboring key value. */
        Linear = 1,
        /** @brief The smooth tangent is computed by taking into account the values of both neighboring keys. */
        Smooth = 2
    };
}
