// SPDX-License-Identifier: MS-PL
#pragma once
#include <cstdint>
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Vector4.hpp"
#include "Microsoft/Xna/Framework/Graphics/PackedVector/IPackedVector.hpp"
#include "Microsoft/Xna/Framework/Graphics/PackedVector/HalfTypeHelper.hpp"

namespace Microsoft::Xna::Framework::Graphics::PackedVector
{
    /// Packed vector type storing two half-precision floats in a 32-bit value.
    struct HalfVector2 : public IPackedVectorT<uint32_t>
    {
        /// Constructs a HalfVector2 with a packed value of zero.
        HalfVector2() : packedValue_(0) {}
        /// Constructs a HalfVector2 from X and Y float components.
        HalfVector2(float x, float y) : packedValue_(Pack(x, y)) {}
        /// Constructs a HalfVector2 from a Vector2.
        HalfVector2(Vector2 vector) : packedValue_(Pack(vector.X, vector.Y)) {}

        /// Gets the packed 32-bit value containing two half-precision floats.
        [[nodiscard]] uint32_t getPackedValueProperty() const override { return packedValue_; }
        /// Sets the packed 32-bit value.
        void setPackedValueProperty(uint32_t v) override { packedValue_ = v; }

        /// Packs the XY components of a Vector4 as half-precision floats.
        void PackFromVector4(const Vector4& v) override { packedValue_ = Pack(v.X, v.Y); }
        /// Expands the packed value to a Vector4 with Z = 0, W = 1.
        [[nodiscard]] Vector4 ToVector4() const
        {
            return {
                HalfTypeHelper::Convert(static_cast<uint16_t>(packedValue_ & 0xFFFF)),
                HalfTypeHelper::Convert(static_cast<uint16_t>(packedValue_ >> 16)),
                0.0f, 1.0f
            };
        }
        /// Expands the packed value to a Vector2.
        [[nodiscard]] Vector2 ToVector2() const
        {
            return {
                HalfTypeHelper::Convert(static_cast<uint16_t>(packedValue_ & 0xFFFF)),
                HalfTypeHelper::Convert(static_cast<uint16_t>(packedValue_ >> 16))
            };
        }

        /// Returns true if both values are equal.
        bool operator==(const HalfVector2& o) const { return packedValue_ == o.packedValue_; }
        /// Returns true if both values are not equal.
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
