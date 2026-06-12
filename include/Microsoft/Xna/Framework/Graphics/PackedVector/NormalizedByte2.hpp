// SPDX-License-Identifier: MS-PL
#pragma once
#include <cstdint>
#include <algorithm>
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Vector4.hpp"
#include "Microsoft/Xna/Framework/Graphics/PackedVector/IPackedVector.hpp"

namespace Microsoft::Xna::Framework::Graphics::PackedVector
{
    struct NormalizedByte2 : public IPackedVectorT<uint16_t>
    {
        NormalizedByte2() : packedValue_(0) {}
        NormalizedByte2(float x, float y) : packedValue_(Pack(x, y)) {}
        NormalizedByte2(Vector2 vector) : packedValue_(Pack(vector.X, vector.Y)) {}

        [[nodiscard]] uint16_t getPackedValueProperty() const override { return packedValue_; }
        void setPackedValueProperty(uint16_t v) override { packedValue_ = v; }

        void PackFromVector4(const Vector4& v) override { packedValue_ = Pack(v.X, v.Y); }
        [[nodiscard]] Vector4 ToVector4() const
        {
            return {
                static_cast<int8_t>(packedValue_ & 0xFF) / 127.0f,
                static_cast<int8_t>((packedValue_ >> 8) & 0xFF) / 127.0f,
                0.0f, 1.0f
            };
        }

        bool operator==(const NormalizedByte2& o) const { return packedValue_ == o.packedValue_; }
        bool operator!=(const NormalizedByte2& o) const { return !(*this == o); }

    private:
        uint16_t packedValue_;
        static uint16_t Pack(float x, float y) {
            auto xi = static_cast<uint8_t>(static_cast<int8_t>(std::clamp(x, -1.0f, 1.0f) * 127.0f));
            auto yi = static_cast<uint8_t>(static_cast<int8_t>(std::clamp(y, -1.0f, 1.0f) * 127.0f));
            return static_cast<uint16_t>(xi | (yi << 8));
        }
    };
}
