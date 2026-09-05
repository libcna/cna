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
     * @brief Packed vector type storing four unsigned byte channels in a 32-bit value.
     */
    struct Byte4 : public IPackedVectorT<uint32_t>
    {
        /** @brief Constructs a Byte4 with a packed value of zero. */
        Byte4() : packedValue_(0) {}

        /**
         * @brief Constructs a Byte4 from four float values in [0, 255].
         * @param x The x component in [0, 255].
         * @param y The y component in [0, 255].
         * @param z The z component in [0, 255].
         * @param w The w component in [0, 255].
         */
        Byte4(float x, float y, float z, float w) : packedValue_(Pack(x, y, z, w)) {}

        /**
         * @brief Constructs a Byte4 from a Vector4 with components in [0, 255].
         * @param vector Vector containing the XYZW components.
         */
        Byte4(Vector4 vector) : packedValue_(Pack(vector.X, vector.Y, vector.Z, vector.W)) {}

        /**
         * @brief Constructs a Byte4 from a raw 32-bit packed value.
         * @param packed The raw packed value.
         */
        explicit Byte4(uint32_t packed) : packedValue_(packed) {}

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
         * @brief Packs the XYZW components of a Vector4 into this Byte4.
         * @param v Vector containing the XYZW components in [0, 255].
         */
        void PackFromVector4(const Vector4& v) override { packedValue_ = Pack(v.X, v.Y, v.Z, v.W); }

        /**
         * @brief Expands the packed value to a Vector4 with each component in [0, 255].
         * @return The unpacked Vector4.
         */
        [[nodiscard]] Vector4 ToVector4() const
        {
            return {
                 (packedValue_        & 0xFF) * 1.0f,
                ((packedValue_ >>  8) & 0xFF) * 1.0f,
                ((packedValue_ >> 16) & 0xFF) * 1.0f,
                ((packedValue_ >> 24) & 0xFF) * 1.0f
            };
        }

        /**
         * @brief Returns true if both Byte4 values are equal.
         * @param o The other Byte4 to compare.
         * @return True if equal.
         */
        bool operator==(const Byte4& o) const { return packedValue_ == o.packedValue_; }

        /**
         * @brief Returns true if both Byte4 values are not equal.
         * @param o The other Byte4 to compare.
         * @return True if not equal.
         */
        bool operator!=(const Byte4& o) const { return !(*this == o); }

    private:
        uint32_t packedValue_;
        static uint32_t Pack(float x, float y, float z, float w) {
            // XNA saturates the channel and rounds it to the nearest integer with ties to even
        // (.NET Math.Round), where FNA rounds a tie away from zero and a NaN channel reaches an
        // integer cast undefined in C++. Measured on the XNA 4.0 runtime:
        // tests/reference/xna40/framework/framework-packing-oracle.json, cases packed/*/ties,
        // packed/*/negative_ties and packed/*/nan_and_infinities.
        
            auto xi = static_cast<uint32_t>(CNA::Internal::ClampAndRound(x, 0.0f, 255.0f));
            auto yi = static_cast<uint32_t>(CNA::Internal::ClampAndRound(y, 0.0f, 255.0f));
            auto zi = static_cast<uint32_t>(CNA::Internal::ClampAndRound(z, 0.0f, 255.0f));
            auto wi = static_cast<uint32_t>(CNA::Internal::ClampAndRound(w, 0.0f, 255.0f));
            return xi | (yi << 8) | (zi << 16) | (wi << 24);
        }
    };
}
