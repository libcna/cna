// SPDX-License-Identifier: MS-PL
#pragma once
#include <cstdint>
#include <algorithm>
#include "Microsoft/Xna/Framework/Vector4.hpp"
#include "Microsoft/Xna/Framework/Graphics/PackedVector/IPackedVector.hpp"

namespace Microsoft::Xna::Framework::Graphics::PackedVector
{
    /// Packed vector type storing BGRA channels in a 16-bit value (5-5-5-1 bits).
    struct Bgra5551 : public IPackedVectorT<uint16_t>
    {
        /// Constructs a Bgra5551 with a packed value of zero.
        Bgra5551() : packedValue_(0) {}
        /// Constructs a Bgra5551 from normalized red, green, blue, alpha floats in [0, 1].
        Bgra5551(float r, float g, float b, float a) : packedValue_(Pack(r, g, b, a)) {}
        /// Constructs a Bgra5551 from a Vector4 representing normalized RGBA color.
        Bgra5551(Vector4 vector) : packedValue_(Pack(vector.X, vector.Y, vector.Z, vector.W)) {}

        /// Gets the packed 16-bit value.
        [[nodiscard]] uint16_t getPackedValueProperty() const override { return packedValue_; }
        /// Sets the packed 16-bit value.
        void setPackedValueProperty(uint16_t v) override { packedValue_ = v; }

        /// Packs the XYZW components of a Vector4 as a Bgra5551 color.
        void PackFromVector4(const Vector4& v) override { packedValue_ = Pack(v.X, v.Y, v.Z, v.W); }
        /// Expands the packed value to a normalized Vector4 RGBA.
        [[nodiscard]] Vector4 ToVector4() const
        {
            return {
                ((packedValue_ >> 10) & 0x1F) / 31.0f,
                ((packedValue_ >>  5) & 0x1F) / 31.0f,
                 (packedValue_        & 0x1F) / 31.0f,
                ((packedValue_ >> 15) & 0x01) ? 1.0f : 0.0f
            };
        }

        /// Returns true if both values are equal.
        bool operator==(const Bgra5551& o) const { return packedValue_ == o.packedValue_; }
        /// Returns true if both values are not equal.
        bool operator!=(const Bgra5551& o) const { return !(*this == o); }

    private:
        uint16_t packedValue_;
        static uint16_t Pack(float r, float g, float b, float a) {
            auto ri = static_cast<uint16_t>(std::clamp(r, 0.0f, 1.0f) * 31.0f + 0.5f);
            auto gi = static_cast<uint16_t>(std::clamp(g, 0.0f, 1.0f) * 31.0f + 0.5f);
            auto bi = static_cast<uint16_t>(std::clamp(b, 0.0f, 1.0f) * 31.0f + 0.5f);
            auto ai = static_cast<uint16_t>(std::clamp(a, 0.0f, 1.0f) + 0.5f);
            return static_cast<uint16_t>((ai << 15) | (ri << 10) | (gi << 5) | bi);
        }
    };
}
