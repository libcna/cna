// SPDX-License-Identifier: MS-PL
#pragma once

#include <cmath>

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
}
