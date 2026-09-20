// SPDX-License-Identifier: MS-PL
#pragma once
#include <any>
#include <cstdint>
#include <string>
#include <algorithm>
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Vector4.hpp"
#include "Microsoft/Xna/Framework/Graphics/PackedVector/IPackedVector.hpp"
#include "CNA/Internal/PackedRounding.hpp"

namespace Microsoft::Xna::Framework::Graphics::PackedVector
{
    /**
     * @brief Packed vector type storing red and green channels as two 16-bit unsigned integers in a 32-bit value.
     */
    struct Rg32 : public IPackedVectorT<uint32_t>
    {
        /** @brief Constructs a Rg32 with a packed value of zero. */
        Rg32() : packedValue_(0) {}

        /**
         * @brief Constructs a Rg32 from normalized red and green floats in [0, 1].
         * @param r The red component in [0, 1].
         * @param g The green component in [0, 1].
         */
        Rg32(float r, float g) : packedValue_(Pack(r, g)) {}

        /**
         * @brief Constructs a Rg32 from a Vector2 with components in [0, 1].
         * @param vector Vector containing the RG components.
         */
        Rg32(Vector2 vector) : packedValue_(Pack(vector.X, vector.Y)) {}

        /**
         * @brief Gets the packed 32-bit value.
         * @return The packed 32-bit value.
         */
        [[nodiscard]] uint32_t getPackedValueProperty() const override { return packedValue_; }

        /**
         * @brief Sets the packed 32-bit value.
         * @param v The new packed 32-bit value.
         */
        void setPackedValueProperty(uint32_t v) override { packedValue_ = v; }

        /**
         * @brief Packs the XY components of a Vector4 as unsigned 16-bit normalized channels.
         * @param v Vector whose XY components are packed.
         */
        void PackFromVector4(const Vector4& v) override { packedValue_ = Pack(v.X, v.Y); }

        /**
         * @brief Expands the packed representation into a Vector2.
         * @return The two unsigned normalized channels as {X, Y}.
         */
        [[nodiscard]] Vector2 ToVector2() const
        {
            return {
                CNA::Internal::UnpackUNorm(0xFFFF, packedValue_),
                CNA::Internal::UnpackUNorm(0xFFFF, packedValue_ >> 16)
            };
        }

        /**
         * @brief Expands the packed value to a Vector4 with Z = 0, W = 1.
         * @return The unpacked Vector4.
         */
        [[nodiscard]] Vector4 ToVector4() const override
        {
            return {
                (packedValue_ & 0xFFFF) / 65535.0f,
                (packedValue_ >> 16)    / 65535.0f,
                0.0f, 1.0f
            };
        }

        /**
         * @brief Returns a string representation of this value.
         * @return A string holding the packed value as 8 uppercase hexadecimal digits.
         */
        [[nodiscard]] std::string ToString() const;

        /**
         * @brief Returns a hash code for this value.
         * @return A hash code derived from the packed value: the packed value reinterpreted as a signed 32-bit integer, which is what
         * `System.UInt32.GetHashCode()` returns.
         */
        [[nodiscard]] int GetHashCode() const
        {
            return static_cast<int>(static_cast<std::int32_t>(packedValue_));
        }

        /**
         * @brief Compares this value with a boxed object for equality.
         *
         * Mirrors the CLR `Equals(object)` contract: an empty object, or an object holding a
         * different type, is unequal; otherwise the two packed values are compared.
         *
         * @param obj The boxed object to compare against.
         * @return @c true if @p obj holds an equal Rg32; @c false otherwise.
         */
        [[nodiscard]] bool Equals(const std::any& obj) const
        {
            const Rg32* other = std::any_cast<Rg32>(&obj);
            return other != nullptr && Equals(*other);
        }

        /**
         * @brief Compares this value with another Rg32 for equality.
         * @param other The Rg32 to compare against.
         * @return @c true if both packed values are equal; @c false otherwise.
         */
        [[nodiscard]] bool Equals(const Rg32& other) const
        {
            return packedValue_ == other.packedValue_;
        }

        /**
         * @brief Returns true if both Rg32 values are equal.
         * @param o The other Rg32 to compare.
         * @return True if equal.
         */
        bool operator==(const Rg32& o) const { return packedValue_ == o.packedValue_; }

        /**
         * @brief Returns true if both Rg32 values are not equal.
         * @param o The other Rg32 to compare.
         * @return True if not equal.
         */
        bool operator!=(const Rg32& o) const { return !(*this == o); }

    private:
        uint32_t packedValue_;
        static uint32_t Pack(float r, float g) {
            // XNA saturates the channel and rounds it to the nearest integer with ties to even
            // (.NET Math.Round), where a "+ 0.5f then truncate" rounds a tie away from zero and a
            // NaN channel reaches an integer cast undefined in C++. Measured on the XNA 4.0
            // runtime: tests/reference/xna40/framework/framework-packing-oracle.json, cases
            // packed/*/ties and packed/*/nan_and_infinities.

            auto ri = static_cast<uint32_t>(CNA::Internal::ClampAndRound(std::clamp(r, 0.0f, 1.0f) * 65535.0f, 0.0f, 65535.0f));
            auto gi = static_cast<uint32_t>(CNA::Internal::ClampAndRound(std::clamp(g, 0.0f, 1.0f) * 65535.0f, 0.0f, 65535.0f));
            return ri | (gi << 16);
        }
    };
}
