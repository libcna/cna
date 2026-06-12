// SPDX-License-Identifier: MS-PL
#pragma once
#include <cstdint>
#include <algorithm>
#include "Microsoft/Xna/Framework/Vector4.hpp"
#include "Microsoft/Xna/Framework/Graphics/PackedVector/IPackedVector.hpp"

namespace Microsoft::Xna::Framework::Graphics::PackedVector
{
    /// Packed vector type storing four signed normalized bytes (XYZW) in a 32-bit value.
    struct NormalizedByte4 : public IPackedVectorT<uint32_t>
    {
        /// Constructs a NormalizedByte4 with a packed value of zero.
        NormalizedByte4() : packedValue_(0) {}
        /// Constructs a NormalizedByte4 from normalized X, Y, Z, W floats in [-1, 1].
        NormalizedByte4(float x, float y, float z, float w) : packedValue_(Pack(x, y, z, w)) {}
        /// Constructs a NormalizedByte4 from a Vector4 with components in [-1, 1].
        NormalizedByte4(Vector4 vector) : packedValue_(Pack(vector.X, vector.Y, vector.Z, vector.W)) {}

        /// Gets the packed 32-bit value.
        [[nodiscard]] uint32_t getPackedValueProperty() const override { return packedValue_; }
        /// Sets the packed 32-bit value.
        void setPackedValueProperty(uint32_t v) override { packedValue_ = v; }

        /// Packs the XYZW components of a Vector4 as signed normalized bytes.
        void PackFromVector4(const Vector4& v) override { packedValue_ = Pack(v.X, v.Y, v.Z, v.W); }
        /// Expands the packed value to a normalized Vector4.
        [[nodiscard]] Vector4 ToVector4() const
        {
            return {
                static_cast<int8_t>( packedValue_        & 0xFF) / 127.0f,
                static_cast<int8_t>((packedValue_ >>  8) & 0xFF) / 127.0f,
                static_cast<int8_t>((packedValue_ >> 16) & 0xFF) / 127.0f,
                static_cast<int8_t>((packedValue_ >> 24) & 0xFF) / 127.0f
            };
        }

        /// Returns true if both values are equal.
        bool operator==(const NormalizedByte4& o) const { return packedValue_ == o.packedValue_; }
        /// Returns true if both values are not equal.
        bool operator!=(const NormalizedByte4& o) const { return !(*this == o); }

    private:
        uint32_t packedValue_;
        static uint32_t Pack(float x, float y, float z, float w) {
            auto xi = static_cast<uint8_t>(static_cast<int8_t>(std::clamp(x,-1.f,1.f)*127.f));
            auto yi = static_cast<uint8_t>(static_cast<int8_t>(std::clamp(y,-1.f,1.f)*127.f));
            auto zi = static_cast<uint8_t>(static_cast<int8_t>(std::clamp(z,-1.f,1.f)*127.f));
            auto wi = static_cast<uint8_t>(static_cast<int8_t>(std::clamp(w,-1.f,1.f)*127.f));
            return static_cast<uint32_t>(xi) | (static_cast<uint32_t>(yi)<<8) | (static_cast<uint32_t>(zi)<<16) | (static_cast<uint32_t>(wi)<<24);
        }
    };
}
