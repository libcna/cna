// SPDX-License-Identifier: MS-PL
#pragma once
#include <any>
#include <cstdint>
#include <string>
#include <algorithm>
#include "Microsoft/Xna/Framework/Vector4.hpp"
#include "Microsoft/Xna/Framework/Graphics/PackedVector/IPackedVector.hpp"
#include "CNA/Internal/PackedRounding.hpp"

namespace Microsoft::Xna::Framework::Graphics::PackedVector
{
    /**
     * @brief Packed vector type containing a single alpha value as an 8-bit unsigned integer.
     */
    struct Alpha8 : public IPackedVectorT<uint8_t>
    {
        /** @brief Constructs an Alpha8 with a packed value of zero. */
        Alpha8() : packedValue_(0) {}

        /**
         * @brief Constructs an Alpha8 from a normalized alpha float in [0, 1].
         * @param alpha The alpha component in the range [0, 1].
         */
        explicit Alpha8(float alpha) : packedValue_(Pack(alpha)) {}

        /**
         * @brief Gets the packed 8-bit alpha value.
         * @return The packed byte value.
         */
        [[nodiscard]] uint8_t getPackedValueProperty() const override { return packedValue_; }

        /**
         * @brief Sets the packed 8-bit alpha value.
         * @param v The new packed byte value.
         */
        void setPackedValueProperty(uint8_t v) override { packedValue_ = v; }

        /**
         * @brief Packs the W component of a Vector4 as an 8-bit alpha value.
         * @param v Vector containing the alpha in the W component.
         */
        void PackFromVector4(const Vector4& v) override { packedValue_ = Pack(v.W); }

        /**
         * @brief Expands the packed value to a Vector4 with components {0, 0, 0, alpha}.
         * @return The unpacked Vector4.
         */
        [[nodiscard]] Vector4 ToVector4() const override { return {0.0f, 0.0f, 0.0f, packedValue_ / 255.0f}; }

        /**
         * @brief Returns the alpha as a normalized float in [0, 1].
         * @return The alpha component.
         */
        [[nodiscard]] float ToAlpha() const { return packedValue_ / 255.0f; }

        /**
         * @brief Returns a string representation of this value.
         * @return A string holding the packed value as 2 uppercase hexadecimal digits.
         */
        [[nodiscard]] std::string ToString() const;

        /**
         * @brief Returns a hash code for this value.
         * @return A hash code derived from the packed value: the packed byte itself, which is
         *         what `System.Byte.GetHashCode()` returns.
         */
        [[nodiscard]] int GetHashCode() const
        {
            return static_cast<int>(packedValue_);
        }

        /**
         * @brief Compares this value with a boxed object for equality.
         *
         * Mirrors the CLR `Equals(object)` contract: an empty object, or an object holding a
         * different type, is unequal; otherwise the two packed values are compared.
         *
         * @param obj The boxed object to compare against.
         * @return @c true if @p obj holds an equal Alpha8; @c false otherwise.
         */
        [[nodiscard]] bool Equals(const std::any& obj) const
        {
            const Alpha8* other = std::any_cast<Alpha8>(&obj);
            return other != nullptr && Equals(*other);
        }

        /**
         * @brief Compares this value with another Alpha8 for equality.
         * @param other The Alpha8 to compare against.
         * @return @c true if both packed values are equal; @c false otherwise.
         */
        [[nodiscard]] bool Equals(const Alpha8& other) const
        {
            return packedValue_ == other.packedValue_;
        }

        /**
         * @brief Returns true if both Alpha8 values are equal.
         * @param o The other Alpha8 to compare.
         * @return True if equal.
         */
        bool operator==(const Alpha8& o) const { return packedValue_ == o.packedValue_; }

        /**
         * @brief Returns true if both Alpha8 values are not equal.
         * @param o The other Alpha8 to compare.
         * @return True if not equal.
         */
        bool operator!=(const Alpha8& o) const { return !(*this == o); }

    private:
        uint8_t packedValue_;
        static uint8_t Pack(float v) {
            // XNA saturates the channel and rounds it to the nearest integer with ties to even
            // (.NET Math.Round), where a "+ 0.5f then truncate" rounds a tie away from zero and a
            // NaN channel reaches an integer cast undefined in C++. Measured on the XNA 4.0
            // runtime: tests/reference/xna40/framework/framework-packing-oracle.json, cases
            // packed/*/ties and packed/*/nan_and_infinities.

            return static_cast<uint8_t>(CNA::Internal::ClampAndRound(std::clamp(v, 0.0f, 1.0f) * 255.0f, 0.0f, 255.0f));
        }
    };
}
