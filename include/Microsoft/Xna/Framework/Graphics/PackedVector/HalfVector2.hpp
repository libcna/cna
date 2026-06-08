#pragma once
#include <cstdint>
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Vector4.hpp"
#include "Microsoft/Xna/Framework/Graphics/PackedVector/IPackedVector.hpp"
#include "Microsoft/Xna/Framework/Graphics/PackedVector/HalfTypeHelper.hpp"

namespace Microsoft::Xna::Framework::Graphics::PackedVector
{
    struct HalfVector2 : public IPackedVectorT<uint32_t>
    {
        HalfVector2() : packedValue_(0) {}
        HalfVector2(float x, float y) : packedValue_(Pack(x, y)) {}
        HalfVector2(Vector2 vector) : packedValue_(Pack(vector.X, vector.Y)) {}

        [[nodiscard]] uint32_t getPackedValueProperty() const override { return packedValue_; }
        void setPackedValueProperty(uint32_t v) override { packedValue_ = v; }

        void PackFromVector4(const Vector4& v) override { packedValue_ = Pack(v.X, v.Y); }
        [[nodiscard]] Vector4 ToVector4() const
        {
            return {
                HalfTypeHelper::Convert(static_cast<uint16_t>(packedValue_ & 0xFFFF)),
                HalfTypeHelper::Convert(static_cast<uint16_t>(packedValue_ >> 16)),
                0.0f, 1.0f
            };
        }
        [[nodiscard]] Vector2 ToVector2() const
        {
            return {
                HalfTypeHelper::Convert(static_cast<uint16_t>(packedValue_ & 0xFFFF)),
                HalfTypeHelper::Convert(static_cast<uint16_t>(packedValue_ >> 16))
            };
        }

        bool operator==(const HalfVector2& o) const { return packedValue_ == o.packedValue_; }
        bool operator!=(const HalfVector2& o) const { return !(*this == o); }

    private:
        uint32_t packedValue_;
        static uint32_t Pack(float x, float y)
        {
            return static_cast<uint32_t>(HalfTypeHelper::Convert(x)) |
                   (static_cast<uint32_t>(HalfTypeHelper::Convert(y)) << 16);
        }
    };
}
