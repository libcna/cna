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
        /// Gets or sets whether the segment between this key and the next key is smooth or stepped.
        [[nodiscard]] CurveContinuity getContinuityProperty() const;
        void setContinuityProperty(CurveContinuity value);

        /// Gets the position of this key on the curve.
        [[nodiscard]] float getPositionProperty() const;

        /// Gets or sets the tangent used when approaching this key from the previous key.
        [[nodiscard]] float getTangentInProperty() const;
        void setTangentInProperty(float value);

        /// Gets or sets the tangent used when leaving this key toward the next key.
        [[nodiscard]] float getTangentOutProperty() const;
        void setTangentOutProperty(float value);

        /// Gets or sets the value of this key.
        [[nodiscard]] float getValueProperty() const;
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

        friend bool operator==(const CurveKey& a, const CurveKey& b);
        friend bool operator!=(const CurveKey& a, const CurveKey& b);

    private:
        float position;
        float value;
        float tangentIn;
        float tangentOut;
        CurveContinuity continuity;
    };
}
