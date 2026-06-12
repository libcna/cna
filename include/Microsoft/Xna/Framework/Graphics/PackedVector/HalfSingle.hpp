// SPDX-License-Identifier: MS-PL
#pragma once
#include <cstdint>
#include "Microsoft/Xna/Framework/Vector4.hpp"
#include "Microsoft/Xna/Framework/Graphics/PackedVector/IPackedVector.hpp"
#include "Microsoft/Xna/Framework/Graphics/PackedVector/HalfTypeHelper.hpp"

namespace Microsoft::Xna::Framework::Graphics::PackedVector
{
    /// Packed vector type storing a single float value as a 16-bit half-precision float.
    struct HalfSingle : public IPackedVectorT<uint16_t>
    {
        /// Constructs a HalfSingle with a packed value of zero.
        HalfSingle() : packedValue_(0) {}
        /// Constructs a HalfSingle from a 32-bit float value.
        explicit HalfSingle(float single) : packedValue_(HalfTypeHelper::Convert(single)) {}
        /// Constructs a HalfSingle from a raw 16-bit packed value.
        explicit HalfSingle(uint16_t packed) : packedValue_(packed) {}

        /// Gets the packed 16-bit half-precision value.
        [[nodiscard]] uint16_t getPackedValueProperty() const override { return packedValue_; }
        /// Sets the packed 16-bit half-precision value.
        void setPackedValueProperty(uint16_t v) override { packedValue_ = v; }

        /// Packs the X component of a Vector4 as a half-precision float.
        void PackFromVector4(const Vector4& v) override { packedValue_ = HalfTypeHelper::Convert(v.X); }
        /// Expands the packed value to a Vector4 with X = value, Y = 0, Z = 0, W = 1.
        [[nodiscard]] Vector4 ToVector4() const { return {ToSingle(), 0.0f, 0.0f, 1.0f}; }
        /// Converts the packed half-precision value back to a 32-bit float.
        [[nodiscard]] float ToSingle() const { return HalfTypeHelper::Convert(packedValue_); }

        /// Returns true if both values are equal.
        bool operator==(const HalfSingle& o) const { return packedValue_ == o.packedValue_; }
        /// Returns true if both values are not equal.
        bool operator!=(const HalfSingle& o) const { return !(*this == o); }

    private:
        uint16_t packedValue_;
    };
}
