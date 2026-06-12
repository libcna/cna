// SPDX-License-Identifier: MS-PL
#pragma once
#include <cstdint>
#include <algorithm>
#include "Microsoft/Xna/Framework/Vector4.hpp"
#include "Microsoft/Xna/Framework/Graphics/PackedVector/IPackedVector.hpp"

namespace Microsoft::Xna::Framework::Graphics::PackedVector
{
    struct Byte4 : public IPackedVectorT<uint32_t>
    {
        Byte4() : packedValue_(0) {}
        Byte4(float x, float y, float z, float w) : packedValue_(Pack(x, y, z, w)) {}
        Byte4(Vector4 vector) : packedValue_(Pack(vector.X, vector.Y, vector.Z, vector.W)) {}
        explicit Byte4(uint32_t packed) : packedValue_(packed) {}

        [[nodiscard]] uint32_t getPackedValueProperty() const override { return packedValue_; }
        void setPackedValueProperty(uint32_t v) override { packedValue_ = v; }

        void PackFromVector4(const Vector4& v) override { packedValue_ = Pack(v.X, v.Y, v.Z, v.W); }
        [[nodiscard]] Vector4 ToVector4() const
        {
            return {
                 (packedValue_        & 0xFF) * 1.0f,
                ((packedValue_ >>  8) & 0xFF) * 1.0f,
                ((packedValue_ >> 16) & 0xFF) * 1.0f,
                ((packedValue_ >> 24) & 0xFF) * 1.0f
            };
        }

        bool operator==(const Byte4& o) const { return packedValue_ == o.packedValue_; }
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
