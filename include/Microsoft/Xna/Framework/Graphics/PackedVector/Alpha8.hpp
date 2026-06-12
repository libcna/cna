// SPDX-License-Identifier: MS-PL
#pragma once
#include <cstdint>
#include <algorithm>
#include "Microsoft/Xna/Framework/Vector4.hpp"
#include "Microsoft/Xna/Framework/Graphics/PackedVector/IPackedVector.hpp"

namespace Microsoft::Xna::Framework::Graphics::PackedVector
{
    /// Packed vector type storing a single alpha value as an 8-bit unsigned integer.
    struct Alpha8 : public IPackedVectorT<uint8_t>
    {
        /// Constructs an Alpha8 with a packed value of zero.
        Alpha8() : packedValue_(0) {}
        /// Constructs an Alpha8 from a normalized alpha float in [0, 1].
        explicit Alpha8(float alpha) : packedValue_(Pack(alpha)) {}

        /// Gets the packed 8-bit alpha value.
        [[nodiscard]] uint8_t getPackedValueProperty() const override { return packedValue_; }
        /// Sets the packed 8-bit alpha value.
        void setPackedValueProperty(uint8_t v) override { packedValue_ = v; }

        /// Packs the W component of a Vector4 as an 8-bit alpha value.
        void PackFromVector4(const Vector4& v) override { packedValue_ = Pack(v.W); }
        /// Expands the packed value to a Vector4 with XYZW = {0, 0, 0, alpha}.
        [[nodiscard]] Vector4 ToVector4() const { return {0.0f, 0.0f, 0.0f, packedValue_ / 255.0f}; }

        /// Returns the alpha as a normalized float in [0, 1].
        [[nodiscard]] float ToAlpha() const { return packedValue_ / 255.0f; }

        /// Returns true if both values are equal.
        bool operator==(const Alpha8& o) const { return packedValue_ == o.packedValue_; }
        /// Returns true if both values are not equal.
        bool operator!=(const Alpha8& o) const { return !(*this == o); }

    private:
        uint8_t packedValue_;
        static uint8_t Pack(float v) {
            return static_cast<uint8_t>(std::clamp(v, 0.0f, 1.0f) * 255.0f + 0.5f);
        }
    };
}
