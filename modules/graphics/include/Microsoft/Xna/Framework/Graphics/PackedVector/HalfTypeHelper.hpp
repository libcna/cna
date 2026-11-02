// SPDX-License-Identifier: MS-PL
#pragma once
#include <cstdint>
#include <cstring>
#include <bit>

namespace Microsoft::Xna::Framework::Graphics::PackedVector
{
    /**
     * @brief Helper converting Single values and XNA's 16-bit packed half representation.
     */
    struct HalfTypeHelper
    {
        /**
         * @brief Converts a 32-bit float to XNA's 16-bit packed half representation.
         * @param f The 32-bit float to convert.
         * @return The 16-bit half-precision representation.
         */
        [[nodiscard]] static uint16_t Convert(float f)
        {
            int32_t i;
            std::memcpy(&i, &f, 4);
            return Convert(i);
        }

        /**
         * @brief Converts a Single bit pattern to XNA's 16-bit packed half representation.
         * @param i The 32-bit signed integer bit pattern of the source float.
         * @return The 16-bit half-precision representation.
         */
        [[nodiscard]] static uint16_t Convert(int32_t i)
        {
            // Microsoft.Xna.Framework 4.0 HalfUtils.Pack IL_001b..IL_008e:
            // XNA uses exponent 31 for finite values and saturates to sign|7fff.
            const uint32_t bits = static_cast<uint32_t>(i);
            const uint32_t sign = (bits & 0x80000000u) >> 16;
            uint32_t magnitude = bits & 0x7fffffffu;
            if (magnitude > 0x47ffefffu)
                return static_cast<uint16_t>(sign | 0x7fffu);
            if (magnitude < 0x38800000u)
            {
                const uint32_t mantissa = (magnitude & 0x007fffffu) | 0x00800000u;
                const int32_t shift = 113 - static_cast<int32_t>(magnitude >> 23);
                magnitude = shift > 31 ? 0u : mantissa >> shift;
                return static_cast<uint16_t>(sign | ((magnitude + 0xfffu + ((magnitude >> 13) & 1u)) >> 13));
            }
            return static_cast<uint16_t>(sign | ((magnitude + 0xc8000000u + 0xfffu +
                                                   ((magnitude >> 13) & 1u)) >> 13));
        }

        /**
         * @brief Converts a 16-bit half-precision float to a 32-bit float.
         * @param h The 16-bit half-precision value to convert.
         * @return The 32-bit float result.
         */
        [[nodiscard]] static float Convert(uint16_t h)
        {
            uint32_t sign     = (static_cast<uint32_t>(h) & 0x8000u) << 16;
            uint32_t exp      = (static_cast<uint32_t>(h) & 0x7C00u) >> 10;
            uint32_t mantissa = (static_cast<uint32_t>(h) & 0x03FFu);
            uint32_t bits;
            if (exp == 0)
            {
                if (mantissa == 0) { bits = sign; }
                else
                {
                    exp = 1;
                    while (!(mantissa & 0x400u)) { mantissa <<= 1; --exp; }
                    mantissa &= 0x3FFu;
                    bits = sign | ((exp + 127 - 15) << 23) | (mantissa << 13);
                }
            }
            // HalfUtils.Unpack IL_005f..IL_008a applies the same finite exponent
            // rebias to exponent 31; these codes do not denote IEEE NaN/infinity.
            else { bits = sign | ((exp + 127 - 15) << 23) | (mantissa << 13); }
            float result;
            std::memcpy(&result, &bits, 4);
            return result;
        }
    };
}
