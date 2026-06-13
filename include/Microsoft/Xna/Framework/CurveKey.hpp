// SPDX-License-Identifier: MS-PL

#pragma once

#include <cstddef>

#include "System/IEquatable.hpp"
#include "System/IComparable.hpp"
#include "Microsoft/Xna/Framework/CurveContinuity.hpp"

namespace Microsoft::Xna::Framework
{
    /** @brief Key point on a Curve. */
    class CurveKey : public System::IEquatable<CurveKey>,
                     public System::IComparable<CurveKey>
    {
    public:
        /**
         * @brief Gets whether the segment between this key and the next key is smooth or stepped.
         * @return The continuity type for this key.
         */
        [[nodiscard]] CurveContinuity getContinuityProperty() const;

        /**
         * @brief Sets whether the segment between this key and the next key is smooth or stepped.
         * @param value The desired continuity type.
         */
        void setContinuityProperty(CurveContinuity value);

        /**
         * @brief Gets the position of this key on the curve.
         * @return The position value.
         */
        [[nodiscard]] float getPositionProperty() const;

        /**
         * @brief Gets the tangent used when approaching this key from the previous key.
         * @return The incoming tangent value.
         */
        [[nodiscard]] float getTangentInProperty() const;

        /**
         * @brief Sets the tangent used when approaching this key from the previous key.
         * @param value The new incoming tangent value.
         */
        void setTangentInProperty(float value);

        /**
         * @brief Gets the tangent used when leaving this key toward the next key.
         * @return The outgoing tangent value.
         */
        [[nodiscard]] float getTangentOutProperty() const;

        /**
         * @brief Sets the tangent used when leaving this key toward the next key.
         * @param value The new outgoing tangent value.
         */
        void setTangentOutProperty(float value);

        /**
         * @brief Gets the value of this key.
         * @return The curve value at this key's position.
         */
        [[nodiscard]] float getValueProperty() const;

        /**
         * @brief Sets the value of this key.
         * @param value The new curve value.
         */
        void setValueProperty(float value);

        /**
         * @brief Creates a key with zero tangents and smooth continuity.
         * @param position Position on the curve.
         * @param value Value of the control point.
         */
        CurveKey(float position, float value);

        /**
         * @brief Creates a key with explicit tangents and smooth continuity.
         * @param position Position on the curve.
         * @param value Value of the control point.
         * @param tangentIn Tangent approaching this point from the previous point.
         * @param tangentOut Tangent leaving this point toward the next point.
         */
        CurveKey(float position, float value, float tangentIn, float tangentOut);

        /**
         * @brief Creates a key with explicit tangents and continuity.
         * @param position Position on the curve.
         * @param value Value of the control point.
         * @param tangentIn Tangent approaching this point from the previous point.
         * @param tangentOut Tangent leaving this point toward the next point.
         * @param continuity Indicates whether the curve is discrete or continuous at this key.
         */
        CurveKey(float position, float value, float tangentIn, float tangentOut, CurveContinuity continuity);

        /**
         * @brief Creates a copy of this key.
         * @return A copy of this key.
         */
        [[nodiscard]] CurveKey Clone() const;

        /**
         * @brief Compares this key position with another key position.
         * @param other The other key to compare with.
         * @return Negative, zero, or positive based on position comparison.
         */
        [[nodiscard]] int CompareTo(const CurveKey& other) const override;

        /**
         * @brief Compares this key with another key for equality.
         * @param other The other key to compare with.
         * @return true if all fields are equal; false otherwise.
         */
        [[nodiscard]] bool Equals(const CurveKey& other) const override;

        /**
         * @brief Gets a hash code from the key fields.
         * @return A hash code combining all key fields.
         */
        [[nodiscard]] std::size_t GetHashCode() const;

        /**
         * @brief Returns true when all fields of both keys are equal.
         * @param a The left-hand operand.
         * @param b The right-hand operand.
         * @return true if the keys are equal.
         */
        friend bool operator==(const CurveKey& a, const CurveKey& b);

        /**
         * @brief Returns true when any field differs between the keys.
         * @param a The left-hand operand.
         * @param b The right-hand operand.
         * @return true if the keys are not equal.
         */
        friend bool operator!=(const CurveKey& a, const CurveKey& b);

    private:
        float position;
        float value;
        float tangentIn;
        float tangentOut;
        CurveContinuity continuity;
    };
}
