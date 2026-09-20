// SPDX-License-Identifier: MS-PL
#pragma once
#include <any>
#include <cstdint>
#include <string>
#include <algorithm>
#include "Microsoft/Xna/Framework/Vector4.hpp"
#include "Microsoft/Xna/Framework/Graphics/PackedVector/IPackedVector.hpp"
#include "CNA/Internal/PackedRounding.hpp"

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
        [[nodiscard]] Vector4 ToVector4() const override
        {
            return {
                ((packedValue_ >> 10) & 0x1F) / 31.0f,
                ((packedValue_ >>  5) & 0x1F) / 31.0f,
                 (packedValue_        & 0x1F) / 31.0f,
                ((packedValue_ >> 15) & 0x01) ? 1.0f : 0.0f
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
         * @return @c true if @p obj holds an equal Bgra5551; @c false otherwise.
         */
        [[nodiscard]] bool Equals(const std::any& obj) const
        {
            const Bgra5551* other = std::any_cast<Bgra5551>(&obj);
            return other != nullptr && Equals(*other);
        }

        /**
         * @brief Compares this value with another Bgra5551 for equality.
         * @param other The Bgra5551 to compare against.
         * @return @c true if both packed values are equal; @c false otherwise.
         */
        [[nodiscard]] bool Equals(const Bgra5551& other) const
        {
            return packedValue_ == other.packedValue_;
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
            // XNA saturates the channel and rounds it to the nearest integer with ties to even
            // (.NET Math.Round), where a "+ 0.5f then truncate" rounds a tie away from zero and a
            // NaN channel reaches an integer cast undefined in C++. Measured on the XNA 4.0
            // runtime: tests/reference/xna40/framework/framework-packing-oracle.json, cases
            // packed/*/ties and packed/*/nan_and_infinities.

            auto ri = static_cast<uint16_t>(CNA::Internal::ClampAndRound(std::clamp(r, 0.0f, 1.0f) * 31.0f, 0.0f, 31.0f));
            auto gi = static_cast<uint16_t>(CNA::Internal::ClampAndRound(std::clamp(g, 0.0f, 1.0f) * 31.0f, 0.0f, 31.0f));
            auto bi = static_cast<uint16_t>(CNA::Internal::ClampAndRound(std::clamp(b, 0.0f, 1.0f) * 31.0f, 0.0f, 31.0f));
            auto ai = static_cast<uint16_t>(CNA::Internal::ClampAndRound(std::clamp(a, 0.0f, 1.0f), 0.0f, 1.0f));
            return static_cast<uint16_t>((ai << 15) | (ri << 10) | (gi << 5) | bi);
        }
    };
}
