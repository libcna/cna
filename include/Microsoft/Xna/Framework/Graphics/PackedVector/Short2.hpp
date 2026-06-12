// SPDX-License-Identifier: MS-PL
#pragma once
#include <cstdint>
#include <algorithm>
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Vector4.hpp"
#include "Microsoft/Xna/Framework/Graphics/PackedVector/IPackedVector.hpp"

namespace Microsoft::Xna::Framework::Graphics::PackedVector
{
    /// Packed vector type storing two signed 16-bit integers (XY) in a 32-bit value.
    struct Short2 : public IPackedVectorT<uint32_t>
    {
        /// Constructs a Short2 with a packed value of zero.
        Short2() : packedValue_(0) {}
        /// Constructs a Short2 from X and Y float values clamped to [-32768, 32767].
        Short2(float x, float y) : packedValue_(Pack(x, y)) {}
        /// Constructs a Short2 from a Vector2 with components clamped to [-32768, 32767].
        Short2(Vector2 vector) : packedValue_(Pack(vector.X, vector.Y)) {}

        /// Gets the packed 32-bit value.
        [[nodiscard]] uint32_t getPackedValueProperty() const override { return packedValue_; }
        /// Sets the packed 32-bit value.
        void setPackedValueProperty(uint32_t v) override { packedValue_ = v; }

        /// Packs the XY components of a Vector4 as signed 16-bit integers.
        void PackFromVector4(const Vector4& v) override { packedValue_ = Pack(v.X, v.Y); }
        /// Expands the packed value to a Vector4 with the integers as floats, Z = 0, W = 1.
        [[nodiscard]] Vector4 ToVector4() const
        {
            return {
                static_cast<int16_t>( packedValue_        & 0xFFFF) * 1.0f,
                static_cast<int16_t>((packedValue_ >> 16) & 0xFFFF) * 1.0f,
                0.0f, 1.0f
            };
        }

        /// Returns true if both values are equal.
        bool operator==(const Short2& o) const { return packedValue_ == o.packedValue_; }
        /// Returns true if both values are not equal.
        bool operator!=(const Short2& o) const { return !(*this == o); }

    private:
        uint32_t packedValue_;
        static uint32_t Pack(float x, float y) {
            auto xi = static_cast<uint16_t>(static_cast<int16_t>(std::clamp(x,-32768.f,32767.f)));
            auto yi = static_cast<uint16_t>(static_cast<int16_t>(std::clamp(y,-32768.f,32767.f)));
            return static_cast<uint32_t>(xi) | (static_cast<uint32_t>(yi) << 16);
        }
    };
}
