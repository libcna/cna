// SPDX-License-Identifier: MS-PL
#pragma once
#include <cstdint>
#include <algorithm>
#include "Microsoft/Xna/Framework/Vector4.hpp"
#include "Microsoft/Xna/Framework/Graphics/PackedVector/IPackedVector.hpp"

namespace Microsoft::Xna::Framework::Graphics::PackedVector
{
    struct Alpha8 : public IPackedVectorT<uint8_t>
    {
        Alpha8() : packedValue_(0) {}
        explicit Alpha8(float alpha) : packedValue_(Pack(alpha)) {}

        [[nodiscard]] uint8_t getPackedValueProperty() const override { return packedValue_; }
        void setPackedValueProperty(uint8_t v) override { packedValue_ = v; }

        void PackFromVector4(const Vector4& v) override { packedValue_ = Pack(v.W); }
        [[nodiscard]] Vector4 ToVector4() const { return {0.0f, 0.0f, 0.0f, packedValue_ / 255.0f}; }

        [[nodiscard]] float ToAlpha() const { return packedValue_ / 255.0f; }

        bool operator==(const Alpha8& o) const { return packedValue_ == o.packedValue_; }
        bool operator!=(const Alpha8& o) const { return !(*this == o); }

    private:
        uint8_t packedValue_;
        static uint8_t Pack(float v) {
            return static_cast<uint8_t>(std::clamp(v, 0.0f, 1.0f) * 255.0f + 0.5f);
        }
    };
}
