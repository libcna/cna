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
     * @brief Packed vector type storing two signed normalized 16-bit integers (XY) in a 32-bit value.
     */
    struct NormalizedShort2 : public IPackedVectorT<uint32_t>
    {
        /** @brief Constructs a NormalizedShort2 with a packed value of zero. */
        NormalizedShort2() : packedValue_(0) {}

        /**
         * @brief Constructs a NormalizedShort2 from normalized X and Y floats in [-1, 1].
         * @param x The x component in [-1, 1].
         * @param y The y component in [-1, 1].
         */
        NormalizedShort2(float x, float y) : packedValue_(Pack(x, y)) {}

        /**
         * @brief Constructs a NormalizedShort2 from a Vector2 with components in [-1, 1].
         * @param vector Vector containing the XY components.
         */
        NormalizedShort2(Vector2 vector) : packedValue_(Pack(vector.X, vector.Y)) {}

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
         * @brief Packs the XY components of a Vector4 as signed normalized 16-bit integers.
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
                CNA::Internal::UnpackSNorm(0xFFFF, packedValue_),
                CNA::Internal::UnpackSNorm(0xFFFF, packedValue_ >> 16)
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
         * @return @c true if @p obj holds an equal NormalizedShort2; @c false otherwise.
         */
        [[nodiscard]] bool Equals(const std::any& obj) const
        {
            const NormalizedShort2* other = std::any_cast<NormalizedShort2>(&obj);
            return other != nullptr && Equals(*other);
        }

        /**
         * @brief Compares this value with another NormalizedShort2 for equality.
         * @param other The NormalizedShort2 to compare against.
         * @return @c true if both packed values are equal; @c false otherwise.
         */
        [[nodiscard]] bool Equals(const NormalizedShort2& other) const
        {
            return packedValue_ == other.packedValue_;
        }

        /**
         * @brief Returns true if both NormalizedShort2 values are equal.
         * @param o The other NormalizedShort2 to compare.
         * @return True if equal.
         */
        bool operator==(const NormalizedShort2& o) const { return packedValue_ == o.packedValue_; }

        /**
         * @brief Returns true if both NormalizedShort2 values are not equal.
         * @param o The other NormalizedShort2 to compare.
         * @return True if not equal.
         */
        bool operator!=(const NormalizedShort2& o) const { return !(*this == o); }

    private:
        uint32_t packedValue_;
        static uint32_t Pack(float x, float y) {
            // XNA saturates the channel and rounds it to the nearest integer with ties to even
        // (.NET Math.Round), where FNA rounds a tie away from zero and a NaN channel reaches an
        // integer cast undefined in C++. Measured on the XNA 4.0 runtime:
        // tests/reference/xna40/framework/framework-packing-oracle.json, cases packed/*/ties,
        // packed/*/negative_ties and packed/*/nan_and_infinities.

            auto xi = static_cast<uint16_t>(static_cast<int16_t>(CNA::Internal::ClampAndRound(std::clamp(x, -1.0f, 1.0f) * 32767.0f, -32767.0f, 32767.0f)));
            auto yi = static_cast<uint16_t>(static_cast<int16_t>(CNA::Internal::ClampAndRound(std::clamp(y, -1.0f, 1.0f) * 32767.0f, -32767.0f, 32767.0f)));
            return static_cast<uint32_t>(xi) | (static_cast<uint32_t>(yi) << 16);
        }
    };
}
