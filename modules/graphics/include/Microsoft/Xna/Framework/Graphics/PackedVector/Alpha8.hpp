// SPDX-License-Identifier: MS-PL
#pragma once
#include <cstdint>
#include <algorithm>
#include "Microsoft/Xna/Framework/Vector4.hpp"
#include "Microsoft/Xna/Framework/Graphics/PackedVector/IPackedVector.hpp"
#include "CNA/Internal/PackedRounding.hpp"

namespace Microsoft::Xna::Framework::Graphics::PackedVector
{
    /**
     * @brief Packed vector type containing a single alpha value as an 8-bit unsigned integer.
     */
    struct Alpha8 : public IPackedVectorT<uint8_t>
    {
        /** @brief Constructs an Alpha8 with a packed value of zero. */
        Alpha8() : packedValue_(0) {}

        /**
         * @brief Constructs an Alpha8 from a normalized alpha float in [0, 1].
         * @param alpha The alpha component in the range [0, 1].
         */
        explicit Alpha8(float alpha) : packedValue_(Pack(alpha)) {}

        /**
         * @brief Gets the packed 8-bit alpha value.
         * @return The packed byte value.
         */
        [[nodiscard]] uint8_t getPackedValueProperty() const override { return packedValue_; }

        /**
         * @brief Sets the packed 8-bit alpha value.
         * @param v The new packed byte value.
         */
        void setPackedValueProperty(uint8_t v) override { packedValue_ = v; }

        /**
         * @brief Packs the W component of a Vector4 as an 8-bit alpha value.
         * @param v Vector containing the alpha in the W component.
         */
        void PackFromVector4(const Vector4& v) override { packedValue_ = Pack(v.W); }

        /**
         * @brief Expands the packed value to a Vector4 with components {0, 0, 0, alpha}.
         * @return The unpacked Vector4.
         */
        [[nodiscard]] Vector4 ToVector4() const { return {0.0f, 0.0f, 0.0f, packedValue_ / 255.0f}; }

        /**
         * @brief Returns the alpha as a normalized float in [0, 1].
         * @return The alpha component.
         */
        [[nodiscard]] float ToAlpha() const { return packedValue_ / 255.0f; }

        /**
         * @brief Returns true if both Alpha8 values are equal.
         * @param o The other Alpha8 to compare.
         * @return True if equal.
         */
        bool operator==(const Alpha8& o) const { return packedValue_ == o.packedValue_; }

        /**
         * @brief Returns true if both Alpha8 values are not equal.
         * @param o The other Alpha8 to compare.
         * @return True if not equal.
         */
        bool operator!=(const Alpha8& o) const { return !(*this == o); }

    private:
        uint8_t packedValue_;
        static uint8_t Pack(float v) {
            // XNA saturates the channel and rounds it to the nearest integer with ties to even
            // (.NET Math.Round), where a "+ 0.5f then truncate" rounds a tie away from zero and a
            // NaN channel reaches an integer cast undefined in C++. Measured on the XNA 4.0
            // runtime: tests/reference/xna40/framework/framework-packing-oracle.json, cases
            // packed/*/ties and packed/*/nan_and_infinities.

            return static_cast<uint8_t>(CNA::Internal::ClampAndRound(std::clamp(v, 0.0f, 1.0f) * 255.0f, 0.0f, 255.0f));
        }
    };
}
