// SPDX-License-Identifier: MS-PL

#include "Microsoft/Xna/Framework/CurveKey.hpp"

#include <cstdint>
#include <cstring>

namespace Microsoft::Xna::Framework
{
    namespace
    {
        std::uint32_t FloatHash(float value)
        {
            if (value == 0.0f) return 0; // XNA/.NET 4 equates both zero signs; retain all other bits.
            std::uint32_t bits = 0;
            static_assert(sizeof(bits) == sizeof(value));
            std::memcpy(&bits, &value, sizeof(value));
            return bits;
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
        // XNA compares positions directly, not via Single.CompareTo: unordered NaNs return 1.
        if (position == other.position)
        {
            return 0;
        }
        return position < other.position ? -1 : 1;
    }

    bool CurveKey::Equals(const std::any& obj) const
    {
        const CurveKey* other = std::any_cast<CurveKey>(&obj);
        return other != nullptr && Equals(*other);
    }

    bool CurveKey::Equals(const CurveKey& other) const
    {
        return *this == other;
    }

    std::size_t CurveKey::GetHashCode() const
    {
        // XNA sums the five hashes with 32-bit wrap, rather than XORing them.
        // Use unsigned arithmetic so overflow is defined before widening to size_t.
        return FloatHash(position) +
            FloatHash(value) +
            FloatHash(tangentIn) +
            FloatHash(tangentOut) +
            static_cast<std::uint32_t>(continuity);
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
