// SPDX-License-Identifier: MS-PL
#pragma once
#include <cstdint>
#include <algorithm>
#include "Microsoft/Xna/Framework/Vector4.hpp"
#include "Microsoft/Xna/Framework/Graphics/PackedVector/IPackedVector.hpp"

namespace Microsoft::Xna::Framework::Graphics::PackedVector
{
    /// Packed vector type storing RGBA as four 16-bit unsigned integers in a 64-bit value.
    struct Rgba64 : public IPackedVectorT<uint64_t>
    {
        /// Constructs a Rgba64 with a packed value of zero.
        Rgba64() : packedValue_(0) {}
        /// Constructs a Rgba64 from normalized red, green, blue, alpha floats in [0, 1].
        Rgba64(float r, float g, float b, float a) : packedValue_(Pack(r, g, b, a)) {}
        /// Constructs a Rgba64 from a Vector4 with components in [0, 1].
        Rgba64(Vector4 vector) : packedValue_(Pack(vector.X, vector.Y, vector.Z, vector.W)) {}

        /// Gets the packed 64-bit value.
        [[nodiscard]] uint64_t getPackedValueProperty() const override { return packedValue_; }
        /// Sets the packed 64-bit value.
        void setPackedValueProperty(uint64_t v) override { packedValue_ = v; }

        /// Packs the XYZW components of a Vector4 as 16-bit unsigned normalized RGBA channels.
        void PackFromVector4(const Vector4& v) override { packedValue_ = Pack(v.X, v.Y, v.Z, v.W); }
        /// Expands the packed value to a normalized Vector4.
        [[nodiscard]] Vector4 ToVector4() const
        {
            return {
                (packedValue_ & 0xFFFF)          / 65535.0f,
                ((packedValue_ >> 16) & 0xFFFF)  / 65535.0f,
                ((packedValue_ >> 32) & 0xFFFF)  / 65535.0f,
                ((packedValue_ >> 48) & 0xFFFF)  / 65535.0f
            };
        }

        /// Returns true if both values are equal.
        bool operator==(const Rgba64& o) const { return packedValue_ == o.packedValue_; }
        /// Returns true if both values are not equal.
        bool operator!=(const Rgba64& o) const { return !(*this == o); }

    private:
        uint64_t packedValue_;
        static uint64_t Pack(float r, float g, float b, float a) {
            auto ri = static_cast<uint64_t>(std::clamp(r, 0.f, 1.f) * 65535.f + 0.5f);
            auto gi = static_cast<uint64_t>(std::clamp(g, 0.f, 1.f) * 65535.f + 0.5f);
            auto bi = static_cast<uint64_t>(std::clamp(b, 0.f, 1.f) * 65535.f + 0.5f);
            auto ai = static_cast<uint64_t>(std::clamp(a, 0.f, 1.f) * 65535.f + 0.5f);
            return ri | (gi << 16) | (bi << 32) | (ai << 48);
        }
    };
}
