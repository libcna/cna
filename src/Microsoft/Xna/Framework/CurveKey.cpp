// SPDX-License-Identifier: MS-PL

#include "Microsoft/Xna/Framework/CurveKey.hpp"

#include <cstdint>
#include <cstring>

namespace Microsoft::Xna::Framework
{
    namespace
    {
        std::size_t FloatHash(float value)
        {
            std::uint32_t bits = 0;
            static_assert(sizeof(bits) == sizeof(value));
            std::memcpy(&bits, &value, sizeof(value));
            return static_cast<std::size_t>(bits);
        }
    }

    CurveContinuity CurveKey::getContinuityProperty() const
    {
        return continuity;
    }

    void CurveKey::setContinuityProperty(CurveContinuity value)
    {
        continuity = value;
    }

    float CurveKey::getPositionProperty() const
    {
        return position;
    }

    float CurveKey::getTangentInProperty() const
    {
        return tangentIn;
    }

    void CurveKey::setTangentInProperty(float value)
    {
        tangentIn = value;
    }

    float CurveKey::getTangentOutProperty() const
    {
        return tangentOut;
    }

    void CurveKey::setTangentOutProperty(float value)
    {
        tangentOut = value;
    }

    float CurveKey::getValueProperty() const
    {
        return value;
    }

    void CurveKey::setValueProperty(float value)
    {
        this->value = value;
    }

    CurveKey::CurveKey(float position, float value)
        : CurveKey(position, value, 0.0f, 0.0f, CurveContinuity::Smooth)
    {
    }

    CurveKey::CurveKey(float position, float value, float tangentIn, float tangentOut)
        : CurveKey(position, value, tangentIn, tangentOut, CurveContinuity::Smooth)
    {
    }

    CurveKey::CurveKey(float position, float value, float tangentIn, float tangentOut, CurveContinuity continuity)
        : position(position),
          value(value),
          tangentIn(tangentIn),
          tangentOut(tangentOut),
          continuity(continuity)
    {
    }

    CurveKey CurveKey::Clone() const
    {
        return CurveKey(position, value, tangentIn, tangentOut, continuity);
    }

    int CurveKey::CompareTo(const CurveKey& other) const
    {
        if (position < other.position)
        {
            return -1;
        }
        if (position > other.position)
        {
            return 1;
        }
        return 0;
    }

    bool CurveKey::Equals(const CurveKey& other) const
    {
        return *this == other;
    }

    std::size_t CurveKey::GetHashCode() const
    {
        return FloatHash(position) ^
            FloatHash(value) ^
            FloatHash(tangentIn) ^
            FloatHash(tangentOut) ^
            static_cast<std::size_t>(continuity);
    }

    bool operator==(const CurveKey& a, const CurveKey& b)
    {
        return a.position == b.position &&
            a.value == b.value &&
            a.tangentIn == b.tangentIn &&
            a.tangentOut == b.tangentOut &&
            a.continuity == b.continuity;
    }

    bool operator!=(const CurveKey& a, const CurveKey& b)
    {
        return !(a == b);
    }
}
