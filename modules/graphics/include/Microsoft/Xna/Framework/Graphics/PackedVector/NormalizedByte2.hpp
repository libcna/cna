// SPDX-License-Identifier: MS-PL
#pragma once
#include <cstdint>
#include <algorithm>
#include <cmath>
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Vector4.hpp"
#include "Microsoft/Xna/Framework/Graphics/PackedVector/IPackedVector.hpp"
#include "CNA/Internal/PackedRounding.hpp"

namespace Microsoft::Xna::Framework::Graphics::PackedVector
{
    /**
     * @brief Packed vector type storing two signed normalized bytes (XY) in a 16-bit value.
     */
    struct NormalizedByte2 : public IPackedVectorT<uint16_t>
    {
        /** @brief Constructs a NormalizedByte2 with a packed value of zero. */
        NormalizedByte2() : packedValue_(0) {}

        /**
         * @brief Constructs a NormalizedByte2 from normalized X and Y floats in [-1, 1].
         * @param x The x component in [-1, 1].
         * @param y The y component in [-1, 1].
         */
        NormalizedByte2(float x, float y) : packedValue_(Pack(x, y)) {}

        /**
         * @brief Constructs a NormalizedByte2 from a Vector2 with components in [-1, 1].
         * @param vector Vector containing the XY components.
         */
        NormalizedByte2(Vector2 vector) : packedValue_(Pack(vector.X, vector.Y)) {}

        /**
         * @brief Gets the packed 16-bit value.
         * @return The packed 16-bit value.
         */
        [[nodiscard]] uint16_t getPackedValueProperty() const override { return packedValue_; }

        /**
         * @brief Sets the packed 16-bit value.
         * @param v The new packed 16-bit value.
         */
        void setPackedValueProperty(uint16_t v) override { packedValue_ = v; }

        /**
         * @brief Packs the XY components of a Vector4 as signed normalized bytes.
         * @param v Vector whose XY components are packed.
         */
        void PackFromVector4(const Vector4& v) override { packedValue_ = Pack(v.X, v.Y); }

        /**
         * @brief Expands the packed value to a Vector4 with Z = 0, W = 1.
         * @return The unpacked Vector4.
         */
        [[nodiscard]] Vector4 ToVector4() const
        {
            return {
                static_cast<int8_t>(packedValue_ & 0xFF) / 127.0f,
                static_cast<int8_t>((packedValue_ >> 8) & 0xFF) / 127.0f,
                0.0f, 1.0f
            };
        }

        /**
         * @brief Returns true if both NormalizedByte2 values are equal.
         * @param o The other NormalizedByte2 to compare.
         * @return True if equal.
         */
        bool operator==(const NormalizedByte2& o) const { return packedValue_ == o.packedValue_; }

        /**
         * @brief Returns true if both NormalizedByte2 values are not equal.
         * @param o The other NormalizedByte2 to compare.
         * @return True if not equal.
         */
        bool operator!=(const NormalizedByte2& o) const { return !(*this == o); }

    private:
        uint16_t packedValue_;
        static uint16_t Pack(float x, float y) {
            // XNA saturates the channel and rounds it to the nearest integer with ties to even
        // (.NET Math.Round), where FNA rounds a tie away from zero and a NaN channel reaches an
        // integer cast undefined in C++. Measured on the XNA 4.0 runtime:
        // tests/reference/xna40/framework/framework-packing-oracle.json, cases packed/*/ties,
        // packed/*/negative_ties and packed/*/nan_and_infinities.

            auto xi = static_cast<uint8_t>(static_cast<int8_t>(CNA::Internal::ClampAndRound(std::clamp(x, -1.0f, 1.0f) * 127.0f, -127.0f, 127.0f)));
            auto yi = static_cast<uint8_t>(static_cast<int8_t>(CNA::Internal::ClampAndRound(std::clamp(y, -1.0f, 1.0f) * 127.0f, -127.0f, 127.0f)));
            return static_cast<uint16_t>(xi | (yi << 8));
        }
    };
}
