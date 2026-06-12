// SPDX-License-Identifier: MS-PL
#pragma once
#include <cstdint>
#include <algorithm>
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Vector4.hpp"
#include "Microsoft/Xna/Framework/Graphics/PackedVector/IPackedVector.hpp"

namespace Microsoft::Xna::Framework::Graphics::PackedVector
{
    struct Bgr565 : public IPackedVectorT<uint16_t>
    {
        Bgr565() : packedValue_(0) {}
        Bgr565(float r, float g, float b) : packedValue_(Pack(r, g, b)) {}
        Bgr565(Vector3 vector) : packedValue_(Pack(vector.X, vector.Y, vector.Z)) {}

        [[nodiscard]] uint16_t getPackedValueProperty() const override { return packedValue_; }
        void setPackedValueProperty(uint16_t v) override { packedValue_ = v; }

        void PackFromVector4(const Vector4& v) override { packedValue_ = Pack(v.X, v.Y, v.Z); }
        [[nodiscard]] Vector4 ToVector4() const
        {
            return {
                ((packedValue_ >> 11) & 0x1F) / 31.0f,
                ((packedValue_ >>  5) & 0x3F) / 63.0f,
                 (packedValue_        & 0x1F) / 31.0f,
                1.0f
            };
        }

        bool operator==(const Bgr565& o) const { return packedValue_ == o.packedValue_; }
        bool operator!=(const Bgr565& o) const { return !(*this == o); }

    private:
        uint16_t packedValue_;
        static uint16_t Pack(float r, float g, float b) {
            auto ri = static_cast<uint16_t>(std::clamp(r, 0.0f, 1.0f) * 31.0f + 0.5f);
            auto gi = static_cast<uint16_t>(std::clamp(g, 0.0f, 1.0f) * 63.0f + 0.5f);
            auto bi = static_cast<uint16_t>(std::clamp(b, 0.0f, 1.0f) * 31.0f + 0.5f);
            return static_cast<uint16_t>((ri << 11) | (gi << 5) | bi);
        }
    };
}
