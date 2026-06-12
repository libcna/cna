// SPDX-License-Identifier: MS-PL
#pragma once
#include <cstdint>
#include <algorithm>
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Vector4.hpp"
#include "Microsoft/Xna/Framework/Graphics/PackedVector/IPackedVector.hpp"

namespace Microsoft::Xna::Framework::Graphics::PackedVector
{
    /// Packed vector type storing red and green channels as two 16-bit unsigned integers in a 32-bit value.
    struct Rg32 : public IPackedVectorT<uint32_t>
    {
        /// Constructs a Rg32 with a packed value of zero.
        Rg32() : packedValue_(0) {}
        /// Constructs a Rg32 from normalized red and green floats in [0, 1].
        Rg32(float r, float g) : packedValue_(Pack(r, g)) {}
        /// Constructs a Rg32 from a Vector2 with components in [0, 1].
        Rg32(Vector2 vector) : packedValue_(Pack(vector.X, vector.Y)) {}

        /// Gets the packed 32-bit value.
        [[nodiscard]] uint32_t getPackedValueProperty() const override { return packedValue_; }
        /// Sets the packed 32-bit value.
        void setPackedValueProperty(uint32_t v) override { packedValue_ = v; }

        /// Packs the XY components of a Vector4 as unsigned 16-bit normalized channels.
        void PackFromVector4(const Vector4& v) override { packedValue_ = Pack(v.X, v.Y); }
        /// Expands the packed value to a Vector4 with Z = 0, W = 1.
        [[nodiscard]] Vector4 ToVector4() const
        {
            return {
                (packedValue_ & 0xFFFF) / 65535.0f,
                (packedValue_ >> 16)    / 65535.0f,
                0.0f, 1.0f
            };
        }

        /// Returns true if both values are equal.
        bool operator==(const Rg32& o) const { return packedValue_ == o.packedValue_; }
        /// Returns true if both values are not equal.
        bool operator!=(const Rg32& o) const { return !(*this == o); }

    private:
        uint32_t packedValue_;
        static uint32_t Pack(float r, float g) {
            auto ri = static_cast<uint32_t>(std::clamp(r, 0.0f, 1.0f) * 65535.0f + 0.5f);
            auto gi = static_cast<uint32_t>(std::clamp(g, 0.0f, 1.0f) * 65535.0f + 0.5f);
            return ri | (gi << 16);
        }
    };
}
