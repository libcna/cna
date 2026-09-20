// SPDX-License-Identifier: MS-PL
#pragma once
#include <any>
#include <cstdint>
#include <string>
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Vector4.hpp"
#include "Microsoft/Xna/Framework/Graphics/PackedVector/IPackedVector.hpp"
#include "Microsoft/Xna/Framework/Graphics/PackedVector/HalfTypeHelper.hpp"

namespace Microsoft::Xna::Framework::Graphics::PackedVector
{
    /**
     * @brief Packed vector type storing two half-precision floats in a 32-bit value.
     */
    struct HalfVector2 : public IPackedVectorT<uint32_t>
    {
        /** @brief Constructs a HalfVector2 with a packed value of zero. */
        HalfVector2() : packedValue_(0) {}

        /**
         * @brief Constructs a HalfVector2 from X and Y float components.
         * @param x The x component.
         * @param y The y component.
         */
        HalfVector2(float x, float y) : packedValue_(Pack(x, y)) {}

        /**
         * @brief Constructs a HalfVector2 from a Vector2.
         * @param vector The Vector2 to pack.
         */
        HalfVector2(Vector2 vector) : packedValue_(Pack(vector.X, vector.Y)) {}

        /**
         * @brief Gets the packed 32-bit value containing two half-precision floats.
         * @return The packed 32-bit value.
         */
        [[nodiscard]] uint32_t getPackedValueProperty() const override { return packedValue_; }

        /**
         * @brief Sets the packed 32-bit value.
         * @param v The new packed 32-bit value.
         */
        void setPackedValueProperty(uint32_t v) override { packedValue_ = v; }

        /**
         * @brief Packs the XY components of a Vector4 as half-precision floats.
         * @param v Vector whose XY components are packed.
         */
        void PackFromVector4(const Vector4& v) override { packedValue_ = Pack(v.X, v.Y); }

        /**
         * @brief Expands the packed value to a Vector4 with Z = 0, W = 1.
         * @return The unpacked Vector4.
         */
        [[nodiscard]] Vector4 ToVector4() const override
        {
            return {
                HalfTypeHelper::Convert(static_cast<uint16_t>(packedValue_ & 0xFFFF)),
                HalfTypeHelper::Convert(static_cast<uint16_t>(packedValue_ >> 16)),
                0.0f, 1.0f
            };
        }

        /**
         * @brief Expands the packed value to a Vector2.
         * @return The unpacked Vector2.
         */
        [[nodiscard]] Vector2 ToVector2() const
        {
            return {
                HalfTypeHelper::Convert(static_cast<uint16_t>(packedValue_ & 0xFFFF)),
                HalfTypeHelper::Convert(static_cast<uint16_t>(packedValue_ >> 16))
            };
        }

        /**
         * @brief Returns a string representation of this value.
         * @return A string holding the expanded Vector2, in that type's own `{X:... Y:...}` form.
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
         * @return @c true if @p obj holds an equal HalfVector2; @c false otherwise.
         */
        [[nodiscard]] bool Equals(const std::any& obj) const
        {
            const HalfVector2* other = std::any_cast<HalfVector2>(&obj);
            return other != nullptr && Equals(*other);
        }

        /**
         * @brief Compares this value with another HalfVector2 for equality.
         * @param other The HalfVector2 to compare against.
         * @return @c true if both packed values are equal; @c false otherwise.
         */
        [[nodiscard]] bool Equals(const HalfVector2& other) const
        {
            return packedValue_ == other.packedValue_;
        }

        /**
         * @brief Returns true if both HalfVector2 values are equal.
         * @param o The other HalfVector2 to compare.
         * @return True if equal.
         */
        bool operator==(const HalfVector2& o) const { return packedValue_ == o.packedValue_; }

        /**
         * @brief Returns true if both HalfVector2 values are not equal.
         * @param o The other HalfVector2 to compare.
         * @return True if not equal.
         */
        bool operator!=(const HalfVector2& o) const { return !(*this == o); }

    private:
        uint32_t packedValue_;
        static uint32_t Pack(float x, float y)
        {
            return static_cast<uint32_t>(HalfTypeHelper::Convert(x)) |
                   (static_cast<uint32_t>(HalfTypeHelper::Convert(y)) << 16);
        }
    };
}
