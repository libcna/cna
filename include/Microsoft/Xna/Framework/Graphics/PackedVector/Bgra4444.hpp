// SPDX-License-Identifier: MS-PL
#pragma once
#include <cstdint>
#include <algorithm>
#include "Microsoft/Xna/Framework/Vector4.hpp"
#include "Microsoft/Xna/Framework/Graphics/PackedVector/IPackedVector.hpp"

namespace Microsoft::Xna::Framework::Graphics::PackedVector
{
    /**
     * @brief Packed vector type storing BGRA channels in a 16-bit value (4 bits each).
     */
    struct Bgra4444 : public IPackedVectorT<uint16_t>
    {
        /** @brief Constructs a Bgra4444 with a packed value of zero. */
        Bgra4444() : packedValue_(0) {}

        /**
         * @brief Constructs a Bgra4444 from normalized RGBA floats in [0, 1].
         * @param r The red component in [0, 1].
         * @param g The green component in [0, 1].
         * @param b The blue component in [0, 1].
         * @param a The alpha component in [0, 1].
         */
        Bgra4444(float r, float g, float b, float a) : packedValue_(Pack(r, g, b, a)) {}

        /**
         * @brief Constructs a Bgra4444 from a Vector4 representing normalized RGBA color.
         * @param vector Vector containing the XYZW (RGBA) components.
         */
        Bgra4444(Vector4 vector) : packedValue_(Pack(vector.X, vector.Y, vector.Z, vector.W)) {}

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
         * @brief Packs the XYZW components of a Vector4 as a Bgra4444 color.
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
                ((packedValue_ >>  8) & 0xF) / 15.0f,
                ((packedValue_ >>  4) & 0xF) / 15.0f,
                 (packedValue_        & 0xF) / 15.0f,
                ((packedValue_ >> 12) & 0xF) / 15.0f
            };
        }

        /**
         * @brief Returns true if both Bgra4444 values are equal.
         * @param o The other Bgra4444 to compare.
         * @return True if equal.
         */
        bool operator==(const Bgra4444& o) const { return packedValue_ == o.packedValue_; }

        /**
         * @brief Returns true if both Bgra4444 values are not equal.
         * @param o The other Bgra4444 to compare.
         * @return True if not equal.
         */
        bool operator!=(const Bgra4444& o) const { return !(*this == o); }

    private:
        uint16_t packedValue_;
        static uint16_t Pack(float r, float g, float b, float a) {
            auto ri = static_cast<uint16_t>(std::clamp(r, 0.0f, 1.0f) * 15.0f + 0.5f);
            auto gi = static_cast<uint16_t>(std::clamp(g, 0.0f, 1.0f) * 15.0f + 0.5f);
            auto bi = static_cast<uint16_t>(std::clamp(b, 0.0f, 1.0f) * 15.0f + 0.5f);
            auto ai = static_cast<uint16_t>(std::clamp(a, 0.0f, 1.0f) * 15.0f + 0.5f);
            return static_cast<uint16_t>((ai << 12) | (ri << 8) | (gi << 4) | bi);
        }
    };
}
