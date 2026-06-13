// SPDX-License-Identifier: MS-PL
#pragma once
#include <cstdint>
#include "Microsoft/Xna/Framework/Vector4.hpp"
#include "Microsoft/Xna/Framework/Graphics/PackedVector/IPackedVector.hpp"
#include "Microsoft/Xna/Framework/Graphics/PackedVector/HalfTypeHelper.hpp"

namespace Microsoft::Xna::Framework::Graphics::PackedVector
{
    /**
     * @brief Packed vector type storing a single float value as a 16-bit half-precision float.
     */
    struct HalfSingle : public IPackedVectorT<uint16_t>
    {
        /** @brief Constructs a HalfSingle with a packed value of zero. */
        HalfSingle() : packedValue_(0) {}

        /**
         * @brief Constructs a HalfSingle from a 32-bit float value.
         * @param single The 32-bit float to convert.
         */
        explicit HalfSingle(float single) : packedValue_(HalfTypeHelper::Convert(single)) {}

        /**
         * @brief Constructs a HalfSingle from a raw 16-bit packed value.
         * @param packed The raw 16-bit half-precision value.
         */
        explicit HalfSingle(uint16_t packed) : packedValue_(packed) {}

        /**
         * @brief Gets the packed 16-bit half-precision value.
         * @return The packed 16-bit value.
         */
        [[nodiscard]] uint16_t getPackedValueProperty() const override { return packedValue_; }

        /**
         * @brief Sets the packed 16-bit half-precision value.
         * @param v The new packed 16-bit value.
         */
        void setPackedValueProperty(uint16_t v) override { packedValue_ = v; }

        /**
         * @brief Packs the X component of a Vector4 as a half-precision float.
         * @param v Vector whose X component is packed.
         */
        void PackFromVector4(const Vector4& v) override { packedValue_ = HalfTypeHelper::Convert(v.X); }

        /**
         * @brief Expands the packed value to a Vector4 with X = value, Y = 0, Z = 0, W = 1.
         * @return The unpacked Vector4.
         */
        [[nodiscard]] Vector4 ToVector4() const { return {ToSingle(), 0.0f, 0.0f, 1.0f}; }

        /**
         * @brief Converts the packed half-precision value back to a 32-bit float.
         * @return The 32-bit float.
         */
        [[nodiscard]] float ToSingle() const { return HalfTypeHelper::Convert(packedValue_); }

        /**
         * @brief Returns true if both HalfSingle values are equal.
         * @param o The other HalfSingle to compare.
         * @return True if equal.
         */
        bool operator==(const HalfSingle& o) const { return packedValue_ == o.packedValue_; }

        /**
         * @brief Returns true if both HalfSingle values are not equal.
         * @param o The other HalfSingle to compare.
         * @return True if not equal.
         */
        bool operator!=(const HalfSingle& o) const { return !(*this == o); }

    private:
        uint16_t packedValue_;
    };
}
