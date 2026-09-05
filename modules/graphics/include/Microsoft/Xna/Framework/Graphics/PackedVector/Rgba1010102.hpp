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
     * @brief Packed vector type storing RGBA channels as 10-10-10-2 bits in a 32-bit value.
     */
    struct Rgba1010102 : public IPackedVectorT<uint32_t>
    {
        /** @brief Constructs a Rgba1010102 with a packed value of zero. */
        Rgba1010102() : packedValue_(0) {}

        /**
         * @brief Constructs a Rgba1010102 from normalized RGBA floats in [0, 1].
         * @param r The red component in [0, 1].
         * @param g The green component in [0, 1].
         * @param b The blue component in [0, 1].
         * @param a The alpha component in [0, 1].
         */
        Rgba1010102(float r, float g, float b, float a) : packedValue_(Pack(r, g, b, a)) {}

        /**
         * @brief Constructs a Rgba1010102 from a Vector4 with components in [0, 1].
         * @param vector Vector containing the XYZW (RGBA) components.
         */
        Rgba1010102(Vector4 vector) : packedValue_(Pack(vector.X, vector.Y, vector.Z, vector.W)) {}

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
         * @brief Packs the XYZW components of a Vector4 as 10-10-10-2 RGBA channels.
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
                 (packedValue_        & 0x3FF) / 1023.0f,
                ((packedValue_ >> 10) & 0x3FF) / 1023.0f,
                ((packedValue_ >> 20) & 0x3FF) / 1023.0f,
                ((packedValue_ >> 30) & 0x003) /    3.0f
            };
        }

        /**
         * @brief Returns true if both Rgba1010102 values are equal.
         * @param o The other Rgba1010102 to compare.
         * @return True if equal.
         */
        bool operator==(const Rgba1010102& o) const { return packedValue_ == o.packedValue_; }

        /**
         * @brief Returns true if both Rgba1010102 values are not equal.
         * @param o The other Rgba1010102 to compare.
         * @return True if not equal.
         */
        bool operator!=(const Rgba1010102& o) const { return !(*this == o); }

    private:
        uint32_t packedValue_;
        static uint32_t Pack(float r, float g, float b, float a) {
            // XNA saturates the channel and rounds it to the nearest integer with ties to even
            // (.NET Math.Round), where a "+ 0.5f then truncate" rounds a tie away from zero and a
            // NaN channel reaches an integer cast undefined in C++. Measured on the XNA 4.0
            // runtime: tests/reference/xna40/framework/framework-packing-oracle.json, cases
            // packed/*/ties and packed/*/nan_and_infinities.

            auto ri = static_cast<uint32_t>(CNA::Internal::ClampAndRound(std::clamp(r, 0.0f, 1.0f) * 1023.0f, 0.0f, 1023.0f));
            auto gi = static_cast<uint32_t>(CNA::Internal::ClampAndRound(std::clamp(g, 0.0f, 1.0f) * 1023.0f, 0.0f, 1023.0f));
            auto bi = static_cast<uint32_t>(CNA::Internal::ClampAndRound(std::clamp(b, 0.0f, 1.0f) * 1023.0f, 0.0f, 1023.0f));
            auto ai = static_cast<uint32_t>(CNA::Internal::ClampAndRound(std::clamp(a, 0.0f, 1.0f) * 3.0f, 0.0f, 3.0f));
            return ri | (gi << 10) | (bi << 20) | (ai << 30);
        }
    };
}
