// SPDX-License-Identifier: MS-PL
#pragma once
#include <cstdint>
#include <algorithm>
#include "Microsoft/Xna/Framework/Vector4.hpp"
#include "Microsoft/Xna/Framework/Graphics/PackedVector/IPackedVector.hpp"

namespace Microsoft::Xna::Framework::Graphics::PackedVector
{
    /// Packed vector type storing four unsigned byte channels in a 32-bit value.
    struct Byte4 : public IPackedVectorT<uint32_t>
    {
        /// Constructs a Byte4 with a packed value of zero.
        Byte4() : packedValue_(0) {}
        /// Constructs a Byte4 from four float values in [0, 255].
        Byte4(float x, float y, float z, float w) : packedValue_(Pack(x, y, z, w)) {}
        /// Constructs a Byte4 from a Vector4 with components in [0, 255].
        Byte4(Vector4 vector) : packedValue_(Pack(vector.X, vector.Y, vector.Z, vector.W)) {}
        /// Constructs a Byte4 from a raw 32-bit packed value.
        explicit Byte4(uint32_t packed) : packedValue_(packed) {}

        /// Gets the packed 32-bit value.
        [[nodiscard]] uint32_t getPackedValueProperty() const override { return packedValue_; }
        /// Sets the packed 32-bit value.
        void setPackedValueProperty(uint32_t v) override { packedValue_ = v; }

        /// Packs the XYZW components of a Vector4 into this Byte4.
        void PackFromVector4(const Vector4& v) override { packedValue_ = Pack(v.X, v.Y, v.Z, v.W); }
        /// Expands the packed value to a Vector4 with each component in [0, 255].
        [[nodiscard]] Vector4 ToVector4() const
        {
            return {
                 (packedValue_        & 0xFF) * 1.0f,
                ((packedValue_ >>  8) & 0xFF) * 1.0f,
                ((packedValue_ >> 16) & 0xFF) * 1.0f,
                ((packedValue_ >> 24) & 0xFF) * 1.0f
            };
        }

        /// Returns true if both values are equal.
        bool operator==(const Byte4& o) const { return packedValue_ == o.packedValue_; }
        /// Returns true if both values are not equal.
        bool operator!=(const Byte4& o) const { return !(*this == o); }

    private:
        uint32_t packedValue_;
        static uint32_t Pack(float x, float y, float z, float w) {
            auto xi = static_cast<uint32_t>(std::clamp(x, 0.0f, 255.0f));
            auto yi = static_cast<uint32_t>(std::clamp(y, 0.0f, 255.0f));
            auto zi = static_cast<uint32_t>(std::clamp(z, 0.0f, 255.0f));
            auto wi = static_cast<uint32_t>(std::clamp(w, 0.0f, 255.0f));
            return xi | (yi << 8) | (zi << 16) | (wi << 24);
        }
    };
}
