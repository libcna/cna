// SPDX-License-Identifier: MS-PL
#pragma once

#include "Microsoft/Xna/Framework/Vector3.hpp"

namespace CNA::Internal::Math
{
// Genuine XNA accumulates these operations wide. Public Vector3 methods
// store a Single result, but an inlined geometry comparison may retain it.
[[nodiscard]] inline double SquaredLengthWide(const Microsoft::Xna::Framework::Vector3 &value)
{
    return static_cast<double>(value.X) * value.X + static_cast<double>(value.Y) * value.Y +
           static_cast<double>(value.Z) * value.Z;
}

[[nodiscard]] inline double SquaredDistanceWide(const Microsoft::Xna::Framework::Vector3 &left,
                                                const Microsoft::Xna::Framework::Vector3 &right)
{
    const double x = static_cast<double>(left.X) - right.X;
    const double y = static_cast<double>(left.Y) - right.Y;
    const double z = static_cast<double>(left.Z) - right.Z;
    return x * x + y * y + z * z;
}
} // namespace CNA::Internal::Math
