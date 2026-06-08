#pragma once
#include <cstdint>
#include <algorithm>
#include "Microsoft/Xna/Framework/Vector4.hpp"
#include "Microsoft/Xna/Framework/Graphics/PackedVector/IPackedVector.hpp"

namespace Microsoft::Xna::Framework::Graphics::PackedVector
{
    struct Bgra4444 : public IPackedVectorT<uint16_t>
    {
        Bgra4444() : packedValue_(0) {}
        Bgra4444(float r, float g, float b, float a) : packedValue_(Pack(r, g, b, a)) {}
        Bgra4444(Vector4 vector) : packedValue_(Pack(vector.X, vector.Y, vector.Z, vector.W)) {}

        [[nodiscard]] uint16_t getPackedValueProperty() const override { return packedValue_; }
        void setPackedValueProperty(uint16_t v) override { packedValue_ = v; }

        void PackFromVector4(const Vector4& v) override { packedValue_ = Pack(v.X, v.Y, v.Z, v.W); }
        [[nodiscard]] Vector4 ToVector4() const
        {
            return {
                ((packedValue_ >>  8) & 0xF) / 15.0f,
                ((packedValue_ >>  4) & 0xF) / 15.0f,
                 (packedValue_        & 0xF) / 15.0f,
                ((packedValue_ >> 12) & 0xF) / 15.0f
            };
        }

        bool operator==(const Bgra4444& o) const { return packedValue_ == o.packedValue_; }
        bool operator!=(const Bgra4444& o) const { return !(*this == o); }

    private:
        uint16_t packedValue_;
        static uint16_t Pack(float r, float g, float b, float a) {
            auto ri = static_cast<uint16_t>(std::clamp(r, 0.0f, 1.0f) * 15.0f + 0.5f);
            auto gi = static_cast<uint16_t>(std::clamp(g, 0.0f, 1.0f) * 15.0f + 0.5f);
            auto bi = static_cast<uint16_t>(std::clamp(b, 0.0f, 1.0f) * 15.0f + 0.5f);
            auto ai = static_cast<uint16_t>(std::clamp(a, 0.0f, 1.0f) * 15.0f + 0.5f);
            return static_cast<uint16_t>((ai << 12) | (ri << 8) | (gi << 4) | bi);
        }
    };
}
