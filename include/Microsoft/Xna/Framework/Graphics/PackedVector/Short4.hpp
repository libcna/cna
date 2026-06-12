// SPDX-License-Identifier: MS-PL
#pragma once
#include <cstdint>
#include <algorithm>
#include "Microsoft/Xna/Framework/Vector4.hpp"
#include "Microsoft/Xna/Framework/Graphics/PackedVector/IPackedVector.hpp"

namespace Microsoft::Xna::Framework::Graphics::PackedVector
{
    /// Packed vector type storing four signed 16-bit integers (XYZW) in a 64-bit value.
    struct Short4 : public IPackedVectorT<uint64_t>
    {
        /// Constructs a Short4 with a packed value of zero.
        Short4() : packedValue_(0) {}
        /// Constructs a Short4 from X, Y, Z, W float values clamped to [-32768, 32767].
        Short4(float x, float y, float z, float w) : packedValue_(Pack(x, y, z, w)) {}
        /// Constructs a Short4 from a Vector4 with components clamped to [-32768, 32767].
        Short4(Vector4 vector) : packedValue_(Pack(vector.X, vector.Y, vector.Z, vector.W)) {}

        /// Gets the packed 64-bit value.
        [[nodiscard]] uint64_t getPackedValueProperty() const override { return packedValue_; }
        /// Sets the packed 64-bit value.
        void setPackedValueProperty(uint64_t v) override { packedValue_ = v; }

        /// Packs the XYZW components of a Vector4 as signed 16-bit integers.
        void PackFromVector4(const Vector4& v) override { packedValue_ = Pack(v.X, v.Y, v.Z, v.W); }
        /// Expands the packed value to a Vector4 with the integers as floats.
        [[nodiscard]] Vector4 ToVector4() const
        {
            return {
                static_cast<int16_t>( packedValue_        & 0xFFFF) * 1.0f,
                static_cast<int16_t>((packedValue_ >> 16) & 0xFFFF) * 1.0f,
                static_cast<int16_t>((packedValue_ >> 32) & 0xFFFF) * 1.0f,
                static_cast<int16_t>((packedValue_ >> 48) & 0xFFFF) * 1.0f
            };
        }

        /// Returns true if both values are equal.
        bool operator==(const Short4& o) const { return packedValue_ == o.packedValue_; }
        /// Returns true if both values are not equal.
        bool operator!=(const Short4& o) const { return !(*this == o); }

    private:
        uint64_t packedValue_;
        static uint64_t Pack(float x, float y, float z, float w) {
            auto xi = static_cast<uint16_t>(static_cast<int16_t>(std::clamp(x,-32768.f,32767.f)));
            auto yi = static_cast<uint16_t>(static_cast<int16_t>(std::clamp(y,-32768.f,32767.f)));
            auto zi = static_cast<uint16_t>(static_cast<int16_t>(std::clamp(z,-32768.f,32767.f)));
            auto wi = static_cast<uint16_t>(static_cast<int16_t>(std::clamp(w,-32768.f,32767.f)));
            return static_cast<uint64_t>(xi) | (static_cast<uint64_t>(yi)<<16) | (static_cast<uint64_t>(zi)<<32) | (static_cast<uint64_t>(wi)<<48);
        }
    };
}
