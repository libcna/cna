// SPDX-License-Identifier: MS-PL
#pragma once
#include <any>
#include <cstdint>
#include <string>
#include <algorithm>
#include <cmath>
#include "Microsoft/Xna/Framework/Vector4.hpp"
#include "Microsoft/Xna/Framework/Graphics/PackedVector/IPackedVector.hpp"
#include "CNA/Internal/PackedRounding.hpp"

namespace Microsoft::Xna::Framework::Graphics::PackedVector
{
    /**
     * @brief Packed vector type storing four signed normalized 16-bit integers (XYZW) in a 64-bit value.
     */
    struct NormalizedShort4 : public IPackedVectorT<uint64_t>
    {
        /** @brief Constructs a NormalizedShort4 with a packed value of zero. */
        NormalizedShort4() : packedValue_(0) {}

        /**
         * @brief Constructs a NormalizedShort4 from normalized XYZW floats in [-1, 1].
         * @param x The x component in [-1, 1].
         * @param y The y component in [-1, 1].
         * @param z The z component in [-1, 1].
         * @param w The w component in [-1, 1].
         */
        NormalizedShort4(float x, float y, float z, float w) : packedValue_(Pack(x, y, z, w)) {}

        /**
         * @brief Constructs a NormalizedShort4 from a Vector4 with components in [-1, 1].
         * @param vector Vector containing the XYZW components.
         */
        NormalizedShort4(Vector4 vector) : packedValue_(Pack(vector.X, vector.Y, vector.Z, vector.W)) {}

        /**
         * @brief Gets the packed 64-bit value.
         * @return The packed 64-bit value.
         */
        [[nodiscard]] uint64_t getPackedValueProperty() const override { return packedValue_; }

        /**
         * @brief Sets the packed 64-bit value.
         * @param v The new packed 64-bit value.
         */
        void setPackedValueProperty(uint64_t v) override { packedValue_ = v; }

        /**
         * @brief Packs the XYZW components of a Vector4 as signed normalized 16-bit integers.
         * @param v The Vector4 to pack.
         */
        void PackFromVector4(const Vector4& v) override { packedValue_ = Pack(v.X, v.Y, v.Z, v.W); }

        /**
         * @brief Expands the packed value to a normalized Vector4.
         * @return The unpacked Vector4.
         */
        [[nodiscard]] Vector4 ToVector4() const override
        {
            return {
                CNA::Internal::UnpackSNorm(0xFFFF, static_cast<std::uint32_t>(packedValue_)),
                CNA::Internal::UnpackSNorm(0xFFFF, static_cast<std::uint32_t>(packedValue_ >> 16)),
                CNA::Internal::UnpackSNorm(0xFFFF, static_cast<std::uint32_t>(packedValue_ >> 32)),
                CNA::Internal::UnpackSNorm(0xFFFF, static_cast<std::uint32_t>(packedValue_ >> 48))
            };
        }

        /**
         * @brief Returns a string representation of this value.
         * @return A string holding the packed value as 16 uppercase hexadecimal digits.
         */
        [[nodiscard]] std::string ToString() const;

        /**
         * @brief Returns a hash code for this value.
         * @return A hash code derived from the packed value: the packed value's low half XORed with its high half, which is what
         * `System.UInt64.GetHashCode()` returns.
         */
        [[nodiscard]] int GetHashCode() const
        {
            return static_cast<int>(static_cast<std::int32_t>(static_cast<std::uint32_t>(packedValue_)
                                            ^ static_cast<std::uint32_t>(packedValue_ >> 32)));
        }

        /**
         * @brief Compares this value with a boxed object for equality.
         *
         * Mirrors the CLR `Equals(object)` contract: an empty object, or an object holding a
         * different type, is unequal; otherwise the two packed values are compared.
         *
         * @param obj The boxed object to compare against.
         * @return @c true if @p obj holds an equal NormalizedShort4; @c false otherwise.
         */
        [[nodiscard]] bool Equals(const std::any& obj) const
        {
            const NormalizedShort4* other = std::any_cast<NormalizedShort4>(&obj);
            return other != nullptr && Equals(*other);
        }

        /**
         * @brief Compares this value with another NormalizedShort4 for equality.
         * @param other The NormalizedShort4 to compare against.
         * @return @c true if both packed values are equal; @c false otherwise.
         */
        [[nodiscard]] bool Equals(const NormalizedShort4& other) const
        {
            return packedValue_ == other.packedValue_;
        }

        /**
         * @brief Returns true if both NormalizedShort4 values are equal.
         * @param o The other NormalizedShort4 to compare.
         * @return True if equal.
         */
        bool operator==(const NormalizedShort4& o) const { return packedValue_ == o.packedValue_; }

        /**
         * @brief Returns true if both NormalizedShort4 values are not equal.
         * @param o The other NormalizedShort4 to compare.
         * @return True if not equal.
         */
        bool operator!=(const NormalizedShort4& o) const { return !(*this == o); }

    private:
        uint64_t packedValue_;
        static uint64_t Pack(float x, float y, float z, float w) {
            // XNA saturates the channel and rounds it to the nearest integer with ties to even
        // (.NET Math.Round), where FNA rounds a tie away from zero and a NaN channel reaches an
        // integer cast undefined in C++. Measured on the XNA 4.0 runtime:
        // tests/reference/xna40/framework/framework-packing-oracle.json, cases packed/*/ties,
        // packed/*/negative_ties and packed/*/nan_and_infinities.

            auto xi = static_cast<uint16_t>(static_cast<int16_t>(CNA::Internal::ClampAndRound(std::clamp(x, -1.0f, 1.0f) * 32767.0f, -32767.0f, 32767.0f)));
            auto yi = static_cast<uint16_t>(static_cast<int16_t>(CNA::Internal::ClampAndRound(std::clamp(y, -1.0f, 1.0f) * 32767.0f, -32767.0f, 32767.0f)));
            auto zi = static_cast<uint16_t>(static_cast<int16_t>(CNA::Internal::ClampAndRound(std::clamp(z, -1.0f, 1.0f) * 32767.0f, -32767.0f, 32767.0f)));
            auto wi = static_cast<uint16_t>(static_cast<int16_t>(CNA::Internal::ClampAndRound(std::clamp(w, -1.0f, 1.0f) * 32767.0f, -32767.0f, 32767.0f)));
            return static_cast<uint64_t>(xi) | (static_cast<uint64_t>(yi)<<16) | (static_cast<uint64_t>(zi)<<32) | (static_cast<uint64_t>(wi)<<48);
        }
    };
}
