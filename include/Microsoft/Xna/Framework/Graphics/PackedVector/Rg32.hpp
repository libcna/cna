#pragma once
#include <cstdint>
#include <algorithm>
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Vector4.hpp"
#include "Microsoft/Xna/Framework/Graphics/PackedVector/IPackedVector.hpp"

namespace Microsoft::Xna::Framework::Graphics::PackedVector
{
    struct Rg32 : public IPackedVectorT<uint32_t>
    {
        Rg32() : packedValue_(0) {}
        Rg32(float r, float g) : packedValue_(Pack(r, g)) {}
        Rg32(Vector2 vector) : packedValue_(Pack(vector.X, vector.Y)) {}

        [[nodiscard]] uint32_t getPackedValueProperty() const override { return packedValue_; }
        void setPackedValueProperty(uint32_t v) override { packedValue_ = v; }

        void PackFromVector4(const Vector4& v) override { packedValue_ = Pack(v.X, v.Y); }
        [[nodiscard]] Vector4 ToVector4() const
        {
            return {
                (packedValue_ & 0xFFFF) / 65535.0f,
                (packedValue_ >> 16)    / 65535.0f,
                0.0f, 1.0f
            };
        }

        bool operator==(const Rg32& o) const { return packedValue_ == o.packedValue_; }
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
