// SPDX-License-Identifier: MS-PL
#pragma once
#include <cstdint>
#include <algorithm>
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Vector4.hpp"
#include "Microsoft/Xna/Framework/Graphics/PackedVector/IPackedVector.hpp"

namespace Microsoft::Xna::Framework::Graphics::PackedVector
{
    struct Short2 : public IPackedVectorT<uint32_t>
    {
        Short2() : packedValue_(0) {}
        Short2(float x, float y) : packedValue_(Pack(x, y)) {}
        Short2(Vector2 vector) : packedValue_(Pack(vector.X, vector.Y)) {}

        [[nodiscard]] uint32_t getPackedValueProperty() const override { return packedValue_; }
        void setPackedValueProperty(uint32_t v) override { packedValue_ = v; }

        void PackFromVector4(const Vector4& v) override { packedValue_ = Pack(v.X, v.Y); }
        [[nodiscard]] Vector4 ToVector4() const
        {
            return {
                static_cast<int16_t>( packedValue_        & 0xFFFF) * 1.0f,
                static_cast<int16_t>((packedValue_ >> 16) & 0xFFFF) * 1.0f,
                0.0f, 1.0f
            };
        }

        bool operator==(const Short2& o) const { return packedValue_ == o.packedValue_; }
        bool operator!=(const Short2& o) const { return !(*this == o); }

    private:
        uint32_t packedValue_;
        static uint32_t Pack(float x, float y) {
            auto xi = static_cast<uint16_t>(static_cast<int16_t>(std::clamp(x,-32768.f,32767.f)));
            auto yi = static_cast<uint16_t>(static_cast<int16_t>(std::clamp(y,-32768.f,32767.f)));
            return static_cast<uint32_t>(xi) | (static_cast<uint32_t>(yi) << 16);
        }
    };
}
