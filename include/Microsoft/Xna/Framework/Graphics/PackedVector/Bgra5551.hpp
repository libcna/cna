// SPDX-License-Identifier: MS-PL
#pragma once
#include <cstdint>
#include <algorithm>
#include "Microsoft/Xna/Framework/Vector4.hpp"
#include "Microsoft/Xna/Framework/Graphics/PackedVector/IPackedVector.hpp"

namespace Microsoft::Xna::Framework::Graphics::PackedVector
{
    /**
     * @brief Packed vector type storing BGRA channels in a 16-bit value (5-5-5-1 bits).
     */
    struct Bgra5551 : public IPackedVectorT<uint16_t>
    {
        /** @brief Constructs a Bgra5551 with a packed value of zero. */
        Bgra5551() : packedValue_(0) {}

        /**
         * @brief Constructs a Bgra5551 from normalized RGBA floats in [0, 1].
         * @param r The red component in [0, 1].
         * @param g The green component in [0, 1].
         * @param b The blue component in [0, 1].
         * @param a The alpha component (0 or 1).
         */
        Bgra5551(float r, float g, float b, float a) : packedValue_(Pack(r, g, b, a)) {}

        /**
         * @brief Constructs a Bgra5551 from a Vector4 representing normalized RGBA color.
         * @param vector Vector containing the XYZW (RGBA) components.
         */
        Bgra5551(Vector4 vector) : packedValue_(Pack(vector.X, vector.Y, vector.Z, vector.W)) {}

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
         * @brief Packs the XYZW components of a Vector4 as a Bgra5551 color.
         * @param v Vector containing the RGBA components in XYZW.
         */
        void PackFromVector4(const Vector4& v) override { packedValue_ = Pack(v.X, v.Y, v.Z, v.W); }

        /**
         * @brief Expands the packed value to a normalized Vector4 RGBA.
         * @return The unpacked Vector4.
         */
        [[nodiscard]] Vector4 ToVector4() const
        {
            return {
                ((packedValue_ >> 10) & 0x1F) / 31.0f,
                ((packedValue_ >>  5) & 0x1F) / 31.0f,
                 (packedValue_        & 0x1F) / 31.0f,
                ((packedValue_ >> 15) & 0x01) ? 1.0f : 0.0f
            };
        }

        /**
         * @brief Returns true if both Bgra5551 values are equal.
         * @param o The other Bgra5551 to compare.
         * @return True if equal.
         */
        bool operator==(const Bgra5551& o) const { return packedValue_ == o.packedValue_; }

        /**
         * @brief Returns true if both Bgra5551 values are not equal.
         * @param o The other Bgra5551 to compare.
         * @return True if not equal.
         */
        bool operator!=(const Bgra5551& o) const { return !(*this == o); }

    private:
        uint16_t packedValue_;
        static uint16_t Pack(float r, float g, float b, float a) {
            auto ri = static_cast<uint16_t>(std::clamp(r, 0.0f, 1.0f) * 31.0f + 0.5f);
            auto gi = static_cast<uint16_t>(std::clamp(g, 0.0f, 1.0f) * 31.0f + 0.5f);
            auto bi = static_cast<uint16_t>(std::clamp(b, 0.0f, 1.0f) * 31.0f + 0.5f);
            auto ai = static_cast<uint16_t>(std::clamp(a, 0.0f, 1.0f) + 0.5f);
            return static_cast<uint16_t>((ai << 15) | (ri << 10) | (gi << 5) | bi);
        }
    };
}
