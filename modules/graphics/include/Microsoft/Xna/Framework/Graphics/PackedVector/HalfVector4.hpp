// SPDX-License-Identifier: MS-PL
#pragma once
#include <any>
#include <cstdint>
#include <string>
#include "Microsoft/Xna/Framework/Vector4.hpp"
#include "Microsoft/Xna/Framework/Graphics/PackedVector/IPackedVector.hpp"
#include "Microsoft/Xna/Framework/Graphics/PackedVector/HalfTypeHelper.hpp"

namespace Microsoft::Xna::Framework::Graphics::PackedVector
{
    /**
     * @brief Packed vector type storing four half-precision floats in a 64-bit value.
     */
    struct HalfVector4 : public IPackedVectorT<uint64_t>
    {
        /** @brief Constructs a HalfVector4 with a packed value of zero. */
        HalfVector4() : packedValue_(0) {}

        /**
         * @brief Constructs a HalfVector4 from X, Y, Z, W float components.
         * @param x The x component.
         * @param y The y component.
         * @param z The z component.
         * @param w The w component.
         */
        HalfVector4(float x, float y, float z, float w) : packedValue_(Pack(x, y, z, w)) {}

        /**
         * @brief Constructs a HalfVector4 from a Vector4.
         * @param vector The Vector4 to pack.
         */
        HalfVector4(Vector4 vector) : packedValue_(Pack(vector.X, vector.Y, vector.Z, vector.W)) {}

        /**
         * @brief Gets the packed 64-bit value containing four half-precision floats.
         * @return The packed 64-bit value.
         */
        [[nodiscard]] uint64_t getPackedValueProperty() const override { return packedValue_; }

        /**
         * @brief Sets the packed 64-bit value.
         * @param v The new packed 64-bit value.
         */
        void setPackedValueProperty(uint64_t v) override { packedValue_ = v; }

        /**
         * @brief Packs the XYZW components of a Vector4 as half-precision floats.
         * @param v The Vector4 to pack.
         */
        void PackFromVector4(const Vector4& v) override { packedValue_ = Pack(v.X, v.Y, v.Z, v.W); }

        /**
         * @brief Expands the packed value to a Vector4.
         * @return The unpacked Vector4.
         */
        [[nodiscard]] Vector4 ToVector4() const override
        {
            return {
                HalfTypeHelper::Convert(static_cast<uint16_t>( packedValue_        & 0xFFFF)),
                HalfTypeHelper::Convert(static_cast<uint16_t>((packedValue_ >> 16) & 0xFFFF)),
                HalfTypeHelper::Convert(static_cast<uint16_t>((packedValue_ >> 32) & 0xFFFF)),
                HalfTypeHelper::Convert(static_cast<uint16_t>((packedValue_ >> 48) & 0xFFFF))
            };
        }

        /**
         * @brief Returns a string representation of this value.
         * @return A string holding the expanded Vector4, in that type's own `{X:... Y:... Z:... W:...}` form.
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
         * @return @c true if @p obj holds an equal HalfVector4; @c false otherwise.
         */
        [[nodiscard]] bool Equals(const std::any& obj) const
        {
            const HalfVector4* other = std::any_cast<HalfVector4>(&obj);
            return other != nullptr && Equals(*other);
        }

        /**
         * @brief Compares this value with another HalfVector4 for equality.
         * @param other The HalfVector4 to compare against.
         * @return @c true if both packed values are equal; @c false otherwise.
         */
        [[nodiscard]] bool Equals(const HalfVector4& other) const
        {
            return packedValue_ == other.packedValue_;
        }

        /**
         * @brief Returns true if both HalfVector4 values are equal.
         * @param o The other HalfVector4 to compare.
         * @return True if equal.
         */
        bool operator==(const HalfVector4& o) const { return packedValue_ == o.packedValue_; }

        /**
         * @brief Returns true if both HalfVector4 values are not equal.
         * @param o The other HalfVector4 to compare.
         * @return True if not equal.
         */
        bool operator!=(const HalfVector4& o) const { return !(*this == o); }

    private:
        uint64_t packedValue_;
        static uint64_t Pack(float x, float y, float z, float w)
        {
            return static_cast<uint64_t>(HalfTypeHelper::Convert(x))       |
                   (static_cast<uint64_t>(HalfTypeHelper::Convert(y)) << 16) |
                   (static_cast<uint64_t>(HalfTypeHelper::Convert(z)) << 32) |
                   (static_cast<uint64_t>(HalfTypeHelper::Convert(w)) << 48);
        }
    };
}
