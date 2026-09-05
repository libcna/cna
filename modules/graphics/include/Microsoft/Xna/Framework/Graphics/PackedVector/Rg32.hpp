// SPDX-License-Identifier: MS-PL
#pragma once
#include <cstdint>
#include <algorithm>
#include "Microsoft/Xna/Framework/Vector2.hpp"
#include "Microsoft/Xna/Framework/Vector4.hpp"
#include "Microsoft/Xna/Framework/Graphics/PackedVector/IPackedVector.hpp"
#include "CNA/Internal/PackedRounding.hpp"

namespace Microsoft::Xna::Framework::Graphics::PackedVector
{
    /**
     * @brief Packed vector type storing red and green channels as two 16-bit unsigned integers in a 32-bit value.
     */
    struct Rg32 : public IPackedVectorT<uint32_t>
    {
        /** @brief Constructs a Rg32 with a packed value of zero. */
        Rg32() : packedValue_(0) {}

        /**
         * @brief Constructs a Rg32 from normalized red and green floats in [0, 1].
         * @param r The red component in [0, 1].
         * @param g The green component in [0, 1].
         */
        Rg32(float r, float g) : packedValue_(Pack(r, g)) {}

        /**
         * @brief Constructs a Rg32 from a Vector2 with components in [0, 1].
         * @param vector Vector containing the RG components.
         */
        Rg32(Vector2 vector) : packedValue_(Pack(vector.X, vector.Y)) {}

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
         * @brief Packs the XY components of a Vector4 as unsigned 16-bit normalized channels.
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
                (packedValue_ & 0xFFFF) / 65535.0f,
                (packedValue_ >> 16)    / 65535.0f,
                0.0f, 1.0f
            };
        }

        /**
         * @brief Returns true if both Rg32 values are equal.
         * @param o The other Rg32 to compare.
         * @return True if equal.
         */
        bool operator==(const Rg32& o) const { return packedValue_ == o.packedValue_; }

        /**
         * @brief Returns true if both Rg32 values are not equal.
         * @param o The other Rg32 to compare.
         * @return True if not equal.
         */
        bool operator!=(const Rg32& o) const { return !(*this == o); }

    private:
        uint32_t packedValue_;
        static uint32_t Pack(float r, float g) {
            // XNA saturates the channel and rounds it to the nearest integer with ties to even
            // (.NET Math.Round), where a "+ 0.5f then truncate" rounds a tie away from zero and a
            // NaN channel reaches an integer cast undefined in C++. Measured on the XNA 4.0
            // runtime: tests/reference/xna40/framework/framework-packing-oracle.json, cases
            // packed/*/ties and packed/*/nan_and_infinities.

            auto ri = static_cast<uint32_t>(CNA::Internal::ClampAndRound(std::clamp(r, 0.0f, 1.0f) * 65535.0f, 0.0f, 65535.0f));
            auto gi = static_cast<uint32_t>(CNA::Internal::ClampAndRound(std::clamp(g, 0.0f, 1.0f) * 65535.0f, 0.0f, 65535.0f));
            return ri | (gi << 16);
        }
    };
}
