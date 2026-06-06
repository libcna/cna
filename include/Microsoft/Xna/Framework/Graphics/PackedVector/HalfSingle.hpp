#pragma once
#include <cstdint>
#include "Microsoft/Xna/Framework/Vector4.hpp"
#include "Microsoft/Xna/Framework/Graphics/PackedVector/IPackedVector.hpp"
#include "Microsoft/Xna/Framework/Graphics/PackedVector/HalfTypeHelper.hpp"

namespace Microsoft::Xna::Framework::Graphics::PackedVector
{
    struct HalfSingle : public IPackedVectorT<uint16_t>
    {
        HalfSingle() : packedValue_(0) {}
        explicit HalfSingle(float single) : packedValue_(HalfTypeHelper::Convert(single)) {}
        explicit HalfSingle(uint16_t packed) : packedValue_(packed) {}

        [[nodiscard]] uint16_t getPackedValueProperty() const override { return packedValue_; }
        void setPackedValueProperty(uint16_t v) override { packedValue_ = v; }

        void PackFromVector4(const Vector4& v) override { packedValue_ = HalfTypeHelper::Convert(v.X); }
        [[nodiscard]] Vector4 ToVector4() const { return {ToSingle(), 0.0f, 0.0f, 1.0f}; }
        [[nodiscard]] float ToSingle() const { return HalfTypeHelper::Convert(packedValue_); }

        bool operator==(const HalfSingle& o) const { return packedValue_ == o.packedValue_; }
        bool operator!=(const HalfSingle& o) const { return !(*this == o); }

    private:
        uint16_t packedValue_;
    };
}
