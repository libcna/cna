// SPDX-License-Identifier: MS-PL
#pragma once
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include <cmath>

namespace CNA::Internal::GamerServices::Avatars {
// The versioned catalog recipe retains binary32 intermediates independently of
// public XNA math. Explicit stores preserve its authored rounding boundaries.
inline float catalogDot(const Microsoft::Xna::Framework::Vector3& a,
    const Microsoft::Xna::Framework::Vector3& b)
{
    const volatile float x=a.X*b.X,y=a.Y*b.Y,z=a.Z*b.Z;
    const volatile float xy=x+y;
    const volatile float result=xy+z;
    return result;
}

inline float catalogLength(const Microsoft::Xna::Framework::Vector3& value)
{
    return std::sqrt(catalogDot(value,value));
}

inline void catalogNormalize(Microsoft::Xna::Framework::Vector3& value)
{
    const volatile float inverse=1.0f/catalogLength(value);
    const volatile float x=value.X*inverse,y=value.Y*inverse,z=value.Z*inverse;
    value={x,y,z};
}

inline Microsoft::Xna::Framework::Vector3 catalogCross(const Microsoft::Xna::Framework::Vector3& a,
    const Microsoft::Xna::Framework::Vector3& b)
{
    const volatile float x1=a.Y*b.Z,x2=a.Z*b.Y,y1=a.Z*b.X,y2=a.X*b.Z,z1=a.X*b.Y,z2=a.Y*b.X;
    const volatile float x=x1-x2,y=y1-y2,z=z1-z2;
    return {x,y,z};
}

inline float catalogDistance(const Microsoft::Xna::Framework::Vector3& a,
    const Microsoft::Xna::Framework::Vector3& b)
{
    const volatile float x=a.X-b.X,y=a.Y-b.Y,z=a.Z-b.Z;
    return catalogLength({x,y,z});
}
}
