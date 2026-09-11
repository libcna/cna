// SPDX-License-Identifier: MS-PL
#pragma once
#include <cstdint>
#include <algorithm>
#include <cmath>
#include "Microsoft/Xna/Framework/Vector4.hpp"
#include "Microsoft/Xna/Framework/Graphics/PackedVector/IPackedVector.hpp"
#include "CNA/Internal/PackedRounding.hpp"

namespace Microsoft::Xna::Framework::Graphics::PackedVector
{
    /**
     * @brief Packed vector type storing four signed normalized bytes (XYZW) in a 32-bit value.
     */
    struct NormalizedByte4 : public IPackedVectorT<uint32_t>
    {
        /** @brief Constructs a NormalizedByte4 with a packed value of zero. */
        NormalizedByte4() : packedValue_(0) {}

        /**
         * @brief Constructs a NormalizedByte4 from normalized XYZW floats in [-1, 1].
         * @param x The x component in [-1, 1].
         * @param y The y component in [-1, 1].
         * @param z The z component in [-1, 1].
         * @param w The w component in [-1, 1].
         */
        NormalizedByte4(float x, float y, float z, float w) : packedValue_(Pack(x, y, z, w)) {}

        /**
         * @brief Constructs a NormalizedByte4 from a Vector4 with components in [-1, 1].
         * @param vector Vector containing the XYZW components.
         */
        NormalizedByte4(Vector4 vector) : packedValue_(Pack(vector.X, vector.Y, vector.Z, vector.W)) {}

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
         * @brief Packs the XYZW components of a Vector4 as signed normalized bytes.
         * @param v The Vector4 to pack.
         */
        void PackFromVector4(const Vector4& v) override { packedValue_ = Pack(v.X, v.Y, v.Z, v.W); }

        /**
         * @brief Expands the packed value to a normalized Vector4.
         * @return The unpacked Vector4.
         */
        [[nodiscard]] Vector4 ToVector4() const
        {
            return {
                static_cast<int8_t>( packedValue_        & 0xFF) / 127.0f,
                static_cast<int8_t>((packedValue_ >>  8) & 0xFF) / 127.0f,
                static_cast<int8_t>((packedValue_ >> 16) & 0xFF) / 127.0f,
                static_cast<int8_t>((packedValue_ >> 24) & 0xFF) / 127.0f
            };
        }

        /**
         * @brief Returns true if both NormalizedByte4 values are equal.
         * @param o The other NormalizedByte4 to compare.
         * @return True if equal.
         */
        bool operator==(const NormalizedByte4& o) const { return packedValue_ == o.packedValue_; }

        /**
         * @brief Returns true if both NormalizedByte4 values are not equal.
         * @param o The other NormalizedByte4 to compare.
         * @return True if not equal.
         */
        bool operator!=(const NormalizedByte4& o) const { return !(*this == o); }

    private:
        uint32_t packedValue_;
        static uint32_t Pack(float x, float y, float z, float w) {
            // XNA saturates the channel and rounds it to the nearest integer with ties to even
        // (.NET Math.Round), where FNA rounds a tie away from zero and a NaN channel reaches an
        // integer cast undefined in C++. Measured on the XNA 4.0 runtime:
        // tests/reference/xna40/framework/framework-packing-oracle.json, cases packed/*/ties,
        // packed/*/negative_ties and packed/*/nan_and_infinities.

            auto xi = static_cast<uint8_t>(static_cast<int8_t>(CNA::Internal::ClampAndRound(std::clamp(x, -1.0f, 1.0f) * 127.0f, -127.0f, 127.0f)));
            auto yi = static_cast<uint8_t>(static_cast<int8_t>(CNA::Internal::ClampAndRound(std::clamp(y, -1.0f, 1.0f) * 127.0f, -127.0f, 127.0f)));
            auto zi = static_cast<uint8_t>(static_cast<int8_t>(CNA::Internal::ClampAndRound(std::clamp(z, -1.0f, 1.0f) * 127.0f, -127.0f, 127.0f)));
            auto wi = static_cast<uint8_t>(static_cast<int8_t>(CNA::Internal::ClampAndRound(std::clamp(w, -1.0f, 1.0f) * 127.0f, -127.0f, 127.0f)));
            return static_cast<uint32_t>(xi) | (static_cast<uint32_t>(yi)<<8) | (static_cast<uint32_t>(zi)<<16) | (static_cast<uint32_t>(wi)<<24);
        }
    };
}
