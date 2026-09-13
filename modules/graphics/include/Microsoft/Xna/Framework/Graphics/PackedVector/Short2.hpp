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
     * @brief Packed vector type storing two signed 16-bit integers (XY) in a 32-bit value.
     */
    struct Short2 : public IPackedVectorT<uint32_t>
    {
        /** @brief Constructs a Short2 with a packed value of zero. */
        Short2() : packedValue_(0) {}

        /**
         * @brief Constructs a Short2 from X and Y float values clamped to [-32768, 32767].
         * @param x The x component.
         * @param y The y component.
         */
        Short2(float x, float y) : packedValue_(Pack(x, y)) {}

        /**
         * @brief Constructs a Short2 from a Vector2 with components clamped to [-32768, 32767].
         * @param vector Vector containing the XY components.
         */
        Short2(Vector2 vector) : packedValue_(Pack(vector.X, vector.Y)) {}

        /**
         * @brief Gets the packed 32-bit value.
         * @return The packed 32-bit value.
         */
        [[nodiscard]] uint32_t getPackedValueProperty() const override { return packedValue_; }

        /**
         * @brief Sets the packed 32-bit value.
         * @param v The new packed 32-bit value.
         */
        void setPackedValueProperty(uint32_t v) override { packedValue_ = v; }

        /**
         * @brief Packs the XY components of a Vector4 as signed 16-bit integers.
         * @param v Vector whose XY components are packed.
         */
        void PackFromVector4(const Vector4& v) override { packedValue_ = Pack(v.X, v.Y); }

        /**
         * @brief Expands the packed value to a Vector4 with the integers as floats, Z = 0, W = 1.
         * @return The unpacked Vector4.
         */
        [[nodiscard]] Vector4 ToVector4() const
        {
            return {
                static_cast<int16_t>( packedValue_        & 0xFFFF) * 1.0f,
                static_cast<int16_t>((packedValue_ >> 16) & 0xFFFF) * 1.0f,
                0.0f, 1.0f
            };
        }

        /**
         * @brief Returns true if both Short2 values are equal.
         * @param o The other Short2 to compare.
         * @return True if equal.
         */
        bool operator==(const Short2& o) const { return packedValue_ == o.packedValue_; }

        /**
         * @brief Returns true if both Short2 values are not equal.
         * @param o The other Short2 to compare.
         * @return True if not equal.
         */
        bool operator!=(const Short2& o) const { return !(*this == o); }

    private:
        uint32_t packedValue_;
        static uint32_t Pack(float x, float y) {
            // XNA saturates the channel and rounds it to the nearest integer with ties to even
        // (.NET Math.Round), where FNA rounds a tie away from zero and a NaN channel reaches an
        // integer cast undefined in C++. Measured on the XNA 4.0 runtime:
        // tests/reference/xna40/framework/framework-packing-oracle.json, cases packed/*/ties,
        // packed/*/negative_ties and packed/*/nan_and_infinities.

            auto xi = static_cast<uint16_t>(static_cast<int16_t>(CNA::Internal::ClampAndRound(x, -32768.0f, 32767.0f)));
            auto yi = static_cast<uint16_t>(static_cast<int16_t>(CNA::Internal::ClampAndRound(y, -32768.0f, 32767.0f)));
            return static_cast<uint32_t>(xi) | (static_cast<uint32_t>(yi) << 16);
        }
    };
}
