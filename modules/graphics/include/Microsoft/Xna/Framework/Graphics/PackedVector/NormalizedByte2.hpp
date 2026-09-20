// SPDX-License-Identifier: MS-PL
#pragma once
#include <any>
#include <cstdint>
#include <string>
#include <algorithm>
#include <cmath>
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Vector4.hpp"
#include "Microsoft/Xna/Framework/Graphics/PackedVector/IPackedVector.hpp"
#include "CNA/Internal/PackedRounding.hpp"

namespace Microsoft::Xna::Framework::Graphics::PackedVector
{
    /**
     * @brief Packed vector type storing two signed normalized bytes (XY) in a 16-bit value.
     */
    struct NormalizedByte2 : public IPackedVectorT<uint16_t>
    {
        /** @brief Constructs a NormalizedByte2 with a packed value of zero. */
        NormalizedByte2() : packedValue_(0) {}

        /**
         * @brief Constructs a NormalizedByte2 from normalized X and Y floats in [-1, 1].
         * @param x The x component in [-1, 1].
         * @param y The y component in [-1, 1].
         */
        NormalizedByte2(float x, float y) : packedValue_(Pack(x, y)) {}

        /**
         * @brief Constructs a NormalizedByte2 from a Vector2 with components in [-1, 1].
         * @param vector Vector containing the XY components.
         */
        NormalizedByte2(Vector2 vector) : packedValue_(Pack(vector.X, vector.Y)) {}

        /**
         * @brief Gets the packed 16-bit value.
         * @return The packed 16-bit value.
         */
        [[nodiscard]] uint16_t getPackedValueProperty() const override { return packedValue_; }

        /**
         * @brief Sets the packed 16-bit value.
         * @param v The new packed 16-bit value.
         */
        void setPackedValueProperty(uint16_t v) override { packedValue_ = v; }

        /**
         * @brief Packs the XY components of a Vector4 as signed normalized bytes.
         * @param v Vector whose XY components are packed.
         */
        void PackFromVector4(const Vector4& v) override { packedValue_ = Pack(v.X, v.Y); }

        /**
         * @brief Expands the packed representation into a Vector2.
         * @return The two signed normalized channels as {X, Y}.
         */
        [[nodiscard]] Vector2 ToVector2() const
        {
            return {
                CNA::Internal::UnpackSNorm(0xFF, static_cast<std::uint32_t>(packedValue_)),
                CNA::Internal::UnpackSNorm(0xFF, static_cast<std::uint32_t>(packedValue_) >> 8)
            };
        }

        /**
         * @brief Expands the packed value to a Vector4 with Z = 0, W = 1.
         * @return The unpacked Vector4.
         */
        [[nodiscard]] Vector4 ToVector4() const override
        {
            const Vector2 expanded = ToVector2();
            return {expanded.X, expanded.Y, 0.0f, 1.0f};
        }

        /**
         * @brief Returns a string representation of this value.
         * @return A string holding the packed value as 4 uppercase hexadecimal digits.
         */
        [[nodiscard]] std::string ToString() const;

        /**
         * @brief Returns a hash code for this value.
         * @return A hash code derived from the packed value: the packed value itself, which is
         *         what `System.UInt16.GetHashCode()` returns.
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
         * @return @c true if @p obj holds an equal NormalizedByte2; @c false otherwise.
         */
        [[nodiscard]] bool Equals(const std::any& obj) const
        {
            const NormalizedByte2* other = std::any_cast<NormalizedByte2>(&obj);
            return other != nullptr && Equals(*other);
        }

        /**
         * @brief Compares this value with another NormalizedByte2 for equality.
         * @param other The NormalizedByte2 to compare against.
         * @return @c true if both packed values are equal; @c false otherwise.
         */
        [[nodiscard]] bool Equals(const NormalizedByte2& other) const
        {
            return packedValue_ == other.packedValue_;
        }

        /**
         * @brief Returns true if both NormalizedByte2 values are equal.
         * @param o The other NormalizedByte2 to compare.
         * @return True if equal.
         */
        bool operator==(const NormalizedByte2& o) const { return packedValue_ == o.packedValue_; }

        /**
         * @brief Returns true if both NormalizedByte2 values are not equal.
         * @param o The other NormalizedByte2 to compare.
         * @return True if not equal.
         */
        bool operator!=(const NormalizedByte2& o) const { return !(*this == o); }

    private:
        uint16_t packedValue_;
        static uint16_t Pack(float x, float y) {
            // XNA saturates the channel and rounds it to the nearest integer with ties to even
        // (.NET Math.Round), where FNA rounds a tie away from zero and a NaN channel reaches an
        // integer cast undefined in C++. Measured on the XNA 4.0 runtime:
        // tests/reference/xna40/framework/framework-packing-oracle.json, cases packed/*/ties,
        // packed/*/negative_ties and packed/*/nan_and_infinities.

            auto xi = static_cast<uint8_t>(static_cast<int8_t>(CNA::Internal::ClampAndRound(std::clamp(x, -1.0f, 1.0f) * 127.0f, -127.0f, 127.0f)));
            auto yi = static_cast<uint8_t>(static_cast<int8_t>(CNA::Internal::ClampAndRound(std::clamp(y, -1.0f, 1.0f) * 127.0f, -127.0f, 127.0f)));
            return static_cast<uint16_t>(xi | (yi << 8));
        }
    };
}
