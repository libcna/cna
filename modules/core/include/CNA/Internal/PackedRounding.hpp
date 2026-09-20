// SPDX-License-Identifier: MS-PL
#pragma once

#include <cmath>
#include <cstdint>

namespace CNA::Internal
{
    /**
     * @brief Rounds a float to the nearest integral value, ties to even.
     *
     * This is the rule the XNA 4.0 framework packs float channels with -- .NET's own
     * `Math.Round(double)`, whose default is banker's rounding. It is not what a `+ 0.5f` then
     * truncate does (that rounds a tie away from zero) and not what `std::lroundf` does (same),
     * and the two differ on every value whose scaled channel lands exactly halfway. Measured on
     * the XNA 4.0 runtime: `tests/reference/xna40/framework/framework-packing-oracle.json`,
     * cases `packed/Byte4/ties` (0.5, 1.5, 2.5, 3.5 pack as 0, 2, 2, 4) and its siblings.
     *
     * `std::nearbyint` obeys the current floating-point rounding mode, which a caller elsewhere
     * in the process is free to change; this is written out so the result cannot depend on it.
     *
     * @param value Value to round. Must be finite.
     * @return The nearest integral value, with an exact tie resolved to the even neighbour.
     */
    [[nodiscard]] inline float RoundHalfToEven(float value)
    {
        const float rounded = std::floor(value + 0.5f);
        if (rounded - value == 0.5f)
        {
            const auto whole = static_cast<long long>(rounded);
            if ((whole & 1LL) != 0LL)
            {
                return rounded - 1.0f;
            }
        }
        return rounded;
    }

    /**
     * @brief Clamps a float to a range and rounds it the way XNA packs a channel.
     *
     * Reproduces, in order, what every float-taking XNA packed-vector constructor and
     * `Color` constructor does to a channel: saturate at the representable range, then round to
     * the nearest integer with ties to even. A NaN channel yields 0, which is what the XNA
     * runtime produces (`color/vector4_nan`, `packed/Byte4/nan_and_infinities` and siblings in
     * `tests/reference/xna40/framework/framework-packing-oracle.json`) and what keeps the
     * caller's cast to an integer type defined -- casting NaN or an out-of-range float to an
     * integer is undefined behaviour in C++, where C# merely leaves the value unspecified.
     *
     * @param value Channel value to convert, already scaled to integer units.
     * @param minimum Lowest representable integer value of the channel.
     * @param maximum Highest representable integer value of the channel.
     * @return The clamped, rounded value, always within [minimum, maximum] and always finite.
     */
    [[nodiscard]] inline float ClampAndRound(float value, float minimum, float maximum)
    {
        if (std::isnan(value))
        {
            return 0.0f;
        }
        if (value <= minimum)
        {
            return minimum;
        }
        if (value >= maximum)
        {
            return maximum;
        }
        return RoundHalfToEven(value);
    }

    /**
     * @brief Expands an unsigned normalized packed channel back to a float in [0, 1].
     *
     * The inverse of the UNORM packing the float-taking packed-vector constructors apply: the
     * channel is masked to its own width and divided by that width's maximum. Kept next to the
     * packing rules above because the two have to agree channel for channel; a packed value and
     * its expansion are one contract, not two.
     *
     * @param bitmask Channel mask, one bit set per channel bit (for example 255 for eight bits).
     * @param value Packed storage, already shifted so the channel occupies the low bits.
     * @return The channel as a float in [0, 1].
     */
    [[nodiscard]] inline float UnpackUNorm(std::uint32_t bitmask, std::uint32_t value)
    {
        value &= bitmask;
        return static_cast<float>(value) / static_cast<float>(bitmask);
    }

    /**
     * @brief Expands a signed normalized packed channel back to a float in [-1, 1].
     *
     * Reproduces the XNA 4.0 runtime's own expansion, including its endpoint rule: the most
     * negative representable code (0x80 for an eight-bit channel, 0x8000 for sixteen) expands to
     * exactly -1, rather than to the -128/127 a plain sign-extended division would give. XNA's
     * SNORM packing clamps at -127 / -32767, so that code is unreachable through packing and only
     * appears when a caller writes `PackedValue` directly -- which is precisely why the rule is
     * written out here rather than left to the division.
     *
     * @param bitmask Channel mask, one bit set per channel bit (for example 255 for eight bits).
     * @param value Packed storage, already shifted so the channel occupies the low bits.
     * @return The channel as a float in [-1, 1].
     */
    [[nodiscard]] inline float UnpackSNorm(std::uint32_t bitmask, std::uint32_t value)
    {
        const std::uint32_t signBit = (bitmask + 1u) >> 1;
        if ((value & signBit) != 0u)
        {
            if ((value & bitmask) == signBit)
            {
                return -1.0f;
            }
            value |= ~bitmask;
        }
        else
        {
            value &= bitmask;
        }
        return static_cast<float>(static_cast<std::int32_t>(value)) / static_cast<float>(bitmask >> 1);
    }
}
