// SPDX-License-Identifier: MS-PL
#pragma once
#include <cstdint>
#include <algorithm>
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Vector4.hpp"
#include "Microsoft/Xna/Framework/Graphics/PackedVector/IPackedVector.hpp"

namespace Microsoft::Xna::Framework::Graphics::PackedVector
{
    /**
     * @brief Packed vector type storing blue, green, and red channels in a 16-bit value (5-6-5 bits).
     */
    struct Bgr565 : public IPackedVectorT<uint16_t>
    {
        /** @brief Constructs a Bgr565 with a packed value of zero. */
        Bgr565() : packedValue_(0) {}

        /**
         * @brief Constructs a Bgr565 from normalized red, green, and blue floats in [0, 1].
         * @param r The red component in [0, 1].
         * @param g The green component in [0, 1].
         * @param b The blue component in [0, 1].
         */
        Bgr565(float r, float g, float b) : packedValue_(Pack(r, g, b)) {}

        /**
         * @brief Constructs a Bgr565 from a Vector3 representing normalized RGB color.
         * @param vector Vector containing the XYZ (RGB) components.
         */
        Bgr565(Vector3 vector) : packedValue_(Pack(vector.X, vector.Y, vector.Z)) {}

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
         * @brief Packs the XYZ components of a Vector4 as a Bgr565 color.
         * @param v Vector containing the RGB components in XYZ.
         */
        void PackFromVector4(const Vector4& v) override { packedValue_ = Pack(v.X, v.Y, v.Z); }

        /**
         * @brief Expands the packed value to a Vector4 with alpha set to 1.
         * @return The unpacked Vector4.
         */
        [[nodiscard]] Vector4 ToVector4() const
        {
            return {
                ((packedValue_ >> 11) & 0x1F) / 31.0f,
                ((packedValue_ >>  5) & 0x3F) / 63.0f,
                 (packedValue_        & 0x1F) / 31.0f,
                1.0f
            };
        }

        /**
         * @brief Returns true if both Bgr565 values are equal.
         * @param o The other Bgr565 to compare.
         * @return True if equal.
         */
        bool operator==(const Bgr565& o) const { return packedValue_ == o.packedValue_; }

        /**
         * @brief Returns true if both Bgr565 values are not equal.
         * @param o The other Bgr565 to compare.
         * @return True if not equal.
         */
        bool operator!=(const Bgr565& o) const { return !(*this == o); }

    private:
        uint16_t packedValue_;
        static uint16_t Pack(float r, float g, float b) {
            auto ri = static_cast<uint16_t>(std::clamp(r, 0.0f, 1.0f) * 31.0f + 0.5f);
            auto gi = static_cast<uint16_t>(std::clamp(g, 0.0f, 1.0f) * 63.0f + 0.5f);
            auto bi = static_cast<uint16_t>(std::clamp(b, 0.0f, 1.0f) * 31.0f + 0.5f);
            return static_cast<uint16_t>((ri << 11) | (gi << 5) | bi);
        }
    };
}
