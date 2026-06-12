// SPDX-License-Identifier: MS-PL
#pragma once
#include <cstdint>
#include <algorithm>
#include "Microsoft/Xna/Framework/Vector4.hpp"
#include "Microsoft/Xna/Framework/Graphics/PackedVector/IPackedVector.hpp"

namespace Microsoft::Xna::Framework::Graphics::PackedVector
{
    struct Rgba1010102 : public IPackedVectorT<uint32_t>
    {
        Rgba1010102() : packedValue_(0) {}
        Rgba1010102(float r, float g, float b, float a) : packedValue_(Pack(r, g, b, a)) {}
        Rgba1010102(Vector4 vector) : packedValue_(Pack(vector.X, vector.Y, vector.Z, vector.W)) {}

        [[nodiscard]] uint32_t getPackedValueProperty() const override { return packedValue_; }
        void setPackedValueProperty(uint32_t v) override { packedValue_ = v; }

        void PackFromVector4(const Vector4& v) override { packedValue_ = Pack(v.X, v.Y, v.Z, v.W); }
        [[nodiscard]] Vector4 ToVector4() const
        {
            return {
                 (packedValue_        & 0x3FF) / 1023.0f,
                ((packedValue_ >> 10) & 0x3FF) / 1023.0f,
                ((packedValue_ >> 20) & 0x3FF) / 1023.0f,
                ((packedValue_ >> 30) & 0x003) /    3.0f
            };
        }

        bool operator==(const Rgba1010102& o) const { return packedValue_ == o.packedValue_; }
        bool operator!=(const Rgba1010102& o) const { return !(*this == o); }

    private:
        uint32_t packedValue_;
        static uint32_t Pack(float r, float g, float b, float a) {
            auto ri = static_cast<uint32_t>(std::clamp(r, 0.f, 1.f) * 1023.f + 0.5f);
            auto gi = static_cast<uint32_t>(std::clamp(g, 0.f, 1.f) * 1023.f + 0.5f);
            auto bi = static_cast<uint32_t>(std::clamp(b, 0.f, 1.f) * 1023.f + 0.5f);
            auto ai = static_cast<uint32_t>(std::clamp(a, 0.f, 1.f) *    3.f + 0.5f);
            return ri | (gi << 10) | (bi << 20) | (ai << 30);
        }
    };
}
