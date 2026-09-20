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
     * @brief Packed vector type storing four unsigned byte channels in a 32-bit value.
     */
    struct Byte4 : public IPackedVectorT<uint32_t>
    {
        /** @brief Constructs a Byte4 with a packed value of zero. */
        Byte4() : packedValue_(0) {}

        /**
         * @brief Constructs a Byte4 from four float values in [0, 255].
         * @param x The x component in [0, 255].
         * @param y The y component in [0, 255].
         * @param z The z component in [0, 255].
         * @param w The w component in [0, 255].
         */
        Byte4(float x, float y, float z, float w) : packedValue_(Pack(x, y, z, w)) {}

        /**
         * @brief Constructs a Byte4 from a Vector4 with components in [0, 255].
         * @param vector Vector containing the XYZW components.
         */
        Byte4(Vector4 vector) : packedValue_(Pack(vector.X, vector.Y, vector.Z, vector.W)) {}

        /**
         * @brief Constructs a Byte4 from a raw 32-bit packed value.
         * @param packed The raw packed value.
         */
        explicit Byte4(uint32_t packed) : packedValue_(packed) {}

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
         * @brief Packs the XYZW components of a Vector4 into this Byte4.
         * @param v Vector containing the XYZW components in [0, 255].
         */
        void PackFromVector4(const Vector4& v) override { packedValue_ = Pack(v.X, v.Y, v.Z, v.W); }

        /**
         * @brief Expands the packed value to a Vector4 with each component in [0, 255].
         * @return The unpacked Vector4.
         */
        [[nodiscard]] Vector4 ToVector4() const override
        {
            return {
                 (packedValue_        & 0xFF) * 1.0f,
                ((packedValue_ >>  8) & 0xFF) * 1.0f,
                ((packedValue_ >> 16) & 0xFF) * 1.0f,
                ((packedValue_ >> 24) & 0xFF) * 1.0f
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
         * @return @c true if @p obj holds an equal Byte4; @c false otherwise.
         */
        [[nodiscard]] bool Equals(const std::any& obj) const
        {
            const Byte4* other = std::any_cast<Byte4>(&obj);
            return other != nullptr && Equals(*other);
        }

        /**
         * @brief Compares this value with another Byte4 for equality.
         * @param other The Byte4 to compare against.
         * @return @c true if both packed values are equal; @c false otherwise.
         */
        [[nodiscard]] bool Equals(const Byte4& other) const
        {
            return packedValue_ == other.packedValue_;
        }

        /**
         * @brief Returns true if both Byte4 values are equal.
         * @param o The other Byte4 to compare.
         * @return True if equal.
         */
        bool operator==(const Byte4& o) const { return packedValue_ == o.packedValue_; }

        /**
         * @brief Returns true if both Byte4 values are not equal.
         * @param o The other Byte4 to compare.
         * @return True if not equal.
         */
        bool operator!=(const Byte4& o) const { return !(*this == o); }

    private:
        uint32_t packedValue_;
        static uint32_t Pack(float x, float y, float z, float w) {
            // XNA saturates the channel and rounds it to the nearest integer with ties to even
        // (.NET Math.Round), where FNA rounds a tie away from zero and a NaN channel reaches an
        // integer cast undefined in C++. Measured on the XNA 4.0 runtime:
        // tests/reference/xna40/framework/framework-packing-oracle.json, cases packed/*/ties,
        // packed/*/negative_ties and packed/*/nan_and_infinities.

            auto xi = static_cast<uint32_t>(CNA::Internal::ClampAndRound(x, 0.0f, 255.0f));
            auto yi = static_cast<uint32_t>(CNA::Internal::ClampAndRound(y, 0.0f, 255.0f));
            auto zi = static_cast<uint32_t>(CNA::Internal::ClampAndRound(z, 0.0f, 255.0f));
            auto wi = static_cast<uint32_t>(CNA::Internal::ClampAndRound(w, 0.0f, 255.0f));
            return xi | (yi << 8) | (zi << 16) | (wi << 24);
        }
    };
}
