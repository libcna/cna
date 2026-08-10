// SPDX-License-Identifier: MS-PL
#pragma once
#include <cstdint>
#include <cstring>
#include <bit>

namespace Microsoft::Xna::Framework::Graphics::PackedVector
{
    /**
     * @brief Helper providing conversion between 32-bit float and 16-bit half-precision float.
     */
    struct HalfTypeHelper
    {
        /**
         * @brief Converts a 32-bit float to a 16-bit half-precision float representation.
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
         * @brief Converts a 32-bit signed integer bit pattern to a 16-bit half-precision float.
         * @param i The 32-bit signed integer bit pattern of the source float.
         * @return The 16-bit half-precision representation.
         */
        [[nodiscard]] static uint16_t Convert(int32_t i)
        {
            int32_t s = (i >> 16) & 0x00008000;
            int32_t e = ((i >> 23) & 0x000000ff) - (127 - 15);
            int32_t m = i & 0x007fffff;
            if (e <= 0)
            {
                if (e < -10) return static_cast<uint16_t>(s);
                m = m | 0x00800000;
                int32_t t = 14 - e;
                int32_t a = (1 << (t - 1)) - 1;
                int32_t b = (m >> t) & 1;
                m = (m + a + b) >> t;
                return static_cast<uint16_t>(s | m);
            }
            else if (e == 0xff - (127 - 15))
            {
                if (m == 0) return static_cast<uint16_t>(s | 0x7c00);
                m >>= 13;
                return static_cast<uint16_t>(s | 0x7c00 | m | ((m == 0) ? 1 : 0));
            }
            else
            {
                m = m + 0x00000fff + ((m >> 13) & 1);
                if ((m & 0x00800000) != 0) { m = 0; e += 1; }
                if (e > 30) return static_cast<uint16_t>(s | 0x7c00);
                return static_cast<uint16_t>(s | (e << 10) | (m >> 13));
            }
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
            else if (exp == 31) { bits = sign | 0x7F800000u | (mantissa << 13); }
            else                { bits = sign | ((exp + 127 - 15) << 23) | (mantissa << 13); }
            float result;
            std::memcpy(&result, &bits, 4);
            return result;
        }
    };
}
