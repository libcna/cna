#pragma once
#include <cstdint>
#include <algorithm>
#include "Microsoft/Xna/Framework/Vector4.hpp"
#include "Microsoft/Xna/Framework/Graphics/PackedVector/IPackedVector.hpp"

namespace Microsoft::Xna::Framework::Graphics::PackedVector
{
    struct NormalizedShort4 : public IPackedVectorT<uint64_t>
    {
        NormalizedShort4() : packedValue_(0) {}
        NormalizedShort4(float x, float y, float z, float w) : packedValue_(Pack(x, y, z, w)) {}
        NormalizedShort4(Vector4 vector) : packedValue_(Pack(vector.X, vector.Y, vector.Z, vector.W)) {}

        [[nodiscard]] uint64_t getPackedValueProperty() const override { return packedValue_; }
        void setPackedValueProperty(uint64_t v) override { packedValue_ = v; }

        void PackFromVector4(const Vector4& v) override { packedValue_ = Pack(v.X, v.Y, v.Z, v.W); }
        [[nodiscard]] Vector4 ToVector4() const
        {
            return {
                static_cast<int16_t>( packedValue_        & 0xFFFF) / 32767.0f,
                static_cast<int16_t>((packedValue_ >> 16) & 0xFFFF) / 32767.0f,
                static_cast<int16_t>((packedValue_ >> 32) & 0xFFFF) / 32767.0f,
                static_cast<int16_t>((packedValue_ >> 48) & 0xFFFF) / 32767.0f
            };
        }

        bool operator==(const NormalizedShort4& o) const { return packedValue_ == o.packedValue_; }
        bool operator!=(const NormalizedShort4& o) const { return !(*this == o); }

    private:
        uint64_t packedValue_;
        static uint64_t Pack(float x, float y, float z, float w) {
            auto xi = static_cast<uint16_t>(static_cast<int16_t>(std::clamp(x,-1.f,1.f)*32767.f));
            auto yi = static_cast<uint16_t>(static_cast<int16_t>(std::clamp(y,-1.f,1.f)*32767.f));
            auto zi = static_cast<uint16_t>(static_cast<int16_t>(std::clamp(z,-1.f,1.f)*32767.f));
            auto wi = static_cast<uint16_t>(static_cast<int16_t>(std::clamp(w,-1.f,1.f)*32767.f));
            return static_cast<uint64_t>(xi) | (static_cast<uint64_t>(yi)<<16) | (static_cast<uint64_t>(zi)<<32) | (static_cast<uint64_t>(wi)<<48);
        }
    };
}
