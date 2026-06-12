// SPDX-License-Identifier: MS-PL
#pragma once
#include <cstdint>
#include "Microsoft/Xna/Framework/Vector4.hpp"
#include "Microsoft/Xna/Framework/Graphics/PackedVector/IPackedVector.hpp"
#include "Microsoft/Xna/Framework/Graphics/PackedVector/HalfTypeHelper.hpp"

namespace Microsoft::Xna::Framework::Graphics::PackedVector
{
    /// Packed vector type storing four half-precision floats in a 64-bit value.
    struct HalfVector4 : public IPackedVectorT<uint64_t>
    {
        /// Constructs a HalfVector4 with a packed value of zero.
        HalfVector4() : packedValue_(0) {}
        /// Constructs a HalfVector4 from X, Y, Z, W float components.
        HalfVector4(float x, float y, float z, float w) : packedValue_(Pack(x, y, z, w)) {}
        /// Constructs a HalfVector4 from a Vector4.
        HalfVector4(Vector4 vector) : packedValue_(Pack(vector.X, vector.Y, vector.Z, vector.W)) {}

        /// Gets the packed 64-bit value containing four half-precision floats.
        [[nodiscard]] uint64_t getPackedValueProperty() const override { return packedValue_; }
        /// Sets the packed 64-bit value.
        void setPackedValueProperty(uint64_t v) override { packedValue_ = v; }

        /// Packs the XYZW components of a Vector4 as half-precision floats.
        void PackFromVector4(const Vector4& v) override { packedValue_ = Pack(v.X, v.Y, v.Z, v.W); }
        /// Expands the packed value to a Vector4.
        [[nodiscard]] Vector4 ToVector4() const
        {
            return {
                HalfTypeHelper::Convert(static_cast<uint16_t>( packedValue_        & 0xFFFF)),
                HalfTypeHelper::Convert(static_cast<uint16_t>((packedValue_ >> 16) & 0xFFFF)),
                HalfTypeHelper::Convert(static_cast<uint16_t>((packedValue_ >> 32) & 0xFFFF)),
                HalfTypeHelper::Convert(static_cast<uint16_t>((packedValue_ >> 48) & 0xFFFF))
            };
        }

        /// Returns true if both values are equal.
        bool operator==(const HalfVector4& o) const { return packedValue_ == o.packedValue_; }
        /// Returns true if both values are not equal.
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
