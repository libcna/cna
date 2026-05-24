#pragma once

#include <string>

#include "Microsoft/Xna/Framework/CurveContinuity.hpp"

namespace Microsoft::Xna::Framework
{
    /// Represents one control point on a Curve.
    class CurveKey
    {
    public:
        /// Position of this key on the curve.
        float Position;

        /// Value stored at this key.
        float Value;

        /// Tangent used when approaching this key from the previous key.
        float TangentIn;

        /// Tangent used when leaving this key toward the next key.
        float TangentOut;

        /// Determines whether the segment after this key is smooth or stepped.
        CurveContinuity Continuity;

        /// Creates a key with zero tangents and smooth continuity.
        CurveKey(float position, float value);

        /// Creates a key with explicit tangents and smooth continuity.
        CurveKey(float position, float value, float tangentIn, float tangentOut);

        /// Creates a key with explicit tangents and continuity.
        CurveKey(float position, float value, float tangentIn, float tangentOut, CurveContinuity continuity);

        /// Creates a copy of this key.
        [[nodiscard]] CurveKey Clone() const;

        /// Compares this key's position with another key's position.
        [[nodiscard]] int CompareTo(const CurveKey& other) const;

        /// Compares this key with another key.
        [[nodiscard]] bool Equals(const CurveKey& other) const;

        /// Returns a hash code based on this key's fields.
        [[nodiscard]] int GetHashCode() const;

        friend bool operator==(const CurveKey& a, const CurveKey& b);
        friend bool operator!=(const CurveKey& a, const CurveKey& b);
    };
}
