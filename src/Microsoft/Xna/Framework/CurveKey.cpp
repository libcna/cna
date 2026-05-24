#include "Microsoft/Xna/Framework/CurveKey.hpp"

#include <cstdint>
#include <cstring>

namespace Microsoft::Xna::Framework
{
    namespace
    {
        int FloatHash(float value)
        {
            std::uint32_t bits = 0;
            static_assert(sizeof(bits) == sizeof(value));
            std::memcpy(&bits, &value, sizeof(value));
            return static_cast<int>(bits);
        }
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
        : Position(position),
          Value(value),
          TangentIn(tangentIn),
          TangentOut(tangentOut),
          Continuity(continuity)
    {
    }

    CurveKey CurveKey::Clone() const
    {
        return CurveKey(Position, Value, TangentIn, TangentOut, Continuity);
    }

    int CurveKey::CompareTo(const CurveKey& other) const
    {
        if (Position < other.Position)
        {
            return -1;
        }
        if (Position > other.Position)
        {
            return 1;
        }
        return 0;
    }

    bool CurveKey::Equals(const CurveKey& other) const
    {
        return *this == other;
    }

    int CurveKey::GetHashCode() const
    {
        return FloatHash(Position) ^
               FloatHash(Value) ^
               FloatHash(TangentIn) ^
               FloatHash(TangentOut) ^
               static_cast<int>(Continuity);
    }

    bool operator==(const CurveKey& a, const CurveKey& b)
    {
        return a.Position == b.Position &&
               a.Value == b.Value &&
               a.TangentIn == b.TangentIn &&
               a.TangentOut == b.TangentOut &&
               a.Continuity == b.Continuity;
    }

    bool operator!=(const CurveKey& a, const CurveKey& b)
    {
        return !(a == b);
    }
}
