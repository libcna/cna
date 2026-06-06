#pragma once
#include <cstdint>
#include <cstring>
#include <bit>

namespace Microsoft::Xna::Framework::Graphics::PackedVector
{
    // Half-precision float (IEEE 754-2008 binary16) helpers.
    // Matches FNA's HalfTypeHelper exactly.
    struct HalfTypeHelper
    {
        [[nodiscard]] static uint16_t Convert(float f)
        {
            uint32_t bits;
            std::memcpy(&bits, &f, 4);
            uint32_t sign     =  (bits >> 16) & 0x8000u;
            uint32_t exp      = ((bits >> 23) & 0xFFu) - 127 + 15;
            uint32_t mantissa =  (bits >> 13) & 0x3FFu;
            if (exp <= 0)      return static_cast<uint16_t>(sign);
            if (exp >= 31)     return static_cast<uint16_t>(sign | 0x7C00u);
            return static_cast<uint16_t>(sign | (exp << 10) | mantissa);
        }

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
