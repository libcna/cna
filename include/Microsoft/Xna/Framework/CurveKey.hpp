// SPDX-License-Identifier: MS-PL

#pragma once

#include <cstddef>

#include "System/IEquatable.hpp"
#include "System/IComparable.hpp"
#include "Microsoft/Xna/Framework/CurveContinuity.hpp"

namespace Microsoft::Xna::Framework
{
    /// Key point on a Curve.
    class CurveKey : public System::IEquatable<CurveKey>,
                     public System::IComparable<CurveKey>
    {
    public:
        /// Gets whether the segment between this key and the next key is smooth or stepped.
        [[nodiscard]] CurveContinuity getContinuityProperty() const;
        /// Sets whether the segment between this key and the next key is smooth or stepped.
        void setContinuityProperty(CurveContinuity value);

        /// Gets the position of this key on the curve.
        [[nodiscard]] float getPositionProperty() const;

        /// Gets the tangent used when approaching this key from the previous key.
        [[nodiscard]] float getTangentInProperty() const;
        /// Sets the tangent used when approaching this key from the previous key.
        void setTangentInProperty(float value);

        /// Gets the tangent used when leaving this key toward the next key.
        [[nodiscard]] float getTangentOutProperty() const;
        /// Sets the tangent used when leaving this key toward the next key.
        void setTangentOutProperty(float value);

        /// Gets the value of this key.
        [[nodiscard]] float getValueProperty() const;
        /// Sets the value of this key.
        void setValueProperty(float value);

        /// Creates a key with zero tangents and smooth continuity.
        CurveKey(float position, float value);

        /// Creates a key with explicit tangents and smooth continuity.
        CurveKey(float position, float value, float tangentIn, float tangentOut);

        /// Creates a key with explicit tangents and continuity.
        CurveKey(float position, float value, float tangentIn, float tangentOut, CurveContinuity continuity);

        /// Creates a copy of this key.
        [[nodiscard]] CurveKey Clone() const;

        /// Compares this key position with another key position.
        [[nodiscard]] int CompareTo(const CurveKey& other) const override;

        /// Compares this key with another key.
        [[nodiscard]] bool Equals(const CurveKey& other) const override;

        /// Gets a hash code from the key fields.
        [[nodiscard]] std::size_t GetHashCode() const;

        /// Returns true when all fields of both keys are equal.
        friend bool operator==(const CurveKey& a, const CurveKey& b);
        /// Returns true when any field differs between the keys.
        friend bool operator!=(const CurveKey& a, const CurveKey& b);

    private:
        float position;
        float value;
        float tangentIn;
        float tangentOut;
        CurveContinuity continuity;
    };
}
