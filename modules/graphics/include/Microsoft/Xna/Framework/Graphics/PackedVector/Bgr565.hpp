// SPDX-License-Identifier: MS-PL
#pragma once
#include <any>
#include <cstdint>
#include <string>
#include <algorithm>
#include "Microsoft/Xna/Framework/Vector3.hpp"
#include "Microsoft/Xna/Framework/Vector4.hpp"
#include "Microsoft/Xna/Framework/Graphics/PackedVector/IPackedVector.hpp"
#include "CNA/Internal/PackedRounding.hpp"

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
         * @brief Expands the packed representation into a Vector3.
         * @return The three unsigned normalized channels as {R, G, B}.
         */
        [[nodiscard]] Vector3 ToVector3() const
        {
            return {
                CNA::Internal::UnpackUNorm(0x1F, static_cast<std::uint32_t>(packedValue_) >> 11),
                CNA::Internal::UnpackUNorm(0x3F, static_cast<std::uint32_t>(packedValue_) >>  5),
                CNA::Internal::UnpackUNorm(0x1F, static_cast<std::uint32_t>(packedValue_))
            };
        }

        /**
         * @brief Expands the packed value to a Vector4 with alpha set to 1.
         * @return The unpacked Vector4.
         */
        [[nodiscard]] Vector4 ToVector4() const override
        {
            return {
                ((packedValue_ >> 11) & 0x1F) / 31.0f,
                ((packedValue_ >>  5) & 0x3F) / 63.0f,
                 (packedValue_        & 0x1F) / 31.0f,
                1.0f
            };
        }

        /**
         * @brief Returns a string representation of this value.
         * @return A string holding the packed value as 4 uppercase hexadecimal digits.
         */
        [[nodiscard]] std::string ToString() const;

        /**
         * @brief Returns a hash code for this value.
         * @return A hash code derived from the packed value: the packed value itself, which is
         *         what `System.UInt16.GetHashCode()` returns.
         */
        [[nodiscard]] int GetHashCode() const
        {
            return static_cast<int>(packedValue_);
        }

        /**
         * @brief Compares this value with a boxed object for equality.
         *
         * Mirrors the CLR `Equals(object)` contract: an empty object, or an object holding a
         * different type, is unequal; otherwise the two packed values are compared.
         *
         * @param obj The boxed object to compare against.
         * @return @c true if @p obj holds an equal Bgr565; @c false otherwise.
         */
        [[nodiscard]] bool Equals(const std::any& obj) const
        {
            const Bgr565* other = std::any_cast<Bgr565>(&obj);
            return other != nullptr && Equals(*other);
        }

        /**
         * @brief Compares this value with another Bgr565 for equality.
         * @param other The Bgr565 to compare against.
         * @return @c true if both packed values are equal; @c false otherwise.
         */
        [[nodiscard]] bool Equals(const Bgr565& other) const
        {
            return packedValue_ == other.packedValue_;
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
            // XNA saturates the channel and rounds it to the nearest integer with ties to even
            // (.NET Math.Round), where a "+ 0.5f then truncate" rounds a tie away from zero and a
            // NaN channel reaches an integer cast undefined in C++. Measured on the XNA 4.0
            // runtime: tests/reference/xna40/framework/framework-packing-oracle.json, cases
            // packed/*/ties and packed/*/nan_and_infinities.

            auto ri = static_cast<uint16_t>(CNA::Internal::ClampAndRound(std::clamp(r, 0.0f, 1.0f) * 31.0f, 0.0f, 31.0f));
            auto gi = static_cast<uint16_t>(CNA::Internal::ClampAndRound(std::clamp(g, 0.0f, 1.0f) * 63.0f, 0.0f, 63.0f));
            auto bi = static_cast<uint16_t>(CNA::Internal::ClampAndRound(std::clamp(b, 0.0f, 1.0f) * 31.0f, 0.0f, 31.0f));
            return static_cast<uint16_t>((ri << 11) | (gi << 5) | bi);
        }
    };
}
