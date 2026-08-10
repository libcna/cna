// SPDX-License-Identifier: MS-PL
#pragma once
#include <cstdint>
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
        [[nodiscard]] Vector4 ToVector4() const
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
